// SPDX-License-Identifier: GPL-3.0-or-later
#include "update.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

#include <curl/curl.h>

#include "http.h"
#include "system.h"

#define UPDATE_REPO "igorwojda/ps5-disk-space-analyzer"
static const char* const kLatestUrl = "https://api.github.com/repos/" UPDATE_REPO "/releases/latest";
// Newest releases first, for the notes of each version since this one.
static const char* const kListUrl = "https://api.github.com/repos/" UPDATE_REPO "/releases?per_page=30";
static const size_t kMaxNotes = 10;          // versions shown in the update dialog
// Only release files of this project are ever downloaded.
static const char* const kDownloadPrefix = "https://github.com/" UPDATE_REPO "/releases/download/";
static const char* const kAgent = "disk-space-analyzer/" APP_VERSION;

static const time_t kRecheckSec = 60 * 60;    // after a check that worked
static const time_t kRetrySec = 5 * 60;       // after one that failed
// Asked checks (app opened or brought back) wait this long after the last
// one, which keeps us under GitHub's 60 requests an hour.
static const time_t kMinGapSec = 60;
static const size_t kMaxDownload = 64 << 20;  // a payload is ~2 MB

// ---------------------------------------------------------------- versions

namespace {

// Digit runs compare as numbers, everything else byte by byte.
int NaturalCompare(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      size_t ie = i, je = j;
      while (ie < a.size() && isdigit((unsigned char)a[ie])) ie++;
      while (je < b.size() && isdigit((unsigned char)b[je])) je++;
      std::string x = a.substr(i, ie - i), y = b.substr(j, je - j);
      x.erase(0, std::min(x.find_first_not_of('0'), x.size()));
      y.erase(0, std::min(y.find_first_not_of('0'), y.size()));
      if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
      if (int c = x.compare(y)) return c < 0 ? -1 : 1;
      i = ie;
      j = je;
    } else {
      if (a[i] != b[j]) return (unsigned char)a[i] < (unsigned char)b[j] ? -1 : 1;
      i++;
      j++;
    }
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return 0;
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t pos = 0;
  for (;;) {
    size_t at = s.find(sep, pos);
    out.push_back(s.substr(pos, at == std::string::npos ? std::string::npos : at - pos));
    if (at == std::string::npos) break;
    pos = at + 1;
  }
  return out;
}

bool AllDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!isdigit((unsigned char)c)) return false;
  return true;
}

std::string StripV(std::string v) {
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
  return v;
}

}  // namespace

int CompareVersions(const std::string& a_in, const std::string& b_in) {
  std::string a = StripV(a_in), b = StripV(b_in);
  a = a.substr(0, a.find('+'));  // build metadata does not count
  b = b.substr(0, b.find('+'));
  size_t ad = a.find('-'), bd = b.find('-');
  std::string acore = a.substr(0, ad), bcore = b.substr(0, bd);
  std::string apre = ad == std::string::npos ? "" : a.substr(ad + 1);
  std::string bpre = bd == std::string::npos ? "" : b.substr(bd + 1);

  auto ac = Split(acore, '.'), bc = Split(bcore, '.');
  for (size_t i = 0; i < 3; i++) {
    unsigned long x = i < ac.size() ? std::strtoul(ac[i].c_str(), nullptr, 10) : 0;
    unsigned long y = i < bc.size() ? std::strtoul(bc[i].c_str(), nullptr, 10) : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  // A release is newer than any of its prereleases.
  if (apre.empty() || bpre.empty()) return apre.empty() == bpre.empty() ? 0 : apre.empty() ? 1 : -1;
  auto ap = Split(apre, '.'), bp = Split(bpre, '.');
  for (size_t i = 0; i < ap.size() && i < bp.size(); i++) {
    bool an = AllDigits(ap[i]), bn = AllDigits(bp[i]);
    if (an != bn) return an ? -1 : 1;  // numeric identifiers sort first
    if (int c = NaturalCompare(ap[i], bp[i])) return c;
  }
  if (ap.size() != bp.size()) return ap.size() < bp.size() ? -1 : 1;
  return 0;
}

// ---------------------------------------------------------------- HTTPS GET

namespace {

struct Fetch {
  std::string body;
  std::atomic<uint64_t>* got = nullptr;  // bytes so far, for progress
  std::string error;
};

#if defined(__PROSPERO__)
// The console's own TLS (libSceSsl) refuses payloads, so libcurl with
// OpenSSL does it, checking GitHub's certificate against Mozilla's CA list
// built in (the toolchain's ca-bundle.crt).
extern "C" const unsigned char ca_bundle[];
extern "C" const size_t ca_bundle_len;
#endif

size_t OnData(char* data, size_t size, size_t n, void* user) {
  auto* f = (Fetch*)user;
  f->body.append(data, size * n);
  if (f->got) *f->got = f->body.size();
  return f->body.size() > kMaxDownload ? 0 : size * n;  // 0 aborts
}

bool Get(const std::string& url, Fetch* f) {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  CURL* c = curl_easy_init();
  if (!c) {
    f->error = "could not start the download";
    return false;
  }
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_USERAGENT, kAgent);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);  // github.com sends files on to its download host
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);  // we run on our own thread
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, OnData);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
#if defined(__PROSPERO__)
  curl_blob ca{(void*)ca_bundle, ca_bundle_len, CURL_BLOB_NOCOPY};
  curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
