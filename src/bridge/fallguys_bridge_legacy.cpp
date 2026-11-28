#include "bridge/fallguys_bridge.h"

#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>
#include <cl_entity.h>
#include <entity_state.h>
#include <com_model.h>
#include <pm_movevars.h>

#include <GoldSrcExt/hook_chain_host.h>

#include "enginedef.h"
#include "serverdef.h"

#include <cstring>

extern NEW_DLL_FUNCTIONS* gpNewDllFunctionsTable;

namespace fallguys_bridge
{
namespace
{

using goldsrcext::CHookChainHost;
using goldsrcext::GoldSrcTrace;
using goldsrcext::HookCallbackFn;
using goldsrcext::HookHandle;
using goldsrcext::IGoldSrcServerPhysicsExt;
using goldsrcext::svencoop::ISvenCoopPlayerMoveExt;

// =============================================================================
// Legacy 32-bit Engine ServerPhysics Bridge
// =============================================================================

typedef int (*fnLegacy_build_number)();
typedef void (*fnLegacy_SV_Physics)();
typedef trace_t (*fnLegacy_SV_PushEntity)(edict_t* ent, vec3_t* push);
typedef void (*fnLegacy_SV_PushMove)(edict_t* pusher, float movetime);
typedef int (*fnLegacy_SV_PushRotate)(edict_t* pusher, float movetime);
typedef void (*fnLegacy_SV_WriteMovevarsToClient)(void* message);
typedef void (*fnLegacy_SV_SingleClipMoveToEntity)(edict_t* ent, const vec3_t& start, const vec3_t& mins, const vec3_t& maxs, const vec3_t& end, trace_t* trace);
typedef void (*fnLegacy_SV_SingleClipMoveToEntity_10152)(edict_t* ent, const vec3_t& start, const vec3_t& mins, const vec3_t& maxs, const vec3_t& end, trace_t* trace, edict_t* passedict);

static fnLegacy_build_number                    s_pfn_build_number                    = NULL;
static fnLegacy_SV_Physics                      s_pfn_SV_Physics                      = NULL;
static fnLegacy_SV_Physics                      s_orig_SV_Physics                     = NULL;
static fnLegacy_SV_PushEntity                   s_pfn_SV_PushEntity                   = NULL;
static fnLegacy_SV_PushEntity                   s_orig_SV_PushEntity                  = NULL;
static fnLegacy_SV_PushMove                     s_pfn_SV_PushMove                     = NULL;
static fnLegacy_SV_PushMove                     s_orig_SV_PushMove                    = NULL;
static fnLegacy_SV_PushRotate                   s_pfn_SV_PushRotate                   = NULL;
static fnLegacy_SV_PushRotate                   s_orig_SV_PushRotate                  = NULL;
static fnLegacy_SV_WriteMovevarsToClient        s_pfn_SV_WriteMovevarsToClient        = NULL;
static fnLegacy_SV_WriteMovevarsToClient        s_orig_SV_WriteMovevarsToClient       = NULL;
static fnLegacy_SV_SingleClipMoveToEntity       s_pfn_SV_SingleClipMoveToEntity       = NULL;
static fnLegacy_SV_SingleClipMoveToEntity_10152 s_pfn_SV_SingleClipMoveToEntity_10152 = NULL;

static hook_t* s_hook_SV_Physics               = NULL;
static hook_t* s_hook_SV_PushEntity            = NULL;
static hook_t* s_hook_SV_PushMove              = NULL;
static hook_t* s_hook_SV_PushRotate            = NULL;
static hook_t* s_hook_SV_WriteMovevarsToClient = NULL;

static model_t* (*s_sv_models)[8192]    = NULL;
static double*     s_host_frametime     = NULL;
static movevars_t* s_pmovevars          = NULL;
static areanode_t (*s_sv_areanodes)[32] = NULL;
static int* s_pg_groupop                = NULL;
static int* s_pg_groupmask              = NULL;

static bool ResolveLegacySymbol(void* moduleBase, const char* symbolName, meta_gamesymbol_kind_t kind, void** outAddr, bool required)
{
    *outAddr = NULL;
    if (!gpMetaUtilFuncs || !gpMetaUtilFuncs->pfnResolveGameSymbol)
        return false;

    const auto status = gpMetaUtilFuncs->pfnResolveGameSymbol(moduleBase, symbolName, kind, outAddr);
    if (status != META_GAMESYMBOL_OK)
    {
        if (required)
        {
            LOG_ERROR(PLID, "Failed to resolve required legacy symbol \"%s\" from gamedata: %s",
                      symbolName,
                      gpMetaUtilFuncs->pfnGetGameSymbolStatusString ? gpMetaUtilFuncs->pfnGetGameSymbolStatusString(status) : "unknown");
        }
        *outAddr = NULL;
        return false;
    }
    return *outAddr != NULL;
}

static void LegacySV_SingleClipMoveToEntity_10152(edict_t* ent, const vec3_t& start, const vec3_t& mins, const vec3_t& maxs, const vec3_t& end, trace_t* trace, edict_t* passedict)
{
    if (s_pfn_SV_SingleClipMoveToEntity_10152)
    {
        s_pfn_SV_SingleClipMoveToEntity_10152(ent, start, mins, maxs, end, trace, passedict);
        return;
    }
    if (s_pfn_SV_SingleClipMoveToEntity)
    {
        s_pfn_SV_SingleClipMoveToEntity(ent, start, mins, maxs, end, trace);
    }
}

static void LegacySV_MoveBounds(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, vec3_t& boxmins, vec3_t& boxmaxs)
{
    for (int i = 0; i < 3; i++)
    {
        if (end[i] > start[i])
        {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        }
        else
        {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }
}

static void LegacySV_ClipToLinksEx(areanode_t* node, moveclip_t* clip)
{
    link_t * l, *next;
    edict_t* touch;
    trace_t  trace;

    for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next)
    {
        next  = l->next;
        touch = EDICT_FROM_AREA(l);

        if (s_pg_groupop)
        {
            if (touch->v.groupinfo != 0 && clip->passedict && clip->passedict->v.groupinfo != 0)
            {
                if ((*s_pg_groupop) == GROUP_OP_AND && (touch->v.groupinfo & clip->passedict->v.groupinfo) == 0)
                    continue;

                if ((*s_pg_groupop) == GROUP_OP_NAND && (touch->v.groupinfo & clip->passedict->v.groupinfo) != 0)
                    continue;
            }
        }

        if (touch->v.solid == SOLID_NOT)
            continue;

        if (touch == clip->passedict)
            continue;

        if (gpNewDllFunctionsTable && gpNewDllFunctionsTable->pfnShouldCollide)
        {
            if (gpNewDllFunctionsTable->pfnShouldCollide(touch, clip->passedict) == 0)
                continue;
        }

        if (touch->v.solid == SOLID_BSP)
        {
            if ((touch->v.flags & FL_MONSTERCLIP) && !clip->monsterClipBrush)
                continue;
        }
        else if (clip->type == MOVE_NORMAL)
        {
            if (touch->v.movetype != MOVETYPE_PUSH && touch->v.movetype != MOVETYPE_PUSHSTEP && touch->v.movetype != MOVETYPE_NOCLIP)
                continue;
        }

        if (clip->ignoretrans && touch->v.rendermode != kRenderNormal && !(touch->v.flags & FL_WORLDBRUSH))
            continue;

        if (clip->boxmins[0] > touch->v.absmax[0] || clip->boxmins[1] > touch->v.absmax[1] || clip->boxmins[2] > touch->v.absmax[2] ||
            clip->boxmaxs[0] < touch->v.absmin[0] || clip->boxmaxs[1] < touch->v.absmin[1] || clip->boxmaxs[2] < touch->v.absmin[2])
            continue;

        if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0])
            continue;

