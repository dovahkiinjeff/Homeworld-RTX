#include "ManagerDock.h"

#include <stdio.h>
#include <string.h>

#include "ConsMgr.h"
#include "Key.h"
#include "LaunchMgr.h"
#include "main.h"
#include "mainrgn.h"
#include "ResearchGUI.h"
#include "Tutor.h"
#include "uiscale.h"

#define MD_PANEL_WIDTH        360
#define MD_BUILD_PANEL_WIDTH  300
#define MD_BUILD_X_OFFSET     (MD_PANEL_WIDTH - MD_BUILD_PANEL_WIDTH)
#define MD_PANEL_HEIGHT       468
#define MD_PANEL_MARGIN 8
#define MD_PANEL_TOP    70

static sdword mdPanelLeft(void)
{
    return MAIN_WindowWidth - uiScaleSize(MD_PANEL_WIDTH) - uiScaleSize(MD_PANEL_MARGIN);
}

static sdword mdPanelTop(void)
{
    sdword top = uiScaleSize(MD_PANEL_TOP);
    sdword height = uiScaleSize(MD_PANEL_HEIGHT);
    if (top + height > MAIN_WindowHeight - uiScaleSize(MD_PANEL_MARGIN))
    {
        top = MAIN_WindowHeight - height - uiScaleSize(MD_PANEL_MARGIN);
    }
    if (top < uiScaleSize(MD_PANEL_MARGIN))
    {
        top = uiScaleSize(MD_PANEL_MARGIN);
    }
    return top;
}

static bool32 mdNameIs(const featom *atom, const char *name)
{
    return atom->name != NULL && strcmp(atom->name, name) == 0;
}

static bool32 mdNameStartsWith(const featom *atom, const char *prefix)
{
    return atom->name != NULL && strncmp(atom->name, prefix, strlen(prefix)) == 0;
}

static void mdSetHidden(featom *atom, bool32 hidden)
{
    if (hidden)
    {
        bitSet(atom->flags, FAF_Hidden);
    }
    else
    {
        bitClear(atom->flags, FAF_Hidden);
    }
}

static void mdPlace(featom *atom, sdword x, sdword y, sdword width, sdword height)
{
    atom->x = (sword)(mdPanelLeft() + uiScaleSize(x));
    atom->y = (sword)(mdPanelTop() + uiScaleSize(y));
    atom->width = (sword)uiScaleSize(width);
    atom->height = (sword)uiScaleSize(height);
}

static void mdPlaceBuild(featom *atom, sdword x, sdword y, sdword width, sdword height)
{
    mdPlace(atom, MD_BUILD_X_OFFSET + x, y, width, height);
}

static void mdPrepareBase(featom *atom, HWManagerDockKind kind)
{
    mdSetHidden(atom, FALSE);
    if (kind == HW_MANAGER_DOCK_BUILD)
    {
        mdPlaceBuild(atom, 0, 0, MD_BUILD_PANEL_WIDTH, MD_PANEL_HEIGHT);
    }
    else
    {
        mdPlace(atom, 0, 0, MD_PANEL_WIDTH, MD_PANEL_HEIGHT);
    }
}

