#pragma once

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
#include "gagp/evolution/grammar/membership.hpp"
#include "../../runtime/payload/staging.hpp"

namespace gagp::evo::grammar::detail {

// A cold read scope ends before its candidate can escape. It never commits
// writes, and an inconsistent read or speculative failure retries ordinarily.
template<class Admission>
DerivationMetadata snapshot_cold_admission(Admission admit) {
  if(payload::StagedPayloads::has_active_scope())return admit();
  payload::StagedPayloads reads;
  DerivationMetadata candidate;
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
    std::vector<std::vector<int>>* environments=nullptr) {
  const char* enabled=std::getenv("GAGP_COLD_PAYLOAD_PROOFS");
  if(!enabled || std::string(enabled)!="1" || payload::StagedPayloads::has_active_scope())
    return reconstruct_derivation(grammar,genome,request,verified,environments);
  const bool registry=std::any_of(genome.ast.consts.begin(),genome.ast.consts.end(),[](const auto& value) {
    return value.tag==ValueTag::String || value.tag==ValueTag::IntList ||
        value.tag==ValueTag::FloatList || value.tag==ValueTag::StringList;
  });
  if(!registry)
    return reconstruct_derivation(grammar,genome,request,verified,environments);
  VerifiedAst prepared;
  std::vector<std::vector<int>> lexical;
  auto witness=snapshot_cold_admission([&] {
    return reconstruct_derivation(grammar,genome,request,verified?&prepared:nullptr,environments?&lexical:nullptr);
  });
  if(verified)*verified=std::move(prepared);
  if(environments)*environments=std::move(lexical);
  return witness;
}

} // namespace gagp::evo::grammar::detail
