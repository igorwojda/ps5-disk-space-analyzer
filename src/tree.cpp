// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <queue>

// ---------------------------------------------------------------- Node

Node::~Node() {
  for (Node* c : children) delete c;
}

std::string Node::path() const {
  if (!parent) return name;
  std::string p = parent->path();
  if (p.empty() || p.back() != '/') p += '/';
  return p + name;
}

bool Node::isAncestorOf(const Node* n) const {
  for (; n; n = n->parent)
    if (n == this) return true;
  return false;
}

static bool BySizeDesc(const Node* a, const Node* b) {
  if (a->size != b->size) return a->size > b->size;
  return a->name < b->name;
}

void ResortUp(Node* n) {
  for (; n && n->parent; n = n->parent) {
    auto& v = n->parent->children;
    std::stable_sort(v.begin(), v.end(), BySizeDesc);
  }
}

void DetachAndFree(Node* n) {
  Node* p = n->parent;
  if (p) {
    for (Node* a = p; a; a = a->parent) {
      a->size -= std::min(a->size, n->size);
      a->files -= std::min(a->files, n->files);
    }
    auto& v = p->children;
    v.erase(std::remove(v.begin(), v.end(), n), v.end());
    ResortUp(p);
  }
  n->parent = nullptr;
  delete n;
}

// ---------------------------------------------------------------- Index

void Index::Add(Node* subtree) {
  std::vector<Node*> stack{subtree};
  while (!stack.empty()) {
    Node* n = stack.back();
    stack.pop_back();
    n->id = (uint32_t)v_.size();
    v_.push_back(n);
    for (Node* c : n->children) stack.push_back(c);
  }
}

void Index::Remove(Node* subtree) {
  std::vector<Node*> stack{subtree};
  while (!stack.empty()) {
    Node* n = stack.back();
    stack.pop_back();
    if (n->id < v_.size() && v_[n->id] == n) v_[n->id] = nullptr;
    for (Node* c : n->children) stack.push_back(c);
  }
}

// ---------------------------------------------------------------- helpers

static uint64_t DiskBytes(const struct stat& st) {
  // Allocated size is what a delete gives back; fall back to the logical
  // size on file systems that do not report blocks.
  uint64_t alloc = (uint64_t)st.st_blocks * 512u;
  return alloc ? alloc : (uint64_t)st.st_size;
}

static std::string Join(const std::string& dir, const char* name) {
  std::string p = dir;
  if (p.empty() || p.back() != '/') p += '/';
  return p + name;
}

static bool StartThread(pthread_t* t, void* (*fn)(void*), void* arg) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  // Deep trees recurse; give the walker a roomy stack.
  pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
  bool ok = pthread_create(t, &attr, fn, arg) == 0;
  pthread_attr_destroy(&attr);
  return ok;
}

// ---------------------------------------------------------------- Scanner

Scanner::~Scanner() {
  Cancel();
  if (started_) pthread_join(thread_, nullptr);
  delete root_;
}

void Scanner::Start(const std::string& path, const std::vector<std::string>& skip) {
  root_path_ = path;
  skip_ = skip;
  started_ = StartThread(&thread_, ThreadMain, this);
  if (!started_) ThreadMain(this);
}

Node* Scanner::TakeRoot() {
  Node* r = root_;
  root_ = nullptr;
  return r;
}

std::string Scanner::CurrentPath() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

void* Scanner::ThreadMain(void* p) {
  auto* self = static_cast<Scanner*>(p);
  auto* root = new Node();
  root->name = self->root_path_;
  root->isDir = true;
  struct stat st;
  if (lstat(self->root_path_.c_str(), &st) == 0) {
    root->size = DiskBytes(st);
    self->Walk(root, self->root_path_, (uint64_t)st.st_dev);
  } else {
    root->error = true;
  }
  self->root_ = root;
  self->done_ = true;
  return nullptr;
}

Node* Scanner::ScanSync(const std::string& path) {
  Scanner s;
  ThreadMain(&s);
  return s.TakeRoot();
}

