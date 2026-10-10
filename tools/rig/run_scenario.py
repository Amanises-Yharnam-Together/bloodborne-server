# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Runs a seamless co-op test on the rig with no one at the pad.

Starts the rig's servers afresh for each scenario (no sign, claim or party board left over from
the last one), then each role's game with an autoplay script (SHADPS4_BLOODBORNE_AUTOPLAY, see
ParseAutoplayScript in bloodborne_party.h), waits until every game logs "Bloodborne autoplay:
done" or the time runs out, stops the games and checks the logs: every step ok, the scenario's
expected lines there, and no known problem (check_party_log.py). Each run's game and server logs
stay in rig/runs/<when>/<scenario>/ (the last RUNS_KEPT batches).

  py -3.12 tools/rig/run_scenario.py <scenario> [<scenario>...] [--keep-servers] [--list]
  py -3.12 tools/rig/run_scenario.py --save-baseline
  py -3.12 tools/rig/run_scenario.py --add-guest2   (a third game for parties of three)
  py -3.12 tools/rig/run_scenario.py --boot-check host [--seconds 40]   (tools/smoke-boot.ps1)

Every scenario starts from the same saves: --save-baseline keeps each role's save and party files
(rig/<role>/baseline) and every run puts them back first, so one test's progress (a boss killed,
a lamp lit, a remembered spot) never leaks into the next.

--engine port plays with the native port (Bloodborne PC, tools/build-bbport.ps1) instead of the
emulator, in rig/port/<role> (engines.py); --engine port --setup makes those folders from the
emulator rig's (its game with the update, each role's baseline saves and party files).
"""
import argparse
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "re"))
import check_party_log  # noqa: E402
import engines  # noqa: E402
from engines import BACKEND_PORT, BB_ROOT, SHADNET_PORT  # noqa: E402
from scenarios import SCENARIOS  # noqa: E402

RIG = BB_ROOT / "rig"
RUNS = RIG / "runs"
RUNS_KEPT = 20

# Each role's game: its P2P port and party role. Its folder (its own user dir, save and account)
# is the build's (engines.py).
ROLES = {
    "host": {"port": 51600, "role": "host"},
    "guest": {"port": 51601, "role": "guest"},
    "guest2": {"port": 51602, "role": "guest"},
}

# The build the games run: the emulator unless --engine says otherwise.
ENGINE = engines.ENGINES["emulator"]


def use_engine(name: str):
    global ENGINE
    ENGINE = engines.ENGINES[name]

DONE = re.compile(r"Bloodborne autoplay: done \((ok|failed)\)")
IN_WORLD = "Bloodborne autoplay: in the world"
STEP_FAILED = re.compile(r"Bloodborne autoplay: step .* FAILED: .*")


def port_open(port: int) -> bool:
    with socket.socket() as sock:
        sock.settimeout(0.5)
        return sock.connect_ex(("127.0.0.1", port)) == 0


def split_games(listing: str, exe: pathlib.Path) -> tuple[list[int], list[int]]:
    """The rig's games (the build's game executable) and every other one (Kaniel's own copy),
    from "<pid>\t<path>" lines."""
    rig, others = [], []
    for line in listing.splitlines():
        pid, _, path = line.strip().partition("\t")
        if pid.isdigit():
            same = path.strip().lower() == str(exe).lower()
            (rig if same else others).append(int(pid))
    return rig, others


def games() -> tuple[list[int], list[int]]:
    """Every build's games: someone playing the emulator stops a port run too."""
    names = " OR ".join(f"Name='{name}'" for name in engines.GAME_PROCESSES)
    listing = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         f"Get-CimInstance Win32_Process -Filter \"{names}\" | "
         "ForEach-Object { \"$($_.ProcessId)`t$($_.ExecutablePath)\" }"],
        capture_output=True, text=True).stdout
    return split_games(listing, ENGINE.exe)


def games_running() -> bool:
    rig, others = games()
    return bool(rig or others)


def stop_games():
    """Closes the rig's games, never another copy, and waits until they are gone (their logs
    stay open until then). A window close first: a game stopped hard leaves some of its memory
    with the GPU driver until Windows restarts (0.2-0.4 GB a game, measured), which over a day of
    runs left no room for three games. A game that does not close in 15 s is stopped."""
    for pid in games()[0]:
        subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
    deadline = time.time() + 15
    while time.time() < deadline and games()[0]:
        time.sleep(0.5)
    for pid in games()[0]:
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    deadline = time.time() + 20
    while time.time() < deadline and games()[0]:
        time.sleep(0.5)


