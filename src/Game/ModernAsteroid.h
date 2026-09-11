#ifndef HW_MODERN_ASTEROID_H
#define HW_MODERN_ASTEROID_H

#include "SpaceObj.h"

/* Renders source-owned high-detail replacements for harvestable Asteroid1-4.
   TRUE means the legacy GEO/PEO should not be drawn for this asteroid. */
bool32 modernAsteroidRender(const Asteroid *asteroid, sdword lod);

/* Submit the matching authored asteroid mesh to DXR as shadow-only geometry.
   The caller owns world rotation/translation and gameplay scaling. */
bool32 modernAsteroidSubmitShadow(const Asteroid *asteroid);

#endif
