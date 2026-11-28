#pragma once

#include "gamesymbols.h"

#include <edict.h>
#include <const.h>
#include <com_model.h>
#include <pmtrace.h>
#include <pm_defs.h>
#include <pm_movevars.h>

#include <entity_state.h>

#define MOVE_NORMAL     0
#define MOVE_NOMONSTERS 1
#define MOVE_MISSILE    2

#define NL_PRESENT      0
#define NL_NEEDS_LOADED 1
#define NL_UNREFERENCED 2
#define NL_CLIENT       3

#define SURF_PLANEBACK      2
#define SURF_DRAWSKY        4
#define SURF_DRAWSPRITE     8
#define SURF_DRAWTURB       0x10
#define SURF_DRAWTILED      0x20
#define SURF_DRAWBACKGROUND 0x40
#define SURF_UNDERWATER     0x80
#define SURF_DONTWARP       0x100

#define FMODEL_ROCKET        0x1
#define FMODEL_GRENADE       0x2
#define FMODEL_GIB           0x4
#define FMODEL_ROTATE        0x8
#define FMODEL_TRACER        0x10
#define FMODEL_ZOMGIB        0x20
#define FMODEL_TRACER2       0x40
#define FMODEL_TRACER3       0x80
#define FMODEL_DYNAMIC_LIGHT 0x100
#define FMODEL_TRACE_HITBOX  0x200

typedef struct
{
    vec3_t       boxmins, boxmaxs; // enclose the test object along entire move
    const float *mins, *maxs;      // size of the moving object
    vec3_t       mins2, maxs2;     // size when clipping against mosnters
    float *      start, *end;
    trace_t      trace;
    short        type;
    short        ignoretrans;
    edict_t*     passedict;
    qboolean     monsterClipBrush;
} moveclip_t;

typedef struct areanode_s
{
    int                axis; // -1 = leaf node
    float              dist;
    struct areanode_s* children[2];
    link_t             trigger_edicts;
    link_t             solid_edicts;
} areanode_t;

#include "bridge/fallguys_bridge.h"

extern fallguys_bridge::ServerPhysicsSession g_ServerPhysicsSession;

int         EngineGetBuildNumber(void);
double      EngineGetHostFrametime(void);
int         EngineGetMaxPrecacheModel(void);
model_t*    EngineGetPrecachedModelByIndex(int i);
msurface_t* EngineGetSurfaceByIndex(model_t* mod, int index);
int         EngineGetSurfaceIndex(model_t* mod, msurface_t* psurf);
model_t*    EngineFindWorldModelBySubModel(model_t* psubmodel);
void        EnginePushEntity(edict_t* ent, const vec3_t* push);
edict_t*    SV_TestEntityPositionEx(edict_t* ent);
hull_t*     SV_HullForBspNew(edict_t* ent, const vec3_t mins, const vec3_t maxs, vec3_t& offset);

extern cvar_t*       sv_gravity;
extern cvar_t*       mp_footsteps;
extern playermove_t* pmove;