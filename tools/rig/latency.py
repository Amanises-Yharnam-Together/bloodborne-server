# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""A ping on the rig, as players over the internet have: every game's traffic to the other games
and to the rig's servers arrives a random 50-100 ms (the default) round trip later.

The games delay what they send each other themselves (SHADPS4_P2P_DELAY_MS, one way: half the
ping); the servers sit behind DelayProxy, a TCP proxy that holds every chunk half the ping each
way. Each way keeps its order, as a real connection would.
"""
import random
import socket
import threading
import time

DEFAULT_PING = "50-100"


def parse_ping(text: str) -> tuple[int, int] | None:
    """A round trip in milliseconds, "80" or "50-100"; None for "0" or "off" (no delay)."""
    text = text.strip().lower()
    if text in ("0", "off", "none", ""):
        return None
    parts = text.split("-")
    if len(parts) > 2 or not all(part.isdigit() for part in parts):
        raise ValueError(f"a ping is milliseconds, \"80\" or \"50-100\": {text!r}")
    low, high = int(parts[0]), int(parts[-1])
    if low > high:
        raise ValueError(f"the ping's range runs low to high: {text!r}")
    return low, high


def one_way(ping: tuple[int, int]) -> tuple[int, int]:
    """Half a round trip, rounded so both ways still add up to it."""
    return ping[0] // 2, (ping[1] + 1) // 2


def game_setting(ping: tuple[int, int]) -> str:
    """SHADPS4_P2P_DELAY_MS for a ping: what each game holds back the packets it sends."""
    low, high = one_way(ping)
    return f"{low}-{high}"


class DelayLine:
    """When each packet of one way is due: a random point in the range after it was sent, but
    never before the one sent ahead of it (no reordering)."""

    def __init__(self, delay_ms: tuple[int, int], seed: int | None = None):
        self.low, self.high = delay_ms[0] / 1000, delay_ms[1] / 1000
        self.random = random.Random(seed)
        self.last_due = 0.0

    def due_at(self, sent: float) -> float:
        due = sent + self.random.uniform(self.low, self.high)
        due = max(due, self.last_due)
        self.last_due = due
        return due


class DelayProxy:
    """Listens on 127.0.0.1:`port` (0 for any) and passes each connection on to `target`, every
    chunk held one way's delay (`delay_ms`, the range of one way) in each direction."""

    def __init__(self, port: int, target: int, delay_ms: tuple[int, int]):
        self.target = target
        self.delay_ms = delay_ms
        self.listener = socket.create_server(("127.0.0.1", port))
        self.port = self.listener.getsockname()[1]
        self.stopping = threading.Event()
        self.sockets: list[socket.socket] = []
        self.lock = threading.Lock()

    def start(self):
        threading.Thread(target=self.accept_all, daemon=True).start()

    def stop(self):
        self.stopping.set()
        self.listener.close()
        with self.lock:
            for sock in self.sockets:
                close(sock)
            self.sockets.clear()

    def accept_all(self):
        while not self.stopping.is_set():
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            try:
                server = socket.create_connection(("127.0.0.1", self.target), timeout=10)
                server.settimeout(None)
            except OSError:
                close(client)
                continue
            for sock in (client, server):
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with self.lock:
                self.sockets += [client, server]
            self.pass_on(client, server)
            self.pass_on(server, client)

    def pass_on(self, source: socket.socket, sink: socket.socket):
        """One direction: a reader that stamps each chunk with when it is due, and a writer that
        sends it then."""
        line = DelayLine(self.delay_ms)
        queue: list[tuple[float, bytes]] = []
        ready = threading.Condition()

        def read():
            while True:
                try:
                    data = source.recv(65536)
                except OSError:
                    data = b""
                with ready:
                    queue.append((line.due_at(time.monotonic()), data))
                    ready.notify()
                if not data:
                    return

        def write():
            while True:
                with ready:
                    while not queue:
                        ready.wait()
                    due, data = queue.pop(0)
                wait = due - time.monotonic()
                if wait > 0:
                    time.sleep(wait)
                if not data:  # the other side closed: so does this one, after what came before
                    try:
                        sink.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
                    return
                try:
                    sink.sendall(data)
                except OSError:
                    close(source)
                    return

        threading.Thread(target=read, daemon=True).start()
        threading.Thread(target=write, daemon=True).start()


def close(sock: socket.socket):
    try:
        sock.close()
    except OSError:
        pass
