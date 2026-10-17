# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""The setup's window style: Inno Setup's own dark VCL style ("Windows11 Modern Dark", built into
SetupCustomStyle.e32 since 6.6) recolored to the launcher's palette, so the buttons, fields, focus
and title bar are slate, parchment and gold instead of Windows blue, and the close button's red
hover goes too (Kaniel 2026-10-17: the installer matches the launcher). make-release.ps1 writes it
beside the staged release and setup.iss loads it with WizardStyleFile; nothing of it is kept in the
repository, as the style is Inno Setup's.

  py -3.12 tools/release/setup_style.py <out.vsf> [<Inno Setup 6 folder>\\SetupCustomStyle.e32]

A style file is "VCL_STYLE 2.0" and one zlib stream: the style's name, its images (MasterStyle:
count, then per image a UTF-16 name, width, height, BGRA pixels and two pad bytes), its objects, and
a table of named colors and fonts (UTF-16 name, 1, ':', UTF-16 value such as $00bbggrr, clWhite or
Tahoma,9,1,r,g,b).
"""
import colorsys
import os
import pathlib
import re
import struct
import sys
import zlib

HEADER = b'VCL_STYLE 2.0'
INNO_STYLES = pathlib.Path(os.environ.get('LOCALAPPDATA', '')) / 'Programs' / 'Inno Setup 6' / 'SetupCustomStyle.e32'
SOURCE_NAME, NAME = 'Windows11 Modern Dark', 'Bloodborne Co-op Dark'

# bbport_launcher_win.py's palette (BG, FIELD, CARD, LINE, ACCENT, ACCENT_DEEP ...), as TColor text.
BG, PANEL, FIELD, CARD = '$00130F0D', '$001A1512', '$00110D0B', '$00221C19'
LINE, ACCENT, ACCENT_DEEP = '$0038302B', '$00365A6E', '$0020262A'
PALETTE = {
    'Window': BG, 'GenericBackground': BG, 'Panel': BG, 'PanelDisabled': BG, 'clBtnFace': BG, 'clMenu': BG,
    'clActiveCaption': BG, 'clInactiveCaption': BG, 'clScrollBar': BG, 'Splitter': BG, 'CategoryButtons': BG,
    'CategoryPanelGroup': BG, 'CategoryButtonsGradientBase': BG, 'CategoryButtonsGradientEnd': BG,
    'ToolBarGradientBase': BG, 'ToolBarGradientEnd': BG, 'GenericGradientBase': PANEL, 'GenericGradientEnd': BG,
    'Edit': FIELD, 'EditDisabled': FIELD, 'ComboBox': FIELD, 'ComboBoxDisabled': FIELD, 'ListBox': FIELD,
    'ListBoxDisabled': FIELD, 'ListView': FIELD, 'TreeView': FIELD, 'Grid': FIELD, 'clWindow': FIELD,
    'clInfoBk': FIELD, 'HintGradientBase': FIELD, 'HintGradientEnd': FIELD,
    'Border': LINE, 'clActiveBorder': LINE, 'clWindowFrame': LINE, 'clInactiveBorder': '$00282320',
    'ButtonNormal': CARD, 'ButtonHot': '$002E2723', 'ButtonPressed': PANEL, 'ButtonFocused': ACCENT_DEEP,
    'ButtonDisabled': PANEL, 'AlternatingRowBackground': '$001D1815',
    'FocusEffect': ACCENT, 'clHighlight': ACCENT,
}
IDENTS = {'clWhite': (255, 255, 255), 'clSilver': (192, 192, 192), 'clGray': (128, 128, 128)}
PARCHMENT = (240, 226, 194)
# The title bar as the launcher's own (CaptionButton): its ground, MUTED glyphs turning TEXT under the
# pointer over a light wash, the close button blood red; DIM when the window is not the active one.
BAR, MUTED, DIM, LINE_RGB = (13, 15, 19), (174, 165, 144), (110, 106, 96), (43, 48, 56)
HOT, PRESSED, CLOSE_HOT, CLOSE_PRESSED = (40, 40, 40), (55, 54, 51), (138, 42, 38), (110, 34, 30)
CAPTION_TEXT = {'CaptionTextNormal': MUTED, 'CaptionTextInactive': DIM}
GOLD_HUE = 39 / 360


def recolor(r, g, b):
    """One color in the launcher's look: Windows blue and the progress bar's green turn gold, the
    close button's red a dark gold, greys slate (dark) to parchment (light)."""
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    if s >= 0.12 and (70 / 360 <= h <= 260 / 360):
        out = colorsys.hsv_to_rgb(GOLD_HUE, min(0.48, max(0.3, s * 0.6)), v)
    elif s >= 0.2 and (h <= 20 / 360 or h >= 340 / 360):
        out = colorsys.hsv_to_rgb(GOLD_HUE, 0.45, v * 0.7)
    elif s < 0.12:
        t = min(1.0, max(0.0, (v - 0.3) / 0.45))
        slate = (0.93, 0.97, 1.07)
        factor = [a + (p / 255 - a) * t for a, p in zip(slate, PARCHMENT)]
        return tuple(min(255, round(c * f)) for c, f in zip((r, g, b), factor))
    else:
        return r, g, b  # gold already, and the rare purple
    return tuple(min(255, round(c * 255)) for c in out)


