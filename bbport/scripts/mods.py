#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Merge loose-file mods through links, preserving the original game and mod files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import time

# Top-level folders of the game's dvdroot_ps4: a mod made of these is a dvdroot_ps4 itself (adhoc:
# the debug menu's fonts, which the retail game does not ship).
GAME_FOLDERS = {'action', 'adhoc', 'chr', 'event', 'facegen', 'font', 'map', 'menu', 'movie', 'msg', 'mtd',
                'obj', 'other', 'param', 'paramdef', 'parts', 'remo', 'script', 'sfx', 'shader',
                'sound'}
# The retail releases' folder names (game_check.py SUPPORTED_TITLES), as a mod's wrapper folder.
SERIALS = ('CUSA03173', 'CUSA00900', 'CUSA00207', 'CUSA00208', 'CUSA00299', 'CUSA01363',
           'CUSA03179', 'CUSA03014', 'CUSA03023')


def child(folder, name):
    """`name` inside `folder`, matching an existing entry case-insensitively (mods made on
    Windows often differ from the game's lower-case names)."""
    folder = Path(folder)
    for entry in (folder.iterdir() if folder.is_dir() else ()):
        if entry.name.casefold() == name.casefold():
            return entry
    return folder / name


# Files that come with a mod without being part of it: notes, previews, links, licences.
EXTRA_SUFFIXES = {'', '.txt', '.md', '.ini', '.json', '.pdf', '.rtf', '.nfo', '.url', '.htm', '.html',
                  '.jpg', '.jpeg', '.png', '.webp', '.gif', '.bmp'}
ARCHIVE_SUFFIXES = {'.zip', '.7z', '.rar'}


def content_root(folder, depth=0):
    """(root, prefix): where a mod's files are and the game path they go to, or None.

    Accepted layouts: <mod>/dvdroot_ps4, <mod>/app0/dvdroot_ps4, <mod>/<serial>/dvdroot_ps4
    (CUSA03173, CUSA00900, ...), up to three wrapper folders around any of these (archives
    extracted into a folder of their name, beside notes and pictures), and the game's folders
    without dvdroot_ps4 (<mod>/chr, <mod>/parts, ...), also next to other folders (Screenshots,
    Optional)."""
    folder = Path(folder)
    if not folder.is_dir():
        return None
    for wrapper in ('', 'app0', *SERIALS):
        base = child(folder, wrapper) if wrapper else folder
        dvdroot = child(base, 'dvdroot_ps4')
        if dvdroot.is_dir():
            return base, ''
    entries = [e for e in folder.iterdir() if not e.name.startswith('.')]
    folders = [e for e in entries if e.is_dir()]
    if any(e.name.casefold() in GAME_FOLDERS for e in folders):
        return folder, 'dvdroot_ps4'
    if depth < 3 and len(folders) == 1 and not [e for e in entries if e.is_file() and
                                                e.suffix.casefold() not in EXTRA_SUFFIXES]:
        return content_root(folders[0], depth + 1)
    return None


def problem(folder):
    """Why `folder` is not a mod (content_root is None), for the launcher's list."""
    folder = Path(folder)
    files = [e for e in folder.rglob('*') if e.is_file()][:2000]
    if not files:
        return 'empty'
    if any(e.suffix.casefold() in ARCHIVE_SUFFIXES for e in files):
        return 'still an archive: extract it here'
    folders = [e for e in folder.iterdir() if e.is_dir() and not e.name.startswith('.')]
    if len(folders) > 1:
        return ('several folders: put each mod in its own folder in Mods (each with dvdroot_ps4, '
                'or folders like chr, parts, map)')
    return 'no game files: expected dvdroot_ps4, or folders like chr, parts, map'


def unrecognized(root):
    """(name, problem) of the folders of `root` that are not mods."""
    root = Path(root)
    if not root.is_dir():
        return []
    return sorted(((p.name, problem(p)) for p in root.iterdir()
                   if p.is_dir() and not p.name.startswith('.') and not content_root(p)),
                  key=lambda item: (item[0].casefold(), item[0]))


