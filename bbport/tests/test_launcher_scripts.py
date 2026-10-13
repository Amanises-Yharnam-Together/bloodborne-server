"""BLauncher.exe runs the preparation scripts (--script): each must import its own sibling modules from
its own folder, as python does, not copies frozen into the launcher. The release's launcher carries a
frozen hud_patch (mods is imported for the mod list); run_path left the scripts folder off sys.path, so
mods.py's `import hud_patch` took the frozen one, which looked for party_hud_icons.bin in the
launcher's own folder and patched the HUD without the party list's icons and portraits (Kaniel
2026-10-13: no portrait left of a teammate's health bar)."""
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

if sys.platform == 'win32':
    import bbport_launcher_win as launcher


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class LauncherScriptTests(unittest.TestCase):
    def test_a_script_imports_its_own_siblings_before_any_other_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scripts, elsewhere = root / 'scripts', root / 'frozen'
            scripts.mkdir()
            elsewhere.mkdir()
            # A same-named module the launcher would find first (as its frozen copy).
            (elsewhere / 'sibling_for_test.py').write_text("WHERE = 'frozen'\n", encoding='utf-8')
            (scripts / 'sibling_for_test.py').write_text("WHERE = 'scripts'\n", encoding='utf-8')
            out = root / 'where.txt'
            (scripts / 'probe.py').write_text(textwrap.dedent(f'''
                import sibling_for_test
                open({str(out)!r}, 'w').write(sibling_for_test.WHERE)
            '''), encoding='utf-8')
            sys.modules.pop('sibling_for_test', None)
            with mock.patch.object(sys, 'path', [str(elsewhere), *sys.path]), \
                    mock.patch.object(launcher, 'attach_stdio', lambda: None):
                code = launcher.run_role(['--script', str(scripts / 'probe.py')])
            sys.modules.pop('sibling_for_test', None)
            self.assertEqual(code, 0)
            self.assertEqual(out.read_text(encoding='utf-8'), 'scripts')

    def test_the_hud_patch_finds_its_icon_strip_when_the_launcher_runs_it(self):
        scripts = Path(__file__).resolve().parents[1] / 'scripts'
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'icons.txt'
            probe = Path(directory) / 'probe.py'
            probe.write_text(textwrap.dedent(f'''
                import sys
                sys.path.insert(0, {str(scripts)!r})
                import hud_patch
                open({str(out)!r}, 'w').write(str(hud_patch.HUD_ICONS.is_file()))
            '''), encoding='utf-8')
            with mock.patch.object(launcher, 'attach_stdio', lambda: None):
                launcher.run_role(['--script', str(probe)])
            self.assertEqual(out.read_text(encoding='utf-8'), 'True')


if __name__ == '__main__':
    unittest.main()
