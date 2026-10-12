# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for code_health.py on clang-tidy output captured from bloodborne_re.cpp.

Run: py -3.12 -m unittest discover -s tools\\re\\tests
"""
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import code_health as tool  # noqa: E402

ROOT = pathlib.Path("/repo")
# Three functions and two lambdas of bloodborne_re.cpp and one inline function of
# bloodborne_party.h (reported once per run), from the threshold-0 run and the nesting runs.
FIXTURE = (HERE / "fixtures" / "code_health_clang_tidy.txt").read_text()


def rows_by_name(output=FIXTURE):
    return {row.name: row for row in tool.parse(output, ROOT)}


def row(name, file="emulator/src/core/bloodborne_re.cpp", **metrics):
    return tool.Function(file, name, **metrics)


class ParseTest(unittest.TestCase):
    def test_reads_each_metric(self):
        harder = rows_by_name()["MakeEnemiesHarder"]
        self.assertEqual(harder.file, "emulator/src/core/bloodborne_re.cpp")
        self.assertEqual(harder.line, 4788)
        self.assertEqual((harder.cognitive, harder.lines, harder.statements, harder.branches,
                          harder.parameters), (65, 63, 62, 15, 0))

    def test_nesting_is_the_deepest_block_written_in_the_body(self):
        # MakeEnemiesHarder: if (effects) { if (table) { if (rows) { LOG_INFO } } } is 3; the
        # `do { if {` that LOG_INFO expands to is not counted.
        rows = rows_by_name()
        self.assertEqual(rows["MakeEnemiesHarder"].nesting, 3)
        self.assertEqual(rows["PolishMultiplayerUi"].nesting, 3)
        # OnLifecycle: one bare `{ scoped_lock ... }` block in the body.
        self.assertEqual(rows["OnLifecycle"].nesting, 1)

    def test_a_function_without_cognitive_warning_has_zero(self):
        text = ("/repo/emulator/src/core/bloodborne_re.cpp:10:6: warning: function 'Tiny' "
                "exceeds recommended size/complexity thresholds [readability-function-size]\n"
                "/repo/emulator/src/core/bloodborne_re.cpp:10:6: note: 3 lines including "
                "whitespace and comments (threshold 0)\n")
        tiny = rows_by_name(text)["Tiny"]
        self.assertEqual((tiny.cognitive, tiny.lines, tiny.nesting), (0, 3, 0))

    def test_header_functions_are_kept_once_and_paths_are_normalised(self):
        rows = [r for r in tool.parse(FIXTURE, ROOT) if r.name == "Observe"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].file, "emulator/src/core/bloodborne_party.h")
        self.assertEqual((rows[0].cognitive, rows[0].lines, rows[0].parameters), (1, 4, 2))

    def test_lambdas_are_named_after_the_function_they_are_in(self):
        rows = rows_by_name()
        self.assertEqual(rows["MakeEnemiesHarder::lambda#1"].cognitive, 1)
        self.assertEqual(rows["lambda#1"].cognitive, 3)  # namespace scope (place_log_listening)

    def test_other_files_are_left_out(self):
        text = ("/repo/bbport/gpu/third_party/httplib/httplib.h:5:6: warning: function 'Big' "
                "has cognitive complexity of 99 (threshold 0) "
                "[readability-function-cognitive-complexity]\n")
        self.assertEqual(tool.parse(text, ROOT), [])

    def test_overloads_get_a_number(self):
        text = "".join(
            f"/repo/emulator/src/core/bloodborne_re.cpp:{line}:6: warning: function 'Hook' "
            f"has cognitive complexity of {value} (threshold 0) "
            f"[readability-function-cognitive-complexity]\n" for line, value in ((9, 2), (3, 1)))
        self.assertEqual([(r.name, r.cognitive) for r in tool.parse(text, ROOT)],
                         [("Hook", 1), ("Hook#2", 2)])


class CheckTest(unittest.TestCase):
    def baseline(self, *rows):
        return tool.baseline_json(list(rows))

    def test_new_functions_must_stay_under_the_limits(self):
        base = self.baseline()
        self.assertEqual(tool.check([row("Small", cognitive=25, nesting=4, lines=120)], base), [])
        problems = tool.check([row("Big", cognitive=26, nesting=5, lines=121)], base)
        self.assertEqual(len(problems), 3)
        self.assertTrue(all(p.startswith("new function Big") for p in problems))

    def test_a_known_function_over_a_limit_may_not_get_worse(self):
        base = self.baseline(row("RunAutoplayStep", cognitive=464, nesting=4, lines=707))
        self.assertEqual(tool.check([row("RunAutoplayStep", cognitive=464, nesting=4,
                                         lines=700)], base), [])
        problems = tool.check([row("RunAutoplayStep", cognitive=465, nesting=4, lines=707)], base)
        self.assertEqual(problems, ["RunAutoplayStep (emulator/src/core/bloodborne_re.cpp:0): "
                                    "cognitive went from 464 to 465 (over 25)"])

    def test_a_known_function_may_change_while_under_the_limits(self):
        base = self.baseline(row("Tick", cognitive=3, lines=10))
        self.assertEqual(tool.check([row("Tick", cognitive=20, lines=100)], base), [])

    def test_a_function_moved_to_another_file_keeps_its_baseline(self):
        base = self.baseline(row("RunAutoplayStep", cognitive=464, lines=707))
        moved = row("RunAutoplayStep", file="emulator/src/core/bloodborne_autoplay.cpp",
                    cognitive=464, lines=707)
        self.assertEqual(tool.check([moved], base), [])

    def test_check_mode_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            saved = pathlib.Path(tmp, "out.txt")
            saved.write_text(FIXTURE)
            baseline = pathlib.Path(tmp, "baseline.json")
            args = ["--from-output", str(saved), "--root", str(ROOT), "--baseline",
                    str(baseline), "--worst", "0"]
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tool.main(args + ["--write-baseline"]), 0)
                self.assertEqual(tool.main(args + ["--check"]), 0)
            data = json.loads(baseline.read_text())
            for entry in data["functions"]:
                if entry["name"] == "MakeEnemiesHarder":
                    entry["cognitive"] = 60
            baseline.write_text(json.dumps(data))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(tool.main(args + ["--check"]), 1)
            self.assertIn("MakeEnemiesHarder", out.getvalue())


class SummaryTest(unittest.TestCase):
    def test_counts_per_file_and_total(self):
        rows = [row("A", cognitive=30, lines=50), row("B", cognitive=120, lines=200),
                row("C", file="emulator/src/core/bloodborne_party.cpp", cognitive=1, lines=10)]
        text = tool.summary(rows)
        self.assertIn("bloodborne_re.cpp", text)
        self.assertRegex(text, r"total\s+3\s+30\s+120\s+2\s+1\s+1  B \(200\)")
        self.assertIn("2 functions over cognitive 25 hold 96% of the function lines", text)


if __name__ == "__main__":
    unittest.main()
