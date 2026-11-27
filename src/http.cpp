// SPDX-License-Identifier: MIT
#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "system.h"

static std::atomic<bool> g_stop{false};
static std::atomic<int> g_listen_fd{-1};
static Handler g_handler;

void Stop() {
  g_stop = true;
  int fd = g_listen_fd.exchange(-1);
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
  }
}

unsigned short FindFreePort(unsigned short start) {
  for (unsigned p = start; p < start + 50 && p <= 65535; p++) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)p);
    bool ok = bind(fd, (sockaddr*)&a, sizeof a) == 0;
    close(fd);
    if (ok) return (unsigned short)p;
  }
  return 0;
}

std::string LanAddress() {
  // Connecting a UDP socket sends nothing; it only picks the outgoing
  // interface, whose address getsockname() then reports.
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return "";
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
  std::string out;
  sockaddr_in me{};
  socklen_t len = sizeof me;
  if (connect(fd, (sockaddr*)&a, sizeof a) == 0 && getsockname(fd, (sockaddr*)&me, &len) == 0 &&
      me.sin_addr.s_addr != htonl(INADDR_ANY)) {
    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf)) out = buf;
  }
  close(fd);
  return out;
}

bool IsLoopback(const std::string& ip) { return ip.empty() || ip.compare(0, 4, "127.") == 0; }

static int Hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static std::string UrlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size() && Hex(s[i + 1]) >= 0 && Hex(s[i + 2]) >= 0) {
      out += (char)(Hex(s[i + 1]) * 16 + Hex(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

static void ParseQuery(const std::string& q, std::map<std::string, std::string>& out) {
  size_t pos = 0;
  while (pos < q.size()) {
    size_t amp = q.find('&', pos);
    if (amp == std::string::npos) amp = q.size();
    std::string kv = q.substr(pos, amp - pos);
    size_t eq = kv.find('=');
    if (!kv.empty())
      out[UrlDecode(kv.substr(0, eq))] = eq == std::string::npos ? "" : UrlDecode(kv.substr(eq + 1));
    pos = amp + 1;
  }
}

static bool SendAll(int fd, const void* data, size_t size) {
  const char* p = (const char*)data;
  while (size) {
    ssize_t n = send(fd, p, size, 0);
    if (n <= 0) return false;
    p += n;
    size -= (size_t)n;
  }
  return true;
}

static const char* Reason(int s) {
  switch (s) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    default: return "Error";
  }
}

static void* Connection(void* arg) {
  int fd = (int)(intptr_t)arg;
  timeval tv{15, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);

  std::string buf;
  char chunk[4096];
  size_t header_end = std::string::npos;
  while (header_end == std::string::npos && buf.size() < 32768) {
    ssize_t n = recv(fd, chunk, sizeof chunk, 0);
    if (n <= 0) break;
    buf.append(chunk, (size_t)n);
    header_end = buf.find("\r\n\r\n");
  }

  Request req;
  Response res;
  sockaddr_in local{};
  socklen_t local_len = sizeof local;
  char ip[INET_ADDRSTRLEN];
  if (getsockname(fd, (sockaddr*)&local, &local_len) == 0 && local.sin_family == AF_INET &&
      inet_ntop(AF_INET, &local.sin_addr, ip, sizeof ip))
    req.local_ip = ip;
  if (header_end == std::string::npos) {
    close(fd);
    return nullptr;
  }
  std::string head = buf.substr(0, header_end);
  size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    res.status = 400;
  } else {
    req.method = head.substr(0, sp1);
    std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t qm = target.find('?');
    req.path = UrlDecode(target.substr(0, qm));
    if (qm != std::string::npos) ParseQuery(target.substr(qm + 1), req.query);

    size_t content_length = 0;
    std::string lower = head;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    size_t cl = lower.find("\r\ncontent-length:");
    if (cl != std::string::npos) content_length = (size_t)std::strtoul(lower.c_str() + cl + 17, nullptr, 10);
    if (content_length > 1 << 20) {
      res.status = 413;
    } else {
      req.body = buf.substr(header_end + 4);
      while (req.body.size() < content_length) {
        ssize_t n = recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        req.body.append(chunk, (size_t)n);
      }
      if (req.method == "POST" && !req.body.empty() && lower.find("x-www-form-urlencoded") != std::string::npos)
        ParseQuery(req.body, req.query);
      g_handler(req, res);
    }
  }

  const void* data = res.data ? res.data : res.body.data();
  size_t size = res.data ? res.size : res.body.size();
  char header[512];
  int hn = std::snprintf(header, sizeof header,
                         "HTTP/1.1 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Cache-Control: %s\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Connection: close\r\n\r\n",
                         res.status, Reason(res.status), res.type.c_str(), size, res.cache.c_str());
  if (SendAll(fd, header, (size_t)hn) && req.method != "HEAD") SendAll(fd, data, size);
  shutdown(fd, SHUT_WR);
  close(fd);
  return nullptr;
}

int Serve(unsigned short port, Handler handler) {
  g_handler = std::move(handler);
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(fd, (sockaddr*)&a, sizeof a) != 0 || listen(fd, 32) != 0) {
    Log("bind/listen on %u failed: %s", port, std::strerror(errno));
    close(fd);
    return -1;
  }
  g_listen_fd = fd;
  Log("listening on port %u", port);
  while (!g_stop) {
    int c = accept(fd, nullptr, nullptr);
    if (c < 0) {
      if (g_stop) break;
      if (errno == EINTR || errno == ECONNABORTED) continue;
      Log("accept: %s", std::strerror(errno));
      usleep(100000);
      continue;
    }
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    if (pthread_create(&t, &attr, Connection, (void*)(intptr_t)c) != 0) close(c);
    pthread_attr_destroy(&attr);
  }
  return 0;
}
