# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for setup_style.py: Inno Setup's dark VCL style recolored to the launcher's palette.

Run: py -3.12 -m unittest discover -s tools\\release\\tests
"""
import colorsys
import pathlib
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import setup_style as tool  # noqa: E402

RELEASE = pathlib.Path(__file__).resolve().parents[1]


def hue(rgb):
    r, g, b = (c / 255 for c in rgb)
    h, s, _v = colorsys.rgb_to_hsv(r, g, b)
    return h * 360, s


def utf16(text):
    return struct.pack('<I', len(text)) + text.encode('utf-16-le')


def entry(name, value):
    return utf16(name) + struct.pack('<I', 1) + b':\x00' + utf16(value)


def small_style(pixels):
    """A style stream shaped like Inno's: name, the MasterStyle images, objects, the color table."""
    body = utf16('Windows11 Modern Dark') + b'\x00\x00MasterStyle\r\n' + struct.pack('<I', 1)
    body += utf16('style.png') + struct.pack('<II', len(pixels), 1)
    body += b''.join(bytes((b, g, r, 255)) for r, g, b in pixels) + b'\x00\x00'
    body += b'\x01\x00\x00\x00objects\x00\x00\x03'
    body += entry('Border', '$00434343') + entry('clHighlight', '$00D07703')
    body += entry('StaticTextNormal', 'Tahoma,9,1,255,255,255') + entry('clWindowText', 'clWhite')
    return b'VCL_STYLE 2.0' + zlib.compress(body)


def unpack(style):
    assert style.startswith(b'VCL_STYLE 2.0')
    return zlib.decompress(style[len(b'VCL_STYLE 2.0'):])


class ColorTest(unittest.TestCase):
    def test_windows_blue_turns_gold(self):
        for blue in ((0, 120, 215), (25, 49, 63), (76, 194, 255)):
            h, s = hue(tool.recolor(*blue))
            self.assertTrue(30 <= h <= 50 and s > 0.2, (blue, h, s))

    def test_the_progress_green_turns_gold(self):
        h, _s = hue(tool.recolor(108, 203, 95))
        self.assertTrue(30 <= h <= 50, h)

    def test_no_red_stays(self):
        for red in ((196, 43, 28), (232, 17, 35)):
            h, _s = hue(tool.recolor(*red))
            self.assertTrue(30 <= h <= 50, (red, h))

    def test_white_is_parchment_and_greys_are_slate(self):
        self.assertEqual(tool.recolor(255, 255, 255), (240, 226, 194))
        r, g, b = tool.recolor(43, 43, 43)
        self.assertTrue(b > r and abs(r - 43) < 8, (r, g, b))

    def test_hex_and_font_values(self):
        self.assertEqual(tool.restyle_value('Border', '$00434343'), '$0038302B')
        self.assertEqual(tool.restyle_value('clHighlight', '$00D07703'), '$00365A6E')
        self.assertEqual(tool.restyle_value('StaticTextNormal', 'Tahoma,9,1,255,255,255'), 'Tahoma,9,1,240,226,194')
        self.assertEqual(tool.restyle_value('clWindowText', 'clWhite'), '$00C2E2F0')


