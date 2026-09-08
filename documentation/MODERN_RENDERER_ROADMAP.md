# Modern renderer architecture and roadmap

## Objective

Preserve Homeworld's game, campaign, asset, save, and simulation behavior while
replacing the 1999 fixed-function graphics path with a Windows-native renderer
capable of Direct3D 12 rasterization, DirectX Raytracing, real-time path tracing,
DLSS/DLAA, high-DPI output, arbitrary aspect ratios, and configurable frame
pacing.

The legacy OpenGL renderer remains available as a build-time visual and behavioral
reference until the D3D12 path reaches parity.  RTX-0058 introduces an
**experimental native D3D12 fixed-function compatibility rasterizer** which is
ON by default: legacy render call sites retain their existing vocabulary, but
those calls are recorded and replayed into D3D12 render targets rather than an
OpenGL context.  The old OpenGL backend can still be compiled explicitly for
A/B comparison with `HW_ENABLE_D3D12_NATIVE_RASTER=OFF`.

## Non-negotiable separation

The simulation and presentation clocks must remain independent:

- Gameplay runs at its existing fixed rate: normally 64 Hz in HomeworldSDL or
  16 Hz with `/vanilla`.
- Rendering may run at any supported presentation rate.
- The renderer consumes immutable previous/current simulation snapshots and an
  interpolation fraction. It must never advance game state.
- Save games, deterministic demos, multiplayer checksums, AI, physics, mission
  scripts, and weapon timing continue to use fixed simulation time.

This separation is also required for stable motion vectors, temporal
anti-aliasing, DLSS, Ray Reconstruction, and Frame Generation.

## Backend boundary

Game code should emit a backend-neutral frame description rather than call a
graphics API directly. The frame contract will contain:

- Camera state: current and previous view/projection matrices, unjittered
  matrices, near/far policy, exposure, and jitter
- Instances: stable object ID, mesh, current/previous transform, visibility,
  team color, animation/morph state, and material overrides
- Lights: type, transform, color, intensity, range, and shadow flags
- Effects: particles, trails, beams, nav lights, damage effects, engine glows,
  and emissive contribution
- UI: ordered 2D draw commands in a separate composition layer
- Environment: BTG/background data, stars, nebulae, fog, and mission lighting

The first extraction adapter may translate legacy OpenGL-style state changes
into this frame description. Direct game-to-OpenGL calls are then removed a
subsystem at a time.

## Asset interpretation

Classic Homeworld assets do not contain a complete physically based material
model. The modern renderer therefore needs deterministic defaults:

- Preserve original diffuse texture, team-color masks, alpha, additive, and
  unlit behavior.
- Derive conservative roughness/metalness defaults from original material and
  blend modes.
- Allow optional, non-destructive sidecar material overrides keyed by stable
  asset/material names.
- Keep original meshes and textures usable without an asset conversion step.
- Treat engine glows, nav lights, beams, explosions, and configured glow maps
  as optional emissive light sources in the path tracer.
- Provide a dedicated global **FX/emissive lighting strength** slider whenever
  those effects contribute to path-traced lighting. A value of zero disables
  only their lighting contribution, not their visible sprites or effects.

## Staged implementation

### Stage 0: native compatibility baseline

- Visual Studio 2022/2026 x64 CMake/vcpkg build
- SDL2 window/input/audio and an experimental D3D12 fixed-function compatibility
  rasterizer; the previous OpenGL renderer remains a build-time fallback/reference
- Per-monitor DPI awareness
- Windowed, borderless, and exclusive modes
- Arbitrary supported resolutions, refresh selection, VSync modes, and frame cap
- Repeatable asset validation and startup diagnostics
- Globally stable LOD0 geometry and removal of N-LIPS distance enlargement
- D3D12 device, direct queue, synchronized frame commands, and a
  triple-buffered DXGI flip-discard swap chain owning visible presentation
- RTX-0058 experimental native raster path removes the runtime OpenGL context,
  `SDL_GL_SwapWindow`, and OpenGL framebuffer readback/upload bridge when
  `HW_ENABLE_D3D12_NATIVE_RASTER=ON`; legacy draw semantics are translated into
  native D3D12 commands while subsystem-by-subsystem backend-neutral extraction
  continues
