// SPDX-License-Identifier: MIT
//
// The uninstall is the one Playden's title remover makes
// (playden-ps5 tools/title-remover/main.c): initialize AppInstUtil, act as the
// installer (ShadowMountPlus's authid), then sceAppInstUtilAppUnInstall. The
// console finishes on its own time; the title is gone once no app folder of it
// is left, as Playden checks. Playden's file formats are from playden-ps5
// (src/runtime/package_installer.cpp, src/installs/install_service.h).
#include "uninstall.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "games.h"
#include "system.h"
#include "tree.h"

#if defined(__PROSPERO__)
#include <ps5/kernel.h>
extern "C" {
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppUnInstall(const char*);
}
#endif

namespace {

const char kPlaydenAppTitle[] = "FAKE77031";

ConsoleLayout MakeLayout(const std::string& root) {
  ConsoleLayout l;
  l.title_bases = {root + "/user", root + "/mnt/ext0/user", root + "/mnt/ext1/user"};
  l.homebrew = root + "/user/data/homebrew";
  l.playden_app = l.homebrew + "/" + kPlaydenAppTitle;
  l.playden_bottles = root + "/user/data/playden/games";
  l.playden_games = {root + "/user/data/playden-games", root + "/mnt/ext0/playden/games",
                     root + "/mnt/ext1/playden/games"};
  for (int i = 0; i < 8; i++) l.playden_games.push_back(root + "/mnt/usb" + std::to_string(i) + "/playden/games");
  return l;
}

bool HasPrefix(const std::string& p, const std::string& dir) {
  return !dir.empty() && p.compare(0, dir.size(), dir) == 0 && (p.size() == dir.size() || p[dir.size()] == '/');
}

// /data is how the console names /user/data.
std::string Canonical(const std::string& p) {
#if defined(__PROSPERO__)
  if (HasPrefix(p, "/data")) return "/user" + p;
#endif
  return p;
}

void Split(const std::string& path, std::string* parent, std::string* name) {
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    parent->clear();
    *name = path;
    return;
  }
  *parent = slash == 0 ? "/" : path.substr(0, slash);
  *name = path.substr(slash + 1);
}

bool IsRealDir(const std::string& p) {
  struct stat st;
  return lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// A game's title (not a system app or a trophy set).
bool IsGameTitle(const std::string& id) {
  return IsTitleId(id) && id.compare(0, 4, "NPXS") != 0 && id.compare(0, 4, "NPWR") != 0;
}

bool IsAppFolder(const ConsoleLayout& l, const std::string& parent) {
  for (const std::string& b : l.title_bases)
    if (parent == b + "/app") return true;
  return false;
}

bool IsPlaydenGames(const ConsoleLayout& l, const std::string& parent) {
  return std::find(l.playden_games.begin(), l.playden_games.end(), parent) != l.playden_games.end();
}

// Everything of a title the system removes when it uninstalls it.
std::vector<std::string> SystemFolders(const ConsoleLayout& l, const std::string& id) {
  std::vector<std::string> out;
  for (const std::string& b : l.title_bases)
    for (const char* kind : {"/app/", "/patch/", "/addcont/", "/appmeta/"}) out.push_back(b + kind + id);
  if (!l.title_bases.empty()) out.push_back(l.title_bases[0] + "/appmeta/external/" + id);
  return out;
}

bool AllDigits(const std::string& s, size_t from, size_t max) {
  size_t n = s.size() - std::min(from, s.size());
  if (n == 0 || n > max) return false;
  for (size_t i = from; i < s.size(); i++)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

// What Playden's request file takes for a game: a Steam app ID, gog-<id> or local-<uuid>.
bool IsRequestWord(const std::string& w) {
  if (AllDigits(w, 0, 20)) return true;
  if (w.compare(0, 4, "gog-") == 0) return AllDigits(w, 4, 20);
  if (w.compare(0, 6, "local-") == 0)
    return w.size() > 6 && w.size() <= 64 && w.find_first_not_of("0123456789abcdef-", 6) == std::string::npos;
  return false;
}

// Playden names a Steam game by its app ID, the others by their install id.
std::string RequestWord(const std::string& install_id) {
  return install_id.compare(0, 6, "steam-") == 0 ? install_id.substr(6) : install_id;
}

std::string ReadSmall(const std::string& path, size_t limit) {
  std::string s;
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return s;
  char buf[1024];
  ssize_t n;
  while (s.size() < limit && (n = read(fd, buf, sizeof buf)) > 0) s.append(buf, (size_t)n);
  close(fd);
  return s;
}

}  // namespace

const ConsoleLayout& Console() {
#if defined(__PROSPERO__)
  static const ConsoleLayout layout = MakeLayout("");
#else
  static const ConsoleLayout layout = [] {
    const char* root = std::getenv("DSA_CONSOLE_ROOT");
    return root && *root ? MakeLayout(root) : ConsoleLayout{};
  }();
#endif
  return layout;
}

bool IsPlaydenTitle(const std::string& id) {
  return id.size() == 9 && id.compare(0, 6, "FAKE78") == 0 && AllDigits(id, 6, 3);
}

std::pair<std::string, std::string> ParsePlaydenOwner(const std::string& text) {
  std::string install, title;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.compare(0, 11, "install_id=") == 0) install = line.substr(11);
    else if (line.compare(0, 9, "title_id=") == 0) title = line.substr(9);
    at = end + 1;
  }
  return {install, title};
}

