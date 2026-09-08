/*=============================================================================
    Name    : Taskbar.c
    Purpose : Code and data to take care of the task bar

    Created 10/7/1997 by lmoloney
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#include "TaskBar.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "CommandDefs.h"
#include "Debug.h"
#include "FEColour.h"
#include "font.h"
#include "FontReg.h"
#include "Globals.h"
#include "InfoOverlay.h"
#include "ModernUI.h"
#include "LaunchMgr.h"
#include "main.h"
#include "mainrgn.h"
#include "Memory.h"
#include "Objectives.h"
#include "ObjTypes.h"
#include "PiePlate.h"
#include "Select.h"
#include "Sensors.h"
#include "SinglePlayer.h"
#include "StringSupport.h"
#include "Subtitle.h"
#include "Tutor.h"
#include "UIControls.h"
#include "uiscale.h"
#include "Universe.h"
#include "utility.h"

#ifdef _MSC_VER
    #define strcasecmp _stricmp
#else
    #include <strings.h>
#endif




/*=============================================================================
    Defs:
=============================================================================*/

#define TB_ACTIVE_COLOR       colRGB(0,180,0)
#define TB_DIM_COLOR          colRGB(0,80,0)

#define TB_MOTHERSHIP_DIM_COLOR     colRGB(128,0,0)

#define TB_RUMarginRight      4
#define TB_ShipsMarginRight   4
#define TB_RefreshInterval    0.5f

#define TB_OffScreenAmount    800


/*=============================================================================
    Data:
=============================================================================*/

//dynamic hyperspace button variables
#define TB_Both         0
#define TB_Objectives   1
#define TB_Hyperspace   2

static sdword tbHypObjState = TB_Both;
//list of task bar buttons
taskbutton tbButtons[TB_MaxButtons];
//sdword tbNumberButtons = 0;

//fibfileheader *tbFileHeader;
regionhandle tbBaseRegion = NULL;               //base region of task bar window
regionhandle tbButtonBaseRegion = NULL;         //base region for the buttons
regionhandle tbBumperRegion = NULL;             //bumper region which is invisible

//info to bump the taskbar up/down
sdword tbBumpUpSpeed = TB_BumpUpSpeed;          //speed to move up/down
sdword tbBumpDownSpeed = TB_BumpDownSpeed;
sdword tbBumpFullHeight = 1;                    //height of bumper region when fully extended
sdword tbRegionsAttached = FALSE;               //flags wether or not the task bar regions are connected to the bumper region
sdword tbBumpSmooth = FALSE;                    //smooth or non-smooth bump of task bar
sdword tbBumpDirection = 0;                     //0 = stationary, 1 = up, 2 = down
sdword tbBumpPosition = 0;                      //0 = all the way down

//info for font printing
sdword tbDotWidth = 1;
sdword tbTextMarginY = 1;
fonthandle tbButtonCaptionFont = 0;                 //font for use with buttons
fonthandle tbObjectiveFont = 0;
fonthandle tbCompactFont = 0;
fonthandle tbTinyFont = 0;

bool32 tbDisable = FALSE;

bool32 tbForceTaskbarVar = FALSE;

bool32 tbTaskBarActive = FALSE;
//bool32 mommyblink;

BabyCallBack    *tbRefreshBaby=NULL;

listwindowhandle tbListWindow = NULL;
regionhandle tbListWindowRegion = NULL;
fescreen *tbScreen;

void tbListWindowInit(char *name, featom *atom);
bool32 tbRefreshBabyFunction(udword num, void *data, struct BabyCallBack *baby);

void tbMothershipIndicator(featom *atom, regionhandle region);
void tbMovingIndicator(featom *atom, regionhandle region);
void tbAttackingIndicator(featom *atom, regionhandle region);
void tbDockingIndicator(featom *atom, regionhandle region);
void tbGuardingIndicator(featom *atom, regionhandle region);
void tbOtherIndicator(featom* atom, regionhandle region);

static fonthandle tbChooseFontForText(const char *text, sdword maxWidth,
                                      fonthandle primary,
                                      fonthandle compact,
                                      fonthandle tiny)
{
    fonthandle save = fontCurrentGet();

    if (text == NULL || maxWidth <= 0) return primary;
    fontMakeCurrent(primary);
    if (fontWidth((char *)text) <= maxWidth)
    {
        fontMakeCurrent(save);
        return primary;
    }
    if (compact != 0)
    {
        fontMakeCurrent(compact);
        if (fontWidth((char *)text) <= maxWidth)
        {
            fontMakeCurrent(save);
            return compact;
        }
    }
    if (tiny != 0)
    {
        fontMakeCurrent(tiny);
        if (fontWidth((char *)text) <= maxWidth)
        {
            fontMakeCurrent(save);
            return tiny;
        }
    }
    fontMakeCurrent(save);
    return tiny != 0 ? tiny : (compact != 0 ? compact : primary);
}

static const char *tbAtomLocalizedText(const featom *atom)
{
    const char *source;
    udword language;

    if (atom == NULL || atom->type != FA_StaticText || atom->pData == NULL)
        return NULL;
    source = (const char *)atom->pData;
    for (language = 0; language < strCurLanguage; ++language)
    {
        source += strlen(source) + 1;
    }
    return source;
}

static bool32 tbStaticTextMatches(const featom *atom, const char *label)
{
    const char *source = tbAtomLocalizedText(atom);
    char normalized[96];
    size_t length;

    if (source == NULL || label == NULL) return FALSE;
    snprintf(normalized, sizeof(normalized), "%s", source);
    length = strlen(normalized);
    while (length > 0 &&
           (normalized[length - 1] == ':' ||
            isspace((unsigned char)normalized[length - 1])))
    {
        normalized[--length] = 0;
    }
    return strcasecmp(normalized, label) == 0;
}

static void tbMergeStaticLabelIntoUserRegion(const char *userRegionName,
                                             const char *label)
{
    featom *targetAtom;
    regionhandle targetRegion;
    featom *best = NULL;
    featom *fallback = NULL;
    sdword bestScore = SDWORD_Max;
    sdword fallbackScore = SDWORD_Max;
    sdword index;

    if (tbScreen == NULL || userRegionName == NULL || label == NULL) return;
    targetAtom = feAtomFindInScreen(tbScreen, (char *)userRegionName);
    if (targetAtom == NULL || targetAtom->region == NULL) return;
    targetRegion = (regionhandle)targetAtom->region;

    for (index = 1; index < tbScreen->nAtoms; ++index)
    {
        featom *candidate = &tbScreen->atoms[index];
        regionhandle candidateRegion;
        sdword targetCy;
        sdword candidateCy;
        sdword horizontalGap = 0;
        sdword score;

        bool32 exact;
        sdword targetHeight;
        sdword candidateHeight;
        if (candidate->type != FA_StaticText || candidate->region == NULL ||
            bitTest(candidate->flags, FAF_Hidden))
            continue;
        candidateRegion = (regionhandle)candidate->region;
        exact = tbStaticTextMatches(candidate, label);
        targetCy = (targetRegion->rect.y0 + targetRegion->rect.y1) / 2;
        candidateCy = (candidateRegion->rect.y0 + candidateRegion->rect.y1) / 2;
        targetHeight = targetRegion->rect.y1 - targetRegion->rect.y0;
        candidateHeight = candidateRegion->rect.y1 - candidateRegion->rect.y0;
        if (candidateRegion->rect.x1 < targetRegion->rect.x0)
            horizontalGap = targetRegion->rect.x0 - candidateRegion->rect.x1;
        else if (targetRegion->rect.x1 < candidateRegion->rect.x0)
            horizontalGap = candidateRegion->rect.x0 - targetRegion->rect.x1;
        score = abs(targetCy - candidateCy) * 4 + horizontalGap;
        if (exact && score < bestScore)
        {
            best = candidate;
            bestScore = score;
        }
        else if (!exact &&
                 abs(targetCy - candidateCy) <= max(targetHeight, candidateHeight) &&
                 horizontalGap <= max(targetRegion->rect.x1 - targetRegion->rect.x0, 20) * 8 &&
                 score < fallbackScore)
        {
            /* Localized/taskbar variants sometimes spell the label
               differently.  A same-row nearest-neighbour fallback keeps the
               geometry robust without relying on English FEM text. */
            fallback = candidate;
            fallbackScore = score;
        }
    }

    if (best == NULL) best = fallback;
    if (best != NULL && best->region != NULL)
    {
        regionhandle labelRegion = (regionhandle)best->region;
        if (labelRegion->rect.x0 < targetRegion->rect.x0)
            targetRegion->rect.x0 = labelRegion->rect.x0;
        if (labelRegion->rect.y0 < targetRegion->rect.y0)
            targetRegion->rect.y0 = labelRegion->rect.y0;
        if (labelRegion->rect.x1 > targetRegion->rect.x1)
            targetRegion->rect.x1 = labelRegion->rect.x1;
        if (labelRegion->rect.y1 > targetRegion->rect.y1)
            targetRegion->rect.y1 = labelRegion->rect.y1;

        /* The user-region callback now owns both the indicator/readout and
           its caption, so the old static text cannot clip independently. */
        bitSet(best->flags, FAF_Hidden);
        regDrawFunctionSet(labelRegion, NULL);
    }
}

static void tbModernizeTaskbarReadouts(void)
{
    tbMergeStaticLabelIntoUserRegion("TB_Moving", "MOVING");
    tbMergeStaticLabelIntoUserRegion("TB_Attacking", "ATTACKING");
    tbMergeStaticLabelIntoUserRegion("TB_Guarding", "GUARDING");
    tbMergeStaticLabelIntoUserRegion("TB_Docking", "DOCKING");
    tbMergeStaticLabelIntoUserRegion("TB_Other", "OTHER");
    tbMergeStaticLabelIntoUserRegion("TB_RUs", "RESOURCES");
}

