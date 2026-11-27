// SPDX-License-Identifier: MIT
#include "report.h"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <functional>

#include <sys/stat.h>

namespace {

// Entries in a report at most. A full drive can hold a million files; past
// this the smallest files of each folder are added up into one entry, which
// keeps the file a few MB and quick to open.
const uint64_t kMaxEntries = 250000;
const uint64_t kHardMaxEntries = 400000;

// A JSON string safe inside a <script> element: no "<", so no "</script>".
void JStr(std::string& o, const std::string& s) {
  o += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case '<': o += "\\u003c"; break;
      case '>': o += "\\u003e"; break;
      case '&': o += "\\u0026"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += (char)c;
        }
    }
  }
  o += '"';
}

void Num(std::string& o, uint64_t v) { o += std::to_string(v); }

void Count(const Node* n, uint64_t* dirs, uint64_t* files) {
  if (!n->isDir) {
    (*files)++;
    return;
  }
  (*dirs)++;
  for (const Node* c : n->children) Count(c, dirs, files);
}

void FileSizes(const Node* n, std::vector<uint64_t>* out) {
  if (!n->isDir) {
    out->push_back(n->size);
    return;
  }
  for (const Node* c : n->children) FileSizes(c, out);
}

struct Writer {
  std::string out;
  uint64_t min_file = 0;  // files smaller than this are added up per folder
  uint64_t grouped = 0;   // how many files that was
};

// A folder is [name, size, files, [children], flags, title] (flags: 1 another
// volume mounted here, 2 not fully readable; flags and title only when set),
// a file [name, size], and a folder's small files added up [count, size,
// each smaller than this].
void Emit(Writer& w, Node* n) {
  std::string& o = w.out;
  o += '[';
  JStr(o, n->name);
  o += ',';
  Num(o, n->size);
  if (!n->isDir) {
    o += ']';
    return;
  }
  o += ',';
  Num(o, n->files);
  o += ",[";
  bool first = true;
  uint64_t small = 0, small_size = 0;
  for (Node* c : n->children) {
    if (!c->isDir && c->size < w.min_file) {
      small++;
      small_size += c->size;
      continue;
    }
    if (!first) o += ',';
    first = false;
    Emit(w, c);
  }
  if (small) {
    if (!first) o += ',';
    o += '[';
    Num(o, small);
    o += ',';
    Num(o, small_size);
    o += ',';
    Num(o, w.min_file);
    o += ']';
    w.grouped += small;
  }
  o += ']';
  int flags = (n->isMount ? 1 : 0) | (n->error ? 2 : 0);
  std::string title;
  if (IsTitleId(n->name)) title = LookupGame(n->name, n->path()).title;
  if (flags || !title.empty()) {
    o += ',';
    Num(o, (uint64_t)flags);
  }
  if (!title.empty()) {
    o += ',';
    JStr(o, title);
  }
  o += ']';
}

bool InScope(const std::string& path, const std::vector<Storage>& scope) {
  for (const Storage& st : scope) {
    const std::string& r = st.path;
    if (path == r) return true;
    if (r == "/" ? path.size() > 1 && path[0] == '/'
                 : path.size() > r.size() && path.compare(0, r.size(), r) == 0 && path[r.size()] == '/')
      return true;
  }
  return false;
}

// Games with their folders on the drives in the report, biggest first.
void EmitGames(std::string& o, const std::vector<GameTotal>& games, const std::vector<Storage>& scope) {
  std::vector<GameTotal> kept;
  for (const GameTotal& gt : games) {
    GameTotal k;
    k.id = gt.id;
    for (const GamePart& p : gt.parts)
      if (InScope(p.path, scope)) {
        k.parts.push_back(p);
        k.size += p.size;
        k.files += p.files;
      }
    if (!k.parts.empty()) kept.push_back(std::move(k));
  }
  std::stable_sort(kept.begin(), kept.end(), [](const GameTotal& a, const GameTotal& b) { return a.size > b.size; });
  o += '[';
  for (size_t i = 0; i < kept.size(); i++) {
    const GameTotal& gt = kept[i];
    GameInfo gi = LookupGame(gt.id, gt.parts[0].path);
    if (i) o += ',';
    o += "{\"id\":";
    JStr(o, gt.id);
    o += ",\"title\":";
    JStr(o, gi.title);
    o += ",\"size\":";
    Num(o, gt.size);
    o += ",\"files\":";
    Num(o, gt.files);
    o += ",\"parts\":[";
    for (size_t k = 0; k < gt.parts.size(); k++) {
      const GamePart& p = gt.parts[k];
      if (k) o += ',';
      o += "{\"cat\":";
      JStr(o, p.category);
      o += ",\"path\":";
      JStr(o, p.path);
      o += ",\"volume\":";
      JStr(o, p.volume);
      o += ",\"size\":";
      Num(o, p.size);
      o += ",\"files\":";
      Num(o, p.files);
      o += '}';
    }
    o += "]}";
  }
  o += ']';
}

}  // namespace

