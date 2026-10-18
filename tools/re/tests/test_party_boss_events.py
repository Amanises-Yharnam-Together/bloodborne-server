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


def build_emevd(events: dict[int, list[tuple[int, int, bytes]]],
                parameters: dict[int, list[tuple[int, int, int, int]]] | None = None) -> bytes:
    """An emevd laid out like the game's: events, instructions, arguments, params, linked
    files, strings. `parameters`: per event, (instruction index, target byte, source byte,
    length) entries."""
    parameters = parameters or {}
    records, args = [], bytearray()
    entries = []
    param_table = bytearray()
    for event, instructions in events.items():
        own = parameters.get(event, [])
        entries.append((event, len(instructions), len(records) * 32, len(own),
                        len(param_table) if own else -1))
        for entry in own:
            param_table += struct.pack('<qqqii', *entry, 0)
        for bank, ident, payload in instructions:
            records.append((bank, ident, len(payload), len(args)))
            args += payload
            args += bytes(-len(args) % 4)
    params = bytes(param_table) or b'P' * 24
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
                       0, args_offset, 0, args_offset, max(len(param_table) // 32, 1),
                       params_offset, 1, linked_offset, len(args), args_offset, len(strings),
                       strings_offset)
    for event, count, first, param_count, param_offset in entries:
        out += struct.pack('<qqqqqii', event, count, first, param_count, param_offset, 0, 0)
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


def init_insight(amount: int) -> tuple[int, int, bytes]:
    return 2000, 0, struct.pack('<iii', 0, tool.INSIGHT_EVENT, amount)


class GuestsEarnInsight(unittest.TestCase):
    """Kaniel 2026-10-11: party guests never got insight from finding or killing bosses. Every
    insight award runs common event 9350 (SpEffect 4680 once per point), which ends for a client,
    and some of the map events that start it end for a client or wait for an alive player."""

    def test_the_insight_award_runs_for_a_client(self):
        award = [(1003, 6, struct.pack('<BBxx', 0, 1)),
                 (2004, 8, struct.pack('<iiB', 10000, 4680, 0))]
        patched, count = tool.patch_client_events(build_emevd({9350: award}),
                                                  tool.CLIENT_EVENTS['common.emevd.dcx'])
        self.assertEqual(read_events(patched)[9350][0][2][:2], bytes([0, tool.NEVER]))
        self.assertEqual(count, 1)

    def test_the_events_that_award_insight_run_for_a_guest(self):
        original = build_emevd({
            # m24_01 12410995: a spot that gives 1 insight, ends for a client.
            12410995: [(1003, 6, struct.pack('<BBxx', 0, 1)), init_insight(1)],
            # m24_01 12410834: ends unless the player is alive (group 15).
            12410834: [(4, 3, struct.pack('<bxxxiBxxx', 15, 10000, 0)),
                       (1000, 2, struct.pack('<BBbx', 0, 0, 15)), init_insight(3)],
            # No insight: left alone.
            12410996: [(1003, 6, struct.pack('<BBxx', 0, 1)),
                       (4, 3, struct.pack('<bxxxiBxxx', 1, 10000, 0))],
        })
        patched, count = tool.let_guests_earn_insight(original)
        events = read_events(patched)
        self.assertEqual(events[12410995][0][2][:2], bytes([0, tool.NEVER]))
        self.assertEqual(events[12410834][0][2][0], tool.UNUSED_GROUP,
                         'an alive check no longer ends the event for a phantom')
        self.assertEqual(events[12410834][1:], read_events(original)[12410834][1:])
        self.assertEqual(events[12410996], read_events(original)[12410996])
        self.assertEqual(count, 2)
        self.assertEqual(tool.let_guests_earn_insight(patched), (patched, 0))

    def test_only_the_local_players_alive_check_moves(self):
        original = build_emevd({13401801: [
            (4, 3, struct.pack('<bxxxiBxxx', 1, 10000, 2)),  # a black phantom: stays
            (4, 3, struct.pack('<bxxxiBxxx', 2, 2410800, 0)),  # another character: stays
            init_insight(1)]})
        patched, count = tool.let_guests_earn_insight(original)
        self.assertEqual((patched, count), (original, 0))


