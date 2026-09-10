#include "ModernMissionBackdrop.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MissionEnum.h"
#include "File.h"
#include "ModernGraphics.h"
#include "glinc.h"
#include "render.h"
#include "texreg.h"

/* RTX-0067: mission skies are pole-safe dual-paraboloid HDR maps packed into
   one 2:1 Texture2D. They are rendered by a native D3D12 fullscreen
   direction-sampling pass; no sky sphere, cube faces, latitude/longitude pole
   geometry, or octahedral fold remains in the visible path. */
#define MMB_DDS_DX10_HEADER_BYTES 148u
#define MMB_DXGI_FORMAT_BC6H_UF16 95u
#define MMB_DXGI_FORMAT_BC7_UNORM 98u
#define MMB_D3D10_RESOURCE_DIMENSION_TEXTURE2D 3u
#define MMB_PI 3.14159265358979323846f
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM_ARB
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB 0x8E8Cu
#endif
#ifndef GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB 0x8E8Fu
#endif

extern bool32 singlePlayerGame;
extern MissionEnum spGetCurrentMission(void);

static GLuint mmbTexture;
static sdword mmbTextureMission;
static sdword mmbTextureAttemptMission;
static bool32 mmbGodRaySourceValid;
static sdword mmbGodRaySourceMission;
/* Keep lighting metadata/authoring live while the defective converted visual
   atlas is bypassed in favor of the authentic BTG background. */
static bool32 mmbPackedSkyVisualsEnabled = FALSE;
static real32 mmbGodRaySourceDirection[3];
static real32 mmbGodRaySourceColor[3] = {1.0f, 1.0f, 1.0f};
static real32 mmbAmbientColor[3] = {0.08f, 0.08f, 0.08f};

typedef struct MMBLightingAuthoringOverride
{
    sdword mission;
    bool32 loaded;
    bool32 loadedFromDisk;
    bool32 enabled;
    bool32 sourceValid;
    real32 direction[3];
    real32 sunIntensity;
    real32 sourceColorScale[3];
    bool32 ambientEnabled;
    real32 ambientScale;
    bool32 ambientColorOverride;
    real32 ambientColor[3];
    bool32 replaceMapLights;
    sdword authoredLightCount;
    ModernMissionAuthoredLight authoredLights[MODERN_MISSION_AUTHORED_LIGHT_MAX];
} MMBLightingAuthoringOverride;

static MMBLightingAuthoringOverride mmbLightingOverride;

static real32 mmbClamp(real32 value, real32 low, real32 high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void mmbNormalizeDirection(real32 direction[3])
{
    const real32 length = (real32)sqrt(
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2]);
    if (length > 0.000001f)
    {
        direction[0] /= length;
        direction[1] /= length;
        direction[2] /= length;
    }
    else
    {
        direction[0] = 0.0f;
        direction[1] = 0.0f;
        direction[2] = -1.0f;
    }
}

static void mmbDefaultAmbientTint(real32 tint[3])
{
    real32 maximum = max(mmbAmbientColor[0],
                         max(mmbAmbientColor[1], mmbAmbientColor[2]));
    if (maximum <= 0.000001f)
    {
        tint[0] = tint[1] = tint[2] = 1.0f;
        return;
    }
    tint[0] = mmbAmbientColor[0] / maximum;
    tint[1] = mmbAmbientColor[1] / maximum;
    tint[2] = mmbAmbientColor[2] / maximum;
}

static void mmbSanitizeAuthoredLight(ModernMissionAuthoredLight *light)
{
    if (light == NULL) return;
    if (light->type != MODERN_MISSION_LIGHT_POINT &&
        light->type != MODERN_MISSION_LIGHT_SPOT &&
        light->type != MODERN_MISSION_LIGHT_AMBIENT)
    {
        light->type = MODERN_MISSION_LIGHT_POINT;
    }
    light->enabled = light->enabled ? TRUE : FALSE;
    light->color[0] = mmbClamp(light->color[0], 0.0f, 4.0f);
    light->color[1] = mmbClamp(light->color[1], 0.0f, 4.0f);
    light->color[2] = mmbClamp(light->color[2], 0.0f, 4.0f);
    light->intensity = mmbClamp(light->intensity, 0.0f, 8.0f);
    light->radius = mmbClamp(light->radius, 100.0f, 1000000.0f);
    light->coneAngle = mmbClamp(light->coneAngle, 1.0f, 89.0f);
    light->edgeAngle = mmbClamp(light->edgeAngle, 0.0f, 89.0f);
    mmbNormalizeDirection(light->direction);
}

static void mmbLightingOverrideDefaults(sdword mission)
{
    memset(&mmbLightingOverride, 0, sizeof(mmbLightingOverride));
    mmbLightingOverride.mission = mission;
    mmbLightingOverride.loaded = TRUE;
    mmbLightingOverride.sourceValid = mmbGodRaySourceValid;
    mmbLightingOverride.direction[0] = mmbGodRaySourceDirection[0];
    mmbLightingOverride.direction[1] = mmbGodRaySourceDirection[1];
    mmbLightingOverride.direction[2] = mmbGodRaySourceDirection[2];
    mmbNormalizeDirection(mmbLightingOverride.direction);
    mmbLightingOverride.sunIntensity = 1.0f;
    mmbLightingOverride.sourceColorScale[0] = 1.0f;
    mmbLightingOverride.sourceColorScale[1] = 1.0f;
    mmbLightingOverride.sourceColorScale[2] = 1.0f;
    mmbLightingOverride.ambientEnabled = TRUE;
    mmbLightingOverride.ambientScale = 1.0f;
    mmbLightingOverride.ambientColorOverride = FALSE;
    mmbDefaultAmbientTint(mmbLightingOverride.ambientColor);
    mmbLightingOverride.replaceMapLights = FALSE;
    mmbLightingOverride.authoredLightCount = 0;
}

static void mmbLightingOverridePath(char path[96], sdword mission)
{
    snprintf(path, 96, "RTXLighting/Mission%02d.rtxlight", mission);
}

static void mmbPushAuthoringLights(void)
{
    HWModernMissionLightEmitter emitters[MODERN_MISSION_AUTHORED_LIGHT_MAX];
    sdword count = 0;
    sdword index;
    hwModernGraphicsSetMissionAuthoringReplacesMapLights(
        mmbLightingOverride.enabled && mmbLightingOverride.replaceMapLights);
    if (!mmbLightingOverride.enabled)
    {
        hwModernGraphicsSetMissionAuthoringLights(NULL, 0);
        return;
    }
    for (index = 0; index < mmbLightingOverride.authoredLightCount &&
                    index < MODERN_MISSION_AUTHORED_LIGHT_MAX; ++index)
    {
        const ModernMissionAuthoredLight *source =
            &mmbLightingOverride.authoredLights[index];
        HWModernMissionLightEmitter *destination = &emitters[count++];
        memset(destination, 0, sizeof(*destination));
        destination->type = source->type == MODERN_MISSION_LIGHT_SPOT ?
            HW_MODERN_MISSION_LIGHT_SPOT :
            (source->type == MODERN_MISSION_LIGHT_AMBIENT ?
             HW_MODERN_MISSION_LIGHT_AMBIENT : HW_MODERN_MISSION_LIGHT_POINT);
        destination->enabled = source->enabled ? 1u : 0u;
        destination->position[0] = source->position[0];
        destination->position[1] = source->position[1];
        destination->position[2] = source->position[2];
        destination->direction[0] = source->direction[0];
        destination->direction[1] = source->direction[1];
        destination->direction[2] = source->direction[2];
        destination->color[0] = source->color[0];
        destination->color[1] = source->color[1];
        destination->color[2] = source->color[2];
        destination->intensity = source->intensity;
        destination->radius = source->radius;
        destination->coneAngle = source->coneAngle;
        destination->edgeAngle = source->edgeAngle;
    }
    hwModernGraphicsSetMissionAuthoringLights(
        count > 0 ? emitters : NULL, (unsigned int)count);
}

