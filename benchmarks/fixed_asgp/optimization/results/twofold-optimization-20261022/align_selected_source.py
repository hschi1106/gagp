"""Promote the exact validated winner after timed work; preserve rejected C++.

The source mirror must already have passed validate_selected_source.py. Refuse
any worktree C++ change since the explicitly named prior admitted build. This
keeps unrelated/user changes out of the promotion, and never alters binaries.
"""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import time
import traceback

import serial
from validate_selected_source import exited_controller


def plan_alignment(prior, current, selected, tracked):
    if not prior or not selected:
        raise ValueError("both complete admitted source maps are required")
    if current != prior:
        raise ValueError("current C++ differs from prior admitted build; preserve and inspect changes")
    for path in set(prior) | set(selected):
        parts = PurePosixPath(path).parts
        if not parts or parts[0] != "cpp" or ".." in parts or PurePosixPath(path).is_absolute():
            raise ValueError("source map path escapes cpp: " + path)
    removed = sorted(set(prior) - set(selected))
    if set(removed) & set(tracked):
        raise ValueError("automatic promotion cannot delete a tracked C++ source")
    return dict(copy=sorted(p for p in selected if selected[p] != prior.get(p)),
                remove=removed, unchanged=sorted(p for p in selected if selected[p] == prior.get(p)))


def source_paths():
    return subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "cpp"],
        cwd=serial.ROOT, text=True).splitlines()


def file_map(paths):
    result = {}
    for path in paths:
        source = serial.ROOT / path
        if source.is_symlink() or not source.is_file():
            raise ValueError("source is not an ordinary file: " + path)
        result[path] = serial.sha(source)
    return result


def main(validation_path, prior_build, name, controllers, execute=False):
    out = serial.RAW / name
    out.mkdir()
    record = out / "manifest.json"
    state = dict(pid=os.getpid(), status="checking", started_epoch=time.time(), execute=execute,
                 validation=str(validation_path), validation_sha256=serial.sha(validation_path),
                 prior_build=prior_build, controllers=controllers)
    serial.save(record, state)
    try:
        if not controllers:
            raise ValueError("explicit exited timed controllers are required")
        for controller in controllers:
            exited_controller(controller)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for controller in controllers:
                exited_controller(controller)
            validation = serial.read(validation_path)
            if validation["status"] != "ok":
                raise ValueError("selected-source full regression has not passed")
            for path, digest in validation["evidence_sha256"].items():
                if serial.sha(path) != digest:
                    raise ValueError("selected-source validation evidence changed: " + path)
            selection = serial.read(validation["selection"])["selected"]
            if serial.sha(selection["binary"]) != validation["candidate_binary_sha256"]:
                raise ValueError("pinned measured binary changed")
            prior_path = serial.RAW / prior_build / "manifest.json"
            prior = serial.read(prior_path)
            if prior["status"] != "ok":
                raise ValueError("prior current-source build was not admitted")
            expected = validation["candidate_source_sha256"]
            mirror = Path(validation["source_mirror"])
            for path, digest in expected.items():
                source = mirror / path
                if source.is_symlink() or serial.sha(source) != digest:
                    raise ValueError("validated source mirror changed: " + path)
            tracked = subprocess.check_output(["git", "ls-files", "cpp"], cwd=serial.ROOT, text=True).splitlines()
            current = file_map(source_paths())
            plan = plan_alignment(prior["source_sha256"], current, expected, tracked)
            state.update(plan=plan, prior_manifest_sha256=serial.sha(prior_path),
                         old_source_sha256=current, selected_source_sha256=expected,
                         candidate_binary_sha256=validation["candidate_binary_sha256"])
            serial.save(record, state)
            if execute:
                # A complete ordinary-file backup precedes the first mutation.
                # Individual attempts/snapshots and the measured binary remain.
                backup = out / "previous-source"
                backup.mkdir()
                for path in current:
                    target = backup / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(serial.ROOT / path, target)
                    if serial.sha(target) != current[path]:
                        raise ValueError("source backup changed: " + path)
                state.update(status="promoting", backup=str(backup), completed=[])
                serial.save(record, state)
                for path in plan["copy"]:
                    target = serial.ROOT / path
                    if any(parent.is_symlink() for parent in target.parents if parent != serial.ROOT):
                        raise ValueError("destination parent is a symlink: " + path)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    temporary = target.with_name(target.name + ".twofold-promotion.tmp")
                    if temporary.exists():
                        raise ValueError("promotion temporary already exists: " + str(temporary))
                    shutil.copyfile(mirror / path, temporary)
                    if serial.sha(temporary) != expected[path]:
                        raise ValueError("promotion copy changed: " + path)
                    temporary.replace(target)
                    state["completed"].append(["copy", path])
                    serial.save(record, state)
                for path in plan["remove"]:
                    (serial.ROOT / path).unlink()
                    state["completed"].append(["remove", path])
                    serial.save(record, state)
                if file_map(source_paths()) != expected:
                    raise ValueError("promoted worktree is not the complete selected source map")
                if serial.sha(selection["binary"]) != validation["candidate_binary_sha256"]:
                    raise ValueError("pinned measured binary changed during promotion")
            state.update(status="ok", finished_epoch=time.time(), source_sha256=serial.sha(Path(__file__)),
                         scope="exact validated selected C++ only; prior source backed up; pinned binaries unchanged; no build-tree reuse or rebuilt-binary speed claim")
    except BaseException as error:
        state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--validation", required=True, type=Path)
    parser.add_argument("--prior-build", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--controller", action="append", required=True)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()
    main(args.validation, args.prior_build, args.name, args.controller, args.execute)
