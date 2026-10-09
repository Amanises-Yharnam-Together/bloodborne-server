# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for check_party_log.py on small hand-made logs.

Run: py -3.12 -m unittest discover -s tools\\re\\tests
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_party_log as tool  # noqa: E402


def line(text: str, level: str = "Info", file: str = "bloodborne_re.cpp:1 X") -> str:
    return f"[Debug] <{level}> (shadPS4:Main) {file}: {text}"


GOOD_BOOT = "\n".join(
    ["[Loader] <Info> (shadPS4:Main) emulator.cpp:468 Run: Revision 1ad074d2531e0000"]
    + [line(marker + "...") for marker in tool.BOOT_MARKERS.values()])


class BootTest(unittest.TestCase):
    def test_a_full_boot_passes(self):
        self.assertEqual(tool.check_boot(GOOD_BOOT), [])
        self.assertEqual(tool.revision(GOOD_BOOT), "1ad074d2531e")

    def test_a_missing_part_fails(self):
        text = GOOD_BOOT.replace(tool.BOOT_MARKERS["join anywhere"], "something else")
        self.assertEqual(tool.check_boot(text), ["missing: join anywhere"])

    def test_a_co_op_error_fails(self):
        text = GOOD_BOOT + "\n" + line(
            "Bloodborne sign timer signature mismatch at 0x18a9757", level="Error")
        failures = tool.check_boot(text)
        self.assertEqual(len(failures), 1)
        self.assertIn("sign timer signature mismatch", failures[0])

    def test_trace_hooks_are_only_warnings(self):
        text = GOOD_BOOT + "\n" + line(
            "Failed to install Bloodborne RE hook Event.WarpNextStage at 0x132e010",
            level="Error", file="bloodborne_re.cpp:8305 InstallReverseEngineeringTrace")
        self.assertEqual(tool.check_boot(text), [])
        self.assertEqual(len(tool.trace_warnings(text)), 1)

    def test_other_errors_are_not_co_op_failures(self):
        text = GOOD_BOOT + "\n[Common.Filesystem] <Error> (SLSession) io_file.cpp:202 Open: " \
                           "Failed to open the file"
        self.assertEqual(tool.check_boot(text), [])


class SessionTest(unittest.TestCase):
    def test_counts_the_events(self):
        text = "\n".join([
            line("Bloodborne party trip started map=0x18010000"),
            line("Bloodborne party trip: re-creating Kaniel on map 0x18010000 (1 of 2 members)"),
            line("Bloodborne party: lowered 3 enemies to another member's HP on map 0x18010000"),
        ])
        counts, problems = tool.summarize(text)
        self.assertEqual(counts["trips"], 1)
        self.assertEqual(counts["players re-created"], 1)
        self.assertEqual(counts["enemies lowered to a member's HP"], 1)
        self.assertEqual(problems, [])

    def test_flags_the_known_problems(self):
        text = "\n".join([
            line("Bloodborne party trip: re-creating ? on map 0x18010000 (1 of 2 members)"),
            line("Bloodborne LeaveSession from the game: eboot+0x193c68e"),
            "[Render.Vulkan] <Critical> vk_presenter.cpp:1135: Device lost during waiting",
        ])
        _, problems = tool.summarize(text)
        joined = "\n".join(problems)
        self.assertIn("online id was not found", joined)
        self.assertIn("left the session outside a trip", joined)
        self.assertIn("graphics device was lost", joined)


if __name__ == "__main__":
    unittest.main()
