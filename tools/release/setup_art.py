# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the setup's wizard images (tools/release/art) in the launcher's look: the hunter logo over
the night street on slate, no box-and-disc icons (Kaniel 2026-10-17: the installer matches the
launcher). Rerun when the logo or the launcher art changes:

  py -3.12 tools/release/setup_art.py
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
ART = Path(__file__).resolve().parent / 'art'
LOGO = ROOT / 'website' / 'public' / 'icon-512.png'
SKYLINE = ROOT / 'bbport' / 'launcher' / 'art' / 'side-skyline.png'

# bbport_launcher_win.py: BG, PANEL, the sky over the moon and the gold rule
BG, PANEL, SKY, GOLD_DIM = (13, 15, 19), (18, 21, 26), (24, 32, 44), (125, 107, 72)

# Inno Setup 6.6's image areas at 100% .. 250% DPI (WizardImageFile 164:314, WizardSmallImageFile square)
WIZARD_SIZES = [(202, 386), (269, 515), (336, 643), (430, 824), (534, 1022)]
SMALL_SIZES = [58, 77, 97, 124, 159]


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def cut_logo(side):
    """The logo without its square of ground: everything joined to a corner and close to BG goes."""
    logo = Image.open(LOGO).convert('RGBA')
    near = Image.eval(ImageChops.difference(logo.convert('RGB'), Image.new('RGB', logo.size, BG)).convert('L'),
                      lambda v: 255 if v < 14 else 0)
    for corner in ((0, 0), (logo.width - 1, 0), (0, logo.height - 1), (logo.width - 1, logo.height - 1)):
        ImageDraw.floodfill(near, corner, 128)
    ground = near.point(lambda v: 255 if v == 128 else 0).filter(ImageFilter.GaussianBlur(1.5))
    logo.putalpha(ImageChops.invert(ground))
    return logo.resize((side, side), Image.LANCZOS)


def wizard_image(w, h):
    """The side column of the Welcome and Finished pages: logo high, the street at the foot."""
    image = Image.new('RGB', (w, h), PANEL)
    draw = ImageDraw.Draw(image)
    for y in range(h):
        t = max(0.0, (y / h - 0.35) / 0.65)
        draw.line([(0, y), (w, y)], fill=mix(PANEL, SKY, t ** 1.6))
    sky = Image.open(SKYLINE).convert('RGBA')
    sky = sky.resize((w, round(sky.height * w / sky.width)), Image.LANCZOS)
    sky = sky.crop((0, sky.height - round(h * 0.6), w, sky.height))
    fade = Image.linear_gradient('L').resize(sky.size).point(lambda v: round(255 * min(1.0, v / 150) ** 2))
    sky.putalpha(ImageChops.multiply(sky.getchannel('A'), fade.point(lambda v: round(v * 0.8))))
    image.paste(sky, (0, h - sky.height), sky)
    side = round(w * 0.8)
    logo = cut_logo(side)
    image.paste(logo, ((w - side) // 2, round(h * 0.06)), logo)
    # one thin gold rule down the inner edge, as between the launcher's column and its pages
    ImageDraw.Draw(image).line([(w - 1, 0), (w - 1, h)], fill=GOLD_DIM)
    return image


def small_image(s):
    logo = cut_logo(s)
    image = Image.new('RGB', (s, s), BG)
    image.paste(logo, (0, 0), logo)
    return image


def main():
    ART.mkdir(exist_ok=True)
    for w, h in WIZARD_SIZES:
        wizard_image(w, h).save(ART / f'wizard-{w}.png', optimize=True)
    for s in SMALL_SIZES:
        small_image(s).save(ART / f'small-{s}.png', optimize=True)


if __name__ == '__main__':
    main()
