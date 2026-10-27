#ifndef DLUMA_FIXES_CURVE_BORROW_BOUNDARY_H
#define DLUMA_FIXES_CURVE_BORROW_BOUNDARY_H
#include "../../dlw_game_frame_authority.h"
#include <atomic>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::curves {
enum class CurveBorrowResult : std::uint8_t {
    Succeeded, InvalidArgument, AuthorityConflict, NoActiveFrame,
    IncompleteFrame, WrongScope, ScopeActive, UnknownBorrow, ForeignBorrow,
};
struct CurveFrameStamp {
    std::uint64_t sequence,serialization_token,observed_qpc;
    std::uintptr_t game_subobject,game_primary;
    std::uint32_t thread_id;
};
enum class TypedCurveCaller : std::uint8_t { PlayerPreservation, PlayerVariablesReset, NativeSessionTeardown };
struct CurveNativeScope {
    std::uintptr_t tracker_identity;
    std::uint64_t serial,transaction;
    std::uint32_t thread_id,depth;
    TypedCurveCaller caller;
    bool active;
};
struct CurveBorrowBoundary {
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::uint64_t next_scope_serial=0;
    std::uint32_t active_scopes=0;
    CurveFrameStamp last_frame_borrow={};
    bool unknown_borrow=false;
    bool foreign_borrow=false;
    bool scope_lost=false;
};

// Uses the actual synchronized engine authority snapshot. A pre-original
// authority_sequence alone is never a completed-frame receipt.
CurveBorrowResult CaptureActiveCurveFrame(const DlumaGameFrameAuthorityState* authority,
    std::uint32_t thread_id,std::uint64_t qpc,CurveFrameStamp* output) noexcept;
// Only call for an exact-audited transient Curve* use. Retained member/vector
// outputs have separate ownership and must never be expired by this function.
CurveBorrowResult NoteTransientCurveBorrow(CurveBorrowBoundary* tracker,
    const DlumaGameFrameAuthorityState* authority,std::uint32_t thread_id,
    std::uint64_t qpc) noexcept;
void NoteUnknownCurveBorrow(CurveBorrowBoundary* tracker) noexcept;
CurveBorrowResult BeginTypedCurveNativeScope(CurveBorrowBoundary* tracker,
    const DlumaGameFrameAuthorityState* authority,std::uint32_t thread_id,
    std::uint64_t qpc,std::uint64_t transaction,TypedCurveCaller caller,
    CurveNativeScope* token) noexcept;
CurveBorrowResult EndTypedCurveNativeScope(CurveBorrowBoundary* tracker,
    std::uint32_t thread_id,CurveNativeScope* token) noexcept;
bool IsTypedCurveNativeScopeActive(CurveBorrowBoundary* tracker,std::uint32_t thread_id,
    const CurveNativeScope* token) noexcept;
// Checks completed trace timestamps, route identity, serialization, recursion,
// overlap and current-frame debt; permits supported scheduler thread migration.
CurveBorrowResult VerifyCurveBorrowBoundary(CurveBorrowBoundary* tracker,
    const DlumaGameFrameAuthorityState* authority) noexcept;
}  // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
