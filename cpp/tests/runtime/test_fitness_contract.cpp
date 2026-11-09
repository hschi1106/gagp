#include <cmath>
#include "../../src/evolution/repro/gpu/completed_fitness_reuse.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <algorithm>
#include <string>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "../../src/runtime/gpu/payload_workspace_budget.hpp"
#include "../../src/runtime/gpu/case_sort_policy.hpp"
#include "../../src/runtime/gpu/case_reuse.hpp"
#include "../../src/runtime/gpu/shape_partition_cache.hpp"
#include "gagp/core/value_semantics.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::Value;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool score_is(const Value& actual, const Value& expected, double wanted,
              const std::string& context) {
  double score = 0.0;
  const bool scored =
      gagp::vm_semantics::fitness_score_for_values(actual, expected, 5.0, score);
  return check(scored && std::fabs(score - wanted) < 1e-12, context);
}

gagp::BytecodeProgram returns(const Value& value) {
  gagp::BytecodeProgram program;
  program.consts = {value};
  program.code = {
      {gagp::Opcode::PushConst, 0, 0, true, false},
      {gagp::Opcode::Return, 0, 0, false, false},
  };
  return program;
}

bool test_completed_fitness_revision() {
  using gagp::evo::repro::CompletedFitnessReuse;
  gagp::FitnessEvalResult completed;completed.ok=true;completed.fitness={-1,-2,-3};
  CompletedFitnessReuse cache;
  if(!check(cache.mapping(1).empty(),"cold completed fitness mapping reused"))return false;
  cache.record(completed,1);
  if(!check(cache.mapping(1)==std::vector<int>({0,1,2}) && cache.mapping(2).empty() &&
      cache.mapping(0).empty(),"completed fitness crossed revision"))return false;
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

bool test_arena_case_sort_proof() {
  gagp::FitnessPolicy policy{gagp::FitnessMetric::Levenshtein};
  using gagp::gpu_detail::arena_case_sort_is_exact;
  if(!check(arena_case_sort_is_exact(policy,1000,1024) && arena_case_sort_is_exact(policy,-2,1024) &&
      arena_case_sort_is_exact(policy,0,1024),"integral sequence sort rejected"))return false;
  for(double penalty:{.5,2.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    if(!check(!arena_case_sort_is_exact(policy,penalty,1024),"nonintegral sort admitted"))return false;
  if(!check(!arena_case_sort_is_exact(policy,1099511627776.0,1),"inexact accumulator bound admitted"))return false;
  policy.proximity=gagp::FitnessProximity::Discounted;
  if(!check(!arena_case_sort_is_exact(policy,1000,1024),"discounted sort admitted"))return false;
  for(auto metric:{gagp::FitnessMetric::Legacy,gagp::FitnessMetric::Absolute,gagp::FitnessMetric::Exact,gagp::FitnessMetric::TupleSummary}) {
    policy={metric};
    if(!check(!arena_case_sort_is_exact(policy,1000,1024),"unproved metric admitted for arena sort"))return false;
  }
  return true;
}

bool test_payload_workspace_budget() {
  using gagp::gpu_detail::payload_workspace_block_count;
  constexpr std::size_t mib=1024u*1024u;
  for(const std::size_t bytes:{64*mib,512*mib,8193*mib,gagp::gpu_detail::kPayloadWorkspaceBytes}) {
    const std::size_t lane_bytes=2*mib+37,extra=12345;
    const auto blocks=payload_workspace_block_count(bytes,1024,lane_bytes,extra);
    if(!check(blocks*32*lane_bytes+extra<=bytes &&
        (blocks==1024 || (blocks+1)*32*lane_bytes+extra>bytes),
        "workspace capacity does not obey exact configured cap"))return false;
  }
  if(!check(payload_workspace_block_count(8192*mib,1024,mib,0)==256 &&
      payload_workspace_block_count(16384*mib,1024,mib,0)==512 &&
      payload_workspace_block_count(16384*mib,1024,mib,1)==511,
      "large workspace cap preserves full-warp capacity and metadata boundary"))return false;
  return check(payload_workspace_block_count(64*mib,5,2*mib,0)==1 &&
      payload_workspace_block_count(64*mib,5,2*mib,1)==0 &&
      payload_workspace_block_count(64*mib,5,1,64*mib)==0 &&
      payload_workspace_block_count(64*mib,5,std::numeric_limits<std::size_t>::max(),0)==0 &&
      payload_workspace_block_count(64*mib,0,1,0)==0,
      "workspace zero capacity or overflow boundary");
}

bool test_integer_case_reuse() {
  std::vector<Value> values(1024*64),answers;
  std::vector<unsigned char> assigned(values.size(),0);
  for(int i=0;i<1024;++i){values[i*64]=Value::from_int(i%7);assigned[i*64]=1;answers.push_back(Value::from_int(i%7+1));}
  gagp::FitnessPolicy policy{gagp::FitnessMetric::Absolute};
  using gagp::gpu_detail::integer_case_reuse_plan;
  auto plan=integer_case_reuse_plan(values,assigned,answers,policy,1000);
  unsigned total=0;double weighted=0,ordinary=0;
  for(std::size_t i=0;i<plan.weights.size();++i){total+=plan.weights[i];
    weighted+=plan.weights[i]*gagp::fitness_case_score(Value::from_int(0),answers[plan.representatives[i]],1000,policy);}
  for(const auto answer:answers)ordinary+=gagp::fitness_case_score(Value::from_int(0),answer,1000,policy);
  if(!check(plan.representatives.size()==7 && total==1024 && ordinary==weighted,"exact case multiplicity or score lost"))return false;
  answers[0]=Value::from_int(99);
  plan=integer_case_reuse_plan(values,assigned,answers,policy,1000);
  if(!check(plan.representatives.size()==8,"case reuse merged different targets"))return false;
  assigned[64]=0;
  plan=integer_case_reuse_plan(values,assigned,answers,policy,1000);
  if(!check(plan.representatives.size()==9,"case reuse merged missing effective bindings"))return false;
  for(auto metric:{gagp::FitnessMetric::Legacy,gagp::FitnessMetric::TupleSummary,gagp::FitnessMetric::Levenshtein}) {
    policy.metric=metric;
    if(!check(integer_case_reuse_plan(values,assigned,answers,policy,1000).weights.empty(),"unproved metric admitted for weighted execution"))return false;
  }
  policy.metric=gagp::FitnessMetric::Absolute;policy.proximity=gagp::FitnessProximity::Discounted;
  if(!check(integer_case_reuse_plan(values,assigned,answers,policy,1000).weights.empty(),"fractional policy admitted"))return false;
  policy.proximity=gagp::FitnessProximity::Capped;
  if(!check(integer_case_reuse_plan(values,assigned,answers,policy,.5).weights.empty() &&
      integer_case_reuse_plan(values,assigned,answers,policy,1099511627776.0).weights.empty(),"fractional or excessive error bound admitted"))return false;
  values[0]=Value::from_float(1.5);
  return check(integer_case_reuse_plan(values,assigned,answers,policy,1000).weights.empty(),"floating input admitted for integer reuse");
}

bool test_shape_case_partition() {
  std::vector<Value> values(1024*64);
  std::vector<unsigned char> assigned(values.size(),0);
  for(int i=0;i<1024;++i) {
    values[i*64]=Value::from_int_list_hash_len(i+1,i%8);
    values[i*64+1]=Value::from_int((i/8)%2);
    assigned[i*64]=assigned[i*64+1]=1;
  }
  using gagp::gpu_detail::shape_case_plan;
  auto plan=shape_case_plan(values,assigned);
  if(!check(plan.representatives.size()==16 && plan.groups.size()==1024,
      "shape partition must retain scalar bindings but ignore list contents"))return false;
  for(int i=0;i<1024;++i) {
    const auto representative=plan.representatives[plan.groups[i]];
    if(!check(Value::container_len(values[i*64])==Value::container_len(values[representative*64]) &&
        values[i*64+1].i==values[representative*64+1].i,"non-equivalent input shapes merged"))return false;
  }
  assigned[1]=0;
  if(!check(shape_case_plan(values,assigned).representatives.size()==17,"missing scalar binding merged"))return false;
  values[0].b=true;
  if(!check(shape_case_plan(values,assigned).groups.empty(),"public offset view admitted without proof"))return false;
  values[0]=Value::from_float(1.0);
  if(!check(shape_case_plan(values,assigned).groups.empty(),"Float input shape admitted"))return false;
  for(int i=0;i<1024;++i)values[i*64]=Value::from_int(i);
  return check(shape_case_plan(values,assigned).groups.empty(),"insufficient repetition admitted for shape cache");
}

bool test_projected_shape_partitions() {
  using gagp::gpu_detail::ShapePartitionCache;
  std::vector<Value> values(128*64);
  std::vector<unsigned char> assigned(values.size(),0);
  for(int i=0;i<128;++i) {
    values[i*64]=Value::from_int_list_hash_len(i+1,i%8);assigned[i*64]=1;
    values[i*64+1]=Value::from_int(i);assigned[i*64+1]=1;
  }
  ShapePartitionCache cache;
  const auto* projected=cache.find_or_add(values,assigned,1);
  if(!check(projected && projected->count==8 && cache.dirty,"unused scalar prevented projected reuse"))return false;
  const auto group_offset=projected->group_offset;
  for(int i=0;i<128;++i)if(!check(cache.groups[group_offset+i]==i%8,"projected group mapping lost a case"))return false;
  cache.dirty=false;
  const auto* same=cache.find_or_add(values,assigned,1);
  if(!check(same && same->count==8 && !cache.dirty && cache.groups.size()==128,
      "existing partition was rebuilt or uploaded again"))return false;
  if(!check(!cache.find_or_add(values,assigned,3) && cache.partitions.size()==2 && !cache.dirty,
      "used varying scalar ignored or declined partition not cached"))return false;
  values[1]=Value::from_float(1.25);assigned[65]=0;
  cache.clear();
  if(!check(cache.groups.empty() && cache.representatives.empty() && cache.partitions.empty() && !cache.dirty,
      "new session retained old case partitions"))return false;
  if(!check(cache.find_or_add(values,assigned,1) && !cache.find_or_add(values,assigned,3),
      "unused Float/missing input was read, or used Float was admitted"))return false;
  values[1]=Value::from_int(0);
  for(int i=0;i<128;++i)values[i*64+1]=Value::from_int(i%2);
  assigned[65]=1;cache.clear();
  assigned[1]=0;
  const auto* missing=cache.find_or_add(values,assigned,3);
  if(!check(missing && missing->count==9,"used missing binding merged with a present binding"))return false;
  cache.clear();
  for(int i=0;i<64;++i)(void)cache.find_or_add(values,assigned,std::uint64_t{1}<<i);
  if(!check(cache.partitions.size()==64 && !cache.find_or_add(values,assigned,0),
      "partition-count cap did not bound session metadata"))return false;
  cache.clear();cache.groups.resize(ShapePartitionCache::max_metadata_ints-1);
  return check(!cache.find_or_add(values,assigned,1) && cache.groups.size()==ShapePartitionCache::max_metadata_ints-1 && !cache.dirty,
      "partition upload exceeded the4MiB GPU metadata cap");
}

bool test_exact_length_bound() {
  std::mt19937 random(2718);
  const double penalties[] = {0, -0.0, .25, 1, 2.5, -3, 20,
      std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
  for (int sample=0; sample<200; ++sample) {
    const int n=random()%12, m=random()%12;
    std::vector<Value> a, b;
    for(int i=0;i<n;++i) a.push_back(sample%2 ? Value::from_char('a'+random()%3) : Value::from_int(random()%3));
    for(int j=0;j<m;++j) b.push_back(sample%2 ? Value::from_char('a'+random()%3) : Value::from_int(random()%3));
    // Independent full matrix oracle; no cap or length shortcut.
    std::vector<std::vector<unsigned>> distance(n+1,std::vector<unsigned>(m+1));
    for(int i=0;i<=n;++i)distance[i][0]=i;
    for(int j=0;j<=m;++j)distance[0][j]=j;
    for(int i=1;i<=n;++i)for(int j=1;j<=m;++j)
      distance[i][j]=std::min({distance[i-1][j]+1,distance[i][j-1]+1,
          distance[i-1][j-1]+unsigned(a[i-1].i!=b[j-1].i)});
    for(double penalty:penalties) for(auto proximity:{gagp::FitnessProximity::Capped,gagp::FitnessProximity::Discounted}) {
      gagp::FitnessPolicy policy{gagp::FitnessMetric::Levenshtein,proximity};
      std::vector<unsigned> row(m+1,999);
      const double d=distance[n][m];
      const double expected=proximity==gagp::FitnessProximity::Capped ? -std::fmin(d,std::fabs(penalty)) : -d/(1+d);
      const auto actual=gagp::fitness_sequence_score(a.data(),n,b.data(),m,penalty,policy,row.data());
      if(!check(actual==expected,"length-bound score differs from full edit matrix"))return false;
      const bool shortcut=gagp::fitness_sequence_length_saturates(n,m,penalty,policy);
      if(!check(!shortcut || row[0]==999,"length bound still evaluated DP"))return false;
      if(!check((n!=m && proximity==gagp::FitnessProximity::Capped) || !shortcut,
          "equal length or discounted score took shortcut"))return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  if(!test_completed_fitness_revision() || !test_arena_case_sort_proof() || !test_payload_workspace_budget() || !test_exact_length_bound() || !test_integer_case_reuse() || !test_shape_case_partition() || !test_projected_shape_partitions())return 1;
  gagp::payload::clear();
  if (!score_is(Value::from_int(3), Value::from_int(3), 0.0, "numeric exact")) return 1;
  if (!score_is(Value::from_float(7.5), Value::from_int(5), -2.5,
                "cross-numeric difference")) return 1;
  if (!score_is(Value::from_int(20), Value::from_int(0), -5.0,
                "numeric penalty clamp")) return 1;
  if (!score_is(Value::from_bool(true), Value::from_int(1), -5.0,
                "non-numeric actual for numeric expected")) return 1;
  if (!score_is(Value::from_bool(true), Value::from_bool(true), 1.0, "bool exact")) return 1;
  if (!score_is(Value::from_bool(false), Value::from_bool(true), 0.0, "bool mismatch")) return 1;
  if (!score_is(Value::from_char('a'), Value::from_char('a'), 1.0, "char exact")) return 1;

  const Value string_a = gagp::payload::make_string_value("same");
  const Value string_b = gagp::payload::make_string_value("different");
  if (!score_is(string_a, string_a, 1.0, "string exact")) return 1;
  if (!score_is(string_b, string_a, 0.0, "string mismatch")) return 1;
  const Value ints = gagp::payload::make_int_list_value({Value::from_int(1)});
  const Value floats = gagp::payload::make_float_list_value({Value::from_float(1.0)});
  if (!score_is(ints, ints, 1.0, "typed-list exact")) return 1;
  if (!score_is(floats, ints, -5.0, "typed-list type mismatch")) return 1;

  const std::vector<gagp::CaseBindings> cases(2);
  const auto solved = gagp::eval_fitness_cpu(
      {returns(Value::from_bool(true))}, cases,
      {Value::from_bool(true), Value::from_bool(true)}, 100, 5.0);
  if (!check(solved.size() == 1 && solved[0] == 2.0,
             "two exact discrete cases define solved score 2")) return 1;

  gagp::BytecodeProgram runtime_error;
  runtime_error.code = {{gagp::Opcode::PushConst, 99, 0, true, false}};
  const auto failed = gagp::eval_fitness_cpu(
      {runtime_error}, {gagp::CaseBindings{}}, {Value::from_int(0)}, 100, 5.0);
  if (!check(failed.size() == 1 && failed[0] == -5.0,
             "runtime error must score one penalty")) return 1;

  const gagp::FitnessPolicy discounted{gagp::FitnessMetric::Absolute,gagp::FitnessProximity::Discounted};
  const auto distance=gagp::eval_fitness_cpu({returns(Value::from_int(3)),runtime_error},
      {{}},{Value::from_int(0)},100,5,1,discounted);
  if(!check(distance==std::vector<double>{-.75,-1.0},"discounted error and runtime ceiling"))return 1;
  const auto list_a=gagp::payload::make_int_list_value({Value::from_int(1),Value::from_int(3)});
  const auto list_b=gagp::payload::make_int_list_value({Value::from_int(1),Value::from_int(2),Value::from_int(3)});
  const gagp::FitnessPolicy edit_distance{gagp::FitnessMetric::Levenshtein};
  const auto edits=gagp::eval_fitness_cpu({returns(list_a)},{{}},{list_b},100,5,1,edit_distance);
  if(!check(edits==std::vector<double>{-1},"Levenshtein insertion uses full payload"))return 1;
  const auto strings=gagp::eval_fitness_cpu({returns(gagp::payload::make_string_value("kitten"))},{{}},
      {gagp::payload::make_string_value("sitting")},100,10,1,edit_distance);
  if(!check(strings==std::vector<double>{-3},"Levenshtein string distance"))return 1;
  gagp::FitnessPolicy tuple{gagp::FitnessMetric::TupleSummary};tuple.summary_count=2;tuple.summary_cap=10;
  const auto tuple_a=gagp::payload::make_int_list_value({Value::from_int(1),Value::from_int(5),Value::from_int(30)});
  const auto tuple_b=gagp::payload::make_int_list_value({Value::from_int(1),Value::from_int(0),Value::from_int(0)});
  const auto summaries=gagp::eval_fitness_cpu({returns(tuple_a)},{{}},{tuple_b},100,10,1,tuple);
  if(!check(std::fabs(summaries[0]+.15)<1e-12,"tuple weighted capped summaries"))return 1;
  const gagp::FitnessPolicy exact{gagp::FitnessMetric::Exact};
  if(!check(gagp::fitness_case_score(Value::from_int(INT64_MAX),Value::from_int(INT64_MAX-1),5,exact)==-1,
      "exact integer comparison retains low bits"))return 1;
  std::vector<unsigned> wrong;
  const auto tuple_counts=gagp::eval_fitness_cpu({returns(tuple_a),runtime_error},{{}},{tuple_b},100,10,1,tuple,&wrong);
  if(!check(wrong==std::vector<unsigned>{0,1},"tuple solved predicate ignores summary distance; error is wrong"))return 1;
  const Value large_actual[]={Value::from_int(1),Value::from_int(INT64_MAX)};
  const Value large_expected[]={Value::from_int(1),Value::from_int(INT64_MAX-1)};
  tuple.summary_count=1;unsigned scratch[3];
  if(!check(std::fabs(gagp::fitness_sequence_score(large_actual,2,large_expected,2,10,tuple,scratch)+.01)<1e-12,
      "tuple summary gap retains low integer bits"))return 1;
  tuple.summary_count=1;
  const auto valid_tuple=gagp::payload::make_int_list_value({Value::from_int(1),Value::from_int(2)});
  const auto bad_fields=Value::from_int_list_hash_len(998877,2);
  gagp::payload::register_list(bad_fields,{Value::from_int(1),Value::from_float(2)});
  const auto bad_length=gagp::payload::make_int_list_value({Value::from_int(1)});
  const auto bad_tag=gagp::payload::make_float_list_value({Value::from_float(1),Value::from_float(2)});
  for(const auto invalid:{bad_fields,bad_length,bad_tag}) {
    bool rejected=false;
    try{(void)gagp::eval_fitness_cpu({returns(valid_tuple)},{{}},{invalid},100,10,1,tuple);}
    catch(const std::invalid_argument&){rejected=true;}
    if(!check(rejected,"malformed tuple answer rejected at setup"))return 1;
    const auto bad_score=gagp::eval_fitness_cpu({returns(invalid)},{{}},{valid_tuple},100,10,1,tuple,&wrong);
    if(!check(wrong==std::vector<unsigned>{1} && std::fabs(bad_score[0]+1.1)<1e-12,
        "malformed tuple actual receives ceiling and wrong case"))return 1;
  }
  std::cout << "gagp_test_fitness_contract: OK\n";
  return 0;
}
