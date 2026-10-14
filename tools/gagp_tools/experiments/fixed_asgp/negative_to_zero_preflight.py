"""Source evidence for NegativeToZero scoring and reproduction profiles.

This checks reviewed source boundaries, not CUDA execution or task parity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def evidence(root: Path, path: str, *needles: str) -> dict:
    source = root / path
    text = source.read_text()
    anchors = []
    for needle in needles:
        if needle not in text:
            raise ValueError(f"capability evidence changed; inspect {source}: {needle}")
        anchors.append({"line": text[:text.index(needle)].count("\n") + 1,
                        "text": needle})
    return {"path": str(source.resolve()), "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "anchors": anchors}


def preflight(root: Path, asgp: Path) -> dict:
    anchors = {
        "asgp_task": evidence(asgp, "src/task/problems.cc",
            "void NegativeToZero(TaskSpec& spec, ProblemData& data) {\n"
            "  spec.carrier = Type::kIntList;\n  spec.output = Type::kIntList;\n"
            "  spec.metric = Metric::kLevenshtein;",
            "constexpr int kMinLen = 1;", "constexpr int kMaxLen = 50;",
            "RandomInts(rng, kMinLen, kMaxLen, -50, 50)",
            "for (int64_t& value : mapped) value = value < 0 ? 0 : value;"),
        "asgp_distance": evidence(asgp, "src/task/task.cc",
            "if (metric == Metric::kLevenshtein && list) {\n"
            "    return EditDistance(produced.list, expected.list, scratch);"),
        "asgp_defaults": evidence(asgp, "src/task/task.h",
            "Scheme scheme = Scheme::kDivideConquer;", "Proximity proximity = Proximity::kRewarded;",
            "int base_size = 1;"),
        "asgp_fitness": evidence(asgp, "src/gp/fitness.cc",
            "return std::min(score.error, config.max_case_error);"),
        "asgp_list_language": evidence(asgp, "src/lang/grammar.cc",
            "Leaf(NodeKind::kEmptyList, Type::kIntList)",
            "Unary(NodeKind::kSingleton, Type::kIntList, Type::kInt)",
            "Binary(NodeKind::kConcat, Type::kIntList, Type::kIntList,"),
        "gagp_fitness": evidence(root, "spec/fitness.md",
            "- `IntList`", "score_case(actual, expected) = 1 if actual == expected else 0"),
        "gpu_list_constant_rejection": evidence(root, "cpp/src/evolution/repro/gpu/phase_grammar.hpp",
            '(d.type!=RType::Int && d.type!=RType::Bool) || d.sampling || d.elements || d.float_range',
            'fail("constant domain capability")'),
        "gpu_list_builtin_rejection": evidence(root, "cpp/src/evolution/repro/gpu/phase_grammar.hpp",
            "case BuiltinId::Len:case BuiltinId::Index:case BuiltinId::Slice:break;\n"
            '          default:fail("builtin capability");'),
        "gpu_import_constant_rejection": evidence(root, "cpp/src/evolution/repro/gpu/phase_layout.hpp",
            "value.tag == ValueTag::Int || value.tag == ValueTag::Bool,\n"
            '                         "registry/noninteger constants require native fallback"'),
        "generic_gpu_list_transport": evidence(root, "cpp/src/evolution/repro/gpu/device/constant_mutation.cuh",
            "case RType::IntList: *tag = ValueTag::IntList; return true;"),
        "generic_gpu_list_test": evidence(root, "cpp/tests/gpu/test_compiled_payload_evolution_gpu.cpp",
            '{"IntList", ValueTag::IntList,',
            "config.reproduction_backend = mode <= 1 ? repro::ReproductionBackend::Cpu\n"
            "                                        : repro::ReproductionBackend::Gpu;"),
    }
    return {
        "contract": "negative-to-zero-capability-v1", "task": "negative_to_zero",
        "status": "native_objective_mismatch_generic_gpu_requires_validation",
        "requested_workload": {"population": 1024, "cases": 1024, "host_threads": 1},
        "evidence_kind": "reviewed_source_anchors_with_hashes",
        "semantics": {"input": "IntList, random length 1..50, elements -50..50",
                      "output": "IntList with each negative element replaced by zero",
                      "asgp_objective": "minimize summed capped Levenshtein distance",
                      "gagp_objective": "maximize summed exact list matches"},
        "ranking_counterexample": {"expected": [0, 0], "candidates": [[1, 0], [1, 1]],
                                   "asgp_distances": [1, 2], "gagp_scores": [0, 0]},
        "limitations": ["Existing native list fitness loses the ASGP distance ranking.",
                        "Fast GPU phase reproduction rejects empty list constants and list constructors.",
                        "Generic GPU reproduction supports typed lists; validate this task separately."],
        "common_policy": "Explicit ASGP Metric::kExact and GAGP penalty=0 align correct-case ranking; this changes the native ASGP objective.",
        "modes": {mode: "not_measured" for mode in
                  ("asgp_1t", "gagp_cpu", "gpu_eval", "gpu_repro", "gpu_overlap")},
        "evidence": anchors,
        "gpu_execution": "not_run",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asgp-source", type=Path, required=True)
    parser.add_argument("--out", type=Path, help="new evidence JSON; existing files are never overwritten")
    args = parser.parse_args()
    result = preflight(ROOT, args.asgp_source)
    text = json.dumps(result, indent=2) + "\n"
    if args.out:
        with args.out.open("x") as handle:
            handle.write(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
