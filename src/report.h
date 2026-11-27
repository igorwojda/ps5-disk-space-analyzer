// SPDX-License-Identifier: GPL-3.0-or-later
// A shareable report of one or more drives: one HTML file with each folder
// tree, the games and the space each takes, that opens in any browser.
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "games.h"
#include "platform.h"
#include "tree.h"

// One scanned drive.
struct ReportDrive {
  Node* root = nullptr;  // the scanned tree
  Storage storage;       // what was scanned
  time_t scanned = 0;
};

// One drive's part of a report (JSON). Built as soon as a drive is scanned,
// so its tree can be freed before the next one is scanned.
std::string ReportDriveJson(const ReportDrive& d);

// The report page: `tmpl` (web/report.html) with the drives and the games
// that have folders on them (`scope`: the drives, in the same order).
// `title` names what it covers: "Internal storage", "All drives".
std::string ReportHtml(const std::string& tmpl, const std::string& title, const std::vector<std::string>& drives,
                       const std::vector<Storage>& scope, const std::vector<GameTotal>& games, bool games_ready);

// "disk-space-report-internal-storage-2026-11-27-1305.html" for the title "Internal storage"
std::string ReportFileName(const std::string& title, time_t t);

// A folder a report can be saved in.
struct ReportTarget {
  std::string name;  // "USB drive 1"
  std::string path;
  std::string kind;  // storage kind, for the hint shown after saving
};

// Where reports can be saved: USB drives and the console's data folder on a
// PS5; any listed storage but the system root on a computer.
std::vector<ReportTarget> ReportTargets(const std::vector<std::string>& extra);
