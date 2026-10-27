#define _HAS_EXCEPTIONS 0

#include "ContentDomainRestartRegistry.h"

#include <cstring>

extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {
namespace {

class RegistryGuard {
public:
    explicit RegistryGuard(ContentDomainRestartRegistry* registry) noexcept
        : registry_(registry), locked_(false)
    {
        if (registry_ &&
            dluma_atomic_compare_exchange(&registry_->lock, 1, 0) == 0)
            locked_ = true;
    }

    ~RegistryGuard()
    {
        if (locked_) dluma_atomic_exchange(&registry_->lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    ContentDomainRestartRegistry* registry_;
    bool locked_;
};

class TransactionGuard {
public:
    explicit TransactionGuard(ContentDomainRestartRegistry* registry) noexcept
        : registry_(registry), locked_(false)
    {
        if (registry_ && dluma_atomic_compare_exchange(
                &registry_->transaction_lock, 1, 0) == 0)
            locked_ = true;
    }

    ~TransactionGuard()
    {
        if (locked_)
            dluma_atomic_exchange(&registry_->transaction_lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    ContentDomainRestartRegistry* registry_;
    bool locked_;
};

void CopyText(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    if (input) {
        while (index + 1 < capacity && input[index]) {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = 0;
}

bool ValidDomain(ContentDomainId id) noexcept
{
    return static_cast<std::uint8_t>(id) <
        static_cast<std::uint8_t>(ContentDomainId::Count);
}

ContentDomainMask LoadTouchedMask(
    const ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return 0;
    return static_cast<ContentDomainMask>(dluma_atomic_compare_exchange(
        const_cast<volatile LONG*>(&registry->touched_mask), 0, 0));
}

ContentDomainMask RequiredMask(
    const ContentDomainRestartRegistry* registry) noexcept
{
    return registry
        ? registry->mandatory_mask | LoadTouchedMask(registry)
        : 0;
}

bool DomainRequired(const ContentDomainRestartRegistry* registry,
                    ContentDomainId id,
                    ContentDomainMask required_mask) noexcept
{
    return registry &&
        (required_mask & DomainBit(id)) != 0;
}

constexpr LONG kTouchTransactionIdle = 0;
constexpr LONG kTouchTransactionPreMutation = 1;
constexpr LONG kTouchTransactionMutating = 2;
constexpr LONG kTouchTransactionCommitting = 3;
constexpr LONG kTouchTransactionCommitConflicted = 4;

void LatchRuntimeFailure(ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return;
    dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
    dluma_atomic_exchange(&registry->admission_blocked, 1);
}

bool IncrementTouchRevision(
    ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return false;
    LONGLONG observed = _InterlockedCompareExchange64(
        &registry->touch_revision, 0, 0);
    for (;;) {
        if (observed == static_cast<LONGLONG>(
                            0x7FFFFFFFFFFFFFFFULL)) {
            LatchRuntimeFailure(registry);
            return false;
        }
        const LONGLONG desired = observed + 1;
        const LONGLONG previous = _InterlockedCompareExchange64(
            &registry->touch_revision, desired, observed);
        if (previous == observed) return true;
        observed = previous;
    }
}

bool IncrementRetainingObservationRevision(
    ContentDomainRestartRegistry* registry,
    LONGLONG* revision) noexcept
{
    if (revision) *revision = 0;
    if (!registry) return false;
    LONGLONG observed = _InterlockedCompareExchange64(
        &registry->retaining_observation_revision, 0, 0);
    for (;;) {
        if (observed == static_cast<LONGLONG>(
                            0x7FFFFFFFFFFFFFFFULL)) {
            LatchRuntimeFailure(registry);
            return false;
        }
        const LONGLONG desired = observed + 1;
        const LONGLONG previous = _InterlockedCompareExchange64(
            &registry->retaining_observation_revision,
            desired, observed);
        if (previous == observed) {
            if (revision) *revision = desired;
            return true;
        }
        observed = previous;
    }
}

bool CaptureStableRetainingObservationRevision(
    ContentDomainRestartRegistry* registry,
    LONGLONG* revision) noexcept
{
    if (revision) *revision = 0;
    if (!registry) return false;
    const LONGLONG before = _InterlockedCompareExchange64(
        &registry->retaining_observation_revision, 0, 0);
    const LONG active = dluma_atomic_compare_exchange(
        &registry->retaining_observation_active_calls, 0, 0);
    const LONGLONG after = _InterlockedCompareExchange64(
        &registry->retaining_observation_revision, 0, 0);
    if (active != 0 || before != after) return false;
    if (revision) *revision = after;
    return true;
}

bool RetainingObservationWindowMatchesPrepared(
    ContentDomainRestartRegistry* registry) noexcept
{
    LONGLONG revision = 0;
    return registry &&
        CaptureStableRetainingObservationRevision(
            registry, &revision) &&
        revision ==
            registry->prepared_retaining_observation_revision;
}

bool BeginTouchTransactionWindow(
    ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return false;
    dluma_atomic_exchange(&registry->late_touch_latched, 0);
    if (dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionPreMutation,
            kTouchTransactionIdle) != kTouchTransactionIdle)
        return false;
    registry->prepared_touch_revision = 0;
    registry->prepared_retaining_observation_revision = 0;
    return true;
}

void EndTouchTransactionWindow(
    ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return;
    dluma_atomic_exchange(&registry->late_touch_latched, 0);
    registry->prepared_touch_revision = 0;
    registry->prepared_retaining_observation_revision = 0;
    /* Idle is published last.  A producer cannot be admitted between the
     * transaction result publication and clearing the frozen window. */
    dluma_atomic_exchange(
        &registry->touch_transaction_state, kTouchTransactionIdle);
}

bool CaptureTouchTransactionSnapshot(
    ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry || dluma_atomic_compare_exchange(
            &registry->touch_transaction_state, 0, 0) !=
            kTouchTransactionPreMutation)
        return false;
    LONGLONG retaining_revision_before = 0;
    if (dluma_atomic_compare_exchange(
            &registry->late_touch_latched, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &registry->runtime_failure_latched, 0, 0) != 0 ||
        !CaptureStableRetainingObservationRevision(
            registry, &retaining_revision_before))
        return false;
    const LONGLONG touch_before = _InterlockedCompareExchange64(
        &registry->touch_revision, 0, 0);
    LONGLONG retaining_revision_after = 0;
    if (!CaptureStableRetainingObservationRevision(
            registry, &retaining_revision_after))
        return false;
    const LONGLONG touch_after = _InterlockedCompareExchange64(
        &registry->touch_revision, 0, 0);
    if (retaining_revision_before != retaining_revision_after ||
        touch_before != touch_after ||
        dluma_atomic_compare_exchange(
            &registry->late_touch_latched, 0, 0) != 0)
        return false;
    registry->prepared_touch_revision = touch_after;
    registry->prepared_retaining_observation_revision =
        retaining_revision_after;
    return true;
}

bool CommitTouchTransactionWindow(
    ContentDomainRestartRegistry* registry,
    ContentDomainMask required_mask) noexcept
{
    if (!registry ||
        dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionCommitting,
            kTouchTransactionMutating) != kTouchTransactionMutating) {
        LatchRuntimeFailure(registry);
        return false;
    }
    const bool clean =
        dluma_atomic_compare_exchange(
            &registry->late_touch_latched, 0, 0) == 0 &&
        dluma_atomic_compare_exchange(
            &registry->runtime_failure_latched, 0, 0) == 0 &&
        _InterlockedCompareExchange64(
            &registry->touch_revision, 0, 0) ==
            registry->prepared_touch_revision &&
        RetainingObservationWindowMatchesPrepared(registry) &&
        RequiredMask(registry) == required_mask;
    if (!clean) LatchRuntimeFailure(registry);
    return clean;
}

bool FinalizeCommittedTouchTransactionWindow(
    ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry || dluma_atomic_compare_exchange(
            &registry->touch_transaction_state, 0, 0) !=
            kTouchTransactionCommitting) {
        LatchRuntimeFailure(registry);
        return false;
    }
    /* Touches describe the outgoing epoch.  Clear them only after every
     * required phase committed successfully, while new producers are still
     * excluded by the committing state. */
    dluma_atomic_exchange(&registry->touched_mask, 0);
    dluma_atomic_exchange(&registry->late_touch_latched, 0);
    registry->prepared_touch_revision = 0;
    registry->prepared_retaining_observation_revision = 0;
    /* Publish Idle with a CAS.  A producer that raced the cleanup changes
     * Committing to CommitConflicted; never overwrite that evidence or
     * report a successful commit. */
    if (dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionIdle,
            kTouchTransactionCommitting) !=
            kTouchTransactionCommitting) {
        LatchRuntimeFailure(registry);
        return false;
    }
    return true;
}

RestartResult CheckTouchTransactionWindow(
    ContentDomainRestartRegistry* registry,
    ContentDomainMask required_mask,
    bool failure_is_post_mutation) noexcept
{
    if (!registry) return RestartResult::PreflightFailed;
    const LONG transaction_state = dluma_atomic_compare_exchange(
        &registry->touch_transaction_state, 0, 0);
    if (transaction_state == kTouchTransactionIdle &&
        registry->phase == RestartPhase::BaselineCaptured)
        return RestartResult::NotRun;
    if (dluma_atomic_compare_exchange(
            &registry->runtime_failure_latched, 0, 0) != 0) {
        registry->phase = RestartPhase::RuntimeFailed;
        LatchRuntimeFailure(registry);
        return RestartResult::RuntimeFailed;
    }
    if (dluma_atomic_compare_exchange(
            &registry->late_touch_latched, 0, 0) == 0 &&
        _InterlockedCompareExchange64(
            &registry->touch_revision, 0, 0) ==
            registry->prepared_touch_revision &&
        RetainingObservationWindowMatchesPrepared(registry) &&
        RequiredMask(registry) == required_mask)
        return RestartResult::NotRun;
    if (failure_is_post_mutation && registry->mutation_started) {
        registry->phase = RestartPhase::RuntimeFailed;
        LatchRuntimeFailure(registry);
        return RestartResult::RuntimeFailed;
    }
    return RestartResult::PreflightFailed;
}

class TouchTransactionPreparationGuard {
public:
    explicit TouchTransactionPreparationGuard(
        ContentDomainRestartRegistry* registry) noexcept
        : registry_(registry), started_(BeginTouchTransactionWindow(registry)),
          retained_(false)
    {}

    ~TouchTransactionPreparationGuard()
    {
        if (started_ && !retained_)
            EndTouchTransactionWindow(registry_);
    }

    bool started() const noexcept { return started_; }
    void Retain() noexcept { retained_ = true; }

private:
    ContentDomainRestartRegistry* registry_;
    bool started_;
    bool retained_;
};

void Publish(ContentDomainRestartRegistry* registry,
             RegistryEventType event, const ContentDomainAdapter& adapter,
             RestartPhase phase, DomainResult result,
             const RestartContext* context) noexcept
{
    if (registry && registry->event_sink)
        registry->event_sink(registry->event_user, event, adapter.id,
                             phase, result, context);
}

void PublishTransaction(ContentDomainRestartRegistry* registry,
                        DomainResult result,
                        const RestartContext* context) noexcept
{
    if (!registry || !registry->event_sink) return;
    registry->event_sink(registry->event_user,
                         RegistryEventType::TransactionResult,
                         ContentDomainId::FrontendVerification,
                         registry->phase, result, context);
}

bool StoreResult(ContentDomainRestartRegistry* registry,
                 std::uint32_t index, RestartPhase phase,
                 DomainResult result) noexcept
{
    if (!registry || index >= registry->count) return false;
    ContentDomainAdapterState& state = registry->states[index];
    switch (phase) {
    case RestartPhase::BaselineCaptured:
        state.baseline_result = result;
        ++state.baseline_calls;
        break;
    case RestartPhase::Preflighting:
        state.preflight_result = result;
        ++state.preflight_calls;
        break;
    case RestartPhase::Quiescing:
        state.quiesce_result = result;
        ++state.quiesce_calls;
        break;
    case RestartPhase::Cancelling:
        state.cancel_result = result;
        ++state.cancel_calls;
        break;
    case RestartPhase::Mutating:
        state.reset_result = result;
        ++state.reset_calls;
        break;
    case RestartPhase::RetiringProviders:
        state.retire_providers_result = result;
        ++state.retire_providers_calls;
        break;
    case RestartPhase::Reloading:
        state.reload_result = result;
        ++state.reload_calls;
        break;
    case RestartPhase::Verifying:
        state.verify_result = result;
        ++state.verify_calls;
        break;
    default:
        return false;
    }
    return true;
}

ContentDomainPhaseFn FunctionForPhase(
    const ContentDomainAdapter& adapter, RestartPhase phase) noexcept
{
    switch (phase) {
    case RestartPhase::BaselineCaptured: return adapter.capture_baseline;
    case RestartPhase::Preflighting: return adapter.preflight;
    case RestartPhase::Quiescing: return adapter.quiesce;
    case RestartPhase::Mutating: return adapter.reset;
    case RestartPhase::RetiringProviders: return adapter.retire_providers;
    case RestartPhase::Reloading: return adapter.reload;
    case RestartPhase::Verifying: return adapter.verify;
    default: return nullptr;
    }
}

struct ActiveDomainCallback {
    ContentDomainRestartRegistry* registry;
    const RestartContext* context;
    ContentDomainId domain;
    RestartPhase phase;
    std::uint32_t thread_id;
};
__declspec(thread) ActiveDomainCallback active_callback = {};
struct ActiveFinalizer {
    const ContentDomainRestartRegistry* registry;
    const RestartContext* context;
    const RegistryFinalizationToken* token;
};
__declspec(thread) ActiveFinalizer active_finalizer = {};

DomainResult InvokeDomainCallback(ContentDomainRestartRegistry* registry,
    ContentDomainAdapter& adapter, ContentDomainPhaseFn function,
    RestartPhase phase, const RestartContext* context) noexcept
{
    if (!function) return DomainResult::Unsupported;
    if (active_callback.registry) {
        LatchRuntimeFailure(registry);
        return DomainResult::Failed;
    }
    active_callback = {registry, context, adapter.id, phase, GetCurrentThreadId()};
    const auto result = function(adapter.user, context);
    const bool unfinished_replay = registry->replay_permit.active;
    active_callback = {};
    if (unfinished_replay) {
        registry->replay_permit.failed = true;
        LatchRuntimeFailure(registry);
        return DomainResult::Failed;
    }
    return result;
}

bool ReplayCallbackMatches(const ContentDomainRestartRegistry* registry, bool allow_failed = false) noexcept
{
    return registry && active_callback.registry == registry &&
        active_callback.context && active_callback.thread_id == GetCurrentThreadId() &&
        active_callback.context->owner_thread_id == active_callback.thread_id &&
        (active_callback.phase == RestartPhase::Mutating ||
         active_callback.phase == RestartPhase::Reloading) &&
        registry->phase == active_callback.phase && registry->transaction_prepared &&
        registry->mutation_started && (allow_failed || !registry->runtime_failure_latched) &&
        registry->prepared_transaction_generation == active_callback.context->transaction_generation &&
        registry->prepared_content_epoch == active_callback.context->content_epoch &&
        (registry->prepared_participating_mask & DomainBit(active_callback.domain)) != 0;
}

bool ValidReplayIdentity(const ReplayOperationIdentity& value) noexcept
{
    if (!ValidDomain(value.domain) || !value.operation_id || !value.ordinal || !value.owner ||
        !std::memchr(value.logical_path, 0, sizeof(value.logical_path))) return false;
    bool any_digest = false;
    for (auto byte : value.content_sha256) any_digest |= byte != 0;
    if (value.identity_kind == ReplayIdentityKind::FileContent)
        return value.provider && value.provider_generation && any_digest && value.logical_path[0];
    if (value.identity_kind != ReplayIdentityKind::InMemoryOwner) return false;
    return !value.provider && !value.provider_generation && !any_digest && !value.logical_path[0];
}

bool SameReplayIdentity(const ReplayOperationIdentity& a,
                        const ReplayOperationIdentity& b) noexcept
{
    return a.domain == b.domain && a.identity_kind == b.identity_kind &&
        a.operation_id == b.operation_id && a.ordinal == b.ordinal &&
        a.parent_ordinal == b.parent_ordinal && a.owner == b.owner &&
        a.provider == b.provider && a.provider_generation == b.provider_generation &&
        std::memcmp(a.content_sha256, b.content_sha256, sizeof(a.content_sha256)) == 0 &&
        std::strlen(a.logical_path) == std::strlen(b.logical_path) &&
        std::memcmp(a.logical_path, b.logical_path, std::strlen(a.logical_path)) == 0;
}

RegistryEventType EventForPhase(RestartPhase phase) noexcept
{
    switch (phase) {
    case RestartPhase::BaselineCaptured:
        return RegistryEventType::BaselineCapture;
    case RestartPhase::Preflighting:
        return RegistryEventType::Preflight;
    case RestartPhase::Quiescing:
        return RegistryEventType::Quiesce;
    case RestartPhase::Cancelling:
        return RegistryEventType::Cancel;
    case RestartPhase::Mutating:
        return RegistryEventType::Reset;
    case RestartPhase::RetiringProviders:
        return RegistryEventType::RetireProviders;
    case RestartPhase::Reloading:
        return RegistryEventType::Reload;
    case RestartPhase::Verifying:
        return RegistryEventType::Verify;
    default:
        return RegistryEventType::TransactionResult;
    }
}

DomainResult CancelQuiescedAdapters(
    ContentDomainRestartRegistry* registry,
    ContentDomainMask cancel_mask,
    const RestartContext* context) noexcept;

RestartResult RunQuiescePhase(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context,
    ContentDomainMask participation_mask,
    ContentDomainMask required_mask,
    ContentDomainMask* succeeded_mask,
    bool* optional_incomplete) noexcept
{
    if (!registry || !context) return RestartResult::PreflightFailed;
    registry->phase = RestartPhase::Quiescing;
    DomainResult raw_results[kMaximumContentDomainAdapters] = {};
    DomainResult final_results[kMaximumContentDomainAdapters] = {};
    ContentDomainMask raw_succeeded = 0;

    /* Dependents stop before the resources they consume.  The callback order
     * is therefore the reverse of reset/reload dependency order. */
    for (std::uint32_t ordered = registry->order_count;
         ordered != 0; --ordered) {
        const std::uint32_t index = registry->order[ordered - 1U];
        ContentDomainAdapter& adapter = registry->adapters[index];
        const ContentDomainMask bit = DomainBit(adapter.id);
        if (!adapter.enabled || (participation_mask & bit) == 0) continue;
        raw_results[index] = InvokeDomainCallback(registry, adapter,
            adapter.quiesce, RestartPhase::Quiescing, context);
        if (raw_results[index] == DomainResult::Succeeded)
            raw_succeeded |= bit;
    }

    ContentDomainMask phase_succeeded = 0;
    ContentDomainMask orphaned_quiesce = 0;
    bool mandatory_failed = false;
    /* Resolve dependency readiness in forward topological order after every
     * reverse-order callback has returned.  A successfully quiesced optional
     * dependent whose dependency failed is resumed immediately. */
    for (std::uint32_t ordered = 0; ordered < registry->order_count;
         ++ordered) {
        const std::uint32_t index = registry->order[ordered];
        ContentDomainAdapter& adapter = registry->adapters[index];
        const ContentDomainMask bit = DomainBit(adapter.id);
        if (!adapter.enabled || (participation_mask & bit) == 0) continue;
        DomainResult result = raw_results[index];
        const ContentDomainMask effective_dependencies =
            adapter.dependency_mask & participation_mask;
        if (result == DomainResult::Succeeded &&
            (effective_dependencies & phase_succeeded) !=
                effective_dependencies) {
            result = DomainResult::Unsupported;
            orphaned_quiesce |= bit;
        }
        final_results[index] = result;
        if (result == DomainResult::Succeeded) {
            phase_succeeded |= bit;
        } else if (DomainRequired(
                       registry, adapter.id, required_mask)) {
            mandatory_failed = true;
        } else if (optional_incomplete) {
            *optional_incomplete = true;
        }
    }

    /* Publish in actual callback order, while reporting the dependency-aware
     * final result rather than a provisional raw success. */
    for (std::uint32_t ordered = registry->order_count;
         ordered != 0; --ordered) {
        const std::uint32_t index = registry->order[ordered - 1U];
        ContentDomainAdapter& adapter = registry->adapters[index];
        const ContentDomainMask bit = DomainBit(adapter.id);
        if (!adapter.enabled || (participation_mask & bit) == 0) continue;
        (void)StoreResult(registry, index, RestartPhase::Quiescing,
                          final_results[index]);
        Publish(registry, RegistryEventType::Quiesce, adapter,
                RestartPhase::Quiescing, final_results[index], context);
    }

    if (orphaned_quiesce) {
        if (CancelQuiescedAdapters(registry, orphaned_quiesce, context) !=
            DomainResult::Succeeded) {
            if (succeeded_mask) *succeeded_mask = 0;
            return RestartResult::RuntimeFailed;
        }
        raw_succeeded &= ~orphaned_quiesce;
        registry->phase = RestartPhase::Quiescing;
    }
    if (mandatory_failed) {
        if (succeeded_mask) *succeeded_mask = raw_succeeded;
        return RestartResult::PreflightFailed;
    }
    if (succeeded_mask) *succeeded_mask = phase_succeeded;
    return optional_incomplete && *optional_incomplete
        ? RestartResult::RuntimePartial
        : RestartResult::Verified;
}

RestartResult RunPhase(ContentDomainRestartRegistry* registry,
                       RestartPhase phase,
                       const RestartContext* context,
                       ContentDomainMask participation_mask,
                       ContentDomainMask required_mask,
                       ContentDomainMask* succeeded_mask,
                       bool* optional_incomplete,
                       bool failure_is_post_mutation) noexcept
{
    if (!registry || !context) return RestartResult::PreflightFailed;
    if (phase == RestartPhase::Quiescing)
        return RunQuiescePhase(
            registry, context, participation_mask, required_mask,
            succeeded_mask,
            optional_incomplete);
    registry->phase = phase;
    ContentDomainMask phase_succeeded = 0;
    bool mandatory_failed = false;
    for (std::uint32_t ordered = 0; ordered < registry->order_count;
         ++ordered) {
        const std::uint32_t index = registry->order[ordered];
        ContentDomainAdapter& adapter = registry->adapters[index];
        const ContentDomainMask bit = DomainBit(adapter.id);
        if (!adapter.enabled || (participation_mask & bit) == 0) continue;
        const RestartResult before_touch = CheckTouchTransactionWindow(
            registry, required_mask, failure_is_post_mutation);
        if (before_touch != RestartResult::NotRun)
            return before_touch;
        ContentDomainPhaseFn function = FunctionForPhase(adapter, phase);
        /* Only provider-owning adapters participate in this phase. Other
         * domains have already released their consumers during reset. */
        if (phase == RestartPhase::RetiringProviders && !function) {
            phase_succeeded |= bit;
            continue;
        }
        DomainResult result = InvokeDomainCallback(registry, adapter,
            function, phase, context);
        const RestartResult after_callback_touch =
            CheckTouchTransactionWindow(
                registry, required_mask, failure_is_post_mutation);
        if (after_callback_touch != RestartResult::NotRun)
            return after_callback_touch;
        /* A callback can validate its own owner even when an earlier
         * dependency failed.  It cannot become transaction-ready unless
         * every declared dependency also succeeded in this phase. */
        const ContentDomainMask effective_dependencies =
            adapter.dependency_mask & participation_mask;
        if (result == DomainResult::Succeeded &&
            (effective_dependencies & phase_succeeded) !=
                effective_dependencies)
            result = DomainResult::Unsupported;
        (void)StoreResult(registry, index, phase, result);
        if (phase == RestartPhase::Verifying) {
            ContentDomainAdapterState& state = registry->states[index];
            state.verified_transaction_generation =
                result == DomainResult::Succeeded
                    ? context->transaction_generation : 0;
            state.verified_baseline_generation =
                result == DomainResult::Succeeded
                    ? context->baseline_generation : 0;
            state.verified_content_epoch =
                result == DomainResult::Succeeded
                    ? context->content_epoch : 0;
        }
        Publish(registry, EventForPhase(phase), adapter, phase, result,
                context);
        const RestartResult after_publish_touch =
            CheckTouchTransactionWindow(
                registry, required_mask, failure_is_post_mutation);
        if (after_publish_touch != RestartResult::NotRun)
            return after_publish_touch;

        if (result == DomainResult::Succeeded) {
            phase_succeeded |= bit;
            continue;
        }
        /* Once reset execution begins, every adapter in the prepared set is
         * part of the transaction.  A failure or newly unsupported owner is
         * fatal even when that adapter was optional at registration time. */
        if (failure_is_post_mutation) {
            if (succeeded_mask) *succeeded_mask = phase_succeeded;
            if (!registry->mutation_started)
                return RestartResult::PreflightFailed;
            registry->phase = RestartPhase::RuntimeFailed;
            dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
            dluma_atomic_exchange(&registry->admission_blocked, 1);
            return RestartResult::RuntimeFailed;
        }
        if (!DomainRequired(registry, adapter.id, required_mask)) {
            if (optional_incomplete) *optional_incomplete = true;
            continue;
        }
        mandatory_failed = true;
    }
    if (succeeded_mask) *succeeded_mask = phase_succeeded;
    if (mandatory_failed) return RestartResult::PreflightFailed;
    return optional_incomplete && *optional_incomplete
        ? RestartResult::RuntimePartial
        : RestartResult::Verified;
}

DomainResult CancelQuiescedAdapters(
    ContentDomainRestartRegistry* registry,
    ContentDomainMask cancel_mask,
    const RestartContext* context) noexcept
{
    if (!registry || !context) return DomainResult::Failed;
    registry->phase = RestartPhase::Cancelling;
    bool failed = false;
    /* Cancellation unwinds reverse-order quiesce, so dependencies resume
     * before the dependents that consume them. */
    for (std::uint32_t ordered = 0; ordered < registry->order_count;
         ++ordered) {
        const std::uint32_t index = registry->order[ordered];
        ContentDomainAdapter& adapter = registry->adapters[index];
        const ContentDomainMask bit = DomainBit(adapter.id);
        if (!adapter.enabled || (cancel_mask & bit) == 0) continue;
        if (!adapter.cancel_quiesce) continue;
        const DomainResult result =
            InvokeDomainCallback(registry, adapter, adapter.cancel_quiesce,
                RestartPhase::Cancelling, context);
        (void)StoreResult(
            registry, index, RestartPhase::Cancelling, result);
        Publish(registry, RegistryEventType::Cancel, adapter,
                RestartPhase::Cancelling, result, context);
        if (result != DomainResult::Succeeded) failed = true;
    }
    if (failed) {
        registry->phase = RestartPhase::RuntimeFailed;
        dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
        dluma_atomic_exchange(&registry->admission_blocked, 1);
        return DomainResult::Failed;
    }
    return DomainResult::Succeeded;
}

}  // namespace

void InitializeRegistry(ContentDomainRestartRegistry* registry) noexcept
{
    if (!registry) return;
    std::memset(registry, 0, sizeof(*registry));
    registry->phase = RestartPhase::Idle;
    registry->last_result = RestartResult::NotRun;
}

void SetRegistryEventSink(ContentDomainRestartRegistry* registry,
                          RegistryEventSinkFn sink, void* user) noexcept
{
    RegistryGuard guard(registry);
    if (!guard.locked() || registry->sealed) return;
    registry->event_sink = sink;
    registry->event_user = user;
}

bool ValidateAdapter(const ContentDomainAdapter* adapter) noexcept
{
    if (!adapter || !ValidDomain(adapter->id) || !adapter->name[0])
        return false;
    if (adapter->retire_providers &&
        adapter->id != ContentDomainId::VfsSources)
        return false;
    if (!adapter->enabled) return true;
    if (!adapter->capture_baseline || !adapter->preflight ||
        !adapter->quiesce || !adapter->reset || !adapter->reload ||
        !adapter->verify)
        return false;
    const ContentDomainMask valid_mask =
        (DomainBit(ContentDomainId::Count) - 1U);
    if ((adapter->dependency_mask & ~valid_mask) != 0 ||
        (adapter->dependency_mask & DomainBit(adapter->id)) != 0)
        return false;
    return true;
}

bool RegisterAdapter(ContentDomainRestartRegistry* registry,
                     const ContentDomainAdapter* adapter) noexcept
{
    RegistryGuard guard(registry);
    if (!guard.locked() || registry->sealed || !adapter ||
        registry->count >= kMaximumContentDomainAdapters ||
        !ValidateAdapter(adapter))
        return false;
    for (std::uint32_t index = 0; index < registry->count; ++index)
        if (registry->adapters[index].id == adapter->id)
            return false;
    const std::uint32_t index = registry->count++;
    registry->adapters[index] = *adapter;
    CopyText(registry->adapters[index].name,
             sizeof(registry->adapters[index].name), adapter->name);
    ContentDomainAdapterState& state = registry->states[index];
    std::memset(&state, 0, sizeof(state));
    state.id = adapter->id;
    state.enabled = adapter->enabled;
    state.mandatory = adapter->mandatory;
    state.destructive = adapter->destructive;
    state.definition_valid = true;
    CopyText(state.name, sizeof(state.name), adapter->name);
    if (adapter->enabled) {
        registry->enabled_mask |= DomainBit(adapter->id);
        if (adapter->mandatory)
            registry->mandatory_mask |= DomainBit(adapter->id);
    }
    return true;
}

bool SnapshotRegistryCallbackContext(const ContentDomainRestartRegistry* registry,
    const RestartContext** context, ContentDomainId* domain,
    RestartPhase* phase) noexcept
{
    if (context) *context = nullptr;
    if (domain) *domain = ContentDomainId::Count;
    if (phase) *phase = RestartPhase::Idle;
    if (!context || !domain || !phase || !registry ||
        active_callback.registry != registry || !active_callback.context ||
        active_callback.thread_id != GetCurrentThreadId() ||
        active_callback.context->owner_thread_id != active_callback.thread_id ||
        registry->phase != active_callback.phase) return false;
    *context = active_callback.context;
    *domain = active_callback.domain;
    *phase = active_callback.phase;
    return true;
}

bool SetRegistryFinalizer(ContentDomainRestartRegistry* registry,RegistryFinalizeFn fn,void* user) noexcept
{
    RegistryGuard guard(registry);
    if(!guard.locked() || registry->sealed || registry->finalize || !fn)return false;
    registry->finalize=fn;registry->finalize_user=user;return true;
}

bool RegistryFinalizationIsCurrent(const ContentDomainRestartRegistry* registry,
    const RestartContext* context,const RegistryFinalizationToken* token) noexcept
{
    if(!registry || !context || !token || active_finalizer.registry!=registry ||
       active_finalizer.context!=context || active_finalizer.token!=token ||
       token->registry_identity!=reinterpret_cast<std::uintptr_t>(registry) ||
       token->owner_thread_id!=GetCurrentThreadId() || context->owner_thread_id!=token->owner_thread_id ||
       !context->transaction_generation || context->transaction_generation!=token->transaction_generation ||
       !context->content_epoch || context->content_epoch!=token->content_epoch ||
       registry->phase!=RestartPhase::Verified || !registry->transaction_prepared ||
       !registry->mutation_started || registry->runtime_failure_latched ||
       registry->prepared_transaction_generation!=token->transaction_generation ||
       registry->prepared_content_epoch!=token->content_epoch ||
       registry->completed_transaction_generation!=token->transaction_generation ||
       RequiredMask(registry)!=token->required_mask ||
       (token->verified_mask&token->required_mask)!=token->required_mask)return false;
    return dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&registry->touch_transaction_state),0,0)==kTouchTransactionCommitting &&
        !dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&registry->retaining_observation_active_calls),0,0) &&
        _InterlockedCompareExchange64(const_cast<volatile LONGLONG*>(&registry->retaining_observation_revision),0,0)==token->retaining_revision;
}

