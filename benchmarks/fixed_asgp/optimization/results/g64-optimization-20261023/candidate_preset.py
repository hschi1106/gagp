"""Export the frozen G64 candidate or run it once in a separate user folder."""
from argparse import ArgumentParser
import fcntl
import json
import os
from pathlib import Path
import subprocess
import time

import serial


FORMAT = "gagp-g64-candidate-preset-v1"


def make_preset(freeze, selection, provenance, root):
    if freeze.get("format") != "gagp-g64-speed-freeze-v1":
        raise ValueError("Requires the frozen G64 speed matrix")
    if (freeze.get("started_epoch"), freeze.get("deadline_epoch")) != (serial.START, serial.DEADLINE):
        raise ValueError("Different study clock")
    if (freeze.get("population"), freeze.get("generations"), freeze.get("repetitions")) != (1024, 64, 3):
        raise ValueError("Frozen primary scale differs")
    if freeze.get("tasks") != list(serial.PAPER_TASKS):
        raise ValueError("Frozen task set differs")
    candidate = freeze["variants"]["candidate"]
    if (candidate["binary"], candidate["binary_sha256"], candidate["extra"]) != (
            selection["binary"], selection["binary_sha256"], selection["candidate_extra"]):
        raise ValueError("Selection and frozen candidate differ")
    if candidate["binary_sha256"] != provenance["selected_binary_sha256"]:
        raise ValueError("Source provenance identifies another binary")
    binary = Path(candidate["binary"])
    if not binary.is_absolute():
        raise ValueError("Frozen binary must be absolute")
    relative = binary.resolve().relative_to(Path(root).resolve())
    settings = dict(freeze["settings"], **candidate["extra"])
    if settings.get("GAGP_GPU_PAYLOAD_WORKSPACE_MIB") != "16384" or settings.get("GAGP_HOST_THREADS") != "1":
        raise ValueError("Frozen workspace/host-worker settings differ")
    if settings.get("GAGP_GPU_PHASE_POPULATION") != "1" or "GAGP_SEARCH_EXPORT_BEST_ONLY" in settings:
        raise ValueError("Requires GPU reproduction and full final-population export")
    return dict(format=FORMAT, binary=str(relative), binary_sha256=candidate["binary_sha256"],
                settings=settings, source_build=selection["source_build"],
                source_sha256=provenance["cpp_source_sha256"], default_generations=64,
                gpu_lock=str(serial.GPU_LOCK), physical_caps=dict(ast_nodes=2048, depth=24, phase_nodes=1024),
                origin=dict(frozen_epoch=freeze["freeze_epoch"], data_seed=freeze["data_seed"]),
                usage_scope="One explicit new user invocation, separate from formal evidence. "
                    "Cases must be specified; no automatic retry, selection, resampling or success claim.")


def invocation(preset, root, source, prepared, grammar, population, out, cases, generations=64, seed=35, ambient=None):
    if preset.get("format") != FORMAT or preset.get("gpu_lock") != str(serial.GPU_LOCK):
        raise ValueError("Unsupported candidate preset or exclusion lock")
    binary = Path(preset["binary"])
    root = Path(root).resolve()
    if binary.is_absolute() or ".." in binary.parts or not (root / binary).resolve().is_relative_to(root):
        raise ValueError("Preset binary escapes repository root")
    if any(type(v) is not int or v < 1 for v in (population, cases, generations)) or type(seed) is not int or seed < 0:
        raise ValueError("Specify positive population/cases/generations and a nonnegative seed")
    out = Path(out).resolve()
    if out.is_relative_to(serial.RAW.resolve()):
        raise ValueError("Choose a user output folder outside the formal study logs")
    outputs = [out, out.with_suffix(".population.json"), out.with_suffix(".history.json"),
               out.with_suffix(".log"), out.with_suffix(".receipt.json")]
    if len(set(outputs)) != len(outputs):
        raise ValueError("Output paths collide; choose a new .json filename")
    environment = {k: v for k, v in (ambient or {}).items()
                   if not k.startswith("GAGP_") and k not in ("LD_PRELOAD", "MALLOC_CONF")}
    environment.update(preset["settings"])
    environment.update(GAGP_SEARCH_GENERATIONS=str(generations), GAGP_BM_SEED=str(seed),
                       GAGP_BM_CASE_COUNT=str(cases), GAGP_SEARCH_EXPORT=str(outputs[1]),
                       GAGP_SEARCH_HISTORY_EXPORT=str(outputs[2]))
    command = list(map(str, [root / binary, "measure", Path(source).resolve(), Path(prepared).resolve(),
                             Path(grammar).resolve(), population, "search_gpu_overlap", out]))
    return command, environment, outputs


