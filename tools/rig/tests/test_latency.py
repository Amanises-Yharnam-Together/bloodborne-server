# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for the rig's network delay (tools/rig/latency.py): the ping setting, the delay line
and the delaying TCP proxy in front of the rig's servers.

Run: py -3.12 -m unittest discover -s tools\\rig\\tests
"""
import pathlib
import socket
import sys
import threading
import time
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import latency  # noqa: E402


class PingTest(unittest.TestCase):
    def test_a_ping_is_one_value_or_a_range(self):
        self.assertEqual(latency.parse_ping("80"), (80, 80))
        self.assertEqual(latency.parse_ping("50-100"), (50, 100))
        self.assertIsNone(latency.parse_ping("0"))
        self.assertIsNone(latency.parse_ping("off"))
        for bad in ("abc", "100-50", "-5", "50-", "1-2-3"):
            with self.assertRaises(ValueError, msg=bad):
                latency.parse_ping(bad)

    def test_each_way_takes_half_the_ping(self):
        self.assertEqual(latency.one_way((50, 100)), (25, 50))
        self.assertEqual(latency.one_way((75, 75)), (37, 38))
        self.assertEqual(latency.game_setting((50, 100)), "25-50")


class DelayLineTest(unittest.TestCase):
    def test_every_packet_is_due_within_the_range_and_in_order(self):
        line = latency.DelayLine((25, 50), seed=3)
        sent, last_due, delays = 100.0, 0.0, set()
        for _ in range(500):
            sent += 0.001  # closer than the range is wide
            due = line.due_at(sent)
            self.assertGreaterEqual(due, last_due)
            self.assertGreaterEqual(due - sent, 0.025 - 1e-9)
            self.assertLessEqual(due - sent, 0.050 + 1e-9)
            delays.add(round(due - sent, 4))
            last_due = due
        self.assertGreater(len(delays), 1, "jitter, not one fixed delay")


class ProxyTest(unittest.TestCase):
    def setUp(self):
        self.server = socket.create_server(("127.0.0.1", 0))
        self.target = self.server.getsockname()[1]
        threading.Thread(target=self.echo, daemon=True).start()

    def tearDown(self):
        self.server.close()

    def echo(self):
        try:
            connection, _ = self.server.accept()
        except OSError:
            return
        with connection:
            while data := connection.recv(4096):
                connection.sendall(data)

    def round_trip(self, port: int, payload: bytes) -> tuple[bytes, float]:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as client:
            started = time.perf_counter()
            client.sendall(payload)
            received = b""
            while len(received) < len(payload):
                received += client.recv(4096)
            return received, time.perf_counter() - started

    def test_the_proxy_passes_everything_on_a_ping_later(self):
        proxy = latency.DelayProxy(0, self.target, (40, 60))
        proxy.start()
        try:
            payload = bytes(range(256)) * 64
            received, took = self.round_trip(proxy.port, payload)
            self.assertEqual(received, payload)
            self.assertGreaterEqual(took, 0.080, "both ways delayed")
            self.assertLess(took, 1.0)
        finally:
            proxy.stop()

    def test_an_outage_holds_everything_until_it_is_over(self):
        # Players' Radmin links dropped for 15-31 s (2026-10-11): nothing gets through then, and
        # what was sent arrives after (TCP sends it again), in order.
        proxy = latency.DelayProxy(0, self.target, (1, 2))
        proxy.start()
        try:
            with socket.create_connection(("127.0.0.1", proxy.port), timeout=5) as client:
                client.sendall(b"before")
                self.assertEqual(client.recv(4096), b"before")
                proxy.outage(0.5)
                started = time.perf_counter()
                client.sendall(b"during")
                self.assertEqual(client.recv(4096), b"during")
                self.assertGreaterEqual(time.perf_counter() - started, 0.45)
        finally:
            proxy.stop()

    def test_a_stopped_proxy_lets_go_of_its_port(self):
        proxy = latency.DelayProxy(0, self.target, (1, 2))
        proxy.start()
        port = proxy.port
        proxy.stop()
        with socket.create_server(("127.0.0.1", port)):
            pass


if __name__ == "__main__":
    unittest.main()
