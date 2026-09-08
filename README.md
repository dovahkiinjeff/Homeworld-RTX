# Homeworld RTX

![Homeworld RTX interface](assets/UI/startup_splash.png)

[![Release](https://img.shields.io/badge/release-v0.91.3--beta.1-d89b42)](https://github.com/dovahkiinjeff/Homeworld-RTX/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-5b9bd5)](#requirements)
[![Renderer](https://img.shields.io/badge/renderer-Direct3D%2012%20%2B%20DXR-63b4c9)](#graphics)
[![Status](https://img.shields.io/badge/status-public%20beta-c86f5b)](docs/LIMITATIONS.md)

Homeworld RTX is a non-commercial Windows source port and graphics overhaul of
the original 1999 Homeworld. It preserves the classic campaign, simulation,
ships, controls, and data while adding a native Direct3D 12 presentation path,
DXR multi-bounce lighting, modern display support, a responsive interface, and
live authoring tools for maps, mission lighting, shadows, and volumetric dust.

> [!IMPORTANT]
> This repository and its releases do **not** contain the retail game archives,
> music, speech, or movies. You must own Homeworld 1 Classic. On first launch,
> select the original `Homeworld.exe`; Homeworld RTX validates and remembers
> the adjacent data installation.

> [!WARNING]
> This is a Windows x64 beta. It is not affiliated with, endorsed by, or
> supported by Gearbox, Relic Entertainment, Sierra On-Line, NVIDIA, or Valve.
> Homeworld and related marks belong to their respective owners.

## Download and quick start

1. Open the [0.91.3 public beta release](https://github.com/dovahkiinjeff/Homeworld-RTX/releases/tag/v0.91.3-beta.1).
2. Download `Homeworld-RTX-v0.91.3-beta.1-Windows-x64.zip`.
3. Extract the ZIP to a normal writable folder. Do not run it from inside the ZIP.
4. Run `HomeworldModern.exe`.
5. At the first-start picker, select your legally installed Homeworld Classic
   `Homeworld.exe`.

Supported data sets:

- Homeworld Remastered Collection - `Homeworld1Classic` data
- Original 1999 Homeworld data, preferably patched to 1.05

The Remastered Collection Classic data requires `Homeworld.big`,
`HW_Comp.vce`, and `HW_Music.wxd`. Original 1999 data additionally benefits
from `Update.big`. A legal `Movies` folder enables the pre-mission animatics.

See the [complete installation and user guide](docs/USER_GUIDE.md) for display
modes, controls, graphics tuning, save locations, and diagnostics.

## Highlights

### Graphics

- Native Windows x64 executable with Direct3D 12/DXGI presentation.
- Experimental native D3D12 fixed-function compatibility rasterizer.
- DXR scene extraction from the exact visible meshes and transforms.
- Progressive path-traced direct light and up to three diffuse bounces.
- Mission-sky environment radiance, HSF map lights, dynamic FX lights,
  emissive LIF materials, and active MEX navigation lights.
- Depth-validated motion reprojection, disocclusion rejection, temporal
  accumulation, spatial filtering, and firefly control.
- NVIDIA DLAA and DLSS Quality, Balanced, Performance, and Ultra Performance.
- Cross-vendor FXAA and AA Off fallbacks.
- Ray-traced soft shadows with editable sun size, ray counts, biases, contact
  reinforcement, and maximum range.
- Optional bloom, god rays, motion blur, chromatic aberration, film grain, and
  output dithering, with the UI kept outside scene post-processing.
- Highest-detail LOD0 policy, stable zoom, modern cursor scaling, and campaign
  sky reconstruction.

### Interface and gameplay

- Resolution-aware 16:9, ultrawide, 4K, and high-DPI interface scaling.
- Rebuilt front end, campaign screens, options, modal dialogs, and in-game
  Build, Research, and Launch docks.
- Ship Dossier with loaded statistics and Fleet Archive descriptions.
- Full HSV fleet palette, including true black.
- Independent engine-trail, navigation-light, harvesting-beam, and hyperspace
  color overrides; visible effects and their DXR emission stay synchronized.
- Optional single-player quality-of-life modes: unlimited strike-craft fuel,
  Super Salvagers, Capture / Build All, and a 1x-4x resource multiplier.
- Presentation frame rate is separated from the fixed simulation clock.

### Live authoring tools

- **Shift+F11 - Map Editor:** browse legal BIG archives, place and transform
  ships/resources, hide original objects through non-destructive overlays,
  author raymarched dust volumes, manage reusable cloud presets, and export
  portable `.rtxmap` files.
- **Shift+F12 - Lighting Editor:** edit the mission key and ambient wash,
  import retail HSF lights, add point/spot/ambient lights, place and aim them
  in the viewport, tune global materials/exposure, and edit ray-traced shadows.
- `kas2c.exe` and its source are retained for mission-script development.
- Human-readable exports are written beneath `RTXExports` beside the executable.

The detailed [authoring guide](docs/EDITING_GUIDE.md) documents every page,
field, shortcut, output path, and safe workflow.

## Requirements

Minimum practical requirements for the beta:

- 64-bit Windows 10 or Windows 11
- A legally installed copy of Homeworld 1 Classic
- Direct3D 12-capable GPU and current vendor driver
- DXR-capable GPU for path tracing and ray-traced shadows
- NVIDIA RTX GPU for DLAA/DLSS modes
- Approximately 1 GB free space for the release, logs, and shader cache

Non-DXR hardware can run the D3D12 presentation/raster path with path tracing
disabled. DLSS gracefully falls back when NVIDIA support is unavailable.

## Essential controls

| Control | Action |
| --- | --- |
| `Shift+F11` | Toggle the live map and volumetric editor |
| `Shift+F12` | Toggle the live lighting/shadow editor |
| `Ctrl+Shift+F12` | Reset the renderer |
| Right-click with ships selected | Open the tactical menu and Ship Dossier |
| Mouse wheel | Camera zoom; editor fly movement when Shift+F11 is open |

Both editors are designed for mouse use and also expose keyboard workflows.
See [Editing Guide](docs/EDITING_GUIDE.md#shortcut-reference).

## Documentation

- [Installation and User Guide](docs/USER_GUIDE.md)
- [Map, Lighting, Shadow, and Volumetric Editing Guide](docs/EDITING_GUIDE.md)
- [Known Limitations and Beta Expectations](docs/LIMITATIONS.md)
- [Native Windows Build Guide](Windows/BUILD_MODERN.md)
- [Graphics Feature Status](documentation/GRAPHICS_FEATURE_STATUS.md)
- [Renderer Architecture and Roadmap](documentation/MODERN_RENDERER_ROADMAP.md)
- [Original Source Baseline](documentation/ORIGINAL_SOURCE_BASELINE.md)
- [Third-Party Notices](THIRD_PARTY_NOTICES.md)
- PDF manual attached to each GitHub release

## Building from source

Install Visual Studio 2022 or 2026 with Desktop development with C++, then run
from a Developer PowerShell:

```powershell
./Windows/bootstrap.ps1
./Windows/build.ps1 -Configuration Release
```

The bootstrap obtains pinned dependencies and NVIDIA Streamline 2.12.0, checks
the Streamline archive digest, and keeps the vcpkg build tree in a no-space
directory beneath LocalAppData. Generated dependencies and build outputs are
not committed.

The build produces `HomeworldModern.exe`, its PDB symbols, required DLLs,
project assets, and `kas2c.exe`. Full details are in
[Windows/BUILD_MODERN.md](Windows/BUILD_MODERN.md).

## Beta status

This release is intended for testing, authoring, and community development.
The Windows/D3D12/DXR path is the active target. Linux and macOS are not
supported by this branch. HDR output, Reflex, Ray Reconstruction, genuine
frame generation, specular ray transport, and complete native-raster parity
remain future work. Read [Known Limitations](docs/LIMITATIONS.md) before filing
a report.

## Reporting problems

Include the mission, exact steps, GPU/driver, display mode, graphics settings,
and the diagnostic ZIP produced by `Windows/run-with-log.ps1`. Please do not
upload retail Homeworld archives, movies, voice files, or music.

## License and ownership

The original Homeworld source baseline is governed by the Relic source-code
agreement in [LICENSE.txt](LICENSE.txt), including its non-commercial terms.
Third-party components retain their own licenses. Project documentation and
new work do not grant rights to Homeworld retail data, imagery, audio, movies,
or trademarks. Review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This project is distributed free of charge and without warranty.
