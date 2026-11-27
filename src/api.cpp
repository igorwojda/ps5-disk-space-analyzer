// SPDX-License-Identifier: MIT
#include "api.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

#include "games.h"
#include "platform.h"
#include "report.h"
#include "system.h"
#include "tree.h"
#include "web_assets.h"

namespace {

// ---------------------------------------------------------------- state

enum class Phase { Idle, Scanning, Ready, Deleting, Moving };

struct State {
  std::mutex mu;
  Phase phase = Phase::Idle;
  Storage storage;
  std::unique_ptr<Scanner> scanner;
  Node* root = nullptr;
  Index index;
  std::vector<Node*> biggest;
  std::unique_ptr<Deleter> deleter;
  uint64_t delete_bytes = 0;
  std::unique_ptr<Mover> mover;
  std::string move_dest;
  uint64_t move_bytes = 0;
  // Last finished delete or move, shown once by the UI.
  int result_seq = 0;
  std::string result_op = "delete";
  uint64_t result_freed = 0, result_errors = 0, result_removed = 0, result_total = 0;
  std::string error;  // last scan error
  time_t scanned = 0;  // when the scan finished
  // Games tab: every game's folders on every volume, counted on their own thread.
  std::unique_ptr<GameScanner> game_scanner;
  std::vector<GameTotal> games;
  bool games_ready = false;
  // Report export: scans the chosen drives one by one, then builds the page.
  struct ReportJob {
    std::vector<Storage> scope;       // the drives, in report order
    std::vector<std::string> drives;  // each drive's part once scanned
    std::vector<size_t> todo;         // drives still to scan
    size_t current = 0;               // the drive being scanned
    std::unique_ptr<Scanner> scanner;
    std::string title;                // "All drives", "Internal storage"
    std::string html, name;           // the page and its file name, when done
  };
  std::unique_ptr<ReportJob> report;
};

State g;
std::string g_web_dir;
std::vector<std::string> g_extra;  // extra folders offered as storages
unsigned short g_port = 0;

// ---------------------------------------------------------------- json

std::string Esc(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
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
  return o;
}

std::string Str(const std::string& s) { return "\"" + Esc(s) + "\""; }
std::string Num(uint64_t v) { return std::to_string(v); }
const char* Bool(bool b) { return b ? "true" : "false"; }

std::string StorageJson(const Storage& s) {
  return "{\"name\":" + Str(s.name) + ",\"path\":" + Str(s.path) + ",\"kind\":" + Str(s.kind) +
         ",\"total\":" + Num(s.total) + ",\"free\":" + Num(s.free) + "}";
}

std::string JoinPath(const std::string& base, const std::string& name) {
  return base.empty() || base.back() == '/' ? base + name : base + "/" + name;
}

// A folder named after a title ID: the game's name, and whether it has an icon.
void GameFields(std::ostringstream& o, Node* n, const std::string& path) {
  if (!n->isDir || !IsTitleId(n->name)) return;
  GameInfo gi = LookupGame(n->name, path);
  if (!gi.title.empty()) o << ",\"title\":" << Str(gi.title);
  if (!gi.icon.empty()) o << ",\"icon\":true";
}

// Fields shared by list rows and biggest files.
void ItemFields(std::ostringstream& o, Node* n, const std::string& path) {
  o << "\"id\":" << n->id << ",\"name\":" << Str(n->name) << ",\"path\":" << Str(path)
    << ",\"size\":" << n->size << ",\"files\":" << n->files << ",\"dir\":" << Bool(n->isDir)
    << ",\"mount\":" << Bool(n->isMount) << ",\"error\":" << Bool(n->error)
    << ",\"protected\":" << Bool(n->isMount || IsProtected(path));
  std::string w = DeleteWarning(path);
  if (!w.empty()) o << ",\"warn\":" << Str(w);
  GameFields(o, n, path);
}

// Chart rings: only what is big enough to see, a few levels deep.
void Rings(std::ostringstream& o, Node* n, int depth, uint64_t min_size) {
  o << "[";
  int count = 0;
  for (Node* c : n->children) {
    if (c->size < min_size || count >= 150) break;  // sorted: the rest are smaller
    if (count++) o << ",";
    o << "{\"id\":" << c->id << ",\"name\":" << Str(c->name) << ",\"size\":" << c->size
      << ",\"dir\":" << Bool(c->isDir);
    if (c->isDir && IsTitleId(c->name)) {
      GameInfo gi = LookupGame(c->name, c->path());
      if (!gi.title.empty()) o << ",\"title\":" << Str(gi.title);
    }
    if (depth > 1 && c->isDir && !c->isMount && !c->children.empty()) {
      o << ",\"kids\":";
      Rings(o, c, depth - 1, min_size);
    }
    o << "}";
  }
  o << "]";
}

std::string NodeJson(Node* n) {
  std::ostringstream o;
  std::string path = n->path();
  o << "{";
  ItemFields(o, n, path);
  o << ",\"parent\":" << (n->parent ? (long long)n->parent->id : -1LL);
  // Breadcrumb from the scan root.
  std::vector<Node*> chain;
  for (Node* a = n; a; a = a->parent) chain.push_back(a);
  std::reverse(chain.begin(), chain.end());
  o << ",\"crumbs\":[";
  for (size_t i = 0; i < chain.size(); i++) {
    o << (i ? "," : "") << "{\"id\":" << chain[i]->id << ",\"name\":" << Str(chain[i]->name);
    if (i && IsTitleId(chain[i]->name)) {
      GameInfo gi = LookupGame(chain[i]->name, chain[i]->path());
      if (!gi.title.empty()) o << ",\"title\":" << Str(gi.title);
    }
    o << "}";
  }
  o << "]";
  // Every child for the list (capped for giant folders).
  o << ",\"children\":[";
  size_t shown = std::min<size_t>(n->children.size(), 5000);
  for (size_t i = 0; i < shown; i++) {
    Node* c = n->children[i];
    o << (i ? "," : "") << "{";
    ItemFields(o, c, JoinPath(path, c->name));
    o << "}";
  }
  o << "],\"more\":" << (n->children.size() - shown);
  // Rings: about half a degree is the smallest visible segment.
  o << ",\"rings\":";
  Rings(o, n, 4, std::max<uint64_t>(1, n->size / 720));
  o << "}";
  return o.str();
}

std::string BiggestJson() {
  std::ostringstream o;
  o << "[";
  for (size_t i = 0; i < g.biggest.size(); i++) {
    Node* n = g.biggest[i];
    std::string parent = n->parent ? n->parent->path() : "";
    o << (i ? "," : "") << "{";
    ItemFields(o, n, JoinPath(parent, n->name));
    o << ",\"folder\":" << Str(parent) << ",\"parentId\":" << (n->parent ? n->parent->id : 0);
    // Ancestors below the root, so the chart can light up the path.
    std::vector<uint32_t> chain;
    for (Node* a = n; a && a != g.root; a = a->parent) chain.push_back(a->id);
    std::reverse(chain.begin(), chain.end());
    o << ",\"chain\":[";
    for (size_t k = 0; k < chain.size(); k++) o << (k ? "," : "") << chain[k];
    o << "]}";
  }
  o << "]";
  return o.str();
}

// The node for `path` in the scanned tree, or null when it is not in it.
Node* FindNode(Node* root, const std::string& path) {
  if (!root) return nullptr;
  const std::string& rp = root->name;
  std::string rest;
  if (path == rp) return root;
  if (rp == "/") rest = path.substr(1);
  else if (path.compare(0, rp.size(), rp) == 0 && path.size() > rp.size() && path[rp.size()] == '/')
    rest = path.substr(rp.size() + 1);
  else return nullptr;
  Node* n = root;
  size_t pos = 0;
  while (n && pos < rest.size()) {
    size_t s = rest.find('/', pos);
    if (s == std::string::npos) s = rest.size();
    std::string name = rest.substr(pos, s - pos);
    Node* next = nullptr;
    for (Node* c : n->children)
      if (c->name == name) next = c;
    n = next;
    pos = s + 1;
  }
  return n;
}

// True when `path` is strictly inside the folder `dir`.
bool IsInside(const std::string& path, const std::string& dir) {
  if (dir == "/") return path.size() > 1 && path[0] == '/';
  return path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/';
}

const GamePart* FindGamePart(const std::string& path) {
  for (const GameTotal& gt : g.games)
    for (const GamePart& part : gt.parts)
      if (part.path == path) return &part;
  return nullptr;
}

// A cut item to paste, given by path: it may come from an earlier scan of
// another storage. Only an existing, unprotected entry inside a listed storage
// other than the system partition (that one only while it is the scanned
// storage, as for delete), never a volume mounted there.
bool CutSourceOk(const std::string& path) {
  if (path.size() < 2 || path[0] != '/' || path.back() == '/') return false;
  if (path.find("/../") != std::string::npos || path.find("/./") != std::string::npos) return false;
  if (path.size() >= 3 && (path.compare(path.size() - 3, 3, "/..") == 0 || path.compare(path.size() - 2, 2, "/.") == 0))
    return false;
  if (IsProtected(path)) return false;
  bool listed = false;
  for (const Storage& st : ListStorages(g_extra))
    if (st.kind != "system" && IsInside(path, st.path)) listed = true;
  if (!listed && FindNode(g.root, path)) listed = true;
  if (!listed) return false;
  size_t slash = path.find_last_of('/');
  std::string parent = slash == 0 ? "/" : path.substr(0, slash);
  struct stat st, pst;
  return lstat(path.c_str(), &st) == 0 && stat(parent.c_str(), &pst) == 0 && st.st_dev == pst.st_dev;
}

// What a delete or move request selected: nodes of the scanned tree by id
// (ids=1,2,3) and game folders on any volume by path (paths=a\nb). A move also
// takes cut items by path (srcs=a\nb, with their sizes from the scan they were
// cut in as sizes=1,2). Protected items are dropped, and so is anything inside
// another selected folder.
std::vector<PathTarget> SelectedLocked(const Request& req, bool cut = false) {
  std::vector<std::string> paths;
  std::vector<std::pair<std::string, uint64_t>> hints;  // cut path -> size
  auto it = req.query.find("ids");
  std::string ids = it == req.query.end() ? "" : it->second;
  size_t pos = 0;
  while (pos < ids.size()) {
    size_t c = ids.find(',', pos);
    if (c == std::string::npos) c = ids.size();
    Node* n = g.index.Get((uint32_t)std::strtoul(ids.substr(pos, c - pos).c_str(), nullptr, 10));
    pos = c + 1;
    if (!n || !n->parent || n->isMount || IsProtected(n->path())) continue;  // never the root
    paths.push_back(n->path());
  }
  it = req.query.find("paths");
  std::string plist = it == req.query.end() ? "" : it->second;
  pos = 0;
  while (pos < plist.size()) {
    size_t c = plist.find('\n', pos);
    if (c == std::string::npos) c = plist.size();
    std::string path = plist.substr(pos, c - pos);
    pos = c + 1;
    // Only folders the game count found, never an arbitrary path.
    if (FindGamePart(path) && !IsProtected(path)) paths.push_back(path);
  }
  if (cut) {
    it = req.query.find("srcs");
    std::string slist = it == req.query.end() ? "" : it->second;
    it = req.query.find("sizes");
    std::string sizes = it == req.query.end() ? "" : it->second;
    size_t spos = 0;
    pos = 0;
    while (pos < slist.size()) {
      size_t c = slist.find('\n', pos);
      if (c == std::string::npos) c = slist.size();
      std::string path = slist.substr(pos, c - pos);
      pos = c + 1;
      size_t sc = sizes.find(',', spos);
      if (sc == std::string::npos) sc = sizes.size();
      uint64_t size = spos < sizes.size() ? std::strtoull(sizes.substr(spos, sc - spos).c_str(), nullptr, 10) : 0;
      spos = sc + 1;
      if (!CutSourceOk(path)) continue;
      paths.push_back(path);
      hints.push_back({path, size});
    }
  }
  std::vector<PathTarget> top;
  for (const std::string& path : paths) {
    bool covered = false;
    for (const std::string& q : paths)
      if (q != path && IsInside(path, q)) covered = true;
    bool dup = false;
    for (const auto& t : top) dup = dup || t.path == path;
    if (covered || dup) continue;
    PathTarget t{path, FindNode(g.root, path)};
    if (t.node) t.size = t.node->size;
    else if (const GamePart* part = FindGamePart(path)) t.size = part->size;
    else
      for (const auto& h : hints)
        if (h.first == path) t.size = h.second;
    top.push_back(t);
  }
  return top;
}

// A folder items may be moved into: a storage on the list (not the system
// partition), or an unprotected folder of the scanned tree.
bool MoveDestinationOk(const std::string& dest) {
  for (const Storage& st : ListStorages(g_extra))
    if (st.path == dest) return st.kind != "system";
  Node* n = FindNode(g.root, dest);
  return n && n->isDir && !n->isMount && !IsProtected(dest);
}

std::string GamesJson() {
  std::ostringstream o;
  o << "{\"state\":\"" << (g.game_scanner ? "counting" : g.games_ready ? "ready" : "idle") << "\",\"games\":[";
  for (size_t i = 0; i < g.games.size(); i++) {
    const GameTotal& gt = g.games[i];
    GameInfo gi = LookupGame(gt.id, gt.parts.empty() ? "" : gt.parts[0].path);
    o << (i ? "," : "") << "{\"id\":" << Str(gt.id) << ",\"size\":" << gt.size << ",\"files\":" << gt.files;
    if (!gi.title.empty()) o << ",\"title\":" << Str(gi.title);
    if (!gi.icon.empty()) o << ",\"icon\":true";
    o << ",\"parts\":[";
    for (size_t k = 0; k < gt.parts.size(); k++) {
      const GamePart& part = gt.parts[k];
      Node* n = FindNode(g.root, part.path);
      o << (k ? "," : "") << "{\"cat\":" << Str(part.category) << ",\"path\":" << Str(part.path)
        << ",\"volume\":" << Str(part.volume) << ",\"size\":" << part.size << ",\"files\":" << part.files
        << ",\"error\":" << Bool(part.error) << ",\"protected\":" << Bool(IsProtected(part.path));
      // In the scanned tree: the Folders tab can show it.
      if (n && n->parent) o << ",\"node\":" << n->id << ",\"parent\":" << n->parent->id;
      std::string w = DeleteWarning(part.path);
      if (!w.empty()) o << ",\"warn\":" << Str(w);
      o << "}";
    }
    o << "]}";
  }
  o << "]}";
  return o.str();
}

// ---------------------------------------------------------------- transitions (locked)

// Count every game's data again, on all volumes (not just the scanned one).
void StartGamesLocked() {
  g.game_scanner.reset();  // cancels and waits for a running one
  std::vector<GameRoot> roots;
  for (const Storage& st : ListStorages(g_extra)) {
#if defined(__PROSPERO__)
    if (st.kind == "system") continue;
#else
    // On a computer only the folders given on the command line (test data).
    if (std::find(g_extra.begin(), g_extra.end(), st.path) == g_extra.end()) continue;
#endif
    roots.push_back({st.path, st.name});
  }
  std::vector<std::string> skip = SkipList();
#if defined(__PROSPERO__)
  // System folders keep their own per-title files (share, licenses).
  for (const char* p : {"/user/system", "/user/license", "/user/settings"}) skip.push_back(p);
#endif
  g.game_scanner = std::make_unique<GameScanner>();
  g.game_scanner->Start(roots, skip);
}

void FinishScanLocked() {
  bool cancelled = g.scanner->Cancelled();
  uint64_t items = g.scanner->Items();
  Node* r = g.scanner->TakeRoot();
  g.scanner.reset();
  if (cancelled) {
    delete r;
    g.phase = g.root ? Phase::Ready : Phase::Idle;
    Log("scan cancelled");
    return;
  }
  delete g.root;
  g.index.Clear();
  g.root = r;
  g.index.Add(r);
  g.biggest = BiggestFiles(r, 500);
  g.phase = Phase::Ready;
  g.scanned = time(nullptr);
  QuerySpace(g.storage.path, &g.storage.total, &g.storage.free);
  Log("scan done: %llu items, %llu bytes", (unsigned long long)items, (unsigned long long)r->size);
}

// Scan `n` again after files in it changed, and fix the sizes above it.
void RescanLocked(Node* n) {
  Node* fresh = Scanner::ScanSync(n->path());
  int64_t dsize = (int64_t)fresh->size - (int64_t)n->size;
  int64_t dfiles = (int64_t)fresh->files - (int64_t)n->files;
  for (Node* c : n->children) {
    g.index.Remove(c);
    delete c;
  }
  n->children = std::move(fresh->children);
  fresh->children.clear();
  for (Node* c : n->children) {
    c->parent = n;
    g.index.Add(c);
  }
  n->size = fresh->size;
  n->files = fresh->files;
  n->error = n->error || fresh->error;
  delete fresh;
  for (Node* a = n->parent; a; a = a->parent) {
    a->size = (uint64_t)std::max<int64_t>(0, (int64_t)a->size + dsize);
    a->files = (uint64_t)std::max<int64_t>(0, (int64_t)a->files + dfiles);
  }
  ResortUp(n);
}

void FinishDeleteLocked() {
  auto results = g.deleter->Results();
  g.result_freed = g.deleter->Freed();
  g.result_errors = g.deleter->Errors();
  g.result_total = results.size();
  g.result_removed = 0;
  g.deleter.reset();
  for (auto& res : results) {
    Node* n = res.node;
    if (!n) {  // on another volume: not in the tree
      if (res.gone) g.result_removed++;
      continue;
    }
    if (res.gone && n->parent) {
      g.index.Remove(n);
      DetachAndFree(n);
      g.result_removed++;
      continue;
    }
    RescanLocked(n);  // partly deleted
  }
  g.biggest = BiggestFiles(g.root, 500);
  g.result_op = "delete";
  g.result_seq++;
  g.phase = Phase::Ready;
  QuerySpace(g.storage.path, &g.storage.total, &g.storage.free);
  StartGamesLocked();
  Log("delete done: freed %llu bytes, %llu errors", (unsigned long long)g.result_freed,
      (unsigned long long)g.result_errors);
}

void FinishMoveLocked() {
  auto results = g.mover->Results();
  g.result_freed = g.mover->Moved();
  g.result_errors = g.mover->Errors();
  g.result_total = results.size();
  g.result_removed = 0;
  g.mover.reset();
  for (auto& res : results) {
    Node* n = res.node;
    if (res.gone) g.result_removed++;
    if (!n) continue;  // on another volume: not in the tree
    if (res.gone && n->parent) {
      g.index.Remove(n);
      DetachAndFree(n);
    } else {
      RescanLocked(n);  // partly moved
    }
  }
  // The folder the items went to, when it is part of this scan.
  if (Node* d = FindNode(g.root, g.move_dest)) RescanLocked(d);
  g.biggest = BiggestFiles(g.root, 500);
  g.result_op = "move";
  g.result_seq++;
  g.phase = Phase::Ready;
  QuerySpace(g.storage.path, &g.storage.total, &g.storage.free);
  StartGamesLocked();
  Log("move done: %llu of %llu items, %llu bytes, %llu errors", (unsigned long long)g.result_removed,
      (unsigned long long)g.result_total, (unsigned long long)g.result_freed,
      (unsigned long long)g.result_errors);
}

void PollReportLocked();

void PollLocked() {
  if (g.phase == Phase::Scanning && g.scanner && g.scanner->Done()) FinishScanLocked();
  if (g.phase == Phase::Deleting && g.deleter && g.deleter->Done()) FinishDeleteLocked();
  if (g.phase == Phase::Moving && g.mover && g.mover->Done()) FinishMoveLocked();
  if (g.game_scanner && g.game_scanner->Done()) {
    g.games = g.game_scanner->Take();
    g.game_scanner.reset();
    g.games_ready = true;
    Log("games counted: %zu", g.games.size());
  }
  PollReportLocked();
}

const char* PhaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";
    case Phase::Scanning: return "scanning";
    case Phase::Ready: return "ready";
    case Phase::Deleting: return "deleting";
    case Phase::Moving: return "moving";
  }
  return "idle";
}

