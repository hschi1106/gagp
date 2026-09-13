"""Exercise snapshot replay in fresh processes through the benchmark adapter."""
import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--adapter", required=True)
    parser.add_argument("--gpu", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        cases = root / "cases.json"
        cases.write_text(json.dumps({"format_version": "fitness-cases", "cases": [
            {"inputs": {}, "expected": {"type": "int", "value": 2}}]}))
        source = root / "ast.json"
        source.write_text(json.dumps({"version": "ast-prefix", "names": [],
            "nodes": [{"kind": k, "i0": 0, "i1": 0} for k in (0, 2, 6, 7, 1)],
            "consts": [{"type": "int", "value": 0}]}))
        snapshot = root / "snapshot.json"
        common = [args.adapter, "--cases", str(cases), "--population-size", "2", "--blocksize", "256",
                  "--snapshot", str(snapshot), "--generations", "1", "--seed", "42", "--penalty", "10"]

        def run(*extra, succeeds=True):
            result = subprocess.run([*common, *extra], capture_output=True, text=True)
            if (result.returncode == 0) != succeeds:
                raise AssertionError(f"unexpected exit {result.returncode}: {result.stdout} {result.stderr}")

        run("--action", "freeze", "--source-ast", str(source))
        frozen = json.loads(snapshot.read_text())
        frozen["programs"][1]["constants"][0]["bits"] = "0000000000000002"
        snapshot.write_text(json.dumps(frozen))
        output = root / "oracle.json"
        run("--action", "oracle", "--engine", "gpu" if args.gpu else "cpu", "--out-json", str(output))
        oracle = json.loads(output.read_text())["programs"]
        assert [p["fitness"] for p in oracle] == [-2, 0], f"unexpected oracle: {oracle}"
        assert [p["cpu_fitness"] for p in oracle] == [-2, 0]
        reproduction_path = root / "reproduction.json"
        run("--action", "freeze-repro", "--out-json", str(reproduction_path))
        reproduction_bytes = reproduction_path.read_bytes()
        reproduction = json.loads(reproduction_bytes)
        assert reproduction["format_version"] == "migration-reproduction-v1"
        assert reproduction["fitness"] == [0, -2]
        assert len(reproduction["parents"]["programs"]) == 2
        assert reproduction["config"]["seed"] == "42"
        assert len(reproduction["candidates"]) == 2
        assert len(reproduction["subtree_ends"]) == 2
        assert len(reproduction["donors"]) == 9 * reproduction["config"]["donor_pool_size_per_type"]
        for candidates in reproduction["candidates"]:
            for candidate in candidates:
                assert len(candidate) == 10
                assert all(isinstance(candidate[i], str) for i in (4, 5, 8))
        for donor in reproduction["donors"]:
            fragment = donor["fragment"]["programs"][0]
            assert fragment["structure"]["nodes"]
            assert all("bits" in constant for constant in fragment["constants"])
        # A second fresh process must materialize identical ranked parents,
        # donor payloads, and all candidate scope metadata.
        run("--action", "freeze-repro", "--out-json", str(reproduction_path))
        assert reproduction_path.read_bytes() == reproduction_bytes
        restored_path = root / "restored-reproduction.json"

        def restore(succeeds=True):
            result = subprocess.run([args.adapter, "--action", "repro-check", "--snapshot",
                str(reproduction_path), "--cases", str(cases), "--out-json", str(restored_path)], capture_output=True, text=True)
            assert (result.returncode == 0) == succeeds, result.stderr

        restore()
        assert restored_path.read_bytes() == reproduction_bytes
        for damage in ("subtree", "signature", "donor", "dimensions"):
            damaged = copy.deepcopy(reproduction)
            if damage == "subtree":
                damaged["subtree_ends"][0][0] += 1
            elif damage == "signature":
                damaged["candidates"][0][0][4] = "18446744073709551616"
            elif damage == "donor":
                damaged["donors"][0]["fragment"]["programs"][0]["structure"]["nodes"][0]["kind"] = 999
            else:
                damaged["donors"].pop()
            reproduction_path.write_text(json.dumps(damaged))
            restore(succeeds=False)
        reproduction_path.write_bytes(reproduction_bytes)
        if args.gpu:
            repro_common = list(common)
            repro_common[repro_common.index("--snapshot") + 1] = str(reproduction_path)
            repro_result = root / "repro-steady.json"
            command = [*repro_common, "--action", "repro-steady", "--repro-backend", "gpu",
                       "--out-json", str(repro_result)]
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            measured_repro = json.loads(repro_result.read_text())
            assert measured_repro["format_version"] == "migration-steady-reproduction-v1"
            assert len(measured_repro["samples"]) == 18
            assert len(measured_repro["children"]["programs"]) == 2
            assert all(s["phases"]["kernel_ms"] > 0 for s in measured_repro["samples"])
            result = subprocess.run([*command, "--seed", "43"], capture_output=True, text=True)
            assert result.returncode != 0 and "settings differ" in result.stderr
        assert [p["cases"][0]["at_fuel_limit"]["value"]["bits"] for p in oracle] == [
            "0000000000000000", "0000000000000002"]
        for program in oracle:
            boundary = program["cases"][0]
            assert boundary["first_non_timeout_fuel"] == 2
            assert boundary["below_boundary"]["error"] == "Timeout"
            assert boundary["at_boundary"]["error"] is None
        run("--action", "steady", "--engine", "gpu" if args.gpu else "cpu", "--out-json", str(output))
        steady = json.loads(output.read_text())
        assert steady["fitness"] == [-2, 0]
        assert steady["warmups"] == 3 and steady["measured_trials"] == 15
        assert len(steady["samples"]) == 18
        assert [s["warmup"] for s in steady["samples"]] == [True] * 3 + [False] * 15
        assert all(s["call_ms"] > 0 for s in steady["samples"])
        if args.gpu:
            assert steady["session_init_ms"] > 0
            assert all(s["phases"]["gpu_eval_kernel_ms"] > 0 for s in steady["samples"])
        run("--action", "steady", "--trials", "0", "--out-json", str(output), succeeds=False)
        run("--action", "steady", "--repro-backend", "gpu", "--out-json", str(output), succeeds=False)
        run("--action", "run", "--engine", "gpu" if args.gpu else "cpu",
            "--repro-backend", "gpu" if args.gpu else "cpu", "--repro-overlap", "on" if args.gpu else "off",
            "--out-json", str(output))
        measured = json.loads(output.read_text())
        assert measured["evolve_call_ms"] > 0
        assert len(measured["generations"]) == 1
        if args.gpu:
            generation = measured["generations"][0]
            assert generation["evaluation"]["gpu_eval_kernel_ms"] > 0
            assert generation["reproduction"]["kernel_ms"] > 0
        frozen["format_version"] = "unsupported"
        snapshot.write_text(json.dumps(frozen))
        run("--action", "run", "--out-json", str(output), succeeds=False)
        bytecode_population = {"format_version": "migration-bytecode-population-v1",
                               "programs": [p["bytecode"] for p in oracle]}
        snapshot.write_text(json.dumps(bytecode_population))
        cases.write_text(json.dumps({"format_version": "migration-evaluation-cases-v1", "cases": [
            {"inputs": [], "expected": oracle[1]["cases"][0]["at_fuel_limit"]["value"]}]}))
        run("--action", "steady", "--engine", "gpu" if args.gpu else "cpu", "--out-json", str(output))
        raw_steady = json.loads(output.read_text())
        assert raw_steady["fitness"] == [-2, 0]
        assert raw_steady["compile_ms"] == 0
        run("--action", "run", "--out-json", str(output), succeeds=False)
        bytecode_population["programs"][0]["code"][0][0] = 999
        snapshot.write_text(json.dumps(bytecode_population))
        run("--action", "steady", "--out-json", str(output), succeeds=False)


if __name__ == "__main__":
    main()
