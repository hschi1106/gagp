#pragma once

#include <memory>
#include <vector>

namespace gagp::payload {

// Internal generation transaction. Each scope belongs to one worker. Commit only
// after every scope has ended; conflicts leave the registry unchanged so callers
// can replay generation sequentially.
class StagedPayloads {
 public:
  struct State;
  StagedPayloads();
  ~StagedPayloads();
  StagedPayloads(const StagedPayloads&) = delete;
  StagedPayloads& operator=(const StagedPayloads&) = delete;
  class Scope {
   public:
    explicit Scope(StagedPayloads& transaction);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
   private:
    State* state_;
  };
  static bool commit_all(const std::vector<StagedPayloads*>& transactions);
  static bool has_active_scope();
  // Repeatable validation of a sealed read-only snapshot, including after a
  // successful commit. Never publishes writes or consumes the snapshot.
  // Active/enclosing scopes and any staged writes conservatively return false.
  bool read_snapshot_unchanged() const;
 private:
  std::unique_ptr<State> state_;
};

}  // namespace gagp::payload
