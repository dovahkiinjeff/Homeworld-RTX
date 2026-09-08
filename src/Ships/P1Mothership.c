// =============================================================================
//  P1Mothership.c
// =============================================================================
//  Copyright Relic Entertainment, Inc. All rights reserved.
//  Created 5/06/1998 by ddunlop
// =============================================================================

#include "P1Mothership.h"

#include "Attack.h"
#include "DefaultShip.h"

typedef struct
{
    udword dummy;
} P1MothershipSpec;

typedef struct
{
    real32 p1mothershipGunRange[NUM_TACTICS_TYPES];
    real32 p1mothershipTooCloseRange[NUM_TACTICS_TYPES];
} P1MothershipStatics;

P1MothershipStatics P1MothershipStatic;

void P1MothershipStaticInit(char *directory,char *filename,struct ShipStaticInfo *statinfo)
{
    udword i;
    P1MothershipStatics *mothershipstat = &P1MothershipStatic;

    statinfo->custstatinfo = mothershipstat;

    /* The shipped P1Mothership data already declares permanent fighter /
       corvette receiving and supplies real In / Out / Out1 dock points, but
       omits the numeric berth limits.  Zero means the Launch Manager meters
       and ShipHasToLaunch() cannot treat it as usable storage.  Match the
       mothership-class 250/250 capacity used by the player motherships. */
    if (statinfo->maxDockableFighters <= 0)
    {
        statinfo->maxDockableFighters = 250;
    }
    if (statinfo->maxDockableCorvettes <= 0)
    {
        statinfo->maxDockableCorvettes = 250;
    }

    /* The Turanic carrier shipped with an unfinished salvage contract.
       Keep the authored attachment points, but make the actual capture
       requirement deterministic: five Salvage Corvettes.  If a data set
       somehow exposes fewer than five physical salvage points, never make
       the requirement impossible to satisfy. */
    if(statinfo->salvageStaticInfo != NULL)
    {
        statinfo->salvageStaticInfo->numNeededForSalvage =
            (statinfo->salvageStaticInfo->numSalvagePoints < P1_MOTHERSHIP_SALVAGERS_REQUIRED) ?
            statinfo->salvageStaticInfo->numSalvagePoints :
            P1_MOTHERSHIP_SALVAGERS_REQUIRED;
    }

    for(i=0;i<NUM_TACTICS_TYPES;i++)
    {
        mothershipstat->p1mothershipGunRange[i] = statinfo->bulletRange[i];
        mothershipstat->p1mothershipTooCloseRange[i] = statinfo->minBulletRange[i] * 0.9f;
    }
}

void P1MothershipAttack(Ship *ship,SpaceObjRotImpTarg *target,real32 maxdist)
{
    ShipStaticInfo *shipstaticinfo = (ShipStaticInfo *)ship->staticinfo;
    P1MothershipStatics *motherstat = (P1MothershipStatics *)shipstaticinfo->custstatinfo;

    attackStraightForward(ship,target,motherstat->p1mothershipGunRange[ship->tacticstype],motherstat->p1mothershipTooCloseRange[ship->tacticstype]);
}

void P1MothershipAttackPassive(Ship *ship,Ship *target,bool32 rotate)
{
    if ((rotate) & ((bool32)((ShipStaticInfo *)(ship->staticinfo))->rotateToRetaliate))
    {
        attackPassiveRotate(ship,target);
    }
    else
    {
        attackPassive(ship,target);
    }
}

CustShipHeader P1MothershipHeader =
{
    P1Mothership,
    sizeof(P1MothershipSpec),
    P1MothershipStaticInit,
    NULL,
    NULL,
    NULL,
    P1MothershipAttack,
    DefaultShipFire,
    P1MothershipAttackPassive,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

