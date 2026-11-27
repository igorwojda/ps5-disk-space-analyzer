// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same steps as ps5-web-file-manager's /api/browser/layout: the patch only
// changes how the browser opens next time, so go home, patch, and reopen.
#include "browser_layout.h"

#include "system.h"

#if defined(__PROSPERO__)

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include <ps5/kernel.h>

#include "browser_patch.h"

extern "C" {
int sceKernelLoadStartModule(const char*, size_t, const void*, uint32_t, void*, int*);
int sceUserServiceInitialize(const int*);
int sceUserServiceGetForegroundUser(int*);
int sceSystemServiceLaunchWebBrowser(const char*, void*);
}

static std::atomic<bool> g_busy{false};
static std::atomic<bool> g_auto_done{false};

static int GoHome() {
  struct {
    uint32_t size;
    int32_t user_id;
  } param = {sizeof(param), -1};
  const int priority = 256;
  sceUserServiceInitialize(&priority);
  int module = sceKernelLoadStartModule("/system_ex/common_ex/lib/libSceShellUIUtil.sprx", 0,
                                        nullptr, 0, nullptr, nullptr);
  if (module < 0) return module;
  auto init = (int (*)())kernel_dynlib_dlsym(-1, (uint32_t)module, "sceShellUIUtilInitialize");
  auto launch = (int (*)(const char*, void*))kernel_dynlib_dlsym(-1, (uint32_t)module,
                                                                  "sceShellUIUtilLaunchByUri");
  if (!init || !launch) return -1;
  init();
  sceUserServiceGetForegroundUser(&param.user_id);
  return launch("pshomeui:navigateToHome?bootCondition=psButton", &param);
}

static void Run(bool fullscreen, std::string url) {
  usleep(250 * 1000);  // let the HTTP response reach the page first
  int err = GoHome();
  if (err) {
    Log("browser layout: going home failed: 0x%08X", err);
    Notify("Disk Space Analyzer: could not switch the browser to %s",
           fullscreen ? "full screen" : "a window");
  } else {
    usleep(1000 * 1000);
    // The patch waits on ShellUI with ptrace and waitpid, which an ignored
    // SIGCHLD would break.
    struct sigaction saved, dfl = {};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    if (sigaction(SIGCHLD, &dfl, &saved) == 0) {
      int rc = BrowserPatch(fullscreen ? 0 : 1, url.c_str());
      sigaction(SIGCHLD, &saved, nullptr);
      Log("browser layout: %s patch result %d", fullscreen ? "fullscreen" : "restore", rc);
    }
  }
  // Reopen even when something failed, so the user is not left on home.
  err = sceSystemServiceLaunchWebBrowser(url.c_str(), nullptr);
  if (err) {
    Log("browser layout: reopening the browser failed: 0x%08X", err);
    Notify("Disk Space Analyzer: open it again from Media");
  }
  g_busy = false;
}

struct Job {
  bool fullscreen;
  std::string url;
};

static void* RunThread(void* arg) {
  auto* job = (Job*)arg;
  Run(job->fullscreen, job->url);
  delete job;
  return nullptr;
}

bool StartBrowserLayout(bool fullscreen, bool auto_once, unsigned short port) {
  if (auto_once && g_auto_done.exchange(true)) return false;
  if (g_busy.exchange(true)) return false;
  Log("browser layout: switching to %s", fullscreen ? "fullscreen" : "window");
  char url[64];
  std::snprintf(url, sizeof url, "http://127.0.0.1:%u/", port);
  auto* job = new Job{fullscreen, url};
  // The patch keeps large tables on the stack: give it 2 MB, as
  // ps5-web-file-manager does.
  pthread_attr_t attr;
  pthread_t thread;
  int err = pthread_attr_init(&attr);
  if (!err) {
    err = pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    if (!err) err = pthread_create(&thread, &attr, RunThread, job);
    pthread_attr_destroy(&attr);
  }
  if (err) {
    delete job;
    g_busy = false;
    return false;
  }
  pthread_detach(thread);
  return true;
}

#else

bool StartBrowserLayout(bool, bool, unsigned short) { return false; }

#endif
