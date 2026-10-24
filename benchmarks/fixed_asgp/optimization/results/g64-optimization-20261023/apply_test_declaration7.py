"""Preserve compiled-but-unvalidated attempt6; fix test declaration ordering."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_candidate_v3
import serial


def main(name):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists() and not (serial.RAW / "build-attempt7").exists()
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    state = dict(pid=os.getpid(), status="preserving_failed_build", started_epoch=time.time(), deadline_epoch=serial.DEADLINE)
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.START+16200, "Optional source-start cutoff; preserve formal budget"
            failed = serial.read(serial.RAW / "build-attempt6/manifest.json")
            assert failed["status"] == "failed" and failed["results"] == [0,2]
            for pid in (failed["pid"], failed["child_pid"]):
                assert not Path(f"/proc/{pid}").exists()
            for old in ("apply-repair6", "private6", "capacity6-dispatch"):
                prior = serial.read(serial.RAW / (old + "-controller.json"))
                assert not Path(f"/proc/{prior['pid']}").exists()
            assert not list(serial.RAW.glob("*-quality-freeze.json"))
            assert all(serial.sha(serial.ROOT/p) == digest for p,digest in failed["source_sha256"].items())
            archive = serial.RAW / "rejected-build6"
            archive.mkdir()
            executable = serial.ROOT / "cpp/build/release/gagp_fixed_asgp_bench"
            shutil.copy2(executable, archive / "bench-unvalidated")
            relative = "cpp/tests/gpu/test_extended_payload_gpu.cu"
            source = serial.ROOT / relative
            target = archive / relative
            target.parent.mkdir(parents=True)
            shutil.copy2(source, target)
            serial.save(archive / "failure.json", dict(build_manifest=str(serial.RAW / "build-attempt6/manifest.json"),
                build_manifest_sha256=serial.sha(serial.RAW / "build-attempt6/manifest.json"),
                executable_produced=True, executable_native_validated=False,
                executable_sha256=serial.sha(archive / "bench-unvalidated"), candidate_search_samples=0,
                reason="test_private_tuple_fitness is defined in a later included fixture header; add its forward declaration",
                full_failed_source_delta=str(serial.RAW / "build-attempt6/source-delta/tracked.patch")))
            text = source.read_text()
            anchor = "bool test_private_layout_fallbacks() {\n"
            assert text.count(anchor) == 1
            source.write_text(text.replace(anchor, "bool test_private_tuple_fitness();\n\n" + anchor))
            state.update(status="building_and_native_checks", changed_test=relative, changed_sha256=serial.sha(source),
                         source_runtime_unchanged=True, archive=str(archive), repaired_epoch=time.time())
            serial.save(record, state)
        build_candidate_v3.run("build-attempt7")
        state.update(status="requested_phase_complete", finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.name)