static void mmbLightingOverrideLoad(sdword mission)
{
    char path[96];
    char line[512];
    filehandle handle;
    int enabled = 0;
    int sourceValid = 0;
    int ambientColorOverride = 0;
    int ambientEnabled = 1;
    int replaceMapLights = 0;
    real32 direction[3];
    real32 colorScale[3];
    real32 ambientColor[3];
    real32 sunIntensity;
    real32 ambientScale;
    sdword authoredLightCount = 0;

    mmbLightingOverrideDefaults(mission);
    mmbLightingOverridePath(path, mission);
    handle = fileOpen(path, FF_TextMode | FF_IgnoreBIG |
                      FF_UserSettingsPath | FF_ReturnNULLOnFail);
    if (handle == 0)
    {
        mmbPushAuthoringLights();
        fprintf(stderr,
                "[ModernBackdrop] Mission %02d has no saved RTX lighting override; using HDR sky metadata.\n",
                mission);
        return;
    }

    direction[0] = mmbLightingOverride.direction[0];
    direction[1] = mmbLightingOverride.direction[1];
    direction[2] = mmbLightingOverride.direction[2];
    colorScale[0] = colorScale[1] = colorScale[2] = 1.0f;
    ambientColor[0] = mmbLightingOverride.ambientColor[0];
    ambientColor[1] = mmbLightingOverride.ambientColor[1];
    ambientColor[2] = mmbLightingOverride.ambientColor[2];
    sunIntensity = 1.0f;
    ambientScale = 1.0f;
    sourceValid = mmbGodRaySourceValid ? 1 : 0;

    while (fileLineRead(handle, line, sizeof(line)) > 0)
    {
        int parsedInt;
        float a, b, c;
        if (sscanf(line, "enabled %d", &parsedInt) == 1)
        {
            enabled = parsedInt;
        }
        else if (sscanf(line, "sourceValid %d", &parsedInt) == 1)
        {
            sourceValid = parsedInt;
        }
        else if (sscanf(line, "direction %f %f %f", &a, &b, &c) == 3)
        {
            direction[0] = (real32)a;
            direction[1] = (real32)b;
            direction[2] = (real32)c;
        }
        else if (sscanf(line, "sunIntensity %f", &a) == 1)
        {
            sunIntensity = (real32)a;
        }
        else if (sscanf(line, "sourceColorScale %f %f %f", &a, &b, &c) == 3)
        {
            colorScale[0] = (real32)a;
            colorScale[1] = (real32)b;
            colorScale[2] = (real32)c;
        }
        else if (sscanf(line, "ambientEnabled %d", &parsedInt) == 1)
        {
            ambientEnabled = parsedInt;
        }
        else if (sscanf(line, "ambientScale %f", &a) == 1)
        {
            ambientScale = (real32)a;
        }
        else if (sscanf(line, "ambientColorOverride %d", &parsedInt) == 1)
        {
            ambientColorOverride = parsedInt;
        }
        else if (sscanf(line, "ambientColor %f %f %f", &a, &b, &c) == 3)
        {
            ambientColor[0] = (real32)a;
            ambientColor[1] = (real32)b;
            ambientColor[2] = (real32)c;
        }
        else if (sscanf(line, "replaceMapLights %d", &parsedInt) == 1)
        {
            replaceMapLights = parsedInt;
        }
        else
        {
            int lightIndex, lightType, lightEnabled;
            float px, py, pz, dx, dy, dz, red, green, blue;
            float intensity, radius, cone, edge;
            if (sscanf(line,
                "light %d %d %d %f %f %f %f %f %f %f %f %f %f %f %f %f",
                &lightIndex, &lightType, &lightEnabled,
                &px, &py, &pz, &dx, &dy, &dz,
                &red, &green, &blue, &intensity, &radius, &cone, &edge) == 16 &&
                lightIndex >= 0 && lightIndex < MODERN_MISSION_AUTHORED_LIGHT_MAX)
            {
                ModernMissionAuthoredLight *light =
                    &mmbLightingOverride.authoredLights[lightIndex];
                memset(light, 0, sizeof(*light));
                light->type = lightType;
                light->enabled = lightEnabled ? TRUE : FALSE;
                light->position[0] = px; light->position[1] = py; light->position[2] = pz;
                light->direction[0] = dx; light->direction[1] = dy; light->direction[2] = dz;
                light->color[0] = red; light->color[1] = green; light->color[2] = blue;
                light->intensity = intensity;
                light->radius = radius;
                light->coneAngle = cone;
                light->edgeAngle = edge;
                mmbSanitizeAuthoredLight(light);
                if (authoredLightCount < lightIndex + 1)
                    authoredLightCount = lightIndex + 1;
            }
        }
    }
    fileClose(handle);

    mmbLightingOverride.enabled = enabled ? TRUE : FALSE;
    mmbLightingOverride.sourceValid = sourceValid ? TRUE : FALSE;
    mmbLightingOverride.direction[0] = direction[0];
    mmbLightingOverride.direction[1] = direction[1];
    mmbLightingOverride.direction[2] = direction[2];
    mmbNormalizeDirection(mmbLightingOverride.direction);
    mmbLightingOverride.sunIntensity = mmbClamp(sunIntensity, 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[0] = mmbClamp(colorScale[0], 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[1] = mmbClamp(colorScale[1], 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[2] = mmbClamp(colorScale[2], 0.0f, 4.0f);
    mmbLightingOverride.ambientEnabled = ambientEnabled ? TRUE : FALSE;
    mmbLightingOverride.ambientScale = mmbClamp(ambientScale, 0.0f, 4.0f);
    mmbLightingOverride.ambientColorOverride = ambientColorOverride ? TRUE : FALSE;
    mmbLightingOverride.ambientColor[0] = mmbClamp(ambientColor[0], 0.0f, 4.0f);
    mmbLightingOverride.ambientColor[1] = mmbClamp(ambientColor[1], 0.0f, 4.0f);
    mmbLightingOverride.ambientColor[2] = mmbClamp(ambientColor[2], 0.0f, 4.0f);
    mmbLightingOverride.replaceMapLights = replaceMapLights ? TRUE : FALSE;
    mmbLightingOverride.authoredLightCount = authoredLightCount;
    mmbLightingOverride.loadedFromDisk = TRUE;
    mmbPushAuthoringLights();
    fprintf(stderr,
            "[ModernBackdrop] Mission %02d RTX lighting override loaded: enabled=%d dir=(%.4f, %.4f, %.4f) sun=%.2f keyColor=(%.2f, %.2f, %.2f) ambient=%.2f ambientTint=%s(%.2f, %.2f, %.2f) customLights=%d.\n",
            mission, mmbLightingOverride.enabled,
            mmbLightingOverride.direction[0],
            mmbLightingOverride.direction[1],
            mmbLightingOverride.direction[2],
            mmbLightingOverride.sunIntensity,
            mmbLightingOverride.sourceColorScale[0],
            mmbLightingOverride.sourceColorScale[1],
            mmbLightingOverride.sourceColorScale[2],
            mmbLightingOverride.ambientScale,
            mmbLightingOverride.ambientColorOverride ? "manual " : "auto ",
            mmbLightingOverride.ambientColor[0],
            mmbLightingOverride.ambientColor[1],
            mmbLightingOverride.ambientColor[2],
            mmbLightingOverride.authoredLightCount);
}

static void mmbLightingOverrideEnsure(sdword mission)
{
    if (mission == 0) return;
    if (!mmbLightingOverride.loaded || mmbLightingOverride.mission != mission)
    {
        mmbLightingOverrideLoad(mission);
    }
}

static bool32 mmbEffectiveLighting(sdword mission, real32 outDirection[3],
                                   real32 outSourceColor[3],
                                   real32 outAmbientColor[3],
                                   bool32 *outSourceValid)
{
    bool32 sourceValid;
    real32 sunIntensity = 1.0f;
    real32 colorScale[3] = {1.0f, 1.0f, 1.0f};
    bool32 ambientEnabled = TRUE;
    real32 ambientScale = 1.0f;
    bool32 ambientColorOverride = FALSE;
    real32 ambientColor[3];
    const real32 *direction = mmbGodRaySourceDirection;

    if (mission == 0 || mmbTextureMission != mission) return FALSE;
    mmbLightingOverrideEnsure(mission);
    sourceValid = mmbGodRaySourceValid;
    mmbDefaultAmbientTint(ambientColor);
    if (mmbLightingOverride.enabled)
    {
        sourceValid = mmbLightingOverride.sourceValid;
        direction = mmbLightingOverride.direction;
        sunIntensity = mmbLightingOverride.sunIntensity;
        colorScale[0] = mmbLightingOverride.sourceColorScale[0];
        colorScale[1] = mmbLightingOverride.sourceColorScale[1];
        colorScale[2] = mmbLightingOverride.sourceColorScale[2];
        ambientEnabled = mmbLightingOverride.ambientEnabled;
        ambientScale = mmbLightingOverride.ambientScale;
        ambientColorOverride = mmbLightingOverride.ambientColorOverride;
        ambientColor[0] = mmbLightingOverride.ambientColor[0];
        ambientColor[1] = mmbLightingOverride.ambientColor[1];
        ambientColor[2] = mmbLightingOverride.ambientColor[2];
    }

    if (outSourceValid != NULL) *outSourceValid = sourceValid;
    if (outDirection != NULL)
    {
        outDirection[0] = direction[0];
        outDirection[1] = direction[1];
        outDirection[2] = direction[2];
    }
    if (outSourceColor != NULL)
    {
        outSourceColor[0] = mmbGodRaySourceColor[0] * sunIntensity * colorScale[0];
        outSourceColor[1] = mmbGodRaySourceColor[1] * sunIntensity * colorScale[1];
        outSourceColor[2] = mmbGodRaySourceColor[2] * sunIntensity * colorScale[2];
    }
    if (outAmbientColor != NULL)
    {
        if (!ambientEnabled)
        {
            outAmbientColor[0] = 0.0f;
            outAmbientColor[1] = 0.0f;
            outAmbientColor[2] = 0.0f;
        }
        else if (ambientColorOverride)
        {
            real32 baseEnergy = max(mmbAmbientColor[0],
                                    max(mmbAmbientColor[1], mmbAmbientColor[2]));
            if (baseEnergy < 0.08f) baseEnergy = 0.08f;
            outAmbientColor[0] = ambientColor[0] * baseEnergy * ambientScale;
            outAmbientColor[1] = ambientColor[1] * baseEnergy * ambientScale;
            outAmbientColor[2] = ambientColor[2] * baseEnergy * ambientScale;
        }
        else
        {
            outAmbientColor[0] = mmbAmbientColor[0] * ambientScale;
            outAmbientColor[1] = mmbAmbientColor[1] * ambientScale;
            outAmbientColor[2] = mmbAmbientColor[2] * ambientScale;
        }
    }
    return TRUE;
}

static void mmbSubmitGodRayProjection(void)
{
    GLfloat modelView[16];
    GLfloat projection[16];
    GLfloat world[4];
    GLfloat eye[4];
    GLfloat clip[4];
    real32 direction[3];
    real32 sourceColor[3];
    bool32 sourceValid = FALSE;
    real32 screenX = 0.5f;
    real32 screenY = 0.5f;
    bool32 visible = FALSE;
    sdword row;
    sdword column;

    if (!mmbEffectiveLighting(mmbTextureMission, direction, sourceColor,
                              NULL, &sourceValid) || !sourceValid)
    {
        hwModernGraphicsSetGodRaySource(
            screenX, screenY, 0.0f, 0.0f, 0.0f, FALSE);
        return;
    }

    /* Project the same infinite world-space direction used by the physical
       mission key. w=0 deliberately removes camera translation, so the debug
       authoring override and god-ray source stay locked to the sky. */
    world[0] = direction[0];
    world[1] = direction[1];
    world[2] = direction[2];
    world[3] = 0.0f;

    glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
    glGetFloatv(GL_PROJECTION_MATRIX, projection);

    for (row = 0; row < 4; ++row)
    {
        eye[row] = 0.0f;
        for (column = 0; column < 4; ++column)
        {
            eye[row] += modelView[column * 4 + row] * world[column];
        }
    }
    for (row = 0; row < 4; ++row)
    {
        clip[row] = 0.0f;
        for (column = 0; column < 4; ++column)
        {
            clip[row] += projection[column * 4 + row] * eye[column];
        }
    }

    if (clip[3] > 0.0001f)
    {
        const real32 inverseW = 1.0f / clip[3];
        screenX = 0.5f + 0.5f * clip[0] * inverseW;
        screenY = 0.5f + 0.5f * clip[1] * inverseW;
        visible = TRUE;
    }

    hwModernGraphicsSetGodRaySource(
        screenX, screenY,
        sourceColor[0], sourceColor[1], sourceColor[2], visible);
}

static void *mmbLoadAsset(const char *relativePath, size_t *fileSize)
{
    char path[1024];
    char *basePath = SDL_GetBasePath();
    void *data = NULL;

    *fileSize = 0;
    if (basePath != NULL)
    {
        snprintf(path, sizeof(path), "%s%s", basePath, relativePath);
        data = SDL_LoadFile(path, fileSize);
        if (data == NULL)
        {
            snprintf(path, sizeof(path), "%s../../../../assets/%s",
                     basePath, relativePath);
            data = SDL_LoadFile(path, fileSize);
        }
        SDL_free(basePath);
    }
    if (data == NULL)
    {
        snprintf(path, sizeof(path), "assets/%s", relativePath);
        data = SDL_LoadFile(path, fileSize);
    }
    if (data == NULL)
    {
        data = SDL_LoadFile(relativePath, fileSize);
    }
    return data;
}

static sdword mmbCurrentCampaignMission(void)
{
    const MissionEnum mission = spGetCurrentMission();
    if (!singlePlayerGame || mission < MISSION_1_KHARAK_SYSTEM ||
        mission > MISSION_16_HIIGARA)
    {
        return 0;
    }
    return (sdword)mission;
}

static void mmbReleaseTexture(void)
{
    if (mmbTexture != 0)
    {
        glDeleteTextures(1, &mmbTexture);
        mmbTexture = 0;
        trClearCurrent();
    }
    mmbTextureMission = 0;
    mmbGodRaySourceValid = FALSE;
    mmbGodRaySourceMission = 0;
    mmbGodRaySourceDirection[0] = 0.0f;
    mmbGodRaySourceDirection[1] = 0.0f;
    mmbGodRaySourceDirection[2] = -1.0f;
    mmbGodRaySourceColor[0] = 1.0f;
    mmbGodRaySourceColor[1] = 1.0f;
    mmbGodRaySourceColor[2] = 1.0f;
    mmbAmbientColor[0] = 0.08f;
    mmbAmbientColor[1] = 0.08f;
    mmbAmbientColor[2] = 0.08f;
    hwModernGraphicsSetMissionAuthoringReplacesMapLights(FALSE);
    hwModernGraphicsSetMissionAuthoringLights(NULL, 0);
}

static udword mmbReadU32(const ubyte *bytes)
{
    return (udword)bytes[0] | ((udword)bytes[1] << 8) |
           ((udword)bytes[2] << 16) | ((udword)bytes[3] << 24);
}

static bool32 mmbValidateDds(const ubyte *data, size_t size,
                             sdword *width, sdword *height,
                             sdword *mipCount, udword *format)
{
    udword dxgiFormat;
    if (size < MMB_DDS_DX10_HEADER_BYTES || memcmp(data, "DDS ", 4) != 0 ||
        mmbReadU32(data + 4) != 124 || mmbReadU32(data + 76) != 32 ||
        mmbReadU32(data + 84) != 0x30315844u ||
        mmbReadU32(data + 132) != MMB_D3D10_RESOURCE_DIMENSION_TEXTURE2D ||
        mmbReadU32(data + 140) != 1)
    {
        return FALSE;
    }
    dxgiFormat = mmbReadU32(data + 128);
    /* RTX-0067's modern path is intentionally one format and one mapping:
       a 2:1 BC6H_UF16 texture containing two packed paraboloid hemispheres.
       Old BC7/equirectangular or 0065 square-octahedral assets must fall back
       instead of being sampled through the wrong projection. */
    if (dxgiFormat != MMB_DXGI_FORMAT_BC6H_UF16)
    {
        return FALSE;
    }
    *height = (sdword)mmbReadU32(data + 12);
    *width = (sdword)mmbReadU32(data + 16);
    *mipCount = (sdword)mmbReadU32(data + 28);
    *format = dxgiFormat;
    if (*width <= 0 || *height <= 0 || *width != *height * 2 ||
        *mipCount <= 0 || *mipCount > 16)
    {
        return FALSE;
    }
    return TRUE;
}

static bool32 mmbUploadBptcDds(const ubyte *data, size_t size,
                               sdword width, sdword height, sdword mipCount,
                               udword format)
{
    size_t offset = MMB_DDS_DX10_HEADER_BYTES;
    sdword level;
    const GLenum internalFormat = format == MMB_DXGI_FORMAT_BC6H_UF16 ?
        GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB :
        GL_COMPRESSED_RGBA_BPTC_UNORM_ARB;
    if (hwGlCompressedTexImage2D == NULL ||
        !glCheckExtension("GL_ARB_texture_compression_bptc"))
    {
        fprintf(stderr,
                "[ModernBackdrop] BPTC texture support is unavailable.\n");
        return FALSE;
    }
    for (level = 0; level < mipCount; ++level)
    {
        const sdword shiftedWidth = width >> level;
        const sdword shiftedHeight = height >> level;
        const sdword mipWidth = shiftedWidth > 0 ? shiftedWidth : 1;
        const sdword mipHeight = shiftedHeight > 0 ? shiftedHeight : 1;
        const size_t blocksWide = ((size_t)mipWidth + 3u) / 4u;
        const size_t blocksHigh = ((size_t)mipHeight + 3u) / 4u;
        const size_t mipBytes = blocksWide * blocksHigh * 16u;
        if (offset > size || mipBytes > size - offset || mipBytes > 0x7fffffffu)
        {
            return FALSE;
        }
        hwGlCompressedTexImage2D(GL_TEXTURE_2D, level, internalFormat,
                                 mipWidth, mipHeight, 0, (GLsizei)mipBytes,
                                 data + offset);
        offset += mipBytes;
    }
    return offset == size;
}

static real32 mmbGodRayLuminance(const ubyte *rgba)
{
    const real32 red = (real32)rgba[0] / 255.0f;
    const real32 green = (real32)rgba[1] / 255.0f;
    const real32 blue = (real32)rgba[2] / 255.0f;
    return red * 0.2126f + green * 0.7152f + blue * 0.0722f;
}

/* Pick one mission-owned volumetric emitter from the actual equirectangular
   mission sky.  This intentionally finds the largest coherent bright object,
   not the single brightest pixel: a broad sun / stellar glow must beat tiny
   point stars.  Horizontal connectivity wraps because u=0 and u=1 are the
   same longitude on the sky sphere. */
static void mmbAnalyzeGodRaySource(sdword mission, sdword width,
                                   sdword height, sdword mipCount)
{
    sdword level = 0;
    sdword sampleWidth = width;
    sdword sampleHeight = height;
    size_t pixelCount;
    ubyte *pixels = NULL;
    real32 *luminance = NULL;
    ubyte *visited = NULL;
    sdword *queue = NULL;
    real64 mean = 0.0;
    real64 variance = 0.0;
    real32 maximum = 0.0f;
    real32 threshold;
    sdword minimumArea;
    sdword bestArea = 0;
    real64 bestIntegrated = 0.0;
    real32 bestPeak = 0.0f;
    real64 bestSin = 0.0;
    real64 bestCos = 0.0;
    real64 bestY = 0.0;
    real64 bestWeight = 0.0;
    real64 bestRed = 0.0;
    real64 bestGreen = 0.0;
    real64 bestBlue = 0.0;
    real64 ambientRed = 0.0;
    real64 ambientGreen = 0.0;
    real64 ambientBlue = 0.0;
    sdword index;

    mmbGodRaySourceValid = FALSE;
    mmbGodRaySourceMission = mission;

    while (level + 1 < mipCount && sampleWidth > 256 && sampleHeight > 128)
    {
        ++level;
        sampleWidth = width >> level;
        sampleHeight = height >> level;
        if (sampleWidth < 1) sampleWidth = 1;
        if (sampleHeight < 1) sampleHeight = 1;
    }
    pixelCount = (size_t)sampleWidth * (size_t)sampleHeight;
    if (pixelCount == 0 || pixelCount > 1024u * 1024u)
    {
        return;
    }

    pixels = (ubyte *)malloc(pixelCount * 4u);
    luminance = (real32 *)malloc(pixelCount * sizeof(real32));
    visited = (ubyte *)calloc(pixelCount, 1u);
    queue = (sdword *)malloc(pixelCount * sizeof(sdword));
    if (pixels == NULL || luminance == NULL || visited == NULL || queue == NULL)
    {
        free(pixels);
        free(luminance);
        free(visited);
        free(queue);
        return;
    }

    while (glGetError() != GL_NO_ERROR)
    {
    }
    glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGetError() != GL_NO_ERROR)
    {
        fprintf(stderr,
                "[ModernBackdrop] Mission %02d god-ray sky analysis readback failed.\n",
                mission);
        free(pixels);
        free(luminance);
        free(visited);
        free(queue);
        return;
    }

    for (index = 0; index < (sdword)pixelCount; ++index)
    {
        const ubyte *rgba = pixels + (size_t)index * 4u;
        const real32 value = mmbGodRayLuminance(rgba);
        luminance[index] = value;
        mean += value;
        ambientRed += (real64)rgba[0] / 255.0;
        ambientGreen += (real64)rgba[1] / 255.0;
        ambientBlue += (real64)rgba[2] / 255.0;
        if (value > maximum) maximum = value;
    }
    mean /= (real64)pixelCount;
    mmbAmbientColor[0] = (real32)(ambientRed / (real64)pixelCount);
    mmbAmbientColor[1] = (real32)(ambientGreen / (real64)pixelCount);
    mmbAmbientColor[2] = (real32)(ambientBlue / (real64)pixelCount);
    for (index = 0; index < (sdword)pixelCount; ++index)
    {
        const real64 delta = (real64)luminance[index] - mean;
        variance += delta * delta;
    }
    variance /= (real64)pixelCount;
    threshold = (real32)(mean + 1.65 * sqrt(variance));
    if (threshold < 0.14f) threshold = 0.14f;
    if (threshold > 0.72f) threshold = 0.72f;
    if (maximum > 0.0f && threshold > maximum * 0.82f)
    {
        threshold = maximum * 0.82f;
    }
    minimumArea = (sdword)(pixelCount / 4096u);
    if (minimumArea < 8) minimumArea = 8;

    for (index = 0; index < (sdword)pixelCount; ++index)
    {
        sdword head = 0;
        sdword tail = 0;
        sdword area = 0;
        real64 integrated = 0.0;
        real32 componentPeak = 0.0f;
        real64 sumSin = 0.0;
        real64 sumCos = 0.0;
        real64 sumY = 0.0;
        real64 sumWeight = 0.0;
        real64 sumRed = 0.0;
        real64 sumGreen = 0.0;
        real64 sumBlue = 0.0;

        if (visited[index] || luminance[index] < threshold)
        {
            continue;
        }
        visited[index] = 1;
        queue[tail++] = index;
        while (head < tail)
        {
            const sdword current = queue[head++];
            const sdword x = current % sampleWidth;
            const sdword y = current / sampleWidth;
            const real32 value = luminance[current];
            const real64 weight = (real64)(value - threshold) + 0.02;
            const real64 u = ((real64)x + 0.5) / (real64)sampleWidth;
            const ubyte *rgba = pixels + (size_t)current * 4u;
            sdword dy;

            ++area;
            integrated += value;
            if (value > componentPeak) componentPeak = value;
            sumSin += sin(2.0 * (real64)MMB_PI * u) * weight;
            sumCos += cos(2.0 * (real64)MMB_PI * u) * weight;
            sumY += ((real64)y + 0.5) * weight;
            sumRed += ((real64)rgba[0] / 255.0) * weight;
            sumGreen += ((real64)rgba[1] / 255.0) * weight;
            sumBlue += ((real64)rgba[2] / 255.0) * weight;
            sumWeight += weight;

            for (dy = -1; dy <= 1; ++dy)
            {
                const sdword ny = y + dy;
                sdword dx;
                if (ny < 0 || ny >= sampleHeight) continue;
                for (dx = -1; dx <= 1; ++dx)
                {
                    sdword nx;
                    sdword neighbor;
                    if (dx == 0 && dy == 0) continue;
                    nx = x + dx;
                    if (nx < 0) nx += sampleWidth;
                    if (nx >= sampleWidth) nx -= sampleWidth;
                    neighbor = ny * sampleWidth + nx;
                    if (!visited[neighbor] && luminance[neighbor] >= threshold)
                    {
                        visited[neighbor] = 1;
                        queue[tail++] = neighbor;
                    }
                }
            }
        }

        if (area >= minimumArea &&
            ((mission == MISSION_2_OUTSKIRTS_OF_KHARAK_SYSTEM)
                ? (componentPeak > bestPeak ||
                   (componentPeak == bestPeak && area > bestArea))
                : (area > bestArea ||
                   (area == bestArea && integrated > bestIntegrated))))
        {
            /* Mission 2 is an intentional exception to the normal
               largest-coherent-region rule.  Its sky contains a broad,
               diffuse galactic-center patch that is larger than the actual
               blue stellar emitter at the horizontal wrap seam.  Selecting
               by peak among coherent regions on this map preserves the
               mission-owned source concept while rejecting the diffuse band. */
            bestArea = area;
            bestIntegrated = integrated;
            bestPeak = componentPeak;
            bestSin = sumSin;
            bestCos = sumCos;
            bestY = sumY;
            bestWeight = sumWeight;
            bestRed = sumRed;
            bestGreen = sumGreen;
            bestBlue = sumBlue;
        }
    }

    if (bestArea >= minimumArea && bestWeight > 0.000001)
    {
        real64 angle = atan2(bestSin, bestCos);
        real32 u;
        real32 v;
        real32 phi;
        real32 theta;
        real32 sinPhi;
        if (angle < 0.0) angle += 2.0 * (real64)MMB_PI;
        u = (real32)(angle / (2.0 * (real64)MMB_PI));
        v = (real32)((bestY / bestWeight) / (real64)sampleHeight);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        phi = MMB_PI * v;
        theta = MMB_PI * 0.5f - 2.0f * MMB_PI * u;
        sinPhi = (real32)sin(phi);
        mmbGodRaySourceDirection[0] = (real32)cos(theta) * sinPhi;
        mmbGodRaySourceDirection[1] = (real32)sin(theta) * sinPhi;
        mmbGodRaySourceDirection[2] = (real32)cos(phi);
        mmbGodRaySourceColor[0] = (real32)(bestRed / bestWeight);
        mmbGodRaySourceColor[1] = (real32)(bestGreen / bestWeight);
        mmbGodRaySourceColor[2] = (real32)(bestBlue / bestWeight);
        mmbGodRaySourceValid = TRUE;
        fprintf(stderr,
                "[ModernBackdrop] Mission %02d sky key: selected coherent bright object "
                "area=%d/%u threshold=%.3f peak=%.3f uv=(%.4f, %.4f) dir=(%.4f, %.4f, %.4f) "
                "keyRGB=(%.3f, %.3f, %.3f) ambientRGB=(%.3f, %.3f, %.3f).\n",
                mission, bestArea, (unsigned int)pixelCount, threshold, bestPeak, u, v,
                mmbGodRaySourceDirection[0], mmbGodRaySourceDirection[1],
                mmbGodRaySourceDirection[2],
                mmbGodRaySourceColor[0], mmbGodRaySourceColor[1],
                mmbGodRaySourceColor[2],
                mmbAmbientColor[0], mmbAmbientColor[1], mmbAmbientColor[2]);
    }
    else
    {
        fprintf(stderr,
                "[ModernBackdrop] Mission %02d has no coherent bright sky object large enough for god rays.\n",
                mission);
    }

    free(pixels);
    free(luminance);
    free(visited);
    free(queue);
}

static bool32 mmbLoadHdrMetadata(sdword logicalMission, sdword assetMission)
{
    char relativePath[96];
    void *fileData;
    size_t fileSize;
    char *text;
    sdword valid, area, sampleWidth, sampleHeight;
    real32 directionX, directionY, directionZ;
    real32 keyR, keyG, keyB;
    real32 ambientR, ambientG, ambientB;
    real32 threshold, peak;
    int parsed;

    snprintf(relativePath, sizeof(relativePath),
             "Missions/mission%02d_sky.hdrmeta", assetMission);
    fileData = mmbLoadAsset(relativePath, &fileSize);
    if (fileData == NULL) return FALSE;
    text = (char *)malloc(fileSize + 1u);
    if (text == NULL)
    {
        SDL_free(fileData);
        return FALSE;
    }
    memcpy(text, fileData, fileSize);
    text[fileSize] = 0;
    SDL_free(fileData);
    parsed = sscanf(text,
        "HWSKYHDR1 %d %f %f %f %f %f %f %f %f %f %f %f %d %d %d",
        &valid, &directionX, &directionY, &directionZ,
        &keyR, &keyG, &keyB, &ambientR, &ambientG, &ambientB,
        &threshold, &peak, &area, &sampleWidth, &sampleHeight);
    free(text);
    if (parsed != 15) return FALSE;

    mmbGodRaySourceMission = logicalMission;
    mmbGodRaySourceValid = valid ? TRUE : FALSE;
    mmbGodRaySourceDirection[0] = directionX;
    mmbGodRaySourceDirection[1] = directionY;
    mmbGodRaySourceDirection[2] = directionZ;
    mmbGodRaySourceColor[0] = keyR;
    mmbGodRaySourceColor[1] = keyG;
    mmbGodRaySourceColor[2] = keyB;
    mmbAmbientColor[0] = ambientR;
    mmbAmbientColor[1] = ambientG;
    mmbAmbientColor[2] = ambientB;
    fprintf(stderr,
            "[ModernBackdrop] Mission %02d HDR sky key: valid=%d area=%d/%d "
            "thresholdLinear=%.4f peakLinear=%.4f dir=(%.4f, %.4f, %.4f) "
            "keyLinear=(%.4f, %.4f, %.4f) ambientLinear=(%.4f, %.4f, %.4f).\n",
            logicalMission, valid, area, sampleWidth * sampleHeight,
            threshold, peak, directionX, directionY, directionZ,
            keyR, keyG, keyB, ambientR, ambientG, ambientB);
    return TRUE;
}

static bool32 mmbEnsureTexture(sdword mission)
{
    char relativePath[96];
    void *fileData;
    size_t fileSize;
    sdword width, height, mipCount;
    sdword assetMission = mission == MISSION_3_RETURN_TO_KHARAK ?
        MISSION_1_KHARAK_SYSTEM : mission;
    udword format;
    GLint maximumTextureSize = 0;

    if (mmbTextureMission == mission && mmbTexture != 0) return TRUE;
    if (mmbTextureAttemptMission == mission) return FALSE;

    mmbReleaseTexture();
    mmbTextureAttemptMission = mission;
    snprintf(relativePath, sizeof(relativePath),
             "Missions/mission%02d_sky.dds", assetMission);
    fileData = mmbLoadAsset(relativePath, &fileSize);
    if (fileData == NULL)
    {
        fprintf(stderr, "[ModernBackdrop] %s was not found; using legacy BTG fallback.\n",
                relativePath);
        return FALSE;
    }
    if (!mmbValidateDds((const ubyte *)fileData, fileSize,
                        &width, &height, &mipCount, &format))
    {
        SDL_free(fileData);
        fprintf(stderr,
                "[ModernBackdrop] %s is not a valid 2:1 dual-paraboloid BC6H_UF16 DDS; using legacy BTG fallback.\n",
                relativePath);
        return FALSE;
    }

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if (maximumTextureSize > 0 &&
        (width > maximumTextureSize || height > maximumTextureSize))
    {
        SDL_free(fileData);
        fprintf(stderr,
                "[ModernBackdrop] %s needs %dx%d texture support (GPU limit %d); using legacy BTG fallback.\n",
                relativePath, width, height, maximumTextureSize);
        return FALSE;
    }

    glGenTextures(1, &mmbTexture);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, mmbTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (!mmbUploadBptcDds((const ubyte *)fileData, fileSize,
                          width, height, mipCount, format))
    {
        SDL_free(fileData);
        glDeleteTextures(1, &mmbTexture);
        mmbTexture = 0;
        trClearCurrent();
        fprintf(stderr,
                "[ModernBackdrop] %s could not be uploaded; using legacy BTG fallback.\n",
                relativePath);
        return FALSE;
    }

    if (format == MMB_DXGI_FORMAT_BC6H_UF16)
    {
        if (!mmbLoadHdrMetadata(mission, assetMission))
        {
            mmbGodRaySourceValid = FALSE;
            mmbGodRaySourceMission = mission;
            fprintf(stderr,
                    "[ModernBackdrop] Mission %02d BC6H HDR metadata is missing; "
                    "sky rendering remains active but sky-key lighting is disabled.\n",
                    mission);
        }
    }
    else
    {
        mmbAnalyzeGodRaySource(mission, width, height, mipCount);
    }
    SDL_free(fileData);
    mmbTextureMission = mission;
    mmbTextureAttemptMission = 0;
    fprintf(stderr,
            format == MMB_DXGI_FORMAT_BC6H_UF16 ?
            "[ModernBackdrop] Mission %02d dual-paraboloid linear-HDR BC6H_UF16 sky loaded (%dx%d, %d mips; asset mission %02d).\n" :
            "[ModernBackdrop] Mission %02d source-faithful BC7 fallback sky loaded (%dx%d, %d mips; asset mission %02d).\n",
            mission, width, height, mipCount, assetMission);
    return TRUE;
}


bool32 modernMissionBackdropSkyLighting(real32 outDirection[3],
                                        real32 outSourceColor[3],
                                        real32 outAmbientColor[3],
                                        bool32 *outSourceValid)
{
    const sdword mission = mmbCurrentCampaignMission();
    if (!mmbEffectiveLighting(mission, outDirection, outSourceColor,
                              outAmbientColor, outSourceValid))
    {
        if (outSourceValid != NULL) *outSourceValid = FALSE;
        return FALSE;
    }
    return TRUE;
}

bool32 modernMissionBackdropGodRaySource(real32 outDirection[3])
{
    const sdword mission = mmbCurrentCampaignMission();
    bool32 sourceValid = FALSE;
    if (outDirection == NULL || mmbGodRaySourceMission != mission ||
        !mmbEffectiveLighting(mission, outDirection, NULL, NULL,
                              &sourceValid) || !sourceValid)
    {
        return FALSE;
    }
    return TRUE;
}

bool32 modernMissionBackdropLightingAuthoringGet(
    ModernMissionLightingAuthoringState *outState)
{
    const sdword mission = mmbCurrentCampaignMission();
    sdword index;
    if (outState == NULL) return FALSE;
    memset(outState, 0, sizeof(*outState));
    outState->mission = mission;
    if (mission == 0 || mmbTextureMission != mission) return FALSE;

    mmbLightingOverrideEnsure(mission);
    outState->available = TRUE;
    outState->sourceValid = mmbLightingOverride.enabled ?
        mmbLightingOverride.sourceValid : mmbGodRaySourceValid;
    outState->overrideActive = mmbLightingOverride.enabled;
    outState->loadedFromDisk = mmbLightingOverride.loadedFromDisk;
    outState->baseDirection[0] = mmbGodRaySourceDirection[0];
    outState->baseDirection[1] = mmbGodRaySourceDirection[1];
    outState->baseDirection[2] = mmbGodRaySourceDirection[2];
    outState->baseSourceColor[0] = mmbGodRaySourceColor[0];
    outState->baseSourceColor[1] = mmbGodRaySourceColor[1];
    outState->baseSourceColor[2] = mmbGodRaySourceColor[2];
    outState->baseAmbientColor[0] = mmbAmbientColor[0];
    outState->baseAmbientColor[1] = mmbAmbientColor[1];
    outState->baseAmbientColor[2] = mmbAmbientColor[2];

    if (mmbLightingOverride.enabled)
    {
        outState->direction[0] = mmbLightingOverride.direction[0];
        outState->direction[1] = mmbLightingOverride.direction[1];
        outState->direction[2] = mmbLightingOverride.direction[2];
        outState->sunIntensity = mmbLightingOverride.sunIntensity;
        outState->sourceColorScale[0] = mmbLightingOverride.sourceColorScale[0];
        outState->sourceColorScale[1] = mmbLightingOverride.sourceColorScale[1];
        outState->sourceColorScale[2] = mmbLightingOverride.sourceColorScale[2];
        outState->ambientEnabled = mmbLightingOverride.ambientEnabled;
        outState->ambientScale = mmbLightingOverride.ambientScale;
        outState->ambientColorOverride = mmbLightingOverride.ambientColorOverride;
        outState->ambientColor[0] = mmbLightingOverride.ambientColor[0];
        outState->ambientColor[1] = mmbLightingOverride.ambientColor[1];
        outState->ambientColor[2] = mmbLightingOverride.ambientColor[2];
        outState->replaceMapLights = mmbLightingOverride.replaceMapLights;
        outState->authoredLightCount = mmbLightingOverride.authoredLightCount;
        for (index = 0; index < outState->authoredLightCount; ++index)
        {
            outState->authoredLights[index] = mmbLightingOverride.authoredLights[index];
        }
    }
    else
    {
        outState->direction[0] = mmbGodRaySourceDirection[0];
        outState->direction[1] = mmbGodRaySourceDirection[1];
        outState->direction[2] = mmbGodRaySourceDirection[2];
        outState->sunIntensity = 1.0f;
        outState->sourceColorScale[0] = 1.0f;
        outState->sourceColorScale[1] = 1.0f;
        outState->sourceColorScale[2] = 1.0f;
        outState->ambientEnabled = TRUE;
        outState->ambientScale = 1.0f;
        outState->ambientColorOverride = FALSE;
        mmbDefaultAmbientTint(outState->ambientColor);
        outState->replaceMapLights = FALSE;
        outState->authoredLightCount = 0;
    }
    return TRUE;
}

bool32 modernMissionBackdropLightingAuthoringSet(
    const ModernMissionLightingAuthoringState *state)
{
    const sdword mission = mmbCurrentCampaignMission();
    sdword index;
    if (state == NULL || mission == 0 || mmbTextureMission != mission ||
        state->mission != mission)
    {
        return FALSE;
    }
    mmbLightingOverrideEnsure(mission);
    mmbLightingOverride.enabled = TRUE;
    mmbLightingOverride.sourceValid = state->sourceValid;
    mmbLightingOverride.direction[0] = state->direction[0];
    mmbLightingOverride.direction[1] = state->direction[1];
    mmbLightingOverride.direction[2] = state->direction[2];
    mmbNormalizeDirection(mmbLightingOverride.direction);
    mmbLightingOverride.sunIntensity = mmbClamp(state->sunIntensity, 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[0] = mmbClamp(state->sourceColorScale[0], 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[1] = mmbClamp(state->sourceColorScale[1], 0.0f, 4.0f);
    mmbLightingOverride.sourceColorScale[2] = mmbClamp(state->sourceColorScale[2], 0.0f, 4.0f);
    mmbLightingOverride.ambientEnabled = state->ambientEnabled ? TRUE : FALSE;
    mmbLightingOverride.ambientScale = mmbClamp(state->ambientScale, 0.0f, 4.0f);
    mmbLightingOverride.ambientColorOverride = state->ambientColorOverride ? TRUE : FALSE;
    mmbLightingOverride.ambientColor[0] = mmbClamp(state->ambientColor[0], 0.0f, 4.0f);
    mmbLightingOverride.ambientColor[1] = mmbClamp(state->ambientColor[1], 0.0f, 4.0f);
    mmbLightingOverride.ambientColor[2] = mmbClamp(state->ambientColor[2], 0.0f, 4.0f);
    mmbLightingOverride.replaceMapLights = state->replaceMapLights ? TRUE : FALSE;
    mmbLightingOverride.authoredLightCount = state->authoredLightCount;
    if (mmbLightingOverride.authoredLightCount < 0)
        mmbLightingOverride.authoredLightCount = 0;
    if (mmbLightingOverride.authoredLightCount > MODERN_MISSION_AUTHORED_LIGHT_MAX)
        mmbLightingOverride.authoredLightCount = MODERN_MISSION_AUTHORED_LIGHT_MAX;
    memset(mmbLightingOverride.authoredLights, 0,
           sizeof(mmbLightingOverride.authoredLights));
    for (index = 0; index < mmbLightingOverride.authoredLightCount; ++index)
    {
        mmbLightingOverride.authoredLights[index] = state->authoredLights[index];
        mmbSanitizeAuthoredLight(&mmbLightingOverride.authoredLights[index]);
    }
    mmbPushAuthoringLights();
    return TRUE;
}

void modernMissionBackdropLightingAuthoringReset(void)
{
    const sdword mission = mmbCurrentCampaignMission();
    if (mission == 0 || mmbTextureMission != mission) return;
    mmbLightingOverrideDefaults(mission);
    mmbPushAuthoringLights();
    fprintf(stderr,
            "[ModernBackdrop] Mission %02d live lighting reset to HDR sky metadata and custom lights cleared (not saved yet).\n",
            mission);
}

static void mmbWriteLightingAuthoringStream(FILE *stream)
{
    sdword index;
    sdword lightCount;
    if (stream == NULL) return;
    lightCount = mmbLightingOverride.authoredLightCount;
    if (lightCount < 0) lightCount = 0;
    if (lightCount > MODERN_MISSION_AUTHORED_LIGHT_MAX)
        lightCount = MODERN_MISSION_AUTHORED_LIGHT_MAX;
    fprintf(stream, "RTXMISSIONLIGHTING 3\n");
    fprintf(stream, "enabled %d\n", mmbLightingOverride.enabled ? 1 : 0);
    fprintf(stream, "sourceValid %d\n", mmbLightingOverride.sourceValid ? 1 : 0);
    fprintf(stream, "direction %.9g %.9g %.9g\n",
            mmbLightingOverride.direction[0],
            mmbLightingOverride.direction[1],
            mmbLightingOverride.direction[2]);
    fprintf(stream, "sunIntensity %.9g\n", mmbLightingOverride.sunIntensity);
    fprintf(stream, "sourceColorScale %.9g %.9g %.9g\n",
            mmbLightingOverride.sourceColorScale[0],
            mmbLightingOverride.sourceColorScale[1],
            mmbLightingOverride.sourceColorScale[2]);
    fprintf(stream, "ambientEnabled %d\n", mmbLightingOverride.ambientEnabled ? 1 : 0);
    fprintf(stream, "ambientScale %.9g\n", mmbLightingOverride.ambientScale);
    fprintf(stream, "ambientColorOverride %d\n",
            mmbLightingOverride.ambientColorOverride ? 1 : 0);
    fprintf(stream, "ambientColor %.9g %.9g %.9g\n",
            mmbLightingOverride.ambientColor[0],
            mmbLightingOverride.ambientColor[1],
            mmbLightingOverride.ambientColor[2]);
    fprintf(stream, "replaceMapLights %d\n",
            mmbLightingOverride.replaceMapLights ? 1 : 0);
    fprintf(stream, "lightCount %d\n", lightCount);
    for (index = 0; index < lightCount; ++index)
    {
        const ModernMissionAuthoredLight *light =
            &mmbLightingOverride.authoredLights[index];
        fprintf(stream,
            "light %d %d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
            index, light->type, light->enabled ? 1 : 0,
            light->position[0], light->position[1], light->position[2],
            light->direction[0], light->direction[1], light->direction[2],
            light->color[0], light->color[1], light->color[2],
            light->intensity, light->radius,
            light->coneAngle, light->edgeAngle);
    }
}

bool32 modernMissionBackdropLightingAuthoringSave(void)
{
    const sdword mission = mmbCurrentCampaignMission();
    char path[96];
    filehandle handle;
    FILE *stream;
    if (mission == 0 || mmbTextureMission != mission) return FALSE;
    mmbLightingOverrideEnsure(mission);
    mmbLightingOverridePath(path, mission);
    handle = fileOpen(path, FF_WriteMode | FF_TextMode | FF_IgnoreBIG |
                      FF_UserSettingsPath | FF_ReturnNULLOnFail);
    if (handle == 0) return FALSE;
    stream = fileStream(handle);
    if (stream == NULL)
    {
        fileClose(handle);
        return FALSE;
    }
    mmbWriteLightingAuthoringStream(stream);
    fileClose(handle);
    mmbLightingOverride.loadedFromDisk = TRUE;
    fprintf(stderr,
            "[ModernBackdrop] Mission %02d RTX lighting saved to %s (%d editable lights).\n",
            mission, path, mmbLightingOverride.authoredLightCount);
    return TRUE;
}

bool32 modernMissionBackdropLightingAuthoringExport(void)
{
    const sdword mission = mmbCurrentCampaignMission();
    char path[1024];
    char exportRoot[1024];
    char exportDirectory[1024];
    char *basePath;
    FILE *stream;
    int pathLength;
    int rootLength;
    int directoryLength;
    if (mission == 0 || mmbTextureMission != mission) return FALSE;
    mmbLightingOverrideEnsure(mission);
    basePath = SDL_GetBasePath();
    if (basePath == NULL)
    {
        fprintf(stderr, "[ModernBackdrop] Mission lighting export failed: SDL_GetBasePath returned NULL.\n");
        return FALSE;
    }
#ifdef _WIN32
    rootLength = snprintf(exportRoot, sizeof(exportRoot), "%sRTXExports", basePath);
    directoryLength = snprintf(exportDirectory, sizeof(exportDirectory),
                               "%sRTXExports\\Missions", basePath);
    pathLength = snprintf(path, sizeof(path),
                          "%sRTXExports\\Missions\\Mission%02d.rtxlight",
                          basePath, mission);
#else
    rootLength = snprintf(exportRoot, sizeof(exportRoot), "%sRTXExports", basePath);
    directoryLength = snprintf(exportDirectory, sizeof(exportDirectory),
                               "%sRTXExports/Missions", basePath);
    pathLength = snprintf(path, sizeof(path),
                          "%sRTXExports/Missions/Mission%02d.rtxlight",
                          basePath, mission);
#endif
    SDL_free(basePath);
    if (rootLength < 0 || (size_t)rootLength >= sizeof(exportRoot) ||
        directoryLength < 0 || (size_t)directoryLength >= sizeof(exportDirectory) ||
        pathLength < 0 || (size_t)pathLength >= sizeof(path))
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed: game-directory path is too long.\n",
                mission);
        return FALSE;
    }

    /* File.c's recursive destination helper is not reliable when both levels
       of a new absolute Windows path are missing.  Create RTXExports first and
       Missions second, then use native stdio for the actual portable export. */
    if (!fileMakeDirectory(exportRoot))
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed: cannot create export root %s.\n",
                mission, exportRoot);
        return FALSE;
    }
    if (!fileMakeDirectory(exportDirectory))
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed: cannot create export directory %s.\n",
                mission, exportDirectory);
        return FALSE;
    }
    stream = fopen(path, "wt");
    if (stream == NULL)
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed: cannot open %s.\n",
                mission, path);
        return FALSE;
    }
    fprintf(stream, "# Homeworld RTX per-mission lighting export. Feed this file back into the project as authored mission lighting.\n");
    fprintf(stream, "# Includes imported retail HSF local lights when replaceMapLights=1.\n");
    mmbWriteLightingAuthoringStream(stream);
    if (fflush(stream) != 0 || ferror(stream))
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed while writing %s.\n",
                mission, path);
        fclose(stream);
        return FALSE;
    }
    if (fclose(stream) != 0)
    {
        fprintf(stderr, "[ModernBackdrop] Mission %02d export failed while closing %s.\n",
                mission, path);
        return FALSE;
    }
    fprintf(stderr, "[ModernBackdrop] Mission %02d lighting exported to %s.\n",
            mission, path);
    return TRUE;
}

