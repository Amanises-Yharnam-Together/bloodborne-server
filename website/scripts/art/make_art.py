"""Turn the generated art in assets-src/art (JPEGs painted on a fake checkered background) into
transparent layers for the site: public/art/*.webp. Run with `python3 scripts/art/make_art.py`
from website/ (needs numpy and Pillow). Pass an output folder to also write full-size PNGs.

The checker squares are light grey and the art is darker or coloured, so each layer is keyed on
how far a pixel is below the checker's darker grey; soft layers (fog) are keyed on a min-filtered
copy, which hides the squares behind the mist."""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from key import key, ss  # noqa: E402

SRC = Path("assets-src/art")
OUT = Path("public/art")
OUT.mkdir(parents=True, exist_ok=True)
PNG = Path(sys.argv[1]) if len(sys.argv) > 1 else None


def load(name):
    return np.asarray(Image.open(SRC / f"{name}.jpg").convert("RGB")).astype(float)


def rgba(C, a):
    return Image.fromarray(
        np.dstack([np.clip(C, 0, 255).astype(np.uint8), np.clip(a * 255, 0, 255).astype(np.uint8)]),
        "RGBA",
    )


def write(im, name, width=None, quality=85):
    if PNG:
        PNG.mkdir(parents=True, exist_ok=True)
        im.save(PNG / f"{name}.png", optimize=True)
    if width and im.width != width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    im.save(OUT / f"{name}.webp", quality=quality, method=6, alpha_quality=90)


# Skylines: solid silhouettes; the far one keeps its painted fog as part of the shapes.
write(rgba(*key(load("near"), 192, 45, 11, 0.0, 45)), "skyline-near", quality=82)
write(rgba(*key(load("far"), 203, 45, 21, 0.0, 999)), "skyline-far", quality=82)

# Fog: soft only, never darker than the mist's own blue-grey; the left half and its mirror make
# a strip that tiles without a seam.
C, a = key(load("fog"), 192, 90, 13, 1.0, 999, hard=False)
fog = rgba(np.maximum(C, [100, 114, 140]), a)
half = fog.crop((0, 0, fog.width // 2, fog.height))
strip = Image.new("RGBA", (half.width * 2, half.height))
strip.paste(half, (0, 0))
strip.paste(half.transpose(Image.FLIP_LEFT_RIGHT), (half.width, 0))
write(strip, "fog", quality=78)

# Moon: the disc is as light as the checker, so it is cut as a circle; outside it only the
# bluish clouds stay, with a soft warm glow added around the rim.
O = load("moon")
H, W = O.shape[:2]
cx, cy, R = 527.5, 505.0, 348.0
Y, X = np.mgrid[0:H, 0:W]
d = np.hypot(X - cx, Y - cy)
disc = np.clip(R - d + 0.5, 0, 1)
C2, a2 = key(O, 199, 120, 27, 0.9, 999, hard=False, tint=(150, 160, 180))
C2 = np.maximum(C2, [140, 150, 172])
a2 = a2 * ss(O[..., 2] - O[..., 0], 2, 10) * ss(d, R + 4, R + 14)
glow = 0.22 * np.exp(-((np.maximum(d - R, 0)) / 70.0) ** 2) * (d > R - 1)
a = disc + (1 - disc) * np.maximum(a2, glow)
wd = disc / np.maximum(a, 1e-3)
wc = (1 - disc) * a2 / np.maximum(a, 1e-3)
wg = np.clip(1 - wd - wc, 0, 1)
C = wd[..., None] * O + wc[..., None] * C2 + wg[..., None] * np.array([240, 226, 194])
s = int(R * 1.6)
moon = rgba(C, a).crop((int(cx - s), int(cy - s), int(cx + s), int(cy + s)))
write(moon, "moon", width=768, quality=85)

# Divider: gold fill and a dark outline on the checker.
div = rgba(*key(load("divider"), 170, 70, 17, 0.0, 45))
write(div.crop(div.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox()), "divider", width=1200, quality=90)

# Icons: seven tiles of checker on a slate sheet, each cut, trimmed and centred in a square.
O = load("icons")
tiles = [(29, 290), (319, 571), (600, 841), (870, 1123), (1151, 1388), (1417, 1666), (1695, 1923)]
names = ["lantern", "blades", "bell", "tricorn", "vial", "skull", "hand"]
for (x0, x1), name in zip(tiles, names):
    im = rgba(*key(O[111:363, x0 + 3 : x1 - 3], 201, 60, 11, 0.0, 32))
    im = im.crop(im.getchannel("A").point(lambda v: 255 if v > 20 else 0).getbbox())
    side = max(im.size)
    pad = int(side * 0.06)
    sq = Image.new("RGBA", (side + 2 * pad,) * 2, (0, 0, 0, 0))
    sq.paste(im, ((sq.width - im.width) // 2, (sq.height - im.height) // 2))
    write(sq, f"icon-{name}", width=192, quality=90)
