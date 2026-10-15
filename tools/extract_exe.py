#!/usr/bin/env python3
"""Generate src/data/*.c from the user's ALIFE.EXE.

ALIFE.EXE carries texts and number tables the port needs. Those are
compiled in from the generated sources in src/data/, which are committed.
Image data is not: the built-in font's glyphs (and the PE resources:
cursors, the icon) stay in the user's EXE, which the port loads at runtime
(gfx/exe_res.c) like the other game data files. Run this script by hand to
regenerate src/data/; the build never runs it.

    tools/extract_exe.py [orig/ALIFE.EXE] [-o src/data]

Only the Windows 95 release (docs/binaries.md) is accepted: the script
checks the size and SHA-256 below before writing anything.

What it writes:
  exe_image.c      the initialised data of .rdata and .data by virtual
                   address (exe_res_va in src/gfx/exe_res.c), minus the
                   image regions (IMAGE_REGIONS below), which are listed by
                   address only and copied from the EXE at runtime. Texts
                   are string literals, tables byte strings; runs of zero
                   bytes are left out (the image is zero-filled).
  credits_script.c the credits of the about box. In the EXE they are code,
                   not data: about_credits_restart (0x4addfb) is a long run
                   of credits_add calls with two rand_range switches. This
                   decodes that code into a small op list (ui/about.c runs
                   it) so that no machine code is copied.

Every region of .rdata/.data is classified: image data (by symbol and
extent, below) or text / number tables. A scan for palettes (VGA or
RGBQUAD) and BITMAPINFOHEADERs outside the image regions makes the script
stop, so new image data can't slip in unclassified. There are no palettes
in .rdata/.data of this EXE; the game's palettes come from the bundles.

Stdlib only.
"""
import argparse
import hashlib
import os
import re
import struct
import sys

EXE_SIZE = 1254400
EXE_SHA256 = "6f034f66c28c06f66e553baee90c9d5d91a98de54399309457b65e05df9e0842"

DATA_SECTIONS = (b".rdata", b".data")

# The credits code (about_credits_restart, 0x4addfb): where the script
# starts (after the font slot setup and the list reset), the two functions
# it calls and the globals holding its three font slots (small1, small2, big).
CREDITS_START = 0x4ADEC8
CREDITS_END = 0x4B1007
CREDITS_ADD = 0x4AD624
RAND_RANGE = 0x42B088
FONT_VARS = (0x522E14, 0x522E10, 0x522BD8)

# Image data in .rdata/.data: (symbol, start). The extent is computed.
FONT_BUILTIN = 0x673A10  # font_builtin: the built-in bitmap font (CHAR layout, docs/rendering.md)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Longest literal per piece (C11 only promises 4095 characters).
MAX_PIECE = 4000


class Pe:
    def __init__(self, img):
        self.img = img
        pe = struct.unpack_from("<I", img, 0x3C)[0]
        if img[pe:pe + 4] != b"PE\0\0":
            sys.exit("not a PE image")
        nsect, = struct.unpack_from("<H", img, pe + 6)
        optsize, = struct.unpack_from("<H", img, pe + 20)
        self.base, = struct.unpack_from("<I", img, pe + 24 + 28)
        self.rsrc_rva, = struct.unpack_from("<I", img, pe + 24 + 96 + 2 * 8)
        self.sections = []
        for i in range(nsect):
            o = pe + 24 + optsize + 40 * i
            name, vsize, va, raw_size, raw = struct.unpack_from("<8sIIII", img, o)
            self.sections.append((name.rstrip(b"\0"), va, vsize, raw_size, raw))

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        sys.exit("no section %r" % name)

    def rva_off(self, rva):
        for _, va, _, raw_size, raw in self.sections:
            if va <= rva < va + raw_size:
                return raw + rva - va
        raise KeyError(hex(rva))

    def va_off(self, va):
        return self.rva_off(va - self.base)

    def u8(self, va):
        return self.img[self.va_off(va)]

    def u32(self, va):
        return struct.unpack_from("<I", self.img, self.va_off(va))[0]

    def cstr(self, va):
        o = self.va_off(va)
        return self.img[o:self.img.index(b"\0", o)]


# ---- image regions -------------------------------------------------------------