        if (clip->trace.allsolid)
            return;

        if (clip->passedict)
        {
            if (touch->v.owner == clip->passedict)
                continue;
            if (clip->passedict->v.owner == touch)
                continue;
        }

        if ((int)touch->v.flags & FL_MONSTER)
        {
            LegacySV_SingleClipMoveToEntity_10152(touch, clip->start, clip->mins2, clip->maxs2, clip->end, &trace, clip->passedict);
        }
        else
        {
            vec3_t tempmins(clip->mins[0], clip->mins[1], clip->mins[2]);
            vec3_t tempmaxs(clip->maxs[0], clip->maxs[1], clip->maxs[2]);
            LegacySV_SingleClipMoveToEntity_10152(touch, clip->start, tempmins, tempmaxs, clip->end, &trace, clip->passedict);
        }

        if (trace.allsolid || trace.startsolid || trace.fraction < clip->trace.fraction)
        {
            trace.ent = touch;
            if (clip->trace.startsolid)
            {
                clip->trace            = trace;
                clip->trace.startsolid = true;
            }
            else
            {
                clip->trace = trace;
            }
        }
        else if (trace.startsolid)
        {
            clip->trace.startsolid = true;
        }
    }

    if (node->axis == -1)
        return;

    if (clip->boxmaxs[node->axis] > node->dist)
        LegacySV_ClipToLinksEx(node->children[0], clip);
    if (clip->boxmins[node->axis] < node->dist)
        LegacySV_ClipToLinksEx(node->children[1], clip);
}

