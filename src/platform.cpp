// SPDX-License-Identifier: MIT
#include "platform.h"

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include "uninstall.h"

bool QuerySpace(const std::string& path, uint64_t* total, uint64_t* free) {
  struct statfs sf;
  if (statfs(path.c_str(), &sf) != 0) return false;
  *total = (uint64_t)sf.f_blocks * (uint64_t)sf.f_bsize;
  *free = (uint64_t)sf.f_bavail * (uint64_t)sf.f_bsize;
  return true;
}

static bool IsDir(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<Storage> ListStorages(const std::vector<std::string>& extra) {
  std::vector<Storage> cand;
#if defined(__PROSPERO__)
  cand.push_back({"Internal storage", "/user", "internal"});
  cand.push_back({"M.2 SSD", "/mnt/ext0", "ext"});
  cand.push_back({"Extended storage", "/mnt/ext1", "ext"});
  for (int i = 0; i < 8; i++) {
    std::string p = "/mnt/usb" + std::to_string(i);
    cand.push_back({"USB drive " + std::to_string(i + 1), p, "usb"});
  }
  cand.push_back({"System partition", "/", "system"});
#else
  if (const char* home = std::getenv("HOME")) cand.push_back({"Home folder", home, "internal"});
  char cwd[MAXPATHLEN];
  if (getcwd(cwd, sizeof cwd)) cand.push_back({"Current folder", cwd, "folder"});
  cand.push_back({"Root", "/", "system"});
#endif
  for (auto& e : extra) {
    size_t slash = e.find_last_of('/');
    std::string name = slash == std::string::npos || slash + 1 == e.size() ? e : e.substr(slash + 1);
    cand.insert(cand.begin(), {name, e, "folder"});
  }

  std::vector<Storage> out;
  for (auto& s : cand) {
    if (!IsDir(s.path)) continue;
    if (!QuerySpace(s.path, &s.total, &s.free)) continue;
#if defined(__PROSPERO__)
    // An empty mount point (e.g. /mnt/usb3 with nothing plugged in) reports
    // the parent file system; skip anything that is not a mount root.
    if (s.kind == "usb" || s.kind == "ext") {
      struct statfs sf;
      if (statfs(s.path.c_str(), &sf) != 0 || s.path != sf.f_mntonname) continue;
    }
#endif
    out.push_back(s);
  }
  return out;
}

uint64_t DiskBytes(const struct stat& st) {
  uint64_t alloc = (uint64_t)st.st_blocks * 512u;
  uint64_t size = (uint64_t)st.st_size;
#if defined(__PROSPERO__)
  // Installed PS5 games' app.pkg reports a fraction of its blocks (HITMAN 3:
  // 108 MB of 67.8 GB) though the whole file is on disk. The PS5 has no
  // sparse files worth knowing about, so the larger of the two is right.
  return alloc > size ? alloc : size;
#else
  // Allocated size is what a delete gives back (sparse files, compression);
  // fall back to the logical size on file systems that do not report blocks.
  return alloc ? alloc : size;
#endif
}

std::vector<std::string> SkipList() {
#if defined(__PROSPERO__)
  return {"/dev", "/proc", "/mnt/sandbox", "/mnt/proc"};
#else
  return {"/dev", "/proc", "/System/Volumes"};
#endif
}

static bool HasPrefix(const std::string& p, const char* dir) {
  size_t n = std::strlen(dir);
  return p.compare(0, n, dir) == 0 && (p.size() == n || p[n] == '/');
}

bool IsProtected(const std::string& p) {
  // Never anything directly under the root, and never the root itself.
  if (p.empty() || p == "/") return true;
  if (p.find('/', 1) == std::string::npos) return true;

  static const char* always[] = {
#if defined(__PROSPERO__)
      "/system", "/system_ex", "/system_data", "/system_tmp", "/preinst", "/preinst2",
      "/update", "/dev", "/proc", "/mnt/sandbox", "/user/system", "/user/license",
      "/user/settings",
#else
      "/System", "/bin", "/sbin", "/usr", "/dev", "/private/var/db", "/Library/Apple",
#endif
  };
  for (const char* a : always)
    if (HasPrefix(p, a)) return true;

#if defined(__PROSPERO__)
  // Volume roots and the top folders of the user partition.
  static const char* exact[] = {"/user/home", "/user/app", "/user/patch", "/user/appmeta",
                                "/user/addcont", "/user/av_contents", "/user/data",
                                "/data/homebrew", "/user/data/homebrew", "/mnt/ext0", "/mnt/ext1"};
  for (const char* e : exact)
    if (p == e) return true;
  if (p.compare(0, 8, "/mnt/usb") == 0 && p.find('/', 8) == std::string::npos) return true;
  // Folders holding installed games: deleting one whole would take the games
  // without telling the console or Playden. Games go one at a time.
  static const char* holders[] = {"/user/data/playden", "/user/data/playden/games", "/user/data/playden-games",
                                  "/data/playden", "/data/playden/games", "/data/playden-games"};
  for (const char* e : holders)
    if (p == e) return true;
  for (const char* m : {"/mnt/ext0", "/mnt/ext1"}) {
    std::string base = m;
    for (const char* sub : {"/user", "/user/app", "/user/patch", "/user/addcont", "/user/appmeta"})
      if (p == base + sub) return true;
  }
  if (p.compare(0, 5, "/mnt/") == 0) {
    size_t s = p.find('/', 5);
    if (s != std::string::npos && (p.compare(s, std::string::npos, "/playden") == 0 ||
                                   p.compare(s, std::string::npos, "/playden/games") == 0))
      return true;
  }
  // Playden itself: uninstalled from its own settings.
  if (HasPrefix(p, "/user/data/homebrew/FAKE77031") || HasPrefix(p, "/data/homebrew/FAKE77031") ||
      HasPrefix(p, "/user/app/FAKE77031"))
    return true;
#else
  if (const char* home = std::getenv("HOME"))
    if (p == home) return true;
#endif
  return false;
}

std::string DeleteWarning(const std::string& p) {
  std::string w = UninstallWarning(p);
  if (!w.empty()) return w;
#if defined(__PROSPERO__)
  // Part of an installed game, here or on extended storage: the game itself
  // is uninstalled through the console (UninstallWarning), these are not.
  std::string q = p;
  for (const char* m : {"/mnt/ext0", "/mnt/ext1"})
    if (HasPrefix(q, m)) q = q.substr(std::strlen(m));
  if (HasPrefix(q, "/user/app"))
    return "Deleting part of a game can stop it from starting. Delete the whole game to uninstall it.";
  if (HasPrefix(q, "/user/patch") || HasPrefix(q, "/user/addcont"))
    return "Deleting only an update or add-on can stop the game from starting. Delete the game itself to "
           "uninstall all of it.";
  if (HasPrefix(q, "/user/appmeta"))
    return "The game's home screen info: its tile can lose its name or picture.";
  if (HasPrefix(p, "/user/home"))
    return "Contains user profile data such as saves.";
#else
  (void)p;
#endif
  return "";
}
