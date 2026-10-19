# SPDX-License-Identifier: GPL-2.0-or-later
"""The port's menu texts in every language the game has.

The sources keep each text as an English and a Russian pair (T("…", "…"), MenuText, AddText,
AddGameText, the Effects table, en[] / ru[] arrays). This tool finds those pairs, and the other
languages' texts live in gpu/shim/menu_texts/<code>.json (English text -> translation). It writes
gpu/shim/bbport_menu_texts.inc, the table the port looks texts up in.

  python3 scripts/menu_texts.py            write the table
  python3 scripts/menu_texts.py --check    fail when the table is stale or a language misses texts
  python3 scripts/menu_texts.py --missing  list the texts each language lacks (JSON)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHIM = ROOT / 'gpu/shim'
SOURCES = ['bbport_overlay.cpp', 'bbport_game_menu.cpp', 'bbport_trade_menu.cpp', 'bbport_settings.h']
TEXTS_DIR = SHIM / 'menu_texts'
TABLE = SHIM / 'bbport_menu_texts.inc'

# The game's languages past English and Russian, in BbSettings::MenuLanguage order: code, the
# language's own name, the PS4 system language ids that pick it (sceSystemServiceParamGetInt 1).
LANGUAGES = [
    ('ja', '日本語', [0]),
    ('fr', 'Français', [2]),
    ('es', 'Español', [3]),
    ('es_419', 'Español (Latinoamérica)', [20]),
    ('de', 'Deutsch', [4]),
    ('it', 'Italiano', [5]),
    ('nl', 'Nederlands', [6]),
    ('pt', 'Português', [7]),
    ('ko', '한국어', [9]),
    ('zh_TW', '繁體中文', [10]),
    ('zh_CN', '简体中文', [11]),
    ('fi', 'Suomi', [12]),
    ('sv', 'Svenska', [13]),
    ('da', 'Dansk', [14]),
    ('no', 'Norsk', [15]),
    ('pl', 'Polski', [16]),
    ('pt_BR', 'Português (Brasil)', [17]),
    ('tr', 'Türkçe', [19]),
]

_TOKEN = re.compile(r'''
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>(?:u8)?"(?:[^"\\\n]|\\.)*")
  | (?P<char>'(?:[^'\\\n]|\\.)+')
  | (?P<name>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<punct>\S)
''', re.S | re.X)

_ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'", '0': '\0', 'r': '\r'}


def _unescape(literal):
    body = literal[literal.index('"') + 1:-1]
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt == 'x':
                digits = re.match(r'[0-9a-fA-F]+', body[i + 2:]).group(0)
                out.append(chr(int(digits, 16)))
                i += 2 + len(digits)
                continue
            if nxt == 'u':
                out.append(chr(int(body[i + 2:i + 6], 16)))
                i += 6
                continue
            out.append(_ESCAPES.get(nxt, nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def _tokens(source):
    """(kind, value) tokens with comments dropped and adjacent string literals joined."""
    out = []
    for m in _TOKEN.finditer(source):
        kind = m.lastgroup
        if kind == 'comment':
            continue
        value = m.group(kind)
        if kind == 'string':
            value = _unescape(value)
            if out and out[-1][0] == 'string':
                out[-1] = ('string', out[-1][1] + value)
                continue
        out.append((kind, value))
    return out


def _cyrillic(text):
    return any('Ѐ' <= c < 'Ԁ' for c in text)


def _array_literals(tokens, at):
    """The string literals of the braced list starting at tokens[at] ('{'), if it holds only those."""
    items, i = [], at + 1
    while i < len(tokens):
        kind, value = tokens[i]
        if kind == 'string':
            items.append(value)
        elif value == '}':
            return items, i
        elif value != ',':
            return None, i
        i += 1
    return None, i


def pairs_in(source):
    """[(english, russian)] in the order they appear in one source file."""
    tokens = _tokens(source)
    found = []
    # Lists of texts only (name_en[] = {...}): paired place by place below.
    lists, in_list = [], set()
    for i, (kind, value) in enumerate(tokens):
        if value == '{' and i >= 2 and tokens[i - 1][1] == '=':
            items, end = _array_literals(tokens, i)
            if items:
                lists.append(items)
                in_list.update(range(i, end + 1))
    # "english", "russian" next to each other: calls and table rows (not two places of one list:
    # the Russian text ends the call or the row, or what follows it is not Russian).
    for i in range(len(tokens) - 3):
        a, comma, b, after = tokens[i], tokens[i + 1], tokens[i + 2], tokens[i + 3]
        following = tokens[i + 4] if i + 4 < len(tokens) else ('', '')
        ends = after[1] in (')', '}') or (after[1] == ',' and not (following[0] == 'string' and _cyrillic(following[1])))
        if a[0] == 'string' and comma[1] == ',' and b[0] == 'string' and _cyrillic(b[1]) \
                and not _cyrillic(a[1]) and a[1].strip() and ends and i not in in_list:
            found.append((a[1], b[1]))
    # name_en[] = {...}; name_ru[] = {...}: the same places of two lists.
    for first, second in zip(lists, lists[1:]):
        if len(first) == len(second) and not any(map(_cyrillic, first)) and any(map(_cyrillic, second)):
            found.extend((e, r) for e, r in zip(first, second) if e != r and e.strip())
    return found


def english_texts():
    """Every English text of the menus, once each, in source order: {english: russian}."""
    texts = {}
    for name in SOURCES:
        for english, russian in pairs_in((SHIM / name).read_text(encoding='utf-8')):
            texts.setdefault(english, russian)
    return texts


def load_language(code):
    path = TEXTS_DIR / f'{code}.json'
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding='utf-8'))


_FORMAT = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcspn%]')


def format_specs(text):
    return [m.group(0) for m in _FORMAT.finditer(text)]


# Kaniel 2026-10-19: a button is named as {L1} and drawn as the game's icon (Ui::Keys::Names).
BUTTONS = ('Cross', 'Circle', 'Square', 'Triangle', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'Options', 'TouchLeft',
           'TouchRight', 'Up', 'Down', 'LeftRight', 'UpDown', 'Dpad', 'LS', 'RS')
_BUTTON = re.compile(r'\{(' + '|'.join(BUTTONS) + r')\}')


def button_names(text):
    """The {Button} names in a text, sorted: every translation keeps the same ones."""
    return sorted(m.group(0) for m in _BUTTON.finditer(text))


def translation_problem(english, translated):
    """What is wrong with a text's translation, or None."""
    if not translated:
        return 'missing'
    if format_specs(translated) != format_specs(english):
        return f'format {format_specs(translated)} != {format_specs(english)}'
    if button_names(translated) != button_names(english):
        return f'buttons {button_names(translated)} != {button_names(english)}'
    return None