def discover(root):
    """Named mods: folders of `root` with an accepted layout (content_root)."""
    root = Path(root)
    if not root.is_dir():
        return []
    return sorted((p.name for p in root.iterdir() if p.is_dir() and content_root(p)),
                  key=lambda name: (name.casefold(), name))


def selected(root, config):
    available = discover(root)
    if not config or not Path(config).is_file():
        return available
    settings = json.loads(Path(config).read_text(encoding='utf-8-sig'))
    disabled_names = settings.get('disabled', [])
    if not isinstance(disabled_names, list) or not all(isinstance(n, str) for n in disabled_names):
        raise ValueError('Disabled mods must be a list of folder names')
    disabled = set(disabled_names)
    order = settings.get('order', [])
    if not isinstance(order, list) or not all(isinstance(n, str) for n in order):
        raise ValueError('Mod order must be a list of folder names')
    # New folders are enabled automatically and appended in alphabetical order.
    return list(dict.fromkeys(n for n in [*order, *available]
                             if n in available and n not in disabled))


def mod_files(folder):
    """(game path, source) of every file a mod replaces or adds under dvdroot_ps4."""
    layout = content_root(folder)
    if not layout:
        raise ValueError(f'{folder}: expected dvdroot_ps4 (or chr/, parts/, ...) inside the mod folder')
    root, prefix = layout
    root = root.resolve()
    for directory, folders, files in os.walk(root, followlinks=False):
        directory = Path(directory)
        for name in [*folders, *files]:
            if (directory / name).is_symlink():
                raise ValueError(f'Mod symlinks are unsupported: {directory / name}')
        for name in sorted(files):
            source = directory / name
            if prefix and source.relative_to(root).parts[0].casefold() not in GAME_FOLDERS:
                continue  # notes and folders beside the game's (Screenshots, Optional)
            relative = Path(prefix) / source.relative_to(root) if prefix else source.relative_to(root)
            # Readme/metadata stay outside the mounted game. This loader handles assets;
            # executable patches use the existing patch compiler, with address validation.
            if relative.parts[0].casefold() != 'dvdroot_ps4':
                if relative.parts[0].casefold() in ('eboot.bin', 'sce_module', 'sce_sys'):
                    raise ValueError(f'Executable/system replacement is unsupported: {source}')
                continue
            if not source.is_file():
                raise ValueError(f'Not a regular mod file: {source}')
            yield relative, source


# On Windows the overlay never holds a symlink, whether or not the user may create them (Developer
# Mode): the game panics reading files through symlinks (Dantelion2 FileTransferTask.cpp(865), every
# boot, with any loose-file mod; upstream #102). Directories become junctions, files hard links
# (copies across volumes). Those are not symlinks, so the overlay remembers what it linked and
# where to. Everything else uses symlinks.
LINKED = {}


def make_link(link, target):
    if os.name != 'nt':
        link.symlink_to(target, target_is_directory=target.is_dir())
        return
    if target.is_dir():
        import _winapi
        _winapi.CreateJunction(str(target), str(link))
    else:
        try:
            os.link(target, link)
        except OSError:
            shutil.copy2(target, link)
    LINKED[str(link)] = target


def is_link(path):
    return path.is_symlink() or str(path) in LINKED or (hasattr(path, 'is_junction') and path.is_junction())


def link_target(path):
    return LINKED.get(str(path)) or path.resolve(strict=True)


def remove_link(path):
    if hasattr(path, 'is_junction') and path.is_junction():
        os.rmdir(path)
    else:
        unlink_file(path, LINKED.get(str(path)))
    LINKED.pop(str(path), None)


def unlink_file(path, linked=None):
    """Windows refuses to delete a read-only file (WinError 5), and a hard link shares the attribute with the file
    it links: a game copied from a read-only source keeps it. The attribute is cleared for the deletion and set
    again on `linked`, the file that stays. From Supermedo's bbport (ba34a3a, emurphy04's PR #77)."""
    try:
        path.unlink()
        return
    except PermissionError:
        mode = path.stat().st_mode
        if os.name != 'nt' or mode & stat.S_IWRITE:
            raise
    os.chmod(path, mode | stat.S_IWRITE)
    try:
        path.unlink()
    finally:
        rest = path if path.exists() else linked
        if rest is not None and rest.exists():
            os.chmod(rest, mode)