static trace_t LegacySV_MoveEx(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type, edict_t* passedict, qboolean monsterClipBrush)
{
    moveclip_t clip;
    vec3_t     worldEndPoint;
    float      worldFraction;

    memset(&clip, 0, sizeof(moveclip_t));

    LegacySV_SingleClipMoveToEntity_10152(g_engfuncs.pfnPEntityOfEntIndex(0), start, mins, maxs, end, &clip.trace, passedict);

    if (clip.trace.fraction != 0)
    {
        worldEndPoint = clip.trace.endpos;
        worldFraction = clip.trace.fraction;

        clip.trace.fraction   = 1;
        clip.start            = start;
        clip.end              = worldEndPoint;
        clip.mins             = mins;
        clip.maxs             = maxs;
        clip.type             = type & 0xFF;
        clip.ignoretrans      = type >> 8;
        clip.passedict        = passedict;
        clip.monsterClipBrush = monsterClipBrush;

        if (type == MOVE_MISSILE)
        {
            for (int i = 0; i < 3; i++)
            {
                clip.mins2[i] = -15;
                clip.maxs2[i] = 15;
            }
        }
        else
        {
            clip.mins2 = mins;
            clip.maxs2 = maxs;
        }

        LegacySV_MoveBounds(start, clip.mins2, clip.maxs2, worldEndPoint, clip.boxmins, clip.boxmaxs);
        LegacySV_ClipToLinksEx(*s_sv_areanodes, &clip);

        gpGlobals->trace_ent = clip.trace.ent;
        clip.trace.fraction *= worldFraction;
    }

    return clip.trace;
}

static void LegacySV_SetGlobalTrace(trace_t* ptrace)
{
    gpGlobals->trace_allsolid     = (float)ptrace->allsolid;
    gpGlobals->trace_startsolid   = (float)ptrace->startsolid;
    gpGlobals->trace_inopen       = (float)ptrace->inopen;
    gpGlobals->trace_inwater      = (float)ptrace->inwater;
    gpGlobals->trace_endpos       = ptrace->endpos;
    gpGlobals->trace_plane_normal = ptrace->plane.normal;
    gpGlobals->trace_fraction     = ptrace->fraction;
    gpGlobals->trace_plane_dist   = ptrace->plane.dist;
    gpGlobals->trace_ent          = ptrace->ent ? ptrace->ent : g_engfuncs.pfnPEntityOfEntIndex(0);
    gpGlobals->trace_hitgroup     = ptrace->hitgroup;
}

class CLegacyServerPhysicsExt final : public IGoldSrcServerPhysicsExt
{
public:
    HookHandle RegisterPhysicsHook(HookCallbackFn<void, double> cb, void* ud, std::int32_t alt) noexcept override
    {
        return m_physics.Register(cb, ud, alt);
    }
    bool UnregisterPhysicsHook(HookHandle h) noexcept override { return m_physics.Unregister(h); }

