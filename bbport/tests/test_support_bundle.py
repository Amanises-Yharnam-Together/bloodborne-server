"""The launcher's support bundle (launcher/bbport_support.py, Kaniel 2026-10-25): one zip with the logs,
reports, settings and a network check, never a save or the player's secret."""
import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

import bbport_support as support  # noqa: E402


def write(path, text='x', when=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    if when is not None:
        os.utime(path, (when, when))
    return path


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        self.user = self.root / 'install' / 'user'
        log = self.user / 'log'
        for name in ('shad_log.txt', 'console.txt', 'shad_log.prev.txt', 'shadnet.txt', 'launcher-errors.txt',
                     'event-patch.txt', 'crash-1792733000.txt', 'freeze-1792733895.txt'):
            write(log / name, name)
        write(log / 'older' / '20261024-210000-shad_log.txt', 'an older run')
        write(log / support.bbport_coop.REPORTS_SENT, '[]')
        for index in range(5):
            write(log / f'coop-trace-2026102{index}-120000-1.txt', f'trace {index}', when=1792700000 + index)
            write(log / f'crash-17927{index}.dmp', 'MDMP', when=1792700000 + index)
        write(self.user / 'savedata' / '1' / 'userdata0010', 'a save')
        write(self.user / 'last_run.log', 'the game exited')
        self.settings = {'game': 'C:/Games/CUSA03173', 'host_ip': '26.1.2.3', 'account': 'p0123456789abcd',
                         'password': 'the-secret', 'harder': 'off'}

    def tearDown(self):
        self.folder.cleanup()

    def make(self, **more):
        target = self.root / 'out' / support.bundle_name(1792900000)
        failed = support.make_bundle(target, self.user, settings=self.settings, **more)
        with zipfile.ZipFile(target) as bundle:
            return failed, {name: bundle.read(name).decode('utf-8', 'replace') for name in bundle.namelist()}

    def test_the_logs_and_reports_are_in(self):
        failed, files = self.make()
        self.assertEqual(failed, [])
        for name in ('log/shad_log.txt', 'log/console.txt', 'log/shad_log.prev.txt', 'log/shadnet.txt',
                     'log/launcher-errors.txt', 'log/event-patch.txt', 'log/crash-1792733000.txt',
                     'log/freeze-1792733895.txt', 'log/older/20261024-210000-shad_log.txt', 'last_run.log'):
            self.assertIn(name, files)
        self.assertNotIn('log/' + support.bbport_coop.REPORTS_SENT, files)

    def test_only_the_newest_traces_and_dumps(self):
        _failed, files = self.make()
        traces = sorted(name for name in files if 'coop-trace-' in name)
        self.assertEqual(traces, [f'log/coop-trace-2026102{index}-120000-1.txt' for index in (2, 3, 4)])
        dumps = sorted(name for name in files if name.endswith('.dmp'))
        self.assertEqual(dumps, [f'log/crash-17927{index}.dmp' for index in (2, 3, 4)])

    def test_no_save_and_no_secret(self):
        _failed, files = self.make()
        self.assertFalse(any('savedata' in name for name in files))
        self.assertNotIn('the-secret', ''.join(files.values()))
        coop = json.loads(files['coop.json'])
        self.assertEqual(coop['password'], support.LEFT_OUT)
        self.assertEqual(coop['account'], 'p0123456789abcd', "the server's log names the player by it")

    def test_texts_and_extra_files(self):
        ini = write(self.root / 'install' / 'bbport.ini', 'upscaler=fsr3\n')
        home = str(self.root / 'home' / 'vitsh')
        _failed, files = self.make(extra_files=[(ini, 'bbport.ini'), (self.root / 'none.ini', 'none.ini')],
                                   texts={'launcher-log.txt': f'started from {home}\\Games'}, home=home)
        self.assertEqual(files['bbport.ini'], 'upscaler=fsr3\n')
        self.assertNotIn('none.ini', files, 'a missing file is skipped')
        self.assertEqual(files['launcher-log.txt'], 'started from %USERPROFILE%\\Games')

    def test_an_unreadable_file_is_listed_not_fatal(self):
        real = zipfile.ZipFile.write

        def busy(bundle, path, name=None, *args, **kwargs):
            if str(name).endswith('freeze-1792733895.txt'):
                raise PermissionError(13, 'The process cannot access the file')
            return real(bundle, path, name, *args, **kwargs)

        zipfile.ZipFile.write = busy
        try:
            failed, files = self.make()
        finally:
            zipfile.ZipFile.write = real
        self.assertEqual(failed, [('log/freeze-1792733895.txt', 'The process cannot access the file')])
        self.assertIn('freeze-1792733895.txt', files['missing.txt'])
        self.assertIn('log/shad_log.txt', files)

    def test_no_half_written_zip_is_left(self):
        self.make()
        self.assertEqual([path.name for path in (self.root / 'out').iterdir()], [support.bundle_name(1792900000)])

    def test_a_live_log_can_be_bundled(self):
        # The game keeps its log open and writing while the bundle reads it.
        with open(self.user / 'log' / 'shad_log.txt', 'a', encoding='utf-8') as live:
            live.write(' and more')
            live.flush()
            _failed, files = self.make()
        self.assertEqual(files['log/shad_log.txt'], 'shad_log.txt and more')

    def test_no_log_folder_yet(self):
        empty = self.root / 'fresh' / 'user'
        target = self.root / 'fresh.zip'
        self.assertEqual(support.make_bundle(target, empty, texts={'system.txt': 'x'}), [])
        with zipfile.ZipFile(target) as bundle:
            self.assertEqual(bundle.namelist(), ['system.txt'])


class ReportTests(unittest.TestCase):
    def test_bundle_name_and_folder(self):
        self.assertRegex(support.bundle_name(1792900000), r'^Yharnam Together support \d{8}-\d{6}\.zip$')
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(support.default_folder(home), Path(home))
            (Path(home) / 'Desktop').mkdir()
            self.assertEqual(support.default_folder(home), Path(home) / 'Desktop')

    def test_network_report(self):
        asked = []

        def answers(host, port):
            asked.append((host, port))
            return port != support.bbport_coop.BACKEND_PORT

        text = support.network_report({'host_ip': '26.1.2.3'}, answers=answers,
                                      addresses=['192.168.1.5', '26.9.8.7'], this_pc=False)
        self.assertIn("Host's IP address: 26.1.2.3 (Radmin VPN)", text)
        self.assertIn('This PC hosts: no', text)
        self.assertIn('VPNs on this PC: Radmin VPN 26.9.8.7', text)
        self.assertIn(f'TCP {support.bbport_coop.SHADNET_PORT}: answers', text)
        self.assertIn(f'TCP {support.bbport_coop.BACKEND_PORT}: no answer', text)
        self.assertEqual({host for host, _port in asked}, {'26.1.2.3'})

    def test_network_report_without_a_host(self):
        text = support.network_report({}, answers=lambda *_a: self.fail('nothing to ask'), addresses=[],
                                      this_pc=False)
        self.assertIn("Host's IP address: (not set)", text)
        self.assertNotIn('TCP', text)

    def test_system_report(self):
        text = support.system_report({'Launcher': '1.7.1', 'Release': '1.2.0'}, now=1792900000)
        self.assertIn('Launcher: 1.7.1\nRelease: 1.2.0\n', text)
        self.assertIn('System: ', text)


class LauncherTests(unittest.TestCase):
    def test_the_button_is_on_the_log_and_co_op_pages_and_translated_everywhere(self):
        import ast
        import bbport_lang
        launcher = Path(__file__).resolve().parents[1] / 'launcher' / 'bbport_launcher_win.py'
        source = launcher.read_text(encoding='utf-8')
        functions = {f.name: f for f in ast.walk(ast.parse(source)) if isinstance(f, ast.FunctionDef)}
        for page in ('build_log', 'build_coop_popup'):
            self.assertIn('self.make_support_bundle', ast.unparse(functions[page]), page)
        names = ('make_support_bundle', 'support_bundle_worker', 'support_bundle_done')
        texts = {n.args[0].value for name in names for n in ast.walk(functions[name]) if isinstance(n, ast.Call)
                 and isinstance(n.func, ast.Name) and n.func.id == '_' and n.args
                 and isinstance(n.args[0], ast.Constant)}
        texts.add('Support bundle')
        self.assertEqual(len(texts), 5)
        for language in ('ar', 'es', 'pt', 'fr', 'de', 'it', 'pl', 'tr', 'zh', 'ja', 'ko'):
            table = bbport_lang.table(language)
            for text in texts:
                self.assertTrue(table.get(text), f'{language}: {text}')
                self.assertEqual(table[text].count('{}'), text.count('{}'), f'{language}: {text}')
        self.assertIn('bbport_support.worker_priority()', ast.unparse(functions['support_bundle_worker']),
                      'zipping never takes time from the game')


if __name__ == '__main__':
    unittest.main()
