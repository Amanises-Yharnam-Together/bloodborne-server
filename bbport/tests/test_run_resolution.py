"""run.py asks patches.py for the startup output the way the game resolves it (Kaniel 2026-10-26: players
on 21:9 monitors in borderless got a scene patched for the ini's 16:9 size while the game showed the
desktop's shape): the game's display (BB_DISPLAY, as gpu/shim/window.cpp picks it) and its window mode."""
from paths import ROOT
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('bbrun_resolution', ROOT / 'run.py')
run_py = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run_py)

# bb-gpu-capabilities --displays: number, name, desktop, refresh, primary.
DISPLAYS = '1\tDELL U2720Q\t3840x2160\t60\t0\n2\tLG 34GN850\t3440x1440\t144\t1\n3\tSteam Deck\t1280x800\t60\t0\n'


class GameDisplayTests(unittest.TestCase):
    def test_the_primary_display_without_bb_display(self):
        self.assertEqual(run_py.choose_desktop(DISPLAYS, ''), (3440, 1440))

    def test_bb_display_by_number_or_part_of_its_name(self):
        self.assertEqual(run_py.choose_desktop(DISPLAYS, '1'), (3840, 2160))
        self.assertEqual(run_py.choose_desktop(DISPLAYS, ' 3 '), (1280, 800))
        self.assertEqual(run_py.choose_desktop(DISPLAYS, 'dell'), (3840, 2160))
        # No such display: the primary one, as the game does.
        self.assertEqual(run_py.choose_desktop(DISPLAYS, '9'), (3440, 1440))
        self.assertEqual(run_py.choose_desktop(DISPLAYS, 'Samsung'), (3440, 1440))

    def test_unknown_desktops(self):
        self.assertIsNone(run_py.choose_desktop('', ''))
        self.assertIsNone(run_py.choose_desktop('1\tX\t0x0\t0\t1\n', ''))
        self.assertIsNone(run_py.choose_desktop('garbage\n', ''))


class WindowModeTests(unittest.TestCase):
    def mode(self, ini, env=None):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bbport.ini'
            path.write_text(ini, encoding='utf-8')
            return run_py.game_window_mode(path, env or {})

    def test_as_the_game_settings_read_it(self):
        self.assertEqual(self.mode(''), 'windowed')
        self.assertEqual(self.mode('window_mode=borderless\n'), 'borderless')
        # The launcher's fullscreen switch until the game's menu wrote a window mode.
        self.assertEqual(self.mode('', {'BB_FULLSCREEN': '1'}), 'borderless')
        self.assertEqual(self.mode('window_mode=windowed\n', {'BB_FULLSCREEN': '1'}), 'windowed')
        self.assertEqual(self.mode('window_mode=windowed\n', {'BB_WINDOW_MODE': 'fullscreen'}), 'fullscreen')


PATCHES = '''
import json, os, sys
from pathlib import Path

def game_app_version(game):
    return None

def game_is_patched(version, env=os.environ):
    return True

PATCH_GROUPS = {"fps": [], "resolution": [], "effects": [], "fixes": []}

def group_list(groups):
    return ", ".join(sorted(groups))

def matching_groups(elf_path):
    return set(PATCH_GROUPS)

if __name__ == "__main__":
    if "--print-scaled" in sys.argv or "--print-output" in sys.argv:
        with open("asked.txt", "a") as asked:
            asked.write(" ".join(sys.argv[1:]) + "\\n")
        if "--print-output" in sys.argv:
            print("2560x1080")
        else:
            print("2560x1080 2560x1080" if "borderless" in sys.argv else "")
    else:
        Path("observed.json").write_text(json.dumps({k: v for k, v in os.environ.items() if k.startswith("BB_")}))
        sys.exit(73)
'''


@unittest.skipUnless(os.name == 'nt', 'run.py needs Windows')
class StartupOutputTests(unittest.TestCase):
    def start(self, ini, **env_extra):
        with tempfile.TemporaryDirectory(prefix='bb resolution ') as directory:
            root = Path(directory)
            shutil.copyfile(ROOT / 'run.py', root / 'run.py')
            (root / 'eboot.bin').touch()
            (root / 'bb-probe.exe').touch()
            (root / 'bbport.ini').write_text(ini, encoding='utf-8')
            scripts = root / 'scripts'
            scripts.mkdir()
            (scripts / 'mods.py').write_text('import sys\nprint(sys.argv[1])\n')
            for name in ('prepare', 'link_libc', 'link_modules', 'content_profile'):
                (scripts / f'{name}.py').write_text('pass\n')
            (scripts / 'patches.py').write_text(PATCHES)
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('BB_', 'STUB_'))}
            env.update(BB_GAME_DIR=str(root), BB_DATA_DIR=str(root), BB_PROBE=str(root / 'bb-probe.exe'),
                       BB_CONFIG=str(root / 'bbport.ini'), **env_extra)
            run = subprocess.run([sys.executable, 'run.py'], cwd=root, env=env, capture_output=True,
                                 text=True, encoding='utf-8', timeout=60)
            self.assertEqual(run.returncode, 73, run.stdout + run.stderr)
            observed = json.loads((root / 'observed.json').read_text(encoding='utf-8'))
            return (root / 'asked.txt').read_text(encoding='utf-8'), observed, run.stdout

    def test_the_window_mode_and_desktop_go_to_the_startup_output(self):
        asked, observed, log = self.start('output_res=1920x1080\naspect_ratio=auto\n', BB_FULLSCREEN='1',
                                          BB_DESKTOP='2560x1080')
        self.assertIn('--print-scaled', asked)
        self.assertIn('--window-mode borderless', asked)
        self.assertIn('--desktop 2560x1080', asked)
        self.assertEqual((observed['BB_RENDER_RES'], observed['BB_OUTPUT_RES']), ('2560x1080', '2560x1080'))
        self.assertIn('Resolution at start: ini 1920x1080, window borderless, desktop 2560x1080 -> output '
                      '2560x1080, scene 2560x1080 (startup patch)', log)

    def test_windowed_keeps_the_choice(self):
        asked, observed, log = self.start('output_res=1920x1080\naspect_ratio=auto\n', BB_DESKTOP='2560x1080')
        self.assertIn('--window-mode windowed', asked)
        self.assertNotIn('BB_OUTPUT_RES', observed)


if __name__ == '__main__':
    unittest.main()
