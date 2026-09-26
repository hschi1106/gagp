#!/usr/bin/env python3
"""Check diagnostic completeness and unchanged outcomes against the timed binary."""
import json
import pathlib
import subprocess
import sys
import tempfile


def run(command: list[str]) -> None:
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode:
        raise AssertionError(f"{command}\n{completed.stdout}\n{completed.stderr}")


def without_timings(value: object) -> object:
    if isinstance(value, dict):
        return {key: without_timings(item) for key, item in value.items()
                if not key.endswith("_ms") and key not in {
                    "diagnostic_only", "population_observations"}}
    if isinstance(value, list):
        return [without_timings(item) for item in value]
    return value


def main() -> int:
    timed, diagnostic, generator, root, cuda = sys.argv[1:]
    grammar = pathlib.Path(root) / "configs/grammar/examples/authoring/scalar.json"
    modes = [("cpu", "cpu", "off", "none"),
             ("cpu", "cpu", "off", "gpu_selection"),
             ("cpu", "cpu", "off", "gpu_candidates"),
             ("cpu", "cpu", "off", "gpu_coupled_donor")]
    if cuda == "1":
        modes += [("gpu", "cpu", "off", "none"),
                  ("gpu", "gpu", "off", "none"),
                  ("gpu", "gpu", "on", "none"),
                  ("cpu", "gpu", "off", "none"),
                  ("gpu", "cpu", "off", "gpu_selection"),
                  ("gpu", "cpu", "off", "gpu_candidates"),
                  ("gpu", "cpu", "off", "gpu_coupled_donor")]
    with tempfile.TemporaryDirectory() as directory:
        work = pathlib.Path(directory)
        cases = work / "cases.json"
        cases.write_text(json.dumps({"format_version": "fitness-cases", "cases": [
            {"inputs": {"n": {"type": "int", "value": n}},
             "expected": {"type": "int", "value": n}} for n in [-2, 0, 7]]}))
        population = work / "population.json"
        run([generator, "--grammar-definition", str(grammar), "--cases", str(cases),
             "--out-json", str(population), "--population-size", "8", "--seed", "41"])
        for engine, backend, overlap, ablation in modes:
            for skip_final in ["on", "off"]:
                common = ["--action", "run", "--snapshot", str(population),
                          "--cases", str(cases), "--grammar-definition", str(grammar),
                          "--population-size", "8", "--generations", "3", "--seed", "41",
                          "--fuel", "32", "--max-total-nodes", "9", "--max-expr-depth", "5",
                          "--engine", engine, "--repro-backend", backend,
                          "--repro-overlap", overlap, "--cpu-repro-ablation", ablation,
                          "--blocksize", "256", "--skip-final-eval", skip_final,
                          "--retain-final-population", "off"]
                plain_file, stats_file = work / "plain.json", work / "stats.json"
                run([timed, *common, "--out-json", str(plain_file)])
                run([diagnostic, *common, "--out-json", str(stats_file)])
                plain = json.loads(plain_file.read_text())
                stats = json.loads(stats_file.read_text())
                assert without_timings(stats) == without_timings(plain), (engine, backend, overlap)
                assert stats["diagnostic_only"] is True
                assert "population_observations" not in plain
                rows = stats["population_observations"]
                phases = ["evaluation", "reproduction"] * 3
                if skip_final == "off":
                    phases.append("evaluation")
                observed_phases = [row["phase"] for row in rows]
                assert observed_phases == phases, (
                    engine, backend, overlap, ablation, skip_final, observed_phases)
                for row in rows:
                    assert row["population_size"] == 8
                    assert 1 <= row["unique_programs"] <= 8
                    assert row["post_backend_verifier_counts"] == {"ok": 8}
                    for histogram in ["node_counts", "depths"]:
                        assert sum(count for _, count in row[histogram]) == 8
                        assert all(size > 0 and count > 0 for size, count in row[histogram])
                    if row["phase"] == "evaluation":
                        assert row["fitness_evaluations_requested"] == 8
                        assert row["case_score_requests"] == 24
                assert sum(row.get("fitness_evaluations_requested", 0) for row in rows) == (
                    24 if skip_final == "on" else 32)
        rejected = subprocess.run([diagnostic, "--action", "steady", "--snapshot", str(population),
                                   "--cases", str(cases), "--out-json", str(work / "invalid.json")],
                                  capture_output=True, text=True)
        assert rejected.returncode == 2
        assert "require --action run" in rejected.stderr
    print(f"statistics: {len(modes) * 2} diagnostic/timed comparisons passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
