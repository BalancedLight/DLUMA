#ifndef DLUMA_FIXES_ITEM_INTERN_POOL_SNAPSHOT_H
#define DLUMA_FIXES_ITEM_INTERN_POOL_SNAPSHOT_H
#include "CurveBaselineSnapshot.h"
namespace dluma::fixes::custom_map_asset_isolation::item_pools {
constexpr std::size_t kMaximumEntries=65536,kMaximumStringBytes=16U*1024U*1024U;
constexpr std::uintptr_t kStringPoolRva=0x1C9C358,kVectorPoolRva=0x1C9C370;
struct StringEntry {std::uintptr_t node,value,characters;std::uint32_t hash,length,capacity,offset;std::uint8_t links[32];};
struct VectorEntry {std::uintptr_t node,next,previous;std::uint32_t values[3],padding;};
struct Snapshot {
    StringEntry* strings;std::size_t string_capacity,string_count;
    VectorEntry* vectors;std::size_t vector_capacity,vector_count;
    std::uint8_t* bytes;std::size_t capacity,used;
    std::uint8_t string_header[24],vector_header[24];
    curves::CurveReadFence fence;bool complete;
};
enum class Result:std::uint8_t {Succeeded,InvalidArgument,ProfileMismatch,FenceRejected,InvalidGraph,ReadFailed,CapacityExceeded,Changed};
struct Difference {Result result;std::size_t appended_strings,appended_vectors;};
Result Capture(const curves::NativeCurveAccess&,Snapshot*) noexcept;
// Interning preserves payload identities/values while tree rotations and tail
// links may change. Added entries remain unowned until native allocation and
// consumer receipts separately qualify them.
Difference CompareBaseline(const Snapshot&,const Snapshot&) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::item_pools
#endif