bool32 modernMissionBackdropLightingAuthoringAdoptMapLights(void)
{
    const sdword mission = mmbCurrentCampaignMission();
    unsigned int mapCount;
    unsigned int index;
    sdword eligible = 0;
    sdword imported = 0;
    if (mission == 0 || mmbTextureMission != mission) return FALSE;
    mmbLightingOverrideEnsure(mission);
    if (mmbLightingOverride.replaceMapLights) return TRUE;
    mapCount = hwModernGraphicsGetMapLightCount();
    for (index = 0; index < mapCount; ++index)
    {
        HWModernMapLightEmitter source;
        if (!hwModernGraphicsGetMapLight(index, &source)) continue;
        if (source.type != 0 && source.type != 5) ++eligible;
    }
    if (eligible == 0) return TRUE;
    if (mmbLightingOverride.authoredLightCount + eligible >
        MODERN_MISSION_AUTHORED_LIGHT_MAX)
    {
        fprintf(stderr,
            "[ModernBackdrop] Cannot adopt %d HSF local lights for mission %02d: authoring limit %d would be exceeded.\n",
            eligible, mission, MODERN_MISSION_AUTHORED_LIGHT_MAX);
        return FALSE;
    }
    for (index = 0; index < mapCount; ++index)
    {
        HWModernMapLightEmitter source;
        ModernMissionAuthoredLight *destination;
        if (!hwModernGraphicsGetMapLight(index, &source)) continue;
        if (source.type == 0 || source.type == 5) continue;
        destination = &mmbLightingOverride.authoredLights[
            mmbLightingOverride.authoredLightCount++];
        memset(destination, 0, sizeof(*destination));
        destination->enabled = TRUE;
        destination->type = source.type == 2 ?
            MODERN_MISSION_LIGHT_SPOT : MODERN_MISSION_LIGHT_POINT;
        destination->position[0] = source.renderPosition[0];
        destination->position[1] = source.renderPosition[1];
        destination->position[2] = source.renderPosition[2];
        destination->direction[0] = source.renderDirection[0];
        destination->direction[1] = source.renderDirection[1];
        destination->direction[2] = source.renderDirection[2];
        destination->color[0] = source.color[0];
        destination->color[1] = source.color[1];
        destination->color[2] = source.color[2];
        destination->intensity = source.intensity;
        destination->radius = 250000.0f;
        destination->coneAngle = source.coneAngle > 0.0f ? source.coneAngle : 25.0f;
        destination->edgeAngle = source.edgeAngle >= 0.0f ? source.edgeAngle : 10.0f;
        mmbSanitizeAuthoredLight(destination);
        ++imported;
    }
    if (imported > 0)
    {
        mmbLightingOverride.enabled = TRUE;
        mmbLightingOverride.replaceMapLights = TRUE;
        mmbPushAuthoringLights();
        fprintf(stderr,
            "[ModernBackdrop] Mission %02d adopted %d HSF local lights into the editable Custom Lights list.\n",
            mission, imported);
    }
    return TRUE;
}

