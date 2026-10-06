"""Dumps Bloodborne talk scripts (64-bit ESD "fsSL") from a script/talk/*.talkesdbnd.dcx.

Lamps are the ESDs whose debug strings say 地上子ワープ椅子 ("warp chair"): ESD function 52 is
"in multiplayer", command 11 sets an event flag (a lamp's travel dialog sets 7MMM01LL).

    py -3.12 tools/re/esdump.py <talkesdbnd.dcx> [esd name substring]
"""
import struct
import sys
import zlib

BASE = 0x6C  # offsets in an ESD are relative to the end of its header
OPERATORS = {0x8C: '+', 0x8E: '-', 0x8F: '*', 0x90: '/', 0x91: '<=', 0x92: '>=', 0x93: '<',
             0x94: '>', 0x95: '==', 0x96: '!=', 0x98: '&&', 0x99: '||'}


def dcx(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'DCX\0', path
    return zlib.decompress(data[data.find(b'DCA\0') + 8:])


def bnd4(data):
    assert data[:4] == b'BND4'
    count = struct.unpack_from('<i', data, 0x0C)[0]
    entry_size = struct.unpack_from('<q', data, 0x20)[0]
    for index in range(count):
        entry = 0x40 + index * entry_size
        size = struct.unpack_from('<q', data, entry + 8)[0]
        offset, _, name_offset = struct.unpack_from('<iii', data, entry + 24)
        end = name_offset
        while data[end:end + 2] != b'\0\0':
            end += 2
        yield data[name_offset:end].decode('utf-16le'), data[offset:offset + size]


def expression(code):
    stack = []
    pop = lambda: stack.pop() if stack else '?'
    i = 0
    while i < len(code):
        op = code[i]
        i += 1
        if op <= 0x7F:
            stack.append(str(op - 0x40))
        elif op == 0x80:
            stack.append(repr(struct.unpack_from('<f', code, i)[0]))
            i += 4
        elif op == 0x81:
            stack.append(repr(struct.unpack_from('<d', code, i)[0]))
            i += 8
        elif op == 0x82:
            stack.append(str(struct.unpack_from('<i', code, i)[0]))
            i += 4
        elif 0x84 <= op <= 0x8A:
            args = [pop() for _ in range(op - 0x84)][::-1]
            stack.append(f'f{pop()}({", ".join(args)})')
        elif op in OPERATORS:
            right, left = pop(), pop()
            stack.append(f'({left} {OPERATORS[op]} {right})')
        elif op == 0xA1:
            break
        elif op == 0xA5:
            end = i
            while code[end:end + 2] != b'\0\0':
                end += 2
            stack.append('"' + code[i:end].decode('utf-16le', 'replace') + '"')
            i = end + 2
        elif op == 0xB8:
            stack.append(f'arg{pop()}')
        elif op == 0xB9:
            stack.append('result')
        else:
            stack.append(f'<{op:02x}>')
    return ' ; '.join(stack)


def dump(esd):
    q = lambda offset, count=1: struct.unpack_from(f'<{count}q', esd, BASE + offset)

    def commands(offset, count):
        out = []
        for k in range(count):
            bank, command, args, arg_count = struct.unpack_from('<iiqq', esd, BASE + offset + k * 0x18)
            text = []
            for j in range(arg_count):
                at, size = q(args + j * 16, 2)
                text.append(expression(esd[BASE + at:BASE + at + size]))
            out.append(f'c{bank}_{command}({", ".join(text)})')
        return out

    def conditions(offset, count, indent):
        for k in range(count):
            at = q(offset + k * 8)[0]
            target, pass_at, pass_count, sub_at, sub_count, code_at, code_size = q(at, 7)
            state = q(target)[0] if target >= 0 else None
            line = f'{indent}if {expression(esd[BASE + code_at:BASE + code_at + code_size])} -> {state}'
            if pass_count:
                line += f' do {commands(pass_at, pass_count)}'
            print(line)
            conditions(sub_at, sub_count, indent + '   ')

    for group in range(q(0x20)[0]):
        group_id, states_at, state_count, _ = q(0x48 + group * 0x20, 4)
        print(f'== group {group_id & 0xFFFFFFFF:#x} states={state_count}')
        for k in range(state_count):
            state_id, cond_at, cond_count, entry_at, entry_count, exit_at, exit_count, while_at, \
                while_count = q(states_at + k * 0x48, 9)
            print(f' state {state_id}: entry={commands(entry_at, entry_count)} '
                  f'exit={commands(exit_at, exit_count)} while={commands(while_at, while_count)}')
            conditions(cond_at, cond_count, '    ')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    wanted = sys.argv[2] if len(sys.argv) > 2 else ''
    for name, esd in bnd4(dcx(sys.argv[1])):
        if wanted in name:
            print(f'##### {name}')
            dump(esd)
