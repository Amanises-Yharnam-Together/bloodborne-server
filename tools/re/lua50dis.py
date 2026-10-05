"""Disassembles the Lua 5.0 event scripts in Bloodborne's script/eventcommon.luabnd.dcx.

01.09 never loads these scripts: it runs a native copy compiled from them (state names such as
"PartyGhostDeath_wait" in the eboot are the Lua function names). The bytecode is still the
easiest way to read what that native code does before finding it in the eboot.

Usage: py -3.12 lua50dis.py <eventcommon.luabnd.dcx> [function-name ...]
"""
import re
import struct
import sys
import zlib

OPS = ("MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE "
       "NEWTABLE SELF ADD SUB MUL DIV POW UNM NOT CONCAT JMP EQ LT LE TEST CALL TAILCALL RETURN "
       "FORLOOP TFORLOOP TFORPREP SETLIST SETLISTO CLOSE CLOSURE").split()
HEADER = bytes.fromhex("1b4c7561500104080406080909 08b6099368e7f57d41".replace(" ", ""))


def bnd4_files(raw):
    count = struct.unpack_from("<i", raw, 0x0C)[0]
    for i in range(count):
        entry = 0x40 + i * 0x24
        size, offset, _, name_offset = struct.unpack_from("<QIiI", raw, entry + 0x10)
        end = raw.index(b"\0\0", name_offset)
        end += (end - name_offset) % 2
        yield raw[name_offset:end].decode("utf-16le"), raw[offset:offset + size]


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, len(HEADER)

    def unpack(self, fmt):
        value = struct.unpack_from(fmt, self.data, self.pos)[0]
        self.pos += struct.calcsize(fmt)
        return value

    def string(self):
        size = self.unpack("<Q")
        value = self.data[self.pos:self.pos + size - 1].decode("cp932", "replace") if size else None
        self.pos += size
        return value

    def function(self):
        f = {"source": self.string(), "line": self.unpack("<i")}
        f["nups"], f["params"], f["vararg"], f["stack"] = (self.unpack("B") for _ in range(4))
        lines = self.unpack("<i")
        self.pos += 4 * lines
        for _ in range(self.unpack("<i")):  # locals
            self.string()
            self.pos += 8
        for _ in range(self.unpack("<i")):  # upvalue names
            self.string()
        f["k"] = []
        for _ in range(self.unpack("<i")):
            kind = self.unpack("B")
            f["k"].append(self.unpack("<d") if kind == 3 else self.string() if kind == 4 else None)
        f["p"] = [self.function() for _ in range(self.unpack("<i"))]
        f["code"] = [self.unpack("<I") for _ in range(self.unpack("<i"))]
        return f


def rk(f, x):
    return repr(f["k"][x - 250]) if x >= 250 else f"R{x}"


def listing(f):
    for pc, i in enumerate(f["code"]):
        op, a, b, c, bx = OPS[i & 63], i >> 24, (i >> 15) & 511, (i >> 6) & 511, (i >> 6) & 0x3FFFF
        if op in ("LOADK", "GETGLOBAL", "SETGLOBAL"):
            text = f"{op} R{a} {f['k'][bx]!r}"
        elif op in ("JMP", "FORLOOP", "TFORPREP"):
            text = f"{op} R{a} ->{pc + 1 + bx - 131071}"
        elif op == "CLOSURE":
            text = f"CLOSURE R{a} proto{bx}"
        elif op in ("GETTABLE", "SELF"):
            text = f"{op} R{a} R{b} {rk(f, c)}"
        elif op in ("SETTABLE", "ADD", "SUB", "MUL", "DIV", "POW", "EQ", "LT", "LE"):
            text = f"{op} {a} {rk(f, b)} {rk(f, c)}"
        else:
            text = f"{op} {a} {b} {c}"
        yield f"  [{pc:3}] {text}"


def named_protos(main):
    for pc, i in enumerate(main["code"]):
        if i & 63 == 34 and pc + 1 < len(main["code"]) and main["code"][pc + 1] & 63 == 7:
            yield main["k"][(main["code"][pc + 1] >> 6) & 0x3FFFF], main["p"][(i >> 6) & 0x3FFFF]


def main(path, wanted):
    data = open(path, "rb").read()
    raw = zlib.decompress(data[0x4C:])
    for name, chunk in bnd4_files(raw):
        if not chunk.startswith(HEADER):
            continue
        print(f"== {name}")
        for fname, proto in named_protos(Reader(chunk).function()):
            if not wanted or fname in wanted or any(re.fullmatch(w, fname) for w in wanted):
                print(f"function {fname} (line {proto['line']})")
                print("\n".join(listing(proto)))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main(sys.argv[1], sys.argv[2:])
