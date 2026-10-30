"""Replay the frozen 1024-program, 1024-case population; never overwrite runs."""
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / "logs/asgp-tuning"
BINARY = OUT / "replay"
INPUTS = ROOT / "logs/asgp-quick-1024cases/ready-p1024-c1024-s0.inputs.json"
GRAMMAR = ROOT / "benchmarks/asgp-quick-20260929/grammar.json"
SETTINGS = [(128, 256), (128, 128), (128, 64), (64, 256), (64, 128),
            (64, 64), (50, 128), (128, 512), (128, 1024), (64, 512),
            (64, 1024), (50, 256), (50, 512), (50, 1024)]


def identity():
    paths = [BINARY, INPUTS, GRAMMAR, HERE / "replay.cpp", HERE / "build.sh"]
    paths += sorted((ROOT / "cpp/build/release").glob("libgagp*.a"))
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in paths}


def command(frames, block, scope):
    return [str(BINARY), str(INPUTS), str(GRAMMAR), str(frames), str(block), scope]


def read_rows(frames, block, scope):
    path = OUT / f"{scope}-f{frames}-b{block}.csv"
    rows = list(csv.DictReader(path.open()))
    modes = ["eval"] if scope == "eval" else ["off", "on"]
    expected = {(mode, rep) for mode in modes for rep in [-1, 0, 1, 2]}
    actual = [(r["mode"], int(r["rep"])) for r in rows]
    if len(actual) != len(expected) or set(actual) != expected:
        raise RuntimeError(f"Incomplete run: {path}")
    if not path.with_suffix(".log").read_text().startswith("PASS exact GPU fitness vector;"):
        raise RuntimeError(f"Missing parity check: {path}")
    if any(int(r["frames"]) != frames or int(r["block"]) != block
           or float(r["total_ms"]) <= 0 for r in rows):
        raise RuntimeError(f"Invalid row: {path}")
    return rows


def run(frames, block, scope):
    path = OUT / f"{scope}-f{frames}-b{block}.csv"
    if not path.exists():
        with path.open("x") as out, path.with_suffix(".log").open("x") as err:
            subprocess.run(command(frames, block, scope), cwd=ROOT, stdout=out,
                           stderr=err, env={**os.environ, "GAGP_CUDA_DEVICE": "0"},
                           check=True)
    rows = read_rows(frames, block, scope)
    med = statistics.median(float(r["total_ms"]) for r in rows if int(r["rep"]) >= 0)
    print(f"{scope}: frames={frames}, block={block}, median={med:.3f} ms", flush=True)
    return rows


def main():
    OUT.mkdir(exist_ok=True, parents=True)
    manifest_path = HERE / "manifest.json"
    hashes = identity()
    if manifest_path.exists():
        if json.loads(manifest_path.read_text())["sha256"] != hashes:
            raise RuntimeError("Artifact identity changed; use a fresh experiment directory")
    else:
        manifest_path.write_text(json.dumps({
            "population": 1024, "cases": 1024, "seed": 0,
            "warmups": 1, "repeats": 3, "device": 0,
            "settings": SETTINGS, "sha256": hashes,
            "selection": "minimum median prepared eval; baseline plus winner get generation off/on",
            "cpu_baseline": "../asgp-quick-20260929/summary.csv: ASGP 1T, population=1024, cases=1024",
            "commands": [command(f, b, "eval") for f, b in SETTINGS],
        }, indent=2) + "\n")
    results = {(f, b): run(f, b, "eval") for f, b in SETTINGS}
    best = min(results, key=lambda key: statistics.median(
        float(r["total_ms"]) for r in results[key] if int(r["rep"]) >= 0))
    print(f"Selected frames={best[0]}, block={best[1]}", flush=True)
    for f, b in dict.fromkeys([(128, 256), best]):
        run(f, b, "generation")
    manifest = json.loads(manifest_path.read_text())
    manifest["selected"] = list(best)
    manifest["generation_commands"] = [command(f, b, "generation")
                                        for f, b in dict.fromkeys([(128, 256), best])]
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