def remove_overlay(overlay, game=None):
    """shutil.rmtree for an overlay, whose read-only links are deleted as remove_link does (`game`: the game
    folder it mirrors, for links made by another process, such as run.py removing the view after the game)."""
    overlay = Path(overlay)

    def retry(function, path, error):
        path = Path(path)
        if function not in (os.unlink, os.remove) or not path.is_file():
            raise error[1] if isinstance(error, tuple) else error
        linked = LINKED.get(str(path))
        if linked is None and game is not None:
            mirror = Path(game) / path.relative_to(overlay)
            if mirror.is_file() and os.path.samefile(path, mirror):
                linked = mirror
        unlink_file(path, linked)
    if sys.version_info >= (3, 12):
        shutil.rmtree(overlay, onexc=retry)
    else:
        shutil.rmtree(overlay, onerror=retry)


def expand(directory):
    """Materialize one directory level; never write through a directory link."""
    if is_link(directory):
        target = link_target(directory)
        if not target.is_dir():
            raise ValueError(f'File/directory conflict at {directory.name}')
        remove_link(directory)
        directory.mkdir()
        for entry in target.iterdir():
            make_link(directory / entry.name, entry)
    elif directory.exists() and not directory.is_dir():
        raise ValueError(f'File/directory conflict at {directory.name}')
    else:
        directory.mkdir(exist_ok=True)


GB = 1024 ** 3


def beside_game(game, out):
    """Hard links only reach files on the same volume: with the game on another drive than out,
    the overlay goes beside the game."""
    return os.name == 'nt' and out.drive.casefold() != game.drive.casefold()


def copy_bytes(game, replacements):
    """Bytes an overlay copies when no file can be hard-linked (the game and out on different
    volumes): the game's files at the top and in each folder a mod reaches (the other folders
    become junctions, which cross volumes), plus the mod files. A game file a mod replaces is
    copied first and removed after, so it counts."""
    reached = {()}
    for relative, _ in replacements:
        parts = [part.casefold() for part in relative.parts[:-1]]
        reached.update(tuple(parts[:depth]) for depth in range(1, len(parts) + 1))
    total = sum(source.stat().st_size for _, source in replacements)
    pending = [(game, ())]
    while pending:
        directory, key = pending.pop()
        for entry in directory.iterdir():
            if entry.is_dir():
                if key + (entry.name.casefold(),) in reached:
                    pending.append((entry, key + (entry.name.casefold(),)))
            else:
                total += entry.stat().st_size
    return total


OWNER_SUFFIX = '.owner'
# A view from before owner files: removed once this old (no launch runs that long).
UNOWNED_AGE = 12 * 3600


def process_alive(pid):
    """True unless the process `pid` is known to be gone (a process we may not query counts as alive)."""
    if os.name == 'nt':
        import ctypes
        kernel32 = ctypes.windll.kernel32
        PROCESS_QUERY_LIMITED_INFORMATION, STILL_ACTIVE, ERROR_ACCESS_DENIED = 0x1000, 259, 5
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return kernel32.GetLastError() == ERROR_ACCESS_DENIED
        try:
            code = ctypes.c_ulong()
            return not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value == STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except OSError:
        return True
    return True


def stale(view):
    """True when no launch uses `view` any more: its owner process is gone, or it has no owner file and is old."""
    owner = Path(str(view) + OWNER_SUFFIX)
    try:
        return not process_alive(int(owner.read_text(encoding='utf-8').strip()))
    except (OSError, ValueError):
        pass
    try:
        return time.time() - view.stat().st_mtime > UNOWNED_AGE
    except OSError:
        return False


