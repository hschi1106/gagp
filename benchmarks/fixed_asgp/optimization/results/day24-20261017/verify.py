"""Post-run provenance and fixture integrity checks; never starts a benchmark."""
from argparse import ArgumentParser
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

from gagp_tools.experiments.fixed_asgp.low_budget import read, save, sha
from quality_data import identity


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    assert not args.out.exists(), "use a new integrity record"
    m = read(args.matrix / "manifest.json")
    summary = read(args.report / "summary.json")
    assert m["stage"] == "complete" and m["requested_phase_complete"]
    assert not Path(f"/proc/{m['controller_pid']}").exists()
    assert all(j["status"] == "ok" for j in m["jobs"].values())
    assert m["settings"]["GAGP_GPU_PAYLOAD_WORKSPACE_MIB"] == "16384"
    assert all(m["settings"][key] == "1" for key in
               ("GAGP_HOST_THREADS", "OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                "MKL_NUM_THREADS", "GAGP_BM_ALIGNED_FITNESS"))
    assert "GAGP_BM_COMMON_FITNESS" not in m["settings"]
    verified = {}

    def check(path, expected):
        path = Path(path)
        key = str(path.resolve())
        if key not in verified:
            verified[key] = sha(path)
        assert verified[key] == expected, path

    check(args.matrix / "bench", m["binary_sha256"])
    for path, expected in m["runtime_source_sha256"].items():
        check(path, expected)
    check(m["preparation_manifest"], m["preparation_manifest_sha256"])
    external = read(args.matrix / "asgp-source-hashes.json")
    for path, expected in external["files"].items():
        check(Path(external["root"]) / path, expected)
    task_checks = {}
    for task, e in m["inputs"].items():
        for field in ("source", "grammar", "quality_pool", "test_source"):
            check(e[field], e[field + "_sha256"])
        grammar = read(Path(e["grammar"]))
        assert grammar["search_limits"] == dict(max_nodes=2048, max_depth=24)
        source = read(Path(e["source"]))
        assert len(source["cases"]) == 1024 and len(source["population"]) == 16384
        for prefix in e["prefixes"].values():
            check(prefix["prepared"], prefix["sha256"])
        train = None
        population_hashes = set()
        for seed in range(5):
            inp = e["quality"][str(seed)]
            for field in ("source", "prepared"):
                check(inp[field], inp[field + "_sha256"])
            data = read(Path(inp["source"]))
            assert len(data["population"]) == 1024
            assert data.get("population_seed", 0) == seed
            assert data["quality_data_seed"] == 20261017
            if train is None:
                train = data["cases"]
                assert data["population"] == source["population"][:1024]
            assert train == data["cases"], (task, seed)
            population_hashes.add(hashlib.sha256(json.dumps(
                data["population"], sort_keys=True, separators=(",", ":")).encode()).hexdigest())
        assert len(population_hashes) == 5, task
        test = read(Path(e["test_source"]))["cases"]
        train_ids, test_ids = {identity(r) for r in train}, {identity(r) for r in test}
        assert len(train_ids) == len(train) == e["split"]["train_count"]
        assert len(test_ids) == len(test) == e["split"]["test_count"]
        assert not train_ids & test_ids
        task_checks[task] = dict(train=len(train), test=len(test), input_overlap=0,
                                 distinct_initial_population_seeds=5)
    formal_counts = {}
    for suite in ("speed", "quality"):
        g = m["selection"][suite + "_generations"]
        rows = [r for r in m[suite].values() if r["generations"] == g]
        assert len(rows) == 300
        for r in rows:
            assert r["status"] == "ok"
            e = m["inputs"][r["task"]]
            inp = e["quality"][str(r["seed"])] if suite == "quality" else dict(
                source=e["source"], prepared=e["prefixes"][str(r["population"])]["prepared"])
            assert r["source"] == inp["source"] and r["prepared"] == inp["prepared"]
            job = m["jobs"][r["job"]]
            assert Path(job["command"][0]).resolve() == (args.matrix / "bench").resolve()
            assert job["command"][1:] == ["measure", r["source"], r["prepared"], e["grammar"],
                str(r["population"]), "search_" + r["mode"], r["output"]]
            extra = job["extra_settings"]
            assert extra["GAGP_BM_SEED"] == str(r["seed"])
            assert extra["GAGP_SEARCH_GENERATIONS"] == str(g)
            assert extra["GAGP_BM_CASE_COUNT"] == str(r["cases"])
        formal_counts[suite] = len(rows)
    audit_count = sum(k.endswith(".judge") for k in m["jobs"])
    assert audit_count == 600 and summary["quality_audited"] == 300
    assert summary["speed_qualified"] == 150
    intervals = sorted((j["started_epoch"], j["started_epoch"] + j["process_s"])
                       for j in m["jobs"].values())
    assert all(end <= m["deadline_epoch"] for _, end in intervals)
    assert all(a[1] <= b[0] for a, b in zip(intervals, intervals[1:]))
    now = time.time()
    assert now < m["deadline_epoch"], "report verification exceeded original budget"
    save(args.out, dict(status="pass", verified_utc=datetime.now(timezone.utc).isoformat(),
        elapsed_including_report_verification_h=(now - m["started_epoch"]) / 3600,
        controller_elapsed_h=m["elapsed_s"] / 3600, deadline_epoch=m["deadline_epoch"],
        manifest_sha256=sha(args.matrix / "manifest.json"),
        summary_sha256=sha(args.report / "summary.json"), verifier_sha256=sha(Path(__file__)),
        formal_runs=formal_counts, cpu_judge_jobs=audit_count, serial_process_intervals=len(intervals),
        verified_file_hashes=verified, external_asgp_files=len(external["files"]), tasks=task_checks))
    print(json.dumps(dict(status="pass", files=len(verified), tasks=len(task_checks),
                         formal_runs=formal_counts, cpu_judge_jobs=audit_count)))


if __name__ == "__main__":
    main()
