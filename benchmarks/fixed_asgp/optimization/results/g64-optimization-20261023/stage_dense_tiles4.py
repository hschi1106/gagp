"""Stage dense case tiles for the measured tiled initial/replay hot paths."""
import difflib

import serial
from stage_dense_candidate import replace


def main():
    build = serial.read(serial.RAW / "build-attempt3/manifest.json")
    assert build["status"] == "ok"
    paths = ["cpp/include/gagp/runtime/gpu/region_types_gpu.hpp", "cpp/src/runtime/gpu/device/kernels.cuh",
             "cpp/src/runtime/gpu/fitness_gpu.cu", "cpp/tests/gpu/test_extended_payload_gpu.cu",
             "cpp/tests/gpu/test_gpu_phase_population.cu"]
    assert all(serial.sha(serial.ROOT / p) == build["source_sha256"][p] for p in paths)
    before = {p: (serial.ROOT / p).read_text() for p in paths}
    after = dict(before)
    p = paths[0]
    after[p] = replace(after[p],
        "  double* replay_warp_scores = nullptr; // Non-null selects logical-warp replay tiles.\n",
        "  double* replay_warp_scores = nullptr; // Tile partials, or original-lane scores for dense case tiles.\n"
        "  int dense_case_tiles = 0; // Nonzero: tiles cover consecutive cases, scores retain original logical lanes.\n")
    p = paths[1]
    after[p] = replace(after[p],
        "  const std::size_t row = tiled_replay ? logical_idx / replay_warps : logical_idx;\n"
        "  const int tile = tiled_replay ? logical_idx % replay_warps : 0;\n",
        "  const bool dense_tiled = tiled_replay && workspace.dense_case_tiles > 0;\n"
        "  const int scheduled_tiles = dense_tiled ? workspace.dense_case_tiles : replay_warps;\n"
        "  const std::size_t row = tiled_replay ? logical_idx / scheduled_tiles : logical_idx;\n"
        "  const int tile = tiled_replay ? logical_idx % scheduled_tiles : 0;\n")
    after[p] = replace(after[p], "    if(tiled_replay && tid==0)workspace.replay_warp_scores[logical_idx]=0.0;\n",
                       "    if(tiled_replay && !dense_tiled && tid==0)workspace.replay_warp_scores[logical_idx]=0.0;\n")
    after[p] = replace(after[p],
        "  const int schedule_begin = dense_lanes ? tid : lane_begin + tid;\n"
        "  const int schedule_end = dense_lanes ? meta.case_count : lane_end;\n",
        "  const int schedule_begin = dense_tiled ? tile*32 + tid : dense_lanes ? tid : lane_begin + tid;\n"
        "  const int schedule_end = dense_tiled ? min((tile+1)*32,meta.case_count) : dense_lanes ? meta.case_count : lane_end;\n")
    after[p] = replace(after[p], "  const int logical_tid = dense_lanes ?\n",
                       "  const int logical_tid = (dense_lanes || dense_tiled) ?\n")
    after[p] = replace(after[p], "  partial_scores[tiled_replay ? logical_tid - lane_begin : logical_tid] = local_score;\n",
        "  if(dense_tiled)workspace.replay_warp_scores[row*fitness_lanes+logical_tid]=local_score;\n"
        "  else partial_scores[tiled_replay ? logical_tid - lane_begin : logical_tid] = local_score;\n")
    after[p] = replace(after[p], "  if (tiled_replay) {\n    if (tid == 0) {\n",
                       "  if (tiled_replay) {\n    if (!dense_tiled && tid == 0) {\n")
    reducer = r'''
// Dense tiles scatter each case score to its original logical lane. Recreate
// both canonical reduction levels; do not reduce in the new physical order.
__global__ void reduce_dense_case_tiles(int count, int lanes, const int* indices,
    const double* partials, double* scores) {
  const int row=blockIdx.x*blockDim.x+threadIdx.x;
  if(row>=count)return;
  double total=0.0;
  for(int base=0;base<lanes;base+=32) {
    double warp=0.0;
    for(int lane=base;lane<min(base+32,lanes);++lane)
      warp=d_canonicalize_fitness_accumulator(warp+partials[static_cast<std::size_t>(row)*lanes+lane]);
    total=d_canonicalize_fitness_accumulator(total+warp);
  }
  scores[indices?indices[row]:row]=total;
}

'''
    after[p] = replace(after[p], "}  // namespace gagp::gpu_detail\n", reducer + "}  // namespace gagp::gpu_detail\n")
    p = paths[2]
    after[p] = replace(after[p], "  const int initial_warps=(impl_->blocksize+31)/32;\n",
        "  const auto* dense_tiles_setting=std::getenv(\"GAGP_GPU_DENSE_CASE_TILES\");\n"
        "  const bool request_dense_tiles=dense_tiles_setting && std::string(dense_tiles_setting)==\"1\";\n"
        "  const bool dense_initial_tiles=request_dense_tiles && tiled_initial &&\n"
        "      impl_->shared_case_count>0 && impl_->shared_case_count<=impl_->blocksize;\n"
        "  const int dense_tile_count=(impl_->shared_case_count+31)/32;\n"
        "  const int initial_warps=(impl_->blocksize+31)/32;\n")
    after[p] = replace(after[p],
        "  const auto initial_tiles_per_program=static_cast<std::size_t>(tiled_initial?initial_warps:1);\n",
        "  const auto initial_tiles_per_program=static_cast<std::size_t>(tiled_initial?(dense_initial_tiles?dense_tile_count:initial_warps):1);\n")
    after[p] = replace(after[p], "  const std::size_t initial_partial_bytes=tiled_initial?sizeof(double)*initial_tasks:0;\n",
        "  const std::size_t initial_partial_bytes=tiled_initial?sizeof(double)*programs.size()*\n"
        "      static_cast<std::size_t>(dense_initial_tiles?impl_->blocksize:initial_warps):0;\n")
    after[p] = replace(after[p], "      initial_payload.workspace.dense_case_lanes=dense_initial;\n",
        "      initial_payload.workspace.dense_case_lanes=dense_initial;\n"
        "      initial_payload.workspace.dense_case_tiles=dense_initial_tiles?dense_tile_count:0;\n"
        "      if(dense_initial_tiles && cudaMemset(initial_payload.workspace.replay_warp_scores,0,initial_partial_bytes)!=cudaSuccess)\n"
        "        return fitness_eval_single_error(ErrCode::Value,\"cuda dense initial lane-score initialization failure\");\n")
    after[p] = replace(after[p],
        "        gpu_detail::reduce_payload_replay<<<(programs.size()+127)/128,128>>>(\n"
        "            programs.size(),initial_warps,nullptr,initial_payload.workspace.replay_warp_scores,dev.d_fitness);\n",
        "        if(dense_initial_tiles)gpu_detail::reduce_dense_case_tiles<<<(programs.size()+127)/128,128>>>(\n"
        "            programs.size(),impl_->blocksize,nullptr,initial_payload.workspace.replay_warp_scores,dev.d_fitness);\n"
        "        else gpu_detail::reduce_payload_replay<<<(programs.size()+127)/128,128>>>(\n"
        "            programs.size(),initial_warps,nullptr,initial_payload.workspace.replay_warp_scores,dev.d_fitness);\n")
    after[p] = replace(after[p], "    const int replay_warps=(impl_->blocksize+31)/32;\n",
        "    const int replay_warps=(impl_->blocksize+31)/32;\n"
        "    const bool dense_replay_tiles=request_dense_tiles && tiled_replay &&\n"
        "        impl_->shared_case_count>0 && impl_->shared_case_count<=impl_->blocksize;\n"
        "    const int replay_tiles_per_program=dense_replay_tiles?dense_tile_count:replay_warps;\n")
    after[p] = replace(after[p],
        "      const std::size_t task_count=replay_programs.size()*(tiled_replay?replay_warps:1);\n",
        "      const std::size_t task_count=replay_programs.size()*(tiled_replay?replay_tiles_per_program:1);\n"
        "      const std::size_t replay_score_bytes=tiled_replay?sizeof(double)*replay_programs.size()*\n"
        "          static_cast<std::size_t>(dense_replay_tiles?impl_->blocksize:replay_warps):0;\n")
    after[p] = replace(after[p], "          (tiled_replay?sizeof(double)*task_count+sizeof(int):0);\n",
                       "          (tiled_replay?replay_score_bytes+sizeof(int):0);\n")
    after[p] = replace(after[p], "      auto replay_workspace=replay_storage.workspace;\n",
        "      auto replay_workspace=replay_storage.workspace;\n"
        "      replay_workspace.dense_case_tiles=dense_replay_tiles?dense_tile_count:0;\n")
    after[p] = replace(after[p],
        "        if(!replay_storage.allocate(reinterpret_cast<void**>(&replay_workspace.replay_warp_scores),sizeof(double)*task_count) ||\n",
        "        if(!replay_storage.allocate(reinterpret_cast<void**>(&replay_workspace.replay_warp_scores),replay_score_bytes) ||\n")
    after[p] = replace(after[p], "          return fitness_eval_single_error(ErrCode::Value,\"cuda replay tile allocation failure\");\n",
        "          return fitness_eval_single_error(ErrCode::Value,\"cuda replay tile allocation failure\");\n"
        "        if(dense_replay_tiles && cudaMemset(replay_workspace.replay_warp_scores,0,replay_score_bytes)!=cudaSuccess)\n"
        "          return fitness_eval_single_error(ErrCode::Value,\"cuda dense replay lane-score initialization failure\");\n")
    after[p] = replace(after[p],
        "      if(tiled_replay)\n        gpu_detail::reduce_payload_replay<<<(replay_programs.size()+127)/128,128>>>(\n"
        "            replay_programs.size(),replay_warps,replay_indices,replay_workspace.replay_warp_scores,dev.d_fitness);\n",
        "      if(tiled_replay) {\n"
        "        if(dense_replay_tiles)gpu_detail::reduce_dense_case_tiles<<<(replay_programs.size()+127)/128,128>>>(\n"
        "            replay_programs.size(),impl_->blocksize,replay_indices,replay_workspace.replay_warp_scores,dev.d_fitness);\n"
        "        else gpu_detail::reduce_payload_replay<<<(replay_programs.size()+127)/128,128>>>(\n"
        "            replay_programs.size(),replay_warps,replay_indices,replay_workspace.replay_warp_scores,dev.d_fitness);\n"
        "      }\n")
    after[p] = replace(after[p], "    if(request_dense)out.execution_profile+=dense_initial?\"-dense-case-lanes\":\"-dense-case-lanes-declined\";\n",
        "    if(request_dense)out.execution_profile+=dense_initial?\"-dense-case-lanes\":\"-dense-case-lanes-declined\";\n"
        "    if(request_dense_tiles)out.execution_profile+=(dense_initial_tiles || (dense_replay_tiles && out.payload_replay_programs))?\n"
        "        \"-dense-case-tiles\":\"-dense-case-tiles-declined\";\n")
    p = paths[3]
    after[p] = replace(after[p], "bool test_tiled_initial_payload() {\n", "bool test_tiled_initial_payload(int count=1024) {\n")
    anchor = "bool test_tiled_initial_payload(int count=1024) {\n"
    start = after[p].index(anchor)
    end = after[p].index("bool test_auto_initial_payload_inputs()", start)
    section = after[p][start:end]
    section = replace(section, "for(int i=0;i<1024;++i)", "for(int i=0;i<count;++i)")
    after[p] = after[p][:start] + section + after[p][end:]
    helper = r'''
bool test_dense_case_tiles() {
  ScopedEnvironment dense("GAGP_GPU_DENSE_CASE_TILES"),replay("GAGP_GPU_TILED_REPLAY");
  setenv("GAGP_GPU_DENSE_CASE_TILES","1",1);setenv("GAGP_GPU_TILED_REPLAY","1",1);
  for(int count:{1,19,33,200,513})if(!test_tiled_initial_payload(count))return false;
  // Overflow retry, subset indices and growth must retain full original lanes.
  return test_initial_payload_capacity_replay() && test_tiled_replay_entry_growth();
}

'''
    after[p] = replace(after[p], "bool test_auto_initial_payload_inputs() {\n", helper + "bool test_auto_initial_payload_inputs() {\n")
    after[p] = replace(after[p], "  if (!test_dense_case_lanes() || !test_auto_initial_payload_inputs()",
                       "  if (!test_dense_case_tiles() || !test_dense_case_lanes() || !test_auto_initial_payload_inputs()")
    p = paths[4]
    start = after[p].index('   const char* names[]={"GAGP_GPU_DENSE_CASE_LANES","GAGP_GPU_TILED_INITIAL_PAYLOAD"};')
    end = after[p].index('       "dense scheduling changed fresh private generations, full CPU audit or dirty workspace scores");', start)
    end = after[p].index("\n", end) + 1
    section = after[p][start:end]
    section = section.replace('"GAGP_GPU_TILED_INITIAL_PAYLOAD"}', '"GAGP_GPU_TILED_INITIAL_PAYLOAD","GAGP_GPU_DENSE_CASE_TILES","GAGP_GPU_TILED_REPLAY"}')
    section = section.replace('saved[2]', 'saved[4]').replace('present[2]', 'present[4]').replace('i<2', 'i<4')
    restore = '   for(int i=0;i<4;++i)if(present[i])setenv(names[i],saved[i].c_str(),1);else unsetenv(names[i]);\n'
    section = replace(section, restore, '')
    section += r'''
   setenv(names[0],"0",1);setenv(names[1],"1",1);setenv(names[3],"1",1);
   const auto ordinary_tiles=owner.evaluate(dense_session,true);
   setenv(names[2],"1",1);const auto dense_tiles=owner.evaluate(dense_session,true);
   const auto dirty_tiles=owner.evaluate(dense_session,true);
   for(int i=0;i<4;++i)if(present[i])setenv(names[i],saved[i].c_str(),1);else unsetenv(names[i]);
   require(ordinary_tiles.ok && dense_tiles.ok && dirty_tiles.ok && dense_tiles.fitness==dense_cpu &&
       ordinary_tiles.fitness==dense_tiles.fitness && dirty_tiles.fitness==dense_tiles.fitness &&
       dense_tiles.wrong==dense_wrong && dense_tiles.case_counts==ordinary_tiles.case_counts &&
       dirty_tiles.case_counts==dense_tiles.case_counts &&
       dense_tiles.initial_payload_overflow_lane_masks==ordinary_tiles.initial_payload_overflow_lane_masks &&
       dense_tiles.execution_profile.find("-dense-case-tiles")!=std::string::npos &&
       dense_tiles.execution_profile.find("-dense-case-tiles-declined")==std::string::npos,
       "dense case tiles changed fresh private generations, original lane masks, replay or full CPU audit");
'''
    after[p] = after[p][:start] + section + after[p][end:]
    target = serial.BASE / "native-dense-tiles4.patch"
    assert not target.exists()
    target.write_text("".join("".join(difflib.unified_diff(before[p].splitlines(True), after[p].splitlines(True),
                                                        fromfile="a/" + p, tofile="b/" + p)) for p in paths))
    serial.save(serial.RAW / "dense-tiles4-staged.json", dict(source_build="build-attempt3", patch=str(target),
                patch_sha256=serial.sha(target), changed_paths=paths,
                scope="Actual tiled initial+replay paths: ceil(C/32) dense case tiles scatter to original L score lanes, zero skipped lanes, same canonical two-level reduction; same16GiB cap"))


if __name__ == "__main__":
    main()
