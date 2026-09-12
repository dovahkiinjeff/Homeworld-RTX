#include "ModernMapEditor.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BigFile.h"
#include "Camera.h"
#include "CameraCommand.h"
#include "FontReg.h"
#include "File.h"
#include "Globals.h"
#include "HorseRace.h"
#include "KAS.h"
#include "Matrix.h"
#include "ManagerDock.h"
#include "ModernUI.h"
#include "ModernGraphics.h"
#include "ObjTypes.h"
#include "Select.h"
#include "SinglePlayer.h"
#include "SoundEvent.h"
#include "Task.h"
#include "SpaceObj.h"
#include "UnivUpdate.h"
#include "Universe.h"
#include "font.h"
#include "main.h"
#include "mainrgn.h"
#include "mouse.h"
#include "prim2d.h"
#include "render.h"
#include "uiscale.h"
#include "utility.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

#define MME_PI 3.14159265358979323846f
#define MME_MAX_ASSETS 1024
#define MME_MAX_TRACKED 2048
#define MME_MAX_VOLUMES 128
#define MME_RENDERER_DUST_VOLUME_LIMIT 64
#define MME_VISIBLE_ROWS 22
#define MME_STATUS_BYTES 192
#define MME_PANEL_MIN_WIDTH 245
#define MME_PANEL_MAX_WIDTH 360
#define MME_TOOLBAR_HEIGHT 42
#define MME_INSPECTOR_ROWS 29
#define MME_MAX_CLOUD_PRESETS 64
#define MME_PRESET_NAME_BYTES 64
#define MME_PRESET_FILE_BYTES 64
#define MME_PRESET_VISIBLE_ROWS 7

#define MME_TAB_ASSETS 0
#define MME_TAB_SCENE 1
#define MME_TAB_VOLUMES 2
#define MME_TAB_COUNT 3

#define MME_TRANSFORM_MOVE 0
#define MME_TRANSFORM_ROTATE 1
#define MME_TRANSFORM_SCALE 2

#define MME_AXIS_X 0
#define MME_AXIS_Y 1
#define MME_AXIS_Z 2

#define MME_GIZMO_NONE 0
#define MME_GIZMO_TRANSLATE 1
#define MME_GIZMO_ROTATE 2

#define MME_CAMERA_PERSPECTIVE 0
#define MME_CAMERA_TOP 1
#define MME_CAMERA_FRONT 2
#define MME_CAMERA_SIDE 3
#define MME_CAMERA_COUNT 4

typedef enum MMEAssetKind
{
    MME_ASSET_SHIP = 0,
    MME_ASSET_DERELICT,
    MME_ASSET_ASTEROID,
    MME_ASSET_DUST,
    MME_ASSET_GAS,
    MME_ASSET_RTX_DUST_VOLUME
} MMEAssetKind;

typedef struct MMEAsset
{
    MMEAssetKind kind;
    sdword subtype;
    ShipRace race;
    char archive[32];
    char path[BF_MAX_FILENAME_LENGTH + 1];
    char label[96];
} MMEAsset;

typedef struct MMETrackedObject
{
    SpaceObj *object;
    ObjType objtype;
    sdword subtype;
    ShipRace race;
    sdword runtimeId;
    bool32 original;
    bool32 created;
    bool32 dirty;
    bool32 deleted;
    vector originalPosition;
    matrix originalCoordsys;
    real32 rotationDegrees[3];
    real32 scale;
    char label[96];
} MMETrackedObject;

typedef struct MMEVolume
{
    bool32 inUse;
    ModernMapDustVolumeSnapshot data;
    char label[64];
} MMEVolume;

typedef struct MMECloudPreset
{
    char name[MME_PRESET_NAME_BYTES];
    char fileName[MME_PRESET_FILE_BYTES];
} MMECloudPreset;

typedef struct MMEGizmoProjection
{
    bool32 valid;
    real32 centerWorld[3];
    sdword centerX;
    sdword centerY;
    real32 worldLength;
    bool32 axisVisible[3];
    sdword moveX[3];
    sdword moveY[3];
    sdword rotateX[3];
    sdword rotateY[3];
    real32 axisScreenX[3];
    real32 axisScreenY[3];
    real32 axisScreenLength[3];
} MMEGizmoProjection;

typedef struct MMEState
{
    bool32 active;
    bool32 reopeningAfterLoad;
    bool32 previousUniversePause;
    bool32 previousKasPause;
    regionhandle leftRegion;
    regionhandle rightRegion;
    regionhandle toolbarRegion;
    regionhandle overlayRegion;
    fonthandle headingFont;
    fonthandle bodyFont;
    fonthandle tinyFont;
    sdword tab;
    sdword assetScroll;
    sdword sceneScroll;
    sdword volumeScroll;
    sdword selectedAsset;
    sdword selectedTracked;
    sdword selectedVolume;
    sdword inspectorRow;
    sdword transformMode;
    sdword axis;
    /* RTX-0090 direct-manipulation transform gizmo.  These fields are only
       live while the left mouse button owns an axis handle. */
    sdword gizmoDragKind;
    sdword gizmoDragAxis;
    sdword gizmoLastMouseX;
    sdword gizmoLastMouseY;
    real32 gizmoAxisScreenX;
    real32 gizmoAxisScreenY;
    real32 gizmoWorldPerPixel;
    bool32 wakeSliderDrag;
    sdword presetSelected;
    sdword presetScroll;
    bool32 presetDropdownOpen;
    sdword viewportMode;
    sdword cameraMode;
    bool32 freeCameraInitialized;
    vector freeCameraEye;
    vector freeCameraLookat;
    real32 freeCameraDistance;
    sdword selectedMission;
    sdword pendingMission;
    bool32 pendingLoadOverlay;
    bool32 applyOverlayAfterBoot;
    char status[MME_STATUS_BYTES];
} MMEState;

static MMEState mme;
static MMEAsset mmeAssets[MME_MAX_ASSETS];
static sdword mmeAssetCount = 0;
static MMETrackedObject mmeTracked[MME_MAX_TRACKED];
static sdword mmeTrackedCount = 0;
static MMEVolume mmeVolumes[MME_MAX_VOLUMES];
static sdword mmeVolumeCount = 0;
static sdword mmeVolumeMission = -1;
static HWModernVolumetricDustVolume mmeRenderVolumes[MME_MAX_VOLUMES];
static MMECloudPreset mmeCloudPresets[MME_MAX_CLOUD_PRESETS];
static sdword mmeCloudPresetCount = 0;
static ModernMapDustVolumeSnapshot mmeVolumeClipboard;
static bool32 mmeVolumeClipboardValid = FALSE;
static real32 mmeDustFadeDistance = 65000.0f;
static real32 mmeDustFadeStrength = 1.0f;

static void mmeBindRegions(void);
static void mmeRotateTrack(MMETrackedObject *track, sdword axis, real32 degrees);

static void mmeSyncDustVolumesToRenderer(void)
{
    unsigned int count = 0;
    sdword i;
    for (i = 0; i < mmeVolumeCount && count < MME_RENDERER_DUST_VOLUME_LIMIT; ++i)
    {
        MMEVolume *source = &mmeVolumes[i];
        HWModernVolumetricDustVolume *target;
        if (!source->inUse || !source->data.enabled || source->data.density <= 0.00001f) continue;
        target = &mmeRenderVolumes[count++];
        memset(target, 0, sizeof(*target));
        target->enabled = source->data.enabled ? 1u : 0u;
        target->shape = (unsigned int)source->data.shape;
        target->shapeSeed = (unsigned int)(source->data.shapeSeed ? source->data.shapeSeed : 1u);
        {
            real32 wakeStrength = source->data.wakeStrength;
            unsigned int wakeStrengthCode;
            if (wakeStrength < 0.0f) wakeStrength = 0.0f;
            if (wakeStrength > 2.0f) wakeStrength = 2.0f;
            /* Preserve the important 0x/1x/2x anchors exactly in four bits:
               codes 0..8 cover 0..1, codes 8..15 cover 1..2. */
            if (wakeStrength <= 1.0f)
                wakeStrengthCode = (unsigned int)(wakeStrength * 8.0f + 0.5f);
            else
                wakeStrengthCode = 8u + (unsigned int)((wakeStrength - 1.0f) * 7.0f + 0.5f);
            target->flags =
                (source->data.receiveMissionKey ? HW_MODERN_DUST_RECEIVE_MISSION_KEY : 0u) |
                (source->data.receiveLocalLights ? HW_MODERN_DUST_RECEIVE_LOCAL_LIGHTS : 0u) |
                (source->data.castVolumetricShadow ? HW_MODERN_DUST_CAST_VOLUMETRIC_SHADOW : 0u) |
                0x80u | ((wakeStrengthCode & 15u) << 3u);
        }
        target->position[0] = source->data.position[0];
        target->position[1] = source->data.position[1];
        target->position[2] = source->data.position[2];
        target->density = source->data.density;
        target->size[0] = source->data.size[0];
        target->size[1] = source->data.size[1];
        target->size[2] = source->data.size[2];
        target->scattering = source->data.scattering;
        target->color[0] = source->data.color[0];
        target->color[1] = source->data.color[1];
        target->color[2] = source->data.color[2];
        target->absorption = source->data.absorption;
        target->anisotropy = source->data.anisotropy;
        target->coverage = source->data.coverage;
        target->noiseScale = source->data.noiseScale;
        target->noiseDetail = source->data.noiseDetail;
        target->shapeVariation = source->data.shapeVariation;
        target->rotationDegrees[0] = source->data.rotationDegrees[0];
        target->rotationDegrees[1] = source->data.rotationDegrees[1];
        target->rotationDegrees[2] = source->data.rotationDegrees[2];
    }
    hwModernGraphicsSetVolumetricDustFade(
        mmeDustFadeDistance, mmeDustFadeStrength);
    hwModernGraphicsSetVolumetricDustVolumes(
        count > 0 ? mmeRenderVolumes : NULL, count);
}

static const color mmeInk = colRGBA(7, 12, 17, 242);
static const color mmeSurface = colRGBA(16, 27, 34, 242);
static const color mmeRaised = colRGBA(27, 40, 48, 248);
static const color mmeLine = colRGB(91, 117, 126);
static const color mmePaper = colRGB(231, 226, 208);
static const color mmeMuted = colRGB(145, 159, 160);
static const color mmeSignal = colRGB(224, 165, 63);
static const color mmeCool = colRGB(104, 187, 199);
static const color mmeDanger = colRGB(200, 90, 75);
static const color mmeVolumeColor = colRGB(142, 111, 218);

static sdword mmeClamp(sdword value, sdword low, sdword high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static real32 mmeClampF(real32 value, real32 low, real32 high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool32 mmePointIn(const rectangle *rect, sdword x, sdword y)
{
    return x >= rect->x0 && x < rect->x1 && y >= rect->y0 && y < rect->y1;
}

static void mmeStatus(const char *text)
{
    if (text == NULL) text = "";
    snprintf(mme.status, sizeof(mme.status), "%s", text);
    if (mme.leftRegion != NULL) regRecursiveSetDirty(mme.leftRegion);
    if (mme.rightRegion != NULL) regRecursiveSetDirty(mme.rightRegion);
    if (mme.toolbarRegion != NULL) regRecursiveSetDirty(mme.toolbarRegion);
}

static void mmeFonts(void)
{
    if (mme.headingFont == FONT_InvalidFontHandle || mme.headingFont == 0)
        mme.headingFont = frFontRegister("Arial_b17.hff");
    if (mme.bodyFont == FONT_InvalidFontHandle || mme.bodyFont == 0)
        mme.bodyFont = frFontRegister("default.hff");
    if (mme.tinyFont == FONT_InvalidFontHandle || mme.tinyFont == 0)
        mme.tinyFont = frFontRegister("default.hff");
}

static void mmeRectInset(rectangle *out, const rectangle *in, sdword amount)
{
    *out = *in;
    out->x0 += amount;
    out->x1 -= amount;
    out->y0 += amount;
    out->y1 -= amount;
}

static void mmeDrawButton(const rectangle *rect, const char *label,
                          bool32 active, bool32 hover)
{
    color fill = active ? colRGBA(224, 165, 63, 54) :
                 (hover ? colRGBA(104, 187, 199, 34) : mmeRaised);
    color border = active ? mmeSignal : (hover ? mmeCool : mmeLine);
    fonthandle old;
    sdword x;
    sdword y;
    primRectSolid2((rectangle *)rect, fill);
    primRectOutline2((rectangle *)rect, 1, border);
    old = fontMakeCurrent(mme.bodyFont);
    x = rect->x0 + ((rect->x1 - rect->x0) - fontWidth((char *)label)) / 2;
    y = rect->y0 + ((rect->y1 - rect->y0) - fontHeight(" ")) / 2;
    fontPrint(x, y, active ? mmePaper : mmeMuted, (char *)label);
    fontMakeCurrent(old);
}

static void mmeDrawText(sdword x, sdword y, color c, const char *text)
{
    fonthandle old = fontMakeCurrent(mme.bodyFont);
    fontPrint(x, y, c, (char *)text);
    fontMakeCurrent(old);
}

static void mmeDrawTiny(sdword x, sdword y, color c, const char *text)
{
    fonthandle old = fontMakeCurrent(mme.tinyFont);
    fontPrint(x, y, c, (char *)text);
    fontMakeCurrent(old);
}

static sdword mmePanelWidth(void)
{
    sdword width = uiScaleSize(292);
    return mmeClamp(width, MME_PANEL_MIN_WIDTH, MME_PANEL_MAX_WIDTH);
}

static rectangle mmeLeftRect(void)
{
    rectangle r;
    sdword w = mmePanelWidth();
    r.x0 = uiScaleSize(10);
    r.y0 = uiScaleSize(MME_TOOLBAR_HEIGHT + 12);
    r.x1 = r.x0 + w;
    r.y1 = MAIN_WindowHeight - uiScaleSize(12);
    return r;
}

static rectangle mmeRightRect(void)
{
    rectangle r;
    sdword w = mmePanelWidth();
    r.x1 = MAIN_WindowWidth - uiScaleSize(10);
    r.x0 = r.x1 - w;
    r.y0 = uiScaleSize(MME_TOOLBAR_HEIGHT + 12);
    r.y1 = MAIN_WindowHeight - uiScaleSize(12);
    return r;
}

static rectangle mmeToolbarRect(void)
{
    rectangle r;
    r.x0 = uiScaleSize(10);
    r.y0 = uiScaleSize(8);
    r.x1 = MAIN_WindowWidth - uiScaleSize(10);
    r.y1 = r.y0 + uiScaleSize(MME_TOOLBAR_HEIGHT);
    return r;
}

static void mmeLayoutTabs(const rectangle *panel, rectangle tabs[MME_TAB_COUNT],
                          rectangle *list, rectangle *footer)
{
    sdword pad = uiScaleSize(10);
    sdword gap = uiScaleSize(4);
    sdword tabH = uiScaleSize(28);
    sdword footerH = uiScaleSize(54);
    sdword innerW = panel->x1 - panel->x0 - pad * 2;
    sdword tabW = (innerW - gap * (MME_TAB_COUNT - 1)) / MME_TAB_COUNT;
    sdword i;
    for (i = 0; i < MME_TAB_COUNT; ++i)
    {
        tabs[i].x0 = panel->x0 + pad + i * (tabW + gap);
        tabs[i].x1 = tabs[i].x0 + tabW;
        tabs[i].y0 = panel->y0 + pad + uiScaleSize(30);
        tabs[i].y1 = tabs[i].y0 + tabH;
    }
    list->x0 = panel->x0 + pad;
    list->x1 = panel->x1 - pad;
    list->y0 = tabs[0].y1 + uiScaleSize(8);
    list->y1 = panel->y1 - pad - footerH;
    footer->x0 = list->x0;
    footer->x1 = list->x1;
    footer->y0 = list->y1 + uiScaleSize(6);
    footer->y1 = panel->y1 - pad;
}

static void mmeLayoutToolbar(const rectangle *bar, rectangle buttons[10])
{
    static const real32 weights[10] = {1.05f, 0.72f, 1.15f, 0.95f, 0.95f,
                                        0.95f, 0.95f, 0.95f, 0.95f, 0.72f};
    real32 total = 0.0f;
    real32 unit;
    sdword gap = uiScaleSize(4);
    sdword pad = uiScaleSize(6);
    sdword i;
    sdword x;
    for (i = 0; i < 10; ++i) total += weights[i];
    unit = (real32)(bar->x1 - bar->x0 - pad * 2 - gap * 9) / total;
    x = bar->x0 + pad;
    for (i = 0; i < 10; ++i)
    {
        sdword w = (sdword)(unit * weights[i]);
        buttons[i].x0 = x;
        buttons[i].x1 = x + w;
        buttons[i].y0 = bar->y0 + uiScaleSize(6);
        buttons[i].y1 = bar->y1 - uiScaleSize(6);
        x = buttons[i].x1 + gap;
    }
}

static bool32 mmeEndsWith(const char *text, const char *suffix)
{
    size_t a;
    size_t b;
    if (text == NULL || suffix == NULL) return FALSE;
    a = strlen(text);
    b = strlen(suffix);
    if (a < b) return FALSE;
    return strcasecmp(text + a - b, suffix) == 0;
}

static const char *mmeBaseName(const char *path)
{
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *p = a;
    if (b != NULL && (p == NULL || b > p)) p = b;
    return p == NULL ? path : p + 1;
}

static void mmeNameWithoutExtension(char *out, sdword outBytes, const char *path)
{
    const char *base = mmeBaseName(path);
    const char *dot = strrchr(base, '.');
    size_t len = dot == NULL ? strlen(base) : (size_t)(dot - base);
    if (len >= (size_t)outBytes) len = (size_t)outBytes - 1;
    memcpy(out, base, len);
    out[len] = 0;
}

static ShipRace mmeRaceFromPath(const char *path)
{
    char token[32];
    sdword i = 0;
    while (*path == '\\' || *path == '/') ++path;
    while (*path && *path != '\\' && *path != '/' && i < (sdword)sizeof(token) - 1)
        token[i++] = *path++;
    token[i] = 0;
    if (token[0] == 0) return (ShipRace)NUM_RACES;
    {
        ShipRace race = StrToShipRace(token);
        if ((sdword)race >= 0 && race < NUM_RACES) return race;
    }
    return (ShipRace)NUM_RACES;
}

static bool32 mmeAssetDuplicate(MMEAssetKind kind, ShipRace race, sdword subtype)
{
    sdword i;
    for (i = 0; i < mmeAssetCount; ++i)
    {
        if (mmeAssets[i].kind == kind && mmeAssets[i].race == race &&
            mmeAssets[i].subtype == subtype)
            return TRUE;
    }
    return FALSE;
}

static void mmeAssetAdd(MMEAssetKind kind, ShipRace race, sdword subtype,
                        const char *archive, const char *path, const char *label)
{
    MMEAsset *asset;
    if (mmeAssetCount >= MME_MAX_ASSETS || mmeAssetDuplicate(kind, race, subtype))
        return;
    asset = &mmeAssets[mmeAssetCount++];
    memset(asset, 0, sizeof(*asset));
    asset->kind = kind;
    asset->race = race;
    asset->subtype = subtype;
    snprintf(asset->archive, sizeof(asset->archive), "%s", archive ? archive : "PROCEDURAL");
    snprintf(asset->path, sizeof(asset->path), "%s", path ? path : "");
    snprintf(asset->label, sizeof(asset->label), "%s", label ? label : "OBJECT");
}

static int mmeAssetSortCompare(const void *left, const void *right)
{
    const MMEAsset *a = (const MMEAsset *)left;
    const MMEAsset *b = (const MMEAsset *)right;
    if (a->kind != b->kind) return (sdword)a->kind - (sdword)b->kind;
    if (a->race != b->race) return (sdword)a->race - (sdword)b->race;
    return strcasecmp(a->label, b->label);
}

static void mmeBuildAssets(void)
{
    sdword archiveIndex;
    mmeAssetCount = 0;
    for (archiveIndex = 0; archiveIndex < bigArchiveCount(); ++archiveIndex)
    {
        sdword fileIndex;
        sdword count = bigArchiveFileCount(archiveIndex);
        const char *archiveName = bigArchiveName(archiveIndex);
        for (fileIndex = 0; fileIndex < count && mmeAssetCount < MME_MAX_ASSETS - 1; ++fileIndex)
        {
            char path[BF_MAX_FILENAME_LENGTH + 1];
            char base[96];
            ShipRace race;
            if (!bigArchiveFileName(archiveIndex, fileIndex, path, sizeof(path)))
                continue;
            if (!mmeEndsWith(path, ".shp")) continue;
            mmeNameWithoutExtension(base, sizeof(base), path);
            race = mmeRaceFromPath(path);
            if ((sdword)race >= 0 && race < NUM_RACES)
            {
                ShipType type = StrToShipType(base);
                if ((sdword)type >= 0 && (sdword)type < TOTAL_NUM_SHIPS)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "%s / %s", ShipRaceToStr(race), ShipTypeToNiceStr(type));
                    mmeAssetAdd(MME_ASSET_SHIP, race, (sdword)type, archiveName, path, label);
                    continue;
                }
            }
            {
                DerelictType type = StrToDerelictType(base);
                if ((sdword)type >= 0 && (sdword)type < NUM_DERELICTTYPES)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "DERELICT / %s", DerelictTypeToStr(type));
                    mmeAssetAdd(MME_ASSET_DERELICT, 0, (sdword)type, archiveName, path, label);
                    continue;
                }
            }
            {
                AsteroidType type = StrToAsteroidType(base);
                if ((sdword)type >= 0 && (sdword)type < NUM_ASTEROIDTYPES)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "RESOURCE / %s", AsteroidTypeToStr(type));
                    mmeAssetAdd(MME_ASSET_ASTEROID, 0, (sdword)type, archiveName, path, label);
                    continue;
                }
            }
            {
                DustCloudType type = StrToDustCloudType(base);
                if ((sdword)type >= 0 && (sdword)type < NUM_DUSTCLOUDTYPES)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "LEGACY DUST / %s", DustCloudTypeToStr(type));
                    mmeAssetAdd(MME_ASSET_DUST, 0, (sdword)type, archiveName, path, label);
                    continue;
                }
            }
            {
                GasCloudType type = StrToGasCloudType(base);
                if ((sdword)type >= 0 && (sdword)type < NUM_GASCLOUDTYPES)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "GAS / %s", GasCloudTypeToStr(type));
                    mmeAssetAdd(MME_ASSET_GAS, 0, (sdword)type, archiveName, path, label);
                    continue;
                }
            }
        }
    }
    mmeAssetAdd(MME_ASSET_RTX_DUST_VOLUME, 0, 0, "RTX PROCEDURAL", "RTX/VolumetricDust",
                "RTX VOLUMETRIC DUST VOLUME");
    if (mmeAssetCount > 1) qsort(mmeAssets, (size_t)mmeAssetCount, sizeof(mmeAssets[0]), mmeAssetSortCompare);
    if (mme.selectedAsset >= mmeAssetCount) mme.selectedAsset = mmeAssetCount - 1;
    if (mme.selectedAsset < 0 && mmeAssetCount > 0) mme.selectedAsset = 0;
}

