# Homeworld Modern interface artwork

Every raster plate in this folder is composed exclusively from the official
Homeworld concept-art archive supplied by the project owner. No generated
imagery is used.

| Output | Archive source | Treatment |
| --- | --- | --- |
| `menu_backdrop.png` | `TG_galaxy map.jpg` | 16:9 crop, restrained color and left-to-right command-interface grade |
| `campaign_plate.png` | `RC_newpathcomp07.jpg` | 16:9 campaign-map crop and subdued presentation grade |
| `fleet_plate.png` | `RC_P1 Cover.jpg` | 16:9 ship study crop and subdued presentation grade |
| `systems_plate.png` | `AK_Main Hangar.jpg` | 16:9 crop, monochrome/cyan technical grade |
| `archive_plate.png` | `RC_Khar Toba.jpg` | 16:9 crop, negative line-study treatment and amber archive grade |
| `startup_splash.png` | `TG_galaxy map.jpg`, `RC_P1 Cover.jpg` | Layered 16:9 official-art composition for the live startup window |
| `fleet_identity_editor.png` | Project-owner runtime capture | Unmodified documentation screenshot of the code-native Fleet Identity page |
| `fleet_control_options.png` | Project-owner runtime capture | Unmodified documentation screenshot of the Fleet Control page with Capture / Build All enabled |

The executable selects plates deterministically from the current UI context;
there is no per-frame randomization. This keeps the archive visible throughout
the front end without producing visual noise or temporal flicker.