def listening_pids(netstat: str, port: int) -> set[int]:
    """The processes `netstat -ano -p TCP` shows listening on `port`."""
    pids = set()
    for line in netstat.splitlines():
        parts = line.split()
        if (len(parts) >= 5 and parts[0] == "TCP" and parts[1].endswith(f":{port}")
                and parts[3] == "LISTENING" and parts[4].isdigit()):
            pids.add(int(parts[4]))
    return pids


def stop_servers():
    """Stops whatever serves the rig's ports (only the rig's: the online kit's are others)."""
    netstat = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True,
                             text=True).stdout
    for pid in listening_pids(netstat, SHADNET_PORT) | listening_pids(netstat, BACKEND_PORT):
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    deadline = time.time() + 15
    while time.time() < deadline and (port_open(SHADNET_PORT) or port_open(BACKEND_PORT)):
        time.sleep(0.5)


def start_server(command: list[str], cwd: pathlib.Path, log: pathlib.Path | None):
    env = dict(os.environ, SHADNET_BLOODBORNE_RE_TRACE="1")  # every summon request's body
    if log is None:
        subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NEW_CONSOLE)
        return
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "wb") as output:  # the server keeps its own handle
        subprocess.Popen(command, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT,
                         creationflags=subprocess.CREATE_NEW_CONSOLE)


def start_servers(log_dir: pathlib.Path | None = None):
    """The rig's shadNet and backend, the ones not up yet, their output in log_dir."""
    if not port_open(SHADNET_PORT):
        start_server([str(RIG / "server" / "shadnet.exe")], RIG / "server",
                     log_dir / "shadnet.log" if log_dir else None)
    if not port_open(BACKEND_PORT):
        start_server([sys.executable, "bb_dummy_server.py", "--address", "127.0.0.1", "--port",
                      str(BACKEND_PORT), "--shadnet-webapi", "http://127.0.0.1:32315"],
                     RIG / "backend", log_dir / "backend.log" if log_dir else None)
    deadline = time.time() + 30
    while time.time() < deadline and not (port_open(SHADNET_PORT) and port_open(BACKEND_PORT)):
        time.sleep(0.5)


def prune_runs(runs: pathlib.Path, kept: int):
    """Removes all but the newest `kept` batch folders (named by time, so they sort by it)."""
    batches = sorted(path for path in runs.iterdir() if path.is_dir()) if runs.is_dir() else []
    for old in batches[:-kept] if kept > 0 else batches:
        shutil.rmtree(old, ignore_errors=True)


def user_dir(role: str) -> pathlib.Path:
    return ENGINE.user_dir(role)


def save_baseline(role: str, engine: engines.Emulator | None = None):
    """Keeps the role's save and party files (rig/<role>/baseline) for every later run."""
    engine = engine or ENGINE
    baseline = engine.role_dir(role) / "baseline"
    if baseline.exists():
        shutil.rmtree(baseline)
    shutil.copytree(engine.saves(role), baseline / "savedata")
    for party_file in engine.user_dir(role).glob("bloodborne_party_*.txt"):
        shutil.copy2(party_file, baseline / party_file.name)


def restore_baseline(role: str) -> bool:
    baseline = ENGINE.role_dir(role) / "baseline"
    if not baseline.is_dir():
        return False
    saves = ENGINE.saves(role)
    shutil.rmtree(saves, ignore_errors=True)
    shutil.copytree(baseline / "savedata", saves)
    for party_file in user_dir(role).glob("bloodborne_party_*.txt"):
        party_file.unlink()
    for party_file in baseline.glob("bloodborne_party_*.txt"):
        shutil.copy2(party_file, user_dir(role) / party_file.name)
    return True


def setup_port(emulator: engines.Emulator, port: engines.Port) -> list[str]:
    """The port's rig folders, from the emulator rig's: the game with its update in one folder
    (the port reads one), and each role's saves and party files (its baseline, else its current
    ones) as the port's own baseline. A role the port has a folder for already stays as it is.
    The roles made."""
    game = emulator.game.parent
    engines.merge_game(game, game.with_name(f"{game.name}-patch"), port.game_dir)
    made = []
    for role in ROLES:
        if not emulator.user_dir(role).is_dir() or port.user_dir(role).exists():
            continue
        baseline = emulator.role_dir(role) / "baseline"
        saves = baseline / "savedata" if baseline.is_dir() else emulator.saves(role)
        party_files = baseline if baseline.is_dir() else emulator.user_dir(role)
        port.saves(role).mkdir(parents=True)
        if saves.is_dir():
            engines.copy_saves(saves, port.saves(role))
        for party_file in party_files.glob("bloodborne_party_*.txt"):
            shutil.copy2(party_file, port.user_dir(role) / party_file.name)
        port.prepare(role)
        save_baseline(role, port)
        made.append(role)
    return made


