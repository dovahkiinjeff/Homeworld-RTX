# Homeworld Modern 0.91.3 Windows source package

This is a complete Windows build tree, not a source overlay. Extract the
archive to its own directory, then run these commands from that directory in
PowerShell:

```powershell
& '.\Windows\bootstrap.ps1'
& '.\Windows\build.ps1' -Configuration Release
```

The bootstrap keeps vcpkg in the shared no-space path
`%LOCALAPPDATA%\HomeworldModernDeps\vcpkg`. This is required because FFmpeg's
vcpkg port rejects dependency build paths containing spaces, even though the
source package itself may remain in a folder such as `HW1 RTX`.
The manifest installation tree is likewise kept under
`%LOCALAPPDATA%\HomeworldModernDeps\installed`, so FFmpeg never receives a
spaced library path through its compiler or linker flags.

Run the game:

```powershell
& '.\out\build\windows-x64\Release\HomeworldModern.exe'
```

On first launch, select the legally installed Homeworld 1 Classic
`Homeworld.exe`. Homeworld Modern validates that executable and the adjacent
`Data` set, then remembers the installation in
`Documents\My Games\Homeworld Modern\installation.ini`. The Remastered
Collection Classic set needs `Homeworld.big`, `HW_Comp.vce`, and
`HW_Music.wxd`; it does not need a separate `Update.big`.

This package contains the Windows source tree and project-owned generated art.
It intentionally excludes copyrighted game data, generated build output,
dependency caches, Linux/macOS/Web build trees, historical Visual Studio
projects, and unrelated legacy utilities.

The 1999 interface artwork is no longer the visible design system. All
front-end, campaign, tutorial, multiplayer, options, in-game menu and modal
screens are composed by responsive code-native templates while the original
callbacks and list models remain functional. Shared buttons, panels, toggles,
sliders, lists, text fields, menus, HUD elements and right-docked managers use
the replacement matte editorial language.

Build, Research, and Launch managers are laid out from named responsive
regions instead of inherited anonymous FIB coordinates. Queue counts,
decrement/increment arrows, costs, totals, scrollbars, and action buttons each
own a separate column, while scalable engine text replaces the old raster
manager titles and captions. The native Gameplay screen includes an Unlimited
Strike-Craft Fuel switch that defaults off and is ignored in multiplayer.
The native fleet-identity palette exposes the complete HSV value range for
primary and stripe colors, including true black. Engine-trail emission is a
separate optional override that drives both visible ribbons/glows and DXR
lights. Navigation lights retain their authored colors by default and offer a
separate player-ships-only override; the selected color modulates both the
visible lamp sprite and its emitted light.
Engine trails use a nine-shell feathered resolution-independent plasma ribbon with a
bright nozzle core at every LOD; the same player-selected emission color
drives its raster glow and DXR point/line lighting.
Harvest beams and hyperspace transitions now use the same player-only color
contract: each can retain its authored palette or use a custom local color,
with the visible effect and its DXR line/surface emitter kept in sync.

With any ship selection active, the tactical right-click menu opens anywhere
in the viewport. Its Ship Dossier action occupies the same right-side dock as
Build, Research, and Launch and reports loaded ship statistics plus concise
Fleet Archive assessments derived from the original 1999 manual.

The UI presentation artwork itself is not generated. Startup, galaxy,
campaign, fleet, systems, and archive plates are deterministic compositions
made only from the official Homeworld concept-art archive supplied for this
project; an included provenance table identifies every source image. Campaign
sky assets are separately generated project artwork whose presets and source
material are included under `tools/`. Eurosecond and Eurose Condensed remain
the in-engine display/data faces. A native Windows splash reports real startup
stages while game data and graphics systems load.
When a legally obtained `Movies` folder is present, FFmpeg decodes the original
Bink `.bik` pre-mission animatics with their scripted speech and subtitles.

Direct3D 12/DXGI owns visible presentation. OpenGL still rasterizes the
compatibility frame, while DXR builds BLAS/TLAS geometry from the exact visible
mesh transforms and integrates progressive alpha-tested multi-bounce diffuse
light.
Actual per-triangle LIF material texels, glow maps, active MEX navigation
lights, authored map lights, and dynamic FX all feed the live ray pass. A
world-only capture keeps UI pixels outside the ray-light composite. Mission
backgrounds now provide environment radiance and drive the optional god-ray
source detector. NVIDIA
Streamline DLAA uses DXR depth/motion guidance on the native world image.
DLSS Quality, Balanced, Performance, and Ultra Performance reconstruct the
internal path-lighting signal to native resolution; FXAA and Off are also
available. In the accepted v19 baseline the old universal interpolation-based
frame-generation path is intentionally unavailable: requests are forced back to
Off rather than presenting the retired interpolation path as vendor frame
generation. The native-resolution UI remains composited outside temporal scene
reconstruction.
Background textures are linearly filtered and receive stable sub-LSB scene
dithering to reduce blockiness and color banding without animated grain.
God-ray source detection captures the luminous mission dome before planets and
world geometry, so engines, trails, and bright planet discs cannot become false
suns. The radial march uses the complete raster world as an occlusion mask, so
shafts do not render through hulls or planets. Projectile
spawn events now retain DXR muzzle-light pulses even when optional legacy
effect artwork is absent, including fighter/scout guns. Gameplay uses an
infinite-far projection and no longer removes objects at the original
10,000-20,000-unit render-list limits. Specular PBR transport, HDR, DLSS Ray
Reconstruction and native D3D12 raster draws remain planned;
the current renderer includes motion-reprojected temporal history, depth-aware
spatial filtering, and radiance clamping.

All sixteen campaign missions use their own fixed 4096x2048 BC7 360-degree
sky built directly from the original BTG mesh, palette, blending energy and
star positions. No generic replacement artwork is used: the red
Gardens/Cathedral of Kadesh arc and the yellow-orange Galactic Core progression
retain their original campaign identities. Missing assets or insufficient
BC7 texture hardware fall back to the original BTG. Intact Kharak uses a
36,864-triangle procedural sphere and a generated
4096x2048 desert albedo, while scarred Kharak remains a separate original
story-state asset.
Generated normal detail is Sobel-smoothed and tilt-limited, affects only
primary-surface shading, receives a bounded neutral-light relief response, and
cannot alter shadow offsets or indirect bounce directions. Diffuse return
energy is bounded before recursion to prevent camera-motion sparkle and
exposure pumping. Its Video and Shift+F12 controls span 0-400%, with a
sculpted 100% clean-install baseline; existing saved values are preserved.
Repeated DXR instances are now history-matched by geometry, material, and
nearest transform instead of unstable render-list position. Screen-stable path
sampling, depth-validated motion history, and lower indirect gain remove the
remaining zoom flicker even when generated-normal strength is already zero.
Compiled presenter shaders are cached under
`%LOCALAPPDATA%\HomeworldModern\ShaderCache`. Driver-specific D3D12 pipeline
library blobs are deliberately not persisted because a blob accepted and
written by one run can crash inside D3D12Core when reloaded later.

The 0.12.0 optimization pass removes the whole-GPU fence previously taken on
every path-traced frame. DXR upload buffers, shader tables, and TLAS resources
are persistently mapped and triple-buffered with the DXGI back buffers. Stable
scene topology uses in-place TLAS refits, and texture-driven emissive light
prototypes are sampled once per material variant instead of rescanning every
triangle every frame. Disabled DXR/post-effect combinations also skip
unnecessary background and world-only OpenGL readbacks. A 600-frame telemetry
line reports the remaining compatibility-readback and DXR staging costs.