def execute_once(command, environment, outputs, hashes, preset):
    if any(path.exists() for path in outputs):
        raise ValueError("Preserve existing invocation artifacts")
    outputs[0].parent.mkdir(parents=True, exist_ok=True)
    started, code, status, error = time.time(), 127, "spawn_failed", None
    with outputs[3].open("x") as log:
        try:
            result = subprocess.run(command, cwd=serial.ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
            code, status = result.returncode, "process_exited"
        except OSError as exception:
            error = repr(exception)
            log.write(error + "\n")
    receipt = dict(status=status, returncode=code, error=error, started_epoch=started, finished_epoch=time.time(),
                   command=command, binary_sha256=preset["binary_sha256"], input_sha256=hashes,
                   settings={k: v for k, v in environment.items()
                             if k.startswith("GAGP_") or k in ("LD_PRELOAD", "MALLOC_CONF")},
                   artifact_sha256={str(p): serial.sha(p) for p in outputs[:-1] if p.exists()},
                   scope="New explicit user invocation, not formal evidence; completion alone does not establish solved task or CPU parity")
    with outputs[-1].open("x") as stream:
        json.dump(receipt, stream, indent=2)
        stream.write("\n")
    print(json.dumps(dict(returncode=code, output=str(outputs[0]), receipt=str(outputs[-1]))))
    return code


def export(freeze_path, selection_path, out):
    freeze_path, selection_path, out = (Path(p).resolve() for p in (freeze_path, selection_path, out))
    if out.exists():
        raise ValueError("Preserve existing preset; choose a new output")
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        freeze, selection = serial.read(freeze_path), serial.read(selection_path)
        provenance_path = Path(freeze["source_provenance"])
        if serial.sha(provenance_path) != freeze["source_provenance_sha256"]:
            raise ValueError("Source provenance changed")
        preset = make_preset(freeze, selection, serial.read(provenance_path), serial.ROOT)
        if serial.sha(serial.ROOT / preset["binary"]) != preset["binary_sha256"]:
            raise ValueError("Pinned candidate binary changed")
        preset["origin"].update(freeze=str(freeze_path.relative_to(serial.ROOT)), freeze_sha256=serial.sha(freeze_path),
                                selection=str(selection_path.relative_to(serial.ROOT)), selection_sha256=serial.sha(selection_path))
        preload = preset["settings"].get("LD_PRELOAD")
        preset["external_runtime_sha256"] = {preload: serial.sha(preload)} if preload else {}
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open("x") as stream:
            json.dump(preset, stream, indent=2)
            stream.write("\n")


def main():
    parser = ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    write = commands.add_parser("export", help="Export the actual frozen candidate after formal timing")
    write.add_argument("--freeze", type=Path, default=serial.RAW / "formal0-speed-freeze.json")
    write.add_argument("--selection", type=Path, default=serial.RAW / "final-selection0.json")
    write.add_argument("--out", type=Path, required=True)
    run = commands.add_parser("run", help="One separate explicit user invocation")
    run.add_argument("--preset", type=Path, required=True)
    for name in ("source", "prepared", "grammar", "out"):
        run.add_argument("--" + name, type=Path, required=True)
    for name in ("population", "cases"):
        run.add_argument("--" + name, type=int, required=True)
    run.add_argument("--generations", type=int, default=64)
    run.add_argument("--seed", type=int, default=35)
    run.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.operation == "export":
        export(args.freeze, args.selection, args.out)
        return 0
    preset = serial.read(args.preset)
    command, environment, outputs = invocation(preset, serial.ROOT, args.source, args.prepared, args.grammar,
        args.population, args.out, args.cases, args.generations, args.seed, os.environ)
    if args.dry_run:
        print(json.dumps(dict(command=command, settings={k: v for k, v in environment.items()
             if k.startswith("GAGP_") or k in ("LD_PRELOAD", "MALLOC_CONF")}, gpu_lock=preset["gpu_lock"],
             new_search_executed=False), indent=2))
        return 0
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if serial.sha(command[0]) != preset["binary_sha256"]:
            raise ValueError("Pinned candidate binary changed")
        for path, digest in preset.get("external_runtime_sha256", {}).items():
            if serial.sha(path) != digest:
                raise ValueError("External runtime dependency changed")
        hashes = {str(p.resolve()): serial.sha(p) for p in (args.preset, args.source, args.prepared, args.grammar)}
        return execute_once(command, environment, outputs, hashes, preset)


if __name__ == "__main__":
    raise SystemExit(main())
