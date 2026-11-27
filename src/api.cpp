// SPDX-License-Identifier: GPL-3.0-or-later
#include "api.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#include "platform.h"
#include "system.h"
#include "tree.h"
#include "web_assets.h"

namespace {

// ---------------------------------------------------------------- state

enum class Phase { Idle, Scanning, Ready, Deleting };

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
  // Last finished delete, shown once by the UI.
  int result_seq = 0;
  uint64_t result_freed = 0, result_errors = 0, result_removed = 0, result_total = 0;
  std::string error;  // last scan error
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

// Fields shared by list rows and biggest files.
void ItemFields(std::ostringstream& o, Node* n, const std::string& path) {
  o << "\"id\":" << n->id << ",\"name\":" << Str(n->name) << ",\"path\":" << Str(path)
    << ",\"size\":" << n->size << ",\"files\":" << n->files << ",\"dir\":" << Bool(n->isDir)
    << ",\"mount\":" << Bool(n->isMount) << ",\"error\":" << Bool(n->error)
    << ",\"protected\":" << Bool(n->isMount || IsProtected(path));
  std::string w = DeleteWarning(path);
  if (!w.empty()) o << ",\"warn\":" << Str(w);
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
  for (size_t i = 0; i < chain.size(); i++)
    o << (i ? "," : "") << "{\"id\":" << chain[i]->id << ",\"name\":" << Str(chain[i]->name) << "}";
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

// ---------------------------------------------------------------- transitions (locked)

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
  QuerySpace(g.storage.path, &g.storage.total, &g.storage.free);
  Log("scan done: %llu items, %llu bytes", (unsigned long long)items, (unsigned long long)r->size);
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
    if (res.gone && n->parent) {
      g.index.Remove(n);
      DetachAndFree(n);
      g.result_removed++;
      continue;
    }
    // Partly deleted: rescan just this subtree.
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
  g.biggest = BiggestFiles(g.root, 500);
  g.result_seq++;
  g.phase = Phase::Ready;
  QuerySpace(g.storage.path, &g.storage.total, &g.storage.free);
  Log("delete done: freed %llu bytes, %llu errors", (unsigned long long)g.result_freed,
      (unsigned long long)g.result_errors);
}

void PollLocked() {
  if (g.phase == Phase::Scanning && g.scanner && g.scanner->Done()) FinishScanLocked();
  if (g.phase == Phase::Deleting && g.deleter && g.deleter->Done()) FinishDeleteLocked();
}

const char* PhaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";
    case Phase::Scanning: return "scanning";
    case Phase::Ready: return "ready";
    case Phase::Deleting: return "deleting";
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
    o << ",\"result\":{\"seq\":" << g.result_seq << ",\"freed\":" << g.result_freed
      << ",\"errors\":" << g.result_errors << ",\"removed\":" << g.result_removed
      << ",\"total\":" << g.result_total << "}}";
    Json(res, 200, o.str());
  } else if (p == "/api/scan" && req.method == "POST") {
    if (g.phase == Phase::Scanning || g.phase == Phase::Deleting) {
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
    g.scanner = std::make_unique<Scanner>();
    g.scanner->Start(s.path, SkipList());
    g.phase = Phase::Scanning;
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
  } else if (p == "/api/delete" && req.method == "POST") {
    if (g.phase != Phase::Ready) {
      Error(res, 409, "busy");
      return;
    }
    std::vector<Node*> targets;
    auto it = req.query.find("ids");
    std::string ids = it == req.query.end() ? "" : it->second;
    size_t pos = 0;
    while (pos < ids.size()) {
      size_t c = ids.find(',', pos);
      if (c == std::string::npos) c = ids.size();
      Node* n = g.index.Get((uint32_t)std::strtoul(ids.substr(pos, c - pos).c_str(), nullptr, 10));
      pos = c + 1;
      if (!n || !n->parent || n->isMount || IsProtected(n->path())) continue;  // never the root
      targets.push_back(n);
    }
    // A selected folder already covers anything selected inside it.
    std::vector<Node*> top;
    for (Node* n : targets) {
      bool covered = false;
      for (Node* m : targets)
        if (m != n && m->isAncestorOf(n)) covered = true;
      if (!covered && std::find(top.begin(), top.end(), n) == top.end()) top.push_back(n);
    }
    if (top.empty()) {
      Error(res, 400, "nothing deletable selected");
      return;
    }
    g.delete_bytes = 0;
    for (Node* n : top) {
      g.delete_bytes += n->size;
      Log("delete: %s", n->path().c_str());
    }
    g.deleter = std::make_unique<Deleter>();
    g.deleter->Start(top);
    g.phase = Phase::Deleting;
    Json(res, 200, "{\"ok\":true,\"count\":" + Num(top.size()) + "}");
  } else if (p == "/api/quit" && req.method == "POST") {
    Log("quit requested");
    Json(res, 200, "{\"ok\":true}");
    Stop();
  } else {
    Error(res, 404, "unknown endpoint");
  }
}
