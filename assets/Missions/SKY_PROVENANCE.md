# HomeworldModern Mission Sky Provenance - RTX-0067

RTX-0067 keeps the authored Homeworld 1 EZ01-EZ16 campaign compositions while
replacing the failed RTX-0065 octahedral visible-sky mapping with a pole-safe
native D3D12 dual-paraboloid environment.

## Runtime background representation

- Format: DDS DX10 `DXGI_FORMAT_BC6H_UF16` (unsigned HDR BC6H)
- Mip count: 1
- Storage: one `Texture2D` per mission
- Layout: two paraboloid hemispheres packed side-by-side in one 2:1 image
- Final dimensions: **4096 x 2048**
- Left 2048-square half: +Z hemisphere
- Right 2048-square half: -Z hemisphere
- Projection extent: **1.0625**, providing 6.25% analytic continuation beyond
  each equator boundary for filtering/compression support
- Runtime equator blend band: `abs(direction.z) < 0.035`
- Mission 3 is a byte-identical copy of Mission 1 because both use EZ01.

There is no sky sphere and no cubemap. A fullscreen native D3D12 pass
reconstructs the camera ray for each pixel, rotates it into world space, maps it
to the appropriate paraboloid hemisphere, and samples the HDR `Texture2D`.
The north and south poles are ordinary interior texels at the centers of the
two hemispheres, so looking straight up or down does not cross a fold or a
latitude/longitude singularity.

## Seam handling

The two paraboloid projections meet at the world-space equator. Each encoded
hemisphere extends analytically beyond its nominal equator before BC6H
compression, so bilinear filtering and 4x4 BC6H blocks near the boundary contain
real neighboring spherical radiance instead of clamp colors. The shader samples
both mathematically equivalent hemispheres in a narrow equator band and blends
between them continuously, preventing an abrupt encoded seam even when the two
BC6H blocks quantize slightly differently.

## Source fidelity and HDR generation

The backgrounds are regenerated directly from the original EZ BTG data in
float32 scene-linear light. Polygon interpolation, smoothing, resizing and
restrained microstructure remain floating point through the BC6H encoder. No
8-bit RGB image is used as an intermediate.

RTX-0067 deliberately excludes point stars from the BC6H background. Tiny,
high-radiance stars are a poor match for block compression and were responsible
for the square/chunky stellar appearance seen in RTX-0065. The original BTG star
catalog is instead rendered at runtime as a separate additive HDR layer using
its authored positions, colors and apparent sizes plus a new smooth analytic
stellar kernel. The nebula/background therefore receives the full BC6H precision
budget while stars remain round, antialiased and HDR-bright.

`tools/generate_mission_skies.py` is the authoritative offline generator for the
RTX-0067 dual-paraboloid background and its lighting metadata.

## Lighting metadata

Every `missionXX_sky.hdrmeta` sidecar is derived from a separate analysis image
that contains the float32 background plus the original BTG star catalog. The
sidecar stores the mission-owned bright-source world direction, linear key
radiance and linear ambient radiance before BC6H compression. Visual god rays
continue to use that authored world direction.

The DXR direct-light path converts that sky-source convention into the shared
ship-light convention at one point in the renderer so all ships receive the
same directional-light correction rather than per-ship exceptions.

## Foreground coverage

The internal RGBA16F raster target uses alpha as explicit foreground coverage:
the mission sky and additive star layer preserve zero background coverage,
opaque legacy geometry writes full coverage, ordinary alpha-blended effects
accumulate their true coverage, and additive effects leave coverage unchanged.
The presenter uses that coverage to convert only the uncovered HDR sky fraction
to display space. This prevents translucent dust, trails and other FX from
showing their rectangular billboard bounds over the linear-HDR sky.