static vector *mmeObjectPosition(SpaceObj *object)
{
    if (object == NULL) return NULL;
    switch (object->objtype)
    {
        case OBJ_ShipType:
        case OBJ_DerelictType:
        case OBJ_AsteroidType:
        case OBJ_DustType:
        case OBJ_GasType:
        case OBJ_NebulaType:
            return &((SpaceObjRotImp *)object)->posinfo.position;
        default:
            return NULL;
    }
}

static RotInfo *mmeObjectRotInfo(SpaceObj *object)
{
    if (object == NULL) return NULL;
    switch (object->objtype)
    {
        case OBJ_ShipType:
        case OBJ_DerelictType:
        case OBJ_AsteroidType:
        case OBJ_DustType:
        case OBJ_GasType:
        case OBJ_NebulaType:
            return &((SpaceObjRotImp *)object)->rotinfo;
        default:
            return NULL;
    }
}

static real32 mmeObjectScale(SpaceObj *object)
{
    if (object == NULL) return 1.0f;
    if (object->objtype == OBJ_AsteroidType) return ((Asteroid *)object)->scaling;
    if (object->objtype == OBJ_DustType) return ((DustCloud *)object)->scaling;
    if (object->objtype == OBJ_GasType) return ((GasCloud *)object)->scaling;
    if (object->objtype == OBJ_NebulaType) return ((Nebula *)object)->scaling;
    return 1.0f;
}

static void mmeSetObjectScale(SpaceObj *object, real32 scale)
{
    if (object == NULL) return;
    scale = mmeClampF(scale, 0.05f, 50.0f);
    if (object->objtype == OBJ_AsteroidType) ((Asteroid *)object)->scaling = scale;
    else if (object->objtype == OBJ_DustType) ((DustCloud *)object)->scaling = scale;
    else if (object->objtype == OBJ_GasType) ((GasCloud *)object)->scaling = scale;
    else if (object->objtype == OBJ_NebulaType) ((Nebula *)object)->scaling = scale;
}

static sdword mmeRuntimeId(SpaceObj *object)
{
    if (object == NULL) return -1;
    if (object->objtype == OBJ_ShipType) return ((Ship *)object)->shipID.shipNumber;
    if (object->objtype == OBJ_DerelictType) return ((Derelict *)object)->derelictID.derelictNumber;
    if (object->objtype == OBJ_AsteroidType || object->objtype == OBJ_DustType ||
        object->objtype == OBJ_GasType || object->objtype == OBJ_NebulaType)
        return ((Resource *)object)->resourceID.resourceNumber;
    return -1;
}

static void mmeDescribeObject(SpaceObj *object, char *out, sdword outBytes,
                              ShipRace *raceOut, sdword *subtypeOut)
{
    ShipRace race = 0;
    sdword subtype = 0;
    if (object == NULL)
    {
        snprintf(out, outBytes, "NONE");
        if (raceOut) *raceOut = 0;
        if (subtypeOut) *subtypeOut = 0;
        return;
    }
    switch (object->objtype)
    {
        case OBJ_ShipType:
        {
            Ship *ship = (Ship *)object;
            race = ship->shiprace;
            subtype = ship->shiptype;
            snprintf(out, outBytes, "%s / %s", ShipRaceToStr(race), ShipTypeToNiceStr(ship->shiptype));
            break;
        }
        case OBJ_DerelictType:
            subtype = ((Derelict *)object)->derelicttype;
            snprintf(out, outBytes, "DERELICT / %s", DerelictTypeToStr((DerelictType)subtype));
            break;
        case OBJ_AsteroidType:
            subtype = ((Asteroid *)object)->asteroidtype;
            snprintf(out, outBytes, "RESOURCE / %s", AsteroidTypeToStr((AsteroidType)subtype));
            break;
        case OBJ_DustType:
            subtype = ((DustCloud *)object)->dustcloudtype;
            snprintf(out, outBytes, "LEGACY DUST / %s", DustCloudTypeToStr((DustCloudType)subtype));
            break;
        case OBJ_GasType:
            subtype = ((GasCloud *)object)->gascloudtype;
            snprintf(out, outBytes, "GAS / %s", GasCloudTypeToStr((GasCloudType)subtype));
            break;
        case OBJ_NebulaType:
            subtype = ((Nebula *)object)->nebulatype;
            snprintf(out, outBytes, "NEBULA / %s", NebulaTypeToStr((NebulaType)subtype));
            break;
        default:
            snprintf(out, outBytes, "OBJECT TYPE %d", (sdword)object->objtype);
            break;
    }
    if (raceOut) *raceOut = race;
    if (subtypeOut) *subtypeOut = subtype;
}

static sdword mmeFindTracked(SpaceObj *object)
{
    sdword i;
    for (i = 0; i < mmeTrackedCount; ++i)
        if (mmeTracked[i].object == object && !mmeTracked[i].deleted)
            return i;
    return -1;
}

static sdword mmeTrackObject(SpaceObj *object, bool32 original)
{
    MMETrackedObject *track;
    vector *position;
    RotInfo *rot;
    sdword existing;
    if (object == NULL) return -1;
    existing = mmeFindTracked(object);
    if (existing >= 0) return existing;
    if (mmeTrackedCount >= MME_MAX_TRACKED) return -1;
    track = &mmeTracked[mmeTrackedCount];
    memset(track, 0, sizeof(*track));
    track->object = object;
    track->objtype = object->objtype;
    track->runtimeId = mmeRuntimeId(object);
    track->original = original;
    track->created = !original;
    track->scale = mmeObjectScale(object);
    mmeDescribeObject(object, track->label, sizeof(track->label), &track->race, &track->subtype);
    position = mmeObjectPosition(object);
    if (position != NULL) track->originalPosition = *position;
    rot = mmeObjectRotInfo(object);
    if (rot != NULL) track->originalCoordsys = rot->coordsys;
    return mmeTrackedCount++;
}

static void mmeSnapshotScene(void)
{
    Node *node;
    mmeTrackedCount = 0;
    node = universe.ShipList.head;
    while (node != NULL)
    {
        Ship *ship = (Ship *)listGetStructOfNode(node);
        mmeTrackObject((SpaceObj *)ship, TRUE);
        node = node->next;
    }
    node = universe.DerelictList.head;
    while (node != NULL)
    {
        Derelict *d = (Derelict *)listGetStructOfNode(node);
        mmeTrackObject((SpaceObj *)d, TRUE);
        node = node->next;
    }
    node = universe.ResourceList.head;
    while (node != NULL)
    {
        Resource *r = (Resource *)listGetStructOfNode(node);
        mmeTrackObject((SpaceObj *)r, TRUE);
        node = node->next;
    }
    mme.selectedTracked = mmeTrackedCount > 0 ? 0 : -1;
}

static bool32 mmeCursorWorldRay(real32 origin[3], real32 direction[3])
{
    const real32 *view = (const real32 *)&rndCameraMatrix;
    real32 ndcX;
    real32 ndcY;
    real32 tanHalfFov;
    real32 aspect;
    real32 viewX;
    real32 viewY;
    real32 viewZ = -1.0f;
    real32 length;
    real32 worldX;
    real32 worldY;
    real32 worldZ;
    real32 fov;
    if (mrCamera == NULL || MAIN_WindowWidth <= 0 || MAIN_WindowHeight <= 0)
        return FALSE;
    ndcX = ((real32)mouseCursorX() / (real32)MAIN_WindowWidth) * 2.0f - 1.0f;
    ndcY = 1.0f - ((real32)mouseCursorY() / (real32)MAIN_WindowHeight) * 2.0f;
    aspect = rndAspectRatio > 0.01f ? rndAspectRatio :
        (real32)MAIN_WindowWidth / (real32)MAIN_WindowHeight;
    fov = mmeClampF(mrCamera->fieldofview, 1.0f, 179.0f);
    tanHalfFov = (real32)tan(fov * (MME_PI / 360.0f));
    viewX = ndcX * tanHalfFov * aspect;
    viewY = ndcY * tanHalfFov;
    length = (real32)sqrt(viewX * viewX + viewY * viewY + viewZ * viewZ);
    if (length <= 0.000001f) return FALSE;
    viewX /= length;
    viewY /= length;
    viewZ /= length;
    worldX = view[0] * viewX + view[1] * viewY + view[2] * viewZ;
    worldY = view[4] * viewX + view[5] * viewY + view[6] * viewZ;
    worldZ = view[8] * viewX + view[9] * viewY + view[10] * viewZ;
    length = (real32)sqrt(worldX * worldX + worldY * worldY + worldZ * worldZ);
    if (length <= 0.000001f) return FALSE;
    direction[0] = worldX / length;
    direction[1] = worldY / length;
    direction[2] = worldZ / length;
    if (origin != NULL)
    {
        origin[0] = mrCamera->eyeposition.x;
        origin[1] = mrCamera->eyeposition.y;
        origin[2] = mrCamera->eyeposition.z;
    }
    return TRUE;
}

static bool32 mmeCursorWorldPoint(vector *out)
{
    real32 origin[3];
    real32 ray[3];
    real32 distance;
    if (out == NULL || !mmeCursorWorldRay(origin, ray)) return FALSE;
    distance = mrCamera != NULL ? mrCamera->distance : 10000.0f;
    if (distance < 500.0f) distance = 500.0f;
    out->x = origin[0] + ray[0] * distance;
    out->y = origin[1] + ray[1] * distance;
    out->z = origin[2] + ray[2] * distance;
    return TRUE;
}

static bool32 mmeProjectPoint(const real32 point[3], sdword *outX, sdword *outY)
{
    hvector world;
    hvector camera;
    hvector screen;
    if (point == NULL || outX == NULL || outY == NULL ||
        MAIN_WindowWidth <= 0 || MAIN_WindowHeight <= 0)
        return FALSE;

    world.x = point[0];
    world.y = point[1];
    world.z = point[2];
    world.w = 1.0f;
    hmatMultiplyHMatByHVec(&camera, &rndCameraMatrix, &world);
    hmatMultiplyHMatByHVec(&screen, &rndProjectionMatrix, &camera);

    /* Match Homeworld's own 3D-to-screen helpers (PiePlate/Select).  The
       previous editor projection assumed a negative camera-space Z convention,
       which is not the convention used by the engine's projection matrix and
       could reject every volume gizmo as being behind the camera. */
    if (screen.z <= 0.0f || (real32)fabs(screen.w) <= 0.000001f)
        return FALSE;

    *outX = primGLToScreenX(screen.x / screen.w);
    *outY = primGLToScreenY(screen.y / screen.w);
    return TRUE;
}

static void mmeStopObjectMotion(SpaceObj *object)
{
    SpaceObjRotImp *rotImp;
    if (object == NULL) return;
    rotImp = (SpaceObjRotImp *)object;
    rotImp->posinfo.velocity.x = rotImp->posinfo.velocity.y = rotImp->posinfo.velocity.z = 0.0f;
    rotImp->posinfo.force.x = rotImp->posinfo.force.y = rotImp->posinfo.force.z = 0.0f;
    rotImp->rotinfo.rotspeed.x = rotImp->rotinfo.rotspeed.y = rotImp->rotinfo.rotspeed.z = 0.0f;
    rotImp->rotinfo.torque.x = rotImp->rotinfo.torque.y = rotImp->rotinfo.torque.z = 0.0f;
}

static void mmeMoveObjectTo(SpaceObj *object, const vector *position)
{
    vector *dest = mmeObjectPosition(object);
    if (dest == NULL || position == NULL) return;
    *dest = *position;
    mmeStopObjectMotion(object);
    univUpdateObjRotInfo((SpaceObjRot *)object);
}

static MMETrackedObject *mmeSelectedTrack(void)
{
    if (mme.selectedTracked < 0 || mme.selectedTracked >= mmeTrackedCount)
        return NULL;
    if (mmeTracked[mme.selectedTracked].deleted) return NULL;
    return &mmeTracked[mme.selectedTracked];
}

static MMEVolume *mmeSelectedVolume(void)
{
    if (mme.selectedVolume < 0 || mme.selectedVolume >= mmeVolumeCount)
        return NULL;
    if (!mmeVolumes[mme.selectedVolume].inUse) return NULL;
    return &mmeVolumes[mme.selectedVolume];
}

static void mmeSelectTrack(sdword index)
{
    if (index < 0 || index >= mmeTrackedCount || mmeTracked[index].deleted)
    {
        mme.selectedTracked = -1;
        return;
    }
    mme.selectedTracked = index;
    mme.selectedVolume = -1;
    mme.gizmoDragKind = MME_GIZMO_NONE;
    mme.inspectorRow = 0;
}

static void mmeSelectVolume(sdword index)
{
    if (index < 0 || index >= mmeVolumeCount || !mmeVolumes[index].inUse)
    {
        mme.selectedVolume = -1;
        return;
    }
    mme.selectedVolume = index;
    mme.selectedTracked = -1;
    mme.gizmoDragKind = MME_GIZMO_NONE;
    mme.inspectorRow = 0;
}

static real32 mmeWrapDegrees(real32 degrees)
{
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

static void mmeVolumeLocalToWorld(const MMEVolume *volume,
                                  real32 lx, real32 ly, real32 lz,
                                  real32 out[3])
{
    real32 rx, ry, rz;
    real32 cx, sx, cy, sy, cz, sz;
    real32 x1, y1, z1, x2, y2, z2;
    if (volume == NULL || out == NULL) return;
    rx = volume->data.rotationDegrees[0] * MME_PI / 180.0f;
    ry = volume->data.rotationDegrees[1] * MME_PI / 180.0f;
    rz = volume->data.rotationDegrees[2] * MME_PI / 180.0f;
    cx = (real32)cos(rx); sx = (real32)sin(rx);
    cy = (real32)cos(ry); sy = (real32)sin(ry);
    cz = (real32)cos(rz); sz = (real32)sin(rz);

    /* Local -> world applies X, then Y, then Z.  The shader uses the exact
       inverse order, so editor wireframes and density remain locked together. */
    x1 = lx;
    y1 = ly * cx - lz * sx;
    z1 = ly * sx + lz * cx;
    x2 = x1 * cy + z1 * sy;
    y2 = y1;
    z2 = -x1 * sy + z1 * cy;
    out[0] = volume->data.position[0] + x2 * cz - y2 * sz;
    out[1] = volume->data.position[1] + x2 * sz + y2 * cz;
    out[2] = volume->data.position[2] + z2;
}

static bool32 mmeSelectedWorldCenter(real32 out[3])
{
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    if (out == NULL) return FALSE;
    if (track != NULL && track->object != NULL)
    {
        vector *p = mmeObjectPosition(track->object);
        if (p == NULL) return FALSE;
        out[0] = p->x; out[1] = p->y; out[2] = p->z;
        return TRUE;
    }
    if (volume != NULL)
    {
        out[0] = volume->data.position[0];
        out[1] = volume->data.position[1];
        out[2] = volume->data.position[2];
        return TRUE;
    }
    return FALSE;
}

static bool32 mmeBuildGizmoProjection(MMEGizmoProjection *gizmo)
{
    real32 eye[3];
    real32 delta[3];
    real32 distance;
    real32 fov;
    real32 worldPerPixel;
    real32 desiredPixels;
    sdword axis;
    if (gizmo == NULL || mrCamera == NULL || MAIN_WindowHeight <= 0)
        return FALSE;
    memset(gizmo, 0, sizeof(*gizmo));
    if (!mmeSelectedWorldCenter(gizmo->centerWorld) ||
        !mmeProjectPoint(gizmo->centerWorld, &gizmo->centerX, &gizmo->centerY))
        return FALSE;

    eye[0] = mrCamera->eyeposition.x;
    eye[1] = mrCamera->eyeposition.y;
    eye[2] = mrCamera->eyeposition.z;
    delta[0] = gizmo->centerWorld[0] - eye[0];
    delta[1] = gizmo->centerWorld[1] - eye[1];
    delta[2] = gizmo->centerWorld[2] - eye[2];
    distance = (real32)sqrt(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
    distance = max(distance, 100.0f);
    fov = mmeClampF(mrCamera->fieldofview, 1.0f, 179.0f);
    worldPerPixel = (2.0f * distance * (real32)tan(fov * MME_PI / 360.0f)) /
                    (real32)MAIN_WindowHeight;
    desiredPixels = (real32)uiScaleSize(74);
    gizmo->worldLength = mmeClampF(worldPerPixel * desiredPixels, 40.0f, 150000.0f);

    for (axis = 0; axis < 3; ++axis)
    {
        real32 movePoint[3] = {gizmo->centerWorld[0], gizmo->centerWorld[1], gizmo->centerWorld[2]};
        real32 rotatePoint[3] = {gizmo->centerWorld[0], gizmo->centerWorld[1], gizmo->centerWorld[2]};
        real32 dx, dy, length;
        movePoint[axis] += gizmo->worldLength;
        rotatePoint[axis] += gizmo->worldLength * 1.30f;
        if (!mmeProjectPoint(movePoint, &gizmo->moveX[axis], &gizmo->moveY[axis]) ||
            !mmeProjectPoint(rotatePoint, &gizmo->rotateX[axis], &gizmo->rotateY[axis]))
            continue;
        dx = (real32)(gizmo->moveX[axis] - gizmo->centerX);
        dy = (real32)(gizmo->moveY[axis] - gizmo->centerY);
        length = (real32)sqrt(dx*dx + dy*dy);
        gizmo->axisScreenLength[axis] = length;
        if (length > 0.001f)
        {
            gizmo->axisScreenX[axis] = dx / length;
            gizmo->axisScreenY[axis] = dy / length;
        }
        else
        {
            gizmo->axisScreenX[axis] = 0.0f;
            gizmo->axisScreenY[axis] = 0.0f;
        }
        gizmo->axisVisible[axis] = TRUE;
    }
    gizmo->valid = TRUE;
    return TRUE;
}

static real32 mmePointDistanceSquared(sdword ax, sdword ay, sdword bx, sdword by)
{
    real32 dx = (real32)(ax - bx);
    real32 dy = (real32)(ay - by);
    return dx*dx + dy*dy;
}

static real32 mmePointSegmentDistanceSquared(real32 px, real32 py,
                                             real32 ax, real32 ay,
                                             real32 bx, real32 by)
{
    real32 abx = bx - ax;
    real32 aby = by - ay;
    real32 denom = abx*abx + aby*aby;
    real32 t;
    real32 dx;
    real32 dy;
    if (denom <= 0.0001f)
    {
        dx = px - ax; dy = py - ay;
        return dx*dx + dy*dy;
    }
    t = ((px-ax)*abx + (py-ay)*aby) / denom;
    t = mmeClampF(t, 0.0f, 1.0f);
    dx = px - (ax + abx*t);
    dy = py - (ay + aby*t);
    return dx*dx + dy*dy;
}

static sdword mmeHitTransformGizmo(sdword mouseX, sdword mouseY, sdword *axisOut)
{
    MMEGizmoProjection gizmo;
    real32 best = 1.0e30f;
    sdword bestKind = MME_GIZMO_NONE;
    sdword bestAxis = -1;
    sdword axis;
    real32 rotateRadius = (real32)uiScaleSize(12);
    real32 translateRadius = (real32)uiScaleSize(9);
    if (axisOut != NULL) *axisOut = -1;
    if (!mmeBuildGizmoProjection(&gizmo)) return MME_GIZMO_NONE;

    /* Rotation knobs get first claim; they deliberately sit beyond each arrow
       tip so direct translation and rotation never fight for the same click. */
    for (axis = 0; axis < 3; ++axis)
    {
        real32 d;
        if (!gizmo.axisVisible[axis]) continue;
        d = mmePointDistanceSquared(mouseX, mouseY,
                                    gizmo.rotateX[axis], gizmo.rotateY[axis]);
        if (d <= rotateRadius*rotateRadius && d < best)
        {
            best = d; bestKind = MME_GIZMO_ROTATE; bestAxis = axis;
        }
    }
    if (bestKind != MME_GIZMO_NONE)
    {
        if (axisOut != NULL) *axisOut = bestAxis;
        return bestKind;
    }

    for (axis = 0; axis < 3; ++axis)
    {
        real32 startX, startY, d;
        if (!gizmo.axisVisible[axis]) continue;
        /* The outer 45% of each arrow is draggable, matching the requested
           grab-the-end interaction while keeping the centre clear for picking. */
        startX = (real32)gizmo.centerX +
                 ((real32)gizmo.moveX[axis] - (real32)gizmo.centerX) * 0.55f;
        startY = (real32)gizmo.centerY +
                 ((real32)gizmo.moveY[axis] - (real32)gizmo.centerY) * 0.55f;
        d = mmePointSegmentDistanceSquared((real32)mouseX, (real32)mouseY,
                                           startX, startY,
                                           (real32)gizmo.moveX[axis],
                                           (real32)gizmo.moveY[axis]);
        if (d <= translateRadius*translateRadius && d < best)
        {
            best = d; bestKind = MME_GIZMO_TRANSLATE; bestAxis = axis;
        }
    }
    if (axisOut != NULL) *axisOut = bestAxis;
    return bestKind;
}

static bool32 mmeBeginGizmoDrag(sdword mouseX, sdword mouseY)
{
    MMEGizmoProjection gizmo;
    sdword axis = -1;
    sdword kind = mmeHitTransformGizmo(mouseX, mouseY, &axis);
    real32 length;
    if (kind == MME_GIZMO_NONE || axis < 0 || !mmeBuildGizmoProjection(&gizmo))
        return FALSE;
    mme.gizmoDragKind = kind;
    mme.gizmoDragAxis = axis;
    mme.gizmoLastMouseX = mouseX;
    mme.gizmoLastMouseY = mouseY;
    mme.axis = axis;
    mme.transformMode = kind == MME_GIZMO_TRANSLATE ? MME_TRANSFORM_MOVE : MME_TRANSFORM_ROTATE;
    length = max(gizmo.axisScreenLength[axis], 1.0f);
    mme.gizmoAxisScreenX = gizmo.axisScreenX[axis];
    mme.gizmoAxisScreenY = gizmo.axisScreenY[axis];
    mme.gizmoWorldPerPixel = gizmo.worldLength / max(length, 12.0f);
    mmeStatus(kind == MME_GIZMO_TRANSLATE ?
        "GIZMO MOVE - DRAG AXIS / RELEASE TO FINISH" :
        "GIZMO ROTATE - DRAG BLUE HANDLE / RELEASE TO FINISH");
    return TRUE;
}

static bool32 mmeUpdateGizmoDrag(sdword mouseX, sdword mouseY)
{
    MMETrackedObject *track;
    MMEVolume *volume;
    real32 dx;
    real32 dy;
    real32 scalar;
    sdword axis;
    if (mme.gizmoDragKind == MME_GIZMO_NONE) return FALSE;
    axis = mmeClamp(mme.gizmoDragAxis, MME_AXIS_X, MME_AXIS_Z);
    dx = (real32)(mouseX - mme.gizmoLastMouseX);
    dy = (real32)(mouseY - mme.gizmoLastMouseY);
    mme.gizmoLastMouseX = mouseX;
    mme.gizmoLastMouseY = mouseY;
    track = mmeSelectedTrack();
    volume = mmeSelectedVolume();
    if (track == NULL && volume == NULL)
    {
        mme.gizmoDragKind = MME_GIZMO_NONE;
        return FALSE;
    }

    if (mme.gizmoDragKind == MME_GIZMO_TRANSLATE)
    {
        real32 deltaWorld;
        scalar = dx * mme.gizmoAxisScreenX + dy * mme.gizmoAxisScreenY;
        if ((real32)fabs(mme.gizmoAxisScreenX) + (real32)fabs(mme.gizmoAxisScreenY) < 0.05f)
            scalar = -dy;
        deltaWorld = scalar * mme.gizmoWorldPerPixel;
        if (keyIsHit(LCONTROLKEY) || keyIsHit(RCONTROLKEY)) deltaWorld *= 0.20f;
        if (keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY)) deltaWorld *= 4.0f;
        if (track != NULL && track->object != NULL)
        {
            vector *p = mmeObjectPosition(track->object);
            if (p != NULL)
            {
                if (axis == MME_AXIS_X) p->x += deltaWorld;
                else if (axis == MME_AXIS_Y) p->y += deltaWorld;
                else p->z += deltaWorld;
                mmeStopObjectMotion(track->object);
                univUpdateObjRotInfo((SpaceObjRot *)track->object);
                track->dirty = TRUE;
            }
        }
        else if (volume != NULL)
        {
            volume->data.position[axis] += deltaWorld;
            mmeSyncDustVolumesToRenderer();
        }
    }
    else
    {
        real32 degrees;
        /* Drag tangent to the projected axis. This feels like pulling the blue
           circular-arrow knob around the selected object instead of typing an
           angle or entering a separate rotation mode. */
        scalar = dx * (-mme.gizmoAxisScreenY) + dy * mme.gizmoAxisScreenX;
        if ((real32)fabs(mme.gizmoAxisScreenX) + (real32)fabs(mme.gizmoAxisScreenY) < 0.05f)
            scalar = dx;
        degrees = scalar * 0.72f;
        if (keyIsHit(LCONTROLKEY) || keyIsHit(RCONTROLKEY)) degrees *= 0.20f;
        if (keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY)) degrees *= 2.0f;
        if (track != NULL && track->object != NULL)
            mmeRotateTrack(track, axis, degrees);
        else if (volume != NULL)
        {
            volume->data.rotationDegrees[axis] =
                mmeWrapDegrees(volume->data.rotationDegrees[axis] + degrees);
            mmeSyncDustVolumesToRenderer();
        }
    }
    return TRUE;
}

