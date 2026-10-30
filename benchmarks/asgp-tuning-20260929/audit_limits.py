"""Summarize the frozen parents after running audit_limits.cpp (no timing run)."""
import csv
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def depth(expression, index=0):
    end = index + int(expression[index][3])
    child, best = index + 1, 0
    while child < end:
        best = max(best, depth(expression, child))
        child += int(expression[child][3])
    assert child == end
    return best + 1


def stats(values):
    return {"min": min(values), "median": statistics.median(values),
            "max": max(values), "mean": statistics.mean(values)}


def main():
    inputs = ROOT / "logs/asgp-quick-1024cases/ready-p1024-c1024-s0.inputs.json"
    data = json.loads(inputs.read_text())
    rows = list(csv.DictReader((ROOT / "logs/asgp-tuning/limits-audit.csv").open()))
    assert len(rows) == len(data["asgp_population"]) == 1024
    assert len(data["cases"]) == 1024
    assert all(int(row["program"]) == i for i, row in enumerate(rows))
    sizes = [sum(map(len, phases)) for phases in data["asgp_population"]]
    depths = [max(depth(p) for p in phases) for phases in data["asgp_population"]]
    maximum_length = max(len(c["input"]) for c in data["cases"])
    assert maximum_length == 50 and max(depths) == 7
    report = {
        "input_sha256": hashlib.sha256(inputs.read_bytes()).hexdigest(),
        "population": len(rows), "cases": len(data["cases"]),
        "max_case_length": maximum_length,
        "asgp_total_phase_nodes": stats(sizes),
        "asgp_max_phase_depth": stats(depths),
    }
    for key in ["nodes", "physical_depth", "expression_depth", "fuel_upper_bound_per_case"]:
        report["gagp_" + key] = stats([int(row[key]) for row in rows])
    report["asgp_steps_upper_bound_per_case"] = stats([(2 * maximum_length - 1) * n for n in sizes])
    report["gagp_extra_physical_nodes"] = stats([int(row["nodes"]) - n for row, n in zip(rows, sizes)])
    report["notes"] = [
        "Static conservative bounds, not measured fuel usage. No backward phase jumps or nested regions.",
        "Original DC skeleton partitions lists into nonempty disjoint windows: at length <=50 there are <=99 visited frames and <=50 live frames.",
        "Fuel upper bound counts all phase instructions/ASGP expression nodes at all 99 frames; short circuiting and early errors only reduce work.",
        "Physical wrappers and lowering helpers are counted in GAGP nodes, not ASGP expression nodes.",
        "Only frozen parents were audited, not mutation donors or children.",
    ]
    assert report["gagp_fuel_upper_bound_per_case"]["max"] < 2_000_000
    assert report["asgp_steps_upper_bound_per_case"]["max"] < 2_000_000
    (HERE / "limits-audit.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
