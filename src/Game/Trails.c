/*=============================================================================
    Name    : Trails.c
    Purpose : Code to draw ship trails

    Created 8/11/1997 by lmoloney
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/

#include "Trails.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Camera.h"
#include "Clouds.h"
#include "CommandDefs.h"
#include "Debug.h"
#include "FastMath.h"
#include "glinc.h"
#include "main.h"
#include "Memory.h"
#include "Particle.h"
#include "prim3d.h"
#include "Randy.h"
#include "render.h"
#include "Select.h"
#include "Shader.h"
#include "SpaceObj.h"
#include "StatScript.h"
#include "Tactics.h"
#include "Teams.h"
#include "Universe.h"

#ifdef HW_ENABLE_D3D12_BACKEND
#include "ModernGraphics.h"
#endif

#if defined _MSC_VER
	#define isnan(x) _isnan(x)
#endif

#define MIN2(x,y) ((x) < (y) ? (x) : (y))
#define MAX2(x,y) ((x) > (y) ? (x) : (y))

#define VECCOPY(D,S)  memcpy(D, S, sizeof(vector))
#define MATCOPY(D,S)  memcpy(D, S, sizeof(matrix))
#define HMATCOPY(D,S) memcpy(D, S, sizeof(hmatrix))

#define TRAIL_LINE_CUTOFF_LOD 2

sdword bTrailRender = 1;
sdword bTrailDraw = 1;

sdword trailsUpdated;
sdword trailsNotUpdated;
sdword trailsRendered;

extern Camera* mrCamera;
extern real32 meshFadeAlpha;

//defaults for reference in case Trail.script gets munged beyond repair
static real32 RANDOM_MAX = 10.0f;
static real32 RANDOM_JITTER = 0.75f;
static real32 VELRATIO_THRESH = 0.1f;
static real32 VELRATIO_ACCELTHRESH = 0.8f;
static real32 VELRATIO_CUTOFF = 0.9f;
static real32 ACCELTHRESH = 8.0f;
static real32 SIZE_MAJOR = 5.0f;
static real32 SIZE_INC = 1.0f;
static real32 SIZEINC_INC = 1.0f;
static real32 SIZE_MAXIMUM = 42.0f;  // NB: was SIZE_MAX but compiler error'd due to namespace clash in stdint.h
static real32 XADJ = 8.0f;
static real32 HALFWIDTH = 4.2f;
static real32 HALFWIDTH_FALLOFF = 0.3f;
static real32 HALFWIDTH_MIN = 4.0f;
static real32 GLOW_RADIALADJUST = 0.82f;
static real32 GLOW_BASERADIALSCALE = 0.2f;
static real32 BURN_RATIOSCALE = 25.0f;
static real32 BURN_RATIOMIN = 6.0f;
static real32 BURN_SIZESCALE = 1.5f;
static real32 RANDOMBURN_FREQ = 0.02f;
static sdword BURN_COLORADJUST = -64;
static sdword COLORADJUST_FREQ = 40;
static sdword COLOR_ADJUST = 12;

static real32 TRAIL_EXPONENT_BASE = 3.6f;
static real32 TRAIL_EXPONENT_RANGE = 0.6f;

static sdword TRAIL_GLOW_1_RED = 255;
static sdword TRAIL_GLOW_1_GREEN = 180;
static sdword TRAIL_GLOW_1_BLUE = 40;
static sdword TRAIL_GLOW_2_RED = 40;
static sdword TRAIL_GLOW_2_GREEN = 180;
static sdword TRAIL_GLOW_2_BLUE = 255;

static sdword TRAIL_EXPANSION_TICKS = 16;

scriptEntry TrailTweaks[] =
{
    makeEntry(RANDOM_MAX, scriptSetReal32CB),
    makeEntry(RANDOM_JITTER, scriptSetReal32CB),
    makeEntry(VELRATIO_THRESH, scriptSetReal32CB),
    makeEntry(VELRATIO_ACCELTHRESH, scriptSetReal32CB),
    makeEntry(VELRATIO_CUTOFF, scriptSetReal32CB),
    makeEntry(ACCELTHRESH, scriptSetReal32CB),
    makeEntry(SIZE_MAJOR, scriptSetReal32CB),
    makeEntry(SIZE_INC, scriptSetReal32CB),
    makeEntry(SIZEINC_INC, scriptSetReal32CB),
    makeEntry(SIZE_MAXIMUM, scriptSetReal32CB),
    makeEntry(XADJ, scriptSetReal32CB),
    makeEntry(HALFWIDTH, scriptSetReal32CB),
    makeEntry(HALFWIDTH_FALLOFF, scriptSetReal32CB),
    makeEntry(HALFWIDTH_MIN, scriptSetReal32CB),
    makeEntry(GLOW_RADIALADJUST, scriptSetReal32CB),
    makeEntry(GLOW_BASERADIALSCALE, scriptSetReal32CB),
    makeEntry(BURN_RATIOSCALE, scriptSetReal32CB),
    makeEntry(BURN_RATIOMIN, scriptSetReal32CB),
    makeEntry(BURN_SIZESCALE, scriptSetReal32CB),
    makeEntry(BURN_COLORADJUST, scriptSetSdwordCB),
    makeEntry(RANDOMBURN_FREQ, scriptSetReal32CB),
    makeEntry(COLORADJUST_FREQ, scriptSetSdwordCB),
    makeEntry(COLOR_ADJUST, scriptSetSdwordCB),
    makeEntry(TRAIL_EXPONENT_BASE, scriptSetReal32CB),
    makeEntry(TRAIL_EXPONENT_RANGE, scriptSetReal32CB),
    makeEntry(TRAIL_GLOW_1_RED, scriptSetSdwordCB),
    makeEntry(TRAIL_GLOW_1_GREEN, scriptSetSdwordCB),
    makeEntry(TRAIL_GLOW_1_BLUE, scriptSetSdwordCB),
    makeEntry(TRAIL_GLOW_2_RED, scriptSetSdwordCB),
    makeEntry(TRAIL_GLOW_2_GREEN, scriptSetSdwordCB),
    makeEntry(TRAIL_GLOW_2_BLUE, scriptSetSdwordCB),
    makeEntry(TRAIL_EXPANSION_TICKS, scriptSetSdwordCB),
    
    END_SCRIPT_ENTRY
};


/*=============================================================================
    Data:
=============================================================================*/
static real32 xaxis[]  = {1.0f, 0.0f, 0.0f};
static real32 yaxis[]  = {0.0f, 1.0f, 0.0f};
static real32 zaxis[]  = {0.0f, 0.0f, -1.0f};
static real32 origin[] = {0.0f, 0.0f, 0.0f};

static real32 NLipsScaleFactor = 1.0f;
/* Transparent engine FX need a small amount of stable screen coverage even
   though ships now remain at their true world-space scale.  This never
   changes ship geometry or nozzle placement; it only prevents the soft
   ribbon from collapsing below a pixel at extreme range. */
static real32 trailScreenCoverageScale = 1.0f;

#define trailCopySegment(D,S) memcpy(D, S, sizeof(trailsegment))

trhandle  g_glowHandle = 0;

#define TM_ACCEL            0
#define TM_GLOW_0           1
#define TM_GLOW_1           2
#define TM_GLOW_2           3
#define TM_GLOW_3           4
#define TM_FRIGATE_GLOW_0   5
#define TM_FRIGATE_GLOW_1   6
#define TM_FRIGATE_GLOW_2   7
#define TM_FRIGATE_GLOW_3   8
#define TM_MOSHIP_GLOW_R1_0 9
#define TM_MOSHIP_GLOW_R1_1 10
#define TM_MOSHIP_GLOW_R1_2 11
#define TM_MOSHIP_GLOW_R1_3 12
#define TM_MOSHIP_GLOW_R2_0 13
#define TM_MOSHIP_GLOW_R2_1 14
#define TM_MOSHIP_GLOW_R2_2 15
#define TM_MOSHIP_GLOW_R2_3 16
#define TM_MOSHIP_GLOW_P1_0 17
#define TM_MOSHIP_GLOW_P1_1 18
#define TM_MOSHIP_GLOW_P1_2 19
#define TM_MOSHIP_GLOW_P1_3 20
#define TM_NUMDEFS          21

char* trailMeshNames[TM_NUMDEFS] =
{
    "energybolt",
    "engine_glow00",
    "engine_glow01",
    "engine_glow02",
    "engine_glow03",
    "engine_glow_frigate00",
    "engine_glow_frigate01",
    "engine_glow_frigate02",
    "engine_glow_frigate03",
    "r1moship0",
    "r1moship1",
    "r1moship2",
    "r1moship3",
    "r2moship0",
    "r2moship1",
    "r2moship2",
    "r2moship3",
    "p1moship0",
    "p1moship1",
    "p1moship2",
    "p1moship3"
};

meshdata* trailMeshes[TM_NUMDEFS];

sdword trailInsertCount = 0;

bool8 _afterburning;

static void trailSegmentsRead(char *directory,char *field,void *dataToFillIn);
static void trailGranularityRead(char *directory,char *field,void *dataToFillIn);
static void trailColorRead(char *directory,char *field,void *dataToFillIn);
scriptEntry trailStaticScriptTable[] =
{
    { "trailSegments",                  trailSegmentsRead,  NULL },
    { "trailGranularity",               trailGranularityRead,  NULL },
    { "trailColor",                     trailColorRead,  NULL },
    { NULL,NULL, NULL}
};

/*=============================================================================
    Functions:
=============================================================================*/

