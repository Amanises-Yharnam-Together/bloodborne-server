"""Game patches against the co-op site table (patches/coop-sites.json, written from the emulator's
bloodborne_patch_sites.h): a patch that writes over a site co-op patches or hooks is reported,
naming both. With BLOODBORNE_EBOOT naming the 01.09 eboot.bin, the patch sets the launcher can
apply are applied to the game's bytes and every required co-op site must survive them."""
from paths import ROOT
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import patches
from patches import (EBOOT_BASE, coop_conflicts, compile_named, load_coop_sites,
                     report_coop_conflicts)

XML = ROOT / 'patches/Bloodborne.xml'
SEGMENTS = [(0, 0x6000000)]


def site(name):
    return next(s for s in load_coop_sites()['sites'] if s['name'] == name)


class TableTests(unittest.TestCase):
    def test_the_launcher_reads_the_committed_table(self):
        coop = load_coop_sites()
        self.assertTrue(coop['features']['seamless core'])
        self.assertTrue(coop['features']['sign timers'])
        self.assertFalse(coop['features']['item button diagnostics'])
        repost = site('sign re-post time')
        self.assertEqual(repost['offset'], 0x018A9757)
        self.assertEqual(repost['original'], bytes.fromhex('C74328 0000F041'))
        self.assertEqual(repost['accepted'], ['C7 43 28 ?? ?? ?? ??'])
        self.assertEqual({s['kind'] for s in coop['sites']}, {'patch', 'hook', 'call'})
        self.assertEqual(len({s['name'] for s in coop['sites']}), len(coop['sites']))


class ConflictTests(unittest.TestCase):
    def test_a_write_over_a_required_site_names_the_patch_and_the_feature(self):
        branch = site('BlockClear2_3 warp home')
        conflicts = coop_conflicts([('Some Mod', branch['offset'], b'\xeb\x46')])
        self.assertEqual(conflicts, [('Some Mod', 'BlockClear2_3 warp home', branch['offset'],
                                      'boss clear', True)])

    def test_a_partial_overlap_counts(self):
        hook = site('leave limit')  # 6 bytes
        conflicts = coop_conflicts([('Tail', hook['offset'] + 5, b'\x90\x90')])
        self.assertEqual([(c[0], c[1]) for c in conflicts], [('Tail', 'leave limit')])
        conflicts = coop_conflicts([('Head', hook['offset'] - 1, b'\x90\x90')])
        self.assertEqual([(c[0], c[1]) for c in conflicts], [('Head', 'leave limit')])

    def test_writes_next_to_a_site_do_not(self):
        hook = site('leave limit')
        self.assertEqual(coop_conflicts([('Before', hook['offset'] - 2, b'\x90\x90'),
                                         ('After', hook['offset'] + 6, b'\x90')]), [])

    def test_an_accepted_variant_is_no_conflict(self):
        # "Uncap FPS++" writes 60 s into the sign re-post time: the store is ours all the same.
        repost = site('sign re-post time')
        self.assertEqual(coop_conflicts([('Uncap FPS++', repost['offset'] + 3,
                                          struct.pack('<f', 60.0))]), [])
        # Another instruction there is.
        self.assertEqual(len(coop_conflicts([('Odd', repost['offset'], b'\x90' * 7)])), 1)

    def test_the_games_own_bytes_written_again_are_no_conflict(self):
        hook = site('LeaveSession')
        self.assertEqual(coop_conflicts([('Same', hook['offset'], hook['original'])]), [])

    def test_optional_sites_and_called_functions_are_reported_too(self):
        diag = site('item button update')
        call = site('SetForcedSummonWarp')
        conflicts = coop_conflicts([('A', diag['offset'], b'\xcc'), ('B', call['offset'], b'\xcc')])
        self.assertEqual({(c[0], c[3], c[4]) for c in conflicts},
                         {('A', 'item button diagnostics', False), ('B', 'seamless core', True)})

    def test_every_patch_over_a_broken_site_is_named(self):
        hook = site('LeaveSession')
        conflicts = coop_conflicts([('One', hook['offset'], b'\xcc'),
                                    ('Two', hook['offset'] + 1, b'\xcc')])
        self.assertEqual([c[0] for c in conflicts], ['One', 'Two'])

    def test_the_report_is_loud_and_names_both_sides(self):
        branch = site('BlockClear2_3 warp home')
        diag = site('item button update')
        conflicts = coop_conflicts([('Some Mod', branch['offset'], b'\xeb\x46'),
                                    ('Other Mod', diag['offset'], b'\xcc')])
        out = io.StringIO()
        report_coop_conflicts(conflicts, out)
        text = out.getvalue()
        self.assertIn('CO-OP CONFLICT', text)
        self.assertIn('"Some Mod"', text)
        self.assertIn('"boss clear" (required)', text)
        self.assertIn(f'{branch["offset"] + EBOOT_BASE:#x}', text)
        self.assertIn('"item button diagnostics" (optional)', text)
        out = io.StringIO()
        report_coop_conflicts([], out)
        self.assertEqual(out.getvalue(), '')


