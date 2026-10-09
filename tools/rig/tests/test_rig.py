# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for the rig's runner and scenarios, without games: a scenario with a typo would
otherwise only fail ten minutes into a run.

Run: py -3.12 -m unittest discover -s tools\\rig\\tests
"""
import pathlib
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import run_scenario  # noqa: E402
import scenarios  # noqa: E402

# RunAutoplayStep's commands (bloodborne_re.cpp).
VERBS = {
    "wait", "wait-session", "wait-members", "wait-map", "travel", "ensure-map", "hurt", "kill",
    "mark", "approach", "attack", "teleport", "press", "stick", "set-flag", "expect-flag",
    "wait-flag", "godmode", "snapshot", "log", "expect-hp", "expect-session", "expect-map", "done",
    "wait-solo", "note-max-hp", "expect-max-hp", "expect-effect",
}
BUTTONS = {"cross", "circle", "square", "triangle", "options", "up", "right", "down", "left",
           "l1", "r1", "l2", "r2", "l3", "r3"}


class ScenarioTest(unittest.TestCase):
    def test_every_command_is_one_the_game_runs(self):
        for name, scenario in scenarios.SCENARIOS.items():
            for role, script in scenario["roles"].items():
                for command in sum(run_scenario.stages_of(script), []):
                    words = command.split()
                    with self.subTest(scenario=name, role=role, command=command):
                        self.assertIn(words[0], VERBS)
                        if words[0] == "press":
                            self.assertIn(words[1], BUTTONS)

    def test_every_role_is_a_rig_game(self):
        for name, scenario in scenarios.SCENARIOS.items():
            with self.subTest(scenario=name):
                self.assertTrue(set(scenario["roles"]) <= set(run_scenario.ROLES))
                for key in ("expect", "forbid"):
                    self.assertTrue(set(scenario.get(key, {})) <= set(scenario["roles"]),
                                    f"{key} names a role the scenario does not run")

    def test_every_pattern_compiles(self):
        for name, scenario in scenarios.SCENARIOS.items():
            for key in ("expect", "forbid"):
                for patterns in scenario.get(key, {}).values():
                    for pattern in patterns:
                        with self.subTest(scenario=name, pattern=pattern):
                            re.compile(pattern)
            for pattern in scenario.get("allow", []):
                re.compile(pattern)

    def test_every_script_starts_where_the_baseline_is(self):
        for name, scenario in scenarios.SCENARIOS.items():
            for role, script in scenario["roles"].items():
                with self.subTest(scenario=name, role=role):
                    self.assertEqual(run_scenario.stages_of(script)[0][0], scenarios.START)

    def test_only_the_kill_made_is_posted(self):
        [pattern] = scenarios.only_kill("0x1001400b")
        made = ("Bloodborne party: kill board gets 1 kills, 0 back (visit 0x1801000000000001): "
                "0x1001400b (dead 0x6 hp 0)")
        false = ("Bloodborne party: kill board gets 2 kills, 0 back (visit 0x1801000000000001): "
                 "0x1001409c (dead 0x1 hp 0) 0x1001409e (dead 0x1 hp 0)")
        self.assertIsNone(re.search(pattern, made))
        self.assertIsNotNone(re.search(pattern, false))

    def test_an_attack_is_one_step_with_a_time_limit(self):
        self.assertEqual(scenarios.attack("marked"), ["attack marked 40"])


class RunnerTest(unittest.TestCase):
    def test_a_role_runs_one_script_or_several_in_turn(self):
        self.assertEqual(run_scenario.stages_of(["wait 1", "done"]), [["wait 1", "done"]])
        self.assertEqual(run_scenario.stages_of([["wait 1"], ["snapshot"]]),
                         [["wait 1"], ["snapshot"]], "stopped and started again in between")
        self.assertEqual(run_scenario.stages_of([]), [[]])


    NETSTAT = "\n".join([
        "Active Connections",
        "  Proto  Local Address          Foreign Address        State           PID",
        "  TCP    127.0.0.1:32313        0.0.0.0:0              LISTENING       4120",
        "  TCP    0.0.0.0:20453          0.0.0.0:0              LISTENING       5532",
        "  TCP    127.0.0.1:31313        0.0.0.0:0              LISTENING       777",
        "  TCP    127.0.0.1:52000        127.0.0.1:32313        ESTABLISHED     9001",
        "  TCP    127.0.0.1:132313       0.0.0.0:0              LISTENING       12",
    ])

    def test_only_the_rigs_listeners_are_stopped(self):
        self.assertEqual(run_scenario.listening_pids(self.NETSTAT, 32313), {4120})
        self.assertEqual(run_scenario.listening_pids(self.NETSTAT, 20453), {5532})
        self.assertEqual(run_scenario.listening_pids(self.NETSTAT, 32314), set(),
                         "the online kit's 31313 and a client's connection are not the rig's")
        self.assertEqual(run_scenario.listening_pids("", 32313), set())

    def test_only_the_rigs_games_are_stopped(self):
        exe = pathlib.Path(r"C:\bb\out\emulator\build\shadPS4.exe")
        listing = "\n".join([
            "4120\tC:\\bb\\out\\emulator\\build\\shadPS4.exe",
            "5532\tc:\\BB\\out\\emulator\\build\\shadps4.exe",
            "777\tC:\\Bloodborne.PC.Online - Copy\\SeamlessCoop\\shadps4.exe",
            "",
            "garbage",
        ])
        self.assertEqual(run_scenario.split_games(listing, exe), ([4120, 5532], [777]),
                         "Kaniel's own game is not the rig's")
        self.assertEqual(run_scenario.split_games("", exe), ([], []))

    def test_a_log_is_judged_up_to_its_scripts_end(self):
        log = ("step 1/2 wait 1 ok\nBloodborne autoplay: done (ok)\n"
               "sceNpMatching2LeaveRoom: called (the rig stopped the other game)\n")
        self.assertNotIn("LeaveRoom", run_scenario.until_done(log))
        self.assertTrue(run_scenario.until_done(log).endswith("done (ok)"))
        self.assertEqual(run_scenario.until_done("no end yet"), "no end yet")

    def test_a_game_that_never_got_going_says_why(self):
        log = ("[Loader] <Info> Files found in game mods folder\n"
               "[Debug] <Critical> (shadPS4:Main) address_space.cpp:200 Impl: Assertion Failed!\n"
               "Insufficient system resources exist to complete the requested service.\n")
        self.assertEqual(run_scenario.boot_failure(log),
                         "Insufficient system resources exist to complete the requested service.")
        self.assertIsNone(run_scenario.boot_failure("a game in the world\n"))

    def test_old_runs_go_newest_stay(self):
        with tempfile.TemporaryDirectory() as folder:
            runs = pathlib.Path(folder)
            for stamp in ("20261009-010000", "20261009-020000", "20261009-030000"):
                (runs / stamp / "join").mkdir(parents=True)
            run_scenario.prune_runs(runs, 2)
            self.assertEqual(sorted(path.name for path in runs.iterdir()),
                             ["20261009-020000", "20261009-030000"])
            run_scenario.prune_runs(runs / "missing", 2)  # no runs yet: nothing to do


if __name__ == "__main__":
    unittest.main()
