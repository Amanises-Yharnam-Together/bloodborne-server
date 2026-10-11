# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Lets a party guest finish boss defeat events (full boss loot for every party member).

Every boss defeat event (one that runs 2003:12, the boss kill) ends for a summoned guest at
1003:105 "end the event by multiplayer state" with state 1 (client), before the trophy
(2003:28), the boss item (2003:4 AwardItemLot) and the progress flags. This writes copies of
the map event files where those 1003:105 checks compare against a state that never happens, so
the guest runs the rest of the event too. Hosts and solo players are not affected (they are
never in state 1).

It also patches common.emevd's death events (9400: the first death wakes in the Hunter's Dream,
9404: the Dream after insight, 9421: a kidnapping wakes in the Gaol). They set where a death
wakes the player but end for a client (1003:6 state 1), so a party guest runs them too; the
emulator then takes the guest there (ChooseDeathRespawn). The Hunter's Dream's first visit
(m21_00 event 9401: the cutscene and the first Yharnam lamp, 12417810) ends for a client the
same way, which left a guest's headstone with no place to awaken.

Boss fogs (open_boss_fog_for_guests): a white phantom's fog event waited for the host's
"entered" flag, so a party guest could not enter a boss fog first (nor after a world reset). It
no longer waits, and it sets the host's flag too, which starts the boss; the emulator posts that
flag to the host (IsBossFogEntryFlag). Boss entrances triggered by walking into the arena accept
a phantom too.

Events are kept in step between the players of a session unless they start with 2000:2 (network
sync) 0, as the first visit does. The Hunter's Dream messengers' gift events (m21_00
12101020-12101028) get that instruction, so one player's gift pick no longer finishes the other's
gift event (the emulator also keeps their flags local, KeepsLocalFlag).