static void tbHideStaticTextInsideRegion(const char *regionName)
{
    featom *targetAtom;
    regionhandle targetRegion;
    sdword index;

    if (tbScreen == NULL || regionName == NULL) return;
    targetAtom = feAtomFindInScreen(tbScreen, (char *)regionName);
    if (targetAtom == NULL || targetAtom->region == NULL) return;
    targetRegion = (regionhandle)targetAtom->region;

    for (index = 1; index < tbScreen->nAtoms; ++index)
    {
        featom *candidate = &tbScreen->atoms[index];
        regionhandle textRegion;
        sdword cx;
        sdword cy;
        if (candidate->type != FA_StaticText || candidate->region == NULL ||
            bitTest(candidate->flags, FAF_Hidden))
            continue;
        textRegion = (regionhandle)candidate->region;
        cx = (textRegion->rect.x0 + textRegion->rect.x1) / 2;
        cy = (textRegion->rect.y0 + textRegion->rect.y1) / 2;
        if (cx >= targetRegion->rect.x0 && cx < targetRegion->rect.x1 &&
            cy >= targetRegion->rect.y0 && cy < targetRegion->rect.y1)
        {
            bitSet(candidate->flags, FAF_Hidden);
            regDrawFunctionSet(textRegion, NULL);
        }
    }
}

static void tbModernizeTaskbarManagerButtons(void)
{
    /* ModernUI draws these captions itself with the compact HUD face.  Kill
       the separate legacy text atoms so they cannot produce SENSO... or
       overlap the live button geometry. */
    tbHideStaticTextInsideRegion("TB_SensorsManager");
    tbHideStaticTextInsideRegion("CSM_Build");
    tbHideStaticTextInsideRegion("CSM_Research");
    tbHideStaticTextInsideRegion("CSM_Launch");
}

static void tbHideStaticTextLabel(const char *label)
{
    sdword index;
    if (tbScreen == NULL || label == NULL) return;
    for (index = 1; index < tbScreen->nAtoms; ++index)
    {
        featom *atom = &tbScreen->atoms[index];
        if (atom->type != FA_StaticText || atom->region == NULL) continue;
        if (!tbStaticTextMatches(atom, label)) continue;
        bitSet(atom->flags, FAF_Hidden);
        regDrawFunctionSet((regionhandle)atom->region, NULL);
    }
}

static void tbHideLegacyCenterRegion(const char *name)
{
    featom *atom;
    if (tbScreen == NULL || name == NULL) return;
    atom = feAtomFindInScreen(tbScreen, (char *)name);
    if (atom == NULL) return;
    bitSet(atom->flags, FAF_Hidden);
    bitSet(atom->flags, FAF_Disabled);
    if (atom->region != NULL)
    {
        regionhandle region = (regionhandle)atom->region;
        /* These controls are being retired, not merely painted over.  Mark
           the already-created live region disabled as well so an invisible
           1999 status control cannot keep stealing mouse events from the new
           compact objective/utility layout. */
        bitSet(region->status, RSF_RegionDisabled);
        regDrawFunctionSet(region, NULL);
    }
}

static void tbSetLiveRegionRect(featom *atom, const rectangle *rect)
{
    regionhandle region;
    if (atom == NULL || atom->region == NULL || rect == NULL) return;
    region = (regionhandle)atom->region;
    region->rect = *rect;
    regRecursiveSetDirty(region);
}

static void tbApplyMinimalCenterLayout(void)
{
    static const char *managerNames[4] = {
        "TB_SensorsManager", "CSM_Build", "CSM_Research", "CSM_Launch"
    };
    rectangle base;
    rectangle objectiveRect;
    rectangle slot;
    featom *objectiveAtom;
    featom *ruAtom;
    listwindowhandle listwindow;
    sdword margin;
    sdword gap;
    sdword rowHeight;
    sdword rowY0;
    sdword rowY1;
    sdword rowWidth;
    sdword ruWidth;
    sdword buttonWidth;
    sdword x;
    sdword index;
    sdword barWidth = 0;
    sdword barGap = 0;
    sdword leftDeckWidth;
    sdword rightDeckWidth;
    sdword iconTrayWidth;
    sdword deckGap;

    if (tbScreen == NULL || tbBaseRegion == NULL) return;

    /* The 1999 center card occupied only a fixed middle slice, leaving large
       dead gaps between it and the modern left/right decks on wide screens.
       Make the live taskbar base exactly span the space between those decks.
       ModernUI uses the same width policy, so all three pieces now meet with
       one deliberate gap and no unused black islands. */
    deckGap = uiScaleSize(4);
    leftDeckWidth = (MAIN_WindowWidth * 34) / 100;
    if (leftDeckWidth > uiScaleSize(448)) leftDeckWidth = uiScaleSize(448);
    if (leftDeckWidth < uiScaleSize(300)) leftDeckWidth = uiScaleSize(300);
    if (leftDeckWidth > MAIN_WindowWidth / 2)
        leftDeckWidth = MAIN_WindowWidth / 2;
    rightDeckWidth = (MAIN_WindowWidth * 21) / 100;
    if (rightDeckWidth > uiScaleSize(300)) rightDeckWidth = uiScaleSize(300);
    if (rightDeckWidth < uiScaleSize(190)) rightDeckWidth = uiScaleSize(190);
    if (rightDeckWidth > MAIN_WindowWidth / 3)
        rightDeckWidth = MAIN_WindowWidth / 3;

    if (MAIN_WindowWidth - leftDeckWidth - rightDeckWidth - deckGap * 2 >
        uiScaleSize(260))
    {
        tbBaseRegion->rect.x0 = leftDeckWidth + deckGap;
        tbBaseRegion->rect.x1 = MAIN_WindowWidth - rightDeckWidth - deckGap;
    }
    base = tbBaseRegion->rect;

    /* Strip the old center card down to behavior only.  All of its fixed
       640x480-era captions, dividers and ornamental rectangles are obsolete
       once the live regions below are re-laid out, and leaving even one of
       them behind is what produced the overlapping/scribbled-looking block
       in the user's capture.  Objective rows are list-window content, not FE
       static-text atoms, so they are unaffected. */
    for (index = 1; index < tbScreen->nAtoms; ++index)
    {
        featom *legacy = &tbScreen->atoms[index];
        regionhandle legacyRegion;
        sdword cx;
        sdword cy;
        if (legacy->region == NULL ||
            (legacy->type != FA_StaticText &&
             legacy->type != FA_DecorativeRegion &&
             legacy->type != FA_OpaqueDecorativeRegion))
            continue;
        legacyRegion = (regionhandle)legacy->region;
        cx = (legacyRegion->rect.x0 + legacyRegion->rect.x1) / 2;
        cy = (legacyRegion->rect.y0 + legacyRegion->rect.y1) / 2;
        if (cx >= base.x0 && cx < base.x1 && cy >= base.y0 && cy < base.y1)
        {
            bitSet(legacy->flags, FAF_Hidden);
            regDrawFunctionSet(legacyRegion, NULL);
        }
    }

    margin = uiScaleSize(3);
    gap = uiScaleSize(3);
    rowHeight = uiScaleSize(20);
    if (rowHeight < 14) rowHeight = 14;
    if (rowHeight > (base.y1 - base.y0) / 2)
        rowHeight = (base.y1 - base.y0) / 2;
    rowY1 = base.y1 - margin;
    rowY0 = rowY1 - rowHeight;

    /* Reserve only the UPPER-RIGHT part of the center deck for the individual
       ship icon tray.  The five utility controls below it retain the full
       center width, exactly matching the user's annotated placement. */
    iconTrayWidth = (base.x1 - base.x0) * 32 / 100;
    if (iconTrayWidth > uiScaleSize(300)) iconTrayWidth = uiScaleSize(300);
    if (iconTrayWidth < uiScaleSize(160)) iconTrayWidth = uiScaleSize(160);
    if (iconTrayWidth > (base.x1 - base.x0) * 45 / 100)
        iconTrayWidth = (base.x1 - base.x0) * 45 / 100;

    /* The old movement/attack/guard/dock/other lamps and the separate
       mothership meter were the crowded block the user explicitly removed.
       They are now retired rather than shuffled around yet again. */
    tbHideLegacyCenterRegion("TB_Moving");
    tbHideLegacyCenterRegion("TB_Attacking");
    tbHideLegacyCenterRegion("TB_Guarding");
    tbHideLegacyCenterRegion("TB_Docking");
    tbHideLegacyCenterRegion("TB_Other");
    tbHideLegacyCenterRegion("TB_Mothership");
    tbHideLegacyCenterRegion("TB_Ships");
    tbHideStaticTextLabel("MOTHERSHIP:");

    /* Reclaim the entire upper center deck for objectives.  The list keeps
       its native callbacks and scrollbar; only its live geometry changes. */
    objectiveAtom = feAtomFindInScreen(tbScreen, "TB_ObjectivesWindowInit");
    if (objectiveAtom != NULL && objectiveAtom->region != NULL)
    {
        listwindow = (listwindowhandle)objectiveAtom->region;
        if (listwindow->scrollbar != NULL)
        {
            barWidth = listwindow->scrollbar->reg.rect.x1 -
                       listwindow->scrollbar->reg.rect.x0;
            barGap = listwindow->scrollbar->reg.rect.x0 -
                     listwindow->reg.rect.x1;
            if (barWidth < 1) barWidth = uiScaleSize(10);
            if (barGap < 0) barGap = uiScaleSize(2);
        }
        objectiveRect.x0 = base.x0 + margin;
        objectiveRect.y0 = base.y0 + margin;
        objectiveRect.x1 = base.x1 - margin - iconTrayWidth - gap -
                           barGap - barWidth;
        objectiveRect.y1 = rowY0 - gap;
        if (objectiveRect.x1 > objectiveRect.x0 + uiScaleSize(40) &&
            objectiveRect.y1 > objectiveRect.y0 + uiScaleSize(16))
        {
            sdword atomIndex;
            for (atomIndex = 1; atomIndex < tbScreen->nAtoms; ++atomIndex)
            {
                featom *decor = &tbScreen->atoms[atomIndex];
                regionhandle decorRegion;
                sdword cx;
                sdword cy;
                if (decor->region == NULL ||
                    (decor->type != FA_DecorativeRegion &&
                     decor->type != FA_OpaqueDecorativeRegion))
                    continue;
                decorRegion = (regionhandle)decor->region;
                cx = (decorRegion->rect.x0 + decorRegion->rect.x1) / 2;
                cy = (decorRegion->rect.y0 + decorRegion->rect.y1) / 2;
                if (cx >= objectiveRect.x0 && cx < objectiveRect.x1 &&
                    cy >= objectiveRect.y0 && cy < objectiveRect.y1)
                {
                    bitSet(decor->flags, FAF_Hidden);
                    regDrawFunctionSet(decorRegion, NULL);
                }
            }
            listwindow->reg.rect = objectiveRect;
            if (listwindow->scrollbar != NULL)
            {
                listwindow->scrollbar->reg.rect.x0 = objectiveRect.x1 + barGap;
                listwindow->scrollbar->reg.rect.x1 =
                    listwindow->scrollbar->reg.rect.x0 + barWidth;
                listwindow->scrollbar->reg.rect.y0 = objectiveRect.y0;
                listwindow->scrollbar->reg.rect.y1 = objectiveRect.y1;
                regRecursiveSetDirty(&listwindow->scrollbar->reg);
            }
            if (listwindow->itemheight > 0)
            {
                listwindow->MaxIndex =
                    (objectiveRect.y1 - objectiveRect.y0 -
                     listwindow->TitleHeight - 12) / listwindow->itemheight;
                if (listwindow->MaxIndex < 1) listwindow->MaxIndex = 1;
                uicListScrollBarAdjust(listwindow);
            }
            regRecursiveSetDirty(&listwindow->reg);
        }
    }

    /* One dense utility row replaces the discarded legacy status block:
       Sensors / Build / Research / Launch plus a self-contained RU readout.
       Every feature remains accessible without spending another row. */
    rowWidth = base.x1 - base.x0 - margin * 2;
    if (rowWidth <= 0) return;
    ruWidth = rowWidth * 23 / 100;
    if (ruWidth < uiScaleSize(96)) ruWidth = uiScaleSize(96);
    if (ruWidth > uiScaleSize(150)) ruWidth = uiScaleSize(150);
    if (ruWidth > rowWidth / 3) ruWidth = rowWidth / 3;
    buttonWidth = (rowWidth - ruWidth - gap * 4) / 4;
    if (buttonWidth < uiScaleSize(48))
    {
        buttonWidth = (rowWidth - gap * 4) / 5;
        ruWidth = buttonWidth;
    }
    x = base.x0 + margin;
    for (index = 0; index < 4; ++index)
    {
        featom *atom = feAtomFindInScreen(tbScreen, (char *)managerNames[index]);
        slot.x0 = x;
        slot.y0 = rowY0;
        slot.x1 = x + buttonWidth;
        slot.y1 = rowY1;
        tbSetLiveRegionRect(atom, &slot);
        x = slot.x1 + gap;
    }
    ruAtom = feAtomFindInScreen(tbScreen, "TB_RUs");
    slot.x0 = x;
    slot.y0 = rowY0;
    slot.x1 = base.x1 - margin;
    slot.y1 = rowY1;
    tbSetLiveRegionRect(ruAtom, &slot);
}