bool SealRegistry(ContentDomainRestartRegistry* registry) noexcept
{
    RegistryGuard guard(registry);
    if (!guard.locked() || registry->sealed || !registry->count)
        return false;
    ContentDomainMask emitted = 0;
    std::uint32_t order_count = 0;
    while (order_count < registry->count) {
        bool progress = false;
        for (std::uint32_t index = 0; index < registry->count; ++index) {
            const ContentDomainAdapter& adapter = registry->adapters[index];
            const ContentDomainMask bit = DomainBit(adapter.id);
            if ((emitted & bit) != 0) continue;
            if (!adapter.enabled) {
                registry->order[order_count++] =
                    static_cast<std::uint8_t>(index);
                emitted |= bit;
                progress = true;
                continue;
            }
            if ((adapter.dependency_mask & ~registry->enabled_mask) != 0)
                return false;
            if ((adapter.dependency_mask & ~emitted) != 0) continue;
            registry->order[order_count++] =
                static_cast<std::uint8_t>(index);
            emitted |= bit;
            progress = true;
        }
        if (!progress) return false;
    }
    registry->order_count = order_count;
    registry->sealed = true;
    return true;
}

bool BeginRetainingObservation(
    ContentDomainRestartRegistry* registry, ContentDomainId id,
    RetainingObservationOrigin origin,
    RetainingObservationToken* token) noexcept
{
    if (token) std::memset(token, 0, sizeof(*token));
    if (!registry || !token || !registry->sealed || !ValidDomain(id) ||
        (origin != RetainingObservationOrigin::ExternalProducer &&
         origin != RetainingObservationOrigin::ResetOwnedReplay))
        return false;

    const LONG active = dluma_atomic_increment(
        &registry->retaining_observation_active_calls);
    if (active <= 0) {
        LatchRuntimeFailure(registry);
        return false;
    }
    token->registry_identity =
        reinterpret_cast<std::uintptr_t>(registry);
    token->domain = id;
    token->origin = origin;
    token->active = true;
    LONG transaction_state = dluma_atomic_compare_exchange(
        &registry->touch_transaction_state, 0, 0);
    if (transaction_state == kTouchTransactionCommitting) {
        const LONG previous = dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionCommitConflicted,
            kTouchTransactionCommitting);
        if (previous == kTouchTransactionCommitting)
            LatchRuntimeFailure(registry);
        transaction_state = previous == kTouchTransactionCommitting
            ? kTouchTransactionCommitConflicted : previous;
    }
    /* Adapter enablement does not exempt a producer from this global barrier.
     * Reserving a retain-capable call protects the transaction window, but
     * it is not evidence that custom content survived.  Exact owner/provider
     * classification promotes touched domains separately at BF8. */
    if (!IncrementRetainingObservationRevision(
            registry, &token->admission_revision)) {
        LatchRuntimeFailure(registry);
        return false;
    }
    transaction_state = dluma_atomic_compare_exchange(
        &registry->touch_transaction_state, 0, 0);
    token->admitted =
        transaction_state == kTouchTransactionIdle &&
        origin == RetainingObservationOrigin::ExternalProducer;
    if (origin == RetainingObservationOrigin::ResetOwnedReplay) {
        /* A future reset adapter must authenticate exact context, owner,
         * object/path and replay ordinal before this origin can bypass
         * normal late-producer handling. */
        LatchRuntimeFailure(registry);
        token->admitted = false;
    }
    return token->admitted;
}

