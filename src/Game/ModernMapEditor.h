#ifndef ___MODERN_MAP_EDITOR_H
#define ___MODERN_MAP_EDITOR_H

#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RTX-0084 realtime map-editor foundation. */

typedef enum ModernMapViewportMode
{
    MME_VIEW_LIT = 0,
    MME_VIEW_WIREFRAME,
    MME_VIEW_BOUNDS,
    MME_VIEW_VOLUMES,
    MME_VIEW_COUNT
} ModernMapViewportMode;

typedef enum ModernMapDustShape
{
    MME_DUST_SPHERE = 0,
    MME_DUST_BOX,
    MME_DUST_ELLIPSOID
} ModernMapDustShape;

typedef struct ModernMapDustVolumeSnapshot
{
    bool32 enabled;
    ModernMapDustShape shape;
    real32 position[3];
    real32 size[3];
    real32 density;
    real32 scattering;
    real32 absorption;
    real32 anisotropy;
    real32 coverage;
    /* Artist-authored single-scattering/albedo tint.  The real-time D3D12
       raymarcher multiplies dynamically evaluated mission/local-light color
       by this. */
    real32 color[3];
    real32 noiseScale;
    real32 noiseDetail;
    /* Per-volume ship-wake response. 0 disables disturbance, 1 preserves the
       original authored wake strength, 2 doubles it. */
    real32 wakeStrength;
    udword shapeSeed;
    real32 shapeVariation;
    /* Persistent authored rigid orientation for the direct F11 transform gizmo. */
    real32 rotationDegrees[3];
    bool32 receiveMissionKey;
    bool32 receiveLocalLights;
    bool32 castVolumetricShadow;
} ModernMapDustVolumeSnapshot;

void modernMapEditorToggle(void);
void modernMapEditorClose(void);
void modernMapEditorCloseForToolSwitch(void);
bool32 modernMapEditorActive(void);

/* Called by mainrgn before normal gameplay handling/rendering. */
bool32 modernMapEditorHandleWorldEvent(udword event, udword data);
bool32 modernMapEditorHandleKey(sdword key);
bool32 modernMapEditorPump(void);
void modernMapEditorCameraUpdate(void);
bool32 modernMapEditorWireframeActive(void);
/* Called from the live main-world draw after scene/selection rendering so the
   selected-object 3D manipulator is guaranteed to be visible in Shift+F11. */
void modernMapEditorDrawWorldGizmo(void);

/* The real-time volumetric renderer consumes these authoring values directly. */
sdword modernMapEditorDustVolumeCount(void);
bool32 modernMapEditorDustVolumeGet(sdword index, ModernMapDustVolumeSnapshot *outVolume);

#ifdef __cplusplus
}
#endif

#endif
