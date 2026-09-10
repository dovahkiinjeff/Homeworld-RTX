# Homeworld RTX Authoring Guide

This guide covers the two live authoring workspaces included in the public beta:

- **Shift+F11:** map, object, and volumetric-dust editing
- **Shift+F12:** mission key lighting, local lights, global material tuning, and shadows

Both tools work as non-destructive overlays. They do not rewrite `Homeworld.big`,
`Update.big`, HSF files, or other retail archives. Save frequently and retain
the exported text files in version control.

## Safe authoring workflow

1. Keep the release in a writable folder outside `Program Files`.
2. Start the campaign mission you want to edit.
3. Open the relevant editor.
4. Work with the simulation and KAS mission script paused when changing layout.
5. Make one class of change at a time and test at normal gameplay speed.
6. Save/export from inside the editor.
7. Copy the human-readable files from `RTXExports` into your project.
8. Reload the mission and verify the overlay from disk.

The editor uses runtime IDs to refer to original mission objects. A major change
to the underlying retail mission can invalidate those references; the original
archive remains untouched and is always the fallback.

# Map Editor - Shift+F11

## Workspace layout

The left panel has three tabs:

- **Assets:** placeable ships, derelicts, asteroids, dust, gas, and RTX volumes.
  The list is built from the table of contents of the legal BIG archives plus
  project procedural assets.
- **Scene:** original mission objects and objects added by the editor. Modified
  and deleted states are shown explicitly.
- **Volumes:** raymarched volumetric dust volumes and their material summary.

The right panel is an inspector. The top toolbar controls mission boot, pause
state, viewport mode, camera preset, save/load, and transition to Lighting.

## Toolbar

| Control | Behavior |
| --- | --- |
| Mission selector | Click advances Mission 01-16; hold Shift while clicking to go backward |
| Load Paused | Boots the selected mission into a clean authoring state |
| Sim Paused / Running | Controls gameplay simulation independently |
| KAS Paused / Running | Controls mission scripting independently |
| View | Cycles Lit, Wireframe, Bounds, and Volumes |
| Camera | Cycles Perspective, Top, Front, and Side |
| Save Map | Writes the current non-destructive overlay |
| Load Map | Reloads the current mission and reapplies the overlay |
| Lighting | Closes the map tool and opens Shift+F12 lighting authoring |
| Done | Closes the editor and restores the previous pause state |

## Camera and selection

- Hold the right mouse button and move the mouse to aim the editor camera.
- Use the mouse wheel over the viewport to fly forward/backward through the
  screen-center ray.
- Click an object or a projected volume marker to select it.
- Press `F` to focus the camera on the selection.
- Press `P` in Assets to place the selected asset at cursor depth.
- Press `P` in Scene or Volumes to move the selection to cursor depth.

The editor intentionally avoids WASD camera movement so list/inspector input
does not fight gameplay keys.

## Transforming objects

Selected world objects expose a direct 3D gizmo:

- Drag colored X/Y/Z arrowheads to translate.
- Drag blue axis handles to rotate.
- `E` chooses rotation mode for keyboard adjustments.
- `R` chooses scale mode. Scaling is limited to resources/volumes because
  arbitrary ship instance scaling is unsafe for gameplay systems.
- `X`, `Y`, or `Z` chooses the keyboard adjustment axis.
- Arrow Up/Down changes the selected inspector row.
- Arrow Left/Right changes the selected value.
- Control applies fine adjustment; Shift applies coarse translation/size steps.

Original objects can be transformed, marked for deletion, or left unchanged.
Editor additions can be duplicated with `Ctrl+Shift+D`. Deletion is represented
in the overlay rather than applied to the BIG archive.

## Volumetric dust

An RTX dust volume is a real D3D12 raymarched medium. Up to 128 volumes can be
authored; up to 64 enabled, non-zero-density volumes are uploaded to the live
renderer.

Volume fields:

