from paths import ROOT
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('bbmods', ROOT / 'scripts/mods.py')
mods = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mods)


class ModTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        self.assets = self.game / 'dvdroot_ps4' / 'chr'
        self.assets.mkdir(parents=True)
        (self.game / 'eboot.bin').write_bytes(b'original executable')
        (self.assets / 'a.dcx').write_bytes(b'original')
        (self.assets / 'b.dcx').write_bytes(b'untouched')
        self.moddir = self.root / 'mods'

    def mod(self, name, contents=b'mod', file='a.dcx'):
        root = self.moddir / name
        folder = root / 'dvdroot_ps4' / 'chr'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / file).write_bytes(contents)
        return root

    def test_merge_replacement_new_file_and_directory_listing(self):
        a = self.mod('A')
        self.mod('A', b'new', 'new.dcx')
        result = mods.build_overlay(self.game, self.root / 'out', [('A', a)])
        folder = result / 'dvdroot_ps4' / 'chr'
        self.assertEqual((folder / 'a.dcx').read_bytes(), b'mod')
        self.assertEqual((folder / 'b.dcx').read_bytes(), b'untouched')
        self.assertEqual((folder / 'new.dcx').read_bytes(), b'new')
        self.assertEqual({p.name for p in folder.iterdir()}, {'a.dcx', 'b.dcx', 'new.dcx'})
        self.assertEqual((self.assets / 'a.dcx').read_bytes(), b'original')
        self.assertFalse((self.assets / 'new.dcx').exists())
        self.assertEqual((result / 'eboot.bin').read_bytes(), b'original executable')

    def test_last_mod_wins_and_reordering_changes_winner(self):
        a, b = self.mod('A', b'A'), self.mod('B', b'B')
        for layers, winner in [([('A', a), ('B', b)], b'B'), ([('B', b), ('A', a)], b'A')]:
            result = mods.build_overlay(self.game, self.root / 'out', layers)
            self.assertEqual((result / 'dvdroot_ps4/chr/a.dcx').read_bytes(), winner)

    def test_selection_disable_and_new_mod(self):
        for name in ['C', 'B', 'A']:
            self.mod(name)
        config = self.root / 'mods.json'
        config.write_text(json.dumps({'order': ['B', 'A', 'deleted'], 'disabled': ['A']}))
        self.assertEqual(mods.selected(self.moddir, config), ['B', 'C'])

    def test_no_mod_returns_original_game(self):
        self.assertEqual(mods.build_overlay(self.game, self.root / 'out', []), self.game)
        self.assertFalse((self.root / 'out').exists())

    def test_directory_conflict_does_not_modify_base(self):
        a = self.mod('A')
        (a / 'dvdroot_ps4/chr/a.dcx').unlink()
        nested = a / 'dvdroot_ps4/chr/b.dcx'
        nested.mkdir()
        (nested / 'child').write_bytes(b'bad')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])
        self.assertEqual((self.assets / 'b.dcx').read_bytes(), b'untouched')
        self.assertEqual(list((self.root / 'out').iterdir()), [])

    def test_mod_symlinks_rejected(self):
        a = self.mod('A')
        try:
            (a / 'dvdroot_ps4/escape').symlink_to(self.assets, target_is_directory=True)
        except OSError as error:  # Windows without Developer Mode: no symlinks to test with
            self.skipTest(f'cannot create a symlink: {error}')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])

    def test_executable_replacement_rejected(self):
        a = self.mod('A')
        (a / 'eboot.bin').write_bytes(b'unsupported')
        with self.assertRaises(ValueError):
            mods.build_overlay(self.game, self.root / 'out', [('A', a)])

    def test_shadps4_sibling_folder_and_global_disable(self):
        legacy = Path(str(self.game) + '-mods')
        folder = legacy / 'dvdroot_ps4/chr'
        folder.mkdir(parents=True)
        (folder / 'a.dcx').write_bytes(b'legacy')
        for enabled, expected in [('1', b'legacy'), ('0', b'original')]:
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/mods.py'), str(self.game),
                '--out', str(self.root / 'out'), '--mods-dir', str(self.moddir), '--enabled', enabled],
                capture_output=True, text=True, check=True)
            self.assertEqual((Path(result.stdout.strip()) / 'dvdroot_ps4/chr/a.dcx').read_bytes(), expected)

    def test_app0_wrapped_mod_discovered(self):
        self.moddir.mkdir()
        (self.moddir / 'Wrapped/app0/dvdroot_ps4').mkdir(parents=True)
        self.assertEqual(mods.discover(self.moddir), ['Wrapped'])

    def test_other_case_replaces_the_game_file(self):
        root = self.moddir / 'Upper'
        (root / 'DVDROOT_PS4/Chr').mkdir(parents=True)
        (root / 'DVDROOT_PS4/Chr/A.DCX').write_bytes(b'upper')
        result = mods.build_overlay(self.game, self.root / 'out', [('Upper', root)])
        folder = result / 'dvdroot_ps4/chr'
        self.assertEqual((folder / 'a.dcx').read_bytes(), b'upper')
        self.assertEqual({p.name for p in folder.iterdir()}, {'a.dcx', 'b.dcx'})
        self.assertEqual({p.name for p in result.iterdir()}, {'dvdroot_ps4', 'eboot.bin'})

    def test_wrapped_and_bare_layouts(self):
        layouts = {'Title': 'CUSA03173/dvdroot_ps4/chr', 'Archive': 'Archive v1.2/dvdroot_ps4/chr',
                   'Bare': 'chr', 'Nested': 'Nested/app0/dvdroot_ps4/chr'}
        for name, path in layouts.items():
            folder = self.moddir / name / path
            folder.mkdir(parents=True)
            (folder / 'a.dcx').write_bytes(name.encode())
        (self.moddir / 'Archive/readme.txt').write_text('notes')
        (self.moddir / 'Junk/stuff').mkdir(parents=True)
        self.assertEqual(mods.discover(self.moddir), ['Archive', 'Bare', 'Nested', 'Title'])
        for name in layouts:
            with self.subTest(name=name):
                result = mods.build_overlay(self.game, self.root / 'out',
                                            [(name, self.moddir / name)])
                self.assertEqual((result / 'dvdroot_ps4/chr/a.dcx').read_bytes(), name.encode())

    def test_mod_site_layouts(self):
        # As mods come from mod sites: notes without a suffix and previews beside the wrapper,
        # two wrappers deep, and the game's folders next to screenshots.
        layouts = {'Readme': 'Readme v1/dvdroot_ps4/chr', 'Deep': 'Deep/Deep v2/dvdroot_ps4/chr',
                   'Shots': 'chr'}
        for name, path in layouts.items():
            folder = self.moddir / name / path
            folder.mkdir(parents=True)
            (folder / 'a.dcx').write_bytes(name.encode())
        (self.moddir / 'Readme/README').write_text('notes')
        (self.moddir / 'Readme/preview.webp').write_bytes(b'image')
        (self.moddir / 'Readme/Nexus page.url').write_text('[InternetShortcut]')
        (self.moddir / 'Shots/Screenshots').mkdir()
        (self.moddir / 'Shots/Screenshots/1.png').write_bytes(b'image')
        (self.moddir / 'Shots/readme.txt').write_text('notes')
        self.assertEqual(mods.discover(self.moddir), ['Deep', 'Readme', 'Shots'])
        for name in layouts:
            with self.subTest(name=name):
                result = mods.build_overlay(self.game, self.root / 'out',
                                            [(name, self.moddir / name)])
                self.assertEqual((result / 'dvdroot_ps4/chr/a.dcx').read_bytes(), name.encode())
                self.assertFalse((result / 'dvdroot_ps4/Screenshots').exists())
                self.assertFalse((result / 'dvdroot_ps4/readme.txt').exists())

    def test_unrecognized_folders_say_why(self):
        self.moddir.mkdir()
        (self.moddir / 'Zipped').mkdir()
        (self.moddir / 'Zipped/mod.zip').write_bytes(b'PK')
        (self.moddir / 'Two/A/dvdroot_ps4').mkdir(parents=True)
        (self.moddir / 'Two/B/dvdroot_ps4').mkdir(parents=True)
        (self.moddir / 'Two/A/dvdroot_ps4/x').write_bytes(b'x')
        (self.moddir / 'Empty').mkdir()
        found = dict(mods.unrecognized(self.moddir))
        self.assertEqual(sorted(found), ['Empty', 'Two', 'Zipped'])
        self.assertIn('archive', found['Zipped'])
        self.assertIn('several folders', found['Two'])
        self.assertEqual(found['Empty'], 'empty')

    def test_invalid_profile_fails(self):
        self.mod('A')
        config = self.root / 'mods.json'
        config.write_text('{"disabled":"A"}')
        with self.assertRaises(ValueError):
            mods.selected(self.moddir, config)

    @unittest.skipIf(os.name == 'nt', 'run.sh is the Linux launcher; Windows starts the game through run.py')
    def test_run_uses_overlay_propagates_exit_and_cleans_view(self):
        self.mod('A')
        python = self.root / 'python'
        python.write_text(f'#!{sys.executable}\nimport subprocess,sys\n'
            'if sys.argv[1] == "scripts/mods.py" or sys.argv[1] == "-c":\n'
            '    sys.exit(subprocess.call([sys.executable,*sys.argv[1:]]))\n')
        python.chmod(0o755)
        probe = self.root / 'probe'
        probe.write_text(f'#!{sys.executable}\nimport json,sys,os\nfrom pathlib import Path\n'
            'game=Path(sys.argv[sys.argv.index("--app0")+1])\n'
            'Path(os.environ["BB_DATA_DIR"],"mounted.json").write_text(json.dumps({\n'
            '"path":str(game),"content":(game/"dvdroot_ps4/chr/a.dcx").read_text()}))\n'
            'sys.exit(7)\n')
        probe.chmod(0o755)
        env = dict(os.environ, BB_PREBUILT='1', BB_PROBE=str(probe), PYTHON=str(python),
            BB_DATA_DIR=str(self.root), BB_GAME_DIR=str(self.game),
            BB_MODS_DIR=str(self.moddir), BB_MODS_ENABLED='1', BB_MODS_CONFIG=str(self.root/'mods.json'))
        result = subprocess.run(['bash', 'run.sh'], cwd=ROOT, env=env, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 7, result.stderr)
        mounted = json.loads((self.root / 'mounted.json').read_text())
        self.assertEqual(mounted['content'], 'mod')
        self.assertFalse(Path(mounted['path']).exists())
        self.assertEqual((self.assets/'a.dcx').read_bytes(), b'original')


