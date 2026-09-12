# Homeworld RTX 0.92.0 - Public Beta 1

> [!CAUTION]
> ## REQUIRED FOR EVERY NEW EXTRACT
> Before first launch, manually copy your legally owned `HW_Music.wxd` and
> `HW_Comp.vce` into the **same directory as `HomeworldModern.exe`**. Steam
> users normally find them under `Homeworld1Classic\Data`. These copyrighted
> retail archives are not included and must not be redistributed.

Version 0.92.0 Public Beta 1 brings the modern reconstruction and frame-generation stack
together with the project's completed 4x gameplay-asset pass. It also packages
the recent weapon-lighting, off-screen shadow, authored asteroid-LOD, campaign
dust-overlay, mission-script, and weapon-FX stability work.

## Modern reconstruction and frame generation

- NVIDIA DLAA and DLSS, AMD FidelityFX Native AA and FSR, and Intel XeSS AA and
  XeSS-SR are working full-scene reconstruction choices.
- NVIDIA Streamline DLSS-G 2X and AMD FidelityFX frame generation are
  integrated as optional, independently selectable presentation features.
- FidelityFX is the packaged cross-vendor frame-generation route for supported
  AMD **and Intel** adapters. Native Intel XeSS-FG is shown honestly as
  runtime-gated until its separate provider is bundled.
- Image Reconstruction and Frame Generation now use readable, vertically
  expanding selectors rather than cycling through a long single-line list.
- DLSS scene jitter is applied exactly once, removing the whole-screen shaking
  caused by the previous duplicate offset.

## Complete 4x gameplay-asset foundation

The modernized gameplay art is delivered at four times its vanilla linear
resolution: ships, the mothership, derelicts, scaffolds, debris, planets,
asteroids, and weapon FX all participate in the upgraded DDS pipeline. Opaque
surface assets receive literal tangent-space normals generated from their final
upscaled color texture wherever a normal map is appropriate. Transparent FX,
team/emissive/material masks, UI, and other technical images are intentionally
preserved according to their role and are not given meaningless normal maps.

BC7 color, BC5 normals, BC4 masks, complete mip chains, canonical aliases, and
the memory-mapped `HomeworldTextures.hwt` archive keep the expanded asset set
practical at runtime.

## Gameplay, lighting, and stability since Beta 4

- Weapon muzzle/origin and impact lights have independent live intensity and
  range controls across guns, missiles, and sustained beams.
- Dark legacy projectile definitions receive a bounded hot-ordnance fallback,
  restoring illumination to affected ships such as the Kushan Assault Frigate.
- Off-screen ships, derelicts, and asteroids remain eligible DXR shadow casters;
  dense asteroid fields use inexpensive authored LODs for shadow-only work.
- All weapon-FX textures use the 4x pipeline without generated normals. The
  particle registry preserves vanilla animation coordinates while tracking the
  replacement surface size, preventing the Scout muzzle-fire crash.
- Authored Mission 1 dust is packaged as a default overlay, Mission 3 carries
  the shared authored placement, and Mission 6 retains its authored dust map.
- Switching authoring tools restores the mission/KAS simulation state so
  campaign objectives continue advancing.

## Road to 1.0

The known remaining release-candidate work is deliberately small and focused:

1. One final performance and loading optimization pass.
2. Capture and production support for ships belonging to the race opposite the
   player's selected starting race.
3. Intentional dust-cloud placement and tuning on the remaining missions.
4. One final campaign-wide lighting pass.

Beta testing can still reveal blockers, but no additional renderer or asset
pipeline rewrite is currently planned for 1.0.

## Watch the project

[![Homeworld RTX project showcase](https://img.youtube.com/vi/R4PvuIpeAaA/maxresdefault.jpg)](https://www.youtube.com/watch?v=R4PvuIpeAaA)

## Package contents

- `HomeworldModern.exe` and matching PDB symbols
- `HomeworldTextures.hwt` optimized texture archive and upgraded assets
- NVIDIA Streamline/DLSS/DLSS-G, AMD FidelityFX/FSR/frame-generation, and Intel
  XeSS-SR runtime files
- `kas2c.exe`, authoring and texture tools, licenses, and third-party notices
- Updated Markdown documentation and PDF manual
- Complete source through this Git tag and GitHub's automatic source archives

Windows 10/11 x64 remains the supported platform. Read `docs/USER_GUIDE.md`
and `docs/LIMITATIONS.md` before reporting issues.
