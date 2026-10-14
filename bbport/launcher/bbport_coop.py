# SPDX-License-Identifier: GPL-2.0-or-later
"""Seamless co-op in the launcher: what the release's bat files and tools\\setup.ps1 did, so a
player installs one setup .exe and plays from BLauncher.exe (Kaniel 2026-10-13: no bat files, the
launcher finds the game).

A co-op install is the release's folder: BLauncher.exe with event-reference.json and
tools\\party_boss_events.py beside it. Its answers live in user\\coop.json (the old settings.bat
is read once). Nothing here touches the player's game folders: the game the port runs is
coop-game\\CUSA03173, folder links (junctions) and hard links to the game with the 1.09 update
over it, and the co-op event files go in coop-game\\CUSA03173-mods.
"""
import collections
import ctypes
import ipaddress
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

TITLE_IDS = ('CUSA03173', 'CUSA00900', 'CUSA00207', 'CUSA01363', 'CUSA03023')  # every Bloodborne release
TESTED_TITLE = 'CUSA03173'
GAME_VERSION = '01.09'
SHADNET_PORT, WEBAPI_PORT, MATCHING_PORT, BACKEND_PORT = 31313, 31315, 31314, 20443
GAME_SERVER = 'https://ss4.scej-network.jp:20443'
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
DETACHED = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0)

# The answers, as 1 - SETUP asked them (FPS is gone: co-op is always the fixed 60 FPS mode; the party is
# its password alone, Kaniel 2026-10-13: the party name went).
DEFAULTS = {'game': '', 'update': '', 'role': 'guest', 'host_ip': '', 'account': '', 'password': '',
            'party_secret': '', 'p2p_port': 'auto', 'harder': 'off', 'prepared': ''}
PARTY_NAME = 'party'  # the party's name on the server: every party there is told apart by its password
HARDER_ON = '3,1.5,3'  # the kit's harder enemies: HP, damage and poise multipliers


# --- answers ----------------------------------------------------------------------------------

def valid_ip(value):
    """An IPv4 address the other players can reach: never loopback (shadnet tells them the address
    it sees, and 127.0.0.1 means nothing to them)."""
    if not re.fullmatch(r'\d{1,3}(\.\d{1,3}){3}', value or ''):
        return False
    try:
        return not ipaddress.IPv4Address(value).is_loopback
    except ValueError:
        return False


def valid_name(value):
    return bool(re.fullmatch(r'[A-Za-z0-9_-]{3,16}', value or ''))


def valid_secret(value):
    return bool(re.fullmatch(r'[A-Za-z0-9_-]{1,32}', value or ''))


def valid_harder(value):
    return value == 'off' or bool(re.fullmatch(r'\d+(\.\d+)?,\d+(\.\d+)?,\d+(\.\d+)?', value or ''))


def missing(settings):
    """The answers still to give (or wrong), in the window's order."""
    checks = (('game', lambda v: bool(v)), ('host_ip', valid_ip), ('account', valid_name),
              ('password', valid_secret), ('party_secret', valid_secret))
    return [key for key, ok in checks if not ok(str(settings.get(key, '')))]


def settings_path(install):
    return Path(install) / 'user' / 'coop.json'


