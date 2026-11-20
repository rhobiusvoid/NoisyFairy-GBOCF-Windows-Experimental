import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from paths import ROOT

CXX = shutil.which('g++') or shutil.which('c++') or shutil.which('clang++')


@unittest.skipUnless(CXX, 'no C++ compiler')
class SavePatchTest(unittest.TestCase):
    """The launcher's link-battle unlock changes exactly the six link bits and keeps a backup."""

    def test_patch(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'test_savepatch'
            subprocess.run([CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(ROOT / 'tests/test_savepatch.cpp'),
                            '-o', str(exe)], check=True)
            r = subprocess.run([str(exe), str(Path(tmp) / 'work')], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn('savepatch ok', r.stdout)


if __name__ == '__main__':
    unittest.main()
