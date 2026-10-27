#define _HAS_EXCEPTIONS 0

#include "FrontendSourceRollbackLifecycle.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::lifecycle {

void Initialize(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier) return;
    std::memset(barrier, 0, sizeof(*barrier));
    barrier->phase = FrontendRollbackPhase::Idle;
    barrier->first_frontend_lookup =
        FrontendLookupOrder::NotObserved;
}

bool BeginOnDestroy(FrontendSourceRollbackBarrier* barrier,
                    std::uint32_t epoch, void* session,
                    void* outgoing_level,
                    std::uint32_t owner_thread_id) noexcept
{
    if (!barrier || !epoch || !session || !outgoing_level ||
        !owner_thread_id ||
        barrier->depth ||
        (barrier->phase != FrontendRollbackPhase::Idle &&
         barrier->phase != FrontendRollbackPhase::ResetComplete &&
         barrier->phase != FrontendRollbackPhase::RuntimePartial &&
         barrier->phase != FrontendRollbackPhase::RuntimeFailed))
        return false;

    Initialize(barrier);
    barrier->epoch = epoch;
    barrier->session = session;
    barrier->outgoing_level = outgoing_level;
    barrier->owner_thread_id = owner_thread_id;
    barrier->depth = 1;
    barrier->phase = FrontendRollbackPhase::OnDestroyActive;
    return true;
}

bool EnterNestedOnDestroy(FrontendSourceRollbackBarrier* barrier,
                          void* session, void* outgoing_level,
                          std::uint32_t thread_id) noexcept
{
    if (!barrier || !barrier->depth ||
        barrier->phase != FrontendRollbackPhase::OnDestroyActive ||
        thread_id != barrier->owner_thread_id ||
        barrier->session != session ||
        barrier->outgoing_level != outgoing_level)
        return false;
    ++barrier->depth;
    return true;
}

void LeaveNestedOnDestroy(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (barrier && barrier->depth > 1) --barrier->depth;
}

bool ObserveOnDestroyOwnedUnload(FrontendSourceRollbackBarrier* barrier,
                                 std::uintptr_t caller_rva, void* session,
                                 void* active_level,
                                 std::uint32_t thread_id,
                                 bool shutdown_or_quiescing) noexcept
{
    if (!barrier || shutdown_or_quiescing || !barrier->epoch ||
        !barrier->depth ||
        barrier->phase != FrontendRollbackPhase::OnDestroyActive ||
        caller_rva != kOnDestroyUnloadReturnRva ||
        thread_id != barrier->owner_thread_id ||
        session != barrier->session || !active_level ||
        active_level != barrier->outgoing_level)
        return false;
    barrier->on_destroy_unload_observed = true;
    ++barrier->owned_unload_depth;
    barrier->phase = FrontendRollbackPhase::OwnedUnloadActive;
    return true;
}

bool CompleteOnDestroyOwnedUnload(
    FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier || !barrier->owned_unload_depth ||
        barrier->phase != FrontendRollbackPhase::OwnedUnloadActive)
        return false;
    --barrier->owned_unload_depth;
    barrier->original_unload_returned = true;
    barrier->release_mismatch = barrier->release_enter_count == 0 ||
        barrier->release_enter_count != barrier->release_exit_count;
    barrier->outgoing_level_release_observed =
        !barrier->release_mismatch && !barrier->receiver_overflow &&
        barrier->unique_receiver_count != 0;
    barrier->phase = barrier->outgoing_level_release_observed
        ? FrontendRollbackPhase::OwnedUnloadCompleted
        : FrontendRollbackPhase::RuntimePartial;
    return barrier->outgoing_level_release_observed;
}

bool BeginOwnedLevelRelease(FrontendSourceRollbackBarrier* barrier,
                            void* level, std::uint32_t thread_id,
                            bool shutdown_or_quiescing) noexcept
{
    if (!barrier || shutdown_or_quiescing || !barrier->epoch ||
        !barrier->depth ||
        barrier->phase != FrontendRollbackPhase::OwnedUnloadActive ||
        !barrier->on_destroy_unload_observed ||
        !barrier->owned_unload_depth || !level ||
        thread_id != barrier->owner_thread_id)
        return false;
    ++barrier->release_enter_count;
    for (std::uint32_t index = 0;
         index < barrier->unique_receiver_count; ++index)
        if (barrier->unique_receivers[index] == level) return true;
    if (barrier->unique_receiver_count >=
        kMaximumOwnedReleaseReceivers) {
        barrier->receiver_overflow = true;
        return true;
    }
    barrier->unique_receivers[barrier->unique_receiver_count++] = level;
    return true;
}

