# Homeworld Modern: native Windows x64 build

This build targets 64-bit Windows 10 and Windows 11 with Visual Studio 2022 or
Visual Studio 2026. SDL2 owns window/input/audio, Direct3D 12/DXGI owns visible
Windows presentation, and OpenGL remains the compatibility scene rasterizer
while native D3D12 draws and DXR are developed. The old `.sln` and `.vcproj`
files are historical and are not used.

## Requirements

- Visual Studio 2022 or 2026 with **Desktop development with C++**, the MSVC
  x64 build tools, and a current Windows SDK
- CMake 3.25 or newer
- Git
- vcpkg (the bootstrap creates a shared checkout automatically)
- Legally obtained Homeworld Classic data files; game assets are not included

The repository includes a bootstrap script. From a Developer PowerShell:

```powershell
.\Windows\bootstrap.ps1
.\Windows\build.ps1 -Configuration Release
```

The bootstrap stores vcpkg under
`%LOCALAPPDATA%\HomeworldModernDeps\vcpkg` by default. This deliberately keeps
FFmpeg's build tree out of game paths containing spaces, which its vcpkg port
does not support. `-VcpkgRoot` may supply another dependency location, but that
path must not contain spaces. The script sets `VCPKG_ROOT` for later shells,
installs WinFlexBison through WinGet when needed, and configures Bison's
package-data directory. The vcpkg manifest installs SDL2, SDL2_net, FFmpeg with
Bink video decoding, and the DirectX Shader Compiler (DXC) used to build the
DXR Shader Model 6.3 library.

The manifest installation tree also defaults to the no-space
`%LOCALAPPDATA%\HomeworldModernDeps\installed\homeworldmodern-0.91.2` path.
Use `-VcpkgInstalledRoot` to override it with another path that contains no
spaces. The source and final executable may still live in a spaced game path.

## Configure and build

Run from the repository root in PowerShell:

```powershell
.\Windows\build.ps1 -Configuration Release
```

The script detects Visual Studio 2022 or 2026 with `vswhere`, configures an
x64 build, compiles the selected configuration, and runs the tests. To repeat
only the compile after configuration:

```powershell
cmake --build .\out\build\windows-x64 --config Release --parallel
```

The one-command build displays errors and the final summary by default so the
large body of legacy warnings does not hide a failure. Pass `-ShowWarnings`
when performing a warning audit.

For a native memory error or Windows security-cookie fast-fail, build the same
Release target with MSVC AddressSanitizer enabled:

```powershell
.\Windows\build.ps1 -Configuration Release -Sanitize
```

This requires the MSVC AddressSanitizer component in Visual Studio. Run the
result through `run-with-log.ps1`; sanitizer diagnostics are captured in
`stderr.log` and the combined diagnostic ZIP. The build script locates and
copies `clang_rt.asan_dynamic-x86_64.dll` beside the sanitized executable.

The executables are written beneath:

```text
out\build\windows-x64\Debug\
out\build\windows-x64\Release\
```

`HomeworldModern.exe` is the game. `SDL2.dll` is copied beside it
automatically. `kas2c.exe` is a build-time mission compiler.

## Bind a legal game installation

The engine source does not contain the copyrighted game data. On first launch,
select the `Homeworld.exe` from your legally installed Homeworld 1 Classic
copy. Homeworld Modern validates the executable and its nearby data directory,
then remembers both under `Documents\My Games\Homeworld Modern`.

Original 1999 data:

