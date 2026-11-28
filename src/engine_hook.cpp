#include <extdll.h>

#include <dllapi.h>
#include <meta_api.h>

#include <cl_entity.h>
#include <entity_state.h>
#include <com_model.h>
#include <pm_movevars.h>

#include "enginedef.h"
#include "serverdef.h"
#include "fallguys.h"
#include "physics.h"

fallguys_bridge::ServerPhysicsSession g_ServerPhysicsSession;

const Vector g_vecZero = {0, 0, 0};

cvar_t* sv_gravity   = NULL;
cvar_t* mp_footsteps = NULL;

edict_t* r_worldentity = NULL;
model_t* r_worldmodel  = NULL;

playermove_t* pmove = NULL;

int EngineGetBuildNumber(void)
{
    return g_ServerPhysicsSession.host ? g_ServerPhysicsSession.host->GetBuildNumber() : 0;
}

double EngineGetHostFrametime(void)
{
    return g_ServerPhysicsSession.host ? g_ServerPhysicsSession.host->GetHostFrametime() : 0.0;
}

int EngineGetMaxPrecacheModel(void)
{
    return g_ServerPhysicsSession.host ? g_ServerPhysicsSession.host->GetMaxPrecachedModels() : 0;
}

model_t* EngineGetPrecachedModelByIndex(int i)
{
    return g_ServerPhysicsSession.host ? g_ServerPhysicsSession.host->GetPrecachedModelByIndex(i) : NULL;
}

msurface_t* EngineGetSurfaceByIndex(model_t* mod, int index)
{
    auto base = mod->surfaces;

    return base + index;
}

int EngineGetSurfaceIndex(model_t* mod, msurface_t* psurf)
{
    auto base = mod->surfaces;

    return static_cast<int>(psurf - base);
}

model_t* EngineFindWorldModelBySubModel(model_t* psubmodel)
{
    for (int i = 0; i < EngineGetMaxPrecacheModel(); ++i)
    {
        auto mod = EngineGetPrecachedModelByIndex(i);

        if (mod && mod->type == mod_brush && mod->name[0] && mod->name[0] != '*')
        {
            if (mod->needload == NL_PRESENT || mod->needload == NL_CLIENT)
            {
                if (mod->vertexes == psubmodel->vertexes)
                    return mod;
            }
        }
    }
    return nullptr;
}

hull_t* SV_HullForBspNew(edict_t* ent, const vec3_t mins, const vec3_t maxs, vec3_t& offset)
{
    model_t* model;
    hull_t*  hull;
    vec3_t   size;

    model = EngineGetPrecachedModelByIndex(ent->v.modelindex);

    if (!model || model->type != mod_brush)
        return NULL;

    size = (maxs - mins);
    if (size[0] <= 8)
    {
        hull   = &model->hulls[0];
        offset = hull->clip_mins;
    }
    else
    {
        if (size[0] <= 36)
        {
            if (size[2] <= 36)
            {
                hull   = &model->hulls[3];
                offset = hull->clip_mins - mins;
            }
            else
            {
                hull   = &model->hulls[1];
                offset = hull->clip_mins - mins;
            }
        }
        else
        {
            hull   = &model->hulls[2];
            offset = hull->clip_mins - mins;
        }
    }

    // calculate an offset value to center the origin
    offset = offset + ent->v.origin;
    return hull;
}

void EnginePushEntity(edict_t* ent, const vec3_t* push)
{
    if (!g_ServerPhysicsSession.host || !ent || !push)
        return;

    const float pushVec[3] = {push->x, push->y, push->z};
    g_ServerPhysicsSession.host->PushEntity(ent, pushVec, nullptr);
}

edict_t* SV_TestEntityPositionEx(edict_t* ent)
{
    return g_ServerPhysicsSession.host ? g_ServerPhysicsSession.host->TestEntityPositionEx(ent) : NULL;
}

