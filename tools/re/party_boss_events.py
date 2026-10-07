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
CLIENT_EVENTS = {
    'common.emevd.dcx': {9400, 9404, 9421},
    'm21_00_00_00.emevd.dcx': {9401},
}


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
        boss = client = 0
        if name.startswith('m'):
            emevd, boss = patch_events(emevd)
        if name in CLIENT_EVENTS:
            emevd, client = patch_client_events(emevd, CLIENT_EVENTS[name])
        if boss or client:
            (target / name).write_bytes(save_dcx(original, emevd))
            print(f'{name}: {boss} boss event checks, {client} client event ends')
            total += boss + client
    print(f'total: {total}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