def remove_stale_views(folder, game):
    """Removes the views in `folder` that a killed launch left behind (it never removed its own), and owner files
    whose view is gone. A view still in use cannot be removed completely; it is left for a later launch."""
    folder = Path(folder)
    if not folder.is_dir():
        return
    for entry in folder.glob('mod-game-*'):
        if entry.name.endswith(OWNER_SUFFIX):
            if not Path(str(entry)[:-len(OWNER_SUFFIX)]).exists():
                entry.unlink(missing_ok=True)
        elif entry.is_dir() and stale(entry):
            try:
                remove_overlay(entry, game)
                Path(str(entry) + OWNER_SUFFIX).unlink(missing_ok=True)
                print(f'Mods: removed {entry.name}, left by a launch that has ended', file=sys.stderr)
            except OSError as error:
                print(f'Mods: could not remove {entry} yet: {error}', file=sys.stderr)


def view_owner():
    """The launch a new view is for: BB_VIEW_OWNER (run.py and run.sh set their own process id, which lasts for the
    whole game), else this process's parent."""
    try:
        return int(os.environ['BB_VIEW_OWNER'])
    except (KeyError, ValueError):
        return os.getppid()


def new_view(folder, game, owner):
    remove_stale_views(folder, game)
    overlay = Path(tempfile.mkdtemp(prefix='mod-game-', dir=folder))
    Path(str(overlay) + OWNER_SUFFIX).write_text(f'{owner}\n', encoding='utf-8')
    return overlay


def build_overlay(game, out, mods, beside=True, owner=None):
    """beside: with the game on another drive than out, the overlay goes beside the game (beside_game); False
    keeps it in out (files are then copied, folders linked). owner: the process the view is for (the launch that
    removes it when the game ends; default view_owner()); views of owners that are gone are removed."""
    owner = view_owner() if owner is None else owner
    game = Path(game).resolve(strict=True)
    replacements = []
    owners = {}
    for name, root in mods:
        count = 0
        for relative, source in mod_files(root):
            key = relative.as_posix().casefold()
            if key in owners:
                print(f'Mods: {relative}: {owners[key]} -> {name}', file=sys.stderr)
            owners[key] = name
            replacements.append((relative, source))
            count += 1
        print(f'Mods: {name}: {count} files', file=sys.stderr)
    if not replacements:
        return game
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    overlay = None
    if beside and beside_game(game, out):
        try:
            overlay = new_view(game.parent, game, owner)
        except OSError as error:
            # A read-only or access-restricted library: build on out's volume instead. Files that
            # cannot be hard-linked across volumes are copied (never symlinked, upstream #102),
            # for every launch: the overlay is private to a launch and removed when it ends.
            need, free = copy_bytes(game, replacements), shutil.disk_usage(out).free
            if need > free:
                raise ValueError(
                    f'cannot write to {game.parent} ({error.strerror or error}), so the overlay would be built '
                    f'in {out}, where hard links cannot reach the game: it needs a copy of {need / GB:.1f} GB '
                    f'and only {free / GB:.1f} GB are free there. Either make {game.parent} writable, '
                    f'or put the game on the same drive as {out}.') from None
            print(f'Mods: cannot write to {game.parent} ({error.strerror or error}); '
                  f'building the overlay in {out}, this build copies {need / GB:.1f} GB', file=sys.stderr)
    if overlay is None:
        overlay = new_view(out, game, owner)
    try:
        for entry in game.iterdir():
            make_link(overlay / entry.name, entry)
        replaced = added = 0
        for relative, source in replacements:
            # Each component takes the game's spelling when it exists in another case.
            parent = overlay
            for part in relative.parts[:-1]:
                parent = child(parent, part)
                expand(parent)
            destination = child(parent, relative.parts[-1])
            if is_link(destination):
                replaced += link_target(destination).is_relative_to(game)
                remove_link(destination)
            elif destination.exists():
                raise ValueError(f'File/directory conflict: {relative}')
            else:
                added += 1
            make_link(destination, source)
        print(f'Mods: {replaced} game files replaced, {added} added', file=sys.stderr)
        return overlay
    except BaseException:
        remove_overlay(overlay)
        Path(str(overlay) + OWNER_SUFFIX).unlink(missing_ok=True)
        raise


