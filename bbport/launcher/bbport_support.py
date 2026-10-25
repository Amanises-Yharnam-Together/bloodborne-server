# SPDX-License-Identifier: GPL-2.0-or-later
"""The support bundle (Kaniel 2026-10-25): one zip with what a bug report needs, made by the launcher's
Support bundle button, while the game runs too. It holds the logs (the game's, the co-op server's, the
launcher's, the co-op traces), the crash and freeze reports, the settings and a network check. It never
holds a save, a game file or the player's secret: coop.json's password is left out, and in the
launcher's own texts the user folder's path shows as %USERPROFILE%.

The game keeps writing its logs while the bundle copies them (their files are shared for reading), so a
bundle made in the middle of a session has everything up to that moment. Zipping runs at background
priority (worker_priority): the game's frame rate comes first."""
import json
import os
import platform
import socket
import sys
import time
import zipfile
from pathlib import Path

import bbport_coop

# The newest few of each kind: a trace can reach 50 MB (TraceFileCap) and a dump a few MB.
KEPT_TRACES = 3
KEPT_DUMPS = 3
# What the zip leaves out of user\log: what we sent already is noted, not news.
SKIPPED_LOG_NAMES = {bbport_coop.REPORTS_SENT}
SECRET_KEYS = ('password',)
LEFT_OUT = '(left out)'


def bundle_name(now=None):
    """'Yharnam Together support 20261025-120000.zip', in local time."""
    return time.strftime('Yharnam Together support %Y%m%d-%H%M%S.zip', time.localtime(now))


def default_folder(home=None):
    """Where the zip goes: the Desktop (OneDrive's too), else the home folder."""
    home = Path(home or Path.home())
    for desktop in (home / 'Desktop', home / 'OneDrive' / 'Desktop'):
        if desktop.is_dir():
            return desktop
    return home


def newest(paths, kept):
    return sorted(paths, key=lambda path: path.stat().st_mtime)[-kept:] if kept else []


def log_files(log):
    """user\\log's files for the bundle as (path, name in the zip): every log and report, the older runs'
    logs, and only the newest KEPT_TRACES co-op traces and KEPT_DUMPS crash and freeze dumps."""
    log = Path(log)
    if not log.is_dir():
        return []
    files = [path for path in log.iterdir() if path.is_file() and path.name not in SKIPPED_LOG_NAMES]
    traces = [path for path in files if path.name.startswith('coop-trace-')]
    dumps = [path for path in files if path.suffix == '.dmp']
    chosen = [path for path in files if path not in traces and path not in dumps]
    chosen += newest(traces, KEPT_TRACES)
    for kind in ('crash-', 'freeze-'):
        chosen += newest([path for path in dumps if path.name.startswith(kind)], KEPT_DUMPS)
    found = [(path, f'log/{path.name}') for path in sorted(chosen)]
    older = log / 'older'
    if older.is_dir():
        found += [(path, f'log/older/{path.name}') for path in sorted(older.iterdir()) if path.is_file()]
    return found


def without_secrets(settings):
    """coop.json's settings with the password replaced: the player id stays (the server's log names it)."""
    return {key: (LEFT_OUT if key in SECRET_KEYS and value else value) for key, value in settings.items()}


def hide_home(text, home=None):
    home = str(home or Path.home())
    return text.replace(home, '%USERPROFILE%') if home and home not in ('/', '\\') else text


def port_answers(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def network_report(settings, answers=port_answers, addresses=None, this_pc=None):
    """What the co-op connection looks like from this PC: the host's address, whether this PC is the
    host, this PC's addresses and VPNs, and whether the host's server ports answer."""
    host = str(settings.get('host_ip', ''))
    addresses = bbport_coop.own_addresses() if addresses is None else addresses
    hosting = bbport_coop.is_this_pc(host) if this_pc is None else this_pc
    lines = [f"Host's IP address: {host or '(not set)'}" + (f' ({bbport_coop.vpn_name(host)})'
                                                            if bbport_coop.vpn_name(host) else ''),
             f'This PC hosts: {"yes" if hosting else "no"}',
             f'This PC\'s addresses: {", ".join(addresses) or "(none found)"}',
             f'VPNs on this PC: {", ".join(f"{name} {ip}" for name, ip in bbport_coop.vpn_addresses(addresses)) or "none"}']
    if bbport_coop.valid_ip(host):
        for name, port in (('co-op server (shadnet)', bbport_coop.SHADNET_PORT),
                           ('reports and web API', bbport_coop.WEBAPI_PORT),
                           ("the game's server port", bbport_coop.BACKEND_PORT)):
            lines.append(f'{name}, TCP {port}: {"answers" if answers(host, port) else "no answer"}')
    return '\n'.join(lines) + '\n'


def system_report(versions, now=None):
    """The launcher's and the release's versions, Windows, Python and the CPU."""
    lines = [f'Made: {time.strftime("%Y-%m-%d %H:%M:%S %z", time.localtime(now))}']
    lines += [f'{name}: {value}' for name, value in versions.items()]
    lines += [f'System: {platform.platform()}', f'Machine: {platform.machine()}',
              f'Processor: {platform.processor() or "?"}', f'CPU threads: {os.cpu_count()}',
              f'Python: {sys.version.split()[0]}']
    return '\n'.join(lines) + '\n'


def worker_priority():
    """Background priority for the calling thread on Windows (CPU and disk), so zipping never takes
    time from the game."""
    if os.name != 'nt':
        return
    try:
        import ctypes
        THREAD_MODE_BACKGROUND_BEGIN = 0x00010000
        ctypes.windll.kernel32.SetThreadPriority(ctypes.windll.kernel32.GetCurrentThread(),
                                                 THREAD_MODE_BACKGROUND_BEGIN)
    except (AttributeError, OSError):
        pass


def make_bundle(destination, user_dir, settings=None, extra_files=(), texts=None, home=None):
    """Writes the zip to DESTINATION and returns what it could not read as [(name, reason)].

    USER_DIR is the install's user folder (its log folder, coop.json, host_overrides.custom.json);
    SETTINGS the co-op settings (their password is left out); EXTRA_FILES more (path, name in the zip),
    such as bbport.ini; TEXTS {name: text} written as they are (the launcher's log, system.txt,
    network.txt). Missing files are skipped quietly; a file that cannot be read is listed in
    missing.txt inside the zip, and returned."""
    user_dir = Path(user_dir)
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    files = log_files(user_dir / 'log')
    for name in ('last_run.log', 'host_overrides.custom.json'):
        if (user_dir / name).is_file():
            files.append((user_dir / name, name))
    files += [(Path(path), name) for path, name in extra_files if Path(path).is_file()]
    texts = dict(texts or {})
    if settings is not None:
        texts['coop.json'] = json.dumps(without_secrets(settings), indent=2) + '\n'
    failed = []
    partial = destination.with_name(destination.name + '.part')
    with zipfile.ZipFile(partial, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as bundle:
        for name, text in texts.items():
            bundle.writestr(name, hide_home(text, home))
        for path, name in files:
            try:
                bundle.write(path, name)
            except OSError as error:  # a dump still being written, an antivirus holding the file
                failed.append((name, error.strerror or str(error)))
        if failed:
            bundle.writestr('missing.txt', ''.join(f'{name}: {reason}\n' for name, reason in failed))
    os.replace(partial, destination)
    return failed
