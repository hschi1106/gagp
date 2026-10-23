import re

path=root/'cpp/src/runtime/gpu/device/phase_register_bank.cuh';assert not path.exists()
changes[path]=(base/'prototypes/phase_register_bank.cuh').read_text()
replace('cpp/include/gagp/runtime/gpu/region_types_gpu.hpp','  bool simple_phases = false;',
    '  bool simple_phases = false;\n  bool register_phase_stack = false;')
replace('cpp/src/runtime/gpu/device/execute_bytecode_device.cuh','  int measure_cached_phase = -1;',
    '  int measure_cached_phase = -1;\n  bool register_phase_stack = false;')
replace('cpp/src/runtime/gpu/device/view_phase_device.cuh','#pragma once',
    '#pragma once\n\n#include "phase_register_bank.cuh"')
replace('cpp/src/runtime/gpu/device/view_phase_device.cuh',
    '  __device__ bool call(BuiltinId,long long*,int&,const DPayloadTables&,DResult&) const {return false;}',
    '  template<class Stack> __device__ __forceinline__ bool call(BuiltinId,Stack&,int&,const DPayloadTables&,DResult&) const {return false;}')
replace('cpp/src/runtime/gpu/device/view_phase_device.cuh',
    '  __device__ bool call(BuiltinId bid,long long* stack,int& sp,',
    '  template<class Stack> __device__ __forceinline__ bool call(BuiltinId bid,Stack& stack,int& sp,')
replace('cpp/src/runtime/gpu/device/view_phase_device.cuh',
    'template <class Binder,class PayloadOps=DViewPhasePayloadOps>',
    'template <class Binder,class PayloadOps=DViewPhasePayloadOps,class Stack=DPhaseArrayBank<16>,class Locals=DPhaseArrayBank<8>>')
replace('cpp/src/runtime/gpu/device/view_phase_device.cuh','  long long stack[16], locals[8];',
    '  Stack stack;Locals locals;')
# Preserve eager scalar reads: auto would otherwise retain a register proxy.
for old in ('const auto right = stack[--sp]; const auto left = stack[sp - 1];',
            'const auto b = stack[--sp]; const auto a = stack[sp - 1];',
            'const auto index = stack[--sp];',
            'const auto hi = stack[--sp], lo = stack[--sp];',
            'const auto hi = stack[--sp], lo = stack[--sp], value = stack[sp - 1];',
            'const auto b = stack[--sp], a = stack[sp - 1];'):
    replace('cpp/src/runtime/gpu/device/view_phase_device.cuh',old,old.replace('const auto','const long long'))
# Only these three binders supply typed-view locals. Every public/generic VM
# keeps its existing path; binder initialization is unchanged apart from storage.
for name,expected in [('cpp/src/runtime/gpu/device/view_phase_device.cuh',1),
                      ('cpp/src/runtime/gpu/device/region_execution_device.cuh',2)]:
    path=root/name;text=changes.get(path,path.read_text())
    pattern=r'__device__ ErrCode init\(long long\* locals,\s*unsigned& local_set,'
    text,count=re.subn(pattern,'template<class Locals> __device__ __forceinline__ ErrCode init(Locals& locals,unsigned& local_set,',text)
    assert count==expected,(name,count)
    changes[path]=text
replace('cpp/src/runtime/gpu/device/region_execution_device.cuh',
    '''      return d_run_bound_view_phase(view,
          DPrivateRegionBindings<Frame>{segment,phase,frame,caller_locals,caller_set,tables},
          payload_tables,fuel,expected,DPrivatePhasePayloadOps<Flavor>{payload_state});''',
    '''      if(tables.register_phase_stack && p.verified_stack_bound>0 && p.verified_stack_bound<=4)
        return d_run_bound_view_phase<DPrivateRegionBindings<Frame>,DPrivatePhasePayloadOps<Flavor>,
            DPhaseRegisterBank<4>,DPhaseRegisterBank<8>>(view,
            DPrivateRegionBindings<Frame>{segment,phase,frame,caller_locals,caller_set,tables},
            payload_tables,fuel,expected,DPrivatePhasePayloadOps<Flavor>{payload_state});
      return d_run_bound_view_phase(view,
          DPrivateRegionBindings<Frame>{segment,phase,frame,caller_locals,caller_set,tables},
          payload_tables,fuel,expected,DPrivatePhasePayloadOps<Flavor>{payload_state});''')
replace('cpp/src/runtime/gpu/device/region_execution_device.cuh',
    '''    return d_run_bound_view_phase(view,
        DViewRegionBindings<Frame>{segment, phase, frame, caller_locals, caller_set, tables},
        payload_tables, fuel, expected);''',
    '''    if(tables.register_phase_stack && phase.program.verified_stack_bound>0 && phase.program.verified_stack_bound<=4)
      return d_run_bound_view_phase<DViewRegionBindings<Frame>,DViewPhasePayloadOps,
          DPhaseRegisterBank<4>,DPhaseRegisterBank<8>>(view,
          DViewRegionBindings<Frame>{segment,phase,frame,caller_locals,caller_set,tables},
          payload_tables,fuel,expected);
    return d_run_bound_view_phase(view,
        DViewRegionBindings<Frame>{segment, phase, frame, caller_locals, caller_set, tables},
        payload_tables, fuel, expected);''')
