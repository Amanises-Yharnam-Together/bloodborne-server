"""The port's menu texts in every language the game has (scripts/menu_texts.py): the table the
port builds in is the one the language files make, every language has every text with the same
printf placeholders, and the pairs are found where the sources keep them. With BLOODBORNE_EBOOT
naming the game's eboot.bin, the rows the port adds to the game's own menu are checked against the
game's font of each language: the game draws them, and a letter its font lacks shows as nothing."""
from paths import ROOT
import os
import struct
import unittest
from pathlib import Path

import menu_texts
from menu_texts import LANGUAGES, english_texts, format_specs, pairs_in, problems, render


class TableTests(unittest.TestCase):
    def test_the_committed_table_is_the_written_one(self):
        self.assertEqual(menu_texts.TABLE.read_text(encoding='utf-8'), render(),
                         'run scripts/menu_texts.py')

    def test_every_language_has_every_text(self):
        self.assertEqual(problems(), [])

    def test_the_game_languages(self):
        codes = [code for code, _, _ in LANGUAGES]
        self.assertEqual(len(codes), 18)
        ids = [i for _, _, language_ids in LANGUAGES for i in language_ids]
        self.assertEqual(sorted(ids + [1, 8]), sorted(set(ids + [1, 8])), 'one language per PS4 id')
        self.assertNotIn(18, ids, 'English (UK) is English')

    def test_format_specs(self):
        self.assertEqual(format_specs('%.0f FPS  %.1f ms  %s'), ['%.0f', '%.1f', '%s'])
        self.assertEqual(format_specs('revive %s  %d%%'), ['%s', '%d', '%%'])


class PairTests(unittest.TestCase):
    def test_calls_rows_and_lists(self):
        source = '''
            T("On", "Вкл"); MenuText("Long "
                "text", "Длинный " "текст");
            {"effect_dof", "Depth of field", "Глубина", "Blur (DoF)", "Размытие (DoF)", true},
            static const char* en[] = {"Cross", "L1", "Options", "Touchpad"};
            static const char* ru[] = {"Крест", "L1", "Options", "Тачпад"};
            // T("Commented", "Нет")
        '''
        self.assertEqual(pairs_in(source), [
            ('On', 'Вкл'), ('Long text', 'Длинный текст'), ('Depth of field', 'Глубина'),
            ('Blur (DoF)', 'Размытие (DoF)'), ('Cross', 'Крест'), ('Touchpad', 'Тачпад')])

    def test_the_sources_have_the_menus(self):
        texts = english_texts()
        self.assertGreater(len(texts), 250)
        for english in ('Blood Echoes', 'Language of this menu.', 'Give Blood Echoes', 'Motion blur',
                        'strike to revive %s  %d%%', 'Vitality'):
            self.assertIn(english, texts)
        self.assertNotIn('Options', texts)


def game_folder():
    eboot = os.environ.get('BLOODBORNE_EBOOT')
    if not eboot:
        return None
    for parent in Path(eboot).resolve().parents:
        for candidate in (parent / 'dvdroot_ps4', parent / 'CUSA03173' / 'dvdroot_ps4'):
            if (candidate / 'menu').is_dir():
                return candidate
    return None


def font_codes(gfx):
    """The characters of the first DefineFont3 in a GFX file (as bbport_game_art.cpp reads it)."""
    at = 8 + (5 + 4 * (gfx[8] >> 3) + 7) // 8 + 4
    while at + 2 <= len(gfx):
        code_size = struct.unpack_from('<H', gfx, at)[0]
        at += 2
        size = code_size & 0x3F
        if size == 0x3F:
            size = struct.unpack_from('<I', gfx, at)[0]
            at += 4
        if code_size >> 6 == 75:
            font = gfx[at:at + size]
            flags, p = font[2], 5 + font[4]
            count = struct.unpack_from('<H', font, p)[0]
            p += 2
            wide = flags & 0x08
            last = struct.unpack_from('<I' if wide else '<H', font, p + count * (4 if wide else 2))[0]
            return {chr(c) for c in struct.unpack_from(f'<{count}H', font, p + last)}
        if code_size >> 6 == 0:
            break
        at += size
    return set()


@unittest.skipUnless(game_folder(), 'needs BLOODBORNE_EBOOT next to the game files')
class GameFontTests(unittest.TestCase):
    FOLDERS = {'ja': 'jpnjp', 'fr': 'frafr', 'es': 'spaes', 'es_419': 'spaar', 'de': 'deude', 'it': 'itait',
               'nl': 'nldnl', 'pt': 'porpt', 'ko': 'korkr', 'zh_TW': 'zhotw', 'zh_CN': 'zhocn', 'fi': 'finfi',
               'sv': 'swese', 'da': 'dandk', 'no': 'norno', 'pl': 'polpl', 'pt_BR': 'porbr', 'tr': 'turtr'}

    def test_the_game_menu_rows_are_in_the_game_font(self):
        game_rows = set()
        for name in ('bbport_game_menu.cpp', 'bbport_trade_menu.cpp'):
            game_rows.update(e for e, _ in pairs_in((menu_texts.SHIM / name).read_text(encoding='utf-8')))
        self.assertEqual(set(self.FOLDERS), {code for code, _, _ in LANGUAGES})
        for code, folder in self.FOLDERS.items():
            codes = font_codes((game_folder() / 'menu' / folder / 'font.gfx').read_bytes())
            table = menu_texts.load_language(code)
            lacking = {c for e in game_rows for c in table[e] if c not in codes and not c.isspace()}
            self.assertEqual(lacking, set(), f'{code}: the game font lacks these')


if __name__ == '__main__':
    unittest.main()
