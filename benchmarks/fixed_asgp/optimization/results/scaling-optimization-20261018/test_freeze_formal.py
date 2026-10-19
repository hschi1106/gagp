"""Timing allocation checks use synthetic records, never benchmark outcomes."""
import json
from pathlib import Path
import tempfile
from unittest import TestCase, main, mock
from freeze_formal import choose_scale, forecast, write_new_freeze, dependency_provenance
import serial


TASKS = ('is_sorted', 'all_equal', 'house_robber')


def observations():
    rows = [dict(task=task, population=str(population), asgp_s='10', gpu_s='2',
                 asgp_process_s='11', gpu_process_s='3')
            for task in TASKS for population in (1024, 16384)]
    full = dict(results={task: dict(process_s=6, evolve_wall_s=1) for task in TASKS})
    pilot = dict(jobs={})
    for task in TASKS:
        for variant in ('asgp', 'gpu_base', 'gpu_exec'):
            search = 20 if variant == 'asgp' else 4
            pilot['jobs'][f'{task}.p16384.{variant}.s0.g8'] = dict(
                search_s=search, process_s=search+10)
    development = dict(jobs={str(population): dict(status='ok', population=population,
        compact_root=0, process_s=10+(3 if population == 1024 else 5), search_s=10)
        for population in (1024, 16384)})
    return rows, full, pilot, development


class TestGlobalScale(TestCase):
    def test_freeze_is_valid_json_and_cannot_replace_existing_selection(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder)/'freeze.json'
            frozen = dict(generations=8, repetitions=3, compact_root_vm=False)
            write_new_freeze(target, frozen)
            self.assertEqual(json.loads(target.read_text()), frozen)
            with self.assertRaises(FileExistsError):
                write_new_freeze(target, dict(generations=16))
            self.assertEqual(json.loads(target.read_text()), frozen)

    def test_external_source_directory_without_git_records_real_compile_inputs(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            external = root/'external'
            source = external/'src'
            source.mkdir(parents=True)
            for name in ('unit.cc', 'unit.h', 'CMakeLists.txt'):
                (source/name).write_text(name)
            build = root/'cpp/build/release'
            link = build/'CMakeFiles/gagp_fixed_asgp_bench.dir/link.txt'
            link.parent.mkdir(parents=True)
            link.write_text('/usr/bin/c++ fixture.o libfixture.a -o fixture')
            for name in ('fixture.o', 'libfixture.a', 'CMakeCache.txt'):
                (build/name).write_text(name)
            for name in ('lang/grammar.cc', 'eval/interpreter.cc', 'lang/printer.cc'):
                path = build/'asgp-list-combine-v1'/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            allocator = root/'allocator.so'
            allocator.write_text('allocator')
            with mock.patch.object(serial, 'ROOT', root), mock.patch.object(serial, 'read',
                    return_value=dict(settings=dict(LD_PRELOAD=str(allocator)))):
                record = dependency_provenance(external)
            self.assertIsNone(record['external_asgp_revision'])
            self.assertEqual(record['external_asgp_source_kind'], 'source_directory')
            self.assertEqual(len(record['external_source_sha256']), 6)
            self.assertEqual(record['linked_input_sha256'][str(build/'libfixture.a')], serial.sha(build/'libfixture.a'))
            self.assertEqual(record['linked_input_sha256'][str(allocator)], serial.sha(allocator))

    def test_priority_and_reserve_boundary(self):
        rows = [dict(generations=16, forecast_s=60), dict(generations=8, forecast_s=40)]
        self.assertIs(choose_scale(rows, 10, 90, 20), rows[0])
        self.assertIs(choose_scale(rows, 11, 90, 20), rows[1])
        self.assertIsNone(choose_scale(rows, 51, 90, 20))
        self.assertIsNone(choose_scale(rows, 91, 90, 0))

    def test_does_not_sort_declared_priority_by_duration(self):
        rows = [dict(forecast_s=40), dict(forecast_s=20)]
        self.assertIs(choose_scale(rows, 0, 100, 0), rows[0])

    def test_includes_both_gpu_audits_and_complete_process_cost(self):
        calibration, estimates = forecast(*observations(), [(8, 3)])
        self.assertEqual(calibration, dict(asgp=2, gpu=2, full_audit=2))
        row = estimates[0]
        self.assertEqual(row['planned_runs'], 54)
        self.assertEqual(row['components']['asgp_search_s'], 360)
        self.assertEqual(row['components']['gpu_original_search_s'], 72)
        self.assertEqual(row['components']['gpu_candidate_search_s'], 72)
        self.assertEqual(row['components']['process_overhead_s'], 198)
        self.assertEqual(row['components']['full_cpu_audit_and_export_s'], 63.75)
        self.assertEqual(row['components']['input_hash_s'], 54)
        self.assertAlmostEqual(row['forecast_s'], 819.75*1.2)

    def test_full_audit_once_per_cell_not_once_per_timing_repetition(self):
        _, (once, three) = forecast(*observations(), [(8, 1), (8, 3)])
        for row in (once, three):
            self.assertEqual(row['components']['full_cpu_audit_and_export_s'], 63.75)
        self.assertEqual(three['unmargined_s']-once['unmargined_s'], 504)

    def test_longer_search_and_more_expensive_final_cpu_audit(self):
        _, (short, longer) = forecast(*observations(), [(8, 1), (16, 1)])
        self.assertEqual(longer['components']['asgp_search_s'], short['components']['asgp_search_s']*2.25)
        self.assertEqual(longer['components']['full_cpu_audit_and_export_s'],
                         short['components']['full_cpu_audit_and_export_s']*1.5)

    def test_candidate_development_gain_cannot_reduce_allocation_forecast(self):
        data = observations()
        data[-1]['jobs']['fast-candidate'] = dict(status='ok', population=16384,
            compact_root=1, process_s=0.02, search_s=0.01)
        _, (row,) = forecast(*data, [(8, 1)])
        self.assertEqual(row['components']['gpu_candidate_search_s'],
                         row['components']['gpu_original_search_s'])
        self.assertEqual(row['components']['process_overhead_s'], 66)

    def test_pilot_drift_never_assumes_historical_rows_get_faster(self):
        rows, full, pilot, development = observations()
        for job in pilot['jobs'].values():
            job.update(search_s=0.1, process_s=0.2)
        calibration, _ = forecast(rows, full, pilot, development, [(8, 1)])
        self.assertEqual(calibration, dict(asgp=1, gpu=1, full_audit=1))


if __name__ == '__main__':
    main()
