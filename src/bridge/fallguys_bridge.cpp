#include "bridge/fallguys_bridge.h"

#include <extdll.h>
#include <meta_api.h>
#include <interface.h>

namespace fallguys_bridge
{

const char* BackendKindName(BackendKind kind) noexcept
{
    switch (kind)
    {
        case BackendKind::kHostNative:
            return "host-native";
        case BackendKind::kLegacyInlineHook:
            return "legacy-inline-hook";
        case BackendKind::kUnavailable:
        default:
            return "unavailable";
    }
}

ServerPhysicsSession AcquireServerPhysicsExt(void* engineHandle, void* engineBase) noexcept
{
    ServerPhysicsSession session;

    if (engineHandle && gpMetaUtilFuncs && gpMetaUtilFuncs->pfnGetProcAddress)
    {
        auto createInterface = reinterpret_cast<CreateInterfaceFn>(
            gpMetaUtilFuncs->pfnGetProcAddress(reinterpret_cast<DLHANDLE>(engineHandle), CREATEINTERFACE_PROCNAME));

        if (createInterface)
        {
            int             returnCode = 0;
            IBaseInterface* raw        = createInterface(
                goldsrcext::kGoldSrcServerPhysicsExtInterfaceName,
                &returnCode);

            if (raw)
            {
                session.kind = BackendKind::kHostNative;
                session.host = static_cast<goldsrcext::IGoldSrcServerPhysicsExt*>(raw);
                return session;
            }
        }
    }

#if !defined(_WIN64) && !defined(__x86_64__) && !defined(__LP64__)
    return AcquireLegacyServerPhysicsExt(engineHandle, engineBase);
#else
    (void)engineBase;
    if (gpMetaUtilFuncs)
    {
        LOG_ERROR(PLID,
                  "Engine does not expose %s; legacy 32-bit gamedata bridge is unavailable on 64-bit builds.",
                  goldsrcext::kGoldSrcServerPhysicsExtInterfaceName);
    }
    return session;
#endif
}

void ReleaseServerPhysicsExt(ServerPhysicsSession& session) noexcept
{
#if !defined(_WIN64) && !defined(__x86_64__) && !defined(__LP64__)
    if (session.kind == BackendKind::kLegacyInlineHook)
    {
        ReleaseLegacyServerPhysicsExt(session);
        return;
    }
#endif
    session = ServerPhysicsSession{};
}

PlayerMoveSession AcquirePlayerMoveExt(void* serverHandle, void* serverBase) noexcept
{
    PlayerMoveSession session;

    if (serverHandle && gpMetaUtilFuncs && gpMetaUtilFuncs->pfnGetProcAddress)
    {
        auto createInterface = reinterpret_cast<CreateInterfaceFn>(
            gpMetaUtilFuncs->pfnGetProcAddress(reinterpret_cast<DLHANDLE>(serverHandle), CREATEINTERFACE_PROCNAME));

        if (createInterface)
        {
            int             returnCode = 0;
            IBaseInterface* raw        = createInterface(
                goldsrcext::svencoop::kSvenCoopPlayerMoveExtInterfaceName,
                &returnCode);

            if (raw)
            {
                session.kind = BackendKind::kHostNative;
                session.host = static_cast<goldsrcext::svencoop::ISvenCoopPlayerMoveExt*>(raw);
                return session;
            }
        }
    }

#if !defined(_WIN64) && !defined(__x86_64__) && !defined(__LP64__)
    return AcquireLegacyPlayerMoveExt(serverHandle, serverBase);
#else
    (void)serverBase;
    if (gpMetaUtilFuncs)
    {
        LOG_ERROR(PLID,
                  "Game DLL does not expose %s; legacy 32-bit gamedata bridge is unavailable on 64-bit builds.",
                  goldsrcext::svencoop::kSvenCoopPlayerMoveExtInterfaceName);
    }
    return session;
#endif
}

void ReleasePlayerMoveExt(PlayerMoveSession& session) noexcept
{
#if !defined(_WIN64) && !defined(__x86_64__) && !defined(__LP64__)
    if (session.kind == BackendKind::kLegacyInlineHook)
    {
        ReleaseLegacyPlayerMoveExt(session);
        return;
    }
#endif
    session = PlayerMoveSession{};
}

} // namespace fallguys_bridge
