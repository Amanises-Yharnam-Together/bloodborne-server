# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Runs a seamless co-op test on the rig with no one at the pad.

Starts the rig's servers if they are not up, then each role's game with an autoplay script
(SHADPS4_BLOODBORNE_AUTOPLAY, see ParseAutoplayScript in bloodborne_party.h), waits until every
game logs "Bloodborne autoplay: done" or the time runs out, stops the games and checks the logs:
every step ok, the scenario's expected lines there, and no known problem (check_party_log.py).

  py -3.12 tools/rig/run_scenario.py <scenario> [<scenario>...] [--keep-servers] [--list]
  py -3.12 tools/rig/run_scenario.py --save-baseline
  py -3.12 tools/rig/run_scenario.py --add-guest2   (a third game for parties of three)

Every scenario starts from the same saves: --save-baseline keeps each role's save and party files
(rig/<role>/baseline) and every run puts them back first, so one test's progress (a boss killed,
a lamp lit, a remembered spot) never leaks into the next.
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
from scenarios import SCENARIOS  # noqa: E402

BB_ROOT = pathlib.Path(os.environ.get("BB_ROOT", r"C:\bb"))
RIG = BB_ROOT / "rig"
EXE = BB_ROOT / "out" / "emulator" / "build" / "shadPS4.exe"
GAME = RIG / "game" / "CUSA03173" / "eboot.bin"
SHADNET_PORT = 32313
BACKEND_PORT = 20453

# Each role's game: its folder under the rig (with its own user dir, save and account) and P2P port.
ROLES = {
    "host": {"dir": RIG / "host", "port": 51600, "role": "host"},
    "guest": {"dir": RIG / "guest", "port": 51601, "role": "guest"},
    "guest2": {"dir": RIG / "guest2", "port": 51602, "role": "guest"},
}

DONE = re.compile(r"Bloodborne autoplay: done \((ok|failed)\)")
IN_WORLD = "Bloodborne autoplay: in the world"
STEP_FAILED = re.compile(r"Bloodborne autoplay: step .* FAILED: .*")


def port_open(port: int) -> bool:
    with socket.socket() as sock:
        sock.settimeout(0.5)
        return sock.connect_ex(("127.0.0.1", port)) == 0


def games_running() -> bool:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq shadPS4.exe"], capture_output=True,
                         text=True).stdout
    return "shadPS4.exe" in out or "shadps4.exe" in out


def stop_games():
    subprocess.run(["taskkill", "/F", "/IM", "shadPS4.exe"], capture_output=True)


def start_servers() -> list[subprocess.Popen]:
    """The rig's shadNet and backend, the ones not up yet."""
    started = []
    if not port_open(SHADNET_PORT):
        started.append(subprocess.Popen([str(RIG / "server" / "shadnet.exe")], cwd=RIG / "server",
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                        creationflags=subprocess.CREATE_NEW_CONSOLE))
    if not port_open(BACKEND_PORT):
        started.append(subprocess.Popen(
            ["py", "-3.12", "bb_dummy_server.py", "--address", "127.0.0.1", "--port",
             str(BACKEND_PORT), "--shadnet-webapi", "http://127.0.0.1:32315"],
            cwd=RIG / "backend", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NEW_CONSOLE))
    deadline = time.time() + 30
    while time.time() < deadline and not (port_open(SHADNET_PORT) and port_open(BACKEND_PORT)):
        time.sleep(0.5)
    return started


def user_dir(role: str) -> pathlib.Path:
    return ROLES[role]["dir"] / "user"


def save_baseline(role: str):
    """Keeps the role's save and party files (rig/<role>/baseline) for every later run."""
    baseline = ROLES[role]["dir"] / "baseline"
    if baseline.exists():
        shutil.rmtree(baseline)
    shutil.copytree(user_dir(role) / "home" / "1000" / "savedata", baseline / "savedata")
    for party_file in user_dir(role).glob("bloodborne_party_*.txt"):
        shutil.copy2(party_file, baseline / party_file.name)


def restore_baseline(role: str) -> bool:
    baseline = ROLES[role]["dir"] / "baseline"
    if not baseline.is_dir():
        return False
    saves = user_dir(role) / "home" / "1000" / "savedata"
    shutil.rmtree(saves, ignore_errors=True)
    shutil.copytree(baseline / "savedata", saves)
    for party_file in user_dir(role).glob("bloodborne_party_*.txt"):
        party_file.unlink()
    for party_file in baseline.glob("bloodborne_party_*.txt"):
        shutil.copy2(party_file, user_dir(role) / party_file.name)
    return True


def add_guest2():
    """rig/guest2: the guest's folder (its save, settings, mods) with its own account
    rig_guest2, registered on the rig's shadNet."""
    source = ROLES["guest"]["dir"] / "user"
    target = ROLES["guest2"]["dir"] / "user"
    if target.exists():
        print(f"{target} exists already")
    else:
        shutil.copytree(source, target, ignore=shutil.ignore_patterns("log", "captures"))
        (target / "log").mkdir()
        users = (target / "users.json").read_text(encoding="utf-8")
        users = users.replace('"shadnet_npid": "rig_guest"', '"shadnet_npid": "rig_guest2"')
        users = users.replace('"user_name": "rig_guest"', '"user_name": "rig_guest2"')
        (target / "users.json").write_text(users, encoding="utf-8", newline="\n")
        print(f"made {target}")
    servers = start_servers()
    sample = RIG / "server" / "clientsample"
    result = subprocess.run([str(sample / "shadnet-sample.exe"), "127.0.0.1", str(SHADNET_PORT),
                             "register", "rig_guest2", "rig", "rig_guest2.rig@p2p.db"],
                            cwd=sample, capture_output=True, text=True, timeout=30)
    print(f"register rig_guest2: {(result.stdout + result.stderr).strip()[:200]}")
    for server in servers:
        server.kill()