bool BeginRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    const RestartContext* context, const ReplayOperationIdentity* operations,
    std::size_t count, RegistryReplayPermitToken* token) noexcept
{
    if (!ReplayCallbackMatches(registry) || !context || context != active_callback.context ||
        !operations || !token || token->generation || !count || count > kMaximumReplayOperations)
        return false;
    auto& permit = registry->replay_permit;
    if (permit.active || permit.failed || permit.generation == UINT64_MAX) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!ValidReplayIdentity(operations[i]) || operations[i].domain != active_callback.domain ||
            (!i && operations[i].parent_ordinal) ||
            (i && (operations[i].ordinal <= operations[i - 1].ordinal || !operations[i].parent_ordinal)))
            return false;
        if (i) {
            bool parent_found = false;
            for (std::size_t j = 0; j < i; ++j)
                parent_found |= operations[j].ordinal == operations[i].parent_ordinal;
            if (!parent_found) return false;
        }
    }
    const auto generation = permit.generation + 1;
    std::memset(&permit, 0, sizeof(permit));
    std::memcpy(permit.operations, operations, count * sizeof(*operations));
    permit.generation = generation;
    permit.transaction_generation = context->transaction_generation;
    permit.content_epoch = context->content_epoch;
    permit.owner_thread_id = active_callback.thread_id;
    permit.domain = active_callback.domain;
    permit.phase = active_callback.phase;
    permit.count = static_cast<std::uint32_t>(count);
    permit.active = true;
    *token = {reinterpret_cast<std::uintptr_t>(registry), generation, context->transaction_generation};
    return true;
}

