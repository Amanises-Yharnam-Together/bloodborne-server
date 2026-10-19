"""Button icons that follow the device in use (bbport.ini button_icons=auto): the Switch set, the pad families, and
the prompt sets file the port swaps the game's icons from while it runs (launcher/bbport_icons.py
write_prompt_sets, read by gpu/shim/bbport_prompts.cpp). Nothing here reads game files.
Run: python -m unittest test_prompt_sets"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'launcher'))
sys.path.insert(0, str(ROOT / 'tests'))

import bbport_controls as controls  # noqa: E402
import bbport_icons as icons  # noqa: E402
from test_button_icons import atlas_image, make_tpf, real_layout  # noqa: E402

NINTENDO_GUID = '030000007e0500000920000000000000'  # vendor 0x057E: a Switch Pro Controller


def read_prompt_sets(data):
    """(sets, [(name, width, height, offsets, {set: bytes})]) of a prompt sets file."""
    assert data[:8] == b'BBPRMPT1', data[:8]
    set_count, entry_count = struct.unpack_from('<II', data, 8)
    at = 16
    sets = []
    for _ in range(set_count):
        length = data[at]
        sets.append(data[at + 1:at + 1 + length].decode())
        at += 1 + length
    entries = []
    for _ in range(entry_count):
        length = data[at]
        name = data[at + 1:at + 1 + length].decode()
        at += 1 + length
        width, height, pieces = struct.unpack_from('<III', data, at)
        at += 12
        offsets = list(struct.unpack_from(f'<{pieces}I', data, at))
        at += 4 * pieces
        payloads = {}
        for set_name in sets:
            payloads[set_name] = data[at:at + 16 * pieces]
            at += 16 * pieces
        entries.append((name, width, height, offsets, payloads))
    assert at == len(data), (at, len(data))
    return sets, entries


class SwitchTests(unittest.TestCase):
    def test_switch_icons_follow_the_pad_bindings_with_nintendo_legends(self):
        plan = icons.plan('switch', {})
        self.assertEqual(plan['KG_Cancel'].tokens, [('pad', 'a')])  # the bottom face button
        self.assertEqual(icons.pad_legend('switch', 'a')[0], 'B')
        self.assertEqual(icons.pad_legend('switch', 'b')[0], 'A')
        self.assertEqual(icons.pad_legend('switch', 'x')[0], 'Y')
        self.assertEqual(icons.pad_legend('switch', 'y')[0], 'X')
        self.assertEqual(icons.pad_legend('xbox', 'a')[0], 'A')
        self.assertEqual([icons.pad_legend('switch', b)[0] for b in
                          ('leftshoulder', 'rightshoulder', 'lefttrigger', 'righttrigger', 'start', 'back')],
                         ['L', 'R', 'ZL', 'ZR', '+', '-'])

    def test_every_switch_glyph_is_drawn_and_differs_from_the_xbox_face_buttons(self):
        switch, xbox = icons.build_glyphs('switch', {}), icons.build_glyphs('xbox', {})
        self.assertEqual(set(switch), set(icons.GLYPH_NAMES))
        for name in ('KG_Cancel', 'KG_OK', 'KG_L2', 'KG_Start'):
            self.assertNotEqual(switch[name].tobytes(), xbox[name].tobytes(), name)
        for image in switch.values():
            self.assertEqual(image.size, (32, 32))
            self.assertTrue(image.getchannel('A').getbbox())

    def test_a_nintendo_pad_means_switch_icons(self):
        self.assertEqual(controls.pad_family(NINTENDO_GUID, 'Anything'), 'switch')
        for name in ('Nintendo Switch Pro Controller', 'Joy-Con (L/R)', 'Pro Controller'):
            self.assertEqual(controls.pad_family('', name), 'switch', name)
        self.assertEqual(controls.resolve_icon_set('auto', [(NINTENDO_GUID, 'Pro Controller')]), 'switch')
        self.assertEqual(controls.resolve_icon_set('switch', []), 'switch')
        self.assertIn('switch', [value for value, _text in controls.ICON_CHOICES])


class PromptSetsTests(unittest.TestCase):
    NAME = 'MENU_Common_00091'

    @classmethod
    def setUpClass(cls):
        cls.atlas = icons.encode_image(atlas_image(icons.ATLAS_ICONS[cls.NAME], (1024, 512)))
        cls.tpf = make_tpf(real_layout([(cls.NAME, 1024, 512, 98, cls.atlas)]))
        cls.data = icons.prompt_sets(cls.tpf, {})
        cls.sets, cls.entries = read_prompt_sets(cls.data)

    def test_the_sets_are_the_games_and_the_three_drawn_ones(self):
        self.assertEqual(self.sets, ['playstation', 'xbox', 'keyboard', 'switch'])

    def test_every_glyph_is_an_entry_whose_playstation_bytes_are_the_games(self):
        by_name = {t.name: t for t in icons.parse_tpf(self.tpf)}
        glyphs = {e[0]: e for e in self.entries if e[0].startswith('KG_')}
        self.assertEqual(set(glyphs), set(icons.GLYPH_NAMES))
        for name, width, height, offsets, payloads in glyphs.values():
            self.assertEqual((width, height), (32, 32))
            self.assertEqual(offsets, list(range(0, 1024, 16)))
            texture = by_name[name]
            self.assertEqual(payloads['playstation'], self.tpf[texture.offset:texture.offset + 1024])

    def test_a_set_holds_the_bytes_the_layer_would_write(self):
        patched = icons.patch_glyphs(self.tpf, icons.build_glyphs('xbox', {}))
        by_name = {t.name: t for t in icons.parse_tpf(patched)}
        for name, _w, _h, _offsets, payloads in self.entries:
            if name.startswith('KG_'):
                texture = by_name[name]
                self.assertEqual(payloads['xbox'], patched[texture.offset:texture.offset + 1024], name)

    def test_the_atlas_buttons_are_entries_of_their_blocks_in_the_tiled_layout(self):
        atlas = [e for e in self.entries if e[0].startswith(self.NAME)]
        self.assertEqual(len(atlas), len(icons.ATLAS_ICONS[self.NAME]))
        glyphs = icons.build_glyphs('keyboard', {})
        patched = bytearray(self.tpf)
        icons.patch_atlases(patched, glyphs)
        texture = next(t for t in icons.parse_tpf(self.tpf) if t.name == self.NAME)
        for (name, width, height, offsets, payloads), (rect, _box, _glyph) in zip(atlas, icons.ATLAS_ICONS[self.NAME]):
            self.assertEqual((width, height), (1024, 512))
            self.assertEqual(len(offsets), (rect[2] - rect[0]) // 4 * ((rect[3] - rect[1]) // 4))
            for i, offset in enumerate(offsets):
                at = texture.offset + offset
                self.assertEqual(payloads['playstation'][i * 16:i * 16 + 16], self.tpf[at:at + 16])
                self.assertEqual(payloads['keyboard'][i * 16:i * 16 + 16], bytes(patched[at:at + 16]))

    def test_the_file_is_cached_by_source_and_bindings(self):
        with tempfile.TemporaryDirectory() as temp:
            game, out = Path(temp) / 'game', Path(temp) / 'out'
            (game / 'dvdroot_ps4/menu').mkdir(parents=True)
            (game / icons.COMMON_TPF).write_bytes(icons.dcx_pack(self.tpf))
            path, built = icons.ensure_prompt_sets(game, out, {})
            self.assertTrue(built)
            self.assertEqual(path.read_bytes(), self.data)
            again, built = icons.ensure_prompt_sets(game, out, {})
            self.assertEqual((again, built), (path, False))
            other, built = icons.ensure_prompt_sets(game, out, {'key.cross': 'F'})
            self.assertTrue(built)
            self.assertNotEqual(other, path)


if __name__ == '__main__':
    unittest.main()
