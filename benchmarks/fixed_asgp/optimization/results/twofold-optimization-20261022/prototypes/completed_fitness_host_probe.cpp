#include "completed_fitness_reuse.hpp"
#include <cassert>
#include <iostream>

int main() {
  gagp::FitnessEvalResult result;result.ok=true;result.fitness={-1.,-2.,-3.};
  gagp::evo::repro::CompletedFitnessReuse cache(true),off(false);
  assert(cache.mapping(7).empty());cache.record(result,7);off.record(result,7);
  assert(cache.mapping(7)==std::vector<int>({0,1,2}));
  assert(cache.mapping(8).empty() && cache.mapping(0).empty() && off.mapping(7).empty());
  cache.advance({2,-1,0});assert(cache.mapping(7)==std::vector<int>({2,-1,0}));
  cache.advance({1,0,2});assert(cache.mapping(7).empty());
  cache.record(result,8);assert(cache.mapping(8)==std::vector<int>({0,1,2}));
  result.ok=false;cache.record(result,8);assert(cache.mapping(8).empty());
  result.ok=true;cache.record(result,8);cache.clear();assert(cache.mapping(8).empty());
  bool rejected=false;try{cache.advance({3,0,1});}catch(const std::logic_error&){rejected=true;}
  assert(rejected);
  std::cout << "completed-fitness host scope oracle: PASS\n";
}