// ---------------------------------------------------------------- handlers

void Json(Response& res, int status, const std::string& body) {
  res.status = status;
  res.type = "application/json";
  res.body = body;
}

void Error(Response& res, int status, const std::string& msg) {
  Json(res, status, "{\"error\":" + Str(msg) + "}");
}

void Static(const Request& req, Response& res) {
  std::string p = req.path == "/" ? "/index.html" : req.path;
  const Asset* a = FindAsset(p.c_str());
  if (!a) {
    Error(res, 404, "not found");
    return;
  }
  res.type = a->type;
  if (res.type.compare(0, 5, "text/") == 0 || res.type == "application/javascript")
    res.type += "; charset=utf-8";
  if (!g_web_dir.empty()) {  // development: edit files without rebuilding
    std::ifstream f(g_web_dir + p, std::ios::binary);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      res.body = ss.str();
      return;
    }
  }
  res.data = a->data;
  res.size = a->size;
}

// A web file's text: from the web folder in development, else built in.
std::string AssetText(const std::string& p) {
  if (!g_web_dir.empty()) {
    std::ifstream f(g_web_dir + p, std::ios::binary);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      return ss.str();
    }
  }
  const Asset* a = FindAsset(p.c_str());
  return a ? std::string((const char*)a->data, a->size) : std::string();
}