def from_settings_bat(path):
    """The answers of the old release's settings.bat (1 - SETUP), as coop.json keeps them."""
    values = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        match = re.fullmatch(r'set "([A-Z0-9_]+)=(.*)"', line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    _party_name, _, party_secret = values.get('PARTY', '').partition(':')
    settings = dict(DEFAULTS)
    settings.update({'game': values.get('GAME', ''),
                     'update': '' if values.get('UPDATE', 'none') == 'none' else values['UPDATE'],
                     'role': values.get('ROLE', 'guest') if values.get('ROLE') in ('host', 'guest') else 'guest',
                     'host_ip': values.get('HOSTIP', ''), 'account': values.get('NPID', ''),
                     'password': values.get('PASS', ''), 'party_secret': party_secret, 'p2p_port': values.get('P2PPORT', 'auto') or 'auto',
                     'harder': values.get('HARDER', 'off') or 'off'})
    return settings


def load_settings(install):
    """coop.json, else the old settings.bat beside BLauncher.exe, else the defaults."""
    path = settings_path(install)
    try:
        saved = json.loads(path.read_text(encoding='utf-8'))
        return {key: str(saved.get(key, default)) for key, default in DEFAULTS.items()}
    except (OSError, ValueError, AttributeError):
        pass
    bat = Path(install) / 'settings.bat'
    if bat.is_file():
        try:
            return from_settings_bat(bat)
        except OSError:
            pass
    return dict(DEFAULTS)


def save_settings(install, settings):
    path = settings_path(install)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({key: settings.get(key, default) for key, default in DEFAULTS.items()},
                               indent=2) + '\n', encoding='utf-8')


# --- the game ---------------------------------------------------------------------------------

def read_sfo(path):
    """A PS4 param.sfo's text and number values ({} when there is none)."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return {}
    if len(data) < 20 or data[:4] != b'\0PSF':
        return {}
    keys_at, data_at, count = struct.unpack_from('<III', data, 8)
    values = {}
    for index in range(min(count, 256)):
        entry = 20 + 16 * index
        if entry + 16 > len(data):
            break
        key_offset, fmt, length, _maximum, value_offset = struct.unpack_from('<HHIII', data, entry)
        key_start = keys_at + key_offset
        key_end = data.find(b'\0', key_start)
        key = data[key_start:key_end].decode('ascii', 'replace')
        at = data_at + value_offset
        if fmt == 0x0404:
            values[key] = struct.unpack_from('<I', data, at)[0] if at + 4 <= len(data) else 0
        else:
            values[key] = data[at:at + length].rstrip(b'\0').decode('utf-8', 'replace')
    return values


def game_version(folder):
    """(title id, app version) of a game folder, or None."""
    sfo = read_sfo(Path(folder) / 'sce_sys' / 'param.sfo')
    return (str(sfo.get('TITLE_ID', '')), str(sfo.get('APP_VER', ''))) if sfo else None


def is_game_folder(folder):
    return (Path(folder) / 'eboot.bin').is_file()


def is_bloodborne(folder):
    version = game_version(folder)
    return is_game_folder(folder) and (version is None or version[0] in TITLE_IDS or not version[0])


def find_game_folder(folder, depth=3):
    """The game folder (the one with eboot.bin) at or inside `folder`, not an update or mods one."""
    folder = Path(folder)
    if not folder.is_dir():
        return None
    if is_game_folder(folder):
        return folder
    if depth <= 0:
        return None
    try:
        children = sorted((entry for entry in folder.iterdir() if entry.is_dir()), key=lambda p: p.name.lower())
    except OSError:
        return None
    for child in children:
        if child.name.lower().endswith(('-patch', '-mods')):
            continue
        found = find_game_folder(child, depth - 1)
        if found:
            return found
    return None


def update_folder(game):
    """The 1.09 update dumped beside the game (CUSA03173-patch), if any."""
    game = Path(game)
    title = (game_version(game) or (game.name, ''))[0] or game.name
    for candidate in (game.parent / f'{game.name}-patch', game.parent / f'{title}-patch'):
        if is_game_folder(candidate):
            return candidate
    return None


# Folders never worth walking into while looking for the game.
SKIPPED = {'windows', '$recycle.bin', 'system volume information', 'programdata', 'recovery',
           'perflogs', 'msocache', 'node_modules', '.git', 'appdata', 'winsxs', 'config.msi',
           'coop-game', 'windowsapps', 'intel', 'amd', 'nvidia'}


def search_game(install, drives=None, homes=None, depth=5, budget_s=20.0, near=None):
    """A Bloodborne game folder on this PC: first next to `near` (a folder the player picked) and
    around the install, then in the usual places in the home folder, then each drive, breadth
    first to `depth` levels, for at most `budget_s` seconds. None when there is none."""
    install = Path(install)
    deadline = time.monotonic() + budget_s
    if homes is None:
        home = Path.home()
        homes = [home / name for name in ('Downloads', 'Desktop', 'Documents', 'Games')]
    if drives is None:
        drives = fixed_drives()
    first = []
    if near:
        near = Path(near)
        first += [near, near.parent]
    first += [install / 'game', install.parent, install.parent.parent]
    for folder in first:
        found = find_game_folder(folder, 2) if folder and Path(folder).is_dir() else None
        if found and is_bloodborne(found):
            return found
    queue = collections.deque((Path(root), 0) for root in [*homes, *drives])
    seen = set()
    while queue and time.monotonic() < deadline:
        folder, level = queue.popleft()
        key = str(folder).lower()
        if key in seen:
            continue
        seen.add(key)
        try:
            entries = list(os.scandir(folder))
        except OSError:
            continue
        names = {entry.name.lower() for entry in entries}
        if 'eboot.bin' in names and is_bloodborne(folder) and not folder.name.lower().endswith(('-patch', '-mods')):
            return folder
        if level >= depth:
            continue
        for entry in sorted(entries, key=lambda e: e.name.lower()):
            try:
                if not entry.is_dir(follow_symlinks=False) or entry.name.lower() in SKIPPED:
                    continue
            except OSError:
                continue
            if entry.name.startswith('.'):
                continue
            queue.append((Path(entry.path), level + 1))
    return None


def fixed_drives():
    """The PC's local drives (C:\\, D:\\ ...); network and removable ones are left out."""
    if sys.platform != 'win32':
        return [Path('/')]
    drives = []
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    for index in range(26):
        if mask & (1 << index):
            root = f'{chr(65 + index)}:\\'
            if ctypes.windll.kernel32.GetDriveTypeW(root) == 3:  # DRIVE_FIXED
                drives.append(Path(root))
    return drives


