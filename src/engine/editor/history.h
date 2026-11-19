#ifndef MYTHOS_ENGINE_EDITOR_HISTORY_H_
#define MYTHOS_ENGINE_EDITOR_HISTORY_H_
// Undo and redo by snapshots of the project. The editor calls Commit() when
// nothing is being dragged or typed: a change made since the last commit
// becomes one undo step, however many frames it took.

#include <deque>

#include "engine/editor/project.h"

namespace mythos::editor {

class History {
 public:
  explicit History(size_t limit = 200) : limit_(limit) {}
  // Starts from the project as it is now (after loading it).
  void Reset(const Project& p) {
    undo_.clear();
    redo_.clear();
    committed_ = p.Take();
    revision_ = p.Revision();
  }
  // Records the project's change since the last commit, if there is one.
  bool Commit(const Project& p) {
    if (p.Revision() == revision_) return false;
    revision_ = p.Revision();
    undo_.push_back(std::move(committed_));
    if (undo_.size() > limit_) undo_.pop_front();
    committed_ = p.Take();
    redo_.clear();
    return true;
  }
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  bool Undo(Project* p) {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(committed_));
    committed_ = std::move(undo_.back());
    undo_.pop_back();
    p->Restore(committed_);
    revision_ = p->Revision();
    return true;
  }
  bool Redo(Project* p) {
    if (redo_.empty()) return false;
    undo_.push_back(std::move(committed_));
    committed_ = std::move(redo_.back());
    redo_.pop_back();
    p->Restore(committed_);
    revision_ = p->Revision();
    return true;
  }

 private:
  size_t limit_;
  std::deque<Snapshot> undo_, redo_;
  Snapshot committed_;
  unsigned revision_ = 0;
};

}  // namespace mythos::editor

#endif  // MYTHOS_ENGINE_EDITOR_HISTORY_H_