class GuestsUseTheDreamShops(unittest.TestCase):
    """Kaniel 2026-10-11: guests could not use the insight shop, and often did not see the
    Hunter Chief Emblem (10,000 echoes) at the messengers. The insight messenger (m21_00 event
    12105043, 2100219) wakes, and sets the flag its talk opens the shop on (12105045), only for a
    non-client; the messengers' shop lines (event 12101000, slots 0-9: flags 12101000-12101009,
    e.g. 12101004 for the emblem) are kept in step with a host whose Dream is not there."""

    DREAM = 'm21_00_00_00.emevd.dcx'

    def test_the_insight_messenger_wakes_for_a_guest(self):
        wake = [(2000, 2, struct.pack('<i', 0)),
                (1003, 101, struct.pack('<BBxxi', 0, 1, 12100320)),
                (1003, 6, struct.pack('<BBxx', 0, 1)),
                (2003, 2, struct.pack('<iBxxx', 12105045, 1))]
        patched, count = tool.patch_client_events(build_emevd({12105043: wake}),
                                                  tool.CLIENT_EVENTS[self.DREAM])
        self.assertEqual(read_events(patched)[12105043][2][2][:2], bytes([0, tool.NEVER]))
        self.assertEqual(count, 1)

    def test_the_shop_lines_unlock_in_the_guests_own_dream(self):
        lines = [(1003, 2, struct.pack('<BBBxi', 0, 1, 0, 0)),
                 (3, 4, struct.pack('<BBxxiB', 0, 3, 0, 1)),
                 (2004, 26, struct.pack('<ii', 0, 1))]
        patched, count = tool.make_events_local(build_emevd({12101000: lines}),
                                                tool.LOCAL_EVENTS[self.DREAM])
        self.assertEqual(read_events(patched)[12101000][0], (2000, 2, bytes(4)))
        self.assertEqual(count, 1)


class GuestsEnterChaliceDungeons(unittest.TestCase):
    """Team altars (2026-10-18): a party guest joined its teammate's chalice dungeon from Chalice
    Dungeon Search and picked its layer, but never warped. The altar's talk sets the layer flag
    (72100420 + altar); m21_00 event 12107100 (a slot per altar) waits on it and warps, but ends
    for a client (1003:6 state 1). For a guest it runs too, and only in the guest's own game:
    one player's warp is not the other's."""

    DREAM = 'm21_00_00_00.emevd.dcx'
    WARP = [(1003, 6, struct.pack('<BBxx', 0, 1)),
            (3, 0, struct.pack('<BBxxi', 0, 1, 0)),
            (1001, 0, struct.pack('<f', 4.0)),
            (2003, 2, struct.pack('<iBxxx', 9020, 0)),
            (2004, 14, struct.pack('<iiii', 10000, 0, 101164, 0))]

    def test_the_altar_warp_runs_for_a_guest(self):
        patched, count = tool.patch_client_events(build_emevd({12107100: self.WARP}),
                                                  tool.CLIENT_EVENTS[self.DREAM])
        self.assertEqual(read_events(patched)[12107100][0][2][:2], bytes([0, tool.NEVER]))
        self.assertEqual(count, 1)

    def test_the_altar_warp_is_each_players_own(self):
        patched, count = tool.make_events_local(build_emevd({12107100: self.WARP}),
                                                tool.LOCAL_EVENTS[self.DREAM])
        self.assertEqual(read_events(patched)[12107100][0], (2000, 2, bytes(4)))
        self.assertEqual(count, 1)