# The file the button icons replace (the launcher generates it from the player's own copy).
ICONS_FILE = 'dvdroot_ps4/menu/common.tpf.dcx'
ICONS_LAYER = 'button icons'


def user_layers(game, mods_dir, config=None, enabled='1'):
    """[(name, folder)] of the player's mods, lowest priority first (none when mods are switched off)."""
    layers = []
    if enabled == '1':
        # shadPS4's loose overlay convention, beside the original game.
        legacy = Path(str(Path(game).resolve()) + '-mods')
        if legacy.is_dir():
            layers.append((legacy.name, legacy))
        # A directly selected loose overlay is also accepted.
        if child(mods_dir, 'dvdroot_ps4').is_dir():
            layers.append((Path(mods_dir).name, Path(mods_dir)))
        else:
            for name in selected(mods_dir, config):
                layers.append((name, Path(mods_dir) / name))
    return layers


def supplier(layers, relative):
    """The name of the last layer that has the game file `relative` (any letter case), or None."""
    found = None
    for name, root in layers:
        if any(path.as_posix().casefold() == relative.casefold() for path, _ in mod_files(root)):
            found = name
    return found


def icons_note(layers):
    """Why the button icons cannot be used with these mod layers (a mod has its own menu/common.tpf.dcx, and the
    player's file wins), or None."""
    owner = supplier(layers, ICONS_FILE)
    return f'Button icons are off for this run: {owner} has its own menu/common.tpf.dcx' if owner else None


def mod_layers(game, mods_dir, config=None, enabled='1', icons=None, port_layers=()):
    """([(name, folder)], note): the layers of an overlay, lowest priority first. The port's own layers
    (`port_layers`, such as the field of view's camera table) go first, then the button icons, so that any mod
    replaces them; a mod that has the same file as the icons takes them out, and the note says so (None
    otherwise)."""
    own = [(Path(layer).name, Path(layer)) for layer in port_layers if Path(layer).is_dir()]
    layers = user_layers(game, mods_dir, config, enabled)
    if not icons:
        return [*own, *layers], None
    note = icons_note(layers)
    return ([*own, *layers], note) if note else ([*own, (ICONS_LAYER, Path(icons)), *layers], None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--mods-dir', required=True, type=Path)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--enabled', choices=('0', '1'), default='1')
    parser.add_argument('--icons-layer', type=Path, help='a folder with the generated button icons (a layer under the mods)')
    parser.add_argument('--layer', type=Path, action='append', default=[],
                        help="a folder of the port's own (fov.py's camera table): a layer under the icons and the mods")
    parser.add_argument('--party-hud', action='store_true',
                        help='put the party HUD (hud_patch.py: the HUD the layers show, patched) over the mods')
    args = parser.parse_args()
    layers, note = mod_layers(args.game, args.mods_dir, args.config, args.enabled, args.icons_layer, args.layer)
    if note:
        print(f'Mods: {note}', file=sys.stderr)
    if args.party_hud:
        import hud_patch
        layers, notes = hud_patch.with_party_hud(args.game, layers, args.out)
        for line in notes:
            print(f'Mods: {line}', file=sys.stderr)
    try:
        print(build_overlay(args.game, args.out, layers))
    except OSError as error:
        # [WinError 1] "Incorrect function": without symlink rights a folder becomes a junction, and junctions
        # exist only on NTFS. A port on an exFAT or FAT32 drive (USB, external) gets its game view in the system
        # drive's temporary folder instead, which is NTFS. From Supermedo's bbport (bf0878b).
        if os.name != 'nt' or getattr(error, 'winerror', None) != 1:
            raise
        fallback = Path(tempfile.gettempdir()) / 'bbport-game-view'
        fallback.mkdir(parents=True, exist_ok=True)
        print(f'Mods: links are not supported on the drive of {args.out}; '
              f'the game view is made in {fallback}', file=sys.stderr)
        print(build_overlay(args.game, fallback, layers, beside=False))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TypeError) as error:
        print(f'Mods: {error}', file=sys.stderr)
        sys.exit(1)