| Field | Purpose | Range/notes |
| --- | --- | --- |
| Enabled | Includes/excludes the volume without deleting it | On/Off |
| Shape | Base boundary | Sphere, Box, Ellipsoid |
| Position X/Y/Z | World-space center | Keyboard step 100 units |
| Rotate X/Y/Z | Rigid volume orientation | 5-degree steps; fine mode supported |
| Size X/Y/Z | Shape extents | 50-500,000 units |
| Density | Medium density | 0-10 |
| Scattering | Out-of-beam light contribution | 0-10 |
| Absorption | Light removed through the medium | 0-10 |
| Anisotropy | Forward/back scattering bias | -0.95 to 0.95 |
| Coverage | Procedural occupancy | 0-2 |
| Color R/G/B | Linear single-scattering/albedo tint | 0-8 per channel |
| Noise Scale | World-space feature frequency | 0.00001-0.1 |
| Noise Detail | Secondary structure strength | 0-2 |
| Wake Strength | Ship disturbance response | 0x off, 1x baseline, 2x maximum |
| Mission Key Light | Receive the mission directional source | On/Off |
| Local Lights | Receive point/spot/FX lighting | On/Off |
| Volume Shadow | Cast volumetric shadowing | On/Off |
| Shape Seed | Deterministic silhouette seed | `G` randomizes |
| Shape Variation | Boundary deformation | 0-2 |
| Distance Fade Start | Full-strength radius before distance attenuation | 5,000-250,000 units; map-wide |
| Distance Fade Strength | Attenuation rate beyond the start radius | 0 disables; 4 is strongest; map-wide |

Shape is an authoring coordinate system rather than a hard rendered surface.
Shape Variation progressively displaces it with rotated, multi-octave 3D
noise. Use moderate-to-high variation when a dense cloud must not reveal an
obvious sphere, box, or ellipsoid. Density and march phase remain anchored in
world space, so camera rotation does not rotate or drag the cloud onscreen.

The two distance-fade rows are live sliders at the bottom of the inspector.
Inside Fade Start the cloud is unchanged; beyond it, strength falls smoothly.
Both values affect the current map and are saved with its overlay.

Tune in this order: size and transform, coverage/noise, density, scattering and
absorption, color, lighting flags, then wake response. Very high density plus
large overlapping volumes can obscure the scene and sharply increase raymarch
cost.

## Cloud preset library

The Volumes tab exposes a reusable preset dropdown:

- **Save New:** stores the selected volume's complete material, shape, seed,
  transform defaults, and lighting flags as a new preset.
- **Overwrite:** replaces the selected preset from the current volume.
- **Load:** loads the preset into the selected volume or creates a new volume.
- **Copy / Paste:** duplicates full volumes between locations; `Ctrl+C` and
  `Ctrl+V` provide the same operation.

Presets and their index live beneath:

```text
RTXExports\CloudPresets\
```

## Map overlay format and path

Save Map writes:

```text
RTXExports\Maps\MissionNN.rtxmap
```

The `RTXMAP 9` text format contains:

- `MISSION` identity
- `ADD` records for placed ships/resources
- `TRANSFORM` records for moved/rotated/scaled objects
- `DELETE` records for hidden original objects
- `VOLUME DUST` records for raymarched media
- one `DUST_FADE` record for map-wide fade start and strength

The packaged `assets\Missions\MissionNN.rtxmap` is used when no writable export
exists. Writable exports take precedence, which makes iteration immediate.

Do not hand-edit runtime IDs unless you understand the mission object order.
Hand editing material values is possible, but the live inspector is safer and
enforces limits.

# Lighting Editor - Shift+F12

The lighting editor does not pause gameplay. Open it after the mission HDR sky
has loaded. Use Tab or click tabs to move between **Mission Key**, **Custom
Lights**, **Global Tuning**, and **Shadows**.

Rows can be adjusted by mouse wheel, `-`/`+` buttons, or Arrow Left/Right.
Arrow Up/Down selects a row.

