#pragma once

#include <GoldSrcExt/server_physics_ext.h>
#include <GoldSrcExt/svencoop/server/playermove_ext.h>

class CASDocumentation;

namespace fallguys_bridge
{

enum class BackendKind
{
    kUnavailable = 0,
    kHostNative,
    kLegacyInlineHook,
};

using LegacyRegisterColor24Fn = void (*)(CASDocumentation* pASDoc);

struct ServerPhysicsSession
{
    BackendKind                           kind = BackendKind::kUnavailable;
    goldsrcext::IGoldSrcServerPhysicsExt* host = nullptr;
};

struct PlayerMoveSession
{
    BackendKind                                   kind                  = BackendKind::kUnavailable;
    goldsrcext::svencoop::ISvenCoopPlayerMoveExt* host                  = nullptr;
    LegacyRegisterColor24Fn                       legacyRegisterColor24 = nullptr;
};

ServerPhysicsSession AcquireServerPhysicsExt(void* engineHandle, void* engineBase) noexcept;
void                 ReleaseServerPhysicsExt(ServerPhysicsSession& session) noexcept;

PlayerMoveSession AcquirePlayerMoveExt(void* serverHandle, void* serverBase) noexcept;
void              ReleasePlayerMoveExt(PlayerMoveSession& session) noexcept;

const char* BackendKindName(BackendKind kind) noexcept;

#if !defined(_WIN64) && !defined(__x86_64__) && !defined(__LP64__)
ServerPhysicsSession AcquireLegacyServerPhysicsExt(void* engineHandle, void* engineBase) noexcept;
void                 ReleaseLegacyServerPhysicsExt(ServerPhysicsSession& session) noexcept;

PlayerMoveSession AcquireLegacyPlayerMoveExt(void* serverHandle, void* serverBase) noexcept;
void              ReleaseLegacyPlayerMoveExt(PlayerMoveSession& session) noexcept;
#endif

} // namespace fallguys_bridge