class StyleTest(unittest.TestCase):
    def test_small_style_round_trip(self):
        out = unpack(tool.restyle(small_style([(0, 120, 215), (255, 255, 255)])))
        self.assertIn('Bloodborne Co-op Dark'.encode('utf-16-le'), out)
        images = tool.read_images(out)
        self.assertEqual(len(images), 1)
        _name, w, h, start = images[0]
        px = out[start:start + w * h * 4]
        self.assertTrue(30 <= hue((px[2], px[1], px[0]))[0] <= 50)
        self.assertEqual((px[6], px[5], px[4]), (240, 226, 194))
        table = dict(tool.read_entries(out))
        self.assertEqual(table['Border'], '$0038302B')
        self.assertEqual(table['clWindowText'], '$00C2E2F0')
        self.assertEqual(table['StaticTextNormal'], 'Tahoma,9,1,240,226,194')

    @unittest.skipUnless(tool.INNO_STYLES.is_file(), 'Inno Setup 6.6+ not installed')
    def test_inno_dark_style_has_no_blue_or_red_left(self):
        out = unpack(tool.restyle(tool.inno_dark_style()))
        # the title bar's close button turns blood red under the pointer, as the launcher's does
        _frames, buttons = tool.caption_parts(out)
        close = {(image, x, y) for image, (x0, y0, x1, y1), _state, is_close in buttons if is_close
                 for x in range(x0, x1) for y in range(y0, y1)}
        for name, w, h, start in tool.read_images(out):
            px = out[start:start + w * h * 4]
            for i in range(0, len(px), 4 * 7):
                b, g, r, a = px[i:i + 4]
                x, y = (i // 4) % w, h - 1 - (i // 4) // w
                if a < 64 or max(r, g, b) < 40 or (name, x, y) in close:
                    continue
                h_, s = hue((r, g, b))
                self.assertFalse(s > 0.3 and (h_ < 25 or 70 < h_ < 260), (r, g, b, a))
        table = dict(tool.read_entries(out))
        self.assertEqual(table['clHighlight'], '$00365A6E')
        self.assertEqual(table['Window'], '$00130F0D')
        self.assertEqual(table['CaptionTextNormal'], 'Segoe UI,9,1,174,165,144')

    @unittest.skipUnless(tool.INNO_STYLES.is_file(), 'Inno Setup 6.6+ not installed')
    def test_inno_title_bar_is_the_launchers(self):
        out = unpack(tool.restyle(tool.inno_dark_style()))
        frames, buttons = tool.caption_parts(out)
        self.assertTrue(frames and len(buttons) >= 20, (len(frames), len(buttons)))
        images = {name: (w, h, start) for name, w, h, start in tool.read_images(out)}

        def colors(part):
            image, (x0, y0, x1, y1) = part[0], part[1]
            w, h, start = images[image]
            seen = {}
            for y in range(y0, y1):
                for x in range(x0, x1):
                    o = start + ((h - 1 - y) * w + x) * 4
                    b, g, r, a = out[o:o + 4]
                    if a == 255:
                        seen[(r, g, b)] = seen.get((r, g, b), 0) + 1
            return max(seen, key=seen.get) if seen else None

        # the bar is the launcher's ground; a button's hover its light wash, the close button's blood red
        self.assertEqual(colors(next(f for f in frames if f[2] == 'Title')), (13, 15, 19))
        hot = {(b[2], b[3]): colors(b) for b in buttons if b[0] == 'style.png'}
        self.assertEqual(hot[('BitmapHot', False)], (40, 40, 40))
        self.assertEqual(hot[('BitmapHot', True)], (138, 42, 38))


class SetupScriptTest(unittest.TestCase):
    def test_setup_uses_the_style_and_art(self):
        iss = (RELEASE / 'setup.iss').read_text(encoding='utf-8')
        self.assertIn('WizardStyleFile={#StyleFile}', iss)
        self.assertIn('includetitlebar', iss)
        for line in iss.splitlines():
            if line.startswith(('WizardImageFile=', 'WizardSmallImageFile=')):
                for name in line.split('=', 1)[1].split(','):
                    self.assertTrue((RELEASE / name).is_file(), name)
        release = (RELEASE.parent / 'make-release.ps1').read_text(encoding='utf-8')
        self.assertIn('setup_style.py', release)
        self.assertIn('/DStyleFile=', release)

    def test_setup_draws_its_own_title_bar(self):
        iss = (RELEASE / 'setup.iss').read_text(encoding='utf-8')
        self.assertIn('Form.BorderStyle := bsNone;', iss)
        self.assertIn('LauncherBar(WizardForm);', iss)
        self.assertIn('LauncherBar(UninstallProgressForm);', iss)
        for event in ('DeinitializeSetup', 'DeinitializeUninstall'):
            body = iss.split(f'procedure {event};', 1)[1].split('end;', 1)[0]
            self.assertIn('StopBar', body)

    def test_setup_speaks_every_language_inno_has(self):
        iss = (RELEASE / 'setup.iss').read_text(encoding='utf-8')
        files = [line.split('MessagesFile: "', 1)[1].rstrip('"') for line in iss.splitlines()
                 if line.startswith('Name: ') and 'MessagesFile:' in line]
        self.assertEqual(files[0], 'compiler:Default.isl')
        self.assertGreater(len(files), 70)
        for name in files:
            if not name.startswith('compiler:'):
                self.assertTrue((RELEASE / name).is_file(), name)
        ours = {'languages\\' + f.name for f in (RELEASE / 'languages').iterdir()}
        self.assertLessEqual(ours, set(files))
        self.assertIn('ShowLanguageDialog=auto', iss)


if __name__ == '__main__':
    unittest.main()