static SpaceObj *mmeCreateFromAsset(const MMEAsset *asset, const vector *position)
{
    if (asset == NULL || position == NULL) return NULL;
    switch (asset->kind)
    {
        case MME_ASSET_SHIP:
            return (SpaceObj *)univAddShip((ShipType)asset->subtype, asset->race,
                                          (vector *)position, universe.curPlayerPtr, 0);
        case MME_ASSET_DERELICT:
            return (SpaceObj *)univAddDerelict((DerelictType)asset->subtype, (vector *)position);
        case MME_ASSET_ASTEROID:
            return (SpaceObj *)univAddAsteroid((AsteroidType)asset->subtype, (vector *)position);
        case MME_ASSET_DUST:
            return (SpaceObj *)univAddDustCloud((DustCloudType)asset->subtype, (vector *)position);
        case MME_ASSET_GAS:
            return (SpaceObj *)univAddGasCloud((GasCloudType)asset->subtype, (vector *)position);
        default:
            return NULL;
    }
}

static sdword mmeCreateDustVolume(const vector *position)
{
    MMEVolume *volume;
    sdword index;
    for (index = 0; index < mmeVolumeCount; ++index)
        if (!mmeVolumes[index].inUse) break;
    if (index >= MME_MAX_VOLUMES) return -1;
    if (index == mmeVolumeCount)
    {
        if (mmeVolumeCount >= MME_MAX_VOLUMES) return -1;
        ++mmeVolumeCount;
    }
    volume = &mmeVolumes[index];
    memset(volume, 0, sizeof(*volume));
    volume->inUse = TRUE;
    volume->data.enabled = TRUE;
    volume->data.shape = MME_DUST_ELLIPSOID;
    volume->data.position[0] = position->x;
    volume->data.position[1] = position->y;
    volume->data.position[2] = position->z;
    volume->data.size[0] = 8000.0f;
    volume->data.size[1] = 4200.0f;
    volume->data.size[2] = 8000.0f;
    volume->data.density = 0.35f;
    volume->data.scattering = 1.0f;
    volume->data.absorption = 0.15f;
    volume->data.anisotropy = 0.45f;
    volume->data.coverage = 1.0f;
    volume->data.color[0] = 1.0f;
    volume->data.color[1] = 1.0f;
    volume->data.color[2] = 1.0f;
    volume->data.noiseScale = 0.0015f;
    volume->data.noiseDetail = 0.55f;
    volume->data.wakeStrength = 1.0f;
    volume->data.shapeSeed = (udword)(0x9e3779b9u ^ (udword)SDL_GetTicks() ^
                                     (udword)(mmeVolumeCount + 1) * 0x85ebca6bu);
    if (volume->data.shapeSeed == 0) volume->data.shapeSeed = 1;
    volume->data.shapeVariation = 0.72f;
    volume->data.receiveMissionKey = TRUE;
    volume->data.receiveLocalLights = TRUE;
    volume->data.castVolumetricShadow = TRUE;
    snprintf(volume->label, sizeof(volume->label), "RTX DUST VOLUME %02d", index + 1);
    return index;
}

static bool32 mmePlaceCurrentAsset(void)
{
    vector position;
    MMEAsset *asset;
    if (mme.selectedAsset < 0 || mme.selectedAsset >= mmeAssetCount)
    {
        mmeStatus("SELECT AN ASSET FIRST");
        return FALSE;
    }
    if (!mmeCursorWorldPoint(&position))
    {
        mmeStatus("CAMERA RAY NOT READY");
        return FALSE;
    }
    asset = &mmeAssets[mme.selectedAsset];
    if (asset->kind == MME_ASSET_RTX_DUST_VOLUME)
    {
        sdword index = mmeCreateDustVolume(&position);
        if (index < 0)
        {
            mmeStatus("RTX VOLUME LIMIT REACHED");
            return FALSE;
        }
        mme.tab = MME_TAB_VOLUMES;
        mmeSelectVolume(index);
        mmeSyncDustVolumesToRenderer();
        mmeStatus("RTX DUST VOLUME PLACED / REAL GPU RAYMARCH ACTIVE");
        return TRUE;
    }
    else
    {
        SpaceObj *object = mmeCreateFromAsset(asset, &position);
        sdword track;
        if (object == NULL)
        {
            mmeStatus("ASSET COULD NOT BE CREATED - STATIC DATA MAY NOT BE LOADED");
            return FALSE;
        }
        track = mmeTrackObject(object, FALSE);
        if (track >= 0)
        {
            mme.tab = MME_TAB_SCENE;
            mmeSelectTrack(track);
            mmeStatus("OBJECT PLACED / UNSAVED");
            return TRUE;
        }
    }
    return FALSE;
}

static bool32 mmeMoveSelectionToCursor(void)
{
    vector position;
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    if (!mmeCursorWorldPoint(&position)) return FALSE;
    if (track != NULL && track->object != NULL)
    {
        mmeMoveObjectTo(track->object, &position);
        track->dirty = TRUE;
        mmeStatus("OBJECT MOVED TO CURSOR DEPTH / UNSAVED");
        return TRUE;
    }
    if (volume != NULL)
    {
        volume->data.position[0] = position.x;
        volume->data.position[1] = position.y;
        volume->data.position[2] = position.z;
        mmeStatus("VOLUME MOVED TO CURSOR DEPTH / UNSAVED");
        return TRUE;
    }
    return FALSE;
}

static bool32 mmeDeleteSelection(void)
{
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    if (volume != NULL)
    {
        volume->inUse = FALSE;
        mme.selectedVolume = -1;
        mmeStatus("RTX VOLUME DELETED / UNSAVED");
        return TRUE;
    }
    if (track == NULL || track->object == NULL) return FALSE;
    if (track->object->objtype == OBJ_ShipType)
        univReallyDeleteThisShipRightNow((Ship *)track->object);
    else if (track->object->objtype == OBJ_DerelictType)
        univReallyDeleteThisDerelictRightNow((Derelict *)track->object);
    else if (track->object->objtype == OBJ_AsteroidType ||
             track->object->objtype == OBJ_DustType ||
             track->object->objtype == OBJ_GasType ||
             track->object->objtype == OBJ_NebulaType)
        univReallyDeleteThisResourceRightNow((Resource *)track->object);
    else
        return FALSE;
    track->object = NULL;
    track->deleted = TRUE;
    track->dirty = TRUE;
    mme.selectedTracked = -1;
    mmeStatus(track->created ? "EDITOR OBJECT DELETED" : "ORIGINAL MISSION OBJECT MARKED FOR DELETE / UNSAVED");
    return TRUE;
}

static bool32 mmeDuplicateSelection(void)
{
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    vector position;
    if (volume != NULL)
    {
        sdword index;
        if (mmeVolumeCount >= MME_MAX_VOLUMES) return FALSE;
        mmeVolumes[mmeVolumeCount] = *volume;
        mmeVolumes[mmeVolumeCount].data.position[0] += 500.0f;
        snprintf(mmeVolumes[mmeVolumeCount].label, sizeof(mmeVolumes[mmeVolumeCount].label),
                 "RTX DUST VOLUME %02d", mmeVolumeCount + 1);
        index = mmeVolumeCount++;
        mmeSelectVolume(index);
        mmeStatus("VOLUME DUPLICATED / UNSAVED");
        return TRUE;
    }
    if (track == NULL || track->object == NULL) return FALSE;
    position = *mmeObjectPosition(track->object);
    position.x += 500.0f;
    {
        SpaceObj *created = NULL;
        if (track->objtype == OBJ_ShipType)
            created = (SpaceObj *)univAddShip((ShipType)track->subtype, track->race,
                                              &position, universe.curPlayerPtr, 0);
        else if (track->objtype == OBJ_DerelictType)
            created = (SpaceObj *)univAddDerelict((DerelictType)track->subtype, &position);
        else if (track->objtype == OBJ_AsteroidType)
            created = (SpaceObj *)univAddAsteroid((AsteroidType)track->subtype, &position);
        else if (track->objtype == OBJ_DustType)
            created = (SpaceObj *)univAddDustCloud((DustCloudType)track->subtype, &position);
        else if (track->objtype == OBJ_GasType)
            created = (SpaceObj *)univAddGasCloud((GasCloudType)track->subtype, &position);
        if (created != NULL)
        {
            sdword newTrack = mmeTrackObject(created, FALSE);
            if (newTrack >= 0)
            {
                mmeSelectTrack(newTrack);
                mmeStatus("OBJECT DUPLICATED / UNSAVED");
                return TRUE;
            }
        }
    }
    return FALSE;
}


static void mmeRotateTrack(MMETrackedObject *track, sdword axis, real32 degrees)
{
    real32 radians = degrees * MME_PI / 180.0f;
    if (track == NULL || track->object == NULL) return;
    if (axis == MME_AXIS_X) univRotateObjPitch((SpaceObjRot *)track->object, radians);
    else if (axis == MME_AXIS_Y) univRotateObjYaw((SpaceObjRot *)track->object, radians);
    else univRotateObjRoll((SpaceObjRot *)track->object, radians);
    track->rotationDegrees[axis] += degrees;
    track->dirty = TRUE;
    univUpdateObjRotInfo((SpaceObjRot *)track->object);
}


static bool32 mmeCopySelectedVolume(void)
{
    MMEVolume *volume = mmeSelectedVolume();
    if (volume == NULL)
    {
        mmeStatus("SELECT A DUST VOLUME TO COPY");
        return FALSE;
    }
    mmeVolumeClipboard = volume->data;
    mmeVolumeClipboardValid = TRUE;
    mmeStatus("DUST VOLUME COPIED / CTRL+V OR PASTE BUTTON TO CREATE COPY");
    return TRUE;
}

static bool32 mmePasteVolumeClipboard(void)
{
    MMEVolume *source = mmeSelectedVolume();
    MMEVolume *dst;
    sdword index;
    if (!mmeVolumeClipboardValid)
    {
        mmeStatus("DUST CLIPBOARD IS EMPTY / COPY A VOLUME FIRST");
        return FALSE;
    }
    {
        vector spawn;
        spawn.x = mmeVolumeClipboard.position[0];
        spawn.y = mmeVolumeClipboard.position[1];
        spawn.z = mmeVolumeClipboard.position[2];
        index = mmeCreateDustVolume(&spawn);
    }
    if (index < 0)
    {
        mmeStatus("RTX VOLUME LIMIT REACHED");
        return FALSE;
    }
    dst = &mmeVolumes[index];
    dst->data = mmeVolumeClipboard;
    /* Paste is a real object-level duplicate, unlike preset LOAD. Preserve the
       authored transform and offset the new cloud just enough to expose it for
       immediate gizmo dragging. If a source volume is selected, offset from
       that source so repeated paste remains predictable. */
    if (source != NULL)
    {
        dst->data.position[0] = source->data.position[0] + 500.0f;
        dst->data.position[1] = source->data.position[1];
        dst->data.position[2] = source->data.position[2];
    }
    else
    {
        dst->data.position[0] += 500.0f;
    }
    snprintf(dst->label, sizeof(dst->label), "RTX DUST VOLUME %02d", index + 1);
    mme.tab = MME_TAB_VOLUMES;
    mmeSelectVolume(index);
    mmeSyncDustVolumesToRenderer();
    mmeStatus("DUST VOLUME PASTED / FULL MATERIAL + SHAPE + TRANSFORM COPIED");
    return TRUE;
}

static bool32 mmeAdjustSelection(sdword direction)
{
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    real32 fine = (keyIsHit(LCONTROLKEY) || keyIsHit(RCONTROLKEY)) ? 0.1f : 1.0f;
    real32 coarse = (keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY)) ? 10.0f : 1.0f;
    if (volume != NULL)
    {
        ModernMapDustVolumeSnapshot *v = &volume->data;
        real32 d = (real32)direction;
        switch (mme.inspectorRow)
        {
            case 0:
                if (direction != 0) v->enabled = !v->enabled;
                break;
            case 1:
                v->shape = (ModernMapDustShape)(((sdword)v->shape + (direction > 0 ? 1 : 2)) % 3);
                break;
            case 2: v->position[0] += d * 100.0f * coarse * fine; break;
            case 3: v->position[1] += d * 100.0f * coarse * fine; break;
            case 4: v->position[2] += d * 100.0f * coarse * fine; break;
            case 5: v->rotationDegrees[0] = mmeWrapDegrees(v->rotationDegrees[0] + d * 5.0f * fine); break;
            case 6: v->rotationDegrees[1] = mmeWrapDegrees(v->rotationDegrees[1] + d * 5.0f * fine); break;
            case 7: v->rotationDegrees[2] = mmeWrapDegrees(v->rotationDegrees[2] + d * 5.0f * fine); break;
            case 8: v->size[0] = mmeClampF(v->size[0] + d * 250.0f * coarse * fine, 50.0f, 500000.0f); break;
            case 9: v->size[1] = mmeClampF(v->size[1] + d * 250.0f * coarse * fine, 50.0f, 500000.0f); break;
            case 10: v->size[2] = mmeClampF(v->size[2] + d * 250.0f * coarse * fine, 50.0f, 500000.0f); break;
            case 11: v->density = mmeClampF(v->density + d * 0.02f * fine, 0.0f, 10.0f); break;
            case 12: v->scattering = mmeClampF(v->scattering + d * 0.05f * fine, 0.0f, 10.0f); break;
            case 13: v->absorption = mmeClampF(v->absorption + d * 0.02f * fine, 0.0f, 10.0f); break;
            case 14: v->anisotropy = mmeClampF(v->anisotropy + d * 0.02f * fine, -0.95f, 0.95f); break;
            case 15: v->coverage = mmeClampF(v->coverage + d * 0.02f * fine, 0.0f, 2.0f); break;
            case 16: v->color[0] = mmeClampF(v->color[0] + d * 0.02f * fine, 0.0f, 8.0f); break;
            case 17: v->color[1] = mmeClampF(v->color[1] + d * 0.02f * fine, 0.0f, 8.0f); break;
            case 18: v->color[2] = mmeClampF(v->color[2] + d * 0.02f * fine, 0.0f, 8.0f); break;
            case 19: v->noiseScale = mmeClampF(v->noiseScale + d * 0.0001f * fine, 0.00001f, 0.1f); break;
            case 20: v->noiseDetail = mmeClampF(v->noiseDetail + d * 0.02f * fine, 0.0f, 2.0f); break;
            case 21: v->wakeStrength = mmeClampF(v->wakeStrength + d * 0.05f * fine, 0.0f, 2.0f); break;
            case 22:
                if (direction != 0) v->receiveMissionKey = !v->receiveMissionKey;
                break;
            case 23:
                if (direction != 0) v->receiveLocalLights = !v->receiveLocalLights;
                break;
            case 24:
                if (direction != 0) v->castVolumetricShadow = !v->castVolumetricShadow;
                break;
            case 25:
                if (direction != 0)
                {
                    sdword delta = direction > 0 ? 1 : -1;
                    v->shapeSeed = (udword)((sdword)v->shapeSeed + delta);
                    if (v->shapeSeed == 0) v->shapeSeed = 1;
                }
                break;
            case 26:
                v->shapeVariation = mmeClampF(v->shapeVariation + d * 0.03f * fine, 0.0f, 2.0f);
                break;
            case 27:
                mmeDustFadeDistance = mmeClampF(
                    mmeDustFadeDistance + d * 5000.0f * coarse * fine,
                    5000.0f, 250000.0f);
                break;
            case 28:
                mmeDustFadeStrength = mmeClampF(
                    mmeDustFadeStrength + d * 0.10f * fine, 0.0f, 4.0f);
                break;
        }
        mmeSyncDustVolumesToRenderer();
        mmeStatus("VOLUME MATERIAL/TRANSFORM LIVE / UNSAVED");
        return TRUE;
    }
    if (track == NULL || track->object == NULL) return FALSE;
    if (mme.transformMode == MME_TRANSFORM_MOVE)
    {
        vector *position = mmeObjectPosition(track->object);
        real32 step = 100.0f * coarse * fine * (real32)direction;
        if (position == NULL) return FALSE;
        if (mme.axis == MME_AXIS_X) position->x += step;
        else if (mme.axis == MME_AXIS_Y) position->y += step;
        else position->z += step;
        mmeStopObjectMotion(track->object);
        univUpdateObjRotInfo((SpaceObjRot *)track->object);
        track->dirty = TRUE;
    }
    else if (mme.transformMode == MME_TRANSFORM_ROTATE)
    {
        mmeRotateTrack(track, mme.axis, 5.0f * fine * (real32)direction);
    }
    else
    {
        real32 scale = mmeObjectScale(track->object);
        if (track->objtype != OBJ_AsteroidType && track->objtype != OBJ_DustType &&
            track->objtype != OBJ_GasType && track->objtype != OBJ_NebulaType)
        {
            mmeStatus("INSTANCE SCALE IS ONLY SAFE FOR RESOURCES/VOLUMES");
            return FALSE;
        }
        scale = mmeClampF(scale + 0.1f * fine * (real32)direction, 0.05f, 50.0f);
        mmeSetObjectScale(track->object, scale);
        track->scale = scale;
        track->dirty = TRUE;
    }
    mmeStatus("OBJECT TRANSFORM LIVE / UNSAVED");
    return TRUE;
}

