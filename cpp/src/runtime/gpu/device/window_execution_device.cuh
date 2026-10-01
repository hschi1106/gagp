#pragma once

// Capability: IntList state plus an optional carried Int, two window requests, one InteriorCut Int
// preparation, Int result, no memo or bound operands. Still executes every
// evolved predicate/base/cut/combine and uses the original entry/instruction fuel.
template<class Frame>
__device__ __noinline__ DResult d_run_window_region(
    const DRegionSegment& segment, const Value* operands,
    const Value* caller_locals, std::uint64_t caller_set,
    const DPayloadTables& payload_tables,
    DPayloadFlavorTraits<DPayloadFlavor::BoundIntListViews>::State& payload_state,
    const DExecutionTables& tables, int& fuel, DRegionWorkspace workspace) {
  constexpr auto Flavor=DPayloadFlavor::BoundIntListViews;
  if(segment.limits.frames==0)return d_error(ErrCode::Timeout);
  if(!workspace.frames || workspace.frame_capacity<segment.limits.frames)return d_error(ErrCode::Value);
  auto* frames=reinterpret_cast<Frame*>(workspace.frames);
  const std::size_t stride=workspace.slot_stride;
  unsigned depth=1;frames[0].state[0]=operands[0];frames[0].next_request=-1;
  if(segment.state_count==2)frames[0].state[1]=operands[1];
  for(;;) {
    auto& frame=frames[(depth-1)*stride];
    DResult result;bool completed=false;
    if(frame.next_request<0) {
      if(fuel<0 || segment.limits.entry_fuel>static_cast<unsigned>(fuel))return d_error(ErrCode::Timeout);
      fuel-=static_cast<int>(segment.limits.entry_fuel);
      if(frame.state[0].tag!=ValueTag::IntList ||
          (segment.state_count==2 && frame.state[1].tag!=ValueTag::Int))return d_error(ErrCode::Type);
      const auto length=Value::container_len(frame.state[0]);bool base=length<=1;
      if(!base) {
        result=d_region_phase<Flavor>(segment,segment.base_predicate_phase,ValueTag::Bool,
            frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel);
        if(result.is_error)return result;base=result.value.b;
      }
      if(base) {
        result=d_region_phase<Flavor>(segment,segment.base_body_phase,ValueTag::Int,
            frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,true);
        if(result.is_error)return result;completed=true;
      } else {
        result=d_region_phase<Flavor>(segment,segment.preparation_phases[0],ValueTag::Int,
            frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel);
        if(result.is_error)return result;
        const auto cut=result.value.i;
        frame.prepared[0]=Value::from_int(cut<1?1:cut>length-1?length-1:cut);
        frame.next_request=0;
      }
    }
    if(!completed && frame.next_request<2) {
      const auto& window=segment.requests[frame.next_request][0].window;
      const auto source=frame.state[0];const auto length=Value::container_len(source);
      std::int64_t begin,end;
      if(length<2 || !d_region_endpoint(window.begin,length,frame,begin) ||
          !d_region_endpoint(window.end,length,frame,end))return d_error(ErrCode::Value);
      const Value args[]{source,Value::from_int(begin),Value::from_int(end)};
      Value next;ErrCode error=ErrCode::Value;
      if(!d_builtin_call<Flavor>(BuiltinId::Slice,args,3,payload_tables,payload_state,next,error))return d_error(error);
      Value carried;
      if(segment.state_count==2) {
        const auto& transition=segment.requests[frame.next_request][1];
        if(transition.kind==RegionTransitionKind::CopyState)carried=frame.state[1];
        else {
          result=d_region_phase<Flavor>(segment,segment.request_expression_phases[transition.expression],ValueTag::Int,
              frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel);
          if(result.is_error)return result;carried=result.value;
        }
      }
      if(depth>=segment.limits.frames)return d_error(ErrCode::Timeout);
      ++frame.next_request;frames[depth*stride].state[0]=next;frames[depth*stride].next_request=-1;
      if(segment.state_count==2)frames[depth*stride].state[1]=carried;
      ++depth;continue;
    }
    if(!completed) {
      // Typed phase proof excludes fallback-token results in this profile.
      result=d_region_phase<Flavor>(segment,segment.combine_phase,ValueTag::Int,
          frame,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,true);
      if(result.is_error)return result;
    }
    if(--depth==0)return result;
    auto& parent=frames[(depth-1)*stride];parent.results[parent.next_request-1]=result.value;
  }
}
