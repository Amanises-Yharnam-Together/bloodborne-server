"""The Windows launcher's Output resolution list (Kaniel 2026-10-24): the monitor's own resolutions, any
shape (21:9, 32:9, 16:10, 4:3), as the game's menus offer them (gpu/shim/bbport_output_sizes.h), and the
default output before one is chosen."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

import bbport_launcher_win as launcher  # noqa: E402


class OutputChoiceTests(unittest.TestCase):
    def values(self, *args):
        return [value for value, _label in launcher.output_choices(*args)]

    def test_the_monitors_modes_once_each_with_the_desktop_and_the_16x9_sizes_that_fit(self):
        modes = [(3440, 1440), (3440, 1440), (2560, 1080), (1920, 1080), (800, 600), (640, 480), (1024, 768),
                 (1720, 720), (1280, 720)]
        self.assertEqual(self.values(modes, (3440, 1440)),
                         ['1024x768', '1280x720', '1720x720', '1920x1080', '2560x1080', '2560x1440', '3440x1440'])

    def test_an_unknown_monitor_offers_the_four_standard_sizes(self):
        self.assertEqual(self.values([], None), ['1280x720', '1920x1080', '2560x1440', '3840x2160'])

    def test_the_current_value_is_kept_even_without_such_a_mode(self):
        self.assertIn('5120x1440', self.values([(1920, 1080)], (1920, 1080), '5120x1440'))
        self.assertEqual(self.values([], (1920, 1080), 'nonsense'), ['1280x720', '1920x1080'])

    def test_labels_name_the_shape(self):
        labels = dict(launcher.output_choices([(2560, 1080), (5120, 1440), (1920, 1200)], (2560, 1080)))
        self.assertEqual(labels['2560x1080'], ('2560 × 1080 (21:9)',))
        self.assertEqual(labels['1920x1080'], ('1920 × 1080 (16:9)',))
        self.assertEqual(launcher.aspect_name(5120, 1440), '32:9')
        self.assertEqual(launcher.aspect_name(1920, 1200), '16:10')
        self.assertEqual(launcher.aspect_name(1024, 768), '4:3')
        self.assertEqual(launcher.aspect_name(3840, 1600), '21:9')

    def test_the_default_output_is_1080_lines_in_the_monitors_shape(self):
        self.assertEqual(launcher.default_output(None), (1920, 1080))
        self.assertEqual(launcher.default_output((3840, 2160)), (1920, 1080))
        self.assertEqual(launcher.default_output((2560, 1080)), (2560, 1080))
        self.assertEqual(launcher.default_output((3440, 1440)), (2580, 1080))
        self.assertEqual(launcher.default_output((1920, 1200)), (1728, 1080))
        self.assertIn(launcher.INI_DEFAULTS['output_res'].count('x'), (1,))

    def test_the_aspect_ratio_setting(self):
        self.assertEqual([value for value, _t in launcher.ASPECTS], ['auto', '16:9'])
        self.assertEqual(launcher.INI_DEFAULTS['aspect_ratio'], 'auto')


if __name__ == '__main__':
    unittest.main()
