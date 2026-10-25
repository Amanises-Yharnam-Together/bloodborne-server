"""The DLSS note in Graphics names the install's bin folder and says which DLSS files are missing there. It said
'next to the game', and a v1.2.1 player looked for them in his Bloodborne game folder."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

if sys.platform == 'win32':
    import bbport_launcher_win as launcher


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class DlssFilesHintTests(unittest.TestCase):
    def install(self, files):
        root = Path(tempfile.mkdtemp())
        (root / 'bin').mkdir()
        for name in files:
            (root / 'bin' / name).write_bytes(b'')
        return root

    def test_all_files_present_names_the_bin_folder(self):
        root = self.install(launcher.DLSS_FILES)
        hint = launcher.dlss_files_hint(root)
        self.assertIn(str(root / 'bin'), hint)
        self.assertIn('found', hint)
        self.assertNotIn('next to the game', hint)

    def test_missing_files_are_named(self):
        root = self.install(['bbport_dlss.dll', 'nvngx_dlss.dll'])
        hint = launcher.dlss_files_hint(root)
        self.assertIn(str(root / 'bin'), hint)
        self.assertIn('nvngx_dlssg.dll missing', hint)

    def test_files_in_the_install_root_do_not_count(self):
        root = self.install([])
        for name in launcher.DLSS_FILES:
            (root / name).write_bytes(b'')
        self.assertIn('missing', launcher.dlss_files_hint(root))


if __name__ == '__main__':
    unittest.main()
