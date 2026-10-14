#!/usr/bin/env python3
"""Compare tests/compbun_dump output with tools/jiff.py's reader.

    compare_jiff.py <bundle.000> <dump dir>

Each <index>_<id>.bin must equal the payload jiff.py reads for the *first*
entry with that ID (the game looks IDs up linearly, so a duplicated ID
resolves to its first entry). Also reports any payload starting with 'COMP',
which the Compression Layer would treat as compressed.
"""
import importlib.util
import os
import sys

spec = importlib.util.spec_from_file_location(
    "jiff", os.path.join(os.path.dirname(__file__), "..", "tools", "jiff.py"))
jiff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jiff)


def main():
    bundle, dump = sys.argv[1], sys.argv[2]
    b = jiff.Bundle(bundle)
    entries = list(b.entries())
    first = {}
    for e in entries:
        first.setdefault(e["id"], e)
    bad = comp = 0
    for e in entries:
        want = b.read(first[e["id"]])
        if want[:4] == b"COMP":
            comp += 1
        path = os.path.join(dump, f"{e['index']:04}_{e['id']}.bin")
        try:
            with open(path, "rb") as f:
                got = f.read()
        except OSError:
            got = None
        if got != want:
            bad += 1
            print(f"MISMATCH index {e['index']} id {e['id']}")
    dups = len(entries) - len(first)
    print(f"{bundle}: {len(entries)} entries, {bad} mismatches, {dups} duplicate ids, {comp} start with COMP")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
