"""Restore saves in the co-op settings (Kaniel 2026-10-25: "don't we have backups? if no let's do that"):
the game's and the launcher's backups listed newest first; Restore puts the chosen one back after asking,
and not while the game runs."""
import ast
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

import bbport_lang  # noqa: E402

if sys.platform == 'win32':
    import bbport_coop
    import bbport_launcher_win as launcher

LAUNCHER = Path(__file__).resolve().parents[1] / 'launcher' / 'bbport_launcher_win.py'
SLOT = Path('savedata/1/CUSA03173/SPRJ0005/userdata0000')


class TextTests(unittest.TestCase):
    def test_restore_saves_is_translated_everywhere(self):
        source = LAUNCHER.read_text(encoding='utf-8')
        names = {'open_save_backups', 'save_backup_line'}
        pieces = [f for f in ast.walk(ast.parse(source)) if isinstance(f, ast.FunctionDef) and f.name in names]
        self.assertEqual({f.name for f in pieces}, names)
        texts = {n.args[0].value for f in pieces for n in ast.walk(f) if isinstance(n, ast.Call)
                 and isinstance(n.func, ast.Name) and n.func.id == '_' and n.args and isinstance(n.args[0], ast.Constant)}
        texts.add('Restore saves…')
        self.assertGreater(len(texts), 12)
        self.assertIn("_('Restore saves…', 'Восстановить сохранения…'),\n                    self.open_save_backups",
                      source, 'the co-op card opens it')
        for language in ('ru', 'ar', 'es', 'pt', 'fr', 'de', 'it', 'pl', 'tr', 'zh', 'ja', 'ko'):
            table = bbport_lang.table(language)
            for text in texts:
                if language == 'ru' and text in bbport_lang.KEYS:
                    continue  # the launcher's older texts carry their Russian beside them in the code
                self.assertTrue(table.get(text), f'{language}: {text}')
                self.assertEqual(table[text].count('{}'), text.count('{}'), f'{language}: {text}')


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class DialogTests(unittest.TestCase):
    def setUp(self):
        import tkinter
        from tkinter import filedialog, messagebox, ttk
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        scratch = Path(self.tmp.name)
        self.install = scratch / 'install'
        self.user = self.install / 'user'
        (scratch / 'config').mkdir()
        (scratch / 'config' / 'settings.json').write_text(json.dumps({'check_updates': False}), encoding='utf-8')
        self.messagebox = mock.Mock(askyesno=mock.Mock(return_value=True))
        for patch in (mock.patch.dict(launcher.os.environ, {'BB_CONFIG': str(scratch / 'bbport.ini'),
                                                            'BB_LAUNCHER_ANIMATIONS': '0'}),
                      mock.patch.object(launcher, 'CONFIG_FILE', scratch / 'config' / 'settings.json'),
                      mock.patch.object(launcher, 'CONFIG_DIR', scratch / 'config'),
                      mock.patch.object(launcher, 'DATA_DIR', scratch),
                      mock.patch.object(launcher, 'PORT_DIR', self.install),
                      mock.patch.object(launcher, 'connected_displays', return_value=[]),
                      mock.patch.object(launcher, 'gpu_tool', return_value=(scratch / 'no-such.exe', {})),
                      mock.patch.object(launcher, 'LANG', 'en'),
                      mock.patch.object(launcher, 'COOP_INSTALL', False),
                      mock.patch.object(bbport_coop, 'search_game', return_value=None),
                      mock.patch.object(launcher, 'fetch_news', return_value=None),
                      mock.patch.object(launcher, 'fetch_patch_notes', return_value=None)):
            patch.start()
            self.addCleanup(patch.stop)
        for _attempt in range(5):
            try:
                self.root = tkinter.Tk()
                break
            except tkinter.TclError:
                time.sleep(0.5)
        else:
            self.skipTest('no display for Tk')
        self.addCleanup(self.root.destroy)
        self.app = launcher.Launcher(self.root, tkinter, ttk, filedialog, self.messagebox)
        self.root.update()

    def write(self, relative, text, under=None):
        path = (under or self.user) / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def test_the_backups_are_listed_newest_first(self):
        self.write(SLOT, 'old', self.user / 'save-backups' / '20261024-120000')
        self.write(SLOT, 'new', self.user / 'save-backups' / '20261024-231630')
        (self.user / 'save-backups' / '20261024-231630' / 'launcher.txt').write_text('before PLAY', encoding='utf-8')
        win = self.app.open_save_backups()
        lines = win.parts['listing'].get(0, 'end')
        self.assertEqual(len(lines), 2)
        self.assertIn('2026-10-24 23:16', lines[0])
        self.assertIn('Before PLAY', lines[0])
        self.assertIn('While playing', lines[1])
        self.assertEqual(win.parts['listing'].curselection(), (0,))
        self.assertEqual(win.parts['restore'].state, 'normal')

    def test_no_backups_says_when_the_first_comes(self):
        win = self.app.open_save_backups()
        self.assertEqual(win.parts['listing'].size(), 0)
        self.assertIn('PLAY', win.parts['note'].cget('text'))
        self.assertEqual(win.parts['restore'].state, 'disabled')

    def test_restore_asks_then_puts_the_backup_back(self):
        self.write(SLOT, 'before the broken altar', self.user / 'save-backups' / '20261024-231630')
        self.write(SLOT, 'broken altar')
        win = self.app.open_save_backups()
        win.parts['restore'].command()
        self.messagebox.askyesno.assert_called_once()
        self.assertEqual((self.user / SLOT).read_text(encoding='utf-8'), 'before the broken altar')
        self.assertEqual(win.parts['listing'].size(), 2, 'the saves it replaced are listed too')
        self.assertIn('2026-10-24 23:16', win.parts['note'].cget('text'))

    def test_no_restore_while_the_game_runs(self):
        self.write(SLOT, 'before', self.user / 'save-backups' / '20261024-231630')
        self.write(SLOT, 'now')
        self.app.process = mock.Mock()
        win = self.app.open_save_backups()
        win.parts['restore'].command()
        self.messagebox.askyesno.assert_not_called()
        self.assertEqual((self.user / SLOT).read_text(encoding='utf-8'), 'now')
        self.app.process = None


if __name__ == '__main__':
    unittest.main()
