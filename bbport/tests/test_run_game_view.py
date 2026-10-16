"""run.py's game view (scripts/mods.py): when it cannot be made with the port's own layers (the button icons, the
party HUD), the game starts without them instead of not at all, as it did before they existed. From Supermedo's
bbport (bf0878b, 9461312: their Quit to Desktop and HUD layout layers; ours are the icons and the party HUD)."""
from paths import ROOT
import importlib.util
from pathlib import Path
import subprocess
import types
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('bbport_run_view', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)

GAME, OUT, DATA = Path('game'), Path('out'), Path('data')


class GameViewTests(unittest.TestCase):
    def view(self, env, icons, results):
        calls = []

        def run_script(name, *args, capture=False, optional=False):
            calls.append((name, [str(a) for a in args], capture, optional))
            return results[len(calls) - 1]
        with mock.patch.object(run, 'run_script', run_script):
            view = run.game_view(env, GAME, OUT, DATA, icons)
        return view, calls

    def test_the_view_with_the_icons_and_the_party_hud(self):
        env = {}
        view, calls = self.view(env, ['--icons-layer', 'icons'], ['view\n'])
        self.assertEqual(view, Path('view'))
        self.assertEqual(len(calls), 1)
        name, args, capture, optional = calls[0]
        self.assertEqual(name, 'mods.py')
        self.assertIn('--party-hud', args)
        self.assertEqual(args[args.index('--icons-layer') + 1], 'icons')
        self.assertTrue(capture and optional)
        self.assertNotIn('BB_PARTY_HUD', env)

    def test_without_its_layers_when_the_view_cannot_be_made_with_them(self):
        env = {'BB_ICONS_SET': 'xbox'}
        with mock.patch('builtins.print') as printed:
            view, calls = self.view(env, ['--icons-layer', 'icons'], [None, 'plain view\n'])
        self.assertEqual(view, Path('plain view'))
        self.assertEqual(len(calls), 2)
        _, args, capture, optional = calls[1]
        self.assertNotIn('--party-hud', args)
        self.assertNotIn('--icons-layer', args)
        self.assertEqual(args[:3], ['game', '--out', 'out'])
        self.assertTrue(capture)
        self.assertFalse(optional)  # the player's own mods failing still stops the start, with mods.py's reason
        # The overlay draws the downed banner itself, and the glyphs are the game's own.
        self.assertEqual(env['BB_PARTY_HUD'], '0')
        self.assertEqual(env['BB_ICONS_SET'], 'playstation')
        self.assertIn('starting without', ' '.join(str(a) for c in printed.call_args_list for a in c.args))

    def test_nothing_to_leave_out_means_one_try_that_must_work(self):
        view, calls = self.view({'BB_PARTY_HUD': '0'}, [], ['view\n'])
        self.assertEqual(view, Path('view'))
        self.assertEqual(len(calls), 1)
        self.assertFalse(calls[0][3])


class OptionalScriptTests(unittest.TestCase):
    def test_an_optional_script_that_fails_returns_none(self):
        failed = types.SimpleNamespace(returncode=1, stdout='')
        with mock.patch.object(run.subprocess, 'run', return_value=failed):
            self.assertIsNone(run.run_script('mods.py', 'x', capture=True, optional=True))

    def test_a_required_script_that_fails_stops_the_start(self):
        failed = types.SimpleNamespace(returncode=3, stdout='')
        with mock.patch.object(run.subprocess, 'run', return_value=failed):
            with self.assertRaises(SystemExit) as stop:
                run.run_script('mods.py', 'x', capture=True)
        self.assertEqual(stop.exception.code, 3)

    def test_an_optional_script_that_works_returns_its_output(self):
        done = types.SimpleNamespace(returncode=0, stdout='view\n')
        with mock.patch.object(run.subprocess, 'run', return_value=done) as called:
            self.assertEqual(run.run_script('mods.py', 'x', capture=True, optional=True), 'view\n')
        self.assertIs(called.call_args.kwargs['stdout'], subprocess.PIPE)


if __name__ == '__main__':
    unittest.main()
