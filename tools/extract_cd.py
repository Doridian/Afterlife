#!/usr/bin/env python3
"""Extract the files from an Afterlife CD image (BIN/CUE or ISO) into a directory.

Pure Python, no external tools needed. Handles single-track MODE1/2352 and
MODE1/2048 images, which is what the retail Afterlife CD is.

    tools/extract_cd.py Afterlife.cue orig/
"""
import argparse
import hashlib
import os
import re
import struct
import sys

SECTOR = 2048


class Image:
    def __init__(self, path, raw_sector, data_offset):
        self.f = open(path, "rb")
        self.raw_sector = raw_sector
        self.data_offset = data_offset

    def read_sectors(self, lba, count):
        if self.raw_sector == SECTOR:
            self.f.seek(lba * SECTOR)
            return self.f.read(count * SECTOR)
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i) * self.raw_sector + self.data_offset)
            out += self.f.read(SECTOR)
        return bytes(out)

    def read_extent(self, lba, size):
        return self.read_sectors(lba, (size + SECTOR - 1) // SECTOR)[:size]


def open_image(path):
    if path.lower().endswith(".cue"):
        cue = open(path, encoding="latin-1").read()
        m = re.search(r'FILE\s+"([^"]+)"', cue)
        if not m:
            sys.exit("cue sheet has no FILE entry")
        bin_path = os.path.join(os.path.dirname(path), m.group(1))
        tracks = re.findall(r"TRACK\s+(\d+)\s+(\S+)", cue)
        if not tracks or tracks[0][1].upper() not in ("MODE1/2352", "MODE1/2048"):
            sys.exit(f"unsupported track layout: {tracks}")
        if tracks[0][1].upper() == "MODE1/2048":
            return Image(bin_path, SECTOR, 0)
        return Image(bin_path, 2352, 16)
    size = os.path.getsize(path)
    if size % 2352 == 0 and size % SECTOR != 0:
        return Image(path, 2352, 16)
    return Image(path, SECTOR, 0)


def walk(img, lba, size, prefix, out):
    data = img.read_extent(lba, size)
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:
            # Records never straddle sectors; skip to the next one.
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        ext_lba = struct.unpack_from("<I", data, pos + 2)[0]
        ext_size = struct.unpack_from("<I", data, pos + 10)[0]
        flags = data[pos + 25]
        name_len = data[pos + 32]
        name = data[pos + 33 : pos + 33 + name_len]
        pos += rec_len
        if name in (b"\x00", b"\x01"):
            continue
        name = name.decode("ascii").split(";")[0].rstrip(".")
        rel = os.path.join(prefix, name)
        if flags & 2:
            walk(img, ext_lba, ext_size, rel, out)
        else:
            out.append((rel, ext_lba, ext_size))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help=".cue, .bin or .iso")
    ap.add_argument("outdir")
    args = ap.parse_args()

    img = open_image(args.image)
    pvd = img.read_sectors(16, 1)
    if pvd[1:6] != b"CD001" or pvd[0] != 1:
        sys.exit("no ISO9660 primary volume descriptor found")
    print("volume:", pvd[40:72].decode("ascii").strip())
    root = pvd[156:190]
    files = []
    walk(img, struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "", files)

    for rel, lba, size in sorted(files):
        dst = os.path.join(args.outdir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        h = hashlib.sha256()
        with open(dst, "wb") as f:
            remaining, cur = size, lba
            while remaining:
                n = min(remaining, 512 * SECTOR)
                chunk = img.read_extent(cur, n)
                f.write(chunk)
                h.update(chunk)
                remaining -= n
                cur += n // SECTOR
        print(f"{h.hexdigest()}  {rel}")


if __name__ == "__main__":
    main()