def server_is_current() -> bool:
    """Whether the rig's shadNet has the newest party boards (/bloodborne/party/hp)."""
    request = urllib.request.Request("http://127.0.0.1:32315/bloodborne/party/hp", data=b"probe 0",
                                     headers={"X-ShadPS4-Bloodborne-Party": "probe:probe"})
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.status == 200
    except (urllib.error.URLError, OSError):
        return False


def log_path(role: str) -> pathlib.Path:
    return ROLES[role]["dir"] / "user" / "log" / "shad_log.txt"


def read_log(role: str) -> str:
    try:
        return log_path(role).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def launch(role: str, script: list[str]):
    config = ROLES[role]
    folder = config["dir"]
    script_path = folder / "autoplay.txt"
    script_path.write_text("\n".join(script) + "\n", encoding="utf-8", newline="\n")
    log = log_path(role)
    if log.exists():
        log.replace(log.with_name("shad_log.prev.txt"))
    env = dict(os.environ)
    env.update({
        "SHADPS4_BLOODBORNE_SEAMLESS_COOP": "1",
        "SHADPS4_P2P_PORT": str(config["port"]),
        "SHADPS4_BLOODBORNE_PARTY": "rig:hunters",
        "SHADPS4_BLOODBORNE_PARTY_ROLE": config["role"],
        "SHADPS4_BLOODBORNE_RE_TRACE": "1",
        "SHADPS4_BLOODBORNE_AUTOPLAY": str(script_path),
    })
    subprocess.Popen([str(EXE), "-g", str(GAME)], cwd=folder, env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def new_crashes(role: str, since: float) -> list[str]:
    """The first lines of the crash reports a role's game wrote since `since`."""
    folder = ROLES[role]["dir"] / "user" / "log"
    reports = []
    for report in sorted(folder.glob("crash-*.txt")):
        if report.stat().st_mtime >= since:
            reports.append(report.read_text(errors="replace").splitlines()[0])
    return reports


def run(name: str, attempt: int = 1) -> bool:
    scenario = SCENARIOS[name]
    roles = list(scenario["roles"])
    print(f"== {name}: {scenario.get('about', '')}")
    for role in roles:
        if not (ROLES[role]["dir"] / "user").is_dir():
            print(f"  {role}: no rig folder at {ROLES[role]['dir']}")
            return False
    for role in roles:
        if not scenario.get("no_baseline") and restore_baseline(role):
            print(f"  {role}: saves put back to the baseline")
    started = time.time()
    deadline = started + scenario.get("timeout", 600)
    launched = []
    for role in roles:
        launch(role, scenario["roles"][role])
        launched.append(role)
        print(f"  started {role}")
        # The next game once this one is in the world (two games loading at once is slow).
        wait_until = time.time() + 90
        while time.time() < wait_until and IN_WORLD not in read_log(role):
            time.sleep(2)
    done = {}
    crashed = {}
    while time.time() < deadline and len(done) + len(crashed) < len(roles):
        for role in roles:
            if role in done or role in crashed:
                continue
            found = DONE.search(read_log(role))
            if found:
                done[role] = found.group(1)
                print(f"  {role} done ({found.group(1)}) after {time.time() - started:.0f} s")
            elif reports := new_crashes(role, started):
                crashed[role] = reports[0]
                print(f"  {role} crashed after {time.time() - started:.0f} s: {reports[0]}")
        if crashed:
            break  # the others wait for it in vain
        time.sleep(2)
    stop_games()
    if crashed:
        # A crash is the emulator's (the GPU's device lost, so far), not the scenario's: once more.
        if attempt == 1:
            print("  a game crashed; running the scenario once more")
            time.sleep(5)
            return run(name, attempt + 1)
        print("  FAILED: a game crashed again")
        return False

    passed = True
    for role in roles:
        text = read_log(role)
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
    print(f"  {'PASSED' if passed else 'FAILED'} in {time.time() - started:.0f} s")
    return passed


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("scenarios", nargs="*")
    parser.add_argument("--keep-servers", action="store_true",
                        help="leave the servers this run started running")
    parser.add_argument("--list", action="store_true", help="list the scenarios")
    parser.add_argument("--save-baseline", action="store_true",
                        help="keep every role's saves as the start of later runs")
    parser.add_argument("--add-guest2", action="store_true",
                        help="make the third game's rig folder and account")
    args = parser.parse_args(argv)
    if args.add_guest2:
        add_guest2()
        return 0
    if args.save_baseline:
        if games_running():
            print("A game is running; its saves may be half written.")
            return 2
        for role, config in ROLES.items():
            if (config["dir"] / "user").is_dir():
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
    servers = start_servers()
    if not server_is_current():
        print("The rig's shadNet is too old (no /bloodborne/party/hp): copy "
              rf"{BB_ROOT}\out\server\shadnet.exe into {RIG}\server.")
        for server in servers:
            server.kill()
        return 2
    try:
        results = {name: run(name) for name in args.scenarios}
    finally:
        stop_games()
        if not args.keep_servers:
            for server in servers:
                server.kill()
    print("\n".join(f"{'ok  ' if ok else 'FAIL'} {name}" for name, ok in results.items()))
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
