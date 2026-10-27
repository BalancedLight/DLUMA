#ifndef DLUMA_FIXES_BLUEPRINT_BASELINE_REBUILD_H
#define DLUMA_FIXES_BLUEPRINT_BASELINE_REBUILD_H
#include "StoredBlueprintOwner.h"
namespace dluma::fixes::custom_map_asset_isolation::crafting_snapshot {struct Snapshot;}
namespace dluma::fixes::custom_map_asset_isolation::blueprint_rebuild {
constexpr std::size_t kEffectTypes=21,kEffectLevels=8,kLevelBytes=56;
struct Effect {std::uint32_t flags,level_count;bool present,active;std::uint8_t levels[kEffectLevels][kLevelBytes];};
struct Fence {
    std::uintptr_t game_di,manager;
    std::uint64_t owner_generation,revision,baseline_generation;
    std::uint32_t thread_id;
    bool writers_closed,initial_pristine;
};
using FenceFn=bool(*)(void*,Fence*) noexcept;
struct Access {std::uintptr_t gamedll_base;char sha256[65];stored_blueprints::ReadFn read;FenceFn fence;void* user;};
struct Baseline {Effect effects[kEffectTypes];Fence fence;bool complete;};
// Captures only the immutable scalar inputs consumed by native Blueprint init.
// This is not a claim that every Crafting-manager child can be retired.
bool CaptureBaseline(const Access&,Baseline*) noexcept;
// Derives the same inputs from the already-sealed complete native graph. This
// avoids a second publication window or adopting the current project's owner.
bool CaptureFrozenBaseline(const crafting_snapshot::Snapshot&,Baseline*) noexcept;
// Native 2EFE10 writes only the supplied parameter block and reads level+48/+52.
// The default native bridge verifies the exact module identity before dispatch.
using AccumulateFn=bool(*)(void*,std::uint8_t*,std::uint32_t,const std::uint8_t*,float) noexcept;
struct Accumulator {std::uintptr_t gamedll_base;char sha256[65];AccumulateFn invoke;void* user;};
struct Rebuild {stored_blueprints::BlueprintCopy* target;std::uint8_t expected[176],desired[176];};
enum class Result:std::uint8_t {Succeeded,InvalidArgument,InvalidId,InvalidBaseline,PayloadChanged,NativeFailed,CapacityExceeded};
// Produces a complete reviewable payload without touching published copies or
// native session/global scratch. Owner publication requires a separate closure.
Result BuildPayload(const Baseline&,const Accumulator&,const std::uint8_t* captured,std::uint8_t* output) noexcept;
Result BuildOwnedPlan(stored_blueprints::Owner*,const Baseline&,const Accumulator&,Rebuild*,std::size_t,std::size_t*) noexcept;
bool NativeAccumulate(void*,std::uint8_t*,std::uint32_t,const std::uint8_t*,float) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::blueprint_rebuild
#endif
