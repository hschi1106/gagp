"""Fail closed before starting any formal stage when frozen provenance changes."""
from contextlib import nullcontext
import json
from pathlib import Path
import tempfile
from unittest import TestCase, main, mock
import formal
import serial


class StageReached(Exception):
    pass


class TestFormalAdmission(TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.freeze = dict(started_epoch=serial.START, deadline_epoch=serial.DEADLINE,
            generations=8, repetitions=3, tasks=list(serial.PAPER_TASKS),
            populations=[1024, 16384], settings={}, binary='pinned-bench', binary_sha256='binary',
            harness_sha256={'frozen-formal.py': 'harness'}, source_sha256={'cpp/runtime.cpp': 'source'})
        self.hashes = {'pinned-bench': 'binary', 'frozen-formal.py': 'harness',
                       str(serial.ROOT/'cpp/runtime.cpp'): 'source'}
        for patch in (mock.patch.object(serial, 'RAW', self.root),
                      mock.patch.object(formal, 'gpu_lock', return_value=nullcontext()),
                      mock.patch.object(serial, 'sha', side_effect=lambda p: self.hashes[str(p)]),
                      mock.patch.object(serial, 'read', side_effect=self.read),
                      mock.patch('sys.argv', ['formal.py'])):
            patch.start()
            self.addCleanup(patch.stop)
        self.stage = mock.patch.object(serial, 'Stage', side_effect=StageReached)
        self.stage_mock = self.stage.start()
        self.addCleanup(self.stage.stop)

    def read(self, path):
        if Path(path) == self.root/'formal-freeze.json':
            return self.freeze
        if Path(path).name == 'protocol.json':
            return dict(settings={})
        return json.loads(Path(path).read_text())

    def assert_rejected(self):
        with self.assertRaises(AssertionError):
            formal.main()
        self.stage_mock.assert_not_called()

    def test_unchanged_provenance_reaches_stage(self):
        with self.assertRaises(StageReached):
            formal.main()
        self.stage_mock.assert_called_once()

    def test_changed_binary_never_starts_stage(self):
        self.hashes['pinned-bench'] = 'changed'
        self.assert_rejected()

    def test_changed_harness_never_starts_stage(self):
        self.hashes['frozen-formal.py'] = 'changed'
        self.assert_rejected()

    def test_changed_runtime_source_never_starts_stage(self):
        self.hashes[str(serial.ROOT/'cpp/runtime.cpp')] = 'changed'
        self.assert_rejected()

    def test_changed_external_asgp_source_never_starts_stage(self):
        self.freeze['external_source_sha256'] = {'external.cc': 'original'}
        self.hashes['external.cc'] = 'changed'
        self.assert_rejected()

    def test_changed_linked_library_never_starts_stage(self):
        self.freeze['linked_input_sha256'] = {'runtime.a': 'original'}
        self.hashes['runtime.a'] = 'changed'
        self.assert_rejected()

    def test_settings_change_never_starts_stage(self):
        self.freeze['settings'] = dict(smaller_workspace='1')
        self.assert_rejected()

    def test_active_controller_never_starts_stage(self):
        (self.root/'other-controller.json').write_text('{"pid":12345}')
        with mock.patch.object(formal, 'alive', return_value=True):
            self.assert_rejected()


if __name__ == '__main__':
    main()