namespace
{

using goldsrcext::GoldSrcTrace;
using goldsrcext::HookHandle;
using goldsrcext::IHookChain;
using goldsrcext::kInvalidHookHandle;

HookHandle g_hPhysicsHook       = kInvalidHookHandle;
HookHandle g_hPushEntityHook    = kInvalidHookHandle;
HookHandle g_hPushMoveHook      = kInvalidHookHandle;
HookHandle g_hPushRotateHook    = kInvalidHookHandle;
HookHandle g_hWriteMovevarsHook = kInvalidHookHandle;

void OnSV_Physics(IHookChain<void, double>* chain, void* /*userData*/, double frametime) noexcept
{
    chain->CallNext(frametime);

    gPhysicsManager.EntityEndFrame();
}

void OnSV_PushEntity(IHookChain<void, edict_s*, const float*, GoldSrcTrace*>* chain, void* /*userData*/, edict_s* ent, const float* push, GoldSrcTrace* outTrace) noexcept
{
    const vec3_t pushVec(
        push ? push[0] : 0.0f,
        push ? push[1] : 0.0f,
        push ? push[2] : 0.0f);

    g_bIsPushEntity = true;
    g_PushEntity    = ent;

    // Here we collect players in NewTouch and add them into pending queue.
    chain->CallNext(ent, push, outTrace);

    // Do the push job for all players
    for (int i = 0; i < g_NumPendingEntities; ++i)
    {
        auto pPendingEntity = g_PendingEntities[i];

        g_PushEntity = pPendingEntity;

        chain->CallOriginal(pPendingEntity, push, nullptr);
    }

    // Add me into pending queue at final stage to commit the SuperPusher touch callback
    if (g_NumPendingEntities < (int)_ARRAYSIZE(g_PendingEntities))
    {
        g_PendingEntities[g_NumPendingEntities] = ent;
        g_NumPendingEntities++;
    }

    // Commit the transaction
    if ((g_bIsPushMove || g_bIsPushRotate) &&
        gPhysicsManager.IsEntitySuperPusher(g_PusherEntity))
    {
        for (int i = 0; i < g_NumPendingEntities; ++i)
        {
            auto pPendingEntity = g_PendingEntities[i];

            if (IsEntityPushee(pPendingEntity) && pPendingEntity->v.groundentity != g_PusherEntity)
            {
                if (g_bIsFallGuysSeason1)
                {
                    if (g_PusherEntity->v.armorvalue > 0)
                    {
                        vec3_t dir = pushVec;
                        dir        = dir.Normalize();

                        if (Legacy_IsEntitySuperPusherFlexible(g_PusherEntity))
                        {
                            pPendingEntity->v.velocity = dir * g_PusherEntity->v.armorvalue * g_PusherEntity->v.speed;

                            if (g_PusherEntity->v.max_health > 0)
                            {
                                if (pPendingEntity->v.velocity.z < g_PusherEntity->v.max_health * g_PusherEntity->v.speed)
                                    pPendingEntity->v.velocity.z = g_PusherEntity->v.max_health * g_PusherEntity->v.speed;
                            }

                            if (g_PusherEntity->v.avelocity[1] != 0 && g_PusherEntity->v.armortype > 0)
                            {
                                vec3_t dir2                = pPendingEntity->v.origin - g_PusherEntity->v.origin;
                                dir2.z                     = 0;
                                dir2                       = dir2.Normalize();
                                pPendingEntity->v.velocity = pPendingEntity->v.velocity + dir2 * g_PusherEntity->v.armortype * g_PusherEntity->v.speed;
                            }
                        }
                        else
                        {
                            pPendingEntity->v.velocity = dir * g_PusherEntity->v.armorvalue;

                            if (g_PusherEntity->v.max_health > 0)
                            {
                                if (pPendingEntity->v.velocity.z < g_PusherEntity->v.max_health)
                                    pPendingEntity->v.velocity.z = g_PusherEntity->v.max_health;
                            }

                            if (g_PusherEntity->v.avelocity[1] != 0 && g_PusherEntity->v.armortype > 0)
                            {
                                vec3_t dir2                = pPendingEntity->v.origin - g_PusherEntity->v.origin;
                                dir2.z                     = 0;
                                dir2                       = dir2.Normalize();
                                pPendingEntity->v.velocity = pPendingEntity->v.velocity + dir2 * g_PusherEntity->v.armortype;
                            }
                        }
                    }
                }
                else
                {
                    g_CurrentSuperPusherPushingVector = pushVec;
                    g_CurrentSuperPusher              = g_PusherEntity;

                    gpGamedllFuncs->dllapi_table->pfnTouch(g_PusherEntity, pPendingEntity);

                    g_CurrentSuperPusher = NULL;
                }
            }
        }
    }
    else if (g_bIsPushPhysics)
    {
        for (int i = 0; i < g_NumPendingEntities; ++i)
        {
            auto pPendingEntity = g_PendingEntities[i];

            if (IsEntityPushee(pPendingEntity) && pPendingEntity->v.groundentity != g_PusherEntity)
            {
                g_CurrentSuperPusherPushingVector = pushVec;
                g_CurrentSuperPusher              = g_PusherEntity;

                gpGamedllFuncs->dllapi_table->pfnTouch(g_PusherEntity, pPendingEntity);

                g_CurrentSuperPusher = NULL;
            }
        }
    }

    g_NumPendingEntities = 0;
    g_PushEntity         = NULL;
    g_bIsPushEntity      = false;
}

void OnSV_PushMove(IHookChain<void, edict_s*, float>* chain, void* /*userData*/, edict_s* pusher, float movetime) noexcept
{
    g_bIsPushMove  = true;
    g_PusherEntity = pusher;

    chain->CallNext(pusher, movetime);

    g_PusherEntity = NULL;
    g_bIsPushMove  = false;
}

int OnSV_PushRotate(IHookChain<int, edict_s*, float>* chain, void* /*userData*/, edict_s* pusher, float movetime) noexcept
{
    g_bIsPushRotate = true;
    g_PusherEntity  = pusher;

    const int result = chain->CallNext(pusher, movetime);

    g_PusherEntity  = NULL;
    g_bIsPushRotate = false;

    return result;
}

void OnSV_WriteMovevarsToClient(IHookChain<void, void*, movevars_s*>* chain, void* /*userData*/, void* message, movevars_s* movevars) noexcept
{
    if (g_bUseCustomStepSound && movevars)
    {
        const int footsteps = movevars->footsteps;
        movevars->footsteps = 0;

        chain->CallNext(message, movevars);

        movevars->footsteps = footsteps;
        return;
    }

    chain->CallNext(message, movevars);
}

} // namespace

