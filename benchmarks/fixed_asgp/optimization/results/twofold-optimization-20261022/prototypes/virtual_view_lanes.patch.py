# Physical view threads can differ from logical score lanes, preserving the
# original case partition, per-lane accumulation and warp reduction exactly.
replace('cpp/src/runtime/gpu/fitness_gpu.cu','  // Resolve immutable captures against this evaluation\'s table, without changing',
    '''  const int fitness_lanes=execution_threads;
  const auto* physical_setting=std::getenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK");
  const std::string physical_text=physical_setting?physical_setting:"0";
  if(physical_text!="0" && physical_text!="32" && physical_text!="64" && physical_text!="128" &&
      physical_text!="256" && physical_text!="512")
    return fitness_eval_single_error(ErrCode::Value,"GAGP_GPU_VIEW_PHYSICAL_BLOCK must be0,32,64,128,256 or512");
  const int requested_physical_block=std::stoi(physical_text);
  const bool virtual_view_lanes=use_views && requested_physical_block>0;
  if(virtual_view_lanes)execution_threads=std::min(execution_threads,requested_physical_block);
  // Resolve immutable captures against this evaluation's table, without changing''')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','kernel_shared_bytes(packed.max_code_len, execution_threads)',
    'kernel_shared_bytes(packed.max_code_len, fitness_lanes)')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','  region_workspace.integer_case_weights=reuse_integer_cases?impl_->d_reuse_weights:nullptr;',
    '  region_workspace.integer_case_weights=reuse_integer_cases?impl_->d_reuse_weights:nullptr;\n  region_workspace.fitness_lanes=fitness_lanes;')
replace('cpp/src/runtime/gpu/device/kernels.cuh',
    '  const int fitness_lanes = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) ? workspace.fitness_lanes : blockDim.x;',
    '  const int fitness_lanes = workspace.fitness_lanes>0?workspace.fitness_lanes:static_cast<int>(blockDim.x);')
replace('cpp/src/runtime/gpu/device/kernels.cuh','  const int case_step = strided ? static_cast<int>(blockDim.x) : 1;',
    '  const int case_step = strided ? fitness_lanes : 1;')
replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp','  std::uint64_t physical_program_cases = 0;',
    '  std::uint64_t physical_program_cases = 0;\n  std::size_t physical_blocksize = 0; // Initial launch; logical score lanes stay unchanged.')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','  out.shape_result_programs=shape_result_programs;',
    '  out.physical_blocksize=global_payload?PayloadWorkspaceStorage::threads:execution_threads;\n  out.shape_result_programs=shape_result_programs;')
replace('cpp/include/gagp/evolution/timing.hpp','  std::uint64_t simple_phase_count = 0;',
    '  std::uint64_t simple_phase_count = 0;\n  std::uint64_t physical_blocksize = 0;')
replace('cpp/src/evolution/timing.cpp','  total->simple_phase_count += value.simple_phase_count;',
    '  total->simple_phase_count += value.simple_phase_count;\n  total->physical_blocksize=std::max(total->physical_blocksize,value.physical_blocksize);')
replace('cpp/src/evolution/evolve.cpp','  evaluation.timing.simple_phase_count=fit.simple_phase_count;',
    '  evaluation.timing.simple_phase_count=fit.simple_phase_count;\n  evaluation.timing.physical_blocksize=fit.physical_blocksize;')
replace('cpp/src/evolution/evolve.cpp','    timing.simple_phase_count=fit.simple_phase_count;',
    '    timing.simple_phase_count=fit.simple_phase_count;\n    timing.physical_blocksize=fit.physical_blocksize;')
replace('cpp/src/bench/fixed_asgp/main.cpp','      {"simple_phase_count",number(e.simple_phase_count)},',
    '      {"simple_phase_count",number(e.simple_phase_count)},\n      {"physical_blocksize",number(e.physical_blocksize)},')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(request_register_stack)out.execution_profile+=',
    '''    if(requested_physical_block)out.execution_profile+=virtual_view_lanes?
        "-physical-view"+std::to_string(execution_threads):"-physical-view-declined";
    if(request_register_stack)out.execution_profile+=''')
# Run the32generation native GPU/independent CPU comparison with128 physical
# view threads and original512 logical lanes. Private arenas keep32 threads.
replace('cpp/tests/gpu/test_gpu_phase_population.cu',
    'setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);setenv("GAGP_GPU_RELAXED_VIEW_REGISTERS",',
    'setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","128",1);setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);setenv("GAGP_GPU_RELAXED_VIEW_REGISTERS",')
