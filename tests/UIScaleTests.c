#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "uiscale.h"

static void expectMode(sdword width, sdword height, sdword percent,
                       real32 scale, sdword offsetX, sdword offsetY)
{
    uiScaleConfigure(width, height, percent);
    assert(fabsf(uiScaleGet() - scale) < 0.001f);
    assert(uiScaleOffsetX() == offsetX);
    assert(uiScaleOffsetY() == offsetY);
    assert(uiScalePositionX(0) == offsetX);
    assert(uiScalePositionY(0) == offsetY);
    assert(uiScalePositionX(UI_LOGICAL_WIDTH) ==
           offsetX + uiScaleSize(UI_LOGICAL_WIDTH));
    assert(uiScalePositionY(UI_LOGICAL_HEIGHT) ==
           offsetY + uiScaleSize(UI_LOGICAL_HEIGHT));
}

int main(void)
{
    expectMode(640, 480, 100, 1.0f, 0, 0);
    expectMode(1280, 720, 100, 1.0f, 320, 120);
    expectMode(1920, 1080, 100, 1.5f, 480, 180);
    expectMode(2560, 1440, 100, 2.0f, 640, 240);
    expectMode(3440, 1440, 100, 2.0f, 1080, 240);
    expectMode(3840, 2160, 100, 3.0f, 960, 360);
    expectMode(1920, 1080, 150, 2.25f, 240, 0);
    expectMode(320, 240, 100, 0.5f, 0, 0);

    puts("UI scaling policy tests passed.");
    return 0;
}
