#pragma once
// Capability proved by host dispatch: one Int coordinate, literal bounds,
// one/two offset requests, no preparations/bound operands, Int result. All
// evolved phases still execute; memo lookup/storage ordering matches generic VM.
__device__ __noinline__ DResult d_run_coordinate_region(
    const DRegionSegment& segment,const Value* operands,
    const Value* caller_locals,std::uint64_t caller_set,
    const DPayloadTables& payload_tables,
    DPayloadFlavorTraits<DPayloadFlavor::BoundIntListViews>::State& payload_state,
    const DExecutionTables& tables,int& fuel,DRegionWorkspace workspace) {
  constexpr auto Flavor=DPayloadFlavor::BoundIntListViews;
  const auto lower=segment.coordinate_domains[0].lower.literal;
  const auto upper=segment.coordinate_domains[0].upper.literal;
  const bool inclusive=segment.coordinate_endpoint==DomainEndpoint::Inclusive;
  if(lower>upper)return d_error(ErrCode::Value);
  if(inclusive || lower!=upper) {
    const auto maximum=inclusive?upper:upper-1;
    for(unsigned r=0;r<segment.request_count;++r) {
      const auto offset=segment.requests[r][0].offset;
      if(!d_region_addable(lower,offset) || !d_region_addable(maximum,offset))return d_error(ErrCode::Value);
    }
  }
  if(segment.limits.frames==0)return d_error(ErrCode::Timeout);
  if(!workspace.frames || workspace.frame_capacity<segment.limits.frames ||
      (segment.memoized && segment.limits.cells>0 && (!workspace.memo_keys || !workspace.memo_values ||
       workspace.memo_capacity<segment.limits.cells)))return d_error(ErrCode::Value);
  // Preserve first entry fuel before a runtime state type failure.
  if(operands[0].tag!=ValueTag::Int) {
    if(fuel<0 || segment.limits.entry_fuel>static_cast<unsigned>(fuel))return d_error(ErrCode::Timeout);
    fuel-=static_cast<int>(segment.limits.entry_fuel);return d_error(ErrCode::Type);
  }
  auto* frames=reinterpret_cast<DUnboxedCoordinateFrame*>(workspace.frames);
  const auto stride=workspace.slot_stride;
  unsigned depth=1,memo_count=0;frames[0].state[0]=operands[0];frames[0].next_request=-1;
  for(;;) {
    auto& frame=frames[(depth-1)*stride];DResult result;bool completed=false;
    if(frame.next_request<0) {
      if(fuel<0 || segment.limits.entry_fuel>static_cast<unsigned>(fuel))return d_error(ErrCode::Timeout);
      fuel-=static_cast<int>(segment.limits.entry_fuel);
      const auto coordinate=frame.state[0].i;
      const bool boundary=coordinate<lower || (inclusive?coordinate>upper:coordinate>=upper);
      bool base=false;
      if(!boundary) {
        result=d_region_phase<Flavor>(segment,segment.base_predicate_phase,ValueTag::Bool,
            frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel);
        if(result.is_error)return result;base=result.value.b;
      }
      if(boundary || base) {
        result=d_region_phase<Flavor>(segment,boundary?segment.boundary_phase:segment.base_body_phase,ValueTag::Int,
            frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,true);
        if(result.is_error)return result;completed=true;
      } else {
        if(segment.memoized)for(unsigned cell=0;cell<memo_count;++cell) {
          if(workspace.memo_keys[cell*DMAX_REGION_STATES*stride]==coordinate) {
            result=d_ok(workspace.memo_values[cell*stride]);completed=true;break;
          }
        }
        if(!completed)frame.next_request=0;
      }
    }
    if(!completed && static_cast<unsigned>(frame.next_request)<segment.request_count) {
      const auto coordinate=frame.state[0].i;
      const auto offset=segment.requests[frame.next_request][0].offset;
      if(!d_region_addable(coordinate,offset))return d_error(ErrCode::Value);
      if(depth>=segment.limits.frames)return d_error(ErrCode::Timeout);
      ++frame.next_request;frames[depth*stride].state[0]=Value::from_int(coordinate+offset);
      frames[depth*stride].next_request=-1;++depth;continue;
    }
    if(!completed) {
      result=d_region_phase<Flavor>(segment,segment.combine_phase,ValueTag::Int,
          frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,true);
      if(result.is_error)return result;
      if(segment.memoized) {
        if(memo_count>=segment.limits.cells)return d_error(ErrCode::Timeout);
        workspace.memo_keys[memo_count*DMAX_REGION_STATES*stride]=frame.state[0].i;
        workspace.memo_values[memo_count*stride]=result.value;++memo_count;
      }
    }
    if(--depth==0)return result;
    auto& parent=frames[(depth-1)*stride];parent.results[parent.next_request-1]=result.value;
  }
}
