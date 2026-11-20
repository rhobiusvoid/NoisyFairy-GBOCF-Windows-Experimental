import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from paths import ROOT

CC = shutil.which('gcc') or shutil.which('cc') or shutil.which('clang')


@unittest.skipUnless(CC, 'no C compiler')
class MouseModelTest(unittest.TestCase):
    """Builds tests/test_mouse_model.c against src/runtime_mouse.c (no SDL needed) and runs it."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name) / 'test_mouse_model'
        subprocess.run([CC, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                        str(ROOT / 'tests/test_mouse_model.c'), str(ROOT / 'src/runtime_mouse.c'),
                        '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_properties(self):
        result = subprocess.run([str(self.exe)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('mouse model ok', result.stdout)

    def test_output_matches_recorded_feel(self):
        """The stick bytes for a scripted input match what the model produced when the aim feel was
        signed off (0.7 Beta). A deliberate model change should regenerate the golden file;
        +-1 absorbs libm rounding differences between platforms."""
        out = subprocess.run([str(self.exe), 'dump'], capture_output=True, text=True, timeout=60).stdout.split('\n')
        golden = (ROOT / 'tests/data/mouse_model_golden.txt').read_text().split('\n')
        out = [l for l in out if l]; golden = [l for l in golden if l]
        self.assertEqual(len(out), len(golden))
        bad = 0
        for a, b in zip(out, golden):
            va, vb = [int(x) for x in a.split()], [int(x) for x in b.split()]
            if va[0] != vb[0] or abs(va[1] - vb[1]) > 1 or abs(va[2] - vb[2]) > 1:
                bad += 1
        # a rounding flip can cascade through the queue's error feedback, so allow a small fraction
        self.assertLess(bad, len(golden) * 0.02, f'{bad} of {len(golden)} samples differ')
