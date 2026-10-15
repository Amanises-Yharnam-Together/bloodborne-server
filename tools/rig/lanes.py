# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Who holds which rig lane (Kaniel 2026-10-15: lanes are shared between threads, so a lane
needs an owner).

A run takes its lane by writing rig/lanes/<n>/owner (lane 0's too) and frees it when it ends:
"<owner>\\n<pid>\\n<when>". A lane another run holds is skipped; a claim whose process is gone
(a run killed outright) is stale and taken over. A lane whose games outlived their run and could
not be stopped (a hung GPU teardown, lane 2 on 2026-10-14) is marked blocked
(rig/lanes/<n>/blocked, their pids) until those processes are gone.

  py -3.12 tools/rig/run_scenario.py --engine port --lanes-status
"""
import ctypes
import os
import pathlib
import socket
import time

OWNER_FILE = "owner"
BLOCKED_FILE = "blocked"


def pid_alive(pid: int) -> bool:
    """Whether process `pid` still runs."""
    if pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    try:
        return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
    finally:
        kernel.CloseHandle(handle)


def default_owner() -> str:
    """Who a claim names: BB_RIG_OWNER (a thread's name), else this user on this PC."""
    return os.environ.get("BB_RIG_OWNER") or f"{os.environ.get('USERNAME', 'someone')}@" \
                                              f"{socket.gethostname()}"


def lane_folder(rig: pathlib.Path, lane: int) -> pathlib.Path:
    return rig / "lanes" / str(lane)


def available(rig: pathlib.Path) -> list[int]:
    """The lanes set up on this rig: 0, and every rig/lanes/<n> with its servers."""
    found = [0]
    folder = rig / "lanes"
    if folder.is_dir():
        found += sorted(int(path.name) for path in folder.iterdir()
                        if path.name.isdigit() and int(path.name) > 0
                        and (path / "server").is_dir())
    return found


def read_claim(rig: pathlib.Path, lane: int) -> dict | None:
    try:
        lines = (lane_folder(rig, lane) / OWNER_FILE).read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    pid = int(lines[1]) if len(lines) > 1 and lines[1].strip().isdigit() else 0
    return {"owner": lines[0] if lines else "?", "pid": pid,
            "since": lines[2] if len(lines) > 2 else "?"}


def blocked_by(rig: pathlib.Path, lane: int, alive=pid_alive) -> list[int]:
    """The processes that still block the lane (none: its blocked mark goes)."""
    path = lane_folder(rig, lane) / BLOCKED_FILE
    try:
        pids = [int(word) for word in path.read_text(encoding="utf-8").split() if word.isdigit()]
    except OSError:
        return []
    running = [pid for pid in pids if alive(pid)]
    if not running:
        path.unlink(missing_ok=True)
    return running


def block(rig: pathlib.Path, lane: int, pids: list[int]):
    folder = lane_folder(rig, lane)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / BLOCKED_FILE).write_text(" ".join(map(str, pids)) + "\n", encoding="utf-8")


def claim(rig: pathlib.Path, lane: int, owner: str, pid: int | None = None,
          alive=pid_alive, now=time.time) -> bool:
    """Takes the lane for `owner` (process `pid`), unless a live run holds it or it is blocked.
    The owner file is made exclusively: of two runs taking one lane at once, one gets it."""
    pid = os.getpid() if pid is None else pid
    if blocked_by(rig, lane, alive):
        return False
    folder = lane_folder(rig, lane)
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / OWNER_FILE
    for _ in range(2):
        try:
            handle = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            held = read_claim(rig, lane)
            if held is not None and held["pid"] == pid:
                return True  # already ours
            if held is not None and alive(held["pid"]):
                return False
            path.unlink(missing_ok=True)  # stale: its run is gone
            continue
        with os.fdopen(handle, "w", encoding="utf-8") as file:
            stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now()))
            file.write(f"{owner}\n{pid}\n{stamp}\n")
        return True
    return False


def release(rig: pathlib.Path, lane: int, pid: int | None = None):
    """Frees the lane if this process (`pid`) holds it."""
    pid = os.getpid() if pid is None else pid
    held = read_claim(rig, lane)
    if held is not None and held["pid"] == pid:
        (lane_folder(rig, lane) / OWNER_FILE).unlink(missing_ok=True)


def claim_free(rig: pathlib.Path, count: int, owner: str, pid: int | None = None,
               alive=pid_alive) -> list[int]:
    """Up to `count` lanes for `owner`, the lowest free ones first."""
    taken = []
    for lane in available(rig):
        if len(taken) == count:
            break
        if claim(rig, lane, owner, pid, alive):
            taken.append(lane)
    return taken


def status(rig: pathlib.Path, alive=pid_alive) -> list[str]:
    """One line a lane: free, held (by whom, since when) or blocked."""
    lines = []
    for lane in available(rig):
        held = read_claim(rig, lane)
        if blocked := blocked_by(rig, lane, alive):
            lines.append(f"lane {lane}: blocked by stray games {', '.join(map(str, blocked))}")
        elif held is not None and alive(held["pid"]):
            lines.append(f"lane {lane}: held by {held['owner']} (pid {held['pid']}, since "
                         f"{held['since']})")
        else:
            lines.append(f"lane {lane}: free")
    return lines


def strays(listing: str, role_dirs: list[pathlib.Path]) -> list[int]:
    """The processes in a "<pid>\\t<command line>" listing that run in one of a lane's role
    folders (its games, and the port's run.py starting them)."""
    wanted = [str(folder).lower().rstrip("\\/") + os.sep for folder in role_dirs]
    found = []
    for line in listing.splitlines():
        pid, _, command = line.strip().partition("\t")
        command = command.lower().replace("/", os.sep)
        if pid.isdigit() and any(folder in command + os.sep for folder in wanted):
            found.append(int(pid))
    return found