bool32 modernMissionBackdropLightingAuthoringReload(void)
{
    const sdword mission = mmbCurrentCampaignMission();
    if (mission == 0 || mmbTextureMission != mission) return FALSE;
    memset(&mmbLightingOverride, 0, sizeof(mmbLightingOverride));
    mmbLightingOverrideLoad(mission);
    return mmbLightingOverride.loadedFromDisk;
}

bool32 modernMissionBackdropActive(void)
{
    /* "Active" also gates mission-light authoring and suppression of the
       unrelated procedural star layer.  It must remain independent from the
       decision to draw the packed sky image. */
    return mmbCurrentCampaignMission() != 0;
}

bool32 modernMissionBackdropRender(real32 fade)
{
    const sdword mission = mmbCurrentCampaignMission();

    /* RTX-0070: Temporarily prefer the original BTG renderer for campaign
       backgrounds.  The source backdrop is continuous in the game's native
       projection and retains every authored galaxy, sun and color field.  The
       generated dual-paraboloid assets currently contain world-fixed radial
       lobes which cannot be repaired by widening the runtime hemisphere blend;
       returning FALSE here selects btgRender's normal compatibility path. */
    if (mission == 0 || !mmbEnsureTexture(mission))
    {
        return FALSE;
    }

    /* Loading still initializes the HDR key/ambient metadata and editable
       per-mission light state.  Only the defective visual atlas is bypassed. */
    if (!mmbPackedSkyVisualsEnabled)
    {
        return FALSE;
    }

    if (fade < 0.0f) fade = 1.0f;
    if (fade > 1.0f) fade = 1.0f;

    /* Record one native fullscreen environment command at exactly the point
       where the legacy BTG backdrop used to be drawn.  If the native path is
       unavailable, return FALSE so the caller retains the original BTG as the
       compatibility fallback. */
    if (!hwModernGraphicsRenderMissionSky((udword)mmbTexture, fade))
    {
        return FALSE;
    }

    mmbSubmitGodRayProjection();
    return TRUE;
}
