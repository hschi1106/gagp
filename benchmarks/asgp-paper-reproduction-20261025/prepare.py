"""Freeze the supplied archive, build the unchanged solver, and export datasets."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def checked(command: list[str], log: Path) -> None:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout)
    result.check_returncode()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("/home/hschi1106/r13921069"))
    parser.add_argument("--root", type=Path, default=Path("logs/asgp-paper-20261025"))
    parser.add_argument("--paper", type=Path, default=Path("ref/ASGP.pdf"))
    args = parser.parse_args()
    root = args.root.resolve()
    source = args.source.resolve()
    protocol = json.loads((HERE / "protocol.json").read_text())
    root.mkdir(parents=True, exist_ok=True)
    identity_path = root / "source-identity.json"
    if not identity_path.exists():
        if (root / "source").exists():
            raise RuntimeError("Snapshot exists without identity; use a new root")
        files = [source / "CMakeLists.txt", source / "README.md"]
        for directory in ("src", "tests", "scripts"):
            files += sorted(p for p in (source / directory).rglob("*")
                            if p.is_file() and "__pycache__" not in p.parts)
        records = {}
        with zipfile.ZipFile(source / "ASGP.zip") as archive:
            for path in files:
                rel = path.relative_to(source).as_posix()
                data = path.read_bytes()
                if rel not in archive.namelist() or archive.read(rel) != data:
                    raise RuntimeError(f"Source differs from supplied archive: {rel}")
                records[rel] = {"sha256": sha(path), "matches_zip": True}
            for path in files:
                target = root / "source" / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(path.read_bytes())
        write(identity_path, {"source_root": str(source), "source_git_commit": None,
                            "archive_sha256": sha(source / "ASGP.zip"),
                            "paper_sha256": sha(args.paper), "files": records})
    identity = json.loads(identity_path.read_text())
    for rel, record in identity["files"].items():
        if sha(root / "source" / rel) != record["sha256"] or not record["matches_zip"]:
            raise RuntimeError(f"Frozen source identity mismatch: {rel}")
    if sha(args.paper) != identity["paper_sha256"]:
        raise RuntimeError("Paper changed")
    documents = [{"path": str(args.paper.resolve()), "sha256": sha(args.paper)}]
    thesis = source / "學位論文_陳繼威.pdf"
    if thesis.exists():
        documents.append({"path": str(thesis), "sha256": sha(thesis)})
    document_path = root / "documents.json"
    if document_path.exists() and json.loads(document_path.read_text()) != documents:
        raise RuntimeError("Document identity changed")
    write(document_path, documents)
    # Never rebuild a binary once a run manifest refers to it.
    if not any(root.glob("*/manifest.json")):
        checked(["cmake", "-S", str(root / "source"), "-B", str(root / "build"),
                 "-DCMAKE_BUILD_TYPE=Release"], root / "configure.log")
        checked(["cmake", "--build", str(root / "build"), "-j", "4"], root / "build.log")
    checked(["ctest", "--test-dir", str(root / "build"), "--output-on-failure"],
            root / "ctest.log")
    checked(["python3", "-m", "unittest", "discover", "-s", str(root / "source/scripts"),
             "-p", "test_*.py", "-v"], root / "author-python-tests.log")
    exporter = root / "export_cases"
    checked(["c++", "-std=c++20", "-O2", "-I", str(root / "source/src"),
             str(HERE / "export_cases.cpp"), str(root / "build/src/libasgp_core.a"),
             "-pthread", "-o", str(exporter)], root / "export-build.log")
    dataset_dir = root / "datasets"
    dataset_dir.mkdir(exist_ok=True)
    datasets = []
    for task in protocol["tasks"]:
        for profile in ("legacy", "count-aligned"):
            data = subprocess.check_output([str(exporter), task["id"], profile])
            value = json.loads(data)
            target = dataset_dir / f"{task['id']}.{profile}.json"
            if target.exists() and target.read_bytes() != data:
                raise RuntimeError(f"Dataset changed: {target}")
            target.write_bytes(data)
            def keys(split: str) -> set[str]:
                return {json.dumps({k: e[k] for k in ("elements", "scalars", "start", "extent")},
                                   sort_keys=True) for e in value[split]}
            train, test = keys("train"), keys("test")
            datasets.append({"task": task["id"], "profile": profile,
                             "file": str(target.relative_to(root)), "sha256": sha(target),
                             "train": len(value["train"]), "test": len(value["test"]),
                             "unique_train": len(train), "unique_test": len(test),
                             "overlapping_inputs": len(train & test),
                             "original_unified_schemes": value["original_unified_schemes"]})
    write(root / "datasets.json", datasets)
    environment = {"created_utc": datetime.now(timezone.utc).isoformat(),
                   "platform": platform.platform(), "compiler": subprocess.check_output(
                       ["c++", "--version"], text=True).splitlines()[0],
                   "cpu": subprocess.check_output(["lscpu"], text=True),
                   "solver_sha256": sha(root / "build/src/asgp"),
                   "exporter_source_sha256": sha(HERE / "export_cases.cpp"),
                   "exporter_sha256": sha(exporter), "dataset_exports": len(datasets)}
    write(root / "environment.json", environment)
    print(json.dumps({"root": str(root), "dataset_exports": len(datasets),
                      "solver_sha256": environment["solver_sha256"]}))


if __name__ == "__main__":
    main()