## Mission Key page

| Field | Meaning |
| --- | --- |
| Sun Azimuth | Rotates the key around world Y; 0 degrees points along -Z |
| Sun Elevation | Raises/lowers the key, limited to -89 to +89 degrees |
| Sun Intensity | Per-mission multiplier, 0-4 in 0.05 steps |
| Sun Red/Green/Blue | Multipliers over detected HDR source color, 0-4 |
| Ambient Strength | HDR-derived fill strength, 0-4 |
| Ambient Color Mode | Auto follows HDR sky mean; Manual uses RGB below |
| Ambient Red/Green/Blue | Manual linear ambient tint, 0-4 |

`P` aims the mission key using the cursor ray. `I` temporarily solos the key by
muting sky ambient, environment, authored lights, and FX. `R` restores the HDR
metadata baseline and reimports retail map lights. These changes are live and
remain unsaved until `S` or **Save Mission**.

## Custom Lights page

When the editor opens, retail HSF local lights are adopted into the editable
list. System key and ambient entries remain identifiable; editable records may
be Point, Spot, or Ambient.

| Field | Meaning |
| --- | --- |
| Select Light | Cycles key, sky ambient, imported HSF lights, and added lights |
| Light Type | Point, Spot, or Ambient for editable records |
| Enabled | Temporarily bypasses a light without deleting it |
| Intensity | Per-light energy, also affected by Mission-Authored global gain |
| Red/Green/Blue | Linear light color |
| Range / Radius | Point/spot falloff distance in world units |
| Spot Cone | Inner spot half-angle |
| Spot Edge | Feather outside the inner cone |
| Y Rotation / Yaw | Rotates a spot around world Y |
| Position X/Y/Z | Fine world-space placement |

Shortcuts:

- `N` adds a light.
- `Delete` removes the selected editable light.
- `P` places the selected point/spot at cursor depth.
- `A` aims the selected spot at the cursor.
- `S` saves the mission override and portable export.
- `L` reloads the saved override, then adopts retail map lights when needed.

Use the on-screen world marker to verify position and direction. Prefer a small
number of deliberate lights; each shadow-casting source adds visibility work.

## Global Tuning page

| Control | Range | What it changes |
| --- | ---: | --- |
| Primary Sun / Shadow | 0-200% | Gain after mission-local key settings |
| Sky Ambient / Color Wash | 0-200% | Mission ambient fill |
| Environment Reflections | 0-200% | Sky sampling and neutral path floor |
| Mission-Authored Lights | 0-200% | HSF and live authored lights |
| FX / Emissive Lights | 0-200% | Weapons, engines, beams, explosions, glow maps, nav lights |
| Surface Reflectivity | 0-200% | Stable primary-surface highlights |
| Surface Roughness | 0-100% | Highlight spread/sharpness |
| Normal Map Strength | 0-400% | Dedicated tangent-space relief, with a legacy generated fallback |
| Path-Light Exposure | 50-200% | Final path-light transfer only |
| Color Banding Filter | 0-200% | Stable scene dithering before 8-bit presentation |

`R` restores the tuned defaults: 100% light groups, 125% reflectivity, 38%
roughness, 100% normal-map strength, 100% exposure, and 100% dithering. `E`
saves options and exports all portable global visual/shader settings.

## Shadows page

| Control | Range | Baseline | Guidance |
| --- | ---: | ---: | --- |
| Shadow Strength | 0-200% | 100% | Global opacity of ray-traced visibility |
| Sun Angular Radius | 0-5 degrees | 0.35 | Larger values broaden distant penumbrae |
| Sun Shadow Samples | 1-16 | 6 | Higher steadiness at higher cost |
| Local Shadow Samples | 1-8 | 2 | Point/spot/FX visibility rays |
| Local Light Radius | 0-10 degrees | 0.20 | Area spread for local sources |
| Receiver Bias | 0.01-2.00 | 0.05 | Outgoing-ray offset; fights acne |
| Normal Bias | 0-4.00 | 0.08 | Surface-normal offset; fights self-intersection |
| Contact Shadow Strength | 0-200% | 100% | Near-field reinforcement beneath soft shadows |
| Contact Distance | 0-10,000 | 1,200 | Range of contact reinforcement |
| Max Shadow Distance | 10,000-1,000,000 | 500,000 | Visibility-ray reach |

