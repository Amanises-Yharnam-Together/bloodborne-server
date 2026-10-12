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
import unittest.mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import engines  # noqa: E402
import run_scenario  # noqa: E402
import scenarios  # noqa: E402

# The game's autoplay verbs (AutoplayVerbs, bloodborne_autoplay.cpp; VerbsTest keeps them equal).
VERBS = {
    "wait", "wait-session", "wait-members", "wait-map", "travel", "ensure-map", "hurt", "kill",
    "mark", "approach", "attack", "teleport", "press", "stick", "set-flag", "expect-flag",
    "wait-flag", "godmode", "snapshot", "log", "expect-hp", "expect-session", "expect-map", "done",
    "wait-solo", "note-max-hp", "expect-max-hp", "expect-effect", "expect-others-near",
    "pickup", "hit", "rise", "approach-downed", "revive-downed", "wait-vital", "wait-member-vital",
    "shot", "expect-hidden", "expect-disabled", "census", "peek", "poke", "dump", "raise-stat",
}
VITALS = {"up", "down", "dead"}
BUTTONS = {"cross", "circle", "square", "triangle", "options", "up", "right", "down", "left",
           "l1", "r1", "l2", "r2", "l3", "r3"}


AUTOPLAY = (pathlib.Path(__file__).resolve().parents[3] / "emulator" / "src" / "core" /
            "bloodborne_autoplay.cpp")