# --- the game the port runs: links to the player's game with the update over it ---------------

def is_junction(path):
    path = Path(path)
    try:
        return path.is_junction() if hasattr(path, 'is_junction') else bool(
            os.lstat(path).st_file_attributes & 0x400)
    except (OSError, AttributeError):
        return False


def make_junction(link, target):
    if sys.platform == 'win32':
        import _winapi
        _winapi.CreateJunction(str(target), str(link))
    else:
        os.symlink(target, link, target_is_directory=True)


def remove_view(path):
    """Removes a folder made of links: a link goes as a link, never what it points at."""
    path = Path(path)
    if is_junction(path) or path.is_symlink():
        os.rmdir(path) if is_junction(path) else path.unlink()
        return
    if not path.exists():
        return
    for entry in list(path.iterdir()):
        if is_junction(entry) or entry.is_symlink():
            os.rmdir(entry) if is_junction(entry) else entry.unlink()
        elif entry.is_dir():
            remove_view(entry)
        else:
            entry.unlink()
    path.rmdir()


def link_file(link, target):
    try:
        os.link(target, link)
    except OSError:  # another drive, or a file system without hard links
        shutil.copy2(target, link)


def build_game_view(game, update, view):
    """`view`: the game with `update` over it. A folder only one of them has is a link to it, a folder
    both have is merged, a file is the update's when it has one. The player's folders stay as
    they are."""
    view = Path(view)
    if view.exists() or is_junction(view):
        remove_view(view)
    _merge(Path(game), Path(update) if update else None, view)