def tcolor(rgb):
    r, g, b = rgb
    return f'$00{b:02X}{g:02X}{r:02X}'


def restyle_value(name, value):
    if name in CAPTION_TEXT:
        font = value.split(',')
        font[3:6] = (str(c) for c in CAPTION_TEXT[name])
        return ','.join(font)
    if name in PALETTE:
        return PALETTE[name]
    if name == 'clWindowText' or value == 'clWhite':
        return tcolor(PARCHMENT)
    if value in IDENTS:
        return tcolor(recolor(*IDENTS[value]))
    if re.fullmatch(r'\$00[0-9A-Fa-f]{6}', value):
        n = int(value[3:], 16)
        return tcolor(recolor(n & 0xFF, (n >> 8) & 0xFF, n >> 16))
    font = value.split(',')
    if len(font) >= 6 and all(x.isdigit() for x in font[3:6]):
        font[3:6] = (str(c) for c in recolor(*(int(x) for x in font[3:6])))
        return ','.join(font)
    return value


def read_images(raw):
    """(name, width, height, offset of the BGRA pixels) for each MasterStyle image."""
    i = raw.index(b'MasterStyle\r\n') + len(b'MasterStyle\r\n')
    count, = struct.unpack_from('<I', raw, i)
    i += 4
    images = []
    for _k in range(count):
        n, = struct.unpack_from('<I', raw, i)
        name = raw[i + 4:i + 4 + 2 * n].decode('utf-16-le')
        i += 4 + 2 * n
        w, h = struct.unpack_from('<II', raw, i)
        i += 8
        images.append((name, w, h, i))
        i += w * h * 4 + 2
    return images


ENTRY = re.compile(rb'(.)\x00\x00\x00((?:[A-Za-z0-9]\x00){1,64})\x01\x00\x00\x00:\x00(.)\x00\x00\x00', re.S)


def _entries(raw, start):
    for m in ENTRY.finditer(raw, start):
        name = m.group(2).decode('utf-16-le')
        if m.group(1)[0] != len(name):
            continue
        end = m.end() + 2 * m.group(3)[0]
        yield m.start(), end, name, raw[m.end():end].decode('utf-16-le')


def _table_start(raw):
    _name, w, h, start = read_images(raw)[-1]
    return start + w * h * 4 + 2


def read_entries(raw):
    return [(name, value) for _s, _e, name, value in _entries(raw, _table_start(raw))]


def _recolor_pixels(px):
    out = bytearray(px)
    cache = {}
    for i in range(0, len(out), 4):
        key = bytes(out[i:i + 4])
        new = cache.get(key)
        if new is None:
            b, g, r, a = key
            if a == 0:
                new = key
            else:
                # premultiplied pixels: recolor the color itself, then multiply again
                k = 255 / a if max(r, g, b) <= a else 1.0
                rr, gg, bb = recolor(min(255, round(r * k)), min(255, round(g * k)), min(255, round(b * k)))
                new = bytes((min(255, round(bb / k)), min(255, round(gg / k)), min(255, round(rr / k)), a))
            cache[key] = new
        out[i:i + 4] = new
    return bytes(out)