bool ReportBusy() { return g.report && g.report->html.empty(); }

// Next step of the report: take a finished drive, start the next one, and
// build the page once every drive is in and the games are counted.
void PollReportLocked() {
  auto* j = g.report.get();
  if (!j || !j->html.empty()) return;
  if (j->scanner && j->scanner->Done()) {
    Node* r = j->scanner->TakeRoot();
    j->scanner.reset();
    Storage st = j->scope[j->current];
    QuerySpace(st.path, &st.total, &st.free);
    if (r) j->drives[j->current] = ReportDriveJson({r, st, time(nullptr)});
    delete r;
  }
  if (!j->scanner && !j->todo.empty()) {
    j->current = j->todo.front();
    j->todo.erase(j->todo.begin());
    j->scanner = std::make_unique<Scanner>();
    j->scanner->Start(j->scope[j->current].path, SkipList());
    Log("report scan: %s", j->scope[j->current].path.c_str());
  }
  if (!j->scanner && j->todo.empty() && !g.game_scanner) {
    j->html = ReportHtml(AssetText("/report.html"), j->title, j->drives, j->scope, g.games, g.games_ready);
    j->name = ReportFileName(j->title, time(nullptr));
    Log("report ready: %zu drives, %zu bytes", j->scope.size(), j->html.size());
  }
}

