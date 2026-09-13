"""Compare population observers with the uninstrumented evolution loop."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plain", required=True)
    parser.add_argument("--capture", required=True)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        cases = root / "cases.json"
        cases.write_text(json.dumps({"format_version": "fitness-cases", "cases": [
            {"inputs": {}, "expected": {"type": "int", "value": 0}}]}))
        source = root / "ast.json"
        source.write_text(json.dumps({"version": "ast-prefix", "names": [],
            "nodes": [{"kind": k, "i0": 0, "i1": 0} for k in (0, 2, 6, 7, 1)],
            "consts": [{"type": "int", "value": 0}]}))
        snapshot = root / "population.json"
        common = ["--cases", str(cases), "--population-size", "16", "--generations", "3",
                  "--seed", "42", "--blocksize", "256", "--skip-final-eval", "on", "--snapshot", str(snapshot)]

        def run(command):
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr

        run([args.adapter, *common, "--action", "freeze", "--source-ast", str(source)])
        modes = [("cpu", "cpu", "off")]
        if args.gpu:
            modes = [("gpu", "cpu", "off"), ("gpu", "gpu", "off"),
                     ("gpu", "gpu", "on"), ("cpu", "gpu", "off")]
        for engine, reproduction, overlap in modes:
            outputs = {}
            for variant, binary in (("plain", args.plain), ("capture", args.capture)):
                output = root / f"{variant}.json"
                run([binary, *common, "--engine", engine, "--repro-backend", reproduction,
                     "--repro-overlap", overlap, "--out-json", str(output)])
                outputs[variant] = json.loads(output.read_text())
            for key in ("best_programs", "fitness_history"):
                assert outputs["plain"][key] == outputs["capture"][key]
            observed = outputs["capture"]["populations"]
            assert [r["phase"] for r in observed] == ["evaluation", "reproduction"] * 3
            for population in observed:
                assert population["population_size"] == 16
                assert sum(count for _, count in population["node_counts"]) == 16
                assert sum(count for _, count in population["depths"]) == 16
                assert sum(population["post_backend_verifier_counts"].values()) == 16
                if population["phase"] == "evaluation":
                    assert population["fitness_evaluations_requested"] == 16
                    assert population["case_score_requests"] == 16


if __name__ == "__main__":
    main()
