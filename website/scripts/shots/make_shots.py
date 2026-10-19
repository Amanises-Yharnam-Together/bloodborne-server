"""Turns the rig's PNG captures into the guide's WebP screenshots in public/docs.

Usage: python3 scripts/shots/make_shots.py <folder with the PNGs>

Each entry: published name, source PNG, crop box (left, top, right, bottom) or None, and the
published width (the height follows). Headless captures draw only the overlays on black, so
the menus are cropped to their panel.
"""

import sys
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parents[2] / "public" / "docs"

GAME = (0, 0, 1920, 1080)
LAUNCHER = (0, 0, 1800, 1170)
COOP_CARD = (270, 222, 1530, 950)
INSERT = (360, 130, 1560, 950)
COMPASS = (680, 28, 1290, 90)
TOOLTIP = (420, 230, 1260, 445)

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
    *[(f"insert-{t}", f"game-insert-{t}.png", INSERT, 1200) for t in ("display", "upscaler", "effects", "controls", "advanced")],
]


def main(src: Path) -> None:
    for name, file, box, width in SHOTS:
        im = Image.open(src / file).convert("RGB")
        if box:
            im = im.crop(box)
        if im.width != width:
            im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
        im.save(OUT / f"{name}.webp", "WEBP", quality=84, method=6)
        print(f"{name}.webp {im.width}x{im.height}")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
