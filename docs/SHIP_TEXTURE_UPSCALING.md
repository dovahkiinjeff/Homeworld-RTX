# Ship texture upscaling pipeline

This tool batch-processes **ship and derelict textures only**. The derelict pass
includes scaffold sections, wreckage, debris, and abandoned or mission-specific
vessels. It ignores UI, cursors, dossier art, asteroids, dust clouds, and mission
skies. Planet textures are handled by the dedicated vanilla-planet restoration
pass. Originals are never modified: output is written to a separate tree with
the same race/ship/LOD paths.

## Why it is split by texture type

- Hull/albedo textures may use Real-ESRGAN restoration.
- Team-color, emissive, opacity, roughness, metallic, AO, height, and other masks
  are copied without generative processing. A model must not invent data in them.
- Dedicated tangent-space normals are derived directly from the final restored
  4x hull art, so panel and surface detail created by the upscaler participates
  in lighting. `--normal-source vanilla` remains available only for deliberate
  compatibility comparisons.
- Existing alpha is extracted before AI processing and restored at target size.
- `.lif` files are left untouched. Deployment uses full-mip BC7 DDS for color,
  BC5 DDS for two-channel normals, and BC4 DDS for team masks. The loader prefers
  DDS and retains TGA/PNG/LIF fallbacks for incomplete or user-authored sets.

The classifier is conservative. Review `ship-texture-manifest.csv` before copying
generated assets into a playable tree.

## Build the deployable DDS tree

After producing and reviewing the restored hull images, build the runtime pack
with `tools/convert_ship_textures_to_dds.py` and the CMake-built
`ship_texture_dds.exe`. Color and normal textures receive complete mip chains;
mask files remain non-generative and receive their own complete mip chains.

Every color DDS may have a sibling named `<texture>_normal.dds`. The RTX mesh
loader supplies that map directly to the ray-traced material. If it is absent,
the renderer deliberately falls back to its legacy generated normal so custom
texture packs remain usable.

## One-time setup

Python 3 and Pillow are required. To install the portable, Vulkan-based
Real-ESRGAN backend from its pinned official release:

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_realesrgan.ps1
```

The download is checksum-verified and installed under the gitignored
`tools/.texture-tools` directory. It works with NVIDIA, AMD, and Intel Vulkan
drivers and does not require CUDA or PyTorch.

## Preview the exact queue

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_ship_texture_upscale.ps1 -PlanOnly
```

## Run the queue

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_ship_texture_upscale.ps1
```

Default output is `out/upscaled-ships`. Existing outputs are skipped, making the
run resumable. Use `-Overwrite` only when intentionally rebuilding generated files.

For a fast deterministic test without an AI backend:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_ship_texture_upscale.ps1 -Backend pillow
```

Use `-Source` to point at a larger legally extracted asset tree. Only top-level
ship race roots (`R1`, `R2`, `P1`, `P2`, `P3`, `Traders`, or `Ships`) and the
`Derelicts` root are admitted. Entries must still live below an LOD descendant.

## Review before deployment

Inspect seams, UV islands, hull lettering, panel lines, alpha edges, team-color
boundaries, engine glow coverage, and mip behavior in motion. AI restoration can
create plausible-looking but incorrect plating. Commit selected results only after
an in-game comparison; never replace the only copy of a source texture.

## Build the indexed runtime archive

Windows release builds can memory-map the finished DDS set from one indexed
`HomeworldTextures.hwt` container instead of opening thousands of individual
files during mission loading:

```powershell
python tools\pack_runtime_textures.py "<release-directory>" out\HomeworldTextures.hwt
```

Place the archive beside `HomeworldModern.exe`. The index provides sorted,
case-insensitive random access; only a requested DDS byte range is copied into
the existing texture upload path. Packed DDS entries are authoritative by
default so release loading does not issue thousands of failed filesystem probes.
Authors can set `HW_LOOSE_TEXTURE_OVERRIDES=1` while developing to restore loose
DDS priority without rebuilding the archive. Asteroid and mission DDS files
currently remain loose because those specialized loaders do not all use the
resource-file layer.

SeedVR2 can be connected later as an expert backend, but its 3B workflow commonly
needs substantially more VRAM and is not the safe unattended default for this
mixed technical texture set.
