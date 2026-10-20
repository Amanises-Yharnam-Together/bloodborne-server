# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for the PC's build lock (build_lock.py): one build at a time, never beside games.

Run: py -3.12 -m unittest discover -s tools\\rig\\tests
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import build_lock  # noqa: E402


def gone_pid() -> int:
    """A process that has ended."""
    process = subprocess.Popen([sys.executable, "-c", "pass"])
    process.wait()
    return process.pid


class BuildLockTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.lock = pathlib.Path(self.folder.name) / "build.lock"

    def tearDown(self):
        self.folder.cleanup()

    def test_a_build_holds_it_until_it_lets_go(self):
        build_lock.take("emu", os.getpid(), self.lock, games=lambda: [], sleep=self.fail)
        self.assertEqual(build_lock.holder(self.lock), ("emu", os.getpid()))
        self.assertFalse(build_lock.try_take("port", os.getppid(), self.lock),
                         "a second build waits")
        build_lock.release(os.getppid(), self.lock)
        self.assertIsNotNone(build_lock.holder(self.lock), "only its holder lets go")
        build_lock.release(os.getpid(), self.lock)
        self.assertIsNone(build_lock.holder(self.lock))

    def test_a_killed_builds_lock_is_taken_over(self):
        self.lock.write_text(f"old\n{gone_pid()}\nthen\n", encoding="utf-8")
        self.assertIsNone(build_lock.holder(self.lock))
        self.assertTrue(build_lock.try_take("new", os.getpid(), self.lock))
        self.assertEqual(build_lock.holder(self.lock), ("new", os.getpid()))

    def test_a_build_waits_for_the_games(self):
        # Kaniel 2026-10-20: builds beside rig games froze his PC.
        seen = iter([[1234, 5678], [1234], []])
        naps = []
        with unittest.mock.patch.dict(os.environ, {"BB_BUILD_BESIDE_GAMES": ""}):
            build_lock.take("port", os.getpid(), self.lock, games=lambda: next(seen),
                            sleep=naps.append)
        self.assertEqual(len(naps), 2)
        self.assertEqual(build_lock.holder(self.lock), ("port", os.getpid()))

    def test_a_player_may_let_a_build_run_beside_games(self):
        with unittest.mock.patch.dict(os.environ, {"BB_BUILD_BESIDE_GAMES": "1"}):
            build_lock.take("port", os.getpid(), self.lock, games=lambda: [1234],
                            sleep=self.fail)
        self.assertEqual(build_lock.holder(self.lock), ("port", os.getpid()))

    def test_a_game_waits_for_the_build(self):
        self.lock.write_text(f"emu\n{os.getpid()}\nnow\n", encoding="utf-8")
        naps = []

        def nap(seconds):
            naps.append(seconds)
            if len(naps) == 3:
                build_lock.release(os.getpid(), self.lock)

        build_lock.wait_for_builds(self.lock, sleep=nap)
        self.assertEqual(len(naps), 3)
        build_lock.wait_for_builds(self.lock, sleep=self.fail)  # free: no wait


if __name__ == "__main__":
    unittest.main()
