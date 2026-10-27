#ifndef DLUMA_FIXES_ITEM_AUXILIARY_SNAPSHOT_H
#define DLUMA_FIXES_ITEM_AUXILIARY_SNAPSHOT_H
#include "CurveBaselineSnapshot.h"

namespace dluma::fixes::custom_map_asset_isolation::item_auxiliary {
constexpr std::uintptr_t kManagerRva=0x1E0EBD0;
constexpr std::size_t kMaximumSpans=32768,kMaximumBytes=32U*1024U*1024U;
constexpr char kPlayerEngineSha256[]="a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c";
struct Access {
    curves::NativeCurveAccess native;
    char engine_sha256[65];
    // The actual singleton or an owner-held header with the same native layout.
    // The enclosing Item owner must qualify that identity in its fence.
    std::uintptr_t manager;
};
struct Span {
    std::uintptr_t address;
    std::uint32_t size,allocation_size,offset;
};
struct Snapshot {
    Span* spans;std::size_t span_capacity,span_count;
    std::uint8_t* bytes;std::size_t capacity,used;
    curves::CurveReadFence fence;
    std::uintptr_t manager;
    std::size_t enum_fields,enum_options,part_color_records;
    bool complete;
};
enum class Result:std::uint8_t {
    Succeeded,InvalidArgument,ProfileMismatch,FenceRejected,ReadFailed,
    InvalidGraph,CapacityExceeded,Changed
};
// Deep metadata for manager maps+8/+32/+56, saved item rows+176,
// registered enum lists+200/+224/+248 and their CEnumInfo option allocations,
// text vectors+272/+296/+320, cut types+344, scalar limits+368..511,
// float table+512, trees+608..704, separate repair counts and part/color costs.
// Primary descriptors/indices, Curve bindings and intern pools have separate
// owners. Capturing these allocations grants no native dispatch or deletion.
Result Capture(const Access&,Snapshot*) noexcept;
Result Verify(const Access&,const Snapshot&) noexcept;
enum class BorrowKind:std::uint8_t {Effect,PickupSounds};
// Tests exact value identity by traversing the captured native map. An address
// merely falling within some archived allocation is not sufficient.
bool ContainsBorrow(const Snapshot&,BorrowKind,std::uintptr_t) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::item_auxiliary
#endif
