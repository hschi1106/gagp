"""Verify replay against the frozen reference and reject a corrupted observation."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--replayer", required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--gpu", action="store_true")
    parser.add_argument("--verifiers", action="store_true")
    parser.add_argument("--bytecode-verifiers", action="store_true")
    args = parser.parse_args()
    corpus = args.fixtures / ("reference-ast-rejections.jsonl" if args.verifiers else "reference-oracle.jsonl")
    provenance = json.loads((args.fixtures / ("ast-rejections-provenance.json" if args.verifiers else "provenance.json")).read_text())
    if args.bytecode_verifiers:
        corpus = args.fixtures / "reference-bytecode-rejections.jsonl"
        provenance = json.loads((args.fixtures / "bytecode-rejections-provenance.json").read_text())
    assert hashlib.sha256(corpus.read_bytes()).hexdigest() == provenance["corpus_sha256"]
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        output = root / "summary.json"
        command = [args.replayer, str(corpus), str(output)] + (["--gpu"] if args.gpu else [])
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        summary = json.loads(output.read_text())
        assert summary["compilation_roundtrips"] == provenance.get("compilation_rows", 0)
        if args.bytecode_verifiers:
            assert summary["execution_rows"] == 0
            assert summary["bytecode_rejection_checks"] == provenance["bytecode_rejection_rows"]
        elif args.verifiers:
            assert summary["execution_rows"] == 0
            assert summary["ast_rejection_checks"] == provenance["ast_rejection_rows"]
        else:
            assert summary["execution_rows"] == provenance["execution_rows"]
            assert summary["bytecode_roundtrips"] == provenance["execution_rows"]
            assert summary["verifier_negative_executions"] > 0
        if args.gpu:
            assert summary["gpu_fitness_checks"] == provenance["execution_rows"]
        rows = [json.loads(line) for line in corpus.read_text().splitlines()]
        if args.verifiers or args.bytecode_verifiers:
            rows[0]["code"] = "ok"
        else:
            rows[0]["result"] = {"error": "NameError", "message_hex": ""}
        corrupted = root / "corrupted.jsonl"
        corrupted.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
        result = subprocess.run([args.replayer, str(corrupted), str(root / "invalid.json")],
                                capture_output=True, text=True)
        assert result.returncode != 0, "replay accepted a changed reference result"
        assert "ordinal 0" in result.stderr
        assert not (root / "invalid.json").exists()
        if args.bytecode_verifiers:
            rows = [json.loads(line) for line in corpus.read_text().splitlines()]
            limited = next(row for row in rows if row["code"] == "resource_limit")
            limited["options"]["max_instructions_per_code"] = 0
            corrupted.write_text(json.dumps(limited) + "\n")
            result = subprocess.run([args.replayer, str(corrupted), str(root / "changed-options.json")],
                                    capture_output=True, text=True)
            assert result.returncode != 0, "replay ignored changed verifier options"


if __name__ == "__main__":
    main()
