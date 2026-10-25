# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""The rig's time budget (budget.py): short scenarios and chapters, no long fixed waits, and the
tiers. Kaniel 2026-10-15: keep scenarios short.

Run: py -3.12 -m unittest discover -s tools\\rig\\tests
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import budget  # noqa: E402
import scenarios  # noqa: E402
import suites  # noqa: E402


class BudgetTest(unittest.TestCase):
    def test_every_scenario_fits_or_is_long(self):
        for name, scenario in scenarios.SCENARIOS.items():
            with self.subTest(scenario=name):
                self.assertEqual(budget.scenario_problems(name, scenario), [])

    def test_every_chapter_fits(self):
        for name, suite in suites.SUITES.items():
            for chapter in suite["chapters"]:
                with self.subTest(suite=name, chapter=chapter["name"]):
                    self.assertEqual(budget.chapter_problems(name, chapter), [])

    def test_the_long_tier_names_real_scenarios(self):
        self.assertEqual(set(budget.LONG) - set(scenarios.SCENARIOS), set())

    def test_the_full_tier_runs_the_recent_bugs(self):
        # Kaniel 2026-10-25: the rig keeps up with the recent bugs: their scenarios run in the
        # full tier (the LONG ones in the long tier), and each names a real scenario.
        self.assertEqual(set(budget.RECENT) - set(scenarios.SCENARIOS), set())
        self.assertEqual(set(budget.RECENT) & set(budget.LONG), set())
        full = budget.tiers(suites.SUITES, scenarios.SCENARIOS)["full"]
        self.assertTrue(set(budget.RECENT) <= set(full))
        self.assertTrue({"dog-alone3", "nightmare-flags", "pings", "chalice-enter-broken",
                         "build-check-harder"} <= set(budget.RECENT))

    def test_an_over_budget_scenario_fails(self):
        slow = {"timeout": 600, "roles": {"host": [scenarios.START, "wait 45"]}}
        problems = budget.scenario_problems("slow", slow)
        self.assertTrue(any("timeout 600 s" in problem for problem in problems), problems)
        self.assertTrue(any("fixed wait of 45 s" in problem for problem in problems), problems)
        self.assertEqual(budget.scenario_problems("soak", slow), [], "the long tier may")

    def test_boots_and_waits_must_fit_the_timeout(self):
        restarted = {"timeout": 250, "roles": {"guest": [[scenarios.START, "wait 30"],
                                                         ["wait 30", "wait 30"]]}}
        self.assertEqual(budget.scenario_need(restarted), 2 * budget.BOOT_S + 90)
        self.assertIn("restarted: needs 290 s (boots and fixed waits), timeout 250 s",
                      budget.scenario_problems("restarted", restarted))

    def test_an_over_budget_chapter_fails(self):
        chapter = suites.chapter("slow", "", {"host": ["wait 60"],
                                              "guest": [suites.RESTART, "wait 25"]},
                                 budget=150)
        problems = budget.chapter_problems("s", chapter)
        self.assertIn("s/slow: budget 150 s (at most 120 s)", problems)
        self.assertTrue(any("s/slow host: a fixed wait of 60 s" in problem
                            for problem in problems), problems)
        chapter["budget"] = 50
        self.assertIn("s/slow host: needs 60 s of fixed waits, budget 50 s",
                      budget.chapter_problems("s", chapter))

    def test_a_restart_brings_its_boot_into_the_budget(self):
        chapter = suites.chapter("crash", "", {"guest": [suites.RESTART, "wait 25"],
                                               "host": ["sync x"]})
        self.assertEqual(budget.chapter_budget(chapter), 120 + budget.BOOT_S)
        self.assertEqual(budget.chapter_problems("s", chapter), [])

    def test_a_suite_tells_each_chapter_its_budget(self):
        compiled = suites.SUITE_SCENARIOS["smoke"]
        host = sum(compiled["roles"]["host"], []) if isinstance(compiled["roles"]["host"][0],
                                                                   list) else compiled["roles"]["host"]
        self.assertIn("chapter join 120", host)
        self.assertIn("chapter hit3 120", host)

    def test_a_suites_timeout_is_its_boot_and_its_chapters(self):
        for name, suite in suites.SUITES.items():
            with self.subTest(suite=name):
                self.assertEqual(suites.SUITE_SCENARIOS[name]["timeout"],
                                 budget.suite_timeout(suite))

    def test_the_tiers(self):
        tiers = budget.tiers(suites.SUITES, scenarios.SCENARIOS)
        self.assertEqual(tiers["quick"][0], "smoke")
        self.assertEqual(set(tiers["full"]), set(suites.SUITES) | set(budget.RECENT))
        self.assertEqual(tiers["full"][:len(suites.SUITES)][-1] in suites.SUITES, True)
        self.assertIn("soak", tiers["long"])
        self.assertIn("ward-door", tiers["long"])


if __name__ == "__main__":
    unittest.main()