Usage: py -3.12 party_boss_events.py <mods dvdroot_ps4\\event> <game event dir> [<update event dir>]
(a file in a later directory replaces the same file in an earlier one, as the update does)
"""
import pathlib
import struct
import sys
import zlib

NEVER = 0x7F  # no multiplayer state has this value
MAIN_GROUP = 0
UNUSED_GROUP = 7  # AND group 7, which no boss event waits on
LOCAL_PLAYER = 10000
AWARD_ITEM_LOT_INCLUDING_CLIENTS = 36  # 2003:36, "クライアントでもアイテム取得"


def load_dcx(data: bytes) -> bytes:
    return zlib.decompress(data[0x4C:])


def save_dcx(original: bytes, payload: bytes) -> bytes:
    packed = zlib.compress(payload, 9)
    return (original[:0x1C] + struct.pack('>II', len(payload), len(packed)) +
            original[0x24:0x4C] + packed)


def patch_events(emevd: bytes) -> tuple[bytes, int]:
    data = bytearray(emevd)
    fields = struct.unpack_from('<16q', data, 16)
    event_count, event_offset, _, instruction_offset = fields[0], fields[1], fields[2], fields[3]
    args_offset = fields[13]
    patched = 0
    for index in range(event_count):
        _, count, first, _, _, _, _ = struct.unpack_from('<qqqqqii', data, event_offset + index * 48)
        instructions = [struct.unpack_from('<iiqqq', data, instruction_offset + first + k * 32)
                        for k in range(count)]
        if not any(bank == 2003 and ident == 12 for bank, ident, _, _, _ in instructions):
            continue
        for number, (bank, ident, length, offset, _) in enumerate(instructions):
            at = args_offset + offset
            # 2003:4 "award item lot" gives nothing to a client; 2003:36 is the same award
            # "even for a client" (names in common.emedf.dcx), so each member gets the boss item.
            if bank == 2003 and ident == 4:
                struct.pack_into('<i', data, instruction_offset + first + number * 32 + 4,
                                 AWARD_ITEM_LOT_INCLUDING_CLIENTS)
                patched += 1
            if bank == 1003 and ident == 105 and length >= 2 and data[at + 1] == 1:
                data[at + 1] = NEVER
                patched += 1
            # 4:3 "wait until the player (10000) is character type 0 (alive)" in the main
            # condition group blocks a white phantom forever. Moved into a condition group no
            # instruction waits on, it no longer blocks anyone (a host passes it at once anyway).
            if (bank == 4 and ident == 3 and length >= 12 and data[at] == MAIN_GROUP and
                    struct.unpack_from('<i', data, at + 4)[0] == LOCAL_PLAYER):
                data[at] = UNUSED_GROUP
                patched += 1
    return bytes(data), patched


# Events that end for a client (1003:6 state 1) but must run for a party guest, per file.
# 9350 awards insight (SpEffect 4680 once per point; see let_guests_earn_insight).
CLIENT_EVENTS = {
    'common.emevd.dcx': {9350, 9400, 9404, 9421},
    'm21_00_00_00.emevd.dcx': {9401},
}
INSIGHT_EVENT = 9350
INITIALIZE_EVENT = (2000, 0)


def let_guests_earn_insight(emevd: bytes) -> tuple[bytes, int]:
    """Every insight award (a boss found or killed, a place reached) is a map event starting
    common event 9350 with the points (2000:0). Some of those end for a client (1003:6 state 1)
    or unless the local player is alive (4:3 player 10000 type 0, in a group an END tests or in
    the main group), so a party guest, a white phantom, never got the insight (Kaniel
    2026-10-11). In those events the client end never matches and the alive check moves to a
    condition group nothing tests (an empty group passes)."""
    data = bytearray(emevd)
    fields = struct.unpack_from('<16q', data, 16)
    event_count, event_offset, instruction_offset = fields[0], fields[1], fields[3]
    args_offset = fields[13]
    patched = 0
    for index in range(event_count):
        _, count, first, _, _, _, _ = struct.unpack_from('<qqqqqii', data, event_offset + index * 48)
        instructions = [struct.unpack_from('<iiqqq', data, instruction_offset + first + k * 32)
                        for k in range(count)]
        if not any((bank, ident) == INITIALIZE_EVENT and length >= 8 and
                   struct.unpack_from('<i', data, args_offset + offset + 4)[0] == INSIGHT_EVENT
                   for bank, ident, length, offset, _ in instructions):
            continue
        for bank, ident, length, offset, _ in instructions:
            at = args_offset + offset
            if bank == 1003 and ident == 6 and length >= 2 and data[at + 1] == 1:
                data[at + 1] = NEVER
                patched += 1
            if (bank == 4 and ident == 3 and length >= 9 and data[at] != UNUSED_GROUP and
                    struct.unpack_from('<i', data, at + 4)[0] == LOCAL_PLAYER and
                    data[at + 8] == ALIVE):
                data[at] = UNUSED_GROUP
                patched += 1
    return bytes(data), patched


def patch_client_events(emevd: bytes, wanted: set[int]) -> tuple[bytes, int]:
    data = bytearray(emevd)
    fields = struct.unpack_from('<16q', data, 16)
    event_count, event_offset, _, instruction_offset = fields[0], fields[1], fields[2], fields[3]
    args_offset = fields[13]
    patched = 0
    for index in range(event_count):
        event, count, first, _, _, _, _ = struct.unpack_from('<qqqqqii', data,
                                                             event_offset + index * 48)
        if event not in wanted:
            continue
        for k in range(count):
            bank, ident, length, offset, _ = struct.unpack_from(
                '<iiqqq', data, instruction_offset + first + k * 32)
            at = args_offset + offset
            # 1003:6 "end the event by multiplayer state" (end type, state): state 1 is a client.
            if bank == 1003 and ident == 6 and length >= 2 and data[at + 1] == 1:
                data[at + 1] = NEVER
                patched += 1
    return bytes(data), patched


# Events that get 2000:2 0 (no network sync) as their first instruction, per file.
LOCAL_EVENTS = {
    'm21_00_00_00.emevd.dcx': {12101020, 12101021, 12101022, 12101024, 12101026, 12101028},
}
NETWORK_SYNC = (2000, 2)
INSTRUCTION_SIZE = 32
# Header offset fields after the instruction table (DS3 layout): unknown, layers, params, linked
# files, arguments, strings.
SHIFTED_OFFSET_FIELDS = (5, 7, 9, 11, 13, 15)


def make_events_local(emevd: bytes, wanted: set[int]) -> tuple[bytes, int]:
    data = bytearray(emevd)
    fields = list(struct.unpack_from('<16q', data, 16))
    event_count, event_offset = fields[0], fields[1]
    instruction_count, instruction_offset = fields[2], fields[3]
    args_offset, args_size = fields[13], fields[12]
    events = [list(struct.unpack_from('<qqqqqii', data, event_offset + index * 48))
              for index in range(event_count)]
    targets = sorted((event[2] for event in events
                      if event[0] in wanted and event[1] > 0 and
                      not already_local(data, instruction_offset, args_offset, event)),
                     reverse=True)
    if not targets:
        return emevd, 0
    # The instruction's argument (4 bytes of 0) can share any aligned zero word of the table.
    zero = next(at for at in range(0, args_size - 3, 4)
                if data[args_offset + at:args_offset + at + 4] == bytes(4))
    record = struct.pack('<iiqqq', NETWORK_SYNC[0], NETWORK_SYNC[1], 4, zero, -1)
    instructions_end = instruction_offset + instruction_count * INSTRUCTION_SIZE
    table = bytearray(data[instruction_offset:instructions_end])
    for first in targets:
        table[first:first] = record
    for event in events:
        moved = sum(1 for first in targets if event[2] > first)
        if event[0] in wanted and event[2] in targets:
            event[1] += 1
        event[2] += moved * INSTRUCTION_SIZE
    delta = len(targets) * INSTRUCTION_SIZE
    out = data[:instruction_offset] + table + data[instructions_end:]
    for index, event in enumerate(events):
        struct.pack_into('<qqqqqii', out, event_offset + index * 48, *event)
    fields[2] += len(targets)
    for field in SHIFTED_OFFSET_FIELDS:
        if fields[field] >= instructions_end:
            fields[field] += delta
    struct.pack_into('<16q', out, 16, *fields)
    struct.pack_into('<I', out, 0x0C, struct.unpack_from('<I', out, 0x0C)[0] + delta)
    return bytes(out), len(targets)


SET_EVENT_FLAG = (2003, 2)
BATCH_SET_EVENT_FLAGS = (2003, 22)
ALIVE = 0
WHITE_PHANTOM = 1
ARGS_ALIGNMENT = 16


def open_boss_fog_for_guests(emevd: bytes) -> tuple[bytes, int]:
    """A white phantom's boss fog event (e.g. m24_01 12414731) waits for the host's "entered
    the fog" flag H (12414700), which the host's own fog event sets, and then sets H + 1. A
    party guest could therefore never enter first, and after a world reset it was stuck behind
    the fog until the host went in again. The wait on H moves to an unused condition group, and
    the final "set H + 1" becomes "set H..H + 1", so a guest entering starts the boss (the boss
    start event waits on H) like a host entering does. The host's fog event (which sets H) also
    opens the fog for a client until the boss's entrance has run. A boss entrance that sets H when a
    player walks into the arena (m24_01 12411702) waits for an alive player; a white phantom
    passes too."""
    data = bytearray(emevd)
    fields = list(struct.unpack_from('<16q', data, 16))
    event_count, event_offset, instruction_offset = fields[0], fields[1], fields[3]
    args_offset = fields[13]
    new_args = bytearray()
    patched = 0
    host_entered_flags = set()

    def read_event(index):
        _, count, first, _, _, _, _ = struct.unpack_from('<qqqqqii', data, event_offset + index * 48)
        at = [instruction_offset + first + k * INSTRUCTION_SIZE for k in range(count)]
        instructions = [struct.unpack_from('<iiqqq', data, place) for place in at]
        args = [bytes(data[args_offset + offset:args_offset + offset + length])
                for _, _, length, offset, _ in instructions]
        return at, instructions, args

    def player_type_checks(instructions, args, player_type):
        return [k for k, (bank, ident, *_) in enumerate(instructions)
                if (bank, ident) == (4, 3) and len(args[k]) >= 9 and
                struct.unpack_from('<i', args[k], 4)[0] == LOCAL_PLAYER and
                args[k][8] == player_type]

    for index in range(event_count):
        at, instructions, args = read_event(index)
        phantom = player_type_checks(instructions, args, WHITE_PHANTOM)
        button = any((bank, ident) == (3, 24) for bank, ident, *_ in instructions)
        if not (phantom and button):
            continue
        sets = [k for k, (bank, ident, *_) in enumerate(instructions)
                if (bank, ident) == SET_EVENT_FLAG and args[k][4] == 1]
        if len(sets) != 1:
            continue
        entered = struct.unpack_from('<i', args[sets[0]], 0)[0]
        host_entered = entered - 1
        waits = [k for k, (bank, ident, *_) in enumerate(instructions)
                 if (bank, ident) == (3, 0) and args[k][1] == 1 and args[k][0] != UNUSED_GROUP and
                 struct.unpack_from('<i', args[k], 4)[0] == host_entered]
        if not waits:
            continue
        host_entered_flags.add(host_entered)
        for k in waits:
            data[args_offset + instructions[k][3]] = UNUSED_GROUP
            patched += 1
        offset = fields[12] + len(new_args)
        new_args += struct.pack('<iiBxxx', host_entered, entered, 1)
        struct.pack_into('<iiqqq', data, at[sets[0]], *BATCH_SET_EVENT_FLAGS, 12, offset,
                         instructions[sets[0]][4])
        patched += 1
    for index in range(event_count):
        at, instructions, args = read_event(index)
        button = any((bank, ident) == (3, 24) for bank, ident, *_ in instructions)
        area = any((bank, ident) == (3, 2) for bank, ident, *_ in instructions)
        starts_boss = any((bank, ident) == SET_EVENT_FLAG and args[k][4] == 1 and
                          struct.unpack_from('<i', args[k], 0)[0] in host_entered_flags
                          for k, (bank, ident, *_) in enumerate(instructions))
        if button and starts_boss and player_type_checks(instructions, args, ALIVE):
            # The host's fog event: it opens the fog until the boss's entrance has run, but
            # skips that for a client (1003:5 state 1), so a guest arriving first faced a shut
            # fog with no way in.
            for k, (bank, ident, length, offset, _) in enumerate(instructions):
                if (bank, ident) == (1003, 5) and length >= 2 and args[k][1] == 1:
                    data[args_offset + offset + 1] = NEVER
                    patched += 1
            continue
        if button or not area or not starts_boss:
            continue
        for k in player_type_checks(instructions, args, ALIVE):
            if 0 < args[k][0] < UNUSED_GROUP:
                data[args_offset + instructions[k][3]] = UNUSED_GROUP
                patched += 1
    if not new_args:
        return bytes(data), patched
    new_args += bytes(-(fields[12] + len(new_args)) % ARGS_ALIGNMENT)
    args_end = args_offset + fields[12]
    out = data[:args_end] + new_args + data[args_end:]
    for field in SHIFTED_OFFSET_FIELDS:
        if field != 13 and fields[field] >= args_end:
            fields[field] += len(new_args)
    fields[12] += len(new_args)
    struct.pack_into('<16q', out, 16, *fields)
    struct.pack_into('<I', out, 0x0C, struct.unpack_from('<I', out, 0x0C)[0] + len(new_args))
    return bytes(out), patched


def already_local(data: bytes, instruction_offset: int, args_offset: int, event: list) -> bool:
    bank, ident, _, offset, _ = struct.unpack_from('<iiqqq', data, instruction_offset + event[2])
    return ((bank, ident) == NETWORK_SYNC and
            data[args_offset + offset:args_offset + offset + 4] == bytes(4))


def main() -> int:
    target = pathlib.Path(sys.argv[1])
    files: dict[str, pathlib.Path] = {}
    for source in sys.argv[2:]:
        for path in pathlib.Path(source).glob('*.emevd.dcx'):
            files[path.name] = path
    target.mkdir(parents=True, exist_ok=True)
    total = 0
    for name, path in sorted(files.items()):
        original = path.read_bytes()
        emevd = load_dcx(original)
        boss = client = fog = insight = 0
        if name.startswith('m'):
            emevd, boss = patch_events(emevd)
            emevd, fog = open_boss_fog_for_guests(emevd)
            emevd, insight = let_guests_earn_insight(emevd)
        local = 0
        if name in CLIENT_EVENTS:
            emevd, client = patch_client_events(emevd, CLIENT_EVENTS[name])
        if name in LOCAL_EVENTS:
            emevd, local = make_events_local(emevd, LOCAL_EVENTS[name])
        if boss or client or local or fog or insight:
            (target / name).write_bytes(save_dcx(original, emevd))
            print(f'{name}: {boss} boss event checks, {fog} boss fog checks, '
                  f'{insight} insight event checks, {client} client event ends, '
                  f'{local} local events')
            total += boss + client + local + fog + insight
    print(f'total: {total}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
