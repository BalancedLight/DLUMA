#ifndef DLUMA_FIXES_ITEM_CURVE_BINDINGS_H
#define DLUMA_FIXES_ITEM_CURVE_BINDINGS_H
#include "CurveBaselineSnapshot.h"

namespace dluma::fixes::custom_map_asset_isolation::curves {
constexpr std::uintptr_t kItemManagerRva=0x1E0EBD0;
constexpr std::uintptr_t kItemManagerVtableRva=0x161E3D8;
constexpr std::uint32_t kNullCurveId=UINT32_MAX;
constexpr std::size_t kItemIndexedCurveCount=6,kItemScalarCurveCount=5;
struct ItemCurveBindingSnapshot {
    std::uintptr_t owner_identity;
    std::uintptr_t vector_identity;
    std::uint32_t indexed_ids[kItemIndexedCurveCount];
    std::uint32_t scalar_ids[kItemScalarCurveCount];
    CurveReadFence fence;
    bool complete;
};
enum class CurveBindingResult : std::uint8_t {
    Succeeded, InvalidArgument, ProfileMismatch, AccessNotQuiescent,
    ReadFailed, ShapeChanged, UnknownCurveReference, ChangedDuringCapture,
    BaselineNotPreserved,
};
struct CurveBindingWrite {
    std::uintptr_t address;
    std::uintptr_t expected;
    std::uintptr_t desired;
};
struct ItemCurveBindingRestorePlan {
    std::uintptr_t owner_identity;
    std::uintptr_t vector_identity;
    CurveReadFence fence;
    CurveBindingWrite writes[kItemIndexedCurveCount+kItemScalarCurveCount];
    std::size_t count;
    bool prepared;
};
// Requires the joined native Curves/ItemManager writer and reader fence, not
// merely a scalar active count. Stores IDs/nulls, never borrowed Curve buffers.
CurveBindingResult CaptureItemCurveBindings(const NativeCurveAccess* access,
    const CurveSnapshot* curves,ItemCurveBindingSnapshot* output) noexcept;
// Produces an exact expected-old/desired pointer plan. It does not execute
// writes, detach/delete curves, or certify all dependent consumers released.
CurveBindingResult PlanItemCurveBindingRestore(const CurveSnapshot* baseline_curves,
    const ItemCurveBindingSnapshot* baseline_bindings,const CurveSnapshot* current_curves,
    const ItemCurveBindingSnapshot* current_bindings,ItemCurveBindingRestorePlan* plan) noexcept;
CurveBindingResult VerifyItemCurveBindings(const ItemCurveBindingSnapshot* baseline,
    const ItemCurveBindingSnapshot* current) noexcept;
}  // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
