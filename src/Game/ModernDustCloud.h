#ifndef ___MODERNDUSTCLOUD_H
#define ___MODERNDUSTCLOUD_H

#include "Types.h"
#include "Color.h"
#include "Clouds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* High-detail replacement for the visible harvestable DustCloud shell.
   Gameplay radius, harvesting shrink, lightning and resource logic remain
   owned by Clouds.c; this module only replaces the rendered mesh/material. */
bool32 modernDustCloudRender(const cloudSystem *system, real32 radius,
                             sdword legacyLod, color cloudColor);
void modernDustCloudShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
