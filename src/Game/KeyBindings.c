/*=============================================================================
    Name    : KeyBindings.c
    Purpose : This file handles all of the logic for the key bindings

    Created 8/04/1999 by Drew Dunlop
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#include "KeyBindings.h"

#include "Debug.h"
#include "FEColour.h"
#include "FontReg.h"
#include "Globals.h"
#include "StringsOnly.h"
#include "StringSupport.h"
#include "Types.h"
#include "UIControls.h"

/*=============================================================================
    Defines :
=============================================================================*/


/*=============================================================================
    Data definitions :
=============================================================================*/

udword kbKeySavedKeys[kbTOTAL_COMMANDS] = {0};
udword kbKeyBindingsVersion = 0;

kbBoundKeys *kbKeyTable = NULL;

kbBoundKeys kbKeyTableEnglish[kbTOTAL_COMMANDS] =
{  // command                 defaultkey      primary key     key to reset to
    { kbNEXT_FORMATION      , TABKEY        , TABKEY        , TABKEY       , KB_NormalBound },
    { kbBUILD_MANAGER       , BKEY          , BKEY          , BKEY         , KB_NormalBound },
    { kbPREVIOUS_FOCUS      , CKEY          , CKEY          , CKEY         , KB_NormalBound },
    { kbNEXT_FOCUS          , VKEY          , VKEY          , VKEY         , KB_NormalBound },
    { kbDOCK                , DKEY          , DKEY          , DKEY         , KB_NormalBound },
    { kbSELECT_ALL_VISIBLE  , EKEY          , EKEY          , EKEY         , KB_NormalBound },
    { kbFOCUS               , FKEY          , FKEY          , FKEY         , KB_NormalBound },
    { kbRESEARCH_MANAGER    , RKEY          , RKEY          , RKEY         , KB_NormalBound },
    { kbHARVEST             , HKEY2         , HKEY2         , HKEY2        , KB_NormalBound },
    { kbMOVE                , MKEY          , MKEY          , MKEY         , KB_NormalBound },
    { kbNEXT_TACTIC         , RBRACK        , RBRACK        , RBRACK       , KB_NormalBound },
    { kbPREVIOUS_TACTIC     , LBRACK        , LBRACK        , LBRACK       , KB_NormalBound },
    { kbSCUTTLE             , SKEY          , SKEY          , SKEY         , KB_NormalBound },
    { kbSHIP_SPECIAL        , ZKEY          , ZKEY          , ZKEY         , KB_NormalBound },
    { kbTACTICAL_OVERLAY    , CAPSLOCKKEY   , CAPSLOCKKEY   , CAPSLOCKKEY  , KB_NormalBound },
    { kbMOTHERSHIP          , HOMEKEY       , HOMEKEY       , HOMEKEY      , KB_NormalBound },
    { kbKAMIKAZE            , KKEY          , KKEY          , KKEY         , KB_NormalBound },
    { kbCANCEL_ORDERS       , TILDEKEY      , TILDEKEY      , TILDEKEY     , KB_NormalBound },
    { kbLAUNCH_MANAGER      , LKEY          , LKEY          , LKEY         , KB_NormalBound },
    { kbSENSORS_MANAGER      , SPACEKEY      , SPACEKEY      , SPACEKEY      , KB_NormalBound },
    { kbFOCUS_LAST           , ENTERKEY      , ENTERKEY      , ENTERKEY      , KB_NormalBound },
    { kbHYPERSPACE           , JKEY          , JKEY          , JKEY          , KB_NormalBound },
    { kbRETIRE               , IKEY          , IKEY          , IKEY          , KB_NormalBound },
    { kbPAUSE_GAME           , PKEY          , PKEY          , PKEY          , KB_NormalBound },
    { kbFLEET_VIEW           , F1KEY         , F1KEY         , F1KEY         , KB_NormalBound },
    { kbTACTICS_EVASIVE      , F2KEY         , F2KEY         , F2KEY         , KB_NormalBound },
    { kbTACTICS_NEUTRAL      , F3KEY         , F3KEY         , F3KEY         , KB_NormalBound },
    { kbTACTICS_AGGRESSIVE   , F4KEY         , F4KEY         , F4KEY         , KB_NormalBound },
    { kbFORMATION_DELTA      , F5KEY         , F5KEY         , F5KEY         , KB_NormalBound },
    { kbFORMATION_BROAD      , F6KEY         , F6KEY         , F6KEY         , KB_NormalBound },
    { kbFORMATION_DELTA3D    , F7KEY         , F7KEY         , F7KEY         , KB_NormalBound },
    { kbFORMATION_CLAW       , F8KEY         , F8KEY         , F8KEY         , KB_NormalBound },
    { kbFORMATION_WALL       , F9KEY         , F9KEY         , F9KEY         , KB_NormalBound },
    { kbFORMATION_SPHERE     , F10KEY        , F10KEY        , F10KEY        , KB_NormalBound },
    { kbFORMATION_PICKET     , F11KEY        , F11KEY        , F11KEY        , KB_NormalBound },
    { kbZOOM_IN              , PLUSKEY       , PLUSKEY       , PLUSKEY       , KB_NormalBound },
    { kbZOOM_OUT             , MINUSKEY      , MINUSKEY      , MINUSKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_0      , ZEROKEY       , ZEROKEY       , ZEROKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_1      , ONEKEY        , ONEKEY        , ONEKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_2      , TWOKEY        , TWOKEY        , TWOKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_3      , THREEKEY      , THREEKEY      , THREEKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_4      , FOURKEY       , FOURKEY       , FOURKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_5      , FIVEKEY       , FIVEKEY       , FIVEKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_6      , SIXKEY        , SIXKEY        , SIXKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_7      , SEVENKEY      , SEVENKEY      , SEVENKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_8      , EIGHTKEY      , EIGHTKEY      , EIGHTKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_9      , NINEKEY       , NINEKEY       , NINEKEY       , KB_NormalBound },
    { kbMULTIPLAYER_CHAT     , TKEY          , TKEY          , TKEY          , KB_NormalBound },
    { kbCHAT_HISTORY_UP      , PAGEUPKEY     , PAGEUPKEY     , PAGEUPKEY     , KB_NormalBound },
    { kbCHAT_HISTORY_DOWN    , PAGEDOWNKEY   , PAGEDOWNKEY   , PAGEDOWNKEY   , KB_NormalBound },
    { kbSCREENSHOT           , SCROLLKEY     , SCROLLKEY     , SCROLLKEY     , KB_NormalBound },
    { kbMUSIC_PREVIOUS       , LESSTHAN      , LESSTHAN      , LESSTHAN      , KB_NormalBound },
    { kbMUSIC_NEXT           , GREATERTHAN   , GREATERTHAN   , GREATERTHAN   , KB_NormalBound },
};