bool CurrentRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    ContentDomainId domain, RegistryReplayPermitToken* token) noexcept
{
    if (token) *token = {};
    if (!token || !ReplayCallbackMatches(registry) || domain != active_callback.domain)
        return false;
    const auto& permit = registry->replay_permit;
    if (!permit.active || permit.failed || permit.domain != domain ||
        permit.owner_thread_id != GetCurrentThreadId() ||
        permit.phase != active_callback.phase ||
        permit.transaction_generation != active_callback.context->transaction_generation ||
        permit.content_epoch != active_callback.context->content_epoch) return false;
    *token = {reinterpret_cast<std::uintptr_t>(registry), permit.generation, permit.transaction_generation};
    return true;
}

bool BeginRetainingReplayObservation(ContentDomainRestartRegistry* registry,
    const RegistryReplayPermitToken* handle, const ReplayOperationIdentity* observed,
    RetainingObservationToken* token) noexcept
{
    if (!ReplayCallbackMatches(registry) || !handle || !observed || !token || token->active ||
        handle->registry_identity != reinterpret_cast<std::uintptr_t>(registry)) return false;
    auto& permit = registry->replay_permit;
    const bool identity_valid = ValidReplayIdentity(*observed);
    const bool valid = permit.active && !permit.failed && permit.generation == handle->generation &&
        permit.transaction_generation == handle->transaction_generation &&
        permit.transaction_generation == active_callback.context->transaction_generation &&
        permit.content_epoch == active_callback.context->content_epoch &&
        permit.owner_thread_id == GetCurrentThreadId() && permit.domain == active_callback.domain &&
        permit.phase == active_callback.phase && permit.entered < permit.count &&
        permit.depth < kMaximumReplayOperations && identity_valid &&
        SameReplayIdentity(permit.operations[permit.entered], *observed) &&
        ((!permit.depth && !observed->parent_ordinal) ||
         (permit.depth && observed->parent_ordinal ==
            permit.operations[permit.active_stack[permit.depth - 1]].ordinal));
    if (!valid) {
        permit.failed = true;
        LatchRuntimeFailure(registry);
        return false;
    }
    const auto index = permit.entered++;
    permit.active_stack[permit.depth++] = index;
    *token = {};
    token->registry_identity = reinterpret_cast<std::uintptr_t>(registry);
    token->domain = permit.domain;
    token->origin = RetainingObservationOrigin::ResetOwnedReplay;
    token->replay_permit_generation = permit.generation;
    token->replay_operation_index = index;
    token->active = true;
    token->admitted = dluma_atomic_increment(&registry->retaining_observation_active_calls) > 0;
    if (!token->admitted) { permit.failed = true; LatchRuntimeFailure(registry); }
    // Authenticated replay neither dirties B0 nor advances the external-producer
    // revision. Its exact native call is still reserved until complete return.
    return token->admitted;
}

