#ifndef HW_TEXTURE_PACK_H
#define HW_TEXTURE_PACK_H

#include "types.h"

/* Read-only, memory-mapped DDS archive. Loose files remain authoritative;
   File.c consults this only after the normal filesystem lookup fails. */
sdword texturePackFileSize(const char *path);
bool32 texturePackRead(const char *path, void *destination, sdword size);
bool32 texturePackLooseOverridesEnabled(void);

#endif
