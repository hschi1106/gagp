#include "gagp/core/region_executable.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include <algorithm>
#include <stdexcept>

namespace gagp {
namespace {
void require(bool valid,const char* reason){if(!valid)throw std::invalid_argument(std::string("region executable: ")+reason);}
std::vector<RegionPhase*> phases(BytecodeProgram& p) {
  auto& s=p.bounded_region_segments.at(0);std::vector<RegionPhase*> result{&s.base_predicate,&s.base_body};
  for(auto& phase:s.preparations)result.push_back(&phase);
  for(auto& phase:s.request_expressions)result.push_back(&phase);
  result.push_back(&s.combine);if(s.boundary)result.push_back(&*s.boundary);return result;
}
const RegionPhase& phase_at(const BytecodeProgram& p,std::size_t slot) {
  const auto& s=p.bounded_region_segments.at(0);
  if(slot==0)return s.base_predicate;if(slot==1)return s.base_body;slot-=2;
  if(slot<s.preparations.size())return s.preparations[slot];slot-=s.preparations.size();
  if(slot<s.request_expressions.size())return s.request_expressions[slot];slot-=s.request_expressions.size();
  if(slot==0)return s.combine;if(slot==1 && s.boundary)return *s.boundary;
  throw std::invalid_argument("region executable: phase slot out of range");
}
void scalar_constants(const std::vector<Value>& values) {
  for(const auto& v:values)require(v.tag==ValueTag::Int || v.tag==ValueTag::Bool ||
      v.tag==ValueTag::Float || v.tag==ValueTag::Char,"registry-dependent constants unsupported");
}
BytecodeVerifyOptions options(){BytecodeVerifyOptions o;o.max_locals_per_code=64;o.max_stack_depth=64;return o;}
int checked(const BytecodeVerifyResult& result){if(!result)throw std::invalid_argument("region executable: "+result.diagnostic.message);return result.verified.max_stack_depth;}
}
std::shared_ptr<const RegionExecutableLayout> RegionExecutableLayout::admit(BytecodeProgram source) {
  require(source.bounded_region_segments.size()==1,"requires exactly one region");
  const auto& plan=source.bounded_region_segments[0].plan;
  require(plan.limits.frames<=128 && plan.limits.cells<=128,"GPU region capacity exceeded");
  scalar_constants(source.consts);for(const auto* phase:phases(source))scalar_constants(phase->program.consts);
  const auto bound=checked(verify_bytecode(source,options()));
  auto result=std::shared_ptr<RegionExecutableLayout>(new RegionExecutableLayout);
  result->source_=std::move(source);result->original_stack_bound_=bound;return result;
}
std::size_t RegionExecutableLayout::phase_count() const {
  const auto& s=source_.bounded_region_segments[0];return 3+s.preparations.size()+s.request_expressions.size()+(s.boundary?1:0);
}
RegionExecutableLayout::Phase RegionExecutableLayout::initial_phase(std::size_t slot) const {
  const auto& original=phase_at(source_,slot);
  auto phase=std::shared_ptr<RegionExecutablePhase>(new RegionExecutablePhase);
  phase->owner_=shared_from_this();phase->slot_=slot;phase->code_=original;
  phase->stack_bound_=original_stack_bound_;return phase;
}
RegionExecutableLayout::Phase RegionExecutableLayout::admit_phase(std::size_t slot,RegionPhase source) const {
  (void)phase_at(source_,slot);scalar_constants(source.program.consts);
  const auto bound=checked(verify_bounded_region_phase(source,source_.bounded_region_segments[0].plan,slot,options()));
  auto phase=std::shared_ptr<RegionExecutablePhase>(new RegionExecutablePhase);
  phase->owner_=shared_from_this();phase->slot_=slot;phase->code_=std::move(source);phase->stack_bound_=bound;return phase;
}
RegionExecutable RegionExecutable::compose(std::shared_ptr<const RegionExecutableLayout> layout,
    std::vector<RegionExecutableLayout::Phase> phases) {
  require(bool(layout) && phases.size()==layout->phase_count(),"wrong layout/phase count");
  for(std::size_t i=0;i<phases.size();++i)
    require(phases[i] && phases[i]->owner_==layout && phases[i]->slot_==i,"wrong owner/grammar-independent layout/phase slot");
  RegionExecutable result;result.layout_=std::move(layout);result.phases_=std::move(phases);return result;
}
BytecodeProgram RegionExecutable::materialize() const {
  require(bool(layout_),"empty or stale executable");
  const auto& root=layout_->source_;const auto& original=root.bounded_region_segments[0];
  BytecodeProgram source;
  source.code=root.code;source.consts=root.consts;source.n_locals=root.n_locals;
  source.var2idx=root.var2idx;source.instruction_fuel=root.instruction_fuel;
  source.bounded_region_segments.resize(1);auto& region=source.bounded_region_segments[0];
  region.plan=original.plan;region.parameter_locals=original.parameter_locals;
  region.preparations.resize(original.preparations.size());
  region.request_expressions.resize(original.request_expressions.size());
  if(original.boundary)region.boundary.emplace();
  auto rows=phases(source);
  for(std::size_t i=0;i<rows.size();++i)*rows[i]=phases_[i]->code_;return source;
}
int RegionExecutable::stack_bound() const {
  require(bool(layout_),"empty or stale executable");int bound=layout_->original_stack_bound_;
  for(const auto& phase:phases_)bound=std::max(bound,phase->stack_bound_);return bound;
}
std::array<RegionExecutableBatch,2> RegionExecutableBatch::partition(
    const std::vector<unsigned char>& routes) && {
  require(routes.size()==programs_.size(),"partition size mismatch");
  std::array<std::size_t,2> counts{};
  for(auto route:routes){require(route<2,"invalid partition route");++counts[route];}
  std::array<RegionExecutableBatch,2> result{RegionExecutableBatch{}, RegionExecutableBatch{}};
  for(std::size_t b=0;b<2;++b){result[b].programs_.reserve(counts[b]);result[b].stack_bounds_.reserve(counts[b]);}
  for(std::size_t i=0;i<routes.size();++i) {
    auto& part=result[routes[i]];
    part.programs_.push_back(std::move(programs_[i]));part.stack_bounds_.push_back(stack_bounds_[i]);
  }
  programs_.clear();stack_bounds_.clear();return result;
}
RegionExecutableBatch::RegionExecutableBatch(const std::vector<RegionExecutable>& programs) {
  programs_.reserve(programs.size());stack_bounds_.reserve(programs.size());
  for(const auto& program:programs){programs_.push_back(program.materialize());stack_bounds_.push_back(program.stack_bound());}
}
}  // namespace gagp
