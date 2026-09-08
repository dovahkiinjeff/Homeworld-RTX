#include "ModernText.h"

#include <SDL2/SDL.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "glinc.h"
#include "prim2d.h"
#include "render.h"
#include "texreg.h"

#define HW_TEXT_CACHE_CAPACITY 512
#define HW_TEXT_MAX_SOURCE     1024

typedef struct HwModernTextEntry
{
    bool32 used;
    udword hash;
    udword stamp;
    sdword pixelHeight;
    sdword width;
    sdword height;
    udword texture;
    char sourceFont[64];
    char text[HW_TEXT_MAX_SOURCE];
} HwModernTextEntry;

static HwModernTextEntry hwTextCache[HW_TEXT_CACHE_CAPACITY];
static udword hwTextStamp = 1;
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
static SDL_GLContext hwTextContext;
#endif

static udword hwTextHashBytes(udword hash, const void *bytes, size_t count)
{
    const unsigned char *cursor = (const unsigned char *)bytes;
    while (count-- > 0)
    {
        hash ^= (udword)*cursor++;
        hash *= 16777619u;
    }
    return hash;
}

static void hwTextNormalize(const char *source, sdword maxCharacters,
                            char *result, size_t resultCount)
{
    size_t output = 0;
    sdword visible = 0;
    if (resultCount == 0) return;
    if (source == NULL) source = "";
    while (*source != '\0' && output + 1 < resultCount &&
           (maxCharacters < 0 || visible < maxCharacters))
    {
        unsigned char current = (unsigned char)*source++;
        if (current == '&')
        {
            if (*source == '\0') break;
            current = (unsigned char)*source++;
        }
        result[output++] = (char)current;
        visible++;
    }
    result[output] = '\0';
}

static bool32 hwTextHeavyFont(const char *sourceFont)
{
    if (sourceFont == NULL) return FALSE;
    return strstr(sourceFont, "wideheavy") != NULL ||
           strstr(sourceFont, "eyechart") != NULL ||
           strstr(sourceFont, "arial_b") != NULL ||
           strstr(sourceFont, "Arial_b") != NULL;
}

static HFONT hwTextCreateFont(const char *sourceFont, sdword pixelHeight)
{
    int weight = hwTextHeavyFont(sourceFont) ? FW_SEMIBOLD : FW_NORMAL;
    if (pixelHeight < 8) pixelHeight = 8;
    return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY,
                       FF_SWISS | VARIABLE_PITCH,
                       L"Bahnschrift SemiCondensed");
}

static WCHAR *hwTextToWide(const char *text)
{
    WCHAR *wide;
    int count;
    UINT codePage = CP_UTF8;
    count = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                text, -1, NULL, 0);
    if (count <= 0)
    {
        codePage = CP_ACP;
        count = MultiByteToWideChar(codePage, 0, text, -1, NULL, 0);
    }
    if (count <= 0) return NULL;
    wide = (WCHAR *)malloc((size_t)count * sizeof(WCHAR));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                            text, -1, wide, count) <= 0)
    {
        free(wide);
        return NULL;
    }
    return wide;
}

