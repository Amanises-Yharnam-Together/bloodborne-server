"""Turns the rig's PNG captures into the guide's WebP screenshots in public/docs.

Usage: python3 scripts/shots/make_shots.py <folder with the PNGs> [more folders...]
(the first folder that has a file wins, so list the newest captures first)

Each entry: published name, source PNG, crop box (left, top, right, bottom) or None, and the
published width (the height follows). Headless captures draw only the overlays on black, so
the menus are cropped to their panel.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

OUT = Path(__file__).resolve().parents[2] / "public" / "docs"

GAME = (0, 0, 1920, 1080)
LAUNCHER = (0, 0, 1800, 1170)
COOP_CARD = (270, 222, 1530, 950)
INSERT = (360, 130, 1560, 950)
COMPASS = (680, 28, 1290, 90)
TOOLTIP = (420, 230, 1260, 445)
# The FPS counter at the top right of visible rig games (1920x1080), painted over.
FPS_BOX = (1570, 10, 1910, 48)

SHOTS = [
    # setup wizard
    ("setup-language", "setup-01-language.png", None, 618),
    ("setup-welcome", "setup-02-welcome.png", (0, 19, 989, 757), 989),
    ("setup-folder", "setup-03-folder.png", (0, 19, 989, 757), 989),
    ("setup-ready", "setup-05-ready.png", (0, 19, 989, 757), 989),
    ("setup-installing", "setup-06-installing.png", (0, 19, 989, 757), 989),
    ("setup-finished", "setup-07-finished.png", (0, 19, 989, 757), 989),
    ("setup-uninstall", "setup-08-uninstall-ask.png", None, 660),
    # launcher
    ("launcher-home", "launcher-home.png", LAUNCHER, 1200),
    ("launcher-first", "launcher-first-home.png", LAUNCHER, 1200),
    ("launcher-coop-empty", "launcher-first-coop-empty.png", COOP_CARD, 1200),
    ("launcher-coop-found", "launcher-coop-find-result.png", COOP_CARD, 1200),
    ("launcher-coop", "launcher-coop-filled.png", COOP_CARD, 1200),
    ("launcher-pkg", "launcher-pkg-dialog.png", None, 853),
    ("launcher-bar-first", "launcher-first-footer.png", None, 1200),
    ("launcher-bar", "launcher-footer-idle.png", None, 1200),
    ("launcher-server-running", "launcher-footer-server-running.png", None, 1200),
    ("launcher-tip-game", "launcher-tooltip-game.png", TOOLTIP, 840),
    ("launcher-tip-saves", "launcher-tooltip-saves.png", TOOLTIP, 840),
    ("launcher-tip-fsr4", "launcher-tooltip-fsr4.png", TOOLTIP, 840),
    *[
        (f"launcher-settings-{t}", f"launcher-settings-{t}.png", LAUNCHER, 1200)
        for t in ("general", "graphics", "display", "controls", "game", "cheats", "mods", "advanced", "log")
    ],
    # the team screen, loading and joining a running hunt
    ("team-none", "game-team-1-no-team.png", GAME, 1440),
    ("team-created", "game-team-2-created.png", GAME, 1440),
    ("team-invite-sent", "game-team-3-invite-sent.png", GAME, 1440),
    ("team-invite", "game-team-4-invite-arrived.png", GAME, 1440),
    ("team-joined", "game-team-5-joined.png", GAME, 1440),
    ("team-ready", "game-team-6-ready.png", GAME, 1440),
    ("team-begin", "game-team-7-begin-the-hunt.png", GAME, 1440),
    ("loading-2", "game-loading-2-b.png", GAME, 1440),
    ("loading-3", "game-loading-3-b.png", GAME, 1440),
    ("loading-waiting", "game-loading-3-waiting.png", GAME, 1440),
    ("hotjoin-page", "game-hot-join-1-insert-team-page.png", GAME, 1440),
    ("hotjoin-invite", "game-hot-join-2-invite-arrived.png", GAME, 1440),
    ("hotjoin-loading", "game-hot-join-3-loading.png", GAME, 1440),
    ("hotjoin-joined", "game-hot-join-4-joined.png", GAME, 1440),
    ("hotjoin-leader", "game-hot-join-5-leader-view.png", GAME, 1440),
    # map and compass
    ("map-team", "game-map-team.png", GAME, 1440),
    ("map-hunters", "game-map-hunters-3.png", GAME, 1440),
    ("map-world", "game-map-world.png", GAME, 1440),
    ("compass-ahead", "game-compass-ahead.png", COMPASS, 1220),
    ("compass-behind", "game-compass-behind.png", COMPASS, 1220),
    ("compass-right", "game-compass-right.png", COMPASS, 1220),
    # the Insert menu's tabs
    # visible games: the HUD
    ("party-alive", "hud-party-alive.png", GAME, 1440),
    ("party-downed", "hud-party-downed.png", GAME, 1440),
    ("you-are-down", "hud-you-are-down.png", GAME, 1440),
    ("you-are-down-revive", "hud-you-are-down-reviving.png", GAME, 1440),
    ("give-inventory", "hud-give-1-inventory.png", GAME, 1440),
    ("give-menu", "hud-give-2-menu.png", GAME, 1440),
    ("give-quantity", "hud-give-3-quantity.png", GAME, 1440),
    ("give-offer", "hud-give-4-offer-received.png", GAME, 1440),
    ("give-received", "hud-give-5-received.png", GAME, 1440),
    ("wipe-lamp", "hud-wipe-lamp.png", GAME, 1440),
    ("wipe-bloodstain", "hud-wipe-lamp-bloodstain.png", GAME, 1440),
    *[(f"insert-{t}", f"game-insert-{t}.png", INSERT, 1200) for t in ("display", "upscaler", "effects", "controls", "advanced")],
]


def has_fps_counter(im: Image.Image) -> bool:
    """The counter is light text in a box; without it the corner is dark."""
    a = np.asarray(im.crop(FPS_BOX).convert("L"))
    return int((a > 150).sum()) > 200


def hide_fps_counter(im: Image.Image) -> Image.Image:
    """Fills the counter's box from the rows just above and below it, softened."""
    a = np.asarray(im).astype(np.float32).copy()
    l, t, r, b = FPS_BOX
    top, bottom = a[t - 1, l:r].copy(), a[b, l:r].copy()
    for y in range(t, b):
        f = (y - t + 1) / (b - t + 1)
        a[y, l:r] = top * (1 - f) + bottom * f
    out = Image.fromarray(a.clip(0, 255).astype(np.uint8))
    patch = out.crop((l - 8, t - 4, r + 8, b + 4)).filter(ImageFilter.GaussianBlur(3))
    out.paste(patch.crop((8, 4, 8 + r - l, 4 + b - t)), (l, t))
    return out


def main(srcs: list[Path]) -> None:
    for name, file, box, width in SHOTS:
        src = next((d for d in srcs if (d / file).exists()), None)
        if src is None:
            print(f"skipped {name}: no {file}")
            continue
        im = Image.open(src / file).convert("RGB")
        if im.width == 3840:
            # 4K captures: the boxes above are in 1920x1080 pixels
            im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
        if file.startswith("hud-") and has_fps_counter(im):
            im = hide_fps_counter(im)
        if box:
            im = im.crop(box)
        if im.width != width:
            im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
        im.save(OUT / f"{name}.webp", "WEBP", quality=84, method=6)
        print(f"{name}.webp {im.width}x{im.height}")


if __name__ == "__main__":
    main([Path(a) for a in sys.argv[1:]])