def _merge(game, update, view):
    view.mkdir(parents=True, exist_ok=True)
    entries = {}
    for entry in game.iterdir():
        entries[entry.name.lower()] = [entry, None]
    if update and update.is_dir():
        for entry in update.iterdir():
            entries.setdefault(entry.name.lower(), [None, None])[1] = entry
    for own, newer in entries.values():
        if own and newer and own.is_dir() and newer.is_dir():
            _merge(own, newer, view / own.name)
            continue
        entry = newer or own
        if entry.is_dir():
            make_junction(view / entry.name, entry)
        else:
            link_file(view / entry.name, entry)


def view_root(install):
    """Where coop-game goes: beside BLauncher.exe, or under %LOCALAPPDATA% when that drive cannot
    hold folder links (exFAT, FAT32)."""
    here = Path(install) / 'coop-game'
    if junctions_work(here):
        return here
    return Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'BloodborneSeamlessCoop' / 'coop-game'


def junctions_work(folder):
    folder = Path(folder)
    probe = folder / '.junction-probe'
    try:
        folder.mkdir(parents=True, exist_ok=True)
        if is_junction(probe):
            os.rmdir(probe)
        make_junction(probe, folder)
        os.rmdir(probe)
        return True
    except OSError:
        return False


# --- the co-op event files ----------------------------------------------------------------------

def event_differences(made, reference):
    """The event files that differ from the ones this release was tested with (or that only one of
    the two has): every player in a party needs the same ones."""
    mine, tested = made.get('files', {}), reference.get('files', {})
    names = sorted(set(mine) | set(tested))
    return [name for name in names
            if name not in mine or name not in tested
            or mine[name].get('output_sha256') != tested[name].get('output_sha256')]


def script_command(install, script, *args):
    """BLauncher.exe --script (no Python needed), or Python from a source tree."""
    if getattr(sys, 'frozen', False):
        return [sys.executable, '--script', str(script), *map(str, args)]
    return [sys.executable, str(script), *map(str, args)]


def prepare(install, settings, say=print):
    """What 1 - SETUP did after its questions: the game view with the update over it, the co-op
    event files made from the player's own game (and compared with the tested ones), and the
    game server pointed at the host. Returns the coop-game folder; raises RuntimeError with a
    message for the player."""
    install = Path(install)
    game = find_game_folder(settings['game'])
    if not game:
        raise RuntimeError(f'No Bloodborne game folder (with eboot.bin) at {settings["game"]}.')
    title, version = game_version(game) or ('?', '?')
    update = None
    if version != GAME_VERSION:
        update = find_game_folder(settings['update'], 0) if settings.get('update') else update_folder(game)
        if not update:
            raise RuntimeError(f'Your game is version {version}; co-op needs the 1.09 update over it. '
                               'Put the dumped update folder next to the game (CUSA03173-patch) '
                               'or install the game and update from your .pkg files.')
    root = view_root(install)
    say('Preparing the game folder (links to your game, nothing is copied)…')
    view = root / 'CUSA03173'
    build_game_view(game, update, view)
    merged = game_version(view)
    if not merged or merged[1] != GAME_VERSION:
        raise RuntimeError(f'The game is version {merged[1] if merged else "?"} with the update over it; '
                           'co-op needs 01.09.')
    game_events = view / 'dvdroot_ps4' / 'event'
    if not (game_events / 'common.emevd.dcx').is_file():
        raise RuntimeError('No dvdroot_ps4\\event\\common.emevd.dcx in the game folder: is it a complete dump?')
    say('Making the co-op event files from your game…')
    events = install / 'event-patch'
    if events.exists():
        shutil.rmtree(events)
    log = install / 'user' / 'log' / 'event-patch.txt'
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, 'w', encoding='utf-8') as out:
        code = subprocess.call(script_command(install, install / 'tools' / 'party_boss_events.py', events, game_events),
                               stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                               creationflags=NO_WINDOW, cwd=install)
    manifest = events / 'manifest.json'
    if code != 0 or not manifest.is_file():
        raise RuntimeError(f'Making the co-op event files failed (log: {log}).')
    note = ''
    reference = install / 'event-reference.json'
    if reference.is_file():
        differ = event_differences(json.loads(manifest.read_text(encoding='utf-8')),
                                   json.loads(reference.read_text(encoding='utf-8')))
        if differ:
            note = ('Your event files differ from the tested ones (' + ', '.join(differ) + '): another game '
                    'version or edition. Co-op may still work if every player has the same files.')
    mods = root / 'CUSA03173-mods'
    if mods.exists():
        remove_view(mods)
    copy_events(install, root)
    if title != TESTED_TITLE:
        note = (note + ' ' if note else '') + (f'This is {title}; co-op is tested on {TESTED_TITLE}. Every player '
                                               'in a party must use the same edition.')
    return root, note


