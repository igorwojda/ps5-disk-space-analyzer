// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same mechanism as owendswang/ps5-web-file-manager, which is known to work:
// a title of category 65536 (Media) whose deeplinkUri points the browser at
// this payload's web server. Only /user/app is touched.
#include "launcher.h"

#include "system.h"

#if defined(__PROSPERO__)

#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <ps5/kernel.h>

#include "web_assets.h"

#define TITLE_ID OWN_TITLE_ID
// Tiles of earlier builds, removed if present.
static const char* const kOldTitleIds[] = {"DSKA00001", "DSAN00001", "SSAN00001"};

extern "C" {
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void*);
int sceAppInstUtilAppUnInstall(const char*);
}

static std::string ReadFile(const char* path) {
  std::string out;
  if (FILE* f = std::fopen(path, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
  }
  return out;
}

static bool WriteFile(const char* path, const void* data, size_t size) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  bool ok = std::fwrite(data, size, 1, f) == 1;
  ok = std::fclose(f) == 0 && ok;
  return ok;
}

static int InstallTitle(const char* title_id, const char* dir) {
  int (*install_title_dir)(const char*, const char*, void*) = nullptr;
  uint32_t handle;
  if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
    install_title_dir = (int (*)(const char*, const char*, void*))kernel_dynlib_resolve(
        -1, handle, "Wudg3Xe3heE");
  if (install_title_dir) return install_title_dir(title_id, dir, nullptr);
  return sceAppInstUtilAppInstallAll(nullptr);
}

int InstallLauncher(unsigned short port) {
  char param[1024];
  std::snprintf(param, sizeof param,
                "{\n"
                "  \"applicationCategoryType\": 65536,\n"
                "  \"titleId\": \"" TITLE_ID "\",\n"
                "  \"deeplinkUri\": \"http://127.0.0.1:%u/\",\n"
                "  \"localizedParameters\": {\n"
                "    \"defaultLanguage\": \"en-US\",\n"
                "    \"en-US\": {\n"
                "      \"titleName\": \"Disk Space Analyzer\"\n"
                "    }\n"
                "  }\n"
                "}\n",
                port);

  const char* dir = "/user/app/" TITLE_ID;
  const char* sce_sys = "/user/app/" TITLE_ID "/sce_sys";
  const char* param_path = "/user/app/" TITLE_ID "/sce_sys/param.json";
  const char* icon_path = "/user/app/" TITLE_ID "/sce_sys/icon0.png";
  const Asset* icon = FindAsset("/icon0.png");

  struct stat st;
  bool icon_ok = stat(icon_path, &st) == 0 && icon && (size_t)st.st_size == icon->size;
  if (ReadFile(param_path) == param && icon_ok) {
    Log("launcher already installed");
    return 0;
  }

  int err = sceAppInstUtilInitialize();
  if (err) {
    Log("sceAppInstUtilInitialize: 0x%08X", err);
    return -1;
  }
  for (const char* old : kOldTitleIds) {
    std::string old_dir = std::string("/user/app/") + old;
    if (stat(old_dir.c_str(), &st) == 0) {
      Log("launcher: removing old tile %s", old);
      sceAppInstUtilAppUnInstall(old);
    }
  }

  if ((mkdir("/user/app", 0700) && errno != EEXIST) || (mkdir(dir, 0755) && errno != EEXIST) ||
      (mkdir(sce_sys, 0755) && errno != EEXIST)) {
    Log("launcher mkdir: %s", std::strerror(errno));
    return -1;
  }
  if (!WriteFile(param_path, param, std::strlen(param)) ||
      (icon && !WriteFile(icon_path, icon->data, icon->size))) {
    Log("launcher write: %s", std::strerror(errno));
    return -1;
  }
  if ((err = InstallTitle(TITLE_ID, "/user/app/"))) {
    Log("launcher install: 0x%08X", err);
    return -1;
  }
  Log("launcher installed (port %u)", port);
  return 0;
}

#else

int InstallLauncher(unsigned short) { return 0; }

#endif
