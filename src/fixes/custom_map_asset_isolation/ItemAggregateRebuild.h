#ifndef DLUMA_FIXES_ITEM_AGGREGATE_REBUILD_H
#define DLUMA_FIXES_ITEM_AGGREGATE_REBUILD_H
#include "CraftingManagerSnapshot.h"
#include "CurveBaselineSnapshot.h"
namespace dluma::fixes::custom_map_asset_isolation::item_aggregate {
constexpr std::size_t kElementalTypes=17,kEffectTypes=21,kMaximumLevels=4096,kMaximumSourceEffects=64,kMaximumSources=32;
struct Pair {std::int32_t type,level;};
struct Effect {
    bool present,active;std::uint32_t flags,level_count;std::int32_t opposite;
    std::uint64_t masks[2];std::uint8_t levels[8][56];
};
struct Elemental {bool present;std::uint64_t masks[2];};
struct Level {std::uint32_t type,key;std::uintptr_t identity;std::uint32_t reported_level;};
struct Baseline {
    Effect effects[kEffectTypes];Elemental elementals[kElementalTypes];
    Level levels[kMaximumLevels];std::size_t level_count;
    blueprint_rebuild::Fence fence;bool complete;
};
struct Source {
    std::uint32_t elemental_levels[kElementalTypes];
    Pair effects[kMaximumSourceEffects];std::size_t effect_count;
};
struct Derived {
    std::uintptr_t elemental_levels[kElementalTypes];std::uint64_t masks[4];
    Pair effects[kEffectTypes];std::size_t effect_count;
    // Exact constructor fields584..647. The remaining aggregate flags/padding,
    // base strings and independently owned buffer headers are not overwritten.
    std::uint8_t parameters[64];
};
using ApplyFn=bool(*)(void*,std::uint8_t*,unsigned,const std::uint8_t*,float,unsigned*) noexcept;
struct Accumulator {std::uintptr_t gamedll_base;char sha256[65];ApplyFn apply;void* user;};
enum class Result:std::uint8_t {Succeeded,InvalidArgument,InvalidBaseline,InvalidSource,CapacityExceeded,Overflow,NativeFailed};
bool CaptureFrozenBaseline(const crafting_snapshot::Snapshot&,Baseline*) noexcept;
// Reads only the assessed CraftPart/WeaponModification or CraftPlan layout.
// The enclosing Item owner must first prove descriptor membership in held B0;
// this function additionally proves every referenced elemental level is in B0.
Result CaptureSource(const curves::NativeCurveAccess&,const Baseline&,std::uintptr_t,Source*) noexcept;
// Implements6B0560/2ED550/6B0280/6B0600 on local output. Unlike6B0790, this
// does not require a live Session store or mutate its process scratch list.
Result Build(const Baseline&,const Source*,std::size_t,const Source* upgrade,const Accumulator&,Derived*) noexcept;
bool NativeApply(void*,std::uint8_t*,unsigned,const std::uint8_t*,float,unsigned*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::item_aggregate
#endif