def copy_events(install, root):
    """The co-op event files into the mods folder the port reads beside the game (every start, as
    2 - PLAY did: a newer release brings newer ones)."""
    target = Path(root) / 'CUSA03173-mods' / 'dvdroot_ps4' / 'event'
    target.mkdir(parents=True, exist_ok=True)
    for file in (Path(install) / 'event-patch').glob('*'):
        if file.is_file():
            shutil.copy2(file, target / file.name)


def prepared_key(settings):
    """What the prepared game folder was made from: preparing again when it changes."""
    return json.dumps([settings.get('game', ''), settings.get('update', '')])


def needs_prepare(install, settings):
    root = view_root(install)
    return (settings.get('prepared') != prepared_key(settings) or not is_game_folder(root / 'CUSA03173')
            or not (Path(install) / 'event-patch' / 'manifest.json').is_file())


def host_overrides(host_ip):
    """user\\host_overrides.custom.json: the game's own server is the host's backend."""
    return json.dumps({GAME_SERVER: f'http://{host_ip}:{BACKEND_PORT}'}, indent=2) + '\n'


def import_saves(install, source):
    """Copies a character's saves into user\\savedata\\1 (a shadPS4 'user' folder, an old co-op
    folder, or a savedata folder holding CUSA... folders). Returns the folder copied from, or None."""
    source = Path(source)
    for candidate in (source / 'home' / '1000' / 'savedata', source / 'user' / 'savedata' / '1',
                      source / 'savedata' / '1', source):
        if candidate.is_dir() and any(child.is_dir() and child.name.upper().startswith('CUSA')
                                      for child in candidate.iterdir()):
            target = Path(install) / 'user' / 'savedata' / '1'
            target.mkdir(parents=True, exist_ok=True)
            shutil.copytree(candidate, target, dirs_exist_ok=True)
            return candidate
    return None


# --- playing ------------------------------------------------------------------------------------

def game_environment(env, settings, install, root):
    """The game's environment for co-op, as 2 - PLAY set it, over the launcher's own."""
    install, root = Path(install), Path(root)
    env = dict(env)
    host = settings['host_ip']
    env.update({
        'BB_DATA_DIR': str(install), 'BB_USER_DIR': str(install / 'user'),
        'BB_GAME_DIR': str(root / 'CUSA03173'), 'BB_FPS': '60',
        'BB_LOG_FILE': str(install / 'user' / 'log' / 'shad_log.txt'), 'BB_LOG_LEVEL': 'info',
        'BB_ONLINE': '1', 'BB_UPNP': '0',
        'BB_SHADNET_SERVER': f'{host}:{SHADNET_PORT}', 'BB_SHADNET_WEBAPI': f'http://{host}:{WEBAPI_PORT}',
        'BB_SHADNET_NPID': settings['account'], 'BB_SHADNET_PASSWORD': settings['password'],
        'BB_USER_NAME': settings['account'],
        'SHADPS4_BLOODBORNE_SEAMLESS_COOP': '1',
        'SHADPS4_BLOODBORNE_PARTY': f'{PARTY_NAME}:{settings["party_secret"]}',
        'SHADPS4_BLOODBORNE_PARTY_ROLE': settings['role'],
        'SHADPS4_BLOODBORNE_RE_TRACE': '0',
    })
    # Co-op is the fixed 60 FPS mode for everyone (a host above 60 made the guests see everyone
    # freeze and fly): no frame cap from the launcher.
    env.pop('BB_FPS_LIMIT', None)
    for key in ('SHADPS4_P2P_PORT', 'SHADPS4_BLOODBORNE_HARDER_ENEMIES', 'SHADPS4_BLOODBORNE_PARTY_DEATH',
                'BB_COMMUNITY_SERVER'):
        env.pop(key, None)
    port = str(settings.get('p2p_port', 'auto'))
    if port not in ('auto', '', '51610', '51611'):
        env['SHADPS4_P2P_PORT'] = port
    harder = str(settings.get('harder', 'off'))
    if harder != 'off' and valid_harder(harder):
        env['SHADPS4_BLOODBORNE_HARDER_ENEMIES'] = harder
    return env