class GuestsSeeTheNightFall(unittest.TestCase):
    """Kaniel's playtest 2026-10-17: after Vicar Amelia the host examined the altar's skull, saw
    the cutscene and the night fell; the guests saw neither. The skull's event (m24_00 12401803:
    the cutscene and time of day 2, 2002:6) ends for a client (1003:6 state 1) before it waits for
    the host's press, so a guest's copy was gone when the host's ran."""

    WARD = 'm24_00_00_00.emevd.dcx'

    def skull_event(self) -> list[tuple[int, int, bytes]]:
        return [(2006, 1, struct.pack('<iBxxx', 2403413, 0)),
                (1003, 2, struct.pack('<BBBxi', 0, 1, 1, 0)),
                (1003, 101, struct.pack('<BBxxi', 0, 1, 12401800)),
                flag_condition(0, 12401800),
                (1014, 0, b''),
                (2006, 2, struct.pack('<i', 2403413)),
                (1003, 6, struct.pack('<BBxx', 0, 1)),
                (4, 3, struct.pack('<bxxxiBxxx', 1, 10000, 0)),
                (3, 24, struct.pack('<bxxxii', 1, 2400010, 2401801)),
                (2003, 2, struct.pack('<iBxxx', 9180, 1)),
                (2002, 6, struct.pack('<iiiiii', 24000030, 0, -1, 24, 10000, 2))]

    def test_the_skull_event_waits_on_a_guest_too(self):
        original = build_emevd({12401803: self.skull_event()})
        patched, count = tool.patch_client_events(original, tool.CLIENT_EVENTS[self.WARD])
        events, before = read_events(patched)[12401803], read_events(original)[12401803]
        self.assertEqual(events[6][2][:2], bytes([0, tool.NEVER]))
        self.assertEqual(count, 1)
        # Only the host's press starts it: a guest's own press must not (its alive check stays).
        self.assertEqual(events[7], before[7])
        self.assertEqual(events[:6] + events[7:], before[:6] + before[7:])


class LocalEventsKeepTheirParameters(unittest.TestCase):
    """Kaniel 2026-10-12: after the Dream patch the messengers sold weapons whose badges nobody
    had. Event 12101000 is started per slot with arguments (2000:0) that its parameter entries
    copy into instructions by index; the inserted 2000:2 moved every instruction one down, so the
    item check (3:4, instruction 4) took nothing and the shop lines opened."""

    def test_the_parameters_follow_their_instructions(self):
        lines = [(1003, 2, struct.pack('<BBBxi', 0, 1, 0, 0)),
                 (3, 4, struct.pack('<BBxxiB', 0, 3, 0, 1)),
                 (2004, 26, struct.pack('<ii', 0, 1))]
        original = build_emevd({12101000: lines},
                               {12101000: [(0, 4, 0, 4), (1, 4, 4, 4), (2, 0, 8, 4)]})
        patched, count = tool.make_events_local(original, {12101000})
        self.assertEqual(count, 1)
        self.assertEqual(parameters_of(patched, 12101000),
                         [(1, 4, 0, 4), (2, 4, 4, 4), (3, 0, 8, 4)])
        self.assertEqual(tool.make_events_local(patched, {12101000}), (patched, 0))

    def test_an_event_an_earlier_insert_moved_keeps_them_too(self):
        # 2026-10-18: the Dream's altar warp (12107100) comes after the shop events; the insert
        # before it moved its instructions, and its parameters were left behind.
        first = [(1003, 2, struct.pack('<BBBxi', 0, 1, 0, 0))]
        warp = [(1003, 6, struct.pack('<BBxx', 0, 1)), (3, 0, struct.pack('<BBxxi', 0, 1, 0))]
        original = build_emevd({12101000: first, 12107100: warp}, {12107100: [(1, 4, 0, 4)]})
        patched, count = tool.make_events_local(original, {12101000, 12107100})
        self.assertEqual(count, 2)
        self.assertEqual(parameters_of(patched, 12107100), [(2, 4, 0, 4)])


def parameters_of(emevd: bytes, wanted: int) -> list[tuple[int, int, int, int]]:
    fields = struct.unpack_from('<16q', emevd, 16)
    for index in range(fields[0]):
        event, _, _, count, offset, _, _ = struct.unpack_from('<qqqqqii', emevd,
                                                              fields[1] + index * 48)
        if event == wanted:
            return [struct.unpack_from('<qqqi', emevd, fields[9] + offset + k * 32)[:4]
                    for k in range(count)]
    return []


if __name__ == '__main__':
    unittest.main()
