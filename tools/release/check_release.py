# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks a staged release folder before it is zipped (make-release.ps1 runs it).

A public release must hold everything a player needs besides their own copy of the game, and
nothing from the game itself or from a player's install: no game files (eboot.bin, param.sfo,
the .dcx archives, event files, system modules), no saves, logs, settings or server databases.
The co-op event files are made from each player's own game by the launcher (bbport_coop.py); the
release carries only their expected hashes (event-reference.json). It is installed by one setup
.exe and played from BLauncher.exe, with no bat files (Kaniel 2026-10-13).

Usage: check_release.py <release folder>      exit code 1 with the problems listed
"""
import json
import pathlib
import sys

REQUIRED = (
    "BLauncher.exe",
    "bin/bb-probe.exe",
    # DLSS: the bridge and NVIDIA's runtime next to bb-probe.exe. Without them the launcher's DLSS
    # option is disabled on every PC (the 0.2.x local releases).
    "bin/bbport_dlss.dll",
    "bin/nvngx_dlss.dll",
    "READ ME FIRST.txt",
    "VERSION.txt",
    "tools/party_boss_events.py",
    "server/shadnet.exe",
    "server/shadnet.cfg",
    "server/Qt6Core.dll",
    "server/clientsample/shadnet-sample.exe",
    "backend/bb_dummy_server.py",
    "backend/ss_info_template.xml",
    "licenses/GPL-2.0-or-later.txt",
    "CREDITS.txt",
)

# Game data, by file name or suffix (lower case).
GAME_NAMES = {"eboot.bin", "param.sfo", "icon0.png", "pic0.png", "pic1.png", "snd0.at9"}
GAME_SUFFIXES = (".dcx", ".bdt", ".bhd", ".bhd5", ".prx", ".sprx", ".emevd", ".pkg", ".sfo")
GAME_FOLDERS = {"dvdroot_ps4", "sce_sys", "sce_module", "uroot", "event-patch"}
# A player's or a test run's own files.
PLAYER_NAMES = {"settings.bat", "host_overrides.custom.json", "mods.json", "patches.json", "bbport.ini"}
PLAYER_SUFFIXES = (".db", ".log", ".pyc", ".bak", ".old", ".dmp")
PLAYER_FOLDERS = {"user", "savedata", "__pycache__", "db", "out", "game", "coop-game"}


def check(root: pathlib.Path) -> list[str]:
    """The problems in the release folder ROOT (empty when it may be published)."""
    root = pathlib.Path(root)
    problems = [f"missing {relative}" for relative in REQUIRED if not (root / relative).is_file()]
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        name = path.name.lower()
        folders = {part.lower() for part in path.relative_to(root).parts[:-1]}
        if name.endswith((".bat", ".cmd")):
            problems.append(f"a bat file in the release (the launcher does their work): {relative}")
        elif name in GAME_NAMES or name.endswith(GAME_SUFFIXES) or folders & GAME_FOLDERS:
            problems.append(f"game file in the release: {relative}")
        elif name in PLAYER_NAMES or name.endswith(PLAYER_SUFFIXES) or folders & PLAYER_FOLDERS:
            problems.append(f"player or test data in the release: {relative}")
    reference = root / "event-reference.json"
    if reference.is_file():
        try:
            files = json.loads(reference.read_text(encoding="utf-8"))["files"]
            for name, entry in files.items():
                if set(entry) - {"input_sha256", "output_sha256"}:
                    problems.append(f"event-reference.json: {name} holds more than its hashes")
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            problems.append(f"event-reference.json is not a list of hashes: {error}")
    return problems


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    problems = check(pathlib.Path(argv[0]))
    for problem in problems:
        print(problem)
    print("release check: " + ("ok" if not problems else f"{len(problems)} problem(s)"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