def _string(b, i):
    return b[i + 1:i + 1 + b[i]].decode('latin1'), i + 1 + b[i]


def _value(b, i):
    """One property value of a Delphi binary form stream (TPF0), and where the next one starts."""
    kind, i = b[i], i + 1
    sizes = {0: 0, 2: 1, 3: 2, 4: 4, 5: 10, 8: 0, 9: 0}
    if kind in sizes:
        raw = b[i:i + sizes[kind]]
        return (int.from_bytes(raw, 'little', signed=True) if kind in (2, 3, 4) else None), i + sizes[kind]
    if kind in (6, 7):
        return _string(b, i)
    if kind in (0x0a, 0x0c, 0x14):
        n, = struct.unpack_from('<i', b, i)
        return None, i + 4 + n
    if kind == 0x12:
        n, = struct.unpack_from('<i', b, i)
        return None, i + 4 + 2 * n
    if kind == 0x0b:
        while b[i]:
            i = _string(b, i)[1]
        return None, i + 1
    if kind == 0x01:
        while b[i]:
            i = _value(b, i)[1]
        return None, i + 1
    if kind == 0x0e:
        while b[i]:
            if b[i] in (2, 3, 4):
                i = _value(b, i)[1]
            while b[i]:
                i = _value(b, _string(b, i)[1])[1]
            i += 1
        return None, i + 1
    raise ValueError(f'value kind {kind} at {i}')


def _object(b, i, out):
    if b[i] & 0xF0 == 0xF0:
        i += 1
    cls, i = _string(b, i)
    name, i = _string(b, i)
    props = {}
    while b[i]:
        key, i = _string(b, i)
        props[key], i = _value(b, i)
    out.append((cls, name, props))
    i += 1
    while b[i]:
        i = _object(b, i, out)
    return i + 1


def style_objects(raw):
    """(class, name, properties) of every object of the style, in order."""
    out = []
    for m in re.finditer(rb'TPF0', raw[_table_start_images(raw):]):
        _object(raw, _table_start_images(raw) + m.end(), out)
    return out


def _table_start_images(raw):
    _name, w, h, start = read_images(raw)[-1]
    return start + w * h * 4 + 2


FRAME_PARTS = ('Title', 'LeftBorder', 'RightBorder', 'BottomBorder', 'Client')
STATES = ('Bitmap', 'ActiveBitmap', 'BitmapHot', 'BitmapPressed')


def caption_parts(raw):
    """The window frames' pictures, (image, rect, part), and their title bar buttons' pictures,
    (image, rect, state, is close), from the style's form objects (Form and ToolWindow)."""
    frames, buttons, in_form = [], [], False
    for cls, name, props in style_objects(raw):
        if cls == 'TSeStyleObject' and props.get('Kind') == 'skForm':
            in_form = True
        elif cls == 'TSeStyleObject' and props.get('Align') == 'saNone':
            in_form = False
        if not in_form:
            continue
        for state in STATES:
            rect = tuple(props.get(f'{state}.{side}', 0) for side in ('Left', 'Top', 'Right', 'Bottom'))
            if rect[2] <= rect[0] or rect[3] <= rect[1]:
                continue
            image = props.get(f'{state}.Name') or 'style.png'
            if cls == 'TSeSystemButton':
                buttons.append((image, rect, state, name.startswith('btnClose')))
            elif name in FRAME_PARTS and state in ('Bitmap', 'ActiveBitmap'):
                frames.append((image, rect, name))
    return frames, buttons


def _lum(px):
    return 0.299 * px[2] + 0.587 * px[1] + 0.114 * px[0]


def _repaint(raw, image, rect, paint):
    """paint(pixels of the rect) -> new pixels; pixels are BGRA, premultiplied, rows stored bottom up."""
    _n, w, h, start = image
    x0, y0, x1, y1 = rect
    offsets = [start + ((h - 1 - y) * w + x) * 4 for y in range(y0, y1) for x in range(x0, x1)]
    new = paint([bytes(raw[o:o + 4]) for o in offsets])
    for o, px in zip(offsets, new):
        raw[o:o + 4] = px


