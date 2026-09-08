#include "ModernPlanet.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "stb_image.h"

#include "ObjTypes.h"
#include "glinc.h"
#include "render.h"
#include "texreg.h"

#define MPLANET_SLICES 192
#define MPLANET_STACKS 96
#define MPLANET_PI 3.14159265358979323846f
#define MPLANET_VARIANT_COUNT 2

typedef struct ModernPlanetVertex
{
    GLfloat position[3];
    GLfloat normal[3];
    GLfloat uv[2];
} ModernPlanetVertex;

typedef struct ModernPlanetMesh
{
    ModernPlanetVertex *vertices;
    uword *indices;
    sdword indexCount;
} ModernPlanetMesh;

typedef struct ModernPlanetVariant
{
    const char *assetPath;
    const char *debugName;
    GLuint texture;
    bool32 textureAttempted;
    ModernPlanetMesh mesh;
} ModernPlanetVariant;

static ModernPlanetVariant modernPlanetVariants[MPLANET_VARIANT_COUNT] =
{
    { "Planets/kharak_albedo.png", "Kharak", 0, FALSE, { NULL, NULL, 0 } },
    { "Planets/kharak_scarred_albedo.png", "Burning Kharak", 0, FALSE, { NULL, NULL, 0 } }
};

static sdword modernPlanetVariantIndex(const Derelict *world)
{
    if (world == NULL) return -1;
    switch (world->derelicttype)
    {
        case PlanetOfOrigin:
            return 0;
        case PlanetOfOrigin_scarred:
            return 1;
        default:
            return -1;
    }
}

static void *modernPlanetLoadAsset(const char *relativePath, size_t *fileSize)
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
    if (data == NULL) data = SDL_LoadFile(relativePath, fileSize);
    return data;
}

static bool32 modernPlanetEnsureTexture(sdword variantIndex)
{
    ModernPlanetVariant *variant;
    void *fileData;
    size_t fileSize;
    stbi_uc *pixels;
    int width, height, channels;

    if (variantIndex < 0 || variantIndex >= MPLANET_VARIANT_COUNT) return FALSE;
    variant = &modernPlanetVariants[variantIndex];
    if (variant->textureAttempted) return variant->texture != 0;
    variant->textureAttempted = TRUE;

    fileData = modernPlanetLoadAsset(variant->assetPath, &fileSize);
    if (fileData == NULL)
    {
        fprintf(stderr, "[ModernPlanet] %s albedo '%s' was not found; using legacy GEO.\n",
                variant->debugName, variant->assetPath);
        return FALSE;
    }
    pixels = stbi_load_from_memory((const stbi_uc *)fileData, (int)fileSize,
                                   &width, &height, &channels, 4);
    SDL_free(fileData);
    if (pixels == NULL || width <= 0 || height <= 0)
    {
        if (pixels != NULL) stbi_image_free(pixels);
        return FALSE;
    }

    glGenTextures(1, &variant->texture);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, variant->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    glGenerateMipmap != NULL ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGenerateMipmap != NULL) glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(pixels);
    fprintf(stderr, "[ModernPlanet] %s albedo loaded from %s (%dx%d).\n",
            variant->debugName, variant->assetPath, width, height);
    return TRUE;
}