bool ValidateReplayFileAuthority(ContentDomainRestartRegistry* registry,
    const ReplayFileAuthority* authority) noexcept
{
    if (!ReplayCallbackMatches(registry) || !authority ||
        authority->registry_identity != reinterpret_cast<std::uintptr_t>(registry)) return false;
    const auto& permit = registry->replay_permit;
    if (!permit.active || permit.failed || !permit.depth || permit.depth > kMaximumReplayOperations ||
        authority->permit_generation != permit.generation ||
        authority->transaction_generation != permit.transaction_generation ||
        authority->transaction_generation != active_callback.context->transaction_generation ||
        authority->content_epoch != permit.content_epoch ||
        authority->content_epoch != active_callback.context->content_epoch ||
        authority->owner_thread_id != GetCurrentThreadId() ||
        permit.owner_thread_id != authority->owner_thread_id || permit.domain != active_callback.domain ||
        permit.phase != active_callback.phase) return false;
    const auto index = permit.active_stack[permit.depth - 1];
    return index < permit.count && index < permit.entered &&
        authority->observation == static_cast<std::uint64_t>(index) + 1 &&
        permit.operations[index].identity_kind == ReplayIdentityKind::FileContent;
}

bool CaptureReplayFileAuthority(ContentDomainRestartRegistry* registry,
    const ReplayOperationIdentity* observed, ReplayFileAuthority* output) noexcept
{
    if (output) *output = {};
    if (!output || !observed || !ReplayCallbackMatches(registry) ||
        !ValidReplayIdentity(*observed) || observed->identity_kind != ReplayIdentityKind::FileContent) return false;
    const auto& permit = registry->replay_permit;
    if (!permit.active || permit.failed || !permit.depth || permit.depth > kMaximumReplayOperations) return false;
    const auto index = permit.active_stack[permit.depth - 1];
    if (index >= permit.count || index >= permit.entered || !SameReplayIdentity(*observed, permit.operations[index])) return false;
    const ReplayFileAuthority captured = {reinterpret_cast<std::uintptr_t>(registry), permit.generation,
        static_cast<std::uint64_t>(index) + 1, permit.transaction_generation, permit.content_epoch, permit.owner_thread_id};
    if (!ValidateReplayFileAuthority(registry, &captured)) return false;
    *output = captured;
    return true;
}

bool EndRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    RegistryReplayPermitToken* handle, bool native_succeeded) noexcept
{
    if (!ReplayCallbackMatches(registry, true) || !handle ||
        handle->registry_identity != reinterpret_cast<std::uintptr_t>(registry)) return false;
    auto& permit = registry->replay_permit;
    if (!permit.active || permit.generation != handle->generation ||
        permit.transaction_generation != handle->transaction_generation) return false;
    const bool success = native_succeeded && !permit.failed && !permit.depth &&
        permit.entered == permit.count && permit.completed == permit.count;
    permit.active = false;
    permit.failed = !success;
    *handle = {};
    if (!success) LatchRuntimeFailure(registry);
    return success;
}

bool EndRetainingObservation(
    ContentDomainRestartRegistry* registry,
    RetainingObservationToken* token) noexcept
{
    if (!registry || !token || !token->active ||
        token->registry_identity !=
            reinterpret_cast<std::uintptr_t>(registry)) {
        LatchRuntimeFailure(registry);
        return false;
    }
    if (token->replay_permit_generation) {
        auto& permit = registry->replay_permit;
        if (!ReplayCallbackMatches(registry, true) || !permit.active || !permit.depth ||
            permit.generation != token->replay_permit_generation ||
            permit.active_stack[permit.depth - 1] != token->replay_operation_index) {
            permit.failed = true;
            LatchRuntimeFailure(registry);
            return false;
        }
        --permit.depth;
        ++permit.completed;
    }
    /* Decrement last: a transaction that observes zero active calls must also
     * observe the pre-original reservation and post-original bookkeeping. */
    token->active = false;
    const LONG remaining = dluma_atomic_decrement(
        &registry->retaining_observation_active_calls);
    std::memset(token, 0, sizeof(*token));
    if (remaining < 0) {
        LatchRuntimeFailure(registry);
        return false;
    }
    return true;
}