class WindowsLinkTests(unittest.TestCase):
    """On Windows the overlay holds junctions and hard links, never symlinks (upstream #102): with
    Developer Mode on, symlinks are created fine and the game then panics reading through them
    (Dantelion2 FileTransferTask.cpp(865), every boot, with any loose-file mod)."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        for folder in ('chr', 'map'):
            (self.game / 'dvdroot_ps4' / folder).mkdir(parents=True)
            (self.game / 'dvdroot_ps4' / folder / 'a.dcx').write_bytes(b'original ' + folder.encode())
        (self.game / 'eboot.bin').write_bytes(b'original executable')
        self.mod = self.root / 'mods' / 'A' / 'dvdroot_ps4' / 'chr'
        self.mod.mkdir(parents=True)
        (self.mod / 'a.dcx').write_bytes(b'mod')
        (self.mod / 'new.dcx').write_bytes(b'new')
        self.layers = [('A', self.root / 'mods' / 'A')]

    def build_with_symlinks_recorded(self):
        real = Path.symlink_to
        calls = []

        def record(path, *args, **kwargs):
            calls.append(str(path))
            return real(path, *args, **kwargs)
        with mock.patch.object(Path, 'symlink_to', record):
            overlay = mods.build_overlay(self.game, self.root / 'out', self.layers)
        return overlay, calls

    @unittest.skipUnless(os.name == 'nt', 'junctions and hard links are Windows')
    def test_no_symlink_is_made_or_left_behind_and_the_game_reads_through_the_links(self):
        overlay, calls = self.build_with_symlinks_recorded()
        self.assertEqual(calls, [])
        entries = [overlay, *overlay.rglob('*')]
        # rglob does not enter junctions: the untouched map folder is one, the merged chr folder is real.
        self.assertTrue(entries)
        self.assertFalse([p for p in entries if p.is_symlink()])
        self.assertTrue((overlay / 'dvdroot_ps4' / 'map').is_junction())
        self.assertFalse((overlay / 'dvdroot_ps4' / 'chr').is_junction())
        self.assertEqual((overlay / 'dvdroot_ps4/map/a.dcx').read_bytes(), b'original map')
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        self.assertEqual((overlay / 'dvdroot_ps4/chr/new.dcx').read_bytes(), b'new')
        self.assertTrue((overlay / 'eboot.bin').samefile(self.game / 'eboot.bin'))  # a hard link
        self.assertTrue((overlay / 'dvdroot_ps4/chr/a.dcx').samefile(self.mod / 'a.dcx'))

    @unittest.skipUnless(os.name == 'nt', 'junctions and hard links are Windows')
    def test_removing_the_overlay_leaves_the_game_and_the_mod_alone(self):
        overlay, _ = self.build_with_symlinks_recorded()
        shutil.rmtree(overlay)
        self.assertFalse(overlay.exists())
        self.assertEqual((self.game / 'dvdroot_ps4/map/a.dcx').read_bytes(), b'original map')
        self.assertEqual((self.game / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'original chr')
        self.assertEqual((self.game / 'eboot.bin').read_bytes(), b'original executable')
        self.assertEqual((self.mod / 'a.dcx').read_bytes(), b'mod')

    def test_windows_uses_junctions_and_hard_links_even_where_symlinks_succeed(self):
        junctions = []
        links = []
        windows = types.SimpleNamespace(name='nt', link=lambda target, link: links.append((str(target), str(link))))
        winapi = types.SimpleNamespace(CreateJunction=lambda target, link: junctions.append((target, link)))
        real_symlink_to = Path.symlink_to
        symlinks = []

        def symlink_that_works(path, *args, **kwargs):  # Developer Mode on: this succeeds
            symlinks.append(str(path))
            return real_symlink_to(path, *args, **kwargs)
        folder, file = self.game / 'dvdroot_ps4', self.game / 'eboot.bin'
        with mock.patch.object(mods, 'os', windows), mock.patch.dict(sys.modules, {'_winapi': winapi}), \
                mock.patch.object(Path, 'symlink_to', symlink_that_works):
            mods.make_link(self.root / 'folder-link', folder)
            mods.make_link(self.root / 'file-link', file)
        self.assertEqual(symlinks, [])
        self.assertEqual(junctions, [(str(folder), str(self.root / 'folder-link'))])
        self.assertEqual(links, [(str(file), str(self.root / 'file-link'))])
        self.assertEqual(mods.LINKED.pop(str(self.root / 'folder-link')), folder)
        self.assertEqual(mods.LINKED.pop(str(self.root / 'file-link')), file)

    def test_windows_copies_a_file_when_it_cannot_be_hard_linked(self):
        def refuse(target, link):
            raise OSError('cross-device link')
        windows = types.SimpleNamespace(name='nt', link=refuse)
        with mock.patch.object(mods, 'os', windows):
            mods.make_link(self.root / 'copy', self.game / 'eboot.bin')
        self.assertEqual((self.root / 'copy').read_bytes(), b'original executable')
        self.assertFalse((self.root / 'copy').is_symlink())
        mods.LINKED.pop(str(self.root / 'copy'))

    def build_with_game_on_another_drive(self, writable_parent, link_fails=False, need_gb=None, free_gb=None):
        """build_overlay as for a game on another drive than out; returns the overlay and keeps
        stderr in self.log. link_fails: os.link refuses like across volumes (EXDEV). need_gb and
        free_gb stand in for the size of the copy and the free space on out's volume."""
        import contextlib
        import errno
        import io
        real = tempfile.mkdtemp
        gb = 1024 ** 3

        def mkdtemp(*args, dir=None, **kwargs):
            if Path(dir).resolve() == self.game.parent.resolve() and not writable_parent:
                raise PermissionError(13, 'Access is denied', str(dir))
            return real(*args, dir=dir, **kwargs)
        self.log = io.StringIO()
        self.link = mock.Mock(side_effect=OSError(errno.EXDEV, 'Invalid cross-device link'))
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(mods, 'beside_game', return_value=True))
            stack.enter_context(mock.patch.object(mods.tempfile, 'mkdtemp', mkdtemp))
            stack.enter_context(contextlib.redirect_stderr(self.log))
            if link_fails:
                stack.enter_context(mock.patch('os.link', self.link))
            if need_gb is not None:
                stack.enter_context(mock.patch.object(mods, 'copy_bytes', return_value=int(need_gb * gb)))
            if free_gb is not None:
                usage = types.SimpleNamespace(total=100 * gb, used=0, free=int(free_gb * gb))
                stack.enter_context(mock.patch.object(mods.shutil, 'disk_usage', return_value=usage))
            overlay = mods.build_overlay(self.game, self.root / 'out', self.layers)
        self.addCleanup(shutil.rmtree, overlay, True)
        return overlay

    def test_game_on_another_drive_gets_its_overlay_beside_it(self):
        overlay = self.build_with_game_on_another_drive(writable_parent=True)
        self.assertEqual(overlay.parent, self.game.parent.resolve())
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        self.assertNotIn('copies', self.log.getvalue())

    def test_unwritable_game_folder_falls_back_to_out_and_says_so(self):
        overlay = self.build_with_game_on_another_drive(writable_parent=False)
        out = (self.root / 'out').resolve()
        self.assertEqual(overlay.parent, out)
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        self.assertEqual((overlay / 'dvdroot_ps4/chr/new.dcx').read_bytes(), b'new')
        self.assertEqual((overlay / 'dvdroot_ps4/map/a.dcx').read_bytes(), b'original map')
        self.assertEqual((overlay / 'eboot.bin').read_bytes(), b'original executable')
        notes = [line for line in self.log.getvalue().splitlines() if str(out) in line]
        self.assertEqual(len(notes), 1, self.log.getvalue())
        self.assertTrue(notes[0].startswith('Mods: '), notes[0])
        self.assertRegex(notes[0], r'this build copies \d+\.\d GB')

    @unittest.skipUnless(os.name == 'nt', 'Windows links files with os.link; the others use symlinks')
    def test_files_that_cannot_be_hard_linked_are_copied_never_symlinked(self):
        overlay = self.build_with_game_on_another_drive(writable_parent=False, link_fails=True)
        self.assertTrue(self.link.called)  # the copy path ran: every link attempt was refused
        for name, original in (('eboot.bin', self.game / 'eboot.bin'),
                               ('dvdroot_ps4/chr/a.dcx', self.mod / 'a.dcx'),
                               ('dvdroot_ps4/chr/new.dcx', self.mod / 'new.dcx')):
            with self.subTest(name=name):
                self.assertEqual((overlay / name).read_bytes(), original.read_bytes())
                self.assertFalse((overlay / name).samefile(original))  # a copy, not a link
        self.assertFalse([p for p in [overlay, *overlay.rglob('*')] if p.is_symlink()])

    def test_copy_bytes_counts_the_files_a_cross_volume_overlay_copies(self):
        replacements = list(mods.mod_files(self.root / 'mods' / 'A'))
        # The game's top-level files, its files in the folders the mod touches (chr; map stays a
        # junction), and the mod's own files.
        expected = len(b'original executable') + len(b'original chr') + len(b'mod') + len(b'new')
        self.assertEqual(mods.copy_bytes(self.game.resolve(), replacements), expected)

    def test_a_copy_that_does_not_fit_stops_before_copying_and_says_why(self):
        with self.assertRaises(ValueError) as caught:
            self.build_with_game_on_another_drive(writable_parent=False, need_gb=50, free_gb=20)
        message = str(caught.exception)
        self.assertIn('50.0 GB', message)
        self.assertIn('20.0 GB', message)
        self.assertIn(str(self.game.parent.resolve()), message)  # the folder to make writable
        self.assertIn('same drive', message)
        self.assertEqual(list((self.root / 'out').glob('mod-game-*')), [])

    def test_a_copy_that_fits_says_how_big_it_is(self):
        self.build_with_game_on_another_drive(writable_parent=False, need_gb=5, free_gb=20)
        self.assertIn('this build copies 5.0 GB', self.log.getvalue())

    @unittest.skipIf(os.name == 'nt', 'symlinks are the other systems')
    def test_other_systems_keep_using_symlinks(self):
        overlay, calls = self.build_with_symlinks_recorded()
        self.assertTrue(calls)
        self.assertTrue((overlay / 'dvdroot_ps4' / 'map').is_symlink())
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')


