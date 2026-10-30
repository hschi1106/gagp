"""Export projected labels into GAGP fitness-cases; do not translate fitness rules."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def value(kind: str, raw: object) -> dict:
    if kind == "charList":
        return {"type": "string", "value": "".join(chr(c) for c in raw)}
    if kind == "intList":
        return {"type": "int_list", "value": raw}
    if kind == "char":
        return {"type": "char", "value": chr(raw)}
    if kind not in {"bool", "int"}:
        raise ValueError(f"Unsupported projected type: {kind}")
    return {"type": kind, "value": raw}


def convert(dataset: dict, split: str, source_sha: str) -> dict:
    cases = []
    for raw in dataset[split]:
        inputs = {}
        if dataset["sequence_input"]:
            inputs["xs"] = value(dataset["list_type"], raw["elements"])
        if len(dataset["scalar_names"]) != len(raw["scalars"]):
            raise ValueError("Scalar names do not match source data")
        for name, scalar in zip(dataset["scalar_names"], raw["scalars"]):
            if name in inputs:
                raise ValueError("Input name collision")
            inputs[name] = value("int", scalar)
        if dataset["scheme"] == "dynamic-programming":
            for name, scalar in zip(("state_a", "state_b", "extent_a", "extent_b"),
                                    raw["start"]+raw["extent"]):
                if name in inputs:
                    raise ValueError("Input name collision")
                inputs[name] = value("int", scalar)
        kind = dataset["output"]
        raw_output = raw["expected"]["list" if kind in {"intList", "charList"}
                                     else "bool" if kind == "bool" else "int"]
        cases.append({"inputs": inputs, "expected": value(kind, raw_output)})
    schema = {"inputs": {k: v["type"] for k, v in cases[0]["inputs"].items()},
              "expected": cases[0]["expected"]["type"]}
    for case in cases:
        actual = {"inputs": {k: v["type"] for k, v in case["inputs"].items()},
                  "expected": case["expected"]["type"]}
        if actual != schema:
            raise ValueError("Inconsistent fixture schema")
    return {"format_version": "fitness-cases", "schema": schema,
            "meta": {"suite": "asgp-source", "problem": dataset["task"],
                     "source_sha256": source_sha, "data_seed": dataset["data_seed"],
                     "profile": dataset["profile"], "split": split, "case_count": len(cases),
                     "fitness_alignment": "Projected labels only; source fitness/tuple summaries and grammar require separate alignment"},
            "cases": cases}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("logs/asgp-paper-20261025"))
    args = parser.parse_args()
    out = args.root / "gagp-fixtures"
    out.mkdir(exist_ok=True)
    records = []
    for row in json.loads((args.root / "datasets.json").read_text()):
        source = args.root / row["file"]
        if sha(source) != row["sha256"]:
            raise ValueError("Source dataset changed")
        dataset = json.loads(source.read_text())
        for split in ("train", "test"):
            fixture = convert(dataset, split, row["sha256"])
            target = out / f"{row['task']}.{row['profile']}.{split}.json"
            text = json.dumps(fixture, ensure_ascii=False, separators=(",", ":"))+"\n"
            if target.exists() and target.read_text() != text:
                raise ValueError(f"Existing fixture differs: {target}")
            target.write_text(text)
            records.append({"task": row["task"], "profile": row["profile"], "split": split,
                            "file": str(target.relative_to(args.root)), "sha256": sha(target),
                            "source_sha256": row["sha256"], "case_count": len(fixture["cases"]),
                            "schema": fixture["schema"]})
    (args.root / "gagp-fixtures.json").write_text(json.dumps(records, indent=2)+"\n")
    print(json.dumps({"fixtures": len(records), "out": str(out)}))


if __name__ == "__main__":
    main()
