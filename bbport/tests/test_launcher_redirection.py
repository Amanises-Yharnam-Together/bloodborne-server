"""Kaniel 2026-10-14: the launcher the installer's last page started inherited the installer's
RedirectionGuard (Windows refuses the process any path through a folder link a user made: WinError
448), so PLAY could not read the game view (links to the player's own copy). The launcher finds the
rule on itself and starts again through the shell, once."""
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

if sys.platform == 'win32':
    import bbport_launcher_win as launcher


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class RedirectionGuardTests(unittest.TestCase):
    def test_a_process_started_from_a_shell_has_no_such_rule(self):
        self.assertFalse(launcher.redirection_guarded())

    def test_a_guarded_launcher_starts_again_through_the_shell_once(self):
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.object(launcher, 'DATA_DIR', Path(folder)), \
                mock.patch.object(launcher.subprocess, 'Popen') as popen:
            self.assertTrue(launcher.restart_without_redirection_guard())
            popen.assert_called_once()
            self.assertEqual(Path(popen.call_args.args[0][0]).name.lower(), 'explorer.exe')
            self.assertEqual(popen.call_args.args[0][1], sys.executable)
            # Started again and still guarded (Explorer's own rule, say): it does not loop.
            self.assertFalse(launcher.restart_without_redirection_guard())
            popen.assert_called_once()
            # A minute later it may try once more.
            marker = Path(folder) / 'relaunched.txt'
            old = time.time() - 120
            import os
            os.utime(marker, (old, old))
            self.assertTrue(launcher.restart_without_redirection_guard())


if __name__ == '__main__':
    unittest.main()
