#include "ModernAsteroid.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "ObjTypes.h"
#include "glinc.h"
#include "render.h"
#include "texreg.h"

#ifdef HW_ENABLE_D3D12_BACKEND
#include "ModernGraphics.h"
#endif

#define MASTEROID_TYPE_COUNT 4
#define MASTEROID_VARIANTS 12
#define MASTEROID_LODS 4
#define MASTEROID_PI 3.14159265358979323846f

/* Close-up geometry is intentionally much denser than the original resource
   PEOs, while distance LODs keep asteroid fields practical. */
/* One close-up subdivision pass over the accepted modern LOD0: doubling both
   parametric axes splits every previous surface quad into four while leaving
   the distance LODs unchanged for large asteroid-field performance. */
static const sdword modernAsteroidSlices[MASTEROID_LODS] = { 192, 64, 36, 18 };
static const sdword modernAsteroidStacks[MASTEROID_LODS] = { 96, 32, 18, 9 };

typedef struct ModernAsteroidVertex
{
    GLfloat position[3];
    GLfloat normal[3];
    GLfloat uv[2];
} ModernAsteroidVertex;

typedef struct ModernAsteroidMesh
{
    ModernAsteroidVertex *vertices;
    uword *indices;
    sdword indexCount;
    sdword vertexCount;
} ModernAsteroidMesh;

typedef struct ModernAsteroidTypeData
{
    GLuint texture;
    bool32 textureAttempted;
    bool32 materialRegistered;
    ModernAsteroidMesh mesh[MASTEROID_VARIANTS][MASTEROID_LODS];
} ModernAsteroidTypeData;

static ModernAsteroidTypeData modernAsteroidTypes[MASTEROID_TYPE_COUNT];
static const char *modernAsteroidAssetPath[MASTEROID_TYPE_COUNT] =
{
    "Asteroids/asteroid1_basecolor.dds",
    "Asteroids/asteroid2_basecolor.dds",
    "Asteroids/asteroid3_basecolor.dds",
    "Asteroids/asteroid4_basecolor.dds"
};
static const char *modernAsteroidNormalPath[MASTEROID_TYPE_COUNT] =
{
    "Asteroids/asteroid1_normal.dds", "Asteroids/asteroid2_normal.dds",
    "Asteroids/asteroid3_normal.dds", "Asteroids/asteroid4_normal.dds"
};
static const char *modernAsteroidOrmPath[MASTEROID_TYPE_COUNT] =
{
    "Asteroids/asteroid1_orm.dds", "Asteroids/asteroid2_orm.dds",
    "Asteroids/asteroid3_orm.dds", "Asteroids/asteroid4_orm.dds"
};
static const char *modernAsteroidMeshPath[MASTEROID_TYPE_COUNT][MASTEROID_LODS] =
{
    {"Asteroids/asteroid1.obj", "Asteroids/asteroid1_lod1.obj", "Asteroids/asteroid1_lod2.obj", "Asteroids/asteroid1_lod3.obj"},
    {"Asteroids/asteroid2.obj", "Asteroids/asteroid2_lod1.obj", "Asteroids/asteroid2_lod2.obj", "Asteroids/asteroid2_lod3.obj"},
    {"Asteroids/asteroid3.obj", "Asteroids/asteroid3_lod1.obj", "Asteroids/asteroid3_lod2.obj", "Asteroids/asteroid3_lod3.obj"},
    {"Asteroids/asteroid4.obj", "Asteroids/asteroid4_lod1.obj", "Asteroids/asteroid4_lod2.obj", "Asteroids/asteroid4_lod3.obj"}
};
static const char *modernAsteroidDebugName[MASTEROID_TYPE_COUNT] =
{
    "Asteroid1", "Asteroid2", "Asteroid3", "Asteroid4"
};

/* Deterministic fixed-function rock materials. The albedo texture still owns
   the detailed palette, but these warm base coefficients prevent the custom
   draw from becoming a white object if legacy color-material state or strong
   scene lighting would otherwise dominate it. */
static const GLfloat modernAsteroidMaterialDiffuse[MASTEROID_TYPE_COUNT][4] =
{
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}
};
static const GLfloat modernAsteroidMaterialAmbient[MASTEROID_TYPE_COUNT][4] =
{
    {0.22f, 0.22f, 0.22f, 1.0f}, {0.22f, 0.22f, 0.22f, 1.0f},
    {0.22f, 0.22f, 0.22f, 1.0f}, {0.22f, 0.22f, 0.22f, 1.0f}
};

/* Base type character only. Per-resource variants below deliberately apply
   much stronger axis ratios and asymmetric low-frequency mass so asteroid
   fields no longer read as a collection of balls. The finished mesh is still
   normalized to the original collision-sphere radius, preserving gameplay. */
static const real32 modernAsteroidAxisScale[MASTEROID_TYPE_COUNT][3] =
{
    { 1.00f, 0.86f, 0.93f },
    { 0.88f, 1.00f, 0.79f },
    { 1.00f, 0.76f, 0.88f },
    { 0.87f, 0.94f, 1.00f }
};

typedef struct ModernAsteroidCrater
{
    real32 direction[3];
    real32 cosRadius;
    real32 depth;
    real32 rim;
} ModernAsteroidCrater;

/* Fixed object-space crater layout. Object rotation still comes from the
   original Homeworld asteroid transform, and shape variants alter the noise
   field so repeated resources do not look stamped. */
