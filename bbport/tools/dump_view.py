#!/usr/bin/env python3
"""Converts the upscaler dumps (BB_DUMP_TRIGGER, BB_PRESENT_DUMP_TRIGGER) into PNGs.

    nix-shell -p "python3.withPackages (p: [p.numpy p.pillow])" --run "python3 tools/dump_view.py out/dump"

Files are <dir>/fNNN_<name>_<w>x<h>_<format>.raw (vk_temporal_upscaler.cpp DumpImages):
- rgba / bgra: 8-bit color as is;
- rgba16f / rgba32f / r11g11b10f: HDR color, Reinhard-mapped and sRGB-encoded (alpha dropped);
- rg16f (motion vectors, in pixels of the render size: TAA adds them to the pixel position,
  FSR gets motionVectorScale 1): hue = direction, brightness =
  length (MOTION_SCALE pixels and more = full), plus a "_mag" image of the length alone;
- f32 (depth): normalized over its own range, near = bright.
Also the presenter's BB_FRAME_DUMP_TRIGGER gain maps of HDR scene highlights,
hdr_gain_NNN_<w>x<h>.raw (vk_scene_hdr.h, RG16F: gain, UI coverage): gain 1 black, up to
GAIN_SCALE white on a log scale; the game's UI in blue.
Writes <same name>.png next to each file.
"""
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

MOTION_SCALE = 16.0  # pixels for full brightness
NAME = re.compile(r'f(\d+)_(\w+?)_(\d+)x(\d+)_(\w+)\.raw$')
GAIN_NAME = re.compile(r'^hdr_gain_(\d+)_(\d+)x(\d+)\.raw$')
GAIN_SCALE = 10.0  # gain for full white


def srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1 / 2.4) - 0.055)


def small_float(bits, mantissa_bits):
    """Unsigned 10/11-bit floats (5-bit exponent, no sign) of B10G11R11_UFLOAT."""
    exponent = (bits >> mantissa_bits) & 31
    mantissa = (bits & ((1 << mantissa_bits) - 1)).astype(np.float32)
    scale = float(1 << mantissa_bits)
    normal = np.ldexp(1.0 + mantissa / scale, exponent.astype(np.int32) - 15)
    denormal = np.ldexp(mantissa / scale, -14)
    return np.where(exponent == 0, denormal, np.where(exponent == 31, np.inf, normal)).astype(np.float32)


def hdr_to_png(rgb):
    mapped = rgb / (1.0 + rgb)
    return (srgb(np.maximum(mapped, 0.0)) * 255.0 + 0.5).astype(np.uint8)


def motion_to_png(uv, width, height):
    px = uv[..., 0]
    py = uv[..., 1]
    length = np.sqrt(px * px + py * py)
    valid = np.isfinite(length)
    length = np.where(valid, length, 0.0)
    angle = (np.arctan2(py, px) + np.pi) / (2 * np.pi)  # 0..1
    value = np.clip(length / MOTION_SCALE, 0.0, 1.0)
    h6 = angle * 6.0
    c = value
    x = c * (1 - np.abs(np.mod(h6, 2) - 1))
    zeros = np.zeros_like(c)
    sector = np.floor(h6).astype(int) % 6
    r = np.choose(sector, [c, x, zeros, zeros, x, c])
    g = np.choose(sector, [x, c, c, x, zeros, zeros])
    b = np.choose(sector, [zeros, zeros, x, c, c, x])
    rgb = np.stack([r, g, b], axis=-1)
    rgb[~valid] = (1.0, 0.0, 1.0)  # NaN or infinite: magenta
    mag = (np.clip(length / MOTION_SCALE, 0, 1) * 255).astype(np.uint8)
    return (rgb * 255).astype(np.uint8), mag, length