def rotate_logs(install):
    """shad_log.txt and console.txt become the .prev ones, as 2 - PLAY kept them."""
    log = Path(install) / 'user' / 'log'
    log.mkdir(parents=True, exist_ok=True)
    for name in ('shad_log', 'console'):
        if (log / f'{name}.txt').is_file():
            shutil.copy2(log / f'{name}.txt', log / f'{name}.prev.txt')
    return log / 'console.txt'


def register_command(install, host_ip, account, password):
    """Registers the account on the host's server (guests register themselves: no host step)."""
    return [str(Path(install) / 'server' / 'clientsample' / 'shadnet-sample.exe'), host_ip, str(SHADNET_PORT),
            'register', account, password, f'{account}.{password}@p2p.db']


def registered(output, code):
    """Whether the account exists on the server after a register: made now, or made before."""
    return code == 0 and ('Account created' in output or 'CreationExistingUsername' in output)


def register(install, host_ip, account, password, timeout=15):
    """(ok, the tool's output)."""
    try:
        result = subprocess.run(register_command(install, host_ip, account, password), capture_output=True,
                                text=True, timeout=timeout, creationflags=NO_WINDOW, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, str(error)
    output = (result.stdout or '') + (result.stderr or '')
    return registered(output, result.returncode), output


# --- the host's servers -------------------------------------------------------------------------

def port_open(port, host='127.0.0.1', timeout=0.3):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def servers_running():
    return port_open(SHADNET_PORT)


def start_servers(install, host_ip, wait_s=10.0):
    """HOST - Start Servers: shadnet (accounts, matchmaking, the party boards; it answers the game's
    backend port itself), and the Python backend only when nothing answers that port. Returns the
    lines to show."""
    install = Path(install)
    server = install / 'server'
    log = install / 'user' / 'log'
    log.mkdir(parents=True, exist_ok=True)
    said = []
    if not servers_running():
        env = dict(os.environ, SHADNET_BACKEND_ADDRESS=host_ip)
        with open(log / 'shadnet.txt', 'a', encoding='utf-8') as out:
            subprocess.Popen([str(server / 'shadnet.exe')], cwd=server, env=env, stdout=out, stderr=subprocess.STDOUT,
                             stdin=subprocess.DEVNULL, creationflags=NO_WINDOW | DETACHED)
        said.append('Started the co-op server.')
    deadline = time.monotonic() + wait_s
    while time.monotonic() < deadline and not (servers_running() and port_open(BACKEND_PORT)):
        time.sleep(0.25)
    if not port_open(BACKEND_PORT):
        backend = install / 'backend'
        with open(log / 'backend.txt', 'a', encoding='utf-8') as out:
            subprocess.Popen(script_command(install, backend / 'bb_dummy_server.py', '--address', host_ip, '--port',
                                            BACKEND_PORT, '--shadnet-webapi', f'http://{host_ip}:{WEBAPI_PORT}'),
                             cwd=backend, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             creationflags=NO_WINDOW | DETACHED)
        said.append('Started the backend.')
    if not servers_running():
        said.append(f'The co-op server did not start (log: {log / "shadnet.txt"}).')
    return said


def our_processes(install):
    """(pid, name) of this install's shadnet and backend."""
    install = str(Path(install).resolve()).lower()
    script = ("Get-CimInstance Win32_Process -Filter \"Name='shadnet.exe' OR Name='BLauncher.exe' OR "
              "Name='python.exe' OR Name='pythonw.exe'\" | ForEach-Object { \"$($_.ProcessId)|$($_.Name)|"
              "$($_.ExecutablePath)|$($_.CommandLine)\" }")
    try:
        out = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, text=True,
                             timeout=20, creationflags=NO_WINDOW).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    found = []
    for line in out.splitlines():
        parts = line.split('|', 3)
        if len(parts) < 4:
            continue
        pid, name, path, command = parts
        if name.lower() == 'shadnet.exe' and path.lower().startswith(install):
            found.append((int(pid), name))
        elif 'bb_dummy_server.py' in command and install in command.lower():
            found.append((int(pid), name))
    return found