kbBoundKeys kbKeyTableFrench[kbTOTAL_COMMANDS] =
{  // command                 defaultkey      primary key     key to reset to
    { kbNEXT_FORMATION      , TABKEY        , TABKEY        , TABKEY       , KB_NormalBound },
    { kbBUILD_MANAGER       , BKEY          , BKEY          , BKEY         , KB_NormalBound },
    { kbPREVIOUS_FOCUS      , CKEY          , XKEY          , XKEY         , KB_NormalBound },
    { kbNEXT_FOCUS          , VKEY          , VKEY          , VKEY         , KB_NormalBound },
    { kbDOCK                , DKEY          , AKEY          , AKEY         , KB_NormalBound },
    { kbSELECT_ALL_VISIBLE  , EKEY          , EKEY          , EKEY         , KB_NormalBound },
    { kbFOCUS               , FKEY          , FKEY          , FKEY         , KB_NormalBound },
    { kbRESEARCH_MANAGER    , RKEY          , RKEY          , RKEY         , KB_NormalBound },
    { kbHARVEST             , HKEY2         , CKEY          , CKEY         , KB_NormalBound },
    { kbMOVE                , MKEY          , DKEY          , DKEY         , KB_NormalBound },
    { kbNEXT_TACTIC         , RBRACK        , RBRACK        , RBRACK       , KB_NormalBound },
    { kbPREVIOUS_TACTIC     , LBRACK        , LBRACK        , LBRACK       , KB_NormalBound },
    { kbSCUTTLE             , SKEY          , SKEY          , SKEY         , KB_NormalBound },
    { kbSHIP_SPECIAL        , ZKEY          , ZKEY          , ZKEY         , KB_NormalBound },
    { kbTACTICAL_OVERLAY    , CAPSLOCKKEY   , CAPSLOCKKEY   , CAPSLOCKKEY  , KB_NormalBound },
    { kbMOTHERSHIP          , HOMEKEY       , HOMEKEY       , HOMEKEY      , KB_NormalBound },
    { kbKAMIKAZE            , KKEY          , KKEY          , KKEY         , KB_NormalBound },
    { kbCANCEL_ORDERS       , TILDEKEY      , TILDEKEY      , TILDEKEY     , KB_NormalBound },
    { kbLAUNCH_MANAGER      , LKEY          , LKEY          , LKEY         , KB_NormalBound },
    { kbSENSORS_MANAGER      , SPACEKEY      , SPACEKEY      , SPACEKEY      , KB_NormalBound },
    { kbFOCUS_LAST           , ENTERKEY      , ENTERKEY      , ENTERKEY      , KB_NormalBound },
    { kbHYPERSPACE           , JKEY          , JKEY          , JKEY          , KB_NormalBound },
    { kbRETIRE               , IKEY          , IKEY          , IKEY          , KB_NormalBound },
    { kbPAUSE_GAME           , PKEY          , PKEY          , PKEY          , KB_NormalBound },
    { kbFLEET_VIEW           , F1KEY         , F1KEY         , F1KEY         , KB_NormalBound },
    { kbTACTICS_EVASIVE      , F2KEY         , F2KEY         , F2KEY         , KB_NormalBound },
    { kbTACTICS_NEUTRAL      , F3KEY         , F3KEY         , F3KEY         , KB_NormalBound },
    { kbTACTICS_AGGRESSIVE   , F4KEY         , F4KEY         , F4KEY         , KB_NormalBound },
    { kbFORMATION_DELTA      , F5KEY         , F5KEY         , F5KEY         , KB_NormalBound },
    { kbFORMATION_BROAD      , F6KEY         , F6KEY         , F6KEY         , KB_NormalBound },
    { kbFORMATION_DELTA3D    , F7KEY         , F7KEY         , F7KEY         , KB_NormalBound },
    { kbFORMATION_CLAW       , F8KEY         , F8KEY         , F8KEY         , KB_NormalBound },
    { kbFORMATION_WALL       , F9KEY         , F9KEY         , F9KEY         , KB_NormalBound },
    { kbFORMATION_SPHERE     , F10KEY        , F10KEY        , F10KEY        , KB_NormalBound },
    { kbFORMATION_PICKET     , F11KEY        , F11KEY        , F11KEY        , KB_NormalBound },
    { kbZOOM_IN              , PLUSKEY       , PLUSKEY       , PLUSKEY       , KB_NormalBound },
    { kbZOOM_OUT             , MINUSKEY      , MINUSKEY      , MINUSKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_0      , ZEROKEY       , ZEROKEY       , ZEROKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_1      , ONEKEY        , ONEKEY        , ONEKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_2      , TWOKEY        , TWOKEY        , TWOKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_3      , THREEKEY      , THREEKEY      , THREEKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_4      , FOURKEY       , FOURKEY       , FOURKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_5      , FIVEKEY       , FIVEKEY       , FIVEKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_6      , SIXKEY        , SIXKEY        , SIXKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_7      , SEVENKEY      , SEVENKEY      , SEVENKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_8      , EIGHTKEY      , EIGHTKEY      , EIGHTKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_9      , NINEKEY       , NINEKEY       , NINEKEY       , KB_NormalBound },
    { kbMULTIPLAYER_CHAT     , TKEY          , TKEY          , TKEY          , KB_NormalBound },
    { kbCHAT_HISTORY_UP      , PAGEUPKEY     , PAGEUPKEY     , PAGEUPKEY     , KB_NormalBound },
    { kbCHAT_HISTORY_DOWN    , PAGEDOWNKEY   , PAGEDOWNKEY   , PAGEDOWNKEY   , KB_NormalBound },
    { kbSCREENSHOT           , SCROLLKEY     , SCROLLKEY     , SCROLLKEY     , KB_NormalBound },
    { kbMUSIC_PREVIOUS       , LESSTHAN      , LESSTHAN      , LESSTHAN      , KB_NormalBound },
    { kbMUSIC_NEXT           , GREATERTHAN   , GREATERTHAN   , GREATERTHAN   , KB_NormalBound },
};

kbBoundKeys kbKeyTableGerman[kbTOTAL_COMMANDS] =
{  // command                 defaultkey      primary key     key to reset to
    { kbNEXT_FORMATION      , TABKEY        , TABKEY        , TABKEY       , KB_NormalBound },
    { kbBUILD_MANAGER       , BKEY          , BKEY          , BKEY         , KB_NormalBound },
    { kbPREVIOUS_FOCUS      , CKEY          , CKEY          , CKEY         , KB_NormalBound },
    { kbNEXT_FOCUS          , VKEY          , VKEY          , VKEY         , KB_NormalBound },
    { kbDOCK                , DKEY          , DKEY          , DKEY         , KB_NormalBound },
    { kbSELECT_ALL_VISIBLE  , EKEY          , EKEY          , EKEY         , KB_NormalBound },
    { kbFOCUS               , FKEY          , FKEY          , FKEY         , KB_NormalBound },
    { kbRESEARCH_MANAGER    , RKEY          , RKEY          , RKEY         , KB_NormalBound },
    { kbHARVEST             , HKEY2         , HKEY2         , HKEY2        , KB_NormalBound },
    { kbMOVE                , MKEY          , WKEY          , WKEY         , KB_NormalBound },
    { kbNEXT_TACTIC         , RBRACK        , RBRACK        , RBRACK       , KB_NormalBound },
    { kbPREVIOUS_TACTIC     , LBRACK        , LBRACK        , LBRACK       , KB_NormalBound },
    { kbSCUTTLE             , SKEY          , SKEY          , SKEY         , KB_NormalBound },
    { kbSHIP_SPECIAL        , ZKEY          , ZKEY          , ZKEY         , KB_NormalBound },
    { kbTACTICAL_OVERLAY    , CAPSLOCKKEY   , CAPSLOCKKEY   , CAPSLOCKKEY  , KB_NormalBound },
    { kbMOTHERSHIP          , HOMEKEY       , HOMEKEY       , HOMEKEY      , KB_NormalBound },
    { kbKAMIKAZE            , KKEY          , KKEY          , KKEY         , KB_NormalBound },
    { kbCANCEL_ORDERS       , TILDEKEY      , TILDEKEY      , TILDEKEY     , KB_NormalBound },
    { kbLAUNCH_MANAGER      , LKEY          , LKEY          , LKEY         , KB_NormalBound },
    { kbSENSORS_MANAGER      , SPACEKEY      , SPACEKEY      , SPACEKEY      , KB_NormalBound },
    { kbFOCUS_LAST           , ENTERKEY      , ENTERKEY      , ENTERKEY      , KB_NormalBound },
    { kbHYPERSPACE           , JKEY          , JKEY          , JKEY          , KB_NormalBound },
    { kbRETIRE               , IKEY          , IKEY          , IKEY          , KB_NormalBound },
    { kbPAUSE_GAME           , PKEY          , PKEY          , PKEY          , KB_NormalBound },
    { kbFLEET_VIEW           , F1KEY         , F1KEY         , F1KEY         , KB_NormalBound },
    { kbTACTICS_EVASIVE      , F2KEY         , F2KEY         , F2KEY         , KB_NormalBound },
    { kbTACTICS_NEUTRAL      , F3KEY         , F3KEY         , F3KEY         , KB_NormalBound },
    { kbTACTICS_AGGRESSIVE   , F4KEY         , F4KEY         , F4KEY         , KB_NormalBound },
    { kbFORMATION_DELTA      , F5KEY         , F5KEY         , F5KEY         , KB_NormalBound },
    { kbFORMATION_BROAD      , F6KEY         , F6KEY         , F6KEY         , KB_NormalBound },
    { kbFORMATION_DELTA3D    , F7KEY         , F7KEY         , F7KEY         , KB_NormalBound },
    { kbFORMATION_CLAW       , F8KEY         , F8KEY         , F8KEY         , KB_NormalBound },
    { kbFORMATION_WALL       , F9KEY         , F9KEY         , F9KEY         , KB_NormalBound },
    { kbFORMATION_SPHERE     , F10KEY        , F10KEY        , F10KEY        , KB_NormalBound },
    { kbFORMATION_PICKET     , F11KEY        , F11KEY        , F11KEY        , KB_NormalBound },
    { kbZOOM_IN              , PLUSKEY       , PLUSKEY       , PLUSKEY       , KB_NormalBound },
    { kbZOOM_OUT             , MINUSKEY      , MINUSKEY      , MINUSKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_0      , ZEROKEY       , ZEROKEY       , ZEROKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_1      , ONEKEY        , ONEKEY        , ONEKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_2      , TWOKEY        , TWOKEY        , TWOKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_3      , THREEKEY      , THREEKEY      , THREEKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_4      , FOURKEY       , FOURKEY       , FOURKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_5      , FIVEKEY       , FIVEKEY       , FIVEKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_6      , SIXKEY        , SIXKEY        , SIXKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_7      , SEVENKEY      , SEVENKEY      , SEVENKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_8      , EIGHTKEY      , EIGHTKEY      , EIGHTKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_9      , NINEKEY       , NINEKEY       , NINEKEY       , KB_NormalBound },
    { kbMULTIPLAYER_CHAT     , TKEY          , TKEY          , TKEY          , KB_NormalBound },
    { kbCHAT_HISTORY_UP      , PAGEUPKEY     , PAGEUPKEY     , PAGEUPKEY     , KB_NormalBound },
    { kbCHAT_HISTORY_DOWN    , PAGEDOWNKEY   , PAGEDOWNKEY   , PAGEDOWNKEY   , KB_NormalBound },
    { kbSCREENSHOT           , SCROLLKEY     , SCROLLKEY     , SCROLLKEY     , KB_NormalBound },
    { kbMUSIC_PREVIOUS       , LESSTHAN      , LESSTHAN      , LESSTHAN      , KB_NormalBound },
    { kbMUSIC_NEXT           , GREATERTHAN   , GREATERTHAN   , GREATERTHAN   , KB_NormalBound },
};

