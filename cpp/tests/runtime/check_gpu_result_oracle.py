"""Check exact direct GPU values and fuel boundaries against the frozen oracle."""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--expected-gpu", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        destination = Path(temp) / "gpu.jsonl"
        result = subprocess.run([args.capture, str(args.oracle), str(destination),
            os.environ.get("GAGP_CUDA_DEVICE", "0")], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        original = {r["ordinal"]: r for r in map(json.loads, args.oracle.read_text().splitlines())
                    if r["kind"] == "execution"}
        actual = list(map(json.loads, destination.read_text().splitlines()))
        assert len(actual) == len(original)
        assert {r["ordinal"] for r in actual} == set(original)
        for row in actual:
            expected = original[row["ordinal"]]
            if args.expected_gpu:
                expected = expected["reference_gpu"]
            assert row["first_non_timeout_fuel"] == expected["first_non_timeout_fuel"], row
            for phase in ("result", "at_probe_cap", "at_boundary", "below_boundary"):
                if phase not in expected:
                    continue
                if expected[phase] is None:
                    continue
                assert row[phase]["ordinary_core_match"], row
                assert row[phase]["error"] == expected[phase]["error"], row
                assert row[phase].get("value") == expected[phase].get("value"), row


if __name__ == "__main__":
    main()
