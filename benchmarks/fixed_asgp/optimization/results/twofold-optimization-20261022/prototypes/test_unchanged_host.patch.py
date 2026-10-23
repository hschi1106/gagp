replace('cpp/tests/runtime/test_fitness_contract.cpp','#include <cmath>',
    '#include <cmath>\n#include "../../src/evolution/repro/gpu/completed_fitness_reuse.hpp"')
replace('cpp/tests/runtime/test_fitness_contract.cpp','bool test_arena_case_sort_proof() {',r'''bool test_completed_fitness_revision() {
  using gagp::evo::repro::CompletedFitnessReuse;
  gagp::FitnessEvalResult completed;completed.ok=true;completed.fitness={-1,-2,-3};
  CompletedFitnessReuse cache(true),disabled(false);
  if(!check(cache.mapping(1).empty(),"cold completed fitness mapping reused"))return false;
  cache.record(completed,1);disabled.record(completed,1);
  if(!check(cache.mapping(1)==std::vector<int>({0,1,2}) && cache.mapping(2).empty() &&
      cache.mapping(0).empty() && disabled.mapping(1).empty(),"completed fitness crossed revision or disabled scope"))return false;
  cache.advance({2,-1,0});
  if(!check(cache.mapping(1)==std::vector<int>({2,-1,0}),"exact parent mapping lost"))return false;
  cache.advance({0,1,2});
  if(!check(cache.mapping(1).empty(),"unscored generation gap reused stale parent indices"))return false;
  cache.record(completed,1);
  if(!check(cache.mapping(1)==std::vector<int>({0,1,2}),"fresh completion did not rebase generation"))return false;
  completed.ok=false;cache.record(completed,1);
  if(!check(cache.mapping(1).empty(),"failed evaluation became completed fitness"))return false;
  completed.ok=true;cache.record(completed,2);cache.clear();
  if(!check(cache.mapping(2).empty(),"cleared completed fitness retained entries"))return false;
  bool rejected=false;try{cache.advance({0,3,1});}catch(const std::logic_error&){rejected=true;}
  if(!check(rejected,"out-of-range proof parent was accepted"))return false;
  return true;
}

bool test_arena_case_sort_proof() {''')
replace('cpp/tests/runtime/test_fitness_contract.cpp','  if(!test_arena_case_sort_proof()',
    '  if(!test_completed_fitness_revision() || !test_arena_case_sort_proof()')
