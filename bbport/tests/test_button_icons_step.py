"""scripts/button_icons.py: the button icons of one start, decided by the launcher's Play or, without it, by
run.py (the kit's PLAY.bat, a restart from the game's menu after the icon style changed there; Kaniel
2026-10-11)."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'launcher'))

try:
    import PIL  # noqa: F401  (bbport_icons draws with Pillow)
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False

import button_icons  # noqa: E402


class ReadIniTests(unittest.TestCase):
    def test_keys_values_comments_and_a_notepad_bom(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bbport.ini'
            path.write_bytes('﻿# settings\nbutton_icons = keyboard\nkey.cross=Ctrl+F, Mouse X1\nbroken\n'.encode())
            self.assertEqual(button_icons.read_ini(path), {'button_icons': 'keyboard', 'key.cross': 'Ctrl+F, Mouse X1'})
            self.assertEqual(button_icons.read_ini(Path(folder) / 'missing.ini'), {})


@unittest.skipUnless(HAVE_PIL, 'the icons need Pillow')
class EnvironmentTests(unittest.TestCase):
    def setUp(self):
        import bbport_icons
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.game = self.root / 'game'
        (self.game / 'dvdroot_ps4/menu').mkdir(parents=True)
        (self.game / 'dvdroot_ps4/menu/common.tpf.dcx').write_bytes(b'x')
        self.layer = self.root / 'out/icons/layer'
        self.ensure = mock.Mock(return_value=(self.layer, True, 0))
        self.prompts = mock.Mock(return_value=(self.root / 'out/icons/prompts-1.bin', True))
        for patch in (mock.patch.object(bbport_icons, 'ensure_layer', self.ensure),
                      mock.patch.object(bbport_icons, 'ensure_prompt_sets', self.prompts)):
            patch.start()
            self.addCleanup(patch.stop)

    def environment(self, ini, pads=None):
        return button_icons.environment(self.game, self.root / 'out', ini, self.root / 'mods', self.root / 'mods.json',
                                        '1', '', lambda: pads)

    def test_keyboard_draws_the_bound_keys(self):
        ini = {'button_icons': 'keyboard', 'key.cross': 'Ctrl+F'}
        env = self.environment(ini)
        self.assertEqual(env['BB_ICONS_DIR'], str(self.layer))
        self.assertEqual(self.ensure.call_args.args[2], 'keyboard')
        self.assertEqual(self.ensure.call_args.args[3], ini)  # the glyphs follow the bindings

    def test_automatic_follows_the_device_in_use_from_the_prompt_sets(self):
        # No layer: the game's own icons are swapped while it runs (bbport_prompts.cpp), whatever is connected.
        env = self.environment({'key.cross': 'F'}, pads=[])
        self.assertEqual(env['BB_PROMPT_SETS'], str(self.root / 'out/icons/prompts-1.bin'))
        self.assertNotIn('BB_ICONS_DIR', env)
        self.ensure.assert_not_called()
        self.assertEqual(self.prompts.call_args.args[2], {'key.cross': 'F'})  # the keyboard set follows bindings
        self.assertIn('follow the device', env['BB_ICONS_NOTE'])

    def test_switch_draws_its_layer(self):
        self.environment({'button_icons': 'switch'})
        self.assertEqual(self.ensure.call_args.args[2], 'switch')

    def test_playstation_is_the_game_s_own(self):
        self.assertEqual(self.environment({'button_icons': 'playstation'}), {})
        self.ensure.assert_not_called()

    def test_an_unknown_choice_is_automatic(self):
        self.assertIn('BB_PROMPT_SETS', self.environment({'button_icons': 'nintendo'}, pads=[]))

    def test_a_failure_leaves_the_game_s_icons_with_the_reason(self):
        self.ensure.side_effect = RuntimeError('broken atlas')
        env = self.environment({'button_icons': 'xbox'})
        self.assertNotIn('BB_ICONS_DIR', env)
        self.assertIn('broken atlas', env['BB_ICONS_NOTE'])


class RunPyTests(unittest.TestCase):
    """run.py decides the icons itself when the launcher did not, and takes the launcher's choice out of the
    environment the game inherits, so the restart from the game's menu decides again."""

    def setUp(self):
        self.source = (ROOT / 'run.py').read_text(encoding='utf-8')

    def test_the_icon_set_follows_the_choice_and_the_controller(self):
        xbox = [('030000005e040000e002000000007200', 'Xbox Wireless Controller')]
        playstation = [('030000004c050000c405000000006800', 'Wireless Controller')]
        self.assertEqual(button_icons.icon_set({'button_icons': 'keyboard'}), 'keyboard')
        self.assertEqual(button_icons.icon_set({'button_icons': 'xbox'}), 'xbox')
        self.assertEqual(button_icons.icon_set({'button_icons': 'switch'}), 'switch')
        # Automatic: the overlay follows the device in use, as the game's icons do.
        self.assertEqual(button_icons.icon_set({}, gamepads=lambda: xbox), 'auto')
        self.assertEqual(button_icons.icon_set({}, gamepads=lambda: playstation), 'auto')
        self.assertEqual(button_icons.icon_set({}), 'auto')

    def test_the_launcher_s_choice_is_taken_out_of_the_environment(self):
        self.assertIn("env.pop('BB_ICONS_DIR', None)", self.source)
        self.assertIn("env.pop('BB_ICONS_NOTE', None)", self.source)
        self.assertIn("env.pop('BB_PROMPT_SETS', None)", self.source)
        self.assertIn("env['BB_PROMPT_SETS'] = prompt_sets", self.source)

    def test_without_it_run_py_asks_the_icon_step(self):
        self.assertIn('button_icons.environment(', self.source)
        self.assertIn("env.get('BB_ICONS', '1') != '0'", self.source)  # the rig switches it off

    def test_the_layer_goes_to_the_mods_script(self):
        self.assertIn("['--icons-layer', icons_dir]", self.source)


if __name__ == '__main__':
    unittest.main()