static void mdPrepareBuild(featom *atom)
{
    /* Compact 300-pixel Build Manager.  The four action buttons are a 2x2
       grid so the panel no longer needs the old one-row horizontal footprint. */
    if (mdNameIs(atom, "CM_BuildShips"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 8, 396, 140, 32);
        return;
    }
    if (mdNameIs(atom, "CM_PauseJobs"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 152, 396, 140, 32);
        return;
    }
    if (mdNameIs(atom, "CM_CancelJobs"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 8, 432, 140, 32);
        return;
    }
    if (mdNameIs(atom, "CM_Close"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 152, 432, 140, 32);
        return;
    }
    if (atom->name == NULL || atom->name[0] == '\0' ||
        atom->type == FA_DecorativeRegion ||
        atom->type == FA_OpaqueDecorativeRegion ||
        mdNameStartsWith(atom, "SV_"))
    {
        mdSetHidden(atom, TRUE);
        return;
    }
    if (mdNameIs(atom, "CM_RUs"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 226, 96, 66, 24);
        return;
    }
    if (mdNameIs(atom, "CM_Ships"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 12, 148, 108, 212);
        return;
    }
    if (mdNameIs(atom, "CM_ShipNumbers"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 122, 148, 22, 212);
        return;
    }
    if (mdNameIs(atom, "CM_LeftArrowDraw"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 146, 148, 14, 212);
        return;
    }
    if (mdNameIs(atom, "CM_RightArrowDraw"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 162, 148, 14, 212);
        return;
    }
    if (mdNameIs(atom, "CM_ShipCost"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 178, 148, 32, 212);
        return;
    }
    if (mdNameIs(atom, "CM_Scroller"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 212, 144, 10, 220);
        return;
    }
    if (mdNameIs(atom, "CM_TotalShips"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 130, 368, 26, 22);
        return;
    }
    if (mdNameIs(atom, "CM_TotalCost"))
    {
        mdSetHidden(atom, FALSE);
        mdPlaceBuild(atom, 178, 368, 32, 22);
        return;
    }
    if (mdNameIs(atom, "CM_MotherShipDraw") ||
        (mdNameStartsWith(atom, "CM_Carrier") && strstr(atom->name, "Draw") != NULL))
    {
        /* Docked Build Manager uses the code-native dynamic factory bank. */
        mdSetHidden(atom, TRUE);
        return;
    }
    mdSetHidden(atom, TRUE);
}

static void mdPrepareLaunch(featom *atom)
{
    if (mdNameIs(atom, "LM_Launch"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 8, 396, 170, 32);
        return;
    }
    if (mdNameIs(atom, "LM_LaunchAll"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 182, 396, 170, 32);
        return;
    }
    if (mdNameIs(atom, "LM_Close"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 182, 432, 170, 32);
        return;
    }
    if (atom->name == NULL || atom->name[0] == '\0' ||
        atom->type == FA_DecorativeRegion ||
        atom->type == FA_OpaqueDecorativeRegion ||
        mdNameStartsWith(atom, "SV_"))
    {
        mdSetHidden(atom, TRUE);
        return;
    }
    if (mdNameIs(atom, "LM_ShipsToLaunch"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 12, 178, 332, 204);
        return;
    }
    if (mdNameIs(atom, "LM_FighterUsed"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 234, 124, 108, 18);
        return;
    }
    if (mdNameIs(atom, "LM_CorvetteUsed"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 234, 145, 108, 18);
        return;
    }
    if (mdNameIs(atom, "LM_MotherShipDraw"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 12, 58, 42, 42);
        return;
    }
    if (mdNameStartsWith(atom, "LM_Carrier") && strstr(atom->name, "Draw") != NULL)
    {
        sdword carrier = atom->name[10] - '0';
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 12 + carrier * 47, 58, 42, 42);
        return;
    }
    if (mdNameIs(atom, "LM_AutoLaunchM"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 25, 104, 16, 16);
        return;
    }
    if (mdNameStartsWith(atom, "LM_AutoLaunchC"))
    {
        sdword carrier = atom->name[14] - '0';
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 25 + carrier * 47, 104, 16, 16);
        return;
    }
    mdSetHidden(atom, TRUE);
}

static void mdPrepareResearch(featom *atom)
{
    if (mdNameIs(atom, "RM_ResearchItem"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 8, 396, 170, 32);
        return;
    }
    if (mdNameIs(atom, "RM_ClearLab"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 182, 396, 170, 32);
        return;
    }
    if (mdNameIs(atom, "RM_ExtendedInfo"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 8, 432, 170, 32);
        return;
    }
    if (mdNameIs(atom, "RM_ExitMenu"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 182, 432, 170, 32);
        return;
    }
    if (atom->name == NULL || atom->name[0] == '\0' ||
        atom->type == FA_DecorativeRegion ||
        atom->type == FA_OpaqueDecorativeRegion)
    {
        mdSetHidden(atom, TRUE);
        return;
    }
    if (mdNameIs(atom, "RM_TechListWindow"))
    {
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 12, 150, 320, 232);
        return;
    }
    if (mdNameStartsWith(atom, "RM_Lab") && strstr(atom->name, "Draw") != NULL &&
        strstr(atom->name, "Connector") == NULL)
    {
        sdword lab = atom->name[6] - '1';
        mdSetHidden(atom, FALSE);
        mdPlace(atom, 12 + lab * 56, 64, 52, 56);
        return;
    }
    if (mdNameStartsWith(atom, "RM_TechImage") ||
        mdNameStartsWith(atom, "RM_LabConnector") || atom->loadedX >= 260)
    {
        mdSetHidden(atom, TRUE);
        return;
    }
    mdSetHidden(atom, TRUE);
}