#endif
  CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(c);
  if (f->body.size() > kMaxDownload) f->error = "file too big";
  else if (rc != CURLE_OK) f->error = curl_easy_strerror(rc);
  else if (status != 200) f->error = "HTTP " + std::to_string(status);
  return f->error.empty();
}

// ---------------------------------------------------------------- release JSON

// Value of the first "key": "..." at or after `from`. Enough for GitHub's
// release JSON: no nesting is needed, only a few string fields.
std::string JsonString(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
  std::string k = "\"" + key + "\"";
  for (size_t p = j.find(k, from); p != std::string::npos; p = j.find(k, p + 1)) {
    size_t q = j.find_first_not_of(" \t\r\n", p + k.size());
    if (q == std::string::npos || j[q] != ':') continue;
    q = j.find_first_not_of(" \t\r\n", q + 1);
    if (q == std::string::npos || j[q] != '"') continue;
    std::string out;
    for (size_t i = q + 1; i < j.size(); i++) {
      if (j[i] == '"') {
        if (at) *at = p;
        return out;
      }
      if (j[i] == '\\' && i + 1 < j.size()) {
        char e = j[++i];
        if (e == 'u' && i + 4 < j.size()) {
          unsigned cp = (unsigned)std::strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16);
          i += 4;
          // A pair of \u escapes for one character past U+FFFF.
          if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < j.size() && j[i + 1] == '\\' && j[i + 2] == 'u') {
            unsigned lo = (unsigned)std::strtoul(j.substr(i + 3, 4).c_str(), nullptr, 16);
            if (lo >= 0xDC00 && lo < 0xE000) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              i += 6;
            }
          }
          if (cp < 0x80) {
            out += (char)cp;
          } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
          } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
          } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
          }
        } else {
          out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e;  // \", \\, \/
        }
      } else {
        out += j[i];
      }
    }
    return "";
  }
  return "";
}

// Last "key": number before `before`.
uint64_t JsonNumberBefore(const std::string& j, const std::string& key, size_t before) {
  std::string k = "\"" + key + "\"";
  size_t p = j.rfind(k, before);
  if (p == std::string::npos) return 0;
  size_t q = j.find_first_not_of(" \t\r\n:", p + k.size());
  return q == std::string::npos ? 0 : std::strtoull(j.c_str() + q, nullptr, 10);
}

// ---------------------------------------------------------------- state

struct State {
  std::mutex mu;
  // idle (never checked), none, available, error, downloading, installing,
  // restarting, failed. A check runs beside it (`checking`), so a known
  // update stays installable while the next check runs.
  std::string state = "idle";
  bool checking = false;
  time_t checked = 0;
  std::string latest, url;
  uint64_t size = 0;
  std::atomic<uint64_t> got{0};
  std::string error;
  std::vector<ReleaseNote> notes;  // versions after this one, up to `notes_for`
  std::string notes_for;           // the latest version the notes were read for
};
State g;

bool Spawn(void* (*fn)(void*)) {
  pthread_t t;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 1024 * 1024);
  bool ok = pthread_create(&t, &attr, fn, nullptr) == 0;
  pthread_attr_destroy(&attr);
  return ok;
}

// The newest release and its .elf, read from GitHub.
struct Release {
  std::string latest, url;
  uint64_t size = 0;
};

