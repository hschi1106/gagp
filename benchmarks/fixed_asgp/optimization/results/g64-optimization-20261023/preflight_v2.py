"""Read-only source/forecast rehearsal on a completed old TRAINING candidate."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import runtime_forecast
import serial
import source_provenance_v2 as source_provenance


def main(name, development, confirmation):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 development=development, confirmation=confirmation, no_freeze=True, new_data=False)
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            confirm = serial.read(serial.RAW / (confirmation + "-controller.json"))
            assert confirm["status"] == "requested_phase_complete" and not Path(f"/proc/{confirm['pid']}").exists()
            state["status"] = "checking_sources_and_costs"
            serial.save(record, state)
            provenance = source_provenance.collect(confirm["source_build"])
            costs, assumptions = runtime_forecast.measured_costs(development, confirmation)
            forecast = runtime_forecast.choose_scale(time.time(), costs)
            forecast.update(assumptions=assumptions, diagnostic_only=True,
                            caveat="Rehearsal, not final selection/freeze; remaining time and selected binary costs will be rechecked")
            serial.save(serial.RAW / (name + "-source-provenance.json"), provenance)
            serial.save(serial.RAW / (name + "-runtime-forecast.json"), forecast)
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     recommended_seed_count=forecast["selected_seed_count"], available_s=forecast["available_s"])
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--development", required=True)
    parser.add_argument("--confirmation", required=True)
    args = parser.parse_args()
    main(args.name, args.development, args.confirmation)
