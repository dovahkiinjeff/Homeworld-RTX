/*============================================================================= 
    Name    : ShipView.h
    Purpose : Renders a specific ship to a window

    Created 7/27/1998 by pgrant
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#ifndef ___SHIPVIEW_H
#define ___SHIPVIEW_H

#include "FEFlow.h"
#include "Region.h"
#include "ShipDefs.h"

struct ShipStaticInfo;

void svStartup(void);
void svShutdown(void);

void svSelectShip(ShipType type);
void svClose(void);

void svDirtyShipView(void);

void svShipViewRender(featom* atom, regionhandle region);

/* Shared by the modern ship dossier so the archive reports the exact same
   maneuverability category as the legacy ship-stat computation. */
void svShipManeuverability(struct ShipStaticInfo *statinfo, char *name);
uword svShipCoverage(struct ShipStaticInfo *statinfo);

#endif
