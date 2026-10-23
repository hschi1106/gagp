from copy import deepcopy
from pathlib import Path
from unittest import TestCase
import serial
import winner_preset as module

class TestWinnerPreset(TestCase):

    def freeze(self):
        selected = dict(binary=str(serial.ROOT / 'logs/immutable/bench'), binary_sha256='binary', build_name='build-attempt25', family='combined6', variant='candidate', extra={'GAGP_GPU_PRIVATE_ADJACENT_CONCAT': '1'})
        return dict(format='gagp-twofold-speed-freeze-v3', started_epoch=serial.START, deadline_epoch=serial.DEADLINE, tasks=list(serial.PAPER_TASKS), populations=[1024, 16384], generations=16, repetitions=2, cases=1024, frozen_epoch=123, candidate_selection={'selected': selected}, variants={'candidate': deepcopy(selected)}, settings={'GAGP_GPU_PAYLOAD_WORKSPACE_MIB': '16384', 'GAGP_HOST_THREADS': '1'}, candidate_source_sha256={'cpp/one': 'source'}, resources={})

    def test_actual_options_and_source_are_preserved(self):
        f = self.freeze()
        p = module.make_preset(f, serial.ROOT)
        self.assertEqual(p['settings'], dict(f['settings'], **f['variants']['candidate']['extra']))
        self.assertEqual(p['binary'], 'logs/immutable/bench')
        self.assertEqual(p['source_sha256'], f['candidate_source_sha256'])

    def test_partial_development_cannot_export(self):
        f = self.freeze()
        f['format'] = 'development'
        with self.assertRaises(ValueError):
            module.make_preset(f, serial.ROOT)

    def test_mismatched_binary_or_options_rejected(self):
        for (field, value) in [('binary_sha256', 'different'), ('extra', {})]:
            f = self.freeze()
            f['variants']['candidate'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                module.make_preset(f, serial.ROOT)

    def test_reduced_workspace_rejected(self):
        f = self.freeze()
        f['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB'] = '8192'
        with self.assertRaises(ValueError):
            module.make_preset(f, serial.ROOT)

    def test_ambient_flags_do_not_change_frozen_recipe(self):
        p = module.make_preset(self.freeze(), serial.ROOT)
        (command, env) = module.invocation(p, serial.ROOT, 'source', 'prepared', 'grammar', 16384, Path('new.json'), generations=64, seed=25, ambient={'PATH': 'retained', 'GAGP_GPU_PRIVATE_CELL_SOA': '1', 'GAGP_GPU_PRIVATE_ADJACENT_CONCAT': '0', 'LD_PRELOAD': 'unfrozen'})
        self.assertNotIn('GAGP_GPU_PRIVATE_CELL_SOA', env)
        self.assertEqual(env['GAGP_GPU_PRIVATE_ADJACENT_CONCAT'], '1')
        self.assertEqual(env['PATH'], 'retained')
        self.assertNotIn('LD_PRELOAD', env)
        self.assertEqual(command[-2:], ['search_gpu_overlap', 'new.json'])
        self.assertEqual(env['GAGP_SEARCH_GENERATIONS'], '64')
        self.assertEqual(env['GAGP_BM_SEED'], '25')

    def test_executable_path_escape_rejected(self):
        p = module.make_preset(self.freeze(), serial.ROOT)
        p['binary'] = '../other/bench'
        with self.assertRaises(ValueError):
            module.invocation(p, serial.ROOT, 's', 'p', 'g', 1024, Path('new.json'))
