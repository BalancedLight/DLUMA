#ifndef DLUMA_FIXES_CRAFTING_MANAGER_SNAPSHOT_H
#define DLUMA_FIXES_CRAFTING_MANAGER_SNAPSHOT_H
#include "BlueprintBaselineRebuild.h"
namespace dluma::fixes::custom_map_asset_isolation::crafting_snapshot {
constexpr std::size_t kMaximumSpans=32768,kMaximumBytes=32U*1024U*1024U;
struct Span {std::uintptr_t address;std::uint32_t size,offset;};
struct Snapshot {Span* spans;std::size_t span_capacity,span_count;std::uint8_t* bytes;std::size_t capacity,used;
    blueprint_rebuild::Fence fence;bool complete;};
enum class Result:std::uint8_t {Succeeded,InvalidArgument,ProfileMismatch,FenceRejected,ReadFailed,InvalidGraph,CapacityExceeded,Changed};
// Walks all four native maps, definition/default/level strings, elemental
// interactions and their modifier vectors. The snapshot preserves addresses
// and exact values; it is not an opaque block replay or a deletion permit.
Result Capture(const blueprint_rebuild::Access&,Snapshot*) noexcept;
Result Verify(const blueprint_rebuild::Access&,const Snapshot&) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_snapshot
#endif
