/*=============================================================================
    Name    : launchMgr.h
    Purpose : Definitons for the launch manager

    Created 5/18/1998 by ddunlop
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/


#ifndef ___LAUNCHMGR_H
#define ___LAUNCHMGR_H

#include "Region.h"
#include "SpaceObj.h"

/*=============================================================================
    Definitions:
=============================================================================*/

#ifdef _WIN32
#define LM_FIBFile              "FEMAN\\Launch_Manager.fib"
#else
#define LM_FIBFile              "FEMAN/Launch_Manager.fib"
#endif
#define LM_LaunchScreen         "Launch_Manager"

#define LM_ShipListTextColor        colRGB(143, 157, 157)
#define LM_ShipSelectedTextColor    colRGB(225, 222, 206)
#define LM_Ship2ListTextColor       colRGB(86, 111, 120)
#define LM_Ship2SelectedTextColor   colRGB(222, 162, 62)
#define LM_UsedColor                colRGB(151, 83, 70);
#define LM_AvailableColor           colRGB(105, 184, 196);

#define LM_DefaultFont          "default.hff"
#define LM_ShipListFont         "HW_EuroseCond_11.hff"
#define LM_FontNameLength       64


//ships available for launch
typedef struct
{
    sword  nShips;
    bool16 bSelected;
    Ship   *ship;
}
LaunchShipsAvailable;

extern bool32 lmActive;


/*=============================================================================
    Function Prototypes:
=============================================================================*/

//start the launch manager.  It will kill itself when you hit the launch button.
sdword lmLaunchBegin(regionhandle region, smemsize ID, udword event, udword data);
void   lmUpdateShipsInside(void);
void   lmStartup(void);
void   lmShutdown(void);
void   lmCloseIfOpen(void);
void   lmFreeTextures(void);
void   lmLoadTextures(void);
void   lmLaunchCapableShipDied (Ship *ship);

#endif
