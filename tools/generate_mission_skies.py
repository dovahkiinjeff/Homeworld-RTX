#!/usr/bin/env python3
"""Regenerate Homeworld campaign skies as linear-HDR dual-paraboloid BC6H_UF16 DDS assets.

The original EZ01-EZ16 BTG geometry, colors, stars, and mission mapping remain
source-authoritative. All composition work stays float32 scene-linear through
rasterization, smoothing, upsampling, star synthesis, and metadata analysis.
The only quantization step is the final BC6H_UF16 block encoding; there is no
8-bit RGB intermediate in the shipping path.
"""

from __future__ import annotations

import argparse
import math
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

HEADER = struct.Struct("<21i")
VERTEX = struct.Struct("<Iddiiiii")
STAR = struct.Struct("<Iddiiii")
POLYGON = struct.Struct("<IIII")

MISSION_BTG = {
    1: "ez01", 2: "ez02", 3: "ez01", 4: "ez04",
    5: "ez05", 6: "ez06", 7: "ez07", 8: "ez08",
    9: "ez09", 10: "ez10", 11: "ez11", 12: "ez12",
    13: "ez13", 14: "ez14", 15: "ez15", 16: "ez16",
}

# Mission 3 reuses the exact EZ01 composition from Mission 1.
UNIQUE_ASSET_MISSIONS = (1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)

# Leave a small analytic continuation margin outside the equator circle in
# each paraboloid half. Runtime cross-fading can then sample either hemisphere
# without ever clamping at the 2D square edge.
PARABOLOID_EXTENT = 1.0625

@dataclass(frozen=True)
class Vertex:
    x: float
    y: float
    rgb: tuple[int, int, int]
    alpha: int
    brightness: int

@dataclass(frozen=True)
class Star:
    x: float
    y: float
    rgb: tuple[int, int, int]
    alpha: int
    texture: str


def srgb_to_linear(value: np.ndarray | float) -> np.ndarray | float:
    value = np.asarray(value, dtype=np.float32)
    result = np.where(value <= 0.04045,
                      value / 12.92,
                      np.power((value + 0.055) / 1.055, 2.4))
    return result.astype(np.float32)


def linear_to_srgb(value: np.ndarray) -> np.ndarray:
    value = np.maximum(np.asarray(value, dtype=np.float32), 0.0)
    return np.where(value <= 0.0031308,
                    value * 12.92,
                    1.055 * np.power(value, 1.0 / 2.4) - 0.055).astype(np.float32)


def parse_btg(path: Path) -> dict:
    data = path.read_bytes()
    fields = HEADER.unpack_from(data, 0)
    names = (
        "version", "num_verts", "num_stars", "num_polys", "x_scroll",
        "y_scroll", "zoom", "page_width", "page_height", "master_red",
        "master_green", "master_blue", "background_red", "background_green",
        "background_blue", "draw_verts", "draw_polys", "draw_stars",
        "draw_outlines", "draw_blends", "render_mode",
    )
    header = dict(zip(names, fields))
    if header["version"] != 0x600:
        raise ValueError(f"{path}: unsupported BTG version 0x{header['version']:x}")
    offset = HEADER.size
    vertices: list[Vertex] = []
    for _ in range(header["num_verts"]):
        _flags, x, y, red, green, blue, alpha, brightness = VERTEX.unpack_from(data, offset)
        offset += VERTEX.size
        vertices.append(Vertex(x, y, (red, green, blue), alpha, brightness))
    stars: list[Star] = []
    for _ in range(header["num_stars"]):
        _flags, x, y, red, green, blue, alpha = STAR.unpack_from(data, offset)
        offset += STAR.size
        length = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        texture = data[offset:offset + length].decode("latin-1", errors="replace")
        offset += length
        stars.append(Star(x, y, (red, green, blue), alpha, texture))
    polygons = []
    for _ in range(header["num_polys"]):
        _flags, v0, v1, v2 = POLYGON.unpack_from(data, offset)
        offset += POLYGON.size
        polygons.append((v0, v1, v2))
    if offset != len(data):
        raise ValueError(f"{path}: parsed {offset} of {len(data)} bytes")
    return {"header": header, "vertices": vertices, "stars": stars,
            "polygons": polygons}