    HookHandle RegisterPushEntityHook(HookCallbackFn<void, edict_s*, const float*, GoldSrcTrace*> cb, void* ud, std::int32_t alt) noexcept override
    {
        return m_pushEntity.Register(cb, ud, alt);
    }
    bool UnregisterPushEntityHook(HookHandle h) noexcept override { return m_pushEntity.Unregister(h); }

    HookHandle RegisterPushMoveHook(HookCallbackFn<void, edict_s*, float> cb, void* ud, std::int32_t alt) noexcept override
    {
        return m_pushMove.Register(cb, ud, alt);
    }
    bool UnregisterPushMoveHook(HookHandle h) noexcept override { return m_pushMove.Unregister(h); }

    HookHandle RegisterPushRotateHook(HookCallbackFn<int, edict_s*, float> cb, void* ud, std::int32_t alt) noexcept override
    {
        return m_pushRotate.Register(cb, ud, alt);
    }
    bool UnregisterPushRotateHook(HookHandle h) noexcept override { return m_pushRotate.Unregister(h); }

    HookHandle RegisterWriteMovevarsHook(HookCallbackFn<void, void*, movevars_s*> cb, void* ud, std::int32_t alt) noexcept override
    {
        return m_writeMovevars.Register(cb, ud, alt);
    }
    bool UnregisterWriteMovevarsHook(HookHandle h) noexcept override { return m_writeMovevars.Unregister(h); }

    int GetBuildNumber() const noexcept override
    {
        return s_pfn_build_number ? s_pfn_build_number() : 0;
    }

    double GetHostFrametime() const noexcept override
    {
        return s_host_frametime ? *s_host_frametime : 0.0;
    }

    int GetMaxPrecachedModels() const noexcept override
    {
        return 8192;
    }

    model_s* GetPrecachedModelByIndex(int index) const noexcept override
    {
        if (!s_sv_models || index < 0 || index >= 8192)
            return nullptr;
        return (*s_sv_models)[index];
    }

    movevars_s* GetServerMoveVars() noexcept override
    {
        return s_pmovevars;
    }

    void PushEntity(edict_s* ent, const float push[3], GoldSrcTrace* outTrace) noexcept override
    {
        if (!ent)
            return;

        m_pushEntity.InvokeWithOriginal(&CallOriginalPushEntity, ent, push, outTrace);
    }

    edict_s* TestEntityPositionEx(edict_s* ent) noexcept override
    {
        if (!ent || !s_sv_areanodes)
            return nullptr;

        const qboolean monsterClip = (ent->v.flags & FL_MONSTERCLIP) ? true : false;
        trace_t        trace       = LegacySV_MoveEx(ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NORMAL, ent, monsterClip);
        if (trace.startsolid)
        {
            LegacySV_SetGlobalTrace(&trace);
            return trace.ent;
        }
        return nullptr;
    }

    void DispatchPhysics() noexcept
    {
        m_physics.InvokeWithOriginal(&CallOriginalPhysics, GetHostFrametime());
    }

    trace_t DispatchPushEntity(edict_t* ent, vec3_t* push) noexcept
    {
        GoldSrcTrace rawTrace{};
        const float  pushVec[3] = {
            push ? push->x : 0.0f,
            push ? push->y : 0.0f,
            push ? push->z : 0.0f,
        };
        m_pushEntity.InvokeWithOriginal(&CallOriginalPushEntity, ent, pushVec, &rawTrace);
        trace_t result;
        std::memcpy(&result, &rawTrace, sizeof(result));
        return result;
    }

    void DispatchPushMove(edict_t* pusher, float movetime) noexcept
    {
        m_pushMove.InvokeWithOriginal(s_orig_SV_PushMove, pusher, movetime);
    }

    int DispatchPushRotate(edict_t* pusher, float movetime) noexcept
    {
        return m_pushRotate.InvokeWithOriginal(s_orig_SV_PushRotate, pusher, movetime);
    }

