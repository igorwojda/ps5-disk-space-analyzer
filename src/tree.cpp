// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <queue>

#include "platform.h"
#include "system.h"

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
  s.root_path_ = path;
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

void Deleter::Start(const std::vector<Target>& targets, const std::vector<DeletePlan>& plans) {
  for (const Target& t : targets) {
    targets_.push_back(t.node);
    paths_.push_back(t.path);
  }
  plans_ = plans;
  plans_.resize(paths_.size());
  started_ = StartThread(&thread_, ThreadMain, this);
  if (!started_) ThreadMain(this);
}

std::string Deleter::CurrentPath() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

void Deleter::SetCurrent(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_ = text;
}

void Deleter::RemovePath(const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) == 0) Remove(path, (uint64_t)st.st_dev);
}

void* Deleter::ThreadMain(void* p) {
  auto* self = static_cast<Deleter*>(p);
  for (size_t i = 0; i < self->paths_.size(); i++) {
    self->index_ = (int)i;
    const std::string& path = self->paths_[i];
    const DeletePlan& plan = self->plans_[i];
    bool keep = false;
    if (!plan.uninstall.empty()) {
      self->touched_.insert(self->touched_.end(), plan.system_removes.begin(), plan.system_removes.end());
      self->touched_.insert(self->touched_.end(), plan.also.begin(), plan.also.end());
      // Already gone (another item of this delete uninstalled it): nothing to ask.
      if (TitleListed(plan.uninstall)) {
        self->SetCurrent("Uninstalling " + plan.uninstall + " through the console");
        std::string why = UninstallTitle(plan.uninstall);
        if (why.empty()) {
          self->freed_ += plan.system_bytes;
        } else {
          // The title stays, so its files do too: deleting them would leave a
          // home screen tile that no longer starts.
          self->notes_.push_back(why);
          self->errors_++;
          keep = true;
        }
      }
    }
    if (!keep) {
      for (const std::string& a : plan.also) self->RemovePath(a);
      self->RemovePath(path);
    }
    struct stat st;
    bool gone = lstat(path.c_str(), &st) != 0 && errno == ENOENT;
    if (gone && !plan.playden_forget.empty() && !AskPlaydenToForget(plan.playden_forget))
      self->notes_.push_back("Playden could not be told about game " + plan.playden_forget +
                             ": uninstall it in Playden too.");
    self->results_.push_back({self->targets_[i], gone});
  }
  self->done_ = true;
  return nullptr;
}

void Deleter::Remove(const std::string& path, uint64_t dev) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return;
  SetCurrent(path);
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

// ---------------------------------------------------------------- Mover

static bool RemoveTreeOn(const std::string& path, uint64_t dev) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return errno == ENOENT;
  if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
  if ((uint64_t)st.st_dev != dev) return false;  // never cross into another volume
  bool ok = true;
  if (DIR* d = opendir(path.c_str())) {
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
      const char* nm = e->d_name;
      if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
      names.push_back(nm);
    }
    closedir(d);
    for (auto& nm : names) ok = RemoveTreeOn(Join(path, nm.c_str()), dev) && ok;
  }
  return rmdir(path.c_str()) == 0 && ok;
}

bool RemoveTree(const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return errno == ENOENT;
  return RemoveTreeOn(path, (uint64_t)st.st_dev);
}

Mover::~Mover() {
  if (started_) pthread_join(thread_, nullptr);
}

void Mover::Start(const std::vector<Target>& targets, const std::string& dest) {
  targets_ = targets;
  dest_ = dest;
  started_ = StartThread(&thread_, ThreadMain, this);
  if (!started_) ThreadMain(this);
}

std::string Mover::CurrentPath() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_;
}

void Mover::SetCurrent(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_ = path;
}

void* Mover::ThreadMain(void* p) {
  auto* self = static_cast<Mover*>(p);
  for (size_t i = 0; i < self->targets_.size(); i++) {
    self->index_ = (int)i;
    const Target& t = self->targets_[i];
    self->SetCurrent(t.path);
    size_t slash = t.path.find_last_of('/');
    std::string name = slash == std::string::npos ? t.path : t.path.substr(slash + 1);
    std::string dst = Join(self->dest_, name.c_str());
    struct stat st;
    if (lstat(dst.c_str(), &st) == 0) {  // never overwrite
      Log("move: %s already exists", dst.c_str());
      self->errors_++;
    } else if (rename(t.path.c_str(), dst.c_str()) == 0) {
      self->moved_ += t.size;
    } else if (errno == EXDEV && lstat(t.path.c_str(), &st) == 0) {
      // Another volume: copy all of it, then remove the original.
      uint64_t before = self->moved_;
      if (self->Copy(t.path, dst, (uint64_t)st.st_dev)) {
        if (!RemoveTree(t.path)) self->errors_++;
      } else {
        Log("move: copy of %s failed, removing the partial copy", t.path.c_str());
        RemoveTree(dst);
        self->moved_ = before;
        self->errors_++;
      }
    } else {
      Log("move: %s: %s", t.path.c_str(), std::strerror(errno));
      self->errors_++;
    }
    bool gone = lstat(t.path.c_str(), &st) != 0 && errno == ENOENT;
    self->results_.push_back({t.node, gone});
  }
  self->done_ = true;
  return nullptr;
}

bool Mover::Copy(const std::string& src, const std::string& dst, uint64_t dev) {
  struct stat st;
  if (lstat(src.c_str(), &st) != 0) return false;
  SetCurrent(src);
  if (S_ISDIR(st.st_mode)) {
    if ((uint64_t)st.st_dev != dev) return false;  // a volume mounted inside
    if (mkdir(dst.c_str(), (st.st_mode & 07777) | 0700) != 0) return false;
    DIR* d = opendir(src.c_str());
    if (!d) return false;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
      const char* nm = e->d_name;
      if (nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
      names.push_back(nm);
    }
    closedir(d);
    for (auto& nm : names)
      if (!Copy(Join(src, nm.c_str()), Join(dst, nm.c_str()), dev)) return false;
    return true;
  }
  if (S_ISLNK(st.st_mode)) {
    char buf[4096];
    ssize_t n = readlink(src.c_str(), buf, sizeof buf - 1);
    if (n < 0) return false;
    buf[n] = 0;
    return symlink(buf, dst.c_str()) == 0;
  }
  if (!S_ISREG(st.st_mode)) return false;
  int in = open(src.c_str(), O_RDONLY);
  if (in < 0) return false;
  int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL, (st.st_mode & 07777) | 0600);
  if (out < 0) {
    close(in);
    return false;
  }
  std::vector<char> buf(1 << 20);
  bool ok = true;
  for (;;) {
    ssize_t n = read(in, buf.data(), buf.size());
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    for (ssize_t off = 0; off < n;) {
      ssize_t w = write(out, buf.data() + off, (size_t)(n - off));
      if (w < 0) {
        if (errno == EINTR) continue;
        ok = false;
        break;
      }
      off += w;
      moved_ += (uint64_t)w;
    }
    if (!ok) break;
  }
  close(in);
  if (close(out) != 0) ok = false;
  return ok;
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
