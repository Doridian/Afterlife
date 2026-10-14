#!/usr/bin/env python3
"""List or dump resources from Afterlife .000 resource bundles (ALIFE/FULL/MEDIUM.000).

The container is an IFF variant with little-endian tags and lengths ("JIFF").
See docs/formats/jiff.md for what is known so far.

    tools/jiff.py list orig/ALIFE.000
    tools/jiff.py stats orig/ALIFE.000
    tools/jiff.py verify orig/ALIFE.000
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
            # Each resource is its own "RESC" chunk (whose length field counts
            # its own 8-byte header). OFST points at the chunk body: u32
            # compression type (always 2 = stored), u32 body size including
            # this 16-byte header, 8 zero bytes, then the payload.
            self.f.seek(pos - 8)
            ctag, clen, flags, size = struct.unpack("<4sIII", self.f.read(16))
            reserved = self.f.read(8)
            yield {
                "index": i,
                "id": self.ids[i],
                "pos": pos,
                "chunk": tag(ctag),
                "chunk_len": clen,
                "offset": pos + 16,
                "flags": flags,
                "reserved": reserved,
                "length": size - 16,
                "type": sniff(self.f.read(16)),
            }

    def read(self, entry):
        self.f.seek(entry["offset"])
        return self.f.read(entry["length"])


def self_size(d, length):
    """Size the payload claims for itself, or None if the format has no size field."""
    if d[:2] == b"BM":
        n = struct.unpack("<I", d[2:6])[0]
        return n + 14 if n + 14 == length else n  # some BMPs omit the 14-byte file header
    if d[4:6] in (b"\x11\xaf", b"\x12\xaf"):
        return struct.unpack("<I", d[:4])[0]
    if d[:4] == b"TILE":
        return struct.unpack("<I", d[4:8])[0] + 8
    if d[:4] == b"AKOS":
        return struct.unpack(">I", d[4:8])[0]
    return None


def verify(b):
    """Check the RESC chain and that payloads with their own size field are complete."""
    ok = True
    entries = sorted(b.entries(), key=lambda e: e["pos"])
    end = b.f.seek(0, 2)
    for k, e in enumerate(entries):
        nxt = entries[k + 1]["pos"] - 8 if k + 1 < len(entries) else end
        problems = []
        if e["chunk"] != "RESC" or e["chunk_len"] != e["length"] + 24:
            problems.append(f"chunk {e['chunk']!r} len {e['chunk_len']}")
        if e["offset"] + e["length"] != nxt:
            problems.append(f"gap {nxt - e['offset'] - e['length']}")
        if e["flags"] != 2 or any(e["reserved"]):
            problems.append(f"type {e['flags']:#x} reserved {e['reserved'].hex()}")
        claimed = self_size(b.read(e)[:16], e["length"])
        if claimed is not None and claimed != e["length"]:
            problems.append(f"payload says {claimed} bytes, entry has {e['length']}")
        if problems:
            ok = False
            print(f"id {e['id']}: " + "; ".join(problems))
    print(f"{len(entries)} resources, {'OK' if ok else 'problems found'}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["list", "stats", "verify", "dump"])
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
        flags = collections.Counter()
        for e in b.entries():
            by_type[e["type"]] += 1
            size[e["type"]] += e["length"]
            flags[e["flags"], e["reserved"].hex()] += 1
        print(f"{b.count} resources")
        for t, n in by_type.most_common():
            print(f"  {t:4}  {n:5}  {size[t]:>12,} bytes")
        print("compression type / reserved bytes:")
        for (f, r), n in flags.most_common():
            print(f"  {f:#06x}  {r}  {n:5}")
    elif args.command == "verify":
        sys.exit(0 if verify(b) else 1)
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
