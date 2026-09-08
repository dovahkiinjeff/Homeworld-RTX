#include "ModernDustCloud.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "glinc.h"
#include "render.h"
#include "texreg.h"

#ifdef HW_ENABLE_D3D12_BACKEND
#include "ModernGraphics.h"
#endif

#define MDUST_VARIANTS 8
#define MDUST_LODS 4
#define MDUST_PI 3.14159265358979323846f

typedef struct ModernDustCloudVertex
{
    GLfloat position[3];
    GLfloat normal[3];
    GLfloat uv[2];
} ModernDustCloudVertex;

typedef struct ModernDustCloudMesh
{
    ModernDustCloudVertex *vertices;
    uword *indices;
    sdword vertexCount;
    sdword indexCount;
} ModernDustCloudMesh;

typedef struct ModernDustCloudData
{
    GLuint texture;
    bool32 textureAttempted;
    ModernDustCloudMesh mesh[MDUST_VARIANTS][MDUST_LODS];
} ModernDustCloudData;

static ModernDustCloudData modernDustCloud;
static const char *modernDustCloudAssetPath =
    "DustClouds/harvestable_dustcloud_albedo.png";
static const sdword modernDustCloudSlices[MDUST_LODS] = {96, 64, 40, 24};
static const sdword modernDustCloudStacks[MDUST_LODS] = {48, 32, 20, 12};

/* Each variant has a deliberately non-spherical large-scale silhouette.  The
   generated shell is normalized to unit radius after deformation, so none of
   this changes the legacy resource/collision footprint. */
static const real32 modernDustCloudAxis[MDUST_VARIANTS][3] =
{
    {1.00f, 0.74f, 0.86f}, {0.79f, 1.00f, 0.72f},
    {0.83f, 0.76f, 1.00f}, {1.00f, 0.88f, 0.67f},
    {0.71f, 1.00f, 0.91f}, {0.92f, 0.69f, 1.00f},
    {1.00f, 0.71f, 0.73f}, {0.76f, 0.87f, 1.00f}
};

