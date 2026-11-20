"""The key/mouse/weapon settings are described in three places (runtime, Controls panel, docs).
These tests keep them in step."""
import re
import unittest

from paths import ROOT

PAD = (ROOT / 'src' / 'runtime_pad.c').read_text()
PANEL = (ROOT / 'gpu' / 'shim' / 'ps4rt_controls.h').read_text()
README = (ROOT / 'README.md').read_text()


def panel_bindings():
    block = PANEL[PANEL.index('kBindings[]'):]
    block = block[:block.index('};')]
    return re.findall(r'\{"(\w+)",\s*"[^"]*",\s*SDL_SCANCODE_\w+,\s*"([^"]+)"\}', block)


class ControlsConsistency(unittest.TestCase):
    def test_every_panel_binding_is_known_to_the_runtime(self):
        bindings = panel_bindings()
        self.assertGreaterEqual(len(bindings), 33)
        for name, _group in bindings:
            probe = 'key_weapon' if name.startswith('weapon') else 'key_' + name
            self.assertIn(probe, PAD, f'runtime does not parse key_{name}')

    def test_weapon_defaults_match_the_official_layout(self):
        expected = ['square', 'cross', 'circle', 'triangle', 'square+triangle', 'square+cross',
                    'circle+triangle', 'circle+cross']
        block = PANEL[PANEL.index('kWeaponDefaults'):]
        block = block[:block.index('};')]
        self.assertEqual(re.findall(r'"([a-z+]+)"', block), expected)
        slots = re.findall(r'\{SDL_SCANCODE_(\d),([A-Z_|]+)\}', PAD[PAD.index('wslot['):])
        self.assertEqual([k for k, _ in slots[:8]], [str(i) for i in range(1, 9)])

    def test_reserved_keys_are_not_default_bindings(self):
        reserved = ('SDL_SCANCODE_F8', 'SDL_SCANCODE_F9', 'SDL_SCANCODE_INSERT')
        defaults = re.findall(r'SDL_SCANCODE_\w+', PANEL[PANEL.index('kBindings[]'):PANEL.index('kPsNames')])
        for key in reserved:
            self.assertNotIn(key, defaults)

    def test_pitch_limit_is_gone(self):
        self.assertNotIn('pitch_lim', PAD)
        self.assertNotIn('mouse_pitch_limit', README)

    def test_ini_defaults_written_for_every_mouse_button(self):
        for key in ('mouse_left', 'mouse_right', 'mouse_middle', 'mouse_x1', 'mouse_x2',
                    'mouse_wheel_up', 'mouse_wheel_down'):
            self.assertIn(key + '=', PAD)


class BuildScript(unittest.TestCase):
    def test_script_syntax_and_pinned_versions(self):
        import subprocess
        script = ROOT / 'scripts' / 'build-windows.sh'
        subprocess.run(['bash', '-n', str(script)], check=True)
        text = script.read_text()
        for pin in ('SDL_TAG=', 'FMT_TAG=', 'XXHASH_TAG=', 'ZLIB_TAG=', 'ZYDIS_TAG=', 'CRYPTOPP_TAG=',
                    'ROBIN_TAG=', 'VKHEADERS_TAG=', 'VKLOADER_TAG='):
            self.assertIn(pin, text)

    def test_building_md_lists_every_pinned_dependency(self):
        text = (ROOT / 'scripts' / 'build-windows.sh').read_text()
        doc = (ROOT / 'BUILDING.md').read_text()
        for var, label in (('SDL_TAG', 'SDL3'), ('FMT_TAG', 'fmt'), ('XXHASH_TAG', 'xxHash'),
                           ('ZLIB_TAG', 'zlib')):
            tag = re.search(var + r'=(\S+)', text).group(1)
            self.assertIn(tag.lstrip('v'), doc, f'BUILDING.md does not mention {label} {tag}')


class Docs(unittest.TestCase):
    def test_environment_doc_is_current(self):
        import subprocess, sys
        r = subprocess.run([sys.executable, str(ROOT / 'scripts' / 'gen_env_docs.py'), '--check'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()


class EnvironmentDocTest(unittest.TestCase):
    def test_every_variable_has_a_purpose(self):
        """A new PS4RT_* variable needs a source comment or an OVERRIDES entry in scripts/gen_env_docs.py."""
        empty = []
        for line in (ROOT / 'docs/ENVIRONMENT.md').read_text().splitlines():
            if line.startswith('| `PS4RT_'):
                cells = [c.strip() for c in line.strip('|').split('|')]
                if not cells[1]:
                    empty.append(cells[0])
        self.assertEqual(empty, [], f'variables without a purpose: {empty}')
