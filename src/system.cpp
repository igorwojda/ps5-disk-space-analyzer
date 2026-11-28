// SPDX-License-Identifier: GPL-3.0-or-later
#include "system.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

static const char kLogDir[] = "/data/disk-space-analyzer/log";
static const int kKeepDays = 10;

static std::mutex g_log_mu;
static bool g_log_on = false;  // file logging (PS5 only)
static FILE* g_log = nullptr;
static char g_log_day[16];     // date of the open file, YYYY-MM-DD

static void DayOf(time_t t, char out[16]) {
  struct tm tm;
  localtime_r(&t, &tm);
  std::strftime(out, 16, "%Y-%m-%d", &tm);
}

// Opens the file for `day` (YYYY-MM-DD.log), closing the previous one. Caller holds g_log_mu.
static void OpenDay(const char* day) {
  if (g_log) std::fclose(g_log);
  std::snprintf(g_log_day, sizeof g_log_day, "%s", day);
  std::string path = std::string(kLogDir) + "/" + day + ".log";
  g_log = std::fopen(path.c_str(), "a");
}

// Removes YYYY-MM-DD.log files from more than kKeepDays days ago.
[[maybe_unused]] static void RemoveOldLogs() {
  char cutoff[16];
  DayOf(std::time(nullptr) - (time_t)kKeepDays * 24 * 60 * 60, cutoff);
  DIR* d = opendir(kLogDir);
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    const char* n = e->d_name;
    // Names sort by date, so a plain string compare finds the old ones.
    if (std::strlen(n) == 14 && std::strcmp(n + 10, ".log") == 0 && std::strncmp(n, cutoff, 10) < 0)
      unlink((std::string(kLogDir) + "/" + n).c_str());
  }
  closedir(d);
}

void LogOpen() {
#if defined(__PROSPERO__)
  // Logs of older builds: disk-analyzer, then a single disk-space-analyzer.log.
  unlink("/data/disk-analyzer/disk-analyzer.log");
  rmdir("/data/disk-analyzer");
  unlink("/data/disk-space-analyzer/disk-space-analyzer.log");
  mkdir("/data/disk-space-analyzer", 0755);
  mkdir(kLogDir, 0755);
  RemoveOldLogs();
  char day[16];
  DayOf(std::time(nullptr), day);
  std::lock_guard<std::mutex> lock(g_log_mu);
  g_log_on = true;
  OpenDay(day);
#endif
}

void Log(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::fprintf(stderr, "[disk-space-analyzer] %s\n", buf);
  std::lock_guard<std::mutex> lock(g_log_mu);
  if (!g_log_on) return;
  time_t now = std::time(nullptr);
  char day[16];
  DayOf(now, day);
  if (std::strcmp(day, g_log_day) != 0) OpenDay(day);  // past midnight: start the new day's file
  if (g_log) {
    std::fprintf(g_log, "%ld %s\n", (long)now, buf);
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