def add_guest2():
    """rig/guest2: the guest's folder (its save, settings, mods) with its own account
    rig_guest2, registered on the rig's shadNet."""
    source = user_dir("guest")
    target = user_dir("guest2")
    if target.exists():
        print(f"{target} exists already")
    else:
        shutil.copytree(source, target, ignore=shutil.ignore_patterns("log", "captures"))
        (target / "log").mkdir()
        users_file = target / "users.json"  # the emulator's account (the port's: engines.py)
        if users_file.exists():
            users = users_file.read_text(encoding="utf-8")
            users = users.replace('"shadnet_npid": "rig_guest"', '"shadnet_npid": "rig_guest2"')
            users = users.replace('"user_name": "rig_guest"', '"user_name": "rig_guest2"')
            users_file.write_text(users, encoding="utf-8", newline="\n")
        print(f"made {target}")
    baseline = ENGINE.role_dir("guest") / "baseline"
    if baseline.is_dir() and not (ENGINE.role_dir("guest2") / "baseline").exists():
        shutil.copytree(baseline, ENGINE.role_dir("guest2") / "baseline")  # starts where guest does
        print("guest2: baseline copied from the guest's")
    start_servers()
    sample = RIG / "server" / "clientsample"
    result = subprocess.run([str(sample / "shadnet-sample.exe"), "127.0.0.1", str(SHADNET_PORT),
                             "register", "rig_guest2", "rig", "rig_guest2.rig@p2p.db"],
                            cwd=sample, capture_output=True, text=True, timeout=30)
    print(f"register rig_guest2: {(result.stdout + result.stderr).strip()[:200]}")
    stop_servers()


def server_is_current() -> bool:
    """Whether the rig's shadNet has the newest party boards (/bloodborne/party/vitals)."""
    request = urllib.request.Request("http://127.0.0.1:32315/bloodborne/party/vitals",
                                     data=b"probe 0 off 0 0 0 0 0 0 0 0",
                                     headers={"X-ShadPS4-Bloodborne-Party": "probe:probe"})
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.status == 200
    except (urllib.error.URLError, OSError):
        return False


def log_path(role: str) -> pathlib.Path:
    return ENGINE.log_path(role)


def console_path(role: str) -> pathlib.Path:
    """What the port prints (its loader and runtime) beside the log; the emulator prints nothing
    the log has not."""
    return ENGINE.user_dir(role) / "log" / "console.txt"


def read_log(role: str) -> str:
    try:
        return log_path(role).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def stages_of(script: list) -> list[list[str]]:
    """A role's scripts in turn: one script, or a list of them (the game is stopped once a stage
    is done, as a crash or a quit would, and started again with the next)."""
    return script if script and isinstance(script[0], list) else [script]


# Settings a scenario switches on for its games ("settings"); never taken from the PC's own.
SCENARIO_SETTINGS = ("SHADPS4_BLOODBORNE_PARTY_DEATH",)


def game_environment(role: str, script: pathlib.Path | None, party: str,
                     settings: dict[str, str] | None = None) -> dict[str, str]:
    """A role's game's settings: co-op on in `party`, the trace on, the autoplay script if any,
    the scenario's own settings, and what the build needs besides."""
    env = dict(os.environ)
    env.update({
        "SHADPS4_BLOODBORNE_SEAMLESS_COOP": "1",
        "SHADPS4_P2P_PORT": str(ROLES[role]["port"]),
        "SHADPS4_BLOODBORNE_PARTY": party,
        "SHADPS4_BLOODBORNE_PARTY_ROLE": ROLES[role]["role"],
        "SHADPS4_BLOODBORNE_RE_TRACE": "1",
    })
    env.pop("SHADPS4_BLOODBORNE_AUTOPLAY", None)
    if script is not None:
        env["SHADPS4_BLOODBORNE_AUTOPLAY"] = str(script)
    for name in SCENARIO_SETTINGS:
        env.pop(name, None)
    env.update(settings or {})
    env.update(ENGINE.environment(role))
    # In the background: the games' windows (SDL, both engines) open and come up without taking
    # the focus from whatever the person at the PC is doing.
    env["SDL_WINDOW_ACTIVATE_WHEN_SHOWN"] = "0"
    env["SDL_WINDOW_ACTIVATE_WHEN_RAISED"] = "0"
    return env


