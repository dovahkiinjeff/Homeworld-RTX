#include "TexturePack.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma pack(push, 1)
typedef struct TexturePackHeader
{
    char magic[8];
    unsigned int version;
    unsigned int count;
    unsigned long long dataOffset;
} TexturePackHeader;

typedef struct TexturePackEntry
{
    char path[256];
    unsigned long long offset;
    unsigned int size;
    unsigned int reserved;
} TexturePackEntry;
#pragma pack(pop)

static HANDLE texturePackFile = INVALID_HANDLE_VALUE;
static HANDLE texturePackMapping = NULL;
static const unsigned char *texturePackView = NULL;
static const TexturePackHeader *texturePackHeader = NULL;
static const TexturePackEntry *texturePackEntries = NULL;
static unsigned long long texturePackLength = 0;
static INIT_ONCE texturePackInitOnce = INIT_ONCE_STATIC_INIT;
static unsigned long long texturePackReadCalls = 0;
static unsigned long long texturePackReadBytes = 0;
static double texturePackReadMilliseconds = 0.0;
static sdword texturePackLooseOverrideMode = -1;

bool32 texturePackLooseOverridesEnabled(void)
{
    if (texturePackLooseOverrideMode < 0)
    {
        const char *value = getenv("HW_LOOSE_TEXTURE_OVERRIDES");
        texturePackLooseOverrideMode =
            value != NULL && value[0] != 0 && strcmp(value, "0") != 0;
    }
    return texturePackLooseOverrideMode ? TRUE : FALSE;
}

static void texturePackLogMetrics(void)
{
    if (texturePackReadCalls != 0)
        fprintf(stderr,
            "[TexturePerf] archive reads=%llu %.1f MiB %.1f ms\n",
            texturePackReadCalls,
            (double)texturePackReadBytes / (1024.0 * 1024.0),
            texturePackReadMilliseconds);
}

static void texturePackNormalize(const char *source, char result[256])
{
    char full[1024];
    const char *start;
    static const char *roots[] = {
        "\\r1\\", "\\r2\\", "\\p1\\", "\\p2\\", "\\p3\\",
        "\\traders\\", "\\asteroids\\"
    };
    size_t i, length;
    strncpy(full, source ? source : "", sizeof(full) - 1);
    full[sizeof(full) - 1] = 0;
    for (i = 0; full[i]; ++i)
    {
        if (full[i] == '/') full[i] = '\\';
        else full[i] = (char)tolower((unsigned char)full[i]);
    }
    start = full;
    for (i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        const char *found = strstr(full, roots[i]);
        if (found != NULL) { start = found + 1; break; }
        length = strlen(roots[i]);
        if (strncmp(full, roots[i] + 1, length - 1) == 0)
        {
            start = full; break;
        }
    }
    strncpy(result, start, 255);
    result[255] = 0;
}

