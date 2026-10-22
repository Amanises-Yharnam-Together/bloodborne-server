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

    def test_online_unlocked_frame_rate_is_opt_in(self):
        # Kaniel 2026-10-22: an opt-in unlocked frame rate online. With it the chosen mode stays
        # (the send pace holds the game's packets to the 60 FPS mode's rates); 30 still stays.
        self.assertEqual(run.frame_mode('uncap', None, online=True, online_unlocked=True), 'uncap')
        # Kaniel 2026-10-22: a guest in the 90 mode made the enemies lag for everyone (90 FPS++ is a
        # fixed 90 FPS step: a PC that cannot hold 90 runs its game slower). Online it is the
        # unlocked mode (real frame time) capped at 90 instead.
        self.assertEqual(run.frame_mode('90', None, online=True, online_unlocked=True), 'uncap')
        self.assertEqual(run.frame_mode('60', '144', online=True, online_unlocked=True), 'uncap')
        self.assertEqual(run.frame_mode('60', None, online=True, online_unlocked=True), '60')
        self.assertEqual(run.frame_mode('30', None, online=True, online_unlocked=True), '30')
        # Offline the switch changes nothing.
        self.assertEqual(run.frame_mode('90', None, online=False, online_unlocked=True), '90')

    def test_online_unlocked_environment(self):
        # The game's frame cap online (at most 120: the movement timing breaks above it) and the
        # send pace, only for the modes above 60 FPS.
        self.assertEqual(run.online_frame_env('uncap', online=True, online_unlocked=True),
                         {'BB_ONLINE_FPS_CAP': '120', 'SHADPS4_BLOODBORNE_SEND_PACE': '1'})
        self.assertEqual(run.online_frame_env('uncap', online=True, online_unlocked=True, asked='90'),
                         {'BB_ONLINE_FPS_CAP': '90', 'SHADPS4_BLOODBORNE_SEND_PACE': '1'})
        self.assertEqual(run.online_frame_env('60', online=True, online_unlocked=True), {})
        self.assertEqual(run.online_frame_env('uncap', online=True, online_unlocked=False), {})
        self.assertEqual(run.online_frame_env('uncap', online=False, online_unlocked=True), {})
        # BB_SEND_PACE=0: the cap without the pace, for comparisons (the rig's fps-pace runs).
        self.assertEqual(run.online_frame_env('uncap', online=True, online_unlocked=True, pace='0'),
                         {'BB_ONLINE_FPS_CAP': '120'})


if __name__ == '__main__':
    unittest.main()
