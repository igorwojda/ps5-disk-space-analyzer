// SPDX-License-Identifier: GPL-3.0-or-later
#include "games.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>

namespace {

std::mutex g_mu;
std::map<std::string, GameInfo> g_cache;

std::string ReadSmallFile(const std::string& path, size_t limit = 256 * 1024) {
  std::string s;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return s;
  char buf[8192];
  size_t n;
  while (s.size() < limit && (n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  std::fclose(f);
  return s;
}

bool IsFile(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void AppendUtf8(std::string& o, uint32_t c) {
  if (c < 0x80) {
    o += (char)c;
  } else if (c < 0x800) {
    o += (char)(0xC0 | (c >> 6));
    o += (char)(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    o += (char)(0xE0 | (c >> 12));
    o += (char)(0x80 | ((c >> 6) & 0x3F));
    o += (char)(0x80 | (c & 0x3F));
  } else {
    o += (char)(0xF0 | (c >> 18));
    o += (char)(0x80 | ((c >> 12) & 0x3F));
    o += (char)(0x80 | ((c >> 6) & 0x3F));
    o += (char)(0x80 | (c & 0x3F));
  }
}

bool Hex4(const std::string& s, size_t i, uint32_t* out) {
  if (i + 4 > s.size()) return false;
  uint32_t v = 0;
  for (size_t k = i; k < i + 4; k++) {
    char c = s[k];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return false;
  }
  *out = v;
  return true;
}

// The JSON string starting at s[i] (after optional whitespace), unescaped.
std::string JsonString(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
  std::string o;
  if (i >= s.size() || s[i] != '"') return o;
  for (i++; i < s.size() && s[i] != '"'; i++) {
    if (s[i] != '\\') {
      o += s[i];
      continue;
    }
    if (++i >= s.size()) break;
    switch (s[i]) {
      case 'n': o += '\n'; break;
      case 't': o += '\t'; break;
      case 'r': o += '\r'; break;
      case 'b': o += '\b'; break;
      case 'f': o += '\f'; break;
      case 'u': {
        uint32_t c;
        if (!Hex4(s, i + 1, &c)) return o;
        i += 4;
        uint32_t lo;
        if (c >= 0xD800 && c < 0xDC00 && i + 2 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u' &&
            Hex4(s, i + 3, &lo) && lo >= 0xDC00 && lo < 0xE000) {
          c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
          i += 6;
        }
        AppendUtf8(o, c);
        break;
      }
      default: o += s[i];  // \" \\ \/
    }
  }
  return o;
}

// Position just after `"key":`, searching from `from`, or npos.
size_t FindKey(const std::string& s, const std::string& key, size_t from) {
  std::string q = "\"" + key + "\"";
  for (size_t p = s.find(q, from); p != std::string::npos; p = s.find(q, p + 1)) {
    size_t i = p + q.size();
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
    if (i < s.size() && s[i] == ':') return i + 1;
  }
  return std::string::npos;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

uint32_t Le32(const std::string& s, size_t i) {
  return (uint32_t)(uint8_t)s[i] | (uint32_t)(uint8_t)s[i + 1] << 8 | (uint32_t)(uint8_t)s[i + 2] << 16 |
         (uint32_t)(uint8_t)s[i + 3] << 24;
}
uint16_t Le16(const std::string& s, size_t i) {
  return (uint16_t)((uint8_t)s[i] | (uint8_t)s[i + 1] << 8);
}

}  // namespace

bool IsTitleId(const std::string& n) {
  if (n.size() != 9) return false;
  for (int i = 0; i < 4; i++)
    if (n[i] < 'A' || n[i] > 'Z') return false;
  for (int i = 4; i < 9; i++)
    if (n[i] < '0' || n[i] > '9') return false;
  return true;
}

std::string TitleFromParamJson(const std::string& s) {
  size_t lp = FindKey(s, "localizedParameters", 0);
  if (lp != std::string::npos) {
    size_t dl = FindKey(s, "defaultLanguage", lp);
    std::string lang = dl == std::string::npos ? "" : JsonString(s, dl);
    for (const std::string& l : {lang, std::string("en-US")}) {
      if (l.empty()) continue;
      size_t k = FindKey(s, l, lp);
      if (k == std::string::npos) continue;
      size_t t = FindKey(s, "titleName", k);
      std::string title = t == std::string::npos ? "" : Trim(JsonString(s, t));
      if (!title.empty()) return title;
    }
  }
  size_t t = FindKey(s, "titleName", lp == std::string::npos ? 0 : lp);
  return t == std::string::npos ? "" : Trim(JsonString(s, t));
}

// PARAM.SFO: header (magic "\0PSF", version, key table, data table, count),
// then 16-byte entries (key offset u16, format u16, length, max length, data offset).
std::string TitleFromParamSfo(const std::string& s) {
  if (s.size() < 20 || s.compare(0, 4, std::string("\0PSF", 4)) != 0) return "";
  uint32_t keys = Le32(s, 8), data = Le32(s, 12), count = Le32(s, 16);
  for (uint32_t e = 0; e < count; e++) {
    size_t at = 20 + (size_t)e * 16;
    if (at + 16 > s.size()) break;
    size_t key = (size_t)keys + Le16(s, at);
    if (key >= s.size() || s.compare(key, 6, std::string("TITLE\0", 6)) != 0) continue;
    if (Le16(s, at + 2) != 0x0204) return "";  // not a UTF-8 string
    size_t off = (size_t)data + Le32(s, at + 12), len = Le32(s, at + 4);
    if (off >= s.size()) return "";
    std::string v = s.substr(off, std::min(len, s.size() - off));
    size_t z = v.find('\0');
    return Trim(z == std::string::npos ? v : v.substr(0, z));
  }
  return "";
}

GameInfo LookupGame(const std::string& id, const std::string& folder) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_cache.find(id);
  if (it != g_cache.end()) return it->second;

  // System copy first (every installed title), then the home screen's copy
  // (has the icon; external/ for extended storage), then homebrew.
  const std::string dirs[] = {
      "/system_data/priv/appmeta/" + id,
      "/user/appmeta/" + id,
      "/user/appmeta/external/" + id,
      "/system_data/priv/appmeta/external/" + id,
      "/user/app/" + id + "/sce_sys",
      "/data/homebrew/" + id + "/sce_sys",
      folder + "/sce_sys",
  };
  GameInfo info;
  for (const std::string& d : dirs) {
    if (info.title.empty()) info.title = TitleFromParamJson(ReadSmallFile(d + "/param.json"));
    if (info.title.empty()) info.title = TitleFromParamSfo(ReadSmallFile(d + "/param.sfo"));
    if (info.icon.empty() && IsFile(d + "/icon0.png")) info.icon = d + "/icon0.png";
    if (!info.title.empty() && !info.icon.empty()) break;
  }
  g_cache.emplace(id, info);
  return info;
}

void ClearGameCache() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_cache.clear();
}
