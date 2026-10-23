#pragma once

#include <cstdint>
#include <unordered_map>
#include "case_reuse.hpp"

namespace gagp::gpu_detail {

struct ShapePartition {
  int group_offset=0, representative_offset=0, count=0;
};

// The owning GPU session clears this cache whenever its immutable cases change.
// Only partitions persist; root results still die at each program boundary.
struct ShapePartitionCache {
  static constexpr std::size_t max_partitions=64;
  static constexpr std::size_t max_metadata_ints=1024*1024; // Both GPU arrays together <=4MiB.
  std::unordered_map<std::uint64_t,ShapePartition> partitions;
  std::vector<int> groups,representatives;
  bool dirty=false;

  void clear() {partitions.clear();groups.clear();representatives.clear();dirty=false;}

  const ShapePartition* find_or_add(const std::vector<Value>& values,
      const std::vector<unsigned char>& assigned,std::uint64_t inputs) {
    const auto found=partitions.find(inputs);
    if(found!=partitions.end())return found->second.count?&found->second:nullptr;
    if(partitions.size()>=max_partitions)return nullptr;
    auto plan=shape_case_plan(values,assigned,inputs);
    ShapePartition item;
    const auto size=groups.size()+representatives.size();
    if(size<=max_metadata_ints && !plan.representatives.empty() && plan.groups.size()+plan.representatives.size()<=max_metadata_ints-size) {
      item={static_cast<int>(groups.size()),static_cast<int>(representatives.size()),static_cast<int>(plan.representatives.size())};
      groups.insert(groups.end(),plan.groups.begin(),plan.groups.end());
      representatives.insert(representatives.end(),plan.representatives.begin(),plan.representatives.end());
      dirty=true;
    }
    const auto inserted=partitions.emplace(inputs,item);
    return item.count?&inserted.first->second:nullptr;
  }
};

} // namespace gagp::gpu_detail
