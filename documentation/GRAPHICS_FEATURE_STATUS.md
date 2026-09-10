# Homeworld Modern graphics feature status

This file distinguishes implemented behavior from planned renderer work. A
setting is not exposed to players until the renderer path behind it works and
has a fallback.

| Feature | Status in 0.91.3 Beta 4 | Delivery gate |
| --- | --- | --- |
| Windows 11 x64 CMake target and manifest | Implemented; Windows build verification required | Clean VS 2022 Debug and Release builds |
| First-run resolution/refresh detection | Implemented for the primary active display | Windows 11 multi-monitor tests |
| Arbitrary window resolution | Implemented in compatibility renderer | Resize and DPI tests on Windows 11 |
| Borderless/exclusive/windowed | Borderless default implemented | Multi-monitor and Alt+Tab tests |
| Resolution-aware UI scaling | Implemented for FE geometry, hit regions, fonts, cursor, manager dock, and all responsive screen templates | Campaign-wide 16:9/21:9/4K visual review |
| Modern UI replacement | Legacy decorative atlases are bypassed; front-end, multiplayer, options, in-game menu and modal screens are recomposed by the responsive shell while callbacks remain intact; Build/Research/Launch use named non-overlapping columns and scalable code-rendered titles/buttons over official-art plates | Campaign-wide visual and localization review |
| Official-art UI plates | Startup, main, campaign, fleet, systems and archive imagery is composed only from the supplied official archive and selected deterministically by context; no generated artwork remains | Campaign-wide composition/cropping review |
| Startup splash | Native Win32 splash opens before renderer setup, reports real subsystem stages, and closes at main-loop readiness | Windows DPI/multi-monitor startup review |
| Bink campaign movies | FFmpeg avformat/avcodec/swscale path is enabled by default with source timing, aspect preservation, subtitle/script updates and skip input | Validate every language/mission `.bik` set on Windows |
| Shader cache | Versioned presenter CSO cache persists in LocalAppData with signature/size validation; unsafe driver-specific pipeline-library persistence is disabled | Cold/warm startup timing across driver updates |
| B/L/R manager dock, persistent Info bar, and Ship Dossier | Implemented for normal gameplay; B/L/R toggles right-docked managers with independent quantity/arrows/cost geometry, the Info bar remains contextual, and the selection-driven right-click action opens a manual-derived dossier in the same dock; old tutorial manager flow retained | Tutorial/campaign manager interaction and visual review |
| Gameplay options | Native responsive panel includes camera sensitivity, paused orders, weapon recoil, persistent Info Manager policy, and single-player-only unlimited strike-craft fuel (default off, ignored in multiplayer) | Campaign/save/load and multiplayer-host validation |
| Fleet-identity palette | Native responsive editor exposes unrestricted hull base/stripe colors plus an independent player engine-emission override and an authored/custom player-nav-light switch. Engine choice drives the visible ribbon/glow and DXR emitter; enemy/allied nav lights remain authored | Single-player preview and campaign-wide FX validation |
| VSync and presentation cap | Implemented at DXGI presentation boundary | Frame-time capture; simulation checksum comparison |
| D3D12/DXR hardware detection | Implemented | Test on DXR and non-DXR adapters |
| D3D12 visible presentation | Implemented: device, queue, triple-buffered flip swap chain, per-slot fences/resources, synchronized command lists, and resize | Windows validation, Alt+Tab/resize, multi-frame capture |
| OpenGL-to-D3D12 compatibility capture | Superseded experimentally by RTX-0058 native raster when `HW_ENABLE_D3D12_NATIVE_RASTER=ON`; the old CPU readback/upload bridge remains available only in the build-time OpenGL fallback | Windows/MSVC compile plus campaign-wide visual/performance A/B validation |
| Highest-detail geometry policy | LOD0 forced globally; panic LOD disabled | Campaign and dense-battle visual/performance review |
| N-LIPS distance scaling | Disabled for ships, effects, particles, trails, and engine glows | Verify transforms and selection geometry at extreme zoom |
| Complete HSF map-light extraction | Implemented and consumed by DXR; all authored records and spot-cone fields preserved | Campaign-wide intensity/direction comparison |
| Live lighting editor | Shift+F12 opens a non-pausing overlay with immediate environment, authored-light, FX/emissive, surface reflectivity/roughness, generated-normal and exposure controls; values persist in the options file | Campaign-wide tuning and sensible preset pass |
| Dynamic FX light extraction | Engines, layered plasma trails, muzzle FX, weapons, beams, and explosions illuminate the DXR scene. Trail nozzle points and illuminated plume lines use the player emission override | Tune per-effect energy/range against reference captures |
| Navigation lights | Active lights cast from exact MEX positions transformed by the ship coordinate system; flash/fade state is retained; the player override modulates both the visible authored mask and emitted light | Fleet-scale range/energy and light-budget tuning |
| Ship emissive material extraction | Actual per-triangle GEO materials/UVs and LIF emissive texels shade emissively; ranked light prototypes are cached per material variant and transformed per instance | Area-light sampling and photometric tuning |
| DXR geometry and path integration | Cached per-object BLAS, triple-buffered TLAS full-build/refit resources, reusable mapped uploads/shader tables, alpha-tested any-hit shaders, direct visibility, motion-reprojected temporal denoising, mission-environment radiance, and up to three diffuse radiance bounces are implemented | Animated/morphed BLAS updates and denoising quality/performance validation |
| World/UI ray-composite separation | Experimental native command-snapshot replay now produces background/world/occluder/final D3D12 surfaces without framebuffer readback; the legacy capture path remains in the build-time fallback | Campaign-wide ordering/parity validation, especially FE/UI and standalone render loops |
| Native D3D12 gameplay draws | Experimental RTX-0058 fixed-function compatibility recorder/replayer covers the active legacy draw vocabulary and runs without an OpenGL context when enabled by default | Windows/MSVC compile; raster parity for every campaign/UI/effect path; replace compatibility vocabulary with backend-neutral subsystem submissions over time |
| HDR10/scRGB output | Not implemented | Linear-light framebuffer and HDR display validation |
| TAA and dynamic resolution | Not implemented | Reactive mask and dynamic-resolution policy |
| Anti-aliasing and DLSS | Off and FXAA are cross-vendor; Streamline 2.12 provides native DLAA plus Quality, Balanced, Performance, and Ultra Performance reconstruction of the lower-resolution path-light signal. The raster world and post-AA UI remain native | Signed-runtime/MSVC build, NVIDIA quality/performance validation, and reactive-mask tuning |
| Reflex | Not implemented | Latency markers and end-to-end frame pacing validation |
| Ray-traced shadows/reflections/GI | Direct visibility and multi-bounce diffuse indirect light are implemented; specular reflection materials are not | Material roughness/metalness mapping and denoising validation |
| Progressive path tracing | Implemented with 1-4 stable samples per pixel, 1-3 radiance bounces, emissive hits, Russian roulette, depth-validated motion history, and accumulation reset | NVIDIA hardware performance and temporal stability validation |
| Generated normal stability | Texture-luminance normals use a smoothed Sobel footprint, a 30-degree tilt ceiling, primary-hit-only shading, bounded neutral-light relief, geometric shadow offsets, and geometric bounce directions. Recursive diffuse return is energy-bounded | Campaign-wide material tuning and close/far motion validation |
| Ray Reconstruction | Not implemented | Path-traced signal and required auxiliary buffers validated |
| Frame Generation | Disabled in the accepted v19 baseline; the retired universal interpolation path is not exposed as vendor frame generation | A future implementation requires a real supported frame-generation integration plus correct motion/depth/UI separation and latency handling |
| FX/emissive lighting strength | Implemented as live Video-menu slider and `/fxLightStrength 0-200` | Per-effect photometric tuning |
| Scene post effects | Chromatic aberration, DXR motion-vector blur, luminance-response-controlled film grain, and mission-dome god rays are live Video controls; all default off and UI is excluded. Planets/world geometry are excluded from source detection and the complete raster world occludes the radial march | Campaign-wide source detection, artifact and performance tuning |
| Campaign backgrounds | Original BTG visuals and stars are restored for all missions; generated HDR metadata remains active for editable mission key/ambient lighting, dust, god rays, and DXR. The defective dual-paraboloid visual atlas is bypassed | Campaign-wide BTG/DXR lighting comparison and future atlas regeneration |
| Planet upgrades | Original Homeworld planet GEO silhouettes, material layout, and six-tile UV mapping are retained beneath high-resolution DDS and regenerated normal overrides | Campaign-wide planet material, seam, lighting, and scale review |
| Volumetric dust | World-locked FP16 density, half-resolution integration, stable depth occlusion, ship wakes, mission/local lighting, noise-displaced boundaries, and RTXMAP 9 distance fade controls are implemented | Extreme-setting quality/performance validation across all authored shapes |
| Replacement asteroid LOD | Four authored asteroid meshes each provide three decimated UV-preserving distance LODs; projected-size selection retains gameplay radii and avoids unrelated sphere fallbacks | Mission 06 field performance and transition review |
| Camera wheel input | SDL wheel deltas accumulate per frame and all notches are applied with exponential zoom, so high-resolution/fast wheel input is not collapsed or dropped | Device coverage for detented and free-spin wheels |

