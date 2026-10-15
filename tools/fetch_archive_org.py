#!/usr/bin/env python3
"""Download the Afterlife CD from archive.org and extract it into a directory.

Fetches the Redump BIN/CUE of the US release from
https://archive.org/details/AfterlifeUSA, checks it, and extracts the CD's
files with extract_cd.py. Pure Python, no external tools needed.

    tools/fetch_archive_org.py orig/
    tools/fetch_archive_org.py --zip "Afterlife (USA).zip" orig/
"""
import argparse
import hashlib
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_cd import extract  # noqa: E402

URL = "https://archive.org/download/AfterlifeUSA/Afterlife%20(USA).zip"
ZIP_SHA1 = "51575fba016c6200b2f531d07cc0d38e248a44e2"
CUE = "Afterlife (USA).cue"

# docs/binaries.md
EXPECTED = {
    "ALIFE.EXE": "6f034f66c28c06f66e553baee90c9d5d91a98de54399309457b65e05df9e0842",
    "ALIFE.000": "1b4c059e9795f51852d8ac35a088c06f1ca42a878068ce32203300ed2f85719c",
    "FULL.000": "9155d0f6b76669247c35206987496496db6b8335d17a35c925b9bfce0ceba003",
    "MEDIUM.000": "ba509afc76e4e0748cac2591643f17a4c3956785d43a3fa0b9c82451c79058d0",
}


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def download(dst):
    print(f"downloading {URL}", file=sys.stderr)
    with urllib.request.urlopen(URL) as r, open(dst, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while chunk := r.read(1 << 20):
            f.write(chunk)
            done += len(chunk)
            if total and sys.stderr.isatty():
                print(f"\r{done >> 20}/{total >> 20} MB", end="", file=sys.stderr)
    if sys.stderr.isatty():
        print(file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--zip", help="use this already downloaded zip instead of downloading it")
    ap.add_argument("--keep-zip", metavar="PATH", help="save the downloaded zip here")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        zip_path = args.zip
        if not zip_path:
            zip_path = os.path.join(tmp, "afterlife.zip")
            download(zip_path)
        if sha1(zip_path) != ZIP_SHA1:
            sys.exit(f"{zip_path}: SHA-1 mismatch, expected {ZIP_SHA1}")
        if args.keep_zip and not args.zip:
            shutil.copyfile(zip_path, args.keep_zip)

        with zipfile.ZipFile(zip_path) as z:
            z.extractall(tmp)
        hashes = extract(os.path.join(tmp, CUE), args.outdir)

    bad = [name for name, want in EXPECTED.items() if hashes.get(name) != want]
    if bad:
        sys.exit(f"unexpected contents: {', '.join(bad)} don't match docs/binaries.md")


if __name__ == "__main__":
    main()
