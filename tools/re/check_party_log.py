# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks a shadPS4 log (user/log/shad_log.txt) from a seamless co-op run.

  check_party_log.py --boot <log>     every co-op patch and hook went in, nothing failed
  check_party_log.py <log> [<log>...] a play session's summary and the known problems in it

Exit code 1 when a check fails (boot) or a known problem shows up (session).
"""
import argparse
import pathlib
import re
import sys

# What a party member's emulator logs at load once each part of seamless co-op is in.
BOOT_MARKERS = {
    "party controller": "Bloodborne party auto-rejoin enabled as ",
    "ghost death": "Bloodborne ghost-death patch: ",
    "host death": "Bloodborne host-death patch: ",
    "boss clear": "Bloodborne boss-clear patch: a boss kill keeps the session",
    "join anywhere": "Bloodborne join-anywhere patch: ",
    "host leaves": "Bloodborne party guests stay where they are when their host leaves",
    "seamless travel": "Bloodborne seamless travel: ",
    "lamps": "Bloodborne lamps as campfires: ",
    "level sync": "Bloodborne party guests keep their own level",
    "solid NPCs": "Bloodborne party guests see NPCs as the host does",
    "full echoes": "Bloodborne party guests get full echoes from kills",
    "shared loot": "Bloodborne party guests see and pick up their own map items",
    "sign timers": "Bloodborne sign timers patched",
    "seamless patches": "Bloodborne seamless patches enabled",
}

# An error from the co-op code, or anything critical. The reverse-engineering trace
# (SHADPS4_BLOODBORNE_RE_TRACE=1) is diagnostics only: its hooks that do not go in are warnings
# (two of them, Event.WarpNextStage and Warp.RespawnTransform.Resolve, never have).
FAILURE = re.compile(r"\[Debug\] <Error> .*bloodborne_(re|party)\.cpp|<Critical>")
TRACE_ONLY = "InstallReverseEngineeringTrace"


def is_failure(line: str) -> bool:
    return bool(FAILURE.search(line)) and TRACE_ONLY not in line
REVISION = re.compile(r"Run: Revision ([0-9a-f]{7,40})")

# Session events worth counting, and the ones that point at a known problem.
EVENTS = {
    "searches": "party auto-rejoin started search",
    "trips": "Bloodborne party trip started",
    "time-of-day trips": "a world reload (time of day) is a trip",
    "players re-created": "re-creating",
    "enemies lowered by the others' damage": "enemies by the other members' damage",
    "echoes from kills apart": "echoes from a kill another member made apart",
    "kill board posts": "kill board gets",
    "enemies hidden (killed by a member)": "enemies a member killed",
    "member leaves": "LogMemberLeave",
    "rooms left": "sceNpMatching2LeaveRoom",
}
PROBLEMS = {
    "a session member's online id was not found (3 players fall back to waiting)":
        re.compile(r"re-creating.* \?"),
    "the game left the session outside a trip":
        re.compile(r"(LeaveSession|CSMultiPlayMan StopRequest) from the game"),
    "the graphics device was lost (GPU driver or emulator, not co-op)":
        re.compile(r"Device lost"),
    "a crash was caught": re.compile(r"crash: exception"),
}
# A game call skipped because its object's vtable was not the expected kind (HasVtable): fine
# for a stale pointer, but a vtable list missing a class quietly turns a feature off.
REFUSAL = re.compile(r"refused a game call")
PROBLEMS["a guarded game call was refused (a wrong object, or a vtable list missing a class)"] = \
    REFUSAL


def check_boot(text: str) -> list[str]:
    """The co-op parts missing from a boot log, and the co-op errors in it."""
    failures = [f"missing: {name}" for name, marker in BOOT_MARKERS.items() if marker not in text]
    failures += [f"error: {line.strip()[:200]}" for line in text.splitlines()
                 if is_failure(line)]
    failures += [f"refused: {line.strip()[:200]}" for line in text.splitlines()
                 if REFUSAL.search(line)]
    return failures


def trace_warnings(text: str) -> list[str]:
    """The diagnostics-only trace hooks that did not go in."""
    return [line.strip()[:200] for line in text.splitlines()
            if FAILURE.search(line) and TRACE_ONLY in line]


def summarize(text: str) -> tuple[dict[str, int], list[str]]:
    """Counts of the session events and the known problems seen (with a sample line)."""
    lines = text.splitlines()
    counts = {name: sum(marker in line for line in lines) for name, marker in EVENTS.items()}
    problems = []
    for name, pattern in PROBLEMS.items():
        hits = [line for line in lines if pattern.search(line)]
        if hits:
            problems.append(f"{name}: {len(hits)}x, e.g. {hits[0].strip()[:160]}")
    problems += [f"co-op error: {line.strip()[:200]}" for line in lines if is_failure(line)]
    return counts, problems


def revision(text: str) -> str:
    found = REVISION.search(text)
    return found.group(1)[:12] if found else "unknown"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--boot", action="store_true", help="check a boot log")
    parser.add_argument("logs", nargs="+", type=pathlib.Path)
    args = parser.parse_args(argv)
    status = 0
    for log in args.logs:
        text = log.read_text(encoding="utf-8", errors="replace")
        print(f"{log} (build {revision(text)})")
        if args.boot:
            failures = check_boot(text)
            for failure in failures:
                print(f"  FAIL {failure}")
            for warning in trace_warnings(text):
                print(f"  warning (trace only) {warning}")
            print("  boot: " + ("FAILED" if failures else f"ok, {len(BOOT_MARKERS)} parts in"))
            status |= 1 if failures else 0
            continue
        counts, problems = summarize(text)
        for name, count in counts.items():
            print(f"  {name}: {count}")
        for problem in problems:
            print(f"  PROBLEM {problem}")
        status |= 1 if problems else 0
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