PlaydenTitles ReadPlaydenTitles(const std::string& homebrew) {
  PlaydenTitles out;
  if (homebrew.empty()) return out;
  DIR* d = opendir(homebrew.c_str());
  if (!d) return out;
  std::vector<std::string> names;
  while (struct dirent* e = readdir(d))
    if (IsPlaydenTitle(e->d_name)) names.push_back(e->d_name);
  closedir(d);
  for (const std::string& name : names) {
    auto owner = ParsePlaydenOwner(ReadSmall(homebrew + "/" + name + "/.playden-title-owner", 4096));
    // The folder must be the title it names: a copied marker is not believed.
    if (!owner.first.empty() && owner.second == name) out[owner.first] = owner.second;
  }
  return out;
}

std::string PlaydenInstallId(const std::string& n) {
  if (n.compare(0, 6, "steam-") == 0 || n.compare(0, 4, "gog-") == 0 || n.compare(0, 6, "local-") == 0) return n;
  return AllDigits(n, 0, 20) ? "steam-" + n : "";
}

PlaydenFolder PlaydenFolderOf(const std::string& raw, const PlaydenTitles& titles) {
  const ConsoleLayout& l = Console();
  std::string parent, name;
  Split(Canonical(raw), &parent, &name);
  PlaydenFolder f;
  std::string install;
  if (IsPlaydenGames(l, parent)) {
    install = PlaydenInstallId(name);
    f.category = "Game";
  } else if (!l.playden_bottles.empty() && parent == l.playden_bottles) {
    install = name;  // bottles are named by install id; "<id>.kept" is not one
    f.category = "Saves";
  }
  auto it = install.empty() ? titles.end() : titles.find(install);
  if (it != titles.end()) f.title = it->second;
  return f;
}

DeletePlan PlanDelete(const std::string& raw, const PlaydenTitles& titles) {
  const ConsoleLayout& l = Console();
  DeletePlan plan;
  if (l.title_bases.empty()) return plan;
  const std::string path = Canonical(raw);
  std::string parent, name;
  Split(path, &parent, &name);

  if (HasPrefix(path, l.playden_app) || name == kPlaydenAppTitle) {
    plan.refuse = "This is Playden itself: uninstall it from Playden's settings.";
    return plan;
  }
  // A title's main folder: uninstalled, never deleted by hand.
  if (IsAppFolder(l, parent) && IsGameTitle(name)) {
    plan.uninstall = name;
    plan.system_removes = SystemFolders(l, name);
    if (IsPlaydenTitle(name)) plan.also.push_back(l.homebrew + "/" + name);
    return plan;
  }
  // The folder a Playden title is installed from.
  if (parent == l.homebrew && IsPlaydenTitle(name)) {
    plan.uninstall = name;
    plan.system_removes = SystemFolders(l, name);
    return plan;
  }
  // A Playden game's files: its title goes first, and Playden forgets it.
  if (IsPlaydenGames(l, parent)) {
    std::string install = PlaydenInstallId(name);
    if (install.empty()) return plan;
    for (const std::string& other : l.playden_games)
      if (other != parent && IsRealDir(other + "/" + name)) {
        plan.refuse = "This Playden game's files are on more than one drive: uninstall it in Playden.";
        return plan;
      }
    auto it = titles.find(install);
    if (it != titles.end() && IsPlaydenTitle(it->second)) {
      plan.uninstall = it->second;
      plan.system_removes = SystemFolders(l, it->second);
      plan.also.push_back(l.homebrew + "/" + it->second);
    }
    std::string word = RequestWord(install);
    if (IsRequestWord(word)) plan.playden_forget = word;
  }
  return plan;
}

