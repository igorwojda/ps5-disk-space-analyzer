// SPDX-License-Identifier: GPL-3.0-or-later
// Where to scan and what must never be deleted.
#pragma once

#include <sys/stat.h>

#include <cstdint>
#include <string>
#include <vector>

struct Storage {
  std::string name;    // "Internal storage"
  std::string path;    // "/user"
  std::string kind;    // icon: "internal", "usb", "ext", "system", "folder"
  uint64_t total = 0;
  uint64_t free = 0;
};

// Storages that exist right now, plus any extra paths given on the command line.
std::vector<Storage> ListStorages(const std::vector<std::string>& extra);

// Refresh free/total of one path. Returns false when it is not mounted.
bool QuerySpace(const std::string& path, uint64_t* total, uint64_t* free);

// Directories a scan never descends into (pseudo file systems, mirrors).
std::vector<std::string> SkipList();

// True when a path must not be selected for deletion.
bool IsProtected(const std::string& path);

// Non-empty when deleting `path` has a side effect worth a warning.
std::string DeleteWarning(const std::string& path);

// Space a file or folder takes on disk: what deleting it gives back.
uint64_t DiskBytes(const struct stat& st);
