"""The co-op release inside the launcher (launcher/bbport_coop.py): what the release's bat files and
tools\\setup.ps1 did (Kaniel 2026-10-13: one setup .exe, no bat files, the launcher finds the game)."""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

import bbport_coop as coop  # noqa: E402


def write_sfo(path, values):
    """A minimal PS4 param.sfo with text values (format 0x0204)."""
    keys, data, entries = b'', b'', []
    for key, value in values.items():
        raw = value.encode() + b'\0'
        entries.append((len(keys), 0x0204, len(raw), len(raw), len(data)))
        keys += key.encode() + b'\0'
        data += raw
    keys_at = 20 + 16 * len(entries)
    data_at = keys_at + len(keys)
    head = b'\0PSF' + struct.pack('<IIII', 0x101, keys_at, data_at, len(entries))
    body = b''.join(struct.pack('<HHIII', *entry) for entry in entries)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(head + body + keys + data)


def make_game(folder, version='01.09', title='CUSA03173'):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'eboot.bin').write_bytes(b'eboot ' + version.encode())
    write_sfo(folder / 'sce_sys' / 'param.sfo', {'TITLE_ID': title, 'APP_VER': version})
    events = folder / 'dvdroot_ps4' / 'event'
    events.mkdir(parents=True, exist_ok=True)
    (events / 'common.emevd.dcx').write_bytes(b'events ' + version.encode())
    return folder


class AnswersTests(unittest.TestCase):
    def test_the_fields_take_what_setup_took(self):
        self.assertTrue(coop.valid_ip('26.12.34.56'))
        self.assertFalse(coop.valid_ip('127.0.0.1'), 'the guests cannot reach loopback')
        self.assertFalse(coop.valid_ip('300.1.1.1'))
        self.assertFalse(coop.valid_ip('host'))
        self.assertTrue(coop.valid_name('Lisara'))
        self.assertFalse(coop.valid_name('ab'))
        self.assertFalse(coop.valid_name('with space'))
        self.assertTrue(coop.valid_secret('pass_1'))
        self.assertFalse(coop.valid_secret(''))
        self.assertTrue(coop.valid_harder('off'))
        self.assertTrue(coop.valid_harder('3,1.5,3'))
        self.assertFalse(coop.valid_harder('3,1.5'))

    def test_missing_answers_are_named(self):
        # No party settings: teams are made in the game (phase 6), so no party name or password.
        self.assertNotIn('party_name', coop.DEFAULTS)
        self.assertNotIn('party_secret', coop.DEFAULTS)
        # Phase 11 (Kaniel 2026-10-15): no account name or password to type; the player id is made.
        settings = dict(coop.DEFAULTS, game='', role='guest', host_ip='', account='', password='')
        self.assertEqual(coop.missing(settings), ['game', 'host_ip'])
        settings.update(game='G', host_ip='26.1.2.3')
        self.assertEqual(coop.missing(settings), [])

    def test_a_player_id_is_made_once_and_kept(self):
        settings = coop.with_player_id(dict(coop.DEFAULTS))
        self.assertTrue(coop.valid_name(settings['account']), settings['account'])
        self.assertTrue(coop.valid_secret(settings['password']))
        self.assertGreaterEqual(len(settings['password']), 32, 'the id is the secret: long and random')
        self.assertEqual(coop.with_player_id(dict(settings)), settings, 'made once, then kept')
        other = coop.with_player_id(dict(coop.DEFAULTS))
        self.assertNotEqual(other['account'], settings['account'])
        # A player who had an account keeps it, and with it its teams and worlds.
        old = coop.with_player_id(dict(coop.DEFAULTS, account='kaniel', password='pw'))
        self.assertEqual((old['account'], old['password']), ('kaniel', 'pw'))
        with tempfile.TemporaryDirectory() as folder:
            install = Path(folder)
            made = coop.load_settings(install)
            self.assertTrue(coop.valid_name(made['account']), 'a new install has its id at once')
            self.assertEqual(coop.load_settings(install)['account'], made['account'], 'saved')

    def test_the_old_settings_bat_carries_over(self):
        with tempfile.TemporaryDirectory() as folder:
            bat = Path(folder) / 'settings.bat'
            bat.write_text('@rem Written by 1 - SETUP.bat\nset "GAME=D:\\Games\\CUSA03173"\nset "UPDATE=none"\n'
                           'set "ROLE=host"\nset "HOSTIP=26.1.2.3"\nset "NPID=kaniel"\nset "PASS=pw"\n'
                           'set "PARTY=hunters:secret"\nset "P2PPORT=auto"\nset "FPS=60"\nset "HARDER=3,1.5,3"\n',
                           encoding='ascii')
            settings = coop.from_settings_bat(bat)
        self.assertEqual(settings['game'], 'D:\\Games\\CUSA03173')
        self.assertEqual(settings['update'], '')
        self.assertEqual((settings['role'], settings['host_ip'], settings['account'], settings['password']),
                         ('host', '26.1.2.3', 'kaniel', 'pw'))
        self.assertNotIn('party_secret', settings)
        self.assertEqual(settings['harder'], '3,1.5,3')

    def test_settings_are_kept_in_the_user_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            install = Path(folder)
            self.assertEqual(dict(coop.load_settings(install), account='', password=''), coop.DEFAULTS)
            coop.save_settings(install, dict(coop.DEFAULTS, account='Lisara', password='pw'))
            self.assertEqual(json.loads((install / 'user' / 'coop.json').read_text('utf-8'))['account'], 'Lisara')
            self.assertEqual(coop.load_settings(install)['account'], 'Lisara')


