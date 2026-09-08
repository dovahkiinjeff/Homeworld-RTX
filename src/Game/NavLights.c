/*=============================================================================
    Name    : NavLights.c
    Purpose : Control operation of NAV lights on ships.

    Created 6/21/1997 by agarden
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#include "NavLights.h"

#include "glinc.h"
#include "Matrix.h"
#include "Memory.h"
#include "prim3d.h"
#include "render.h"
#include "Universe.h"
#include "utility.h"

static color navLightEffectiveColor(Ship *ship, NAVLightStatic *navLightStatic)
{
    /* RenderNAVLights is also used for Derelict objects because the original
       layouts intentionally match through navLightInfo.  Never read the Ship-
       only playerowner field until the shared objtype says this is a Ship.
       Pointer identity avoids dereferencing a stale/corrupt owner as well. */
    if (utyNavLightColorOverride && ship != NULL &&
        ship->objtype == OBJ_ShipType &&
        ship->playerowner == universe.curPlayerPtr)
    {
        return utyNavLightColor;
    }
    return navLightStatic->color;
}

#ifdef HW_ENABLE_D3D12_BACKEND
#include "ModernGraphics.h"

static void navLightSubmitModernEmitter(Ship *ship,
                                        NAVLightStatic *navLightStatic,
                                        color lightColor,
                                        real32 fade)
{
    vector worldPosition;
    HWModernDynamicLightEmitter emitter = {0};

    matMultiplyMatByVec(&worldPosition, &ship->rotinfo.coordsys,
                       &navLightStatic->position);
    vecAddTo(worldPosition, ship->posinfo.position);
    emitter.source = HW_MODERN_LIGHT_NAV;
    emitter.shape = HW_MODERN_LIGHT_POINT;
    emitter.position[0] = worldPosition.x;
    emitter.position[1] = worldPosition.y;
    emitter.position[2] = worldPosition.z;
    emitter.color[0] = colUbyteToReal(colRed(lightColor));
    emitter.color[1] = colUbyteToReal(colGreen(lightColor));
    emitter.color[2] = colUbyteToReal(colBlue(lightColor));
    emitter.radius = navLightStatic->size * 12.0f;
    if (emitter.radius < 24.0f)
    {
        emitter.radius = 24.0f;
    }
    emitter.intensity = navLightStatic->size * 4.0f * fade;
    if (emitter.intensity < 4.0f * fade)
    {
        emitter.intensity = 4.0f * fade;
    }
    hwModernGraphicsSubmitDynamicLight(&emitter);
}
#endif

#ifndef SW_Render
    #ifdef _WIN32
        #include <windows.h>
    #endif
#endif

/*-----------------------------------------------------------------------------
    Name        : navLightBillboardEnable
    Description : setup modelview matrix for rendering billboarded sprites
    Inputs      : s - ship to obtain coordinate system from
                  nls - NAV Light static info
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void navLightBillboardEnable(Ship *s, NAVLightStatic *nls)
{
    vector src, dst;

    //object -> worldspace
    src = nls->position;
    matMultiplyMatByVec(&dst, &s->rotinfo.coordsys, &src);
    vecAddTo(dst, s->posinfo.position);

    //setup billboarding
    glPushMatrix();
    rndBillboardEnable(&dst);
    glDisable(GL_CULL_FACE);
}

/*-----------------------------------------------------------------------------
    Name        : navLightBillboardDisable
    Description : reset modelview matrix to non-billboard state
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void navLightBillboardDisable(void)
{
    //undo billboarding
    rndBillboardDisable();
    glPopMatrix();
    glEnable(GL_CULL_FACE);
}

/*-----------------------------------------------------------------------------
    Name        : navLightStaticInfoDelete
    Description : Delete the static info of a set of nav lights
    Inputs      : staticInfo - array of navlight structures to free
    Outputs     : unregisters the navlight textures, if any.
    Return      :
----------------------------------------------------------------------------*/
void navLightStaticInfoDelete(NAVLightStaticInfo *staticInfo)
{
    sdword i, num = staticInfo->numNAVLights;
    NAVLightStatic *navLightStatic = staticInfo->navlightstatics;

    dbgAssertOrIgnore(staticInfo != NULL);

    for( i=0 ; i < num ; i++, navLightStatic++)
    {
        if (navLightStatic->texturehandle != TR_InvalidHandle)
        {
            trTextureUnregister(navLightStatic->texturehandle);
        }
    }
    memFree(staticInfo);
}

