#!/usr/bin/env python3
"""Convert staged ship textures to mipmapped BC7/BC5/BC4 DDS assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--color-root", type=Path, required=True)
    parser.add_argument("--vanilla-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--normal-source", choices=("upscaled", "vanilla"),
                        default="upscaled",
                        help="derive normals from final restored color or vanilla art")
    args = parser.parse_args()
    colors = sorted(args.color_root.rglob("*.tga"))
    masks = sorted(args.color_root.rglob("*_teamEffect*.png"))
    records = []
    total = len(colors) + len(masks)
    jobs = [("color", path) for path in colors] + [("mask", path) for path in masks]

    def convert(job):
        kind, source = job
        if kind == "color":
            color = source
            relative = color.relative_to(args.color_root)
            vanilla = (args.vanilla_root / relative).with_suffix(".png")
            output = (args.output / relative).with_suffix(".dds")
            normal = output.with_name(output.stem + "_normal.dds")
            output.parent.mkdir(parents=True, exist_ok=True)
            if args.overwrite or not output.is_file() or not normal.is_file():
                if args.normal_source == "upscaled":
                    command = (str(args.converter), "colorfull", str(color),
                               str(output), str(normal))
                else:
                    command = (str(args.converter), "color", str(color),
                               str(vanilla), str(output), str(normal))
                result = subprocess.run(command)
                if result.returncode:
                    raise RuntimeError(f"converter failed ({result.returncode}): {relative}")
            return [
                {"kind": "color-bc7", "source": relative.as_posix(),
                 "output": output.relative_to(args.output).as_posix(),
                 "bytes": output.stat().st_size, "sha256": digest(output)},
                {"kind": "normal-bc5", "source": relative.as_posix(),
                 "normal_source": args.normal_source,
                 "output": normal.relative_to(args.output).as_posix(),
                 "bytes": normal.stat().st_size, "sha256": digest(normal)},
            ]
        mask = source
        relative = mask.relative_to(args.color_root)
        output = (args.output / relative).with_suffix(".dds")
        output.parent.mkdir(parents=True, exist_ok=True)
        if args.overwrite or not output.is_file():
            result = subprocess.run((str(args.converter), "mask", str(mask), str(output)))
            if result.returncode:
                raise RuntimeError(f"converter failed ({result.returncode}): {relative}")
        return [{"kind": "mask-bc4", "source": relative.as_posix(),
                 "output": output.relative_to(args.output).as_posix(),
                 "bytes": output.stat().st_size, "sha256": digest(output)}]

    complete = 0
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        for result_records in pool.map(convert, jobs):
            records.extend(result_records)
            complete += 1
            if complete % 100 == 0:
                print(f"converted {complete}/{total}", flush=True)

    manifest = args.output / "ship-dds-manifest.json"
    manifest.write_text(json.dumps(records, indent=2), encoding="utf-8")
    print(json.dumps({"source_jobs": total, "dds_files": len(records),
                      "bytes": sum(item["bytes"] for item in records),
                      "manifest": str(manifest)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