kbBoundKeys kbKeyTableSpanish[kbTOTAL_COMMANDS] =
{  // command                 defaultkey      primary key     key to reset to
    { kbNEXT_FORMATION      , TABKEY        , TABKEY        , TABKEY       , KB_NormalBound },
    { kbBUILD_MANAGER       , BKEY          , BKEY          , BKEY         , KB_NormalBound },
    { kbPREVIOUS_FOCUS      , CKEY          , CKEY          , CKEY         , KB_NormalBound },
    { kbNEXT_FOCUS          , VKEY          , VKEY          , VKEY         , KB_NormalBound },
    { kbDOCK                , DKEY          , AKEY          , AKEY         , KB_NormalBound },
    { kbSELECT_ALL_VISIBLE  , EKEY          , EKEY          , EKEY         , KB_NormalBound },
    { kbFOCUS               , FKEY          , FKEY          , FKEY         , KB_NormalBound },
    { kbRESEARCH_MANAGER    , RKEY          , RKEY          , RKEY         , KB_NormalBound },
    { kbHARVEST             , HKEY2         , HKEY2         , HKEY2        , KB_NormalBound },
    { kbMOVE                , MKEY          , DKEY          , DKEY         , KB_NormalBound },
    { kbNEXT_TACTIC         , RBRACK        , RBRACK        , RBRACK       , KB_NormalBound },
    { kbPREVIOUS_TACTIC     , LBRACK        , LBRACK        , LBRACK       , KB_NormalBound },
    { kbSCUTTLE             , SKEY          , SKEY          , SKEY         , KB_NormalBound },
    { kbSHIP_SPECIAL        , ZKEY          , ZKEY          , ZKEY         , KB_NormalBound },
    { kbTACTICAL_OVERLAY    , CAPSLOCKKEY   , CAPSLOCKKEY   , CAPSLOCKKEY  , KB_NormalBound },
    { kbMOTHERSHIP          , HOMEKEY       , HOMEKEY       , HOMEKEY      , KB_NormalBound },
    { kbKAMIKAZE            , KKEY          , KKEY          , KKEY         , KB_NormalBound },
    { kbCANCEL_ORDERS       , TILDEKEY      , TILDEKEY      , TILDEKEY     , KB_NormalBound },
    { kbLAUNCH_MANAGER      , LKEY          , LKEY          , LKEY         , KB_NormalBound },
    { kbSENSORS_MANAGER      , SPACEKEY      , SPACEKEY      , SPACEKEY      , KB_NormalBound },
    { kbFOCUS_LAST           , ENTERKEY      , ENTERKEY      , ENTERKEY      , KB_NormalBound },
    { kbHYPERSPACE           , JKEY          , JKEY          , JKEY          , KB_NormalBound },
    { kbRETIRE               , IKEY          , IKEY          , IKEY          , KB_NormalBound },
    { kbPAUSE_GAME           , PKEY          , PKEY          , PKEY          , KB_NormalBound },
    { kbFLEET_VIEW           , F1KEY         , F1KEY         , F1KEY         , KB_NormalBound },
    { kbTACTICS_EVASIVE      , F2KEY         , F2KEY         , F2KEY         , KB_NormalBound },
    { kbTACTICS_NEUTRAL      , F3KEY         , F3KEY         , F3KEY         , KB_NormalBound },
    { kbTACTICS_AGGRESSIVE   , F4KEY         , F4KEY         , F4KEY         , KB_NormalBound },
    { kbFORMATION_DELTA      , F5KEY         , F5KEY         , F5KEY         , KB_NormalBound },
    { kbFORMATION_BROAD      , F6KEY         , F6KEY         , F6KEY         , KB_NormalBound },
    { kbFORMATION_DELTA3D    , F7KEY         , F7KEY         , F7KEY         , KB_NormalBound },
    { kbFORMATION_CLAW       , F8KEY         , F8KEY         , F8KEY         , KB_NormalBound },
    { kbFORMATION_WALL       , F9KEY         , F9KEY         , F9KEY         , KB_NormalBound },
    { kbFORMATION_SPHERE     , F10KEY        , F10KEY        , F10KEY        , KB_NormalBound },
    { kbFORMATION_PICKET     , F11KEY        , F11KEY        , F11KEY        , KB_NormalBound },
    { kbZOOM_IN              , PLUSKEY       , PLUSKEY       , PLUSKEY       , KB_NormalBound },
    { kbZOOM_OUT             , MINUSKEY      , MINUSKEY      , MINUSKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_0      , ZEROKEY       , ZEROKEY       , ZEROKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_1      , ONEKEY        , ONEKEY        , ONEKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_2      , TWOKEY        , TWOKEY        , TWOKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_3      , THREEKEY      , THREEKEY      , THREEKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_4      , FOURKEY       , FOURKEY       , FOURKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_5      , FIVEKEY       , FIVEKEY       , FIVEKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_6      , SIXKEY        , SIXKEY        , SIXKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_7      , SEVENKEY      , SEVENKEY      , SEVENKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_8      , EIGHTKEY      , EIGHTKEY      , EIGHTKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_9      , NINEKEY       , NINEKEY       , NINEKEY       , KB_NormalBound },
    { kbMULTIPLAYER_CHAT     , TKEY          , TKEY          , TKEY          , KB_NormalBound },
    { kbCHAT_HISTORY_UP      , PAGEUPKEY     , PAGEUPKEY     , PAGEUPKEY     , KB_NormalBound },
    { kbCHAT_HISTORY_DOWN    , PAGEDOWNKEY   , PAGEDOWNKEY   , PAGEDOWNKEY   , KB_NormalBound },
    { kbSCREENSHOT           , SCROLLKEY     , SCROLLKEY     , SCROLLKEY     , KB_NormalBound },
    { kbMUSIC_PREVIOUS       , LESSTHAN      , LESSTHAN      , LESSTHAN      , KB_NormalBound },
    { kbMUSIC_NEXT           , GREATERTHAN   , GREATERTHAN   , GREATERTHAN   , KB_NormalBound },
};