def background_window_options() -> dict:
    """Popen options for a game started in the background: no console window of its own, and
    its first window shown without being activated."""
    if os.name != "nt":
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4  # SW_SHOWNOACTIVATE
    return {"startupinfo": startup, "creationflags": subprocess.CREATE_NO_WINDOW}


def set_aside(path: pathlib.Path):
    """The last game's file as <name>.prev<suffix>, for the next game's to start afresh."""
    for _ in range(20):  # a game that just stopped may still hold it for a moment
        try:
            if path.exists():
                path.replace(path.with_name(f"{path.stem}.prev{path.suffix}"))
            return
        except PermissionError:
            time.sleep(0.5)


def launch(role: str, script: list[str] | None, party: str = "rig:hunters",
           settings: dict[str, str] | None = None) -> subprocess.Popen:
    folder = ENGINE.role_dir(role)
    script_path = None
    if script is not None:
        script_path = folder / "autoplay.txt"
        script_path.write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    set_aside(log_path(role))
    ENGINE.prepare(role)
    env = game_environment(role, script_path, party, settings)
    if ENGINE.name == "emulator":
        return subprocess.Popen(ENGINE.command(role), cwd=folder, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                **background_window_options())
    console = console_path(role)
    set_aside(console)
    console.parent.mkdir(parents=True, exist_ok=True)
    with open(console, "wb") as output:  # the game keeps its own handle
        return subprocess.Popen(ENGINE.command(role), cwd=folder, env=env,
                                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                **background_window_options())


def stop_game(game: subprocess.Popen):
    """One game, at once (as a crash would), and wait until it is gone: whatever it started
    goes with it (the port's launcher starts the game itself)."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(game.pid)], capture_output=True)
    game.kill()
    try:
        game.wait(timeout=20)
    except subprocess.TimeoutExpired:
        pass


def new_crashes(role: str, since: float) -> list[str]:
    """The first lines of the crash reports a role's game wrote since `since`."""
    folder = user_dir(role) / "log"
    reports = []
    for report in sorted(folder.glob("crash-*.txt")):
        if report.stat().st_mtime >= since:
            reports.append(report.read_text(errors="replace").splitlines()[0])
    return reports


def keep_logs(run_dir: pathlib.Path, roles: list[str], suffix: str = ""):
    """Each role's game log and script, next to the servers' logs."""
    run_dir.mkdir(parents=True, exist_ok=True)
    for role in roles:
        script = ENGINE.role_dir(role) / "autoplay.txt"
        for source, kept in ((log_path(role), f"{role}{suffix}.log"),
                             (console_path(role), f"{role}{suffix}-console.txt"),
                             (script, f"{role}{suffix}-script.txt")):
            try:
                shutil.copy2(source, run_dir / kept)
            except OSError:
                pass


def boot_check(role: str, seconds: int) -> int:
    """The boot smoke test (tools/smoke-boot.ps1): the role's game alone, in a party of its own
    so it never meets a real one, for `seconds`; every co-op patch and hook must be in by then
    (check_party_log.py --boot). Needs no player and no server."""
    if missing := ENGINE.missing():
        print(missing)
        return 2
    if games_running():
        print("A game is running; not starting another.")
        return 2
    started = time.time()
    game = launch(role, None, party="smoke:boot")
    while time.time() < started + seconds and game.poll() is None:
        time.sleep(1)
    exited = game.poll()
    stop_game(game)
    stop_games()
    text = read_log(role)
    print(f"{log_path(role)} (build {check_party_log.revision(text)})")
    if exited is not None:
        print(f"  the game ended by itself after {time.time() - started:.0f} s (code {exited}): "
              f"{boot_failure(text) or 'see ' + str(console_path(role))}")
    failures = check_party_log.check_boot(text)
    for failure in failures:
        print(f"  FAIL {failure}")
    for warning in check_party_log.trace_warnings(text):
        print(f"  warning (trace only) {warning}")
    print("  boot: " + ("FAILED" if failures else
                        f"ok, {len(check_party_log.BOOT_MARKERS)} parts in"))
    return 1 if failures else 0


