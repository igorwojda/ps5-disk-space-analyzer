// SPDX-License-Identifier: MIT
#include "games.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>

#include "platform.h"

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

// ---------------------------------------------------------------- game data

namespace {

// How deep below a volume root a title ID folder can sit:
// /user/av_contents/photo/NPXS40087/<ID> is the deepest known.
const int kMaxDepth = 5;

std::vector<std::string> Segments(const std::string& path) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t s = path.find('/', pos);
    if (s == std::string::npos) s = path.size();
    if (s > pos) out.push_back(path.substr(pos, s - pos));
    pos = s + 1;
  }
  return out;
}

// System apps (NPXS40087 holds every capture) and trophy sets are not games.
bool IsGamePrefix(const std::string& s, size_t i) {
  return s.compare(i, 4, "NPXS") != 0 && s.compare(i, 4, "NPWR") != 0;
}
bool IsGameId(const std::string& name) { return IsTitleId(name) && IsGamePrefix(name, 0); }

bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAlnum(char c) { return IsUpper(c) || IsDigit(c) || (c >= 'a' && c <= 'z'); }

// A title ID inside a longer name, as game dumps are named:
// "PPSA02100-app", "Indiana Jones [PPSA26786]", "PPSA05496 [PS5] LEGO ... .exfat".
std::string EmbeddedGameId(const std::string& n) {
  for (size_t i = 0; i + 9 <= n.size(); i++) {
    if (i > 0 && IsAlnum(n[i - 1])) continue;
    if (i + 9 < n.size() && IsAlnum(n[i + 9])) continue;
    std::string id = n.substr(i, 9);
    if (IsGameId(id)) return id;
  }
  return "";
}

}  // namespace

std::string GameCategory(const std::string& path) {
  std::vector<std::string> seg = Segments(path);
  if (seg.size() < 2) return "Other";
  const std::string& parent = seg[seg.size() - 2];
  for (size_t i = 0; i + 1 < seg.size(); i++) {
    if (seg[i] == "av_contents") return "Captures";
    if (seg[i].compare(0, 8, "savedata") == 0 || seg[i] == "np_cloud_save") return "Saves";
    if (seg[i] == "appmeta") return "Metadata";
  }
  if (parent == "app") return "Game";
  if (parent == "patch") return "Update";
  if (parent == "addcont") return "Add-ons";
  if (parent == "homebrew") return "Game dump";
  if (parent == "download") return "Downloads";
  return "Other";
}

GameScanner::~GameScanner() {
  Cancel();
  if (started_) pthread_join(thread_, nullptr);
}

void GameScanner::Start(const std::vector<GameRoot>& roots, const std::vector<std::string>& skip) {
  roots_ = roots;
  skip_ = skip;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
  started_ = pthread_create(&thread_, &attr, ThreadMain, this) == 0;
  pthread_attr_destroy(&attr);
  if (!started_) ThreadMain(this);
}

std::vector<GameTotal> GameScanner::Take() { return std::move(result_); }

void* GameScanner::ThreadMain(void* p) {
  auto* self = static_cast<GameScanner*>(p);
  for (const GameRoot& r : self->roots_) {
    struct stat st;
    if (lstat(r.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) self->Find(r.path, r, 0, (uint64_t)st.st_dev);
  }
  std::map<std::string, GameTotal> by_id;
  for (auto& f : self->found_) {
    GameTotal& g = by_id[f.first];
    g.id = f.first;
    g.size += f.second.size;
    g.files += f.second.files;
    g.parts.push_back(std::move(f.second));
  }
  self->found_.clear();
  for (auto& kv : by_id) {
    GameTotal& g = kv.second;
    // Leftover home screen data of a game that is gone is not worth a row.
    bool only_meta = std::all_of(g.parts.begin(), g.parts.end(),
                                 [](const GamePart& p) { return p.category == "Metadata"; });
    if (only_meta) continue;
    std::sort(g.parts.begin(), g.parts.end(),
              [](const GamePart& a, const GamePart& b) { return a.size > b.size; });
    self->result_.push_back(std::move(g));
  }
  std::sort(self->result_.begin(), self->result_.end(), [](const GameTotal& a, const GameTotal& b) {
    return a.size != b.size ? a.size > b.size : a.id < b.id;
  });
  self->done_ = true;
  return nullptr;
}

void GameScanner::Find(const std::string& dir, const GameRoot& root, int depth, uint64_t dev) {
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  std::vector<std::string> subdirs;
  while (struct dirent* e = readdir(d)) {
    const char* nm = e->d_name;
    if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
    if (e->d_type == DT_DIR || e->d_type == DT_UNKNOWN || e->d_type == DT_REG) subdirs.push_back(nm);
  }
  closedir(d);
  for (const std::string& nm : subdirs) {
    if (cancel_) return;
    std::string path = dir.back() == '/' ? dir + nm : dir + "/" + nm;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || (uint64_t)st.st_dev != dev) continue;
    bool dir = S_ISDIR(st.st_mode);
    if (!dir && !S_ISREG(st.st_mode)) continue;
    if (std::find(skip_.begin(), skip_.end(), path) != skip_.end()) continue;
    // A folder named after the game, or a dump (folder or disk image) with
    // the title ID in its name.
    std::string id = dir && IsGameId(nm) ? nm : EmbeddedGameId(nm);
    if (!id.empty()) {
      GamePart part;
      part.category = id == nm ? GameCategory(path)
                      : path.find("/playgo/") != std::string::npos ? "Install data"  // EP9000-CUSA00207_00-...
                      : "Game dump";
      part.path = path;
      part.volume = root.volume;
      part.size = DiskBytes(st);
      if (dir) Measure(path, dev, &part);
      else part.files = 1;
      found_.emplace_back(id, std::move(part));
    } else if (dir && depth + 1 < kMaxDepth) {
      Find(path, root, depth + 1, dev);
    }
  }
}

void GameScanner::Measure(const std::string& path, uint64_t dev, GamePart* part) {
  DIR* d = opendir(path.c_str());
  if (!d) {
    part->error = true;
    return;
  }
  while (struct dirent* e = readdir(d)) {
    if (cancel_) break;
    const char* nm = e->d_name;
    if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
    std::string child = path + "/" + nm;
    struct stat st;
    if (lstat(child.c_str(), &st) != 0) {
      part->error = true;
      continue;
    }
    part->size += DiskBytes(st);
    if (S_ISDIR(st.st_mode)) {
      if ((uint64_t)st.st_dev == dev) Measure(child, dev, part);
    } else {
      part->files++;
    }
  }
  closedir(d);
}