    void DispatchWriteMovevars(void* message) noexcept
    {
        m_writeMovevars.InvokeWithOriginal(&CallOriginalWriteMovevars, message, s_pmovevars);
    }

    void ClearHooks() noexcept
    {
        m_physics.Clear();
        m_pushEntity.Clear();
        m_pushMove.Clear();
        m_pushRotate.Clear();
        m_writeMovevars.Clear();
    }

private:
    static void CallOriginalPhysics(double /*frametime*/)
    {
        if (s_orig_SV_Physics)
            s_orig_SV_Physics();
    }

    static void CallOriginalPushEntity(edict_s* ent, const float* push, GoldSrcTrace* outTrace)
    {
        if (!s_orig_SV_PushEntity)
            return;

        vec3_t pushVec(
            push ? push[0] : 0.0f,
            push ? push[1] : 0.0f,
            push ? push[2] : 0.0f);
        trace_t result = s_orig_SV_PushEntity(ent, &pushVec);
        if (outTrace)
            std::memcpy(outTrace, &result, sizeof(result));
    }

    static void CallOriginalWriteMovevars(void* message, movevars_s* /*movevars*/)
    {
        if (s_orig_SV_WriteMovevarsToClient)
            s_orig_SV_WriteMovevarsToClient(message);
    }

    CHookChainHost<void, double>                                m_physics;
    CHookChainHost<void, edict_s*, const float*, GoldSrcTrace*> m_pushEntity;
    CHookChainHost<void, edict_s*, float>                       m_pushMove;
    CHookChainHost<int, edict_s*, float>                        m_pushRotate;
    CHookChainHost<void, void*, movevars_s*>                    m_writeMovevars;
};

static CLegacyServerPhysicsExt s_legacyServerPhysics;

static void Detour_SV_Physics()
{
    s_legacyServerPhysics.DispatchPhysics();
}

static trace_t Detour_SV_PushEntity(edict_t* ent, vec3_t* push)
{
    return s_legacyServerPhysics.DispatchPushEntity(ent, push);
}

static void Detour_SV_PushMove(edict_t* pusher, float movetime)
{
    s_legacyServerPhysics.DispatchPushMove(pusher, movetime);
}

static int Detour_SV_PushRotate(edict_t* pusher, float movetime)
{
    return s_legacyServerPhysics.DispatchPushRotate(pusher, movetime);
}

static void Detour_SV_WriteMovevarsToClient(void* message)
{
    s_legacyServerPhysics.DispatchWriteMovevars(message);
}

// =============================================================================
// Legacy 32-bit Sven Co-op Server PlayerMove Bridge
// =============================================================================

typedef void(SC_SERVER_DECL* fnLegacy_CPlayerMove_PlayStepSound)(void* pthis, SC_SERVER_DUMMYARG int iType, float flVolume, bool bIsJump);
typedef void (*fnLegacy_PM_PlaySoundFX_SERVER)(int playerindex, vec3_t* origin, int type, const char* sound, float vol, float att, int flags, int pitch);
typedef void(SC_SERVER_CDECL* fnLegacy_RegisterSCScriptColor24)(CASDocumentation* pthis);

static fnLegacy_CPlayerMove_PlayStepSound s_pfn_CPlayerMove_PlayStepSound  = NULL;
static fnLegacy_CPlayerMove_PlayStepSound s_orig_CPlayerMove_PlayStepSound = NULL;
static fnLegacy_PM_PlaySoundFX_SERVER     s_pfn_PM_PlaySoundFX_SERVER      = NULL;
static fnLegacy_PM_PlaySoundFX_SERVER     s_orig_PM_PlaySoundFX_SERVER     = NULL;
static fnLegacy_RegisterSCScriptColor24   s_pfn_RegisterSCScriptColor24    = NULL;

static hook_t* s_hook_CPlayerMove_PlayStepSound = NULL;
static hook_t* s_hook_PM_PlaySoundFX_SERVER     = NULL;

static void* s_activeLegacyPlayerMoveThis = NULL;

