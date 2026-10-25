from paths import ROOT
import re
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

import shader_pack

PROFILE = struct.pack('<4I', shader_pack.PROFILE_MAGIC, 17, 8, 7) + bytes(64)


def cache(folder, extra=()):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'profile.bin').write_bytes(PROFILE)
    for name in ('g_0x00000000000000aa.key', 'c_0x00000000000000bb.key', '0x00000000000000aa.meta',
                 '0x00000000000000aa_0.spv', *extra):
        (folder / name).write_bytes(b'x')
    return folder


class ShaderPackTest(unittest.TestCase):
    def test_names_match_the_port(self):
        """The same names as the port's own check (tests/test_cache_consistency.cpp)."""
        source = (ROOT / 'tests' / 'test_cache_consistency.cpp').read_text()
        cases = re.findall(r'assert\((!?)IsPackEntryName\("((?:[^"\\]|\\.)*)"\)\);', source)
        self.assertGreater(len(cases), 15)
        for negated, name in cases:
            name = name.encode().decode('unicode_escape')
            self.assertEqual(shader_pack.is_entry(name), not negated, name)

    def test_make_keeps_cache_blobs_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = cache(Path(tmp) / 'CUSA03173', ['vk_pipeline_cache.vkc', '0x00000000000000cc.meta.tmp'])
            out = Path(tmp) / 'packs' / 'nvidia.zip'
            entries, digest = shader_pack.make(folder, out)
            self.assertEqual(entries, 5)
            with zipfile.ZipFile(out) as pack:
                names = pack.namelist()
            self.assertEqual(names[0], 'profile.bin')
            self.assertNotIn('vk_pipeline_cache.vkc', names)
            self.assertNotIn('0x00000000000000cc.meta.tmp', names)
            details = shader_pack.info(out)
            self.assertEqual(details['versions'], (17, 8, 7))
            self.assertEqual((details['pipelines'], details['shaders'], details['binaries']), (2, 1, 1))
            self.assertEqual(details['foreign'], [])
            self.assertEqual(details['sha256'], digest)
            self.assertFalse(out.with_name('nvidia.zip.tmp').exists())

    def test_make_refuses_a_cache_without_profile_or_pipelines(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = cache(Path(tmp) / 'a')
            (folder / 'profile.bin').unlink()
            with self.assertRaises(SystemExit):
                shader_pack.make(folder, Path(tmp) / 'a.zip')
            folder = Path(tmp) / 'b'
            folder.mkdir()
            (folder / 'profile.bin').write_bytes(PROFILE)
            with self.assertRaises(SystemExit):
                shader_pack.make(folder, Path(tmp) / 'b.zip')
            (folder / 'profile.bin').write_bytes(b'older build')
            (folder / 'g_0x00000000000000aa.key').write_bytes(b'x')
            with self.assertRaises(SystemExit):
                shader_pack.make(folder, Path(tmp) / 'b.zip')

    def test_info_names_foreign_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            pack = Path(tmp) / 'odd.zip'
            with zipfile.ZipFile(pack, 'w') as out:
                out.writestr('profile.bin', PROFILE)
                out.writestr('../escape.key', b'x')
            self.assertEqual(shader_pack.info(pack)['foreign'], ['../escape.key'])
            self.assertEqual(shader_pack.main(['info', str(pack)]), 1)

    def test_source_versions_are_the_ports(self):
        versions = shader_pack.source_versions()
        self.assertEqual(len(versions), 3)
        self.assertTrue(all(v > 0 for v in versions))

    def test_release_copies_current_packs_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            packs = Path(tmp) / 'packs'
            shader_pack.make(cache(Path(tmp) / 'now'), packs / 'nvidia.zip')
            old = cache(Path(tmp) / 'old')
            (old / 'profile.bin').write_bytes(struct.pack('<4I', shader_pack.PROFILE_MAGIC, 16, 8, 7))
            shader_pack.make(old, packs / 'amd.zip')
            dest = Path(tmp) / 'shader_packs'
            dest.mkdir()
            (dest / 'removed.zip').write_bytes(b'from an older release')
            copied, stale = shader_pack.copy_current(packs, dest, versions=(17, 8, 7))
            self.assertEqual((copied, stale), (['nvidia.zip'], ['amd.zip']))
            self.assertEqual(sorted(p.name for p in dest.iterdir()), ['nvidia.zip'])


if __name__ == '__main__':
    unittest.main()
