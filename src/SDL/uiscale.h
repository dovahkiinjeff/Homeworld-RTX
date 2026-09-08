#ifndef HW_UI_SCALE_H
#define HW_UI_SCALE_H

#include "Types.h"

#define UI_LOGICAL_WIDTH  640
#define UI_LOGICAL_HEIGHT 480

/* Recomputes the logical UI canvas for the current drawable. The percentage
   is a modifier around the automatic scale: 100 is automatic, 50 is half of
   automatic, and 200 is twice automatic (subject to fitting on screen). */
void uiScaleConfigure(sdword outputWidth, sdword outputHeight, sdword percent);

real32 uiScaleGet(void);
sdword uiScaleOffsetX(void);
sdword uiScaleOffsetY(void);

sdword uiScalePositionX(sdword logicalX);
sdword uiScalePositionY(sdword logicalY);
sdword uiScaleSize(sdword logicalSize);

#endif