static void tbDrawCommandIndicator(regionhandle region, uword count,
                                   const char *label)
{
    rectangle lamp;
    rectangle labelRect;
    sdword height;
    sdword size;
    sdword padding = 3;
    fonthandle save;
    fonthandle labelFont;
    color lampColor = count > 0 ? TB_ACTIVE_COLOR : TB_DIM_COLOR;

    if (region == NULL) return;
    height = region->rect.y1 - region->rect.y0;
    size = (height * 58) / 100;
    if (size < 5) size = 5;
    if (size > height - 2) size = height - 2;

    lamp.x0 = region->rect.x0 + padding;
    lamp.x1 = lamp.x0 + size;
    lamp.y0 = region->rect.y0 + (height - size) / 2;
    lamp.y1 = lamp.y0 + size;
    if (count > 0) primRectSolid2(&lamp, lampColor);
    else primRectOutline2(&lamp, 1, lampColor);

    labelRect = region->rect;
    labelRect.x0 = lamp.x1 + padding;
    labelRect.x1 -= padding;
    if (labelRect.x1 <= labelRect.x0) return;
    labelFont = tbTinyFont != 0 ? tbTinyFont : tbCompactFont;
    save = fontMakeCurrent(labelFont);
    fontPrintCentreCentreRectangle(&labelRect, colRGB(143, 157, 157),
                                   (char *)label);
    fontMakeCurrent(save);
}

static void tbDrawLabelValue(regionhandle region, color c,
                             const char *label, const char *value)
{
    rectangle rect;
    rectangle labelRect;
    rectangle valueRect;
    fonthandle save;
    fonthandle sharedFont;
    fonthandle labelFont;
    fonthandle valueFont;
    char combined[128];
    sdword innerWidth;
    sdword gap = 6;
    sdword labelWidth;
    sdword valueWidth;

    if (region == NULL || label == NULL || value == NULL) return;
    rect = region->rect;
    rect.x0 += 4;
    rect.x1 -= 4;
    rect.y0 += 1;
    rect.y1 -= 1;
    innerWidth = rect.x1 - rect.x0;
    if (innerWidth <= 0) return;

    snprintf(combined, sizeof(combined), "%s  %s", label, value);
    sharedFont = tbTinyFont != 0 ? tbTinyFont : tbCompactFont;
    save = fontMakeCurrent(sharedFont);
    labelWidth = fontWidth((char *)label);
    valueWidth = fontWidth((char *)value);
    if (labelWidth + gap + valueWidth <= innerWidth)
    {
        sdword y = rect.y0 + ((rect.y1 - rect.y0) - fontHeight(NULL)) / 2;
        fontPrint(rect.x0, y, c, (char *)label);
        fontPrint(rect.x1 - valueWidth, y, c, (char *)value);
        fontMakeCurrent(save);
        return;
    }
    fontMakeCurrent(save);

    /* Very narrow layouts switch to two centred lines.  Nothing is clipped,
       and the RU value therefore never escapes the box. */
    labelRect = rect;
    valueRect = rect;
    labelRect.y1 = rect.y0 + (rect.y1 - rect.y0) / 2;
    valueRect.y0 = labelRect.y1;
    labelFont = tbTinyFont != 0 ? tbTinyFont : tbCompactFont;
    valueFont = tbTinyFont != 0 ? tbTinyFont : tbCompactFont;
    save = fontMakeCurrent(labelFont);
    fontPrintCentreCentreRectangle(&labelRect, c, (char *)label);
    fontMakeCurrent(valueFont);
    fontPrintCentreCentreRectangle(&valueRect, c, (char *)value);
    fontMakeCurrent(save);
}

void tbRUs(featom *atom, regionhandle region);
void tbTactics(char *name, featom *atom);
void tbTacticsEvasive(char *name, featom *atom);
void tbTacticsAggressive(char *name, featom *atom);
void tbTacticsNeutral(char *name, featom *atom);
void tbShips(featom *atom, regionhandle region);


void tbCalcTotalShipCommands(void);

void tbTaskBarInit(void);
void tbTaskBarEnd(void);
void feToggleButtonSetFromScreen(char *name, sdword bPressed, fescreen *screen);

regionhandle tbFindRegion(char* name);

// Found in objectives.c
extern Objective **objectives;
extern sdword objectivesUsed;


enum
{
    TB_TACTIC_NONE = 0,
    TB_TACTIC_AGGR,
    TB_TACTIC_NEUT,
    TB_TACTIC_EVAS,
};

featom *tbAtomA = NULL;
featom *tbAtomN = NULL;
featom *tbAtomE = NULL;

uword tbShipsMoving = 0;
uword tbShipsAttacking = 0;
uword tbShipsGuarding = 0;
uword tbShipsDocking = 0;
uword tbShipsOther = 0;

uword tbShipsEvas = 0;
uword tbShipsNeut = 0;
uword tbShipsAggr = 0;

// used for the objectives list window
// max width of the complete/incomplete words for wrapping
udword tbStatusWidth=0;
// timer for the selection of objectives in the window
real32 tbTimeOutSelect;


//uword tbCommonTactic = TB_TACTIC_NONE;

void tbSetupHyperspace(void);