def source_vertex_color(vertex: Vertex, background: np.ndarray) -> np.ndarray:
    energy = (vertex.alpha / 255.0) * (vertex.brightness / 255.0)
    authored = srgb_to_linear(np.asarray(vertex.rgb, dtype=np.float32) / 255.0)
    return np.maximum(authored * energy, background)


def rasterize_triangle(image: np.ndarray, points: np.ndarray,
                       colors: np.ndarray) -> None:
    height, width, _channels = image.shape
    x0 = max(0, int(math.floor(float(np.min(points[:, 0])))))
    x1 = min(width, int(math.ceil(float(np.max(points[:, 0])))) + 1)
    y0 = max(0, int(math.floor(float(np.min(points[:, 1])))))
    y1 = min(height, int(math.ceil(float(np.max(points[:, 1])))) + 1)
    if x0 >= x1 or y0 >= y1:
        return
    ax, ay = points[0]; bx, by = points[1]; cx, cy = points[2]
    denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(float(denominator)) < 1.0e-8:
        return
    xs = np.arange(x0, x1, dtype=np.float32)[None, :] + 0.5
    ys = np.arange(y0, y1, dtype=np.float32)[:, None] + 0.5
    w0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / denominator
    w1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / denominator
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -1.0e-5) & (w1 >= -1.0e-5) & (w2 >= -1.0e-5)
    if not np.any(inside):
        return
    interpolated = (w0[..., None] * colors[0] +
                    w1[..., None] * colors[1] +
                    w2[..., None] * colors[2])
    destination = image[y0:y1, x0:x1]
    destination[inside] = interpolated[inside]


def box_blur_axis(image: np.ndarray, radius: int, axis: int, wrap: bool) -> np.ndarray:
    if radius <= 0:
        return image
    pad = [(0, 0)] * image.ndim
    pad[axis] = (radius, radius)
    padded = np.pad(image, pad, mode="wrap" if wrap else "edge")
    cumsum = np.cumsum(padded, axis=axis, dtype=np.float32)
    zero_shape = list(cumsum.shape); zero_shape[axis] = 1
    cumsum = np.concatenate((np.zeros(zero_shape, dtype=np.float32), cumsum), axis=axis)
    window = radius * 2 + 1
    hi = [slice(None)] * image.ndim; lo = [slice(None)] * image.ndim
    hi[axis] = slice(window, window + image.shape[axis])
    lo[axis] = slice(0, image.shape[axis])
    return (cumsum[tuple(hi)] - cumsum[tuple(lo)]) / float(window)


def smooth_float(image: np.ndarray) -> np.ndarray:
    # Three box passes closely approximate the prior Gaussian smoothing but
    # never leave float32. Longitude wraps; latitude uses edge extension.
    result = image
    for radius in (3, 3, 4):
        result = box_blur_axis(result, radius, 1, True)
        result = box_blur_axis(result, radius, 0, False)
    return result.astype(np.float32, copy=False)


def resize_float(image: np.ndarray, width: int, height: int) -> np.ndarray:
    channels = []
    for channel in range(image.shape[2]):
        plane = Image.fromarray(image[..., channel].astype(np.float32), mode="F")
        channels.append(np.asarray(plane.resize((width, height), Image.Resampling.LANCZOS),
                                   dtype=np.float32))
    return np.stack(channels, axis=2)