class IconLayerTests(unittest.TestCase):
    """The button icons are a layer under the player's mods: alone they still make an overlay, and a mod that
    supplies the same file wins."""
    COMMON = 'dvdroot_ps4/menu/common.tpf.dcx'

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        (self.game / 'dvdroot_ps4/menu').mkdir(parents=True)
        (self.game / 'dvdroot_ps4/menu/common.tpf.dcx').write_bytes(b'game icons')
        (self.game / 'dvdroot_ps4/menu/other.dcx').write_bytes(b'other')
        (self.game / 'eboot.bin').write_bytes(b'exe')
        self.moddir = self.root / 'mods'
        self.icons = self.root / 'icons-cache'
        (self.icons / 'dvdroot_ps4/menu').mkdir(parents=True)
        (self.icons / 'dvdroot_ps4/menu/common.tpf.dcx').write_bytes(b'generated icons')

    def user_mod(self, name, file='common.tpf.dcx'):
        folder = self.moddir / name / 'dvdroot_ps4/menu'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / file).write_bytes(name.encode())

    def overlay(self, layers):
        return mods.build_overlay(self.game, self.root / 'out', layers)

    def test_icons_alone_make_an_overlay_with_the_generated_file(self):
        layers, note = mods.mod_layers(self.game, self.moddir, None, '1', self.icons)
        self.assertEqual([name for name, _ in layers], ['button icons'])
        self.assertIsNone(note)
        result = self.overlay(layers)
        self.assertEqual((result / self.COMMON).read_bytes(), b'generated icons')
        self.assertEqual((result / 'dvdroot_ps4/menu/other.dcx').read_bytes(), b'other')
        self.assertEqual((self.game / self.COMMON).read_bytes(), b'game icons')
        self.assertFalse([p for p in [result, *result.rglob('*')] if p.is_symlink()])

    def test_the_icons_go_under_the_mods_and_other_mods_still_apply(self):
        self.user_mod('Other', 'other.dcx')
        layers, note = mods.mod_layers(self.game, self.moddir, None, '1', self.icons)
        self.assertEqual([name for name, _ in layers], ['button icons', 'Other'])
        self.assertIsNone(note)
        result = self.overlay(layers)
        self.assertEqual((result / self.COMMON).read_bytes(), b'generated icons')
        self.assertEqual((result / 'dvdroot_ps4/menu/other.dcx').read_bytes(), b'Other')

    def test_a_mod_that_supplies_the_file_wins_and_the_icons_are_dropped_with_a_note(self):
        self.user_mod('Mine')
        layers, note = mods.mod_layers(self.game, self.moddir, None, '1', self.icons)
        self.assertEqual([name for name, _ in layers], ['Mine'])
        self.assertIn('Mine', note)
        self.assertIn('off', note)
        self.assertEqual((self.overlay(layers) / self.COMMON).read_bytes(), b'Mine')

    def test_a_disabled_mod_does_not_count_and_the_sibling_mods_folder_does(self):
        self.user_mod('Mine')
        config = self.root / 'mods.json'
        config.write_text(json.dumps({'disabled': ['Mine']}))
        layers, note = mods.mod_layers(self.game, self.moddir, config, '1', self.icons)
        self.assertEqual([name for name, _ in layers], ['button icons'])
        legacy = Path(str(self.game) + '-mods/dvdroot_ps4/menu')
        legacy.mkdir(parents=True)
        (legacy / 'COMMON.TPF.DCX').write_bytes(b'legacy')  # another case spells the same file
        layers, note = mods.mod_layers(self.game, self.moddir, config, '1', self.icons)
        self.assertEqual([name for name, _ in layers], [self.game.name + '-mods'])
        self.assertIn('-mods', note)

    def test_mods_switched_off_leave_the_icons(self):
        self.user_mod('Mine')
        layers, note = mods.mod_layers(self.game, self.moddir, None, '0', self.icons)
        self.assertEqual([name for name, _ in layers], ['button icons'])
        self.assertIsNone(note)

    def test_without_icons_the_layers_are_the_mods_alone(self):
        self.user_mod('Mine')
        layers, note = mods.mod_layers(self.game, self.moddir, None, '1', None)
        self.assertEqual([name for name, _ in layers], ['Mine'])
        self.assertIsNone(note)

    def test_the_script_takes_the_icons_layer(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/mods.py'), str(self.game),
                                 '--out', str(self.root / 'out'), '--mods-dir', str(self.moddir),
                                 '--icons-layer', str(self.icons)], capture_output=True, text=True, check=True)
        self.assertEqual((Path(result.stdout.strip()) / self.COMMON).read_bytes(), b'generated icons')