def gain_to_png(gain, ui):
    """HDR scene highlights: log-scaled gain (1 black, GAIN_SCALE white), UI coverage in blue."""
    gain = np.nan_to_num(gain, nan=1.0, posinf=GAIN_SCALE, neginf=1.0)
    level = np.clip(np.log(np.maximum(gain, 1.0)) / np.log(GAIN_SCALE), 0.0, 1.0)
    rgb = np.stack([level, level, np.maximum(level, np.clip(ui, 0.0, 1.0) * 0.6)], axis=-1)
    return (rgb * 255.0 + 0.5).astype(np.uint8)


def convert_gain(path, w, h):
    data = np.frombuffer(path.read_bytes(), np.float16)[: w * h * 2].reshape(h, w, 2)
    gain, ui = data[..., 0].astype(np.float32), data[..., 1].astype(np.float32)
    out = path.with_suffix('.png')
    Image.fromarray(gain_to_png(gain, ui)).save(out)
    return (f'{out.name} gain: max {np.nanmax(gain):.2f}, above 1.05 {np.mean(gain > 1.05) * 100:.1f}%, '
            f'UI {np.mean(ui > 0.5) * 100:.1f}%')


def convert(path):
    if g := GAIN_NAME.match(path.name):
        return convert_gain(path, int(g.group(2)), int(g.group(3)))
    m = NAME.search(path.name)
    if not m:
        return None
    _frame, name, w, h, fmt = m.groups()
    w, h = int(w), int(h)
    data = path.read_bytes()
    out = path.with_suffix('.png')
    note = ''
    if fmt in ('rgba', 'bgra'):
        img = np.frombuffer(data, np.uint8)[: w * h * 4].reshape(h, w, 4)[..., :3]
        if fmt == 'bgra':
            img = img[..., ::-1]
        Image.fromarray(np.ascontiguousarray(img)).save(out)
    elif fmt in ('rgba16f', 'rgba32f'):
        dtype = np.float16 if fmt == 'rgba16f' else np.float32
        img = np.frombuffer(data, dtype)[: w * h * 4].reshape(h, w, 4)[..., :3].astype(np.float32)
        img = np.nan_to_num(img, nan=0.0, posinf=0.0, neginf=0.0)
        Image.fromarray(hdr_to_png(img)).save(out)
    elif fmt == 'r11g11b10f':
        packed = np.frombuffer(data, np.uint32)[: w * h].reshape(h, w)
        img = np.stack([small_float(packed & 0x7FF, 6), small_float((packed >> 11) & 0x7FF, 6),
                        small_float(packed >> 22, 5)], axis=-1)
        img = np.nan_to_num(img, nan=0.0, posinf=0.0, neginf=0.0)
        Image.fromarray(hdr_to_png(img)).save(out)
    elif fmt == 'rg16f':
        uv = np.frombuffer(data, np.float16)[: w * h * 2].reshape(h, w, 2).astype(np.float32)
        rgb, mag, length = motion_to_png(uv, w, h)
        Image.fromarray(rgb).save(out)
        Image.fromarray(mag).save(path.with_name(path.stem + '_mag.png'))
        finite = length[np.isfinite(length)]
        note = (f' motion px: median {np.median(finite):.2f}, p99 {np.percentile(finite, 99):.2f}, '
                f'max {finite.max():.1f}, zero {np.mean(finite < 0.01) * 100:.0f}%')
    elif fmt == 'f32':
        depth = np.frombuffer(data, np.float32)[: w * h].reshape(h, w)
        finite = depth[np.isfinite(depth)]
        lo, hi = (finite.min(), finite.max()) if finite.size else (0.0, 1.0)
        norm = (depth - lo) / max(hi - lo, 1e-9)
        Image.fromarray((np.clip(norm, 0, 1) * 255).astype(np.uint8)).save(out)
        note = f' depth {lo:.5f}..{hi:.5f}'
    else:
        return None
    return f'{out.name}{note}'


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else 'out/dump')
    for path in sorted(root.glob('*.raw')):
        result = convert(path)
        if result:
            print(result)


if __name__ == '__main__':
    main()