class FindGameTests(unittest.TestCase):
    def test_a_game_folder_is_found_inside_the_one_picked(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            game = make_game(root / 'Bloodborne' / 'CUSA03173', version='01.00')
            make_game(root / 'Bloodborne' / 'CUSA03173-patch')
            self.assertEqual(coop.find_game_folder(root), game, 'not the update next to it')
            self.assertEqual(coop.find_game_folder(game), game)
            self.assertIsNone(coop.find_game_folder(root / 'nothing'))
            self.assertEqual(coop.update_folder(game), root / 'Bloodborne' / 'CUSA03173-patch')
            self.assertEqual(coop.game_version(game), ('CUSA03173', '01.00'))

    def test_the_search_looks_near_the_install_and_on_the_drives(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            install = root / 'Programs' / 'Bloodborne Seamless Co-op'
            install.mkdir(parents=True)
            game = make_game(root / 'drive' / 'Games' / 'PS4' / 'CUSA03173')
            (root / 'drive' / 'Windows' / 'System32').mkdir(parents=True)
            found = coop.search_game(install, drives=[root / 'drive'], homes=[])
            self.assertEqual(found, game)
            self.assertIsNone(coop.search_game(install, drives=[root / 'empty'], homes=[]))

    def test_another_game_is_not_taken_for_bloodborne(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            make_game(root / 'drive' / 'CUSA00001', title='CUSA00001')
            self.assertIsNone(coop.search_game(root / 'install', drives=[root / 'drive'], homes=[]))


@unittest.skipUnless(sys.platform == 'win32', 'folder links (junctions) are Windows')
class GameViewTests(unittest.TestCase):
    def test_the_update_goes_over_the_game_through_links(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            game = make_game(root / 'CUSA03173', version='01.00')
            (game / 'dvdroot_ps4' / 'map').mkdir()
            (game / 'dvdroot_ps4' / 'map' / 'm.dcx').write_bytes(b'map')
            update = make_game(root / 'CUSA03173-patch')
            view = root / 'coop-game' / 'CUSA03173'
            coop.build_game_view(game, update, view)
            self.assertEqual(coop.game_version(view), ('CUSA03173', '01.09'))
            self.assertEqual((view / 'eboot.bin').read_bytes(), b'eboot 01.09')
            self.assertEqual((view / 'dvdroot_ps4' / 'map' / 'm.dcx').read_bytes(), b'map')
            # Built again (another game): only the links go, never the game behind them.
            coop.build_game_view(game, None, view)
            self.assertEqual(coop.game_version(view), ('CUSA03173', '01.00'))
            self.assertTrue((game / 'dvdroot_ps4' / 'map' / 'm.dcx').is_file())
            self.assertTrue((update / 'eboot.bin').is_file())
            coop.remove_view(view)
            self.assertFalse(view.exists())
            self.assertTrue((game / 'eboot.bin').is_file())


class EventFilesTests(unittest.TestCase):
    def test_made_files_are_compared_with_the_tested_ones(self):
        reference = {'files': {'a.emevd.dcx': {'output_sha256': '1'}, 'b.emevd.dcx': {'output_sha256': '2'}}}
        made = {'files': {'a.emevd.dcx': {'output_sha256': '1'}, 'b.emevd.dcx': {'output_sha256': '9'},
                          'c.emevd.dcx': {'output_sha256': '3'}}}
        self.assertEqual(coop.event_differences(made, reference), ['b.emevd.dcx', 'c.emevd.dcx'])
        self.assertEqual(coop.event_differences(reference, reference), [])


class PlayTests(unittest.TestCase):
    def test_the_game_starts_as_2_play_started_it(self):
        install = Path('C:/Co-op')
        settings = dict(coop.DEFAULTS, role='guest', host_ip='26.1.2.3', account='Lisara', password='pw',
                        harder='3,1.5,3')
        env = coop.game_environment({'BB_FPS': 'uncap', 'BB_FPS_LIMIT': '240', 'KEEP': '1',
                                     'SHADPS4_BLOODBORNE_PARTY': 'old:x', 'SHADPS4_BLOODBORNE_PARTY_ROLE': 'host'},
                                    settings, install, Path('C:/Co-op/coop-game'))
        self.assertEqual(env['KEEP'], '1')
        self.assertEqual(env['BB_FPS'], '60')
        self.assertNotIn('BB_FPS_LIMIT', env)
        self.assertEqual(env['BB_ONLINE'], '1')
        self.assertEqual(env['BB_GAME_DIR'], str(Path('C:/Co-op/coop-game/CUSA03173')))
        self.assertEqual(env['BB_USER_DIR'], str(install / 'user'))
        # Phase 11, one port: the game signs in on the link's UDP port.
        self.assertEqual(env['BB_SHADNET_SERVER'], '26.1.2.3:31314')
        self.assertEqual(env['BB_SHADNET_WEBAPI'], 'http://26.1.2.3:31315')
        self.assertEqual((env['BB_SHADNET_NPID'], env['BB_SHADNET_PASSWORD']), ('Lisara', 'pw'))
        self.assertEqual(env['SHADPS4_BLOODBORNE_SEAMLESS_COOP'], '1')
        # The party is the team made in the game: no party or role setting reaches it.
        self.assertNotIn('SHADPS4_BLOODBORNE_PARTY', env)
        self.assertNotIn('SHADPS4_BLOODBORNE_PARTY_ROLE', env)
        self.assertEqual(env['SHADPS4_BLOODBORNE_HARDER_ENEMIES'], '3,1.5,3')
        self.assertNotIn('SHADPS4_P2P_PORT', env)
        env = coop.game_environment({}, dict(settings, harder='off', p2p_port='51700'), install, install)
        self.assertNotIn('SHADPS4_BLOODBORNE_HARDER_ENEMIES', env)
        self.assertEqual(env['SHADPS4_P2P_PORT'], '51700')

    def test_this_pc_hosts_when_the_host_ip_is_its_own(self):
        """No Player/Host switch (Kaniel 2026-10-16): PLAY starts the server when the host's IP is this PC's."""
        import socket
        own = [ip for ip in socket.gethostbyname_ex(socket.gethostname())[2] if not ip.startswith('127.')]
        if own:
            self.assertTrue(coop.is_this_pc(own[0]), own[0])
        self.assertFalse(coop.is_this_pc('192.0.2.77'), "a documentation address is nobody's PC")
        self.assertFalse(coop.is_this_pc(''))
        self.assertFalse(coop.is_this_pc('not an ip'))

    def test_the_host_overrides_point_the_game_at_the_host(self):
        self.assertEqual(json.loads(coop.host_overrides('26.1.2.3')),
                         {'https://ss4.scej-network.jp:20443': 'http://26.1.2.3:20443'})

    def test_a_crash_goes_to_the_host_with_its_logs(self):
        # Phase 11: after a crash, the next PLAY sends the dump and the crashed run's logs (the
        # .prev ones, which the start just made) to the host's server, once each.
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder)
            for name in ('shad_log.prev.txt', 'console.prev.txt', 'shad_log.txt'):
                (log / name).write_text('log', encoding='utf-8')
            self.assertEqual(coop.reports_to_send(log), [], 'no crash: nothing')
            (log / 'crash-20261015-174001.dmp').write_bytes(b'MDMP')
            self.assertEqual([path.name for path in coop.reports_to_send(log)],
                             ['crash-20261015-174001.dmp', 'console.prev.txt', 'shad_log.prev.txt'])
            coop.note_reports_sent(log, coop.reports_to_send(log))
            self.assertEqual(coop.reports_to_send(log), [], 'sent once')
            (log / 'crash-20261016-090000.dmp').write_bytes(b'MDMP')
            self.assertEqual([path.name for path in coop.reports_to_send(log)][0], 'crash-20261016-090000.dmp')
        request = coop.report_request('26.1.2.3', Path('crash-1.dmp'), b'MDMP', 'p0123', 'secret')
        self.assertEqual(request.full_url, 'http://26.1.2.3:31315/bb/report/crash-1.dmp')
        self.assertEqual((request.get_header('X-player'), request.get_header('X-secret')), ('p0123', 'secret'))
        self.assertEqual(request.get_method(), 'POST')

    def test_the_server_runs_under_its_watchdog(self):
        # Phase 11: shadnet --watchdog keeps the co-op server running (it starts it again after a
        # crash or a hang); stopping stops the watchdog first, so it starts nothing again.
        install = Path('C:/Co-op')
        self.assertEqual(coop.server_command(install), [str(install / 'server' / 'shadnet.exe'), '--watchdog'])
        found = [(11, 'shadnet.exe', 'shadnet.exe --beat'), (12, 'python.exe', 'bb_dummy_server.py'),
                 (10, 'shadnet.exe', 'shadnet.exe --watchdog')]
        self.assertEqual([pid for pid, _name, _command in coop.stop_order(found)], [10, 11, 12])

    def test_nothing_registers_the_account(self):
        # Phase 11: the host's server makes a player's account at its first sign-in.
        self.assertFalse(hasattr(coop, 'register'))
        self.assertFalse(hasattr(coop, 'register_command'))

    def test_the_firewall_rules_cover_the_game_and_for_the_host_the_servers(self):
        install = Path('C:/Co-op')
        guest = coop.firewall_commands(install, host=False)
        self.assertEqual(len(guest), 1)
        self.assertIn(str(install / 'bin' / 'bb-probe.exe'), guest[0])
        host = coop.firewall_commands(install, host=True)
        self.assertEqual(len(host), 3)
        # Phase 11: the game's HTTP and one UDP port; shadNet's TCP port stays on the host's PC.
        self.assertTrue(any('localport=31315,20443 ' in line for line in host))
        self.assertTrue(any('protocol=UDP' in line and 'localport=31314 ' in line for line in host))
        self.assertFalse(any('31313' in line for line in host))


if __name__ == '__main__':
    unittest.main()
