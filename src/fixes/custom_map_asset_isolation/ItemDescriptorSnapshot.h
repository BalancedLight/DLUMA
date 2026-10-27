#ifndef DLUMA_FIXES_ITEM_DESCRIPTOR_SNAPSHOT_H
#define DLUMA_FIXES_ITEM_DESCRIPTOR_SNAPSHOT_H
#include "ItemAuxiliarySnapshot.h"
#include "ItemInternPoolSnapshot.h"
#include "CraftingManagerSnapshot.h"
namespace dluma::fixes::custom_map_asset_isolation::item_descriptors {
constexpr std::size_t kMaximumDescriptors=65536,kMaximumSpans=524288,kMaximumBytes=256U*1024U*1024U;
struct Record {std::uintptr_t identity;std::uint32_t id,size,vector_index,index_count;};
struct Snapshot {
    Record* records;std::size_t record_capacity,count;
    item_auxiliary::Span* spans;std::size_t span_capacity,span_count;
    std::uint8_t* bytes;std::size_t capacity,used;
    curves::CurveReadFence fence;std::uintptr_t manager;
    std::uint32_t unqualified_offset;bool complete;
};
struct Inputs {
    const item_pools::Snapshot* pools;
    const item_auxiliary::Snapshot* auxiliary;
    const crafting_snapshot::Snapshot* crafting;
};
enum class Result:std::uint8_t {Succeeded,InvalidArgument,ProfileMismatch,FenceRejected,ReadFailed,
    InvalidGraph,UnsupportedType,UnqualifiedField,ForeignBorrow,CapacityExceeded,Changed};
// The caller closes the actual Item/Crafting/pool writers for the joined read.
// The snapshot covers primary allocations and indices; referenced interned,
// auxiliary and Crafting allocations must belong to the supplied snapshots.
Result Capture(const curves::NativeCurveAccess&,std::uintptr_t manager,const Inputs&,Snapshot*) noexcept;
Result Verify(const curves::NativeCurveAccess&,const Snapshot&) noexcept;
bool Contains(const Snapshot&,std::uintptr_t) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::item_descriptors
#endif
