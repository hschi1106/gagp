#!/usr/bin/env python3
import json
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile


def run(cli: str, arguments: list[str], expected: int = 0,
        cwd: pathlib.Path | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([cli, *arguments], text=True, capture_output=True, cwd=cwd)
    if result.returncode != expected:
        raise AssertionError(
            f"exit {result.returncode}, expected {expected}\nstdout={result.stdout}\nstderr={result.stderr}"
        )
    return result


def write(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, separators=(",", ":")), encoding="utf-8")


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)


def sha256(value: object) -> str:
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def main() -> int:
    cli = sys.argv[1]
    repository = pathlib.Path(sys.argv[2])
    evaluator = sys.argv[3]
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        cases = root / "cases.json"
        write(cases, {
            "format_version": "fitness-cases",
            "cases": [{"inputs": {}, "expected": {"type": "int", "value": 7}}],
        })

        # Every checked legacy preset must convert from content alone.
        outputs: dict[str, str] = {}
        for preset in ("all", "num_list", "scalar", "sequence", "string", "string_list"):
            source = repository / "configs" / "grammar" / "migration" / "v1" / f"{preset}.json"
            destination = root / f"{preset}.out.json"
            run(cli, ["--input", str(source), "--cases", str(cases),
                      "--conversion-profile", "constrained-intent-v1",
                      "--out", str(destination)])
            decoded = json.loads(destination.read_text(encoding="utf-8"))
            if decoded["format_version"] != "grammar-definition-v2":
                raise AssertionError(f"{preset} did not produce grammar-definition-v2")
            outputs[preset] = destination.read_text(encoding="utf-8")
            checked = repository / "configs" / "grammar" / f"{preset}.json"
            if outputs[preset] != checked.read_text(encoding="utf-8"):
                raise AssertionError(f"{preset} migration differs from its checked v2 definition")
            repeated = root / f"{preset}.repeat.json"
            run(cli, ["--input", str(source), "--cases", str(cases),
                      "--conversion-profile", "constrained-intent-v1",
                      "--out", str(repeated)])
            if repeated.read_text(encoding="utf-8") != outputs[preset]:
                raise AssertionError(f"{preset} migration is not deterministic")

        renamed = root / "unrelated-name.json"
        renamed.write_text(
            (repository / "configs/grammar/migration/v1/scalar.json").read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        renamed_out = root / "renamed.out.json"
        run(cli, ["--input", str(renamed), "--cases", str(cases),
                  "--conversion-profile", "constrained-intent-v1",
                  "--out", str(renamed_out)])
        if renamed_out.read_text(encoding="utf-8") != outputs["scalar"]:
            raise AssertionError("grammar-config migration depends on its filename")

        original_input = renamed.read_text(encoding="utf-8")
        rejected = run(cli, ["--input", str(renamed), "--cases", str(cases),
                             "--conversion-profile", "constrained-intent-v1",
                             "--out", str(renamed)], expected=2)
        if "migration input and output must differ" not in rejected.stderr:
            raise AssertionError(f"same-path diagnostic changed: {rejected.stderr}")
        if renamed.read_text(encoding="utf-8") != original_input:
            raise AssertionError("same-path rejection changed the migration input")

        aliased_out = root / "aliased-output.json"
        aliased_out.hardlink_to(renamed)
        rejected = run(cli, ["--input", str(renamed), "--cases", str(cases),
                             "--conversion-profile", "constrained-intent-v1",
                             "--out", str(aliased_out)], expected=2)
        if "migration input and output must differ" not in rejected.stderr:
            raise AssertionError(f"aliased-path diagnostic changed: {rejected.stderr}")
        if (renamed.read_text(encoding="utf-8") != original_input or
                aliased_out.read_text(encoding="utf-8") != original_input):
            raise AssertionError("aliased-path rejection changed the migration input")

        missing_profile = run(
            cli,
            ["--input", str(renamed), "--cases", str(cases),
             "--out", str(root / "missing-profile.out")],
            expected=2,
        )
        if "requires --conversion-profile constrained-intent-v1" not in missing_profile.stderr:
            raise AssertionError(
                f"grammar-config conversion-profile diagnostic changed: {missing_profile.stderr}"
            )

        unknown_config_value = json.loads(renamed.read_text(encoding="utf-8"))
        unknown_config_value["limits"]["future_limit"] = 1
        unknown_config = root / "unknown-config.json"
        unknown_config_out = root / "unknown-config.out"
        write(unknown_config, unknown_config_value)
        unknown_config_out.write_text("existing migration output\n", encoding="utf-8")
        rejected = run(cli, ["--input", str(unknown_config), "--cases", str(cases),
                             "--conversion-profile", "constrained-intent-v1",
                             "--out", str(unknown_config_out)], expected=2)
        if "legacy grammar-config limits has unknown field: future_limit" not in rejected.stderr:
            raise AssertionError(f"legacy config unknown-field diagnostic changed: {rejected.stderr}")
        if unknown_config_out.read_text(encoding="utf-8") != "existing migration output\n":
            raise AssertionError("migration failure replaced an existing output")

        legacy_ast = {
            "format_version": "ast-prefix",
            "version": "ast-prefix",
            "nodes": [
                {"kind": 0, "i0": 0, "i1": 0},
                {"kind": 2, "i0": 0, "i1": 0},
                {"kind": 6, "i0": 0, "i1": 0},
                {"kind": 55, "i0": 0, "i1": 0},
                {"kind": 7, "i0": 0, "i1": 0},
                {"kind": 7, "i0": 1, "i1": 0},
                {"kind": 7, "i0": 2, "i1": 0},
                {"kind": 52, "i0": 1, "i1": 0},
                {"kind": 7, "i0": 2, "i1": 0},
                {"kind": 1, "i0": 0, "i1": 0},
            ],
            "names": ["element", "accumulator", "index"],
            "consts": [
                {"type": "int_list", "value": []},
                {"type": "int", "value": 0},
                {"type": "int", "value": 7},
                {"type": "string", "value": "payload"},
                {"type": "string_list", "value": ["a", "b"]},
                {"type": "float", "value": 1.25},
                {"type": "bool", "value": True},
                {"type": "char", "value": "z"},
                {"type": "float_list", "value": [1.5, -2.25]},
                {"type": "int", "value": "-9223372036854775808"},
                {"type": "int", "value": "9223372036854775807"},
                {"type": "int_list", "value": [
                    "-9223372036854775808", "9223372036854775807",
                    9007199254740991,
                ]},
            ],
            "linear_rec_binders": [{
                "node_index": 3, "elem_name": 0, "accum_name": 1, "index_name": 2,
            }],
        }
        ast_in = root / "legacy-ast.json"
        ast_out = root / "materialized.json"
        write(ast_in, legacy_ast)
        run(cli, ["--input", str(ast_in), "--cases", str(cases), "--out", str(ast_out),
                  "--fuel", "1000", "--max-nodes", "128", "--max-depth", "32"])
        migrated = json.loads(ast_out.read_text(encoding="utf-8"))
        if migrated["format_version"] != "grammar-materialized-v2":
            raise AssertionError("AST migration did not produce the materialized envelope")
        if migrated["semantic_version"] != "gagp-native-2.0.0-restricted-1":
            raise AssertionError("AST migration did not record its target runtime semantics")
        if migrated["source_identity"]["format_version"] != "ast-prefix-v1":
            raise AssertionError("AST migration did not retain the explicit release-1 identity")
        if migrated["ast"]["version"] != "ast-prefix-v2":
            raise AssertionError("AST migration did not produce ast-prefix-v2")
        if migrated["ast"]["consts"] != []:
            raise AssertionError("AST migration did not detach its constant pool")
        if migrated["constants"][3] != {"type": "String", "values": ["payload"]}:
            raise AssertionError("string payload was not preserved")
        if migrated["constants"][4] != {"type": "StringList", "values": [["a", "b"]]}:
            raise AssertionError("list payload was not preserved")
        expected_extra_values = [
            {"type": "Float", "values": [1.25]},
            {"type": "Bool", "values": [True]},
            {"type": "Char", "values": ["z"]},
            {"type": "FloatList", "values": [[1.5, -2.25]]},
        ]
        if migrated["constants"][5:9] != expected_extra_values:
            raise AssertionError(
                f"exact typed scalar/list payloads were not preserved: {migrated['constants'][5:9]}"
            )
        expected_exact_integers = [
            {"type": "Int", "values": ["-9223372036854775808"]},
            {"type": "Int", "values": ["9223372036854775807"]},
            {"type": "IntList", "values": [[
                "-9223372036854775808", "9223372036854775807",
                "9007199254740991",
            ]]},
        ]
        if migrated["constants"][9:12] != expected_exact_integers:
            raise AssertionError(
                f"exact signed-64 constants were not preserved: {migrated['constants'][9:12]}"
            )
        if migrated["search_limits"] != {"max_depth": 32, "max_nodes": 128}:
            raise AssertionError("explicit AST migration search limits were not preserved")
        if migrated["execution_limits"] != {"fuel": 1000}:
            raise AssertionError("explicit AST migration fuel was not preserved")
        if not any(node["kind"] == 73 for node in migrated["ast"]["nodes"]):
            raise AssertionError("legacy LinearRec did not lower to a general traversal")
        isolated_evaluator = root / "gagp_evolve_cli"
        shutil.copy2(evaluator, isolated_evaluator)
        evaluated = run(str(isolated_evaluator),
                        ["--cases", str(cases), "--eval-ast-json", str(ast_out)],
                        cwd=root)
        if "AST_EVAL fitness=0.000000" not in evaluated.stdout:
            raise AssertionError("migrated materialized AST did not execute without package files")

        missing_constants = json.loads(ast_out.read_text(encoding="utf-8"))
        del missing_constants["constants"]
        missing_constants_path = root / "missing-constants.json"
        write(missing_constants_path, missing_constants)
        rejected = run(str(isolated_evaluator),
                       ["--cases", str(cases), "--eval-ast-json",
                        str(missing_constants_path)], expected=2, cwd=root)
        if "missing field: constants" not in rejected.stderr:
            raise AssertionError(f"missing detached-pool diagnostic changed: {rejected.stderr}")

        double_constants = json.loads(ast_out.read_text(encoding="utf-8"))
        double_constants["ast"]["consts"] = [{"type": "int", "value": 0}]
        double_constants_path = root / "double-constants.json"
        write(double_constants_path, double_constants)
        rejected = run(str(isolated_evaluator),
                       ["--cases", str(cases), "--eval-ast-json",
                        str(double_constants_path)], expected=2, cwd=root)
        if "must not contain two constant pools" not in rejected.stderr:
            raise AssertionError(f"double-pool diagnostic changed: {rejected.stderr}")

        wrong_semantics = json.loads(ast_out.read_text(encoding="utf-8"))
        wrong_semantics["semantic_version"] = "gagp-native-1.0.0"
        wrong_semantics_path = root / "wrong-semantics.json"
        write(wrong_semantics_path, wrong_semantics)
        rejected = run(str(isolated_evaluator),
                       ["--cases", str(cases), "--eval-ast-json",
                        str(wrong_semantics_path)], expected=2, cwd=root)
        if "semantic version mismatch" not in rejected.stderr:
            raise AssertionError(f"semantic-version diagnostic changed: {rejected.stderr}")

        for label, invalid_type, invalid_value in (
            ("out-of-range-int", "int", "9223372036854775808"),
            ("imprecise-int-number", "int", 9007199254740992),
            ("out-of-range-list", "int_list", ["-9223372036854775809"]),
            ("imprecise-list-number", "int_list", [-9007199254740992]),
        ):
            invalid_ast = json.loads(json.dumps(legacy_ast))
            invalid_ast["consts"].append({"type": invalid_type, "value": invalid_value})
            invalid_path = root / f"{label}.json"
            write(invalid_path, invalid_ast)
            rejected = run(cli, ["--input", str(invalid_path), "--cases", str(cases),
                                 "--out", str(root / f"{label}.out"), "--fuel", "1000",
                                 "--max-nodes", "128", "--max-depth", "32"], expected=2)
            if "signed 64-bit decimal string or a safe integral JSON number" not in rejected.stderr:
                raise AssertionError(f"{label} legacy integer diagnostic changed: {rejected.stderr}")

        malformed = root / "malformed.json"
        malformed_out = root / "bad.json"
        malformed.write_text('{"format_version":', encoding="utf-8")
        malformed_out.write_text("existing parse output\n", encoding="utf-8")
        rejected = run(cli, ["--input", str(malformed), "--cases", str(cases),
                             "--out", str(malformed_out)], expected=2)
        if "malformed migration input" not in rejected.stderr:
            raise AssertionError("malformed input diagnostic changed")
        if malformed_out.read_text(encoding="utf-8") != "existing parse output\n":
            raise AssertionError("parse failure replaced an existing output")

        output_directory = root / "output-directory"
        output_directory.mkdir()
        rejected = run(cli, ["--input", str(renamed), "--cases", str(cases),
                             "--conversion-profile", "constrained-intent-v1",
                             "--out", str(output_directory)], expected=2)
        if "failed writing migration output" not in rejected.stderr:
            raise AssertionError(f"atomic-write diagnostic changed: {rejected.stderr}")
        if not output_directory.is_dir():
            raise AssertionError("failed atomic write replaced the existing output")
        if list(root.glob(".gagp_migrate_artifact.tmp.*")):
            raise AssertionError("failed atomic write left a temporary output behind")

        bytecode = root / "bytecode.json"
        write(bytecode, {"format_version": "migration-bytecode-v1"})
        rejected = run(cli, ["--input", str(bytecode), "--cases", str(cases),
                             "--out", str(root / "bytecode.out")], expected=2)
        if "AST/type provenance is absent" not in rejected.stderr:
            raise AssertionError("bytecode rejection diagnostic changed")

        seeds = root / "seeds.json"
        write(seeds, {"format_version": "population-seeds", "seeds": [{"seed": 1}]})
        rejected = run(cli, ["--input", str(seeds), "--cases", str(cases),
                             "--out", str(root / "seeds.out")], expected=2)
        if "materialize every seed with the frozen old build" not in rejected.stderr:
            raise AssertionError("seed-only rejection diagnostic changed")

        population = root / "population.json"
        write(population, {"format_version": "grammar-population-v1"})
        rejected = run(cli, ["--input", str(population), "--out",
                             str(root / "population.out")], expected=2)
        if "migrate every materialized member independently" not in rejected.stderr:
            raise AssertionError(f"population rejection diagnostic changed: {rejected.stderr}")

        # A release-1 generated artifact is already materialized. Its embedded
        # schema and limits are authoritative, so migration needs no case file
        # and must not replay the old generator.
        generated_grammar = {
            "format_version": "grammar-definition-v1",
            "entry": {"nonterminal": "Main", "type": "IntList"},
            "inputs": [],
            "search_limits": {"max_nodes": 128, "max_depth": 32},
            "execution_limits": {"fuel": 777},
            "nonterminals": [],
        }
        generated_shape = {
            "version": "ast-prefix",
            "nodes": [
                {"kind": 0, "i0": 0, "i1": 0},
                {"kind": 2, "i0": 0, "i1": 0},
                {"kind": 6, "i0": 0, "i1": 0},
                {"kind": 53, "i0": 0, "i1": 1},  # MapList[IntList]
                {"kind": 7, "i0": 0, "i1": 0},
                {"kind": 52, "i0": 0, "i1": 0},
                {"kind": 1, "i0": 0, "i1": 0},
            ],
            "names": ["element"],
            "consts": [],
        }
        generated_constants = [
            {"type": "IntList", "values": [["-9", "12"]]},
            {"type": "String", "values": ["a\u0000雪"]},
            {"type": "StringList", "values": [["", "a\u0000b", "雪"]]},
            {"type": "FloatList", "values": [[0.5, -2.25]]},
            {"type": "Int", "values": ["9223372036854775807"]},
            {"type": "Char", "values": ["😀"]},
        ]
        generated = {
            "format_version": "grammar-generated-v1",
            "semantic_version": "gagp-native-1.0.0",
            "generator_version": "typed-derivation-v1",
            "rng_version": "splitmix64-rejection-v1",
            "grammar_hash": sha256(generated_grammar),
            "grammar": generated_grammar,
            "inputs": [],
            "input_schema_hash": sha256([]),
            "return_type": "IntList",
            "request": {
                "nonterminal": 0,
                "type": "IntList",
                "visible_environment": [],
                "scope_mapping": [],
            },
            "seed": "18446744073709551615",
            "payload_seeding": "domain-only-v1",
            "search_limits": {"max_nodes": 128, "max_depth": 32},
            "execution_limits": {"fuel": 777},
            "ast_shape": generated_shape,
            "constants": generated_constants,
            "derivation": {
                "logical_steps": 1,
                "derived_nodes": 7,
                "lowered_instructions": 1,
                "nodes": [],
                "choices": [[0, 0, 4294967295, 0, 7]],
                "templates": [],
                "holes": [],
            },
        }
        generated_in = root / "generated-v1.json"
        generated_out = root / "generated-materialized.json"
        write(generated_in, generated)
        run(cli, ["--input", str(generated_in), "--out", str(generated_out)])
        generated_migrated = json.loads(generated_out.read_text(encoding="utf-8"))
        if generated_migrated["semantic_version"] != "gagp-native-2.0.0-restricted-1":
            raise AssertionError("generated migration did not record target runtime semantics")
        if generated_migrated["source_identity"]["format_version"] != "grammar-generated-v1":
            raise AssertionError("generated migration lost its source format identity")
        if generated_migrated["source_identity"]["content_sha256"] != sha256(generated):
            raise AssertionError("generated migration source content identity changed")
        if generated_migrated["inputs"] != [] or generated_migrated["return_type"] != "IntList":
            raise AssertionError("generated migration did not preserve its embedded type contract")
        if generated_migrated["search_limits"] != {"max_depth": 32, "max_nodes": 128}:
            raise AssertionError("generated migration did not preserve embedded search limits")
        if generated_migrated["execution_limits"] != {"fuel": 777}:
            raise AssertionError("generated migration did not preserve embedded execution fuel")
        if any(node["kind"] == 53 for node in generated_migrated["ast"]["nodes"]):
            raise AssertionError("generated-v1 MapList survived migration")
        if not any(node["kind"] == 72 for node in generated_migrated["ast"]["nodes"]):
            raise AssertionError("generated-v1 MapList did not lower to general traversal")
        if generated_migrated["ast"]["consts"] != []:
            raise AssertionError("generated migration duplicated its detached exact constant pool")
        if generated_migrated["constants"][:len(generated_constants)] != generated_constants:
            raise AssertionError(
                f"generated migration changed exact constants or typed payloads: "
                f"{generated_migrated['constants']!r}"
            )
        generated_cases = root / "generated-cases.json"
        write(generated_cases, {
            "format_version": "fitness-cases",
            "cases": [{
                "inputs": {},
                "expected": {"type": "int_list", "value": [-9, 12]},
            }],
        })
        evaluated = run(str(isolated_evaluator), ["--cases", str(generated_cases),
                                                  "--eval-ast-json", str(generated_out)],
                        cwd=root)
        if "AST_EVAL fitness=1.000000" not in evaluated.stdout:
            raise AssertionError("migrated generated-v1 artifact did not execute exactly")

        # FilterList has a distinct lowering path: it retains each source
        # element for which its predicate evaluates true.
        filtered = json.loads(json.dumps(generated))
        filtered["ast_shape"] = {
            "version": "ast-prefix",
            "nodes": [
                {"kind": 0, "i0": 0, "i1": 0},
                {"kind": 2, "i0": 0, "i1": 0},
                {"kind": 6, "i0": 0, "i1": 0},
                {"kind": 54, "i0": 0, "i1": 0},  # FilterList
                {"kind": 7, "i0": 0, "i1": 0},
                {"kind": 7, "i0": 1, "i1": 0},
                {"kind": 1, "i0": 0, "i1": 0},
            ],
            "names": ["element"],
            "consts": [],
        }
        filtered["constants"] = [
            {"type": "IntList", "values": [["-9", "12"]]},
            {"type": "Bool", "values": [True]},
        ]
        filtered_in = root / "filtered-v1.json"
        filtered_out = root / "filtered-materialized.json"
        write(filtered_in, filtered)
        run(cli, ["--input", str(filtered_in), "--out", str(filtered_out)])
        filtered_migrated = json.loads(filtered_out.read_text(encoding="utf-8"))
        if any(node["kind"] == 54 for node in filtered_migrated["ast"]["nodes"]):
            raise AssertionError("generated-v1 FilterList survived migration")
        if not any(node["kind"] == 72 for node in filtered_migrated["ast"]["nodes"]):
            raise AssertionError("generated-v1 FilterList did not lower to general traversal")
        evaluated = run(str(isolated_evaluator), ["--cases", str(generated_cases),
                                                  "--eval-ast-json", str(filtered_out)],
                        cwd=root)
        if "AST_EVAL fitness=1.000000" not in evaluated.stdout:
            raise AssertionError("migrated generated-v1 FilterList did not execute exactly")

        unknown_generated = dict(generated)
        unknown_generated["mystery"] = True
        unknown_in = root / "generated-unknown.json"
        write(unknown_in, unknown_generated)
        rejected = run(cli, ["--input", str(unknown_in),
                             "--out", str(root / "generated-unknown.out")], expected=2)
        if "unknown field: mystery" not in rejected.stderr:
            raise AssertionError(f"generated unknown-field diagnostic changed: {rejected.stderr}")

        malformed_generated = json.loads(json.dumps(generated))
        malformed_generated["execution_limits"]["fuel"] = 0
        malformed_in = root / "generated-malformed.json"
        write(malformed_in, malformed_generated)
        rejected = run(cli, ["--input", str(malformed_in),
                             "--out", str(root / "generated-malformed.out")], expected=2)
        if "execution_limits.fuel must be a positive integer" not in rejected.stderr:
            raise AssertionError(f"generated malformed-limit diagnostic changed: {rejected.stderr}")

        ambiguous_ast = dict(legacy_ast)
        ambiguous_ast["consts"] = [*legacy_ast["consts"], {"type": "num_list", "value": []}]
        ambiguous = root / "ambiguous-ast.json"
        write(ambiguous, ambiguous_ast)
        rejected = run(cli, ["--input", str(ambiguous), "--cases", str(cases),
                             "--out", str(root / "ambiguous.out"), "--fuel", "1000",
                             "--max-nodes", "128", "--max-depth", "32"], expected=2)
        if "ambiguous" not in rejected.stderr or "int_list, float_list or string_list" not in rejected.stderr:
            raise AssertionError(f"ambiguous legacy list diagnostic changed: {rejected.stderr}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