/*=============================================================================
    Functions:
=============================================================================*/
/*-----------------------------------------------------------------------------
    Name        : tbBumperProcess
    Description : Region processor callback for handling the bumper region
    Inputs      : region - region to handle processing
                  ID - user-assigned ID set when region created
                  event - enumeration of event to be processed
                  data - additional event data (message specific)
    Outputs     : ...user defined...
    Return      : flags indicating further operation:
----------------------------------------------------------------------------*/
udword tbBumperProcess(regionhandle region, smemsize ID, udword event, udword data)
{
    if (tbDisable)
    {
        if (tbRegionsAttached)
        {
            regRegionScroll(tbBumperRegion, 0, tbBumpFullHeight);
            regMoveLinkChild(tbBaseRegion, NULL);
            tbRegionsAttached = FALSE;

            tbTaskBarEnd();
        }
        return 0;
    }

    if (event == RPE_Enter)
    {                                                       //if just entering region
        tbSetupHyperspace();

        if((tutorial==TUTORIAL_ONLY) && !tutEnable.bTaskbarOpen)
            return 0;

        if (piePointSpecMode != PSM_Idle)
            return 0;

        if ((mrHoldRight != mrNULL) && !tbForceTaskbarVar)
            return 0;

        if (tbBumpSmooth)
        {                                                   //if smooth-bump
            tbBumpDirection = 1;                            //start moving it up
            if (!tbRegionsAttached)
            {                                               //attach the task bar to bumper
                regMoveLinkChild(tbBaseRegion, tbBumperRegion);
                tbRegionsAttached = TRUE;

                tbCalcTotalShipCommands();
                tbRefreshBaby = taskCallBackRegister(tbRefreshBabyFunction, 0, NULL, (real32)TB_RefreshInterval);
                tbTaskBarActive = TRUE;
                //dbgMessage("Taskbar on ");

                tutGameMessage("Game_TaskbarOn");
            }
        }
        else
        {                                                   //else bump instantly
            if (!tbRegionsAttached)
            {
                regMoveLinkChild(tbBaseRegion, tbBumperRegion);
                regRegionScroll(tbBumperRegion, 0, -(tbBumpFullHeight));
                tbRegionsAttached = TRUE;

                tbTaskBarInit();

                tutGameMessage("Game_TaskbarOn");
            }
        }
    }
    else if (event == RPE_Exit)
    {
        if (gameIsRunning &&
            (mrRenderMainScreen || (smSensorsActive && !smFleetIntel)) &&
            tutorial != TUTORIAL_ONLY)
        {
            /* The modern Info manager is persistent; leaving the old mouse
               bumper no longer dismisses it during ordinary gameplay. */
            return 0;
        }
        if((tutorial==TUTORIAL_ONLY) &&
           (!tutEnable.bTaskbarClose) &&
           (!tbForceTaskbarVar))
            return 0;

        if (tbBumpSmooth)                                   //if just exiting region
        {                                                   //if smooth-bump
            tbBumpDirection = 2;                            //start moving down
        }
        else
        {                                                   //else bump instantly
            if (tbRegionsAttached)
            {
                regRegionScroll(tbBumperRegion, 0, tbBumpFullHeight);
                regMoveLinkChild(tbBaseRegion, NULL);
                tbRegionsAttached = FALSE;

                //regRegionDelete(tbBumperRegion);

                tbTaskBarEnd();
                tutGameMessage("Game_TaskbarOff");
            }
        }
    }
    tbForceTaskbarVar = FALSE;
    return(0);
}

/*-----------------------------------------------------------------------------
    Name        : tbForceTaskbar
    Description : Forces the taskbar on the screen by simulating a mouse enter event
    Inputs      : On - if TRUE, taskbar is forced on, if FALSE, taskbar is force off.
    Outputs     :
    Return      : void
----------------------------------------------------------------------------*/
// warning - I use tbBumperRegion just as a holder so that some region is
//           actually passed into the callback function.  tbBumperProcess
//           doesn't seem to do anything with the region parameter for now.
void tbForceTaskbar(bool32 On)
{
    tbForceTaskbarVar = TRUE;

    if (On)
    {
        tbBumperProcess(tbBumperRegion, 0, RPE_Enter, 0);
    }
    else
    {
        tbBumperProcess(tbBumperRegion, 0, RPE_Exit, 0);
    }
}

void tbEnsurePersistentInfo(void)
{
    if (!gameIsRunning ||
        !(mrRenderMainScreen || (smSensorsActive && !smFleetIntel)) || tbDisable ||
        tutorial == TUTORIAL_ONLY || tbBumperRegion == NULL ||
        tbBaseRegion == NULL)
    {
        return;
    }
    if (!tbRegionsAttached)
    {
        tbForceTaskbar(TRUE);
    }
}

bool32 tbInfoBarVisible(void)
{
    return tbBaseRegion != NULL && tbRegionsAttached && !tbDisable;
}

void tbGetInfoBarRect(rectangle *rect)
{
    if (rect == NULL)
    {
        return;
    }
    if (tbBaseRegion != NULL)
    {
        *rect = tbBaseRegion->rect;
    }
    else
    {
        rect->x0 = rect->x1 = 0;
        rect->y0 = rect->y1 = 0;
    }
}


void tbSensorsHook(void)
{
    if (smFleetIntel)
    {
        /* Fleet Intel remains a modal cinematic screen and keeps the legacy
           taskbar teardown behavior. */
        if (tbRegionsAttached)
        {
            regRegionScroll(tbBumperRegion, 0, tbBumpFullHeight);
            regMoveLinkChild(tbBaseRegion, NULL);
            tbRegionsAttached = FALSE;
            tbTaskBarEnd();
        }
        return;
    }

    /* RTX-0046: ordinary Sensors is now another gameplay viewport.  Keep the
       Info/task bar attached throughout the transition instead of visibly
       popping it out and back in. */
    if (!tbRegionsAttached && tbBumperRegion != NULL && tbBaseRegion != NULL)
    {
        tbForceTaskbar(TRUE);
    }
    if (tbBumperRegion != NULL)
    {
        regSiblingMoveToFront(tbBumperRegion);
    }
}

void tbSensorsBegin(char* name, featom* atom)
{
    smSensorsBegin(name, atom);
}

//this function doesn't actually draw anything, because each button is
//drawn independently.  However, it does move the task bar up/down if needed.
//!!! the motion will therefore be frame-rate dependent.!!!
void tbButtonRegionDraw(featom *atom, regionhandle region)
{
    sdword movement;
    if (tbBumpSmooth)
    {                                                       //only move smooth when it's enabled
        if (tbBumpDirection == 1)
        {                                                   //if bumping up
            movement = min(tbBumpUpSpeed, tbBumpFullHeight - TB_BumperHeight - tbBumpPosition);
            regRegionScroll(tbBumperRegion, 0, -movement);  //move up a bit
            tbBumpPosition += movement;
            if (tbBumpPosition >= tbBumpFullHeight - TB_BumperHeight)
            {                                               //if all the way up
                tbBumpDirection = 0;                        //no longer moving
            }
        }
        else if (tbBumpDirection == 2)
        {                                                   //if bumping down
            movement = min(tbBumpDownSpeed, tbBumpPosition);
            regRegionScroll(tbBumperRegion, 0, movement);   //move down a bit
            tbBumpPosition -= movement;
            if (tbBumpPosition <= 0)
            {                                               //if reaches the bottom
                tbBumpDirection = 0;                        //stop moving
                regMoveLinkChild(tbBaseRegion, NULL);       //detach the regions
                tbRegionsAttached = FALSE;
            }
        }
    }
}
void tbFleetManager(char *name, featom *atom)
{
    feScreenStart(ghMainRegion, "Fleet_manager");
}
void tbSensorsManager(char *name, featom *atom)
{
    bitClear(((regionhandle)atom->region)->status,RSF_CurrentSelected);
//    feScreenStart(ghMainRegion, "Sensors_manager");
    smSensorsBegin(NULL, NULL);
}


#if TB_SECRET_BUTTON
//exits the game, like right now
void tbExitImmediately(char *name, featom *atom)
{
    if (!multiPlayerGame)
    {
#ifdef HW_BUILD_FOR_DEBUGGING
        dbgMessagef("Quit game, baby!");
#endif
        utyCloseOK(NULL, 0, 0, 0);
    }
}
#endif


/*-----------------------------------------------------------------------------
    Name        : tbButtonDraw
    Description : Render callback for task bar buttons
    Inputs      : region - region we're rendering
    Outputs     : ...user defined...
    Return      : void
----------------------------------------------------------------------------*/
void tbButtonDraw(regionhandle region)
{
    fonthandle  fhSave;
    fonthandle  labelFont;
    sdword width, movement;
    char string[TB_MaxString];
    rectangle r;
    struct tagRegion styledRegion;

    movement = 0;
    if (tbBumpSmooth)
    {                                                       //only move smooth when it's enabled
        if (tbBumpDirection == 1)
        {                                                   //if bumping up
            movement = min(tbBumpUpSpeed, tbBumpFullHeight - TB_BumperHeight - tbBumpPosition);
        }
        else if (tbBumpDirection == 2)
        {                                                   //if bumping down
            movement = -min(tbBumpDownSpeed, tbBumpPosition);
        }
    }
    r.y0 = region->rect.y0 + movement;
    r.y1 = region->rect.y1 + movement;
    r.x0 = region->rect.x0;
    r.x1 = region->rect.x1;
    styledRegion = *region;
    styledRegion.rect = r;
    modernUIDrawButton(&styledRegion,
        bitTest(tbButtons[region->userID].flags, TBF_Pressed), FALSE);

    //tbButtons[region->userID];
    strcpy(string, tbButtons[region->userID].caption);      //copy the string to local copy

    dbgAssertOrIgnore(tbButtons[region->userID].caption);           //verify there is a string

    fhSave = fontCurrentGet();                              //save the current font
    width = r.x1 - r.x0 - 4;
    labelFont = tbChooseFontForText(string, width,
                                    tbButtonCaptionFont,
                                    tbCompactFont, tbTinyFont);
    fontMakeCurrent(labelFont);
    if (r.y0 < MAIN_WindowHeight && r.y1 > 0)
    {
        rectangle textRect = r;
        textRect.x0 += 2;
        textRect.x1 -= 2;
        /* The legacy taskbar used a fixed top-left baseline.  Center against
           the actual live button rectangle.  The caption face is selected
           dynamically so Sensors/Build/Research/Launch are always complete
           words rather than ellipsized fragments. */
        fontPrintCentreCentreRectangle(&textRect,
                  bitTest(tbButtons[region->userID].flags, TBF_Pressed) ?
                      colRGB(225, 222, 206) : colRGB(143, 157, 157), string);
    }

    fontMakeCurrent(fhSave);
}

