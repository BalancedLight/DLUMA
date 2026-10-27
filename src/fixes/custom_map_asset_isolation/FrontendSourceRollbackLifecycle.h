#ifndef DLUMA_FIXES_FRONTEND_SOURCE_ROLLBACK_LIFECYCLE_H
#define DLUMA_FIXES_FRONTEND_SOURCE_ROLLBACK_LIFECYCLE_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::lifecycle {

constexpr std::uintptr_t kOnDestroyUnloadReturnRva = 0x00431C83U;
constexpr std::uintptr_t kAfterFadeUnloadReturnRva = 0x00434DD6U;
constexpr std::size_t kMaximumOwnedReleaseReceivers = 16;

enum class FrontendRollbackPhase : std::uint8_t {
    Idle,
    OnDestroyActive,
    OwnedUnloadActive,
    OwnedUnloadCompleted,
    OriginalReturned,
    ResetInProgress,
    ResetComplete,
    RuntimePartial,
    RuntimeFailed,
};

enum class FrontendLookupOrder : std::uint8_t {
    NotObserved,
    BeforeReset,
    AfterReset,
};

struct FrontendSourceRollbackBarrier {
    std::uint32_t epoch;
    void* session;
    void* outgoing_level;
    std::uint32_t owner_thread_id;
    std::uint32_t depth;
    std::uint32_t owned_unload_depth;
    std::uint32_t release_enter_count;
    std::uint32_t release_exit_count;
    std::uint32_t unique_receiver_count;
    void* unique_receivers[kMaximumOwnedReleaseReceivers];

    FrontendRollbackPhase phase;
    FrontendLookupOrder first_frontend_lookup;

    bool on_destroy_unload_observed;
    bool outgoing_level_release_observed;
    bool receiver_overflow;
    bool release_mismatch;
    bool original_unload_returned;
    bool original_returned;
    bool binding_consumed;
    bool reset_started;
    bool reset_completed;
};

void Initialize(FrontendSourceRollbackBarrier* barrier) noexcept;

bool BeginOnDestroy(FrontendSourceRollbackBarrier* barrier,
                    std::uint32_t epoch, void* session,
                    void* outgoing_level,
                    std::uint32_t owner_thread_id) noexcept;

bool EnterNestedOnDestroy(FrontendSourceRollbackBarrier* barrier,
                          void* session, void* outgoing_level,
                          std::uint32_t thread_id) noexcept;

void LeaveNestedOnDestroy(FrontendSourceRollbackBarrier* barrier) noexcept;

bool ObserveOnDestroyOwnedUnload(FrontendSourceRollbackBarrier* barrier,
                                 std::uintptr_t caller_rva, void* session,
                                 void* active_level,
                                 std::uint32_t thread_id,
                                 bool shutdown_or_quiescing) noexcept;

bool CompleteOnDestroyOwnedUnload(
    FrontendSourceRollbackBarrier* barrier) noexcept;

bool BeginOwnedLevelRelease(FrontendSourceRollbackBarrier* barrier,
                            void* level, std::uint32_t thread_id,
                            bool shutdown_or_quiescing) noexcept;
bool CompleteOwnedLevelRelease(FrontendSourceRollbackBarrier* barrier,
                               void* level, std::uint32_t thread_id,
                               bool shutdown_or_quiescing) noexcept;

bool MarkOriginalReturned(FrontendSourceRollbackBarrier* barrier) noexcept;

bool ConsumeForReset(FrontendSourceRollbackBarrier* barrier) noexcept;

bool BeginReset(FrontendSourceRollbackBarrier* barrier) noexcept;

void CompleteReset(FrontendSourceRollbackBarrier* barrier,
                   bool succeeded,
                   bool runtime_failed = true) noexcept;

void MarkRuntimePartial(FrontendSourceRollbackBarrier* barrier) noexcept;
void MarkRuntimeFailed(FrontendSourceRollbackBarrier* barrier) noexcept;

FrontendLookupOrder ObserveFirstFrontendLookup(
    FrontendSourceRollbackBarrier* barrier) noexcept;

bool ReadyForReset(const FrontendSourceRollbackBarrier* barrier) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::lifecycle

#endif