static BOOL CALLBACK texturePackInitializeOnce(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    char path[MAX_PATH];
    char *slash;
    LARGE_INTEGER size;
    const TexturePackHeader *header;
    unsigned int i;
    (void)once; (void)parameter; (void)context;
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return TRUE;
    slash = strrchr(path, '\\');
    if (slash == NULL || (size_t)(slash - path) + sizeof("HomeworldTextures.hwt") >= MAX_PATH) return TRUE;
    strcpy(slash + 1, "HomeworldTextures.hwt");
    texturePackFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                                  NULL);
    if (texturePackFile == INVALID_HANDLE_VALUE) return TRUE;
    if (!GetFileSizeEx(texturePackFile, &size) || size.QuadPart < (LONGLONG)sizeof(TexturePackHeader)) goto fail;
    texturePackLength = (unsigned long long)size.QuadPart;
    texturePackMapping = CreateFileMappingA(texturePackFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (texturePackMapping == NULL) goto fail;
    texturePackView = (const unsigned char *)MapViewOfFile(texturePackMapping, FILE_MAP_READ, 0, 0, 0);
    if (texturePackView == NULL) goto fail;
    header = (const TexturePackHeader *)texturePackView;
    if (memcmp(header->magic, "HWTPACK1", 8) != 0 || header->version != 1 ||
        header->dataOffset < sizeof(TexturePackHeader) ||
        header->dataOffset > texturePackLength ||
        (unsigned long long)header->count * sizeof(TexturePackEntry) >
            header->dataOffset - sizeof(TexturePackHeader))
    {
        goto fail;
    }
    texturePackEntries = (const TexturePackEntry *)(texturePackView + sizeof(TexturePackHeader));
    for (i = 0; i < header->count; ++i)
    {
        const TexturePackEntry *entry = &texturePackEntries[i];
        if (memchr(entry->path, 0, sizeof(entry->path)) == NULL ||
            entry->offset < header->dataOffset || entry->offset > texturePackLength ||
            entry->size > texturePackLength - entry->offset ||
            (i > 0 && strcmp(texturePackEntries[i - 1].path, entry->path) >= 0)) goto fail;
    }
    texturePackHeader = header;
    atexit(texturePackLogMetrics);
    fprintf(stderr, "[TexturePack] memory-mapped %u DDS assets (%.1f MiB).\n",
            header->count, (double)texturePackLength / (1024.0 * 1024.0));
    return TRUE;
fail:
    texturePackHeader = NULL;
    texturePackEntries = NULL;
    if (texturePackView != NULL) { UnmapViewOfFile(texturePackView); texturePackView = NULL; }
    if (texturePackMapping != NULL) { CloseHandle(texturePackMapping); texturePackMapping = NULL; }
    if (texturePackFile != INVALID_HANDLE_VALUE) { CloseHandle(texturePackFile); texturePackFile = INVALID_HANDLE_VALUE; }
    texturePackLength = 0;
    return TRUE;
}

static void texturePackInitialize(void)
{
    InitOnceExecuteOnce(&texturePackInitOnce, texturePackInitializeOnce, NULL, NULL);
}

static const TexturePackEntry *texturePackFind(const char *path)
{
    char key[256];
    sdword low, high;
    texturePackInitialize();
    if (texturePackHeader == NULL) return NULL;
    texturePackNormalize(path, key);
    low = 0; high = (sdword)texturePackHeader->count - 1;
    while (low <= high)
    {
        sdword middle = low + (high - low) / 2;
        int comparison = strcmp(key, texturePackEntries[middle].path);
        if (comparison == 0) return &texturePackEntries[middle];
        if (comparison < 0) high = middle - 1; else low = middle + 1;
    }
    return NULL;
}

sdword texturePackFileSize(const char *path)
{
    const TexturePackEntry *entry = texturePackFind(path);
    if (entry == NULL || entry->size > 0x7fffffffu ||
        entry->offset + entry->size > texturePackLength) return -1;
    return (sdword)entry->size;
}

bool32 texturePackRead(const char *path, void *destination, sdword size)
{
    const TexturePackEntry *entry = texturePackFind(path);
    clock_t started;
    if (entry == NULL || destination == NULL || size != (sdword)entry->size ||
        entry->offset + entry->size > texturePackLength) return FALSE;
    started = clock();
    memcpy(destination, texturePackView + entry->offset, entry->size);
    ++texturePackReadCalls;
    texturePackReadBytes += entry->size;
    texturePackReadMilliseconds +=
        (double)(clock() - started) * 1000.0 / CLOCKS_PER_SEC;
    return TRUE;
}
#else
sdword texturePackFileSize(const char *path) { (void)path; return -1; }
bool32 texturePackRead(const char *path, void *destination, sdword size)
{ (void)path; (void)destination; (void)size; return FALSE; }
bool32 texturePackLooseOverridesEnabled(void) { return TRUE; }
#endif
