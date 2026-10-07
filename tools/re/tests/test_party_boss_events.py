# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for party_boss_events.py on small hand-made event files.

Run: py -3.12 -m unittest discover -s tools\\re\\tests
"""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import party_boss_events as tool  # noqa: E402

HOST_ENTERED = 12414700
GUEST_ENTERED = 12414701


def flag_condition(group: int, flag: int) -> tuple[int, int, bytes]:
    return 3, 0, struct.pack('<BBBxi', group, 1, 0, flag)


def phantom_fog_event() -> list[tuple[int, int, bytes]]:
    """m24_01's 12414731: a white phantom walks into Gascoigne's fog once the host did."""
    return [
        (2000, 2, struct.pack('<i', 0)),
        flag_condition(1, 12411702),
        flag_condition(1, HOST_ENTERED),
        (4, 3, struct.pack('<bxxxiBxxx', 1, 10000, 1)),
        (3, 24, struct.pack('<bxxxii', 1, 2410040, 2411800)),
        (2003, 2, struct.pack('<iBxxx', GUEST_ENTERED, 1)),
    ]


def host_fog_event() -> list[tuple[int, int, bytes]]:
    """m24_00's 12404840: the fog stays shut for a client until the boss's entrance has run."""
    return [
        (1003, 5, struct.pack('<BBxx', 2, 1)),
        (2005, 3, struct.pack('<iBxxx', 2401800, 0)),
        flag_condition(2, 12411700),
        (4, 3, struct.pack('<bxxxiBxxx', 2, 10000, 0)),
        (3, 24, struct.pack('<bxxxii', 2, 2410040, 2411800)),
        (2003, 2, struct.pack('<iBxxx', HOST_ENTERED, 1)),
    ]


def boss_intro_event() -> list[tuple[int, int, bytes]]:
    """m24_01's 12411702: an alive player walking into the arena starts the boss's entrance."""
    return [
        flag_condition(1, 12411700),
        (4, 3, struct.pack('<bxxxiBxxx', 1, 10000, 0)),
        (3, 2, struct.pack('<bBxxii', 1, 1, 10000, 2412805)),
        (2003, 2, struct.pack('<iBxxx', 72410511, 1)),
        (2003, 2, struct.pack('<iBxxx', HOST_ENTERED, 1)),
    ]


def build_emevd(events: dict[int, list[tuple[int, int, bytes]]]) -> bytes:
    """An emevd laid out like the game's: events, instructions, arguments, params, linked
    files, strings."""
    records, args = [], bytearray()
    entries = []
    for event, instructions in events.items():
        entries.append((event, len(instructions), len(records) * 32))
        for bank, ident, payload in instructions:
            records.append((bank, ident, len(payload), len(args)))
            args += payload
            args += bytes(-len(args) % 4)
    params = b'P' * 24
    linked = struct.pack('<q', 0)
    strings = 'm24_01'.encode('utf-16-le') + bytes(4)
    event_offset = 144
    instruction_offset = event_offset + len(entries) * 48
    args_offset = instruction_offset + len(records) * 32
    params_offset = args_offset + len(args)
    linked_offset = params_offset + len(params)
    strings_offset = linked_offset + len(linked)
    total = strings_offset + len(strings)
    out = bytearray(b'EVD\0' + struct.pack('<III', 0xFF00, 0xCC, total))
    out += struct.pack('<16q', len(entries), event_offset, len(records), instruction_offset,
                       0, args_offset, 0, args_offset, 1, params_offset, 1, linked_offset,
                       len(args), args_offset, len(strings), strings_offset)
    for event, count, first in entries:
        out += struct.pack('<qqqqqii', event, count, first, 0, -1, 0, 0)
    for bank, ident, length, offset in records:
        out += struct.pack('<iiqqq', bank, ident, length, offset, -1)
    return bytes(out + args + params + linked + strings)


def read_events(emevd: bytes) -> dict[int, list[tuple[int, int, bytes]]]:
    fields = struct.unpack_from('<16q', emevd, 16)
    events = {}
    for index in range(fields[0]):
        event, count, first, *_ = struct.unpack_from('<qqqqqii', emevd, fields[1] + index * 48)
        events[event] = []
        for k in range(count):
            bank, ident, length, offset, _ = struct.unpack_from(
                '<iiqqq', emevd, fields[3] + first + k * 32)
            at = fields[13] + offset
            events[event].append((bank, ident, emevd[at:at + length]))
    return events


def strings_of(emevd: bytes) -> bytes:
    fields = struct.unpack_from('<16q', emevd, 16)
    return emevd[fields[15]:fields[15] + fields[14]]


class GuestsEnterBossFogFirst(unittest.TestCase):
    """A white phantom's boss fog event waits for the host's "entered" flag, so a guest could
    never enter first and was stuck behind the fog after a world reset."""

    def setUp(self):
        self.original = build_emevd({12414731: phantom_fog_event(),
                                     12414730: host_fog_event(),
                                     12411702: boss_intro_event()})
        self.patched, self.count = tool.open_boss_fog_for_guests(self.original)
        self.events = read_events(self.patched)

    def test_the_guest_no_longer_waits_for_the_host(self):
        bank, ident, payload = self.events[12414731][2]
        self.assertEqual((bank, ident), (3, 0))
        self.assertEqual(struct.unpack_from('<i', payload, 4)[0], HOST_ENTERED)
        self.assertEqual(payload[0], tool.UNUSED_GROUP)
        self.assertEqual(self.events[12414731][1][2][0], 1, 'other conditions stay')

    def test_a_guest_entering_counts_as_the_host_entering(self):
        # The boss starts (AI, HP bar) on the host's "entered" flag, so the guest sets it too.
        bank, ident, payload = self.events[12414731][5]
        self.assertEqual((bank, ident), (2003, 22))
        self.assertEqual(struct.unpack('<iiB', payload[:9]), (HOST_ENTERED, GUEST_ENTERED, 1))

    def test_a_guest_sees_the_fog_open_before_the_boss_entrance(self):
        # The host's fog event opens the fog until the boss's entrance, but skipped that for a
        # client, so a guest arriving first (Cleric Beast) faced a fog it could not enter.
        bank, ident, payload = self.events[12414730][0]
        self.assertEqual((bank, ident), (1003, 5))
        self.assertEqual(payload[:2], bytes([2, tool.NEVER]))
        self.assertEqual(self.events[12414730][1:], read_events(self.original)[12414730][1:])
        self.assertEqual(self.count, 4)

    def test_a_guest_walking_into_the_arena_starts_the_boss(self):
        # The boss's entrance waited for an alive (host) player in the arena.
        bank, ident, payload = self.events[12411702][1]
        self.assertEqual((bank, ident), (4, 3))
        self.assertEqual(payload[0], tool.UNUSED_GROUP)
        self.assertEqual(self.events[12411702][2][2][0], 1, 'the arena check stays')

    def test_the_rest_of_the_file_still_lines_up(self):
        self.assertEqual(strings_of(self.patched), strings_of(self.original))
        self.assertEqual(struct.unpack_from('<I', self.patched, 12)[0], len(self.patched))
        fields = struct.unpack_from('<16q', self.patched, 16)
        self.assertEqual(self.patched[fields[9]:fields[9] + 24], b'P' * 24)

    def test_patching_twice_changes_nothing(self):
        again, count = tool.open_boss_fog_for_guests(self.patched)
        self.assertEqual((again, count), (self.patched, 0))


if __name__ == '__main__':
    unittest.main()