static const ModernAsteroidCrater modernAsteroidCraters[MASTEROID_TYPE_COUNT][5] =
{
    {
        {{ 0.77f, 0.41f, 0.49f }, 0.9780f, 0.050f, 0.010f},
        {{-0.38f, 0.84f, 0.39f }, 0.9860f, 0.038f, 0.008f},
        {{ 0.15f,-0.44f, 0.89f }, 0.9900f, 0.030f, 0.006f},
        {{-0.68f,-0.26f,-0.68f }, 0.9925f, 0.024f, 0.005f},
        {{ 0.52f,-0.82f,-0.23f }, 0.9940f, 0.020f, 0.004f}
    },
    {
        {{-0.64f, 0.33f, 0.69f }, 0.9740f, 0.060f, 0.012f},
        {{ 0.44f, 0.88f,-0.16f }, 0.9830f, 0.045f, 0.009f},
        {{ 0.72f,-0.47f, 0.50f }, 0.9880f, 0.036f, 0.007f},
        {{-0.17f,-0.94f, 0.29f }, 0.9910f, 0.029f, 0.006f},
        {{-0.84f,-0.04f,-0.54f }, 0.9940f, 0.022f, 0.004f}
    },
    {
        {{ 0.31f, 0.28f, 0.91f }, 0.9690f, 0.072f, 0.014f},
        {{-0.80f, 0.54f, 0.26f }, 0.9790f, 0.052f, 0.010f},
        {{ 0.66f, 0.71f,-0.23f }, 0.9850f, 0.043f, 0.009f},
        {{-0.41f,-0.50f, 0.76f }, 0.9890f, 0.035f, 0.007f},
        {{ 0.07f,-0.93f,-0.36f }, 0.9930f, 0.025f, 0.005f}
    },
    {
        {{-0.25f, 0.61f, 0.75f }, 0.9660f, 0.078f, 0.015f},
        {{ 0.79f, 0.16f, 0.59f }, 0.9760f, 0.060f, 0.012f},
        {{-0.69f,-0.58f, 0.43f }, 0.9830f, 0.050f, 0.010f},
        {{ 0.40f,-0.84f,-0.36f }, 0.9880f, 0.039f, 0.008f},
        {{-0.91f, 0.10f,-0.40f }, 0.9920f, 0.030f, 0.006f}
    }
};

