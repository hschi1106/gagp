from pathlib import Path
import subprocess
import unittest
from unittest import mock

import source_provenance_v2


class TestExternalProvenance(unittest.TestCase):
    def test_archive_directory_does_not_invent_an_upstream_revision(self):
        result = subprocess.CompletedProcess([], 128, "", "fatal: not a git repository")
        with mock.patch.object(source_provenance_v2.subprocess, "run", return_value=result):
            metadata = source_provenance_v2.external_identity(Path("archive"))
        self.assertIsNone(metadata["git_head"])
        self.assertEqual(metadata["kind"], "directory_without_git_metadata")

    def test_git_head_and_local_changes_are_preserved_when_available(self):
        result = subprocess.CompletedProcess([], 0, "abc123\n", "")
        with mock.patch.object(source_provenance_v2.subprocess, "run", return_value=result), \
                mock.patch.object(source_provenance_v2.subprocess, "check_output", return_value=" M src/eval.cc\n"):
            metadata = source_provenance_v2.external_identity(Path("checkout"))
        self.assertEqual(metadata["git_head"], "abc123")
        self.assertEqual(metadata["worktree_status"], " M src/eval.cc\n")

    def test_unexpected_permission_or_git_error_is_not_hidden_as_an_archive(self):
        result = subprocess.CompletedProcess([], 128, "", "fatal: detected dubious ownership")
        with mock.patch.object(source_provenance_v2.subprocess, "run", return_value=result):
            with self.assertRaises(RuntimeError):
                source_provenance_v2.external_identity(Path("checkout"))


if __name__ == "__main__":
    unittest.main()
