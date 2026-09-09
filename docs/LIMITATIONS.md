# Known Limitations and Beta Expectations

Homeworld RTX 0.91.3 Beta is a working public test build, not a finished remaster.
The following boundaries are intentional and should be understood before use.

## Platform

- Windows 10/11 x64 only.
- The active renderer depends on Direct3D 12, DXGI, DXR, and Windows tooling.
- There are no supported Linux, macOS, Steam Deck, or ARM64 binaries.
- DXR lighting requires capable hardware. Reconstruction is no longer
  NVIDIA-only: the package contains NVIDIA Streamline, AMD FidelityFX, and
  Intel XeSS-SR runtimes, with capability-based Auto modes.

## Required retail data

- The release contains no retail BIG archives, speech, music, or movies.
- A legal Homeworld Classic installation is required.
- **New-extract blocker:** `HW_Music.wxd` and `HW_Comp.vce` must currently be
  copied manually from that legal installation into the same directory as
  `HomeworldModern.exe`; first-run verification otherwise fails.
- Alternate language and patched data layouts are validated structurally, but
  the full matrix of editions has not been campaign-tested.
- Do not combine Remastered Collection Classic data with a separate original
  `Update.big`.

## Renderer

- DLAA/DLSS, FidelityFX Native AA/FSR, and XeSS AA/XeSS-SR share the same
  corrected full-scene color, depth, pixel-motion, jitter, reset, and native-UI
  contract.
- The first cross-vendor beta deliberately leaves vendor frame generation off.
  Super resolution/native AA is integrated; interpolated frames and latency
  middleware require separate validation.
- Reactive/transparency input is accepted by the shared interface but remains
  conservative until the legacy effect renderer exposes a reliable opaque-only
  scene boundary. Highly translucent trails, beams, and explosions are the most
  important AMD/Intel test cases.
- FidelityFX machine-learning acceleration is hardware-dependent. Unsupported
  adapters use the runtime's compatible provider; selecting FSR does not imply
  that an RX 9000-only path is active.

- Native D3D12 fixed-function compatibility rasterization is experimental and
  is not parity-complete for every rare legacy state.
- HDR10 and scRGB output are not implemented; output is SDR.
- DLSS Ray Reconstruction is available on compatible NVIDIA hardware and falls
  back to DLSS Super Resolution when its feature/runtime contract is rejected.
- Frame Generation is disabled. The retired generic interpolation experiment
  is not presented as NVIDIA DLSS-G.
- NVIDIA Reflex markers/latency integration are not implemented.
- Full specular reflection transport and authored metalness/roughness materials
  are not implemented. Classic materials use deterministic approximations.
- Animated/morphed BLAS updates and uncommon transparency/reactive cases may
  still need work.
- Multiple importance sampling and a production neural denoiser are not present.
- Very high path sample, bounce, shadow-ray, light-count, and volumetric settings
  can be expensive even on recent GPUs.
- The shader cache is persisted, but driver-specific D3D12 pipeline-library
  blobs are deliberately not reused because cross-run driver behavior was unsafe.

## Visual behavior

- The renderer forces LOD0 and removes N-LIPS distance enlargement. Dense fleets
  may therefore cost more GPU time than the original game.
- Path-traced lighting converges progressively. Fine noise can be visible after
  cuts, fast movement, visibility changes, or at low sample counts.
- Fast motion intentionally reduces temporal history to avoid ghosting.
- Generated normals are inferred from legacy LIF luminance and cannot reproduce
  hand-authored modern normal maps.
- Post effects are artistic additions and default conservatively; they may not
  match every mission palette.
- UI plate crops and ultrawide safe-canvas composition still require broad
  localization and DPI review.

## Gameplay and multiplayer

- The project aims to preserve the fixed simulation contract, but this beta has
  not been exhaustively checksum-tested across every campaign and multiplayer
  scenario.
- Unlimited fuel, resource multiplier, Super Salvagers, and Capture / Build All
  are single-player convenience features and are excluded or constrained for
  multiplayer safety.
- Legacy online service code is not a supported matchmaking service. Treat LAN
  functionality as experimental and use identical builds/settings where required.
- Save compatibility follows the HomeworldSDL baseline; retain backups before
  relying on a beta for a long campaign.

## Editors

- Shift+F11 and Shift+F12 are development tools and assume familiarity with
  mission scale, object types, and lighting tradeoffs.
- Map overlays depend on runtime object IDs. Changes to the underlying mission
  can invalidate transformations/deletions.
- Only 64 enabled non-zero-density volumetric dust volumes are uploaded to the
  live raymarcher, though up to 128 can be authored.
- Extremely large/overlapping volumes, high density, high shadow counts, and
  many local lights can severely reduce performance.
- The editors do not provide undo history. Save versions of exported text files.
- Mission and KAS pause states are independent; changing a live mission without
  pausing scripts can produce moving targets or script-side changes.
- Export directories must be writable.

## Content and support

- This is a free, non-commercial community project.
- It is not affiliated with or supported by Gearbox, Relic, Sierra, NVIDIA,
  Steam, or the original developers/publishers.
- Do not report project bugs to the commercial game-support teams.
- Do not upload copyrighted retail data in bug reports.
- No warranty is provided. The Relic source-code agreement in `LICENSE.txt`
  governs the original source baseline and includes restrictive terms.

## Useful report checklist

Before opening an issue, record:

1. Windows edition/build.
2. GPU model and driver version.
3. Homeworld data edition and language.
4. Mission/mode and save provenance.
5. Display mode, resolution, refresh, VSync, and frame cap.
6. Path-tracing, sample, bounce, AA/DLSS, shadow, and post-effect settings.
7. Exact steps and whether the issue reproduces after renderer reset/restart.
8. Diagnostic logs or dump, without retail assets.
