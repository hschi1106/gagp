"""Differentially check donor interception/replay and malformed tape rejection."""
import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--plain", required=True)
    parser.add_argument("--capture", required=True)
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
        population = root / "population.json"
        reproduction = root / "reproduction.json"
        common = ["--cases", str(cases), "--population-size", "16", "--seed", "42",
                  "--mutation-rate", "1", "--mutation-subtree-prob", "1"]

        def run(command, error=None):
            result = subprocess.run(command, capture_output=True, text=True)
            if error is None:
                assert result.returncode == 0, result.stderr
            else:
                assert result.returncode != 0 and error in result.stderr, result.stderr

        run([args.adapter, *common, "--action", "freeze", "--source-ast", str(source), "--snapshot", str(population)])
        run([args.adapter, *common, "--action", "freeze-repro", "--snapshot", str(population), "--out-json", str(reproduction)])
        outputs = {}
        for mode, binary in (("plain", args.plain), ("capture", args.capture), ("replay", args.capture)):
            output = root / f"{mode}.json"
            command = [binary, *common, "--snapshot", str(reproduction), "--out-json", str(output)]
            if mode == "replay":
                command.extend(["--donor-tape", str(root / "capture.json")])
            run(command)
            outputs[mode] = json.loads(output.read_text())
        assert outputs["capture"]["calls"]
        assert outputs["replay"]["generated_calls"] == 0
        assert outputs["replay"]["replayed_calls"] == len(outputs["capture"]["calls"])
        for mode in ("capture", "replay"):
            for key in ("children", "verification"):
                assert outputs[mode][key] == outputs["plain"][key]
        steady_path = root / "steady.json"
        steady_command = [args.capture, *common, "--snapshot", str(reproduction),
            "--donor-tape", str(root / "capture.json"), "--steady", "--out-json", str(steady_path)]
        run(steady_command)
        measured = json.loads(steady_path.read_text())
        assert measured["format_version"] == "migration-steady-cpu-reproduction-v1"
        assert measured["children"] == outputs["plain"]["children"]
        assert measured["verification"] == outputs["plain"]["verification"]
        assert measured["generated_calls"] == 0 and measured["validation_calls"] == 1
        assert len(measured["samples"]) == 18
        assert [s["warmup"] for s in measured["samples"]] == [True] * 3 + [False] * 15
        assert all(s["call_ms"] > 0 and s["phases"]["mutation_ms"] > 0 for s in measured["samples"])
        run([*steady_command, "--trials", "14"], "measured trials")
        for damage, error in (("missing", "exhausted"), ("extra", "unused"),
                              ("request", "differs"), ("rng", "RNG state")):
            changed = copy.deepcopy(outputs["capture"])
            if damage == "missing":
                changed["calls"].pop()
            elif damage == "extra":
                changed["calls"].append(changed["calls"][-1])
            elif damage == "request":
                changed["calls"][0]["depth"] += 1
            else:
                changed["calls"][0]["rng_after"] = "invalid"
            tape = root / "damaged.json"
            tape.write_text(json.dumps(changed))
            run([args.capture, *common, "--snapshot", str(reproduction), "--donor-tape", str(tape),
                 "--out-json", str(root / "rejected.json")], error)


if __name__ == "__main__":
    main()