def enforce_spherical_edges(image: np.ndarray) -> None:
    height, width, _channels = image.shape
    seam_width = max(12, width // 256)
    for index in range(seam_width):
        t = index / max(1, seam_width - 1)
        t = t * t * (3.0 - 2.0 * t)
        left = image[:, index].copy(); right = image[:, width - 1 - index].copy()
        average = (left + right) * 0.5
        image[:, index] = average * (1.0 - t) + left * t
        image[:, width - 1 - index] = average * (1.0 - t) + right * t
    pole_rows = max(3, height // 512)
    for index in range(pole_rows):
        t = index / max(1, pole_rows - 1)
        t = t * t * (3.0 - 2.0 * t)
        for row in (index, height - 1 - index):
            mean = np.mean(image[row], axis=0, keepdims=True)
            image[row] = mean * (1.0 - t) + image[row] * t


def add_microstructure(image: np.ndarray, background: np.ndarray,
                       material: Image.Image, mission: int) -> None:
    height, width, _channels = image.shape
    tile = material.convert("L")
    phase = (mission * 97) % tile.width
    tile = tile.transform(tile.size, Image.Transform.AFFINE,
                          (1, 0, phase, 0, 1, phase // 2),
                          resample=Image.Resampling.BICUBIC)
    mirrored = Image.new("L", (tile.width * 2, tile.height * 2))
    mirrored.paste(tile, (0, 0))
    mirrored.paste(tile.transpose(Image.Transpose.FLIP_LEFT_RIGHT), (tile.width, 0))
    mirrored.paste(tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM), (0, tile.height))
    mirrored.paste(tile.transpose(Image.Transpose.ROTATE_180), (tile.width, tile.height))
    texture = np.asarray(mirrored.resize((width, height), Image.Resampling.BICUBIC),
                         dtype=np.float32) / 255.0
    texture = (texture - float(np.mean(texture))) / max(1.0e-5, float(np.std(texture)))
    texture = np.clip(texture, -2.0, 2.0)
    field = np.max(np.maximum(image - background[None, None, :], 0.0), axis=2)
    mask = np.clip(field * 8.0, 0.0, 1.0)
    modulation = 1.0 + texture[..., None] * mask[..., None] * 0.018
    image[:] = np.maximum(background, image * modulation)


def add_star(image: np.ndarray, star: Star, page_width: int, page_height: int) -> None:
    height, width, _channels = image.shape
    x = (star.x / page_width * width) % width
    y = np.clip(star.y / page_height * height, 0.0, height - 1.0)
    texture = star.texture.lower()
    if "bigstar" in texture:
        core_radius, halo_radius, radiance = 3.0, 18.0, 8.0
    elif "neb" in texture:
        core_radius, halo_radius, radiance = 2.2, 13.0, 6.0
    elif "b02" in texture:
        core_radius, halo_radius, radiance = 1.6, 8.0, 5.0
    elif "b03" in texture:
        core_radius, halo_radius, radiance = 1.1, 5.0, 4.0
    else:
        core_radius, halo_radius, radiance = 1.35, 6.5, 4.5
    energy = np.clip(star.alpha / 255.0, 0.0, 1.0)
    color = srgb_to_linear(np.asarray(star.rgb, dtype=np.float32) / 255.0)
    extent = int(math.ceil(halo_radius * 3.0))
    x_center = int(math.floor(x)); y_center = int(math.floor(y))
    for oy in range(-extent, extent + 1):
        yy = y_center + oy
        if yy < 0 or yy >= height:
            continue
        for ox in range(-extent, extent + 1):
            xx = (x_center + ox) % width
            dx = (x_center + ox + 0.5) - x; dy = (y_center + oy + 0.5) - y
            distance2 = dx * dx + dy * dy
            core = math.exp(-distance2 / (2.0 * core_radius * core_radius))
            halo = math.exp(-distance2 / (2.0 * halo_radius * halo_radius))
            contribution = energy * (core * radiance + halo * radiance * 0.055)
            image[yy, xx] += color * contribution


def render_btg(btg: dict, width: int, height: int,
               material: Image.Image, mission: int) -> np.ndarray:
    header = btg["header"]
    background_srgb = np.asarray((header["background_red"], header["background_green"],
                                  header["background_blue"]), dtype=np.float32) / 255.0
    background = srgb_to_linear(background_srgb)
    base_width, base_height = width // 2, height // 2
    base = np.empty((base_height, base_width, 3), dtype=np.float32); base[:] = background
    point_scale = np.asarray((base_width / header["page_width"],
                              base_height / header["page_height"]), dtype=np.float32)
    vertex_colors = [source_vertex_color(vertex, background) for vertex in btg["vertices"]]
    for indices in btg["polygons"]:
        vertices = [btg["vertices"][index] for index in indices]
        points = np.asarray([(vertex.x, vertex.y) for vertex in vertices], dtype=np.float32) * point_scale
        colors = np.asarray([vertex_colors[index] for index in indices], dtype=np.float32)
        rasterize_triangle(base, points, colors)
    base = smooth_float(base)
    final = resize_float(base, width, height)
    add_microstructure(final, background, material, mission)
    # RTX-0067: the visible background deliberately excludes point stars.
    # Those are rendered from the original BTG star catalog as native HDR
    # camera-facing billboards so BC6H blocks can never turn them into squares.
    enforce_spherical_edges(final)
    return np.clip(final, 0.0, 16.0).astype(np.float32, copy=False)



def reproject_dual_paraboloid(image: np.ndarray) -> np.ndarray:
    """Pack +Z and -Z spherical hemispheres into one seamless 2:1 Texture2D.

    Each half is a full paraboloid square. Pixels outside the unit hemisphere
    disk are intentionally filled by the analytic continuation through the
    equator, so BC6H blocks and bilinear filtering at the disk boundary see
    the correct neighboring spherical directions instead of clamp garbage.
    Runtime sampling cross-fades the two mathematically equivalent mappings
    in a narrow equatorial band to hide independent BC6H endpoint error.
    """
    src_h, src_w, channels = image.shape
    if channels != 3 or src_w != src_h * 2:
        raise ValueError("source environment must be float RGB 2:1 lat/long")
    side = src_h
    output = np.empty((side, side * 2, 3), dtype=np.float32)
    x = ((((np.arange(side, dtype=np.float32) + 0.5) / float(side)) * 2.0 - 1.0)
         * PARABOLOID_EXTENT)[None, :]
    chunk_rows = 64
    for y_start in range(0, side, chunk_rows):
        y_end = min(side, y_start + chunk_rows)
        py = ((((np.arange(y_start, y_end, dtype=np.float32) + 0.5) /
               float(side)) * 2.0 - 1.0) * PARABOLOID_EXTENT)[:, None]
        px = np.broadcast_to(x, (y_end - y_start, side))
        py = np.broadcast_to(py, px.shape)
        radius2 = px * px + py * py
        inv = 1.0 / np.maximum(1.0 + radius2, 1.0e-12)
        dx = 2.0 * px * inv
        dy = 2.0 * py * inv
        base_z = (1.0 - radius2) * inv
        for hemisphere, dz in ((0, base_z), (1, -base_z)):
            theta = np.arctan2(dy, dx)
            u = np.mod((0.5 * math.pi - theta) / (2.0 * math.pi), 1.0)
            v = np.arccos(np.clip(dz, -1.0, 1.0)) / math.pi
            fx = u * src_w - 0.5
            fy = v * src_h - 0.5
            x0_floor = np.floor(fx)
            y0_floor = np.floor(fy)
            tx = (fx - x0_floor).astype(np.float32)
            ty = (fy - y0_floor).astype(np.float32)
            x0 = np.mod(x0_floor.astype(np.int64), src_w)
            x1 = (x0 + 1) % src_w
            y0 = np.clip(y0_floor.astype(np.int64), 0, src_h - 1)
            y1 = np.clip(y0 + 1, 0, src_h - 1)
            p00 = image[y0, x0]; p10 = image[y0, x1]
            p01 = image[y1, x0]; p11 = image[y1, x1]
            top = p00 + (p10 - p00) * tx[..., None]
            bottom = p01 + (p11 - p01) * tx[..., None]
            sampled = top + (bottom - top) * ty[..., None]
            output[y_start:y_end, hemisphere * side:(hemisphere + 1) * side] = sampled
    return output


def reduce_energy(image: np.ndarray, target_width: int = 256,
                  target_height: int = 128) -> np.ndarray:
    h, w, c = image.shape
    if w % target_width or h % target_height:
        return resize_float(image, target_width, target_height)
    fy, fx = h // target_height, w // target_width
    return image.reshape(target_height, fy, target_width, fx, c).mean(axis=(1, 3), dtype=np.float32)


def analyze_sky(image: np.ndarray, mission: int) -> dict:
    sample = reduce_energy(image)
    h, w, _ = sample.shape
    lum = sample[...,0] * 0.2126 + sample[...,1] * 0.7152 + sample[...,2] * 0.0722
    mean = float(np.mean(lum)); variance = float(np.mean((lum - mean) ** 2)); maximum = float(np.max(lum))
    threshold = mean + 1.65 * math.sqrt(max(0.0, variance))
    threshold = max(threshold, min(0.035, maximum * 0.30))
    if maximum > 0.0:
        threshold = min(threshold, maximum * 0.82)
    minimum_area = max(8, (w * h) // 4096)
    visited = np.zeros((h, w), dtype=np.bool_)
    best = None
    for y0 in range(h):
        for x0 in range(w):
            if visited[y0, x0] or lum[y0, x0] < threshold:
                continue
            queue = [(x0, y0)]; visited[y0, x0] = True; head = 0
            area = 0; integrated = 0.0; peak = 0.0
            s_sin=s_cos=s_y=s_weight=s_r=s_g=s_b=0.0
            while head < len(queue):
                x,y = queue[head]; head += 1
                value = float(lum[y,x]); weight = value - threshold + 0.02
                u = (x + 0.5) / w
                area += 1; integrated += value; peak = max(peak, value)
                s_sin += math.sin(2.0 * math.pi * u) * weight
                s_cos += math.cos(2.0 * math.pi * u) * weight
                s_y += (y + 0.5) * weight; s_weight += weight
                s_r += float(sample[y,x,0]) * weight
                s_g += float(sample[y,x,1]) * weight
                s_b += float(sample[y,x,2]) * weight
                for dy in (-1,0,1):
                    ny=y+dy
                    if ny < 0 or ny >= h: continue
                    for dx in (-1,0,1):
                        if dx==0 and dy==0: continue
                        nx=(x+dx)%w
                        if not visited[ny,nx] and lum[ny,nx] >= threshold:
                            visited[ny,nx]=True; queue.append((nx,ny))
            if area < minimum_area: continue
            candidate=(area,integrated,peak,s_sin,s_cos,s_y,s_weight,s_r,s_g,s_b)
            if best is None:
                best=candidate
            elif mission == 2:
                if peak > best[2] or (peak == best[2] and area > best[0]): best=candidate
            elif area > best[0] or (area == best[0] and integrated > best[1]): best=candidate
    ambient=np.mean(sample,axis=(0,1)).astype(np.float64)
    result={"valid":0,"direction":(0.0,0.0,-1.0),"key":(0.0,0.0,0.0),
            "ambient":tuple(float(x) for x in ambient),"threshold":threshold,
            "peak":maximum,"area":0,"sample_width":w,"sample_height":h}
    if best is None or best[6] <= 1.0e-9:
        return result
    area,integrated,peak,s_sin,s_cos,s_y,s_weight,s_r,s_g,s_b=best
    angle=math.atan2(s_sin,s_cos)
    if angle < 0: angle += 2.0*math.pi
    u=angle/(2.0*math.pi); v=max(0.0,min(1.0,(s_y/s_weight)/h))
    phi=math.pi*v; theta=math.pi*0.5-2.0*math.pi*u; sinphi=math.sin(phi)
    result.update(valid=1,
                  direction=(math.cos(theta)*sinphi, math.sin(theta)*sinphi, math.cos(phi)),
                  key=(s_r/s_weight,s_g/s_weight,s_b/s_weight),
                  peak=peak, area=area)
    return result


def write_metadata(path: Path, metadata: dict) -> None:
    d=metadata["direction"]; k=metadata["key"]; a=metadata["ambient"]
    path.write_text(
        "HWSKYHDR1 %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d\n" %
        (metadata["valid"], d[0],d[1],d[2], k[0],k[1],k[2], a[0],a[1],a[2],
         metadata["threshold"], metadata["peak"], metadata["area"],
         metadata["sample_width"], metadata["sample_height"]), encoding="ascii")


def write_hwfloat(path: Path, image: np.ndarray) -> None:
    h,w,c=image.shape
    assert c==3
    with path.open("wb") as stream:
        stream.write(b"HWF6"); stream.write(struct.pack("<II",w,h))
        stream.write(np.asarray(image,dtype="<f4",order="C").tobytes())


def preview_image(image: np.ndarray) -> Image.Image:
    # Preview only. Shipping data never passes through this display transform.
    mapped = image / (1.0 + image)
    encoded = linear_to_srgb(mapped)
    return Image.fromarray(np.uint8(np.clip(encoded,0.0,1.0)*255.0+0.5), "RGB")


def ensure_encoder(source_root: Path, requested: Path | None, temporary: Path) -> Path:
    if requested is not None: return requested
    encoder=temporary/"bc6h_encode"
    subprocess.run(("c++","-O3","-std=c++17",str(source_root/"tools"/"bc6h_encode.cpp"),"-o",str(encoder)),check=True)
    return encoder


def parse_missions(spec: str) -> set[int]:
    selected=set()
    for item in spec.split(","):
        bounds=item.strip().split("-",1); first=int(bounds[0]); last=int(bounds[-1])
        selected.update(range(min(first,last),max(first,last)+1))
    if not selected or not selected.issubset(MISSION_BTG):
        raise SystemExit("--missions must select missions 1 through 16")
    return selected


def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--btg-dir",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--width",type=int,default=4096)
    parser.add_argument("--height",type=int,default=2048)
    parser.add_argument("--encoder",type=Path)
    parser.add_argument("--preview-dir",type=Path)
    parser.add_argument("--missions",type=str,default="1-16")
    args=parser.parse_args()
    if args.width != args.height*2 or args.width%4 or args.height%4:
        raise SystemExit("sky dimensions must be a block-aligned 2:1 image")
    selected=parse_missions(args.missions)
    source_root=Path(__file__).resolve().parent.parent
    material=Image.open(source_root/"tools"/"sky_materials"/"btg_microstructure.png").convert("L")
    args.output.mkdir(parents=True,exist_ok=True)
    if args.preview_dir: args.preview_dir.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="homeworld-modern-bc6h-") as directory:
        temporary=Path(directory); encoder=ensure_encoder(source_root,args.encoder,temporary)
        for mission in UNIQUE_ASSET_MISSIONS:
            # Mission 3 is represented by Mission 1's exact asset at runtime.
            if mission not in selected and not (mission == 1 and 3 in selected): continue
            stem=MISSION_BTG[mission]; btg=parse_btg(args.btg_dir/f"{stem}.btg")
            background=render_btg(btg,args.width,args.height,material,mission)
            # Lighting metadata still sees the exact authored BTG stars even
            # though the visible star raster is now a separate native layer.
            metadata_image=background.copy()
            for star in btg["stars"]:
                add_star(metadata_image, star, btg["header"]["page_width"],
                         btg["header"]["page_height"])
            enforce_spherical_edges(metadata_image)
            metadata=analyze_sky(metadata_image,mission)
            image=reproject_dual_paraboloid(background)
            if args.preview_dir:
                preview_image(image).resize((1024,512),Image.Resampling.LANCZOS).save(args.preview_dir/f"mission{mission:02d}.png")
            raw=temporary/f"mission{mission:02d}.hwf6"; write_hwfloat(raw,image)
            output=args.output/f"mission{mission:02d}_sky.dds"
            subprocess.run((str(encoder),str(raw),str(output)),check=True)
            write_metadata(args.output/f"mission{mission:02d}_sky.hdrmeta",metadata)
            print(f"mission {mission:02d}: {stem.upper()} -> {output.name} "
                  f"({output.stat().st_size/(1024*1024):.2f} MiB BC6H_UF16 dual-paraboloid, "
                  f"peak={float(np.max(image)):.3f}, keyLinear={metadata['key']}, "
                  f"ambientLinear={metadata['ambient']})", flush=True)

if __name__ == "__main__": main()