def free_commit_gb() -> float:
    """What Windows can still commit (its commit limit minus what is committed), in GB."""
    out = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "$c = (Get-Counter '\\Memory\\Committed Bytes','\\Memory\\Commit Limit')"
         ".CounterSamples; $c[1].CookedValue - $c[0].CookedValue"],
        capture_output=True, text=True).stdout.strip()
    try:
        return float(out) / 2**30
    except ValueError:
        return float("inf")  # not known: let the games try


def boot_failure(text: str) -> str | None:
    """Why a game stopped before it got going: the emulator's failed assertion and its reason."""
    found = re.search(r"Assertion Failed!\s*\n(.*)", text)
    return found.group(1).strip() or "assertion failed" if found else None


def until_done(text: str) -> str:
    """A log up to its script's end: what follows is the rig stopping the games (the others see
    the first one go and leave its room)."""
    found = DONE.search(text)
    return text[:found.end()] if found else text


def role_text(run_dir: pathlib.Path, role: str, stages: int) -> str:
    """Everything a role's games logged while their scripts ran, stage after stage."""
    texts = []
    for stage in range(1, stages):
        try:
            texts.append((run_dir / f"{role}-stage{stage}.log").read_text(encoding="utf-8",
                                                                           errors="replace"))
        except OSError:
            pass
    return "\n".join(until_done(text) for text in texts + [read_log(role)])