Shadow tuning order:

1. Set sun and local sample counts for your performance target.
2. Set sun/local angular radius for the desired softness.
3. Lower receiver and normal bias until acne appears, then increase slightly.
4. Tune contact strength/distance to restore grounding lost to soft shadows.
5. Shorten maximum distance only when the mission scale permits it.

Too little bias creates self-shadow acne; too much disconnects shadows from
their casters (peter-panning). High sample counts multiply path-tracing cost.
`R` resets only the Shadows page when it is active. `E` exports shadows with
the global visual state.

## Lighting save and export paths

`S` or **Save Mission** writes both:

```text
Documents\My Games\Homeworld Modern\RTXLighting\MissionNN.rtxlight
RTXExports\Missions\MissionNN.rtxlight
```

The first is the live per-user override. The second is the portable file to
commit or share. The format begins with `RTXMISSIONLIGHTING 3` and records key
direction/energy, ambient state, whether authored lights replace retail map
lights, and every editable local light.

`E` or **Export Visuals** writes:

```text
RTXExports\Global\RTX-Visual-Settings.txt
```

This contains display timing, path settings, image quality, post effects,
global tuning, and shadow settings. Resolution, refresh rate, AA/DLSS mode,
and frame generation are deliberately excluded so an author's export does not
force hardware-specific choices on another user.

# Shortcut reference

## Map Editor

| Key | Action |
| --- | --- |
| `Shift+F11` or `Esc` | Close editor |
| Right mouse + move | Aim camera |
| Mouse wheel | Fly forward/back |
| `P` | Place selected asset or move selection to cursor depth |
| `F` | Focus selection |
| `E` | Rotation mode |
| `R` | Scale mode for resources/volumes |
| `X`, `Y`, `Z` | Select adjustment axis |
| Arrow Up/Down | Select inspector row |
| Arrow Left/Right | Adjust value |
| `Ctrl` | Fine adjustment modifier |
| `Shift` | Coarse translation/size modifier |
| `Delete` | Delete/hide selection through overlay |
| `Ctrl+Shift+D` | Duplicate selection |
| `G` | Randomize selected dust silhouette |
| `Ctrl+C`, `Ctrl+V` | Copy/paste full dust volume |
| `Ctrl+S` | Save map overlay |
| `L` | Reload mission and overlay |

## Lighting Editor

| Key | Action |
| --- | --- |
| `Shift+F12` or `Esc` | Close editor and save global options |
| `Tab` | Cycle Mission, Lights, Tuning, Shadows |
| Arrow Up/Down | Select row |
| Arrow Left/Right | Adjust live value |
| `P` | Place/aim key or place selected local light |
| `A` | Aim selected spot light |
| `N` | Add local light |
| `Delete` | Delete selected editable local light |
| `I` | Solo mission key |
| `R` | Reset active authoring/tuning page |
| `S` | Save and export mission lighting |
| `L` | Reload saved mission lighting |
| `E` | Save and export global visual/shadow settings |

# Publishing authored work

Share only `.rtxmap`, `.rtxlight`, cloud-preset, and visual-setting text files
plus art you have the right to redistribute. Never include `Homeworld.big`,
`Update.big`, voice/music archives, movies, or extracted retail assets.

For a packaged project preset, keep this layout:

```text
Missions\MissionNN.rtxmap
RTXLighting\MissionNN.rtxlight
CloudPresets\index.txt
CloudPresets\*.rtxdust
```

Document the required mission/data edition, gameplay-affecting object changes,
and the graphics cost of high-volume/high-light scenes.