static bool32 modernPlanetEnsureMesh(sdword variantIndex)
{
    ModernPlanetMesh *mesh;
    sdword row, column, index = 0;
    const sdword vertexCount = (MPLANET_STACKS + 1) * (MPLANET_SLICES + 1);
    const sdword triangleCount = MPLANET_STACKS * MPLANET_SLICES * 2;

    if (variantIndex < 0 || variantIndex >= MPLANET_VARIANT_COUNT) return FALSE;
    mesh = &modernPlanetVariants[variantIndex].mesh;
    if (mesh->vertices != NULL && mesh->indices != NULL) return TRUE;

    mesh->vertices = (ModernPlanetVertex *)malloc(sizeof(ModernPlanetVertex) * vertexCount);
    mesh->indices = (uword *)malloc(sizeof(uword) * triangleCount * 3);
    if (mesh->vertices == NULL || mesh->indices == NULL)
    {
        free(mesh->vertices);
        free(mesh->indices);
        mesh->vertices = NULL;
        mesh->indices = NULL;
        return FALSE;
    }

    for (row = 0; row <= MPLANET_STACKS; ++row)
    {
        const real32 v = (real32)row / (real32)MPLANET_STACKS;
        const real32 phi = v * MPLANET_PI;
        const real32 sinPhi = (real32)sin(phi);
        const real32 cosPhi = (real32)cos(phi);
        for (column = 0; column <= MPLANET_SLICES; ++column)
        {
            const real32 u = (real32)column / (real32)MPLANET_SLICES;
            /* Stable smooth equirectangular sphere.
               Keep poles on world Y and rotate the longitude seam to the rear
               side of the globe so neither Mission 1 nor Mission 3 shows a
               front-facing seam or radial polar fan. */
            const real32 theta = u * 2.0f * MPLANET_PI - 0.5f * MPLANET_PI;
            const real32 nx = (real32)cos(theta) * sinPhi;
            const real32 ny = cosPhi;
            const real32 nz = (real32)sin(theta) * sinPhi;
            ModernPlanetVertex *vertex = &mesh->vertices[row * (MPLANET_SLICES + 1) + column];

            vertex->normal[0] = nx;
            vertex->normal[1] = ny;
            vertex->normal[2] = nz;
            /* Both campaign variants are deliberately exact spheres.  Mission
               3's destruction is carried only by its independent burned
               albedo; there is no radial scar, crater, rim or displacement. */
            vertex->position[0] = nx;
            vertex->position[1] = ny;
            vertex->position[2] = nz;
            vertex->uv[0] = u;
            vertex->uv[1] = v;
        }
    }

    for (row = 0; row < MPLANET_STACKS; ++row)
    {
        for (column = 0; column < MPLANET_SLICES; ++column)
        {
            const uword a = (uword)(row * (MPLANET_SLICES + 1) + column);
            const uword b = (uword)(a + 1);
            const uword c = (uword)(a + MPLANET_SLICES + 1);
            const uword d = (uword)(c + 1);
            mesh->indices[index++] = a;
            mesh->indices[index++] = b;
            mesh->indices[index++] = c;
            mesh->indices[index++] = b;
            mesh->indices[index++] = d;
            mesh->indices[index++] = c;
        }
    }

    mesh->indexCount = index;
    fprintf(stderr, "[ModernPlanet] %s mesh generated (%s).\n",
            modernPlanetVariants[variantIndex].debugName,
            variantIndex == 1 ? "perfect sphere / scarred albedo only" : "pristine sphere");
    return TRUE;
}

bool32 modernPlanetRender(const Derelict *world)
{
    GLboolean cullWasEnabled;
    real32 radius;
    sdword variantIndex;
    ModernPlanetVariant *variant;

    variantIndex = modernPlanetVariantIndex(world);
    if (variantIndex < 0 ||
        !modernPlanetEnsureTexture(variantIndex) ||
        !modernPlanetEnsureMesh(variantIndex))
    {
        return FALSE;
    }
    variant = &modernPlanetVariants[variantIndex];

    radius = world->staticinfo->staticheader.staticCollInfo.originalcollspheresize;
    if (radius <= 0.0f) radius = 1.0f;
    cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    /* rndRenderAHomeworld intentionally disables depth testing for world
       planets.  The generated 0020 sphere has outward CCW winding, so
       disabling culling here lets the back hemisphere render over the front
       hemisphere.  That is the source of the long dark longitude strips and
       the triangular saw-tooth fan visible near the pole.  Cull back faces
       while drawing the closed planet surface; restore the caller's enable
       state afterward. */
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glPushMatrix();
    glScalef(radius, radius, radius);
    rndTextureEnable(TRUE);
    rndTextureEnvironment(RTE_Modulate);
    trClearCurrent();
    glBindTexture(GL_TEXTURE_2D, variant->texture);
    glColor4ub(255, 255, 255, 255);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(ModernPlanetVertex),
                    variant->mesh.vertices[0].position);
    glNormalPointer(GL_FLOAT, sizeof(ModernPlanetVertex),
                    variant->mesh.vertices[0].normal);
    glTexCoordPointer(2, GL_FLOAT, sizeof(ModernPlanetVertex),
                      variant->mesh.vertices[0].uv);
    glDrawElements(GL_TRIANGLES, variant->mesh.indexCount,
                   GL_UNSIGNED_SHORT, variant->mesh.indices);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPopMatrix();
    if (!cullWasEnabled) glDisable(GL_CULL_FACE);
    return TRUE;
}
