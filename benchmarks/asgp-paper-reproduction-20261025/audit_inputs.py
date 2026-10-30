"""Check five references independently and compare repeated terminal result fields."""
import argparse
import json
from pathlib import Path


def reference(task: str, raw: dict) -> int:
    xs = raw["elements"]
    if task == "sum_of_elements":
        return sum(xs)
    if task == "median":
        return sorted(xs)[1]
    if task == "smallest":
        return min(xs)
    if task == "fibonacci":
        a, b = 0, 1
        for _ in range(raw["start"][0]):
            a, b = b, a+b
        return a
    if task == "house_robber":
        a, b = 0, 0
        for x in xs:
            a, b = b, max(b, a+x)
        return b
    raise ValueError(task)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("logs/asgp-paper-20261025"))
    args = parser.parse_args()
    root = args.root
    checked = 0
    for task in ("sum_of_elements", "median", "smallest", "fibonacci", "house_robber"):
        dataset = json.loads((root / "datasets" / f"{task}.legacy.json").read_text())
        for split in ("train", "test"):
            fixture = json.loads((root / "gagp-fixtures" / f"{task}.legacy.{split}.json").read_text())
            if len(fixture["cases"]) != len(dataset[split]):
                raise ValueError("Case count differs")
            for raw, case in zip(dataset[split], fixture["cases"]):
                if reference(task, raw) != raw["expected"]["int"] or case["expected"] != {
                        "type": "int", "value": raw["expected"]["int"]}:
                    raise ValueError(f"Reference/fixture differs: {task}/{split}")
                if dataset["sequence_input"] and case["inputs"]["xs"] != {
                        "type": "int_list", "value": raw["elements"]}:
                    raise ValueError("Input sequence differs")
                if dataset["scheme"] == "dynamic-programming":
                    for key, value in zip(("state_a", "state_b", "extent_a", "extent_b"),
                                          raw["start"]+raw["extent"]):
                        if case["inputs"][key] != {"type": "int", "value": value}:
                            raise ValueError("DP input differs")
                checked += 1
    fields = ("success", "train_solved", "raw_success", "evaluations", "nodes_before",
              "nodes_after", "rewrites", "rewrite_attempts", "train_cases", "test_cases")
    repeated = []
    for study in ("native-10", "pilot-native", "validation-1t"):
        for path in sorted((root / study).glob("*-base0-seed*.json")):
            other = root / "focus-100" / path.name
            if not other.exists():
                continue
            a, b = json.loads(path.read_text()), json.loads(other.read_text())
            if a["status"] != "completed" or b["status"] != "completed":
                continue
            if not all(a[k] == b[k] for k in fields):
                raise ValueError(f"Repeated run differs: {path}")
            repeated.append({"study": study, "run": path.stem})
    result = {"independent_reference_cases_checked": checked,
              "repeat_runs_identical_excluding_timing": len(repeated),
              "comparison_scope": "Terminal result fields, not complete generation trajectories",
              "compared_fields": list(fields), "repeat_runs": repeated}
    (root / "input-audit.json").write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k: v for k, v in result.items() if k != "repeat_runs"}))


if __name__ == "__main__":
    main()