bool TitleListed(const std::string& id) {
  for (const std::string& b : Console().title_bases) {
    struct stat st;
    std::string p = b + "/app/" + id;
    if (lstat(p.c_str(), &st) == 0) return true;
    // A folder that could not be checked is never taken for gone.
    if (errno != ENOENT && errno != ENOTDIR) return true;
  }
  return false;
}

std::string UninstallTitle(const std::string& id) {
  if (!TitleListed(id)) return "";
#if defined(__PROSPERO__)
  int err = sceAppInstUtilInitialize();
  if (err) Log("uninstall: sceAppInstUtilInitialize 0x%08X (continuing)", err);
  const uint64_t kAuthidInstaller = 0x4800000000000006ull;
  pid_t me = getpid();
  uint64_t old = kernel_get_ucred_authid(me);
  kernel_set_ucred_authid(me, kAuthidInstaller);
  err = sceAppInstUtilAppUnInstall(id.c_str());
  kernel_set_ucred_authid(me, old);
  Log("uninstall %s: 0x%08X", id.c_str(), err);
  if (err) {
    char msg[128];
    std::snprintf(msg, sizeof msg, "The console refused to uninstall %s (0x%08X). Nothing of it was deleted.",
                  id.c_str(), (unsigned)err);
    return msg;
  }
  for (int i = 0; i < 60; i++) {
    if (!TitleListed(id)) return "";
    sleep(1);
  }
  return "The console has not finished uninstalling " + id + " yet. Scan again in a minute to check.";
#else
  // A computer has no system to ask: remove what the console would.
  for (const std::string& p : SystemFolders(Console(), id)) RemoveTree(p);
  Log("uninstall %s (simulated)", id.c_str());
  return TitleListed(id) ? "Could not remove " + id + "." : "";
#endif
}

bool AskPlaydenToForget(const std::string& game) {
  if (!IsRequestWord(game)) return false;
  const std::string& app = Console().playden_app;
  if (app.empty() || !IsRealDir(app)) return true;  // no Playden: nothing to tell
  // Real folders all the way and a plain file with one name: a link anywhere
  // would send the line elsewhere.
  std::string dev = app + "/dev";
  if (mkdir(dev.c_str(), 0777) == 0) chmod(dev.c_str(), 0777);  // Playden deletes the file once read
  else if (errno != EEXIST) return false;
  if (!IsRealDir(dev)) return false;
  int fd = open((dev + "/install-request.txt").c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0666);
  if (fd < 0) return false;
  struct stat st;
  bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1;
  std::string line = "uninstall " + game + "\n";
  if (ok) {
    fchmod(fd, 0666);  // Playden runs as another user
    ok = write(fd, line.data(), line.size()) == (ssize_t)line.size();
  }
  ok = close(fd) == 0 && ok;
  Log("playden request \"uninstall %s\": %s", game.c_str(), ok ? "written" : "failed");
  return ok;
}

std::string RemovedWithTitle(const std::string& raw) {
  const ConsoleLayout& l = Console();
  std::string path = Canonical(raw), parent, name;
  Split(path, &parent, &name);
  if (!IsGameTitle(name)) return "";
  for (const std::string& p : SystemFolders(l, name))
    if (p == path) return name;
  return "";
}

std::string UninstallWarning(const std::string& raw) {
  const ConsoleLayout& l = Console();
  if (l.title_bases.empty()) return "";
  std::string parent, name;
  Split(Canonical(raw), &parent, &name);
  bool app = IsAppFolder(l, parent) && IsGameTitle(name);
  if ((app || parent == l.homebrew) && IsPlaydenTitle(name))
    return "Removes this Playden game's PS5 title through the console. Its files stay, and Playden offers "
           "to finish its setup again.";
  if (app)
    return "Uninstalls the game through the console, as the home screen does: its update and add-ons go too. "
           "Saves stay.";
  if (IsPlaydenGames(l, parent) && !PlaydenInstallId(name).empty())
    return "Uninstalls this Playden game: its PS5 title goes through the console, and Playden is told to "
           "forget it. Saves stay.";
  if (parent == l.playden_bottles) return "A Playden game's Windows folder: its saves are in it.";
  return "";
}
