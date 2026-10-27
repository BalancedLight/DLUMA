#ifndef DLUMA_FIXES_CURVE_APPEND_ONLY_OWNER_H
#define DLUMA_FIXES_CURVE_APPEND_ONLY_OWNER_H
#include "CurveNativeGateway.h"
#include "ItemCurveBindings.h"
#include "CurveProcessCaches.h"
namespace dluma::fixes::custom_map_asset_isolation::curves {
using CurveCompareWriteFn=bool(*)(void*,std::uintptr_t,const void*,const void*,std::size_t) noexcept;
using CurveDeleteFn=bool(*)(void*,const CurveGateLease*,std::uintptr_t) noexcept;
struct CurveOwnerOperations {
    CurveNativeGateway* gateway;
    // Expected-value stores prevent overwriting a changed binding/vector. The
    // production implementation still requires the actual joined native gate.
    CurveCompareWriteFn compare_write;CurveDeleteFn destroy;void* user;
};
enum class CurveOwnerResult : std::uint8_t {
    Succeeded, InvalidArgument, GateRejected, SnapshotFailed, BaselineChanged,
    BindingFailed, OwnershipUnknown, BorrowersActive, WriteFailed, DeleteFailed,
    AlreadyMutated, WrongTransaction, CacheFailed,
};
struct CurveAppendOnlyOwner {
    CurveOwnerOperations operations={};
    CurveSnapshot baseline={},current={};ItemCurveBindingSnapshot baseline_bindings={};
    CurveProcessCacheSnapshot baseline_caches={},current_caches={};CurveProcessCachePlan cache_writes={};
    CurveGateLease lease={};ItemCurveBindingRestorePlan bindings={};
    std::uintptr_t detached[kMaximumCurves]={};std::size_t detached_count=0,retired_count=0;
    std::uintptr_t vector_identity=0;std::uint32_t current_count=0;
    std::uint64_t transaction=0,completed_transaction=0;CurveOwnerResult result=CurveOwnerResult::InvalidArgument;
    bool baseline_captured=false,prepared=false,mutation_started=false,verified=false;
};
bool InitializeCurveOwner(CurveAppendOnlyOwner*,const CurveOwnerOperations*,
    const CurveSnapshot* baseline_storage,const CurveSnapshot* working_storage) noexcept;
CurveOwnerResult CaptureCurveOwnerBaseline(CurveAppendOnlyOwner*) noexcept;
CurveOwnerResult PrepareCurveOwner(CurveAppendOnlyOwner*,std::uint64_t transaction) noexcept;
CurveOwnerResult CancelCurveOwner(CurveAppendOnlyOwner*) noexcept;
// Restores ItemManager pointers first, then removes only the owned appended
// tail. B0 objects, arrays and pointer-vector allocation remain intact.
CurveOwnerResult ResetCurveOwner(CurveAppendOnlyOwner*) noexcept;
CurveOwnerResult VerifyCurveOwner(CurveAppendOnlyOwner*) noexcept;
// Retires after actual borrower closure; failure retains every remaining object.
CurveOwnerResult RetireCurveOwnerTail(CurveAppendOnlyOwner*) noexcept;
CurveOwnerResult CompleteCurveOwner(CurveAppendOnlyOwner*) noexcept;
// Final transaction verification after other authenticated baseline reloads.
// Performs no stores or native deletion. With held_lease, the root owns release
// through OpenCurveGateway after its final commit; otherwise admission reopens.
CurveOwnerResult ReverifyCompletedCurveOwner(CurveAppendOnlyOwner*,std::uint64_t transaction,
    CurveGateLease* held_lease=nullptr) noexcept;
bool NativeCurveCompareWrite(void*,std::uintptr_t,const void*,const void*,std::size_t) noexcept;
bool NativeCurveDelete(void*,const CurveGateLease*,std::uintptr_t) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