def stop_servers(install):
    stopped = 0
    for pid, _name in our_processes(install):
        subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True, creationflags=NO_WINDOW)
        stopped += 1
    return stopped


# --- Windows Firewall ---------------------------------------------------------------------------

RULE_GAME, RULE_HOST_TCP, RULE_HOST_UDP = 'BB coop port', 'BB coop host TCP', 'BB coop host UDP'


def firewall_commands(install, host):
    """The netsh lines that let the game (and for the host the servers) through Windows Firewall."""
    probe = Path(install) / 'bin' / 'bb-probe.exe'
    lines = [f'netsh advfirewall firewall delete rule name="{RULE_GAME}" & '
             f'netsh advfirewall firewall add rule name="{RULE_GAME}" dir=in action=allow '
             f'program="{probe}" enable=yes profile=any']
    if host:
        lines.append(f'netsh advfirewall firewall delete rule name="{RULE_HOST_TCP}" & '
                     f'netsh advfirewall firewall add rule name="{RULE_HOST_TCP}" dir=in action=allow protocol=TCP '
                     f'localport={SHADNET_PORT},{WEBAPI_PORT},{BACKEND_PORT} enable=yes profile=any')
        lines.append(f'netsh advfirewall firewall delete rule name="{RULE_HOST_UDP}" & '
                     f'netsh advfirewall firewall add rule name="{RULE_HOST_UDP}" dir=in action=allow protocol=UDP '
                     f'localport={MATCHING_PORT},51610-51611 enable=yes profile=any')
    return lines


def firewall_ready(install, host):
    """Whether this install's rules are there (reading rules needs no administrator)."""
    probe = str(Path(install) / 'bin' / 'bb-probe.exe').lower()
    names = [RULE_GAME] + ([RULE_HOST_TCP, RULE_HOST_UDP] if host else [])
    for name in names:
        try:
            out = subprocess.run(['netsh', 'advfirewall', 'firewall', 'show', 'rule', f'name={name}', 'verbose'],
                                 capture_output=True, text=True, timeout=15, creationflags=NO_WINDOW)
        except (OSError, subprocess.TimeoutExpired):
            return False
        if out.returncode != 0 or (name == RULE_GAME and probe not in out.stdout.lower()):
            return False
    return True


def allow_through_firewall(install, host):
    """Asks Windows for the administrator once (the UAC prompt) and adds the rules. True when the
    player said yes (the rules are then checked again by the caller)."""
    command = ' & '.join(firewall_commands(install, host))
    result = ctypes.windll.shell32.ShellExecuteW(None, 'runas', 'cmd.exe', f'/c {command}', None, 0)
    return result > 32