/*-----------------------------------------------------------------------------
    Name        : trailStartup
    Description : Startup the trail module
    Inputs      :
    Outputs     : nothing yet!
    Return      :
----------------------------------------------------------------------------*/
void trailStartup(void)
{
    sdword i;
    char   filename[128];

#if TRAIL_VERBOSE_LEVEL >= 1
    dbgMessage("Startup engine trail module");
#endif

    strcpy(filename, "etg\\textures\\glow32");
    g_glowHandle = trTextureRegister(filename, NULL, NULL);

    for (i = 0; i < TM_NUMDEFS; i++)
    {
        strcpy(filename, "misc\\EngineGlows\\");
        strcat(filename, trailMeshNames[i]);
        strcat(filename, ".geo");
        trailMeshes[i] = meshLoad(filename);
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailShutdown
    Description : Shut down the engine trails.
    Inputs      :
    Outputs     : nothing yet!
    Return      :
----------------------------------------------------------------------------*/
void trailShutdown(void)
{
    sdword i;

#if TRAIL_VERBOSE_LEVEL >= 1
    dbgMessage("Shutdown engine trail module");
#endif

    for (i = 0; i < TM_NUMDEFS; i++)
    {
        if (trailMeshes[i] != NULL)
        {
            meshFree(trailMeshes[i]);
            trailMeshes[i] = NULL;
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailStaticDelete
    Description : Deletes a trail static structure and all info associated therewith
    Inputs      : trailInfo - structure to delete
    Outputs     : ..
    Return      :
----------------------------------------------------------------------------*/
void trailStaticDelete(trailstatic *trailInfo)
{
    memFree(trailInfo);
}

/*-----------------------------------------------------------------------------
    Name        : mistrailNew
    Description : allocate and initialize a new missile trail
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
missiletrail* mistrailNew(trailstatic* staticInfo, void* vmissile)
{
    missiletrail* trail;
    MissileStaticInfo* missilestaticinfo;
    Missile* missile;

    missile = (Missile*)vmissile;

    dbgAssertOrIgnore(staticInfo != NULL);
#if TRAIL_VERBOSE_LEVEL >= 2
    dbgMessagef("mistrailNew: %d segments, 0x%x staticInfo", staticInfo->nSegments, staticInfo);
#endif
    trail = memAlloc(mistrailSize(staticInfo->nSegments), "Missile Trail", NonVolatile);
    trail->vmissile = vmissile;
    trail->iHead = trail->iTail = trail->nLength = 0;
    trail->grainCounter = 0;
    trail->staticInfo = staticInfo;

    missilestaticinfo = missile->staticinfo;

    trail->width = missilestaticinfo->trailWidth;
    trail->height = missilestaticinfo->trailHeight;

    return trail;
}

/*-----------------------------------------------------------------------------
    Name        : trailNew
    Description : Allocate and initialize a new ship trail
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
shiptrail *trailNew(trailstatic *staticInfo, void* vship, bool8 second, ubyte trailNum)
{
    shiptrail *trail;
    Ship* ship;
    ShipStaticInfo* shipstaticinfo;

#if TRAIL_VERBOSE_LEVEL >= 2
    if (staticInfo != NULL)
    {
        dbgMessagef("trailNew: %d segments, 0x%x staticInfo", staticInfo->nSegments, staticInfo);
    }
#endif
    if (staticInfo != NULL)
    {
        trail = memAlloc(trailSize(staticInfo->nSegments), "Ship Engine Trail", NonVolatile);
    }
    else
    {
        trail = memAlloc(trailSize(1), "Ship Capital Trail", NonVolatile);
    }
    trail->ran = 0.0f;
    trail->ranCounter = 0;
    trail->lastvelsquared = trail->prevvelsquared = 0.0f;
    trail->vship = vship;
    trail->iHead = trail->iTail = trail->nLength = 0;
    trail->grainCounter = 0;
    trail->staticInfo = staticInfo;
    trail->second = second;
    trail->trailNum = trailNum;

    trail->wobbly = FALSE;

    ship = (Ship*)vship;
    shipstaticinfo = (ShipStaticInfo*)ship->staticinfo;

    if (second == 2 || second == 3)
    {
        //if second == 3, race 1
        trail->width = shipstaticinfo->trailWidth[trailNum];
        trail->height = shipstaticinfo->trailHeight[trailNum];
        trail->angle = shipstaticinfo->trailAngle[trailNum];

        if (shipstaticinfo->trailRibbonAdjust[trailNum] == -1.0f)
        {
            trail->ribbonadjust = 1.28f;
        }
        else
        {
            trail->ribbonadjust = shipstaticinfo->trailRibbonAdjust[trailNum];
        }

        if (shipstaticinfo->trailLength[trailNum] != -1.0f)
        {
            trail->ribbonadjust = shipstaticinfo->trailLength[trailNum] / trail->width;
        }

        trail->style = (bool8)(second+1);
        if (shipstaticinfo->trailStyle[trailNum] == 3)
        {
            trail->style = 5;
        }

        trail->exponent = 4.0f;
        trail->state = TRAIL_CONTRACTED;
        trail->counter = 0;

        trail->scalecap = shipstaticinfo->trailScaleCap[trailNum];

        return trail;
    }

    trail->state = TRAIL_CONTRACTED;
    trail->counter = 0;

    trail->width = shipstaticinfo->trailWidth[trailNum];
    trail->height = shipstaticinfo->trailHeight[trailNum];
    if (shipstaticinfo->trailAngle[trailNum] == -1.0f)
    {
        trail->angle = -1.0f;
    }
    else
    {
        trail->angle = DEG_TO_RAD(shipstaticinfo->trailAngle[trailNum]);
    }
    trail->ribbonadjust = shipstaticinfo->trailRibbonAdjust[trailNum];
    trail->style = (bool8)shipstaticinfo->trailStyle[trailNum];

    return(trail);
}

/*-----------------------------------------------------------------------------
    Name        : mistrailDelete
    Description : delete a missile trail structure
    Inputs      :
    Outputs     : frees its memory with memFree
    Return      :
----------------------------------------------------------------------------*/
void mistrailDelete(missiletrail* trail)
{
#if TRAIL_VERBOSE_LEVEL >= 2
    if (trail->staticInfo != NULL)
    {
        dbgMessagef("mistrailDelete: freeing trail0x%x with %d segments",
                    trail, trail->staticInfo->nSegments);
    }
#endif
    memFree(trail);
}

/*-----------------------------------------------------------------------------
    Name        : trailDelete
    Description : Delete an engine trail structure
    Inputs      :
    Outputs     : Frees its memory with memFree
    Return      :
----------------------------------------------------------------------------*/
void trailDelete(shiptrail *trail)
{
#if TRAIL_VERBOSE_LEVEL >= 2
    if (trail->staticInfo != NULL)
    {
        dbgMessagef("trailDelete: freeing trail 0x%x with %d segments", trail, trail->staticInfo->nSegments);
    }
#endif
    memFree(trail);
}

static void homogenize(vector *v, hvector *h)
{
    real32 oneOverW;

    vecGrabVecFromHVec(*v, *h);
    if (h->w == 0.0f || h->w == 1.0f)
        return;

    oneOverW = 1.0f / h->w;
    v->x *= oneOverW;
    v->y *= oneOverW;
    v->z *= oneOverW;
}

/*-----------------------------------------------------------------------------
    Name        : trailInplacePossibleRotate
    Description : rotate a vector around the z axis if provided angle != -1.0
    Inputs      : vec - the vector to rotate
                  degrees - angle
    Outputs     : vec is modified (rotated), or not (if degrees = -1.0)
    Return      :
----------------------------------------------------------------------------*/
void trailInplacePossibleRotate(vector* vec, real32 degrees)
{
    hmatrix hmat;
    hvector hvec0, hvec1;

    if (degrees == -1.0f)
    {
        return;
    }

    vecMakeHVecFromVec(hvec0, *vec);
    hmatMakeRotAboutZ(&hmat, (real32)cos(degrees), (real32)sin(degrees));
    hmatMultiplyHMatByHVec(&hvec1, &hmat, &hvec0);
    homogenize(vec, &hvec1);
    vecNormalize(vec);
}

/*-----------------------------------------------------------------------------
    Name        : trailInplaceShipToWorld
    Description : object -> world transform on a point
    Inputs      : vec - the vector to transform
                  coordsys - 3x3 rotation matrix
                  position - translation vector
    Outputs     : vec is modified (moved to worldspace)
    Return      :
----------------------------------------------------------------------------*/
void trailInplaceShipToWorld(real32* vec, real32* coordsys, real32* position)
{
    vector temp;

    matMultiplyMatByVec(&temp, (matrix*)coordsys, (vector*)vec);
    vec[0] = position[0] + temp.x;
    vec[1] = position[1] + temp.y;
    vec[2] = position[2] + temp.z;
}

//convenience fn.  copies shipstaticinfo.enginenozzleoffset into provided vector
void trailGetNozzleOffset(real32* vec, shiptrail* trail)
{
    Ship* ship = (Ship*)trail->vship;

    VECCOPY(vec, &((ShipStaticInfo*)ship->staticinfo)->engineNozzleOffset[trail->trailNum]);
}

//convenience fn.  copies ship.coordsys into provided matrix
void trailGetCoordsys(real32* mat, shiptrail* trail)
{
    Ship* ship = (Ship*)trail->vship;
    MATCOPY(mat, &ship->rotinfo.coordsys);
}

//convenience fn.  copies ship.position into provided vector
void trailGetTranslation(real32* trans, shiptrail* trail)
{
    Ship* ship = (Ship*)trail->vship;
    VECCOPY(trans, &ship->posinfo.position);
}

//random real 0->n
real32 trailRealRand(real32 n)
{
    if (n == 0.0f)
    {
        return 0.0f;
    }
    else
    {
        return((real32)(ranRandom(RANDOM_TRAILS_0) % (sdword)(n * 10000.0f)) / 10000.0f);
    }
}

/*-----------------------------------------------------------------------------
    Name        : mistrailUpdate
    Description : update a missile trail
    Inputs      :
    Outputs     : updates trail's circular queue
    Return      :
----------------------------------------------------------------------------*/
void mistrailUpdate(missiletrail* trail, vector* position)
{
    trailstatic* trailStatic = trail->staticInfo;

    if (!enableTrails)
    {
        return;
    }

    trail->grainCounter--;
    if (trail->grainCounter < 0)
    {
        trail->grainCounter = trailStatic->granularity;
    }
    else
    {
        return;
    }

    dbgAssertOrIgnore(trail->iHead < trailStatic->nSegments);

    trail->segments[trail->iHead].position = *position;

    if (trail->nLength < trailStatic->nSegments)
    {
        //if still building missile trail, insert new point
        trail->nLength++;
        trail->iHead++;
        if (trail->iHead >= trailStatic->nSegments)
        {
            trail->iHead = 0;
        }
    }
    else
    {
        //else the list is full size, wrap around
        trail->iHead = (trail->iHead + 1) < trailStatic->nSegments ? trail->iHead + 1 : 0;
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailUpdate
    Description : Update a trail
    Inputs      : position - 3d location to put at head of trail
    Outputs     : Updates trail's circular queue
    Return      : void
----------------------------------------------------------------------------*/
void trailUpdate(shiptrail *trail, vector *position)
{
    trailstatic *trailStatic = trail->staticInfo;
    trailsegment* seg;
    trailsegment* seg2;

    if (!enableTrails)
    {
        return;
    }

    if (trail->style > 2)
    {
        trail->exponent = trailRealRand(TRAIL_EXPONENT_RANGE) + TRAIL_EXPONENT_BASE;
        if (trail->counter > 0)
        {
            trail->counter--;
        }
        switch (trail->state)
        {
        case TRAIL_CONTRACTING:
            if (trail->counter == 0)
            {
                trail->state = TRAIL_CONTRACTED;
            }
            break;

        case TRAIL_CONTRACTED:
            break;

        case TRAIL_EXPANDING:
            if (trail->counter == 0)
            {
                trail->state = TRAIL_EXPANDED;
            }
            break;

        case TRAIL_EXPANDED:
            break;
        }
        return;
    }

    trail->grainCounter--;
    if (trail->grainCounter < 0)
    {
        trail->grainCounter = trailStatic->granularity;
    }
    else
    {
        return;
    }

    dbgAssertOrIgnore(trail->iHead < trailStatic->nSegments);

#if TRAIL_GATHER_STATS
    trailsUpdated++;
#endif

    seg = &trail->segments[trail->iHead];

    trailGetNozzleOffset(seg->position, trail);
    trailGetCoordsys(seg->rotation, trail);
    trailGetTranslation(seg->translation, trail);

    trailInplaceShipToWorld(seg->position, seg->rotation, seg->translation);

    VECCOPY(seg->horizontal, yaxis);
    trailInplacePossibleRotate((vector*)seg->horizontal, trail->angle);
    trailInplaceShipToWorld(seg->horizontal, seg->rotation, origin);

    VECCOPY(seg->vertical, xaxis);
    trailInplacePossibleRotate((vector*)seg->vertical, trail->angle);
    trailInplaceShipToWorld(seg->vertical, seg->rotation, origin);

    trail->segments[trail->iHead].wide = FALSE;

    seg2 = &trail->segments[(trail->iHead - 1) <= 1 ? trailStatic->nSegments - 1 : trail->iHead - 1];

    if (trail->nLength < trailStatic->nSegments)
    {
        //if still building ship trail, insert new point
        trail->nLength++;                                     //update head and length
        trail->iHead++;
        if ((trail->iHead) >= trailStatic->nSegments)
        {
            trail->iHead = 0;
        }
    }
    else
    {
        //else the list is full size, wrap around
        trail->iHead = (trail->iHead + 1) < trailStatic->nSegments ? trail->iHead + 1 : 0;
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailColorDesaturate
    Description : desaturates a color.  involves an hsv conversion, and back
    Inputs      : c - the color to desaturate
                  de - amount to desaturate [0..255]
    Outputs     :
    Return      : modified color
----------------------------------------------------------------------------*/
color trailColorDesaturate(color c, GLint de)
{
    GLfloat h, s, v;
    GLfloat red = colUbyteToReal(colRed(c));
    GLfloat green = colUbyteToReal(colGreen(c));
    GLfloat blue = colUbyteToReal(colBlue(c));

    colRGBToHSV(&h, &s, &v, red, green, blue);

    s -= colUbyteToReal(de);
    if (s < 0.0f)
    {
        s = 0.0f;
    }

    colHSVToRGB(&red, &green, &blue, h, s, v);

    return colRGB(colRealToUbyte(red), colRealToUbyte(green), colRealToUbyte(blue));
}

/*-----------------------------------------------------------------------------
    Name        : trailColorSaturate
    Description : saturates a color.  involves an hsv conversion, and back
    Inputs      : c - the color to saturate
                  inc - amount to saturate [0..255]
    Outputs     :
    Return      : modified color
----------------------------------------------------------------------------*/
color trailColorSaturate(color c, GLint inc)
{
    GLfloat h, s, v;
    GLfloat red = colUbyteToReal(colRed(c));
    GLfloat green = colUbyteToReal(colGreen(c));
    GLfloat blue = colUbyteToReal(colBlue(c));

    colRGBToHSV(&h, &s, &v, red, green, blue);

    s += colUbyteToReal(inc);
    if (s > 1.0f)
    {
        s = 1.0f;
    }

    colHSVToRGB(&red, &green, &blue, h, s, v);

    return colRGB(colRealToUbyte(red), colRealToUbyte(green), colRealToUbyte(blue));
}

//returns the ratio of trail height to width
real32 trailHeightScalar(shiptrail* trail)
{
    return (trail->height == -1.0f || trail->width == -1.0f)
            ? 0.25f
            : trail->height / trail->width;
}

//returns half the trail width
real32 trailHalfWidth(shiptrail* trail)
{
    return (trail->width == -1.0f) ? HALFWIDTH : 0.5f * trail->width;
}

//returns half the trail height
real32 trailHalfHeight(shiptrail* trail)
{
    return (trail->height == -1.0f)
            ? trailHeightScalar(trail) * HALFWIDTH
            : 0.5f * trail->height;
}

//returns a scalar to modify the size of a ribbon, normally the height of the trail
real32 trailRibbonAdjust(shiptrail* trail)
{
    return (trail->ribbonadjust == -1.0f) ? 1.0f : trail->ribbonadjust;
}

static real32 trailModernCoverageScale(Ship *ship, shiptrail *trail)
{
#ifdef HW_MODERN_DISABLE_NLIPS
    real32 screenRadius = ship->collInfo.selCircleRadius;
    real32 worldRadius = ship->staticinfo->staticheader.staticCollInfo.collspheresize;
    real32 halfWidth = trailHalfWidth(trail);
    real32 pixelsPerWorldUnit;
    real32 projectedHalfWidth;
    real32 scale;

    if (screenRadius <= 0.0f || worldRadius <= 0.0f || halfWidth <= 0.0f)
    {
        return 1.0f;
    }

    pixelsPerWorldUnit = screenRadius / worldRadius;
    projectedHalfWidth = halfWidth * pixelsPerWorldUnit;
    if (projectedHalfWidth >= 0.58f)
    {
        return 1.0f;
    }

    scale = 0.58f / MAX2(projectedHalfWidth, 0.01f);
    return MIN2(MAX2(scale, 1.0f), 2.0f);
#else
    (void)ship;
    (void)trail;
    return 1.0f;
#endif
}

/*-----------------------------------------------------------------------------
    Name        : trailDrawCapitalGlow
    Description : renders a capital ship's engine glow
    Inputs      : trail - the ship trail structure
                  LOD - the lod of the ship
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailDrawCapitalGlow(shiptrail* trail, sdword LOD)
{
    Ship* ship = (Ship*)trail->vship;
    ShipStaticInfo* shipstaticinfo = (ShipStaticInfo*)ship->staticinfo;
    real32 maxvel, mag, velratio;
    vector vel, pos;
    hmatrix coordMatrixForGL;
    real32 scaleFactor;
    real32 scale[3];
    color lightColor;
    bool32 moshipGlow;
    GLboolean depthTestWasEnabled;
    GLboolean depthWriteWasEnabled;
    GLint previousDepthFunc;
    extern bool8 g_NoMatSwitch;

    if (trail->width == -1.0f || trail->state == TRAIL_CONTRACTED)
    {
        return;
    }

    if (trail->width == 1.0f && trail->height == 1.0f)
    {
        moshipGlow = TRUE;
    }
    else
    {
        moshipGlow = FALSE;
    }

    if (shipstaticinfo->shiprace == P1 &&
        shipstaticinfo->shipclass == CLASS_Mothership)
    {
        moshipGlow = 2;
        trail->width = 1.0f;
        trail->height = 1.0f;
        trail->ribbonadjust = 1.0f;
    }

//    maxvel = ship->staticinfo->staticheader.maxvelocity;
    maxvel = tacticsGetShipsMaxVelocity(ship);
    VECCOPY(&vel, &ship->posinfo.velocity);
    mag = fsqrt(vecMagnitudeSquared(vel));
    velratio = 0.5f * mag / maxvel;
    if (velratio == 0.0f)
    {
        switch (trail->state)
        {
        case TRAIL_CONTRACTED:
            return;

        case TRAIL_CONTRACTING:
            break;

        case TRAIL_EXPANDED:
        case TRAIL_EXPANDING:
            trail->state = TRAIL_CONTRACTING;
            trail->counter = (ubyte)TRAIL_EXPANSION_TICKS;
            break;
        }
    }

    trailGetNozzleOffset((real32*)&pos, trail);

    /* Engine glow is translucent but must still respect opaque scene depth. */
    depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    rndLightingEnable(TRUE);
    rndTextureEnable(FALSE);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    rndAdditiveBlends(TRUE);

    if (trail->style == 4 || trail->style == 5)
    {
        lightColor = colRGB(TRAIL_GLOW_1_RED, TRAIL_GLOW_1_GREEN,
                            TRAIL_GLOW_1_BLUE);
        if (usingShader)
        {
            meshSetSpecular(2,
                            (ubyte)TRAIL_GLOW_1_RED,
                            (ubyte)TRAIL_GLOW_1_GREEN,
                            (ubyte)TRAIL_GLOW_1_BLUE,
                            160);
        }
        else
        {
            glColor4ub((GLubyte)TRAIL_GLOW_1_RED, (GLubyte)TRAIL_GLOW_1_GREEN,
                       (GLubyte)TRAIL_GLOW_1_BLUE, 160);
        }
    }
    else
    {
        lightColor = colRGB(TRAIL_GLOW_2_RED, TRAIL_GLOW_2_GREEN,
                            TRAIL_GLOW_2_BLUE);
        if (usingShader)
        {
            meshSetSpecular(2,
                            (ubyte)TRAIL_GLOW_2_RED,
                            (ubyte)TRAIL_GLOW_2_GREEN,
                            (ubyte)TRAIL_GLOW_2_BLUE,
                            160);
        }
        else
        {
            glColor4ub((GLubyte)TRAIL_GLOW_2_RED, (GLubyte)TRAIL_GLOW_2_GREEN,
                       (GLubyte)TRAIL_GLOW_2_BLUE, 160);
        }
    }

    glPushMatrix();
    hmatMakeHMatFromMat(&coordMatrixForGL, &ship->rotinfo.coordsys);
    hmatPutVectIntoHMatrixCol4(ship->posinfo.position, coordMatrixForGL);
    glMultMatrixf((GLfloat*)&coordMatrixForGL);

    if (moshipGlow && (moshipGlow != 2))
    {
        scaleFactor = 1.0f;
        glTranslatef(0.0f, 0.0f, pos.z);
    }
    else
    {
        if (moshipGlow)
        {
            scaleFactor = 1.0f;
            glTranslatef(pos.x, pos.y, pos.z);
        }
        else
        {
#ifdef HW_MODERN_DISABLE_NLIPS
            scaleFactor = 1.0f;
            glTranslatef(pos.x, pos.y, pos.z);
#else
            if (shipstaticinfo->scaleCap != 0.0f)
            {
                scaleFactor = 1.0f - selCameraSpace.z * shipstaticinfo->scaleCap;
                glTranslatef(pos.x * scaleFactor, pos.y * scaleFactor, pos.z * scaleFactor);
            }
            else
            {
                glTranslatef(pos.x, pos.y, pos.z);
            }

            if (trail->scalecap == -1.0f || trail->scalecap == 0.0f)
            {
                scaleFactor = 1.0f;
            }
            else
            {
                scaleFactor = 1.0f - selCameraSpace.z * trail->scalecap;
            }
#endif
        }
    }

    if (trail->counter)
    {
        real32 x, y, z;
        real32 scalar;

        x = trail->height;
        y = trail->width;
        z = trail->width * trail->ribbonadjust;
        if (trail->state == TRAIL_EXPANDING)
        {
            scalar = 1.0f - ((real32)trail->counter / (real32)TRAIL_EXPANSION_TICKS);
        }
        else
        {
            scalar = (real32)trail->counter / (real32)TRAIL_EXPANSION_TICKS;
        }
        z *= scalar;
        if (z == 0.0f)
        {
            z = 0.023f;
        }

        scale[0] = x * scaleFactor;
        scale[1] = y * scaleFactor;
        scale[2] = z * scaleFactor;
    }
    else
    {
        scale[0] = scaleFactor * trail->height;
        scale[1] = scaleFactor * trail->width;
        scale[2] = scaleFactor * trail->width * trail->ribbonadjust;
    }

#ifdef HW_ENABLE_D3D12_BACKEND
    {
        /* Reuse the authored EngX nozzle and the exact original glow transform.
           Do not derive this from the billboard or from N-LIPS-scaled geometry. */
        HWModernDynamicLightEmitter emitter;
        vector worldPosition = pos;
        vector direction;
        real32 lightRadius = MAX2(scale[0], MAX2(scale[1], scale[2]));
        real32 shipRadius = shipstaticinfo->staticheader.staticCollInfo.collspheresize *
            ship->magnitudeSquared;
        real32 maximumLocalRadius = MAX2(18.0f, shipRadius * 0.18f);

        /* The R1/R2 mothership glow mesh bakes its lateral nozzle placement
           into the GEO and the original draw path translates only by EngX.z.
           Match that transform exactly instead of moving the light to x/y
           coordinates the visible glow never used. */
        if (moshipGlow && moshipGlow != 2)
        {
            worldPosition.x = 0.0f;
            worldPosition.y = 0.0f;
        }
        trailInplaceShipToWorld((real32 *)&worldPosition,
                                (real32 *)&ship->rotinfo.coordsys,
                                (real32 *)&ship->posinfo.position);
        VECCOPY(&direction, &zaxis);
        trailInplaceShipToWorld((real32 *)&direction,
                                (real32 *)&ship->rotinfo.coordsys, origin);
        vecNormalize(&direction);

        memset(&emitter, 0, sizeof(emitter));
        emitter.source = HW_MODERN_LIGHT_ENGINE;
        emitter.shape = HW_MODERN_LIGHT_POINT;
        VECCOPY(emitter.position, &worldPosition);
        VECCOPY(emitter.direction, &direction);
        emitter.color[0] = colUbyteToReal(colRed(lightColor));
        emitter.color[1] = colUbyteToReal(colGreen(lightColor));
        emitter.color[2] = colUbyteToReal(colBlue(lightColor));
        lightRadius *= 0.58f;
        if (lightRadius > maximumLocalRadius) lightRadius = maximumLocalRadius;
        emitter.radius = MAX2(lightRadius, 1.0f);
        emitter.intensity = emitter.radius * 4.2f;
        hwModernGraphicsSubmitDynamicLight(&emitter);
    }
#endif

    glScalef(scale[0], scale[1], scale[2]);

    g_NoMatSwitch = TRUE;
    glEnable(GL_NORMALIZE);
    if (usingShader)
    {
        shSetExponent(2, trail->exponent);
    }

    glShadeModel(GL_SMOOTH);

    if (shipstaticinfo->shiprace == P1 &&
        shipstaticinfo->shipclass == CLASS_Mothership)
    {
        switch (LOD)
        {
            case 0:
            case 1:
                meshRender(trailMeshes[TM_MOSHIP_GLOW_P1_0], 0);
                break;
            case 2:
                meshRender(trailMeshes[TM_MOSHIP_GLOW_P1_1], 0);
                break;
            case 3:
                meshRender(trailMeshes[TM_MOSHIP_GLOW_P1_2], 0);
                break;
            default:
                meshRender(trailMeshes[TM_MOSHIP_GLOW_P1_3], 0);
        }
    }
    else if (trail->style == 5)
    {
        switch (LOD)
        {
        case 0:
        case 1:
            meshRender(trailMeshes[TM_FRIGATE_GLOW_0], 0);
            break;
        case 2:
            meshRender(trailMeshes[TM_FRIGATE_GLOW_1], 0);
            break;
        case 3:
            meshRender(trailMeshes[TM_FRIGATE_GLOW_2], 0);
            break;
        default:
            meshRender(trailMeshes[TM_FRIGATE_GLOW_3], 0);
        }
    }
    else
    {
        if (moshipGlow)
        {
            if (shipstaticinfo->shiprace == R1)
            {
                glScalef(1.0f, 1.5f, 1.0f);
                switch (LOD)
                {
                case 0:
                case 1:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R1_0], 0);
                    break;
                case 2:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R1_1], 0);
                    break;
                case 3:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R1_2], 0);
                    break;
                default:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R1_3], 0);
                }
            }
            else
            {
                switch (LOD)
                {
                case 0:
                case 1:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R2_0], 0);
                    break;
                case 2:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R2_1], 0);
                    break;
                case 3:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R2_2], 0);
                    break;
                default:
                    meshRender(trailMeshes[TM_MOSHIP_GLOW_R2_3], 0);
                }
            }
        }
        else
        {
            switch (LOD)
            {
            case 0:
            case 1:
                meshRender(trailMeshes[TM_GLOW_0], 0);
                break;
            case 2:
                meshRender(trailMeshes[TM_GLOW_1], 0);
                break;
            case 3:
                meshRender(trailMeshes[TM_GLOW_2], 0);
                break;
            default:
                meshRender(trailMeshes[TM_GLOW_3], 0);
            }
        }
    }

    glDisable(GL_NORMALIZE);
    g_NoMatSwitch = FALSE;

    glDepthMask(depthWriteWasEnabled);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc((GLenum)previousDepthFunc);
    glDisable(GL_BLEND);
    rndAdditiveBlends(FALSE);
    glPopMatrix();

    if (usingShader)
        meshSetSpecular(-1, 0, 0, 0, 0);
}


