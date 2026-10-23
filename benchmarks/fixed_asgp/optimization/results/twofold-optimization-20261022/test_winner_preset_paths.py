import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

import serial
import winner_preset2


class TestWinnerPresetPaths(TestCase):
    def fixture(self, root):
        binary = root / 'logs/bench'
        binary.parent.mkdir()
        binary.write_text('unit fixture, not a native executable')
        selected = dict(binary=str(binary), binary_sha256=serial.sha(binary), build_name='fixture',
                        family='fixture', variant='candidate', extra={})
        freeze = dict(format='gagp-twofold-speed-freeze-v3', started_epoch=serial.START,
                      deadline_epoch=serial.DEADLINE, tasks=list(serial.PAPER_TASKS), populations=[1024, 16384],
                      generations=8, repetitions=2, cases=1024, frozen_epoch=1,
                      candidate_selection={'selected': selected}, variants={'candidate': selected},
                      candidate_source_sha256={'cpp/fixture': 'fixture'}, resources={},
                      settings={'GAGP_GPU_PAYLOAD_WORKSPACE_MIB': '16384'})
        path = root / 'freeze.json'
        path.write_text(json.dumps(freeze))
        return path

    def test_relative_cli_paths_export_the_same_repository_origin(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            freeze = self.fixture(root)
            out = root / 'preset.json'
            with patch.object(serial, 'ROOT', root):
                winner_preset2.main(Path(os.path.relpath(freeze)), Path(os.path.relpath(out)))
            result = json.loads(out.read_text())
            self.assertEqual(result['binary'], 'logs/bench')
            self.assertEqual(result['origin']['freeze'], 'freeze.json')
            self.assertEqual(result['origin']['freeze_sha256'], serial.sha(freeze))

    def test_existing_relative_output_is_preserved(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            freeze = self.fixture(root)
            out = root / 'preset.json'
            out.write_text('existing result')
            with patch.object(serial, 'ROOT', root), self.assertRaisesRegex(ValueError, 'preserve'):
                winner_preset2.main(Path(os.path.relpath(freeze)), Path(os.path.relpath(out)))
            self.assertEqual(out.read_text(), 'existing result')
