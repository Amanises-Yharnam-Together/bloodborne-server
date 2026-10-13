# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""The game's own packets in co-op traces (SHADPS4_BLOODBORNE_PACKET_TRACE, docs/NATIVE_NETCODE.md
phase 0): how many went out and came in per channel, their sizes, the members and the NetSync
states they moved in; and, with --payloads, the payloads a channel carried.

Usage: py -3.12 tools\\re\\packet_summary.py <coop-trace file>... [--payloads CH] [--limit N]
"""
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import sys

KINDS = {"net-out": "out", "net-in": "in"}


@dataclasses.dataclass
class Packet:
    utc_ms: int
    direction: str  # "out" or "in"
    channel: int
    size: int
    peer: str
    map: int
    sync: int
    match: int
    payload: str  # hex, "" when the trace left it out
    source: str = ""  # the trace's role (host, guest, ...)


def parse(text: str, source: str = "") -> list[Packet]:
    """The packet records of one trace, in order. Other records are skipped; the trace's own
    first line names its role, used as `source` unless one is given."""
    packets = []
    for line in text.splitlines():
        parts = line.split(" ")
        if len(parts) < 2:
            continue
        fields = dict(part.split("=", 1) for part in parts[2:] if "=" in part)
        if parts[1] == "trace" and not source:
            source = fields.get("role", "")
            continue
        if parts[1] not in KINDS:
            continue
        try:
            packets.append(Packet(int(parts[0]), KINDS[parts[1]], int(fields["ch"], 16),
                                  int(fields["size"]), fields.get("peer", ""),
                                  int(fields["map"], 16), int(fields["sync"]),
                                  int(fields["match"]), fields.get("payload", ""), source))
        except (KeyError, ValueError):
            continue
    return packets


@dataclasses.dataclass
class ChannelStats:
    count: int = 0
    total: int = 0
    smallest: int | None = None
    largest: int = 0
    peers: set = dataclasses.field(default_factory=set)
    syncs: set = dataclasses.field(default_factory=set)
    matches: set = dataclasses.field(default_factory=set)

    def add(self, packet: Packet):
        self.count += 1
        self.total += packet.size
        self.smallest = packet.size if self.smallest is None else min(self.smallest, packet.size)
        self.largest = max(self.largest, packet.size)
        self.peers.add(packet.peer)
        self.syncs.add(packet.sync)
        self.matches.add(packet.match)


def summarize(packets: list[Packet]) -> dict[tuple[int, str], ChannelStats]:
    """Per (channel, direction): counts, bytes, sizes, peers and states."""
    stats: dict[tuple[int, str], ChannelStats] = {}
    for packet in packets:
        stats.setdefault((packet.channel, packet.direction), ChannelStats()).add(packet)
    return dict(sorted(stats.items()))


def numbers(values: set) -> str:
    return ",".join(str(value) for value in sorted(values))


def table(stats: dict[tuple[int, str], ChannelStats], seconds: float) -> list[str]:
    lines = [f"{'ch':>4} {'dir':>3} {'count':>7} {'per s':>7} {'bytes':>9} {'size':>11} "
             f"{'sync':>9} {'match':>7}  peers"]
    for (channel, direction), entry in stats.items():
        sizes = (f"{entry.smallest}" if entry.smallest == entry.largest
                 else f"{entry.smallest}-{entry.largest}")
        rate = entry.count / seconds if seconds > 0 else 0.0
        lines.append(f"0x{channel:02x} {direction:>3} {entry.count:>7} {rate:>7.1f} "
                     f"{entry.total:>9} {sizes:>11} {numbers(entry.syncs):>9} "
                     f"{numbers(entry.matches):>7}  {','.join(sorted(entry.peers))}")
    return lines


def payload_lines(packets: list[Packet], channel: int, limit: int) -> list[str]:
    """The payloads `channel` carried, oldest first, the same one in a row shown once with a
    count."""
    lines: list[str] = []
    last = None
    repeats = 0
    for packet in packets:
        if packet.channel != channel or not packet.payload:
            continue
        key = (packet.source, packet.direction, packet.peer, packet.payload)
        if key == last:
            repeats += 1
            continue
        if repeats:
            lines.append(f"    (x{repeats + 1})")
        repeats = 0
        last = key
        if len(lines) >= limit:
            break
        lines.append(f"{packet.utc_ms} {packet.source or '-'} {packet.direction} {packet.peer} "
                     f"map={packet.map:x} sync={packet.sync} size={packet.size} {packet.payload}")
    if repeats:
        lines.append(f"    (x{repeats + 1})")
    return lines


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("traces", nargs="+", type=pathlib.Path)
    parser.add_argument("--payloads", type=lambda text: int(text, 16), metavar="CH",
                        help="list the payloads of this channel (hex)")
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args(argv)
    packets: list[Packet] = []
    for path in args.traces:
        found = parse(path.read_text(encoding="utf-8", errors="replace"))
        print(f"{path.name}: {len(found)} packets ({found[0].source if found else '-'})")
        if found:
            seconds = (found[-1].utc_ms - found[0].utc_ms) / 1000.0
            print("\n".join(table(summarize(found), seconds)))
        packets += found
    if args.payloads is not None:
        packets.sort(key=lambda packet: packet.utc_ms)
        print(f"\npayloads of 0x{args.payloads:02x}:")
        print("\n".join(payload_lines(packets, args.payloads, args.limit)) or "  none traced")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