bool MarkDomainTouched(ContentDomainRestartRegistry* registry,
                       ContentDomainId id) noexcept
{
    if (!registry || !registry->sealed || !ValidDomain(id)) return false;
    const ContentDomainMask bit = DomainBit(id);
    if ((registry->enabled_mask & bit) == 0) return false;
    const auto publish_touch = [&]() noexcept {
        LONG observed = dluma_atomic_compare_exchange(&registry->touched_mask, 0, 0);
        while ((observed & static_cast<LONG>(bit)) == 0) {
            const LONG previous = dluma_atomic_compare_exchange(
                &registry->touched_mask, observed | static_cast<LONG>(bit), observed);
            if (previous == observed) return;
            observed = previous;
        }
    };
    LONG transaction_state = dluma_atomic_compare_exchange(
        &registry->touch_transaction_state, 0, 0);
    if (transaction_state == kTouchTransactionCommitting) {
        const LONG previous = dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionCommitConflicted,
            kTouchTransactionCommitting);
        transaction_state = previous == kTouchTransactionCommitting
            ? kTouchTransactionCommitConflicted : previous;
    }
    publish_touch();
    if (!IncrementTouchRevision(registry)) return false;
    LONG after_publish = dluma_atomic_compare_exchange(
        &registry->touch_transaction_state, 0, 0);
    if (after_publish == kTouchTransactionCommitting) {
        const LONG previous = dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionCommitConflicted,
            kTouchTransactionCommitting);
        after_publish = previous == kTouchTransactionCommitting
            ? kTouchTransactionCommitConflicted : previous;
    }
    if (after_publish == kTouchTransactionIdle) {
        publish_touch();
        after_publish = dluma_atomic_compare_exchange(
            &registry->touch_transaction_state, 0, 0);
    }
    if (transaction_state == kTouchTransactionIdle)
        transaction_state = after_publish;
    if (transaction_state == kTouchTransactionPreMutation) {
        dluma_atomic_exchange(&registry->late_touch_latched, 1);
    } else if (transaction_state == kTouchTransactionMutating ||
               transaction_state == kTouchTransactionCommitting ||
               transaction_state == kTouchTransactionCommitConflicted) {
        dluma_atomic_exchange(&registry->late_touch_latched, 1);
        LatchRuntimeFailure(registry);
    }
    return true;
}

bool DomainWasTouched(const ContentDomainRestartRegistry* registry,
                      ContentDomainId id) noexcept
{
    return registry && ValidDomain(id) &&
        (LoadTouchedMask(registry) & DomainBit(id)) != 0;
}

RestartResult CaptureRegistryBaseline(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    TransactionGuard transaction(registry);
    if (!registry || !context || !registry->sealed ||
        !transaction.locked() || registry->runtime_failure_latched ||
        registry->baseline_captured)
        return RestartResult::PreflightFailed;
    bool optional_incomplete = false;
    ContentDomainMask baseline_ready = 0;
    const ContentDomainMask required_mask = RequiredMask(registry);
    LONGLONG observation_revision_before = 0;
    if (!CaptureStableRetainingObservationRevision(
            registry, &observation_revision_before))
        return RestartResult::PreflightFailed;
    const RestartResult result = RunPhase(
        registry, RestartPhase::BaselineCaptured, context,
        registry->enabled_mask, required_mask, &baseline_ready,
        &optional_incomplete, false);
    LONGLONG observation_revision_after = 0;
    const bool observation_window_stable =
        CaptureStableRetainingObservationRevision(
            registry, &observation_revision_after) &&
        observation_revision_before == observation_revision_after;
    if (result == RestartResult::PreflightFailed ||
        !observation_window_stable) {
        registry->baseline_ready_mask = 0;
        registry->phase = RestartPhase::Idle;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return result;
    }
    if ((baseline_ready & required_mask) != required_mask) {
        registry->baseline_ready_mask = 0;
        registry->phase = RestartPhase::Idle;
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return registry->last_result;
    }
    registry->baseline_ready_mask = baseline_ready;
    registry->baseline_captured = true;
    registry->phase = RestartPhase::BaselineCaptured;
    /* Optional adapters are captured proactively so a later exact touch can
     * promote them into the required set.  An unavailable adapter that is
     * not required must not downgrade the transaction baseline. */
    registry->last_result = RestartResult::Verified;
    PublishTransaction(registry, DomainResult::Succeeded, context);
    return registry->last_result;
}

RestartResult ExtendRegistryBaseline(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context,
    ContentDomainMask requested_mask) noexcept
{
    TransactionGuard transaction(registry);
    if (!registry || !context || !registry->sealed ||
        !transaction.locked() || !registry->baseline_captured ||
        registry->runtime_failure_latched ||
        registry->transaction_prepared || registry->mutation_started ||
        context->transaction_generation || context->content_epoch ||
        LoadTouchedMask(registry) != 0 ||
        dluma_atomic_compare_exchange(
            &registry->retaining_observation_active_calls, 0, 0) != 0)
        return RestartResult::PreflightFailed;

    const ContentDomainMask participation_mask =
        requested_mask & registry->enabled_mask &
        ~registry->baseline_ready_mask;
    if (!participation_mask) return RestartResult::Verified;
    /* Mandatory startup infrastructure must be immutable before optional
     * readiness can join its generation. */
    if ((registry->baseline_ready_mask & registry->mandatory_mask) !=
            registry->mandatory_mask)
        return RestartResult::PreflightFailed;

    LONGLONG observation_revision_before = 0;
    if (!CaptureStableRetainingObservationRevision(
            registry, &observation_revision_before))
        return RestartResult::PreflightFailed;
    bool optional_incomplete = false;
    ContentDomainMask newly_ready = 0;
    const RestartResult result = RunPhase(
        registry, RestartPhase::BaselineCaptured, context,
        participation_mask, 0, &newly_ready,
        &optional_incomplete, false);
    LONGLONG observation_revision_after = 0;
    const bool stable = CaptureStableRetainingObservationRevision(
            registry, &observation_revision_after) &&
        observation_revision_before == observation_revision_after &&
        LoadTouchedMask(registry) == 0;
    registry->phase = RestartPhase::BaselineCaptured;
    if (!stable || result == RestartResult::RuntimeFailed ||
        result == RestartResult::PreflightFailed) {
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(
            registry, DomainResult::Unsupported, context);
        return registry->last_result;
    }
    registry->baseline_ready_mask |= newly_ready;
    registry->last_result = newly_ready
        ? RestartResult::Verified : RestartResult::RuntimePartial;
    PublishTransaction(
        registry,
        newly_ready ? DomainResult::Succeeded
                    : DomainResult::Unsupported,
        context);
    return registry->last_result;
}

RestartResult RunRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    const RestartResult prepared =
        PrepareRegistryTransaction(registry, context);
    if (prepared == RestartResult::PreflightFailed ||
        prepared == RestartResult::RuntimeFailed)
        return prepared;
    return ExecutePreparedRegistryTransaction(registry, context);
}

RestartResult PrepareRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    TransactionGuard transaction(registry);
    if (!registry || !context || !registry->sealed ||
        !transaction.locked() || !registry->baseline_captured ||
        registry->runtime_failure_latched ||
        registry->transaction_prepared ||
        !context->transaction_generation || !context->content_epoch)
        return RestartResult::PreflightFailed;
    TouchTransactionPreparationGuard touch_window(registry);
    if (!touch_window.started())
        return RestartResult::PreflightFailed;
    registry->phase = RestartPhase::BarrierEntered;
    registry->mutation_started = false;
    if (!CaptureTouchTransactionSnapshot(registry)) {
        registry->phase = RestartPhase::BaselineCaptured;
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return registry->last_result;
    }
    const ContentDomainMask required_mask = RequiredMask(registry);
    if ((registry->baseline_ready_mask & required_mask) != required_mask) {
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return registry->last_result;
    }
    bool optional_incomplete = false;
    ContentDomainMask preflight_ready = 0;
    RestartResult result = RunPhase(
        registry, RestartPhase::Preflighting, context,
        required_mask, required_mask, &preflight_ready,
        &optional_incomplete, false);
    if (result == RestartResult::PreflightFailed) {
        registry->phase = RestartPhase::BaselineCaptured;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return result;
    }
    if (RequiredMask(registry) != required_mask ||
        CheckTouchTransactionWindow(
            registry, required_mask, false) !=
        RestartResult::NotRun) {
        registry->phase = RestartPhase::BaselineCaptured;
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return registry->last_result;
    }

    ContentDomainMask quiesce_ready = 0;
    result = RunPhase(registry, RestartPhase::Quiescing, context,
                      preflight_ready, required_mask, &quiesce_ready,
                      &optional_incomplete, false);
    if (result == RestartResult::RuntimeFailed) {
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Failed, context);
        return result;
    }
    if (result == RestartResult::PreflightFailed) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, quiesce_ready, context);
        if (cancelled != DomainResult::Succeeded) {
            registry->last_result = RestartResult::RuntimeFailed;
            PublishTransaction(registry, DomainResult::Failed, context);
            return registry->last_result;
        }
        registry->phase = RestartPhase::BaselineCaptured;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        return result;
    }
    if (RequiredMask(registry) != required_mask ||
        CheckTouchTransactionWindow(
            registry, required_mask, false) !=
        RestartResult::NotRun) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, quiesce_ready, context);
        registry->phase = cancelled == DomainResult::Succeeded
            ? RestartPhase::BaselineCaptured
            : RestartPhase::RuntimeFailed;
        registry->last_result = cancelled == DomainResult::Succeeded
            ? RestartResult::PreflightFailed
            : RestartResult::RuntimeFailed;
        PublishTransaction(
            registry,
            cancelled == DomainResult::Succeeded
                ? DomainResult::Unsupported
                : DomainResult::Failed,
            context);
        return registry->last_result;
    }
    if ((quiesce_ready & required_mask) != required_mask) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, quiesce_ready, context);
        registry->phase = cancelled == DomainResult::Succeeded
            ? RestartPhase::BaselineCaptured
            : RestartPhase::RuntimeFailed;
        registry->last_result = cancelled == DomainResult::Succeeded
            ? RestartResult::PreflightFailed
            : RestartResult::RuntimeFailed;
        PublishTransaction(
            registry,
            cancelled == DomainResult::Succeeded
                ? DomainResult::Unsupported
                : DomainResult::Failed,
            context);
        return registry->last_result;
    }

    registry->transaction_prepared = true;
    registry->prepared_participating_mask = quiesce_ready;
    registry->prepared_required_mask = required_mask;
    registry->prepared_optional_incomplete = optional_incomplete;
    registry->prepared_transaction_generation =
        context->transaction_generation;
    registry->prepared_content_epoch = context->content_epoch;
    registry->phase = RestartPhase::BarrierEntered;
    registry->last_result = optional_incomplete
        ? RestartResult::RuntimePartial
        : RestartResult::Verified;
    touch_window.Retain();
    return registry->last_result;
}

