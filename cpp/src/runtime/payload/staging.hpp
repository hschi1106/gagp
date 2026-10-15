#pragma once

#include <memory>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace gagp::payload {

// Internal generation transaction. Each scope belongs to one worker. Commit only
// after every scope has ended; conflicts leave the registry unchanged so callers
// can replay generation sequentially.
class PayloadBudgetExceeded : public std::runtime_error {
 public:
  PayloadBudgetExceeded() : std::runtime_error("CPU fitness payload byte budget exceeded") {}
};

// Check a prospective temporary allocation before constructing it.
void check_allocation_budget(std::size_t bytes);

class StagedPayloads {
 public:
  struct State;
  // A finite limit creates a discard-only evaluation overlay; it cannot commit.
  // Counts cumulative registered payload bytes plus keys, including replacements.
  // Finite scopes release writes at scope exit and may be reused across cases.
  explicit StagedPayloads(std::size_t byte_limit = std::numeric_limits<std::size_t>::max());
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
