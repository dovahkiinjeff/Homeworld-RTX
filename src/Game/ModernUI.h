#ifndef ___MODERN_UI_H
#define ___MODERN_UI_H

#include "Color.h"
#include "FEFlow.h"
#include "Region.h"

struct Ship;

/*
 * Homeworld Modern's interface is deliberately code-native.  FEMan still
 * supplies strings and callbacks for most legacy screens, but none of its
 * original decorative bitmaps or control chrome are rendered.
 */

bool32 modernUIOwnsScreen(const char *screenName);
void modernUIApplyPalette(void);
/* Recompose legacy FEMan controls into the responsive modern shell while
 * retaining their original callbacks, list models and game-side state. */
void modernUIPrepareScreen(fescreen *screen);
regionhandle modernUICreateOwnedScreen(regionhandle parent, fescreen *screen);
void modernUIOwnedScreenDeleted(const fescreen *screen);

color modernUITextColor(const regionhandle region);
void modernUIDrawBase(regionhandle region);
void modernUIDrawStaticRectangle(regionhandle region);
void modernUIDrawDecorative(regionhandle region);
void modernUIDrawButton(regionhandle region, bool32 checked, bool32 roundControl);
void modernUIDrawSlider(regionhandle region, bool32 vertical);
void modernUIDrawScrollBar(regionhandle region);
void modernUIDrawScrollBarButton(regionhandle region);
void modernUIDrawListFrame(regionhandle region);
void modernUIDrawTextEntryFrame(regionhandle region);
void modernUIDrawListSelection(rectangle *rect);
void modernUIDrawMenuItem(regionhandle region, bool32 checked, bool32 linked);
void modernUIDrawDivider(regionhandle region);

/* Selection-driven archive panel used by the tactical right-click menu. */
void modernUIShowShipDossier(struct Ship *ship, sdword selectionCount,
                             bool32 mixedSelection);
void modernUICloseShipDossier(void);

/* Live, non-pausing path-light/material tuning overlay. */
void modernUIToggleLightingEditor(void);
void modernUICloseLightingEditor(void);
bool32 modernUILightingEditorActive(void);

/* Persistent tactical command surface flanking the original center task bar. */
void modernUICommandBarStartup(void);
void modernUICommandBarBringToFront(void);
void modernUICommandBarShutdown(void);

#endif