/*-----------------------------------------------------------------------------
    Name        : trailDrawBillboardedSquareThingz
    Description : draws the engine glow(s) and possible afterburner flash, appropriately scaled.
                  also sets a flag in provided trailsegment indicating whether afterburners
                  were engaged
    Inputs      : trail - the shiptrail structure
                  seg - current segment being considered.  not necessarily a part of trail
                  rad - radius of object (ship)
                  ship - the ship
                  c - color of the segment
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailDrawBillboardedSquareThingz(shiptrail* trail, trailsegment* seg, real32 rad, Ship* ship, color c)
{
    vector pos, vel;
    real32 mag, maxvel, velratio;
    color haloColor, bloomColor, coreColor;
    GLboolean depthTestWasEnabled;
    GLboolean depthWriteWasEnabled;
    GLint previousDepthFunc;

    seg->wide = FALSE;

    /* Use a continuous deterministic pulse.  The legacy random step changed
       radius every few frames and made the nozzle appear to pop or vanish. */
    trail->ran = 0.975f + 0.025f * (real32)sin(
        (double)(universe.totaltimeelapsed * 7.0f +
                 (real32)trail->trailNum * 1.713f));

    maxvel = tacticsGetShipsMaxVelocity(ship);

    //scale radius by velocity fn
    VECCOPY(&vel, &ship->posinfo.velocity);
    mag = fsqrt(vecMagnitudeSquared(vel));
    velratio = mag / maxvel;
    if (velratio < MIN2(VELRATIO_THRESH, 0.02f))
    {
        return;
    }

    //grab ship's static glow radius if one exists
    if (ship->staticinfo->trailSpriteRadius[trail->trailNum] != -1.0f)
    {
        rad = ship->staticinfo->trailSpriteRadius[trail->trailNum];
    }

    rad = (1.0f - GLOW_BASERADIALSCALE) * (GLOW_RADIALADJUST * velratio * rad * trail->ran)
          + GLOW_BASERADIALSCALE * rad;

    depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasEnabled);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    rndLightingEnable(FALSE);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    rndTextureEnvironment(RTE_Modulate);

    glColor4ub(255,255,255,255);

    VECCOPY(&pos, seg->position);
    pos.z -= 0.6f*XADJ;
    //offset also by ship's static sprite offset amount
    pos.z -= ship->staticinfo->trailSpriteOffset[trail->trailNum];
    trailInplaceShipToWorld((real32*)&pos, seg->rotation, seg->translation);

