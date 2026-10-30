"""Invoke the supplied author's runner without changing solver behavior."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("logs/asgp-paper-20261025"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--tasks", nargs="+")
    args = parser.parse_args()
    protocol = json.loads((HERE / "protocol.json").read_text())
    root = args.root.resolve()
    identity = json.loads((root / "source-identity.json").read_text())
    for rel, record in identity["files"].items():
        if sha(root / "source" / rel) != record["sha256"]:
            raise RuntimeError(f"Frozen source changed: {rel}")
    tasks = args.tasks or [t["id"] for t in protocol["tasks"]]
    if not set(tasks) <= {t["id"] for t in protocol["tasks"]}:
        parser.error("task is outside the paper's 30 supported tasks")
    # The original runner resumes completed records, but retries failed ones.
    # Keep failures immutable here; an explicit new output directory is required.
    for path in args.out.glob("*-base*-seed*.json"):
        if json.loads(path.read_text()).get("status") != "completed":
            raise RuntimeError(f"Existing failed attempt retained: {path}; use a new output directory")
    command = [sys.executable, str(root / "source/scripts/benchmark.py"),
               "--exe", str(root / "build/src/asgp"), "--out", str(args.out),
               "--tasks", *tasks, "--runs", str(args.runs), "--seed", "0",
               "--evaluations", str(protocol["evaluations"]),
               "--threads", str(protocol["threads"]),
               "--data-seed", str(protocol["data_seed"]),
               "--data-profile", protocol["primary_data_profile"],
               "--simplify", "--timeout", str(protocol["timeout_seconds"])]
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
