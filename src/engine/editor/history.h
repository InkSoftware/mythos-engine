#ifndef MYTHOS_ENGINE_EDITOR_HISTORY_H_
#define MYTHOS_ENGINE_EDITOR_HISTORY_H_

#include <deque>

#include "engine/editor/project.h"

namespace mythos::editor {
// Linear undo and redo over whole-project snapshots. The editor calls `Commit`
// after each batch of edits; a snapshot is taken only when the project's
// revision changed. `committed_` holds the state at the last commit, so undo
// steps back to the snapshot before it. Keeps at most `limit` undo steps.
class History {
 public:
  explicit History(const size_t limit = 200) : limit_(limit) {}

  // Forgets all history and takes `p` as the current state.
  void Reset(const Project& p) {
    undo_.clear();
    redo_.clear();
    committed_ = p.Take();
    revision_ = p.Revision();
  }

  // Records `p` as a new undo step if it changed since the last call (this
  // clears redo). Returns whether it did.
  bool Commit(const Project& p) {
    if (p.Revision() == revision_) return false;
    revision_ = p.Revision();
    undo_.push_back(std::move(committed_));
    if (undo_.size() > limit_) undo_.pop_front();
    committed_ = p.Take();
    redo_.clear();
    return true;
  }

  [[nodiscard]] bool CanUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool CanRedo() const { return !redo_.empty(); }

  // Restores the previous state into `p`. Returns false when there is nothing
  // to undo.
  bool Undo(Project* const p) {
    if (undo_.empty()) return false;
    redo_.push_back(std::move(committed_));
    committed_ = std::move(undo_.back());
    undo_.pop_back();
    p->Restore(committed_);
    revision_ = p->Revision();
    return true;
  }

  // The inverse of `Undo`; false when there is nothing to redo.
  bool Redo(Project* const p) {
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
}

#endif
