# Homeworld RTX 0.91.3 Beta 4 - Installation and User Guide

## What this build is

Homeworld RTX is a modern Windows x64 port of the original Homeworld engine.
It reads the classic game data you already own and supplies a replacement
executable, modern renderer, interface assets, and authoring overlays. It does
not modify your retail BIG archives.

The beta is intentionally Windows-only. Direct3D 12, DXGI, DXR, the Windows
SDK, and NVIDIA Streamline are central to the current renderer. Portability can
be revisited after the native renderer reaches campaign-wide parity.

## Installation

### Recommended clean install

1. Download the Windows x64 ZIP from GitHub Releases.
2. Create a writable folder such as `C:\Games\Homeworld RTX`.
3. Extract every file and folder into it.
4. From your legally owned Homeworld Classic data folder, manually copy
   `HW_Music.wxd` and `HW_Comp.vce` into this new folder, directly beside
   `HomeworldModern.exe`.
5. Launch `HomeworldModern.exe`.
6. Choose the `Homeworld.exe` belonging to Homeworld 1 Classic.

> **Required beta workaround:** the current first-run verification does not reliably locate `HW_Music.wxd` and `HW_Comp.vce` through the selected retail installation. A new extract will fail verification unless both legally owned archives are manually present beside `HomeworldModern.exe`. Steam users will normally find the originals under `Homeworld1Classic\Data`.

The program checks the executable and nearby data folder. The remembered
location is stored in:

```text
Documents\My Games\Homeworld Modern\installation.ini
```

You can keep Homeworld RTX outside Steam. This protects the mod from Steam
verification and keeps exports easy to find.

### Accepted game data

For Homeworld Remastered Collection, select:

```text
...\Homeworld\Homeworld1Classic\Homeworld.exe
```

Its data set normally includes:

```text
Data\Homeworld.big
Data\HW_Comp.vce
Data\HW_Music.wxd
```

Copy `Data\HW_Comp.vce` and `Data\HW_Music.wxd` into the Homeworld RTX folder.
Leave the originals in place and never redistribute either archive.

For an original 1999 installation, use its `Homeworld.exe` and data directory.
`Update.big` is recommended for original data, but should not be added to the
Remastered Collection Classic set because its updates are already folded into
`Homeworld.big`.

The optional `Movies` directory enables the original pre-mission Bink videos.
Retail data is never included in this project's downloads.

### Updating later

Keep personal exports and diagnostics before replacing a beta:

```text
RTXExports\
Documents\My Games\Homeworld Modern\RTXLighting\
Documents\My Games\Homeworld Modern\Homeworld.cfg
```

Extract a newer release over a copy or into a fresh directory. A fresh folder
is easiest to diagnose because obsolete DLLs cannot remain beside the game.

## First run

With no saved profile and no command-line resolution, the game adopts the
primary display's active mode and starts borderless. Interface scale is derived
from resolution and constrained to the window. Examples:

| Output | Automatic interface scale |
| --- | ---: |
| 1280x720 | 1.0x |
| 1920x1080 | 1.5x |
| 2560x1440 | 2.0x |
| 3440x1440 | 2.0x centered safe canvas |
| 3840x2160 | 3.0x |

The UI, mouse hit regions, fonts, cursor, modal dialogs, and manager docks share
one scaling policy. The interface remains native resolution when DLSS is used.

## Video settings

### Volumetric dust and restored environments

Campaign dust is a world-space FP16 participating medium, not a stack of
camera-facing sprites. It receives mission and local lighting, ship wakes, and
real scene depth while retaining its position as the camera rotates. A
half-resolution integration pass controls cost without reducing material
precision, and Shift+F11 provides map-wide fade distance/strength controls.

Visible campaign skies use Homeworld's original BTG artwork and stars. Modern
mission key and ambient metadata remain active for DXR, dust, god rays, and
Shift+F12 editing. Original planet geometry and UV layouts are likewise kept
beneath upgraded DDS color and generated-normal assets.