static void mmeCameraClampAngles(Camera *camera)
{
    const real32 limit = 85.0f * MME_PI / 180.0f;
    if (camera == NULL) return;
    while (camera->angle < 0.0f) camera->angle += 2.0f * MME_PI;
    while (camera->angle >= 2.0f * MME_PI) camera->angle -= 2.0f * MME_PI;
    camera->declination = mmeClampF(camera->declination, -limit, limit);
}

static void mmeCameraForwardFromAngles(const Camera *camera, vector *forward)
{
    real32 cosDecl;
    if (camera == NULL || forward == NULL) return;
    cosDecl = (real32)cos(camera->declination);
    forward->x = -(real32)cos(camera->angle) * cosDecl;
    forward->y = -(real32)sin(camera->angle) * cosDecl;
    forward->z =  (real32)sin(camera->declination);
    if (vecMagnitudeSquared(*forward) > 0.000001f) vecNormalize(forward);
}

static void mmeCameraAnglesFromForward(const vector *forward, real32 *angle, real32 *declination)
{
    vector direction;
    if (forward == NULL || angle == NULL || declination == NULL) return;
    direction = *forward;
    if (vecMagnitudeSquared(direction) <= 0.000001f) return;
    vecNormalize(&direction);
    *declination = (real32)asin(mmeClampF(direction.z, -1.0f, 1.0f));
    *angle = (real32)atan2(-direction.y, -direction.x);
    if (*angle < 0.0f) *angle += 2.0f * MME_PI;
}

static void mmeFocusSelection(void)
{
    vector target;
    vector forward;
    real32 distance;
    CameraCommand *command = &universe.mainCameraCommand;
    CameraStackEntry *entry = command->currentCameraStack;
    Camera *desired = entry != NULL ? &entry->remembercam : &command->actualcamera;
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    if (track != NULL && track->object != NULL)
        target = *mmeObjectPosition(track->object);
    else if (volume != NULL)
    {
        target.x = volume->data.position[0];
        target.y = volume->data.position[1];
        target.z = volume->data.position[2];
    }
    else return;

    if (!mme.freeCameraInitialized)
    {
        mme.freeCameraEye = command->actualcamera.eyeposition;
        mme.freeCameraLookat = command->actualcamera.lookatpoint;
        mme.freeCameraDistance = max(command->actualcamera.distance, 1.0f);
        mme.freeCameraInitialized = TRUE;
    }
    vecSub(forward, target, mme.freeCameraEye);
    distance = (real32)sqrt(vecMagnitudeSquared(forward));
    if (distance <= 1.0f) return;
    mme.freeCameraDistance = distance;
    mme.freeCameraLookat = target;
    mmeCameraAnglesFromForward(&forward, &desired->angle, &desired->declination);
    mmeCameraClampAngles(desired);
    mmeStatus("CAMERA AIM FOCUSED ON SELECTION");
}

static bool32 mmeControlHeld(void)
{
    return keyIsHit(LCONTROLKEY) || keyIsHit(RCONTROLKEY);
}

static bool32 mmeShiftHeld(void)
{
    return keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY);
}

static void mmeFreeCameraAdoptCurrentPivot(void)
{
    Camera *camera = &universe.mainCameraCommand.actualcamera;
    vector delta;
    mme.freeCameraEye = camera->eyeposition;
    mme.freeCameraLookat = camera->lookatpoint;
    vecSub(delta, camera->lookatpoint, camera->eyeposition);
    mme.freeCameraDistance = (real32)sqrt(vecMagnitudeSquared(delta));
    if (mme.freeCameraDistance <= 1.0f)
        mme.freeCameraDistance = max(camera->distance, 1.0f);
    mme.freeCameraInitialized = TRUE;
}

static void mmeApplyFreeCameraPose(void)
{
    CameraCommand *command = &universe.mainCameraCommand;
    CameraStackEntry *entry = command->currentCameraStack;
    Camera *desired = entry != NULL ? &entry->remembercam : &command->actualcamera;
    vector forward;
    vector offset;
    if (!mme.active || desired == NULL) return;
    if (!mme.freeCameraInitialized) mmeFreeCameraAdoptCurrentPivot();

    /* Shift+F11 owns a genuine FPS camera pose. Legacy Homeworld input still
       supplies yaw/declination, but it no longer orbits the eye around a fleet
       focus point. Rebuild the look target from the editor-owned fixed eye. */
    mmeCameraClampAngles(desired);
    mmeCameraForwardFromAngles(desired, &forward);
    if (vecMagnitudeSquared(forward) <= 0.000001f) return;
    offset = forward;
    vecMultiplyByScalar(offset, mme.freeCameraDistance);
    vecAdd(mme.freeCameraLookat, mme.freeCameraEye, offset);

    desired->oldlookatpoint = desired->lookatpoint;
    desired->lookatpoint = mme.freeCameraLookat;
    desired->eyeposition = mme.freeCameraEye;
    desired->distance = mme.freeCameraDistance;
    if (desired != &command->actualcamera)
        cameraCopyPositionInfo(&command->actualcamera, desired);
}

static bool32 mmeWheelFly(sdword direction)
{
    CameraCommand *command = &universe.mainCameraCommand;
    CameraStackEntry *entry = command->currentCameraStack;
    Camera *desired = entry != NULL ? &entry->remembercam : &command->actualcamera;
    vector forward;
    vector delta;
    real32 step;
    if (!mme.active || desired == NULL || direction == 0) return FALSE;
    if (!mme.freeCameraInitialized) mmeFreeCameraAdoptCurrentPivot();

    /* Shift+F11 viewport navigation: wheel is locomotion, never zoom. Scroll
       up/down is the FPS W/S axis along the live mouse-look direction. */
    mmeCameraForwardFromAngles(desired, &forward);
    if (vecMagnitudeSquared(forward) <= 0.000001f) return FALSE;
    step = mmeClampF(mme.freeCameraDistance * 0.14f, 90.0f, 12000.0f);
    if (mmeShiftHeld()) step *= 3.0f;
    delta = forward;
    vecMultiplyByScalar(delta, step * (real32)direction);
    vecAddTo(mme.freeCameraEye, delta);
    vecAddTo(mme.freeCameraLookat, delta);
    mmeApplyFreeCameraPose();
    mmeStatus(direction > 0 ?
        "CAMERA FLY: WHEEL UP = FPS FORWARD" :
        "CAMERA FLY: WHEEL DOWN = FPS BACKWARD");
    return TRUE;
}

static void mmeUpdateFreeCamera(void)
{
    if (!mme.active) return;
    /* ccControl runs first and supplies mouse yaw/pitch. This final editor pass
       discards legacy fleet focus/orbit/zoom and reapplies the FPS eye pose. */
    mmeApplyFreeCameraPose();
}

static void mmeApplyCameraPreset(void)
{
    real32 angle = universe.mainCameraCommand.actualcamera.angle;
    real32 decl = universe.mainCameraCommand.actualcamera.declination;
    if (mme.cameraMode == MME_CAMERA_TOP)
    {
        angle = 0.0f;
        decl = MME_PI * 0.49f;
    }
    else if (mme.cameraMode == MME_CAMERA_FRONT)
    {
        angle = 0.0f;
        decl = 0.0f;
    }
    else if (mme.cameraMode == MME_CAMERA_SIDE)
    {
        angle = MME_PI * 0.5f;
        decl = 0.0f;
    }
    else
    {
        mmeStatus("PERSPECTIVE CAMERA - FPS MOUSE LOOK");
        return;
    }
    ccChangeAngleDeclination(&universe.mainCameraCommand, angle, decl);
    if (universe.mainCameraCommand.currentCameraStack != NULL)
    {
        universe.mainCameraCommand.currentCameraStack->remembercam.angle = angle;
        universe.mainCameraCommand.currentCameraStack->remembercam.declination = decl;
    }
    mmeApplyFreeCameraPose();
    mmeStatus(mme.cameraMode == MME_CAMERA_TOP ? "TOP VIEW" :
              (mme.cameraMode == MME_CAMERA_FRONT ? "FRONT VIEW" : "SIDE VIEW"));
}

static const char *mmeViewportName(void)
{
    static const char *names[MME_VIEW_COUNT] = {"LIT", "WIREFRAME", "BOUNDS", "VOLUMES"};
    return names[mmeClamp(mme.viewportMode, 0, MME_VIEW_COUNT - 1)];
}

static const char *mmeCameraName(void)
{
    static const char *names[MME_CAMERA_COUNT] = {"PERSPECTIVE", "TOP", "FRONT", "SIDE"};
    return names[mmeClamp(mme.cameraMode, 0, MME_CAMERA_COUNT - 1)];
}

static const char *mmeAxisName(void)
{
    return mme.axis == MME_AXIS_X ? "X" : (mme.axis == MME_AXIS_Y ? "Y" : "Z");
}

static bool32 mmeExportPath(char *out, sdword outBytes, sdword mission)
{
    char *base = SDL_GetBasePath();
    char root[1024];
    char maps[1024];
    int rootLength;
    int mapsLength;
    int pathLength;
    if (base == NULL || out == NULL || outBytes <= 0)
    {
        if (base != NULL) SDL_free(base);
        return FALSE;
    }
#ifdef _WIN32
    rootLength = snprintf(root, sizeof(root), "%sRTXExports", base);
    mapsLength = snprintf(maps, sizeof(maps), "%sRTXExports\\Maps", base);
    pathLength = snprintf(out, (size_t)outBytes, "%sRTXExports\\Maps\\Mission%02d.rtxmap", base, mission);
#else
    rootLength = snprintf(root, sizeof(root), "%sRTXExports", base);
    mapsLength = snprintf(maps, sizeof(maps), "%sRTXExports/Maps", base);
    pathLength = snprintf(out, (size_t)outBytes, "%sRTXExports/Maps/Mission%02d.rtxmap", base, mission);
#endif
    SDL_free(base);
    if (rootLength < 0 || (size_t)rootLength >= sizeof(root) ||
        mapsLength < 0 || (size_t)mapsLength >= sizeof(maps) ||
        pathLength < 0 || pathLength >= outBytes)
    {
        fprintf(stderr, "[MapEditor] Export path is too long.\n");
        return FALSE;
    }

    /* Mirror RTX-0082's proven export-directory strategy: create each absolute
       Windows path level separately instead of using the legacy recursive helper. */
    if (!fileMakeDirectory(root) || !fileMakeDirectory(maps))
    {
        fprintf(stderr, "[MapEditor] Could not create RTXExports/Maps beside the running executable.\n");
        return FALSE;
    }
    return TRUE;
}

static bool32 mmePackagedMapPath(char *out, sdword outBytes, sdword mission)
{
    char *base = SDL_GetBasePath();
    int pathLength;
    if (base == NULL || out == NULL || outBytes <= 0)
    {
        if (base != NULL) SDL_free(base);
        return FALSE;
    }
#ifdef _WIN32
    pathLength = snprintf(out, (size_t)outBytes,
                          "%sMissions\\Mission%02d.rtxmap", base, mission);
#else
    pathLength = snprintf(out, (size_t)outBytes,
                          "%sMissions/Mission%02d.rtxmap", base, mission);
#endif
    SDL_free(base);
    if (pathLength < 0 || pathLength >= outBytes)
    {
        fprintf(stderr, "[MapEditor] Packaged mission RTXMAP path is too long.\n");
        return FALSE;
    }
    return TRUE;
}


static bool32 mmeCloudPresetPaths(char *directory, sdword directoryBytes,
                                  char *indexPath, sdword indexBytes)
{
    char *base = SDL_GetBasePath();
    char root[1024];
    int rootLength;
    int directoryLength;
    int indexLength;
    if (base == NULL || directory == NULL || indexPath == NULL ||
        directoryBytes <= 0 || indexBytes <= 0)
    {
        if (base != NULL) SDL_free(base);
        return FALSE;
    }
#ifdef _WIN32
    /* The deployed Release directory is atomically replaced by RTX-Build-Deploy.ps1.
       Keep reusable presets at the RTX project root so rebuilds cannot delete them. */
    rootLength = snprintf(root, sizeof(root), "%s..\\..\\RTXExports", base);
    directoryLength = snprintf(directory, (size_t)directoryBytes,
                               "%s..\\..\\RTXExports\\CloudPresets", base);
    indexLength = snprintf(indexPath, (size_t)indexBytes,
                           "%s..\\..\\RTXExports\\CloudPresets\\presets.idx", base);
#else
    rootLength = snprintf(root, sizeof(root), "%s../../RTXExports", base);
    directoryLength = snprintf(directory, (size_t)directoryBytes,
                               "%s../../RTXExports/CloudPresets", base);
    indexLength = snprintf(indexPath, (size_t)indexBytes,
                           "%s../../RTXExports/CloudPresets/presets.idx", base);
#endif
    SDL_free(base);
    if (rootLength < 0 || (size_t)rootLength >= sizeof(root) ||
        directoryLength < 0 || directoryLength >= directoryBytes ||
        indexLength < 0 || indexLength >= indexBytes)
    {
        fprintf(stderr, "[MapEditor] Cloud preset path is too long.\n");
        return FALSE;
    }
    if (!fileMakeDirectory(root) || !fileMakeDirectory(directory))
    {
        fprintf(stderr, "[MapEditor] Could not create RTXExports/CloudPresets.\n");
        return FALSE;
    }
    return TRUE;
}

static bool32 mmeCloudPresetFilePath(const char *fileName, char *out, sdword outBytes)
{
    char directory[1024];
    char indexPath[1024];
    int n;
    if (fileName == NULL || out == NULL || outBytes <= 0) return FALSE;
    if (!mmeCloudPresetPaths(directory, sizeof(directory), indexPath, sizeof(indexPath)))
        return FALSE;
#ifdef _WIN32
    n = snprintf(out, (size_t)outBytes, "%s\\%s", directory, fileName);
#else
    n = snprintf(out, (size_t)outBytes, "%s/%s", directory, fileName);
#endif
    return n >= 0 && n < outBytes;
}

static bool32 mmeCloudPresetExists(const char *fileName)
{
    char path[1200];
    FILE *fp;
    if (!mmeCloudPresetFilePath(fileName, path, sizeof(path))) return FALSE;
    fp = fopen(path, "rb");
    if (fp == NULL) return FALSE;
    fclose(fp);
    return TRUE;
}

static bool32 mmeCloudPresetWriteIndex(void)
{
    char directory[1024];
    char indexPath[1024];
    FILE *fp;
    sdword i;
    if (!mmeCloudPresetPaths(directory, sizeof(directory), indexPath, sizeof(indexPath)))
        return FALSE;
    fp = fopen(indexPath, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "[MapEditor] Could not write cloud preset index: %s\n", indexPath);
        return FALSE;
    }
    fprintf(fp, "# RTX Cloud Preset Index v1\n");
    for (i = 0; i < mmeCloudPresetCount; ++i)
        fprintf(fp, "%s|%s\n", mmeCloudPresets[i].fileName, mmeCloudPresets[i].name);
    {
        sdword failed = FALSE;
        if (fflush(fp) != 0 || ferror(fp)) failed = TRUE;
        if (fclose(fp) != 0) failed = TRUE;
        if (failed)
        {
            fprintf(stderr, "[MapEditor] Cloud preset index write failed: %s\n", indexPath);
            return FALSE;
        }
    }
    return TRUE;
}

static void mmeCloudPresetRefresh(void)
{
    char directory[1024];
    char indexPath[1024];
    FILE *fp;
    char line[256];
    sdword oldSelected = mme.presetSelected;
    mmeCloudPresetCount = 0;
    if (!mmeCloudPresetPaths(directory, sizeof(directory), indexPath, sizeof(indexPath)))
    {
        mme.presetSelected = -1;
        return;
    }
    fp = fopen(indexPath, "rb");
    if (fp != NULL)
    {
        while (fgets(line, sizeof(line), fp) != NULL &&
               mmeCloudPresetCount < MME_MAX_CLOUD_PRESETS)
        {
            char *sep;
            char *end;
            MMECloudPreset *preset;
            if (line[0] == '#') continue;
            end = line + strlen(line);
            while (end > line && (end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
            sep = strchr(line, '|');
            if (sep == NULL) continue;
            *sep++ = 0;
            if (line[0] == 0 || sep[0] == 0) continue;
            if (!mmeCloudPresetExists(line)) continue;
            preset = &mmeCloudPresets[mmeCloudPresetCount++];
            snprintf(preset->fileName, sizeof(preset->fileName), "%.63s", line);
            snprintf(preset->name, sizeof(preset->name), "%.63s", sep);
        }
        fclose(fp);
    }
    if (mmeCloudPresetCount <= 0)
    {
        mme.presetSelected = -1;
        mme.presetScroll = 0;
    }
    else
    {
        mme.presetSelected = mmeClamp(oldSelected < 0 ? 0 : oldSelected,
                                      0, mmeCloudPresetCount - 1);
        mme.presetScroll = mmeClamp(mme.presetScroll, 0,
            mmeCloudPresetCount > MME_PRESET_VISIBLE_ROWS ?
                mmeCloudPresetCount - MME_PRESET_VISIBLE_ROWS : 0);
    }
}

static bool32 mmeCloudPresetWriteFile(const MMECloudPreset *preset,
                                      const ModernMapDustVolumeSnapshot *v)
{
    char path[1200];
    FILE *fp;
    if (preset == NULL || v == NULL ||
        !mmeCloudPresetFilePath(preset->fileName, path, sizeof(path))) return FALSE;
    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "[MapEditor] Could not write cloud preset: %s\n", path);
        return FALSE;
    }
    fprintf(fp, "# RTX DUST PRESET 1\n");
    fprintf(fp, "NAME %s\n", preset->name);
    /* Keep the same field order as RTXMAP 5 VOLUME DUST for durable round-trip
       compatibility. LOAD intentionally preserves the target volume position. */
    fprintf(fp,
        "VOLUME DUST %d %d %u %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.8f %.6f %.6f %d %d %d\n",
        v->enabled ? 1 : 0, (sdword)v->shape,
        (unsigned int)v->shapeSeed, v->shapeVariation,
        v->position[0], v->position[1], v->position[2],
        v->rotationDegrees[0], v->rotationDegrees[1], v->rotationDegrees[2],
        v->size[0], v->size[1], v->size[2],
        v->density, v->scattering, v->absorption, v->anisotropy, v->coverage,
        v->color[0], v->color[1], v->color[2], v->noiseScale, v->noiseDetail,
        v->wakeStrength, v->receiveMissionKey ? 1 : 0,
        v->receiveLocalLights ? 1 : 0, v->castVolumetricShadow ? 1 : 0);
    {
        sdword failed = FALSE;
        if (fflush(fp) != 0 || ferror(fp)) failed = TRUE;
        if (fclose(fp) != 0) failed = TRUE;
        if (failed)
        {
            fprintf(stderr, "[MapEditor] Cloud preset write failed: %s\n", path);
            return FALSE;
        }
    }
    return TRUE;
}

static bool32 mmeCloudPresetReadFile(const MMECloudPreset *preset,
                                     ModernMapDustVolumeSnapshot *out)
{
    char path[1200];
    char line[1024];
    FILE *fp;
    if (preset == NULL || out == NULL ||
        !mmeCloudPresetFilePath(preset->fileName, path, sizeof(path))) return FALSE;
    fp = fopen(path, "rb");
    if (fp == NULL) return FALSE;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if (strncmp(line, "VOLUME DUST ", 12) == 0)
        {
            char kind[32];
            sdword enabled, shape, key, local, shadow;
            ModernMapDustVolumeSnapshot v;
            sdword parsed;
            memset(&v, 0, sizeof(v));
            v.color[0] = v.color[1] = v.color[2] = 1.0f;
            v.shapeSeed = 1u;
            v.shapeVariation = 0.72f;
            v.wakeStrength = 1.0f;
            parsed = sscanf(line,
                "VOLUME %31s %d %d %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                kind, &enabled, &shape, &v.shapeSeed, &v.shapeVariation,
                &v.position[0], &v.position[1], &v.position[2],
                &v.rotationDegrees[0], &v.rotationDegrees[1], &v.rotationDegrees[2],
                &v.size[0], &v.size[1], &v.size[2],
                &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                &v.noiseScale, &v.noiseDetail, &v.wakeStrength,
                &key, &local, &shadow);
            if (parsed == 28 && strcasecmp(kind, "DUST") == 0)
            {
                v.enabled = enabled ? TRUE : FALSE;
                v.shape = (ModernMapDustShape)mmeClamp(shape, MME_DUST_SPHERE, MME_DUST_ELLIPSOID);
                v.receiveMissionKey = key ? TRUE : FALSE;
                v.receiveLocalLights = local ? TRUE : FALSE;
                v.castVolumetricShadow = shadow ? TRUE : FALSE;
                *out = v;
                fclose(fp);
                return TRUE;
            }
        }
    }
    fclose(fp);
    return FALSE;
}

