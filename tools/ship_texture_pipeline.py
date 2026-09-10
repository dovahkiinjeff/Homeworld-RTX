#!/usr/bin/env python3
"""Non-destructive, resumable ship-texture upscaling for Homeworld RTX."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Optional

from PIL import Image, ImageChops, ImageStat

IMAGE_EXTENSIONS = {".png", ".tga", ".bmp", ".jpg", ".jpeg"}
SHIP_ROOTS = {"r1", "r2", "p1", "p2", "p3", "traders", "ships", "derelicts"}
NON_COLOR_WORDS = (
    "_normal", "_nrm", "normalmap", "_rough", "roughness", "_metal", "metallic",
    "_spec", "specular", "_ao", "occlusion", "_height", "_bump", "_mask",
    "teameffect", "team_effect", "_alpha", "opacity", "_glow", "_emissive",
    "_lights", "engine_light",
)
NORMAL_WORDS = ("_normal", "_nrm", "normalmap", "_bump")
MASK_WORDS = (
    "teameffect", "team_effect", "_mask", "_alpha", "opacity", "_rough",
    "roughness", "_metal", "metallic", "_spec", "specular", "_ao", "occlusion",
    "_height", "_glow", "_emissive", "_lights", "engine_light",
)


@dataclass
class Job:
    source: str
    relative: str
    output: str
    category: str
    action: str
    backend: str
    width: int
    height: int
    target_width: int
    target_height: int
    mode: str
    has_alpha: bool
    source_sha256: str
    output_sha256: str = ""
    status: str = "planned"
    message: str = ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_ship_path(relative: Path) -> bool:
    parts = [part.lower() for part in relative.parts]
    if not parts or parts[0] not in SHIP_ROOTS:
        return False
    # A real ship texture lives below a ship directory and normally an LOD tree.
    return len(parts) >= 3 and (any(p.startswith("lod") for p in parts) or "ships" in parts)


def category_for(path: Path, image: Image.Image) -> str:
    name = path.stem.lower()
    if any(word in name for word in NORMAL_WORDS):
        return "normal"
    if any(word in name for word in MASK_WORDS):
        return "mask"
    # Paletted/grayscale inputs are data unless their name explicitly says otherwise.
    if image.mode in {"1", "L", "I", "F", "P"}:
        return "mask"
    return "albedo"


def target_size(width: int, height: int, scale: int, max_dimension: int) -> tuple[int, int]:
    factor = min(float(scale), max_dimension / max(width, height))
    factor = max(1.0, factor)
    return max(1, round(width * factor)), max(1, round(height * factor))


def alpha_present(image: Image.Image) -> bool:
    return "A" in image.getbands() or (image.mode == "P" and "transparency" in image.info)


def prepare_ai_color(image: Image.Image, minimum_dimension: int = 64) -> Image.Image:
    """Give very small Homeworld tiles enough context for Real-ESRGAN.

    NCNN inference on the original 2-32 pixel-wide strips can return black or
    chromatic-static frames.  Preconditioning changes neither the selected AI
    model nor its x4/tile settings; the result is resized back to exactly four
    times the vanilla dimensions after inference.
    """
    color = image.convert("RGB")
    shortest = min(color.size)
    if shortest >= minimum_dimension:
        return color
    factor = math.ceil(minimum_dimension / shortest)
    return color.resize((color.width * factor, color.height * factor),
                        Image.Resampling.LANCZOS)


def plan_jobs(source: Path, output: Path, scale: int, max_dimension: int,
              backend: str, output_format: str = "png") -> list[Job]:
    jobs: list[Job] = []
    for item in sorted(source.rglob("*")):
        if not item.is_file() or item.suffix.lower() not in IMAGE_EXTENSIONS:
            continue
        relative = item.relative_to(source)
        if not is_ship_path(relative):
            continue
        with Image.open(item) as image:
            image.load()
            category = category_for(relative, image)
            if category == "mask":
                tw, th = image.width, image.height
            else:
                tw, th = target_size(image.width, image.height, scale, max_dimension)
            selected = backend if category == "albedo" else "pillow"
            if category == "mask":
                action = "copy-native-size"
            else:
                action = "ai-upscale" if category == "albedo" and backend != "pillow" else "deterministic-upscale"
            output_relative = relative
            if category == "albedo" and output_format != "png":
                output_relative = relative.with_suffix("." + output_format)
            jobs.append(Job(
                source=str(item.resolve()), relative=relative.as_posix(),
                output=str((output / output_relative).resolve()), category=category,
                action=action, backend=selected, width=image.width, height=image.height,
                target_width=tw, target_height=th, mode=image.mode,
                has_alpha=alpha_present(image), source_sha256=sha256(item),
            ))
    return jobs


def resize_data_texture(image: Image.Image, size: tuple[int, int], category: str) -> Image.Image:
    # Normals and continuous material maps need smooth interpolation. Discrete team masks
    # use nearest-neighbour so AI never invents ownership or emissive coverage.
    resample = Image.Resampling.BICUBIC if category == "normal" else Image.Resampling.NEAREST
    resized = image.resize(size, resample=resample)
    if category == "normal" and resized.mode in {"RGB", "RGBA"}:
        channels = resized.split()
        rgb = Image.merge("RGB", channels[:3])
        pixels = []
        for r, g, b in rgb.getdata():
            x, y, z = r / 127.5 - 1.0, g / 127.5 - 1.0, b / 127.5 - 1.0
            length = math.sqrt(x*x + y*y + z*z) or 1.0
            pixels.append(tuple(round((v / length + 1.0) * 127.5) for v in (x, y, z)))
        rgb.putdata(pixels)
        if resized.mode == "RGBA":
            rgb.putalpha(channels[3])
        resized = rgb
    return resized


def run_realesrgan(executable: Path, source: Path, destination: Path,
                   target: tuple[int, int], model: str, tile: int) -> None:
    with tempfile.TemporaryDirectory(prefix="hwrtx-upscale-") as temp_name:
        temp = Path(temp_name)
        prepared = temp / "input.png"
        generated = temp / "output.png"
        with Image.open(source) as original:
            original.load()
            rgba = original.convert("RGBA")
            alpha = rgba.getchannel("A")
            prepare_ai_color(rgba).save(prepared)
        command = [str(executable), "-i", str(prepared), "-o", str(generated),
                   "-n", model, "-s", "4", "-t", str(tile), "-f", "png"]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not generated.exists():
            detail = (result.stderr or result.stdout or "unknown Real-ESRGAN error").strip()
            raise RuntimeError(detail)
        with Image.open(generated) as ai_image:
            color = ai_image.convert("RGB").resize(target, Image.Resampling.LANCZOS)
        alpha = alpha.resize(target, Image.Resampling.LANCZOS)
        color.putalpha(alpha)
        destination.parent.mkdir(parents=True, exist_ok=True)
        color.save(destination, optimize=True)


def image_mean_rgb(path: Path) -> float:
    with Image.open(path) as image:
        return sum(ImageStat.Stat(image.convert("RGB")).mean) / 3.0


def run_realesrgan_batch(jobs: list[Job], executable: Path, model: str, tile: int,
                         batch_size: int = 64) -> None:
    if not jobs:
        return
    # realesrgan-ncnn-vulkan can silently emit black frames when handed several
    # thousand files in one directory invocation.  Bounded batches avoid that
    # failure while retaining exactly the same model, scale and tile settings.
    for batch_start in range(0, len(jobs), batch_size):
        batch = jobs[batch_start:batch_start + batch_size]
        with tempfile.TemporaryDirectory(prefix="hwrtx-upscale-batch-") as temp_name:
            temp = Path(temp_name)
            inputs, outputs = temp / "input", temp / "output"
            inputs.mkdir()
            outputs.mkdir()
            indexed = []
            for index, job in enumerate(batch):
                prepared = inputs / f"{index:04d}.png"
                with Image.open(job.source) as original:
                    original.load()
                    prepare_ai_color(original).save(prepared, compress_level=1)
                indexed.append((job, prepared.name, image_mean_rgb(Path(job.source))))
            command = [str(executable), "-i", str(inputs), "-o", str(outputs),
                       "-n", model, "-s", "4", "-t", str(tile), "-f", "png"]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError((result.stderr or result.stdout or "batch Real-ESRGAN failed").strip())
            for job, generated_name, source_mean in indexed:
                generated = outputs / generated_name
                if (generated.is_file() and source_mean > 16.0 and
                        image_mean_rgb(generated) < max(1.0, source_mean * 0.20)):
                    # Retry an isolated frame if the batch triggered the silent
                    # black-output failure.
                    isolated = temp / ("isolated-" + generated_name)
                    command = [str(executable), "-i", str(inputs / generated_name),
                               "-o", str(isolated), "-n", model, "-s", "4",
                               "-t", str(tile), "-f", "png"]
                    retry = subprocess.run(command, capture_output=True, text=True)
                    if retry.returncode == 0 and isolated.is_file():
                        generated = isolated
                if not generated.is_file():
                    job.status, job.message = "failed", "Real-ESRGAN did not produce output"
                    continue
                destination = Path(job.output)
                try:
                    with Image.open(generated) as ai_image:
                        color = ai_image.convert("RGB").resize(
                            (job.target_width, job.target_height), Image.Resampling.LANCZOS)
                    with Image.open(job.source) as original:
                        original.load()
                        if alpha_present(original):
                            alpha = original.convert("RGBA").getchannel("A").resize(
                                (job.target_width, job.target_height), Image.Resampling.LANCZOS)
                            color.putalpha(alpha)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    color.save(destination)
                    job.output_sha256 = sha256(destination)
                    job.status, job.message = "complete", ""
                except Exception as error:
                    job.status, job.message = "failed", str(error)


def process_job(job: Job, realesrgan: Optional[Path], model: str, tile: int,
                previous: Optional[dict] = None) -> Job:
    source, destination = Path(job.source), Path(job.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and previous:
        unchanged = (
            previous.get("source_sha256") == job.source_sha256
            and previous.get("target_width") == job.target_width
            and previous.get("target_height") == job.target_height
            and previous.get("backend") == job.backend
            and previous.get("status") in {"complete", "skipped"}
        )
        if unchanged:
            job.output_sha256 = sha256(destination)
            job.status, job.message = "skipped", "matching output already exists"
            return job
    try:
        size = (job.target_width, job.target_height)
        if job.backend == "realesrgan":
            if not realesrgan or not realesrgan.is_file():
                raise RuntimeError("Real-ESRGAN executable not found; run setup or pass --realesrgan")
            run_realesrgan(realesrgan, source, destination, size, model, tile)
        else:
            with Image.open(source) as image:
                image.load()
                if job.category == "mask":
                    result = image.copy()
                elif job.category == "albedo":
                    result = image.resize(size, Image.Resampling.LANCZOS)
                else:
                    result = resize_data_texture(image, size, job.category)
                result.save(destination, optimize=True)
        job.output_sha256 = sha256(destination)
        job.status, job.message = "complete", ""
    except Exception as error:  # Keep the queue resumable and report every failure.
        job.status, job.message = "failed", str(error)
    return job


def write_manifests(jobs: Iterable[Job], directory: Path) -> None:
    jobs = list(jobs)
    directory.mkdir(parents=True, exist_ok=True)
    json_path = directory / "ship-texture-manifest.json"
    csv_path = directory / "ship-texture-manifest.csv"
    json_path.write_text(json.dumps([asdict(job) for job in jobs], indent=2), encoding="utf-8")
    with csv_path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(jobs[0]).keys()) if jobs else list(Job.__annotations__))
        writer.writeheader()
        writer.writerows(asdict(job) for job in jobs)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Asset root containing R1/R2/etc.")
    parser.add_argument("--output", type=Path, required=True, help="Separate generated output root")
    parser.add_argument("--scale", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--max-dimension", type=int, default=4096)
    parser.add_argument("--backend", choices=("pillow", "realesrgan"), default="realesrgan")
    parser.add_argument("--realesrgan", type=Path, help="Path to realesrgan-ncnn-vulkan.exe")
    parser.add_argument("--model", default="realesrgan-x4plus")
    parser.add_argument("--tile", type=int, default=256)
    parser.add_argument("--output-format", choices=("png", "tga"), default="png")
    parser.add_argument("--batch-ai", action="store_true", help="Run all pending AI jobs in one GPU process")
    parser.add_argument("--execute", action="store_true", help="Process; otherwise only write a plan")
    parser.add_argument("--overwrite", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if not source.is_dir():
        raise SystemExit(f"Source directory does not exist: {source}")
    if source == output or source in output.parents:
        raise SystemExit("Output must be a separate directory outside the source tree")
    jobs = plan_jobs(source, output, args.scale, args.max_dimension, args.backend,
                     args.output_format)
    old_manifest = output / "ship-texture-manifest.json"
    previous_jobs = {}
    if old_manifest.is_file():
        try:
            previous_jobs = {entry["relative"]: entry for entry in json.loads(old_manifest.read_text(encoding="utf-8"))}
        except (OSError, ValueError, KeyError, TypeError):
            previous_jobs = {}
    if args.overwrite:
        for job in jobs:
            Path(job.output).unlink(missing_ok=True)
    if args.execute:
        pending_ai = []
        for job in jobs:
            previous = previous_jobs.get(job.relative)
            if args.batch_ai and job.backend == "realesrgan":
                destination = Path(job.output)
                unchanged = destination.exists() and previous and (
                    previous.get("source_sha256") == job.source_sha256
                    and previous.get("target_width") == job.target_width
                    and previous.get("target_height") == job.target_height
                    and previous.get("backend") == job.backend
                    and previous.get("output") == job.output
                    and previous.get("status") in {"complete", "skipped"})
                if unchanged:
                    job.output_sha256 = sha256(destination)
                    job.status, job.message = "skipped", "matching output already exists"
                else:
                    pending_ai.append(job)
            else:
                process_job(job, args.realesrgan, args.model, args.tile, previous)
        if pending_ai:
            if not args.realesrgan or not args.realesrgan.is_file():
                for job in pending_ai:
                    job.status, job.message = "failed", "Real-ESRGAN executable not found"
            else:
                try:
                    run_realesrgan_batch(pending_ai, args.realesrgan, args.model, args.tile)
                except Exception as error:
                    for job in pending_ai:
                        job.status, job.message = "failed", str(error)
    write_manifests(jobs, output)
    counts = {status: sum(job.status == status for job in jobs) for status in ("planned", "complete", "skipped", "failed")}
    categories = {kind: sum(job.category == kind for job in jobs) for kind in ("albedo", "mask", "normal")}
    print(json.dumps({"jobs": len(jobs), "categories": categories, "status": counts,
                      "manifest": str(output / "ship-texture-manifest.json")}, indent=2))
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