path=root/'cpp/tests/gpu/test_bounded_region_gpu.cpp';text=changes[path]
a=text.index('bool test_projected_shape_inputs()');b=text.index('\nint main()',a);part=text[a:b]
part=part.replace('"GAGP_GPU_RELAXED_VIEW_REGISTERS","GAGP_GPU_REGISTER_PHASE_STACK"',
    '"GAGP_GPU_RELAXED_VIEW_REGISTERS","GAGP_GPU_REGISTER_PHASE_STACK","GAGP_GPU_VIEW_PHYSICAL_BLOCK"')
part=part.replace('for(int i=0;i<1024;++i)', 'for(int i=0;i<1025;++i)')
part=part.replace('pack_programs_with_shared_case_count(programs,1024,0)', 'pack_programs_with_shared_case_count(programs,1025,0)')
part=part.replace('  auto segment=sequence_segment(ValueTag::IntList);',
    '  setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","128",1);\n  auto segment=sequence_segment(ValueTag::IntList);')
part=part.replace('for(int fuel:{0,1,3,16,1000}) {', 'for(int fuel:{0,1,3,16,1000})for(int physical:{32,128,256}) {')
part=part.replace('    unsetenv("GAGP_GPU_REGISTER_PHASE_STACK");\n    const auto a=',
    '    setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","0",1);\n    unsetenv("GAGP_GPU_REGISTER_PHASE_STACK");\n    const auto a=')
part=part.replace('    setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);\n    const auto b=',
    '    setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK",std::to_string(physical).c_str(),1);\n    setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);\n    const auto b=')
part=part.replace('b.shape_result_groups==8 && b.physical_program_cases==1040,',
    'b.shape_result_groups==8 && b.physical_program_cases==1041 && b.physical_blocksize==static_cast<unsigned>(physical),')
marker='  // Missing/wrong USED captures force ordinary admission/execution and retain errors.'
addition=r'''  // Sorted/strided integral cases also retain their original512 logical lanes.
  const FitnessPolicy capped{FitnessMetric::Absolute,FitnessProximity::Capped};
  for(int physical:{32,64,128,256}) {
    FitnessSessionGpu reference,cached;
    unsetenv("GAGP_GPU_SHAPE_RESULT_CACHE");
    if(!reference.init(cases,answers,1000,512,7,capped).ok)return false;
    setenv("GAGP_GPU_SHAPE_RESULT_CACHE","1",1);
    if(!cached.init(cases,answers,1000,512,7,capped).ok)return false;
    setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","0",1);const auto a=reference.eval_programs(programs,true);
    setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK",std::to_string(physical).c_str(),1);const auto b=cached.eval_programs(programs,true);
    if(!check(a.ok && b.ok && a.fitness==b.fitness && a.case_counts==b.case_counts && a.wrong==b.wrong &&
        b.fitness==eval_fitness_cpu(programs,cases,answers,1000,7,512,capped) && b.physical_blocksize==static_cast<unsigned>(physical),
        "physical view lanes duplicated strided cases or changed canonical score order"))return false;
  }
  FitnessSessionGpu wide_reference,wide_cached;
  unsetenv("GAGP_GPU_SHAPE_RESULT_CACHE");
  if(!wide_reference.init(cases,answers,1000,1024,7,policy).ok)return false;
  setenv("GAGP_GPU_SHAPE_RESULT_CACHE","1",1);
  if(!wide_cached.init(cases,answers,1000,1024,7,policy).ok)return false;
  setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","0",1);const auto original_wide=wide_reference.eval_programs(programs,true);
  setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","128",1);const auto virtual_wide=wide_cached.eval_programs(programs,true);
  if(!check(original_wide.ok && virtual_wide.ok && original_wide.fitness==virtual_wide.fitness &&
      original_wide.case_counts==virtual_wide.case_counts && virtual_wide.physical_blocksize==128 &&
      virtual_wide.fitness==eval_fitness_cpu(programs,cases,answers,1000,7,1024,policy),
      "virtual physical lanes changed1024-lane fractional reduction or overflowed score scratch"))return false;
'''
assert part.count(marker)==1;part=part.replace(marker,addition+marker)
# The fallback assertions below use128 logical lanes; clamp wider physical requests.
part=part.replace('  const auto ignored=mismatch.eval_programs({plain},true);',
    '''  setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","127",1);
  const auto invalid=mismatch.eval_programs({plain},true);
  if(!check(!invalid.ok,"invalid physical view block setting accepted"))return false;
  setenv("GAGP_GPU_VIEW_PHYSICAL_BLOCK","128",1);
  const auto ignored=mismatch.eval_programs({plain},true);''')
changes[path]=text[:a]+part+text[b:]