class StaleViewTests(unittest.TestCase):
    """A launch removes its view when the game ends, but a launch that is killed (the rig ends a test, a crash,
    Task Manager) never gets there: every one left a mod-game-* folder, 1.7 GB each with the party HUD, and the
    rig filled the disk. Each view names the process that owns it in <view>.owner, and the next launch removes
    the views whose owner is gone."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        (self.game / 'dvdroot_ps4' / 'chr').mkdir(parents=True)
        (self.game / 'dvdroot_ps4' / 'chr' / 'a.dcx').write_bytes(b'original')
        (self.game / 'eboot.bin').write_bytes(b'exe')
        mod = self.root / 'mods' / 'A' / 'dvdroot_ps4' / 'chr'
        mod.mkdir(parents=True)
        (mod / 'a.dcx').write_bytes(b'mod')
        self.layers = [('A', self.root / 'mods' / 'A')]
        self.out = self.root / 'out'

    def finished_pid(self):
        process = subprocess.Popen([sys.executable, '-c', 'pass'])
        process.wait()
        return process.pid

    def build(self, owner):
        return mods.build_overlay(self.game, self.out, self.layers, owner=owner)

    def test_the_view_names_its_owner(self):
        view = self.build(owner=os.getpid())
        self.assertEqual(Path(str(view) + '.owner').read_text().strip(), str(os.getpid()))

    def test_a_view_whose_owner_is_gone_is_removed_by_the_next_launch(self):
        stale = self.build(owner=self.finished_pid())
        current = self.build(owner=os.getpid())
        self.assertFalse(stale.exists())
        self.assertFalse(Path(str(stale) + '.owner').exists())
        self.assertEqual((current / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        self.assertEqual((self.game / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'original')
        self.assertEqual((self.game / 'eboot.bin').read_bytes(), b'exe')

    def test_a_running_launchs_view_is_kept(self):
        running = self.build(owner=os.getpid())
        self.build(owner=os.getpid())
        self.assertEqual((running / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')

    def test_views_from_before_owners_are_removed_once_old(self):
        self.out.mkdir()
        old, recent = self.out / 'mod-game-old', self.out / 'mod-game-recent'
        for view in (old, recent):
            (view / 'dvdroot_ps4').mkdir(parents=True)
            (view / 'dvdroot_ps4' / 'x').write_bytes(b'x')
        day_ago = time.time() - 24 * 3600
        os.utime(old, (day_ago, day_ago))
        self.build(owner=os.getpid())
        self.assertFalse(old.exists())
        self.assertTrue(recent.exists())

    def test_an_owner_file_without_its_view_is_removed(self):
        self.out.mkdir()
        orphan = self.out / 'mod-game-gone.owner'
        orphan.write_text(str(self.finished_pid()))
        self.build(owner=os.getpid())
        self.assertFalse(orphan.exists())

    def test_the_script_names_the_launch_that_started_it(self):
        command = [sys.executable, str(ROOT / 'scripts/mods.py'), str(self.game), '--out', str(self.out),
                   '--mods-dir', str(self.root / 'mods')]
        for env, owner in ((dict(os.environ, BB_VIEW_OWNER='4242'), '4242'), (os.environ, str(os.getpid()))):
            env = {key: value for key, value in env.items() if owner == '4242' or key != 'BB_VIEW_OWNER'}
            with self.subTest(owner=owner):
                result = subprocess.run(command, capture_output=True, text=True, check=True, env=env)
                view = Path(result.stdout.strip())
                self.assertEqual(Path(str(view) + '.owner').read_text().strip(), owner)

    def test_run_py_names_itself_the_owner(self):
        self.assertIn("env['BB_VIEW_OWNER'] = str(os.getpid())", (ROOT / 'run.py').read_text(encoding='utf-8'))
        self.assertIn('export BB_VIEW_OWNER=$(cat /proc/$$/winpid 2>/dev/null || echo $$)',
                      (ROOT / 'run.sh').read_text(encoding='utf-8'))


@unittest.skipUnless(os.name == 'nt', 'read-only hard links and junctions are Windows')
class ReadOnlyGameFileTests(unittest.TestCase):
    """A hard link shares the read-only attribute with the game file it links (a game copied from a read-only
    source), and Windows refuses to delete a read-only file: replacing such a file with a mod, or removing the
    overlay after the game, failed with WinError 5 "Access is denied". The attribute is cleared for the deletion
    and put back on the game file. From Supermedo's bbport (ba34a3a, emurphy04's PR #77)."""

    def setUp(self):
        import stat
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        self.chr = self.game / 'dvdroot_ps4' / 'chr'
        self.chr.mkdir(parents=True)
        (self.game / 'eboot.bin').write_bytes(b'original executable')
        (self.chr / 'a.dcx').write_bytes(b'original')
        (self.chr / 'b.dcx').write_bytes(b'untouched')
        self.read_only = [self.game / 'eboot.bin', self.chr / 'a.dcx', self.chr / 'b.dcx']
        for path in self.read_only:
            os.chmod(path, stat.S_IREAD)
        self.addCleanup(self.cleanup)
        mod = self.root / 'mods' / 'A' / 'dvdroot_ps4' / 'chr'
        mod.mkdir(parents=True)
        (mod / 'a.dcx').write_bytes(b'mod')
        self.layers = [('A', self.root / 'mods' / 'A')]

    def cleanup(self):
        import stat
        for path in self.root.rglob('*'):
            if path.is_file() and not path.is_junction():
                os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
        self.temp.cleanup()

    def assert_game_intact(self):
        import stat
        self.assertEqual((self.game / 'eboot.bin').read_bytes(), b'original executable')
        self.assertEqual((self.chr / 'a.dcx').read_bytes(), b'original')
        self.assertEqual((self.chr / 'b.dcx').read_bytes(), b'untouched')
        for path in self.read_only:
            self.assertFalse(path.stat().st_mode & stat.S_IWRITE, f'{path} lost its read-only attribute')

    def test_a_mod_replaces_a_read_only_game_file(self):
        overlay = mods.build_overlay(self.game, self.root / 'out', self.layers)
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')
        self.assertTrue((overlay / 'dvdroot_ps4/chr/b.dcx').samefile(self.chr / 'b.dcx'))  # a hard link
        self.assert_game_intact()

    def test_the_overlay_is_removed_and_the_game_keeps_its_files_and_attributes(self):
        overlay = mods.build_overlay(self.game, self.root / 'out', self.layers)
        mods.remove_overlay(overlay, self.game)
        self.assertFalse(overlay.exists())
        self.assert_game_intact()

    def test_an_overlay_made_by_another_run_is_removed_too(self):
        # run.py removes the view after the game: the links it made in mods.py's process are unknown there.
        overlay = mods.build_overlay(self.game, self.root / 'out', self.layers)
        mods.LINKED.clear()
        mods.remove_overlay(overlay, self.game)
        self.assertFalse(overlay.exists())
        self.assert_game_intact()