replace('cpp/src/runtime/gpu/device/kernels.cuh',
    '''      region_bindings, region_binding_count,
  };''',
    '''      region_bindings, region_binding_count,
  };
  execution_tables.register_phase_stack=workspace.register_phase_stack;''')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','  region_workspace.simple_phases=request_simple && region_workspace.private_typed_phases;',
    '''  region_workspace.simple_phases=request_simple && region_workspace.private_typed_phases;
  const auto* register_stack_setting=std::getenv("GAGP_GPU_REGISTER_PHASE_STACK");
  const bool request_register_stack=register_stack_setting && std::string(register_stack_setting)=="1";
  region_workspace.register_phase_stack=request_register_stack && (direct_phases || region_workspace.private_typed_phases);''')
replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(request_relaxed)out.execution_profile+=',
    '''    if(request_register_stack)out.execution_profile+=region_workspace.register_phase_stack?"-register-phase-stack":"-register-phase-stack-declined";
    if(request_relaxed)out.execution_profile+=''')
# Exercise the storage change with native reproduction/fresh CPU audits.
replace('cpp/tests/gpu/test_gpu_phase_population.cu',
    'setenv("GAGP_GPU_RELAXED_VIEW_REGISTERS","1",1);setenv("GAGP_REUSE_IMPORTED_EXECUTABLE",',
    'setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);setenv("GAGP_GPU_RELAXED_VIEW_REGISTERS","1",1);setenv("GAGP_REUSE_IMPORTED_EXECUTABLE",')
path=root/'cpp/tests/gpu/test_bounded_region_gpu.cpp';text=changes[path]
a=text.index('bool test_projected_shape_inputs()');b=text.index('\nint main()',a);part=text[a:b]
part=part.replace('"GAGP_GPU_PROJECT_SHAPE_INPUTS","GAGP_GPU_RELAXED_VIEW_REGISTERS"',
    '"GAGP_GPU_PROJECT_SHAPE_INPUTS","GAGP_GPU_RELAXED_VIEW_REGISTERS","GAGP_GPU_REGISTER_PHASE_STACK"')
part=part.replace('    const auto a=reference.eval_programs(programs,true),b=cached.eval_programs(programs,true);',
    '''    unsetenv("GAGP_GPU_REGISTER_PHASE_STACK");
    const auto a=reference.eval_programs(programs,true);
    setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);
    const auto b=cached.eval_programs(programs,true);''')
changes[path]=text[:a]+part+text[b:]
# Also cover the private allocating typed phase path, including low-fuel tests.
path=root/'cpp/tests/gpu/test_gpu_phase_population.cu';text=changes[path]
a=text.index('void test_private_intlist_generations(');b=text.index('\nvoid test_whole_program_node_budget()',a);part=text[a:b]
part=part.replace('"GAGP_GPU_REUSE_PRIVATE_WORKSPACE","GAGP_GPU_SIMPLE_PHASES"',
    '"GAGP_GPU_REUSE_PRIVATE_WORKSPACE","GAGP_GPU_SIMPLE_PHASES","GAGP_GPU_REGISTER_PHASE_STACK"')
part=part.replace('saved[24];bool present[24]','saved[25];bool present[25]').replace('i<24;','i<25;')
part=part.replace('setenv("GAGP_GPU_SIMPLE_PHASES","0",1);const auto simple_off=owner.evaluate(session,true);',
    '''setenv("GAGP_GPU_REGISTER_PHASE_STACK","0",1);
  setenv("GAGP_GPU_SIMPLE_PHASES","0",1);const auto simple_off=owner.evaluate(session,true);''')
part=part.replace('setenv("GAGP_GPU_SIMPLE_PHASES","1",1);\n  require(simple_off.ok',
    'setenv("GAGP_GPU_SIMPLE_PHASES","1",1);setenv("GAGP_GPU_REGISTER_PHASE_STACK","1",1);\n  require(simple_off.ok')
changes[path]=text[:a]+part+text[b:]
# A standalone randomized storage oracle is separate from the integrated tests.
path=root/'cpp/tests/gpu/test_phase_register_gpu.cu';assert not path.exists()
changes[path]=(base/'prototypes/test_phase_register_gpu.cu').read_text()
replace('cpp/CMakeLists.txt','  add_executable(gagp_test_fitness_accumulator_gpu tests/gpu/test_fitness_accumulator_gpu.cu)',
    '''  add_executable(gagp_test_phase_register_gpu tests/gpu/test_phase_register_gpu.cu)
  target_link_libraries(gagp_test_phase_register_gpu PRIVATE gagp_gpu)
  add_test(NAME gagp_test_phase_register_gpu COMMAND gagp_test_phase_register_gpu)
  set_tests_properties(gagp_test_phase_register_gpu PROPERTIES LABELS "contract;unit;gpu")

  add_executable(gagp_test_fitness_accumulator_gpu tests/gpu/test_fitness_accumulator_gpu.cu)''')