def run(name: str, batch: pathlib.Path, attempt: int = 1) -> bool:
    scenario = SCENARIOS[name]
    roles = list(scenario["roles"])
    run_dir = batch / (name if attempt == 1 else f"{name}-{attempt}")
    print(f"== {name}: {scenario.get('about', '')}")
    if missing := ENGINE.missing():
        print(f"  {missing}")
        return False
    for role in roles:
        if not user_dir(role).is_dir():
            print(f"  {role}: no rig folder at {ENGINE.role_dir(role)}")
            return False
    if games()[1]:
        print("  another game is running (someone is playing): not starting the rig's")
        return False
    for role in roles:
        if not scenario.get("no_baseline") and restore_baseline(role):
            print(f"  {role}: saves put back to the baseline")
    need = ENGINE.commit_gb * len(roles)
    free = free_commit_gb()
    if free < need:
        print(f"  not enough memory for {len(roles)} games: {free:.1f} GB can be committed, "
              f"{need} GB needed (close something big, or give Windows a bigger page file)")
        return False
    stop_servers()
    start_servers(run_dir)
    if not server_is_current():
        print("  The rig's shadNet is too old (no /bloodborne/party/vitals): copy "
              rf"{BB_ROOT}\out\server\shadnet.exe into {RIG}\server.")
        return False
    started = time.time()
    deadline = started + scenario.get("timeout", 600)
    stages = {role: stages_of(scenario["roles"][role]) for role in roles}
    stage = {role: 0 for role in roles}
    processes = {}
    for role in roles:
        processes[role] = launch(role, stages[role][0], settings=scenario.get("settings"))
        print(f"  started {role}")
        # The next game once this one is in the world, where loading side by side is slow.
        wait_until = time.time() + 90 if ENGINE.staggered_start else 0
        while (time.time() < wait_until and IN_WORLD not in read_log(role)
               and processes[role].poll() is None):
            time.sleep(2)
    done = {}
    crashed = {}
    while time.time() < deadline and len(done) + len(crashed) < len(roles):
        for role in roles:
            if role in done or role in crashed:
                continue
            found = DONE.search(read_log(role))
            if found and found.group(1) == "ok" and stage[role] + 1 < len(stages[role]):
                stage[role] += 1
                print(f"  {role} stage {stage[role]} done after {time.time() - started:.0f} s; "
                      "stopping its game and starting it again")
                stop_game(processes[role])
                keep_logs(run_dir, [role], f"-stage{stage[role]}")
                processes[role] = launch(role, stages[role][stage[role]],
                                         settings=scenario.get("settings"))
            elif found:
                done[role] = found.group(1)
                print(f"  {role} done ({found.group(1)}) after {time.time() - started:.0f} s")
            elif reports := new_crashes(role, started):
                crashed[role] = reports[0]
                print(f"  {role} crashed after {time.time() - started:.0f} s: {reports[0]}")
            elif processes[role].poll() is not None:
                # Gone without a crash report: it never got going (the memory ran out, say).
                crashed[role] = boot_failure(read_log(role)) or "the game exited"
                print(f"  {role} stopped after {time.time() - started:.0f} s: {crashed[role]}")
        if crashed:
            break  # the others wait for it in vain
        time.sleep(2)
    stop_games()
    keep_logs(run_dir, roles)
    if crashed:
        # A crash is the emulator's (the GPU's device lost, so far), not the scenario's: once more.
        if attempt == 1:
            print("  a game crashed; running the scenario once more")
            time.sleep(30)  # what a dead game held goes back to Windows
            return run(name, batch, attempt + 1)
        print("  FAILED: a game crashed again")
        return False

    passed = True
    for role in roles:
        text = role_text(run_dir, role, stage[role] + 1)
        status = done.get(role, "timed out")
        print(f"  {role}: {status} (build {check_party_log.revision(text)})")
        if status != "ok":
            passed = False
        for failed in STEP_FAILED.findall(text):
            print(f"    {failed}")
        for pattern in scenario.get("expect", {}).get(role, []):
            if not re.search(pattern, text):
                print(f"    expected, not seen: {pattern}")
                passed = False
        for pattern in scenario.get("forbid", {}).get(role, []):
            if found := re.search(pattern, text):
                print(f"    must not happen, seen: {found.group(0)[:160]}")
                passed = False
        _, problems = check_party_log.summarize(text)
        allowed = scenario.get("allow", [])
        for problem in problems:
            if not any(re.search(pattern, problem) for pattern in allowed):
                print(f"    PROBLEM {problem}")
                passed = False
    print(f"  {'PASSED' if passed else 'FAILED'} in {time.time() - started:.0f} s (logs in "
          f"{run_dir})")
    return passed


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("scenarios", nargs="*")
    parser.add_argument("--keep-servers", action="store_true",
                        help="leave the rig's servers running afterwards")
    parser.add_argument("--list", action="store_true", help="list the scenarios")
    parser.add_argument("--save-baseline", action="store_true",
                        help="keep every role's saves as the start of later runs")
    parser.add_argument("--add-guest2", action="store_true",
                        help="make the third game's rig folder and account")
    parser.add_argument("--engine", choices=sorted(engines.ENGINES), default="emulator",
                        help="the build the games run (default: the emulator)")
    parser.add_argument("--setup", action="store_true",
                        help="make the port's rig folders from the emulator rig's (--engine port)")
    parser.add_argument("--boot-check", choices=sorted(ROLES), metavar="ROLE",
                        help="boot the role's game alone and check that co-op went in")
    parser.add_argument("--seconds", type=int, default=40,
                        help="how long --boot-check lets the game boot (default 40)")
    args = parser.parse_args(argv)
    use_engine(args.engine)
    if args.setup:
        if ENGINE.name != "port":
            print("--setup makes the port's rig folders: --engine port --setup")
            return 2
        if games_running():
            print("A game is running; its game folder may be in use.")
            return 2
        made = setup_port(engines.ENGINES["emulator"], ENGINE)
        print(f"game for the port: {ENGINE.game_dir}")
        print(f"made {', '.join(made)}" if made else "every role had a port folder already")
        return 0
    if args.boot_check:
        return boot_check(args.boot_check, args.seconds)
    if args.add_guest2:
        add_guest2()
        return 0
    if args.save_baseline:
        if games_running():
            print("A game is running; its saves may be half written.")
            return 2
        for role in ROLES:
            if user_dir(role).is_dir():
                save_baseline(role)
                print(f"{role}: baseline saved")
        return 0
    if args.list or not args.scenarios:
        for name, scenario in SCENARIOS.items():
            print(f"{name}: {scenario.get('about', '')} ({', '.join(scenario['roles'])})")
        return 0
    if games_running():
        print("A game is running; not starting the rig.")
        return 2
    unknown = [name for name in args.scenarios if name not in SCENARIOS]
    if unknown:
        print(f"No such scenario: {', '.join(unknown)} (--list shows them)")
        return 2
    batch = RUNS / time.strftime("%Y%m%d-%H%M%S")
    prune_runs(RUNS, RUNS_KEPT - 1)
    try:
        results = {name: run(name, batch) for name in args.scenarios}
    finally:
        stop_games()
        if not args.keep_servers:
            stop_servers()
    print("\n".join(f"{'ok  ' if ok else 'FAIL'} {name}" for name, ok in results.items()))
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
