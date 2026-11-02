#pragma once

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace gagp {

// Engine profile: process defaults of the exact engine switches.
//
// The engine grew one environment switch per optimization. Every switch listed
// here is exact: it changes speed or memory use, never a published genotype,
// fitness value, champion or random draw (CPU reference equality is tested in
// gagp_test_phase_population_cpu_reference and the CPU/GPU parity tests).
// The values are the settings of the measured mainline (the formal G64
// optimization settings plus the warp/choice-table/initial-replay switches), so
// a run needs no switch to use the mainline engine.
//
// Lookup: an explicitly set variable always wins. Otherwise the profile value
// is used unless GAGP_ENGINE_PROFILE=legacy, which restores the historical
// "every switch unset" behavior for ablations and old records.
//
// Search semantics are not here: they belong to gagp-search-config-v1
// (spec/search_config.md). Diagnostics (GAGP_GPU_DIAGNOSTICS, GAGP_SEARCH_TRACE),
// device choice (GAGP_CUDA_DEVICE) and host workers (GAGP_HOST_THREADS) are
// also not profile members.
struct EngineDefault {
  const char* name;
  const char* value;
};

inline constexpr EngineDefault kMainlineEngineProfile[] = {
    {"GAGP_BATCH_PHASE_EXPORT", "1"},
    {"GAGP_BATCH_PHASE_IMPORT", "1"},
    {"GAGP_BOUND_ADD_PHASE", "1"},
    {"GAGP_BOUND_COMPARE_PHASE", "1"},
    {"GAGP_BUCKET_WORKSPACE", "1"},
    {"GAGP_COLD_IMPORT_EXECUTABLES", "1"},
    {"GAGP_COLD_PAYLOAD_PROOFS", "1"},
    {"GAGP_COMPACT_FRAMES", "1"},
    {"GAGP_CONSTANT_PHASE", "1"},
    {"GAGP_COORDINATE_EXECUTOR", "1"},
    {"GAGP_COORDINATE_RING_MEMO", "1"},
    {"GAGP_CPU_ALL_FIT_PAIRS", "1"},
    {"GAGP_CPU_BOUND_PHASE", "1"},
    {"GAGP_CPU_COMPACT_ADMISSION", "1"},
    {"GAGP_CPU_COORDINATE_DRIVER", "1"},
    {"GAGP_CPU_COORDINATE_VIEWS", "1"},
    {"GAGP_CPU_COUNTED_PAIRS", "1"},
    {"GAGP_CPU_DENSE_MEMO", "1"},
    {"GAGP_CPU_FUSED_ADMISSION", "1"},
    {"GAGP_CPU_LAZY_ANALYSIS", "1"},
    {"GAGP_CPU_REGION_VIEWS", "1"},
    {"GAGP_CPU_SCALAR_VIEWS", "1"},
    {"GAGP_CPU_SELECTED_MUTATION", "1"},
    {"GAGP_CPU_SELECTED_PARENTS", "1"},
    {"GAGP_CPU_TYPED_PHASE", "1"},
    {"GAGP_DIRECT_PHASE", "1"},
    {"GAGP_DIRECT_ROOT", "1"},
    {"GAGP_DYNAMIC_PROGRAM_QUEUE", "1"},
    {"GAGP_FUSED_COLD_EXECUTABLE", "0"},
    {"GAGP_GPU_ADAPTIVE_REPLAY", "1"},
    {"GAGP_GPU_CASE_VIEW_BLOCK", "hybrid"},
    {"GAGP_GPU_CHOICE_TABLES", "1"},
    {"GAGP_GPU_COALESCED_COUNTS", "0"},
    {"GAGP_GPU_COMPACT_COMPILE_FRAMES", "1"},
    {"GAGP_GPU_COMPACT_INTLIST_CELLS", "1"},
    {"GAGP_GPU_CONCAT_CACHE", "1"},
    {"GAGP_GPU_CONSTRUCTIVE_EXPORT", "1"},
    {"GAGP_GPU_CUDA_POOL", "0"},
    {"GAGP_GPU_DEFERRED_CROSSOVER_COPY", "1"},
    {"GAGP_GPU_DENSE_CASE_LANES", "0"},
    {"GAGP_GPU_DENSE_CASE_TILES", "tuple"},
    {"GAGP_GPU_DENSE_COORDINATE_MEMO", "1"},
    {"GAGP_GPU_FUSED_DONOR_SITES", "1"},
    {"GAGP_GPU_FUSED_LIST_CONCAT", "1"},
    {"GAGP_GPU_FUSED_SPLICE_DEPTH", "1"},
    {"GAGP_GPU_GLOBAL_PAYLOAD", "1"},
    {"GAGP_GPU_GLOBAL_WINDOW_ALIASES", "1"},
    {"GAGP_GPU_INITIAL_REPLAY_CAPACITY", "1"},
    {"GAGP_GPU_INPLACE_MUTATION", "1"},
    {"GAGP_GPU_INTEGER_CASE_REUSE", "1"},
    {"GAGP_GPU_LINEAR_ELITE", "1"},
    {"GAGP_GPU_PARENT_FITNESS_CACHE", "1"},
    {"GAGP_GPU_PAYLOAD_EPOCH_INDEX", "1"},
    {"GAGP_GPU_PAYLOAD_INDEX", "1"},
    {"GAGP_GPU_PAYLOAD_WORKSPACE_MIB", "16384"},
    {"GAGP_GPU_PRIVATE_ADJACENT_CONCAT", "1"},
    {"GAGP_GPU_PRIVATE_BOUND_PHASES", "1"},
    {"GAGP_GPU_PRIVATE_CELL_SOA", "0"},
    {"GAGP_GPU_PRIVATE_GLOBAL_OFFSETS", "1"},
    {"GAGP_GPU_PRIVATE_INTLIST_OFFSETS", "1"},
    {"GAGP_GPU_PRIVATE_LAUNCH_BOUND", "32"},
    {"GAGP_GPU_PRIVATE_MEASURE_CACHE", "1"},
    {"GAGP_GPU_PRIVATE_TUPLE_SCORE", "1"},
    {"GAGP_GPU_PRIVATE_TYPED_PHASES", "1"},
    {"GAGP_GPU_PRIVATE_WARP_COPY", "1"},
    {"GAGP_GPU_PRIVATE_WINDOW_EXECUTOR", "1"},
    {"GAGP_GPU_PROJECT_SHAPE_INPUTS", "0"},
    {"GAGP_GPU_REGISTER_PHASE_STACK", "0"},
    {"GAGP_GPU_RELAXED_VIEW_REGISTERS", "1"},
    {"GAGP_GPU_REUSE_PRIVATE_WORKSPACE", "1"},
    {"GAGP_GPU_SHAPE_RESULT_CACHE", "1"},
    {"GAGP_GPU_SHORT_EDIT_DISTANCE", "0"},
    {"GAGP_GPU_SIMPLE_PHASES", "0"},
    {"GAGP_GPU_SMALL_INT_HASH", "1"},
    {"GAGP_GPU_SORT_ARENA_CASES", "1"},
    {"GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT", "0"},
    {"GAGP_GPU_STAGE_STORAGE", "1"},
    {"GAGP_GPU_STATIC_WEIGHTS", "1"},
    {"GAGP_GPU_TILED_INITIAL_PAYLOAD", "auto"},
    {"GAGP_GPU_TILED_REPLAY", "1"},
    {"GAGP_GPU_TRIVIAL_PHASES", "1"},
    {"GAGP_GPU_UNCHANGED_FITNESS_REUSE", "1"},
    {"GAGP_GPU_VIEW_BOUND128", "0"},
    {"GAGP_GPU_VIEW_PHYSICAL_BLOCK", "0"},
    {"GAGP_GPU_WARP_VARIATION", "1"},
    {"GAGP_GPU_WINDOW_LEAF_DIRECT", "1"},
    {"GAGP_HYBRID_PHASE_EXECUTOR", "1"},
    {"GAGP_MEASURE_PHASE_CACHE", "1"},
    {"GAGP_OWNED_CROSSOVER_HANDOFF", "1"},
    {"GAGP_OWNED_PHASE_CAPS", "1"},
    {"GAGP_OWNED_PHASE_PACK", "1"},
    {"GAGP_OWNED_PHASE_PACKED", "1"},
    {"GAGP_OWNED_PREPARATION", "1"},
    {"GAGP_PER_PHASE_STACK_BOUND", "1"},
    {"GAGP_PHASE_CAP_BUCKETS", "1"},
    {"GAGP_PHASE_REPRO_THREADS", "1"},
    {"GAGP_POPULATION_HANDOFF", "1"},
    {"GAGP_PREPARED_CASE_VIEWS", "1"},
    {"GAGP_REUSE_ADMISSION_COMPILE", "1"},
    {"GAGP_REUSE_IMPORTED_EXECUTABLE", "1"},
    {"GAGP_REUSE_IMPORT_CERTIFICATE", "1"},
    {"GAGP_REUSE_PHASE_GRAMMAR", "1"},
    {"GAGP_REUSE_SITE_CONTRACTS", "1"},
    {"GAGP_SELECTED_SITES", "1"},
    {"GAGP_SINGLE_EVAL_IDENTITY", "1"},
    {"GAGP_SORT_CASES", "1"},
    {"GAGP_TYPED_VIEW_PHASE", "1"},
    {"GAGP_UNBOXED_WINDOW_FRAMES", "1"},
    {"GAGP_VIEW_PROFILE", "1"},
    {"GAGP_WINDOW_EXECUTOR", "1"},
    {"GAGP_WINDOW_SOA", "1"},
};

inline bool engine_profile_enabled() {
  const char* profile = std::getenv("GAGP_ENGINE_PROFILE");
  if (!profile || std::strcmp(profile, "mainline") == 0) return true;
  if (std::strcmp(profile, "legacy") == 0) return false;
  throw std::invalid_argument("GAGP_ENGINE_PROFILE must be mainline or legacy");
}

// Drop-in replacement of std::getenv for engine switches.
inline const char* engine_env(const char* name) {
  if (const char* value = std::getenv(name)) return value;
  if (!engine_profile_enabled()) return nullptr;
  for (const auto& entry : kMainlineEngineProfile)
    if (std::strcmp(entry.name, name) == 0) return entry.value;
  return nullptr;
}

}  // namespace gagp
