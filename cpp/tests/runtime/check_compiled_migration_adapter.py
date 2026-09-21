"""Check compiled population/request replay through the private timing adapter."""
import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        grammar = root / "grammar.json"
        grammar.write_text(json.dumps({
            "format_version": "grammar-definition-v1",
            "entry": {"nonterminal": "Main", "type": "Int"},
            "search_limits": {"max_nodes": 6, "max_depth": 4},
            "execution_limits": {"fuel": 100},
            "nonterminals": [{"id": "Main", "type": "Int", "scope": [],
                "alternatives": [{"id": "leaf", "weight": 1,
                    "expression": {"constant": {"type": "Int", "values": ["7"]}}}]}]}))
        cases = root / "cases.json"
        cases.write_text(json.dumps({"format_version": "fitness-cases", "cases": [
            {"inputs": {}, "expected": {"type": "int", "value": 7}}]}))
        snapshot = root / "population.json"

        def invoke(command, error=None):
            result = subprocess.run(["rtk", "proxy", *command], capture_output=True, text=True)
            if error is None:
                assert result.returncode == 0, result.stderr
            else:
                assert result.returncode != 0 and error in result.stderr, result.stderr
            return result

        invoke([args.generator, "--grammar-definition", str(grammar), "--cases", str(cases),
                "--population-size", "3", "--seed", "42", "--out-json", str(snapshot)])
        original = json.loads(snapshot.read_text())
        common = [args.adapter, "--snapshot", str(snapshot), "--grammar-definition", str(grammar),
                  "--cases", str(cases), "--population-size", "3", "--generations", "2",
                  "--engine", "cpu", "--repro-backend", "cpu", "--repro-overlap", "off",
                  "--fuel", "100", "--mutation-rate", "1"]
        output = root / "result.json"
        for action in ("run", "oracle", "steady"):
            invoke([*common, "--action", action, "--out-json", str(output)])
            result = json.loads(output.read_text())
            if action == "oracle":
                assert result["format_version"] == "grammar-oracle-v1"
                assert len(result["programs"]) == 3
                for program in result["programs"]:
                    assert "ast" in program and "bytecode" not in program
                    assert program["cpu_fitness"] == 0
            if action == "run":
                assert len(result["generations"]) == 2
                for generation in result["generations"]:
                    counts = generation["reproduction"]
                    assert counts["crossover_attempts"] == 2
                    assert counts["mutation_attempts"] == 3
                    for key in ("contract_rejections", "budget_rejections", "generation_rejections",
                                "acceptance_rejections", "fallback_children", "unchanged_children",
                                "changed_children"):
                        assert isinstance(counts[key], int) and counts[key] >= 0
        # A single-production constant artifact replays identically under a smaller
        # explicit node budget. Verify it through the public replay API first.
        requested = copy.deepcopy(original)
        for member in requested["members"]:
            member["search_limits"]["max_nodes"] = 5
        snapshot.write_text(json.dumps(requested))
        invoke([args.generator, "--replay-json", str(snapshot), "--grammar-definition", str(grammar),
                "--cases", str(cases), "--out-json", str(root / "replayed.json")])
        invoke([*common, "--action", "run", "--out-json", str(output)])
        requested["members"][0] = original["members"][0]
        snapshot.write_text(json.dumps(requested))
        invoke([*common, "--action", "run", "--out-json", str(output)], "one exact generation request")
        snapshot.write_text(json.dumps(original))
        missing = common.copy()
        index = missing.index("--grammar-definition")
        del missing[index:index + 2]
        invoke([*missing, "--action", "run", "--out-json", str(output)], "requires --grammar-definition")
        wrong_fuel = common.copy()
        wrong_fuel[wrong_fuel.index("--fuel") + 1] = "101"
        invoke([*wrong_fuel, "--action", "run", "--out-json", str(output)], "--fuel must match")
        for action in ("freeze", "freeze-repro", "repro-check", "repro-steady"):
            invoke([*common, "--action", action, "--out-json", str(output)], "supports only run, oracle and steady")
        bounded_grammar = Path(__file__).resolve().parents[3] / "configs/grammar_definitions/bounded_memo.json"
        bounded_cases = root / "bounded-cases.json"
        bounded_cases.write_text(json.dumps({"format_version": "fitness-cases", "cases": [{
            "inputs": {name: {"type": "int", "value": value} for name, value in {
                "row": 2, "column": 2, "rows": 3, "columns": 4, "base_value": 1}.items()},
            "expected": {"type": "int", "value": 8}}]}))
        invoke([args.generator, "--grammar-definition", str(bounded_grammar), "--cases", str(bounded_cases),
                "--population-size", "3", "--seed", "42", "--out-json", str(snapshot)])
        bounded = common.copy()
        bounded[bounded.index("--grammar-definition") + 1] = str(bounded_grammar)
        bounded[bounded.index("--cases") + 1] = str(bounded_cases)
        bounded[bounded.index("--fuel") + 1] = "10000"
        invoke([*bounded, "--action", "oracle", "--out-json", str(output)])
        oracle = json.loads(output.read_text())
        assert oracle["format_version"] == "grammar-oracle-v1"
        for program in oracle["programs"]:
            assert len(program["ast"]["bounded_region_specs"]) == 1
            assert program["cpu_fitness"] == 0 and program["fitness"] == 0
    print("compiled migration adapter checks passed")


if __name__ == "__main__":
    main()
