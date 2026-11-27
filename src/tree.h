// SPDX-License-Identifier: GPL-3.0-or-later
// File tree model, background scanner and deleter.
#pragma once

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct Node {
  std::string name;              // root: full path; others: entry name
  Node* parent = nullptr;
  std::vector<Node*> children;   // sorted by size, biggest first
  uint64_t size = 0;             // bytes on disk, including all descendants
  uint64_t files = 0;            // regular files (and links) inside, incl. self
  uint32_t id = 0;               // index in the Index, stable while the node lives
  bool isDir = false;
  bool isMount = false;          // another volume mounted here; not descended
  bool error = false;            // could not be read completely

  ~Node();
  std::string path() const;
  bool isAncestorOf(const Node* n) const;  // true also when n == this
};

// id -> node, so the web UI can refer to nodes by number.
class Index {
 public:
  void Add(Node* subtree);
  void Remove(Node* subtree);  // before freeing it
  Node* Get(uint32_t id) const { return id < v_.size() ? v_[id] : nullptr; }
  void Clear() { v_.clear(); }

 private:
  std::vector<Node*> v_;
};

// Remove a node from the tree, subtracting its size from its ancestors and
// keeping sibling lists sorted. Frees the node.
void DetachAndFree(Node* n);

// Re-sort `n` inside its parent and up the chain after sizes changed.
void ResortUp(Node* n);

// A scan that runs on its own thread. Poll Done(), then take the root.
class Scanner {
 public:
  ~Scanner();
  void Start(const std::string& path, const std::vector<std::string>& skip);
  void Cancel() { cancel_ = true; }
  bool Done() const { return done_.load(); }
  bool Cancelled() const { return cancel_.load(); }
  Node* TakeRoot();  // caller owns; valid once Done()

  uint64_t Items() const { return items_.load(); }
  uint64_t Bytes() const { return bytes_.load(); }
  std::string CurrentPath();

  // Synchronous scan, used to refresh a subtree after a partial delete.
  static Node* ScanSync(const std::string& path);

 private:
  static void* ThreadMain(void* self);
  void Walk(Node* dir, const std::string& path, uint64_t dev);

  pthread_t thread_{};
  bool started_ = false;
  std::mutex mutex_;
  std::string root_path_;
  std::string current_;
  std::vector<std::string> skip_;
  Node* root_ = nullptr;
  std::atomic<bool> done_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<uint64_t> items_{0};
  std::atomic<uint64_t> bytes_{0};
  std::vector<std::pair<uint64_t, uint64_t>> hardlinks_;  // (dev, ino), sorted
};

// Deletes a list of paths on its own thread. Never touches the tree.
class Deleter {
 public:
  struct Result {
    Node* node;
    bool gone;  // path no longer exists
  };
  ~Deleter();
  void Start(const std::vector<Node*>& targets);
  bool Done() const { return done_.load(); }
  int Index() const { return index_.load(); }
  size_t Total() const { return paths_.size(); }
  uint64_t Freed() const { return freed_.load(); }
  uint64_t Errors() const { return errors_.load(); }
  std::string CurrentPath();
  const std::vector<Result>& Results() const { return results_; }

 private:
  static void* ThreadMain(void* self);
  void Remove(const std::string& path, uint64_t dev);

  pthread_t thread_{};
  bool started_ = false;
  std::mutex mutex_;
  std::vector<Node*> targets_;
  std::vector<std::string> paths_;
  std::vector<Result> results_;
  std::string current_;
  std::atomic<bool> done_{false};
  std::atomic<int> index_{0};
  std::atomic<uint64_t> freed_{0};
  std::atomic<uint64_t> errors_{0};
};

// The biggest regular files below `root`.
std::vector<Node*> BiggestFiles(Node* root, size_t limit);