bool CompleteOwnedLevelRelease(FrontendSourceRollbackBarrier* barrier,
                               void* level, std::uint32_t thread_id,
                               bool shutdown_or_quiescing) noexcept
{
    if (!barrier || shutdown_or_quiescing || !level ||
        barrier->phase != FrontendRollbackPhase::OwnedUnloadActive ||
        !barrier->owned_unload_depth ||
        thread_id != barrier->owner_thread_id ||
        barrier->release_exit_count >= barrier->release_enter_count) {
        if (barrier && barrier->owned_unload_depth)
            barrier->release_mismatch = true;
        return false;
    }
    ++barrier->release_exit_count;
    return true;
}

bool MarkOriginalReturned(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier || barrier->depth != 1 ||
        barrier->phase != FrontendRollbackPhase::OwnedUnloadCompleted)
        return false;
    barrier->original_returned = true;
    barrier->phase = FrontendRollbackPhase::OriginalReturned;
    return true;
}

bool ReadyForReset(const FrontendSourceRollbackBarrier* barrier) noexcept
{
    return barrier && barrier->epoch && barrier->depth == 1 &&
           barrier->phase == FrontendRollbackPhase::OriginalReturned &&
           barrier->original_returned &&
           barrier->original_unload_returned &&
           barrier->on_destroy_unload_observed &&
           barrier->outgoing_level_release_observed &&
           barrier->release_enter_count != 0 &&
           barrier->release_enter_count == barrier->release_exit_count &&
           barrier->unique_receiver_count != 0 &&
           !barrier->receiver_overflow && !barrier->release_mismatch &&
           !barrier->binding_consumed && !barrier->reset_started;
}

bool ConsumeForReset(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!ReadyForReset(barrier)) return false;
    barrier->binding_consumed = true;
    return true;
}

bool BeginReset(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier || !barrier->binding_consumed ||
        !barrier->epoch || barrier->depth != 1 ||
        barrier->phase != FrontendRollbackPhase::OriginalReturned ||
        !barrier->original_returned ||
        !barrier->original_unload_returned ||
        !barrier->on_destroy_unload_observed ||
        !barrier->outgoing_level_release_observed ||
        !barrier->release_enter_count ||
        barrier->release_enter_count != barrier->release_exit_count ||
        !barrier->unique_receiver_count || barrier->receiver_overflow ||
        barrier->release_mismatch ||
        barrier->reset_started)
        return false;
    barrier->reset_started = true;
    barrier->phase = FrontendRollbackPhase::ResetInProgress;
    return true;
}

void CompleteReset(FrontendSourceRollbackBarrier* barrier,
                   bool succeeded, bool runtime_failed) noexcept
{
    if (!barrier || !barrier->reset_started ||
        barrier->phase != FrontendRollbackPhase::ResetInProgress)
        return;
    barrier->reset_completed = succeeded;
    barrier->phase = succeeded
        ? FrontendRollbackPhase::ResetComplete
        : runtime_failed
            ? FrontendRollbackPhase::RuntimeFailed
            : FrontendRollbackPhase::RuntimePartial;
    barrier->depth = 0;
}

void MarkRuntimePartial(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier) return;
    barrier->binding_consumed = true;
    if (barrier->phase != FrontendRollbackPhase::RuntimeFailed)
        barrier->phase = FrontendRollbackPhase::RuntimePartial;
    barrier->depth = 0;
}

void MarkRuntimeFailed(FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier) return;
    barrier->binding_consumed = true;
    barrier->phase = FrontendRollbackPhase::RuntimeFailed;
    barrier->depth = 0;
}

FrontendLookupOrder ObserveFirstFrontendLookup(
    FrontendSourceRollbackBarrier* barrier) noexcept
{
    if (!barrier ||
        barrier->first_frontend_lookup !=
            FrontendLookupOrder::NotObserved)
        return FrontendLookupOrder::NotObserved;
    barrier->first_frontend_lookup =
        barrier->phase == FrontendRollbackPhase::ResetComplete &&
                barrier->reset_completed
            ? FrontendLookupOrder::AfterReset
            : FrontendLookupOrder::BeforeReset;
    if (barrier->first_frontend_lookup ==
        FrontendLookupOrder::BeforeReset)
        barrier->phase = FrontendRollbackPhase::RuntimeFailed;
    return barrier->first_frontend_lookup;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::lifecycle
