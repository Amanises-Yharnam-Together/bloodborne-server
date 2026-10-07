# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Lets a party guest finish boss defeat events (full boss loot for every party member).

Every boss defeat event (one that runs 2003:12, the boss kill) ends for a summoned guest at
1003:105 "end the event by multiplayer state" with state 1 (client), before the trophy
(2003:28), the boss item (2003:4 AwardItemLot) and the progress flags. This writes copies of
the map event files where those 1003:105 checks compare against a state that never happens, so
the guest runs the rest of the event too. Hosts and solo players are not affected (they are
never in state 1).

Usage: py -3.12 party_boss_events.py <mods dvdroot_ps4\\event> <game event dir> [<update event dir>]
(a file in a later directory replaces the same file in an earlier one, as the update does)
"""
import pathlib
import struct
import sys
import zlib

NEVER = 0x7F  # no multiplayer state has this value


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
        for bank, ident, length, offset, _ in instructions:
            at = args_offset + offset
            if bank == 1003 and ident == 105 and length >= 2 and data[at + 1] == 1:
                data[at + 1] = NEVER
                patched += 1
    return bytes(data), patched


def main() -> int:
    target = pathlib.Path(sys.argv[1])
    files: dict[str, pathlib.Path] = {}
    for source in sys.argv[2:]:
        for path in pathlib.Path(source).glob('m*.emevd.dcx'):
            files[path.name] = path
    target.mkdir(parents=True, exist_ok=True)
    total = 0
    for _, path in sorted(files.items()):
        original = path.read_bytes()
        emevd, patched = patch_events(load_dcx(original))
        if patched:
            (target / path.name).write_bytes(save_dcx(original, emevd))
            print(f'{path.name}: {patched} boss event checks')
            total += patched
    print(f'total: {total}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
