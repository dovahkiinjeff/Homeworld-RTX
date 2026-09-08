# Original Homeworld source baseline

The untouched `homeworld-master.zip` archive supplied for the Windows
modernization was validated on 2026-08-09 with this SHA-256 digest:

```text
12a48c074c160227c68a6e4d3e45b9d70538bc2d1ed3ebbeded697d0993a3e22
```

It contains Relic's original Win32 game layer, RGL software/Direct3D/3dfx
renderers, tools, file-format documentation, libraries, and historical build
instructions. The archive is an authoritative reference for original runtime
behavior and on-disk data layouts.

## Integration policy

Homeworld Modern does not wholesale replace HomeworldSDL with this tree. The
original platform and renderer code depends on obsolete Visual C++, DirectDraw,
early Direct3D, 3dfx Glide, and 32-bit pointer-sized file layouts. Replacing the
SDL port would discard years of portability work without solving native x64 or
modern rendering.

Instead:

- Original game behavior and file-format definitions are the reference when
  auditing HomeworldSDL changes.
- SDL2 remains the compatibility platform layer for window, input, audio, and
  the transitional OpenGL renderer.
- Disk structures retain explicit 32-bit fields; x64 runtime structures use
  native pointers and conversion code uses byte cursors plus fixed-width
  offsets.
- The future D3D12/DXR renderer consumes backend-neutral frame data rather than
  reviving RGL, DirectDraw, or the original D3D backend.

## License boundary

The supplied original archive includes Relic's proprietary EULA, not an
open-source license. It describes non-commercial use and restricts
distribution. The original archive is therefore not bundled into Homeworld
Modern. Anyone considering public source or binary distribution should review
the included Homeworld license and obtain qualified legal advice.