- `Homeworld.big`
- `Update.big` (recommended for original 1999 data)
- `HW_Comp.vce`
- `HW_Music.wxd`
- `Movies\` (required only for campaign animatics)

Homeworld Remastered Collection's Homeworld 1 Classic data:

- `Homeworld.big`
- `HW_Comp.vce`
- `HW_Music.wxd`
- `Movies\` (required only for campaign animatics)

Do not add `Update.big` to the Remastered set. Its content is already folded
into that edition's `Homeworld.big`, and the engine deliberately continues
without the separate patch archive.

Launch example:

```powershell
& ".\out\build\windows-x64\Release\HomeworldModern.exe"
```

The engine validates whether `Homeworld.big` uses the original or Remastered
table-of-contents layout at startup. Alternate language or patched Remastered
archives are not identified by one hard-coded file count.

## Run with crash logging

Use the diagnostic launcher while stabilizing the native x64 port:

```powershell
$env:HW_Data = "D:\Games\HomeworldClassic\Data"
& ".\Windows\run-with-log.ps1" -Configuration Release
```

It records startup stages, stdout, stderr, the exit code, OS/GPU/display
details, and command-line arguments beneath `out\logs`. An unhandled native
exception also produces a readable `crash-*.txt` and a `crash-*.dmp` minidump.
The launcher configures per-user Windows Error Reporting for this executable as
well, allowing security-cookie fast-fails to create `HomeworldModern*.dmp` even
when Windows bypasses the process exception handler.
At the end of the run the launcher prints the path of a single ZIP containing
everything needed for diagnosis.

To determine whether a failure is tied specifically to fullscreen setup:

```powershell
& ".\Windows\run-with-log.ps1" -Configuration Release -Windowed
```

Additional game switches can be appended normally after the launcher options.
Release builds emit `HomeworldModern.pdb` beside the executable so minidump
addresses can be resolved to functions and source lines.

## Modern display and frame controls

The compatibility renderer accepts these command-line switches:

| Switch | Effect |
| --- | --- |
| `/window` | Resizable window |
| `/borderless` or `/fullscreen` | Borderless desktop fullscreen (default) |
| `/exclusive` | Exclusive fullscreen using the requested display mode |
| `/width N` | Custom width, 320-16384 |
| `/height N` | Custom height, 240-16384 |
| `/depth N` | Color depth: 16, 24, or 32 |
| `/refresh N` | Exclusive refresh rate; `0` chooses automatically |
| `/fps N` | Presentation cap; `0` is uncapped |
| `/uiScale N` | UI scale modifier, 50-200%; `100` uses the automatic size |
| `/raytracing` | Enable multi-bounce DXR path tracing when supported (default) |
| `/noRaytracing` | Disable ray lighting while retaining D3D12/DXGI presentation |
| `/fxLightStrength N` | FX/emissive cast-light strength, 0-200%; sprites remain visible at 0 |
| `/vsync` | VSync on (default) |
| `/adaptiveVsync` | Adaptive VSync where supported, otherwise ordinary VSync |
| `/noVsync` | VSync off |

Examples:

```powershell
# 2560x1440 resizable window, uncapped presentation
& ".\out\build\windows-x64\Release\HomeworldModern.exe" `
  /window /width 2560 /height 1440 /noVsync /fps 0

# 3840x2160 exclusive mode at 120 Hz, capped to 120 fps
& ".\out\build\windows-x64\Release\HomeworldModern.exe" `
  /exclusive /width 3840 /height 2160 /refresh 120 /fps 120 /vsync
