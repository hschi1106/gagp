"""Exercise the real Stage lock while the formal controller lock is held."""
from contextlib import nullcontext
import json
from pathlib import Path
import tempfile
from unittest import TestCase, mock, main
import formal_attempt1
import serial


class StageReached(Exception):
    pass


class TestDistinctControllerStageLocks(TestCase):
    def test_real_stage_admission_does_not_lock_against_its_controller(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binary = root/'pinned-bench'
            binary.write_text('#!/bin/sh\nexit 0\n')
            frozen = dict(started_epoch=serial.START, deadline_epoch=serial.DEADLINE,
                generations=8, repetitions=2, tasks=list(serial.PAPER_TASKS),
                populations=[1024,16384], settings={}, binary=str(binary),
                binary_sha256=serial.sha(binary), harness_sha256={}, source_sha256={})
            (root/'formal-freeze-attempt1.json').write_text(json.dumps(frozen))
            original_stage, original_read = serial.Stage, serial.read
            admitted = []
            def checked_stage(name, path, settings, resume):
                # Use the real lock acquisition that the earlier preflight-only
                # tests never reached. No benchmark process is launched here.
                stage = original_stage(name, path, settings=settings, strict=False, resume=resume)
                stage.lock.close()
                admitted.append(name)
                raise StageReached()
            def read(path):
                if Path(path).name == 'protocol.json':
                    return dict(settings={})
                return original_read(path)
            with mock.patch.object(serial, 'RAW', root), mock.patch.object(serial, 'read', side_effect=read), \
                    mock.patch.object(serial, 'Stage', side_effect=checked_stage), \
                    mock.patch.object(formal_attempt1, 'gpu_lock', return_value=nullcontext()), \
                    mock.patch('sys.argv', ['formal_attempt1.py']):
                with self.assertRaises(StageReached):
                    formal_attempt1.main()
            self.assertEqual(admitted, ['formal-attempt1'])
            self.assertTrue((root/'formal-attempt1/manifest.json').exists())
            self.assertTrue((root/'formal-attempt1-controller.lock').exists())
            self.assertTrue((root/'formal-attempt1.lock').exists())


if __name__ == '__main__':
    main()
