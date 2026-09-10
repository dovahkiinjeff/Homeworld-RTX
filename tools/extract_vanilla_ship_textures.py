#!/usr/bin/env python3
"""Extract and decode legally owned HW1 ship LIFs from an RBF1.23 archive."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath

from PIL import Image

CLASSIC = struct.Struct("<IIH2xIIIIb3x")
REMASTER = struct.Struct("<IIH2xIIIIIb3x")
LIF_HEADER = struct.Struct("<8s10I")
SHIP_ROOTS = {"r1", "r2", "p1", "p2", "p3", "traders"}


@dataclass
class Entry:
    name_length: int
    stored_length: int
    real_length: int
    offset: int
    compression: int
    name: str = ""


def decrypt_name(data: bytes) -> str:
    mask = 213
    output = bytearray()
    for value in data:
        decoded = value ^ mask
        output.append(decoded)
        mask = decoded
    return output.decode("latin-1")


def read_bits(data: bytes):
    for value in data:
        for shift in range(7, -1, -1):
            yield (value >> shift) & 1


def lzss_expand(data: bytes, expected: int) -> bytes:
    bits = iter(read_bits(data))

    def take(count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | next(bits)
        return value

    window = bytearray(4096)
    position = 1
    output = bytearray()
    try:
        while True:
            if take(1):
                value = take(8)
                output.append(value)
                window[position] = value
                position = (position + 1) & 4095
            else:
                match = take(12)
                if match == 0:
                    break
                length_code = take(4)
                # C decoder adds BREAK_EVEN (1), then copies inclusively.
                for index in range(length_code + 2):
                    value = window[(match + index) & 4095]
                    output.append(value)
                    window[position] = value
                    position = (position + 1) & 4095
    except StopIteration as error:
        raise ValueError("truncated LZSS stream") from error
    if len(output) != expected:
        raise ValueError(f"LZSS expanded to {len(output)} bytes, expected {expected}")
    return bytes(output)


def entry_from(values: tuple[int, ...], remastered: bool) -> Entry:
    if remastered:
        _crc1, _crc2, name_len, stored, real, offset, _stamp, _unknown, compression = values
    else:
        _crc1, _crc2, name_len, stored, real, offset, _stamp, compression = values
    return Entry(name_len, stored, real, offset, compression)


def score_layout(blob: bytes, count: int, parser: struct.Struct) -> int:
    table_start = 15
    table_end = table_start + count * parser.size
    if table_end > len(blob):
        return -1
    score = 0
    samples = min(count, 32)
    for sample in range(samples):
        index = 0 if samples == 1 else sample * (count - 1) // (samples - 1)
        values = parser.unpack_from(blob, table_start + index * parser.size)
        entry = entry_from(values, parser is REMASTER)
        data_start = entry.offset + entry.name_length + 1
        valid = (0 < entry.name_length <= 128 and entry.compression in (0, 1)
                 and entry.offset >= table_end and data_start + entry.stored_length <= len(blob)
                 and (entry.compression or entry.stored_length == entry.real_length))
        score += int(valid)
    return score


def read_archive(path: Path) -> tuple[bytes, list[Entry]]:
    blob = path.read_bytes()
    if blob[:7] != b"RBF1.23":
        raise ValueError("not an RBF1.23 Homeworld archive")
    count, _flags = struct.unpack_from("<II", blob, 7)
    classic_score = score_layout(blob, count, CLASSIC)
    remaster_score = score_layout(blob, count, REMASTER)
    parser = REMASTER if remaster_score > classic_score else CLASSIC
    if max(classic_score, remaster_score) <= 0:
        raise ValueError("could not detect BIG table layout")
    entries = []
    for index in range(count):
        values = parser.unpack_from(blob, 15 + index * parser.size)
        entry = entry_from(values, parser is REMASTER)
        raw_name = blob[entry.offset:entry.offset + entry.name_length]
        entry.name = decrypt_name(raw_name)
        entries.append(entry)
    return blob, entries


def is_ship_lif(name: str) -> bool:
    path = PureWindowsPath(name)
    parts = [part.lower() for part in path.parts]
    return (len(parts) >= 4 and parts[0] in SHIP_ROOTS
            and parts[1] != "mothership" and any(p.startswith("lod") for p in parts)
            and path.suffix.lower() == ".lif")


def entry_bytes(blob: bytes, entry: Entry) -> bytes:
    start = entry.offset + entry.name_length + 1
    stored = blob[start:start + entry.stored_length]
    return lzss_expand(stored, entry.real_length) if entry.compression else stored


def decode_lif(data: bytes) -> tuple[Image.Image, Image.Image | None, Image.Image | None, dict]:
    if len(data) < LIF_HEADER.size:
        raise ValueError("short LIF")
    ident, version, flags, width, height, palette_crc, image_crc, data_off, palette_off, team0_off, team1_off = LIF_HEADER.unpack_from(data)
    if ident.rstrip(b"\0") != b"Willy 7" or version != 0x104 or width <= 0 or height <= 0:
        raise ValueError("invalid LIF header")
    pixels = width * height
    paletted = bool(flags & 0x2)
    has_alpha = bool(flags & 0x8)
    if paletted:
        indices = data[data_off:data_off + pixels]
        palette = data[palette_off:palette_off + 1024]
        if len(indices) != pixels or len(palette) != 1024:
            raise ValueError("truncated paletted LIF")
        rgba = bytearray()
        for index in indices:
            rgba.extend(palette[index * 4:index * 4 + 4])
        image = Image.frombytes("RGBA", (width, height), bytes(rgba))
        effect_length = 256
        effect_indices = indices
    else:
        channels = 4 if has_alpha else 4
        raw = data[data_off:data_off + pixels * channels]
        if len(raw) != pixels * channels:
            raise ValueError("truncated true-color LIF")
        image = Image.frombytes("RGBA", (width, height), raw)
        effect_length = pixels
        effect_indices = None

    def effect(offset: int) -> Image.Image | None:
        if not offset:
            return None
        raw = data[offset:offset + effect_length]
        if len(raw) != effect_length:
            return None
        if effect_indices is not None:
            raw = bytes(raw[index] for index in effect_indices)
        return Image.frombytes("L", (width, height), raw)

    metadata = {"width": width, "height": height, "flags": flags,
                "paletted": paletted, "alpha": has_alpha,
                "palette_crc": palette_crc, "image_crc": image_crc}
    return image, effect(team0_off), effect(team1_off), metadata


def safe_relative(name: str) -> Path:
    parts = [part for part in PureWindowsPath(name).parts if part not in ("", ".", "..")]
    if not parts or any(":" in part for part in parts):
        raise ValueError(f"unsafe archive path: {name}")
    return Path(*parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    blob, entries = read_archive(args.archive)
    selected = [entry for entry in entries if is_ship_lif(entry.name)]
    manifest = []
    for entry in selected:
        relative = safe_relative(entry.name)
        destination = args.output / relative.with_suffix(".png")
        destination.parent.mkdir(parents=True, exist_ok=True)
        try:
            image, team0, team1, metadata = decode_lif(entry_bytes(blob, entry))
            image.save(destination, optimize=True)
            if team0 is not None and metadata["flags"] & 0x10:
                team0.save(destination.with_name(destination.stem + "_teamEffect0.png"), optimize=True)
            if team1 is not None and metadata["flags"] & 0x20:
                team1.save(destination.with_name(destination.stem + "_teamEffect1.png"), optimize=True)
            manifest.append({"source": entry.name, "png": destination.relative_to(args.output).as_posix(), **metadata})
        except Exception as error:
            manifest.append({"source": entry.name, "error": str(error)})
    manifest_path = args.output / "vanilla-ship-extraction.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    failures = sum("error" in item for item in manifest)
    print(json.dumps({"archive_entries": len(entries), "ship_lifs": len(selected),
                      "decoded": len(selected) - failures, "failed": failures,
                      "manifest": str(manifest_path)}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
