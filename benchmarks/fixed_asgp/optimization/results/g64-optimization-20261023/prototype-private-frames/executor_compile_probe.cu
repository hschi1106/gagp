// Compile only: these kernels are deliberately never launched. This checks
// actual VM template/caller compatibility, not VM behavior or performance.
#include <cuda_runtime.h>
#include "cpp/src/runtime/gpu/device/execute_bytecode_device.cuh"
#include "private_window_executor_access.cuh"

namespace gagp::gpu_detail {

#define GAGP_PACKED_COMPILE_PROBE(Name, States, SoA, Flavor, Result) \
__global__ void Name(const DRegionSegment* segments, const Value* operands, \
    const DPayloadTables* payload_tables, DPayloadFlavorTraits<DPayloadFlavor::Flavor>::State* payloads, \
    const DExecutionTables* tables, int* fuel, const DRegionWorkspace* workspaces, DResult* outputs) { \
  const unsigned index = blockIdx.x * blockDim.x + threadIdx.x; \
  outputs[index] = d_run_window_region<PackedPrivateWindowLocalFrame<States>, SoA, \
      DPayloadFlavor::Flavor, ValueTag::Result>(segments[index], operands + index * States, \
          nullptr, 0, payload_tables[index], payloads[index], tables[index], fuel[index], workspaces[index]); \
}

GAGP_PACKED_COMPILE_PROBE(packed_probe_bound_aos_1, 1, false, BoundIntListViews, Int)
GAGP_PACKED_COMPILE_PROBE(packed_probe_bound_soa_1, 1, true, BoundIntListViews, Int)
GAGP_PACKED_COMPILE_PROBE(packed_probe_bound_aos_2, 2, false, BoundIntListViews, Int)
GAGP_PACKED_COMPILE_PROBE(packed_probe_bound_soa_2, 2, true, BoundIntListViews, Int)
GAGP_PACKED_COMPILE_PROBE(packed_probe_arena_aos_1, 1, false, ArenaMixed, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_arena_soa_1, 1, true, ArenaMixed, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_arena_aos_2, 2, false, ArenaMixed, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_arena_soa_2, 2, true, ArenaMixed, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_indexed_aos_1, 1, false, IndexedArena, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_indexed_soa_1, 1, true, IndexedArena, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_indexed_aos_2, 2, false, IndexedArena, IntList)
GAGP_PACKED_COMPILE_PROBE(packed_probe_indexed_soa_2, 2, true, IndexedArena, IntList)
#undef GAGP_PACKED_COMPILE_PROBE

} // namespace gagp::gpu_detail