kbBoundKeys kbKeyTableItalian[kbTOTAL_COMMANDS] =
{  // command                 defaultkey      primary key     key to reset to
    { kbNEXT_FORMATION      , TABKEY        , TABKEY        , TABKEY       , KB_NormalBound },
    { kbBUILD_MANAGER       , BKEY          , BKEY          , BKEY         , KB_NormalBound },
    { kbPREVIOUS_FOCUS      , CKEY          , CKEY          , CKEY         , KB_NormalBound },
    { kbNEXT_FOCUS          , VKEY          , VKEY          , VKEY         , KB_NormalBound },
    { kbDOCK                , DKEY          , AKEY          , AKEY         , KB_NormalBound },
    { kbSELECT_ALL_VISIBLE  , EKEY          , EKEY          , EKEY         , KB_NormalBound },
    { kbFOCUS               , FKEY          , FKEY          , FKEY         , KB_NormalBound },
    { kbRESEARCH_MANAGER    , RKEY          , RKEY          , RKEY         , KB_NormalBound },
    { kbHARVEST             , HKEY2         , HKEY2         , HKEY2        , KB_NormalBound },
    { kbMOVE                , MKEY          , MKEY          , MKEY         , KB_NormalBound },
    { kbNEXT_TACTIC         , RBRACK        , RBRACK        , RBRACK       , KB_NormalBound },
    { kbPREVIOUS_TACTIC     , LBRACK        , LBRACK        , LBRACK       , KB_NormalBound },
    { kbSCUTTLE             , SKEY          , SKEY          , SKEY         , KB_NormalBound },
    { kbSHIP_SPECIAL        , ZKEY          , ZKEY          , ZKEY         , KB_NormalBound },
    { kbTACTICAL_OVERLAY    , CAPSLOCKKEY   , CAPSLOCKKEY   , CAPSLOCKKEY  , KB_NormalBound },
    { kbMOTHERSHIP          , HOMEKEY       , HOMEKEY       , HOMEKEY      , KB_NormalBound },
    { kbKAMIKAZE            , KKEY          , KKEY          , KKEY         , KB_NormalBound },
    { kbCANCEL_ORDERS       , TILDEKEY      , TILDEKEY      , TILDEKEY     , KB_NormalBound },
    { kbLAUNCH_MANAGER      , LKEY          , LKEY          , LKEY         , KB_NormalBound },
    { kbSENSORS_MANAGER      , SPACEKEY      , SPACEKEY      , SPACEKEY      , KB_NormalBound },
    { kbFOCUS_LAST           , ENTERKEY      , ENTERKEY      , ENTERKEY      , KB_NormalBound },
    { kbHYPERSPACE           , JKEY          , JKEY          , JKEY          , KB_NormalBound },
    { kbRETIRE               , IKEY          , IKEY          , IKEY          , KB_NormalBound },
    { kbPAUSE_GAME           , PKEY          , PKEY          , PKEY          , KB_NormalBound },
    { kbFLEET_VIEW           , F1KEY         , F1KEY         , F1KEY         , KB_NormalBound },
    { kbTACTICS_EVASIVE      , F2KEY         , F2KEY         , F2KEY         , KB_NormalBound },
    { kbTACTICS_NEUTRAL      , F3KEY         , F3KEY         , F3KEY         , KB_NormalBound },
    { kbTACTICS_AGGRESSIVE   , F4KEY         , F4KEY         , F4KEY         , KB_NormalBound },
    { kbFORMATION_DELTA      , F5KEY         , F5KEY         , F5KEY         , KB_NormalBound },
    { kbFORMATION_BROAD      , F6KEY         , F6KEY         , F6KEY         , KB_NormalBound },
    { kbFORMATION_DELTA3D    , F7KEY         , F7KEY         , F7KEY         , KB_NormalBound },
    { kbFORMATION_CLAW       , F8KEY         , F8KEY         , F8KEY         , KB_NormalBound },
    { kbFORMATION_WALL       , F9KEY         , F9KEY         , F9KEY         , KB_NormalBound },
    { kbFORMATION_SPHERE     , F10KEY        , F10KEY        , F10KEY        , KB_NormalBound },
    { kbFORMATION_PICKET     , F11KEY        , F11KEY        , F11KEY        , KB_NormalBound },
    { kbZOOM_IN              , PLUSKEY       , PLUSKEY       , PLUSKEY       , KB_NormalBound },
    { kbZOOM_OUT             , MINUSKEY      , MINUSKEY      , MINUSKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_0      , ZEROKEY       , ZEROKEY       , ZEROKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_1      , ONEKEY        , ONEKEY        , ONEKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_2      , TWOKEY        , TWOKEY        , TWOKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_3      , THREEKEY      , THREEKEY      , THREEKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_4      , FOURKEY       , FOURKEY       , FOURKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_5      , FIVEKEY       , FIVEKEY       , FIVEKEY       , KB_NormalBound },
    { kbCONTROL_GROUP_6      , SIXKEY        , SIXKEY        , SIXKEY        , KB_NormalBound },
    { kbCONTROL_GROUP_7      , SEVENKEY      , SEVENKEY      , SEVENKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_8      , EIGHTKEY      , EIGHTKEY      , EIGHTKEY      , KB_NormalBound },
    { kbCONTROL_GROUP_9      , NINEKEY       , NINEKEY       , NINEKEY       , KB_NormalBound },
    { kbMULTIPLAYER_CHAT     , TKEY          , TKEY          , TKEY          , KB_NormalBound },
    { kbCHAT_HISTORY_UP      , PAGEUPKEY     , PAGEUPKEY     , PAGEUPKEY     , KB_NormalBound },
    { kbCHAT_HISTORY_DOWN    , PAGEDOWNKEY   , PAGEDOWNKEY   , PAGEDOWNKEY   , KB_NormalBound },
    { kbSCREENSHOT           , SCROLLKEY     , SCROLLKEY     , SCROLLKEY     , KB_NormalBound },
    { kbMUSIC_PREVIOUS       , LESSTHAN      , LESSTHAN      , LESSTHAN      , KB_NormalBound },
    { kbMUSIC_NEXT           , GREATERTHAN   , GREATERTHAN   , GREATERTHAN   , KB_NormalBound },
};


kbBoundKeys kbKeySaveTable[kbTOTAL_COMMANDS];

// This is a look-up table that has a value of TRUE for
bool8 kbCanMapKey[KEY_TOTAL_KEYS];

// This is a look-up table for the strings
sdword kbKeyToString[KEY_TOTAL_KEYS];

/* Modern bindings now include the top-row numeric control-group keys.  The
   original localized key-name table never had entries for those ten keys,
   so keep one authoritative formatter that safely covers them for both the
   modern and legacy binding UIs. */
static const char *kbKeyNameForKey(udword key)
{
    switch (key)
    {
        case ZEROKEY:  return "0";
        case ONEKEY:   return "1";
        case TWOKEY:   return "2";
        case THREEKEY: return "3";
        case FOURKEY:  return "4";
        case FIVEKEY:  return "5";
        case SIXKEY:   return "6";
        case SEVENKEY: return "7";
        case EIGHTKEY: return "8";
        case NINEKEY:  return "9";
        default: break;
    }
    if (key >= KEY_TOTAL_KEYS || kbKeyToString[key] == 0)
        return "?";
    return strGetString(kbKeyToString[key]);
}

// fonthandle for the keyboard list window font initialized in the options init
fonthandle kbKeyListFont;

// pointer to the listwindow handle
listwindowhandle kbListWindow=NULL;

bool32 bkDisableKeyRemap=FALSE;

/*=============================================================================
    Private Function Prototypes :
=============================================================================*/

void kbSetNewKey(udword keypressed);
bool32 kbKeyUsed(udword keytocheck);

/*=============================================================================
    Function Logic :
=============================================================================*/


/*-----------------------------------------------------------------------------
    Name        : kbListTitleDraw
    Description : This function draws the title for the keyboard list window
    Inputs      : Rectangle of draw area
    Outputs     : none
    Parameters  : rectangle *rect
    Return      : void
-----------------------------------------------------------------------------*/
void kbListTitleDraw(rectangle *rect)
{
    fonthandle oldfont;
    rectangle r = *rect;

    oldfont = fontMakeCurrent(kbKeyListFont);

    fontPrintf(rect->x0+6, rect->y0, colWhite, strGetString(strCommandToBind));
    fontPrintf(rect->x0 + (((rect->x1-rect->x0)*3)/5), rect->y0, colWhite, strGetString(strKeyBound));

    r.x0 -= 2;
    r.x1++;
    r.y1--;

    primRectTranslucent2(&r, colRGBA(0,160,100,63));
//    primLine2(r.x0,r.y1,r.x1,r.y1,colRGB(20,200,20));
//    primRectOutline2(&r, 1, colRGB(20,200,20));

    fontMakeCurrent(oldfont);
}