static bool32 hwTextRasterize(const char *sourceFont,
                              sdword pixelHeight,
                              const char *text,
                              bool32 uploadTexture,
                              sdword *outWidth,
                              sdword *outHeight,
                              udword *outTexture)
{
    HDC dc = NULL;
    HFONT font = NULL;
    HGDIOBJ oldFont = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ oldBitmap = NULL;
    BITMAPINFO info;
    WCHAR *wide = NULL;
    SIZE extent;
    void *dibBits = NULL;
    unsigned char *rgba = NULL;
    sdword width;
    sdword height;
    sdword x;
    sdword y;
    bool32 result = FALSE;

    if (outWidth != NULL) *outWidth = 0;
    if (outHeight != NULL) *outHeight = pixelHeight;
    if (outTexture != NULL) *outTexture = 0;
    if (text == NULL || text[0] == '\0') return TRUE;

    wide = hwTextToWide(text);
    if (wide == NULL) goto cleanup;
    dc = CreateCompatibleDC(NULL);
    if (dc == NULL) goto cleanup;
    font = hwTextCreateFont(sourceFont, pixelHeight);
    if (font == NULL) goto cleanup;
    oldFont = SelectObject(dc, font);
    if (!GetTextExtentPoint32W(dc, wide, (int)wcslen(wide), &extent))
        goto cleanup;

    width = extent.cx > 0 ? extent.cx : 1;
    height = pixelHeight + 4;
    if (height < extent.cy + 2) height = extent.cy + 2;
    if (outWidth != NULL) *outWidth = width;
    if (outHeight != NULL) *outHeight = height;
    if (!uploadTexture)
    {
        result = TRUE;
        goto cleanup;
    }

    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dibBits, NULL, 0);
    if (bitmap == NULL || dibBits == NULL) goto cleanup;
    oldBitmap = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, width, height, BLACKNESS);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 0, 0, wide, (int)wcslen(wide));

    rgba = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (rgba == NULL) goto cleanup;
    for (y = 0; y < height; y++)
    {
        const unsigned char *source =
            (const unsigned char *)dibBits + (size_t)y * (size_t)width * 4u;
        unsigned char *dest = rgba + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; x++)
        {
            unsigned char coverage = source[x * 4u + 0u];
            if (source[x * 4u + 1u] > coverage) coverage = source[x * 4u + 1u];
            if (source[x * 4u + 2u] > coverage) coverage = source[x * 4u + 2u];
            dest[x * 4u + 0u] = 255;
            dest[x * 4u + 1u] = 255;
            dest[x * 4u + 2u] = 255;
            dest[x * 4u + 3u] = coverage;
        }
    }

    if (outTexture == NULL) goto cleanup;
    glGenTextures(1, (GLuint *)outTexture);
    if (*outTexture == 0) goto cleanup;
    glBindTexture(GL_TEXTURE_2D, *outTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    result = TRUE;

cleanup:
    if (!result && outTexture != NULL && *outTexture != 0)
    {
        glDeleteTextures(1, (GLuint *)outTexture);
        *outTexture = 0;
    }
    free(rgba);
    if (oldBitmap != NULL) SelectObject(dc, oldBitmap);
    if (bitmap != NULL) DeleteObject(bitmap);
    if (oldFont != NULL) SelectObject(dc, oldFont);
    if (font != NULL) DeleteObject(font);
    if (dc != NULL) DeleteDC(dc);
    free(wide);
    return result;
}

void hwModernTextReset(void)
{
    sdword index;
    for (index = 0; index < HW_TEXT_CACHE_CAPACITY; index++)
    {
        if (hwTextCache[index].texture != 0)
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
        {
            if (hwTextContext != NULL)
#endif
                glDeleteTextures(1, (GLuint *)&hwTextCache[index].texture);
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
        }
#endif
        memset(&hwTextCache[index], 0, sizeof(hwTextCache[index]));
    }
    hwTextStamp = 1;
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
    hwTextContext = SDL_GL_GetCurrentContext();
#endif
}

static void hwTextCheckContext(void)
{
#ifndef HW_ENABLE_D3D12_NATIVE_RASTER
    SDL_GLContext current = SDL_GL_GetCurrentContext();
    if (current != hwTextContext)
    {
        /* Texture names belong to the previous context and must not be
           deleted through the new one. */
        memset(hwTextCache, 0, sizeof(hwTextCache));
        hwTextStamp = 1;
        hwTextContext = current;
    }
#endif
}

static HwModernTextEntry *hwTextFind(const char *sourceFont,
                                     sdword pixelHeight,
                                     const char *text,
                                     udword hash)
{
    sdword index;
    for (index = 0; index < HW_TEXT_CACHE_CAPACITY; index++)
    {
        HwModernTextEntry *entry = &hwTextCache[index];
        if (entry->used && entry->hash == hash &&
            entry->pixelHeight == pixelHeight &&
            strcmp(entry->sourceFont, sourceFont) == 0 &&
            strcmp(entry->text, text) == 0)
            return entry;
    }
    return NULL;
}

