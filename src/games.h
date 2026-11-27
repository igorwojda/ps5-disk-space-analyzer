// SPDX-License-Identifier: GPL-3.0-or-later
// Game names and icons for folders named after a title ID (PPSA01325, CUSA00207).
#pragma once

#include <string>

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