/*-----------------------------------------------------------------------------
    Name        : tbButtonProcess
    Description : Region processor callback for task bar buttons
    Inputs      : region - region to handle processing
                  ID - user-assigned ID set when region created
                  event - enumeration of event to be processed
                  data - additional event data (message specific)
    Outputs     : ...user defined...
    Return      : flags indicating further operation:

----------------------------------------------------------------------------*/
udword tbButtonProcess(regionhandle region, smemsize ID, udword event, udword data)
{
    sdword index;
    switch (event)
    {                                                       //if button pressed
        case RPE_ReleaseLeft:
            for (index = 0; index < TB_MaxButtons; index++)
            {
                bitClear(tbButtons[index].flags, TBF_Pressed);//no buttons pressed
            }
            bitClear(tbButtons[ID].flags, TBF_HeldDown);    //button not depressed
            bitSet(tbButtons[ID].flags, TBF_Pressed);       //this button pressed
            tbButtons[ID].function(&tbButtons[ID], tbButtons[ID].userData);
            break;
        case RPE_ExitHoldLeft:
            bitClear(tbButtons[ID].flags, TBF_HeldDown);    //button not depressed
            break;
        case RPE_EnterHoldLeft:
            bitSet(tbButtons[ID].flags, TBF_HeldDown);      //button not depressed
            break;
    }
    return(0);
}

#if TB_TEST
void tbTestFunc(struct taskbutton *button, ubyte *userData)
{
    dbgMessagef("Test button %x", userData);
}
#endif
/*-----------------------------------------------------------------------------
    Name        : tbStartup
    Description : Startup the taskbar module.
    Inputs      : void
    Outputs     : loads in taskbar .FIB file, clears the button list etc.
    Return      : void
    Note        : mainrgn.c, feflow.c and region.c must be started at this point.
----------------------------------------------------------------------------*/
void tbStartup(void)
{
    fescreen *screen;
    sdword index;
    char   temp[100];
    fonthandle fhSave;
    regionhandle reg;

//    regionhandle reg;
//    fonthandle  fhSave;

//    tbFileHeader = feScreensLoad(TB_FileName);

    tbHypObjState = TB_Both;
    screen = feScreenFind(TB_ScreenName);
    tbScreen = screen;
    dbgAssertOrIgnore(screen->nAtoms >= 2);

    /* The original FIB keeps a second, secret/off-canvas strip around y=530.
       Resolution scaling can fold those regions back into the viewport as
       isolated outlines (notably TB_ExitImmediately).  The modern Info bar
       owns only the atoms contained by its declared base rectangle. */
    for (index = 1; index < screen->nAtoms; ++index)
    {
        featom *candidate = &screen->atoms[index];
        if (candidate->loadedY < screen->atoms[0].loadedY ||
            candidate->loadedY >= screen->atoms[0].loadedY +
                                  screen->atoms[0].loadedHeight)
        {
            bitSet(candidate->flags, FAF_Hidden);
            bitSet(candidate->flags, FAF_Disabled);
        }
    }
    tbBumpFullHeight = screen->atoms[0].height;
    tbBumperRegion = regChildAlloc(ghMainRegion, 0, 0,      //create the 'bumper' region
            0, MAIN_WindowWidth, tbBumpFullHeight + TB_BumperHeight,
            0, RPE_Enter | RPE_Exit);

    //... specify region handler

    regFunctionSet(tbBumperRegion, tbBumperProcess);
    feCallbackAdd(TB_FleetManager,  tbFleetManager);
    feCallbackAdd(TB_SensorsManager,tbSensorsManager);
//    feCallbackAdd(TB_InfoOverlay,   tbInfoOverlay);
    feCallbackAdd(TB_ObjectivesListWindow, tbListWindowInit);

    //feCallbackAdd("TB_Tactics", tbTactics);
    feCallbackAdd("TB_Tactics_E", tbTacticsEvasive);
    feCallbackAdd("TB_Tactics_A", tbTacticsAggressive);
    feCallbackAdd("TB_Tactics_N", tbTacticsNeutral);

    feDrawCallbackAdd("TB_Moving", tbMovingIndicator);
    feDrawCallbackAdd("TB_Guarding", tbGuardingIndicator);
    feDrawCallbackAdd("TB_Attacking", tbAttackingIndicator);
    feDrawCallbackAdd("TB_Docking", tbDockingIndicator);
    feDrawCallbackAdd("TB_Other", tbOtherIndicator);
    feDrawCallbackAdd("TB_Mothership", tbMothershipIndicator);
    feDrawCallbackAdd("TB_RUs", tbRUs);
    feDrawCallbackAdd("TB_Ships", tbShips);

//    feDrawCallbackAdd(TB_MissionButtons, tbButtonRegionDraw);
#if TB_SECRET_BUTTON
    feCallbackAdd("TB_ExitImmediately", tbExitImmediately);
#endif
    tbBaseRegion = feRegionsAdd(tbBumperRegion, screen, FALSE);    //add the task bar to the button region
    tbRegionsAttached = TRUE;
                                                            //move taskbar down off bottom of screen
    regRegionScroll(tbBumperRegion, 0, MAIN_WindowHeight - TB_BumperHeight);
    regMoveLinkChild(tbBaseRegion, NULL);
    tbRegionsAttached = FALSE;

    //now scan through the screen's atom list to find the user region for drawing buttons
    for (index = 1; index < screen->nAtoms; index++)
    {
        if (screen->atoms[index].type == FA_UserRegion)
        {
            tbButtonBaseRegion = (regionhandle)screen->atoms[index].region;
            break;
        }
    }

    /* Keep the hidden bar exactly one bar-height below the physical output.
       Opening it scrolls by tbBumpFullHeight, so its lower edge lands on the
       true last pixel at every aspect ratio and UI scale. */
    regRegionScroll(tbBaseRegion, 0,
        MAIN_WindowHeight + tbBumpFullHeight - tbBaseRegion->rect.y1);

    //load a font if needed
    tbButtonCaptionFont = frFontRegister(TB_FontFile);
    tbObjectiveFont = frFontRegister(TB_ObjectiveFontFile);
    tbCompactFont = frFontRegister("HW_EuroseCond_11.hff");
    if (tbCompactFont == 0) tbCompactFont = frFontRegister("hw_eurosecond_11.hff");
    if (tbCompactFont == 0) tbCompactFont = frFontRegister("Arial_12.hff");
    if (tbCompactFont == 0) tbCompactFont = tbButtonCaptionFont;
    tbTinyFont = frFontRegister("Small_Fonts_8.hff");
    if (tbTinyFont == 0) tbTinyFont = tbCompactFont;

    /* Fold legacy labels into their live readout regions.  From this point
       those regions own their whole caption/value geometry and can fit it at
       draw time without separate text boxes fighting for space. */
    tbModernizeTaskbarReadouts();
    tbModernizeTaskbarManagerButtons();
    tbApplyMinimalCenterLayout();


    // Calculate the max size of the objective status
    fhSave = fontMakeCurrent(tbObjectiveFont);

    sprintf(temp, "[%s] ", strGetString(strObjComplete));
    tbStatusWidth = fontWidth(temp);

    sprintf(temp, "[%s] ", strGetString(strObjIncomplete));
    if (fontWidth(temp) > tbStatusWidth)  tbStatusWidth = fontWidth(temp);

    fontMakeCurrent(fhSave);

    feAllCallOnCreate(screen);
//    tbNumberButtons = 0;

#if TB_TEST
    tbButtonCreate("0Caption", tbTestFunc, (ubyte *)0x00, 0);
    tbButtonCreate("1Caption", tbTestFunc, (ubyte *)0x01, 0);
    tbButtonCreate("2Caption", tbTestFunc, (ubyte *)0x02, 0);
//  tbButtonCreate("3Caption", tbTestFunc, (ubyte *)0x03, 0);
//  tbButtonCreate("4Caption", tbTestFunc, (ubyte *)0x04, 0);
//  tbButtonCreate("5Caption", tbTestFunc, (ubyte *)0x05, 0);
//  tbButtonCreate("6Caption", tbTestFunc, (ubyte *)0x06, 0);
//  tbButtonCreate("7Caption", tbTestFunc, (ubyte *)0x07, 0);
//  tbButtonCreate("8Caption", tbTestFunc, (ubyte *)0x08, 0);
//  tbButtonCreate("9Caption", tbTestFunc, (ubyte *)0x09, 0);
//  tbButtonCreate("10Caption", tbTestFunc, (ubyte *)0x10, 0);
    tbButtonListRefresh();
#endif

    reg = tbFindRegion("TB_SensorsManager");
    bitSet(reg->status, RSF_CantFocusTo);
    reg = tbFindRegion("CSM_Build");
    bitSet(reg->status, RSF_CantFocusTo);
    reg = tbFindRegion("CSM_Research");
    bitSet(reg->status, RSF_CantFocusTo);
    reg = tbFindRegion("CSM_Launch");
    bitSet(reg->status, RSF_CantFocusTo);
    reg = tbFindRegion("CSM_Hyperspace");
    if (reg != NULL)
    {
        featom *hypAtom = feAtomFindInScreen(tbScreen, "CSM_Hyperspace");
        bitSet(reg->status, RSF_CantFocusTo);
        bitSet(reg->status, RSF_RegionDisabled);
        regDrawFunctionSet(reg, NULL);
        if (hypAtom != NULL)
        {
            bitSet(hypAtom->flags, FAF_Hidden);
            bitSet(hypAtom->flags, FAF_Disabled);
        }
    }

    modernUICommandBarStartup();
}

/*-----------------------------------------------------------------------------
    Name        : tbShutdown
    Description : Shuts down the task bar module.
    Inputs      : void
    Outputs     : Frees the task bar screen etc...
    Return      : void
----------------------------------------------------------------------------*/
void tbShutdown(void)
{
    sdword index;

    modernUICommandBarShutdown();
    PossiblyResetTaskbar();

    // free all the buttons
    for (index = 0; index < TB_MaxButtons; index++)
    {
        if (bitTest(tbButtons[index].flags,TBF_InUse))
        {
            tbButtonDelete(&tbButtons[index]);
        }
    }
//  while (tbNumberButtons > 0)
//  {
//      tbButtonDelete(&tbButtons[tbNumberButtons - 1]);    //free the button up
//  }
    //... free the regions if they are detached
    if (!tbRegionsAttached)                                 //if regions not attached
    {
        regRegionDelete(tbBaseRegion);                      //delete the base region and everything thereunder
    }
    regRegionDelete(tbBumperRegion);
//    feScreensDelete(tbFileHeader);                          //free the fe screen

    //uicListCleanUp(tbListWindow);
}