static HwModernTextEntry *hwTextAllocate(void)
{
    HwModernTextEntry *oldest = &hwTextCache[0];
    sdword index;
    for (index = 0; index < HW_TEXT_CACHE_CAPACITY; index++)
    {
        HwModernTextEntry *entry = &hwTextCache[index];
        if (!entry->used) return entry;
        if (entry->stamp < oldest->stamp) oldest = entry;
    }
    if (oldest->texture != 0)
        glDeleteTextures(1, (GLuint *)&oldest->texture);
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static HwModernTextEntry *hwTextEntry(const char *sourceFont,
                                      sdword pixelHeight,
                                      const char *normalized,
                                      bool32 requireTexture)
{
    HwModernTextEntry *entry;
    udword hash = 2166136261u;
    if (sourceFont == NULL) sourceFont = "regular";
    hash = hwTextHashBytes(hash, sourceFont, strlen(sourceFont));
    hash = hwTextHashBytes(hash, &pixelHeight, sizeof(pixelHeight));
    hash = hwTextHashBytes(hash, normalized, strlen(normalized));
    entry = hwTextFind(sourceFont, pixelHeight, normalized, hash);
    if (entry != NULL && (!requireTexture || entry->texture != 0))
    {
        entry->stamp = hwTextStamp++;
        return entry;
    }
    if (entry == NULL) entry = hwTextAllocate();
    else if (entry->texture != 0)
    {
        glDeleteTextures(1, (GLuint *)&entry->texture);
        entry->texture = 0;
    }
    if (!hwTextRasterize(sourceFont, pixelHeight, normalized,
                         requireTexture, &entry->width, &entry->height,
                         &entry->texture))
    {
        memset(entry, 0, sizeof(*entry));
        return NULL;
    }
    entry->used = TRUE;
    entry->hash = hash;
    entry->stamp = hwTextStamp++;
    entry->pixelHeight = pixelHeight;
    strncpy(entry->sourceFont, sourceFont, sizeof(entry->sourceFont) - 1);
    entry->sourceFont[sizeof(entry->sourceFont) - 1] = '\0';
    strncpy(entry->text, normalized, sizeof(entry->text) - 1);
    entry->text[sizeof(entry->text) - 1] = '\0';
    return entry;
}

sdword hwModernTextWidth(const char *sourceFont,
                         sdword pixelHeight,
                         const char *text,
                         sdword maxCharacters)
{
    char normalized[HW_TEXT_MAX_SOURCE];
    HwModernTextEntry *entry;
    if (pixelHeight <= 0) return 0;
    hwTextCheckContext();
    hwTextNormalize(text, maxCharacters, normalized, sizeof(normalized));
    entry = hwTextEntry(sourceFont, pixelHeight, normalized, FALSE);
    return entry != NULL ? entry->width : 0;
}

bool32 hwModernTextDraw(const char *sourceFont,
                        sdword pixelHeight,
                        sdword x,
                        sdword y,
                        color tint,
                        const char *text,
                        sdword maxCharacters)
{
    char normalized[HW_TEXT_MAX_SOURCE];
    HwModernTextEntry *entry;
    bool32 textureOn;
    bool32 blendOn;
    bool32 alphaTestOn;
#ifdef HW_ENABLE_D3D12_NATIVE_RASTER
    if (pixelHeight <= 0) return FALSE;
#else
    if (pixelHeight <= 0 || SDL_GL_GetCurrentContext() == NULL) return FALSE;
#endif
    hwTextCheckContext();
    hwTextNormalize(text, maxCharacters, normalized, sizeof(normalized));
    if (normalized[0] == '\0') return TRUE;
    entry = hwTextEntry(sourceFont, pixelHeight, normalized, TRUE);
    if (entry == NULL || entry->texture == 0) return FALSE;

    trClearCurrent();
    textureOn = rndTextureEnable(TRUE);
    blendOn = (bool32)glIsEnabled(GL_BLEND);
    alphaTestOn = (bool32)glIsEnabled(GL_ALPHA_TEST);
    if (!blendOn) glEnable(GL_BLEND);
    if (alphaTestOn) glDisable(GL_ALPHA_TEST);
    rndAdditiveBlends(FALSE);
    rndTextureEnvironment(RTE_Modulate);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glColor4ub(colRed(tint), colGreen(tint), colBlue(tint), colAlpha(tint));
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(primScreenToGLX(x), primScreenToGLY(y));
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(primScreenToGLX(x), primScreenToGLY(y + entry->height));
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(primScreenToGLX(x + entry->width), primScreenToGLY(y));
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(primScreenToGLX(x + entry->width),
               primScreenToGLY(y + entry->height));
    glEnd();
    rndTextureEnable(textureOn);
    if (!blendOn) glDisable(GL_BLEND);
    if (alphaTestOn) glEnable(GL_ALPHA_TEST);
    return TRUE;
}
