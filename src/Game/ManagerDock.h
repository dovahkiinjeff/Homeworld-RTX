#ifndef HW_MANAGER_DOCK_H
#define HW_MANAGER_DOCK_H

#include "FEFlow.h"

typedef enum HWManagerDockKind
{
    HW_MANAGER_DOCK_BUILD = 0,
    HW_MANAGER_DOCK_RESEARCH,
    HW_MANAGER_DOCK_LAUNCH
} HWManagerDockKind;

/* The scripted tutorial depends on the original full-screen coordinates. */
bool32 mdManagerDockEnabled(void);

/* Reflows an already-loaded manager FIB into the right-side gameplay dock. */
void mdPrepareManagerScreen(fescreen *screen, HWManagerDockKind kind);

/* Adds B/L/R toggle keys to an active manager screen. */
void mdInstallManagerToggleKeys(regionhandle baseRegion);

/* Closes any manager other than the requested one. */
void mdCloseOtherManagers(HWManagerDockKind requested);

/* Shared geometry for code-native panels which occupy the same right-side
 * command dock (for example the Ship Dossier). */
void mdGetPanelRect(rectangle *rect);
void mdCloseAllManagers(void);

#endif