static bool32 mmeCloudPresetSaveNew(void)
{
    MMEVolume *volume = mmeSelectedVolume();
    sdword slot;
    MMECloudPreset *preset;
    if (volume == NULL)
    {
        mmeStatus("SELECT A DUST VOLUME BEFORE SAVING A PRESET");
        return FALSE;
    }
    if (mmeCloudPresetCount >= MME_MAX_CLOUD_PRESETS)
    {
        mmeStatus("CLOUD PRESET LIMIT REACHED");
        return FALSE;
    }
    for (slot = 1; slot <= 999; ++slot)
    {
        char fileName[MME_PRESET_FILE_BYTES];
        snprintf(fileName, sizeof(fileName), "CloudPreset%03d.rtxdust", slot);
        if (!mmeCloudPresetExists(fileName)) break;
    }
    if (slot > 999)
    {
        mmeStatus("NO FREE CLOUD PRESET FILE SLOT");
        return FALSE;
    }
    preset = &mmeCloudPresets[mmeCloudPresetCount];
    snprintf(preset->fileName, sizeof(preset->fileName), "CloudPreset%03d.rtxdust", slot);
    snprintf(preset->name, sizeof(preset->name), "CLOUD PRESET %03d", slot);
    if (!mmeCloudPresetWriteFile(preset, &volume->data))
    {
        mmeStatus("CLOUD PRESET SAVE FAILED - CHECK LOG");
        return FALSE;
    }
    mmeCloudPresetCount++;
    mme.presetSelected = mmeCloudPresetCount - 1;
    mme.presetScroll = mmeCloudPresetCount > MME_PRESET_VISIBLE_ROWS ?
        mmeCloudPresetCount - MME_PRESET_VISIBLE_ROWS : 0;
    if (!mmeCloudPresetWriteIndex())
    {
        mmeStatus("PRESET SAVED BUT INDEX WRITE FAILED - CHECK LOG");
        return FALSE;
    }
    mmeStatus("CLOUD PRESET SAVED / DROPDOWN UPDATED");
    return TRUE;
}

static bool32 mmeCloudPresetOverwrite(void)
{
    MMEVolume *volume = mmeSelectedVolume();
    if (volume == NULL)
    {
        mmeStatus("SELECT A DUST VOLUME TO OVERWRITE PRESET");
        return FALSE;
    }
    if (mme.presetSelected < 0 || mme.presetSelected >= mmeCloudPresetCount)
    {
        mmeStatus("SELECT A CLOUD PRESET FIRST");
        return FALSE;
    }
    if (!mmeCloudPresetWriteFile(&mmeCloudPresets[mme.presetSelected], &volume->data))
    {
        mmeStatus("CLOUD PRESET OVERWRITE FAILED - CHECK LOG");
        return FALSE;
    }
    mmeStatus("CLOUD PRESET OVERWRITTEN FROM SELECTED VOLUME");
    return TRUE;
}

static bool32 mmeCloudPresetLoad(void)
{
    MMEVolume *volume = mmeSelectedVolume();
    ModernMapDustVolumeSnapshot loaded;
    real32 position[3];
    bool32 created = FALSE;
    if (mme.presetSelected < 0 || mme.presetSelected >= mmeCloudPresetCount)
    {
        mmeStatus("SELECT A CLOUD PRESET FIRST");
        return FALSE;
    }
    if (!mmeCloudPresetReadFile(&mmeCloudPresets[mme.presetSelected], &loaded))
    {
        mmeStatus("CLOUD PRESET LOAD FAILED - CHECK FILE/LOG");
        return FALSE;
    }
    if (volume == NULL)
    {
        vector spawn;
        sdword index;
        if (mme.freeCameraInitialized) spawn = mme.freeCameraLookat;
        else spawn = universe.mainCameraCommand.actualcamera.lookatpoint;
        index = mmeCreateDustVolume(&spawn);
        if (index < 0)
        {
            mmeStatus("RTX VOLUME LIMIT REACHED");
            return FALSE;
        }
        mme.tab = MME_TAB_VOLUMES;
        mmeSelectVolume(index);
        volume = mmeSelectedVolume();
        if (volume == NULL) return FALSE;
        created = TRUE;
    }
    position[0] = volume->data.position[0];
    position[1] = volume->data.position[1];
    position[2] = volume->data.position[2];
    volume->data = loaded;
    volume->data.position[0] = position[0];
    volume->data.position[1] = position[1];
    volume->data.position[2] = position[2];
    mmeSyncDustVolumesToRenderer();
    mmeStatus(created ?
        "CLOUD CREATED FROM PRESET AT CAMERA LOOK TARGET / LIVE UNSAVED" :
        "CLOUD PRESET LOADED / POSITION PRESERVED / LIVE UNSAVED");
    return TRUE;
}

static const char *mmeObjTypeToken(ObjType type)
{
    switch (type)
    {
        case OBJ_ShipType: return "SHIP";
        case OBJ_DerelictType: return "DERELICT";
        case OBJ_AsteroidType: return "ASTEROID";
        case OBJ_DustType: return "DUST";
        case OBJ_GasType: return "GAS";
        case OBJ_NebulaType: return "NEBULA";
        default: return "OBJECT";
    }
}

static bool32 mmeSaveMap(void)
{
    char path[768];
    FILE *fp;
    sdword i;
    sdword mission = spGetCurrentMission();
    if (!mmeExportPath(path, sizeof(path), mission))
    {
        mmeStatus("MAP EXPORT PATH FAILED");
        return FALSE;
    }
    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "[MapEditor] fopen failed for %s\n", path);
        mmeStatus("MAP EXPORT FAILED - CHECK LOG/PATH");
        return FALSE;
    }
    fprintf(fp, "RTXMAP 9\n");
    fprintf(fp, "MISSION %d\n", mission);
    fprintf(fp, "DUST_FADE %.6f %.6f\n",
            mmeDustFadeDistance, mmeDustFadeStrength);
    fprintf(fp, "# Non-destructive overlay. Original mission data/BIG archives remain untouched.\n");
    fprintf(fp, "# Volume fields drive the live D3D12 raymarched volumetric-dust renderer.\n");
    fprintf(fp, "# Manual RTX volumetric-dust authoring.\n");
    for (i = 0; i < mmeTrackedCount; ++i)
    {
        MMETrackedObject *t = &mmeTracked[i];
        if (t->created && t->deleted) continue;
        if (t->original && t->deleted)
        {
            fprintf(fp, "DELETE %s %d\n", mmeObjTypeToken(t->objtype), t->runtimeId);
            continue;
        }
        if (t->object == NULL) continue;
        if (t->created)
        {
            vector *p = mmeObjectPosition(t->object);
            if (t->objtype == OBJ_ShipType)
                fprintf(fp, "ADD SHIP %s %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                        ShipRaceToStr(t->race), ShipTypeToStr((ShipType)t->subtype),
                        p->x, p->y, p->z, t->rotationDegrees[0], t->rotationDegrees[1],
                        t->rotationDegrees[2], mmeObjectScale(t->object));
            else
                fprintf(fp, "ADD %s %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                        mmeObjTypeToken(t->objtype),
                        t->objtype == OBJ_DerelictType ? DerelictTypeToStr((DerelictType)t->subtype) :
                        (t->objtype == OBJ_AsteroidType ? AsteroidTypeToStr((AsteroidType)t->subtype) :
                        (t->objtype == OBJ_DustType ? DustCloudTypeToStr((DustCloudType)t->subtype) :
                        (t->objtype == OBJ_GasType ? GasCloudTypeToStr((GasCloudType)t->subtype) : "Unknown"))),
                        p->x, p->y, p->z, t->rotationDegrees[0], t->rotationDegrees[1],
                        t->rotationDegrees[2], mmeObjectScale(t->object));
        }
        else if (t->dirty)
        {
            vector *p = mmeObjectPosition(t->object);
            fprintf(fp, "TRANSFORM %s %d %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                    mmeObjTypeToken(t->objtype), t->runtimeId,
                    p->x, p->y, p->z, t->rotationDegrees[0], t->rotationDegrees[1],
                    t->rotationDegrees[2], mmeObjectScale(t->object));
        }
    }
    for (i = 0; i < mmeVolumeCount; ++i)
    {
        MMEVolume *v = &mmeVolumes[i];
        if (!v->inUse) continue;
        fprintf(fp,
                "VOLUME DUST %d %d %u %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.8f %.6f %.6f %d %d %d %d\n",
                v->data.enabled ? 1 : 0, (sdword)v->data.shape,
                (unsigned int)v->data.shapeSeed, v->data.shapeVariation,
                v->data.position[0], v->data.position[1], v->data.position[2],
                v->data.rotationDegrees[0], v->data.rotationDegrees[1], v->data.rotationDegrees[2],
                v->data.size[0], v->data.size[1], v->data.size[2],
                v->data.density, v->data.scattering, v->data.absorption,
                v->data.anisotropy, v->data.coverage,
                v->data.color[0], v->data.color[1], v->data.color[2], v->data.noiseScale,
                v->data.noiseDetail, v->data.wakeStrength,
                v->data.receiveMissionKey ? 1 : 0,
                v->data.receiveLocalLights ? 1 : 0,
                v->data.castVolumetricShadow ? 1 : 0,
                0);
    }
    {
        sdword writeFailed = FALSE;
        if (fflush(fp) != 0 || ferror(fp))
            writeFailed = TRUE;
        if (fclose(fp) != 0)
            writeFailed = TRUE;
        if (writeFailed)
        {
            mmeStatus("MAP EXPORT WRITE FAILED - CHECK LOG");
            return FALSE;
        }
    }
    fprintf(stderr, "[MapEditor] Exported %s\n", path);
    mmeStatus("MAP SAVED + EXPORTED TO RTXExports/Maps");
    return TRUE;
}

static MMETrackedObject *mmeFindRuntimeTrack(const char *typeToken, sdword runtimeId)
{
    sdword i;
    for (i = 0; i < mmeTrackedCount; ++i)
    {
        MMETrackedObject *t = &mmeTracked[i];
        if (t->runtimeId == runtimeId && t->original && !t->deleted &&
            strcasecmp(mmeObjTypeToken(t->objtype), typeToken) == 0)
            return t;
    }
    return NULL;
}

static void mmeApplyTrackTransform(MMETrackedObject *track, real32 x, real32 y, real32 z,
                                   real32 pitch, real32 yaw, real32 roll, real32 scale)
{
    vector p;
    if (track == NULL || track->object == NULL) return;
    p.x = x; p.y = y; p.z = z;
    mmeMoveObjectTo(track->object, &p);
    if (pitch != 0.0f) mmeRotateTrack(track, MME_AXIS_X, pitch);
    if (yaw != 0.0f) mmeRotateTrack(track, MME_AXIS_Y, yaw);
    if (roll != 0.0f) mmeRotateTrack(track, MME_AXIS_Z, roll);
    mmeSetObjectScale(track->object, scale);
    track->scale = scale;
    track->dirty = TRUE;
}

static bool32 mmeLoadMapOverlay(void)
{
    char path[768];
    char packagedPath[768];
    char line[1024];
    FILE *fp;
    bool32 loadedPackaged = FALSE;
    sdword mission = spGetCurrentMission();
    if (!mmeExportPath(path, sizeof(path), mission)) return FALSE;
    fp = fopen(path, "rb");
    if (fp == NULL && mmePackagedMapPath(packagedPath, sizeof(packagedPath), mission))
    {
        fp = fopen(packagedPath, "rb");
        if (fp != NULL)
        {
            loadedPackaged = TRUE;
            fprintf(stderr,
                    "[MapEditor] No writable RTXMAP override; loading packaged mission map: %s\n",
                    packagedPath);
        }
    }
    if (fp == NULL)
    {
        mmeStatus("NO RTXMAP EXPORT OR PACKAGED MAP FOR THIS MISSION");
        return FALSE;
    }
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char token[32];
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        if (sscanf(line, "%31s", token) != 1) continue;
        if (strcasecmp(token, "DUST_FADE") == 0)
        {
            real32 distance, strength;
            if (sscanf(line, "DUST_FADE %f %f", &distance, &strength) == 2)
            {
                mmeDustFadeDistance = mmeClampF(distance, 5000.0f, 250000.0f);
                mmeDustFadeStrength = mmeClampF(strength, 0.0f, 4.0f);
            }
        }
        else if (strcasecmp(token, "DELETE") == 0)
        {
            char type[32]; sdword id;
            if (sscanf(line, "DELETE %31s %d", type, &id) == 2)
            {
                MMETrackedObject *t = mmeFindRuntimeTrack(type, id);
                if (t != NULL)
                {
                    mmeSelectTrack((sdword)(t - mmeTracked));
                    mmeDeleteSelection();
                }
            }
        }
        else if (strcasecmp(token, "TRANSFORM") == 0)
        {
            char type[32]; sdword id; real32 x,y,z,pitch,yaw,roll,scale;
            if (sscanf(line, "TRANSFORM %31s %d %f %f %f %f %f %f %f",
                       type, &id, &x,&y,&z,&pitch,&yaw,&roll,&scale) == 9)
            {
                MMETrackedObject *t = mmeFindRuntimeTrack(type, id);
                mmeApplyTrackTransform(t, x,y,z,pitch,yaw,roll,scale);
            }
        }
        else if (strcasecmp(token, "ADD") == 0)
        {
            char kind[32]; char a[64]; char b[64];
            real32 x=0.0f,y=0.0f,z=0.0f,pitch=0.0f,yaw=0.0f,roll=0.0f,scale=1.0f;
            SpaceObj *object = NULL;
            sdword n = sscanf(line, "ADD %31s %63s %63s %f %f %f %f %f %f %f",
                              kind, a, b, &x,&y,&z,&pitch,&yaw,&roll,&scale);
            vector pos;
            pos.x = x; pos.y = y; pos.z = z;
            if (strcasecmp(kind, "SHIP") == 0 && n == 10)
            {
                ShipRace race = StrToShipRace(a);
                ShipType type = StrToShipType(b);
                if ((sdword)race >= 0 && race < NUM_RACES && (sdword)type >= 0 && (sdword)type < TOTAL_NUM_SHIPS)
                    object = (SpaceObj *)univAddShip(type, race, &pos, universe.curPlayerPtr, 0);
            }
            else
            {
                /* Non-ship ADD lines only have one type-name token; b is x in the
                   generic sscanf above, so parse them separately. */
                char typeName[64];
                if (sscanf(line, "ADD %31s %63s %f %f %f %f %f %f %f",
                           kind, typeName, &x,&y,&z,&pitch,&yaw,&roll,&scale) == 9)
                {
                    pos.x=x; pos.y=y; pos.z=z;
                    if (strcasecmp(kind, "DERELICT") == 0)
                    {
                        DerelictType t = StrToDerelictType(typeName);
                        if ((sdword)t >= 0 && (sdword)t < NUM_DERELICTTYPES)
                            object = (SpaceObj *)univAddDerelict(t, &pos);
                    }
                    else if (strcasecmp(kind, "ASTEROID") == 0)
                    {
                        AsteroidType t = StrToAsteroidType(typeName);
                        if ((sdword)t >= 0 && (sdword)t < NUM_ASTEROIDTYPES)
                            object = (SpaceObj *)univAddAsteroid(t, &pos);
                    }
                    else if (strcasecmp(kind, "DUST") == 0)
                    {
                        DustCloudType t = StrToDustCloudType(typeName);
                        if ((sdword)t >= 0 && (sdword)t < NUM_DUSTCLOUDTYPES)
                            object = (SpaceObj *)univAddDustCloud(t, &pos);
                    }
                    else if (strcasecmp(kind, "GAS") == 0)
                    {
                        GasCloudType t = StrToGasCloudType(typeName);
                        if ((sdword)t >= 0 && (sdword)t < NUM_GASCLOUDTYPES)
                            object = (SpaceObj *)univAddGasCloud(t, &pos);
                    }
                }
            }
            if (object != NULL)
            {
                sdword idx = mmeTrackObject(object, FALSE);
                MMETrackedObject *t = idx >= 0 ? &mmeTracked[idx] : NULL;
                if (t != NULL) mmeApplyTrackTransform(t, x,y,z,pitch,yaw,roll,scale);
            }
        }
        else if (strcasecmp(token, "VOLUME") == 0)
        {
            char kind[32]; sdword enabled, shape, key, local, shadow, converted = 0;
            ModernMapDustVolumeSnapshot v;
            memset(&v, 0, sizeof(v));
            {
                sdword parsed;
                /* RTXMAP 5 adds per-volume ship-wake strength. RTXMAP 4 adds
                   rigid X/Y/Z volume orientation; older formats remain readable. */
                v.color[0] = v.color[1] = v.color[2] = 1.0f;
                v.shapeSeed = 1u;
                v.shapeVariation = 0.72f;
                v.wakeStrength = 1.0f;
                v.rotationDegrees[0] = v.rotationDegrees[1] = v.rotationDegrees[2] = 0.0f;
                parsed = sscanf(line,
                    "VOLUME %31s %d %d %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d %d",
                    kind, &enabled, &shape, &v.shapeSeed, &v.shapeVariation,
                    &v.position[0], &v.position[1], &v.position[2],
                    &v.rotationDegrees[0], &v.rotationDegrees[1], &v.rotationDegrees[2],
                    &v.size[0], &v.size[1], &v.size[2],
                    &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                    &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                    &v.noiseScale, &v.noiseDetail, &v.wakeStrength, &key, &local, &shadow, &converted);
                if (parsed != 29)
                {
                    converted = 0;
                    parsed = sscanf(line,
                        "VOLUME %31s %d %d %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                        kind, &enabled, &shape, &v.shapeSeed, &v.shapeVariation,
                        &v.position[0], &v.position[1], &v.position[2],
                        &v.rotationDegrees[0], &v.rotationDegrees[1], &v.rotationDegrees[2],
                        &v.size[0], &v.size[1], &v.size[2],
                        &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                        &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                        &v.noiseScale, &v.noiseDetail, &v.wakeStrength, &key, &local, &shadow);
                }
                if (parsed != 29 && parsed != 28)
                {
                    /* RTXMAP 4: exact v5 layout minus wake strength. Parse it
                       explicitly before older formats so key/local/shadow can
                       never be shifted into the new float field. */
                    v.color[0] = v.color[1] = v.color[2] = 1.0f;
                    v.shapeSeed = 1u;
                    v.shapeVariation = 0.72f;
                    v.wakeStrength = 1.0f;
                    v.rotationDegrees[0] = v.rotationDegrees[1] = v.rotationDegrees[2] = 0.0f;
                    parsed = sscanf(line,
                        "VOLUME %31s %d %d %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                        kind, &enabled, &shape, &v.shapeSeed, &v.shapeVariation,
                        &v.position[0], &v.position[1], &v.position[2],
                        &v.rotationDegrees[0], &v.rotationDegrees[1], &v.rotationDegrees[2],
                        &v.size[0], &v.size[1], &v.size[2],
                        &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                        &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                        &v.noiseScale, &v.noiseDetail, &key, &local, &shadow);
                }
                if (parsed != 29 && parsed != 28 && parsed != 27)
                {
                    /* RTXMAP 3: deterministic shape randomization, no rigid
                       X/Y/Z volume orientation and no wake-strength field. */
                    v.rotationDegrees[0] = v.rotationDegrees[1] = v.rotationDegrees[2] = 0.0f;
                    v.color[0] = v.color[1] = v.color[2] = 1.0f;
                    v.shapeSeed = 1u;
                    v.shapeVariation = 0.72f;
                    v.wakeStrength = 1.0f;
                    parsed = sscanf(line,
                        "VOLUME %31s %d %d %u %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                        kind, &enabled, &shape, &v.shapeSeed, &v.shapeVariation,
                        &v.position[0], &v.position[1], &v.position[2],
                        &v.size[0], &v.size[1], &v.size[2],
                        &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                        &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                        &v.noiseScale, &v.noiseDetail, &key, &local, &shadow);
                }
                if (parsed != 29 && parsed != 28 && parsed != 27 && parsed != 24)
                {
                    /* RTXMAP 2: RGB material tint but no deterministic shape
                       seed/variation fields. */
                    v.rotationDegrees[0] = v.rotationDegrees[1] = v.rotationDegrees[2] = 0.0f;
                    v.color[0] = v.color[1] = v.color[2] = 1.0f;
                    v.shapeSeed = 1u;
                    v.shapeVariation = 0.72f;
                    v.wakeStrength = 1.0f;
                    parsed = sscanf(line,
                        "VOLUME %31s %d %d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                        kind, &enabled, &shape,
                        &v.position[0], &v.position[1], &v.position[2],
                        &v.size[0], &v.size[1], &v.size[2],
                        &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                        &v.coverage, &v.color[0], &v.color[1], &v.color[2],
                        &v.noiseScale, &v.noiseDetail, &key, &local, &shadow);
                }
                if (parsed != 29 && parsed != 28 && parsed != 27 && parsed != 24 && parsed != 22)
                {
                    v.color[0] = v.color[1] = v.color[2] = 1.0f;
                    v.shapeSeed = 1u;
                    v.shapeVariation = 0.72f;
                    v.wakeStrength = 1.0f;
                    parsed = sscanf(line,
                        "VOLUME %31s %d %d %f %f %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
                        kind, &enabled, &shape,
                        &v.position[0], &v.position[1], &v.position[2],
                        &v.size[0], &v.size[1], &v.size[2],
                        &v.density, &v.scattering, &v.absorption, &v.anisotropy,
                        &v.coverage, &v.noiseScale, &v.noiseDetail,
                        &key, &local, &shadow);
                }
                if ((parsed == 29 || parsed == 28 || parsed == 27 || parsed == 24 || parsed == 22 || parsed == 19) && strcasecmp(kind, "DUST") == 0)
                {
                if (mmeVolumeCount < MME_MAX_VOLUMES)
                {
                    MMEVolume *dst = &mmeVolumes[mmeVolumeCount];
                    memset(dst, 0, sizeof(*dst));
                    dst->inUse = TRUE;
                    v.enabled = enabled ? TRUE : FALSE;
                    v.shape = (ModernMapDustShape)mmeClamp(shape, MME_DUST_SPHERE, MME_DUST_ELLIPSOID);
                    v.receiveMissionKey = key ? TRUE : FALSE;
                    v.receiveLocalLights = local ? TRUE : FALSE;
                    v.castVolumetricShadow = shadow ? TRUE : FALSE;
                    dst->data = v;
                    snprintf(dst->label, sizeof(dst->label), "RTX DUST VOLUME %02d", mmeVolumeCount + 1);
                    ++mmeVolumeCount;
                    }
                }
            }
        }
    }
    fclose(fp);
    mmeSyncDustVolumesToRenderer();
    mmeStatus(loadedPackaged ?
              "PACKAGED MISSION RTXMAP LOADED / REAL VOLUMETRIC DUST ACTIVE" :
              "RTXMAP OVERLAY LOADED / REAL VOLUMETRIC DUST ACTIVE");
    return TRUE;
}