RestartResult ExecutePreparedRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    TransactionGuard transaction(registry);
    if (!registry || !context || !registry->sealed ||
        !transaction.locked() || !registry->baseline_captured ||
        registry->runtime_failure_latched ||
        !registry->transaction_prepared ||
        (registry->prepared_participating_mask &
             registry->prepared_required_mask) !=
            registry->prepared_required_mask ||
        registry->prepared_transaction_generation !=
            context->transaction_generation ||
        registry->prepared_content_epoch != context->content_epoch)
        return RestartResult::PreflightFailed;
    if (RequiredMask(registry) != registry->prepared_required_mask ||
        CheckTouchTransactionWindow(
            registry, registry->prepared_required_mask, false) !=
            RestartResult::NotRun) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, registry->prepared_participating_mask, context);
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->phase = cancelled == DomainResult::Succeeded
            ? RestartPhase::BaselineCaptured
            : RestartPhase::RuntimeFailed;
        registry->last_result = cancelled == DomainResult::Succeeded
            ? RestartResult::PreflightFailed
            : RestartResult::RuntimeFailed;
        if (cancelled != DomainResult::Succeeded) {
            dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
            dluma_atomic_exchange(&registry->admission_blocked, 1);
        }
        PublishTransaction(
            registry,
            cancelled == DomainResult::Succeeded
                ? DomainResult::Unsupported
                : DomainResult::Failed,
            context);
        EndTouchTransactionWindow(registry);
        return registry->last_result;
    }
    bool optional_incomplete = registry->prepared_optional_incomplete;
    ContentDomainMask phase_succeeded = 0;

    RestartResult result = RunPhase(
        registry, RestartPhase::Mutating, context,
        registry->prepared_participating_mask,
        registry->prepared_required_mask, &phase_succeeded,
        &optional_incomplete, true);
    if (result == RestartResult::PreflightFailed &&
        !registry->mutation_started) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, registry->prepared_participating_mask, context);
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        if (cancelled != DomainResult::Succeeded) {
            registry->phase = RestartPhase::RuntimeFailed;
            dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
            dluma_atomic_exchange(&registry->admission_blocked, 1);
            registry->last_result = RestartResult::RuntimeFailed;
            PublishTransaction(registry, DomainResult::Failed, context);
            EndTouchTransactionWindow(registry);
            return registry->last_result;
        }
        registry->phase = RestartPhase::BaselineCaptured;
        registry->last_result = RestartResult::PreflightFailed;
        PublishTransaction(registry, DomainResult::Unsupported, context);
        EndTouchTransactionWindow(registry);
        return registry->last_result;
    }
    if (result == RestartResult::RuntimeFailed) {
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return result;
    }
    if (!registry->mutation_started) {
        const DomainResult cancelled = CancelQuiescedAdapters(
            registry, registry->prepared_participating_mask, context);
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->phase = cancelled == DomainResult::Succeeded
            ? RestartPhase::BaselineCaptured
            : RestartPhase::RuntimeFailed;
        registry->last_result = cancelled == DomainResult::Succeeded
            ? RestartResult::PreflightFailed
            : RestartResult::RuntimeFailed;
        if (cancelled != DomainResult::Succeeded) {
            dluma_atomic_exchange(&registry->runtime_failure_latched, 1);
            dluma_atomic_exchange(&registry->admission_blocked, 1);
        }
        PublishTransaction(
            registry,
            cancelled == DomainResult::Succeeded
                ? DomainResult::Unsupported
                : DomainResult::Failed,
            context);
        EndTouchTransactionWindow(registry);
        return registry->last_result;
    }
    result = RunPhase(registry, RestartPhase::RetiringProviders, context,
                      registry->prepared_participating_mask,
                      registry->prepared_required_mask,
                      &phase_succeeded,
                      &optional_incomplete, true);
    if (result == RestartResult::RuntimeFailed) {
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return result;
    }
    result = RunPhase(registry, RestartPhase::Reloading, context,
                      registry->prepared_participating_mask,
                      registry->prepared_required_mask,
                      &phase_succeeded,
                      &optional_incomplete, true);
    if (result == RestartResult::RuntimeFailed) {
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return result;
    }
    result = RunPhase(registry, RestartPhase::Verifying, context,
                      registry->prepared_participating_mask,
                      registry->prepared_required_mask,
                      &phase_succeeded,
                      &optional_incomplete, true);
    if (result == RestartResult::RuntimeFailed) {
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->last_result = result;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return result;
    }

    if (!CommitTouchTransactionWindow(
            registry, registry->prepared_required_mask)) {
        registry->phase = RestartPhase::RuntimeFailed;
        registry->transaction_prepared = false;
        registry->prepared_optional_incomplete = false;
        registry->prepared_transaction_generation = 0;
        registry->prepared_content_epoch = 0;
        registry->prepared_participating_mask = 0;
        registry->prepared_required_mask = 0;
        registry->last_result = RestartResult::RuntimeFailed;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return registry->last_result;
    }
    const std::uint64_t previous_completed_generation =
        registry->completed_transaction_generation;
    registry->phase = RestartPhase::Verified;
    registry->completed_transaction_generation = context->transaction_generation;
    if(registry->finalize) {
        const RegistryFinalizationToken token={reinterpret_cast<std::uintptr_t>(registry),
            context->transaction_generation,context->content_epoch,context->owner_thread_id,
            registry->prepared_required_mask,phase_succeeded,registry->prepared_retaining_observation_revision};
        bool finalized=false;
        if(!active_finalizer.registry && !active_callback.registry) {
            active_finalizer={registry,context,&token};
            finalized=RegistryFinalizationIsCurrent(registry,context,&token) &&
                registry->finalize(registry->finalize_user,context,&token) &&
                RegistryFinalizationIsCurrent(registry,context,&token);
            active_finalizer={};
        }
        if(!finalized) {
            LatchRuntimeFailure(registry);
            registry->phase=RestartPhase::RuntimeFailed;
            registry->last_result=RestartResult::RuntimeFailed;
            registry->completed_transaction_generation=previous_completed_generation;
            registry->transaction_prepared=false;
            registry->prepared_optional_incomplete=false;
            registry->prepared_transaction_generation=0;
            registry->prepared_content_epoch=0;
            registry->prepared_participating_mask=registry->prepared_required_mask=0;
            PublishTransaction(registry,DomainResult::Failed,context);
            EndTouchTransactionWindow(registry);
            return registry->last_result;
        }
    }
    registry->transaction_prepared = false;
    registry->prepared_optional_incomplete = false;
    registry->prepared_transaction_generation = 0;
    registry->prepared_content_epoch = 0;
    registry->prepared_participating_mask = 0;
    registry->prepared_required_mask = 0;
    registry->last_result = optional_incomplete
        ? RestartResult::RuntimePartial
        : RestartResult::Verified;
    registry->completed_transaction_generation =
        context->transaction_generation;
    if (!FinalizeCommittedTouchTransactionWindow(registry)) {
        registry->phase = RestartPhase::RuntimeFailed;
        registry->last_result = RestartResult::RuntimeFailed;
        registry->completed_transaction_generation =
            previous_completed_generation;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return registry->last_result;
    }
    PublishTransaction(registry, DomainResult::Succeeded, context);
    return registry->last_result;
}

