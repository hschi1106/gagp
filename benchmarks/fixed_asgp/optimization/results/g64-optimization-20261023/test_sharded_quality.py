import copy
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import prepare_inputs
import quality_sharded
import serial


class TestShardedQuality(unittest.TestCase):
    def test_partitioning_retains_all30_tasks_and_every_seed_variant(self):
        tasks = [f"task{i}" for i in range(30)]
        seeds = list(range(35, 115))
        rows = quality_sharded.expected_searches("quality", tasks, seeds)
        self.assertEqual(len(rows), 4800)
        self.assertEqual(len({(r["task"], r["seed"], r["variant"]) for r in rows}), 4800)
        for seed in seeds:
            self.assertEqual(sum(r["seed"] == seed for r in rows), 60)
        self.assertEqual({r["stage"] for r in rows},
                         {f"quality.s{seed}-{v}" for seed in seeds for v in ("baseline", "candidate")})

    def test_freeze_guard_precedes_any_new_data_and_keeps_resources(self):
        tasks = [f"task{i}" for i in range(30)]
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "dummy-binary"
            binary.write_bytes(b"unit fixture, never executed")
            freeze = dict(format="gagp-g64-quality-freeze-v1", started_epoch=serial.START,
                          deadline_epoch=serial.DEADLINE, population=1024, generations=64, tasks=tasks,
                          seeds=list(range(35, 45)), data_seed=20261024, reserve_s=14400,
                          train_limit=200, test_limit=1000,
                          settings=dict(GAGP_HOST_THREADS="1", GAGP_GPU_PAYLOAD_WORKSPACE_MIB="16384", GAGP_GPU_PHASE_POPULATION="1"),
                          variants={v: dict(binary=str(binary), binary_sha256=serial.sha(binary), extra={})
                                    for v in ("baseline", "candidate")}, harness_sha256={},
                          first_heldout_use="after immutable candidate/scale freeze")
            with mock.patch.object(serial, "read", return_value=dict(tasks=tasks)):
                prepare_inputs.validate_freeze(freeze)
                for key, value in [("tasks", tasks[:9]), ("generations", 8),
                                   ("seeds", list(reversed(freeze["seeds"]))), ("train_limit", 100),
                                   ("data_seed", 20261022), ("reserve_s", 3600)]:
                    invalid = copy.deepcopy(freeze)
                    invalid[key] = value
                    with self.assertRaises(AssertionError):
                        prepare_inputs.validate_freeze(invalid)
                invalid = copy.deepcopy(freeze)
                invalid["variants"]["candidate"]["extra"]["GAGP_GPU_PAYLOAD_WORKSPACE_MIB"] = "8192"
                with self.assertRaises(AssertionError):
                    prepare_inputs.validate_freeze(invalid)
            self.assertEqual(list(Path(temporary).iterdir()), [binary])


if __name__ == "__main__":
    unittest.main()