static void mmeRequestMissionLoad(sdword mission, bool32 loadOverlay)
{
    mission = mmeClamp(mission, 1, 16);
    mme.pendingMission = mission;
    mme.pendingLoadOverlay = loadOverlay;
    mmeStatus(loadOverlay ? "RELOADING MISSION PAUSED + APPLYING RTXMAP..." :
                            "LOADING SELECTED MISSION PAUSED...");
}

static void mmeCloseRegions(void)
{
    regionhandle left = mme.leftRegion;
    regionhandle right = mme.rightRegion;
    regionhandle toolbar = mme.toolbarRegion;
    regionhandle overlay = mme.overlayRegion;
    mme.leftRegion = mme.rightRegion = mme.toolbarRegion = mme.overlayRegion = NULL;
    if (left != NULL) regRegionDelete(left);
    if (right != NULL) regRegionDelete(right);
    if (toolbar != NULL) regRegionDelete(toolbar);
    if (overlay != NULL) regRegionDelete(overlay);
}

static void mmeOpenInternal(bool32 fromBoot)
{
    rectangle left;
    rectangle right;
    rectangle toolbar;
    if (ghMainRegion == NULL || !gameIsRunning) return;
    modernUICloseLightingEditor();
    modernUICloseShipDossier();
    mdCloseAllManagers();
    mmeFonts();
    left = mmeLeftRect();
    right = mmeRightRect();
    toolbar = mmeToolbarRect();
    mme.active = TRUE;
    mme.previousUniversePause = fromBoot ? FALSE : universePause;
    mme.previousKasPause = fromBoot ? FALSE : kasEditorPauseGet();
    universePause = TRUE;
    kasEditorPauseSet(TRUE);
    mme.selectedMission = spGetCurrentMission();
    if (mmeVolumeMission != mme.selectedMission)
    {
        memset(mmeVolumes, 0, sizeof(mmeVolumes));
        mmeVolumeCount = 0;
        mmeVolumeMission = mme.selectedMission;
    }
    mme.tab = MME_TAB_ASSETS;
    mme.assetScroll = mme.sceneScroll = mme.volumeScroll = 0;
    mme.selectedAsset = 0;
    mme.selectedTracked = -1;
    mme.selectedVolume = -1;
    mme.inspectorRow = 0;
    mme.presetSelected = -1;
    mme.presetScroll = 0;
    mme.presetDropdownOpen = FALSE;
    mme.transformMode = MME_TRANSFORM_MOVE;
    mme.axis = MME_AXIS_X;
    mme.gizmoDragKind = MME_GIZMO_NONE;
    mme.gizmoDragAxis = MME_AXIS_X;
    mme.viewportMode = MME_VIEW_LIT;
    mme.cameraMode = MME_CAMERA_PERSPECTIVE;
    mmeFreeCameraAdoptCurrentPivot();
    mmeBuildAssets();
    mmeSnapshotScene();
    mmeCloudPresetRefresh();
    mme.overlayRegion = regChildAlloc(ghMainRegion, (smemsize)&mme, 0, 0,
        MAIN_WindowWidth, MAIN_WindowHeight, 0, RPE_DrawEveryFrame);
    mme.leftRegion = regChildAlloc(ghMainRegion, (smemsize)&mme,
        left.x0, left.y0, left.x1-left.x0, left.y1-left.y0, 0,
        RPE_PressLeft | RPE_ReleaseLeft | RPE_WheelUp | RPE_WheelDown | RPE_DrawEveryFrame);
    mme.rightRegion = regChildAlloc(ghMainRegion, (smemsize)&mme,
        right.x0, right.y0, right.x1-right.x0, right.y1-right.y0, 0,
        RPE_PressLeft | RPE_HoldLeft | RPE_ReleaseLeft | RPE_WheelUp | RPE_WheelDown | RPE_DrawEveryFrame);
    mme.toolbarRegion = regChildAlloc(ghMainRegion, (smemsize)&mme,
        toolbar.x0, toolbar.y0, toolbar.x1-toolbar.x0, toolbar.y1-toolbar.y0, 0,
        RPE_PressLeft | RPE_ReleaseLeft | RPE_DrawEveryFrame);
    mmeBindRegions();
    if (mme.overlayRegion != NULL) regSiblingMoveToFront(mme.overlayRegion);
    if (mme.leftRegion != NULL) regSiblingMoveToFront(mme.leftRegion);
    if (mme.rightRegion != NULL) regSiblingMoveToFront(mme.rightRegion);
    if (mme.toolbarRegion != NULL) regSiblingMoveToFront(mme.toolbarRegion);
    mmeStatus("EDITOR FPS CAMERA / RMB-MOUSE LOOK / WHEEL FORWARD-BACK / NO CTRL");
    fprintf(stderr, "[MapEditor] Opened mission %02d: %d BIG-backed placeable assets, %d scene objects.\n",
            mme.selectedMission, mmeAssetCount, mmeTrackedCount);
}

static void mmeDrawListRow(const rectangle *row, const char *label, const char *sub,
                           bool32 selected, bool32 hover)
{
    if (selected) primRectSolid2((rectangle *)row, colRGBA(224,165,63,42));
    else if (hover) primRectSolid2((rectangle *)row, colRGBA(104,187,199,24));
    if (selected) primRectOutline2((rectangle *)row, 1, mmeSignal);
    mmeDrawTiny(row->x0 + uiScaleSize(6), row->y0 + uiScaleSize(3), selected ? mmePaper : mmeMuted, label);
    if (sub != NULL && sub[0] != 0)
        mmeDrawTiny(row->x0 + uiScaleSize(6), row->y0 + uiScaleSize(16), mmeLine, sub);
}

static void mmeDrawLeft(regionhandle region)
{
    rectangle panel = mmeLeftRect();
    rectangle tabs[MME_TAB_COUNT];
    rectangle list;
    rectangle footer;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    sdword rowH = uiScaleSize(31);
    sdword maxRows;
    sdword i;
    sdword renderableVolumes = 0;
    char header[128];
    char footerInfo[160];
    (void)region;
    primRectSolid2(&panel, mmeInk);
    primRectOutline2(&panel, 1, mmeLine);
    snprintf(header, sizeof(header), "RTX MAP EDITOR / MISSION %02d", spGetCurrentMission());
    mmeDrawText(panel.x0 + uiScaleSize(10), panel.y0 + uiScaleSize(8), mmePaper, header);
    mmeLayoutTabs(&panel, tabs, &list, &footer);
    mmeDrawButton(&tabs[0], "ASSETS", mme.tab == MME_TAB_ASSETS, mmePointIn(&tabs[0], mouseX, mouseY));
    mmeDrawButton(&tabs[1], "SCENE", mme.tab == MME_TAB_SCENE, mmePointIn(&tabs[1], mouseX, mouseY));
    mmeDrawButton(&tabs[2], "VOLUMES", mme.tab == MME_TAB_VOLUMES, mmePointIn(&tabs[2], mouseX, mouseY));
    maxRows = (list.y1 - list.y0) / rowH;
    if (maxRows > MME_VISIBLE_ROWS) maxRows = MME_VISIBLE_ROWS;
    for (i = 0; i < maxRows; ++i)
    {
        rectangle row;
        sdword index;
        row.x0 = list.x0; row.x1 = list.x1;
        row.y0 = list.y0 + i * rowH; row.y1 = row.y0 + rowH - uiScaleSize(2);
        if (mme.tab == MME_TAB_ASSETS)
        {
            index = mme.assetScroll + i;
            if (index >= mmeAssetCount) break;
            mmeDrawListRow(&row, mmeAssets[index].label, mmeAssets[index].archive,
                           index == mme.selectedAsset, mmePointIn(&row, mouseX, mouseY));
        }
        else if (mme.tab == MME_TAB_SCENE)
        {
            index = mme.sceneScroll + i;
            if (index >= mmeTrackedCount) break;
            if (mmeTracked[index].deleted)
                mmeDrawListRow(&row, mmeTracked[index].label, "DELETED (overlay)",
                               index == mme.selectedTracked, mmePointIn(&row, mouseX, mouseY));
            else
            {
                char sub[64];
                snprintf(sub, sizeof(sub), "%s / ID %d%s",
                         mmeTracked[index].original ? "MISSION" : "EDITOR",
                         mmeTracked[index].runtimeId,
                         mmeTracked[index].dirty ? " / MODIFIED" : "");
                mmeDrawListRow(&row, mmeTracked[index].label, sub,
                               index == mme.selectedTracked, mmePointIn(&row, mouseX, mouseY));
            }
        }
        else
        {
            index = mme.volumeScroll + i;
            if (index >= mmeVolumeCount) break;
            if (!mmeVolumes[index].inUse) continue;
            {
                char sub[80];
                snprintf(sub, sizeof(sub), "DENS %.2f / SCAT %.2f",
                         mmeVolumes[index].data.density, mmeVolumes[index].data.scattering);
                mmeDrawListRow(&row, mmeVolumes[index].label, sub,
                               index == mme.selectedVolume, mmePointIn(&row, mouseX, mouseY));
            }
        }
    }
    primRectSolid2(&footer, mmeSurface);
    primRectOutline2(&footer, 1, mmeLine);
    mmeDrawTiny(footer.x0 + uiScaleSize(6), footer.y0 + uiScaleSize(5), mmePaper,
                mme.tab == MME_TAB_ASSETS ? "P = PLACE SELECTED BIG/RTX ASSET" :
                (mme.tab == MME_TAB_SCENE ? "CLICK WORLD OR LIST / P = MOVE TO CURSOR" :
                                            "RTX VOLUMES / PRESETS + COPY/PASTE ON RIGHT"));
    if (mme.tab == MME_TAB_VOLUMES)
    {
        for (i = 0; i < mmeVolumeCount && renderableVolumes < MME_RENDERER_DUST_VOLUME_LIMIT; ++i)
            if (mmeVolumes[i].inUse && mmeVolumes[i].data.enabled &&
                mmeVolumes[i].data.density > 0.00001f) ++renderableVolumes;
        snprintf(footerInfo, sizeof(footerInfo),
                 "SCROLL FOR MORE / 22 = LIST ROWS ONLY / RTX UPLOAD %d/%d",
                 renderableVolumes, MME_RENDERER_DUST_VOLUME_LIMIT);
        mmeDrawTiny(footer.x0 + uiScaleSize(6), footer.y0 + uiScaleSize(20), mmeMuted, footerInfo);
    }
    else
        mmeDrawTiny(footer.x0 + uiScaleSize(6), footer.y0 + uiScaleSize(20), mmeMuted,
                    "DRAG X/Y/Z ARROW ENDS = MOVE  |  DRAG BLUE HANDLES = ROTATE");
    mmeDrawTiny(footer.x0 + uiScaleSize(6), footer.y0 + uiScaleSize(35), mmeMuted,
                "DEL DELETE  F FOCUS  G RANDOMIZE  CTRL+C/V CLOUD  CTRL+S MAP  L RELOAD  |  SHIFT+F11 CLOSE");
}

static sdword mmeInspectorCount(void)
{
    if (mmeSelectedVolume() != NULL) return MME_INSPECTOR_ROWS;
    if (mmeSelectedTrack() != NULL) return 14;
    if (mme.tab == MME_TAB_ASSETS && mme.selectedAsset >= 0 && mme.selectedAsset < mmeAssetCount) return 7;
    return 1;
}

static void mmeInspectorValues(char labels[MME_INSPECTOR_ROWS][32],
                               char values[MME_INSPECTOR_ROWS][64])
{
    MMETrackedObject *track = mmeSelectedTrack();
    MMEVolume *volume = mmeSelectedVolume();
    sdword i;
    for (i=0;i<MME_INSPECTOR_ROWS;i++) { labels[i][0]=0; values[i][0]=0; }
    if (volume != NULL)
    {
        static const char *names[MME_INSPECTOR_ROWS] = {
            "ENABLED","SHAPE","POSITION X","POSITION Y","POSITION Z",
            "ROTATE X","ROTATE Y","ROTATE Z","SIZE X","SIZE Y","SIZE Z",
            "DENSITY","SCATTERING","ABSORPTION","ANISOTROPY","COVERAGE",
            "COLOR R","COLOR G","COLOR B","NOISE SCALE","NOISE DETAIL",
            "WAKE STRENGTH","MISSION KEY LIGHT","LOCAL LIGHTS","VOLUME SHADOW",
            "SHAPE SEED (G RANDOM)","SHAPE VARIATION",
            "DISTANCE FADE START","DISTANCE FADE STRENGTH"
        };
        static const char *shapeNames[3] = {"SPHERE","BOX","ELLIPSOID"};
        for (i=0;i<MME_INSPECTOR_ROWS;i++) snprintf(labels[i],32,"%s",names[i]);
        snprintf(values[0],64,"%s",volume->data.enabled ? "ON" : "OFF");
        snprintf(values[1],64,"%s",shapeNames[mmeClamp((sdword)volume->data.shape,0,2)]);
        snprintf(values[2],64,"%.1f",volume->data.position[0]);
        snprintf(values[3],64,"%.1f",volume->data.position[1]);
        snprintf(values[4],64,"%.1f",volume->data.position[2]);
        snprintf(values[5],64,"%+.1f DEG",volume->data.rotationDegrees[0]);
        snprintf(values[6],64,"%+.1f DEG",volume->data.rotationDegrees[1]);
        snprintf(values[7],64,"%+.1f DEG",volume->data.rotationDegrees[2]);
        snprintf(values[8],64,"%.1f",volume->data.size[0]);
        snprintf(values[9],64,"%.1f",volume->data.size[1]);
        snprintf(values[10],64,"%.1f",volume->data.size[2]);
        snprintf(values[11],64,"%.3f",volume->data.density);
        snprintf(values[12],64,"%.3f",volume->data.scattering);
        snprintf(values[13],64,"%.3f",volume->data.absorption);
        snprintf(values[14],64,"%.3f",volume->data.anisotropy);
        snprintf(values[15],64,"%.3f",volume->data.coverage);
        snprintf(values[16],64,"%.3f",volume->data.color[0]);
        snprintf(values[17],64,"%.3f",volume->data.color[1]);
        snprintf(values[18],64,"%.3f",volume->data.color[2]);
        snprintf(values[19],64,"%.6f",volume->data.noiseScale);
        snprintf(values[20],64,"%.3f",volume->data.noiseDetail);
        snprintf(values[21],64,"%.2fx",volume->data.wakeStrength);
        snprintf(values[22],64,"%s",volume->data.receiveMissionKey ? "ON" : "OFF");
        snprintf(values[23],64,"%s",volume->data.receiveLocalLights ? "ON" : "OFF");
        snprintf(values[24],64,"%s",volume->data.castVolumetricShadow ? "ON" : "OFF");
        snprintf(values[25],64,"%u",(unsigned int)volume->data.shapeSeed);
        snprintf(values[26],64,"%.3f",volume->data.shapeVariation);
        snprintf(values[27],64,"%.0f UNITS",mmeDustFadeDistance);
        snprintf(values[28],64,"%.2fx",mmeDustFadeStrength);
        return;
    }
    if (track != NULL && track->object != NULL)
    {
        vector *p = mmeObjectPosition(track->object);
        static const char *names[14] = {
            "POSITION X","POSITION Y","POSITION Z","ROTATE X","ROTATE Y","ROTATE Z",
            "INSTANCE SCALE","RUNTIME ID","SOURCE","OBJECT TYPE","GIZMO",
            "LAST/ACTIVE AXIS","VELOCITY","NOTES"
        };
        for (i=0;i<14;i++) snprintf(labels[i],32,"%s",names[i]);
        snprintf(values[0],64,"%.1f",p ? p->x : 0.0f);
        snprintf(values[1],64,"%.1f",p ? p->y : 0.0f);
        snprintf(values[2],64,"%.1f",p ? p->z : 0.0f);
        snprintf(values[3],64,"%+.1f DEG",track->rotationDegrees[0]);
        snprintf(values[4],64,"%+.1f DEG",track->rotationDegrees[1]);
        snprintf(values[5],64,"%+.1f DEG",track->rotationDegrees[2]);
        snprintf(values[6],64,"%.2fx",mmeObjectScale(track->object));
        snprintf(values[7],64,"%d",track->runtimeId);
        snprintf(values[8],64,"%s",track->original ? "ORIGINAL MISSION" : "EDITOR ADD");
        snprintf(values[9],64,"%s",mmeObjTypeToken(track->objtype));
        snprintf(values[10],64,"DIRECT MOUSE DRAG");
        snprintf(values[11],64,"%s",mmeAxisName());
        snprintf(values[12],64,"FROZEN WHILE PAUSED");
        snprintf(values[13],64,"%s",track->dirty ? "UNSAVED CHANGE" : "BASELINE");
        return;
    }
    if (mme.tab == MME_TAB_ASSETS && mme.selectedAsset >= 0 && mme.selectedAsset < mmeAssetCount)
    {
        MMEAsset *asset = &mmeAssets[mme.selectedAsset];
        static const char *kindNames[] = {"SHIP","DERELICT","ASTEROID","LEGACY DUST","GAS","RTX VOLUME"};
        snprintf(labels[0],32,"BIG/RTX ASSET");
        snprintf(values[0],64,"%s",asset->label);
        snprintf(labels[1],32,"ARCHIVE");
        snprintf(values[1],64,"%.63s",asset->archive);
        snprintf(labels[2],32,"ARCHIVE PATH");
        snprintf(values[2],64,"%.63s",asset->path);
        snprintf(labels[3],32,"KIND");
        snprintf(values[3],64,"%s",kindNames[mmeClamp((sdword)asset->kind,0,5)]);
        snprintf(labels[4],32,"RACE");
        snprintf(values[4],64,"%s",asset->kind==MME_ASSET_SHIP ? ShipRaceToStr(asset->race) : "N/A");
        snprintf(labels[5],32,"SUBTYPE");
        snprintf(values[5],64,"%d",asset->subtype);
        snprintf(labels[6],32,"PLACEMENT");
        snprintf(values[6],64,"P / CURSOR DEPTH");
        return;
    }
    snprintf(labels[0],32,"SELECTION");
    snprintf(values[0],64,"NONE");
}


static bool32 mmeCloudPresetUIVisible(void)
{
    return mme.tab == MME_TAB_VOLUMES;
}

static void mmeRightInspectorRect(const rectangle *panel, rectangle *inner)
{
    mmeRectInset(inner, panel, uiScaleSize(10));
    inner->y0 += uiScaleSize(34);
    inner->y1 -= uiScaleSize(mmeCloudPresetUIVisible() ? 152 : 74);
}

static void mmeCloudPresetLayout(const rectangle *panel, rectangle *dropBox,
                                 rectangle buttons[5], rectangle *dropList,
                                 sdword *dropRowH, sdword *dropRows)
{
    sdword pad = uiScaleSize(10);
    sdword gap = uiScaleSize(4);
    sdword rowH = uiScaleSize(22);
    sdword top = panel->y1 - uiScaleSize(138);
    sdword w = panel->x1 - panel->x0 - pad * 2;
    sdword third = (w - gap * 2) / 3;
    sdword half = (w - gap) / 2;
    sdword rows = mmeCloudPresetCount < MME_PRESET_VISIBLE_ROWS ?
        mmeCloudPresetCount : MME_PRESET_VISIBLE_ROWS;
    dropBox->x0 = panel->x0 + pad;
    dropBox->x1 = panel->x1 - pad;
    dropBox->y0 = top + uiScaleSize(16);
    dropBox->y1 = dropBox->y0 + rowH;
    buttons[0].x0 = dropBox->x0;
    buttons[0].x1 = buttons[0].x0 + third;
    buttons[0].y0 = dropBox->y1 + gap;
    buttons[0].y1 = buttons[0].y0 + rowH;
    buttons[1].x0 = buttons[0].x1 + gap;
    buttons[1].x1 = buttons[1].x0 + third;
    buttons[1].y0 = buttons[0].y0;
    buttons[1].y1 = buttons[0].y1;
    buttons[2].x0 = buttons[1].x1 + gap;
    buttons[2].x1 = dropBox->x1;
    buttons[2].y0 = buttons[0].y0;
    buttons[2].y1 = buttons[0].y1;
    buttons[3].x0 = dropBox->x0;
    buttons[3].x1 = buttons[3].x0 + half;
    buttons[3].y0 = buttons[0].y1 + gap;
    buttons[3].y1 = buttons[3].y0 + rowH;
    buttons[4].x0 = buttons[3].x1 + gap;
    buttons[4].x1 = dropBox->x1;
    buttons[4].y0 = buttons[3].y0;
    buttons[4].y1 = buttons[3].y1;
    dropList->x0 = dropBox->x0;
    dropList->x1 = dropBox->x1;
    dropList->y1 = dropBox->y0 - uiScaleSize(2);
    dropList->y0 = dropList->y1 - rows * rowH;
    if (dropRowH != NULL) *dropRowH = rowH;
    if (dropRows != NULL) *dropRows = rows;
}

