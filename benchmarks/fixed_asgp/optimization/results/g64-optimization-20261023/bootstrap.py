"""Create this study once; never reset its clock or consume new heldout data."""
from datetime import datetime, timezone
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[5]
BASE = Path(__file__).resolve().parent
RAW = ROOT / "logs/g64-optimization-20261023"
START = datetime(2026, 10, 23, 13, 51, 28, tzinfo=timezone.utc).timestamp()


def read(path):
    return json.loads(Path(path).read_text())


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    with Path("/tmp/gagp-support-20261015-gpu.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if (BASE / "protocol.json").exists() or RAW.exists():
            raise RuntimeError("Study exists; preserve clock and existing attempts")
        prior = ROOT / "benchmarks/fixed_asgp/optimization/results/twofold-optimization-20261022"
        preset = read(prior / "final2/winner_preset.json")
        old = read(ROOT / "logs/day24-20261017/manifest.json")
        source_paths = subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "cpp"],
            cwd=ROOT, text=True).splitlines()
        source_hashes = {p: sha(ROOT / p) for p in source_paths}
        assert source_hashes == preset["source_sha256"], "Current CPP differs from selected baseline"
        baseline = ROOT / preset["binary"]
        assert sha(baseline) == preset["binary_sha256"]
        inputs = {}
        for task, entry in old["inputs"].items():
            q = entry["quality"]["0"]
            paths = {"source": (q["source"], q["source_sha256"]),
                     "prepared": (q["prepared"], q["prepared_sha256"]),
                     "grammar": (entry["grammar"], entry["grammar_sha256"])}
            for path, digest in paths.values():
                assert sha(path) == digest, "Development input changed: " + path
            inputs[task] = {"train_count": entry["split"]["train_count"],
                            **{k: str(Path(v[0])) for k, v in paths.items()},
                            "sha256": {str(Path(v[0])): v[1] for v in paths.values()}}
        assert len(inputs) == 30
        RAW.mkdir(parents=True)
        (RAW / "baseline").mkdir()
        shutil.copy2(baseline, RAW / "baseline/bench")
        protocol = dict(
            format="gagp-g64-optimization-v1", started_epoch=START,
            deadline_epoch=START + 86400, development_cutoff_epoch=START + 36000,
            timed_cutoff_epoch=START + 72000, report_reserve_s=14400,
            baseline_binary=str(RAW / "baseline/bench"),
            baseline_binary_sha256=preset["binary_sha256"], baseline_settings=preset["settings"],
            baseline_source_sha256=source_hashes,
            baseline_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            tasks=list(inputs), development_inputs=inputs,
            primary=dict(population=1024, generations=64, cases="fixed per-task <=200 unique training; finite domains may have fewer",
                         repetitions=3, target_geomean=1.3, stretch_targets=[1.5, 2.0],
                         timer="unprofiled evolve_wall_ms; cold process and audit costs separately"),
            quality=dict(tasks=30, target_seeds=list(range(35, 55)), minimum_seeds=list(range(35, 45)),
                         data_seed=20261024, train_target=200, test_target=1000,
                         generation_champion_fitness_wrong_ast="paired equality required for execution-only admission",
                         success="final training-selected champion train AND input-disjoint test wrong==0",
                         noninferiority_margin_pp=5, confidence="one-sided97.5%; independent seed clusters; insufficient evidence remains unproven"),
            controls=dict(asgp="new same-setting ASGP1T controls, never historical denominators",
                          population16384="secondary; separate coverage and no quality extrapolation"),
            candidate_rule="one global binary/settings chosen from old TRAINING parity and unprofiled runtime; freeze scale before new heldout use",
            resources=preset["resources"], gpu_lock="/tmp/gagp-support-20261015-gpu.lock",
            exclusions=["no CPU reproduction", "no smaller workspace", "no test-guided tuning",
                        "no resampling or hidden reruns", "no deleting failed/interrupted/budget-not-run rows",
                        "no subagents or push", "do not modify previous studies or unrelated presentations"])
        (BASE / "protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")
        record = dict(status="initialized", started_epoch=START, deadline_epoch=START + 86400,
                      protocol_sha256=sha(BASE / "protocol.json"), baseline_sha256=sha(RAW / "baseline/bench"),
                      source_files=len(source_hashes), development_tasks=len(inputs), heldout_created=False)
        (RAW / "bootstrap.json").write_text(json.dumps(record, indent=2) + "\n")
        print(json.dumps(record))


if __name__ == "__main__":
    main()
