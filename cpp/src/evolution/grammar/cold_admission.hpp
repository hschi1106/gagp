#pragma once

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
#include "gagp/evolution/grammar/membership.hpp"
#include "../../runtime/payload/staging.hpp"

namespace gagp::evo::grammar::detail {

// Called only by the cold wrapper below, inside its speculative read scope.
DerivationMetadata reconstruct_cold_import(const CompiledGrammar& grammar,const ProgramGenome& genome,
    const GenerationRequest& request,VerifiedAst* verified,std::vector<std::vector<int>>* environments);

// Internal accessor: requires a validated private certificate and an active read scope.
std::shared_ptr<const BytecodeProgram> cold_import_bytecode(const CompiledGrammar& grammar,
    const ProgramGenome& genome,const DerivationMetadata& witness,const GenerationRequest& request,
    const std::vector<std::string>* inputs);

// A cold read scope ends before its candidate can escape. It never commits
// writes, and an inconsistent read or speculative failure retries ordinarily.
template<class Admission>
auto snapshot_cold_admission(Admission admit) -> decltype(admit()) {
  if(payload::StagedPayloads::has_active_scope())return admit();
  payload::StagedPayloads reads;
  decltype(admit()) candidate;
  std::exception_ptr failure;
  {
    payload::StagedPayloads::Scope scope(reads);
    try {candidate=admit();}catch(...){failure=std::current_exception();}
  }
  if(!reads.read_snapshot_unchanged())return admit();
  if(failure)std::rethrow_exception(failure);
  return candidate;
}

inline DerivationMetadata reconstruct_cold_derivation(const CompiledGrammar& grammar,
    const ProgramGenome& genome,const GenerationRequest& request,VerifiedAst* verified=nullptr,
    std::vector<std::vector<int>>* environments=nullptr,
    std::shared_ptr<const BytecodeProgram>* executable=nullptr,const std::vector<std::string>* inputs=nullptr) {
  const char* enabled=std::getenv("GAGP_COLD_PAYLOAD_PROOFS");
  const auto ordinary=[&] {
    auto witness=reconstruct_derivation(grammar,genome,request,verified,environments);
    if(executable)executable->reset();
    return witness;
  };
  if(!enabled || std::string(enabled)!="1" || payload::StagedPayloads::has_active_scope())return ordinary();
  const bool registry=std::any_of(genome.ast.consts.begin(),genome.ast.consts.end(),[](const auto& value) {
    return value.tag==ValueTag::String || value.tag==ValueTag::IntList ||
        value.tag==ValueTag::FloatList || value.tag==ValueTag::StringList;
  });
  if(!registry)return ordinary();
  VerifiedAst prepared;
  std::vector<std::vector<int>> lexical;
  struct Candidate {DerivationMetadata witness;std::shared_ptr<const BytecodeProgram> executable;};
  auto candidate=snapshot_cold_admission([&] {
    Candidate result;
    result.witness=reconstruct_cold_import(grammar,genome,request,verified?&prepared:nullptr,environments?&lexical:nullptr);
    if(executable)result.executable=cold_import_bytecode(grammar,genome,result.witness,request,inputs);
    return result;
  });
  if(verified)*verified=std::move(prepared);
  if(environments)*environments=std::move(lexical);
  if(executable)*executable=std::move(candidate.executable);
  return std::move(candidate.witness);
}

} // namespace gagp::evo::grammar::detail