static void mmeDrawCloudPresetUI(const rectangle *panel)
{
    rectangle dropBox;
    rectangle buttons[5];
    rectangle dropList;
    sdword rowH;
    sdword rows;
    sdword x = mouseCursorX();
    sdword y = mouseCursorY();
    char label[96];
    sdword i;
    if (!mmeCloudPresetUIVisible()) return;
    mmeCloudPresetLayout(panel, &dropBox, buttons, &dropList, &rowH, &rows);
    mmeDrawTiny(dropBox.x0, dropBox.y0 - uiScaleSize(13), mmeMuted,
                "CLOUD PRESET LIBRARY");
    primRectSolid2(&dropBox, mmeRaised);
    primRectOutline2(&dropBox, 1,
        mme.presetDropdownOpen ? mmeSignal : (mmePointIn(&dropBox,x,y) ? mmeCool : mmeLine));
    if (mme.presetSelected >= 0 && mme.presetSelected < mmeCloudPresetCount)
        snprintf(label, sizeof(label), "%s  v", mmeCloudPresets[mme.presetSelected].name);
    else
        snprintf(label, sizeof(label), "<NO PRESETS>  v");
    mmeDrawTiny(dropBox.x0 + uiScaleSize(6), dropBox.y0 + uiScaleSize(5), mmePaper, label);
    mmeDrawButton(&buttons[0], "LOAD", FALSE, mmePointIn(&buttons[0],x,y));
    mmeDrawButton(&buttons[1], "SAVE NEW", FALSE, mmePointIn(&buttons[1],x,y));
    mmeDrawButton(&buttons[2], "OVERWRITE", FALSE, mmePointIn(&buttons[2],x,y));
    mmeDrawButton(&buttons[3], "COPY", mmeVolumeClipboardValid, mmePointIn(&buttons[3],x,y));
    mmeDrawButton(&buttons[4], "PASTE", mmeVolumeClipboardValid, mmePointIn(&buttons[4],x,y));
    mmeDrawTiny(dropBox.x0, buttons[3].y1 + uiScaleSize(5), mmeMuted,
                "Ctrl+C / Ctrl+V also copy/paste full dust volumes");
    if (mme.presetDropdownOpen && rows > 0)
    {
        primRectSolid2(&dropList, mmeInk);
        primRectOutline2(&dropList, 1, mmeSignal);
        for (i = 0; i < rows; ++i)
        {
            sdword presetIndex = mme.presetScroll + i;
            rectangle row;
            if (presetIndex >= mmeCloudPresetCount) break;
            row.x0 = dropList.x0;
            row.x1 = dropList.x1;
            row.y0 = dropList.y0 + i * rowH;
            row.y1 = row.y0 + rowH;
            if (presetIndex == mme.presetSelected)
                primRectSolid2(&row, colRGBA(224,165,63,48));
            else if (mmePointIn(&row,x,y))
                primRectSolid2(&row, colRGBA(104,187,199,28));
            mmeDrawTiny(row.x0 + uiScaleSize(6), row.y0 + uiScaleSize(5),
                        presetIndex == mme.presetSelected ? mmePaper : mmeMuted,
                        mmeCloudPresets[presetIndex].name);
        }
    }
}

static void mmeDrawRight(regionhandle region)
{
    rectangle panel = mmeRightRect();
    rectangle inner;
    char labels[MME_INSPECTOR_ROWS][32];
    char values[MME_INSPECTOR_ROWS][64];
    sdword rowH;
    sdword i;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)region;
    primRectSolid2(&panel, mmeInk);
    primRectOutline2(&panel, 1, mmeLine);
    mmeDrawText(panel.x0 + uiScaleSize(10), panel.y0 + uiScaleSize(8), mmePaper, "INSPECTOR");
    mmeRightInspectorRect(&panel, &inner);
    rowH = (inner.y1 - inner.y0) / MME_INSPECTOR_ROWS;
    mmeInspectorValues(labels, values);
    for (i = 0; i < MME_INSPECTOR_ROWS; ++i)
    {
        rectangle row;
        row.x0=inner.x0; row.x1=inner.x1;
        row.y0=inner.y0+i*rowH; row.y1=row.y0+rowH-uiScaleSize(2);
        if (i == mme.inspectorRow) primRectSolid2(&row, colRGBA(224,165,63,32));
        else if (mmePointIn(&row,mouseX,mouseY)) primRectSolid2(&row,colRGBA(104,187,199,20));
        mmeDrawTiny(row.x0+uiScaleSize(5),row.y0+uiScaleSize(3), i==mme.inspectorRow?mmeSignal:mmeMuted, labels[i]);
        if (values[i][0])
        {
            sdword w = fontWidth(values[i]);
            mmeDrawTiny(row.x1-uiScaleSize(5)-w,row.y0+uiScaleSize(3),mmePaper,values[i]);
        }
        if (mmeSelectedVolume()!=NULL && (i==21 || i==27 || i==28))
        {
            rectangle slider;
            real32 t = i==21
                ? mmeClampF(mmeSelectedVolume()->data.wakeStrength * 0.5f, 0.0f, 1.0f)
                : (i==27
                    ? mmeClampF((mmeDustFadeDistance-5000.0f)/245000.0f,0.0f,1.0f)
                    : mmeClampF(mmeDustFadeStrength*0.25f,0.0f,1.0f));
            sdword knobX;
            slider.x0 = row.x0 + uiScaleSize(112);
            slider.x1 = row.x1 - uiScaleSize(48);
            slider.y0 = row.y1 - uiScaleSize(6);
            slider.y1 = slider.y0 + uiScaleSize(2);
            primRectSolid2(&slider, mmeLine);
            knobX = slider.x0 + (sdword)((real32)(slider.x1-slider.x0)*t);
            primCircleSolid2(knobX, slider.y0+uiScaleSize(1), uiScaleSize(4), 12, mmeSignal);
        }
    }
    {
        rectangle info;
        info.x0=panel.x0+uiScaleSize(10); info.x1=panel.x1-uiScaleSize(10);
        info.y0=panel.y1-uiScaleSize(64); info.y1=panel.y1-uiScaleSize(10);
        primRectSolid2(&info,mmeSurface);
        primRectOutline2(&info,1,mmeLine);
        if (mmeCloudPresetUIVisible())
        {
            /* The volume footer is owned by the preset/copy-paste controls below. */
        }
        else if (mmeSelectedTrack()!=NULL)
        {
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(4),mmePaper,
                "DIRECT TRANSFORM GIZMO");
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(20),mmeMuted,
                "Drag colored arrowheads to move; drag blue knobs to rotate that axis.");
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(36),mmeMuted,
                "RMB/mouse aims; viewport wheel flies forward/back through the screen-center ray.");
        }
        else
        {
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(4),mmePaper,
                "BIG ARCHIVE ASSET BROWSER");
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(20),mmeMuted,
                "Read-only Update.big/Homeworld.big TOC; original archives are never modified.");
            mmeDrawTiny(info.x0+uiScaleSize(5),info.y0+uiScaleSize(36),mmeMuted,
                "RMB/mouse aims; viewport wheel flies forward/back; P places the selected asset.");
        }
    }
    mmeDrawCloudPresetUI(&panel);
}

static void mmeDrawToolbar(regionhandle region)
{
    rectangle bar = mmeToolbarRect();
    rectangle b[10];
    char missionText[32];
    char viewText[48];
    char cameraText[48];
    sdword x = mouseCursorX();
    sdword y = mouseCursorY();
    (void)region;
    primRectSolid2(&bar, mmeInk);
    primRectOutline2(&bar,1,mmeLine);
    mmeLayoutToolbar(&bar,b);
    snprintf(missionText,sizeof(missionText),"MISSION %02d",mme.selectedMission);
    snprintf(viewText,sizeof(viewText),"VIEW %s",mmeViewportName());
    snprintf(cameraText,sizeof(cameraText),"CAM %s",mmeCameraName());
    mmeDrawButton(&b[0],missionText,FALSE,mmePointIn(&b[0],x,y));
    mmeDrawButton(&b[1],"LOAD PAUSED",TRUE,mmePointIn(&b[1],x,y));
    mmeDrawButton(&b[2],universePause?"SIM PAUSED":"SIM RUNNING",universePause,mmePointIn(&b[2],x,y));
    mmeDrawButton(&b[3],kasEditorPauseGet()?"KAS PAUSED":"KAS RUNNING",kasEditorPauseGet(),mmePointIn(&b[3],x,y));
    mmeDrawButton(&b[4],viewText,mme.viewportMode!=MME_VIEW_LIT,mmePointIn(&b[4],x,y));
    mmeDrawButton(&b[5],cameraText,mme.cameraMode!=MME_CAMERA_PERSPECTIVE,mmePointIn(&b[5],x,y));
    mmeDrawButton(&b[6],"SAVE MAP",TRUE,mmePointIn(&b[6],x,y));
    mmeDrawButton(&b[7],"LOAD MAP",FALSE,mmePointIn(&b[7],x,y));
    mmeDrawButton(&b[8],"LIGHTING",FALSE,mmePointIn(&b[8],x,y));
    mmeDrawButton(&b[9],"DONE",FALSE,mmePointIn(&b[9],x,y));
    if (mme.status[0])
    {
        sdword statusY=bar.y1+uiScaleSize(2);
        mmeDrawTiny(bar.x0+uiScaleSize(6),statusY,mmeSignal,mme.status);
    }
}

static void mmeDrawVolumeEllipseRing(const MMEVolume *volume, sdword plane,
                                     real32 rx, real32 ry, color c)
{
    const sdword segments = 32;
    sdword i;
    sdword lastX = 0, lastY = 0;
    bool32 lastVisible = FALSE;
    for (i = 0; i <= segments; ++i)
    {
        real32 a = ((real32)i / (real32)segments) * MME_PI * 2.0f;
        real32 localX = 0.0f, localY = 0.0f, localZ = 0.0f;
        real32 p[3];
        sdword sx = 0, sy = 0;
        bool32 visible;
        if (plane == 0) { localX = (real32)cos(a) * rx; localY = (real32)sin(a) * ry; }
        else if (plane == 1) { localX = (real32)cos(a) * rx; localZ = (real32)sin(a) * ry; }
        else { localY = (real32)cos(a) * rx; localZ = (real32)sin(a) * ry; }
        mmeVolumeLocalToWorld(volume, localX, localY, localZ, p);
        visible = mmeProjectPoint(p, &sx, &sy);
        if (visible && lastVisible) primLine2(lastX, lastY, sx, sy, c);
        lastX = sx; lastY = sy; lastVisible = visible;
    }
}

static void mmeDrawVolumeWire(const MMEVolume *volume, bool32 selected)
{
    sdword cx,cy;
    color c;
    real32 hx,hy,hz;
    if (volume == NULL || !volume->inUse) return;
    c = selected ? mmeSignal : (volume->data.enabled ? mmeVolumeColor : mmeMuted);
    hx=volume->data.size[0]*0.5f;
    hy=volume->data.size[1]*0.5f;
    hz=volume->data.size[2]*0.5f;
    if (volume->data.shape == MME_DUST_BOX)
    {
        real32 corners[8][3];
        sdword sx[8],sy[8];
        bool32 visible[8];
        static const sdword edges[12][2] = {
            {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}
        };
        sdword i,e;
        for(i=0;i<8;i++)
        {
            mmeVolumeLocalToWorld(volume,
                (i&1)?hx:-hx, (i&2)?hy:-hy, (i&4)?hz:-hz, corners[i]);
            visible[i]=mmeProjectPoint(corners[i],&sx[i],&sy[i]);
        }
        for(e=0;e<12;e++)
        {
            sdword a=edges[e][0],b=edges[e][1];
            if(visible[a]&&visible[b]) primLine2(sx[a],sy[a],sx[b],sy[b],c);
        }
    }
    else
    {
        /* Three orthogonal great-circle rings make Sphere/Ellipsoid volume shape
           and scale legible from any viewport without needing renderer support. */
        real32 sxr = hx, syr = hy, szr = hz;
        if (volume->data.shape == MME_DUST_SPHERE)
        {
            real32 r = (hx + hy + hz) / 3.0f;
            sxr = syr = szr = r;
        }
        mmeDrawVolumeEllipseRing(volume, 0, sxr, syr, c);
        mmeDrawVolumeEllipseRing(volume, 1, sxr, szr, c);
        mmeDrawVolumeEllipseRing(volume, 2, syr, szr, c);
    }
    if(mmeProjectPoint(volume->data.position,&cx,&cy))
    {
        char tag[96];
        primLine2(cx-uiScaleSize(7),cy,cx+uiScaleSize(7),cy,c);
        primLine2(cx,cy-uiScaleSize(7),cx,cy+uiScaleSize(7),c);
        if (selected)
        {
            snprintf(tag,sizeof(tag),"DENS %.2f  SCAT %.2f  G %.2f",
                     volume->data.density,volume->data.scattering,volume->data.anisotropy);
            mmeDrawTiny(cx+uiScaleSize(10),cy-uiScaleSize(6),c,tag);
        }
    }
}

static color mmeGizmoAxisColor(sdword axis)
{
    if (axis == MME_AXIS_X) return colRGB(230, 69, 58);
    if (axis == MME_AXIS_Y) return colRGB(236, 205, 58);
    return colRGB(64, 205, 111);
}

static void mmeDrawTransformGizmo(void)
{
    MMEGizmoProjection gizmo;
    const color rotationColor = colRGB(62, 151, 255);
    sdword axis;
    if (!mmeBuildGizmoProjection(&gizmo)) return;

    /* A small neutral hub separates picking the object from manipulating it. */
    primCircleSolid2(gizmo.centerX, gizmo.centerY, uiScaleSize(4), 16,
                     colRGBA(235,235,235,220));
    for (axis = 0; axis < 3; ++axis)
    {
        color axisColor;
        real32 dx, dy, length, ux, uy, px, py;
        sdword tipX, tipY;
        triangle arrow;
        sdword ringRadius;
        triangle rotateArrow;
        if (!gizmo.axisVisible[axis]) continue;
        axisColor = mmeGizmoAxisColor(axis);
        if (mme.gizmoDragKind != MME_GIZMO_NONE && mme.gizmoDragAxis == axis)
            axisColor = mmePaper;
        tipX = gizmo.moveX[axis];
        tipY = gizmo.moveY[axis];
        primLineThick2(gizmo.centerX, gizmo.centerY, tipX, tipY,
                       uiScaleSize(2), axisColor);
        dx = (real32)(tipX - gizmo.centerX);
        dy = (real32)(tipY - gizmo.centerY);
        length = (real32)sqrt(dx*dx + dy*dy);
        if (length > 1.0f)
        {
            real32 head = (real32)uiScaleSize(11);
            real32 half = (real32)uiScaleSize(5);
            ux = dx / length; uy = dy / length;
            px = -uy; py = ux;
            arrow.x0 = tipX; arrow.y0 = tipY;
            arrow.x1 = (sdword)((real32)tipX - ux*head + px*half);
            arrow.y1 = (sdword)((real32)tipY - uy*head + py*half);
            arrow.x2 = (sdword)((real32)tipX - ux*head - px*half);
            arrow.y2 = (sdword)((real32)tipY - uy*head - py*half);
            primTriSolid2(&arrow, axisColor);
        }

        /* User-requested blue circular rotation knob at the end of each axis.
           It is intentionally beyond the translation arrowhead so click intent
           is unambiguous. */
        ringRadius = uiScaleSize(7);
        primCircleBorder(gizmo.rotateX[axis], gizmo.rotateY[axis],
                         max(1, ringRadius-2), ringRadius, 20,
                         (mme.gizmoDragKind == MME_GIZMO_ROTATE && mme.gizmoDragAxis == axis)
                            ? mmePaper : rotationColor);
        rotateArrow.x0 = gizmo.rotateX[axis] + ringRadius;
        rotateArrow.y0 = gizmo.rotateY[axis] - uiScaleSize(1);
        rotateArrow.x1 = gizmo.rotateX[axis] + uiScaleSize(3);
        rotateArrow.y1 = gizmo.rotateY[axis] - ringRadius - uiScaleSize(2);
        rotateArrow.x2 = gizmo.rotateX[axis] + uiScaleSize(1);
        rotateArrow.y2 = gizmo.rotateY[axis] - uiScaleSize(2);
        primTriSolid2(&rotateArrow, rotationColor);

        if (axis == MME_AXIS_X) mmeDrawTiny(tipX+uiScaleSize(5), tipY, axisColor, "X");
        else if (axis == MME_AXIS_Y) mmeDrawTiny(tipX+uiScaleSize(5), tipY, axisColor, "Y");
        else mmeDrawTiny(tipX+uiScaleSize(5), tipY, axisColor, "Z");
    }
}

static void mmeDrawObjectMarker(const MMETrackedObject *track, bool32 selected, bool32 bounds)
{
    vector *p;
    sdword x,y;
    color c=selected?mmeSignal:mmeCool;
    if(track==NULL||track->object==NULL||track->deleted)return;
    p=mmeObjectPosition(track->object);
    if(p==NULL||!mmeProjectPoint((const real32 *)p,&x,&y))return;
    if(bounds)
    {
        sdword rad=uiScaleSize(8);
        if(((SpaceObjRotImp *)track->object)->collInfo.selCircleRadius>0.0f)
        {
            rad=ABS(primGLToScreenScaleX(((SpaceObjRotImp *)track->object)->collInfo.selCircleRadius));
            rad=mmeClamp(rad,uiScaleSize(4),uiScaleSize(90));
        }
        primCircleBorder(x,y,rad>1?rad-1:1,rad,32,c);
    }
    if(selected)
    {
        primLine2(x-uiScaleSize(10),y,x+uiScaleSize(10),y,c);
        primLine2(x,y-uiScaleSize(10),x,y+uiScaleSize(10),c);
        mmeDrawTiny(x+uiScaleSize(12),y-uiScaleSize(6),c,track->label);
    }
}

static void mmeDrawOverlay(regionhandle region)
{
    sdword i;
    (void)region;
    if(!mme.active)return;
    if(mme.viewportMode==MME_VIEW_BOUNDS)
    {
        for(i=0;i<mmeTrackedCount;i++)
            mmeDrawObjectMarker(&mmeTracked[i],i==mme.selectedTracked,TRUE);
    }
    else
    {
        MMETrackedObject *track=mmeSelectedTrack();
        if(track!=NULL)mmeDrawObjectMarker(track,TRUE,FALSE);
    }
    if(mme.viewportMode==MME_VIEW_VOLUMES||mme.selectedVolume>=0)
    {
        for(i=0;i<mmeVolumeCount;i++)
        {
            if(!mmeVolumes[i].inUse)continue;
            mmeDrawVolumeWire(&mmeVolumes[i],i==mme.selectedVolume);
        }
    }
}

void modernMapEditorDrawWorldGizmo(void)
{
    /* Draw the manipulator from the live main-world render callback instead of
       relying on a sibling UI region to happen to composite it afterward.
       Selection, hit testing and transform application still use the same
       projection, so what is visible is exactly what is draggable. */
    if (!mme.active) return;
    if (mmeSelectedTrack() == NULL && mmeSelectedVolume() == NULL) return;
    mmeDrawTransformGizmo();
}

static sdword mmeListIndexAt(const rectangle *list, sdword scroll, sdword mouseY)
{
    sdword rowH=uiScaleSize(31);
    sdword row=(mouseY-list->y0)/rowH;
    if(mouseY<list->y0||mouseY>=list->y1||row<0)return -1;
    return scroll+row;
}

static udword mmeLeftProcess(regionhandle region, smemsize ID, udword event, udword data)
{
    rectangle panel=mmeLeftRect();
    rectangle tabs[MME_TAB_COUNT]; rectangle list; rectangle footer;
    sdword x=mouseCursorX(),y=mouseCursorY();
    sdword index;
    (void)ID;(void)data;
    if(event!=RPE_ReleaseLeft&&event!=RPE_WheelUp&&event!=RPE_WheelDown)return 0;
    mmeLayoutTabs(&panel,tabs,&list,&footer);
    if(event==RPE_ReleaseLeft)
    {
        for(index=0;index<MME_TAB_COUNT;index++)
            if(mmePointIn(&tabs[index],x,y))
            {
                mme.tab=index; regRecursiveSetDirty(region); return RPR_Redraw;
            }
        if(mmePointIn(&list,x,y))
        {
            if(mme.tab==MME_TAB_ASSETS)
            {
                index=mmeListIndexAt(&list,mme.assetScroll,y);
                if(index>=0&&index<mmeAssetCount)mme.selectedAsset=index;
            }
            else if(mme.tab==MME_TAB_SCENE)
            {
                index=mmeListIndexAt(&list,mme.sceneScroll,y);
                if(index>=0&&index<mmeTrackedCount&&!mmeTracked[index].deleted)mmeSelectTrack(index);
            }
            else
            {
                index=mmeListIndexAt(&list,mme.volumeScroll,y);
                if(index>=0&&index<mmeVolumeCount&&mmeVolumes[index].inUse)mmeSelectVolume(index);
            }
            regRecursiveSetDirty(region); return RPR_Redraw;
        }
    }
    else
    {
        sdword delta=event==RPE_WheelUp?-1:1;
        if(mme.tab==MME_TAB_ASSETS)mme.assetScroll=mmeClamp(mme.assetScroll+delta,0,mmeAssetCount>1?mmeAssetCount-1:0);
        else if(mme.tab==MME_TAB_SCENE)mme.sceneScroll=mmeClamp(mme.sceneScroll+delta,0,mmeTrackedCount>1?mmeTrackedCount-1:0);
        else mme.volumeScroll=mmeClamp(mme.volumeScroll+delta,0,mmeVolumeCount>1?mmeVolumeCount-1:0);
        regRecursiveSetDirty(region);return RPR_Redraw;
    }
    return 0;
}

static bool32 mmeSetWakeSliderFromMouse(const rectangle *inner, sdword rowH)
{
    MMEVolume *volume = mmeSelectedVolume();
    rectangle slider;
    real32 t;
    if (volume == NULL || inner == NULL || rowH <= 0) return FALSE;
    slider.x0 = inner->x0 + uiScaleSize(112);
    slider.x1 = inner->x1 - uiScaleSize(48);
    slider.y0 = inner->y0 + 21*rowH;
    slider.y1 = slider.y0 + rowH;
    if (!mmePointIn(&slider, mouseCursorX(), mouseCursorY()) && !mme.wakeSliderDrag)
        return FALSE;
    t = (real32)(mouseCursorX() - slider.x0) / (real32)max(1, slider.x1-slider.x0);
    volume->data.wakeStrength = mmeClampF(t, 0.0f, 1.0f) * 2.0f;
    mme.inspectorRow = 21;
    mmeSyncDustVolumesToRenderer();
    mmeStatus("WAKE STRENGTH LIVE / 0 = OFF / 1 = BASELINE / 2 = MAX");
    return TRUE;
}