Dense asteroid fields use LODs derived from the four authored replacement
meshes. Distant rocks preserve those silhouettes and textures rather than
switching to unrelated round procedural substitutes.

### Display

- **Display Mode:** borderless desktop, exclusive fullscreen, or resizable window.
- **Output Resolution:** applied on the next launch where required.
- **Refresh Rate:** automatic or a supported exclusive-mode refresh.
- **Frame Limit:** presentation cap; zero is uncapped.
- **Interface Scale:** automatic resolution-derived scale with a 50-200% modifier.
- **VSync:** on, adaptive where supported, or off.

Presentation rate does not change simulation rate. AI, physics, scripts,
weapons, demos, multiplayer checksums, and save behavior remain tied to the
fixed gameplay clock.

### Path tracing

- **Path Tracing:** turns DXR lighting on or off.
- **Light Bounces:** one to three diffuse radiance bounces.
- **Samples:** one to four new stochastic paths per pixel per frame.
- **Normal Map Strength:** 0-400% tangent-space surface relief. Dedicated ship
  normal maps are used when installed; legacy textures retain an automatic
  luminance-derived fallback.
- **FX Emission:** 0-200% light cast by weapons, engines, beams, explosions,
  glow maps, and navigation lights. Zero removes their light, not the visible FX.
- **Weapon Origin Intensity / Range:** 0-400% controls for light created at
  every weapon muzzle or launch point, including sustained ion-beam origins.
- **Weapon Impact Intensity / Range:** 0-400% controls for light created at
  every resolved bullet, beam, and missile impact point. Impact lighting is
  collision-driven and remains available when optional hit sprites are culled.

Start at one sample and two or three bounces. Increase samples before bounces
when you want a steadier image; increase bounces for richer indirect fill.
Normal-map strength at 100% is the tuned baseline.

The temporal accumulator rejects mismatched depth, invalid previous transforms,
camera cuts, newly revealed geometry, and LOD/visibility discontinuities. Fast
camera movement deliberately retains less history than a still view.

### Image quality

- **Off:** no post anti-aliasing.
- **Auto / Native AA (recommended):** selects DLAA on supported NVIDIA GPUs,
  XeSS AA on Intel GPUs, or FidelityFX Native AA on AMD and other adapters.
- **Auto / Quality:** selects DLSS Quality, XeSS Quality, or FSR Quality from
  the active adapter and installed signed runtimes.
- **FXAA:** universal, inexpensive spatial fallback with no temporal history.
- **Universal Temporal AA:** vendor-neutral native-resolution temporal
  reconstruction, resolved through the packaged FidelityFX runtime.
- **DLAA / DLSS:** NVIDIA native AA plus Quality, Balanced, Performance, and
  Ultra Performance full-scene modes.
- **FSR Native AA / FSR:** FidelityFX native AA plus Quality, Balanced,
  Performance, and Ultra Performance modes. The Direct3D 12 runtime is
  cross-vendor and is also the dependable temporal fallback.
- **XeSS AA / XeSS-SR:** Intel native AA plus Quality, Balanced, Performance,
  and Ultra Performance modes. Intel hardware uses the optimized path; XeSS
  also provides its documented shader-model fallback on supported non-Intel
  hardware.
- **Brightness:** final output transfer.
- **Effect Density and Effect Budget:** control background/trail/impact complexity.
- **Frame Generation:** shown as unavailable; real Streamline DLSS-G and Reflex
  are not integrated in this beta.

Temporal reconstruction does not reduce UI resolution: the HUD-less world is
reconstructed first and the interface is composited afterward at display
resolution. Explicit unsupported modes fall back safely; Auto avoids selecting
an unavailable backend. The Windows package includes the signed runtime DLLs;
the selected backend still requires a compatible current driver.

### Post effects

- **Chromatic Aberration:** radial RGB separation; default off.
- **Motion Blur:** uses DXR motion vectors; default off.
- **Luminance Film Grain:** response-controlled grain that protects deep blacks
  and highlights; default off.
