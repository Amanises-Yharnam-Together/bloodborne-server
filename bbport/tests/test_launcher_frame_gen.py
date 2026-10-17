"""The launcher's frame generation settings (Supermedo's 026f8ff, 389124d) are the keys and values the
game reads from bbport.ini (gpu/shim/bbport_settings.cpp), with the same defaults.

Run: python -m pytest -q tests/test_launcher_frame_gen.py"""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'launcher'))
SETTINGS = (ROOT / 'gpu' / 'shim' / 'bbport_settings.cpp').read_text(encoding='utf-8')
SETTINGS_H = (ROOT / 'gpu' / 'shim' / 'bbport_settings.h').read_text(encoding='utf-8')

if sys.platform == 'win32':
    import bbport_launcher_win as launcher


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class FrameGenSettings(unittest.TestCase):
    def test_keys_the_game_reads(self):
        for key in ('frame_generation', 'frame_gen_provider', 'frame_gen_multiplier', 'frame_gen_fit_display'):
            self.assertIn(f'key == "{key}"', SETTINGS, key)
            self.assertIn(key, launcher.INI_DEFAULTS, key)

    def test_defaults_match_the_game(self):
        self.assertEqual(launcher.INI_DEFAULTS['frame_generation'], '0')
        self.assertEqual(launcher.INI_DEFAULTS['frame_gen_provider'], 'fsr3')
        self.assertEqual(launcher.INI_DEFAULTS['frame_gen_multiplier'], '2')
        self.assertEqual(launcher.INI_DEFAULTS['frame_gen_fit_display'], '1')
        self.assertIn('std::atomic<int> frame_gen_provider{FrameGenFsr3};', SETTINGS_H)
        self.assertIn('std::atomic<int> frame_gen_multiplier{2};', SETTINGS_H)
        self.assertIn('std::atomic<bool> frame_gen_fit_display{true};', SETTINGS_H)

    def test_values_the_game_takes(self):
        self.assertEqual([v for v, _t in launcher.FRAME_GEN_PROVIDERS], ['fsr3', 'dlss'])
        self.assertIn('value == "dlss" ? FrameGenDlss : FrameGenFsr3', SETTINGS)
        self.assertEqual([v for v, _t in launcher.FRAME_GEN_MULTIPLIERS], ['2', '3', '4'])
        self.assertTrue(re.search(r'frame_gen_multiplier = std::clamp\(i, 2, 4\)', SETTINGS))
        self.assertIn('frame_gen_fit_display', launcher.INI_FLAGS)
        self.assertNotIn('frame_gen_provider', launcher.INI_FLAGS)


if __name__ == '__main__':
    unittest.main()
