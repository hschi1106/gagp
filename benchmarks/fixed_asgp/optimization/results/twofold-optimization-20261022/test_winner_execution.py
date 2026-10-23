from contextlib import redirect_stdout
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

import run_winner


class TestWinnerExecution(TestCase):
    def outputs(self, root):
        out = root / 'new.json'
        return [out, out.with_suffix('.population.json'), out.with_suffix('.history.json'),
                out.with_suffix('.log'), out.with_suffix('.receipt.json')]

    def test_failed_spawn_is_recorded_once_without_retry(self):
        with TemporaryDirectory() as directory:
            outputs = self.outputs(Path(directory))
            with patch.object(run_winner.subprocess, 'run', side_effect=PermissionError('not executable')) as native, redirect_stdout(io.StringIO()):
                result = run_winner.execute_once(['not-executable'], {}, outputs, {}, {'binary_sha256': 'verified'})
            self.assertEqual(result, 127)
            native.assert_called_once()
            receipt = json.loads(outputs[-1].read_text())
            self.assertEqual(receipt['status'], 'spawn_failed')
            self.assertIn('PermissionError', receipt['error'])
            self.assertIn('not executable', outputs[-2].read_text())
            self.assertFalse(outputs[0].exists())

    def test_nonzero_exit_preserves_output_and_refuses_reexecution(self):
        with TemporaryDirectory() as directory:
            outputs = self.outputs(Path(directory))

            def failed_native(*args, **kwargs):
                outputs[0].write_text('partial native output')
                return type('Exit', (), {'returncode': 9})()

            with patch.object(run_winner.subprocess, 'run', side_effect=failed_native) as native, redirect_stdout(io.StringIO()):
                result = run_winner.execute_once(['native'], {}, outputs, {}, {'binary_sha256': 'verified'})
                with self.assertRaisesRegex(ValueError, 'preserve'):
                    run_winner.execute_once(['native'], {}, outputs, {}, {'binary_sha256': 'verified'})
            self.assertEqual(result, 9)
            native.assert_called_once()
            self.assertEqual(outputs[0].read_text(), 'partial native output')
            receipt = json.loads(outputs[-1].read_text())
            self.assertEqual(receipt['status'], 'process_exited')
            self.assertEqual(receipt['returncode'], 9)
            self.assertIn(str(outputs[0]), receipt['artifacts'])