/*-----------------------------------------------------------------------------
    Name        : tbButtonDelete / tbButtonDeleteByData
    Description : Delete a button either by pointer or by user data reference
    Inputs      : button - used to directly free the button.
                  userData - used to search for the button to free.
    Outputs     : Frees the button and moves the rest of the list back one.
    Return      : void
----------------------------------------------------------------------------*/
void tbButtonDelete(taskbutton *button)
{
//    sdword index;

    dbgAssertOrIgnore(button - tbButtons >= 0 && button - tbButtons < TB_MaxButtons);
    dbgAssertOrIgnore(bitTest(tbButtons[button - tbButtons].flags, TBF_InUse));
//    dbgAssertOrIgnore(tbNumberButtons > 0);
    memFree(tbButtons[button - tbButtons].caption);
    regRegionDelete(tbButtons[button - tbButtons].reg);
    tbButtons[button - tbButtons].flags = 0;                //flag as no longer in use
//  for (index = button - tbButtons + 1; index < tbNumberButtons; index++)
//  {
//      tbButtons[index - 1] = tbButtons[index];            //move the whole list back one
//  }
//  tbNumberButtons--;                                      //one less button
}

void tbButtonDeleteByData(ubyte *userData)
{
    sdword index;

    for (index = 0; index < TB_MaxButtons; index++)
    {
        if (tbButtons[index].userData == userData)          //if this is the button
        {
            tbButtonDelete(&tbButtons[index]);
            return;
        }
    }
#if TB_ERROR_CHECKING
    dbgFatalf(DBG_Loc, "tbButtonDeleteByData: couldn't find button with userData 0x%x in list of %d", userData, TB_MaxButtons);
#endif
}

/*-----------------------------------------------------------------------------
    Name        : tbButtonSelect/tbButtonSelectByData
    Description : Select a button
    Inputs      : button - pointer to button to select
                  userData - data of button to search for
    Outputs     : Just sets the pressed bit.  Does not call the buttons user function
    Return      :
----------------------------------------------------------------------------*/
void tbButtonSelect(taskbutton *button)
{
    sdword index;
    for (index = 0; index < TB_MaxButtons; index++)
    {
        bitClear(tbButtons[index].flags, TBF_Pressed);      //no buttons pressed
    }
    bitSet(button->flags, TBF_Pressed);                     //this button pressed
}

void tbButtonSelectByData(ubyte *userData)
{
    sdword index;

    for (index = 0; index < TB_MaxButtons; index++)
    {
        if (tbButtons[index].userData == userData)          //if this is the button
        {
            tbButtonSelect(&tbButtons[index]);
            return;
        }
    }
#if TB_ERROR_CHECKING
    dbgFatalf(DBG_Loc, "tbButtonSelectByData: couldn't find button with userData 0x%x in list of %d", userData, TB_MaxButtons);
#endif
}

/*-----------------------------------------------------------------------------
    Name        : tbButtonCreate
    Description : Create a new button on the task bar
    Inputs      : caption - string to draw on the button.
                  function - function to call when it is hit
                  userData - pointer to user data passed back in the callback function.
    Outputs     : Allocates a new button and does some setup, but all resizing
                    is left to tbButtonListRefresh.
    Return      :
----------------------------------------------------------------------------*/
taskbutton *tbButtonCreate(char *caption, tbfunction function, ubyte *userData, udword flags)
{
    sdword index;

    for (index = 0; index < TB_MaxButtons; index++)
    {
        if (!bitTest(tbButtons[index].flags,TBF_InUse))
        {                                                   //if free button
            tbButtons[index].reg =                        //allocate the region structure for the button
                regChildAlloc(tbButtonBaseRegion, index, 0,
                    tbButtonBaseRegion->rect.y0, 2, tbButtonBaseRegion->rect.y1 -
                    tbButtonBaseRegion->rect.y0, 0, RPE_LeftClickButton);
            regFunctionSet(tbButtons[index].reg, tbButtonProcess);
            regDrawFunctionSet(tbButtons[index].reg, tbButtonDraw);
            tbButtons[index].flags = flags | TBF_InUse;
            tbButtons[index].caption = memStringDupe(caption);
            tbButtons[index].userData = userData;
            tbButtons[index].function = function;
            return(&tbButtons[index]);
        }
    }
#if TB_ERROR_CHECKING
    dbgFatalf(DBG_Loc, "Ran out of task bar buttons: %d", TB_MaxButtons);
#endif
    return(NULL);
//  tbButtons[tbNumberButtons].reg =                        //allocate the region structure for the button
//      regChildAlloc(tbButtonBaseRegion, tbNumberButtons, 0,
//          tbButtonBaseRegion->rect.y0, 2, tbButtonBaseRegion->rect.y1 -
//          tbButtonBaseRegion->rect.y0, 0, RPE_LeftClickButton);
//  regFunctionSet(tbButtons[tbNumberButtons].reg, tbButtonProcess);
//  regDrawFunctionSet(tbButtons[tbNumberButtons].reg, tbButtonDraw);
//  tbButtons[tbNumberButtons].flags = flags;
//  tbButtons[tbNumberButtons].caption = memStringDupe(caption);
//  tbButtons[tbNumberButtons].userData = userData;
//  tbButtons[tbNumberButtons].function = function;
//  tbNumberButtons++;
//  return(&tbButtons[tbNumberButtons - 1]);
}