class BuiltInPatchTests(unittest.TestCase):
    def test_named_writes_keep_the_patch_names(self):
        writes = compile_named(XML, ['Uncap FPS++', 'Disable DoF'], '01.09', SEGMENTS)
        self.assertEqual({name for name, _, _ in writes}, {'Uncap FPS++', 'Disable DoF'})
        self.assertEqual([(o, d) for _, o, d in writes],
                         patches.compile_patches(XML, ['Uncap FPS++', 'Disable DoF'], '01.09',
                                                 SEGMENTS))

    def test_no_selectable_patch_breaks_a_required_site(self):
        # The FPS presets, the effect switches, the cheats, the resolution and heap patches: each
        # alone, on the table's bytes.
        names = {n for preset in patches.FPS_PRESETS.values() for n in preset}
        names |= {n for pair in patches.EFFECTS.values() for n in pair if n}
        names |= set(patches.MODEL_LOD.values())
        names |= {patches.RESOLUTION_TEMPLATE, 'Increased Graphics Heap Sizes',
                  patches.INTEL_TONEMAP, patches.SKIP_NETWORK_CHOICE}
        for name in sorted(names):
            with self.subTest(patch=name):
                conflicts = coop_conflicts(compile_named(XML, [name], '01.09', SEGMENTS))
                self.assertEqual([c for c in conflicts if c[4]], [])

    def test_main_warns_about_an_external_patch_over_a_co_op_site(self):
        branch = site('BlockClear2_3 warp home')
        external = f"""<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA03173</ID></TitleID>
  <Metadata Title="Bloodborne" Name="Boss Mod" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="{branch['offset'] + EBOOT_BASE:#x}" Value="EB46"/></PatchList>
  </Metadata>
</Patch>"""
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            header = bytearray(64)
            header[0:7] = b'\x7fELF\x02\x01\x01'
            struct.pack_into('<Q', header, 0x20, 64)
            struct.pack_into('<HH', header, 0x36, 56, 1)
            (out / 'eboot.elf').write_bytes(bytes(header) + struct.pack(
                '<IIQQQQQQ', 1, 5, 0, 0, 0, 0x6000000, 0x6000000, 0x1000))
            (out / 'mods').mkdir()
            (out / 'mods/boss.xml').write_text(external, encoding='utf-8')
            run = subprocess.run([sys.executable, str(ROOT / 'scripts/patches.py'), '--out', str(out),
                                  '--fps', 'uncap', '--settings', str(out / 'none.ini'),
                                  '--game-dir', str(out), '--xml', str(XML),
                                  '--patches-dir', str(out / 'mods')],
                                 # BB_FORCE_PATCHES: the test's ELF has none of the game's code.
                                 env={**os.environ, 'SHADPS4_BLOODBORNE_SEAMLESS_COOP': '1',
                                      'BB_FORCE_PATCHES': '1'},
                                 capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('CO-OP CONFLICT', run.stderr)
        self.assertIn('"boss.xml/Boss Mod"', run.stderr)
        self.assertIn('"boss clear" (required)', run.stderr)
        # The default patches themselves leave every co-op site alone.
        self.assertEqual(run.stderr.count('CO-OP CONFLICT'), 1, run.stderr)


def eboot_image(path):
    """The 01.09 eboot.bin's loadable segments at their image offsets (eboot vaddr 0 at 0)."""
    from prepare import parse_self
    elf, _, ph, _, _ = parse_self(Path(path).read_bytes())
    image = bytearray(max(p['vaddr'] + p['memsz'] for p in ph if p['type'] == 1))
    for p in ph:
        if p['type'] == 1:
            image[p['vaddr']:p['vaddr'] + p['filesz']] = elf[p['offset']:p['offset'] + p['filesz']]
    return image


@unittest.skipUnless(os.environ.get('BLOODBORNE_EBOOT'), 'BLOODBORNE_EBOOT names no eboot.bin')
class EbootTests(unittest.TestCase):
    """The case that broke: the kit's default patch set applied to the game, then the co-op sites."""

    @classmethod
    def setUpClass(cls):
        cls.image = eboot_image(os.environ['BLOODBORNE_EBOOT'])
        cls.coop = load_coop_sites()

    def patched(self, names):
        image = bytearray(self.image)
        for _, offset, data in compile_named(XML, names, '01.09', SEGMENTS):
            image[offset:offset + len(data)] = data
        return image

    def broken_sites(self, image, required_only=True):
        return [s['name'] for s in self.coop['sites']
                if (self.coop['features'][s['feature']] or not required_only)
                and not patches.matches_coop_site(image[s['offset']:s['offset'] + len(s['original'])], s)]

    def test_the_unpatched_game_holds_every_site(self):
        self.assertEqual(self.broken_sites(self.image, required_only=False), [])

    def test_the_default_patch_set_keeps_every_required_site(self):
        # run.py's default (BB_FPS=uncap), the effect switches' defaults with every effect on (no
        # patch), the Intel tone map fix and the resolution and heap patches of a 4K output.
        names = patches.FPS_PRESETS['uncap'] + [patches.INTEL_TONEMAP, patches.RESOLUTION_TEMPLATE,
                                                'Increased Graphics Heap Sizes']
        image = self.patched(names)
        self.assertEqual(self.broken_sites(image), [])
        # Uncap FPS++ rewrites the sign re-post time: a variant the table accepts.
        repost = next(s for s in self.coop['sites'] if s['name'] == 'sign re-post time')
        self.assertTrue(patches.matches_coop_site(image[repost['offset']:repost['offset'] + 7], repost))

    def test_every_selectable_patch_keeps_every_required_site(self):
        names = {n for preset in patches.FPS_PRESETS.values() for n in preset}
        names |= {n for pair in patches.EFFECTS.values() for n in pair if n}
        names |= set(patches.MODEL_LOD.values())
        for name in sorted(names):
            with self.subTest(patch=name):
                self.assertEqual(self.broken_sites(self.patched([name])), [])


if __name__ == '__main__':
    unittest.main()