bool FetchLatest(Release* r, std::string* err) {
  Fetch f;
  if (!Get(kLatestUrl, &f)) {
    *err = f.error;
    return false;
  }
  r->latest = StripV(JsonString(f.body, "tag_name"));
  size_t at = 0;
  for (size_t from = 0;; from = at + 1) {
    std::string u = JsonString(f.body, "browser_download_url", from, &at);
    if (u.empty()) break;
    if (u.size() > 4 && u.compare(u.size() - 4, 4, ".elf") == 0) {
      r->url = u;
      r->size = JsonNumberBefore(f.body, "size", at);
      break;
    }
  }
  if (r->latest.empty()) {
    *err = "no release found";
    return false;
  }
  return true;
}

// The notes of every release after this copy, up to `latest`, newest first.
bool FetchNotes(const std::string& latest, std::vector<ReleaseNote>* out, std::string* err) {
  Fetch f;
  if (!Get(kListUrl, &f)) {
    *err = f.error;
    return false;
  }
  // Each release's fields, its "body" included, come after its "tag_name"
  // and before the next release's.
  std::vector<size_t> tags;
  for (size_t p = f.body.find("\"tag_name\""); p != std::string::npos; p = f.body.find("\"tag_name\"", p + 1)) tags.push_back(p);
  for (size_t i = 0; i < tags.size() && out->size() < kMaxNotes; i++) {
    size_t end = i + 1 < tags.size() ? tags[i + 1] : f.body.size();
    std::string one = f.body.substr(tags[i], end - tags[i]);
    std::string v = StripV(JsonString(one, "tag_name"));
    if (CompareVersions(v, APP_VERSION) <= 0 || CompareVersions(v, latest) > 0) continue;
    std::string body = JsonString(one, "body");
    body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
    out->push_back({v, body});
  }
  std::sort(out->begin(), out->end(),
            [](const ReleaseNote& a, const ReleaseNote& b) { return CompareVersions(a.version, b.version) > 0; });
  return true;
}

// Newer than this copy, with an .elf of this project to install.
bool Installable(const Release& r) {
  if (CompareVersions(r.latest, APP_VERSION) <= 0) return false;
  if (r.url.compare(0, std::strlen(kDownloadPrefix), kDownloadPrefix) != 0) {
    Log("update %s has no .elf file to install (%s)", r.latest.c_str(), r.url.c_str());
    return false;
  }
  return true;
}

void* CheckThread(void*) {
  Release r;
  std::string err;
  bool ok = FetchLatest(&r, &err);
  bool newer = ok && Installable(r);

  // What changed since this copy: read once for each newer version found.
  // Without them the update is still shown, and the next check tries again.
  if (newer) {
    bool known;
    {
      std::lock_guard<std::mutex> lock(g.mu);
      known = g.notes_for == r.latest;
    }
    std::vector<ReleaseNote> notes;
    std::string nerr;
    if (!known && FetchNotes(r.latest, &notes, &nerr)) {
      std::lock_guard<std::mutex> lock(g.mu);
      g.notes = std::move(notes);
      g.notes_for = r.latest;
    } else if (!known) {
      Log("update notes: %s", nerr.c_str());
    }
  }

  std::lock_guard<std::mutex> lock(g.mu);
  g.checking = false;
  g.checked = std::time(nullptr);
  // An install started meanwhile owns the state now.
  bool busy = g.state == "downloading" || g.state == "installing" || g.state == "restarting";
  if (!ok) {
    Log("update check failed: %s", err.c_str());
    // A failed check does not hide an update already found.
    if (!busy && g.state != "available") {
      g.state = "error";
      g.error = err;
    }
    return nullptr;
  }
  Log("update check: latest %s, running %s%s", r.latest.c_str(), APP_VERSION, newer ? ": update available" : "");
  if (busy) return nullptr;
  g.latest = r.latest;
  g.url = r.url;
  g.size = r.size;
  g.error.clear();
  g.state = newer ? "available" : "none";
  return nullptr;
}