class VerbsTest(unittest.TestCase):
    def test_the_verbs_are_the_games(self):
        text = AUTOPLAY.read_text(encoding="utf-8")
        table = text[text.index("AutoplayVerbs ="):]
        table = table[:table.index("});")]
        self.assertEqual(set(re.findall(r'\{"([a-z-]+)", Autoplay\w+\}', table)), VERBS)


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
                        if words[0] in ("wait-vital", "wait-member-vital"):
                            self.assertIn(words[1], VITALS)

    def test_every_shot_has_a_plain_name_of_its_own(self):
        for name, scenario in scenarios.SCENARIOS.items():
            for role, script in scenario["roles"].items():
                shots = [command.split() for command in sum(run_scenario.stages_of(script), [])
                         if command.split()[0] == "shot"]
                with self.subTest(scenario=name, role=role):
                    for words in shots:
                        self.assertEqual(len(words), 2, words)
                        self.assertRegex(words[1], r"^[A-Za-z0-9_-]{1,40}$")
                    names = [words[1] for words in shots]
                    self.assertEqual(len(names), len(set(names)), "a later one would replace it")

    def test_party_death_scenarios_switch_it_on(self):
        for name, scenario in scenarios.SCENARIOS.items():
            commands = [command.split()[0] for script in scenario["roles"].values()
                        for command in sum(run_scenario.stages_of(script), [])]
            uses = any(verb in ("approach-downed", "revive-downed", "wait-vital", "wait-member-vital")
                       for verb in commands)
            with self.subTest(scenario=name):
                self.assertEqual(
                    scenario.get("settings", {}).get("SHADPS4_BLOODBORNE_PARTY_DEATH") == "1", uses)

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
            if "saves" in scenario or scenario.get("no_baseline"):  # where it was left
                continue
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

    def test_a_scenario_can_start_from_other_saves(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            (root / "baseline" / "savedata").mkdir(parents=True)
            (root / "baseline" / "savedata" / "old").write_text("x")
            (root / "baseline" / "bloodborne_party_1.txt").write_text("old")
            other = root / "new" / "savedata"
            other.mkdir(parents=True)
            (other / "fresh").write_text("y")
            user = root / "user"
            user.mkdir()
            (user / "bloodborne_party_1.txt").write_text("played")
            engine = unittest.mock.Mock()
            engine.role_dir.return_value = root
            engine.saves.return_value = user / "savedata"
            with unittest.mock.patch.object(run_scenario, "ENGINE", engine), \
                    unittest.mock.patch.object(run_scenario, "user_dir", lambda role: user):
                self.assertTrue(run_scenario.restore_baseline("host", other))
                self.assertTrue((user / "savedata" / "fresh").is_file())
                self.assertFalse((user / "bloodborne_party_1.txt").exists(),
                                 "a new character has no party files")
                self.assertTrue(run_scenario.restore_baseline("host"))
                self.assertTrue((user / "savedata" / "old").is_file())
                self.assertEqual((user / "bloodborne_party_1.txt").read_text(), "old")


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

    def test_a_runs_pictures_go_with_its_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            engine = engines.Port(root)
            shots = engine.shots("host")
            shots.mkdir(parents=True)
            (shots / "sees-down.png").write_bytes(b"png")
            engine.shot_request("host").write_text("never taken", encoding="utf-8")
            run_dir = root / "rig" / "runs" / "20261010-040000" / "down-revive"
            with unittest.mock.patch.object(run_scenario, "ENGINE", engine):
                run_scenario.keep_shots(run_dir, ["host", "guest"])  # the guest took none
            self.assertEqual((run_dir / "host-sees-down.png").read_bytes(), b"png")
            self.assertEqual(list(shots.iterdir()), [], "moved: the next run starts with none")
            self.assertFalse(engine.shot_request("host").exists(),
                             "a request no frame took is not taken by the next run's first frame")

    def test_play_gets_the_games_into_the_world_together_and_leaves_the_rest_to_you(self):
        for name, players in (("down-revive", 2), ("down-revive3", 3)):
            scenario = scenarios.SCENARIOS[name]
            scripts = run_scenario.play_scripts(scenario)
            with self.subTest(scenario=name):
                self.assertEqual(list(scripts), list(scenario["roles"]))
                for script in scripts.values():
                    self.assertEqual(script, [scenarios.START, f"wait-session {players} 300"])
                    self.assertTrue(all(step.split()[0] in VERBS for step in script))

    def test_every_game_sees_one_hp_for_the_shared_enemy(self):
        # Kaniel 2026-10-10: two players hitting one enemy saw different HP bars. A scenario's
        # "same_hp" handle must show the same HP in every role's last census, below its max.
        line = "[Debug] <Info> () x: Bloodborne autoplay census: {} hp {}/{} dead 0x0 hidden 0"
        host = "\n".join([line.format("0x1001400b", 484, 484), line.format("0x1001400b", 300, 484)])
        guest = line.format("0x1001400b", 301, 484)
        self.assertEqual(run_scenario.last_census_hp(host, "0x1001400b"), (300, 484))
        self.assertIsNone(run_scenario.same_hp_problem({"host": host, "guest": guest},
                                                       "0x1001400b"))
        self.assertIn("apart", run_scenario.same_hp_problem(
            {"host": host, "guest": line.format("0x1001400b", 400, 484)}, "0x1001400b"))
        self.assertIn("never hit", run_scenario.same_hp_problem(
            {"host": line.format("0x1001400b", 484, 484), "guest": line.format("0x1001400b", 484, 484)},
            "0x1001400b"))
        self.assertIn("no census", run_scenario.same_hp_problem({"host": host, "guest": ""},
                                                                "0x1001400b"))

    def test_the_shared_enemy_took_each_hit_once(self):
        # Kaniel's friend 2026-10-11: each of the guest's hits came back a second time. With
        # "hp_sums_hits", the last census must show max HP less every role's own hits (the game's
        # hit function, "by this player"), a few HP apart at most (shares round). Not the `hit`
        # step's drop: another member's damage written by the board while it swings counts there
        # (rig hit3 2026-10-11: the guest's step saw the host's 484 -> 482).
        census = "[Debug] <Info> () x: Bloodborne autoplay census: {} hp {}/{} dead 0x0 hidden 0"
        hit = ("[Debug] <Info> () x: Bloodborne party: hit on {} by this player 0x10044000: "
               "{} -> {} (t=1.0)")
        step = "[Debug] <Info> () x: Bloodborne autoplay: hit {}: hp {} -> {} in attack cycle 0"
        texts = {
            "host": "\n".join([hit.format("0x1001400b", 484, 430),
                               census.format("0x1001400b", 330, 484)]),
            "guest": "\n".join([step.format("0x1001400b", 430, 420),
                                hit.format("0x1001400b", 430, 380),
                                hit.format("0x1001400b", 380, 330),
                                census.format("0x1001400b", 331, 484)]),
        }
        self.assertEqual(run_scenario.hit_damage(texts["guest"], "0x1001400b"), 100)
        self.assertIsNone(run_scenario.hits_sum_problem(texts, "0x1001400b"))
        twice = dict(texts, host=texts["host"].replace("330/484", "280/484"))
        self.assertIn("took 204", run_scenario.hits_sum_problem(twice, "0x1001400b"))
        self.assertIn("no hit", run_scenario.hits_sum_problem(
            {"host": census.format("0x1001400b", 484, 484)}, "0x1001400b"))

    def test_a_hit_reaches_the_other_games_within_the_lag(self):
        # Kaniel's forest run 2026-10-11: the host's hits showed on the guests' bars a second late.
        # The rig's games share one clock (t=): from each role's own hit to the first lowering of
        # that enemy in each other game, on average (one late round is no trend).
        hit = ("[Debug] <Info> () x: Bloodborne party: hit on {} by this player 0x10044000: "
               "{} -> {} (t={})")
        low = ("[Debug] <Info> () x: Bloodborne party: lowered 1 enemies by the other members' "
               "damage on map 0x18010000: {} {}->{} (t={})")
        texts = {
            "host": hit.format("0x1001400b", 484, 382, "100.0"),
            "guest": "\n".join([low.format("0x1001400b", 484, 382, "100.3"),
                                hit.format("0x1001400b", 382, 280, "110.0")]),
            "guest2": "\n".join([low.format("0x1001400b", 484, 382, "100.6"),
                                 low.format("0x1001400b", 382, 280, "110.2")]),
        }
        self.assertAlmostEqual(run_scenario.hp_board_lag(texts, "0x1001400b"), 1.1 / 3)
        self.assertIsNone(run_scenario.hp_board_lag({"host": texts["host"]}, "0x1001400b"))
        # Rig hit3 2026-10-11: a game the hit reached through the game itself lowers nothing for
        # it; its next lowering (another hit, 35 s on) or a write given back (up) is no lag.
        later = dict(texts, host="\n".join([
            texts["host"], low.format("0x1001400b", 280, 178, "145.0"),
            low.format("0x1001400b", 76, 178, "145.1")]))
        self.assertAlmostEqual(run_scenario.hp_board_lag(later, "0x1001400b"), 1.1 / 3)

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