/*-----------------------------------------------------------------------------
    Name        : kbListItemDraw
    Description : This function draws each item in the list window
    Inputs      : rect, and the item pointer
    Outputs     : none
    Parameters  : rectangle *rect, listitemhandle data
    Return      : void
-----------------------------------------------------------------------------*/
void kbListItemDraw(rectangle *rect, listitemhandle data)
{
    fonthandle      oldfont;
    kbBoundKeys    *bkey = (kbBoundKeys *)data->data;
    color           c = FEC_ListItemStandard;

    oldfont = fontMakeCurrent(kbKeyListFont);

    // if selected then this color
    if (bitTest(data->flags, UICLI_Selected))
    {
        c = FEC_ListItemSelected;
    }

    if (bkey->status == KB_NotBound)
    {
        // if not bound then print in red
        c = colRGB(255,20,20);
    }
    else if (bkey->status == KB_GetKeyPress)
    {
        // if not bound then print in yellow color
        c = colRGB(255,255,20);
    }

    fontPrintf(rect->x0 + 6, rect->y0, c, kbModernCommandName((sdword)bkey->command));

    if (bkey->status == KB_GetKeyPress)
    {
        // if getting keypress then show it with ????
        fontPrintf(rect->x0 + (((rect->x1-rect->x0)*3)/5), rect->y0, c, "??????");
    }
    else
    {
        if (bkey->primarykey == 0)
        {
            // no key assigned to this
            fontPrintf(rect->x0 + (((rect->x1-rect->x0)*3)/5), rect->y0, c, "%s", strGetString(strNoKeyBound));
        }
        else
        {
            // key assigned so print the name of that key
            dbgAssertOrIgnore(bkey->primarykey < KEY_TOTAL_KEYS);
            fontPrintf(rect->x0 + (((rect->x1-rect->x0)*3)/5), rect->y0, c, "%s", kbKeyNameForKey(bkey->primarykey));
        }
    }

    fontMakeCurrent(oldfont);
}


/*-----------------------------------------------------------------------------
    Name        : kbListWindowCB
    Description : This function initializes the key bindings list window
    Inputs      : name, and atom
    Outputs     : none
    Parameters  : char *string, featom *atom
    Return      : void
-----------------------------------------------------------------------------*/
void kbListWindowCB(char *string, featom *atom)
{
    fonthandle oldfont;
    sdword     index;
    kbBoundKeys    *bkey;

    if (FEFIRSTCALL(atom))
    {
        oldfont = fontMakeCurrent(kbKeyListFont);
        kbListWindow = (listwindowhandle)atom->pData;

        uicListWindowInit(kbListWindow,
                          kbListTitleDraw,                      //  title draw
                          NULL,                                 //  title click process, no title
                          fontHeight(" ")+(fontHeight(" ")>>1), //  title height, no title
                          kbListItemDraw,                       // item draw funtcion
                          fontHeight(" ")+(fontHeight(" ")>>1),       // item height
                          UICLW_CanSelect|UICLW_CanHaveFocus);

        for (index = 0; index < kbTOTAL_COMMANDS; index++)
        {
            uicListAddItem(kbListWindow, (ubyte *)&kbKeyTable[index], UICLI_CanSelect, UICLW_AddToHead);
        }

        fontMakeCurrent(oldfont);
        return;
    }
    else if (FELASTCALL(atom))
    {
        bitClear(kbListWindow->windowflags, UICLW_GetKeyPressed);

        for (index = 0; index < kbTOTAL_COMMANDS; index++)
        {
            if (kbKeyTable[index].primarykey == 0)
            {
                kbKeyTable[index].status = KB_NotBound;
            }
            else
            {
                kbKeyTable[index].status = KB_NormalBound;
            }
        }

        kbListWindow = NULL;
        return;
    }
    // General callbacks because of user input
    switch (kbListWindow->message)
    {
        case CM_AcceptText:
        case CM_DoubleClick:
            bkey = (kbBoundKeys *)kbListWindow->CurLineSelected->data;
            bitSet(kbListWindow->windowflags, UICLW_GetKeyPressed);
#ifdef DEBUG_STOMP
            regVerify((regionhandle)&kbListWindow->reg);
#endif
            bitSet(kbListWindow->reg.status, RSF_DrawThisFrame);
            bkey->status = KB_GetKeyPress;
            break;
        case CM_KeyCaptured:
            kbSetNewKey(kbListWindow->keypressed);
            break;
        case CM_LoseFocus:
        case CM_NewItemSelected:
            bitClear(kbListWindow->windowflags, UICLW_GetKeyPressed);
            for (index = 0; index < kbTOTAL_COMMANDS; index++)
            {
                if (kbKeyTable[index].primarykey == 0)
                {
                    kbKeyTable[index].status = KB_NotBound;
                }
                else
                {
                    kbKeyTable[index].status = KB_NormalBound;
                }
            }
            break;
    }
}


/*-----------------------------------------------------------------------------
    Name        : kbPoolListItemDraw
    Description : This function draws each item in the key pool list window
    Inputs      : rect, and the item pointer
    Outputs     : none
    Parameters  : rectangle *rect, listitemhandle data
    Return      : void
-----------------------------------------------------------------------------*/
void kbPoolListItemDraw(rectangle *rect, listitemhandle data)
{
    fonthandle      oldfont;
    color           c = FEC_ListItemStandard;
    sword           key1, key2;
    udword          udata = (udword)data->data;

    oldfont = fontMakeCurrent(kbKeyListFont);

    // unpack the two key's
    key1 = (sword) (udata&(0x0000FFFF));
    key2 = (sword) ((udata&(0xFFFF0000))>>16);

    // if selected then this color
    if (bitTest(data->flags, UICLI_Selected))
    {
        c = FEC_ListItemSelected;
    }

    if (key1 != -1)
    {
        fontPrintf(rect->x0 + 6, rect->y0, c, kbKeyNameForKey((udword)key1));
    }

    if (key2 != -1)
    {
        fontPrintf(rect->x0 + ( (rect->x1-rect->x0)>>1 ), rect->y0, c, kbKeyNameForKey((udword)key2));
    }

    fontMakeCurrent(oldfont);
}

/*-----------------------------------------------------------------------------
    Name        : kbPoolListWindowCB
    Description : This function Initializes the list of keys window
    Inputs      : keypressed
    Outputs     : none
    Parameters  : udword keypressed
    Return      : void
-----------------------------------------------------------------------------*/
void kbPoolListWindowCB(char *string, featom *atom)
{
    fonthandle          oldfont;
    static sdword       index;
    listwindowhandle    keypool;
    sdword              done;
    udword              data;
    sword               key1 = 0, key2 = 0;

    if (FEFIRSTCALL(atom))
    {
        oldfont = fontMakeCurrent(kbKeyListFont);
        keypool = (listwindowhandle)atom->pData;

        uicListWindowInit(keypool,
                          NULL,                      //  title draw
                          NULL,                      //  title click process, no title
                          0,                         //  title height, no title
                          kbPoolListItemDraw,                       // item draw funtcion
                          fontHeight(" ")+(fontHeight(" ")>>1),       // item height
                          UICLW_CanSelect|UICLW_CanHaveFocus);

        done = 0;
        for (index=0;index<KEY_TOTAL_KEYS;index++)
        {
            if (kbCanMapKey[index] == TRUE)
            {
                if (done == 0)
                {
                    key1 = (sword)index;
                    done = 1;
                }
                else if (done == 1)
                {
                    key2 = (sword)index;
                    done = 2;
                }
            }

            if (done == 2)
            {
                // pack the two 16-bit swords into the on udword
                data = (udword) ( (key1)| ((key2<<16)) );
                key1 = -1;
                key2 = -1;
                done = 0;
                uicListAddItem(keypool, (ubyte *)data,  UICLI_CanSelect, UICLW_AddToTail);
            }
        }
        if (done == 1)
        {
            // pack the two 16-bit swords into the on udword
            key2 = -1;
            data = (udword) ( (key1)| ((key2<<16)) );
            key1 = -1;
            done = 0;
            uicListAddItem(keypool, (ubyte *)data,  UICLI_CanSelect, UICLW_AddToTail);
        }

        fontMakeCurrent(oldfont);
        return;
    }
    else if (FELASTCALL(atom))
    {
        return;
    }
}