def font_extent(pe, va):
    """End of a CHAR font at va: header, glyph table and the last glyph."""
    o = pe.va_off(va)
    img = pe.img
    bpp = img[o + 0x15]
    end = o + 0x19 + 256 * 4
    for g in range(256):
        off, = struct.unpack_from("<I", img, o + 0x19 + 4 * g)
        if off:
            p = o + 0x15 + off
            w, h = img[p], img[p + 1]
            end = max(end, p + 4 + (w * h * bpp + 7) // 8)
    return va + end - o


def image_regions(pe):
    return [(FONT_BUILTIN, font_extent(pe, FONT_BUILTIN) - FONT_BUILTIN, "font_builtin: the built-in font's glyphs")]


def check_no_other_images(pe, regions):
    """Stops if something looks like a palette or a DIB outside the regions."""
    def inside(va, n):
        return any(r <= va and va + n <= r + rl for r, rl, _ in regions)

    for name in DATA_SECTIONS:
        _, va, _, raw_size, raw = pe.section(name)
        b = pe.img[raw:raw + raw_size]
        sva = pe.base + va
        for i in range(0, max(0, len(b) - 768), 16):
            w = b[i:i + 768]
            if (not inside(sva + i, 768) and all(c < 64 for c in w)
                    and sum(1 for c in w if c) > 600 and len(set(w)) > 40):
                sys.exit("possible VGA palette at 0x%x: classify it (IMAGE_REGIONS)" % (sva + i))
        for i in range(0, max(0, len(b) - 1024), 4):
            w = b[i:i + 1024]
            if (not inside(sva + i, 1024) and sum(1 for c in w if c) > 500
                    and all(w[k] == 0 for k in range(3, 1024, 4))):
                sys.exit("possible RGBQUAD palette at 0x%x: classify it (IMAGE_REGIONS)" % (sva + i))
        for i in range(0, len(b) - 40, 4):
            if b[i:i + 4] == b"\x28\0\0\0":
                w, h, planes, bits = struct.unpack_from("<iiHH", b, i + 4)
                if (0 < w <= 1024 and 0 < abs(h) <= 1024 and planes == 1 and bits in (1, 4, 8, 24)
                        and not inside(sva + i, 40)):
                    sys.exit("possible BITMAPINFOHEADER at 0x%x: classify it (IMAGE_REGIONS)" % (sva + i))


# ---- C literals -----------------------------------------------------------------


def text_literal(b):
    """Readable C string literal for text bytes (Windows-1252)."""
    out = []
    prev = None
    for c in b:
        if c == 0x22:
            out.append('\\"')
        elif c == 0x5C:
            out.append("\\\\")
        elif c == 0x3F and prev == 0x3F:
            out.append("\\?")  # no trigraphs
        elif c == 0x0A:
            out.append("\\n")
        elif c == 0x0D:
            out.append("\\r")
        elif c == 0x09:
            out.append("\\t")
        elif 0x20 <= c < 0x7F:
            out.append(chr(c))
        else:
            out.append("\\%03o" % c)  # fixed width: never runs into the next char
        prev = c
    return '"' + "".join(out) + '"'


def hex_literal(b):
    return '"' + "".join("\\x%02x" % c for c in b) + '"'


def is_textish(c):
    return 0x20 <= c < 0x7F or c in (0x09, 0x0A, 0x0D) or c >= 0x80


def text_run(b, i):
    """End (exclusive, before the NUL) of a text string starting at i, or None."""
    j = i
    while j < len(b) and is_textish(b[j]):
        j += 1
    if j >= len(b) or b[j] != 0 or j - i < 4:
        return None
    s = b[i:j]
    plain = sum(1 for c in s if c < 0x80 and (chr(c).isalnum() or c == 0x20))
    if plain * 2 < len(s) or not any(c < 0x80 and chr(c).isalpha() for c in s):
        return None
    return j


def pieces(b):
    """Splits section bytes into ('text'|'bin', start, bytes) runs, skipping zeros."""
    out = []
    i, n = 0, len(b)
    while i < n:
        if b[i] == 0:
            i += 1
            continue
        j = text_run(b, i)
        if j is not None:
            s = b[i:j + 1]  # with the NUL
            while len(s) > MAX_PIECE:
                out.append(("text", i, s[:MAX_PIECE]))
                i += MAX_PIECE
                s = s[MAX_PIECE:]
            out.append(("text", i, s))
            i = j + 1
            continue
        # binary up to the next text string or 8 zero bytes
        k = i
        while k < n and k - i < MAX_PIECE:
            if b[k] == 0 and b[k:k + 8] == b"\0" * min(8, n - k):
                break
            if k > i and b[k - 1] == 0 and text_run(b, k) is not None:
                break
            k += 1
        chunk = b[i:k].rstrip(b"\0")
        out.append(("bin", i, chunk))
        i += len(chunk)
    return out


def load_symbols():
    syms = {}
    path = os.path.join(ROOT, "symbols", "ALIFE.EXE.txt")
    try:
        with open(path) as f:
            for line in f:
                m = re.match(r"\s*0x([0-9a-fA-F]+)\s+data\s+(\w+)", line)
                if m:
                    syms.setdefault(int(m.group(1), 16), m.group(2))
    except OSError:
        pass
    return syms


def header(exe_name, what):
    return ("/*\n"
            " * GENERATED by tools/extract_exe.py from %s\n"
            " * (%d bytes, SHA-256 %s).\n"
            " * Do not edit; regenerate instead.\n"
            " * The data is LucasArts' and not covered by this repository's license.\n"
            " *\n"
            " * %s\n"
            " */\n"
            '#include "data/exe_data.h"\n\n') % (exe_name, EXE_SIZE, EXE_SHA256, what)


# ---- exe_image.c -------------------------------------------------------------------


def gen_image(pe, exe_name):
    syms = load_symbols()
    regions = image_regions(pe)
    check_no_other_images(pe, regions)
    lines = [header(exe_name, "The texts and number tables of ALIFE.EXE's .rdata and .data sections\n"
                    " * by virtual address (gfx/exe_res.c builds the image from these pieces;\n"
                    " * the gaps are zero bytes). Image data is not here: exe_image_regions\n"
                    " * lists it by address only, and gfx/exe_res.c copies it from the user's\n"
                    " * ALIFE.EXE at runtime. Comments name the data symbols of\n"
                    " * symbols/ALIFE.EXE.txt.")]
    sect_defs = []
    total = 0
    for name in DATA_SECTIONS:
        _, va, _, raw_size, raw = pe.section(name)
        b = bytearray(pe.img[raw:raw + raw_size])
        sva = pe.base + va
        for r, rl, _ in regions:
            if sva <= r and r + rl <= sva + raw_size:
                b[r - sva:r - sva + rl] = bytes(rl)
        b = bytes(b)
        ident = "k_" + name.decode().strip(".")
        lines.append("static const ExePiece %s[] = {\n" % ident)
        sym_list = sorted(a for a in syms if sva <= a < sva + raw_size
                          and not any(r <= a < r + rl for r, rl, _ in regions))
        si = 0
        count = 0
        for kind, start, data in pieces(b):
            pva = sva + start
            names = []
            while si < len(sym_list) and sym_list[si] < pva + len(data):
                names.append("0x%x %s" % (sym_list[si], syms[sym_list[si]]))
                si += 1
            if names:
                lines.append("    /* %s */\n" % ", ".join(names))
            if kind == "text":
                body = data[:-1] if data.endswith(b"\0") else data
                lines.append("    {0x%x, %d, %s},\n" % (pva, len(data), text_literal(body)))
            else:
                lines.append("    {0x%x, %d,\n" % (pva, len(data)))
                for o in range(0, len(data), 16):
                    lines.append("     %s%s\n" % (hex_literal(data[o:o + 16]), "}," if o + 16 >= len(data) else ""))
            count += 1
            total += len(data)
        lines.append("};\n\n")
        sect_defs.append("    {0x%x, 0x%x, %s, %d},\n" % (sva, raw_size, ident, count))
    lines.append("const ExeSection exe_sections[EXE_SECTIONS] = {\n")
    lines.extend(sect_defs)
    lines.append("};\n\n")
    lines.append("/* Image data, read from the user's ALIFE.EXE at runtime. */\n")
    lines.append("const ExeRegion exe_image_regions[] = {\n")
    for r, rl, what in regions:
        lines.append("    {0x%x, %d}, /* %s */\n" % (r, rl, what))
    lines.append("};\nconst size_t exe_n_image_regions = sizeof exe_image_regions / sizeof exe_image_regions[0];\n")
    return "".join(lines), total


# ---- credits_script.c --------------------------------------------------------------


def decode_credits(pe):
    """Decodes about_credits_restart's code into {va: insn}, following jumps."""
    insns = {}
    work = [CREDITS_START]

    def s8(v):
        return v - 256 if v >= 128 else v

    while work:
        pc = work.pop()
        while CREDITS_START <= pc < CREDITS_END and pc not in insns:
            op = pe.u8(pc)
            b1 = pe.u8(pc + 1)
            if op == 0x6A:
                ins, n = ("push", s8(b1)), 2
            elif op == 0x68:
                ins, n = ("push", pe.u32(pc + 1)), 5
            elif op == 0x50:
                ins, n = ("push_eax",), 1
            elif op == 0xA1:
                var = pe.u32(pc + 1)
                if var not in FONT_VARS:
                    sys.exit("credits: unknown global %x at %x" % (var, pc))
                ins, n = ("font", FONT_VARS.index(var)), 5
            elif op == 0xE8:
                ins, n = ("call", (pc + 5 + pe.u32(pc + 1)) & 0xFFFFFFFF), 5
            elif op == 0x83 and b1 == 0xC4:
                ins, n = ("add_esp", pe.u8(pc + 2)), 3
            elif op == 0x83 and b1 == 0x7D:
                ins, n = ("cmp", pe.u8(pc + 2), s8(pe.u8(pc + 3)) & 0xFFFFFFFF), 4
            elif op == 0x89 and b1 == 0x45:
                ins, n = ("store", pe.u8(pc + 2)), 3
            elif op == 0x8B and b1 == 0x45:
                ins, n = ("load", pe.u8(pc + 2)), 3
            elif op == 0x0F and b1 == 0x87:
                t = (pc + 6 + pe.u32(pc + 2)) & 0xFFFFFFFF
                ins, n = ("ja", t), 6
                work.append(t)
            elif op == 0xE9:
                ins, n = ("jmp", (pc + 5 + pe.u32(pc + 1)) & 0xFFFFFFFF), 5
            elif op == 0xEB:
                ins, n = ("jmp", pc + 2 + s8(b1)), 2
            elif op == 0xFF and b1 == 0x24 and pe.u8(pc + 2) == 0x85:
                ins, n = ("jtab", pe.u32(pc + 3)), 7
            else:
                ins, n = ("end",), 1  # pop edi: the epilogue
            insns[pc] = (ins, n)
            if ins[0] == "end":
                break
            if ins[0] == "jmp":
                work.append(ins[1])
                break
            if ins[0] == "jtab":
                # the cmp before it bounds the index
                cmp = [v for a, v in insns.items() if v[0][0] == "cmp" and a < pc and a + 13 >= pc]
                if not cmp:
                    sys.exit("credits: jump table without a bound at %x" % pc)
                cases = cmp[0][0][2] + 1
                for k in range(cases):
                    work.append(pe.u32(ins[1] + 4 * k))
                insns[pc] = (("jtab", ins[1], [pe.u32(ins[1] + 4 * k) for k in range(cases)]), n)
                break
            pc += n
    return insns


def credits_ops(pe):
    """Turns the instructions into ops: (va, dict)."""
    insns = decode_credits(pe)
    addrs = sorted(insns)
    ops = []
    stack = []  # (value, is_font)
    group = None
    i = 0
    while i < len(addrs):
        a = addrs[i]
        ins, n = insns[a]
        if group is None:
            group = a
        k = ins[0]
        if k == "push":
            stack.append((ins[1], None))
        elif k == "font":
            nxt = insns[addrs[i + 1]][0]
            if nxt[0] != "push_eax":
                sys.exit("credits: font slot not pushed at %x" % a)
            stack.append((0, ins[1]))
            i += 1
        elif k == "call" and ins[1] == CREDITS_ADD:
            if len(stack) != 6:
                sys.exit("credits: credits_add with %d args at %x" % (len(stack), a))
            kind, text, x, y, arg, flags = [v for v in reversed(stack)]
            for v, f in (kind, text, x, y, flags):
                if f is not None:
                    sys.exit("credits: font slot in an odd argument at %x" % a)
            op = {"op": "CREDITS_ADD", "kind": kind[0], "x": x[0], "y": y[0], "flags": flags[0]}
            if arg[1] is not None:
                op["font"] = arg[1]
            else:
                op["arg"] = arg[0]
            if kind[0] == 2:
                op["text"] = pe.cstr(text[0]) if text[0] else None
            ops.append((group, op))
        elif k == "call" and ins[1] == RAND_RANGE:
            if len(stack) != 2:
                sys.exit("credits: rand_range with %d args at %x" % (len(stack), a))
            st = insns[addrs[i + 2]][0] if insns[addrs[i + 1]][0][0] == "add_esp" else None
            if not st or st[0] != "store":
                sys.exit("credits: rand_range result not stored at %x" % a)
            ops.append((group, {"op": "CREDITS_RAND", "var": st[1], "lo": stack[1][0], "hi": stack[0][0]}))
            stack = []
            group = None
            i += 3
            continue
        elif k == "call":
            sys.exit("credits: call to %x at %x" % (ins[1], a))
        elif k == "add_esp":
            del stack[max(0, len(stack) - ins[1] // 4):]
            group = None
        elif k == "cmp":
            ja, load, jtab = (insns[addrs[i + d]][0] for d in (1, 2, 3))
            if ja[0] != "ja" or load != ("load", ins[1]) or jtab[0] != "jtab":
                sys.exit("credits: odd switch at %x" % a)
            ops.append((group, {"op": "CREDITS_SWITCH", "var": ins[1], "cases": jtab[2], "target": ja[1]}))
            group = None
            i += 4
            continue
        elif k == "jmp":
            ops.append((group, {"op": "CREDITS_GOTO", "target": ins[1]}))
            group = None
        elif k == "end":
            ops.append((group, {"op": "CREDITS_END"}))
            group = None
        else:
            sys.exit("credits: unexpected %s at %x" % (k, a))
        # an op boundary must fall through to the next decoded instruction
        if group is None and k not in ("jmp", "end") and i + 1 < len(addrs) and addrs[i + 1] != a + n:
            sys.exit("credits: gap after %x" % a)
        i += 1
    if stack:
        sys.exit("credits: arguments left on the stack")
    return ops


def gen_credits(pe, exe_name):
    ops = credits_ops(pe)
    index = {va: k for k, (va, _) in enumerate(ops)}

    def target(va):
        if va not in index:
            sys.exit("credits: jump into an op at %x" % va)
        return index[va]

    lines = [header(exe_name, "The about box's credits (ui/about.c), decoded by the generator from\n"
                    " * the code of about_credits_restart (0x4addfb): credits_add calls with\n"
                    " * their arguments in order, and the two rand_range switches that pick\n"
                    " * one of several lines. Targets are op indexes.")]
    lines.append("const CreditsOp credits_script[] = {\n")
    for k, (va, op) in enumerate(ops):
        f = [".op = %s" % op["op"]]
        if op["op"] == "CREDITS_ADD":
            f.append(".kind = %d" % op["kind"])
            if "text" in op:
                f.append(".text = %s" % (text_literal(op["text"]) if op["text"] is not None else "NULL"))
            f.append(".x = %d" % op["x"])
            f.append(".y = %d" % op["y"])
            if "font" in op:
                f.append(".font = %d" % op["font"])
            else:
                f.append(".font = -1, .arg = %d" % struct.unpack("<i", struct.pack("<I", op["arg"] & 0xFFFFFFFF))[0])
            f.append(".flags = 0x%x" % op["flags"])
        elif op["op"] == "CREDITS_RAND":
            f.append(".var = %d, .lo = %d, .hi = %d" % (op["var"], op["lo"], op["hi"]))
        elif op["op"] == "CREDITS_SWITCH":
            f.append(".var = %d, .target = %d, .n_cases = %d" % (op["var"], target(op["target"]), len(op["cases"])))
            f.append(".cases = {%s}" % ", ".join(str(target(c)) for c in op["cases"]))
        elif op["op"] == "CREDITS_GOTO":
            f.append(".target = %d" % target(op["target"]))
        lines.append("    /* %d: 0x%x */ {%s},\n" % (k, va, ", ".join(f)))
    lines.append("};\nconst size_t credits_script_len = sizeof credits_script / sizeof credits_script[0];\n")
    if max((len(op.get("cases", ())) for _, op in ops), default=0) > 8:
        sys.exit("credits: more than 8 cases (CreditsOp.cases)")
    return "".join(lines), len(ops)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", nargs="?", default=os.path.join(ROOT, "orig", "ALIFE.EXE"))
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "src", "data"))
    args = ap.parse_args()
    with open(args.exe, "rb") as f:
        img = f.read()
    sha = hashlib.sha256(img).hexdigest()
    if len(img) != EXE_SIZE or sha != EXE_SHA256:
        sys.exit("%s: expected %d bytes with SHA-256 %s, got %d bytes, %s"
                 % (args.exe, EXE_SIZE, EXE_SHA256, len(img), sha))
    pe = Pe(img)
    name = "ALIFE.EXE (Windows 95 release)"
    os.makedirs(args.out, exist_ok=True)
    for fname, (text, n) in (("exe_image.c", gen_image(pe, name)),
                             ("credits_script.c", gen_credits(pe, name))):
        with open(os.path.join(args.out, fname), "w", newline="\n") as f:
            f.write(text)
        unit = "ops" if fname == "credits_script.c" else "bytes"
        print("%s: %d %s, %d bytes of source" % (fname, n, unit, len(text)))


if __name__ == "__main__":
    main()
