# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks a shadPS4 log (user/log/shad_log.txt) from a seamless co-op run.

  check_party_log.py --boot <log>     every co-op patch and hook went in, nothing failed
  check_party_log.py <log> [<log>...] a play session's summary and the known problems in it
  check_party_log.py --trace <log>    also the co-op traces next to the log (coop-trace-*.txt):
                                      frames per type, board failures, peers silent over 5 s

Exit code 1 when a check fails (boot) or a known problem shows up (session).
"""
import argparse
import json
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
    "feature summary": "Co-op features: ",
}

# The co-op install's one summary line ("Co-op features: N/M on (skipped: <feature> [<reason>],
# ...)") and which features are required, from the co-op site table
# (emulator/src/core/bloodborne_patch_sites.h, written out for the launcher). A required feature
# skipped (a game patch over its bytes, a hook that did not go in) fails the log.
COOP_SITES = pathlib.Path(__file__).resolve().parents[2] / "bbport" / "patches" / "coop-sites.json"
FEATURES = re.compile(r"Co-op features: \d+/\d+ on(?: \(skipped: (.*)\))?\s*$")
SKIPPED = re.compile(r"(?:^|, )(.+?) \[(.*?)\]")


def required_features(path: pathlib.Path = COOP_SITES) -> set[str]:
    table = json.loads(path.read_text(encoding="utf-8"))
    return {feature["name"] for feature in table["features"] if feature["required"]}


def skipped_features(text: str) -> list[tuple[str, str]]:
    """(feature, reason) skipped at the last co-op install in the log."""
    summaries = [found for found in map(FEATURES.search, text.splitlines()) if found]
    if not summaries or summaries[-1].group(1) is None:
        return []
    return SKIPPED.findall(summaries[-1].group(1))


def skipped_required(text: str) -> list[str]:
    required = required_features()
    return [f"required co-op feature skipped: {name} [{reason}]"
            for name, reason in skipped_features(text) if name in required]

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
    # Phase 9's proof test: the rig lets a game reach the server only (SHADPS4_NET_FIREWALL).
    "the game reached past the server (another game, or the internet)":
        re.compile(r"Net firewall: blocked"),
}
# A game call skipped because its object's vtable was not the expected kind (HasVtable): fine
# for a stale pointer, but a vtable list missing a class quietly turns a feature off.
REFUSAL = re.compile(r"refused a game call")
PROBLEMS["a guarded game call was refused (a wrong object, or a vtable list missing a class)"] = \
    REFUSAL


def check_boot(text: str) -> list[str]:
    """The co-op parts missing from a boot log, and the co-op errors in it."""
    failures = [f"missing: {name}" for name, marker in BOOT_MARKERS.items() if marker not in text]
    failures += skipped_required(text)
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
    problems += skipped_required(text)
    problems += [f"co-op error: {line.strip()[:200]}" for line in lines if is_failure(line)]
    return counts, problems


def revision(text: str) -> str:
    found = REVISION.search(text)
    return found.group(1)[:12] if found else "unknown"


# The co-op trace (docs/WORLD_SYNC.md, Stage B): "<UTC ms> <kind> <key>=<value> ..." per line.
TRACE_GLOB = "coop-trace-*.txt"
# A peer whose frames stop this long (each one sends every 0.5 s) is an outage candidate.
SILENT_MS = 5_000


def traces_next_to(log: pathlib.Path) -> list[pathlib.Path]:
    """The co-op traces in the log's folder, oldest first (their names start with the time)."""
    return sorted(log.parent.glob(TRACE_GLOB))


def trace_records(text: str):
    """(UTC ms, kind, fields) per readable line; the values are left escaped."""
    for raw in text.splitlines():
        tokens = raw.split()
        if len(tokens) < 2 or not tokens[0].isdigit():
            continue
        fields = dict(token.split("=", 1) for token in tokens[2:] if "=" in token)
        yield int(tokens[0]), tokens[1], fields


def summarize_trace(text: str) -> dict:
    """Frames per direction and type, board posts and failures per endpoint, lines dropped, the
    gaps over SILENT_MS between one peer's frames, and each change of the kills' path (the world
    log or the kill board)."""
    frames: dict[str, int] = {}
    posts: dict[str, int] = {}
    failures: dict[str, int] = {}
    last_from: dict[str, int] = {}
    gaps = []
    paths = []
    dropped = 0
    for at, kind, fields in trace_records(text):
        if kind == "wlog-path":
            why = fields.get("why", "?").replace("\\s", " ")
            what = f"{fields['what']} " if "what" in fields else ""
            paths.append(f"{at} {what}{fields.get('path', '?')} ({why})")
        if kind in ("p2p-in", "p2p-out"):
            if fields.get("from-peer") == "0":
                continue  # not a room peer: the game drops it
            key = f"{kind} {fields.get('type', '?')}"
            frames[key] = frames.get(key, 0) + 1
            if kind == "p2p-in":
                peer = fields.get("peer", "?")
                if peer in last_from and at - last_from[peer] > SILENT_MS:
                    gaps.append(f"peer {peer} silent {(at - last_from[peer]) / 1000:.1f} s "
                                f"until {at}")
                last_from[peer] = at
        elif kind == "board-out":
            endpoint = fields.get("endpoint", "?")
            posts[endpoint] = posts.get(endpoint, 0) + 1
        elif kind == "board-in" and ("error" in fields or fields.get("status") != "200"):
            endpoint = fields.get("endpoint", "?")
            failures[endpoint] = failures.get(endpoint, 0) + 1
        elif kind == "trace-dropped":
            dropped += int(fields.get("count", "0"))
    return {"frames": frames, "board posts": posts, "board failures": failures,
            "dropped": dropped, "gaps": gaps, "kills paths": paths}


def print_trace_summary(trace: pathlib.Path) -> None:
    summary = summarize_trace(trace.read_text(encoding="utf-8", errors="replace"))
    print(f"  trace {trace.name}")
    for key, count in sorted(summary["frames"].items()):
        print(f"    {key}: {count}")
    for endpoint, count in sorted(summary["board posts"].items()):
        failed = summary["board failures"].get(endpoint, 0)
        print(f"    {endpoint}: {count} posts, {failed} failed")
    if summary["dropped"]:
        print(f"    lines dropped (queue full): {summary['dropped']}")
    for gap in summary["gaps"]:
        print(f"    outage candidate: {gap}")
    for path in summary["kills paths"]:
        print(f"    kills path: {path}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--boot", action="store_true", help="check a boot log")
    parser.add_argument("--trace", action="store_true",
                        help="also summarize the co-op traces next to each log")
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
        if args.trace:
            for trace in traces_next_to(log):
                print_trace_summary(trace)
        status |= 1 if problems else 0
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