/*-----------------------------------------------------------------------------
    Name        : kbSetNewKey
    Description : This function sets the new keypressed to the currently selected command.
    Inputs      : keypressed
    Outputs     : none
    Parameters  : udword keypressed
    Return      : void
-----------------------------------------------------------------------------*/
void kbSetNewKey(udword keypressed)
{
    kbBoundKeys *bkey;
    sdword       index;

    keypressed&=0x000000FF;

    dbgAssertOrIgnore(keypressed < KEY_TOTAL_KEYS);

    bkey = (kbBoundKeys *)kbListWindow->CurLineSelected->data;
    if (kbCanMapKey[keypressed])
    {
        for (index=0;index<kbTOTAL_COMMANDS;index++)
        {
            if (kbKeyTable[index].primarykey == keypressed)
            {
                kbKeyTable[index].status = KB_NotBound;
                kbKeyTable[index].primarykey = 0;
            }
        }
        bkey->primarykey = keypressed;
        bkey->status = KB_NormalBound;
#ifdef DEBUG_STOMP
        regVerify((regionhandle)&kbListWindow->reg);
#endif
        bitSet(kbListWindow->reg.status, RSF_DrawThisFrame);
    }
    else
    {
        if (bkey->primarykey != 0)
            bkey->status = KB_NormalBound;
        else
            bkey->status = KB_NotBound;

        if (gameIsRunning)
        {
            feScreenStart(feStack[feStackIndex].baseRegion, "In_Game_Invalid_Key");
        }
        else
        {
            feScreenStart(feStack[feStackIndex].baseRegion, "Invalid_Key");
        }
        // this key is not a key that can be remapped.
    }
}


/*-----------------------------------------------------------------------------
    Name        : kbKeyResetToDefault
    Description : This function will reset all of the keyboard bindings to
                  default.
    Inputs      : name, and atom
    Outputs     : none
    Parameters  : char *string, featom *atom
    Return      : void
-----------------------------------------------------------------------------*/
void kbKeyResetToDefault(char *string, featom *atom)
{
    kbModernResetDefaults();

#ifdef DEBUG_STOMP
    if (kbListWindow != NULL)
        regVerify(&kbListWindow->reg);
#endif
    if (kbListWindow != NULL)
        bitSet(kbListWindow->reg.status, RSF_DrawThisFrame);
}

/*-----------------------------------------------------------------------------
    Native options UI accessors.  The modern UI uses these instead of
    duplicating the binding table or reaching through kbListWindow.  Keeping
    the mutation rules here also means legacy and modern binding screens can
    never disagree about which keys are legal or how collisions are resolved.
-----------------------------------------------------------------------------*/
sdword kbModernCommandCount(void)
{
    return kbTOTAL_COMMANDS;
}

const char *kbModernCommandName(sdword index)
{
    if (kbKeyTable == NULL || index < 0 || index >= kbTOTAL_COMMANDS)
        return "";

    /* Keep the original localized names for the 1999 remap set.  The modern
       extension intentionally uses explicit, stable English UI labels so we
       can expose real gameplay actions without shifting the legacy string-ID
       table or requiring new language resource files. */
    if (index <= kbLAUNCH_MANAGER)
        return strGetString(kbKeyTable[index].command + strKeyCommandOffset);

    switch (index)
    {
        case kbSENSORS_MANAGER:    return "SENSORS MANAGER";
        case kbFOCUS_LAST:         return "FOCUS LAST EVENT";
        case kbHYPERSPACE:         return "HYPERSPACE";
        case kbRETIRE:             return "RETIRE";
        case kbPAUSE_GAME:         return "PAUSE GAME";
        case kbFLEET_VIEW:         return "FLEET VIEW";
        case kbTACTICS_EVASIVE:    return "TACTICS: EVASIVE";
        case kbTACTICS_NEUTRAL:    return "TACTICS: NEUTRAL";
        case kbTACTICS_AGGRESSIVE: return "TACTICS: AGGRESSIVE";
        case kbFORMATION_DELTA:    return "FORMATION: DELTA";
        case kbFORMATION_BROAD:    return "FORMATION: BROAD";
        case kbFORMATION_DELTA3D:  return "FORMATION: DELTA 3D";
        case kbFORMATION_CLAW:     return "FORMATION: CLAW";
        case kbFORMATION_WALL:     return "FORMATION: WALL";
        case kbFORMATION_SPHERE:   return "FORMATION: SPHERE";
        case kbFORMATION_PICKET:   return "FORMATION: PICKET";
        case kbZOOM_IN:            return "CAMERA ZOOM IN";
        case kbZOOM_OUT:           return "CAMERA ZOOM OUT";
        case kbCONTROL_GROUP_0:    return "CONTROL GROUP 0";
        case kbCONTROL_GROUP_1:    return "CONTROL GROUP 1";
        case kbCONTROL_GROUP_2:    return "CONTROL GROUP 2";
        case kbCONTROL_GROUP_3:    return "CONTROL GROUP 3";
        case kbCONTROL_GROUP_4:    return "CONTROL GROUP 4";
        case kbCONTROL_GROUP_5:    return "CONTROL GROUP 5";
        case kbCONTROL_GROUP_6:    return "CONTROL GROUP 6";
        case kbCONTROL_GROUP_7:    return "CONTROL GROUP 7";
        case kbCONTROL_GROUP_8:    return "CONTROL GROUP 8";
        case kbCONTROL_GROUP_9:    return "CONTROL GROUP 9";
        case kbMULTIPLAYER_CHAT:   return "MULTIPLAYER CHAT";
        case kbCHAT_HISTORY_UP:    return "CHAT HISTORY UP";
        case kbCHAT_HISTORY_DOWN:  return "CHAT HISTORY DOWN";
        case kbSCREENSHOT:         return "SCREENSHOT";
        case kbMUSIC_PREVIOUS:     return "MUSIC: PREVIOUS TRACK";
        case kbMUSIC_NEXT:         return "MUSIC: NEXT TRACK";
        default:                   return "";
    }
}

const char *kbModernKeyName(sdword index)
{
    udword key;
    if (kbKeyTable == NULL || index < 0 || index >= kbTOTAL_COMMANDS)
        return "";
    key = kbKeyTable[index].primarykey;
    if (key == 0)
        return strGetString(strNoKeyBound);
    if (key >= KEY_TOTAL_KEYS)
        return "?";
    return kbKeyNameForKey(key);
}

udword kbModernPrimaryKey(sdword index)
{
    if (kbKeyTable == NULL || index < 0 || index >= kbTOTAL_COMMANDS)
        return 0;
    return kbKeyTable[index].primarykey;
}

bool32 kbModernAssignKey(sdword index, udword keypressed)
{
    sdword other;
    keypressed &= 0x000000ff;
    if (kbKeyTable == NULL || index < 0 || index >= kbTOTAL_COMMANDS ||
        keypressed >= KEY_TOTAL_KEYS || !kbCanMapKey[keypressed])
    {
        return FALSE;
    }

    for (other = 0; other < kbTOTAL_COMMANDS; ++other)
    {
        if (other != index && kbKeyTable[other].primarykey == keypressed)
        {
            kbKeyTable[other].primarykey = 0;
            kbKeyTable[other].status = KB_NotBound;
        }
    }
    kbKeyTable[index].primarykey = keypressed;
    kbKeyTable[index].status = KB_NormalBound;
    return TRUE;
}

void kbModernClearKey(sdword index)
{
    if (kbKeyTable == NULL || index < 0 || index >= kbTOTAL_COMMANDS)
        return;
    kbKeyTable[index].primarykey = 0;
    kbKeyTable[index].status = KB_NotBound;
}

void kbModernResetDefaults(void)
{
    sdword index;
    if (kbKeyTable == NULL) return;
    for (index = kbTOTAL_COMMANDS - 1; index >= 0; --index)
    {
        kbKeyTable[index].primarykey = kbKeyTable[index].resettokey;
        kbKeyTable[index].status = KB_NormalBound;
    }
}