void InstallEngineHooks()
{
    if (!g_ServerPhysicsSession.host)
        return;

    g_hPhysicsHook       = g_ServerPhysicsSession.host->RegisterPhysicsHook(&OnSV_Physics, nullptr);
    g_hPushEntityHook    = g_ServerPhysicsSession.host->RegisterPushEntityHook(&OnSV_PushEntity, nullptr);
    g_hPushMoveHook      = g_ServerPhysicsSession.host->RegisterPushMoveHook(&OnSV_PushMove, nullptr);
    g_hPushRotateHook    = g_ServerPhysicsSession.host->RegisterPushRotateHook(&OnSV_PushRotate, nullptr);
    g_hWriteMovevarsHook = g_ServerPhysicsSession.host->RegisterWriteMovevarsHook(&OnSV_WriteMovevarsToClient, nullptr);
}

void UninstallEngineHooks()
{
    if (!g_ServerPhysicsSession.host)
        return;

    if (g_hPhysicsHook != kInvalidHookHandle)
    {
        g_ServerPhysicsSession.host->UnregisterPhysicsHook(g_hPhysicsHook);
        g_hPhysicsHook = kInvalidHookHandle;
    }
    if (g_hPushEntityHook != kInvalidHookHandle)
    {
        g_ServerPhysicsSession.host->UnregisterPushEntityHook(g_hPushEntityHook);
        g_hPushEntityHook = kInvalidHookHandle;
    }
    if (g_hPushMoveHook != kInvalidHookHandle)
    {
        g_ServerPhysicsSession.host->UnregisterPushMoveHook(g_hPushMoveHook);
        g_hPushMoveHook = kInvalidHookHandle;
    }
    if (g_hPushRotateHook != kInvalidHookHandle)
    {
        g_ServerPhysicsSession.host->UnregisterPushRotateHook(g_hPushRotateHook);
        g_hPushRotateHook = kInvalidHookHandle;
    }
    if (g_hWriteMovevarsHook != kInvalidHookHandle)
    {
        g_ServerPhysicsSession.host->UnregisterWriteMovevarsHook(g_hWriteMovevarsHook);
        g_hWriteMovevarsHook = kInvalidHookHandle;
    }
}
