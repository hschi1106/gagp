"""Rehearse empirical costs on old TRAINING; no source selection or data freeze."""
from argparse import ArgumentParser
import fcntl
import os
import time
import traceback

import runtime_forecast_v2
import serial


def main(name, development, confirmation):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, new_data=False, no_freeze=True,
                 development=development, confirmation=confirmation)
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.DEADLINE - 14400
            costs, assumptions = runtime_forecast_v2.measured_costs(development, confirmation)
            forecast = runtime_forecast_v2.choose_scale(time.time(), costs)
            forecast.update(assumptions=assumptions, diagnostic_only=True,
                            limitation="Old TRAINING timing only; final selected candidate and actual remaining time must be rechecked")
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
