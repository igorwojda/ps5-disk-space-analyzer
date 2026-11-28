// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "system.h"

namespace {

#if defined(__PROSPERO__)
const char* const kFile = "/data/disk-space-analyzer/settings.json";
const char* const kTmp = "/data/disk-space-analyzer/settings.json.tmp";
#else
const char* const kFile = nullptr;  // computer: for this run only
const char* const kTmp = nullptr;
#endif

std::mutex g_mu;
// key -> the value as JSON text (true, 12, "text"), written back as it was read.
std::map<std::string, std::string> g_values;

void SkipSpace(const std::string& s, size_t& i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
}

// A JSON string starting at s[i] ('"'), as written, quotes included.
bool ReadString(const std::string& s, size_t& i, std::string& out) {
  size_t start = i++;
  while (i < s.size() && s[i] != '"') i += s[i] == '\\' ? 2 : 1;
  if (i >= s.size()) return false;
  out = s.substr(start, ++i - start);
  return true;
}

// Reads {"key": value, ...}. Values are strings, numbers, true, false or null.
bool Parse(const std::string& s, std::map<std::string, std::string>& out) {
  size_t i = 0;
  SkipSpace(s, i);
  if (i >= s.size() || s[i++] != '{') return false;
  SkipSpace(s, i);
  if (i < s.size() && s[i] == '}') return true;
  while (i < s.size()) {
    std::string key, value;
    if (s[i] != '"' || !ReadString(s, i, key)) return false;
    SkipSpace(s, i);
    if (i >= s.size() || s[i++] != ':') return false;
    SkipSpace(s, i);
    if (i < s.size() && s[i] == '"') {
      if (!ReadString(s, i, value)) return false;
    } else {
      size_t start = i;
      while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' &&
             s[i] != '\t')
        i++;
      value = s.substr(start, i - start);
      if (value.empty()) return false;
    }
    out[key.substr(1, key.size() - 2)] = value;
    SkipSpace(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '}') return true;
    if (s[i++] != ',') return false;
    SkipSpace(s, i);
  }
  return false;
}

// Writes a new file next to the old one, then swaps it in, so a crash never leaves half a file.
// Caller holds g_mu.
void Save() {
  if (!kFile) return;
  FILE* f = std::fopen(kTmp, "w");
  if (!f) {
    Log("settings: cannot write %s", kTmp);
    return;
  }
  std::fputs("{", f);
  bool first = true;
  for (const auto& kv : g_values) {
    std::fprintf(f, "%s\n  \"%s\": %s", first ? "" : ",", kv.first.c_str(), kv.second.c_str());
    first = false;
  }
  std::fputs(g_values.empty() ? "}\n" : "\n}\n", f);
  bool ok = std::fflush(f) == 0;
  ok = std::fclose(f) == 0 && ok;
  if (!ok || std::rename(kTmp, kFile) != 0) {
    Log("settings: cannot save %s", kFile);
    std::remove(kTmp);
  }
}

// Reads the file into g_values; none, or not valid, is every setting at its default.
// Caller holds g_mu.
void Load() {
  if (!kFile) return;  // computer: g_values is all there is
  g_values.clear();
  std::ifstream in(kFile, std::ios::binary);
  if (!in) return;
  std::stringstream ss;
  ss << in.rdbuf();
  std::map<std::string, std::string> values;
  if (!Parse(ss.str(), values)) {
    Log("settings: %s is not valid, using the defaults", kFile);
    return;
  }
  g_values = std::move(values);
}

}  // namespace

std::string SettingsJson() {
  std::lock_guard<std::mutex> lock(g_mu);
  Load();
  std::string o = "{";
  for (const auto& kv : g_values) o += (o.size() > 1 ? ",\"" : "\"") + kv.first + "\":" + kv.second;
  return o + "}";
}

bool SettingBool(const std::string& key, bool def) {
  std::lock_guard<std::mutex> lock(g_mu);
  Load();
  auto it = g_values.find(key);
  if (it == g_values.end()) return def;
  if (it->second == "true") return true;
  if (it->second == "false") return false;
  return def;
}

void SetSettingJson(const std::string& key, const std::string& json) {
  std::lock_guard<std::mutex> lock(g_mu);
  Load();  // keeps edits made to the file by hand
  g_values[key] = json;
  Save();
}

void SetSetting(const std::string& key, bool value) { SetSettingJson(key, value ? "true" : "false"); }