/*-----------------------------------------------------------------------------
    Name        : kbCheckBindings
    Description : This function will checks to see if this key is a remappable
                  key and then do the conversion.
    Inputs      : keypressed
    Outputs     : none
    Parameters  : sdword keypressed
    Return      : void
-----------------------------------------------------------------------------*/
sdword kbCheckBindings(sdword keypressed)
{
    sdword index;

    if (kbCanMapKey[keypressed])
    {
        for (index=0;index<kbTOTAL_COMMANDS;index++)
        {
            if (kbKeyTable[index].primarykey == keypressed)
            {
                return (kbKeyTable[index].defaultkey);
            }
        }

        // key not bound to anything !!!!!
        return(0);
    }
    else
    {
        return (keypressed);
    }
}


/*-----------------------------------------------------------------------------
    Name        : kbRestoreSavedSettings
    Description : This function will restore the previosly saved settings.
    Inputs      : none
    Outputs     : none
    Parameters  : void
    Return      : void
-----------------------------------------------------------------------------*/
void kbRestoreSavedSettings(void)
{
    sdword index;

    for (index=0; index < kbTOTAL_COMMANDS; index++)
    {
        kbKeyTable[index] = kbKeySaveTable[index];
    }
}


/*-----------------------------------------------------------------------------
    Name        : kbSaveSettings
    Description : This function will save the current settings to a temporary
                  structure.  This is called when the options are started so
                  That a user can cancel any changes they don't like.
    Inputs      : void
    Outputs     : none
    Parameters  : void
    Return      : void
-----------------------------------------------------------------------------*/
void kbSaveSettings(void)
{
    sdword index;

    for (index=0; index < kbTOTAL_COMMANDS; index++)
    {
        kbKeySaveTable[index] = kbKeyTable[index];
        kbKeySavedKeys[index] = kbKeyTable[index].primarykey;
    }
}

/*-----------------------------------------------------------------------------
    Name        : kbKeyUsed
    Description : This functino returns true if the key is already bound to something, FALSE otherwise
    Inputs      : key to check
    Outputs     : boolean
    Parameters  : sdword keytocheck
    Return      : bool32
-----------------------------------------------------------------------------*/
bool32 kbKeyUsed(udword keytocheck)
{
    sdword index, count=0;

    for (index=0; index < kbTOTAL_COMMANDS; index++)
    {
        if (kbKeyTable[index].primarykey == keytocheck)
        {
            count++;
        }
    }

    if (count > 1) return(TRUE);

    return(FALSE);
}

/*-----------------------------------------------------------------------------
    Name        : kbCommandKeyIsHit
    Description : This function will return true if the key is hit for the command specified
    Inputs      : command
    Outputs     : boolean
    Parameters  : udword command
    Return      : bool32
-----------------------------------------------------------------------------*/
bool32 kbCommandKeyIsHit(udword command)
{
    return(keyIsHit(kbKeyTable[command].primarykey));
}

/*-----------------------------------------------------------------------------
    Name        : kbKeyBoundToCommand
    Description : This function returns the key that is bound to that command
    Inputs      : Command to get the key for
    Outputs     : key bound
    Parameters  : udword command
    Return      : udword
-----------------------------------------------------------------------------*/
udword kbKeyBoundToCommand(udword command)
{
    if (kbKeyTable != NULL) return(kbKeyTable[command].primarykey);

    return (0);
}