static bool32 mmeSetDustFadeSliderFromMouse(const rectangle *inner, sdword rowH)
{
    rectangle slider;
    sdword row;
    real32 t;
    if (mmeSelectedVolume() == NULL || inner == NULL || rowH <= 0) return FALSE;
    row = mme.wakeSliderDrag ? mme.inspectorRow :
        (mouseCursorY() - inner->y0) / rowH;
    if (row != 27 && row != 28) return FALSE;
    slider.x0 = inner->x0 + uiScaleSize(112);
    slider.x1 = inner->x1 - uiScaleSize(48);
    slider.y0 = inner->y0 + row*rowH;
    slider.y1 = slider.y0 + rowH;
    if (!mmePointIn(&slider, mouseCursorX(), mouseCursorY()) &&
        !mme.wakeSliderDrag) return FALSE;
    t = mmeClampF((real32)(mouseCursorX()-slider.x0) /
                  (real32)max(1,slider.x1-slider.x0),0.0f,1.0f);
    if (row == 27)
        mmeDustFadeDistance = 5000.0f + t*245000.0f;
    else
        mmeDustFadeStrength = t*4.0f;
    mme.inspectorRow = row;
    mmeSyncDustVolumesToRenderer();
    mmeStatus(row==27 ? "DISTANCE FADE START LIVE / FULL STRENGTH INSIDE RADIUS" :
                        "DISTANCE FADE STRENGTH LIVE / 0 = DISABLED / 4 = STRONG");
    return TRUE;
}

static udword mmeRightProcess(regionhandle region, smemsize ID, udword event, udword data)
{
    rectangle panel=mmeRightRect(); rectangle inner;
    sdword rowH; sdword row;
    (void)ID;(void)data;
    if(event!=RPE_PressLeft&&event!=RPE_HoldLeft&&event!=RPE_ReleaseLeft&&
       event!=RPE_WheelUp&&event!=RPE_WheelDown)return 0;
    mmeRightInspectorRect(&panel,&inner);
    rowH=(inner.y1-inner.y0)/MME_INSPECTOR_ROWS;
    if (mmeCloudPresetUIVisible())
    {
        rectangle dropBox; rectangle buttons[5]; rectangle dropList;
        sdword dropRowH; sdword dropRows;
        mmeCloudPresetLayout(&panel,&dropBox,buttons,&dropList,&dropRowH,&dropRows);
        if ((event==RPE_PressLeft || event==RPE_HoldLeft) &&
            ((mme.presetDropdownOpen && mmePointIn(&dropList,mouseCursorX(),mouseCursorY())) ||
             mmePointIn(&dropBox,mouseCursorX(),mouseCursorY()) ||
             (mouseCursorY() >= buttons[0].y0 && mouseCursorY() <= buttons[4].y1 &&
              mouseCursorX() >= dropBox.x0 && mouseCursorX() <= dropBox.x1)))
        {
            regRecursiveSetDirty(region);return RPR_Redraw;
        }
        if (event==RPE_ReleaseLeft)
        {
            if (mme.presetDropdownOpen && dropRows > 0 &&
                mmePointIn(&dropList,mouseCursorX(),mouseCursorY()))
            {
                sdword visibleRow=(mouseCursorY()-dropList.y0)/dropRowH;
                sdword presetIndex=mme.presetScroll+visibleRow;
                if(presetIndex>=0&&presetIndex<mmeCloudPresetCount)mme.presetSelected=presetIndex;
                mme.presetDropdownOpen=FALSE;
                regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&dropBox,mouseCursorX(),mouseCursorY()))
            {
                mme.presetDropdownOpen=!mme.presetDropdownOpen;
                regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&buttons[0],mouseCursorX(),mouseCursorY()))
            {
                mmeCloudPresetLoad(); mme.presetDropdownOpen=FALSE; regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&buttons[1],mouseCursorX(),mouseCursorY()))
            {
                mmeCloudPresetSaveNew(); mme.presetDropdownOpen=FALSE; regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&buttons[2],mouseCursorX(),mouseCursorY()))
            {
                mmeCloudPresetOverwrite(); mme.presetDropdownOpen=FALSE; regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&buttons[3],mouseCursorX(),mouseCursorY()))
            {
                mmeCopySelectedVolume(); regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mmePointIn(&buttons[4],mouseCursorX(),mouseCursorY()))
            {
                mmePasteVolumeClipboard(); regRecursiveSetDirty(region);return RPR_Redraw;
            }
            if(mme.presetDropdownOpen) mme.presetDropdownOpen=FALSE;
        }
        if ((event==RPE_WheelUp||event==RPE_WheelDown) && mme.presetDropdownOpen &&
            mmePointIn(&dropList,mouseCursorX(),mouseCursorY()))
        {
            sdword delta=event==RPE_WheelUp?-1:1;
            sdword maxScroll=mmeCloudPresetCount>MME_PRESET_VISIBLE_ROWS?
                mmeCloudPresetCount-MME_PRESET_VISIBLE_ROWS:0;
            mme.presetScroll=mmeClamp(mme.presetScroll+delta,0,maxScroll);
            regRecursiveSetDirty(region);return RPR_Redraw;
        }
        if ((event==RPE_WheelUp||event==RPE_WheelDown) &&
            mmePointIn(&dropBox,mouseCursorX(),mouseCursorY()))
        {
            if (mmeCloudPresetCount > 0)
            {
                sdword delta=event==RPE_WheelUp?-1:1;
                mme.presetSelected=mmeClamp(mme.presetSelected+delta,0,mmeCloudPresetCount-1);
            }
            regRecursiveSetDirty(region);return RPR_Redraw;
        }
        if ((event==RPE_WheelUp||event==RPE_WheelDown) &&
            mouseCursorY() >= buttons[0].y0 && mouseCursorY() <= buttons[4].y1 &&
            mouseCursorX() >= dropBox.x0 && mouseCursorX() <= dropBox.x1)
        {
            regRecursiveSetDirty(region);return RPR_Redraw;
        }
    }
    if(event==RPE_PressLeft)
    {
        if (mmeSetWakeSliderFromMouse(&inner,rowH))
        {
            mme.wakeSliderDrag=TRUE; regRecursiveSetDirty(region); return RPR_Redraw;
        }
        if (mmeSetDustFadeSliderFromMouse(&inner,rowH))
        {
            mme.wakeSliderDrag=TRUE; regRecursiveSetDirty(region); return RPR_Redraw;
        }
        return 0;
    }
    if(event==RPE_HoldLeft && mme.wakeSliderDrag)
    {
        if (mme.inspectorRow==21) mmeSetWakeSliderFromMouse(&inner,rowH);
        else mmeSetDustFadeSliderFromMouse(&inner,rowH);
        regRecursiveSetDirty(region); return RPR_Redraw;
    }
    if(event==RPE_ReleaseLeft)
    {
        if (mme.wakeSliderDrag)
        {
            if (mme.inspectorRow==21) mmeSetWakeSliderFromMouse(&inner,rowH);
            else mmeSetDustFadeSliderFromMouse(&inner,rowH);
            mme.wakeSliderDrag=FALSE; regRecursiveSetDirty(region); return RPR_Redraw;
        }
        if(mmePointIn(&inner,mouseCursorX(),mouseCursorY()))
        {
            row=(mouseCursorY()-inner.y0)/rowH;
            mme.inspectorRow=mmeClamp(row,0,mmeInspectorCount()-1);
            regRecursiveSetDirty(region);return RPR_Redraw;
        }
    }
    if(event==RPE_WheelUp||event==RPE_WheelDown)
    {
        mmeAdjustSelection(event==RPE_WheelUp?1:-1);
        regRecursiveSetDirty(region);return RPR_Redraw;
    }
    return 0;
}

static udword mmeToolbarProcess(regionhandle region, smemsize ID, udword event, udword data)
{
    rectangle bar=mmeToolbarRect(); rectangle b[10]; sdword i; sdword x=mouseCursorX(),y=mouseCursorY();
    (void)ID;(void)data;
    if(event!=RPE_ReleaseLeft)return 0;
    mmeLayoutToolbar(&bar,b);
    for(i=0;i<10;i++)if(mmePointIn(&b[i],x,y))break;
    if(i>=10)return 0;
    switch(i)
    {
        case 0:
            if (keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY))
            {
                mme.selectedMission--; if(mme.selectedMission<1)mme.selectedMission=16;
            }
            else
            {
                mme.selectedMission++; if(mme.selectedMission>16)mme.selectedMission=1;
            }
            mmeStatus("MISSION SELECTED - CLICK LOAD PAUSED TO BOOT CLEAN AUTHORING STATE");
            break;
        case 1: mmeRequestMissionLoad(mme.selectedMission,FALSE); break;
        case 2: universePause=!universePause; mmeStatus(universePause?"SIMULATION PAUSED":"SIMULATION RUNNING (KAS STATE IS INDEPENDENT)"); break;
        case 3: kasEditorPauseSet(!kasEditorPauseGet()); mmeStatus(kasEditorPauseGet()?"MISSION SCRIPT/KAS PAUSED":"MISSION SCRIPT/KAS RUNNING"); break;
        case 4: mme.viewportMode=(mme.viewportMode+1)%MME_VIEW_COUNT; break;
        case 5: mme.cameraMode=(mme.cameraMode+1)%MME_CAMERA_COUNT; mmeApplyCameraPreset(); break;
        case 6: mmeSaveMap(); break;
        case 7: mmeRequestMissionLoad(spGetCurrentMission(),TRUE); break;
        case 8: modernMapEditorCloseForToolSwitch(); modernUIToggleLightingEditor(); return RPR_Redraw;
        case 9: modernMapEditorClose(); return RPR_Redraw;
    }
    regRecursiveSetDirty(region);return RPR_Redraw;
}

bool32 modernMapEditorHandleWorldEvent(udword event, udword data)
{
    SpaceObj *clicked;
    sdword i;
    (void)data;
    if(!mme.active)return FALSE;

    if(event==RPE_WheelUp) return mmeWheelFly(+1);
    if(event==RPE_WheelDown) return mmeWheelFly(-1);

    if(event==RPE_PressLeft)
    {
        if (mmeBeginGizmoDrag(mouseCursorX(), mouseCursorY()))
            return TRUE;
        /* Preserve the old editor behaviour: the world press is owned here so
           normal RTS band-selection never starts underneath the editor. */
        return TRUE;
    }
    if(event==RPE_HoldLeft)
    {
        if (mme.gizmoDragKind != MME_GIZMO_NONE)
        {
            mmeUpdateGizmoDrag(mouseCursorX(), mouseCursorY());
            return TRUE;
        }
        return TRUE;
    }
    if(event!=RPE_ReleaseLeft)return FALSE;
    if (mme.gizmoDragKind != MME_GIZMO_NONE)
    {
        mmeUpdateGizmoDrag(mouseCursorX(), mouseCursorY());
        mme.gizmoDragKind = MME_GIZMO_NONE;
        mmeStatus("TRANSFORM APPLIED / UNSAVED - DRAG ANOTHER HANDLE OR SAVE MAP");
        return TRUE;
    }

    /* Volumes are editor-only, so test their projected markers before legacy object picking. */
    for(i=mmeVolumeCount-1;i>=0;i--)
    {
        sdword x,y; real32 *p;
        if(!mmeVolumes[i].inUse)continue;
        p=mmeVolumes[i].data.position;
        if(mmeProjectPoint(p,&x,&y)&&ABS(x-mouseCursorX())<=uiScaleSize(12)&&ABS(y-mouseCursorY())<=uiScaleSize(12))
        {
            mme.tab=MME_TAB_VOLUMES; mmeSelectVolume(i); return TRUE;
        }
    }
    clicked=(SpaceObj *)selSelectionClick(universe.RenderList.head,&universe.mainCameraCommand.actualcamera,
                                          mouseCursorX(),mouseCursorY(),TRUE,TRUE);
    if(clicked!=NULL)
    {
        sdword index=mmeFindTracked(clicked);
        if(index<0)index=mmeTrackObject(clicked,TRUE);
        mme.tab=MME_TAB_SCENE; mmeSelectTrack(index); mmeStatus("WORLD OBJECT SELECTED / DRAG GIZMO TO TRANSFORM");
    }
    else
    {
        mme.selectedTracked=-1; mme.selectedVolume=-1; mmeStatus("WORLD SELECTION CLEARED");
    }
    return TRUE;
}

bool32 modernMapEditorHandleKey(sdword key)
{
    if(!mme.active)return FALSE;
    if(key==F11KEY&&(keyIsHit(LSHIFTKEY)||keyIsHit(RSHIFTKEY)))
    {
        modernMapEditorClose(); return TRUE;
    }
    if(key==ESCKEY){modernMapEditorClose();return TRUE;}
    /* Camera locomotion is intentionally mouse/wheel-only in Shift+F11 so
       editor text/list controls never fight WASD or arrow-key navigation. */
    if(key==CKEY&&mmeControlHeld()&&mmeSelectedVolume()!=NULL){mmeCopySelectedVolume();return TRUE;}
    if(key==VKEY&&mmeControlHeld()){mmePasteVolumeClipboard();return TRUE;}
    if(key==SKEY&&mmeControlHeld()){mmeSaveMap();return TRUE;}
    if(key==DKEY&&mmeControlHeld()&&mmeShiftHeld()){mmeDuplicateSelection();return TRUE;}
    if(key==GKEY && mmeSelectedVolume()!=NULL)
    {
        MMEVolume *volume = mmeSelectedVolume();
        udword seed = volume->data.shapeSeed;
        seed ^= (udword)SDL_GetTicks() + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed = seed * 1664525u + 1013904223u;
        if (seed == 0) seed = 1;
        volume->data.shapeSeed = seed;
        if (volume->data.shapeVariation < 0.05f) volume->data.shapeVariation = 0.72f;
        mmeStatus("DUST SHAPE RANDOMIZED / G AGAIN FOR ANOTHER SILHOUETTE");
        return TRUE;
    }
    if(key==PKEY)
    {
        if(mme.tab==MME_TAB_ASSETS)mmePlaceCurrentAsset();else mmeMoveSelectionToCursor();return TRUE;
    }
    if(key==EKEY){mme.transformMode=MME_TRANSFORM_ROTATE;mmeStatus("TRANSFORM MODE: ROTATE");return TRUE;}
    if(key==RKEY){mme.transformMode=MME_TRANSFORM_SCALE;mmeStatus("TRANSFORM MODE: SCALE (resources/volumes)");return TRUE;}
    if(key==XKEY){mme.axis=MME_AXIS_X;mmeStatus("ACTIVE AXIS: X");return TRUE;}
    if(key==YKEY){mme.axis=MME_AXIS_Y;mmeStatus("ACTIVE AXIS: Y");return TRUE;}
    if(key==ZKEY){mme.axis=MME_AXIS_Z;mmeStatus("ACTIVE AXIS: Z");return TRUE;}
    if(key==ARRUP){sdword count=mmeInspectorCount();mme.inspectorRow=(mme.inspectorRow+count-1)%count;return TRUE;}
    if(key==ARRDOWN){sdword count=mmeInspectorCount();mme.inspectorRow=(mme.inspectorRow+1)%count;return TRUE;}
    if(key==ARRLEFT){mmeAdjustSelection(-1);return TRUE;}
    if(key==ARRRIGHT){mmeAdjustSelection(1);return TRUE;}
    if(key==DELETEKEY){mmeDeleteSelection();return TRUE;}
    if(key==FKEY){mmeFocusSelection();return TRUE;}
    if(key==LKEY){mmeRequestMissionLoad(spGetCurrentMission(),TRUE);return TRUE;}
    return FALSE;
}

bool32 modernMapEditorWireframeActive(void)
{
    return mme.active && mme.viewportMode==MME_VIEW_WIREFRAME;
}

bool32 modernMapEditorActive(void)
{
    return mme.active;
}

void modernMapEditorClose(void)
{
    bool32 restoreUniverse;
    bool32 restoreKas;
    if(!mme.active)return;
    restoreUniverse=mme.previousUniversePause;
    restoreKas=mme.previousKasPause;
    mme.active=FALSE;
    mmeCloseRegions();
    universePause=restoreUniverse;
    kasEditorPauseSet(restoreKas);
    fprintf(stderr,"[MapEditor] Closed. Simulation=%s KAS=%s.\n",universePause?"paused":"running",kasEditorPauseGet()?"paused":"running");
}

void modernMapEditorCloseForToolSwitch(void)
{
    if (!mme.active) return;
    /* The map editor temporarily pauses both simulation and KAS on entry.
       A tool switch is still a real editor close, so restore the state from
       before Shift+F11 was opened.  Capturing the editor's current state here
       promoted that temporary KAS pause into a permanent mission pause; all
       objective completion watches then appeared to stop advancing. */
    modernMapEditorClose();
}

void modernMapEditorToggle(void)
{
    if(mme.active){modernMapEditorClose();return;}
    if(!gameIsRunning||ghMainRegion==NULL)return;
    memset(&mme,0,sizeof(mme));
    mme.headingFont=mme.bodyFont=mme.tinyFont=FONT_InvalidFontHandle;
    mme.selectedAsset=0; mme.selectedTracked=-1; mme.selectedVolume=-1;
    mme.pendingMission=0;
    mmeOpenInternal(FALSE);
}

void modernMapEditorCameraUpdate(void)
{
    /* Keep authored clouds alive after the editor UI is closed, never inherit
       the previous mission's volumes, and apply the current mission RTXMAP on
       ordinary campaign startup. Previously mmeLoadMapOverlay() was reached
       only through the editor's explicit Load Overlay action, so a fresh game
       started Mission 1 with an empty renderer until Shift+F11 loaded it. */
    if (gameIsRunning && mmeVolumeMission != spGetCurrentMission())
    {
        sdword mission = spGetCurrentMission();
        memset(mmeVolumes, 0, sizeof(mmeVolumes));
        mmeVolumeCount = 0;
        mmeVolumeMission = mission;
        mmeDustFadeDistance = 65000.0f;
        mmeDustFadeStrength = 1.0f;
        if (mission > 0)
        {
            if (mmeLoadMapOverlay())
                fprintf(stderr,
                    "[MapEditor] Mission %02d RTXMAP automatically applied "
                    "during normal gameplay startup.\n", mission);
            else
                fprintf(stderr,
                    "[MapEditor] Mission %02d has no automatic RTXMAP overlay.\n",
                    mission);
        }
    }
    mmeSyncDustVolumesToRenderer();
    if (mme.active) mmeUpdateFreeCamera();
}

bool32 modernMapEditorPump(void)
{
    sdword mission;
    bool32 loadOverlay;
    if(mme.pendingMission<=0)return FALSE;
    mission=mme.pendingMission; loadOverlay=mme.pendingLoadOverlay;
    mme.pendingMission=0; mme.pendingLoadOverlay=FALSE;
    mme.active=FALSE; mmeCloseRegions();
    soundEventStopMusic(0.0f);
    soundEventPause(TRUE);
    gameEnd();
    singlePlayerGame=TRUE;
    tutorial=TUTORIAL_SINGLEPLAYER;
    numPlayers=2; curPlayer=0;
    strcpy(playerNames[0],"Map Editor");
    spResetMissionSequenceToBeginning();
    spMapEditorBootConfigure(TRUE,(MissionEnum)mission);
    singlePlayerInit();
    horseRaceInit();
    gameStart(NULL);
    horseRaceShutdown();
    /* Mission warp/bootstrap can start campaign music; keep the editor quiet. */
    soundEventStopMusic(0.0f);
    soundEventPause(FALSE);
    spMapEditorBootConfigure(FALSE,MISSION_ENUM_NOT_INITIALISED);
    if(hrAbortLoadingGame)
    {
        fprintf(stderr,"[MapEditor] Mission %02d load aborted.\n",mission);
        hrAbortLoadingGame=FALSE;
        return TRUE;
    }
    universePause=TRUE;
    kasEditorPauseSet(TRUE);
    memset(mmeVolumes,0,sizeof(mmeVolumes));
    mmeVolumeCount=0;
    mmeVolumeMission=-1;
    mmeSyncDustVolumesToRenderer();
    memset(&mme,0,sizeof(mme));
    mme.headingFont=mme.bodyFont=mme.tinyFont=FONT_InvalidFontHandle;
    mme.selectedAsset=0; mme.selectedTracked=-1; mme.selectedVolume=-1;
    mmeOpenInternal(TRUE);
    if(loadOverlay)mmeLoadMapOverlay();
    return TRUE;
}

sdword modernMapEditorDustVolumeCount(void)
{
    sdword i,count=0;
    for(i=0;i<mmeVolumeCount;i++)if(mmeVolumes[i].inUse)count++;
    return count;
}

bool32 modernMapEditorDustVolumeGet(sdword index, ModernMapDustVolumeSnapshot *outVolume)
{
    sdword i,count=0;
    if(outVolume==NULL||index<0)return FALSE;
    for(i=0;i<mmeVolumeCount;i++)
    {
        if(!mmeVolumes[i].inUse)continue;
        if(count==index){*outVolume=mmeVolumes[i].data;return TRUE;}
        count++;
    }
    return FALSE;
}

/* Region callback bindings are kept at the bottom so all editor actions above
   are available without forward-declaration noise. */
static void mmeBindRegions(void)
{
    if(mme.overlayRegion!=NULL)regDrawFunctionSet(mme.overlayRegion,mmeDrawOverlay);
    if(mme.leftRegion!=NULL){regDrawFunctionSet(mme.leftRegion,mmeDrawLeft);regFunctionSet(mme.leftRegion,mmeLeftProcess);}
    if(mme.rightRegion!=NULL){regDrawFunctionSet(mme.rightRegion,mmeDrawRight);regFunctionSet(mme.rightRegion,mmeRightProcess);}
    if(mme.toolbarRegion!=NULL){regDrawFunctionSet(mme.toolbarRegion,mmeDrawToolbar);regFunctionSet(mme.toolbarRegion,mmeToolbarProcess);}
}