class LinklessDriveTests(unittest.TestCase):
    """[WinError 1] "Incorrect function": without symlink rights a folder of the view is a junction, which only NTFS
    has, so a port on an exFAT or FAT32 drive (USB, external) stopped at start. The view is then made in the system
    drive's temporary folder. From Supermedo's bbport (bf0878b)."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'CUSA03173'
        (self.game / 'dvdroot_ps4' / 'chr').mkdir(parents=True)
        (self.game / 'dvdroot_ps4' / 'chr' / 'a.dcx').write_bytes(b'original')
        (self.game / 'eboot.bin').write_bytes(b'exe')
        mod = self.root / 'mods' / 'A' / 'dvdroot_ps4' / 'chr'
        mod.mkdir(parents=True)
        (mod / 'a.dcx').write_bytes(b'mod')
        self.layers = [('A', self.root / 'mods' / 'A')]

    def test_beside_game_false_keeps_the_view_in_out(self):
        with mock.patch.object(mods, 'beside_game', return_value=True):
            overlay = mods.build_overlay(self.game, self.root / 'out', self.layers, beside=False)
        self.addCleanup(shutil.rmtree, overlay, True)
        self.assertEqual(overlay.parent, (self.root / 'out').resolve())
        self.assertEqual((overlay / 'dvdroot_ps4/chr/a.dcx').read_bytes(), b'mod')

    @unittest.skipUnless(os.name == 'nt', 'WinError codes are Windows')
    def test_incorrect_function_makes_the_view_in_the_temporary_folder(self):
        import contextlib
        import io
        refused = OSError(None, 'Incorrect function', None, 1)
        calls = []

        def build(game, out, layers, beside=True):
            calls.append((Path(out), beside))
            if len(calls) == 1:
                raise refused
            return Path(out) / 'view'
        argv = ['mods.py', str(self.game), '--out', str(self.root / 'out'), '--mods-dir', str(self.root / 'mods')]
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(mods, 'build_overlay', build), mock.patch.object(sys, 'argv', argv), \
                mock.patch.object(mods.tempfile, 'gettempdir', return_value=str(self.root / 'temp')), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            mods.main()
        fallback = self.root / 'temp' / 'bbport-game-view'
        self.assertEqual(calls, [(self.root / 'out', True), (fallback, False)])
        self.assertEqual(stdout.getvalue().strip(), str(fallback / 'view'))
        self.assertIn(str(fallback), stderr.getvalue())

    @unittest.skipUnless(os.name == 'nt', 'WinError codes are Windows')
    def test_other_errors_still_stop(self):
        def build(game, out, layers, beside=True):
            raise OSError(None, 'Access is denied', None, 5)
        argv = ['mods.py', str(self.game), '--out', str(self.root / 'out'), '--mods-dir', str(self.root / 'mods')]
        with mock.patch.object(mods, 'build_overlay', build), mock.patch.object(sys, 'argv', argv):
            with self.assertRaises(OSError):
                mods.main()
