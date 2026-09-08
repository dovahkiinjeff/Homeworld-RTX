// =============================================================================
//  P2Mothership.c
// =============================================================================
//  Copyright Relic Entertainment, Inc. All rights reserved.
//  Created 5/07/1998 by ddunlop
// =============================================================================

#include "P2Mothership.h"

#include "Attack.h"
#include "DefaultShip.h"

typedef struct
{
    udword dummy;
} P2MothershipSpec;

typedef struct
{
    real32 p2mothershipGunRange[NUM_TACTICS_TYPES];
    real32 p2mothershipTooCloseRange[NUM_TACTICS_TYPES];
} P2MothershipStatics;

P2MothershipStatics P2MothershipStatic;

void P2MothershipStaticInit(char *directory,char *filename,struct ShipStaticInfo *statinfo)
{
    udword i;
    P2MothershipStatics *mothershipstat = &P2MothershipStatic;

    statinfo->custstatinfo = mothershipstat;

    /* Match the captured Turanic production-hull contract.  The shipped P2
       data already has twelve In/Out pairs and a 250-fighter berth, but no
       corvette berth limit; keep both small-ship classes usable when the
       Kadeshi mothership becomes player-owned. */
    if (statinfo->maxDockableFighters <= 0)
    {
        statinfo->maxDockableFighters = 250;
    }
    if (statinfo->maxDockableCorvettes <= 0)
    {
        statinfo->maxDockableCorvettes = 250;
    }

    /* The retail P2 data has eight physical salvage points but an impossible
       NUM_NEEDED_FOR_SALVAGE of 99.  CAPTURE / BUILD ALL makes the exceptional
       mothership contract deterministic: five physically clamped Salvage
       Corvettes, matching the Turanic carrier. */
    if (statinfo->salvageStaticInfo != NULL)
    {
        statinfo->salvageStaticInfo->numNeededForSalvage =
            (statinfo->salvageStaticInfo->numSalvagePoints < P2_MOTHERSHIP_SALVAGERS_REQUIRED) ?
            statinfo->salvageStaticInfo->numSalvagePoints :
            P2_MOTHERSHIP_SALVAGERS_REQUIRED;
    }

    for(i=0;i<NUM_TACTICS_TYPES;i++)
    {
        mothershipstat->p2mothershipGunRange[i] = statinfo->bulletRange[i];
        mothershipstat->p2mothershipTooCloseRange[i] = statinfo->minBulletRange[i] * 0.9f;
    }
}

void P2MothershipAttack(Ship *ship,SpaceObjRotImpTarg *target,real32 maxdist)
{
    ShipStaticInfo *shipstaticinfo = (ShipStaticInfo *)ship->staticinfo;
    P2MothershipStatics *motherstat = (P2MothershipStatics *)shipstaticinfo->custstatinfo;
    real32 toocloserange = motherstat->p2mothershipTooCloseRange[ship->tacticstype];

    // currently only correct if we are attacking a mothership class ship - otherwise we don't mind it ramming it
    if ((target->objtype == OBJ_ShipType) && (((Ship *)target)->staticinfo->shipclass == CLASS_Mothership))
    {
        // correct for P2 Mothership size - the laser is at the end of the ship, which is really long so we should
        // add the radius of the P2 Mothership to correct

        toocloserange += shipstaticinfo->staticheader.staticCollInfo.collspheresize;
    }

    attackStraightForward(ship,target,motherstat->p2mothershipGunRange[ship->tacticstype],toocloserange);
}

void P2MothershipAttackPassive(Ship *ship,Ship *target,bool32 rotate)
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

CustShipHeader P2MothershipHeader =
{
    P2Mothership,
    sizeof(P2MothershipSpec),
    P2MothershipStaticInit,
    NULL,
    NULL,
    NULL,
    P2MothershipAttack,
    DefaultShipFire,
    P2MothershipAttackPassive,
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

