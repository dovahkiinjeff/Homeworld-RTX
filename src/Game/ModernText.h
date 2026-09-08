#ifndef HW_MODERN_TEXT_H
#define HW_MODERN_TEXT_H

#include "Types.h"
#include "Color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolution-native Windows text.  Text is rasterized once at its final
   screen pixel height and cached as an alpha texture; it is never enlarged
   from a low-resolution Homeworld font atlas. */
bool32 hwModernTextDraw(const char *sourceFont,
                        sdword pixelHeight,
                        sdword x,
                        sdword y,
                        color tint,
                        const char *text,
                        sdword maxCharacters);

sdword hwModernTextWidth(const char *sourceFont,
                         sdword pixelHeight,
                         const char *text,
                         sdword maxCharacters);

void hwModernTextReset(void);

#ifdef __cplusplus
}
#endif

#endif
