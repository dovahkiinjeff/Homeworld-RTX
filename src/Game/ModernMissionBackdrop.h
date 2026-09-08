#ifndef HW_MODERN_MISSION_BACKDROP_H
#define HW_MODERN_MISSION_BACKDROP_H

#include "Types.h"

/* Draws the campaign mission environment as a native fullscreen D3D12
   direction lookup from a pole-safe dual-paraboloid HDR Texture2D. TRUE means
   the modern layer rendered successfully and the caller must skip the visible
   legacy BTG; FALSE deliberately leaves the BTG as a fallback. */
bool32 modernMissionBackdropRender(real32 fade);
bool32 modernMissionBackdropActive(void);
bool32 modernMissionBackdropSkyLighting(real32 outDirection[3],
                                        real32 outSourceColor[3],
                                        real32 outAmbientColor[3],
                                        bool32 *outSourceValid);
bool32 modernMissionBackdropGodRaySource(real32 outDirection[3]);

/* RTX-0076/0077: mission-local live lighting authoring.  The HDR metadata
   remains the immutable baseline; saved overrides can move/tint the key,
   override ambient hue, and add local point/spot/global ambient lights. */
#define MODERN_MISSION_AUTHORED_LIGHT_MAX 16

typedef enum ModernMissionAuthoredLightType
{
    MODERN_MISSION_LIGHT_POINT = 1,
    MODERN_MISSION_LIGHT_SPOT = 2,
    MODERN_MISSION_LIGHT_AMBIENT = 3
} ModernMissionAuthoredLightType;

typedef struct ModernMissionAuthoredLight
{
    bool32 enabled;
    sdword type;
    real32 position[3];
    real32 direction[3];
    real32 color[3];
    real32 intensity;
    real32 radius;
    real32 coneAngle;
    real32 edgeAngle;
} ModernMissionAuthoredLight;

typedef struct ModernMissionLightingAuthoringState
{
    sdword mission;
    bool32 available;
    bool32 sourceValid;
    bool32 overrideActive;
    bool32 loadedFromDisk;
    real32 baseDirection[3];
    real32 direction[3];
    real32 baseSourceColor[3];
    real32 sunIntensity;
    real32 sourceColorScale[3];
    real32 baseAmbientColor[3];
    bool32 ambientEnabled;
    real32 ambientScale;
    bool32 ambientColorOverride;
    real32 ambientColor[3];
    bool32 replaceMapLights;
    sdword authoredLightCount;
    ModernMissionAuthoredLight authoredLights[MODERN_MISSION_AUTHORED_LIGHT_MAX];
} ModernMissionLightingAuthoringState;

bool32 modernMissionBackdropLightingAuthoringGet(
    ModernMissionLightingAuthoringState *outState);
bool32 modernMissionBackdropLightingAuthoringSet(
    const ModernMissionLightingAuthoringState *state);
void modernMissionBackdropLightingAuthoringReset(void);
bool32 modernMissionBackdropLightingAuthoringSave(void);
bool32 modernMissionBackdropLightingAuthoringExport(void);
bool32 modernMissionBackdropLightingAuthoringReload(void);
/* Converts the currently active HSF local emitters into editable authoring
   lights and suppresses their immutable DXR copies. Safe to call repeatedly. */
bool32 modernMissionBackdropLightingAuthoringAdoptMapLights(void);

#endif