```

Borderless fullscreen always uses the desktop's pixel dimensions. Custom
dimensions apply to windowed mode or a supported exclusive display mode.

## First start and automatic display selection

When no `Homeworld.cfg` profile exists and no command-line resolution is
supplied, the engine queries the primary display's active mode and adopts its
width, height, color depth, and refresh rate. Windows starts in borderless
desktop fullscreen by default. Later starts preserve the saved mode; explicit
`/window`, `/borderless`, `/exclusive`, `/width`, `/height`, and `/refresh`
arguments still take precedence where supplied.

## Automatic UI scaling

The classic interface uses a 640x480 logical canvas. Homeworld Modern scales
that canvas uniformly, keeps it centred inside wide and ultrawide outputs, and
extends marked backgrounds to the full drawable. Visual controls, their mouse
hit regions, fonts, and the software cursor use one shared scale.

| Output | Automatic UI scale |
| --- | ---: |
| 1280x720 | 1.0x |
| 1920x1080 | 1.5x |
| 2560x1440 | 2.0x |
| 3440x1440 | 2.0x, centred safe canvas |
| 3840x2160 | 3.0x |

`/uiScale 50` through `/uiScale 200` modifies the automatic size. The result
is constrained to fit the output, including resolutions below 640x480. The
chosen modifier is saved in `Homeworld.cfg`.

The `/fps` limiter controls presentation only. It deliberately does not alter
the fixed gameplay clock, preserving AI, physics, campaign scripting, save
games, demos, and multiplayer determinism. Smooth interpolation between fixed
simulation snapshots is a separate renderer milestone.

## Current revision boundary

This revision establishes the native x64 build and modern presentation layer,
including first-run display detection and resolution-aware UI scaling.
All renderables now use their highest-detail LOD0 geometry and N-LIPS distance
enlargement is disabled across ships, effects, particles, trails, and glows.

Compatibility OpenGL still rasterizes gameplay, but it no longer presents the
Windows window directly. The completed OpenGL back buffer is captured into a
D3D12 upload resource and D3D12/DXGI presents it through a triple-buffered
flip-discard swap chain. This transitional CPU readback is deliberately not the
final high-performance renderer; native D3D12 draw conversion will remove it.

The multi-bounce DXR pass is active on supported GPUs. It captures the exact
camera and visible mesh hierarchy, caches per-object BLAS geometry, refits a
triple-buffered TLAS when scene topology is stable, uploads
HSF/dynamic/emissive lights through persistent per-frame buffers, and
integrates up to three diffuse radiance bounces with progressive stochastic
sampling and visibility rays. The result lights the original compatibility
textures and team colors rather than replacing them.

Version 0.90.1 sends every GEO polygon's material, UVs, and actual
LIF color/alpha/emissive texels to DXR. Alpha-tested any-hit shaders preserve
cutouts, glow-map triangles cast light from sampled surface positions, and
active MEX navigation lights cast from their exact transformed points. A
world-only capture masks taskbar, manager, and other UI pixels out of the ray
lighting composite.

The Build, Research, and Launch docks now use explicit named regions instead
of inherited anonymous FIB geometry. Quantities, decrement/increment arrows,
costs, totals, scrollbars, and action buttons have independent responsive
columns, with scalable code-rendered labels over the supplied official-art
plates. Gameplay Options includes unlimited strike-craft fuel; it defaults off
and is deliberately ignored in multiplayer.

The legacy UI composition has been superseded by a native responsive shell.
Front-end, multiplayer, options, in-game menu and modal controls retain the
original engine callbacks but are reflowed into resolution-independent
workspaces, command panels and notices. Decorative FEMan LIF regions are
suppressed, and packaged interface plates are resolved relative to the
executable so launching from a different working directory is safe. The plates
and startup splash are built exclusively from the supplied official Homeworld
art archive, with provenance recorded in `assets/UI`. Original Bink mission
animatics are decoded through FFmpeg when `Movies` is present. Compiled
presenter shaders are cached per user; the presenter PSO is rebuilt safely at
startup rather than loading a driver-specific pipeline-library blob.
The Video
screen provides display mode, resolution, refresh, frame limit, UI scale, path
tracing, bounce/sample counts, generated normals, emissive FX energy,
brightness, effect density, Off/FXAA/DLAA and four DLSS path-light quality
modes, chromatic aberration, motion blur, response-controlled luminance film
grain, and background-only mission-driven god rays. Background sampling is
bilinear and the scene receives stable sub-LSB dithering to reduce color
banding without affecting the native UI. Motion blur
uses DXR motion vectors; all four controls default to 0%. Post effects operate
on the world before the UI is
composited at native resolution. Mission backgrounds are also sampled as
environment radiance by path tracing. D3D12 native raster draw conversion,
specular materials, HDR
display output, and DLSS Ray Reconstruction remain later
milestones. The precise implementation state is tracked in
`documentation/GRAPHICS_FEATURE_STATUS.md`.

The accepted v19 baseline does not expose frame generation. The retired
universal interpolation path remains disabled rather than being presented as
vendor frame generation; any future frame-generation work requires a supported
integration with correct motion/depth/UI separation and latency handling.

Version 0.12.0 also removes the per-frame whole-GPU drain, reuses mapped DXR
uploads and shader tables for each swap-chain slot, caches ranked glow-map
light prototypes by material, and omits background/world compatibility
readbacks when no enabled feature consumes them. Every 600 presented frames,
the log reports average OpenGL readback and DXR command-staging times; this
identifies the remaining compatibility bridge cost without changing gameplay.