static real32 modernAsteroidClamp(real32 value, real32 lo, real32 hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static real32 modernAsteroidLerp(real32 a, real32 b, real32 t)
{
    return a + (b - a) * t;
}

static real32 modernAsteroidSmooth(real32 t)
{
    return t * t * (3.0f - 2.0f * t);
}

static udword modernAsteroidHash(udword x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static real32 modernAsteroidHash3(sdword x, sdword y, sdword z, udword seed)
{
    udword h = seed;
    h ^= modernAsteroidHash((udword)x + 0x9e3779b9u);
    h ^= modernAsteroidHash((udword)y + 0x85ebca6bu);
    h ^= modernAsteroidHash((udword)z + 0xc2b2ae35u);
    h = modernAsteroidHash(h);
    return ((real32)(h & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

static real32 modernAsteroidValueNoise(real32 x, real32 y, real32 z, udword seed)
{
    sdword x0 = (sdword)floor((double)x);
    sdword y0 = (sdword)floor((double)y);
    sdword z0 = (sdword)floor((double)z);
    real32 tx = modernAsteroidSmooth(x - (real32)x0);
    real32 ty = modernAsteroidSmooth(y - (real32)y0);
    real32 tz = modernAsteroidSmooth(z - (real32)z0);
    real32 c000 = modernAsteroidHash3(x0,     y0,     z0,     seed);
    real32 c100 = modernAsteroidHash3(x0 + 1, y0,     z0,     seed);
    real32 c010 = modernAsteroidHash3(x0,     y0 + 1, z0,     seed);
    real32 c110 = modernAsteroidHash3(x0 + 1, y0 + 1, z0,     seed);
    real32 c001 = modernAsteroidHash3(x0,     y0,     z0 + 1, seed);
    real32 c101 = modernAsteroidHash3(x0 + 1, y0,     z0 + 1, seed);
    real32 c011 = modernAsteroidHash3(x0,     y0 + 1, z0 + 1, seed);
    real32 c111 = modernAsteroidHash3(x0 + 1, y0 + 1, z0 + 1, seed);
    real32 x00 = modernAsteroidLerp(c000, c100, tx);
    real32 x10 = modernAsteroidLerp(c010, c110, tx);
    real32 x01 = modernAsteroidLerp(c001, c101, tx);
    real32 x11 = modernAsteroidLerp(c011, c111, tx);
    real32 y0v = modernAsteroidLerp(x00, x10, ty);
    real32 y1v = modernAsteroidLerp(x01, x11, ty);
    return modernAsteroidLerp(y0v, y1v, tz);
}

static real32 modernAsteroidFbm(real32 x, real32 y, real32 z, udword seed, sdword octaves)
{
    real32 sum = 0.0f;
    real32 amplitude = 0.55f;
    real32 total = 0.0f;
    sdword i;

    for (i = 0; i < octaves; ++i)
    {
        sum += modernAsteroidValueNoise(x, y, z, seed + (udword)i * 0x9e3779b9u) * amplitude;
        total += amplitude;
        x = x * 2.03f + 7.13f;
        y = y * 2.03f - 3.71f;
        z = z * 2.03f + 5.29f;
        amplitude *= 0.50f;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

static void modernAsteroidNormalize3(real32 v[3])
{
    real32 length = (real32)sqrt((double)(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]));
    if (length > 0.000001f)
    {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

static real32 modernAsteroidCraterDelta(const real32 direction[3], sdword typeIndex)
{
    real32 delta = 0.0f;
    sdword i;
    for (i = 0; i < 5; ++i)
    {
        const ModernAsteroidCrater *crater = &modernAsteroidCraters[typeIndex][i];
        real32 craterDir[3] = { crater->direction[0], crater->direction[1], crater->direction[2] };
        real32 dot;
        real32 x;
        real32 bowl;
        real32 rimX;
        real32 rim;
        modernAsteroidNormalize3(craterDir);
        dot = direction[0]*craterDir[0] + direction[1]*craterDir[1] + direction[2]*craterDir[2];
        if (dot <= crater->cosRadius) continue;
        x = (dot - crater->cosRadius) / (1.0f - crater->cosRadius);
        x = modernAsteroidClamp(x, 0.0f, 1.0f);
        bowl = x*x*(3.0f - 2.0f*x);
        rimX = (x - 0.12f) / 0.075f;
        rim = (real32)exp((double)(-rimX * rimX));
        delta -= crater->depth * bowl;
        delta += crater->rim * rim;
    }
    return delta;
}

static real32 modernAsteroidSignedHash(udword seed)
{
    return ((real32)(modernAsteroidHash(seed) & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

static void modernAsteroidVariantAxisScale(sdword typeIndex, sdword variant,
                                           real32 outAxis[3])
{
    udword seed = 0x8f3a2d71u ^ (udword)(typeIndex + 1) * 0x9e3779b9u ^
                  (udword)(variant + 1) * 0x85ebca6bu;
    sdword family = variant % 6;
    sdword dominant = (sdword)(modernAsteroidHash(seed) % 3u);
    sdword secondary = (dominant + 1 + (sdword)(modernAsteroidHash(seed ^ 0x51ed270bu) & 1u)) % 3;
    sdword tertiary = 3 - dominant - secondary;
    real32 axis[3] = {1.0f, 1.0f, 1.0f};
    real32 a = 0.5f + 0.5f * modernAsteroidSignedHash(seed ^ 0x243f6a88u);
    real32 b = 0.5f + 0.5f * modernAsteroidSignedHash(seed ^ 0xb7e15162u);
    sdword i;

    switch (family)
    {
        case 0: /* long / shard-like */
            axis[dominant] = 1.00f;
            axis[secondary] = 0.58f + 0.12f * a;
            axis[tertiary] = 0.70f + 0.12f * b;
            break;
        case 1: /* flattened slab */
            axis[dominant] = 0.48f + 0.10f * a;
            axis[secondary] = 1.00f;
            axis[tertiary] = 0.84f + 0.10f * b;
            break;
        case 2: /* chunky triaxial */
            axis[dominant] = 1.00f;
            axis[secondary] = 0.72f + 0.12f * a;
            axis[tertiary] = 0.78f + 0.14f * b;
            break;
        case 3: /* cigar / spindle */
            axis[dominant] = 1.00f;
            axis[secondary] = 0.54f + 0.10f * a;
            axis[tertiary] = 0.55f + 0.12f * b;
            break;
        case 4: /* broad asymmetric boulder */
            axis[dominant] = 0.88f + 0.08f * a;
            axis[secondary] = 1.00f;
            axis[tertiary] = 0.66f + 0.12f * b;
            break;
        default: /* less extreme but visibly non-spherical */
            axis[dominant] = 1.00f;
            axis[secondary] = 0.76f + 0.12f * a;
            axis[tertiary] = 0.68f + 0.16f * b;
            break;
    }

    for (i = 0; i < 3; ++i)
    {
        real32 jitter = 1.0f + modernAsteroidSignedHash(seed + (udword)i * 0x27d4eb2du) * 0.055f;
        outAxis[i] = modernAsteroidAxisScale[typeIndex][i] * axis[i] * jitter;
        outAxis[i] = modernAsteroidClamp(outAxis[i], 0.42f, 1.0f);
    }
}

static real32 modernAsteroidRadiusField(const real32 direction[3], sdword typeIndex, sdword variant)
{
    udword seed = 0x4a53f00du + (udword)(typeIndex + 1) * 0x1f123bb5u + (udword)variant * 0x6a09e667u;
    real32 large = modernAsteroidFbm(direction[0]*1.55f + 11.0f,
                                     direction[1]*1.55f - 7.0f,
                                     direction[2]*1.55f + 3.0f,
                                     seed, 4);
    real32 medium = modernAsteroidFbm(direction[0]*5.2f - 5.0f,
                                      direction[1]*5.2f + 13.0f,
                                      direction[2]*5.2f - 9.0f,
                                      seed ^ 0xa5a5a5a5u, 3);
    real32 fine = modernAsteroidFbm(direction[0]*15.0f + 19.0f,
                                    direction[1]*15.0f - 17.0f,
                                    direction[2]*15.0f + 23.0f,
                                    seed ^ 0x3c6ef372u, 2);
    real32 biasDirection[3] = {
        modernAsteroidSignedHash(seed ^ 0x31415926u),
        modernAsteroidSignedHash(seed ^ 0x27182818u),
        modernAsteroidSignedHash(seed ^ 0x16180339u)
    };
    real32 ridgeDirection[3] = {
        modernAsteroidSignedHash(seed ^ 0x6a09e667u),
        modernAsteroidSignedHash(seed ^ 0xbb67ae85u),
        modernAsteroidSignedHash(seed ^ 0x3c6ef372u)
    };
    real32 familyStrength = (variant % 6 == 4) ? 0.115f : 0.070f;
    real32 asymmetric;
    real32 ridge;
    real32 radius;

    modernAsteroidNormalize3(biasDirection);
    modernAsteroidNormalize3(ridgeDirection);
    asymmetric = direction[0]*biasDirection[0] +
                 direction[1]*biasDirection[1] +
                 direction[2]*biasDirection[2];
    ridge = (real32)fabs(direction[0]*ridgeDirection[0] +
                         direction[1]*ridgeDirection[1] +
                         direction[2]*ridgeDirection[2]);

    radius = 0.875f + large*0.105f + medium*0.048f + fine*0.016f;
    radius += asymmetric * familyStrength;
    radius += (ridge - 0.50f) * 0.055f;
    radius += modernAsteroidCraterDelta(direction, typeIndex);
    return modernAsteroidClamp(radius, 0.58f, 1.08f);
}

static void modernAsteroidPositionForDirection(const real32 direction[3], sdword typeIndex,
                                                sdword variant, real32 position[3])
{
    real32 variantAxis[3];
    real32 radius = modernAsteroidRadiusField(direction, typeIndex, variant);
    modernAsteroidVariantAxisScale(typeIndex, variant, variantAxis);
    position[0] = direction[0] * variantAxis[0] * radius;
    position[1] = direction[1] * variantAxis[1] * radius;
    position[2] = direction[2] * variantAxis[2] * radius;
}

static void modernAsteroidCross(const real32 a[3], const real32 b[3], real32 out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void modernAsteroidSubtract(const real32 a[3], const real32 b[3], real32 out[3])
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void *modernAsteroidLoadAsset(const char *relativePath, size_t *fileSize)
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
            snprintf(path, sizeof(path), "%s../../../../assets/%s", basePath, relativePath);
            data = SDL_LoadFile(path, fileSize);
        }
        SDL_free(basePath);
    }
    if (data == NULL)
    {
        snprintf(path, sizeof(path), "assets/%s", relativePath);
        data = SDL_LoadFile(path, fileSize);
    }
    if (data == NULL) data = SDL_LoadFile(relativePath, fileSize);
    return data;
}

static bool32 modernAsteroidEnsureTexture(sdword typeIndex)
{
    ModernAsteroidTypeData *type;
    void *baseData = NULL, *normalData = NULL, *ormData = NULL;
    size_t baseSize = 0, normalSize = 0, ormSize = 0;
    unsigned char *basePixels = NULL, *normalPixels = NULL, *ormPixels = NULL;
    unsigned int width = 0, height = 0, nw = 0, nh = 0, ow = 0, oh = 0;
    unsigned int *emissive = NULL;

    if (typeIndex < 0 || typeIndex >= MASTEROID_TYPE_COUNT) return FALSE;
    type = &modernAsteroidTypes[typeIndex];
    if (type->textureAttempted) return type->texture != 0;
    type->textureAttempted = TRUE;

    baseData = modernAsteroidLoadAsset(modernAsteroidAssetPath[typeIndex], &baseSize);
    normalData = modernAsteroidLoadAsset(modernAsteroidNormalPath[typeIndex], &normalSize);
    ormData = modernAsteroidLoadAsset(modernAsteroidOrmPath[typeIndex], &ormSize);
    if (baseData == NULL || normalData == NULL || ormData == NULL)
    {
        fprintf(stderr, "[ModernAsteroid] %s PBR DDS set incomplete; using legacy PEO.\n",
                modernAsteroidDebugName[typeIndex]);
        if (baseData) SDL_free(baseData);
        if (normalData) SDL_free(normalData);
        if (ormData) SDL_free(ormData);
        return FALSE;
    }
#ifdef HW_ENABLE_D3D12_BACKEND
    if (!hwModernGraphicsDecodeDds(baseData, (unsigned int)baseSize,
                                   &width, &height, &basePixels) ||
        !hwModernGraphicsDecodeDds(normalData, (unsigned int)normalSize,
                                   &nw, &nh, &normalPixels) ||
        !hwModernGraphicsDecodeDds(ormData, (unsigned int)ormSize,
                                   &ow, &oh, &ormPixels) ||
        width != nw || height != nh || width != ow || height != oh)
    {
        fprintf(stderr, "[ModernAsteroid] %s PBR DDS decode/size mismatch; using legacy PEO.\n",
                modernAsteroidDebugName[typeIndex]);
        if (basePixels) hwModernGraphicsFreeDecodedDds(basePixels);
        if (normalPixels) hwModernGraphicsFreeDecodedDds(normalPixels);
        if (ormPixels) hwModernGraphicsFreeDecodedDds(ormPixels);
        SDL_free(baseData); SDL_free(normalData); SDL_free(ormData);
        return FALSE;
    }
#else
    SDL_free(baseData); SDL_free(normalData); SDL_free(ormData);
    return FALSE;
#endif

    glGenTextures(1, &type->texture);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, type->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    glGenerateMipmap != NULL ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    if (!hwModernGraphicsUploadBoundDds(baseData, (unsigned int)baseSize))
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, basePixels);
        if (glGenerateMipmap != NULL) glGenerateMipmap(GL_TEXTURE_2D);
    }
    emissive = (unsigned int *)calloc((size_t)width * height,
                                      sizeof(unsigned int));
    if (emissive != NULL)
    {
        hwModernGraphicsRegisterSurfaceTexture(
            type, width, height, (const unsigned int *)basePixels, emissive,
            (const unsigned int *)normalPixels, (const unsigned int *)ormPixels);
        type->materialRegistered = TRUE;
        free(emissive);
    }
    hwModernGraphicsFreeDecodedDds(basePixels);
    hwModernGraphicsFreeDecodedDds(normalPixels);
    hwModernGraphicsFreeDecodedDds(ormPixels);
    SDL_free(baseData); SDL_free(normalData); SDL_free(ormData);
    fprintf(stderr, "[ModernAsteroid] %s authored PBR set loaded (%ux%u BC7/BC5/BC7 DDS).\n",
            modernAsteroidDebugName[typeIndex], width, height);
    return TRUE;
}

/* Loads the authored OBJ as an indexed triangle list. We intentionally expand
   OBJ position/UV/normal triplets because its independent indices cannot be
   represented by Homeworld's single 16-bit element stream otherwise. */
static bool32 modernAsteroidEnsureImportedMesh(sdword typeIndex, sdword lod)
{
    ModernAsteroidMesh *mesh;
    void *fileData;
    size_t fileSize;
    char *text, *line, *context;
    sdword positions = 0, normals = 0, texcoords = 0, faces = 0;
    real32 *p = NULL, *n = NULL, *t = NULL;
    sdword pi = 0, ni = 0, ti = 0, face = 0;
    real32 mins[3] = {1.0e30f, 1.0e30f, 1.0e30f};
    real32 maxs[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
    real32 center[3], maximumRadius = 0.0f;
    sdword i;

    if (typeIndex < 0 || typeIndex >= MASTEROID_TYPE_COUNT) return FALSE;
    if (lod < 0 || lod >= MASTEROID_LODS) return FALSE;
    mesh = &modernAsteroidTypes[typeIndex].mesh[0][lod];
    if (mesh->vertices != NULL && mesh->indices != NULL) return TRUE;
    fileData = modernAsteroidLoadAsset(modernAsteroidMeshPath[typeIndex][lod], &fileSize);
    if (fileData == NULL || fileSize == 0) return FALSE;
    text = (char *)malloc(fileSize + 1);
    if (!text) { SDL_free(fileData); return FALSE; }
    memcpy(text, fileData, fileSize); text[fileSize] = 0;
    SDL_free(fileData);

    context = NULL;
    for (line = strtok_s(text, "\r\n", &context); line;
         line = strtok_s(NULL, "\r\n", &context))
    {
        if (line[0] == 'v' && line[1] == ' ') ++positions;
        else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') ++normals;
        else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') ++texcoords;
        else if (line[0] == 'f' && line[1] == ' ') ++faces;
    }
    if (positions <= 0 || normals <= 0 || texcoords <= 0 || faces <= 0 ||
        faces * 3 > 65535) { free(text); return FALSE; }
    p = (real32 *)malloc((size_t)positions * 3 * sizeof(real32));
    n = (real32 *)malloc((size_t)normals * 3 * sizeof(real32));
    t = (real32 *)malloc((size_t)texcoords * 2 * sizeof(real32));
    mesh->vertices = (ModernAsteroidVertex *)calloc((size_t)faces * 3,
                                                     sizeof(ModernAsteroidVertex));
    mesh->indices = (uword *)malloc((size_t)faces * 3 * sizeof(uword));
    if (!p || !n || !t || !mesh->vertices || !mesh->indices) goto failed;

    free(text);
    text = NULL;
    fileData = modernAsteroidLoadAsset(modernAsteroidMeshPath[typeIndex][lod], &fileSize);
    if (!fileData) goto failed;
    text = (char *)malloc(fileSize + 1);
    if (!text) { SDL_free(fileData); goto failed; }
    memcpy(text, fileData, fileSize); text[fileSize] = 0; SDL_free(fileData);
    context = NULL;
    for (line = strtok_s(text, "\r\n", &context); line;
         line = strtok_s(NULL, "\r\n", &context))
    {
        if (line[0] == 'v' && line[1] == ' ')
        {
            if (sscanf(line + 2, "%f %f %f", &p[pi*3], &p[pi*3+1], &p[pi*3+2]) == 3) ++pi;
        }
        else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ')
        {
            if (sscanf(line + 3, "%f %f %f", &n[ni*3], &n[ni*3+1], &n[ni*3+2]) == 3) ++ni;
        }
        else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ')
        {
            if (sscanf(line + 3, "%f %f", &t[ti*2], &t[ti*2+1]) == 2) ++ti;
        }
        else if (line[0] == 'f' && line[1] == ' ')
        {
            int pv[3], tv[3], nv[3], corner;
            if (sscanf(line + 2, "%d/%d/%d %d/%d/%d %d/%d/%d",
                       &pv[0], &tv[0], &nv[0], &pv[1], &tv[1], &nv[1],
                       &pv[2], &tv[2], &nv[2]) != 9) goto failed;
            for (corner = 0; corner < 3; ++corner)
            {
                ModernAsteroidVertex *v = &mesh->vertices[face * 3 + corner];
                int pp = pv[corner] - 1, tt = tv[corner] - 1, nn = nv[corner] - 1;
                if (pp < 0 || pp >= pi || tt < 0 || tt >= ti || nn < 0 || nn >= ni) goto failed;
                memcpy(v->position, &p[pp*3], 3*sizeof(real32));
                memcpy(v->normal, &n[nn*3], 3*sizeof(real32));
                modernAsteroidNormalize3(v->normal);
                v->uv[0] = t[tt*2];
                v->uv[1] = 1.0f - t[tt*2+1];
                mesh->indices[face * 3 + corner] = (uword)(face * 3 + corner);
                for (i = 0; i < 3; ++i)
                {
                    if (v->position[i] < mins[i]) mins[i] = v->position[i];
                    if (v->position[i] > maxs[i]) maxs[i] = v->position[i];
                }
            }
            ++face;
        }
    }
    for (i = 0; i < 3; ++i) center[i] = (mins[i] + maxs[i]) * 0.5f;
    for (i = 0; i < face * 3; ++i)
    {
        real32 length;
        mesh->vertices[i].position[0] -= center[0];
        mesh->vertices[i].position[1] -= center[1];
        mesh->vertices[i].position[2] -= center[2];
        length = (real32)sqrt((double)(mesh->vertices[i].position[0]*mesh->vertices[i].position[0] +
                                      mesh->vertices[i].position[1]*mesh->vertices[i].position[1] +
                                      mesh->vertices[i].position[2]*mesh->vertices[i].position[2]));
        if (length > maximumRadius) maximumRadius = length;
    }
    if (maximumRadius <= 0.000001f) goto failed;
    for (i = 0; i < face * 3; ++i)
    {
        mesh->vertices[i].position[0] /= maximumRadius;
        mesh->vertices[i].position[1] /= maximumRadius;
        mesh->vertices[i].position[2] /= maximumRadius;
    }
    mesh->vertexCount = face * 3;
    mesh->indexCount = face * 3;
    free(text); free(p); free(n); free(t);
    fprintf(stderr, "[ModernAsteroid] %s authored LOD%d OBJ loaded: %d vertices / %d triangles.\n",
            modernAsteroidDebugName[typeIndex], lod, mesh->vertexCount, face);
    return TRUE;
failed:
    if (text) free(text); free(p); free(n); free(t);
    free(mesh->vertices); free(mesh->indices);
    mesh->vertices = NULL; mesh->indices = NULL;
    return FALSE;
}

static bool32 modernAsteroidEnsureMesh(sdword typeIndex, sdword variant, sdword lod)
{
    ModernAsteroidMesh *mesh;
    sdword slices;
    sdword stacks;
    sdword vertexCount;
    sdword triangleCount;
    sdword row, column, index;
    real32 maxLength = 0.0f;

    if (typeIndex < 0 || typeIndex >= MASTEROID_TYPE_COUNT) return FALSE;
    if (variant < 0 || variant >= MASTEROID_VARIANTS) return FALSE;
    if (lod < 0) lod = 0;
    if (lod >= MASTEROID_LODS) lod = MASTEROID_LODS - 1;

    mesh = &modernAsteroidTypes[typeIndex].mesh[variant][lod];
    if (mesh->vertices != NULL && mesh->indices != NULL) return TRUE;

    slices = modernAsteroidSlices[lod];
    stacks = modernAsteroidStacks[lod];
    vertexCount = (stacks + 1) * (slices + 1);
    triangleCount = stacks * slices * 2;

    mesh->vertices = (ModernAsteroidVertex *)malloc(sizeof(ModernAsteroidVertex) * vertexCount);
    mesh->indices = (uword *)malloc(sizeof(uword) * triangleCount * 3);
    if (mesh->vertices == NULL || mesh->indices == NULL)
    {
        free(mesh->vertices);
        free(mesh->indices);
        mesh->vertices = NULL;
        mesh->indices = NULL;
        return FALSE;
    }
    memset(mesh->vertices, 0, sizeof(ModernAsteroidVertex) * vertexCount);

    for (row = 0; row <= stacks; ++row)
    {
        real32 v = (real32)row / (real32)stacks;
        real32 phi = v * MASTEROID_PI;
        real32 sinPhi = (real32)sin((double)phi);
        real32 cosPhi = (real32)cos((double)phi);
        for (column = 0; column <= slices; ++column)
        {
            real32 u = (real32)column / (real32)slices;
            real32 theta = u * 2.0f * MASTEROID_PI - MASTEROID_PI;
            real32 direction[3];
            real32 position[3];
            real32 length;
            ModernAsteroidVertex *vertex = &mesh->vertices[row * (slices + 1) + column];

            direction[0] = (real32)cos((double)theta) * sinPhi;
            direction[1] = cosPhi;
            direction[2] = (real32)sin((double)theta) * sinPhi;
            modernAsteroidPositionForDirection(direction, typeIndex, variant, position);
            vertex->position[0] = position[0];
            vertex->position[1] = position[1];
            vertex->position[2] = position[2];
            vertex->uv[0] = u;
            vertex->uv[1] = v;

            length = (real32)sqrt((double)(position[0]*position[0] + position[1]*position[1] + position[2]*position[2]));
            if (length > maxLength) maxLength = length;
        }
    }

    /* Exact-size invariant: the visual mesh's outermost point is the original
       asteroid collision sphere. Harvest shrink continues to happen through
       the existing caller's glScalef(). */
    if (maxLength > 0.000001f)
    {
        real32 inv = 1.0f / maxLength;
        sdword vtx;
        for (vtx = 0; vtx < vertexCount; ++vtx)
        {
            mesh->vertices[vtx].position[0] *= inv;
            mesh->vertices[vtx].position[1] *= inv;
            mesh->vertices[vtx].position[2] *= inv;
        }
    }

    index = 0;
    for (row = 0; row < stacks; ++row)
    {
        for (column = 0; column < slices; ++column)
        {
            uword a = (uword)(row * (slices + 1) + column);
            uword b = (uword)(a + 1);
            uword c = (uword)(a + slices + 1);
            uword d = (uword)(c + 1);
            mesh->indices[index++] = a;
            mesh->indices[index++] = b;
            mesh->indices[index++] = c;
            mesh->indices[index++] = b;
            mesh->indices[index++] = d;
            mesh->indices[index++] = c;
        }
    }

    /* Smooth geometry normals from neighboring displaced vertices. This keeps
       the crater/ridge relief in the real geometry, so the existing raster,
       ray-traced and path-traced lighting all see the modern silhouette. */
    for (row = 0; row <= stacks; ++row)
    {
        for (column = 0; column <= slices; ++column)
        {
            ModernAsteroidVertex *vertex = &mesh->vertices[row * (slices + 1) + column];
            sdword c0 = column == slices ? 0 : column;
            sdword left = (c0 + slices - 1) % slices;
            sdword right = (c0 + 1) % slices;
            sdword down = row > 0 ? row - 1 : row;
            sdword up = row < stacks ? row + 1 : row;
            const real32 *pLeft = mesh->vertices[row * (slices + 1) + left].position;
            const real32 *pRight = mesh->vertices[row * (slices + 1) + right].position;
            const real32 *pDown = mesh->vertices[down * (slices + 1) + c0].position;
            const real32 *pUp = mesh->vertices[up * (slices + 1) + c0].position;
            real32 tangentU[3];
            real32 tangentV[3];
            real32 normal[3];
            real32 outward;

            if (row == 0 || row == stacks)
            {
                normal[0] = vertex->position[0];
                normal[1] = vertex->position[1];
                normal[2] = vertex->position[2];
                modernAsteroidNormalize3(normal);
            }
            else
            {
                modernAsteroidSubtract(pRight, pLeft, tangentU);
                modernAsteroidSubtract(pUp, pDown, tangentV);
                modernAsteroidCross(tangentV, tangentU, normal);
                modernAsteroidNormalize3(normal);
                outward = normal[0]*vertex->position[0] + normal[1]*vertex->position[1] + normal[2]*vertex->position[2];
                if (outward < 0.0f)
                {
                    normal[0] = -normal[0];
                    normal[1] = -normal[1];
                    normal[2] = -normal[2];
                }
            }
            vertex->normal[0] = normal[0];
            vertex->normal[1] = normal[1];
            vertex->normal[2] = normal[2];
        }
    }

    /* Copy the first seam normal to the duplicated final longitude vertex. */
    for (row = 0; row <= stacks; ++row)
    {
        ModernAsteroidVertex *first = &mesh->vertices[row * (slices + 1)];
        ModernAsteroidVertex *last = &mesh->vertices[row * (slices + 1) + slices];
        last->normal[0] = first->normal[0];
        last->normal[1] = first->normal[1];
        last->normal[2] = first->normal[2];
    }

    mesh->indexCount = index;
    mesh->vertexCount = vertexCount;
    fprintf(stderr, "[ModernAsteroid] %s variant %d LOD%d generated: %d vertices / %d triangles.\n",
            modernAsteroidDebugName[typeIndex], variant, lod,
            vertexCount, index / 3);
    return TRUE;
}

static sdword modernAsteroidTypeIndex(const Asteroid *asteroid)
{
    if (asteroid == NULL) return -1;
    switch (asteroid->asteroidtype)
    {
        case Asteroid1: return 0;
        case Asteroid2: return 1;
        case Asteroid3: return 2;
        case Asteroid4: return 3;
        default: return -1;
    }
}


static sdword modernAsteroidChooseLod(const Asteroid *asteroid, real32 radius)
{
    real32 distance;
    real32 apparentRadius;

    if (asteroid == NULL || radius <= 0.0f) return MASTEROID_LODS - 1;
    distance = (real32)sqrt((double)asteroid->cameraDistanceSquared);
    if (distance <= radius * 1.25f) return 0;
    apparentRadius = (radius * asteroid->scaling) / distance;
    if (apparentRadius >= 0.035f) return 0;
    if (apparentRadius >= 0.015f) return 1;
    if (apparentRadius >= 0.006f) return 2;
    return 3;
}

static sdword modernAsteroidVariantIndex(const Asteroid *asteroid)
{
    udword id;
    if (asteroid == NULL) return 0;
    id = asteroid->resourceID.resourceNumber;
    return (sdword)(modernAsteroidHash(id ^ ((udword)asteroid->asteroidtype * 0x9e3779b9u)) % MASTEROID_VARIANTS);
}

bool32 modernAsteroidSubmitShadow(const Asteroid *asteroid)
{
#ifdef HW_ENABLE_D3D12_BACKEND
    sdword typeIndex;
    sdword lod;
    sdword triangle;
    real32 radius;
    ModernAsteroidTypeData *type;
    ModernAsteroidMesh *mesh;
    HWModernTriangleSurface *surfaces;
    float modelView[16];
    float baseColor[3];

    if (!hwModernGraphicsIsRaytracingSceneOpen()) return FALSE;
    typeIndex = modernAsteroidTypeIndex(asteroid);
    if (typeIndex < 0) return FALSE;
    radius = asteroid->staticinfo->staticheader.staticCollInfo.originalcollspheresize;
    if (radius <= 0.0f)
        radius = asteroid->staticinfo->staticheader.staticCollInfo.collspheresize;
    if (radius <= 0.0f) radius = 1.0f;

    /* Match the exact authored modern LOD that this asteroid would draw at
       the present camera distance. Crossing the frustum edge must not swap
       its shadow silhouette to the unrelated retail GEO. */
    lod = modernAsteroidChooseLod(asteroid, radius);
    if (!modernAsteroidEnsureTexture(typeIndex) ||
        !modernAsteroidEnsureImportedMesh(typeIndex, lod))
        return FALSE;
    type = &modernAsteroidTypes[typeIndex];
    mesh = &type->mesh[0][lod];
    surfaces = (HWModernTriangleSurface *)calloc(
        (size_t)(mesh->indexCount / 3), sizeof(HWModernTriangleSurface));
    if (surfaces == NULL) return FALSE;

    for (triangle = 0; triangle < mesh->indexCount / 3; ++triangle)
    {
        HWModernTriangleSurface *surface = &surfaces[triangle];
        const ModernAsteroidVertex *a = &mesh->vertices[mesh->indices[triangle*3+0]];
        const ModernAsteroidVertex *b = &mesh->vertices[mesh->indices[triangle*3+1]];
        const ModernAsteroidVertex *c = &mesh->vertices[mesh->indices[triangle*3+2]];
        surface->materialIdentity = type;
        surface->baseColor[0] = surface->baseColor[1] = surface->baseColor[2] = 1.0f;
        surface->opacity = 1.0f;
        surface->uv[0] = a->uv[0]; surface->uv[1] = a->uv[1];
        surface->uv[2] = b->uv[0]; surface->uv[3] = b->uv[1];
        surface->uv[4] = c->uv[0]; surface->uv[5] = c->uv[1];
    }
    baseColor[0] = modernAsteroidMaterialDiffuse[typeIndex][0];
    baseColor[1] = modernAsteroidMaterialDiffuse[typeIndex][1];
    baseColor[2] = modernAsteroidMaterialDiffuse[typeIndex][2];
    glPushMatrix();
    glScalef(radius, radius, radius);
    glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
    hwModernGraphicsSubmitTriangleGeometryMasked(
        mesh, &mesh->vertices[0].position[0],
        (unsigned int)mesh->vertexCount, (unsigned int)sizeof(ModernAsteroidVertex),
        (const unsigned short *)mesh->indices,
        (unsigned int)(mesh->indexCount / 3), (unsigned int)(sizeof(uword) * 3),
        0u, surfaces, (unsigned int)sizeof(HWModernTriangleSurface),
        modelView, baseColor, 0x02u);
    glPopMatrix();
    free(surfaces);
    return TRUE;
#else
    (void)asteroid;
    return FALSE;
#endif
}

bool32 modernAsteroidRender(const Asteroid *asteroid, sdword lod)
{
    sdword typeIndex;
    sdword variant;
    real32 radius;
    ModernAsteroidTypeData *type;
    ModernAsteroidMesh *mesh;
    GLboolean textureWasEnabled;
    GLboolean lightingWasEnabled;
    GLboolean colorMaterialWasEnabled;
    GLboolean cullWasEnabled;
    GLboolean depthTestWasEnabled;
    GLboolean depthWriteWasEnabled = GL_TRUE;
    GLint previousDepthFunc = GL_LEQUAL;
    GLint previousTexture = 0;
    GLint previousTextureEnvironment = GL_MODULATE;
    static const GLfloat blackEmission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const GLfloat neutralMaterial[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    typeIndex = modernAsteroidTypeIndex(asteroid);
    if (typeIndex < 0) return FALSE; /* Asteroid0 intentionally stays on minor-object path. */
    radius = asteroid->staticinfo->staticheader.staticCollInfo.originalcollspheresize;
    if (radius <= 0.0f) radius = asteroid->staticinfo->staticheader.staticCollInfo.collspheresize;
    if (radius <= 0.0f) radius = 1.0f;

    /* The project globally forces legacy LOD0, but doing that for a dense
       asteroid field would waste millions of triangles. Pick a modern mesh
       LOD from projected size while keeping every resource as real geometry. */
    lod = modernAsteroidChooseLod(asteroid, radius);
    /* Every level is derived from the corresponding user-authored replacement
       OBJ.  Distance reduction must never substitute an unrelated sphere. */
    variant = 0;

    if (!modernAsteroidEnsureTexture(typeIndex))
    {
        return FALSE;
    }
    if (!modernAsteroidEnsureImportedMesh(typeIndex, lod))
    {
        return FALSE;
    }

    type = &modernAsteroidTypes[typeIndex];
    mesh = &type->mesh[variant][lod];

    {
        static bool32 loggedDeterministicMaterial = FALSE;
        if (!loggedDeterministicMaterial)
        {
            loggedDeterministicMaterial = TRUE;
            fprintf(stderr,
                "[ModernAsteroid] projected-size LOD active: every level is a decimated "
                "user-authored OBJ with preserved UVs and gameplay radius.\n");
        }
    }

    /* The native D3D12 fixed-function bridge does not preserve every classic
       GL material bit in glPushAttrib(). Capture the important state manually
       so this draw cannot inherit emission/color-material from the previous
       ship/effect and cannot leave asteroid state behind for the next object. */
    textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    lightingWasEnabled = glIsEnabled(GL_LIGHTING);
    colorMaterialWasEnabled = glIsEnabled(GL_COLOR_MATERIAL);
    cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                  &previousTextureEnvironment);

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_LIGHTING_BIT |
                 GL_CURRENT_BIT | GL_POLYGON_BIT | GL_COLOR_BUFFER_BIT |
                 GL_DEPTH_BUFFER_BIT);
    /* Harvestable asteroids are ordinary opaque world geometry. Establish a
       complete depth contract instead of inheriting transient FX state. */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);

    /* Force real GL/native-raster texture state directly. rndTextureEnable()
       maintains its own legacy cache and could no-op when that cache disagreed
       with the D3D12 compatibility state, leaving the generated mesh nearly
       white. */
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, type->texture);

    /* Never inherit emissive ship/FX state. The detailed PNG remains the
       albedo; these coefficients only establish a stable warm rock response
       under Homeworld's authored lights. */
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, blackEmission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,
                 modernAsteroidMaterialAmbient[typeIndex]);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,
                 modernAsteroidMaterialDiffuse[typeIndex]);
    glColor4ub(255, 255, 255, 255);

    glPushMatrix();
    glScalef(radius, radius, radius);
#ifdef HW_ENABLE_D3D12_BACKEND
    /* The procedural replacement bypasses Mesh.c, so submit the exact modern
       triangle mesh explicitly to DXR. This makes the asteroid both a shadow
       receiver and an opaque blocker: ships can cast sun shadows across it,
       and its silhouette participates in ray visibility just like stock GEO. */
    if (hwModernGraphicsIsRaytracingSceneOpen())
    {
        HWModernTriangleSurface *surfaces;
        sdword triangle;
        float modelView[16];
        float baseColor[3] = {
            modernAsteroidMaterialDiffuse[typeIndex][0],
            modernAsteroidMaterialDiffuse[typeIndex][1],
            modernAsteroidMaterialDiffuse[typeIndex][2]
        };
        glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
        surfaces = (HWModernTriangleSurface *)calloc(
            (size_t)(mesh->indexCount / 3), sizeof(HWModernTriangleSurface));
        if (surfaces != NULL)
        {
            for (triangle = 0; triangle < mesh->indexCount / 3; ++triangle)
            {
                HWModernTriangleSurface *surface = &surfaces[triangle];
                const ModernAsteroidVertex *a = &mesh->vertices[mesh->indices[triangle*3+0]];
                const ModernAsteroidVertex *b = &mesh->vertices[mesh->indices[triangle*3+1]];
                const ModernAsteroidVertex *c = &mesh->vertices[mesh->indices[triangle*3+2]];
                surface->materialIdentity = type;
                surface->baseColor[0] = surface->baseColor[1] = surface->baseColor[2] = 1.0f;
                surface->opacity = 1.0f;
                surface->uv[0] = a->uv[0]; surface->uv[1] = a->uv[1];
                surface->uv[2] = b->uv[0]; surface->uv[3] = b->uv[1];
                surface->uv[4] = c->uv[0]; surface->uv[5] = c->uv[1];
            }
        hwModernGraphicsSubmitTriangleGeometry(
            mesh,
            &mesh->vertices[0].position[0],
            (unsigned int)mesh->vertexCount,
            (unsigned int)sizeof(ModernAsteroidVertex),
            (const unsigned short *)mesh->indices,
            (unsigned int)(mesh->indexCount / 3),
            (unsigned int)(sizeof(uword) * 3),
            0u,
            surfaces, (unsigned int)sizeof(HWModernTriangleSurface),
            modelView, baseColor);
            free(surfaces);
        }
    }
#endif
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(ModernAsteroidVertex), mesh->vertices[0].position);
    glNormalPointer(GL_FLOAT, sizeof(ModernAsteroidVertex), mesh->vertices[0].normal);
    glTexCoordPointer(2, GL_FLOAT, sizeof(ModernAsteroidVertex), mesh->vertices[0].uv);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_SHORT, mesh->indices);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
    glPopAttrib();

    /* Native compatibility material state is global rather than part of the
       emulated attribute stack. Return it to the neutral legacy expectation. */
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, blackEmission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, neutralMaterial);
    glColor4ub(255, 255, 255, 255);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, previousTextureEnvironment);
    if (textureWasEnabled) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if (lightingWasEnabled) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    if (colorMaterialWasEnabled) glEnable(GL_COLOR_MATERIAL); else glDisable(GL_COLOR_MATERIAL);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthWriteWasEnabled);
    glDepthFunc((GLenum)previousDepthFunc);
    return TRUE;
}