bool32 mdManagerDockEnabled(void)
{
    return tutorial != TUTORIAL_ONLY;
}

void mdPrepareManagerScreen(fescreen *screen, HWManagerDockKind kind)
{
    sdword index;
    if (screen == NULL || !mdManagerDockEnabled())
    {
        return;
    }

    mdPrepareBase(&screen->atoms[0], kind);
    for (index = 1; index < screen->nAtoms; ++index)
    {
        switch (kind)
        {
            case HW_MANAGER_DOCK_BUILD:
                mdPrepareBuild(&screen->atoms[index]);
                break;
            case HW_MANAGER_DOCK_RESEARCH:
                mdPrepareResearch(&screen->atoms[index]);
                break;
            case HW_MANAGER_DOCK_LAUNCH:
                mdPrepareLaunch(&screen->atoms[index]);
                break;
        }
        screen->atoms[index].region = NULL;
    }

    if (kind == HW_MANAGER_DOCK_BUILD)
    {
        fprintf(stderr, "[ManagerDock] %s docked at (%d,%d), %dx%d pixels.\n",
                screen->name, mdPanelLeft() + uiScaleSize(MD_BUILD_X_OFFSET),
                mdPanelTop(), uiScaleSize(MD_BUILD_PANEL_WIDTH),
                uiScaleSize(MD_PANEL_HEIGHT));
    }
    else
    {
        fprintf(stderr, "[ManagerDock] %s docked at (%d,%d), %dx%d pixels.\n",
                screen->name, mdPanelLeft(), mdPanelTop(),
                uiScaleSize(MD_PANEL_WIDTH), uiScaleSize(MD_PANEL_HEIGHT));
    }
}

void mdCloseOtherManagers(HWManagerDockKind requested)
{
    if (requested != HW_MANAGER_DOCK_BUILD && cmActive)
    {
        cmCloseIfOpen();
    }
    if (requested != HW_MANAGER_DOCK_RESEARCH && rmGUIActive)
    {
        rmCloseIfOpen();
    }
    if (requested != HW_MANAGER_DOCK_LAUNCH && lmActive)
    {
        lmCloseIfOpen();
    }
}

void mdGetPanelRect(rectangle *rect)
{
    if (rect == NULL)
    {
        return;
    }
    rect->x0 = mdPanelLeft();
    rect->y0 = mdPanelTop();
    rect->x1 = rect->x0 + uiScaleSize(MD_PANEL_WIDTH);
    rect->y1 = rect->y0 + uiScaleSize(MD_PANEL_HEIGHT);
}

void mdCloseAllManagers(void)
{
    if (cmActive)
    {
        cmCloseIfOpen();
    }
    if (rmGUIActive)
    {
        rmCloseIfOpen();
    }
    if (lmActive)
    {
        lmCloseIfOpen();
    }
}

static udword mdManagerToggleKey(regionhandle region, smemsize ID,
                                  udword event, udword data)
{
    (void)region;
    (void)data;
    if (event != RPE_KeyDown)
    {
        return 0;
    }
    switch (ID)
    {
        case BKEY:
            mrBuildShips(NULL, NULL);
            break;
        case RKEY:
            mrResearch(NULL, NULL);
            break;
        case LKEY:
            mrLaunch(NULL, NULL);
            break;
    }
    return 0;
}

void mdInstallManagerToggleKeys(regionhandle baseRegion)
{
    if (baseRegion == NULL || !mdManagerDockEnabled())
    {
        return;
    }
    regKeyChildAlloc(baseRegion, BKEY, RPE_KeyDown,
                     (regionfunction)mdManagerToggleKey, 1, BKEY);
    regKeyChildAlloc(baseRegion, RKEY, RPE_KeyDown,
                     (regionfunction)mdManagerToggleKey, 1, RKEY);
    regKeyChildAlloc(baseRegion, LKEY, RPE_KeyDown,
                     (regionfunction)mdManagerToggleKey, 1, LKEY);
}