#ifdef HW_ENABLE_D3D12_BACKEND
    {
        HWModernDynamicLightEmitter emitter;
        vector direction;

        VECCOPY(&direction, &zaxis);
        trailInplaceShipToWorld((real32 *)&direction, seg->rotation, origin);
        vecNormalize(&direction);

        memset(&emitter, 0, sizeof(emitter));
        emitter.source = HW_MODERN_LIGHT_ENGINE;
        emitter.shape = HW_MODERN_LIGHT_POINT;
        VECCOPY(emitter.position, &pos);
        VECCOPY(emitter.direction, &direction);
        emitter.color[0] = colUbyteToReal(colRed(c));
        emitter.color[1] = colUbyteToReal(colGreen(c));
        emitter.color[2] = colUbyteToReal(colBlue(c));
        {
            real32 shipRadius = ship->staticinfo->staticheader.staticCollInfo.collspheresize *
                ship->magnitudeSquared;
            real32 localRadius = rad * 0.62f;
            real32 maximumLocalRadius = MAX2(12.0f, shipRadius * 0.14f);
            if (localRadius > maximumLocalRadius) localRadius = maximumLocalRadius;
            emitter.radius = MAX2(localRadius, 1.0f);
        }
        emitter.intensity = emitter.radius * 3.2f;
        hwModernGraphicsSubmitDynamicLight(&emitter);
    }
