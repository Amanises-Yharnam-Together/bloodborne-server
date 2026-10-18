# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for check_party_log.py on small hand-made logs.

Run: py -3.12 -m unittest discover -s tools\\re\\tests
"""
import contextlib
import io
import pathlib
import sys
import tempfile
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

    def test_the_ports_team_mode_party_controller_counts(self):
        team = line("InitPartyController: Bloodborne party: the team lobby decides the party "
                    "(team mode)")
        text = GOOD_BOOT.replace(tool.BOOT_MARKERS["party controller"], "something else")
        self.assertEqual(tool.check_boot(text), ["missing: party controller"])
        self.assertEqual(tool.check_boot(text + "\n" + team), [])

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

    def test_a_skipped_required_feature_fails(self):
        text = GOOD_BOOT + "\n" + line(
            "Co-op features: 33/35 on (skipped: sign timers [signature mismatch at 0x18a9757], "
            "item button diagnostics [hook failed at 0x12fd8cd])")
        self.assertEqual(tool.skipped_features(text),
                         [("sign timers", "signature mismatch at 0x18a9757"),
                          ("item button diagnostics", "hook failed at 0x12fd8cd")])
        self.assertEqual(tool.check_boot(text), [
            "required co-op feature skipped: sign timers [signature mismatch at 0x18a9757]"])

    def test_a_skipped_optional_feature_passes(self):
        text = GOOD_BOOT + "\n" + line(
            "Co-op features: 34/35 on (skipped: item button diagnostics [hook failed at "
            "0x12fd8cd])")
        self.assertEqual(tool.check_boot(text), [])
        text = GOOD_BOOT + "\n" + line("Co-op features: 35/35 on")
        self.assertEqual(tool.skipped_features(text), [])
        self.assertEqual(tool.check_boot(text), [])

    def test_the_required_features_come_from_the_co_op_site_table(self):
        required = tool.required_features()
        self.assertIn("seamless core", required)
        self.assertIn("sign timers", required)
        self.assertNotIn("item button diagnostics", required)

    def test_a_session_log_with_a_skipped_required_feature_is_a_problem(self):
        text = line("Co-op features: 0/1 on (skipped: seamless core [hook failed at 0x12f836e; "
                    "rolled back])")
        _, problems = tool.summarize(text)
        self.assertIn("required co-op feature skipped: seamless core [hook failed at 0x12f836e; "
                      "rolled back]", problems)

    def test_other_errors_are_not_co_op_failures(self):
        text = GOOD_BOOT + "\n[Common.Filesystem] <Error> (SLSession) io_file.cpp:202 Open: " \
                           "Failed to open the file"
        self.assertEqual(tool.check_boot(text), [])


class SessionTest(unittest.TestCase):
    def test_counts_the_events(self):
        text = "\n".join([
            line("Bloodborne party trip started map=0x18010000"),
            line("Bloodborne party trip: re-creating Kaniel on map 0x18010000 (1 of 2 members)"),
            line("Bloodborne party: lowered 3 enemies by the other members' damage on map 0x18010000"),
        ])
        counts, problems = tool.summarize(text)
        self.assertEqual(counts["trips"], 1)
        self.assertEqual(counts["players re-created"], 1)
        self.assertEqual(counts["enemies lowered by the others' damage"], 1)
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

    def test_flags_a_game_reaching_past_the_server(self):
        # Phase 9's proof test: the rig's games reach only the server (SHADPS4_NET_FIREWALL).
        text = line("Net firewall: blocked a datagram to 127.0.0.1:51601", level="Warning")
        _, problems = tool.summarize(text)
        self.assertEqual(len(problems), 1)
        self.assertIn("past the server", problems[0])

    def test_flags_a_refused_game_call(self):
        # A guarded call skipped because the object's vtable was not the expected kind: either a
        # wrong pointer or a vtable list that misses a class (the feature quietly stops).
        text = line("Bloodborne refused a game call: EventFlagOn got an object (0x812345678) "
                    "with vtable eboot+0x5320b70", level="Warning")
        _, problems = tool.summarize(text)
        self.assertEqual(len(problems), 1)
        self.assertIn("refused", problems[0])
        self.assertEqual(len(tool.check_boot(GOOD_BOOT + "\n" + text)), 1)


TRACE = "\n".join([
    "1760000000000 trace version=1 role=host process=7",
    "1760000000100 p2p-in type=30 peer=0a000002:3658 from-peer=1 payload=00",
    "1760000000600 p2p-in type=30 peer=0a000002:3658 from-peer=1 payload=00",
    "1760000000700 p2p-out type=32 peer=0a000002:3658 payload=00",
    "1760000001000 p2p-in type=31 peer=0a000003:3658 from-peer=1 payload=00",
    "1760000001200 p2p-in type=30 peer=0b000009:1 from-peer=0 payload=00",
    "1760000001300 board-out endpoint=/bloodborne/party/hp size=2 body=ab",
    "1760000001400 board-in endpoint=/bloodborne/party/hp status=200 same=1",
    "1760000002000 board-in endpoint=/bloodborne/party/hp error=Connection",
    "1760000002100 trace-dropped count=12",
    # 0a000002 goes quiet for 7.4 s (an outage candidate); 0a000003 for 4 s (not one).
    "1760000005000 p2p-in type=31 peer=0a000003:3658 from-peer=1 payload=00",
    "1760000008000 p2p-in type=30 peer=0a000002:3658 from-peer=1 payload=00",
])


class TraceTest(unittest.TestCase):
    def test_counts_the_frames_and_posts(self):
        summary = tool.summarize_trace(TRACE)
        self.assertEqual(summary["frames"], {"p2p-in 30": 3, "p2p-in 31": 2, "p2p-out 32": 1})
        self.assertEqual(summary["board posts"], {"/bloodborne/party/hp": 1})
        self.assertEqual(summary["board failures"], {"/bloodborne/party/hp": 1})
        self.assertEqual(summary["dropped"], 12)

    def test_names_the_kills_path_as_it_changes(self):
        trace = "\n".join([
            "1760000000000 wlog-path path=board why=no\\speers",
            "1760000003000 wlog-path path=log why=1\\speer\\shas\\sit",
        ])
        self.assertEqual(tool.summarize_trace(trace)["kills paths"],
                         ["1760000000000 board (no peers)", "1760000003000 log (1 peer has it)"])
        self.assertEqual(tool.summarize_trace(TRACE)["kills paths"], [])

    def test_names_the_shown_characters_path_too(self):
        trace = "1760000003000 wlog-path what=shown path=board why=b\\sruns\\sa\\sbuild\\swithout\\sit"
        self.assertEqual(tool.summarize_trace(trace)["kills paths"],
                         ["1760000003000 shown board (b runs a build without it)"])

    def test_finds_the_peers_long_silences(self):
        gaps = tool.summarize_trace(TRACE)["gaps"]
        self.assertEqual(len(gaps), 1)
        self.assertIn("0a000002:3658", gaps[0])
        self.assertIn("7.4 s", gaps[0])

    def test_finds_the_traces_next_to_a_log(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            (folder / "shad_log.txt").write_text("log")
            (folder / "coop-trace-20261012-100000-7.txt").write_text(TRACE)
            (folder / "coop-trace-20261012-090000-7.txt").write_text(TRACE)
            (folder / "other.txt").write_text("x")
            self.assertEqual([path.name for path in tool.traces_next_to(folder / "shad_log.txt")],
                             ["coop-trace-20261012-090000-7.txt",
                              "coop-trace-20261012-100000-7.txt"])

    def test_main_summarizes_the_traces_when_asked(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            log = folder / "shad_log.txt"
            log.write_text(line("Bloodborne party trip started map=0x18010000"))
            (folder / "coop-trace-20261012-100000-7.txt").write_text(TRACE)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                tool.main([str(log)])
            self.assertNotIn("coop-trace", out.getvalue())
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                tool.main(["--trace", str(log)])
            self.assertIn("coop-trace-20261012-100000-7.txt", out.getvalue())
            self.assertIn("p2p-in 30: 3", out.getvalue())
            self.assertIn("silent", out.getvalue())


if __name__ == "__main__":
    unittest.main()