- Scaled right-side B/L/R manager dock and persistent normal-play Info bar
- Complete HSF map-light preservation plus dynamic FX, exact active MEX nav
  lights, per-triangle GEO materials/UVs, and LIF emissive/alpha texels consumed
  by the 0.8.1 progressive DXR pass

### Stage 1: frame extraction and interpolation

- Backend-neutral renderer interface and frame packet
- Stable render object IDs
- Previous/current transforms for ships, weapons, resource objects, particles,
  and camera state
- Fixed-tick interpolation without simulation changes
- RenderDoc-friendly frame markers and a deterministic reference capture mode

### Stage 2: D3D12 raster parity

- DXGI adapter/output selection and flip-model swap chain
- DirectX 12 Agility SDK and DXC shader pipeline
- Descriptor/resource lifetime management and upload streaming
- HLSL equivalents for legacy transforms, fog, lighting, team colors, alpha,
  additive effects, UI, backgrounds, and picking
- HDR-ready linear lighting pipeline with SDR output first
- OpenGL/D3D12 comparison captures for every campaign rendering subsystem

### Stage 3: temporal inputs and reconstruction

- Jittered and unjittered camera matrices
- Per-pixel motion vectors including moving/morphing objects
- Linear depth, exposure, reactive/transparency masks, and disocclusion handling
- Native TAA fallback and dynamic internal render resolution
- Clean separation of world rendering and UI composition

Depth and object/camera motion guidance are active for native DLAA. Reactive
masks and dynamic internal resolution remain prerequisites for DLSS Super
Resolution and Frame Generation.

### Stage 4: DXR scene extraction and lighting

- BLAS generation/caching for static polygon objects and exact per-frame TLAS
  transforms are active in 0.8.1; animated/morphed BLAS updates remain
- Direct visibility plus up to three diffuse radiance bounces are active;
  specular material transport remains
- Alpha-tested any-hit, per-triangle emissive LIF handling and ranked emissive
  surface samples are active
- Denoising and history invalidation for camera cuts, hyperspace, mission loads,
  and resolution changes

### Stage 5: path tracer (progressive diffuse integrator active)

- Physically based BSDF evaluation mapped from classic materials
- Direct-light sampling is active; multiple importance sampling remains
- Emissive geometry/effects, environment lighting, transparent effects, and fog
- Temporal accumulation and scalable bounce/sample settings are active;
  production denoising and firefly control remain
- Raster fallback on hardware without supported raytracing

### Stage 6: NVIDIA Streamline and DLSS (path-light upscaling active)

- Native-resolution DLAA and DLSS Quality, Balanced, Performance, and Ultra
  Performance are active through signed Streamline 2.12 binaries. DLSS modes
  reduce the internal DXR path-light resolution and reconstruct it to the
  native raster world; FXAA is the cross-vendor fallback
- Reflex integration and correct frame markers
- Ray Reconstruction for the path-traced signal when its required buffers are
  correct
- Frame Generation only after UI separation, motion vectors, depth, frame
  indices, reset behavior, and latency markers are validated
- Capability detection with native/TAA rendering when an NVIDIA feature is
  unavailable

The bootstrap script downloads NVIDIA's signed Streamline release, validates
its SHA-256 digest, and copies only the required runtime files beside the game
at build time. Vendor binaries are not committed to the source archive.

## Acceptance gates

Each stage must pass these gates before the next becomes the default:

1. Clean Visual Studio x64 Debug and Release builds.
2. Startup and asset loading from original 1.05 and Remastered Classic data.
3. Front end, tutorial, all campaign missions, skirmish, save/load, NIS, audio,
   and common effects complete without a regression.
4. Simulation checksums remain unchanged when only presentation settings vary.
5. Window resize, mode changes, Alt+Tab, DPI changes, and monitor moves recover
   without device/resource loss.
6. Automated reference captures show known and reviewed visual differences.