/*-----------------------------------------------------------------------------
    Name        : tbButtonListRefresh
    Description : Resize all the buttons to fit in the TB_MissionButtons user region
    Inputs      : void
    Outputs     : adjusts x0/x1 of all buttons to fit
    Return      :
----------------------------------------------------------------------------*/
void tbButtonListRefresh(void)
{
    sdword index, x, width, count;

    for (index = count = 0; index < TB_MaxButtons; index++)         //for each button
    {
        if (bitTest(tbButtons[index].flags,TBF_InUse))
        {
            count++;
        }
    }
    if (count == 0)
    {
        return;
    }
    width = tbButtonBaseRegion->rect.x1 - tbButtonBaseRegion->rect.x0;//width of button area
    width = (width - TB_ButtonMarginLeft - TB_ButtonMarginRight +//minus margins
             TB_ButtonMarginTween) /                        //over the number of buttons
                count - TB_ButtonMarginTween;               //minus a margin per button
    width = min(width, TB_MaxButtonWidth);                  //max the width out
    x = tbButtonBaseRegion->rect.x0 + TB_ButtonMarginLeft;  //location of first button
    for (index = 0; index < TB_MaxButtons; index++)         //for each button
    {
        if (bitTest(tbButtons[index].flags,TBF_InUse))
        {
            tbButtons[index].reg->rect.x0 = x;
            tbButtons[index].reg->rect.x1 = x + width;      //set horiz extents
            x += width + TB_ButtonMarginTween;
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : tbObjectiveItemDraw
    Description : Draw the fleet objective text to list window (one line)
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void tbObjectiveItemDraw(rectangle *rect, listitemhandle data)
{
    sdword     x, y;
    fonthandle oldfont;
    uiclistitem  *listitem;
    tasklistitem  *taskitem = (tasklistitem *)(data->data);
    Objective *objective = (Objective *)((tasklistitem *)(data->data))->objective, *obj;
    char text[TBL_MaxCharsPerLine];
    color c = colBlack;
    Node *search;

    oldfont = fontMakeCurrent(tbObjectiveFont);

    x = rect->x0 + 3;
    y = rect->y0 + ((rect->y1 - rect->y0) - fontHeight(" ")) / 2 - 1;

    search = tbListWindow->listofitems.head;
    while (search != NULL)
    {
        listitem = (uiclistitem *)listGetStructOfNode(search);

        obj = (Objective *)((tasklistitem *)(listitem->data))->objective;
        if (obj != NULL)
        {
            if (obj->status)
                listitem->flags = 0;
        }

        search = search->next;
    }

    if (taskTimeElapsed > tbTimeOutSelect)
    {
        search = tbListWindow->listofitems.head;
        while (search != NULL)
        {
            listitem = (uiclistitem *)listGetStructOfNode(search);

            bitClear(listitem->flags, UICLI_Selected);

            search = search->next;
        }

        tbTimeOutSelect = REALlyBig;
    }

    switch (taskitem->type)
    {
        case TBL_PrimaryObj1st:
        case TBL_SecondaryObj1st:
            sprintf(text, "[%s]",
                    objective->status ? strGetString(strObjComplete) : strGetString(strObjIncomplete));

            if (bitTest(data->flags, UICLI_Selected))
                c = TB_SelectedColor;
            else
                c = (objective->status) ? TB_CompleteColor : TB_IncompleteColor;

            fontPrint(x, y, c, text);

            x += tbStatusWidth;
            sprintf(text, "%s", taskitem->descFrag);
            break;
        case TBL_PrimaryObj2nd:
        case TBL_SecondaryObj2nd:
            x += tbStatusWidth;
            sprintf(text, "%s", taskitem->descFrag);

            if (bitTest(data->flags, UICLI_Selected))
                c = TB_SelectedColor;
            else
                c = (objective->status) ? TB_CompleteColor : TB_IncompleteColor;
            break;
        case TBL_HeaderSecondary:
            c = TB_SelectedColor;
            sprintf(text, "%s", taskitem->descFrag);
            break;
        default:
            // invalid taskitem
            dbgMessage("This definetly shouldn't happen, call Drew");
            dbgAssertOrIgnore(FALSE);
            break;
    }

    fontPrint(x, y, c, text);

    fontMakeCurrent(oldfont);
}

void tbObjectivesHyperspace(ubyte *data)
{
    tbSetupHyperspace();
    tbObjectivesListCleanUp();
}

void tbObjectivesListAddItem(ubyte *data)
{
    Objective    *objective = (Objective *)data;
    tasklistitem *taskitem;
    sdword        numchopped, i;
    char          chopbuf[TBL_MaxCharsPerLine*4];
    char         *chopstrings[6];
    Node         *search;
    uiclistitem  *listitem;
    bool32          secondfound=FALSE;
    rectangle     rect;

    dbgAssertOrIgnore(strlen(objective->description) <= TBL_MaxCharsPerLine*3);

    // check for a secondary objective
    search = tbListWindow->listofitems.head;
    while (search != NULL)
    {
        listitem = listGetStructOfNode(search);

        if (((tasklistitem *)listitem->data)->type == TBL_HeaderSecondary)
        {
            secondfound = TRUE;
        }

        search = search->next;
    }

    rect = tbListWindow->reg.rect;
    rect.x0 += 3;
    rect.x1 -= (tbStatusWidth + 16);

    if (objective->primary == FALSE)
    {
        if (secondfound == FALSE)
        {
            // add the secondary objectives header to the end of the list
            taskitem = (tasklistitem *)memAlloc(sizeof(tasklistitem), "TaskBarObjective", NonVolatile);

            taskitem->objective = NULL;
            taskitem->type = TBL_HeaderSecondary;
            strcpy(taskitem->descFrag,strGetString(strobjSecondary));

            uicListAddItem(tbListWindow, (ubyte *)taskitem, 0, UICLW_AddToTail);
        }

        // chop up the objective
        numchopped   =  subStringsChop(&rect,
                                       tbObjectiveFont,
                                       strlen(objective->description),
                                       objective->description,
                                       chopbuf,
                                       chopstrings);

        // add all the chopped strings
        for (i=0;i<numchopped;i++)
        {
            taskitem = (tasklistitem *)memAlloc(sizeof(tasklistitem), "TaskBarObjective", NonVolatile);

            taskitem->objective = (struct Objective *)objective;
            if (i == 0) taskitem->type = TBL_SecondaryObj1st;
            else        taskitem->type = TBL_SecondaryObj2nd;

            strcpy(taskitem->descFrag, chopstrings[i]);

            uicListAddItem(tbListWindow, (ubyte *)taskitem, UICLI_CanSelect, UICLW_AddToTail);
        }
    }
    else
    {
        // chop up the objective
        numchopped   =  subStringsChop(&rect,
                                       tbObjectiveFont,
                                       strlen(objective->description),
                                       objective->description,
                                       chopbuf,
                                       chopstrings);

        // add all the chopped strings
        for (i=numchopped-1;i>=0;i--)
        {
            taskitem = (tasklistitem *)memAlloc(sizeof(tasklistitem), "TaskBarObjective", NonVolatile);

            taskitem->objective = (struct Objective *)objective;
            if (i == 0) taskitem->type = TBL_PrimaryObj1st;
            else        taskitem->type = TBL_PrimaryObj2nd;

            strcpy(taskitem->descFrag, chopstrings[i]);

            uicListAddItem(tbListWindow, (ubyte *)taskitem, UICLI_CanSelect, UICLW_AddToHead);
        }
    }
}

void tbObjectivesListRemoveItem(ubyte *data)
{
    Objective    *objective = (Objective*)data;
    Node         *search;
    uiclistitem  *listitem[4];
    sdword        numitems=-1, i;
    tasklistitem *taskitem;

    search = tbListWindow->listofitems.head;
    while (search != NULL)
    {
        taskitem = (tasklistitem *)((uiclistitem *)listGetStructOfNode(search))->data;

        if ( ((Objective *)taskitem->objective) == objective)
            listitem[++numitems] = (uiclistitem *)listGetStructOfNode(search);

        search = search->next;
    }

    for (i=numitems;i>=0;i--)
    {
        uicListRemoveItem(tbListWindow, listitem[i]);
    }
}

void tbObjectivesListCleanUp(void)
{
    Node         *search;
    tasklistitem *taskitem;

    search = tbListWindow->listofitems.head;
    while (search != NULL)
    {
        taskitem = (tasklistitem *)((uiclistitem *)listGetStructOfNode(search))->data;

        memFree(taskitem);

        search = search->next;
    }

    uicListCleanUp(tbListWindow);
}

/*-----------------------------------------------------------------------------
    Name        : tbListWindowInit
    Description :
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void tbListWindowInit(char *name, featom *atom)
{
    fonthandle oldfont;

    if (FEFIRSTCALL(atom))
    {
        oldfont = fontMakeCurrent(tbObjectiveFont);

        tbListWindow = (listwindowhandle)atom->pData;
        tbListWindowRegion = atom->region;

        uicListWindowInit(tbListWindow,
                          NULL,                             // title draw, no title
                          NULL,                             // title click process, no title
                          0,                                // title height, no title
                          tbObjectiveItemDraw,              // item draw function
                          fontHeight(" ") + (fontHeight(" ") >> 2) - 2, // item height
                          UICLW_CanSelect);

        fontMakeCurrent(oldfont);

        atom->status = 0;

        return;
    }
    else if (FELASTCALL(atom))
    {
        tbListWindowRegion = NULL;
//        tbListWindow = NULL;
        return;
    }
    else if (tbListWindow->message == CM_NewItemSelected)
    {
        tasklistitem *taskitem;
        Objective    *objective = (Objective*)(((tasklistitem *)tbListWindow->CurLineSelected->data)->objective);
        Node         *search;
        uiclistitem  *listitem;

        tbTimeOutSelect = taskTimeElapsed + 0.5;

        search = tbListWindow->listofitems.head;
        while (search != NULL)
        {
            listitem = (uiclistitem *)listGetStructOfNode(search);
            taskitem = (tasklistitem *)listitem->data;

            if ( ((Objective *)taskitem->objective) == objective)
                bitSet(listitem->flags, UICLI_Selected);
            else
                bitClear(listitem->flags, UICLI_Selected);

            search = search->next;
        }

        if (!objective->status)
        {
            poPopupFleetIntelligence(objective);
        }
    }
}

void tbMovingIndicator(featom *atom, regionhandle region)
{
    (void)atom;
    tbDrawCommandIndicator(region, tbShipsMoving, "MOVING");
}

void tbGuardingIndicator(featom *atom, regionhandle region)
{
    (void)atom;
    tbDrawCommandIndicator(region, tbShipsGuarding, "GUARDING");
}

void tbDockingIndicator(featom *atom, regionhandle region)
{
    (void)atom;
    tbDrawCommandIndicator(region, tbShipsDocking, "DOCKING");
}

void tbAttackingIndicator(featom *atom, regionhandle region)
{
    (void)atom;
    tbDrawCommandIndicator(region, tbShipsAttacking, "ATTACKING");
}

void tbOtherIndicator(featom* atom, regionhandle region)
{
    (void)atom;
    tbDrawCommandIndicator(region, tbShipsOther, "OTHER");
}

void tbBarDraw(rectangle *rect, color back, color fore, real32 percent)
//percent is actually 0.0 to 1.0
{
    rectangle temp;
    primRectSolid2(rect, back);

    if (percent > 1.0f)
    {
        percent = 1.0f;
    }

    temp.x0 = rect->x0;
    temp.y0 = rect->y0;
    temp.x1 = rect->x0 + (sdword)((rect->x1-rect->x0)*percent);
    temp.y1 = rect->y1;

    primRectSolid2(&temp, fore);
}

void tbMothershipIndicator(featom *atom, regionhandle region)
{
    color col,backcol;
    uword r,g;
    real32 percent;
    rectangle *rect = &region->rect;
    Ship *mommy;

    mommy = universe.curPlayerPtr->PlayerMothership;

    if (!mommy)
    {
        percent = 0.0f;
    }
    else
    {
        percent = mommy->health / mommy->staticinfo->maxhealth;
    }

    backcol  = colRGB(30,30,30);

    if (percent > 0.50f)
    {
        g = 255;
        //r = (uword)((1 - percent)*(510));
        r = 0;
    }
    else
    {
        if (percent < 0.25f)
        {
            r = 255;
            g = 0;
        }
        else
        {
            //g = (uword)((percent-0.25f)*(1022));
            g = 255;
            r = 255;
        }
    }

    col = colRGB(r,g,0);

    //mommyblink = !mommyblink;
    //if ((percent < 0.25f) && mommyblink) col = TB_MOTHERSHIP_DIM_COLOR;

    tbBarDraw(rect, backcol, col, percent);
}

void tbCalcTotalShipCommands(void)
{
    sdword i;
    Ship *ship;
    CommandToDo *command;

    tbShipsEvas = 0;
    tbShipsNeut = 0;
    tbShipsAggr = 0;
    tbShipsMoving = 0;
    tbShipsAttacking = 0;
    tbShipsGuarding = 0;
    tbShipsDocking = 0;
    tbShipsOther = 0;

    for(i=0; i<selSelected.numShips; ++i)
    {
        ship = selSelected.ShipPtr[i];
        command = ship->command;

        if (command)
        {
            switch (command->ordertype.order)
            {
                case COMMAND_HALT:
                case COMMAND_MILITARY_PARADE:
                case COMMAND_LAUNCH_SHIP:
                case COMMAND_NULL:
                    if (bitTest(command->ordertype.attributes, COMMAND_MASK_PROTECTING))
                    {
                        tbShipsGuarding++;
                    }
                    break;

                case COMMAND_DOCK:
                    tbShipsDocking++;
                    break;

                case COMMAND_MOVE:
                    tbShipsMoving++;
                    break;

                case COMMAND_ATTACK:
                    tbShipsAttacking++;
                    break;

                default:
                    tbShipsOther++;
                    break;
            }
        }
        //now tactics
        switch(ship->tacticstype)
        {
        case Evasive:
            tbShipsEvas++;
            break;
        case Neutral:
            tbShipsNeut++;
            break;
        case Aggressive:
            tbShipsAggr++;
            break;
        default:
            break;
        }

        /*
        if ((evas + neut == 0) && (aggr > 0))
            tbCommonTactic = TB_TACTIC_AGGR;
        else if ((evas + aggr == 0) && (neut > 0))
            tbCommonTactic = TB_TACTIC_NEUT;
        else if ((neut + aggr == 0) && (evas > 0))
            tbCommonTactic = TB_TACTIC_EVAS;
        else
            tbCommonTactic = TB_TACTIC_NONE;
          */
    }
}

void tbRUs(featom *atom, regionhandle region)
{
    char value[32];
    color textColor = atom != NULL ? atom->borderColor : TB_SelectedColor;
    snprintf(value, sizeof(value), "%d", universe.curPlayerPtr->resourceUnits);
    feStaticRectangleDraw(region);
    tbDrawLabelValue(region, textColor, "RESOURCES", value);
}

void tbShips(featom *atom, regionhandle region)
{
    sdword width;
    fonthandle oldfont;
    //rectangle rect = region->rect;

    oldfont = fontMakeCurrent(tbButtonCaptionFont);

    //primModeSet2();
    //primRectSolid2(&rect, colRGB(0, 0, 0));

    width = fontWidthf("%d", universe.curPlayerPtr->totalships);//width of number
    feStaticRectangleDraw(region);                          //draw regular rectangle as backdrop
    fontPrintf(region->rect.x1 - width - TB_RUMarginRight,
               (region->rect.y1 - region->rect.y0 - fontHeight(NULL)) / 2 + region->rect.y0,
               atom->borderColor, "%d", universe.curPlayerPtr->totalships);

    fontMakeCurrent(oldfont);
}

bool32 tbRefreshBabyFunction(udword num, void *data, struct BabyCallBack *baby)
{
    if (!tbTaskBarActive) return (TRUE);

    tbCalcTotalShipCommands();

#ifdef DEBUG_STOMP
    regVerify(tbBaseRegion);
#endif
    bitSet(tbBaseRegion->status, RSF_DrawThisFrame);

    return !tbTaskBarActive;
}

void SetAtom(featom *atom, uword value)
{
    buttonhandle button = (buttonhandle)atom->region;

    bitSet(atom->status, value);
#ifdef DEBUG_STOMP
    regVerify(&button->reg);
#endif
    bitSet(button->reg.status, RSF_DrawThisFrame);

}

void tbSetTactics(TacticsType tactic)
{
    uword i;
    Ship *ship;

    for(i=0; i<selSelected.numShips; ++i)
    {
        ship = selSelected.ShipPtr[i];
        ship->tacticstype = tactic;
    }

    switch (tactic)
    {
    case Evasive:
        feToggleButtonSetFromScreen("TB_Tactics_E", TRUE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_A", FALSE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_N", FALSE, tbScreen);
        //SetAtom(tbAtomA, FALSE);
        //SetAtom(tbAtomN, FALSE);
        dbgMessage("E");
        break;

    case Neutral:
        feToggleButtonSetFromScreen("TB_Tactics_N", TRUE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_A", FALSE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_E", FALSE, tbScreen);
        //SetAtom(tbAtomA, FALSE);
        //SetAtom(tbAtomE, FALSE);
        dbgMessage("N");
    break;

    case Aggressive:
        feToggleButtonSetFromScreen("TB_Tactics_A", TRUE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_E", FALSE, tbScreen);
        feToggleButtonSetFromScreen("TB_Tactics_N", FALSE, tbScreen);
        //SetAtom(tbAtomE, FALSE);
        //SetAtom(tbAtomN, FALSE);
        dbgMessage("A");
        break;
    default:
        break;
    }
}

void tbTacticsEvasive(char *name, featom *atom)
{
    if (FEFIRSTCALL(atom))
    {
        feToggleButtonSetFromScreen(name, (tbShipsEvas > 0), tbScreen);
        tbAtomE = atom;

        dbgMessage("First Evasive call!");
    }
    else
    {
        tbSetTactics(Evasive);
    }
}

void tbTacticsNeutral(char *name, featom *atom)
{
    if (FEFIRSTCALL(atom))
    {
        feToggleButtonSetFromScreen(name, (tbShipsNeut > 0), tbScreen);
        tbAtomN = atom;
        dbgMessage("First Neutral call!");
    }
    else
    {
        tbSetTactics(Neutral);
    }
}

void tbTacticsAggressive(char *name, featom *atom)
{
    if (FEFIRSTCALL(atom))
    {
        feToggleButtonSetFromScreen(name, (tbShipsAggr > 0), tbScreen);
        tbAtomA = atom;
        dbgMessage("First Aggressive call!");
    }
    else
    {
        tbSetTactics(Aggressive);
    }
}

void tbRefreshTacticsCheckboxes(void)
{
    if (tbShipsAggr == 0)
        feToggleButtonSetFromScreen("TB_Tactics_A", FALSE, tbScreen);
    else
        feToggleButtonSetFromScreen("TB_Tactics_A", TRUE, tbScreen);

    if (tbShipsNeut == 0)
        feToggleButtonSetFromScreen("TB_Tactics_N", FALSE, tbScreen);
    else
        feToggleButtonSetFromScreen("TB_Tactics_N", TRUE, tbScreen);

    if (tbShipsEvas == 0)
        feToggleButtonSetFromScreen("TB_Tactics_E", FALSE, tbScreen);
    else
        feToggleButtonSetFromScreen("TB_Tactics_E", TRUE, tbScreen);
}

regionhandle tbFindRegion(char* name)
{
    sdword i;
    featom* atom;

    for (i = 0; i < tbScreen->nAtoms; i++)
    {
        atom = &tbScreen->atoms[i];
        if (atom->name != NULL &&
            strcasecmp(atom->name, name) == 0)
        {
            return (regionhandle)atom->region;
        }
    }
    return NULL;
}

void tbSetupHyperspace(void)
{
    regionhandle hs = tbFindRegion("CSM_Hyperspace");
    regionhandle lw = tbFindRegion("TB_ObjectivesWindowInit");
    if (hs == NULL || lw == NULL)
    {
        return;
    }
#ifdef HW_BUILD_FOR_DEBUGGING
    regVerify(hs);
    regVerify(lw);
#endif

    /* The giant legacy hyperspace card is retired.  Objectives always keep
       this center slot; the modern command deck exposes hyperspace as a
       compact bottom-bar button when the mission enables it. */
    if (tbHypObjState == TB_Both)
    {
        regRegionScroll(hs, 0, TB_OffScreenAmount);
        tbHypObjState = TB_Objectives;
    }
    else if (tbHypObjState == TB_Hyperspace)
    {
        regRegionScroll(hs, 0, TB_OffScreenAmount);
        regRegionScroll(lw, 0, -TB_OffScreenAmount);
        tbHypObjState = TB_Objectives;
    }

    /*    if (tbSetup)
    {
        //reset if still setup
        void tbResetHyperspace(void);
        tbResetHyperspace();
    }

    tbSetup = TRUE;

    tbSP = singlePlayerGame;

    if (singlePlayerGame)
    {
        tbHS = singlePlayerGameInfo.playerCanHyperspace;
        if (singlePlayerGameInfo.playerCanHyperspace)
        {
            regRegionScroll(lw, 0, TB_OffScreenAmount);
        }
        else
        {
            regRegionScroll(hs, 0, TB_OffScreenAmount);
        }
    }
    else
    {
        regRegionScroll(hs, 0, TB_OffScreenAmount);
    }*/
}

void tbResetHyperspace(void)
{
/*    regionhandle hs = tbFindRegion("CSM_Hyperspace");
    regionhandle lw = tbFindRegion("TB_ObjectivesWindowInit");
    if (hs == NULL || lw == NULL)
    {
        return;
    }
#ifdef HW_BUILD_FOR_DEBUGGING
    regVerify(hs);
    regVerify(lw);
#endif

    if (!tbSetup)
    {
        //don't reset if not setup
        return;
    }

    tbSetup = FALSE;

    if (tbSP)
    {
        if (tbHS)
        {
            regRegionScroll(lw, 0, -TB_OffScreenAmount);
        }
        else
        {
            regRegionScroll(hs, 0, -TB_OffScreenAmount);
        }
    }
    else
    {
        regRegionScroll(hs, 0, -TB_OffScreenAmount);
    }*/
}

void tbTaskBarInit(void)
{
    tbCalcTotalShipCommands();
    tbRefreshTacticsCheckboxes();
    tbRefreshBaby = taskCallBackRegister(tbRefreshBabyFunction, 0, NULL, (real32)TB_RefreshInterval);
    tbTaskBarActive = TRUE;
    //dbgMessage("Taskbar on ");

    tbSetupHyperspace();
}

void tbTaskBarEnd(void)
{
    tbTaskBarActive = FALSE;
    //dbgMessage("Taskbar off ");
}

void PossiblyResetTaskbar(void)
//this is called by gameEnd
{
    if (tbTaskBarActive)
    {
        if (tbRegionsAttached)
        {
            regRegionScroll(tbBumperRegion, 0, tbBumpFullHeight);
            regMoveLinkChild(tbBaseRegion, NULL);
            tbRegionsAttached = FALSE;

            tbTaskBarEnd();
        }
    }
}

void feToggleButtonSetFromScreen(char *name, sdword bPressed, fescreen *screen)
{
    featom *atom;
    buttonhandle button;

    atom = feAtomFindInScreen(screen,
                              name);                        //find first named atom in screen
    if (atom == NULL) return;
    dbgAssertOrIgnore(atom != NULL);

    button = (buttonhandle)atom->region;

    if (bPressed)
    {
        bitSet(atom->status, FAS_Checked);
#ifdef DEBUG_STOMP
        regVerify(&button->reg);
#endif
       bitSet(button->reg.status, RSF_DrawThisFrame);
    }
    else
    {
        bitClear(atom->status, FAS_Checked);
#ifdef DEBUG_STOMP
        regVerify(&button->reg);
#endif
        bitSet(button->reg.status, RSF_DrawThisFrame);
    }
}
