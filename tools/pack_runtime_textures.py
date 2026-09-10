#!/usr/bin/env python3
"""Build HomeworldTextures.hwt: a sorted, random-access DDS container."""
from __future__ import annotations
import argparse
import pathlib
import struct

MAGIC = b"HWTPACK1"
HEADER = struct.Struct("<8sIIQ")
ENTRY = struct.Struct("<256sQII")
ROOTS = ("r1", "r2", "p1", "p2", "p3", "traders", "asteroids")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    files = []
    for root_name in ROOTS:
        folder = args.root / root_name
        if not folder.exists():
            folder = args.root / root_name.upper()
        if not folder.exists():
            continue
        for path in folder.rglob("*.dds"):
            key = path.relative_to(args.root).as_posix().replace("/", "\\").lower()
            encoded = key.encode("utf-8")
            if len(encoded) >= 256:
                raise ValueError(f"archive path too long: {key}")
            files.append((key, encoded, path, path.stat().st_size))
    files.sort(key=lambda item: item[0])
    if len({item[0] for item in files}) != len(files):
        raise ValueError("case-insensitive duplicate archive paths")
    data_offset = HEADER.size + ENTRY.size * len(files)
    offset = data_offset
    entries = []
    for _, encoded, path, size in files:
        entries.append((encoded, offset, size, path))
        offset += size
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(HEADER.pack(MAGIC, 1, len(entries), data_offset))
        for encoded, position, size, _ in entries:
            stream.write(ENTRY.pack(encoded.ljust(256, b"\0"), position, size, 0))
        for _, _, _, path in entries:
            with path.open("rb") as source:
                while chunk := source.read(8 * 1024 * 1024):
                    stream.write(chunk)
    temporary.replace(args.output)
    print(f"packed {len(entries)} DDS files, {args.output.stat().st_size / 2**20:.1f} MiB")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
