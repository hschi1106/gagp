from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class TestBuildConfiguration(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake") and shutil.which("c++"),
                         "CMake and a C++ compiler are needed for configuration")
    def test_requested_cuda_cannot_silently_become_cpu_only(self) -> None:
        # Explicit NOTFOUND avoids consulting any installed GPU/toolkit. The same
        # build directory must recover when the user deliberately selects CPU.
        with tempfile.TemporaryDirectory(prefix="gagp-config-") as directory:
            command = ["cmake", "-S", str(ROOT / "cpp"), "-B", directory,
                       "-DCMAKE_CUDA_COMPILER=NOTFOUND"]
            requested = subprocess.run(command + ["-DGAGP_ENABLE_CUDA=ON"],
                                       capture_output=True, text=True)
            self.assertNotEqual(requested.returncode, 0)
            self.assertIn("GAGP_ENABLE_CUDA=ON requires a CUDA compiler",
                          requested.stdout + requested.stderr)
            cpu = subprocess.run(command + ["-DGAGP_ENABLE_CUDA=OFF"],
                                 capture_output=True, text=True)
            self.assertEqual(cpu.returncode, 0, cpu.stdout + cpu.stderr)
