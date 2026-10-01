#pragma once
#include <memory>
#include "gagp/core/bytecode.hpp"

namespace gagp {
class RegionExecutable;
class RegionExecutableLayout;
class RegionExecutablePhase final {
 public:
  const RegionPhase& code() const { return code_; }
 private:
  std::shared_ptr<const RegionExecutableLayout> owner_;
  std::size_t slot_ = 0;
  int stack_bound_ = 0;
  RegionPhase code_;
  RegionExecutablePhase() = default;
  friend class RegionExecutableLayout;
  friend class RegionExecutable;
};
// Bytecode/type/index safety, NOT grammar membership. Scalar constants only;
// shared payload inputs remain owned by the fitness session. No public mutators.
class RegionExecutableLayout final : public std::enable_shared_from_this<RegionExecutableLayout> {
 public:
  using Phase = std::shared_ptr<const RegionExecutablePhase>;
  static std::shared_ptr<const RegionExecutableLayout> admit(BytecodeProgram source);
  Phase initial_phase(std::size_t slot) const;
  Phase admit_phase(std::size_t slot, RegionPhase source) const;
  std::size_t phase_count() const;
 private:
  RegionExecutableLayout() = default;
  BytecodeProgram source_;
  int original_stack_bound_ = 0;
  friend class RegionExecutable;
};
class RegionExecutable final {
 public:
  RegionExecutable() = default;
  static RegionExecutable compose(std::shared_ptr<const RegionExecutableLayout> layout,
      std::vector<RegionExecutableLayout::Phase> phases);
  BytecodeProgram materialize() const;
  int stack_bound() const;
 private:
  std::shared_ptr<const RegionExecutableLayout> layout_;
  std::vector<RegionExecutableLayout::Phase> phases_;
};
// An immutable snapshot binds materialized bytecode to its compositional proof.
// Arbitrary mutable BytecodeProgram vectors cannot construct this certificate.
class RegionExecutableBatch final {
 public:
  explicit RegionExecutableBatch(const std::vector<RegionExecutable>& programs);
  const std::vector<BytecodeProgram>& programs() const { return programs_; }
  const std::vector<int>& stack_bounds() const { return stack_bounds_; }
 private:
  std::vector<BytecodeProgram> programs_;
  std::vector<int> stack_bounds_;
};
}  // namespace gagp
