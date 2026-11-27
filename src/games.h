// SPDX-License-Identifier: MIT
// Game names and icons for folders named after a title ID (PPSA01325, CUSA00207),
// and the space each game takes across all its folders.
#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// True for a name like "PPSA01325": four capital letters and five digits.
bool IsTitleId(const std::string& name);

struct GameInfo {
  std::string title;  // "" when no metadata was found
  std::string icon;   // path of icon0.png, "" when none
};

// Looks the title up once and caches it. `folder` is the folder being shown,
// whose own sce_sys/ is the last place searched (homebrew on USB drives).
GameInfo LookupGame(const std::string& id, const std::string& folder);

// Forget cached lookups, so a rescan sees newly installed games.
void ClearGameCache();

// Exposed for tests: the title in a param.json / param.sfo, or "".
std::string TitleFromParamJson(const std::string& json);
std::string TitleFromParamSfo(const std::string& sfo);

// ---------------------------------------------------------------- game data

// One folder a game owns: /user/app/PPSA01325, .../savedata_prospero/PPSA01325, ...
struct GamePart {
  std::string category;  // "Game", "Update", "Add-ons", "Saves", "Captures", "Metadata", ...
  std::string path;
  std::string volume;    // storage name, "Internal storage"
  uint64_t size = 0;
  uint64_t files = 0;
  bool error = false;    // could not be read completely
};

struct GameTotal {
  std::string id;        // title ID
  uint64_t size = 0;
  uint64_t files = 0;
  std::vector<GamePart> parts;  // biggest first
};

struct GameRoot {
  std::string path;    // where to look: a volume root
  std::string volume;  // its name
};

// A folder that belongs to a game though it is not named after it (a Playden
// game's files, named by store ID).
struct GameFolder {
  std::string id;        // title ID
  std::string category;  // GamePart::category
};

// Finds every folder named after a game's title ID on the given volumes, and
// the folders given by path, and adds them up per game. Runs on its own
// thread; poll Done().
class GameScanner {
 public:
  ~GameScanner();
  void Start(const std::vector<GameRoot>& roots, const std::vector<std::string>& skip,
             const std::map<std::string, GameFolder>& named = {});
  void Cancel() { cancel_ = true; }
  bool Done() const { return done_.load(); }
  std::vector<GameTotal> Take();  // biggest first; valid once Done()

 private:
  static void* ThreadMain(void* self);
  void Find(const std::string& dir, const GameRoot& root, int depth, uint64_t dev);
  void Measure(const std::string& path, uint64_t dev, GamePart* part);

  pthread_t thread_{};
  bool started_ = false;
  std::vector<GameRoot> roots_;
  std::vector<std::string> skip_;
  std::map<std::string, GameFolder> named_;
  std::vector<std::pair<std::string, GamePart>> found_;  // (title ID, part)
  std::vector<GameTotal> result_;
  std::atomic<bool> done_{false};
  std::atomic<bool> cancel_{false};
};

// What a folder holding a title ID's data is for, from where it sits.
std::string GameCategory(const std::string& path);
