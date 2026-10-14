#!/usr/bin/env python3
"""List or dump resources from Afterlife .000 resource bundles (ALIFE/FULL/MEDIUM.000).

The container is an IFF variant with little-endian tags and lengths ("JIFF").
See docs/formats/jiff.md for what is known so far.

    tools/jiff.py list orig/ALIFE.000
    tools/jiff.py stats orig/ALIFE.000
    tools/jiff.py dump orig/ALIFE.000 out/
"""
import argparse
import collections
import os
import struct
import sys


def tag(b):
    return b[::-1].decode("latin-1")


def sniff(head):
    if head[:2] == b"BM":
        return "bmp"
    if head[4:6] in (b"\x11\xaf", b"\x12\xaf"):
        return "flc" if head[4] == 0x12 else "fli"
    if head[:4] == b"\x00\x00\x02\x00":
        return "cur"
    if head[:4] == b"ALSV":
        return "alsv"
    if head[:4] == b"CHAP":
        return "txt"
    if head[:4].isalpha() and head[:4].isupper() or head[:4] == b"iMUS":
        return head[:4].decode("ascii").lower()
    return "bin"


class Bundle:
    def __init__(self, path):
        self.f = open(path, "rb")
        hdr = self.f.read(8)
        if tag(hdr[:4]) != "JIFF":
            sys.exit(f"{path}: not a JIFF file")
        chunks = {}
        pos = 8
        while True:
            self.f.seek(pos)
            h = self.f.read(8)
            name, length = tag(h[:4]), struct.unpack("<I", h[4:])[0]
            chunks[name] = (pos + 8, length)
            if name == "RESC":
                break
            pos += 8 + length
        self.f.seek(chunks["HEAD"][0])
        (self.count,) = struct.unpack("<I", self.f.read(4))
        self.f.seek(chunks["OFST"][0])
        self.offsets = struct.unpack(f"<{self.count}I", self.f.read(4 * self.count))
        self.f.seek(chunks["RCID"][0])
        self.ids = struct.unpack(f"<{self.count}I", self.f.read(4 * self.count))
        # Resource offsets are relative to the start of the RESC chunk body.
        self.base = chunks["RESC"][0]

    def entries(self):
        for i in range(self.count):
            pos = self.base + self.offsets[i]
            self.f.seek(pos)
            # 16-byte entry header: u32 flags (always 2 so far), u32 length
            # counted from offset 8, then 8 zero bytes. The payload is an
            # embedded file in its own format.
            flags, length = struct.unpack("<II", self.f.read(8))
            self.f.seek(pos + 16)
            yield {
                "index": i,
                "id": self.ids[i],
                "offset": pos + 16,
                "flags": flags,
                "length": length - 8,
                "type": sniff(self.f.read(16)),
            }

    def read(self, entry):
        self.f.seek(entry["offset"])
        return self.f.read(entry["length"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["list", "stats", "dump"])
    ap.add_argument("bundle")
    ap.add_argument("outdir", nargs="?")
    args = ap.parse_args()

    b = Bundle(args.bundle)
    if args.command == "list":
        print("index      id  type  flags      offset     length")
        for e in b.entries():
            print(f"{e['index']:5} {e['id']:7}  {e['type']:4}  {e['flags']:5}  {e['offset']:#10x} {e['length']:10}")
    elif args.command == "stats":
        by_type = collections.Counter()
        size = collections.Counter()
        for e in b.entries():
            by_type[e["type"]] += 1
            size[e["type"]] += e["length"]
        print(f"{b.count} resources")
        for t, n in by_type.most_common():
            print(f"  {t:4}  {n:5}  {size[t]:>12,} bytes")
    else:
        if not args.outdir:
            sys.exit("dump needs an output directory")
        os.makedirs(args.outdir, exist_ok=True)
        for e in b.entries():
            name = f"{e['id']:05}.{e['type']}"
            with open(os.path.join(args.outdir, name), "wb") as f:
                f.write(b.read(e))


if __name__ == "__main__":
    main()