bool MarkRegistryMutationStarted(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    if (!registry || !context || !registry->transaction_prepared ||
        registry->phase != RestartPhase::Mutating ||
        registry->mutation_started ||
        registry->prepared_transaction_generation !=
            context->transaction_generation ||
        registry->prepared_content_epoch != context->content_epoch)
        return false;
    if (dluma_atomic_compare_exchange(
            &registry->touch_transaction_state,
            kTouchTransactionMutating,
            kTouchTransactionPreMutation) !=
        kTouchTransactionPreMutation)
        return false;
    if (dluma_atomic_compare_exchange(
            &registry->late_touch_latched, 0, 0) != 0 ||
        _InterlockedCompareExchange64(
            &registry->touch_revision, 0, 0) !=
            registry->prepared_touch_revision ||
        !RetainingObservationWindowMatchesPrepared(registry) ||
        RequiredMask(registry) != registry->prepared_required_mask ||
        dluma_atomic_compare_exchange(
            &registry->runtime_failure_latched, 0, 0) != 0) {
        /* A touch racing the state transition may already have observed the
         * irreversible window and latched RuntimeFailed.  Reopen the
         * cancellable state only when no such failure was published. */
        if (dluma_atomic_compare_exchange(
                &registry->runtime_failure_latched, 0, 0) == 0)
            (void)dluma_atomic_compare_exchange(
                &registry->touch_transaction_state,
                kTouchTransactionPreMutation,
                kTouchTransactionMutating);
        return false;
    }
    registry->mutation_started = true;
    return true;
}

bool CancelPreparedRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept
{
    TransactionGuard transaction(registry);
    if (!registry || !context || !transaction.locked() ||
        !registry->transaction_prepared || registry->mutation_started ||
        registry->prepared_transaction_generation !=
            context->transaction_generation ||
        registry->prepared_content_epoch != context->content_epoch)
        return false;
    const ContentDomainMask cancel_mask =
        registry->prepared_participating_mask;
    const DomainResult cancelled = CancelQuiescedAdapters(
        registry, cancel_mask, context);
    registry->transaction_prepared = false;
    registry->prepared_optional_incomplete = false;
    registry->prepared_transaction_generation = 0;
    registry->prepared_content_epoch = 0;
    registry->prepared_participating_mask = 0;
    registry->prepared_required_mask = 0;
    if (cancelled != DomainResult::Succeeded) {
        registry->last_result = RestartResult::RuntimeFailed;
        PublishTransaction(registry, DomainResult::Failed, context);
        EndTouchTransactionWindow(registry);
        return false;
    }
    registry->phase = RestartPhase::BaselineCaptured;
    registry->last_result = RestartResult::PreflightFailed;
    PublishTransaction(registry, DomainResult::Unsupported, context);
    EndTouchTransactionWindow(registry);
    return true;
}

std::size_t SnapshotRegistryStates(
    ContentDomainRestartRegistry* registry,
    ContentDomainAdapterState* output, std::size_t capacity) noexcept
{
    if (!registry || !output || !capacity) return 0;
    /* Adapter state is mutated under the transaction lock, not the
     * registration lock.  A non-blocking snapshot must therefore use the
     * same owner or report no data while a transaction is active. */
    TransactionGuard guard(registry);
    if (!guard.locked()) return 0;
    const std::size_t count = registry->count < capacity
        ? registry->count : capacity;
    std::memcpy(output, registry->states,
                sizeof(ContentDomainAdapterState) * count);
    const ContentDomainMask touched = LoadTouchedMask(registry);
    for (std::size_t index = 0; index < count; ++index)
        output[index].touched =
            (touched & DomainBit(output[index].id)) != 0;
    return count;
}

bool CanAdmitCustomProject(
    const ContentDomainRestartRegistry* registry) noexcept
{
    return registry && dluma_atomic_compare_exchange(
               const_cast<volatile LONG*>(
                   &registry->runtime_failure_latched), 0, 0) == 0 &&
           dluma_atomic_compare_exchange(
               const_cast<volatile LONG*>(
                   &registry->admission_blocked), 0, 0) == 0 &&
           dluma_atomic_compare_exchange(
               const_cast<volatile LONG*>(
                   &registry->touch_transaction_state), 0, 0) ==
               kTouchTransactionIdle &&
           dluma_atomic_compare_exchange(
               const_cast<volatile LONG*>(
                   &registry->retaining_observation_active_calls), 0, 0) ==
               0 &&
           !registry->transaction_prepared;
}

bool DomainRestoredInCurrentTransaction(
    const ContentDomainRestartRegistry* registry, ContentDomainId domain,
    const RestartContext* context) noexcept
{
    if (!registry || !context || !ValidDomain(domain) ||
        !context->baseline_generation || !context->transaction_generation ||
        !context->content_epoch || !registry->transaction_prepared ||
        !registry->mutation_started || registry->runtime_failure_latched ||
        registry->phase != RestartPhase::Verifying ||
        registry->prepared_transaction_generation != context->transaction_generation ||
        registry->prepared_content_epoch != context->content_epoch ||
        (registry->prepared_required_mask & DomainBit(domain)) == 0 ||
        (registry->prepared_participating_mask & DomainBit(domain)) == 0)
        return false;
    for (std::uint32_t i = 0; i < registry->count; ++i) {
        const ContentDomainAdapterState& state = registry->states[i];
        if (state.id != domain) continue;
        return state.reset_result == DomainResult::Succeeded &&
            state.reload_result == DomainResult::Succeeded &&
            state.verify_result == DomainResult::Succeeded &&
            state.verified_transaction_generation == context->transaction_generation &&
            state.verified_baseline_generation == context->baseline_generation &&
            state.verified_content_epoch == context->content_epoch;
    }
    return false;
}

const char* ContentDomainName(ContentDomainId id) noexcept
{
    switch (id) {
    case ContentDomainId::VfsSources: return "VfsSources";
    case ContentDomainId::PlayerGameplayParams:
        return "PlayerGameplayParams";
    case ContentDomainId::LocalizedTexts: return "LocalizedTexts";
    case ContentDomainId::Curves: return "Curves";
    case ContentDomainId::CharacterDefinitions:
        return "CharacterDefinitions";
    case ContentDomainId::AnimationEventDefinitions:
        return "AnimationEventDefinitions";
    case ContentDomainId::BuffDefinitions: return "BuffDefinitions";
    case ContentDomainId::LifePlaceDefinitions:
        return "LifePlaceDefinitions";
    case ContentDomainId::VehicleStateMachine:
        return "VehicleStateMachine";
    case ContentDomainId::CollectableDefinitions:
        return "CollectableDefinitions";
    case ContentDomainId::AppearanceDefinitions:
        return "AppearanceDefinitions";
    case ContentDomainId::ItemDefinitions: return "ItemDefinitions";
    case ContentDomainId::SkillDefinitions: return "SkillDefinitions";
    case ContentDomainId::QuestHubs: return "QuestHubs";
    case ContentDomainId::Quests: return "Quests";
    case ContentDomainId::ContentOnDemand: return "ContentOnDemand";
    case ContentDomainId::UnclassifiedPersistentConsumers:
        return "UnclassifiedPersistentConsumers";
    case ContentDomainId::Audio: return "Audio";
    case ContentDomainId::FrontendVerification:
        return "FrontendVerification";
    case ContentDomainId::NativeFrontendEntries:
        return "NativeFrontendEntries";
    case ContentDomainId::NativeMaterialDefinitions:
        return "NativeMaterialDefinitions";
    case ContentDomainId::NativeRetainedResources:
        return "NativeRetainedResources";
    default: return "Unknown";
    }
}

const char* RestartPhaseName(RestartPhase phase) noexcept
{
    switch (phase) {
    case RestartPhase::Idle: return "Idle";
    case RestartPhase::BaselineCaptured: return "BaselineCaptured";
    case RestartPhase::BarrierEntered: return "BarrierEntered";
    case RestartPhase::Preflighting: return "Preflighting";
    case RestartPhase::Quiescing: return "Quiescing";
    case RestartPhase::Cancelling: return "Cancelling";
    case RestartPhase::Mutating: return "Mutating";
    case RestartPhase::RetiringProviders: return "RetiringProviders";
    case RestartPhase::Reloading: return "Reloading";
    case RestartPhase::Verifying: return "Verifying";
    case RestartPhase::Verified: return "Verified";
    case RestartPhase::RuntimeFailed: return "RuntimeFailed";
    default: return "Unknown";
    }
}

const char* DomainResultName(DomainResult result) noexcept
{
    switch (result) {
    case DomainResult::NotRun: return "NotRun";
    case DomainResult::Succeeded: return "Succeeded";
    case DomainResult::Unsupported: return "Unsupported";
    case DomainResult::Failed: return "Failed";
    default: return "Unknown";
    }
}

const char* RestartResultName(RestartResult result) noexcept
{
    switch (result) {
    case RestartResult::NotRun: return "NotRun";
    case RestartResult::Verified: return "Verified";
    case RestartResult::RuntimePartial: return "RuntimePartial";
    case RestartResult::PreflightFailed: return "PreflightFailed";
    case RestartResult::RuntimeFailed: return "RuntimeFailed";
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot
