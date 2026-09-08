/*=============================================================================
    Name    : KeyBindings.h
    Purpose : This file contains all of the defines and headers for the Key
              bindings system.

    Created 8/04/1999 by Drew Dunlop
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#ifndef ___KEYBINDINGS_H
#define ___KEYBINDINGS_H

#include "FEFlow.h"
#include "font.h"

/*=============================================================================
    Defines :
=============================================================================*/


/*=============================================================================
    Structure Prototypes :
=============================================================================*/

typedef enum
{
    kbNEXT_FORMATION=0,
    kbBUILD_MANAGER,
    kbPREVIOUS_FOCUS,
    kbNEXT_FOCUS,
    kbDOCK,
    kbSELECT_ALL_VISIBLE,
    kbFOCUS,
    kbRESEARCH_MANAGER,
    kbHARVEST,
    kbMOVE,
    kbNEXT_TACTIC,
    kbPREVIOUS_TACTIC,
    kbSCUTTLE,
    kbSHIP_SPECIAL,
    kbTACTICAL_OVERLAY,
    kbMOTHERSHIP,
    kbKAMIKAZE,
    kbCANCEL_ORDERS,
    kbLAUNCH_MANAGER,

    /* Modern extended remap set.  These all route back through the existing
       gameplay key handlers by using their legacy action key as defaultkey. */
    kbSENSORS_MANAGER,
    kbFOCUS_LAST,
    kbHYPERSPACE,
    kbRETIRE,
    kbPAUSE_GAME,
    kbFLEET_VIEW,
    kbTACTICS_EVASIVE,
    kbTACTICS_NEUTRAL,
    kbTACTICS_AGGRESSIVE,
    kbFORMATION_DELTA,
    kbFORMATION_BROAD,
    kbFORMATION_DELTA3D,
    kbFORMATION_CLAW,
    kbFORMATION_WALL,
    kbFORMATION_SPHERE,
    kbFORMATION_PICKET,
    kbZOOM_IN,
    kbZOOM_OUT,

    /* Control groups keep their original Ctrl/Alt/Shift/double-press
       semantics because the remapper translates the physical key back to
       the legacy 0-9 action key before mainrgn processes modifiers. */
    kbCONTROL_GROUP_0,
    kbCONTROL_GROUP_1,
    kbCONTROL_GROUP_2,
    kbCONTROL_GROUP_3,
    kbCONTROL_GROUP_4,
    kbCONTROL_GROUP_5,
    kbCONTROL_GROUP_6,
    kbCONTROL_GROUP_7,
    kbCONTROL_GROUP_8,
    kbCONTROL_GROUP_9,
    kbMULTIPLAYER_CHAT,
    kbCHAT_HISTORY_UP,
    kbCHAT_HISTORY_DOWN,
    kbSCREENSHOT,
    kbMUSIC_PREVIOUS,
    kbMUSIC_NEXT,

    kbTOTAL_COMMANDS
} CommandsToBind;

#define KB_NormalBound      0
#define KB_NotBound         1
#define KB_GetKeyPress      2

typedef struct kbBoundKeys
{
    udword      command;
    udword      defaultkey;
    udword      primarykey;
    udword      resettokey;
    udword      status;
} kbBoundKeys;


/*=============================================================================
    External Variables :
=============================================================================*/

extern fonthandle kbKeyListFont;

extern udword kbKeySavedKeys[kbTOTAL_COMMANDS];
extern udword kbKeyBindingsVersion;

/*=============================================================================
    Function Prototypes :
=============================================================================*/

void kbListWindowCB(char *string, featom *atom);
void kbKeyResetToDefault(char *string, featom *atom);

void kbPoolListWindowCB(char *string, featom *atom);

sdword kbCheckBindings(sdword keypressed);

void kbRestoreSavedSettings(void);
void kbSaveSettings(void);

udword kbKeyBoundToCommand(udword command);
bool32 kbCommandKeyIsHit(udword command);

void kbInitKeyBindings(void);

/* Native/modern options UI helpers.  These deliberately keep the binding
   table authoritative inside KeyBindings.c so alternate front ends can use
   the exact same validation, duplicate-key eviction and localized labels as
   the legacy list window without depending on its FE/listwindow internals. */
sdword kbModernCommandCount(void);
const char *kbModernCommandName(sdword index);
const char *kbModernKeyName(sdword index);
udword kbModernPrimaryKey(sdword index);
bool32 kbModernAssignKey(sdword index, udword keypressed);
void kbModernClearKey(sdword index);
void kbModernResetDefaults(void);

#endif
