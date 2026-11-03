"""Judge every finished run's exported champion on the held-out test split and on the
train split with the CPU `quality-source` path (one program per run, all cases).

usage: judge.py WORKERS
Writes runs/judge/<arm>/<task>.s<seed>.p<pop>.<split>.json and judge.json (summary).
Resumable: an existing judge output is reused."""
import subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c

KEEP = ("cases", "exact", "wrong", "runtime_errors", "cases_at_cap", "capped_l1_mean", "grammar_admitted")


def judge(item):
    name, res = item
    arm, rest = name.split("/", 1)
    out = {}
    for split in ("test", "train"):
        src = c.split_source(res["task"], split)
        path = c.ROOT / "runs" / "judge" / arm / f"{rest}.{split}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists():
            env = c.base_env(res["task"])
            env["GAGP_BM_CASE_COUNT"] = str(len(c.read(src)["cases"]))
            tmp = path.with_suffix(".tmp.json")
            r = subprocess.run([str(c.BIN), "quality-source", str(src), res["export"], str(c.grammar(res["task"])), str(tmp)],
                               env=env, capture_output=True, text=True, timeout=1800)
            if r.returncode: return name, {"error": r.stderr[-500:]}
            tmp.replace(path)
        final = c.read(path)["final"]
        out[split] = {k: final.get(k) for k in KEEP}
    return name, out


if __name__ == "__main__":
    workers = int(sys.argv[1])
    runs = {}
    for kind in ("gpu", "cpu", "gpugen", "gpuclean"):
        p = c.ROOT / f"record_{kind}.json"
        if p.exists(): runs.update({k: v for k, v in c.read(p).items() if v.get("code") == 0})
    summary_path = c.ROOT / "judge.json"
    summary = c.read(summary_path) if summary_path.exists() else {}
    todo = [(k, v) for k, v in sorted(runs.items()) if "error" in summary.get(k, {"error": 1})]
    with ThreadPoolExecutor(workers) as ex:
        for i, (name, out) in enumerate(ex.map(judge, todo)):
            summary[name] = out
            if i % 50 == 0 or i == len(todo) - 1: c.save(summary_path, summary)
    c.save(summary_path, summary)
    bad = [k for k, v in summary.items() if "error" in v]
    print(f"judged {len(summary)} runs, {len(bad)} errors", *bad[:10], sep="\n")
