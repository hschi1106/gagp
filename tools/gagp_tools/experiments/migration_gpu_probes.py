"""Observer-only CUDA variation counters for a separately compiled diagnostic."""
from __future__ import annotations


EVENTS = (
    "variation.assembled", "variation.parent_fallback", "metadata.valid", "metadata.invalid",
    "mutation.none", "mutation.subtree", "mutation.constant", "output.valid", "output.invalid",
    "fallback.invalid_crossover", "fallback.node_limit", "fallback.prefix_out_of_range",
    "fallback.nonpositive_replacement", "fallback.missing_donor",
)


def before(source: str, anchor: str, code: str) -> str:
    if source.count(anchor) != 1:
        raise ValueError(f"expected exactly one GPU probe anchor: {anchor}")
    return source.replace(anchor, "// MIGRATION PROBE BEGIN\n" + code +
                          "\n// MIGRATION PROBE END\n" + anchor, 1)


def instrument_variation(source: str) -> str:
    for side in ("a", "b"):
        code = f"""  if (tid == 0) {{
    atomicAdd(&::gagp::migration::device_decisions[child_{side}_fallback ? 1 : 0], 1ULL);
    atomicAdd(&::gagp::migration::device_decisions[4 + static_cast<int>(mutation_kind_{side})], 1ULL);
    if (!valid_cross_{side}) atomicAdd(&::gagp::migration::device_decisions[9], 1ULL);
    if (out_{side}_len > max_nodes) atomicAdd(&::gagp::migration::device_decisions[10], 1ULL);
    if (prefix_{side} >= len_{side}) atomicAdd(&::gagp::migration::device_decisions[11], 1ULL);
    if (replace_{side} <= 0) atomicAdd(&::gagp::migration::device_decisions[12], 1ULL);
    if (donor_ptr_for_{side} == nullptr) atomicAdd(&::gagp::migration::device_decisions[13], 1ULL);
  }}"""
        source = before(source, f"  if (child_{side}_fallback) {{", code)
        source = before(source, f"    if (child_{side}_fallback) child_{side}_meta.valid = 0;",
            f"    atomicAdd(&::gagp::migration::device_decisions[child_{side}_meta.valid ? 2 : 3], 1ULL);")
        source = before(source, f"    child_meta_out[out_idx_{side}] = child_{side}_meta;",
            f"    atomicAdd(&::gagp::migration::device_decisions[child_{side}_meta.valid ? 7 : 8], 1ULL);")
    return source


def instrument_launch(source: str) -> str:
    count = len(EVENTS)
    source = before(source, '#include "device/variation_kernels.cuh"', f"""namespace gagp::migration {{
__device__ unsigned long long device_decisions[{count}];
void record_operator_decision(const char*);
}}""")
    source = before(source, "  const auto variation_t0 = std::chrono::steady_clock::now();", f"""  unsigned long long captured_decisions[{count}] = {{}};
  if (!ensure_cuda(cudaMemcpyToSymbol(::gagp::migration::device_decisions, captured_decisions,
                                      sizeof(captured_decisions)), "reset diagnostic counters", message_out)) {{
    return false;
  }}""")
    names = ",\n".join(f'      "gpu_kernel.{event}"' for event in EVENTS)
    source = before(source, "  const auto variation_t1 = std::chrono::steady_clock::now();", f"""  if (!ensure_cuda(cudaMemcpyFromSymbol(captured_decisions, ::gagp::migration::device_decisions,
                                        sizeof(captured_decisions)), "read diagnostic counters", message_out)) {{
    return false;
  }}
  const char* decision_names[] = {{
{names}}};
  for (int event = 0; event < {count}; ++event) {{
    for (unsigned long long i = 0; i < captured_decisions[event]; ++i) {{
      ::gagp::migration::record_operator_decision(decision_names[event]);
    }}
  }}""")
    return source