static real32 mdustClamp(real32 x, real32 lo, real32 hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static udword mdustHash(udword x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static real32 mdustSignedHash(udword x)
{
    return ((real32)(mdustHash(x) & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

static void mdustNormalize3(real32 v[3])
{
    real32 m = (real32)sqrt((double)(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]));
    if (m > 0.000001f)
    {
        v[0] /= m; v[1] /= m; v[2] /= m;
    }
}

static void mdustCross(const real32 a[3], const real32 b[3], real32 out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void mdustSub(const real32 a[3], const real32 b[3], real32 out[3])
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static udword mdustSystemSeed(const cloudSystem *system)
{
    udword seed = 0x4d445354u;
    if (system != NULL && system->position != NULL)
    {
        sdword qx = (sdword)floor((double)(system->position->x * 0.03125f));
        sdword qy = (sdword)floor((double)(system->position->y * 0.03125f));
        sdword qz = (sdword)floor((double)(system->position->z * 0.03125f));
        seed ^= mdustHash((udword)qx + 0x9e3779b9u);
        seed ^= mdustHash((udword)qy + 0x85ebca6bu);
        seed ^= mdustHash((udword)qz + 0xc2b2ae35u);
    }
    if (system != NULL)
    {
        seed ^= mdustHash((udword)(system->radius * 17.0f));
    }
    return mdustHash(seed);
}

static real32 mdustDirectionalLobe(const real32 d[3],
                                   real32 x, real32 y, real32 z,
                                   real32 sharpness)
{
    real32 axis[3] = {x, y, z};
    real32 dot;
    mdustNormalize3(axis);
    dot = mdustClamp(d[0]*axis[0] + d[1]*axis[1] + d[2]*axis[2], 0.0f, 1.0f);
    return (real32)pow((double)dot, (double)sharpness);
}

static real32 mdustRadiusField(const real32 d[3], udword seed, sdword variant)
{
    real32 phase = (real32)(seed & 0xffffu) * (2.0f * MDUST_PI / 65535.0f);
    real32 n0 = (real32)sin((double)(d[0]*5.7f + d[1]*3.1f - d[2]*4.3f + phase));
    real32 n1 = (real32)sin((double)(d[0]*11.4f - d[1]*8.8f + d[2]*7.2f + phase*1.71f));
    real32 n2 = (real32)sin((double)(d[0]*22.1f + d[1]*17.7f + d[2]*14.9f - phase*0.63f));
    real32 lobeA = mdustDirectionalLobe(d,
        0.72f + 0.18f*mdustSignedHash(seed+1u),
        0.31f + 0.22f*mdustSignedHash(seed+2u),
        0.62f + 0.18f*mdustSignedHash(seed+3u), 5.0f);
    real32 lobeB = mdustDirectionalLobe(d,
       -0.54f + 0.20f*mdustSignedHash(seed+4u),
        0.76f + 0.15f*mdustSignedHash(seed+5u),
       -0.19f + 0.20f*mdustSignedHash(seed+6u), 6.0f);
    real32 lobeC = mdustDirectionalLobe(d,
        0.08f + 0.20f*mdustSignedHash(seed+7u),
       -0.61f + 0.18f*mdustSignedHash(seed+8u),
        0.79f + 0.16f*mdustSignedHash(seed+9u), 7.0f);
    real32 dent = mdustDirectionalLobe(d,
       -0.68f + 0.18f*mdustSignedHash(seed+10u),
       -0.44f + 0.18f*mdustSignedHash(seed+11u),
        0.36f + 0.18f*mdustSignedHash(seed+12u), 9.0f);
    real32 r = 0.78f;

    r += 0.18f*lobeA + 0.14f*lobeB + 0.11f*lobeC - 0.10f*dent;
    r += n0*0.085f + n1*0.040f + n2*0.018f;
    /* A broad waist/cap break makes the silhouette read as a particulate
       aggregate rather than a noisy sphere. */
    r += 0.055f * (real32)sin((double)(d[2]*3.4f + (real32)variant*0.71f));
    return mdustClamp(r, 0.48f, 1.20f);
}

static bool32 mdustEnsureMesh(sdword variant, sdword lod)
{
    ModernDustCloudMesh *mesh;
    sdword slices, stacks, vertexCount, triangleCount;
    sdword row, column, index;
    real32 maximumRadius = 0.0f;
    udword seed;

    if (variant < 0 || variant >= MDUST_VARIANTS || lod < 0 || lod >= MDUST_LODS)
        return FALSE;
    mesh = &modernDustCloud.mesh[variant][lod];
    if (mesh->vertices != NULL && mesh->indices != NULL) return TRUE;

    slices = modernDustCloudSlices[lod];
    stacks = modernDustCloudStacks[lod];
    vertexCount = (slices + 1) * (stacks + 1);
    triangleCount = slices * stacks * 2;
    if (vertexCount >= 65535) return FALSE;

    mesh->vertices = (ModernDustCloudVertex *)malloc(
        (size_t)vertexCount * sizeof(ModernDustCloudVertex));
    mesh->indices = (uword *)malloc(
        (size_t)triangleCount * 3u * sizeof(uword));
    if (mesh->vertices == NULL || mesh->indices == NULL)
    {
        free(mesh->vertices); free(mesh->indices);
        memset(mesh, 0, sizeof(*mesh));
        return FALSE;
    }
    memset(mesh->vertices, 0, (size_t)vertexCount * sizeof(ModernDustCloudVertex));
    seed = mdustHash(0x8f1bbcdcu ^ (udword)(variant * 0x9e3779b9u));

    index = 0;
    for (row = 0; row <= stacks; ++row)
    {
        real32 v = (real32)row / (real32)stacks;
        real32 theta = v * MDUST_PI;
        real32 sy = (real32)cos((double)theta);
        real32 sr = (real32)sin((double)theta);
        for (column = 0; column <= slices; ++column)
        {
            real32 u = (real32)column / (real32)slices;
            real32 phi = u * 2.0f * MDUST_PI;
            real32 d[3] = {
                sr * (real32)cos((double)phi),
                sy,
                sr * (real32)sin((double)phi)
            };
            real32 r = mdustRadiusField(d, seed, variant);
            real32 x = d[0] * r * modernDustCloudAxis[variant][0];
            real32 y = d[1] * r * modernDustCloudAxis[variant][1];
            real32 z = d[2] * r * modernDustCloudAxis[variant][2];
            real32 rr = (real32)sqrt((double)(x*x + y*y + z*z));
            ModernDustCloudVertex *vertex = &mesh->vertices[index++];
            vertex->position[0] = x;
            vertex->position[1] = y;
            vertex->position[2] = z;
            vertex->uv[0] = u;
            vertex->uv[1] = 1.0f - v;
            if (rr > maximumRadius) maximumRadius = rr;
        }
    }

    if (maximumRadius <= 0.00001f) maximumRadius = 1.0f;
    for (index = 0; index < vertexCount; ++index)
    {
        mesh->vertices[index].position[0] /= maximumRadius;
        mesh->vertices[index].position[1] /= maximumRadius;
        mesh->vertices[index].position[2] /= maximumRadius;
    }

    index = 0;
    for (row = 0; row < stacks; ++row)
    {
        for (column = 0; column < slices; ++column)
        {
            uword a = (uword)(row * (slices + 1) + column);
            uword b = (uword)(a + slices + 1);
            uword c = (uword)(a + 1);
            uword d = (uword)(b + 1);
            mesh->indices[index++] = a; mesh->indices[index++] = b; mesh->indices[index++] = c;
            mesh->indices[index++] = c; mesh->indices[index++] = b; mesh->indices[index++] = d;
        }
    }

    /* Smooth normals from the actual new lobe geometry, not from a sphere. */
    for (index = 0; index < triangleCount; ++index)
    {
        uword ia = mesh->indices[index*3+0];
        uword ib = mesh->indices[index*3+1];
        uword ic = mesh->indices[index*3+2];
        real32 e1[3], e2[3], n[3];
        mdustSub(mesh->vertices[ib].position, mesh->vertices[ia].position, e1);
        mdustSub(mesh->vertices[ic].position, mesh->vertices[ia].position, e2);
        mdustCross(e1, e2, n);
        mesh->vertices[ia].normal[0] += n[0];
        mesh->vertices[ia].normal[1] += n[1];
        mesh->vertices[ia].normal[2] += n[2];
        mesh->vertices[ib].normal[0] += n[0];
        mesh->vertices[ib].normal[1] += n[1];
        mesh->vertices[ib].normal[2] += n[2];
        mesh->vertices[ic].normal[0] += n[0];
        mesh->vertices[ic].normal[1] += n[1];
        mesh->vertices[ic].normal[2] += n[2];
    }
    for (index = 0; index < vertexCount; ++index)
    {
        real32 n[3] = {
            mesh->vertices[index].normal[0],
            mesh->vertices[index].normal[1],
            mesh->vertices[index].normal[2]
        };
        mdustNormalize3(n);
        mesh->vertices[index].normal[0] = n[0];
        mesh->vertices[index].normal[1] = n[1];
        mesh->vertices[index].normal[2] = n[2];
    }

    mesh->vertexCount = vertexCount;
    mesh->indexCount = triangleCount * 3;
    return TRUE;
}

static void *mdustLoadAsset(const char *relativePath, size_t *fileSize)
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

static bool32 mdustEnsureTexture(void)
{
    void *fileData;
    size_t fileSize;
    stbi_uc *pixels;
    int width, height, channels;

    if (modernDustCloud.textureAttempted) return modernDustCloud.texture != 0;
    modernDustCloud.textureAttempted = TRUE;
    fileData = mdustLoadAsset(modernDustCloudAssetPath, &fileSize);
    if (fileData == NULL)
    {
        fprintf(stderr, "[ModernDustCloud] albedo '%s' not found; using legacy fallback.\n",
                modernDustCloudAssetPath);
        return FALSE;
    }
    pixels = stbi_load_from_memory((const stbi_uc *)fileData, (int)fileSize,
                                   &width, &height, &channels, 4);
    SDL_free(fileData);
    if (pixels == NULL || width <= 0 || height <= 0)
    {
        if (pixels != NULL) stbi_image_free(pixels);
        fprintf(stderr, "[ModernDustCloud] albedo decode failed; using legacy fallback.\n");
        return FALSE;
    }

    glGenTextures(1, &modernDustCloud.texture);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, modernDustCloud.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    glGenerateMipmap != NULL ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGenerateMipmap != NULL) glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(pixels);
    fprintf(stderr, "[ModernDustCloud] new 2026 harvestable resource material loaded (%dx%d).\n",
            width, height);
    return TRUE;
}

static sdword mdustChooseLod(sdword legacyLod)
{
    if (legacyLod <= 0) return 0;
    if (legacyLod == 1) return 1;
    if (legacyLod == 2) return 2;
    return 3;
}

bool32 modernDustCloudRender(const cloudSystem *system, real32 radius,
                             sdword legacyLod, color cloudColor)
{
    udword seed;
    sdword variant, lod;
    ModernDustCloudMesh *mesh;
    GLboolean textureWasEnabled, lightingWasEnabled, colorMaterialWasEnabled;
    GLboolean cullWasEnabled, depthTestWasEnabled, blendWasEnabled;
    GLboolean depthWriteWasEnabled = GL_TRUE;
    GLint previousDepthFunc = GL_LEQUAL;
    GLint previousTexture = 0;
    GLint previousTextureEnvironment = GL_MODULATE;
    GLfloat diffuse[4], ambient[4];
    static const GLfloat blackEmission[4] = {0.0f,0.0f,0.0f,1.0f};
    static const GLfloat neutralMaterial[4] = {1.0f,1.0f,1.0f,1.0f};
    static bool32 logged = FALSE;

    if (system == NULL || radius <= 0.0f) return FALSE;
    seed = mdustSystemSeed(system);
    variant = (sdword)(seed % MDUST_VARIANTS);
    lod = mdustChooseLod(legacyLod);
    if (!mdustEnsureTexture() || !mdustEnsureMesh(variant, lod)) return FALSE;
    mesh = &modernDustCloud.mesh[variant][lod];

    if (!logged)
    {
        logged = TRUE;
        fprintf(stderr,
            "[ModernDustCloud] brand-new lobe mesh + authored albedo active: "
            "8 silhouettes, up to 9216 triangles, original resource radius/harvest logic preserved.\n");
    }

    diffuse[0] = mdustClamp((real32)colRed(cloudColor) / 255.0f * 1.10f, 0.0f, 1.0f);
    diffuse[1] = mdustClamp((real32)colGreen(cloudColor) / 255.0f * 1.06f, 0.0f, 1.0f);
    diffuse[2] = mdustClamp((real32)colBlue(cloudColor) / 255.0f * 1.02f, 0.0f, 1.0f);
    diffuse[3] = 1.0f;
    ambient[0] = diffuse[0] * 0.24f;
    ambient[1] = diffuse[1] * 0.24f;
    ambient[2] = diffuse[2] * 0.24f;
    ambient[3] = 1.0f;

    textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    lightingWasEnabled = glIsEnabled(GL_LIGHTING);
    colorMaterialWasEnabled = glIsEnabled(GL_COLOR_MATERIAL);
    cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    blendWasEnabled = glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &previousTextureEnvironment);

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_LIGHTING_BIT |
                 GL_CURRENT_BIT | GL_POLYGON_BIT | GL_COLOR_BUFFER_BIT |
                 GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, modernDustCloud.texture);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, blackEmission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glColor4ub(255,255,255,255);

    glPushMatrix();
    glScalef(radius, radius, radius);
#ifdef HW_ENABLE_D3D12_BACKEND
    if (hwModernGraphicsIsRaytracingSceneOpen())
    {
        float modelView[16];
        float baseColor[3] = {diffuse[0], diffuse[1], diffuse[2]};
        glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
        hwModernGraphicsSubmitTriangleGeometry(
            mesh,
            &mesh->vertices[0].position[0],
            (unsigned int)mesh->vertexCount,
            (unsigned int)sizeof(ModernDustCloudVertex),
            (const unsigned short *)mesh->indices,
            (unsigned int)(mesh->indexCount / 3),
            (unsigned int)(sizeof(uword) * 3),
            (unsigned int)variant,
            NULL, 0u, modelView, baseColor);
    }
#endif
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(ModernDustCloudVertex),
                    mesh->vertices[0].position);
    glNormalPointer(GL_FLOAT, sizeof(ModernDustCloudVertex),
                    mesh->vertices[0].normal);
    glTexCoordPointer(2, GL_FLOAT, sizeof(ModernDustCloudVertex),
                      mesh->vertices[0].uv);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_SHORT, mesh->indices);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
    glPopAttrib();

    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, blackEmission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, neutralMaterial);
    glColor4ub(255,255,255,255);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, previousTextureEnvironment);
    if (textureWasEnabled) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if (lightingWasEnabled) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    if (colorMaterialWasEnabled) glEnable(GL_COLOR_MATERIAL); else glDisable(GL_COLOR_MATERIAL);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(depthWriteWasEnabled);
    glDepthFunc((GLenum)previousDepthFunc);
    return TRUE;
}

void modernDustCloudShutdown(void)
{
    sdword variant, lod;
    if (modernDustCloud.texture != 0)
    {
        glDeleteTextures(1, &modernDustCloud.texture);
        modernDustCloud.texture = 0;
    }
    modernDustCloud.textureAttempted = FALSE;
    for (variant = 0; variant < MDUST_VARIANTS; ++variant)
    {
        for (lod = 0; lod < MDUST_LODS; ++lod)
        {
            ModernDustCloudMesh *mesh = &modernDustCloud.mesh[variant][lod];
            free(mesh->vertices);
            free(mesh->indices);
            memset(mesh, 0, sizeof(*mesh));
        }
    }
}