/*-----------------------------------------------------------------------------
    Name        : RenderNAVLights
    Description : TODO: render sorted by projected depth value so alpha sorts correctly
    Inputs      : ship - the ship whose navlights we are to render
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void RenderNAVLights(Ship *ship)
{
   sdword i;
   NAVLight *navLight;
   NAVLightInfo *navLightInfo;
   ShipStaticInfo *shipStaticInfo;
   NAVLightStatic *navLightStatic;
   vector origin = {0.0f, 0.0f, 0.0f};
   NAVLightStaticInfo *navLightStaticInfo;
   real32 fade;
   bool32 lightOn;
   color lightColor;
   extern bool32 bFade;
   extern real32 meshFadeAlpha;

   fade = bFade ? meshFadeAlpha : 1.0f;

   shipStaticInfo = (ShipStaticInfo *)ship->staticinfo;

    navLightInfo = ship->navLightInfo;
   if(shipStaticInfo->navlightStaticInfo && navLightInfo != NULL)
   {
      glDepthMask(GL_FALSE);
      rndAdditiveBlends(TRUE);
      lightOn = rndLightingEnable(FALSE);

      navLightStaticInfo = shipStaticInfo->navlightStaticInfo;
      navLightStatic = navLightStaticInfo->navlightstatics;
      navLight = navLightInfo->navLights;

      for( i=0 ; i<navLightStaticInfo->numNAVLights ; i++, navLight ++, navLightStatic ++)
      {
			// Account for the startdelay.
			if(navLight->lastTimeFlashed == navLightStatic->startdelay)
			{
				navLight->lastTimeFlashed = universe.totaltimeelapsed + navLightStatic->startdelay;
			}
			
			if(universe.totaltimeelapsed > navLight->lastTimeFlashed)
			{
				if(navLight->lightstate == 1)
				{
					navLight->lastTimeFlashed = universe.totaltimeelapsed + navLightStatic->flashrateoff;
				}
				else
				{
					navLight->lastTimeFlashed = universe.totaltimeelapsed + navLightStatic->flashrateon;
				}
				
				navLight->lightstate = 1 - navLight->lightstate;
			}

			if(navLight->lightstate)
			{
                lightColor = navLightEffectiveColor(ship, navLightStatic);
#ifdef HW_ENABLE_D3D12_BACKEND
                /* MEX-authored local point transformed by the same ship
                   coordinate system as the visible billboard. */
                navLightSubmitModernEmitter(ship, navLightStatic,
                                             lightColor, fade);
#endif
				if (ship->currentLOD <= (sdword)navLightStatic->minLOD)
				{
					navLightBillboardEnable(ship, navLightStatic);

					if(navLightStatic->texturehandle == TR_InvalidHandle)
					{
						primCircleSolid3Fade(&origin, navLightStatic->size, 10, lightColor, fade);
					}
					else
					{
						/* Authored nav sprites are usually white masks.  Explicitly
						   restore modulate mode so the player-only override tints the
						   visible lamp as well as its DXR emitter. */
						rndTextureEnvironment(RTE_Modulate);
						primSolidTexture3Fade(&origin, navLightStatic->size, lightColor, navLightStatic->texturehandle, fade);
					}

					navLightBillboardDisable();
				}
				else
				{
					color tempColor;

                    tempColor = colRGB(colRed(lightColor) * 2 / 3,
					                   colGreen(lightColor) * 2 / 3,
									   colBlue(lightColor) * 2 / 3);

                    rndTextureEnable(FALSE);
                    rndAdditiveBlends(TRUE);
                    glEnable(GL_POINT_SMOOTH);
                    primPointSize3Fade(&navLightStatic->position, 2.0f, tempColor, fade);
                    glDisable(GL_POINT_SMOOTH);
				}
			}
      }

      rndLightingEnable(lightOn);
      rndAdditiveBlends(FALSE);
      glDepthMask(GL_TRUE);
    }
}
