// SPDX-License-Identifier: MIT
// Disk Space Analyzer: a payload that serves a web UI for finding and
// deleting what takes space. On the PS5 it also adds a tile to the Media tab
// that opens the UI in the console's browser.
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "api.h"
#include "http.h"
#include "launcher.h"
#include "system.h"

#if defined(__PROSPERO__)
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <cstdint>

#define PROCESS_NAME "disk-space-analyzer.elf"
// Names of earlier builds, stopped as well when this one starts.
static const char* const kOldNames[] = {"disk-analyzer.elf", "storage-analyzer.elf"};

// The kernel keeps a short thread name, so a long name comes back cut off.
static bool SameName(const char* name, const char* tdname) {
  size_t n = strnlen(tdname, 32);
  if (n == std::strlen(name)) return std::strncmp(name, tdname, n) == 0;
  return n >= 16 && n < std::strlen(name) && std::strncmp(name, tdname, n) == 0;
}

// pid of another process with our name (an older copy still running).
static pid_t FindOther(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  size_t size = 0;
  if (sysctl(mib, 4, nullptr, &size, nullptr, 0)) return -1;
  auto* buf = (uint8_t*)std::malloc(size);
  if (!buf) return -1;
  pid_t found = -1, me = getpid();
  if (!sysctl(mib, 4, buf, &size, nullptr, 0)) {
    for (uint8_t* p = buf; p < buf + size;) {
      int structsize = *(int*)p;
      pid_t pid = *(pid_t*)&p[72];
      const char* tdname = (const char*)&p[447];
      if (structsize <= 0) break;
      p += structsize;
      if (SameName(name, tdname) && pid != me) found = pid;
    }
  }
  std::free(buf);
  return found;
}
#endif

static const unsigned short kDefaultPort = 8833;

int main(int argc, char* argv[]) {
  unsigned short port = kDefaultPort;
  std::string web_dir;
  std::vector<std::string> extra;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = (unsigned short)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--web") && i + 1 < argc) web_dir = argv[++i];
    else if (!std::strcmp(argv[i], "-h") || !std::strcmp(argv[i], "--help")) {
      std::printf("Usage: %s [--port N] [--web DIR] [PATH...]\n"
                  "  --port N   first port to try (default %u)\n"
                  "  --web DIR  serve the UI from DIR instead of the built-in copy\n",
                  argv[0], kDefaultPort);
      return 0;
    } else if (argv[i][0] != '-') {
      std::string p = argv[i];
      while (p.size() > 1 && p.back() == '/') p.pop_back();
      extra.push_back(p);
    }
  }

  signal(SIGPIPE, SIG_IGN);
  LogOpen();
  Log("starting");

#if defined(__PROSPERO__)
  // One instance: replace an older copy so re-sending the ELF updates it.
  syscall(SYS_thr_set_name, -1, PROCESS_NAME);
  for (int tries = 0; tries < 10; tries++) {
    pid_t old = FindOther(PROCESS_NAME);
    for (const char* name : kOldNames)
      if (old <= 0) old = FindOther(name);
    if (old <= 0) break;
    Log("stopping older instance %d", (int)old);
    kill(old, SIGKILL);
    sleep(1);
  }
#endif

  port = FindFreePort(port);
  if (!port) {
    Notify("Disk Space Analyzer: no free network port");
    return 1;
  }
  ApiInit(web_dir, extra, port);
  std::string ip = LanAddress();
  std::string url = "http://" + (ip.empty() ? std::string("<PS5 IP>") : ip) + ":" + std::to_string(port);
  Log("version %s, %s", APP_VERSION, url.c_str());

  if (InstallLauncher(port) == 0) {
    Notify("Disk Space Analyzer %s is running\nOpen it from Media, or %s", APP_VERSION, url.c_str());
  } else {
    Notify("Disk Space Analyzer %s is running on %s\n(the Media tile could not be added, see log)",
           APP_VERSION, url.c_str());
  }
#if !defined(__PROSPERO__)
  std::printf("Open http://localhost:%u/ (LAN: %s/)\n", port, url.c_str());
  std::fflush(stdout);
#endif

  int rc = Serve(port, ApiHandle);
  usleep(300 * 1000);  // let the last response go out
  Log("stopped");
  return rc;
}
