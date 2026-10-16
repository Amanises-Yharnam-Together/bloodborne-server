"""The launcher's look (Kaniel 2026-10-16: "redesign launcher to not use bloodborne native images and look
more like a game"): it shows no picture of the game (the sce_sys cover art and icon of the player's copy),
and the pages are a game-style menu down the side, all of them in the Advanced view and Play alone in the
Simple one."""
import json
import sys
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

if sys.platform == 'win32':
    import bbport_coop
    import bbport_launcher_win as launcher

# A 1x1 PNG: a valid picture wherever one is opened.
PNG = bytes.fromhex('89504e470d0a1a0a0000000d4948445200000001000000010806000000'
                    '1f15c4890000000d49444154789c6360f8cfc0f01f0005000201a3a9b0'
                    'e20000000049454e44ae426082')


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class LookTests(unittest.TestCase):
    def setUp(self):
        import tempfile
        import tkinter
        from tkinter import filedialog, messagebox, ttk
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        scratch = Path(self.tmp.name)
        self.game = scratch / 'CUSA03173'
        (self.game / 'sce_sys').mkdir(parents=True)
        for name in ('pic0.png', 'pic1.png', 'icon0.png'):
            (self.game / 'sce_sys' / name).write_bytes(PNG)
        (scratch / 'config').mkdir()
        (scratch / 'config' / 'settings.json').write_text(
            json.dumps({'check_updates': False, 'game_dir': str(self.game), 'ui_advanced': True}), encoding='utf-8')
        self.opened = []
        from PIL import Image
        real_open = Image.open

        def recording_open(path, *args, **kwargs):
            self.opened.append(str(path))
            return real_open(path, *args, **kwargs)

        for patch in (mock.patch.dict(launcher.os.environ, {'BB_CONFIG': str(scratch / 'bbport.ini'),
                                                            'BB_LAUNCHER_ANIMATIONS': '0'}),
                      mock.patch.object(launcher, 'CONFIG_FILE', scratch / 'config' / 'settings.json'),
                      mock.patch.object(launcher, 'CONFIG_DIR', scratch / 'config'),
                      mock.patch.object(launcher, 'DATA_DIR', scratch),
                      mock.patch.object(launcher, 'connected_displays', return_value=[]),
                      mock.patch.object(launcher, 'gpu_tool', return_value=(scratch / 'no-such.exe', {})),
                      mock.patch.object(launcher, 'LANG', 'en'),
                      mock.patch.object(launcher, 'COOP_INSTALL', False),
                      mock.patch.object(bbport_coop, 'search_game', return_value=None),
                      mock.patch.object(Image, 'open', side_effect=recording_open)):
            patch.start()
            self.addCleanup(patch.stop)
        for _attempt in range(5):  # Tk now and then cannot find its scripts right after another window closed
            try:
                self.root = tkinter.Tk()
                break
            except tkinter.TclError:
                time.sleep(0.5)
        else:
            self.skipTest('no display for Tk')
        self.addCleanup(self.root.destroy)
        self.app = launcher.Launcher(self.root, tkinter, ttk, filedialog, messagebox)
        self.settle()

    def settle(self, seconds=0.6):
        end = time.time() + seconds
        while time.time() < end:
            self.root.update()
            time.sleep(0.02)

    def test_no_picture_of_the_game_is_shown(self):
        self.app.start_motion()
        self.settle()
        tk_files = [self.root.tk.call(name, 'cget', '-file') for name in self.root.tk.call('image', 'names')
                    if self.root.tk.call('image', 'type', name) == 'photo']
        game = str(self.game).lower()
        for path in self.opened + [str(f) for f in tk_files]:
            self.assertFalse(str(path).lower().startswith(game), f'game art shown: {path}')

    def test_the_side_menu_lists_every_page_in_the_advanced_view(self):
        side = self.app.side
        self.assertEqual(side.visible(), [name for name, _t in self.app.tab_names])
        self.app.set_mode(False)
        self.settle()
        self.assertEqual(side.visible(), ['play'], 'the Simple view: Play alone')
        self.app.set_mode(True)
        self.settle()
        self.assertEqual(len(side.visible()), len(self.app.tab_names))

    def test_a_click_on_an_entry_shows_its_page(self):
        side = self.app.side
        y = side.entry_y(side.visible().index('graphics'))
        side.pick(y)
        self.settle()
        self.assertEqual(self.app.current_page, 'graphics')
        self.assertEqual(side.active, 'graphics')
        side.pick(side.entry_y(0))
        self.settle()
        self.assertEqual(self.app.current_page, 'play')

    def test_the_window_is_tall_enough_for_the_menu(self):
        self.root.update_idletasks()
        self.app.fit_window()
        self.settle()
        self.assertGreaterEqual(self.root.winfo_height() + 2, min(self.app.side.min_height(),
                                                                  launcher.work_area((self.root.winfo_screenwidth(),
                                                                                      self.root.winfo_screenheight()))[3]
                                                                  - self.app.frame_extra()))


    def test_the_launcher_draws_its_own_window_buttons(self):
        """Kaniel 2026-10-16: the launcher's own minimize, maximize and close instead of the Windows caption."""
        import ctypes
        self.app.start_motion()
        self.settle()
        self.assertTrue(self.root.overrideredirect(), 'no Windows caption')
        self.assertEqual(set(self.app.caption_buttons), {'min', 'max', 'close'})
        user32 = ctypes.windll.user32
        user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
        user32.GetWindowLongPtrW.argtypes = [ctypes.c_void_p, ctypes.c_int]
        exstyle = user32.GetWindowLongPtrW(self.app.chrome.hwnd(), -20)
        self.assertTrue(exstyle & 0x00040000, 'still a taskbar button (WS_EX_APPWINDOW)')
        before = self.root.geometry()
        self.app.caption_buttons['max'].command()
        self.settle(0.3)
        self.assertTrue(self.app.chrome.zoomed())
        self.assertNotEqual(self.root.geometry(), before)
        self.app.caption_buttons['max'].command()
        self.settle(0.3)
        self.assertEqual(self.root.geometry(), before, 'restored where it was')
        self.app.caption_buttons['min'].command()
        self.settle(0.3)
        self.assertTrue(user32.IsIconic(ctypes.c_void_p(self.app.chrome.hwnd())), 'minimized')
        user32.ShowWindow(ctypes.c_void_p(self.app.chrome.hwnd()), 9)


if __name__ == '__main__':
    unittest.main()
