// SPDX-License-Identifier: MIT
// Deleting what the console or Playden keeps a record of.
//
// A game's main folder (/user/app/<TITLE ID>) is uninstalled through the
// system, as the home screen does, so app.db and the home screen forget it;
// deleting the folder by hand leaves a tile that no longer starts.
//
// Playden (github.com/leolobato/playden-ps5) runs Windows games, each as its
// own PS5 title FAKE78xxx registered from /data/homebrew/FAKE78xxx, with the
// game's files in <drive>/playden/games/<game> (/data/playden-games on
// internal storage). It keeps its own list of installed games: when a game's
// files go, it is asked to forget the install through its request file, or it
// shows the game as waiting for a drive.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Where titles and Playden's games live. PS5 paths on the console; on a
// computer, the same layout below $DSA_CONSOLE_ROOT (for testing), else empty.
struct ConsoleLayout {
  std::vector<std::string> title_bases;    // /user, /mnt/ext1/user: their app/, patch/, addcont/, appmeta/
  std::string homebrew;                    // /user/data/homebrew
  std::string playden_app;                 // /user/data/homebrew/FAKE77031: Playden itself
  std::string playden_bottles;             // /user/data/playden/games: each game's Windows folder and saves
  std::vector<std::string> playden_games;  // /user/data/playden-games, /mnt/ext1/playden/games, ...
};
const ConsoleLayout& Console();

// FAKE78 and three digits: one of Playden's game titles.
bool IsPlaydenTitle(const std::string& id);

// Playden install id (steam-2610, gog-1207658695, local-<uuid>) -> its PS5
// title (FAKE78024), from <homebrew>/<TITLE>/.playden-title-owner.
using PlaydenTitles = std::map<std::string, std::string>;
PlaydenTitles ReadPlaydenTitles(const std::string& homebrew);
// Exposed for tests: install_id and title_id of a marker's text.
std::pair<std::string, std::string> ParsePlaydenOwner(const std::string& text);

// The install id a folder in a Playden games folder holds ("2610" ->
// "steam-2610"; gog-..., local-... as they are), "" for any other name.
std::string PlaydenInstallId(const std::string& folder_name);

// The Playden game a folder holds: its files or its bottle. title is "" when
// the path is neither, or Playden has no title for the game.
struct PlaydenFolder {
  std::string title;     // FAKE78024
  std::string category;  // "Game" (its files) or "Saves" (its bottle)
};
PlaydenFolder PlaydenFolderOf(const std::string& path, const PlaydenTitles& titles);

// What deleting a path takes besides removing its files.
struct DeletePlan {
  std::string refuse;             // not deleted: why
  std::string uninstall;          // title to uninstall through the system first
  std::vector<std::string> also;  // deleted with it once the title is gone
  std::string playden_forget;     // asks Playden to forget this game once the files are gone
  // What the uninstall removes by itself (its update, add-ons, metadata),
  // and the space they take as far as it is known.
  std::vector<std::string> system_removes;
  uint64_t system_bytes = 0;
};
DeletePlan PlanDelete(const std::string& path, const PlaydenTitles& titles);

// True while any copy of the title's app folder is on the console.
bool TitleListed(const std::string& id);

// Uninstalls a title as the home screen does and waits (up to a minute) for
// the console to finish. "" when it is gone, else why not.
std::string UninstallTitle(const std::string& id);

// Adds "uninstall <game>" to Playden's request file, which Playden reads the
// next time it starts. True when written, or when Playden is not installed.
bool AskPlaydenToForget(const std::string& game);

// The title whose uninstall removes `path` (its app, update, add-on or home
// screen folder), or "".
std::string RemovedWithTitle(const std::string& path);

// Non-empty when deleting `path` uninstalls a title or tells Playden.
std::string UninstallWarning(const std::string& path);