- **God Rays:** searches the mission background for the authored bright source;
  ships and planet discs cannot become false suns, and world geometry occludes
  the radial march.
- **Bloom:** soft scene glow, 0-400%, with 100% as the visual baseline.
- **Color Banding Filter:** stable scene-only output dithering; UI is excluded.

## Lighting model

The ray pass consumes the scene rather than replacing it. It uses:

- Exact visible GEO triangles and object transforms.
- Per-triangle material identity and UVs.
- LIF color, alpha, team masks, and emissive/glow texels.
- Alpha-tested visibility for cutout materials.
- Mission HDR sky radiance and detected bright-source metadata.
- Retail HSF directional, point, spot, and ambient lights.
- Active MEX navigation lights in their transformed ship positions.
- Dynamic engines, layered trails, muzzles, projectiles, harvesting beams,
  hyperspace effects, and explosions.

Static geometry is cached in BLAS records. Visible instances are submitted to
a triple-buffered TLAS, which is refitted when topology remains stable. Upload
buffers and shader tables are reused across frames.

Classic assets do not carry modern metalness/roughness channels. The current
model derives conservative material behavior and exposes global reflectivity,
roughness, and generated-normal controls. Specular ray transport is not yet a
full physically based reflection system.

## Modern interface

The main menu, campaign, tutorial, multiplayer, options, in-game menus, and
dialogs use responsive code-native layout templates. Build, Research, and
Launch are right-docked managers with separate queue, quantity, cost, total,
scroll, and action regions.

With ships selected, open the tactical right-click menu and choose **Ship
Dossier**. The dossier shares the right dock and shows loaded ship statistics
plus a concise Fleet Archive assessment.

## Fleet identity editor

Fleet Identity is a unified livery and emissive editor for the local player's
ships. Select one of six channels on the left, choose hue and saturation in the
large color field, set value with the vertical slider, and check the live
RGB/HSV/hex material key before selecting **Apply**. The picker exposes the
entire HSV range with no minimum-brightness gate, so true black and near-black
hulls are valid choices.

The six independently selectable channels are:

- **Hull Base:** primary player-ship pigment across the fleet.
- **Stripe / Marking:** secondary livery and team markings.
- **Engine Emission:** engine ribbons, nozzle glow, and linked DXR light.
- **Nav Lights:** navigation-light sprites and point emitters.
- **Harvest Beam:** harvesting beam, nozzle glow, and line-light emission.
- **Hyperspace:** gate, slice, field effect, and matching DXR emission.

For the four effect channels, a single switch chooses between the authored
color and a custom player color. The visible raster effect and its ray-traced
light use the same selection automatically; there is no separate lighting menu
to synchronize. Overrides apply only to the local player's ships. Enemy and
allied fleets retain their authored identities.

## Capture / Build All

This single-player Fleet Control option turns exceptional captured production
ships into usable factories. Enable **Capture / Build All**, salvage an enemy
carrier or mothership, and the captured hull appears with your owned factories
while retaining its native build roster. In other words, you can quite
literally steal a carrier or mothership and build from it.

The roster belongs to the captured factory rather than being replaced by the
player's ordinary ship list. Turanic and Kadeshi production hulls therefore
build their own native ships. If the option is turned off later, captured hulls
remain yours and already queued jobs may finish, but foreign factories are
hidden from new build selection and cannot accept new construction orders until
the option is re-enabled. Multiplayer retains its original owner-race rules.

## Gameplay options

These additions are opt-in and saved with the user configuration:

- **Unlimited Strike-Craft Fuel:** single-player only; normal orders and docking
  behavior remain available.
- **Issue Orders While Paused:** accept tactical commands while simulation time
  is stopped.
- **Ship Weapon Recoil:** enable physical firing impulse.
- **Cursor Scale:** 25-100% live preview.
- **Resource Multiplier:** 1x-4x deposited RU yield and collector harvest/fill
  rate; authored resource values are unchanged; single-player only.
