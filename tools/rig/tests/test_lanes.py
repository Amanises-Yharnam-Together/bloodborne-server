# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Lane claims (lanes.py): two threads never take one lane, a dead run's claim is taken over,
a lane with games that would not stop is skipped.

Run: py -3.12 -m unittest discover -s tools\\rig\\tests
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import lanes  # noqa: E402


class LaneClaimTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.rig = pathlib.Path(self.folder.name)
        for lane in (1, 2, 3):
            (self.rig / "lanes" / str(lane) / "server").mkdir(parents=True)
        (self.rig / "lanes" / "4").mkdir()  # never set up: no servers
        self.running = {100, 200, 300}

    def tearDown(self):
        self.folder.cleanup()

    def alive(self, pid: int) -> bool:
        return pid in self.running

    def test_the_lanes_set_up(self):
        self.assertEqual(lanes.available(self.rig), [0, 1, 2, 3])

    def test_a_held_lane_is_skipped(self):
        self.assertEqual(lanes.claim_free(self.rig, 2, "phase7", 100, self.alive), [0, 1])
        self.assertEqual(lanes.claim_free(self.rig, 2, "phase8", 200, self.alive), [2, 3])
        self.assertEqual(lanes.claim_free(self.rig, 1, "rig", 300, self.alive), [],
                         "every lane is taken")
        self.assertEqual(lanes.read_claim(self.rig, 2)["owner"], "phase8")

    def test_one_lane_one_owner(self):
        self.assertTrue(lanes.claim(self.rig, 1, "phase7", 100, self.alive))
        self.assertFalse(lanes.claim(self.rig, 1, "phase8", 200, self.alive))
        self.assertTrue(lanes.claim(self.rig, 1, "phase7", 100, self.alive), "already ours")

    def test_a_dead_runs_claim_is_taken_over(self):
        self.assertTrue(lanes.claim(self.rig, 1, "phase7", 100, self.alive))
        self.running.discard(100)
        self.assertTrue(lanes.claim(self.rig, 1, "phase8", 200, self.alive))
        self.assertEqual(lanes.read_claim(self.rig, 1)["pid"], 200)

    def test_release_frees_only_its_own(self):
        lanes.claim(self.rig, 1, "phase7", 100, self.alive)
        lanes.release(self.rig, 1, 200)
        self.assertIsNotNone(lanes.read_claim(self.rig, 1), "not the releaser's")
        lanes.release(self.rig, 1, 100)
        self.assertIsNone(lanes.read_claim(self.rig, 1))
        self.assertIn("lane 1: free", lanes.status(self.rig, self.alive))

    def test_a_blocked_lane_is_skipped_until_its_games_are_gone(self):
        lanes.block(self.rig, 2, [300])
        self.assertEqual(lanes.claim_free(self.rig, 4, "phase7", 100, self.alive), [0, 1, 3])
        self.assertIn("lane 2: blocked by stray games 300", lanes.status(self.rig, self.alive))
        self.running.discard(300)
        self.assertTrue(lanes.claim(self.rig, 2, "phase8", 200, self.alive))
        self.assertFalse((self.rig / "lanes" / "2" / "blocked").exists())

    def test_status_names_the_holder(self):
        lanes.claim(self.rig, 3, "phase8", 200, self.alive)
        [line] = [line for line in lanes.status(self.rig, self.alive) if line.startswith("lane 3")]
        self.assertRegex(line, r"^lane 3: held by phase8 \(pid 200, since \d{4}-")

    def test_a_lanes_strays_are_its_own_games_only(self):
        listing = ("25732\tC:\\bb\\out\\x\\bin\\bb-probe.exe C:\\bb\\rig\\port\\lanes\\2\\host\\out\\"
                   "boot-linked.bin\n"
                   "41\tC:\\bb\\out\\x\\bin\\bb-probe.exe C:\\bb\\rig\\port\\host\\out\\boot.bin\n"
                   "42\tC:\\bb\\out\\x\\bin\\bb-probe.exe C:\\bb\\rig\\port\\lanes\\12\\host\\x\n")
        lane2 = [pathlib.Path(r"C:\bb\rig\port\lanes\2\host"),
                 pathlib.Path(r"C:\bb\rig\port\lanes\2\guest")]
        self.assertEqual(lanes.strays(listing, lane2), [25732])
        self.assertEqual(lanes.strays(listing, [pathlib.Path(r"C:\bb\rig\port\host")]), [41])

    def test_this_process_is_alive(self):
        import os
        self.assertTrue(lanes.pid_alive(os.getpid()))
        self.assertFalse(lanes.pid_alive(0))


if __name__ == "__main__":
    unittest.main()
