#pragma once

#include <memory>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>
#include "gagp/core/value.hpp"

namespace gagp::payload {

// Internal generation transaction. Each scope belongs to one worker. Commit only
// after every scope has ended; conflicts leave the registry unchanged so callers
// can replay generation sequentially.
class PayloadBudgetExceeded : public std::runtime_error {
 public:
  explicit PayloadBudgetExceeded(const char* message = "CPU fitness payload byte budget exceeded")
      : std::runtime_error(message) {}
};

// Invocation-local logical list budget, independent of physical payload storage.
// Input cells are charged once by the caller; slices and empty concat are free.
class ListAllocationScope {
 public:
  ListAllocationScope(std::size_t limit, std::size_t initial_cells);
  ~ListAllocationScope();
  ListAllocationScope(const ListAllocationScope&) = delete;
  ListAllocationScope& operator=(const ListAllocationScope&) = delete;
 private:
  std::size_t previous_limit_, previous_used_;
};
bool list_allocation_budget_active();
void charge_list_allocation(std::size_t cells);

// Borrows an invocation-owned staged vector, otherwise copies under the registry
// lock into scratch. The returned pointer is only valid until the next payload
// registration or scope exit; callers must finish reads before publishing output.
const std::vector<Value>* read_list(const Value& key, std::vector<Value>& scratch);
// Transfers a freshly constructed list into the active overlay. Validation,
// content identity and cumulative byte charging match the public constructors.
Value make_owned_list_value(ValueTag tag, std::vector<Value>&& elems);

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
