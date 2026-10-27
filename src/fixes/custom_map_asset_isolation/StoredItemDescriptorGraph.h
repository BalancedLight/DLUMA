#ifndef DLUMA_FIXES_STORED_ITEM_DESCRIPTOR_GRAPH_H
#define DLUMA_FIXES_STORED_ITEM_DESCRIPTOR_GRAPH_H
#include "PlayerStoredProgressSnapshot.h"
#include "StoredBlueprintOwner.h"
namespace dluma::fixes::custom_map_asset_isolation::stored_item_graph {
constexpr std::size_t kDescriptorCacheSize=4096,kMaximumItems=32768,kMaximumStringBytes=16U*1024U*1024U;
enum class Failure:std::uint8_t {None,InvalidArgument,OwnerUnqualified,Changed,InvalidVector,
    InvalidItemType,DescriptorUnowned,InvalidDescriptor,UnownedBlueprint,InvalidString,Limit};
struct Scratch {std::uintptr_t descriptors[kDescriptorCacheSize];};
struct Proof {
    std::uintptr_t state,context;std::uint64_t generation,revision;
    std::uint32_t live_items,lost_items,descriptor_count,blueprint_count;
    std::uint64_t string_bytes;Failure failure;bool complete;
};
// A read-only guard for the exact inventory sectional serializer. The caller
// holds its existing joined preservation/definition reader fence for the full
// capture AND following native serializer; this helper grants no retirement.
bool Capture(const player_stored_progress::Reader&,const stored_blueprints::Owner*,
    std::uintptr_t state,std::uint64_t generation,Scratch*,Proof*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::stored_item_graph
#endif
