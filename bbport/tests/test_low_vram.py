"""Low-VRAM profile (run.py low_vram_mb, Supermedo 44a6433): discrete GPUs with up to 4.5 GB."""
from paths import ROOT
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('bbport_run_low_vram', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class LowVramTests(unittest.TestCase):
    def caps(self, directory, output):
        """A stand-in for bb-gpu-capabilities --vram printing `output`."""
        if os.name == 'nt':
            tool = Path(directory) / 'caps.cmd'
            tool.write_text(f'@echo {output}\n')
        else:
            tool = Path(directory) / 'caps'
            tool.write_text(f'#!/bin/sh\necho {output}\n')
            tool.chmod(0o755)
        return tool

    def test_discrete_cards_up_to_4_5_gb(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertEqual(run.low_vram_mb({}, self.caps(temp, '4096 1')), 4096)
            self.assertEqual(run.low_vram_mb({}, self.caps(temp, '6144 1')), 0)
            # Integrated GPUs follow the driver's budget already.
            self.assertEqual(run.low_vram_mb({}, self.caps(temp, '512 0')), 0)

    def test_setting_forces_or_disables_it(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertEqual(run.low_vram_mb({'BB_LOW_VRAM': '1'}, self.caps(temp, '24138 1')), 24138)
            self.assertEqual(run.low_vram_mb({'BB_LOW_VRAM': '0'}, self.caps(temp, '4096 1')), 0)

    def test_unknown_memory(self):
        with tempfile.TemporaryDirectory() as temp:
            missing = Path(temp) / 'missing.exe'
            self.assertEqual(run.low_vram_mb({}, missing), 0)
            self.assertEqual(run.low_vram_mb({'BB_LOW_VRAM': '1'}, missing), 4096)
            self.assertEqual(run.low_vram_mb({}, self.caps(temp, 'garbage')), 0)

    def test_texture_budget_leaves_room_for_the_desktop(self):
        self.assertEqual(run.low_vram_budget_mb(4096), 4096 - 600)
        self.assertEqual(run.low_vram_budget_mb(2048), 2048)  # never below 2 GB

    def test_physical_cores(self):
        self.assertGreaterEqual(run.physical_cores(), 1)


if __name__ == '__main__':
    unittest.main()