def problems(texts=None):
    """What keeps the languages from matching the sources: [(code, english, problem)]."""
    texts = english_texts() if texts is None else texts
    out = []
    for code, _, _ in LANGUAGES:
        table = load_language(code)
        for english in texts:
            problem = translation_problem(english, table.get(english))
            if problem:
                out.append((code, english, problem))
        for english in table:
            if english not in texts:
                out.append((code, english, 'not in the sources'))
    return out


def _c_string(text):
    out = []
    for c in text:
        if c == '\\':
            out.append('\\\\')
        elif c == '"':
            out.append('\\"')
        elif c == '\n':
            out.append('\\n')
        elif ord(c) < 0x20:
            out.append(f'\\x{ord(c):02x}""')
        else:
            out.append(c)
    return '"' + ''.join(out) + '"'


def render(texts=None):
    texts = english_texts() if texts is None else texts
    tables = {code: load_language(code) for code, _, _ in LANGUAGES}
    lines = [
        '// SPDX-License-Identifier: GPL-2.0-or-later',
        '// Written by scripts/menu_texts.py from gpu/shim/menu_texts/*.json: do not edit.',
        '// BB_MENU_LANG(enum name, code, own name, PS4 language ids...) and',
        '// BB_MENU_TEXT(english, {one translation per language above, nullptr when missing}).',
        '#ifdef BB_MENU_LANG',
    ]
    for code, name, ids in LANGUAGES:
        enum = ''.join(part.capitalize() for part in code.split('_'))
        lines.append(f'BB_MENU_LANG({enum}, "{code}", {_c_string(name)}, {", ".join(map(str, ids))})')
    lines += ['#endif', '#ifdef BB_MENU_TEXT']
    for english in texts:
        row = [tables[code].get(english) for code, _, _ in LANGUAGES]
        cells = ',\n             '.join(_c_string(t) if t else 'nullptr' for t in row)
        lines.append(f'BB_MENU_TEXT({_c_string(english)},\n            {{{cells}}})')
    lines += ['#endif', '']
    return '\n'.join(lines)


def main(argv):
    if '--missing' in argv:
        texts = english_texts()
        report = {}
        for code, english, problem in problems(texts):
            report.setdefault(code, []).append({'english': english, 'russian': texts.get(english),
                                                'problem': problem})
        print(json.dumps(report, ensure_ascii=False, indent=1))
        return 0
    table = render()
    if '--check' in argv:
        bad = problems()
        for code, english, problem in bad[:40]:
            print(f'{code}: {problem}: {english!r}')
        stale = not TABLE.exists() or TABLE.read_text(encoding='utf-8') != table
        if stale:
            print(f'{TABLE.name} is stale: run scripts/menu_texts.py')
        return 1 if bad or stale else 0
    TABLE.write_bytes(table.encode('utf-8'))
    print(f'wrote {TABLE} ({len(english_texts())} texts, {len(LANGUAGES)} languages)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