#endif

    if (g_glowHandle != TR_InvalidHandle)
    {
        trMakeCurrent(g_glowHandle);
        partFilter(TRUE);

        rndBillboardEnable(&pos);
        haloColor = colRGBA(colRed(c), colGreen(c), colBlue(c), 60);
        bloomColor = colRGBA(
            MIN2(255, colRed(c) + 40),
            MIN2(255, colGreen(c) + 40),
            MIN2(255, colBlue(c) + 40), 176);
        coreColor = colRGBA(255, 250, 226, 238);
        primSolidTexture3((vector*)&origin, rad * 2.25f, haloColor, g_glowHandle);
        primSolidTexture3((vector*)&origin, rad * 1.18f, bloomColor, g_glowHandle);
        primSolidTexture3((vector*)&origin, rad * 0.48f, coreColor, g_glowHandle);
        rndBillboardDisable();
    }
    else
    {
        rndBillboardEnable(&pos);
        primCircleSolid3((vector*)&origin, rad * 2.0f, 32,
                         colRGBA(colRed(c), colGreen(c), colBlue(c), 54));
        primCircleSolid3((vector*)&origin, rad, 32,
                         colRGBA(colRed(c), colGreen(c), colBlue(c), 184));
        primCircleSolid3((vector*)&origin, rad * 0.42f, 32,
                         colRGBA(255, 250, 226, 238));
        rndBillboardDisable();
    }

    glEnable(GL_CULL_FACE);
    rndLightingEnable(TRUE);
    glDepthMask(depthWriteWasEnabled);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc((GLenum)previousDepthFunc);

    if (trailMeshes[TM_ACCEL] != NULL
        && (trail->lastvelsquared - trail->prevvelsquared > ACCELTHRESH)
        && (velratio > VELRATIO_ACCELTHRESH))
    {
        _afterburning = TRUE;
        seg->wide = TRUE;
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailStraighten
    Description : apply tension to the initial portion of a trail
    Inputs      : positions - segment positions
                  coordsys - coordinate system at engine nozzle
    Outputs     : positions is modified
    Return      :
----------------------------------------------------------------------------*/
void trailStraighten(vector positions[], real32* coordsys)
{
    vector directions[6];
    real32 distances[6];
    sdword i;

    //gather info
    for (i = 0; i < 6; i++)
    {
        vecSub(directions[i], positions[i+1], positions[i]);
        distances[i] = vecMagnitudeSquared(directions[i]);
        if (distances[i] > 0.0f)
        {
            distances[i] = fsqrt(distances[i]);
        }
        vecNormalize(&directions[i]);
    }

    //setup tension
#if 1
    VECCOPY(&directions[0], &zaxis);
    trailInplaceShipToWorld((real32*)&directions[0], coordsys, origin);
    vecNormalize(&directions[0]);

    vecAddTo(directions[1], directions[0]);
    vecMultiplyByScalar(directions[1], 0.5f);

    vecMultiplyByScalar(directions[2], 2.0f);
    vecAddTo(directions[2], directions[0]);
    vecMultiplyByScalar(directions[2], 0.3333f);

//    vecMultiplyByScalar(directions[3], 4.0f);
//    vecAddTo(directions[3], directions[0]);
//    vecMultiplyByScalar(directions[3], 0.25f);

//    vecMultiplyByScalar(directions[4], 6.0f);
//    vecAddTo(directions[4], directions[0]);
//    vecMultiplyByScalar(directions[4], 0.1666f);
#endif

    //rebuild trail segments
    for (i = 2; i < 6; i++)
    {
        VECCOPY(&positions[i], &directions[i]);
        vecMultiplyByScalar(positions[i], distances[i]);
        vecAddTo(positions[i], positions[i-1]);
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailPositions
    Description : store the trail segment info in provided lists
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailPositions(sdword n, vector positions[], vector horizontals[], vector verticals[], shiptrail* trail)
{
    sdword i, index;
    trailsegment* segment;

    index = trail->iHead <= 0 ? trail->staticInfo->nSegments - 1 : trail->iHead - 1;

    if (horizontals == NULL)
    {
        for (i = 0; i < n; i++)
        {
            segment = &trail->segments[index];

            VECCOPY(&positions[i+1], segment->position);

            index = index <= 1 ? trail->staticInfo->nSegments - 1 : index - 1;
        }
    }
    else
    {
        for (i = 0; i < n; i++)
        {
            segment = &trail->segments[index];

            VECCOPY(&horizontals[i+1], segment->horizontal);
            VECCOPY(&verticals[i+1], segment->vertical);

            VECCOPY(&positions[i+1], segment->position);

            index = index <= 1 ? trail->staticInfo->nSegments - 1 : index - 1;
        }
    }
}

static real32 _HALFWIDTH, _HALFHEIGHT;
static shiptrail* activeTrail;
static sdword activeSegments;

/*-----------------------------------------------------------------------------
    Name        : trailLineInit
    Description : initializes globals before a trail is rendered
    Inputs      : trail - the trail
                  n - maximum number of segments
    Outputs     : activeTrail, _HALFWIDTH, _HALFHEIGHT are set
    Return      :
----------------------------------------------------------------------------*/
void trailLineInit(shiptrail* trail, sdword n)
{
    activeTrail = trail;
    activeSegments = n;
    _HALFWIDTH  = trailHalfWidth(trail) - HALFWIDTH_FALLOFF;
    _HALFHEIGHT = trailHalfHeight(trail) - HALFWIDTH_FALLOFF;
}

#define VERT(V) glVertex3fv((GLfloat*)&V)
#define COLx(C,A) glColor4ub(colRed(C), colGreen(C), colBlue(C), (A))

#define PYRAMID_ALPHA_LO  15
#define PYRAMID_ALPHA_MID 79
#define PYRAMID_ALPHA_HI  143

/*-----------------------------------------------------------------------------
    Name        : trailLinePyramid
    Description : draws a translucent pyramid, the outer sheath of engine trails at
                  particular LODs
    Inputs      : LOD - the lod
                  i - which position in provided arrays
                  vectora, vectorb - from, to vectors
                  c - the colour of the segment
                  horiz - horizontal components of the coordinate space
                  vert - vertical components
                  wide - whether this trail is wide or not
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailLinePyramid(
    sdword LOD, sdword i, vector* vectora, vector* vectorb, color c,
    vector horiz[], vector vert[], bool32 wide)
{
    vector from, to;
    real32 VS, halfWidth, width, height;
    real32 scaleFactor, temp;
    static vector a0, a0hi, a0lo, a1, a2;
    static vector b0, b0hi, b0lo, b1, b2;
    static color  cb;

    //setup

    VECCOPY(&from, vectora);
    VECCOPY(&to, vectorb);

    VS = trailHeightScalar(activeTrail);
    halfWidth = trailHalfWidth(activeTrail);

    scaleFactor = NLipsScaleFactor * trailScreenCoverageScale;

    //calc

    if (i == 0)
    {
        VECCOPY(&a0, &from);

        temp = halfWidth * scaleFactor;
        a1.x = a0.x - horiz[i].x * temp;
        a1.y = a0.y - horiz[i].y * temp;
        a1.z = a0.z - horiz[i].z * temp;

        a2.x = a0.x + horiz[i].x * temp;
        a2.y = a0.y + horiz[i].y * temp;
        a2.z = a0.z + horiz[i].z * temp;

        temp *= VS;
        a0lo.x = a0.x - vert[i].x * temp;
        a0lo.y = a0.y - vert[i].y * temp;
        a0lo.z = a0.z - vert[i].z * temp;

        a0hi.x = a0.x + vert[i].x * temp;
        a0hi.y = a0.y + vert[i].y * temp;
        a0hi.z = a0.z + vert[i].z * temp;

        cb = c;
    }
    else
    {
        VECCOPY(&a0, &b0);
        VECCOPY(&a1, &b1);
        VECCOPY(&a2, &b2);
        VECCOPY(&a0lo, &b0lo);
        VECCOPY(&a0hi, &b0hi);

        {
            color ct = cb;
            cb = c;
            c = ct;
        }
    }

    VECCOPY(&b0, &to);

    if (wide)
    {
        width = _HALFWIDTH * BURN_SIZESCALE;
        height = VS*_HALFWIDTH * BURN_SIZESCALE;
        if (BURN_COLORADJUST < 0)
        {
            cb = trailColorDesaturate(cb, -BURN_COLORADJUST);
        }
        else
        {
            cb = trailColorSaturate(cb, BURN_COLORADJUST);
        }
    }
    else
    {
        width = _HALFWIDTH;
        height = VS*_HALFWIDTH;
    }

    temp = width * scaleFactor;
    b1.x = b0.x - horiz[i+1].x * temp;
    b1.y = b0.y - horiz[i+1].y * temp;
    b1.z = b0.z - horiz[i+1].z * temp;

    b2.x = b0.x + horiz[i+1].x * temp;
    b2.y = b0.y + horiz[i+1].y * temp;
    b2.z = b0.z + horiz[i+1].z * temp;

    temp = height * scaleFactor;
    b0lo.x = b0.x - vert[i+1].x * temp;
    b0lo.y = b0.y - vert[i+1].y * temp;
    b0lo.z = b0.z - vert[i+1].z * temp;

    b0hi.x = b0.x + vert[i+1].x * temp;
    b0hi.y = b0.y + vert[i+1].y * temp;
    b0hi.z = b0.z + vert[i+1].z * temp;

    //render

    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    glBegin(GL_TRIANGLES);
    //a
    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a1);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b1);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0hi);

    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a1);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0hi);
    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0hi);
    
    //b
    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0hi);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0hi);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b2);

    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0hi);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b2);
    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a2);
    
    //c
    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a2);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b2);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0lo);

    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a2);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0lo);
    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0lo);
    
    //d
    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0lo);
    COLx(cb,PYRAMID_ALPHA_HI);
    VERT(b0lo);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b1);

    COLx(c,PYRAMID_ALPHA_HI);
    VERT(a0lo);
    COLx(cb,PYRAMID_ALPHA_LO);
    VERT(b1);
    COLx(c,PYRAMID_ALPHA_LO);
    VERT(a1);
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

/*-----------------------------------------------------------------------------
    Name        : trailLineFuzzySheath
    Description : draws a simplified pyramid (ie. flat)
    Inputs      : LOD - the lod number
                  i - trail segment number
                  vectora, vectorb - from, to
                  c - segment colour
                  horiz - horizontal components of the coordinate space
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailLineFuzzySheath(sdword LOD, sdword i, vector* vectora, vector* vectorb, color c, vector horiz[])
{
    vector from, to;
    real32 halfWidth, width, scalar = 0.9f;
    real32 scaleFactor, temp;
    static vector a0, a1, a2;
    static vector b0, b1, b2;
    static color  cb;

    //setup
    scaleFactor = NLipsScaleFactor * trailScreenCoverageScale;

    VECCOPY(&from, vectora);
    VECCOPY(&to, vectorb);

    halfWidth = scalar * trailHalfWidth(activeTrail);

    //calc

    if (i == 0)
    {
        VECCOPY(&a0, &from);

        temp = halfWidth * scaleFactor;
        a1.x = a0.x - horiz[i].x * temp;
        a1.y = a0.y - horiz[i].y * temp;
        a1.z = a0.z - horiz[i].z * temp;

        a2.x = a0.x + horiz[i].x * temp;
        a2.y = a0.y + horiz[i].y * temp;
        a2.z = a0.z + horiz[i].z * temp;

        cb = c;
    }
    else
    {
        VECCOPY(&a0, &b0);
        VECCOPY(&a1, &b1);
        VECCOPY(&a2, &b2);

        {
            color ct = cb;
            cb = c;
            c = ct;
        }
    }

    VECCOPY(&b0, &to);

    width = scalar * _HALFWIDTH;

    temp = width * scaleFactor;
    b1.x = b0.x - horiz[i+1].x * temp;
    b1.y = b0.y - horiz[i+1].y * temp;
    b1.z = b0.z - horiz[i+1].z * temp;

    b2.x = b0.x + horiz[i+1].x * temp;
    b2.y = b0.y + horiz[i+1].y * temp;
    b2.z = b0.z + horiz[i+1].z * temp;

    //render

    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    glBegin(GL_TRIANGLE_STRIP);
    COLx(c, PYRAMID_ALPHA_MID);
    VERT(a1);
    COLx(cb, PYRAMID_ALPHA_MID);
    VERT(b1);
    COLx(c, PYRAMID_ALPHA_MID);
    VERT(a2);
    COLx(cb, PYRAMID_ALPHA_MID);
    VERT(b2);
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

/*-----------------------------------------------------------------------------
    Name        : trailLineBillboard
    Description : draws a pseudo-billboarded ribbon as an engine trail
    Inputs      : LOD - lod number
                  i - segment number
                  vectora, vectorb - from, to vectors
                  c - colour of the segment
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailLineBillboard(
    sdword LOD, sdword i, vector* vectora, vector* vectorb, color c)
{
    vector veye, vseg, vcross;
    vector from, fromHi;
    vector to, toHi;
    vector subAmount;
    ubyte  alpha;
    static color  cb;
    static vector lastTo, lastToHi;

    //setup

    VECCOPY(&from, vectora);
    VECCOPY(&to, vectorb);

    //calc

    vecSub(veye, from, mrCamera->eyeposition);
    vecSub(vseg, to, from);
    vecNormalize(&veye);
    vecNormalize(&vseg);
    vecCrossProduct(vcross, vseg, veye);
    vecNormalize(&vcross);

    vecScalarMultiply(fromHi, vcross, trailScreenCoverageScale*trailRibbonAdjust(activeTrail)*(_HALFHEIGHT+HALFWIDTH_FALLOFF));
    vecAddTo(fromHi, from);

    vecScalarMultiply(toHi, vcross, trailScreenCoverageScale*trailRibbonAdjust(activeTrail)*_HALFHEIGHT);
    vecAddTo(toHi, to);

    vecScalarMultiply(subAmount, vcross, trailScreenCoverageScale*trailRibbonAdjust(activeTrail)*(_HALFHEIGHT+HALFWIDTH_FALLOFF));
    vecSubFrom(from, subAmount);
    vecScalarMultiply(subAmount, vcross, trailScreenCoverageScale*trailRibbonAdjust(activeTrail)*_HALFHEIGHT);
    vecSubFrom(to, subAmount);

    //render

    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    alpha = usingShader ? (ubyte)(127.0f * meshFadeAlpha) : 127;

    glBegin(GL_TRIANGLE_STRIP);
    if (i == 0)
    {
        cb = c;
    }
    else
    {
        VECCOPY(&from, &lastTo);
        VECCOPY(&fromHi, &lastToHi);
    }
    COLx(cb,alpha);
    VERT(from);
    COLx(cb,alpha);
    VERT(fromHi);
    COLx(c,alpha);
    VERT(to);
    COLx(c,alpha);
    VERT(toHi);
    glEnd();

    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    VECCOPY(&lastTo, &to);
    VECCOPY(&lastToHi, &toHi);

    cb = c;
}

/*-----------------------------------------------------------------------------
    Name        : trailSurpriseColorAdjust
    Description : adjusts a given colour with some random flair
    Inputs      : index - trail segment number
                  max - max number of trail segments
                  c - colour of the segment
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
color trailSurpriseColorAdjust(sdword index, sdword max, color c)
{
    ubyte r, g, b;
    ubyte randval;

    if ((COLOR_ADJUST == 0) || (COLORADJUST_FREQ == 0) || (index > (max - 5)))
    {
        return c;
    }

    if ((sdword)(ranRandom(RANDOM_TRAILS_2) % 100) > COLORADJUST_FREQ)
    {
        return c;
    }

    r = colRed(c);
    g = colGreen(c);
    b = colBlue(c);

    randval = (ubyte)(ranRandom(RANDOM_TRAILS_3) % COLOR_ADJUST);

    if (ranRandom(RANDOM_TRAILS_4) & 1)
    {
        r = MIN2(r+randval,255);
        g = MIN2(g+randval,255);
        b = MIN2(b+randval,255);
    }
    else
    {
        r = MAX2(r-randval,0);
        g = MAX2(g-randval,0);
        b = MAX2(b-randval,0);
    }
	
    return colRGB(r,g,b);
}

/*-----------------------------------------------------------------------------
    Name        : trailLineSequence
    Description : render an engine trail that is merely a line
    Inputs      : LOD - the LOD number
                  n - number of segments
                  vectors - segment position array
                  segmentArray - array of segment colours
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailLineSequence(sdword LOD, sdword n, vector vectors[], color* segmentArray)
{
    sdword i;
    color c;
    ubyte alpha;

    alpha = 163;
    glEnable(GL_BLEND);

    if (usingShader)
    {
        alpha = (ubyte)((real32)alpha * meshFadeAlpha);
    }

    if (LOD == 3)
    {
        glLineWidth(MIN2(4.0f, MAX2(2.0f, 2.0f * trailScreenCoverageScale)));
    }
    glBegin(GL_LINE_STRIP);
    for (i = 0; i < n; i++)
    {
        c = trailSurpriseColorAdjust(i, n, segmentArray[i+(i==0)]);
        glColor4ub(colRed(c), colGreen(c), colBlue(c), alpha);
        glVertex3fv((GLfloat*)(vectors + i));
    }
    glEnd();
    if (LOD == 3)
    {
        glLineWidth(1.0f);
    }

    glDisable(GL_BLEND);
}

static real32 trailModernSmoothStep(real32 low, real32 high, real32 value)
{
    real32 t;
    if (value <= low) return 0.0f;
    if (value >= high) return 1.0f;
    t = (value - low) / (high - low);
    return t * t * (3.0f - 2.0f * t);
}

static color trailModernBrighten(color c, real32 amount, ubyte alpha)
{
    real32 inverse = 1.0f - amount;
    return colRGBA(
        (ubyte)MIN2(255, (sdword)((real32)colRed(c) * inverse + 255.0f * amount)),
        (ubyte)MIN2(255, (sdword)((real32)colGreen(c) * inverse + 255.0f * amount)),
        (ubyte)MIN2(255, (sdword)((real32)colBlue(c) * inverse + 255.0f * amount)),
        alpha);
}

/* A resolution-independent feathered plasma ribbon.  Several very low-alpha
 * shells approximate a continuous bloom profile without relying on the
 * original tiny trail texture.  The deliberately restrained hot core avoids
 * hard white/red bands and additive saturation.  All animation is continuous
 * and deterministic. */
static void trailModernLayeredSequence(sdword n, vector vectors[],
                                       color *segmentArray)
{
    static const real32 widthScale[9] = {
        1.08f, 0.94f, 0.81f, 0.69f, 0.58f,
        0.48f, 0.39f, 0.31f, 0.235f
    };
    static const real32 whiteMix[9] = {
        0.00f, 0.01f, 0.025f, 0.05f, 0.085f,
        0.13f, 0.19f, 0.27f, 0.36f
    };
    static const real32 layerAlpha[9] = {
        7.0f, 8.0f, 9.0f, 11.0f, 14.0f,
        18.0f, 23.0f, 30.0f, 40.0f
    };
    vector right[40];
    vector center[40];
    vector tangent, eye, cross;
    vector previousRight = {1.0f, 0.0f, 0.0f};
    real32 baseWidth;
    sdword i, layer;

    if (n < 2) return;
    baseWidth = MAX2(0.12f,
        MAX2(trailHalfHeight(activeTrail),
             trailHalfWidth(activeTrail) * 0.30f) *
        trailRibbonAdjust(activeTrail) * trailScreenCoverageScale * 0.72f);

    for (i = 0; i < n; ++i)
    {
        sdword before = i > 0 ? i - 1 : i;
        sdword after = i + 1 < n ? i + 1 : i;
        real32 magnitudeSquared;
        real32 inverseMagnitude;
        real32 age = (real32)i / (real32)(n - 1);
        real32 wave = (real32)sin((double)(
            universe.totaltimeelapsed * 4.2f +
            (real32)i * 0.67f + (real32)activeTrail->trailNum * 1.31f));

        vecSub(tangent, vectors[after], vectors[before]);
        vecSub(eye, mrCamera->eyeposition, vectors[i]);
        vecCrossProduct(cross, tangent, eye);
        magnitudeSquared = vecMagnitudeSquared(cross);
        if (magnitudeSquared > 0.000001f)
        {
            inverseMagnitude = 1.0f / fsqrt(magnitudeSquared);
            vecScalarMultiply(right[i], cross, inverseMagnitude);
            VECCOPY(&previousRight, &right[i]);
        }
        else
        {
            VECCOPY(&right[i], &previousRight);
        }
        VECCOPY(&center[i], &vectors[i]);
        /* Keep the hot core coherent; this small displacement affects the
           plume as a whole and fades toward the tail. */
        center[i].x += right[i].x * baseWidth * 0.035f * wave * (1.0f - age);
        center[i].y += right[i].y * baseWidth * 0.035f * wave * (1.0f - age);
        center[i].z += right[i].z * baseWidth * 0.035f * wave * (1.0f - age);
    }

    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    for (layer = 0; layer < 9; ++layer)
    {
        glBegin(GL_TRIANGLE_STRIP);
        for (i = 0; i < n; ++i)
        {
            real32 age = (real32)i / (real32)(n - 1);
            real32 tailFade = 1.0f - trailModernSmoothStep(0.48f, 1.0f, age);
            real32 taper = 1.0f - 0.80f * age;
            real32 pulse = 0.975f + 0.025f * (real32)sin((double)(
                universe.totaltimeelapsed * 8.0f +
                (real32)i * 0.42f + (real32)activeTrail->trailNum));
            real32 width = baseWidth * widthScale[layer] * taper;
            real32 alphaValue = layerAlpha[layer] * tailFade * pulse;
            vector low, high, offset;
            color source = segmentArray[i + (i == 0)];
            color shaded;

            if (usingShader) alphaValue *= meshFadeAlpha;
            vecScalarMultiply(offset, right[i], width);
            vecSub(low, center[i], offset);
            vecAdd(high, center[i], offset);
            shaded = trailModernBrighten(source, whiteMix[layer],
                (ubyte)MIN2(255, MAX2(0, (sdword)alphaValue)));
            glColor4ub(colRed(shaded), colGreen(shaded), colBlue(shaded),
                       colAlpha(shaded));
            glVertex3fv((GLfloat *)&low);
            glVertex3fv((GLfloat *)&high);
        }
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

/*-----------------------------------------------------------------------------
    Name        : trailLine
    Description : renders an engine trail, accounting for LOD [0..2]
    Inputs      : ...
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailLine(sdword LOD, sdword i, vector vectors[], color c,
               vector horiz[], vector vert[], bool32 wides[])
{
    switch (LOD)
    {
    case 0:
    case 1:
        trailLineBillboard(LOD, i, vectors + i, vectors + i + 1, c);
        if (activeTrail->style == 1)
        {
            trailLinePyramid(LOD, i, vectors + i, vectors + i + 1, c, horiz, vert, wides[i]);
        }
        break;
    case 2:
        trailLineBillboard(LOD, i, vectors + i, vectors + i + 1, c);
        trailLineFuzzySheath(LOD, i, vectors + i, vectors + i + 1, c, horiz);
        break;
    }
}

#undef VERT
#undef COL
#undef COLw

/*-----------------------------------------------------------------------------
    Name        : mistrailDrawLine
    Description :
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void mistrailDrawLine(vector* a, vector* b, sdword segment, sdword nSegments, color c0, color c1)
{
    sdword alpha;

    alpha = (sdword)(255.0f * (1.0f - ((real32)segment / (real32)nSegments)));

    glBegin(GL_LINES);
    glColor4ub(colRed(c0), colGreen(c0), colBlue(c0), (GLubyte)alpha);
    glVertex3fv((GLfloat*)a);
    glColor4ub(colRed(c1), colGreen(c1), colBlue(c1), (GLubyte)alpha);
    glVertex3fv((GLfloat*)b);
    glEnd();
}

/*-----------------------------------------------------------------------------
    Name        : mistrailDraw
    Description : draw missile trails
    Inputs      :
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void mistrailDraw(vector* current, missiletrail* trail, sdword LOD, sdword teamIndex)
{
    sdword index, count;
    vector* lastVector;
    trailstatic* trailStatic = trail->staticInfo;
    color prevColor = colBlack;
    color* segmentArray;

    if (!enableTrails)
    {
        return;
    }

    dbgAssertOrIgnore(teamIndex >= 0 && teamIndex < MAX_MULTIPLAYER_PLAYERS);

    rndLightingEnable(FALSE);
    rndTextureEnable(FALSE);
    glShadeModel(GL_SMOOTH);

    index = trail->iHead <= 0 ? trailStatic->nSegments - 1 : trail->iHead - 1;
    lastVector = current;
    segmentArray = trail->staticInfo->segmentColor[teamIndex];

    rndAdditiveBlends(TRUE);
    glEnable(GL_BLEND);

    for (count = 1; count < trail->nLength; count++)
    {
        dbgAssertOrIgnore(index >= 0 && index < trailStatic->nSegments);

        if (count == 1)
        {
            prevColor = segmentArray[count];
        }
        mistrailDrawLine(lastVector, &trail->segments[index].position,
                         count, trail->nLength, prevColor, segmentArray[count]);
        prevColor = segmentArray[count];

        lastVector = &trail->segments[index].position;
        index = index <= 1 ? trailStatic->nSegments - 1: index - 1;
        if (index == trail->iHead)
        {
            break;
        }
    }

    rndLightingEnable(TRUE);
    glDisable(GL_BLEND);
    rndAdditiveBlends(FALSE);
}

/*-----------------------------------------------------------------------------
    Name        : trailDraw
    Description : Draw ship trails
    Inputs      : trail - pointer to ship trail
                  LOD - level-of-detail to draw it at (typically the level of
                    the ship it is attached to).
                  current - current location of ship
    Outputs     :
    Return      : void
----------------------------------------------------------------------------*/
void trailDraw(vector *current, shiptrail *trail, sdword LOD, sdword teamIndex)
{
    sdword index;
//    sdword count;
    trailsegment lastSegment, currentSegment;
    color *segmentArray;
    real32 mag;
    bool32 dontdrawtrail = FALSE;

    Ship* ship = (Ship*)trail->vship;

#ifdef HW_MODERN_DISABLE_NLIPS
    NLipsScaleFactor = 1.0f;
#else
    NLipsScaleFactor = ship->magnitudeSquared;
#endif
    if (isnan((double)NLipsScaleFactor))
    {
        NLipsScaleFactor = 1.0f;
    }
    trailScreenCoverageScale = trailModernCoverageScale(ship, trail);

    if (!enableTrails || !bTrailRender)
    {
        return;
    }

    if (!IS_MOVING_LINEARLY(ship->posinfo.isMoving) || (ship->flags & (SOF_DontDrawTrails|SOF_Clamped|SOF_Disabled)))  // clamped objs dont draw trails either
    {
        dontdrawtrail = TRUE;
    }
    else if ((ship->flags & SOF_NISShip) == 0)
    {
        // for non NIS ships, if they don't have a command or its NULL, don't draw engine trails
        if ((ship->isDodging) || (universe.totaltimeelapsed < ship->DodgeTime))
        {
            CommandToDo *shipcommand = ship->command;
            if (shipcommand == NULL)
            {
                dontdrawtrail = TRUE;
            }
            else if ((shipcommand->ordertype.attributes & (COMMAND_MASK_ATTACKING_AND_MOVING|COMMAND_MASK_HOLDING_PATTERN|COMMAND_MASK_PROTECTING)) == 0)
            {
                switch (shipcommand->ordertype.order)
                {
                    case COMMAND_NULL:
                    case COMMAND_HALT:
                        dontdrawtrail = TRUE;
                        break;
                }
            }
        }
    }

    if (trail->style > 2)
    {
        if (dontdrawtrail)
        {
            switch (trail->state)
            {
            case TRAIL_CONTRACTING:
                break;

            case TRAIL_EXPANDED:
            case TRAIL_EXPANDING:
                trail->state = TRAIL_CONTRACTING;
                trail->counter = (ubyte)TRAIL_EXPANSION_TICKS - trail->counter;
                break;

            case TRAIL_CONTRACTED:
                return;
            }
        }
        else
        {

            switch (trail->state)
            {
            case TRAIL_CONTRACTED:
                trail->state = TRAIL_EXPANDING;
                trail->counter = (ubyte)TRAIL_EXPANSION_TICKS;
                break;

            case TRAIL_EXPANDED:
            case TRAIL_EXPANDING:
                break;

            case TRAIL_CONTRACTING:
                trail->state = TRAIL_EXPANDING;
                trail->counter = (ubyte)TRAIL_EXPANSION_TICKS - trail->counter;
                break;
            }
        }

        trailDrawCapitalGlow(trail, LOD);
        return;
    }

    if (dontdrawtrail)
    {
        trailZeroLength(trail);
        return;
    }

    trailGetNozzleOffset(lastSegment.position, trail);
    trailGetCoordsys(lastSegment.rotation, trail);
    trailGetTranslation(lastSegment.translation, trail);

    trail->segments[trail->iHead].wide = FALSE;
    {
        /* The nozzle bloom is a physical engine feature, not a low-LOD
           embellishment.  Keep it visible at every visual LOD so zooming out
           cannot collapse a moving ship to a bare one-pixel trail. */
        real32 rad = ship->staticinfo->staticheader.staticCollInfo.collspheresize;
        segmentArray = trail->staticInfo->segmentColor[teamIndex];
        _afterburning = FALSE;
        trailDrawBillboardedSquareThingz(trail, &lastSegment, rad, ship, segmentArray[2]);

        if (lastSegment.wide)
        {
            trail->segments[trail->iHead].wide = TRUE;
        }
    }

    lastSegment.position[2] -= 0.3f*XADJ;
    for (index = 0; index < 3; index++)
    {
        lastSegment.position[index] *= NLipsScaleFactor;
    }
    trailCopySegment(&currentSegment, &lastSegment);

    mag = fsqrt(vecMagnitudeSquared(ship->posinfo.velocity));
    if (mag < 0.001f)   //decision
    {
        return;
    }
    dbgAssertOrIgnore(teamIndex >= 0 && teamIndex < MAX_MULTIPLAYER_PLAYERS);

#if TRAIL_GATHER_STATS
    trailsRendered++;
#endif

    segmentArray = trail->staticInfo->segmentColor[teamIndex];

    //actually display the trail now
    if (trail->nLength > 3)
    {
        vector segments[40];
        sdword i, n;
/*
        real32 size = SIZE_MAJOR * velratio * NLipsScaleFactor;
        real32 sizeinc = SIZE_INC * velratio * NLipsScaleFactor;
        real32 sizeinc_inc = SIZEINC_INC * velratio * NLipsScaleFactor;
        real32 sizemax = SIZE_MAXIMUM * NLipsScaleFactor;
*/

        n = MIN2(38, trail->nLength);

        trailPositions(n, segments, NULL, NULL, trail);
        VECCOPY(&segments[n], &segments[n-1]);

        trailInplaceShipToWorld(currentSegment.position, currentSegment.rotation, currentSegment.translation);
        VECCOPY(&segments[0], currentSegment.position);

        //now draw
        if (bTrailDraw)
        {
            rndLightingEnable(FALSE);
            rndTextureEnable(FALSE);
            rndAdditiveBlends(TRUE);

            //initialize convenient globals
            trailLineInit(trail, n);

            //NaN checking (error correction)
            for (i = 0; i < n; i++)
            {
                if (isnan((double)segments[i].x))
                {
                    n = (i == 0) ? 0 : i - 1;
                    break;
                }
            }

            /* Replace the resolution-dependent legacy line/pyramid split
               with one coherent high-definition plasma plume at every LOD. */
            trailModernLayeredSequence(n, segments, segmentArray);

#ifdef HW_ENABLE_D3D12_BACKEND
            if (n >= 2)
            {
                HWModernDynamicLightEmitter emitter;
                color lightColor = segmentArray[2];
                sdword illuminatedLength = MIN2(n - 1, 8);
                real32 plumeRadius = MAX2(0.65f,
                    trailHalfHeight(trail) * trailRibbonAdjust(trail) *
                    trailScreenCoverageScale * 0.72f);

                memset(&emitter, 0, sizeof(emitter));
                emitter.source = HW_MODERN_LIGHT_ENGINE;
                emitter.shape = HW_MODERN_LIGHT_LINE;
                VECCOPY(emitter.position, &segments[0]);
                VECCOPY(emitter.endPosition, &segments[illuminatedLength]);
                emitter.color[0] = colUbyteToReal(colRed(lightColor));
                emitter.color[1] = colUbyteToReal(colGreen(lightColor));
                emitter.color[2] = colUbyteToReal(colBlue(lightColor));
                emitter.radius = plumeRadius;
                emitter.intensity = plumeRadius * 1.15f;
                hwModernGraphicsSubmitDynamicLight(&emitter);
            }
#endif

            rndLightingEnable(TRUE);
            glEnable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            rndAdditiveBlends(FALSE);
        }

        //compute the acceleration with this
        if (trail->ranCounter == 0)
        {
            trail->prevvelsquared = trail->lastvelsquared;
            trail->lastvelsquared = vecMagnitudeSquared(ship->posinfo.velocity);
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailMakeWobbly
    Description : enter/exit a trail/glow's "wobbly" state
    Inputs      : ship - the ship whose trail(s)/glow(s) are to "wobble"
                  state - TRUE or FALSE
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailMakeWobbly(void* vship, bool32 state)
{
    sdword i;
    Ship* ship = (Ship*)vship;

    dbgAssertOrIgnore(ship != NULL);

    for (i = 0; i < MAX_NUM_TRAILS; i++)
    {
        if (ship->trail[i] != NULL)
        {
            ship->trail[i]->wobbly = state;
        }
    }
}


/*=============================================================================
    Trail-static info parsing functions:
=============================================================================*/
static sdword tpNSegments;
static sdword tpGranularity;
static color *tpColorArray[MAX_MULTIPLAYER_PLAYERS];
#define TP_InvalidColor     colRGBA(255, 0, 255, 13)

static void trailSegmentsRead(char *directory,char *field,void *dataToFillIn)
{                                                           //read number of trail segments
    sdword nScanned, index, teamIndex;
    nScanned = sscanf(field, "%d", &tpNSegments);
    dbgAssertOrIgnore(nScanned == 1);
    dbgAssertOrIgnore(tpNSegments > 1 && tpNSegments < 1000);
    
    for (teamIndex = 0; teamIndex < MAX_MULTIPLAYER_PLAYERS; teamIndex++)
    {
        tpColorArray[teamIndex] = memAlloc(sizeof(color) * tpNSegments, "Temp. Color Array", 0);

        for (index = 0; index < tpNSegments; index++)
        {                                                   //clear all color entries
            tpColorArray[teamIndex][index] = TP_InvalidColor;
        }
    }
}
static void trailGranularityRead(char *directory,char *field,void *dataToFillIn)
{                                                           //read trail granularity
    sdword nScanned;
    nScanned = sscanf(field, "%d", &tpGranularity);
    dbgAssertOrIgnore(nScanned == 1);
}
static void trailColorRead(char *directory,char *field,void *dataToFillIn)
{
    sdword nScanned, index, teamIndex;
    udword red, green, blue;

    nScanned = sscanf(field, "%d,%d,%d,%d,%d", &teamIndex, &index, &red, &green, &blue);
    dbgAssertOrIgnore(nScanned == 5);
    dbgAssertOrIgnore(index >= 0 && index < tpNSegments);
    dbgAssertOrIgnore(tpColorArray[teamIndex] != NULL);
    tpColorArray[teamIndex][index] = colRGB(red, green, blue);
}

/*-----------------------------------------------------------------------------
    Name        : trailStaticInfoParse
    Description : Parse a ship definition file and create a trailstatic structure
    Inputs      : fileName - name of .shp file to load from
    Outputs     : Potentially allocates new memory and initializes structure
    Return      : pointer to new or default trailstatic structure or NULL if
                    no info found.
----------------------------------------------------------------------------*/
trailstatic *trailStaticInfoParse(char *directory, char *fileName)
{
    sdword index, j, lastIndex, teamIndex;
    udword red, green, blue;
    trailstatic *newStatic;

    tpGranularity = tpNSegments = 0;                        //set initial values
    for (teamIndex = 0; teamIndex < MAX_MULTIPLAYER_PLAYERS; teamIndex++)
    {
        tpColorArray[teamIndex] = NULL;
    }
    scriptSet(directory, fileName, trailStaticScriptTable); //read in the script
    if (tpColorArray[0] == NULL)                            //if nothing was loaded
    {
        return(NULL);
    }
    dbgAssertOrIgnore(tpGranularity > 0);
    dbgAssertOrIgnore(tpNSegments > 1 && tpGranularity < 1000);

    newStatic = memAlloc(trailStaticSize(tpNSegments), "Trail Static Info", NonVolatile);
    newStatic->granularity = tpGranularity;                 //init basic info
    newStatic->nSegments = tpNSegments;

    //now build the interpolated color table
    for (teamIndex = 0; teamIndex < MAX_MULTIPLAYER_PLAYERS; teamIndex++)
    {
        newStatic->segmentColor[teamIndex] = (color *)(((ubyte *)newStatic) + sizeof(trailstatic) + sizeof(color) * tpNSegments * teamIndex);
        for (index = 0; index < tpNSegments; index++)
        {                                                       //ensure first entry valid
            if (tpColorArray[teamIndex][index] != TP_InvalidColor)
            {
                tpColorArray[teamIndex][0] = tpColorArray[teamIndex][index];
                break;
            }
        }
#if TRAIL_ERROR_CHECKING
        if (tpColorArray[teamIndex][0] == TP_InvalidColor)
        {                                                       //if first entry still not set
            dbgFatalf(DBG_Loc, "No color specified for the trail of %s", fileName);
        }
#endif
        for (index = tpNSegments - 1; index > 0; index--)
        {
            if (tpColorArray[teamIndex][index] != TP_InvalidColor)
            {
                tpColorArray[teamIndex][tpNSegments - 1] = tpColorArray[teamIndex][index];
                break;
            }
        }
#if TRAIL_ERROR_CHECKING
        if (tpColorArray[teamIndex][tpNSegments - 1] == TP_InvalidColor)
        {                                                       //if first entry still not set
            dbgFatalf(DBG_Loc, "No color specified for the trail of %s", fileName);
        }
#endif

        for (index = lastIndex = 0; index < tpNSegments; index++)
        {                                                       //process entire list
            newStatic->segmentColor[teamIndex][index] = tpColorArray[teamIndex][index];
            if (tpColorArray[teamIndex][index] != TP_InvalidColor)
            {                                                   //if this color is set
//                tpColorArray[teamIndex][index] &= TP_KeyColor;
                for (j = lastIndex + 1; j < index; j++)
                {                                               //between last valid color to this one
                    red = (colRed(tpColorArray[teamIndex][lastIndex]) * (index - j) +
                        colRed(tpColorArray[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    green = (colGreen(tpColorArray[teamIndex][lastIndex]) * (index - j) +
                        colGreen(tpColorArray[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    blue = (colBlue(tpColorArray[teamIndex][lastIndex]) * (index - j) +
                        colBlue(tpColorArray[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    newStatic->segmentColor[teamIndex][j] = colRGB(red, green, blue);//interpolate the colors
                }
                newStatic->segmentColor[teamIndex][index] &= TP_KeyColor;
                lastIndex = index;
            }
        }
        memFree(tpColorArray[teamIndex]);                                  //free old color array
    }
    return(newStatic);
}

/*-----------------------------------------------------------------------------
    Name        : trailRecolorize
    Description : Recolorize the specified trail to reflect a change in team colors.
    Inputs      : trailStatic - static info for trail to recolorize.
    Outputs     : Trail colors will be re-interpolated to reflect new color changes.
    Return      : void
----------------------------------------------------------------------------*/
void trailRecolorize(trailstatic *trailStatic)
{
    sdword teamIndex, index, lastIndex, cpIndex, j;
    udword red, green, blue;

    for (teamIndex = 0; teamIndex < MAX_MULTIPLAYER_PLAYERS; teamIndex++)
    {                                                       //for each team
        for (index = lastIndex = cpIndex = 0; index < trailStatic->nSegments; index++)
        {                                                   //for each trail segment
            if ((trailStatic->segmentColor[teamIndex][index] & (~TP_KeyColor)) == 0)
            {                                               //if this color is set
                trailStatic->segmentColor[teamIndex][index] = //set new control point color
                         teColorSchemes[teamIndex].trailColors[cpIndex] & TP_KeyColor;
                cpIndex++;                                  //update control point index
                dbgAssertOrIgnore(cpIndex <= TE_NumberTrailColors);
                
                for (j = lastIndex + 1; j < index; j++)
                {                                           //between last valid color to this one
                    red = (colRed(trailStatic->segmentColor[teamIndex][lastIndex]) * (index - j) +
                        colRed(trailStatic->segmentColor[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    green = (colGreen(trailStatic->segmentColor[teamIndex][lastIndex]) * (index - j) +
                        colGreen(trailStatic->segmentColor[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    blue = (colBlue(trailStatic->segmentColor[teamIndex][lastIndex]) * (index - j) +
                        colBlue(trailStatic->segmentColor[teamIndex][index]) * (j - lastIndex)) / (index - lastIndex);
                    trailStatic->segmentColor[teamIndex][j] = colRGB(red, green, blue);//interpolate the colors
                }
				
                lastIndex = index;
            }
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailsRecolorize
    Description : Recolorize all ship trails to reflect a change in team colors.
    Inputs      : void
    Outputs     : ALL ships will have their trail colors re-interpolated to
                    reflect new color changes.
    Return      : void
----------------------------------------------------------------------------*/
void trailsRecolorize(void)
{
    sdword shiprace, firstshiptype, lastshiptype, shiptype, i;
    ShipStaticInfo *shipstaticinfo;

    for (shiprace=0;shiprace<NUM_RACES;shiprace++)
    {
        firstshiptype = FirstShipTypeOfRace[shiprace];
        lastshiptype = LastShipTypeOfRace[shiprace];
        for (shiptype=firstshiptype;shiptype<=lastshiptype;shiptype++)
        {
            shipstaticinfo = GetShipStaticInfo(shiptype,shiprace);
            for (i = 0; i < MAX_NUM_TRAILS; i++)
            {
                if (shipstaticinfo->trailStatic[i] != NULL)
                {
                    trailRecolorize(shipstaticinfo->trailStatic[i]);
                }
            }
            //InitStatShipInfoPost(shipstaticinfo);
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : mistrailsRecolorize
    Description : recolorize all missile trails to reflect a change in team colors
    Inputs      :
    Outputs     : ALL missiles will have their trail colors re-interpolated to
                  reflect new color changes
    Return      :
----------------------------------------------------------------------------*/
void mistrailsRecolorize(void)
{
    sdword shiprace;

    for (shiprace = 0; shiprace < NUM_RACES; shiprace++)
    {
        if (missileStaticInfos[shiprace].trailStatic != NULL)
        {
            trailRecolorize(missileStaticInfos[shiprace].trailStatic);
        }
    }
}

/*-----------------------------------------------------------------------------
    Name        : trailZeroLength
    Description : Make a trail zero segments long
    Inputs      : trail - trail who's length we are to zero
    Outputs     :
    Return      : void
----------------------------------------------------------------------------*/
void trailZeroLength(shiptrail* trail)
{
    dbgAssertOrIgnore(trail != NULL);
    trail->iHead = trail->iTail = trail->nLength = 0;
    trail->grainCounter = 0;
}

/*-----------------------------------------------------------------------------
    Name        : trailMove
    Description : Moves the history of positions by a given amount.
    Inputs      : trail - trail to move
                  delta - amount to move by
    Outputs     :
    Return      :
----------------------------------------------------------------------------*/
void trailMove(shiptrail* trail, vector *delta)
{
    sdword index;
    trailsegment *segment;

    dbgAssertOrIgnore(trail != NULL);
    segment = trail->segments;
    for (index = trail->staticInfo->nSegments; index >= 0; index--, segment++)
    {
        segment->position[0] += delta->x;
        segment->position[1] += delta->y;
        segment->position[2] += delta->z;
    }
}
