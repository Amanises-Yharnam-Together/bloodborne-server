# SPDX-License-Identifier: GPL-2.0-or-later
"""The co-op screens' own art (scripts/coop_art.py): the atlas the overlay embeds holds the pieces in the order
shim/bbport_coop_art.h names them, and the party HUD's strip has a frame for each of hud_patch's icons."""
from paths import ROOT  # noqa: F401 (puts scripts/ on the path)
from pathlib import Path
import re
import struct
import unittest
import zlib

import coop_art
import hud_patch

PORT = Path(__file__).resolve().parents[1]


class CoopArtTests(unittest.TestCase):
    def test_the_pieces_are_the_headers(self):
        header = (PORT / 'gpu/shim/bbport_coop_art.h').read_text(encoding='utf-8')
        body = header[header.index('enum class Piece {'):header.index('Count')]
        names = re.findall(r'^\s+(\w+),', body, re.M)
        camel = [''.join(part.capitalize() for part in re.split(r'[-_]', name)) for name, _ in coop_art.PIECES]
        self.assertEqual(names, camel)

    def test_the_atlas_holds_every_piece(self):
        width, height, rects, rgba = coop_art.decode_atlas(coop_art.ATLAS.read_bytes())
        self.assertEqual(len(rects), len(coop_art.PIECES))
        for x, y, w, h in rects:
            self.assertTrue(w > 0 and h > 0 and x + w <= width and y + h <= height)
        # No two pieces overlap.
        for i, a in enumerate(rects):
            for b in rects[i + 1:]:
                self.assertTrue(a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or a[1] + a[3] <= b[1] or
                                b[1] + b[3] <= a[1])

    def test_the_pack_round_trips(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow is not installed')
        images = [Image.new('RGBA', (30 + 7 * i, 20 + 3 * i), (i, 2 * i, 3 * i, 255)) for i in range(5)]
        width, height, rects, rgba = coop_art.decode_atlas(coop_art.encode_atlas(images, 256))
        self.assertEqual((width, height), (256, 256))
        for i, (x, y, w, h) in enumerate(rects):
            self.assertEqual((w, h), images[i].size)
            at = (y * width + x) * 4
            self.assertEqual(tuple(rgba[at:at + 4]), (i, 2 * i, 3 * i, 255))

    def test_the_hud_strip_has_a_frame_per_icon_and_the_portrait_frame(self):
        data = coop_art.HUD_STRIP.read_bytes()
        self.assertEqual(data[:4], b'BBIC')
        width, height, count = struct.unpack_from('<HHH', data, 4)
        self.assertEqual(count, len(hud_patch.HUD_ICON_FRAMES) + 1)
        self.assertEqual((width, height), (coop_art.ICON * count, coop_art.ICON))
        rgba = zlib.decompress(data[10:])
        alpha = lambda frame, x, y: rgba[(y * width + frame * coop_art.ICON + x) * 4 + 3]
        self.assertEqual(alpha(0, 64, 64), 0, 'none: nothing')
        for frame in range(1, count - 1):
            self.assertGreater(alpha(frame, 64, 64), 200, f'frame {frame}: a badge')
            self.assertEqual(alpha(frame, 1, 1), 0, f'frame {frame}: round')
        self.assertEqual(alpha(count - 1, 64, 64), 0, 'the portrait shows through its frame')
        # The downed drop's ring fills with red: none at 0%, the top right at 30%, all of it at 100%.
        red = lambda frame, x, y: rgba[(y * width + frame * coop_art.ICON + x) * 4] > 180 and \
            rgba[(y * width + frame * coop_art.ICON + x) * 4 + 1] < 90
        first = len(hud_patch.HUD_ICON_FRAMES) - coop_art.DOWNED_STEPS
        radius = round(coop_art.DOWNED_RING[0])
        self.assertFalse(red(first, 64 + radius, 64))
        self.assertTrue(red(first + 3, 64 + radius, 64 - 4) or red(first + 3, 64 + radius - 1, 64 - 8))
        self.assertTrue(red(first + 10, 64 - radius, 64))


if __name__ == '__main__':
    unittest.main()
