#include <extdll.h>
#include <com_model.h>
#include <pm_defs.h>
#include <GoldSrcExt/server_physics_ext.h>
#include <GoldSrcExt/svencoop/server/playermove_ext.h>

#include <cstddef>
#include <iostream>

using goldsrcext::GoldSrcTrace;

// Verify GoldSrcTrace matches HLSDK trace_t layout identically on 32-bit and 64-bit.
static_assert(sizeof(GoldSrcTrace) == sizeof(trace_t), "GoldSrcTrace size must match HLSDK trace_t");
static_assert(offsetof(GoldSrcTrace, allsolid) == offsetof(trace_t, allsolid), "GoldSrcTrace::allsolid mismatch");
static_assert(offsetof(GoldSrcTrace, startsolid) == offsetof(trace_t, startsolid), "GoldSrcTrace::startsolid mismatch");
static_assert(offsetof(GoldSrcTrace, inopen) == offsetof(trace_t, inopen), "GoldSrcTrace::inopen mismatch");
static_assert(offsetof(GoldSrcTrace, inwater) == offsetof(trace_t, inwater), "GoldSrcTrace::inwater mismatch");
static_assert(offsetof(GoldSrcTrace, fraction) == offsetof(trace_t, fraction), "GoldSrcTrace::fraction mismatch");
static_assert(offsetof(GoldSrcTrace, endpos) == offsetof(trace_t, endpos), "GoldSrcTrace::endpos mismatch");
static_assert(offsetof(GoldSrcTrace, planeNormal) == offsetof(trace_t, plane) + offsetof(plane_t, normal), "GoldSrcTrace::planeNormal mismatch");
static_assert(offsetof(GoldSrcTrace, planeDist) == offsetof(trace_t, plane) + offsetof(plane_t, dist), "GoldSrcTrace::planeDist mismatch");
static_assert(offsetof(GoldSrcTrace, ent) == offsetof(trace_t, ent), "GoldSrcTrace::ent mismatch");
static_assert(offsetof(GoldSrcTrace, hitgroup) == offsetof(trace_t, hitgroup), "GoldSrcTrace::hitgroup mismatch");

// Verify playermove_t and playermove_10152_t prefix layout alignment.
static_assert(offsetof(playermove_t, player_index) == offsetof(playermove_10152_t, player_index), "playermove player_index mismatch");
static_assert(offsetof(playermove_t, sztexturename) == offsetof(playermove_10152_t, sztexturename), "playermove sztexturename mismatch");
static_assert(offsetof(playermove_t, physents) == offsetof(playermove_10152_t, physents), "playermove physents mismatch");

// Verify color24 plain value type layout.
static_assert(sizeof(color24) == 3, "color24 must be 3 bytes");
static_assert(offsetof(color24, r) == 0 && offsetof(color24, g) == 1 && offsetof(color24, b) == 2, "color24 RGB offsets mismatch");

// Verify texture_t width/height offsets match HLND2T gl_model.h.
static_assert(offsetof(texture_t, width) == 16, "texture_t::width offset mismatch");
static_assert(offsetof(texture_t, height) == 20, "texture_t::height offset mismatch");

int main()
{
    std::cout << "FallGuys ABI checks passed (sizeof(void*)=" << sizeof(void*)
              << ", sizeof(trace_t)=" << sizeof(trace_t)
              << ", sizeof(playermove_10152_t)=" << sizeof(playermove_10152_t)
              << ", sizeof(model_t)=" << sizeof(model_t) << ").\n";
    return 0;
}