- **Super Salvagers:** 2x mobility/agility/speed/braking and 3x effective health.
- **Capture / Build All:** captured enemy carriers and exceptional production
  motherships become usable factories with their native build rosters; see the
  dedicated section above for exact enable/disable behavior.

Simulation-changing convenience options are kept out of multiplayer behavior.

## Command-line examples

```powershell
# 1440p resizable window, uncapped, no VSync
./HomeworldModern.exe /window /width 2560 /height 1440 /noVsync /fps 0

# 4K exclusive mode at 120 Hz
./HomeworldModern.exe /exclusive /width 3840 /height 2160 /refresh 120 /fps 120 /vsync

# Cross-vendor presentation without DXR lighting
./HomeworldModern.exe /noRaytracing /fxaa

# Heavier path tracing
./HomeworldModern.exe /raytracing /pathSamples 4 /pathBounces 3 /dlssQuality
```

Frequently useful switches:

| Switch | Meaning |
| --- | --- |
| `/window` | Resizable window |
| `/borderless` or `/fullscreen` | Borderless desktop mode |
| `/exclusive` | Exclusive fullscreen |
| `/width N`, `/height N` | Requested dimensions |
| `/refresh N` | Exclusive refresh; zero selects automatically |
| `/fps N` | Presentation cap; zero is uncapped |
| `/uiScale N` | 50-200% modifier over automatic scale |
| `/raytracing`, `/noRaytracing` | Enable/disable DXR lighting |
| `/pathSamples N` | 1-4 samples per pixel per frame |
| `/pathBounces N` | 1-3 diffuse bounces |
| `/fxLightStrength N` | 0-200% FX/emissive light contribution |
| `/dlaa`, `/fxaa`, `/aaOff` | AA selection |
| `/dlssQuality`, `/dlssBalanced` | DLSS quality modes |
| `/dlssPerformance`, `/dlssUltraPerformance` | DLSS performance modes |
| `/vsync`, `/adaptiveVsync`, `/noVsync` | Presentation synchronization |
| `/vanilla` | Restore the original universe update rate |

The Video and Shift+F12 interfaces are preferred for normal use because they
show allowed ranges and save the configuration.

## Files created by the game

| Location | Purpose |
| --- | --- |
| `Documents\My Games\Homeworld Modern\Homeworld.cfg` | Saved options |
| `Documents\My Games\Homeworld Modern\installation.ini` | Validated data location |
| `Documents\My Games\Homeworld Modern\RTXLighting\MissionNN.rtxlight` | Per-user mission lighting overrides |
| `RTXExports\Global\RTX-Visual-Settings.txt` | Portable global visual/shadow export |
| `RTXExports\Missions\MissionNN.rtxlight` | Portable mission-light export |
| `RTXExports\Maps\MissionNN.rtxmap` | Non-destructive map overlay export |
| `RTXExports\CloudPresets\` | Reusable volumetric dust presets |
| `%LOCALAPPDATA%\HomeworldModern\ShaderCache` | Versioned presenter shader cache |

## Diagnostics

Source-tree users can launch through:

```powershell
./Windows/run-with-log.ps1 -Configuration Release -DataPath "D:\Games\HomeworldClassic\Data"
```

The script records startup stages, stdout/stderr, arguments, OS, GPU, display,
exit code, and crash dump information beneath `out\logs`, then prepares a ZIP.
Use `-Windowed` when testing whether a failure is specific to fullscreen.

For a binary-release report, provide your GPU, driver version, Windows version,
mission, exact settings, and reproduction steps. Never attach retail data.

## Next reading

- [Editing Guide](EDITING_GUIDE.md)
- [Known Limitations](LIMITATIONS.md)
- [Build Guide](../Windows/BUILD_MODERN.md)
- [Graphics Feature Status](../documentation/GRAPHICS_FEATURE_STATUS.md)