static void CallOriginalPlayStepSound(playermove_s* /*pmove*/, int iType, float flVolume, bool bIsJump)
{
    if (s_orig_CPlayerMove_PlayStepSound && s_activeLegacyPlayerMoveThis)
    {
#if defined(_WIN32) && !defined(_WIN64)
        const int dummy = 0;
#endif
        s_orig_CPlayerMove_PlayStepSound(s_activeLegacyPlayerMoveThis, SC_SERVER_PASS_DUMMYARG iType, flVolume, bIsJump);
    }
}

static void CallOriginalPlaySoundFX(int playerindex, float* origin, int type, const char* sound, float vol, float att, int flags, int pitch)
{
    if (s_orig_PM_PlaySoundFX_SERVER)
    {
        s_orig_PM_PlaySoundFX_SERVER(playerindex, reinterpret_cast<vec3_t*>(origin), type, sound, vol, att, flags, pitch);
    }
}

class CLegacyPlayerMoveExt final : public ISvenCoopPlayerMoveExt
{
public:
    HookHandle RegisterPlayStepSoundHook(
        HookCallbackFn<void, playermove_s*, int, float, bool> cb,
        void*                                                 ud,
        std::int32_t                                          alt) noexcept override
    {
        return m_playStepSound.Register(cb, ud, alt);
    }

    bool UnregisterPlayStepSoundHook(HookHandle h) noexcept override
    {
        return m_playStepSound.Unregister(h);
    }

    HookHandle RegisterPlaySoundFXHook(
        HookCallbackFn<void, int, float*, int, const char*, float, float, int, int> cb,
        void*                                                                       ud,
        std::int32_t                                                                alt) noexcept override
    {
        return m_playSoundFX.Register(cb, ud, alt);
    }

    bool UnregisterPlaySoundFXHook(HookHandle h) noexcept override
    {
        return m_playSoundFX.Unregister(h);
    }

    void DispatchPlayStepSound(void* pthis, int iType, float flVolume, bool bIsJump) noexcept
    {
        playermove_s* pm             = *reinterpret_cast<playermove_s**>(static_cast<char*>(pthis) + 0x460);
        void*         prevTarget     = s_activeLegacyPlayerMoveThis;
        s_activeLegacyPlayerMoveThis = pthis;
        m_playStepSound.InvokeWithOriginal(&CallOriginalPlayStepSound, pm, iType, flVolume, bIsJump);
        s_activeLegacyPlayerMoveThis = prevTarget;
    }

    void DispatchPlaySoundFX(int playerindex, vec3_t* origin, int type, const char* sound, float vol, float att, int flags, int pitch) noexcept
    {
        m_playSoundFX.InvokeWithOriginal(&CallOriginalPlaySoundFX, playerindex, reinterpret_cast<float*>(origin), type, sound, vol, att, flags, pitch);
    }

    void ClearHooks() noexcept
    {
        m_playStepSound.Clear();
        m_playSoundFX.Clear();
    }

private:
    CHookChainHost<void, playermove_s*, int, float, bool>                       m_playStepSound;
    CHookChainHost<void, int, float*, int, const char*, float, float, int, int> m_playSoundFX;
};

static CLegacyPlayerMoveExt s_legacyPlayerMove;

static void SC_SERVER_DECL Detour_CPlayerMove_PlayStepSound(void* pthis, SC_SERVER_DUMMYARG int iType, float flVolume, bool bIsJump)
{
    s_legacyPlayerMove.DispatchPlayStepSound(pthis, iType, flVolume, bIsJump);
}

static void Detour_PM_PlaySoundFX_SERVER(int playerindex, vec3_t* origin, int type, const char* sound, float vol, float att, int flags, int pitch)
{
    s_legacyPlayerMove.DispatchPlaySoundFX(playerindex, origin, type, sound, vol, att, flags, pitch);
}

static void LegacyRegisterColor24Adapter(CASDocumentation* pASDoc)
{
    if (s_pfn_RegisterSCScriptColor24)
        s_pfn_RegisterSCScriptColor24(pASDoc);
}

} // namespace

