# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for packet_summary.py on a small hand-made trace.

Run: py -3.12 -m unittest discover -s tools\\re\\tests
"""
import contextlib
import io
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import packet_summary as tool  # noqa: E402

TRACE = "\n".join([
    "1000 trace version=3 role=host process=1",
    "1000 net-out ch=06 size=120 peer=rig-guest map=18000000 sync=4 match=3",
    "1500 net-out ch=06 size=140 peer=rig-guest map=18000000 sync=4 match=3",
    "1600 place from-state=Playing from-map=0 from-visit=0 from-since=0 to-state=Playing "
    "to-map=18000000 to-visit=1 to-since=1600 cause=Load",
    "1700 net-in ch=34 size=2 peer=rig-guest map=18000000 sync=4 match=3 payload=0102",
    "1800 net-in ch=34 size=2 peer=rig-guest map=18000000 sync=4 match=3 payload=0102",
    "1900 net-in ch=34 size=2 peer=rig-guest map=18000000 sync=4 match=3 payload=0304",
    "2000 net-in ch=zz size=2 peer=rig-guest map=18000000 sync=4 match=3",
    "3000 net-out ch=10 size=2048 peer=rig-guest map=18000000 sync=1 match=3",
])


class PacketSummaryTest(unittest.TestCase):
    def test_only_whole_packet_records_count(self):
        packets = tool.parse(TRACE)
        self.assertEqual(len(packets), 6)
        self.assertEqual(packets[0], tool.Packet(1000, "out", 6, 120, "rig-guest", 0x18000000,
                                                 4, 3, "", "host"))
        self.assertEqual(packets[2].payload, "0102")

    def test_channels_are_summed_per_direction(self):
        stats = tool.summarize(tool.parse(TRACE))
        self.assertEqual(list(stats), [(0x06, "out"), (0x10, "out"), (0x34, "in")])
        self.assertEqual((stats[(6, "out")].count, stats[(6, "out")].total), (2, 260))
        self.assertEqual((stats[(6, "out")].smallest, stats[(6, "out")].largest), (120, 140))
        self.assertEqual(stats[(0x10, "out")].syncs, {1})
        lines = tool.table(stats, 2.0)
        self.assertIn("0x06 out       2     1.0       260     120-140", lines[1])

    def test_payloads_in_a_row_are_counted_once(self):
        lines = tool.payload_lines(tool.parse(TRACE), 0x34, 10)
        self.assertEqual(len(lines), 3)
        self.assertTrue(lines[0].endswith("size=2 0102"))
        self.assertEqual(lines[1], "    (x2)")
        self.assertTrue(lines[2].endswith("0304"))

    def test_main_prints_the_table_and_payloads(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "coop-trace-20261013-120000-1.txt"
            path.write_text(TRACE, encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(tool.main([str(path), "--payloads", "34"]), 0)
        text = out.getvalue()
        self.assertIn("6 packets (host)", text)
        self.assertIn("payloads of 0x34:", text)
        self.assertIn("0304", text)


if __name__ == "__main__":
    unittest.main()
