#include "uiscale.h"

/* At 720p the original 640x480 UI is kept at 1x. It grows with vertical
   resolution after that: 1.5x at 1080p, 2x at 1440p, and 3x at 2160p.
   The final scale is always constrained to the drawable. */
#define UI_AUTO_REFERENCE_WIDTH  1280.0f
#define UI_AUTO_REFERENCE_HEIGHT 720.0f
#define UI_AUTO_MAX_SCALE        4.0f
#define UI_SCALE_PERCENT_MIN     50
#define UI_SCALE_PERCENT_MAX     200

static real32 uiCurrentScale = 1.0f;
static sdword uiCurrentOffsetX = 0;
static sdword uiCurrentOffsetY = 0;

static real32 uiMinimum(real32 a, real32 b)
{
    return a < b ? a : b;
}

static real32 uiMaximum(real32 a, real32 b)
{
    return a > b ? a : b;
}

static sdword uiRound(real32 value)
{
    return value >= 0.0f ? (sdword)(value + 0.5f) : (sdword)(value - 0.5f);
}

void uiScaleConfigure(sdword outputWidth, sdword outputHeight, sdword percent)
{
    real32 fitScale;
    real32 automaticScale;
    real32 modifier;
    sdword canvasWidth;
    sdword canvasHeight;

    if (outputWidth <= 0 || outputHeight <= 0)
    {
        uiCurrentScale = 1.0f;
        uiCurrentOffsetX = 0;
        uiCurrentOffsetY = 0;
        return;
    }

    if (percent < UI_SCALE_PERCENT_MIN)
    {
        percent = UI_SCALE_PERCENT_MIN;
    }
    else if (percent > UI_SCALE_PERCENT_MAX)
    {
        percent = UI_SCALE_PERCENT_MAX;
    }

    fitScale = uiMinimum((real32)outputWidth / (real32)UI_LOGICAL_WIDTH,
                         (real32)outputHeight / (real32)UI_LOGICAL_HEIGHT);
    automaticScale = uiMinimum((real32)outputWidth / UI_AUTO_REFERENCE_WIDTH,
                               (real32)outputHeight / UI_AUTO_REFERENCE_HEIGHT);
    automaticScale = uiMaximum(automaticScale, 1.0f);
    automaticScale = uiMinimum(automaticScale, UI_AUTO_MAX_SCALE);

    modifier = (real32)percent / 100.0f;
    uiCurrentScale = uiMinimum(automaticScale * modifier, fitScale);
    if (uiCurrentScale <= 0.0f)
    {
        uiCurrentScale = fitScale;
    }

    canvasWidth = uiRound((real32)UI_LOGICAL_WIDTH * uiCurrentScale);
    canvasHeight = uiRound((real32)UI_LOGICAL_HEIGHT * uiCurrentScale);
    uiCurrentOffsetX = (outputWidth - canvasWidth) / 2;
    uiCurrentOffsetY = (outputHeight - canvasHeight) / 2;
}

real32 uiScaleGet(void)
{
    return uiCurrentScale;
}

sdword uiScaleOffsetX(void)
{
    return uiCurrentOffsetX;
}

sdword uiScaleOffsetY(void)
{
    return uiCurrentOffsetY;
}

sdword uiScalePositionX(sdword logicalX)
{
    return uiCurrentOffsetX + uiRound((real32)logicalX * uiCurrentScale);
}

sdword uiScalePositionY(sdword logicalY)
{
    return uiCurrentOffsetY + uiRound((real32)logicalY * uiCurrentScale);
}

sdword uiScaleSize(sdword logicalSize)
{
    return uiRound((real32)logicalSize * uiCurrentScale);
}
