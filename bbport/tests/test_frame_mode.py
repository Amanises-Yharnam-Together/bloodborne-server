"""run.py's frame rate mode: the in-game FPS limit (bbport.ini fps_limit) needs the unlocked mode."""
from paths import ROOT
import importlib.util
import unittest

spec = importlib.util.spec_from_file_location('bbport_run', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class FrameModeTests(unittest.TestCase):
    def test_an_fps_limit_unlocks_the_frame_rate(self):
        # Kaniel 2026-10-10: 240 set in the game's menu, still 60 on top (the kit's 60 FPS mode is
        # the game's own fixed 60 FPS timestep, which no cap can raise).
        self.assertEqual(run.frame_mode('60', '240'), 'uncap')
        self.assertEqual(run.frame_mode('60', '144'), 'uncap')

    def test_no_limit_keeps_the_chosen_mode(self):
        self.assertEqual(run.frame_mode('60', None), '60')
        self.assertEqual(run.frame_mode('60', '0'), '60')
        self.assertEqual(run.frame_mode('uncap', None), 'uncap')
        self.assertEqual(run.frame_mode('60', 'nonsense'), '60')

    def test_the_30_fps_mode_stays(self):
        # 30 is chosen for the shadPS4-like timing or an unpatched game: a cap does not change it.
        self.assertEqual(run.frame_mode('30', '240'), '30')


    def test_online_play_runs_at_60(self):
        # Kaniel 2026-10-13: hosting uncapped (100-200 FPS) made the guests see everyone freeze and
        # fly and their hits land late; the host at 60 fixed it. Online, the high frame rate modes
        # and an FPS limit give the fixed 60 FPS mode; 30 stays.
        self.assertEqual(run.frame_mode('uncap', None, online=True), '60')
        self.assertEqual(run.frame_mode('90', None, online=True), '60')
        self.assertEqual(run.frame_mode('60', '240', online=True), '60')
        self.assertEqual(run.frame_mode('60', None, online=True), '60')
        self.assertEqual(run.frame_mode('30', '240', online=True), '30')
        self.assertEqual(run.frame_mode('uncap', None, online=False), 'uncap')


if __name__ == '__main__':
    unittest.main()
