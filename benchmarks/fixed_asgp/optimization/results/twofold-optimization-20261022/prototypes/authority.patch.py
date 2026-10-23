path=root/'docs/design/payload.md';text=path.read_text()
text+='''

### Optional projected shape inputs

With `GAGP_GPU_SHAPE_RESULT_CACHE=1`, session-fixed
`GAGP_GPU_PROJECT_SHAPE_INPUTS=1` can omit unused caller locals from input-shape
keys. Fresh direct-root LOADs always remain dependencies. Each reachable phase
adds Parameter bindings only when its bytecode LOADs that binding; a verified
literal-true base predicate excludes its unreachable cut/request/combine.
Boundary dependencies remain included. Complete existing typeflow is still
required; this dependency check cannot grant admission to a missing or wrongly
typed parameter that the existing profile declines.

The session retains partitions by64-bit input mask, including declined masks,
with at most64 partitions and4MiB total GPU group/representative metadata.
GPU metadata is uploaded again only when an admitted partition is added.
Reinitializing a session clears every partition. Program metadata selects its
own group offsets/count; shared result storage fits the largest admitted group.
Actual root executions remain in `physical_program_cases`;
`shape_result_groups` reports the maximum admitted count in that evaluation.
No scalar result/error survives a program or generation boundary. Per-case
labels, logical coverage, original ordered score updates and the additional
fourfold-saving threshold remain unchanged.

### Optional small phase register banks

`GAGP_GPU_REGISTER_PHASE_STACK=1` adds a typed phase implementation for fresh
verified stack bounds1..4 and at most8 locals. It uses explicit64-bit fields for
four operand slots and eight local slots, retaining the original bytecode,
branching, builtin allocation, fuel and error ordering. Used input presence and
private parameter type checks remain lazy. Other phases retain the array-backed
VM; generic/Float programs do not acquire typed admission through this flag.
The profile appends `-register-phase-stack` or its `-declined` suffix. Fixed
fields permit compiler register allocation; they do not guarantee a resource or
speed improvement. Device storage-oracle, low-fuel, private list, hybrid stack
and changing-generation CPU/GPU tests cover this path.

### Optional physical view blocks with fixed logical score lanes

`GAGP_GPU_VIEW_PHYSICAL_BLOCK` accepts0(default),32,64,128,256 or512. Typed view
execution can use up to that many physical threads while retaining the original
logical fitness lane count. Each physical thread visits separate logical lanes;
each logical lane retains its original ordered case accumulation. Strided case
iteration advances by logical lanes, and shared score storage reserves every
logical lane before the original warp/final reduction. Private arenas retain
their existing32-thread execution and replay rules.

This setting changes concurrency and scratch slot geometry, not case coverage,
score reduction, fuel, reproduction or search parameters. Workspace allocation
uses physical threads; complete logical score slots remain in shared memory.
The profile records `-physical-viewN` or `-physical-view-declined`;
`physical_blocksize` records the initial launch width while benchmark
`gpu_blocksize`/CPU-audit reduction lanes retain the configured logical width.
Tests cover partial case counts, sorted integral and ordered fractional scoring,
32/64/128/256 physical widths,1024 logical lanes, invalid settings and fresh
GPU-produced populations.
'''
changes[path]=text
path=root/'.agents/skills/gagp-navigation/SKILL.md';text=path.read_text()
text+='''

- Projected shape inputs use fresh root/phase LOAD dependencies in
  `cpp/src/runtime/gpu/shape_result_cache.hpp`; bounded session partitions live
  in `shape_partition_cache.hpp`, with device offsets in `device_types_gpu.hpp`.
  `GAGP_GPU_PROJECT_SHAPE_INPUTS` retains complete typed admission. Contract:
  `docs/design/payload.md`; tests: fitness-contract, bounded-region GPU and
  phase-population GPU.
- Small typed phase register banks live in
  `cpp/src/runtime/gpu/device/phase_register_bank.cuh`; `view_phase_device.cuh`
  owns the shared VM/storage interface and `region_execution_device.cuh` guards
  each fresh bound. `GAGP_GPU_REGISTER_PHASE_STACK` uses the normal fitness
  workspace/execution-table flag path. Test `gagp_test_phase_register_gpu` adds
  the independent storage oracle; integrated tests/contracts are the owners above.
- `GAGP_GPU_VIEW_PHYSICAL_BLOCK` separates typed-view physical thread geometry
  from original logical score lanes in `fitness_gpu.cu` / `device/kernels.cuh`.
  Reserve all logical shared scores and stride cases by logical lanes. Native
  evaluation timing/benchmark rows expose physical launch width. Contract/tests
  are the same owners above; no operator, case or reduction-budget change.
'''
changes[path]=text