def _pixel(rgb, alpha=255):
    r, g, b = rgb
    return bytes((round(b * alpha / 255), round(g * alpha / 255), round(r * alpha / 255), alpha))


def _frame_paint(pixels):
    opaque = [p for p in pixels if p[3] == 255]
    fill = max(set(opaque), key=opaque.count) if opaque else None
    return [p if p[3] == 0 else _pixel(BAR if p == fill else LINE_RGB, p[3]) for p in pixels]


def _button_paint(state, close):
    if state == 'Bitmap':
        ground, ink = BAR, DIM
    elif state == 'ActiveBitmap':
        ground, ink = BAR, MUTED
    elif state == 'BitmapHot':
        ground, ink = CLOSE_HOT if close else HOT, PARCHMENT
    else:
        ground, ink = CLOSE_PRESSED if close else PRESSED, PARCHMENT

    def paint(pixels):
        opaque = [p for p in pixels if p[3] == 255]
        clear = len(opaque) * 2 < len(pixels)
        base = None if clear else max(set(opaque), key=opaque.count)
        far = max((abs(_lum(p) - (_lum(base) if base else 0)) for p in pixels if p[3]), default=1) or 1
        out = []
        for p in pixels:
            if clear:
                out.append(p if p[3] == 0 else _pixel(ink, p[3]))
                continue
            t = min(1.0, abs(_lum(p) - _lum(base)) / far)
            out.append(_pixel(tuple(round(a + (b - a) * t) for a, b in zip(ground, ink)), 255 if p[3] else 0))
        return out
    return paint


def repaint_caption(raw):
    images = {image[0]: image for image in read_images(raw)}
    frames, buttons = caption_parts(raw)
    for image, rect, _part in frames:
        _repaint(raw, images[image], rect, _frame_paint)
    for image, rect, state, close in buttons:
        _repaint(raw, images[image], rect, _button_paint(state, close))


def restyle(style):
    if not style.startswith(HEADER):
        raise ValueError('not a VCL style file')
    raw = bytearray(zlib.decompress(style[len(HEADER):]))
    raw = raw.replace(SOURCE_NAME.encode('utf-16-le'), NAME.encode('utf-16-le'), 1)
    for _name, w, h, start in read_images(raw):
        raw[start:start + w * h * 4] = _recolor_pixels(raw[start:start + w * h * 4])
    repaint_caption(raw)
    table = _table_start(raw)
    parts, last = [bytes(raw[:table])], table
    for s, e, name, value in _entries(raw, table):
        new = restyle_value(name, value)
        parts.append(bytes(raw[last:s]))
        parts.append(struct.pack('<I', len(name)) + name.encode('utf-16-le') + b'\x01\x00\x00\x00:\x00'
                     + struct.pack('<I', len(new)) + new.encode('utf-16-le'))
        last = e
    parts.append(bytes(raw[last:]))
    return HEADER + zlib.compress(b''.join(parts), 9)


def inno_dark_style(path=INNO_STYLES):
    """The "Windows11 Modern Dark" style file as Inno Setup carries it (a resource of the .e32)."""
    data = pathlib.Path(path).read_bytes()
    for m in re.finditer(re.escape(HEADER), data):
        stream = zlib.decompressobj()
        try:
            head = stream.decompress(data[m.end():m.end() + 4096], 256)
        except zlib.error:
            continue
        if SOURCE_NAME.encode('utf-16-le') not in head[:64]:
            continue
        stream = zlib.decompressobj()
        stream.decompress(data[m.end():])
        return data[m.start():len(data) - len(stream.unused_data)]
    raise SystemExit(f'No "{SOURCE_NAME}" style in {path} (Inno Setup 6.6 or newer is needed)')


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    source = sys.argv[2] if len(sys.argv) == 3 else INNO_STYLES
    pathlib.Path(sys.argv[1]).write_bytes(restyle(inno_dark_style(source)))


if __name__ == '__main__':
    main()