std::string ReportDriveJson(const ReportDrive& in) {
  Writer w;
  uint64_t dirs = 0, files = 0;
  Count(in.root, &dirs, &files);
  if (dirs + files > kMaxEntries) {
    // Keep the biggest files: everything above the size of the budget-th one.
    uint64_t budget = std::max<uint64_t>(20000, kMaxEntries > dirs ? kMaxEntries - dirs : 0);
    std::vector<uint64_t> sizes;
    sizes.reserve((size_t)files);
    FileSizes(in.root, &sizes);
    if (budget < sizes.size()) {
      std::nth_element(sizes.begin(), sizes.begin() + (long)(budget - 1), sizes.end(), std::greater<uint64_t>());
      uint64_t t = sizes[(size_t)(budget - 1)];
      // Many files share a size (one disk block): keep those of that size too
      // while the report stays under the hard limit, else drop them all.
      uint64_t keep = (uint64_t)std::count_if(sizes.begin(), sizes.end(), [t](uint64_t s) { return s >= t; });
      w.min_file = t && keep + dirs <= kHardMaxEntries ? t : t + 1;
    }
  }
  w.out.reserve((size_t)(std::min<uint64_t>(dirs + files, kMaxEntries) * 48 + 1024));
  std::string& d = w.out;
  d += "{\"storage\":{\"name\":";
  JStr(d, in.storage.name);
  d += ",\"path\":";
  JStr(d, in.storage.path);
  d += ",\"kind\":";
  JStr(d, in.storage.kind);
  d += ",\"total\":";
  Num(d, in.storage.total);
  d += ",\"free\":";
  Num(d, in.storage.free);
  d += "},\"scanned\":";
  Num(d, (uint64_t)in.scanned);
  d += ",\"folders\":";
  Num(d, dirs);
  d += ",\"groupedBelow\":";
  Num(d, w.min_file);
  d += ",\"tree\":";
  Emit(w, in.root);
  d += ",\"grouped\":";
  Num(d, w.grouped);
  d += '}';
  return std::move(w.out);
}

std::string ReportHtml(const std::string& tmpl, const std::string& title, const std::vector<std::string>& drives,
                       const std::vector<Storage>& scope, const std::vector<GameTotal>& games, bool games_ready) {
  size_t n = 4096;
  for (const std::string& dj : drives) n += dj.size() + 1;
  std::string d;
  d.reserve(n);
  d += "{\"app\":\"Disk Space Analyzer\",\"version\":";
  JStr(d, APP_VERSION);
  d += ",\"title\":";
  JStr(d, title);
  d += ",\"created\":";
  Num(d, (uint64_t)time(nullptr));
  d += ",\"gamesReady\":";
  d += games_ready ? "true" : "false";
  d += ",\"games\":";
  EmitGames(d, games, scope);
  d += ",\"drives\":[";
  for (size_t i = 0; i < drives.size(); i++) {
    if (i) d += ',';
    d += drives[i];
  }
  d += "]}";

  static const std::string kMark = "/*REPORT_DATA*/";
  size_t at = tmpl.find(kMark);
  if (at == std::string::npos) return tmpl;
  std::string html;
  html.reserve(tmpl.size() + d.size());
  html.append(tmpl, 0, at);
  html += d;
  html.append(tmpl, at + kMark.size(), std::string::npos);
  return html;
}

std::string ReportFileName(const std::string& title, time_t t) {
  std::string slug;
  for (unsigned char c : title) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) slug += (char)c;
    else if (c >= 'A' && c <= 'Z') slug += (char)(c - 'A' + 'a');
    else if (!slug.empty() && slug.back() != '-') slug += '-';
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug.empty()) slug = "storage";
  struct tm tm {};
  localtime_r(&t, &tm);
  char date[32];
  std::strftime(date, sizeof date, "%Y-%m-%d-%H%M", &tm);
  return "disk-space-report-" + slug + "-" + date + ".html";
}

std::vector<ReportTarget> ReportTargets(const std::vector<std::string>& extra) {
  std::vector<ReportTarget> out;
  auto writable = [](const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && access(p.c_str(), W_OK) == 0;
  };
  for (const Storage& s : ListStorages(extra)) {
#if defined(__PROSPERO__)
    if (s.kind != "usb") continue;  // M.2 and extended drives are not readable on a computer
#else
    if (s.kind == "system") continue;
#endif
    if (writable(s.path)) out.push_back({s.name, s.path, s.kind});
  }
#if defined(__PROSPERO__)
  // /data: reachable over FTP and from file managers.
  if (writable("/user/data")) out.push_back({"Console data folder (/data)", "/user/data", "internal"});
#endif
  return out;
}
