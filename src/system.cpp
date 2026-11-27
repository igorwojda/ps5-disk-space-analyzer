// SPDX-License-Identifier: GPL-3.0-or-later
#include "system.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

static FILE* g_log = nullptr;

void LogOpen() {
#if defined(__PROSPERO__)
  // Log of builds named disk-analyzer.
  unlink("/data/disk-analyzer/disk-analyzer.log");
  rmdir("/data/disk-analyzer");
  mkdir("/data/disk-space-analyzer", 0755);
  g_log = std::fopen("/data/disk-space-analyzer/disk-space-analyzer.log", "w");
#endif
}

void Log(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "[disk-space-analyzer] %s\n", buf);
  if (g_log) {
    std::fprintf(g_log, "%ld %s\n", (long)std::time(nullptr), buf);
    std::fflush(g_log);  // keep the line if we crash right after
  }
}

#if defined(__PROSPERO__)
extern "C" {
typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
}
#endif

void Notify(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Log("notify: %s", buf);
#if defined(__PROSPERO__)
  notify_request_t req;
  std::memset(&req, 0, sizeof req);
  std::snprintf(req.message, sizeof req.message, "%s", buf);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
#endif
}
