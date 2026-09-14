"""Generate auditable, host-only operator decision probes outside the reference tree.

Every anchor must match exactly once. This intentionally fails closed when the
reference implementation changes. Generated translation units are diagnostics,
never performance baselines. GPU kernel decisions require separate probes.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path

from gagp_tools.experiments.migration_gpu_probes import EVENTS, instrument_launch, instrument_variation


DECLARATION = (
    "namespace gagp::migration { void record_operator_decision(const char*); }\n"
)


def insert_event(source: str, anchor: str, event: str) -> str:
    """Insert a counter before the final line of an exact multiline anchor."""
    if source.count(anchor) != 1:
        raise ValueError(f"expected exactly one anchor for {event}")
    lines = anchor.splitlines(keepends=True)
    indent = lines[-1][:len(lines[-1]) - len(lines[-1].lstrip())]
    replacement = "".join(lines[:-1]) + (
        f'{indent}::gagp::migration::record_operator_decision("{event}");\n'
    ) + lines[-1]
    return source.replace(anchor, replacement, 1)


def insert_event_variant(source: str, anchors: tuple[str, ...], event: str) -> str:
    """Accept one explicit historical/current spelling, never an ambiguous match."""
    matches = [anchor for anchor in anchors if source.count(anchor)]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one supported anchor for {event}")
    return insert_event(source, matches[0], event)


def instrument(source: str, kind: str) -> tuple[str, list[str]]:
    events: list[str] = []

    def add(anchor: str, name: str) -> None:
        nonlocal source
        event = f"{kind}.{name}"
        source = insert_event(source, anchor, event)
        events.append(event)

    if kind in ("crossover", "backend"):
        for condition, name in (("candidate.nodes.empty()", "child.empty"),
                ("out.meta.node_count > limits.max_total_nodes", "child.node_limit"),
                ("out.meta.max_depth > limits.max_expr_depth", "child.depth_limit")):
            add(f"  if ({condition}) {{\n    return fallback_parent;", name)
        add("    return fallback_parent;\n  }\n  return out;", "child.accepted")
    if kind == "crossover":
        for condition, name in (("expr_a.empty() || expr_b.empty()", "pair.no_expression"),
                ("compatible_a.empty()", "pair.no_compatible_expression"),
                ("roots_a.empty() || roots_b.empty()", "pair.no_compatible_root")):
            add(f"  if ({condition}) {{\n    return {{parent_a, parent_b}};", name)
    elif kind == "mutation":
        add("  if (genome.ast.nodes.empty()) {\n    return generate_random_genome(seed, limits, grammar);",
            "empty_parent.regenerated")
        add("  if (std::bernoulli_distribution(subtree_prob)(rng)) {\n    mutated = typed_subtree_mutation(genome.ast, verified, rng, limits, grammar);",
            "subtree.attempted")
        add("  if (mutated.nodes.empty()) {\n    mutated = constant_perturbation(genome.ast, rng);",
            "constant.attempted")
        for condition, name in (("mutated.nodes.empty()", "child.empty"),
                ("out.meta.node_count > limits.max_total_nodes", "child.node_limit"),
                ("out.meta.max_depth > limits.max_expr_depth", "child.depth_limit")):
            add(f"  if ({condition}) {{\n    return genome;", name)
        add("    return genome;\n  }\n  return out;", "child.accepted")
    elif kind == "backend":
        add("  if (!pair.valid) {\n    return {parent_a, parent_b};", "pair.invalid")
        add("  if (!candidate_is_valid(target)) {\n    return parent;", "donor.invalid_target")
        add("  if (donor == nullptr || donor->ast.nodes.empty()) {\n    return parent;", "donor.unavailable")
        add("    auto maybe_mutate = [&](ProgramGenome& child, const ProgramGenome& parent, const CandidateRange& site) {\n      if (prob_dist(rng) < cfg.mutation_rate) {",
            "mutation.eligible")
        add("      if (prob_dist(rng) < cfg.mutation_rate) {\n        const auto mutation_t0 = std::chrono::steady_clock::now();",
            "mutation.selected")
        # The compiled branch precedes this legacy ablation in the candidate;
        # frozen references retain the original leading if. Both remain exact.
        event = "backend.mutation.invalid_coupled_pair"
        condition = "(cfg.cpu_repro_ablation == CpuReproAblation::GpuCoupledDonor && !coupled_pair.valid) {\n          child = parent;"
        source = insert_event_variant(source, ("        if " + condition,
            "        } else if " + condition), event)
        events.append(event)
    elif kind == "pack":
        add("    if (copyback.child_meta[static_cast<std::size_t>(child_index)].valid == 0) {\n      next = fallback_parent_for_child(scored, copyback, child_index);",
            "decode.device_invalid")
        add("      if (next.ast.nodes.empty()) {\n        next = fallback_parent_for_child(scored, copyback, child_index);",
            "decode.empty")
        add("            verify_ast(compacted.ast, cfg.verification_inputs)) {\n          out.push_back(std::move(compacted));",
            "decode.accepted")
        add("          continue;\n        }\n        next = fallback_parent_for_child(scored, copyback, child_index);",
            "decode.invalid_binders_or_ast")
        add("  while (static_cast<int>(out.size()) < cfg.population_size) {\n    out.push_back(compact_genome_tables(*scored.front().genome));",
            "decode.population_padding")
    elif kind != "crossover":
        raise ValueError(f"unknown operator source {kind}")
    return DECLARATION + source, events


def generate(reference: Path, output: Path, gpu: bool = False) -> dict:
    generated = {}
    for relative, kind in (("crossover.cpp", "crossover"), ("mutation.cpp", "mutation"),
                            ("repro/backend.cpp", "backend"), ("repro/pack.cpp", "pack")):
        path = reference / "cpp/src/evolution" / relative
        original = path.read_text()
        modified, events = instrument(original, kind)
        generated[relative] = (path, original, modified, events)
    if gpu:
        for relative, transform in (("repro/gpu/launch.cu", instrument_launch),
                ("repro/gpu/device/variation_kernels.cuh", instrument_variation)):
            path = reference / "cpp/src/evolution" / relative
            original = path.read_text()
            generated[relative] = (path, original, transform(original), list(EVENTS))
    # Validate every source before creating any output.
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"version": "migration-operator-probes-v1", "sources": {}, "gpu": gpu,
                "scope": ("host decisions and GPU variation, mutation choice, metadata and output decisions"
                          if gpu else "host decisions only; excludes GPU kernel internal fallbacks")}
    for relative, (path, original, modified, events) in generated.items():
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(modified)
        (output / (target.stem + ".diff")).write_text("".join(difflib.unified_diff(
            original.splitlines(keepends=True), modified.splitlines(keepends=True),
            fromfile=str(path), tofile=str(target))))
        manifest["sources"][relative] = {
            "reference": str(path), "reference_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "generated_sha256": hashlib.sha256(target.read_bytes()).hexdigest(), "events": events}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    generate(args.reference.resolve(), args.output.resolve(), args.gpu)


if __name__ == "__main__":
    main()
