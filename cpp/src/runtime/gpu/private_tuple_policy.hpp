#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "gagp/core/bytecode.hpp"
#include "gagp/core/builtin.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace gagp::gpu_detail {

// Extra admission for the zero-logical-cap tuple profile. Private descriptors
// have a finite backing-length representation. Prove every concat produces at
// most three cells, while inputs/slices may retain their original length. Region
// results are inductively <=3: base/combine/boundary prove that invariant using
// the same bound for child results. No source task names or answer values enter
// this proof. Ordinary verification and private typeflow must also pass.
//
// The analysis of one code block depends only on its code, constants, initial
// local bounds, result requirement and (for root code) the segment plans. A
// memo keyed by exactly these contents, compared in full, returns the same
// answer as a fresh analysis. Code reading an IntList constant also depends on
// registry contents and is never memoized.
struct TupleProofMemo {
  static constexpr std::size_t kMaxEntries = std::size_t(1) << 20;
  std::unordered_map<std::string,bool> entries;
};

inline bool bounded_tuple_allocations(const BytecodeProgram& program,
    const std::array<ValueTag,64>& input_types, TupleProofMemo* memo = nullptr) {
  struct State { std::vector<int> stack, locals; };
  constexpr int scalar=-1, input_bound=65536, tuple_bound=3;
  const auto analyze=[&](const auto& code,const auto& constants,int local_count,
      const std::vector<int>& initial,bool tuple_result,bool root)->bool {
    if(local_count<0 || local_count>64 || initial.size()!=std::size_t(local_count))return false;
    std::vector<std::optional<State>> states(code.size()+1);
    states[0]=State{{},initial};bool returned=false;
    const auto merge=[&](std::size_t ip,const State& source)->bool {
      if(ip>code.size())return false;
      if(!states[ip]){states[ip]=source;return true;}
      auto& target=*states[ip];
      if(target.stack.size()!=source.stack.size() || target.locals.size()!=source.locals.size())return false;
      for(std::size_t i=0;i<target.stack.size();++i)target.stack[i]=std::max(target.stack[i],source.stack[i]);
      for(std::size_t i=0;i<target.locals.size();++i)target.locals[i]=std::max(target.locals[i],source.locals[i]);
      return true;
    };
    for(std::size_t ip=0;ip<=code.size();++ip) {
      if(!states[ip])continue;
      auto state=*states[ip];
      const auto finish=[&] {
        if(state.stack.empty() || (tuple_result && (state.stack.back()<0 || state.stack.back()>tuple_bound)))return false;
        returned=true;return true;
      };
      if(ip==code.size()){if(!finish())return false;continue;}
      const auto ins=code[ip];
      const auto consume=[&](int n,int result)->bool {
        if(n<0 || state.stack.size()<std::size_t(n))return false;
        state.stack.resize(state.stack.size()-n);state.stack.push_back(result);return true;
      };
      switch(ins.op) {
        case Opcode::PushConst: {
          if(ins.a<0 || std::size_t(ins.a)>=constants.size())return false;
          const auto value=constants[ins.a];int bound=scalar;
          if(value.tag==ValueTag::IntList) {
            std::vector<Value> contents;
            if(!payload::lookup_list(value,&contents) || contents.size()>input_bound)return false;
            bound=contents.size();
          }
          state.stack.push_back(bound);break;
        }
        case Opcode::Load:
          if(ins.a<0 || ins.a>=local_count)return false;
          state.stack.push_back(state.locals[ins.a]);break;
        case Opcode::Store:
          if(ins.a<0 || ins.a>=local_count || state.stack.empty())return false;
          state.locals[ins.a]=state.stack.back();state.stack.pop_back();break;
        case Opcode::Neg:case Opcode::Not:
          if(!consume(1,scalar))return false;break;
        case Opcode::Add:case Opcode::Sub:case Opcode::Mul:case Opcode::Mod:
        case Opcode::Eq:case Opcode::Ne:case Opcode::Lt:case Opcode::Le:case Opcode::Gt:case Opcode::Ge:
          if(!consume(2,scalar))return false;break;
        case Opcode::CheckInt:case Opcode::CheckList:
          if(state.stack.empty())return false;break;
        case Opcode::Jmp:case Opcode::JmpIfTrue:case Opcode::JmpIfFalse:
          if(ins.op!=Opcode::Jmp) {
            if(state.stack.empty())return false;state.stack.pop_back();
          }
          if(ins.a<=int(ip) || !merge(ins.a,state))return false;
          if(ins.op==Opcode::Jmp)continue;
          break;
        case Opcode::CallBuiltin: {
          if(ins.b<0 || state.stack.size()<std::size_t(ins.b))return false;
          int bound=scalar;
          const auto first=state.stack.size()-ins.b;
          switch(static_cast<BuiltinId>(ins.a)) {
            case BuiltinId::Singleton:if(ins.b!=1)return false;bound=1;break;
            case BuiltinId::Concat:
              if(ins.b!=2 || state.stack[first]<0 || state.stack[first+1]<0 ||
                  state.stack[first]>tuple_bound-state.stack[first+1])return false;
              bound=state.stack[first]+state.stack[first+1];break;
            case BuiltinId::Slice:
              if(ins.b!=3 || state.stack[first]<0)return false;
              bound=state.stack[first];break;
            case BuiltinId::Len:case BuiltinId::Index:case BuiltinId::IndexNonnegative:
            case BuiltinId::IndexClampedOrZero:case BuiltinId::IsLetter:case BuiltinId::Clip:
            case BuiltinId::TruncDiv0:case BuiltinId::TruncMod0:case BuiltinId::IDiv0:
            case BuiltinId::IMod0:case BuiltinId::Min:case BuiltinId::Max:break;
            default:return false;
          }
          if(!consume(ins.b,bound))return false;break;
        }
        case Opcode::BoundedRegion: {
          if(!root || ins.a<0 || std::size_t(ins.a)>=program.bounded_region_segments.size())return false;
          const auto& plan=program.bounded_region_segments[ins.a].plan;
          const auto count=plan.state_types.size()+plan.bound_operand_count;
          if(!consume(count,plan.result_type==ValueTag::IntList?tuple_bound:scalar))return false;
          break;
        }
        case Opcode::Return:if(!finish())return false;continue;
        default:return false;
      }
      if(!merge(ip+1,state))return false;
    }
    return returned;
  };
  const auto append=[](std::string& key,const void* data,std::size_t bytes) {
    key.append(static_cast<const char*>(data),bytes);
  };
  const auto memo_analyze=[&](const auto& code,const auto& constants,int local_count,
      const std::vector<int>& initial,bool tuple_result,bool root)->bool {
    if(!memo || std::any_of(constants.begin(),constants.end(),[](const Value& v){return v.tag==ValueTag::IntList;}))
      return analyze(code,constants,local_count,initial,tuple_result,root);
    std::string key;
    key.reserve(16+initial.size()*4+code.size()*12+constants.size()*10);
    const std::uint64_t header[4]={std::uint64_t(local_count),std::uint64_t(tuple_result)|std::uint64_t(root)<<1,
        initial.size(),code.size()};
    append(key,header,sizeof(header));
    append(key,initial.data(),initial.size()*sizeof(int));
    for(const auto& ins:code) {
      const std::int32_t words[3]={std::int32_t(ins.op),ins.a,ins.b};
      append(key,words,sizeof(words));key.push_back(char(ins.has_a | ins.has_b<<1));
    }
    const std::uint64_t count=constants.size();append(key,&count,sizeof(count));
    for(const auto& value:constants){append(key,&value.i,sizeof(value.i));key.push_back(char(value.tag));key.push_back(char(value.b));}
    if(root) {
      const std::uint64_t segments=program.bounded_region_segments.size();append(key,&segments,sizeof(segments));
      for(const auto& segment:program.bounded_region_segments) {
        const std::uint64_t words[2]={segment.plan.state_types.size()+std::uint64_t(segment.plan.bound_operand_count),
            std::uint64_t(segment.plan.result_type)};
        append(key,words,sizeof(words));
      }
    }
    if(const auto found=memo->entries.find(key);found!=memo->entries.end())return found->second;
    const bool result=analyze(code,constants,local_count,initial,tuple_result,root);
    if(memo->entries.size()>=TupleProofMemo::kMaxEntries)memo->entries.clear();
    memo->entries.emplace(std::move(key),result);
    return result;
  };
  for(const auto& segment:program.bounded_region_segments) {
    const auto& plan=segment.plan;
    const auto phase=[&](const RegionPhase& p,bool result)->bool {
      if(p.program.n_locals<0 || p.program.n_locals>64)return false;
      std::vector<int> locals(p.program.n_locals,scalar);
      for(const auto& binding:p.bindings) {
        if(binding.local<0 || std::size_t(binding.local)>=locals.size())return false;
        ValueTag type=ValueTag::Invalid;int bound=input_bound;
        switch(binding.source.bank) {
          case RegionSlotBank::State:type=plan.state_types.at(binding.source.slot);break;
          case RegionSlotBank::Parameter:type=plan.parameter_types.at(binding.source.slot);break;
          case RegionSlotBank::Prepared:type=plan.preparations.at(binding.source.slot).type;break;
          case RegionSlotBank::Result:type=plan.result_type;bound=tuple_bound;break;
          case RegionSlotBank::Measure:type=ValueTag::Int;break;
        }
        locals[binding.local]=type==ValueTag::IntList?bound:scalar;
      }
      return memo_analyze(p.program.code,p.program.consts,p.program.n_locals,locals,
          result && plan.result_type==ValueTag::IntList,false);
    };
    if(!phase(segment.base_predicate,false) || !phase(segment.base_body,true) || !phase(segment.combine,true) ||
        (segment.boundary && !phase(*segment.boundary,true)))return false;
    for(const auto& p:segment.preparations)if(!phase(p,false))return false;
    for(const auto& p:segment.request_expressions)if(!phase(p,false))return false;
  }
  if(program.n_locals<0 || program.n_locals>64)return false;
  std::vector<int> locals(program.n_locals,scalar);
  for(std::size_t i=0;i<locals.size();++i)if(input_types[i]==ValueTag::IntList)locals[i]=input_bound;
  return memo_analyze(program.code,program.consts,program.n_locals,locals,false,true);
}

} // namespace gagp::gpu_detail
