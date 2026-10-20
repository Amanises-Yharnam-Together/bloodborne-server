# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""One build at a time on the PC, and never beside the rig's games (Kaniel 2026-10-20: his PC
froze with builds running beside rig games, worst of all several builds at once; rig runs side by
side are fine).

A build holds C:\\bb\\build.lock ("<owner>\\n<pid>\\n<when>") while it runs: it waits for the build
before it and for every game (engines.GAME_PROCESSES) to end first. A rig game waits for the
build (run_scenario.py's launch). A lock whose process is gone (a build killed outright) is stale
and taken over. tools\\build-*.ps1 and make-release.ps1 build under it; any other build too:

  py -3.12 tools\\rig\\build_lock.py run --owner <who> -- <command ...>
  py -3.12 tools\\rig\\build_lock.py status

BB_BUILD_BESIDE_GAMES=1: a build may run beside games (someone playing said so); it still waits
for the build before it.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import engines  # noqa: E402
import lanes  # noqa: E402

LOCK = engines.BB_ROOT / "build.lock"
# Set for a build's own process tree: a build script a build runs (make-release.ps1 runs
# build-server.ps1) is inside its lock already.
HELD = "BB_BUILD_LOCK_HELD"
POLL_SECONDS = 10


def holder(lock: pathlib.Path = LOCK) -> tuple[str, int] | None:
    """Who holds the lock (owner, pid), or None: free, or held by a process that is gone."""
    try:
        lines = lock.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    pid = int(lines[1]) if len(lines) > 1 and lines[1].strip().isdigit() else 0
    if not lanes.pid_alive(pid):
        return None
    return lines[0] if lines else "?", pid


def games_running() -> list[int]:
    """Every game on the PC, the rig's in any lane and anyone's own."""
    if os.name != "nt":
        return []
    names = " OR ".join(f"Name='{name}'" for name in engines.GAME_PROCESSES)
    listing = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         f"(Get-CimInstance Win32_Process -Filter \"{names}\").ProcessId"],
        capture_output=True, text=True).stdout
    return [int(pid) for pid in listing.split() if pid.isdigit()]


def try_take(owner: str, pid: int, lock: pathlib.Path = LOCK) -> bool:
    """Takes the lock for `pid` if it is free or stale; True when `pid` holds it."""
    held = holder(lock)
    if held:
        return held[1] == pid
    lock.parent.mkdir(parents=True, exist_ok=True)
    lock.unlink(missing_ok=True)  # stale: its process is gone
    try:
        handle = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return False  # another build took it just now
    with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as file:
        file.write(f"{owner}\n{pid}\n{time.strftime('%Y-%m-%d %H:%M:%S')}\n")
    return True


def take(owner: str, pid: int, lock: pathlib.Path = LOCK, games=games_running,
         sleep=time.sleep) -> None:
    """Waits for the build before and every game, then holds the lock for `pid`."""
    beside_games = os.environ.get("BB_BUILD_BESIDE_GAMES") == "1"
    said = None
    while True:
        held = holder(lock)
        running = [] if beside_games else games()
        if held and held[1] != pid:
            why = f"waiting for the build of {held[0]} (pid {held[1]}) to end"
        elif running:
            why = f"waiting for {len(running)} game(s) to end (pids {running})"
        elif try_take(owner, pid, lock):
            print(f"Build lock: {owner} builds (pid {pid})", flush=True)
            return
        else:
            why = "another build took the lock first"
        if why != said:
            print(f"Build lock: {why}", flush=True)
            said = why
        sleep(POLL_SECONDS)


def release(pid: int, lock: pathlib.Path = LOCK) -> None:
    held = holder(lock)
    if held and held[1] == pid:
        lock.unlink(missing_ok=True)


def wait_for_builds(lock: pathlib.Path = LOCK, sleep=time.sleep) -> None:
    """A game waits while a build runs (run_scenario.py's launch)."""
    said = None
    while held := holder(lock):
        if held != said:
            print(f"Build lock: waiting for the build of {held[0]} (pid {held[1]}) before "
                  "starting a game", flush=True)
            said = held
        sleep(POLL_SECONDS)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="action", required=True)
    run = sub.add_parser("run", help="run a build command under the lock")
    run.add_argument("--owner", default=lanes.default_owner())
    run.add_argument("command", nargs=argparse.REMAINDER)
    sub.add_parser("status", help="who holds the lock")
    args = parser.parse_args(argv)
    if args.action == "status":
        held = holder()
        print(f"held by {held[0]} (pid {held[1]})" if held else "free")
        return 0
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("run needs a command after --")
    if os.environ.get(HELD) == "1":
        return subprocess.run(command).returncode
    take(args.owner, os.getpid())
    try:
        return subprocess.run(command, env={**os.environ, HELD: "1"}).returncode
    finally:
        release(os.getpid())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