// Sends the payload to the ELF loader, which starts it.
bool SendToLoader(const std::string& elf, std::string* err) {
  std::string host = "127.0.0.1", port = "9021";
  if (const char* env = std::getenv("DSA_LOADER")) {
    std::string v = env;
    size_t colon = v.rfind(':');
    host = v.substr(0, colon);
    if (colon != std::string::npos) port = v.substr(colon + 1);
  }
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
    *err = "Unknown ELF loader address " + host + ":" + port;
    return false;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  timeval tv{15, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  bool ok = fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0;
  freeaddrinfo(res);
  if (!ok) {
    if (fd >= 0) close(fd);
    *err = "The ELF loader is not reachable on port " + port + ". Is it running?";
    return false;
  }
  const char* p = elf.data();
  size_t left = elf.size();
  while (left) {
    ssize_t n = send(fd, p, left, 0);
    if (n <= 0) break;
    p += n;
    left -= (size_t)n;
  }
  shutdown(fd, SHUT_WR);
  close(fd);
  if (left) {
    *err = "Sending the update to the ELF loader failed";
    return false;
  }
  return true;
}

void* InstallThread(void*) {
  // Always install the newest release, even when a newer one came out after
  // the update was found. If GitHub cannot be asked, the one found is used.
  std::string err;
  Release r;
  if (FetchLatest(&r, &err)) {
    if (!Installable(r)) err = "No newer version to install (latest is " + r.latest + ")";
  } else {
    Log("update: could not re-check the latest release (%s), installing the one found", err.c_str());
    err.clear();
    std::lock_guard<std::mutex> lock(g.mu);
    r.latest = g.latest;
    r.url = g.url;
    r.size = g.size;
  }
  std::string url = r.url, latest = r.latest;
  uint64_t size = r.size;
  Fetch f;
  if (err.empty()) {
    {
      std::lock_guard<std::mutex> lock(g.mu);
      g.latest = latest;
      g.url = url;
      g.size = size;
    }
    Log("update: downloading %s", url.c_str());
    f.got = &g.got;
    if (!Get(url, &f)) err = "Download failed: " + f.error;
    else if (size && f.body.size() != size) err = "Download incomplete (" + std::to_string(f.body.size()) + " of " + std::to_string(size) + " bytes)";
    else if (f.body.size() < 4 || f.body.compare(0, 4, "\x7f" "ELF") != 0) err = "The downloaded file is not a payload";
  }

  if (err.empty()) {
    {
      std::lock_guard<std::mutex> lock(g.mu);
      g.state = "installing";
    }
    Log("update: sending %zu bytes to the ELF loader", f.body.size());
    if (SendToLoader(f.body, &err)) {
      {
        std::lock_guard<std::mutex> lock(g.mu);
        g.state = "restarting";
      }
      // Step aside at once rather than wait to be stopped: the new copy finds
      // an older one by thread name, which can miss (the kernel may list the
      // process under a worker thread's name), and would then take the next
      // port instead of ours. Stopping only after the loader has the bytes
      // keeps this copy running when the hand-over fails.
      Log("update: %s sent to the loader, stopping this copy", latest.c_str());
      Stop();
      return nullptr;
    }
  }
  std::lock_guard<std::mutex> lock(g.mu);
  Log("update failed: %s", err.c_str());
  g.state = "failed";
  g.error = err;
  return nullptr;
}

}  // namespace

UpdateStatus GetUpdateStatus(bool check) {
  std::lock_guard<std::mutex> lock(g.mu);
  time_t now = std::time(nullptr);
  time_t age = now - g.checked;
  bool idle = g.state == "idle" || g.state == "none" || g.state == "available" ||
              g.state == "error" || g.state == "failed";
  bool due = g.state == "idle" || (check && age >= kMinGapSec) ||
             ((g.state == "none" || g.state == "available") && age >= kRecheckSec) ||
             (g.state == "error" && age >= kRetrySec);
  if (idle && due && !g.checking) {
    g.checking = Spawn(CheckThread);
  }
  UpdateStatus s;
  s.state = g.state == "idle" ? "checking" : g.state;
  s.checking = g.checking;
  s.current = APP_VERSION;
  s.latest = g.latest;
  s.size = g.size;
  s.got = g.got;
  s.error = g.error;
  if (g.notes_for == g.latest) s.notes = g.notes;
  return s;
}

bool StartUpdate(std::string* err) {
  std::lock_guard<std::mutex> lock(g.mu);
  if (g.state != "available" && g.state != "failed") {
    *err = g.state == "downloading" || g.state == "installing" || g.state == "restarting"
               ? "An update is already being installed"
               : "No update available";
    return false;
  }
  g.state = "downloading";
  g.got = 0;
  g.error.clear();
  if (!Spawn(InstallThread)) {
    g.state = "failed";
    *err = g.error = "Could not start the update";
    return false;
  }
  return true;
}
