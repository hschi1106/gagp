replace('cpp/tests/runtime/test_fitness_contract.cpp','#include "../../src/runtime/gpu/case_reuse.hpp"',
    '#include "../../src/runtime/gpu/case_reuse.hpp"\n#include "../../src/runtime/gpu/shape_partition_cache.hpp"')
path=root/'cpp/tests/runtime/test_fitness_contract.cpp';text=changes[path]
marker='bool test_exact_length_bound() {'
addition=r'''bool test_projected_shape_partitions() {
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

'''
assert text.count(marker)==1
changes[path]=text.replace(marker,addition+marker)
replace('cpp/tests/runtime/test_fitness_contract.cpp','!test_shape_case_partition())return 1;',
    '!test_shape_case_partition() || !test_projected_shape_partitions())return 1;')
