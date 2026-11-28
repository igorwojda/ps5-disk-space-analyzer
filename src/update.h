// SPDX-License-Identifier: GPL-3.0-or-later
// Self update from the project's GitHub releases.
//
// A check reads the latest release and compares its tag with APP_VERSION
// (semver). Installing reads the latest release again, so the newest one is
// installed even if it came out after the check, then downloads its .elf and sends it to the ELF
// loader (127.0.0.1:9021, or DSA_LOADER=host:port), then stops this copy so
// the new one gets its port.
#pragma once

#include <cstdint>
#include <string>

struct UpdateStatus {
  // "checking", "none" (up to date), "available", "error" (check failed),
  // "downloading", "installing" (sending to the loader), "restarting",
  // "failed" (install failed; can be tried again)
  std::string state;
  bool checking = false;  // a check is running (a found update stays installable)
  std::string current;  // APP_VERSION
  std::string latest;   // newest release, without the leading "v"
  uint64_t got = 0, size = 0;  // download progress
  std::string error;
};

// Starts a check when none ran recently, or with `check` when the last one
// is over a minute old (the app was opened or brought back). Never blocks on
// the network.
UpdateStatus GetUpdateStatus(bool check = false);

// Starts downloading and installing the newer release. False (with `err`)
// when there is none or an install is already running.
bool StartUpdate(std::string* err);

// <0, 0, >0 like strcmp. Semver order; prerelease parts compare digit runs
// as numbers, so 1.0.0-beta10 is newer than 1.0.0-beta9.
int CompareVersions(const std::string& a, const std::string& b);