std::string ReportStatusJson() {
  auto* j = g.report.get();
  if (!j) return "{\"state\":\"idle\"}";
  std::ostringstream o;
  const char* state = !j->html.empty() ? "ready" : j->scanner ? "scanning" : "games";
  o << "{\"state\":\"" << state << "\",\"total\":" << j->scope.size()
    << ",\"index\":" << (j->scope.size() - j->todo.size()) << ",\"drives\":[";
  for (size_t i = 0; i < j->scope.size(); i++) o << (i ? "," : "") << Str(j->scope[i].name);
  o << "]";
  if (j->scanner)
    o << ",\"name\":" << Str(j->scope[j->current].name) << ",\"items\":" << j->scanner->Items()
      << ",\"bytes\":" << j->scanner->Bytes() << ",\"current\":" << Str(j->scanner->CurrentPath());
  if (!j->html.empty()) o << ",\"size\":" << j->html.size() << ",\"file\":" << Str(j->name);
  o << "}";
  return o.str();
}

std::string TargetsJson() {
  std::string o = "[";
  auto list = ReportTargets(g_extra);
  for (size_t i = 0; i < list.size(); i++)
    o += (i ? "," : "") + std::string("{\"name\":") + Str(list[i].name) + ",\"path\":" + Str(list[i].path) +
         ",\"kind\":" + Str(list[i].kind) + "}";
  return o + "]";
}

}  // namespace

