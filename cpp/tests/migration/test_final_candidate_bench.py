#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys
import tempfile


def write(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, separators=(",", ":")), encoding="utf-8")


def run(binary: str, arguments: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([binary, *arguments], capture_output=True, text=True)
    if result.returncode != expected:
        raise AssertionError(
            f"exit {result.returncode}, expected {expected}\nstdout={result.stdout}\nstderr={result.stderr}"
        )
    return result


def structure(nodes: list[dict[str, int]], names: list[str]) -> dict[str, object]:
    return {
        "version": "ast-prefix",
        "nodes": nodes,
        "names": names,
        "consts": [],
        "linear_rec_binders": [],
        "asgp_dc_binders": [],
        "asgp_dp1d_specs": [],
        "asgp_dp2d_specs": [],
    }


def value(tag: int, bits: str, **extra: object) -> dict[str, object]:
    return {"tag": tag, "bits": bits, "bool": False, **extra}


def member(nodes: list[dict[str, int]], constants: list[dict[str, object]]) -> dict[str, object]:
    return {"structure": structure(nodes, []), "constants": constants}


def assert_reproduction_result(result: dict[str, object]) -> None:
    if result["format_version"] != "migration-steady-cpu-reproduction-v1":
        raise AssertionError("CPU reproduction schema changed")
    if result["warmups"] != 1 or result["measured_trials"] != 1:
        raise AssertionError("CPU reproduction trial counts changed")
    samples = result["samples"]
    if len(samples) != 2 or samples[0]["warmup"] is not True or samples[1]["warmup"] is not False:
        raise AssertionError(f"CPU reproduction samples changed: {samples}")
    children = result["children"]
    if children["format_version"] != "final-candidate-children-v2":
        raise AssertionError("CPU reproduction child schema changed")
    if len(children["programs"]) != 1:
        raise AssertionError(f"CPU reproduction child count changed: {children}")


def main() -> int:
    binary = sys.argv[1]
    generator = sys.argv[2]
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        cases = root / "cases.json"
        snapshot = root / "population.json"
        output = root / "steady.json"
        write(cases, {"format_version": "fitness-cases", "cases": [{
            "inputs": {}, "expected": {"type": "int_list", "value": [2, 4, 6]},
        }]})
        write(snapshot, {"format_version": "migration-population-v1", "programs": [{
            "structure": structure([
                {"kind": 0, "i0": 0, "i1": 0},
                {"kind": 2, "i0": 0, "i1": 0},
                {"kind": 6, "i0": 0, "i1": 0},
                {"kind": 53, "i0": 0, "i1": 1},
                {"kind": 7, "i0": 0, "i1": 0},
                {"kind": 13, "i0": 0, "i1": 0},
                {"kind": 52, "i0": 0, "i1": 0},
                {"kind": 7, "i0": 1, "i1": 0},
                {"kind": 1, "i0": 0, "i1": 0},
            ], ["u"]),
            "constants": [
                value(5, "0003131ec2e3def1", elements=[
                    value(0, "0000000000000001"),
                    value(0, "0000000000000002"),
                    value(0, "0000000000000003"),
                ]),
                value(0, "0000000000000002"),
            ],
        }]})
        common = [
            "--snapshot", str(snapshot), "--cases", str(cases),
            "--population-size", "1", "--engine", "cpu",
            "--repro-backend", "cpu", "--repro-overlap", "off",
            "--fuel", "20000", "--out-json", str(output),
        ]
        run(binary, ["--action", "steady", *common, "--warmups", "1", "--trials", "1"])
        measured = json.loads(output.read_text(encoding="utf-8"))
        if measured["format_version"] != "migration-steady-eval-v1":
            raise AssertionError("steady schema changed")
        if measured["fitness"] != [1]:
            raise AssertionError(f"specialized payload migration changed fitness: {measured['fitness']}")
        rejected = run(binary, ["--action", "run", *common], expected=2)
        if "requires --grammar-definition" not in rejected.stderr:
            raise AssertionError(f"missing honest-membership diagnostic: {rejected.stderr}")
        rejected = run(binary, ["--action", "repro-steady", *common], expected=2)
        if "unsupported frozen reproduction version" not in rejected.stderr:
            raise AssertionError(f"missing frozen-reproduction diagnostic: {rejected.stderr}")

        bytecode_snapshot = root / "bytecode-population.json"
        bytecode_cases = root / "bytecode-cases.json"
        bytecode_output = root / "bytecode-steady.json"
        opaque_string = value(4, "0003000000001234", materialized=False)
        write(bytecode_snapshot, {
            "format_version": "migration-bytecode-population-v1",
            "programs": [{
                "format_version": "migration-bytecode-v1",
                "n_locals": 0,
                "consts": [opaque_string, value(0, "0000000000000001")],
                "code": [
                    [0, 0, 0, True, False],
                    [0, 1, 0, True, False],
                    [19, 7, 2, True, True],
                    [20, 0, 0, False, False],
                ],
                "var2idx": [], "dc": [], "dp1d": [], "dp2d": [],
            }],
        })
        write(bytecode_cases, {
            "format_version": "migration-evaluation-cases-v1",
            "cases": [{
                "inputs": [[0, value(0, "0000000000000000")]],
                "expected": value(0, "0000000000000001"),
            }],
        })
        bytecode_common = [
            "--snapshot", str(bytecode_snapshot), "--cases", str(bytecode_cases),
            "--population-size", "1", "--engine", "cpu",
            "--repro-backend", "cpu", "--repro-overlap", "off",
            "--fuel", "20000", "--out-json", str(bytecode_output),
        ]
        run(binary, ["--action", "steady", *bytecode_common,
                     "--warmups", "1", "--trials", "1"])
        bytecode_measured = json.loads(bytecode_output.read_text(encoding="utf-8"))
        if bytecode_measured["fitness"] != [-1]:
            raise AssertionError(
                f"opaque payload fallback replay changed fitness: {bytecode_measured['fitness']}"
            )
        bytecode_rejected = run(binary, ["--action", "run", *bytecode_common], expected=2)
        if "steady runtime evaluation only" not in bytecode_rejected.stderr:
            raise AssertionError(f"missing source-less-bytecode diagnostic: {bytecode_rejected.stderr}")

        grammar = root / "grammar.json"
        reproduction = root / "reproduction.json"
        reproduction_output = root / "reproduction-output.json"
        reproduction_repeat = root / "reproduction-repeat.json"
        write(grammar, {
            "format_version": "grammar-definition-v2",
            "entry": {"nonterminal": "Main", "category": "Program", "type": "Int"},
            "search_limits": {"max_nodes": 5, "max_depth": 4},
            "execution_limits": {"fuel": 100},
            "nonterminals": [{
                "id": "Main", "category": "Program", "type": "Int", "scope": [],
                "alternatives": [{
                    "id": "return-seven", "weight": 1,
                    "expression": {
                        "control": "program(Block)->Program", "type": "Int", "args": [{
                            "control": "block_cons(Statement,Block)->Block", "type": "Int", "args": [{
                                "control": "return(Int)->Statement", "type": "Int",
                                "args": [{"constant": {"type": "Int", "values": ["7"]}}],
                            }, {
                                "control": "block_nil()->Block", "type": "Int", "args": [],
                            }],
                        }],
                    },
                }],
            }],
        })
        current_cases = root / "current-cases.json"
        current_population = root / "current-population.json"
        current_output = root / "current-steady.json"
        write(current_cases, {"format_version": "fitness-cases", "cases": [{
            "inputs": {}, "expected": {"type": "int", "value": 7},
        }]})
        run(generator, [
            "--grammar-definition", str(grammar),
            "--cases", str(current_cases),
            "--out-json", str(current_population),
            "--population-size", "1", "--seed", "41",
        ])
        run(binary, [
            "--action", "steady", "--snapshot", str(current_population),
            "--cases", str(current_cases), "--grammar-definition", str(grammar),
            "--population-size", "1", "--engine", "cpu",
            "--repro-backend", "cpu", "--repro-overlap", "off",
            "--fuel", "100", "--penalty", "9", "--max-total-nodes", "5", "--max-expr-depth", "4",
            "--warmups", "1", "--trials", "1", "--out-json", str(current_output),
        ])
        current_measured = json.loads(current_output.read_text(encoding="utf-8"))
        if current_measured["fitness"] != [0]:
            raise AssertionError(
                f"current population replay changed fitness: {current_measured['fitness']}"
            )
        current_run = root / "current-run.json"
        current_run_args = [
            "--action", "run", "--snapshot", str(current_population),
            "--cases", str(current_cases), "--grammar-definition", str(grammar),
            "--population-size", "1", "--generations", "1", "--engine", "cpu",
            "--repro-backend", "cpu", "--repro-overlap", "off",
            "--selection-pressure", "1", "--fuel", "100",
            "--max-total-nodes", "5", "--max-expr-depth", "4",
            "--mutation-rate", "0", "--mutation-subtree-prob", "0.8",
            "--seed", "41", "--skip-final-eval", "on",
            "--retain-final-population", "off", "--out-json", str(current_run),
        ]
        run(binary, current_run_args)
        evolved = json.loads(current_run.read_text(encoding="utf-8"))
        if evolved["format_version"] != "migration-run-v1" or len(evolved["generations"]) != 1:
            raise AssertionError(f"current population evolution schema changed: {evolved}")
        assert "final_population" not in evolved
        assert "final_population_validation" not in evolved
        # Skipping final evaluation takes precedence over retention in the
        # evolution API; it cannot yield a scored final-population artifact.
        retained_but_skipped = current_run_args[:]
        retained_but_skipped[retained_but_skipped.index("--retain-final-population") + 1] = "on"
        run(binary, retained_but_skipped)
        assert "final_population" not in json.loads(current_run.read_text())
        current_reproduction = root / "current-reproduction.json"
        current_repro_common = [
            "--action", "repro-steady", "--snapshot", str(current_population),
            "--cases", str(current_cases), "--grammar-definition", str(grammar),
            "--population-size", "1", "--engine", "cpu",
            "--repro-backend", "cpu", "--repro-overlap", "off",
            "--selection-pressure", "1", "--fuel", "100",
            "--max-total-nodes", "5", "--max-expr-depth", "4",
            "--mutation-rate", "0", "--mutation-subtree-prob", "0.8",
            "--seed", "41", "--warmups", "1", "--trials", "1",
            "--out-json", str(current_reproduction),
        ]
        run(binary, current_repro_common)
        current_reproduced = json.loads(current_reproduction.read_text(encoding="utf-8"))
        assert_reproduction_result(current_reproduced)
        if current_reproduced["fitness_prepare_ms"] <= 0:
            raise AssertionError("current reproduction omitted fixed-fitness preparation")
        # A larger physical representation budget must not relax source admission.
        resource_grammar = root / "resource-grammar.json"
        resource_population = root / "resource-population.json"
        definition = json.loads(grammar.read_text())
        definition["search_limits"] = {"max_nodes": 80, "max_depth": 20}
        write(resource_grammar, definition)
        run(generator, ["--grammar-definition", str(resource_grammar),
                        "--cases", str(current_cases), "--out-json", str(resource_population),
                        "--population-size", "1", "--seed", "41"])
        resource_common = list(current_repro_common)
        for option, setting in {
            "--grammar-definition": str(resource_grammar),
            "--snapshot": str(resource_population),
            "--max-total-nodes": "80", "--max-expr-depth": "20",
        }.items():
            resource_common[resource_common.index(option) + 1] = setting
        run(binary, [*resource_common, "--source-max-total-nodes", "5",
                     "--source-max-expr-depth", "1"])
        resource_result = json.loads(current_reproduction.read_text())
        assert_reproduction_result(resource_result)
        if resource_result["source_resource_budget"] != {
            "max_nodes": 5, "offspring_max_depth": 1, "initial_depth_unbounded": True,
            "physical_max_nodes": 80, "physical_max_depth": 20,
        }:
            raise AssertionError("source/physical budget evidence missing")
        rejected = run(binary, [*resource_common, "--source-max-total-nodes", "4",
                               "--source-max-expr-depth", "1"], expected=2)
        if "initial source resource budget exceeded" not in rejected.stderr:
            raise AssertionError(f"source budget was not enforced: {rejected.stderr}")
        rejected = run(binary, [*resource_common, "--source-max-total-nodes", "5"], expected=2)
        if "must both be positive" not in rejected.stderr:
            raise AssertionError(f"incomplete source budget accepted: {rejected.stderr}")
        rejected = run(binary, [*resource_common, "--minimum-dc-frames", "4"], expected=2)
        if "only to frozen source AST imports" not in rejected.stderr:
            raise AssertionError(f"DC frame reservation accepted a current artifact: {rejected.stderr}")
        rejected = run(binary, [*resource_common, "--normalize-typed-storage", "on"], expected=2)
        if "only to frozen source AST imports" not in rejected.stderr:
            raise AssertionError(f"storage normalization accepted a current artifact: {rejected.stderr}")
        parent = member([
            {"kind": 0, "i0": 0, "i1": 0},
            {"kind": 2, "i0": 0, "i1": 0},
            {"kind": 6, "i0": 0, "i1": 0},
            {"kind": 7, "i0": 0, "i1": 0},
            {"kind": 1, "i0": 0, "i1": 0},
        ], [value(0, "0000000000000007")])
        donor = {
            "format_version": "migration-population-v1",
            "programs": [member(
                [{"kind": 7, "i0": 0, "i1": 0}],
                [value(0, "0000000000000007")],
            )],
        }
        write(reproduction, {
            "format_version": "migration-reproduction-v1",
            "parents": {"format_version": "migration-population-v1", "programs": [parent]},
            "fitness": [1],
            "config": {
                "population_size": 1,
                "pair_count": 1,
                "candidates_per_program": 1,
                "donor_pool_size_per_type": 1,
                "max_nodes": 5,
                "max_donor_nodes": 1,
                "max_names": 1,
                "max_consts": 1,
                "max_linear_rec_binders": 0,
                "max_asgp_dc_binders": 0,
                "max_asgp_dp1d_specs": 0,
                "max_asgp_dp2d_specs": 0,
                "tournament_k": 1,
                "max_expr_depth": 4,
                "max_for_k": 16,
                "mutation_ratio": 0,
                "mutation_subtree_ratio": 0.8,
                "seed": "41",
            },
            "subtree_ends": [[5, 5, 4, 4, 5]],
            "candidates": [[]],
            "donors": [{"type": type_index, "fragment": donor} for type_index in range(9)],
        })
        repro_common = [
            "--action", "repro-steady",
            "--snapshot", str(reproduction),
            "--cases", str(cases),
            "--grammar-definition", str(grammar),
            "--population-size", "1",
            "--engine", "cpu",
            "--repro-backend", "cpu",
            "--repro-overlap", "off",
            "--selection-pressure", "1",
            "--fuel", "100",
            "--max-total-nodes", "5",
            "--max-expr-depth", "4",
            "--mutation-rate", "0",
            "--mutation-subtree-prob", "0.8",
            "--seed", "41",
            "--warmups", "1",
            "--trials", "1",
        ]
        run(binary, [*repro_common, "--out-json", str(reproduction_output)])
        first = json.loads(reproduction_output.read_text(encoding="utf-8"))
        assert_reproduction_result(first)
        run(binary, [*repro_common, "--out-json", str(reproduction_repeat)])
        second = json.loads(reproduction_repeat.read_text(encoding="utf-8"))
        assert_reproduction_result(second)
        if first["children"] != second["children"]:
            raise AssertionError("CPU reproduction changed across fresh processes")
        mixed_grammar = json.loads(grammar.read_text())
        mixed_grammar["nonterminals"].append({
            "id": "Real", "type": "Float", "scope": [], "alternatives": [{
                "id": "two", "weight": 1,
                "expression": {"constant": {"type": "Float", "values": [2.0]}}}]})
        mixed_grammar_path = root / "mixed-grammar.json"
        write(mixed_grammar_path, mixed_grammar)
        float_parent = json.loads(json.dumps(parent))
        float_parent["constants"] = [value(1, "4000000000000000")]
        mixed_snapshot = root / "mixed-population.json"
        write(mixed_snapshot, {"format_version": "migration-population-v1",
                               "programs": [parent, float_parent]})
        mixed_cases = root / "mixed-cases.json"
        write(mixed_cases, {"format_version": "fitness-cases", "cases": [
            {"inputs": {}, "expected": {"type": "int", "value": 7}},
            {"inputs": {}, "expected": {"type": "float", "value": 2.0}}]})
        mixed_output = root / "mixed-output.json"
        mixed_args = ["--action", "run", "--snapshot", str(mixed_snapshot),
                      "--cases", str(mixed_cases), "--grammar-definition", str(mixed_grammar_path),
                      "--population-size", "2", "--generations", "2", "--selection-pressure", "1",
                      "--fuel", "100", "--penalty", "9", "--max-total-nodes", "5", "--max-expr-depth", "4",
                      "--engine", "cpu", "--repro-backend", "cpu", "--out-json", str(mixed_output)]
        run(binary, [*mixed_args, "--population-roots", "Main,Real",
                     "--retain-final-population", "on"])
        mixed_result = json.loads(mixed_output.read_text())
        assert [g["mean_fitness"] for g in mixed_result["generations"]] == [-5, -5]
        assert mixed_result["final_population"]["format_version"] == "final-candidate-children-v2"
        assert len(mixed_result["final_population"]["programs"]) == 2
        assert mixed_result["final_population_validation"] == {
            "members": 2, "native_membership_lowering": True,
            "budget": "initial-including-grandfathered-parents",
        }
        absent = run(binary, mixed_args, expected=2)
        assert "mixed-return frozen cases" in absent.stderr
        for roots, diagnostic in [("Main,Missing", "unknown population root"),
                                  ("Main,Main", "distinct exact result types"),
                                  ("Main,", "nonempty comma-separated"),
                                  ("Main,,Real", "empty root ID")]:
            rejected = run(binary, [*mixed_args, "--population-roots", roots], expected=2)
            assert diagnostic in rejected.stderr, rejected.stderr

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