## Source migration inventory

The legacy renderer is not a single module. Approximately 2,000 direct OpenGL
calls are spread across 40 source files. The highest-density migration units
are `prim2d.c`, `render.c`, `Mesh.c`, `prim3d.c`, `Particle.c`, `mainrgn.c`,
`texreg.c`, `Clouds.c`, `Trails.c`, and `BTG.c`.

The migration order is therefore:

1. Window/device ownership and an API-neutral frame lifecycle. D3D12/DXGI owns
   visible presentation; RTX-0058 experimentally removes the runtime OpenGL
   context and replays legacy fixed-function draw vocabulary directly through
   D3D12 rather than a framebuffer capture bridge.
2. UI/2D batching and texture registry.
3. Mesh/material submission, camera data, and object transforms. The first
   DXR extraction adapter is active in 0.8.1.
4. Backgrounds, particles, trails, beams, and NIS rendering. Nav-light scene
   extraction is active in 0.8.1.
5. Temporal depth/motion buffers, NVIDIA DLAA and DLSS path-light render-resolution modes.
6. Progressive multi-bounce DXR integration with temporal reprojection and
   environment radiance. A depth-aware spatial filter now complements temporal
   accumulation; specular material transport and an AI ray denoiser remain.

The compatibility OpenGL renderer remains a build-time visual reference until
the native D3D12 path passes campaign-wide comparison testing.  The experimental
native path is intentionally not considered parity-complete yet: screenshot
readback, uncommon fixed-function states, line/point presentation details,
texture mip generation, and framebuffer-object edge cases still require native
implementations or validation.