/*-----------------------------------------------------------------------------
    Name        : kbInitKeyBindings
    Description : This function Initializes the keybindings.
    Inputs      : void
    Outputs     : none
    Parameters  : void
    Return      : void
-----------------------------------------------------------------------------*/
void kbInitKeyBindings(void)
{
    sdword index;
    bool32   bInFile=FALSE;

    kbKeyListFont = frFontRegister("hw_eurosecond_11.hff");

    for (index=0;index<KEY_TOTAL_KEYS;index++)
    {
        kbCanMapKey[index] = FALSE;
    }

    if (!bkDisableKeyRemap)
    {
        kbCanMapKey[TABKEY]         = TRUE;
        kbCanMapKey[BKEY]           = TRUE;
        kbCanMapKey[CKEY]           = TRUE;
        kbCanMapKey[VKEY]           = TRUE;
        kbCanMapKey[DKEY]           = TRUE;
        kbCanMapKey[EKEY]           = TRUE;
        kbCanMapKey[FKEY]           = TRUE;
        kbCanMapKey[RKEY]           = TRUE;
        kbCanMapKey[HKEY2]           = TRUE;
        kbCanMapKey[MKEY]           = TRUE;
        kbCanMapKey[RBRACK]         = TRUE;
        kbCanMapKey[LBRACK]         = TRUE;
        kbCanMapKey[SKEY]           = TRUE;
        kbCanMapKey[ZKEY]           = TRUE;
        kbCanMapKey[CAPSLOCKKEY]    = TRUE;
        kbCanMapKey[SPACEKEY]       = TRUE;
        kbCanMapKey[ENTERKEY]       = TRUE;
        kbCanMapKey[IKEY]           = TRUE;
        kbCanMapKey[JKEY]           = TRUE;
        kbCanMapKey[PKEY]           = TRUE;
        kbCanMapKey[HOMEKEY]        = TRUE;
        kbCanMapKey[KKEY]           = TRUE;
        kbCanMapKey[TILDEKEY]       = TRUE;
        kbCanMapKey[LKEY]           = TRUE;
        kbCanMapKey[TKEY]           = TRUE;
        kbCanMapKey[ZEROKEY]        = TRUE;
        kbCanMapKey[ONEKEY]         = TRUE;
        kbCanMapKey[TWOKEY]         = TRUE;
        kbCanMapKey[THREEKEY]       = TRUE;
        kbCanMapKey[FOURKEY]        = TRUE;
        kbCanMapKey[FIVEKEY]        = TRUE;
        kbCanMapKey[SIXKEY]         = TRUE;
        kbCanMapKey[SEVENKEY]       = TRUE;
        kbCanMapKey[EIGHTKEY]       = TRUE;
        kbCanMapKey[NINEKEY]        = TRUE;
        kbCanMapKey[PAGEUPKEY]      = TRUE;
        kbCanMapKey[PAGEDOWNKEY]    = TRUE;
        kbCanMapKey[SCROLLKEY]      = TRUE;
        kbCanMapKey[LESSTHAN]       = TRUE;
        kbCanMapKey[GREATERTHAN]    = TRUE;
        // start of extra keys
//        kbCanMapKey[PRINTKEY]       = TRUE;
        kbCanMapKey[ARRUP]          = TRUE;
        kbCanMapKey[ARRDOWN]        = TRUE;
        kbCanMapKey[ENDKEY]         = TRUE;
        kbCanMapKey[INSERTKEY]      = TRUE;
        kbCanMapKey[DELETEKEY]      = TRUE;
        kbCanMapKey[AKEY]           = TRUE;
        kbCanMapKey[NKEY]           = TRUE;
        kbCanMapKey[OKEY]           = TRUE;

        if (pilotView)
        {
            kbCanMapKey[QKEY]           = FALSE;
        }
        else
        {
            kbCanMapKey[QKEY]           = TRUE;
        }

        kbCanMapKey[UKEY]           = TRUE;
        kbCanMapKey[WKEY]           = TRUE;
        kbCanMapKey[XKEY]           = TRUE;
        kbCanMapKey[YKEY]           = TRUE;
        kbCanMapKey[NUMPAD0]        = TRUE;
        kbCanMapKey[NUMPAD1]        = TRUE;
        kbCanMapKey[NUMPAD2]        = TRUE;
        kbCanMapKey[NUMPAD3]        = TRUE;
        kbCanMapKey[NUMPAD4]        = TRUE;
        kbCanMapKey[NUMPAD5]        = TRUE;
        kbCanMapKey[NUMPAD6]        = TRUE;
        kbCanMapKey[NUMPAD7]        = TRUE;
        kbCanMapKey[NUMPAD8]        = TRUE;
        kbCanMapKey[NUMPAD9]        = TRUE;
        kbCanMapKey[NUMSTARKEY]     = TRUE;
        kbCanMapKey[NUMSLASHKEY]    = TRUE;
        kbCanMapKey[NUMDOTKEY]      = TRUE;
        kbCanMapKey[PLUSKEY]        = TRUE;
        kbCanMapKey[MINUSKEY]       = TRUE;
        kbCanMapKey[NUMPLUSKEY]     = TRUE;
        kbCanMapKey[NUMMINUSKEY]    = TRUE;
        kbCanMapKey[F1KEY]          = TRUE;
        kbCanMapKey[F2KEY]          = TRUE;
        kbCanMapKey[F3KEY]          = TRUE;
        kbCanMapKey[F4KEY]          = TRUE;
        kbCanMapKey[F5KEY]          = TRUE;
        kbCanMapKey[F6KEY]          = TRUE;
        kbCanMapKey[F7KEY]          = TRUE;
        kbCanMapKey[F8KEY]          = TRUE;
        kbCanMapKey[F9KEY]          = TRUE;
        kbCanMapKey[F10KEY]         = TRUE;
        kbCanMapKey[F11KEY]         = TRUE;
 //       kbCanMapKey[BACKSLASHKEY]   = TRUE;
    }

    kbKeyToString[AKEY] =         strAKEY;
    kbKeyToString[BKEY] =         strBKEY;
    kbKeyToString[CKEY] =         strCKEY;
    kbKeyToString[DKEY] =         strDKEY;
    kbKeyToString[EKEY] =         strEKEY;
    kbKeyToString[FKEY] =         strFKEY;
    kbKeyToString[GKEY] =         strGKEY;
    kbKeyToString[HKEY2] =         strHKEY;
    kbKeyToString[IKEY] =         strIKEY;
    kbKeyToString[JKEY] =         strJKEY;
    kbKeyToString[KKEY] =         strKKEY;
    kbKeyToString[LKEY] =         strLKEY;
    kbKeyToString[MKEY] =         strMKEY;
    kbKeyToString[NKEY] =         strNKEY;
    kbKeyToString[OKEY] =         strOKEY;
    kbKeyToString[PKEY] =         strPKEY;
    kbKeyToString[QKEY] =         strQKEY;
    kbKeyToString[RKEY] =         strRKEY;
    kbKeyToString[SKEY] =         strSKEY;
    kbKeyToString[TKEY] =         strTKEY;
    kbKeyToString[UKEY] =         strUKEY;
    kbKeyToString[VKEY] =         strVKEY;
    kbKeyToString[WKEY] =         strWKEY;
    kbKeyToString[XKEY] =         strXKEY;
    kbKeyToString[YKEY] =         strYKEY;
    kbKeyToString[ZKEY] =         strZKEY;
    kbKeyToString[BACKSPACEKEY] = strBACKSPACEKEY;
    kbKeyToString[TABKEY] =       strTABKEY;
    kbKeyToString[ARRLEFT] =      strARRLEFT;
    kbKeyToString[ARRRIGHT] =     strARRRIGHT;
    kbKeyToString[ARRUP] =        strARRUP;
    kbKeyToString[ARRDOWN] =      strARRDOWN;
    kbKeyToString[ENDKEY] =       strENDKEY;
    kbKeyToString[LBRACK] =       strLBRACK;
    kbKeyToString[RBRACK] =       strRBRACK;
    kbKeyToString[CAPSLOCKKEY] =  strCAPSLOCKKEY;
    kbKeyToString[SPACEKEY] =     strSPACEKEY;
    kbKeyToString[ENTERKEY] =     strENTERKEY;
    kbKeyToString[HOMEKEY] =      strHOMEKEY;
    kbKeyToString[PAGEDOWNKEY] =  strPAGEDOWNKEY;
    kbKeyToString[PAGEUPKEY] =    strPAGEUPKEY;
    kbKeyToString[BACKSLASHKEY] = strBACKSLASHKEY;
    kbKeyToString[PAUSEKEY] =     strPAUSEKEY;
    kbKeyToString[SCROLLKEY] =    strSCROLLKEY;
    kbKeyToString[PRINTKEY] =     strPRINTKEY;
    kbKeyToString[INSERTKEY] =    strINSERTKEY;
    kbKeyToString[DELETEKEY] =    strDELETEKEY;
    kbKeyToString[LESSTHAN] =     strLESSTHAN;
    kbKeyToString[GREATERTHAN] =  strGREATERTHAN;
    kbKeyToString[TILDEKEY] =     strTILDEKEY;
    kbKeyToString[NUMPAD0] =      strNUMPAD0;
    kbKeyToString[NUMPAD1] =      strNUMPAD1;
    kbKeyToString[NUMPAD2] =      strNUMPAD2;
    kbKeyToString[NUMPAD3] =      strNUMPAD3;
    kbKeyToString[NUMPAD4] =      strNUMPAD4;
    kbKeyToString[NUMPAD5] =      strNUMPAD5;
    kbKeyToString[NUMPAD6] =      strNUMPAD6;
    kbKeyToString[NUMPAD7] =      strNUMPAD7;
    kbKeyToString[NUMPAD8] =      strNUMPAD8;
    kbKeyToString[NUMPAD9] =      strNUMPAD9;
    kbKeyToString[NUMMINUSKEY] =  strNUMMINUSKEY;
    kbKeyToString[NUMPLUSKEY] =   strNUMPLUSKEY;
    kbKeyToString[NUMSTARKEY] =   strNUMSTARKEY;
    kbKeyToString[NUMSLASHKEY] =  strNUMSLASHKEY;
    kbKeyToString[NUMDOTKEY] =    strNUMDOTKEY;
    kbKeyToString[MINUSKEY] =     strMINUSKEY;
    kbKeyToString[PLUSKEY] =      strPLUSKEY;
    kbKeyToString[F1KEY] =        strF1KEY;
    kbKeyToString[F2KEY] =        strF2KEY;
    kbKeyToString[F3KEY] =        strF3KEY;
    kbKeyToString[F4KEY] =        strF4KEY;
    kbKeyToString[F5KEY] =        strF5KEY;
    kbKeyToString[F6KEY] =        strF6KEY;
    kbKeyToString[F7KEY] =        strF7KEY;
    kbKeyToString[F8KEY] =        strF8KEY;
    kbKeyToString[F9KEY] =        strF9KEY;
    kbKeyToString[F10KEY] =       strF10KEY;
    kbKeyToString[F11KEY] =       strF11KEY;
    kbKeyToString[F12KEY] =       strF12KEY;

    for (index=0;index<kbTOTAL_COMMANDS;index++)
    {
        if (kbKeySavedKeys[index] != 0) bInFile = TRUE;
    }

    switch (strCurLanguage)
    {
        case languageEnglish:
            kbKeyTable = &kbKeyTableEnglish[0];
        break;
        case languageFrench:
            kbKeyTable = &kbKeyTableFrench[0];
        break;
        case languageGerman:
            kbKeyTable = &kbKeyTableGerman[0];
        break;
        case languageSpanish:
            kbKeyTable = &kbKeyTableSpanish[0];
        break;
        case languageItalian:
            kbKeyTable = &kbKeyTableItalian[0];
        break;
    }

    if (bInFile)
    {
        for (index=0;index<kbTOTAL_COMMANDS;index++)
        {
            /* Config files written before the extended command map only have
               the original 19 entries.  Preserve every old binding exactly,
               including intentional unbound (0) entries, but seed newly
               introduced commands from their defaults on the first migrated
               launch instead of silently leaving them all unbound. */
            if (kbKeyBindingsVersion < 2 &&
                index > kbLAUNCH_MANAGER &&
                kbKeySavedKeys[index] == 0)
            {
                continue;
            }
            if (kbKeyBindingsVersion < 3 &&
                index > kbZOOM_OUT &&
                kbKeySavedKeys[index] == 0)
            {
                continue;
            }
            kbKeyTable[index].primarykey = kbKeySavedKeys[index];
        }
    }
    kbKeyBindingsVersion = 3;

    // check the key-bindings and reset anything that's messed up
    for (index=0;index<kbTOTAL_COMMANDS;index++)
    {
        if (kbKeyTable[index].primarykey == 0)
        {
            // i guess the user can have no key for a function ?? don't know why but ...
            kbKeyTable[index].status = KB_NotBound;
            continue;
        }
        // is the key even a mappable one ?
        if (!kbCanMapKey[kbKeyTable[index].primarykey])
        {
            // just assign it to default key
            kbKeyTable[index].primarykey = kbKeyTable[index].defaultkey;
            kbKeyTable[index].status = KB_NormalBound;
        }
        // make sure that it's only used once
        if (kbKeyUsed(kbKeyTable[index].primarykey))
        {   // set to not mapped, can't have the same key do multiple things
            kbKeyTable[index].primarykey = 0;
            kbKeyTable[index].status = KB_NotBound;
        }
    }
}