void ApiInit(const std::string& web_dir, const std::vector<std::string>& extra, unsigned short port) {
  g_web_dir = web_dir;
  g_extra = extra;
  g_port = port;
}

void ApiHandle(const Request& req, Response& res) {
  const std::string& p = req.path;
  if (p.compare(0, 5, "/api/") != 0) {
    Static(req, res);
    return;
  }

  if (p == "/api/info") {
    // A request from another device arrived on the LAN address already. The
    // PS5 browser uses 127.0.0.1, so ask the routing table instead.
    std::string ip = IsLoopback(req.local_ip) ? LanAddress() : req.local_ip;
    Json(res, 200, "{\"version\":" + Str(APP_VERSION) + ",\"ip\":" + Str(ip) + ",\"port\":" + Num(g_port) + "}");
    return;
  }

  if (p == "/api/icon") {
    // Only by title ID, so this never serves an arbitrary file.
    auto it = req.query.find("id");
    std::string id = it == req.query.end() ? "" : it->second;
    std::string icon = IsTitleId(id) ? LookupGame(id, "").icon : "";
    std::ifstream f(icon, std::ios::binary);
    if (icon.empty() || !f) {
      Error(res, 404, "no icon");
      return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    res.type = "image/png";
    res.cache = "max-age=86400";
    res.body = ss.str();
    return;
  }

  std::lock_guard<std::mutex> lock(g.mu);
  PollLocked();

  if (p == "/api/storages") {
    std::string o = "[";
    auto list = ListStorages(g_extra);
    for (size_t i = 0; i < list.size(); i++) o += (i ? "," : "") + StorageJson(list[i]);
    Json(res, 200, o + "]");
  } else if (p == "/api/status") {
    std::ostringstream o;
    o << "{\"phase\":\"" << PhaseName(g.phase) << "\"";
    if (g.root || g.phase == Phase::Scanning) o << ",\"storage\":" << StorageJson(g.storage);
    if (g.root) o << ",\"root\":" << g.root->id << ",\"rootSize\":" << g.root->size;
    if (g.scanner)
      o << ",\"items\":" << g.scanner->Items() << ",\"bytes\":" << g.scanner->Bytes()
        << ",\"current\":" << Str(g.scanner->CurrentPath());
    if (g.deleter)
      o << ",\"index\":" << g.deleter->Index() << ",\"total\":" << g.deleter->Total()
        << ",\"freed\":" << g.deleter->Freed() << ",\"goal\":" << g.delete_bytes
        << ",\"current\":" << Str(g.deleter->CurrentPath());
    if (g.mover)
      o << ",\"index\":" << g.mover->Index() << ",\"total\":" << g.mover->Total()
        << ",\"moved\":" << g.mover->Moved() << ",\"goal\":" << g.move_bytes
        << ",\"dest\":" << Str(g.move_dest) << ",\"current\":" << Str(g.mover->CurrentPath());
    o << ",\"result\":{\"op\":" << Str(g.result_op) << ",\"seq\":" << g.result_seq << ",\"freed\":" << g.result_freed
      << ",\"errors\":" << g.result_errors << ",\"removed\":" << g.result_removed
      << ",\"total\":" << g.result_total << "}}";
    Json(res, 200, o.str());
  } else if (p == "/api/scan" && req.method == "POST") {
    if (g.phase == Phase::Scanning || g.phase == Phase::Deleting || g.phase == Phase::Moving || ReportBusy()) {
      Error(res, 409, "busy");
      return;
    }
    auto it = req.query.find("path");
    if (it == req.query.end() || it->second.empty()) {
      Error(res, 400, "missing path");
      return;
    }
    // Only a listed storage can be scanned, never an arbitrary folder.
    Storage s;
    bool found = false;
    for (auto& st : ListStorages(g_extra))
      if (st.path == it->second) s = st, found = true;
    if (!found) {
      Error(res, 400, "not a storage");
      return;
    }
    g.storage = s;
    ClearGameCache();
    g.scanner = std::make_unique<Scanner>();
    g.scanner->Start(s.path, SkipList());
    g.phase = Phase::Scanning;
    StartGamesLocked();
    Log("scan start: %s", s.path.c_str());
    Json(res, 200, "{\"ok\":true}");
  } else if (p == "/api/cancel" && req.method == "POST") {
    if (g.scanner) g.scanner->Cancel();
    Json(res, 200, "{\"ok\":true}");
  } else if (p == "/api/node") {
    if (!g.root) {
      Error(res, 404, "no scan");
      return;
    }
    auto it = req.query.find("id");
    Node* n = it == req.query.end() ? g.root : g.index.Get((uint32_t)std::strtoul(it->second.c_str(), nullptr, 10));
    if (!n) {
      Error(res, 404, "gone");
      return;
    }
    Json(res, 200, NodeJson(n));
  } else if (p == "/api/biggest") {
    Json(res, 200, g.root ? BiggestJson() : "[]");
  } else if (p == "/api/games") {
    Json(res, 200, GamesJson());
  } else if (p == "/api/delete" && req.method == "POST") {
    if (g.phase != Phase::Ready) {
      Error(res, 409, "busy");
      return;
    }
    std::vector<PathTarget> top = SelectedLocked(req);
    if (top.empty()) {
      Error(res, 400, "nothing deletable selected");
      return;
    }
    g.delete_bytes = 0;
    for (const auto& t : top) {
      g.delete_bytes += t.size;
      Log("delete: %s", t.path.c_str());
    }
    g.deleter = std::make_unique<Deleter>();
    g.deleter->Start(top);
    g.phase = Phase::Deleting;
    Json(res, 200, "{\"ok\":true,\"count\":" + Num(top.size()) + "}");
  } else if (p == "/api/move" && req.method == "POST") {
    if (g.phase != Phase::Ready) {
      Error(res, 409, "busy");
      return;
    }
    auto it = req.query.find("dest");
    std::string dest = it == req.query.end() ? "" : it->second;
    while (dest.size() > 1 && dest.back() == '/') dest.pop_back();
    if (dest.empty() || !MoveDestinationOk(dest)) {
      Error(res, 400, "Items cannot be moved to this folder");
      return;
    }
    // Leave out what is already there, and never a folder into itself.
    std::vector<PathTarget> top;
    for (const PathTarget& t : SelectedLocked(req, true)) {
      if (dest == t.path || IsInside(dest, t.path)) {
        Error(res, 400, "A folder cannot be moved into itself");
        return;
      }
      size_t slash = t.path.find_last_of('/');
      std::string parent = slash == 0 ? "/" : t.path.substr(0, slash);
      if (parent != dest) top.push_back(t);
    }
    if (top.empty()) {
      Error(res, 400, "Nothing to move: the items are already in this folder, or no longer exist");
      return;
    }
    // Moving to another volume copies the data: it has to fit there.
    struct stat dst;
    uint64_t need = 0, total = 0, free = 0;
    if (stat(dest.c_str(), &dst) != 0) {
      Error(res, 400, "Destination folder not found");
      return;
    }
    for (const PathTarget& t : top) {
      struct stat sst;
      if (lstat(t.path.c_str(), &sst) == 0 && sst.st_dev != dst.st_dev) need += t.size;
    }
    if (need && QuerySpace(dest, &total, &free) && need > free) {
      Json(res, 400, "{\"error\":\"not enough space\",\"need\":" + Num(need) + ",\"free\":" + Num(free) + "}");
      return;
    }
    g.move_bytes = 0;
    for (const PathTarget& t : top) {
      g.move_bytes += t.size;
      Log("move: %s -> %s", t.path.c_str(), dest.c_str());
    }
    g.move_dest = dest;
    g.mover = std::make_unique<Mover>();
    g.mover->Start(top, dest);
    g.phase = Phase::Moving;
    Json(res, 200, "{\"ok\":true,\"count\":" + Num(top.size()) + "}");
  } else if (p == "/api/report/targets") {
    Json(res, 200, TargetsJson());
  } else if (p == "/api/report/start" && req.method == "POST") {
    if (g.phase == Phase::Scanning || g.phase == Phase::Deleting || g.phase == Phase::Moving || ReportBusy()) {
      Error(res, 409, "busy");
      return;
    }
    // All drives (but the system partition), or the listed drives asked for.
    auto list = ListStorages(g_extra);
    auto job = std::make_unique<State::ReportJob>();
    if (req.query.count("all")) {
      for (const Storage& st : list)
        if (st.kind != "system") job->scope.push_back(st);
    } else {
      auto it = req.query.find("paths");
      std::string plist = it == req.query.end() ? "" : it->second;
      size_t pos = 0;
      while (pos < plist.size()) {
        size_t c = plist.find('\n', pos);
        if (c == std::string::npos) c = plist.size();
        std::string path = plist.substr(pos, c - pos);
        pos = c + 1;
        for (const Storage& st : list) {
          bool dup = false;
          for (const Storage& have : job->scope) dup = dup || have.path == st.path;
          if (st.path == path && !dup) job->scope.push_back(st);
        }
      }
    }
    if (job->scope.empty()) {
      Error(res, 400, "No drive to report on");
      return;
    }
    if (req.query.count("all")) job->title = "All drives";
    for (const Storage& st : job->scope)
      if (!req.query.count("all")) job->title += (job->title.empty() ? "" : ", ") + st.name;
    job->drives.resize(job->scope.size());
    for (size_t i = 0; i < job->scope.size(); i++) {
      // The drive scanned already: no need to scan it again.
      if (g.root && g.phase == Phase::Ready && g.storage.path == job->scope[i].path) {
        Storage st = job->scope[i];
        job->drives[i] = ReportDriveJson({g.root, st, g.scanned});
      } else {
        job->todo.push_back(i);
      }
    }
    if (!g.games_ready && !g.game_scanner) StartGamesLocked();
    g.report = std::move(job);
    Log("report start: %zu drives, %zu to scan", g.report->scope.size(), g.report->todo.size());
    PollReportLocked();
    Json(res, 200, ReportStatusJson());
  } else if (p == "/api/report/status") {
    Json(res, 200, ReportStatusJson());
  } else if (p == "/api/report/cancel" && req.method == "POST") {
    if (ReportBusy()) {
      g.report.reset();  // stops and waits for its scan
      Log("report cancelled");
    }
    Json(res, 200, "{\"ok\":true}");
  } else if (p == "/api/report" || (p == "/api/report/save" && req.method == "POST")) {
    if (!g.report || g.report->html.empty()) {
      Error(res, 409, "No report is ready");
      return;
    }
    const std::string& name = g.report->name;
    const std::string& html = g.report->html;
    if (p == "/api/report") {
      res.status = 200;
      res.type = "text/html; charset=utf-8";
      res.body = html;
      if (req.query.count("download")) res.headers = "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
      return;
    }
    // Only a folder from the list, never an arbitrary path.
    auto it = req.query.find("dest");
    std::string dest = it == req.query.end() ? "" : it->second;
    bool ok = false;
    for (const ReportTarget& t : ReportTargets(g_extra)) ok = ok || t.path == dest;
    if (!ok) {
      Error(res, 400, "The report cannot be saved there");
      return;
    }
    std::string path = JoinPath(dest, name);
    std::string tmp = path + ".part";
    bool written = false;
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      written = f && f.write(html.data(), (std::streamsize)html.size()) && f.flush();
    }
    if (!written || std::rename(tmp.c_str(), path.c_str()) != 0) {
      unlink(tmp.c_str());
      Log("report save failed: %s", path.c_str());
      Error(res, 500, "Could not write the file. Is the drive full or read-only?");
      return;
    }
    Log("report saved: %s (%zu bytes)", path.c_str(), html.size());
    Json(res, 200, "{\"ok\":true,\"path\":" + Str(path) + ",\"size\":" + Num(html.size()) + "}");
  } else if (p == "/api/quit" && req.method == "POST") {
    Log("quit requested");
    Json(res, 200, "{\"ok\":true}");
    Stop();
  } else {
    Error(res, 404, "unknown endpoint");
  }
}
