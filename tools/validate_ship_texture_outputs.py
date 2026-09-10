#!/usr/bin/env python3
"""Validate every output referenced by a ship-texture pipeline manifest."""

import argparse
import json
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    jobs = json.loads(args.manifest.read_text(encoding="utf-8"))
    failures = []
    formats = {}
    for job in jobs:
        output = Path(job["output"])
        try:
            with Image.open(output) as image:
                image.verify()
            with Image.open(output) as image:
                actual = image.size
                expected = (job["target_width"], job["target_height"])
                if actual != expected:
                    failures.append(f"{job['relative']}: {actual} != {expected}")
                if job["category"] == "albedo" and actual != (job["width"] * 4, job["height"] * 4):
                    failures.append(f"{job['relative']}: albedo is not exactly 4x vanilla")
                if job["category"] == "mask" and actual != (job["width"], job["height"]):
                    failures.append(f"{job['relative']}: technical mask changed dimensions")
                formats[image.format] = formats.get(image.format, 0) + 1
        except Exception as error:
            failures.append(f"{job['relative']}: {error}")
    summary = {"jobs": len(jobs), "valid": len(jobs) - len(failures),
               "failed": len(failures), "formats": formats,
               "examples": failures[:20]}
    print(json.dumps(summary, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