ServerPhysicsSession AcquireLegacyServerPhysicsExt(void* /*engineHandle*/, void* engineBase) noexcept
{
    ServerPhysicsSession session;

    void* addr = NULL;
    if (!ResolveLegacySymbol(engineBase, build_number_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_build_number = reinterpret_cast<fnLegacy_build_number>(addr);

    if (!ResolveLegacySymbol(engineBase, SV_Physics_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_SV_Physics  = reinterpret_cast<fnLegacy_SV_Physics>(addr);
    s_orig_SV_Physics = s_pfn_SV_Physics;

    if (!ResolveLegacySymbol(engineBase, SV_PushEntity_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_SV_PushEntity  = reinterpret_cast<fnLegacy_SV_PushEntity>(addr);
    s_orig_SV_PushEntity = s_pfn_SV_PushEntity;

    if (!ResolveLegacySymbol(engineBase, SV_PushMove_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_SV_PushMove  = reinterpret_cast<fnLegacy_SV_PushMove>(addr);
    s_orig_SV_PushMove = s_pfn_SV_PushMove;

    if (!ResolveLegacySymbol(engineBase, SV_PushRotate_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_SV_PushRotate  = reinterpret_cast<fnLegacy_SV_PushRotate>(addr);
    s_orig_SV_PushRotate = s_pfn_SV_PushRotate;

    if (!ResolveLegacySymbol(engineBase, SV_WriteMovevarsToClient_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_SV_WriteMovevarsToClient  = reinterpret_cast<fnLegacy_SV_WriteMovevarsToClient>(addr);
    s_orig_SV_WriteMovevarsToClient = s_pfn_SV_WriteMovevarsToClient;

    if (s_pfn_build_number() >= 10152)
    {
        if (!ResolveLegacySymbol(engineBase, SV_SingleClipMoveToEntity_10152_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
            return session;
        s_pfn_SV_SingleClipMoveToEntity_10152 = reinterpret_cast<fnLegacy_SV_SingleClipMoveToEntity_10152>(addr);
    }
    else
    {
        if (!ResolveLegacySymbol(engineBase, SV_SingleClipMoveToEntity_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
            return session;
        s_pfn_SV_SingleClipMoveToEntity = reinterpret_cast<fnLegacy_SV_SingleClipMoveToEntity>(addr);
    }

    if (!ResolveLegacySymbol(engineBase, sv_models_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_sv_models = reinterpret_cast<model_t*(*)[8192]>(addr);

    if (!ResolveLegacySymbol(engineBase, host_frametime_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_host_frametime = reinterpret_cast<double*>(addr);

    if (!ResolveLegacySymbol(engineBase, pmovevars_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_pmovevars = reinterpret_cast<movevars_t*>(addr);

    if (!ResolveLegacySymbol(engineBase, sv_areanodes_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_sv_areanodes = reinterpret_cast<areanode_t(*)[32]>(addr);

    if (!ResolveLegacySymbol(engineBase, pg_groupop_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_pg_groupop = reinterpret_cast<int*>(addr);

    if (!ResolveLegacySymbol(engineBase, pg_groupmask_GameSymbol, META_GAMESYMBOL_KIND_GLOBAL, &addr, true))
        return session;
    s_pg_groupmask = reinterpret_cast<int*>(addr);

    s_hook_SV_Physics               = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_SV_Physics, (void*)Detour_SV_Physics, (void**)&s_orig_SV_Physics, true);
    s_hook_SV_PushEntity            = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_SV_PushEntity, (void*)Detour_SV_PushEntity, (void**)&s_orig_SV_PushEntity, true);
    s_hook_SV_PushMove              = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_SV_PushMove, (void*)Detour_SV_PushMove, (void**)&s_orig_SV_PushMove, true);
    s_hook_SV_PushRotate            = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_SV_PushRotate, (void*)Detour_SV_PushRotate, (void**)&s_orig_SV_PushRotate, true);
    s_hook_SV_WriteMovevarsToClient = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_SV_WriteMovevarsToClient, (void*)Detour_SV_WriteMovevarsToClient, (void**)&s_orig_SV_WriteMovevarsToClient, true);

    session.kind = BackendKind::kLegacyInlineHook;
    session.host = &s_legacyServerPhysics;
    return session;
}

void ReleaseLegacyServerPhysicsExt(ServerPhysicsSession& session) noexcept
{
    s_legacyServerPhysics.ClearHooks();

    if (gpMetaUtilFuncs && gpMetaUtilFuncs->pfnUnHook)
    {
        if (s_hook_SV_Physics)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_SV_Physics);
            s_hook_SV_Physics = NULL;
        }
        if (s_hook_SV_PushEntity)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_SV_PushEntity);
            s_hook_SV_PushEntity = NULL;
        }
        if (s_hook_SV_PushMove)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_SV_PushMove);
            s_hook_SV_PushMove = NULL;
        }
        if (s_hook_SV_PushRotate)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_SV_PushRotate);
            s_hook_SV_PushRotate = NULL;
        }
        if (s_hook_SV_WriteMovevarsToClient)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_SV_WriteMovevarsToClient);
            s_hook_SV_WriteMovevarsToClient = NULL;
        }
    }

    session = ServerPhysicsSession{};
}

PlayerMoveSession AcquireLegacyPlayerMoveExt(void* /*serverHandle*/, void* serverBase) noexcept
{
    PlayerMoveSession session;

    void* addr = NULL;
    if (!ResolveLegacySymbol(serverBase, PM_PlaySoundFX_SERVER_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_PM_PlaySoundFX_SERVER  = reinterpret_cast<fnLegacy_PM_PlaySoundFX_SERVER>(addr);
    s_orig_PM_PlaySoundFX_SERVER = s_pfn_PM_PlaySoundFX_SERVER;

    if (!ResolveLegacySymbol(serverBase, CPlayerMove_PlayStepSound_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, true))
        return session;
    s_pfn_CPlayerMove_PlayStepSound  = reinterpret_cast<fnLegacy_CPlayerMove_PlayStepSound>(addr);
    s_orig_CPlayerMove_PlayStepSound = s_pfn_CPlayerMove_PlayStepSound;

    if (ResolveLegacySymbol(serverBase, RegisterSCScriptColor24_GameSymbol, META_GAMESYMBOL_KIND_FUNCTION, &addr, false))
    {
        s_pfn_RegisterSCScriptColor24 = reinterpret_cast<fnLegacy_RegisterSCScriptColor24>(addr);
    }

    s_hook_CPlayerMove_PlayStepSound = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_CPlayerMove_PlayStepSound, (void*)Detour_CPlayerMove_PlayStepSound, (void**)&s_orig_CPlayerMove_PlayStepSound, true);
    s_hook_PM_PlaySoundFX_SERVER     = gpMetaUtilFuncs->pfnInlineHook((void*)s_pfn_PM_PlaySoundFX_SERVER, (void*)Detour_PM_PlaySoundFX_SERVER, (void**)&s_orig_PM_PlaySoundFX_SERVER, true);

    session.kind                  = BackendKind::kLegacyInlineHook;
    session.host                  = &s_legacyPlayerMove;
    session.legacyRegisterColor24 = s_pfn_RegisterSCScriptColor24 ? &LegacyRegisterColor24Adapter : nullptr;
    return session;
}

void ReleaseLegacyPlayerMoveExt(PlayerMoveSession& session) noexcept
{
    s_legacyPlayerMove.ClearHooks();

    if (gpMetaUtilFuncs && gpMetaUtilFuncs->pfnUnHook)
    {
        if (s_hook_CPlayerMove_PlayStepSound)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_CPlayerMove_PlayStepSound);
            s_hook_CPlayerMove_PlayStepSound = NULL;
        }
        if (s_hook_PM_PlaySoundFX_SERVER)
        {
            gpMetaUtilFuncs->pfnUnHook(s_hook_PM_PlaySoundFX_SERVER);
            s_hook_PM_PlaySoundFX_SERVER = NULL;
        }
    }

    s_pfn_RegisterSCScriptColor24 = NULL;
    session                       = PlayerMoveSession{};
}

} // namespace fallguys_bridge