void Scanner::Walk(Node* dir, const std::string& path, uint64_t dev) {
  DIR* d = opendir(path.c_str());
  if (!d) {
    dir->error = true;
    return;
  }
  uint64_t local = 0;
  while (struct dirent* e = readdir(d)) {
    if (cancel_) break;
    const char* nm = e->d_name;
    if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;

    std::string child_path = Join(path, nm);
    struct stat st;
    if (lstat(child_path.c_str(), &st) != 0) {
      dir->error = true;
      continue;
    }
    items_++;
    if ((++local & 255) == 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      current_ = child_path;
    }

    auto* n = new Node();
    n->name = nm;
    n->parent = dir;
    dir->children.push_back(n);

    if (S_ISDIR(st.st_mode)) {
      n->isDir = true;
      n->size = DiskBytes(st);
      if (std::find(skip_.begin(), skip_.end(), child_path) != skip_.end()) {
        n->isMount = true;  // shown, never descended
      } else if ((uint64_t)st.st_dev != dev) {
        n->isMount = true;  // another volume: has its own entry on the storage list
      } else {
        Walk(n, child_path, dev);
      }
    } else {
      n->files = 1;
      // Count hard-linked data once.
      if (S_ISREG(st.st_mode) && st.st_nlink > 1) {
        auto key = std::make_pair((uint64_t)st.st_dev, (uint64_t)st.st_ino);
        auto it = std::lower_bound(hardlinks_.begin(), hardlinks_.end(), key);
        if (it != hardlinks_.end() && *it == key) {
          n->size = 0;
        } else {
          hardlinks_.insert(it, key);
          n->size = DiskBytes(st);
        }
      } else {
        n->size = DiskBytes(st);
      }
    }
    if (!n->isDir) bytes_ += n->size;
    dir->size += n->size;
    dir->files += n->files;
  }
  closedir(d);
  std::sort(dir->children.begin(), dir->children.end(), BySizeDesc);
}

// ---------------------------------------------------------------- Deleter

Deleter::~Deleter() {
  if (started_) pthread_join(thread_, nullptr);
}

void Deleter::Start(const std::vector<Node*>& targets) {
  targets_ = targets;
  for (Node* n : targets) paths_.push_back(n->path());
  started_ = StartThread(&thread_, ThreadMain, this);
  if (!started_) ThreadMain(this);
}

std::string Deleter::CurrentPath() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

void* Deleter::ThreadMain(void* p) {
  auto* self = static_cast<Deleter*>(p);
  for (size_t i = 0; i < self->paths_.size(); i++) {
    self->index_ = (int)i;
    const std::string& path = self->paths_[i];
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) self->Remove(path, (uint64_t)st.st_dev);
    bool gone = lstat(path.c_str(), &st) != 0 && errno == ENOENT;
    self->results_.push_back({self->targets_[i], gone});
  }
  self->done_ = true;
  return nullptr;
}

void Deleter::Remove(const std::string& path, uint64_t dev) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = path;
  }
  if (S_ISDIR(st.st_mode)) {
    if ((uint64_t)st.st_dev != dev) {  // never cross into another volume
      errors_++;
      return;
    }
    if (DIR* d = opendir(path.c_str())) {
      std::vector<std::string> names;
      while (struct dirent* e = readdir(d)) {
        const char* nm = e->d_name;
        if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
        names.push_back(nm);
      }
      closedir(d);
      for (auto& nm : names) Remove(Join(path, nm.c_str()), dev);
    }
    if (rmdir(path.c_str()) == 0) freed_ += DiskBytes(st);
    else errors_++;
  } else {
    if (unlink(path.c_str()) == 0) freed_ += DiskBytes(st);
    else errors_++;
  }
}

// ---------------------------------------------------------------- queries

std::vector<Node*> BiggestFiles(Node* root, size_t limit) {
  auto cmp = [](const Node* a, const Node* b) { return a->size > b->size; };
  std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> heap(cmp);
  std::vector<Node*> stack{root};
  while (!stack.empty()) {
    Node* n = stack.back();
    stack.pop_back();
    if (n->isDir) {
      for (Node* c : n->children) stack.push_back(c);
    } else if (n->size > 0) {
      if (heap.size() < limit) heap.push(n);
      else if (n->size > heap.top()->size) {
        heap.pop();
        heap.push(n);
      }
    }
  }
  std::vector<Node*> out;
  while (!heap.empty()) {
    out.push_back(heap.top());
    heap.pop();
  }
  std::reverse(out.begin(), out.end());
  return out;
}
