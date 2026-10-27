#define _HAS_EXCEPTIONS 0

#include "FrontendContentColdReboot.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {
namespace {

class QueueGuard {
public:
    explicit QueueGuard(ColdRebootCriticalQueue* queue) noexcept
        : queue_(queue), locked_(false)
    {
        if (queue_ &&
            dluma_atomic_compare_exchange(&queue_->lock, 1, 0) == 0)
            locked_ = true;
    }
    ~QueueGuard()
    {
        if (locked_) dluma_atomic_exchange(&queue_->lock, 0);
    }
    bool locked() const noexcept { return locked_; }
private:
    ColdRebootCriticalQueue* queue_;
    bool locked_;
};

class TransactionGuard {
public:
    explicit TransactionGuard(FrontendContentColdReboot* reboot) noexcept
        : reboot_(reboot), locked_(false)
    {
        if (reboot_ && dluma_atomic_compare_exchange(
                &reboot_->transaction_active, 1, 0) == 0)
            locked_ = true;
    }
    ~TransactionGuard()
    {
        if (locked_)
            dluma_atomic_exchange(&reboot_->transaction_active, 0);
    }
    bool locked() const noexcept { return locked_; }
private:
    FrontendContentColdReboot* reboot_;
    bool locked_;
};

std::uint64_t AtomicIncrement64(volatile LONGLONG* value) noexcept
{
    if (!value) return 0;
    LONGLONG observed = _InterlockedCompareExchange64(value, 0, 0);
    for (;;) {
        const LONGLONG desired = observed + 1;
        const LONGLONG previous = _InterlockedCompareExchange64(
            value, desired, observed);
        if (previous == observed)
            return static_cast<std::uint64_t>(desired);
        observed = previous;
    }
}

void AtomicSaturatingIncrement(volatile LONG* value) noexcept
{
    if (!value) return;
    LONG observed = dluma_atomic_compare_exchange(value, 0, 0);
    for (;;) {
        if (observed == 0x7FFFFFFFL) return;
        const LONG previous = dluma_atomic_compare_exchange(
            value, observed + 1, observed);
        if (previous == observed) return;
        observed = previous;
    }
}

void CopyFailureReason(char* output, std::size_t capacity,
                       const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    if (input) {
        while (index + 1U < capacity && input[index]) {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = '\0';
}

void StoreState(FrontendContentColdReboot* reboot,
                ColdRebootState state) noexcept
{
    if (reboot)
        dluma_atomic_exchange(&reboot->state, static_cast<LONG>(state));
}

void Emit(FrontendContentColdReboot* reboot,
          ColdRebootEventType type, const RestartContext* context,
          ContentDomainId domain = ContentDomainId::FrontendVerification,
          RestartPhase phase = RestartPhase::Idle,
          DomainResult result = DomainResult::NotRun,
          BarrierRejectReason reason = BarrierRejectReason::None,
          std::uint64_t value0 = 0,
          std::uint64_t value1 = 0) noexcept
{
    if (!reboot || !reboot->critical_queue) return;
    ColdRebootCriticalEvent event = {};
    event.baseline_generation = context
        ? context->baseline_generation : reboot->baseline_generation;
    event.transaction_generation = context
        ? context->transaction_generation
        : reboot->completed_transaction_generation;
    event.thread_id = context ? context->owner_thread_id : 0;
    event.content_epoch = context ? context->content_epoch : 0;
    event.type = type;
    event.domain = domain;
    event.phase = phase;
    event.domain_result = result;
    event.reason = reason;
    event.value0 = value0;
    event.value1 = value1;
    (void)PublishCriticalEvent(reboot->critical_queue,
                               &reboot->platform, &event);
}

void RegistryEventBridge(void* user, RegistryEventType event,
                         ContentDomainId domain, RestartPhase phase,
                         DomainResult result,
                         const RestartContext* context) noexcept
{
    FrontendContentColdReboot* reboot =
        static_cast<FrontendContentColdReboot*>(user);
    ColdRebootEventType type = ColdRebootEventType::ColdRebootPreflightDomain;
    switch (event) {
    case RegistryEventType::BaselineCapture:
        type = ColdRebootEventType::ColdRebootBaselineCaptured;
        break;
    case RegistryEventType::Preflight:
        type = ColdRebootEventType::ColdRebootPreflightDomain;
        break;
    case RegistryEventType::Quiesce:
        type = ColdRebootEventType::ColdRebootDomainQuiesce;
        break;
    case RegistryEventType::Cancel:
        type = ColdRebootEventType::ColdRebootDomainCancel;
        break;
    case RegistryEventType::Reset:
        type = ColdRebootEventType::ColdRebootDomainReset;
        StoreState(reboot, ColdRebootState::Mutating);
        break;
    case RegistryEventType::Reload:
        type = ColdRebootEventType::ColdRebootDomainReload;
        break;
    case RegistryEventType::Verify:
        type = ColdRebootEventType::ColdRebootDomainVerify;
        StoreState(reboot, ColdRebootState::Verifying);
        break;
    case RegistryEventType::TransactionResult:
        return;
    }
    Emit(reboot, type, context, domain, phase, result);
}

BarrierRejectReason ValidateBarrier(
    const FrontendContentColdReboot& reboot,
    const ColdRebootBarrierEvidence& evidence) noexcept
{
    if (!reboot.master_enabled) return BarrierRejectReason::Disabled;
    if (!reboot.baseline_captured)
        return BarrierRejectReason::BaselineUnavailable;
    if (!reboot.armed_epoch ||
        reboot.armed_epoch != evidence.content_epoch)
        return BarrierRejectReason::NotArmed;
    if (reboot.completed_epoch == evidence.content_epoch)
        return BarrierRejectReason::RepeatedEpoch;
    if (!reboot.preflight_prepared || !reboot.registry ||
        !reboot.registry->transaction_prepared ||
        !reboot.prepared_transaction_generation ||
        reboot.prepared_epoch != evidence.content_epoch)
        return BarrierRejectReason::PreflightNotPrepared;
    if (!evidence.exact_state_five_owner || evidence.state != 5U)
        return BarrierRejectReason::WrongState;
    if (!evidence.please_wait_active)
        return BarrierRejectReason::PleaseWaitInactive;
    if (!evidence.pipeline_wait_returned)
        return BarrierRejectReason::PipelineWaitIncomplete;
    if (!evidence.session_destroyed || evidence.outgoing_session)
        return BarrierRejectReason::SessionStillAlive;
    if (evidence.active_level)
        return BarrierRejectReason::ActiveLevelStillPresent;
    if (evidence.custom_player_di)
        return BarrierRejectReason::CustomPlayerStillPresent;
    if (!evidence.source_reset_ready)
        return BarrierRejectReason::SourceResetNotReady;
    if (evidence.next_project_staging_started)
        return BarrierRejectReason::NextProjectStagingStarted;
    if (!evidence.game_thread_id ||
        evidence.current_thread_id != evidence.game_thread_id)
        return BarrierRejectReason::WrongThread;
    if (evidence.shutdown_requested)
        return BarrierRejectReason::Shutdown;
    if (evidence.same_project_transition)
        return BarrierRejectReason::SameProjectTransition;
    return BarrierRejectReason::None;
}

BarrierRejectReason ValidatePreflight(
    const FrontendContentColdReboot& reboot,
    const ColdRebootPreflightEvidence& evidence) noexcept
{
    if (!reboot.master_enabled) return BarrierRejectReason::Disabled;
    if (!reboot.baseline_captured)
        return BarrierRejectReason::BaselineUnavailable;
    if (!reboot.armed_epoch ||
        reboot.armed_epoch != evidence.content_epoch)
        return BarrierRejectReason::NotArmed;
    if (reboot.completed_epoch == evidence.content_epoch)
        return BarrierRejectReason::RepeatedEpoch;
    if (reboot.preflight_prepared)
        return BarrierRejectReason::NestedTransaction;
    if (!evidence.exact_state_five_owner || evidence.state != 5U)
        return BarrierRejectReason::WrongState;
    if (!evidence.please_wait_active)
        return BarrierRejectReason::PleaseWaitInactive;
    if (!evidence.pipeline_wait_returned)
        return BarrierRejectReason::PipelineWaitIncomplete;
    if (evidence.next_project_staging_started)
        return BarrierRejectReason::NextProjectStagingStarted;
    if (!evidence.game_thread_id ||
        evidence.current_thread_id != evidence.game_thread_id)
        return BarrierRejectReason::WrongThread;
    if (evidence.shutdown_requested)
        return BarrierRejectReason::Shutdown;
    if (evidence.same_project_transition)
        return BarrierRejectReason::SameProjectTransition;
    return BarrierRejectReason::None;
}

}  // namespace

void InitializeCriticalQueue(ColdRebootCriticalQueue* queue) noexcept
{
    if (queue) std::memset(queue, 0, sizeof(*queue));
}

bool PublishCriticalEvent(ColdRebootCriticalQueue* queue,
                          const ColdRebootPlatform* platform,
                          const ColdRebootCriticalEvent* event) noexcept
{
    if (!queue || !event) return false;
    QueueGuard guard(queue);
    if (!guard.locked()) {
        AtomicSaturatingIncrement(&queue->dropped);
        dluma_atomic_exchange(&queue->overflow_latched, 1);
        return false;
    }
    if (queue->count >= kColdRebootCriticalEventCapacity) {
        dluma_atomic_exchange(&queue->overflow_latched, 1);
        AtomicSaturatingIncrement(&queue->dropped);
        return false;
    }
    ColdRebootCriticalEvent copy = *event;
    copy.sequence = AtomicIncrement64(&queue->next_sequence);
    if (!copy.qpc && platform && platform->read_qpc)
        copy.qpc = platform->read_qpc(platform->user);
    if (!copy.thread_id && platform && platform->read_thread_id)
        copy.thread_id = platform->read_thread_id(platform->user);
    const std::size_t index =
        (queue->head + queue->count) % kColdRebootCriticalEventCapacity;
    queue->records[index] = copy;
    ++queue->count;
    return true;
}

std::size_t DrainCriticalEvents(ColdRebootCriticalQueue* queue,
                                ColdRebootCriticalEvent* output,
                                std::size_t capacity) noexcept
{
    if (!queue || !output || !capacity) return 0;
    QueueGuard guard(queue);
    if (!guard.locked()) return 0;
    std::size_t written = 0;
    while (written < capacity && queue->count) {
        output[written++] = queue->records[queue->head];
        queue->head = (queue->head + 1U) %
            kColdRebootCriticalEventCapacity;
        --queue->count;
    }
    /* The synthetic record receives its sequence at drain time, so it must
     * follow every older retained record.  If the caller supplies a small
     * output buffer, keep the latch until a later drain empties the queue;
     * this preserves authoritative sequence order across drain cycles. */
    if (!queue->count && written < capacity) {
        const LONG dropped = dluma_atomic_exchange(&queue->dropped, 0);
        /* Producers intentionally do not wait for this lock.  Clear the
         * advisory latch, then restore it if a producer recorded a new loss
         * after our counter snapshot.  This prevents a drain from erasing a
         * concurrent loss notification. */
        dluma_atomic_exchange(&queue->overflow_latched, 0);
        if (dluma_atomic_compare_exchange(&queue->dropped, 0, 0) != 0)
            dluma_atomic_exchange(&queue->overflow_latched, 1);
        if (dropped > 0) {
            ColdRebootCriticalEvent overflow = {};
            overflow.sequence = AtomicIncrement64(&queue->next_sequence);
            overflow.type = ColdRebootEventType::ColdRebootCriticalOverflow;
            overflow.value0 = static_cast<std::uint64_t>(dropped);
            output[written++] = overflow;
        }
    }
    return written;
}

void InitializeColdReboot(FrontendContentColdReboot* reboot,
                          ContentDomainRestartRegistry* registry,
                          ColdRebootCriticalQueue* critical_queue,
                          const ColdRebootPlatform* platform,
                          bool master_enabled) noexcept
{
    if (!reboot) return;
    std::memset(reboot, 0, sizeof(*reboot));
    reboot->registry = registry;
    reboot->critical_queue = critical_queue;
    if (platform) reboot->platform = *platform;
    reboot->master_enabled = master_enabled;
    reboot->last_registry_result = RestartResult::NotRun;
    StoreState(reboot, master_enabled ? ColdRebootState::AwaitingBaseline
                                      : ColdRebootState::Disabled);
    if (registry && !registry->sealed)
        SetRegistryEventSink(registry, &RegistryEventBridge, reboot);
}

RestartResult CaptureColdRebootBaseline(
    FrontendContentColdReboot* reboot,
    const RestartContext* context) noexcept
{
    if (!reboot || !context || !reboot->master_enabled ||
        !reboot->registry || reboot->baseline_captured ||
        !context->baseline_generation)
        return RestartResult::PreflightFailed;
    const RestartResult result =
        CaptureRegistryBaseline(reboot->registry, context);
    reboot->last_registry_result = result;
    if (result == RestartResult::Verified ||
        result == RestartResult::RuntimePartial) {
        reboot->baseline_generation = context->baseline_generation;
        reboot->baseline_captured = true;
        StoreState(reboot, ColdRebootState::Armed);
        Emit(reboot, ColdRebootEventType::ColdRebootBaselineCaptured,
             context, ContentDomainId::FrontendVerification,
             RestartPhase::BaselineCaptured, DomainResult::Succeeded);
    }
    return result;
}

bool ArmColdRebootForEpoch(FrontendContentColdReboot* reboot,
                           std::uint32_t content_epoch) noexcept
{
    if (!reboot || !reboot->master_enabled || !reboot->baseline_captured ||
        !content_epoch || reboot->restart_required ||
        reboot->completed_epoch == content_epoch ||
        reboot->preflight_prepared ||
        dluma_atomic_compare_exchange(&reboot->transaction_active, 0, 0) != 0)
        return false;
    reboot->armed_epoch = content_epoch;
    reboot->barrier_seen = false;
    reboot->mutation_started = false;
    reboot->last_reject_reason = BarrierRejectReason::None;
    reboot->show_menu_guard_armed = true;
    StoreState(reboot, ColdRebootState::Armed);
    return true;
}

bool AbandonArmedColdRebootBeforeMutation(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch) noexcept
{
    if (!reboot || !content_epoch ||
        reboot->armed_epoch != content_epoch ||
        reboot->mutation_started || reboot->preflight_prepared ||
        reboot->restart_required || reboot->active_epoch_quarantined ||
        dluma_atomic_compare_exchange(
            &reboot->transaction_active, 0, 0) != 0)
        return false;
    reboot->armed_epoch = 0;
    reboot->show_menu_guard_armed = false;
    reboot->barrier_seen = false;
    reboot->prepared_transaction_generation = 0;
    reboot->prepared_epoch = 0;
    StoreState(reboot, reboot->baseline_captured
                           ? ColdRebootState::RuntimePartial
                           : reboot->master_enabled
                               ? ColdRebootState::AwaitingBaseline
                               : ColdRebootState::Disabled);
    return true;
}

bool QuarantineActiveEpochForCleanup(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch,
    const char* failure_reason) noexcept
{
    if (!reboot || !reboot->master_enabled ||
        !reboot->baseline_captured || !content_epoch ||
        reboot->restart_required || reboot->mutation_started ||
        reboot->completed_epoch == content_epoch ||
        (reboot->armed_epoch &&
         reboot->armed_epoch != content_epoch) ||
        (reboot->active_epoch_quarantined &&
         reboot->quarantined_epoch != content_epoch))
        return false;
    reboot->active_epoch_quarantined = true;
    reboot->transition_attestation_failure = true;
    reboot->new_project_admission_blocked = true;
    reboot->cleanup_still_authorized = true;
    reboot->quarantined_epoch = content_epoch;
    reboot->show_menu_guard_armed = true;
    CopyFailureReason(
        reboot->transition_attestation_failure_reason,
        sizeof(reboot->transition_attestation_failure_reason),
        failure_reason ? failure_reason
                       : "change_after_fade_attestation_failed");
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.content_epoch = content_epoch;
    Emit(reboot,
         ColdRebootEventType::TransitionAttestationQuarantined,
         &context, ContentDomainId::FrontendVerification,
         RestartPhase::Idle, DomainResult::Unsupported,
         BarrierRejectReason::TransitionAttestationFailed);
    return true;
}

bool NoteQuarantinedCleanupVerified(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch) noexcept
{
    if (!reboot || !content_epoch ||
        !reboot->active_epoch_quarantined ||
        reboot->quarantined_epoch != content_epoch ||
        reboot->completed_epoch != content_epoch ||
        !reboot->completed_transaction_generation ||
        reboot->restart_required)
        return false;
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.transaction_generation =
        reboot->completed_transaction_generation;
    context.content_epoch = content_epoch;
    reboot->active_epoch_quarantined = false;
    reboot->transition_attestation_failure = false;
    reboot->new_project_admission_blocked = false;
    reboot->cleanup_still_authorized = false;
    reboot->quarantined_epoch = 0;
    Emit(reboot, ColdRebootEventType::TransitionQuarantineCleared,
         &context, ContentDomainId::FrontendVerification,
         RestartPhase::Verified, DomainResult::Succeeded);
    return true;
}

ColdRebootOutcome PrepareColdRebootAtStateFive(
    FrontendContentColdReboot* reboot,
    const ColdRebootPreflightEvidence* evidence) noexcept
{
    if (!reboot || !evidence) return ColdRebootOutcome::Rejected;
    const BarrierRejectReason rejection =
        ValidatePreflight(*reboot, *evidence);
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.content_epoch = evidence->content_epoch;
    context.owner_thread_id = evidence->current_thread_id;
    if (rejection != BarrierRejectReason::None) {
        reboot->last_reject_reason = rejection;
        Emit(reboot, ColdRebootEventType::ColdRebootBarrierRejected,
             &context, ContentDomainId::FrontendVerification,
             RestartPhase::Preflighting, DomainResult::Unsupported,
             rejection, evidence->state, evidence->caller_return_rva);
        if (rejection == BarrierRejectReason::Disabled)
            return ColdRebootOutcome::NotApplicable;
        LatchRestartRequired(
            reboot, rejection, evidence->content_epoch);
        return ColdRebootOutcome::Rejected;
    }
    RestartResult result = RestartResult::PreflightFailed;
    {
        TransactionGuard transaction(reboot);
        if (!transaction.locked()) {
            reboot->last_reject_reason =
                BarrierRejectReason::NestedTransaction;
            Emit(reboot, ColdRebootEventType::ColdRebootBarrierRejected,
                 &context, ContentDomainId::FrontendVerification,
                 RestartPhase::Preflighting, DomainResult::Unsupported,
                 BarrierRejectReason::NestedTransaction,
                 evidence->state, evidence->caller_return_rva);
            LatchRestartRequired(
                reboot, BarrierRejectReason::NestedTransaction,
                evidence->content_epoch);
            return ColdRebootOutcome::Rejected;
        }
        context.transaction_generation =
            AtomicIncrement64(&reboot->next_transaction_generation);
        context.user = reboot;
        StoreState(reboot, ColdRebootState::Preflighting);
        Emit(reboot, ColdRebootEventType::ColdRebootPreflightBegin,
             &context, ContentDomainId::FrontendVerification,
             RestartPhase::Preflighting);
        result = PrepareRegistryTransaction(reboot->registry, &context);
    }
    reboot->last_registry_result = result;
    if (result == RestartResult::RuntimeFailed) {
        reboot->last_reject_reason =
            BarrierRejectReason::QuiesceCancelFailure;
        reboot->restart_required = true;
        StoreState(reboot, ColdRebootState::RuntimeFailed);
        Emit(reboot, ColdRebootEventType::ColdRebootFailed, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::RuntimeFailed, DomainResult::Failed,
             BarrierRejectReason::QuiesceCancelFailure);
        return ColdRebootOutcome::RuntimeFailed;
    }
    if (result == RestartResult::PreflightFailed) {
        reboot->last_reject_reason =
            BarrierRejectReason::MandatoryPreflightFailed;
        Emit(reboot, ColdRebootEventType::ColdRebootFailed, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::Preflighting, DomainResult::Unsupported,
             BarrierRejectReason::MandatoryPreflightFailed);
        LatchRestartRequired(
            reboot, BarrierRejectReason::MandatoryPreflightFailed,
            evidence->content_epoch);
        return ColdRebootOutcome::PreflightFailed;
    }
    reboot->prepared_transaction_generation =
        context.transaction_generation;
    reboot->prepared_epoch = evidence->content_epoch;
    reboot->preflight_prepared = true;
    StoreState(reboot, ColdRebootState::BarrierEntered);
    return result == RestartResult::RuntimePartial
        ? ColdRebootOutcome::RuntimePartial
        : ColdRebootOutcome::Verified;
}

ColdRebootOutcome EnterColdRebootBarrier(
    FrontendContentColdReboot* reboot,
    const ColdRebootBarrierEvidence* evidence) noexcept
{
    if (!reboot || !evidence) return ColdRebootOutcome::Rejected;
    const BarrierRejectReason rejection = ValidateBarrier(*reboot, *evidence);
    if (rejection != BarrierRejectReason::None) {
        reboot->last_reject_reason = rejection;
        RestartContext rejected = {};
        rejected.baseline_generation = reboot->baseline_generation;
        rejected.transaction_generation =
            reboot->prepared_transaction_generation;
        rejected.content_epoch = evidence->content_epoch;
        rejected.owner_thread_id = evidence->current_thread_id;
        bool cancelled = true;
        if (reboot->preflight_prepared &&
            reboot->prepared_epoch == evidence->content_epoch) {
            cancelled = CancelPreparedRegistryTransaction(
                reboot->registry, &rejected);
            reboot->preflight_prepared = false;
            reboot->prepared_transaction_generation = 0;
            reboot->prepared_epoch = 0;
        }
        if (!cancelled) {
            reboot->last_reject_reason =
                BarrierRejectReason::QuiesceCancelFailure;
            reboot->restart_required = true;
            StoreState(reboot, ColdRebootState::RuntimeFailed);
            Emit(reboot, ColdRebootEventType::ColdRebootFailed,
                 &rejected, ContentDomainId::FrontendVerification,
                 RestartPhase::RuntimeFailed, DomainResult::Failed,
                 BarrierRejectReason::QuiesceCancelFailure);
            return ColdRebootOutcome::RuntimeFailed;
        }
        Emit(reboot, ColdRebootEventType::ColdRebootBarrierRejected,
             &rejected, ContentDomainId::FrontendVerification,
             RestartPhase::Idle, DomainResult::Unsupported, rejection,
             evidence->state, evidence->caller_return_rva);
        if (rejection == BarrierRejectReason::Disabled)
            return ColdRebootOutcome::NotApplicable;
        LatchRestartRequired(
            reboot, rejection, evidence->content_epoch);
        return ColdRebootOutcome::Rejected;
    }

    RestartContext context = {};
    RestartResult result = RestartResult::PreflightFailed;
    {
        TransactionGuard transaction(reboot);
        if (!transaction.locked()) {
            reboot->last_reject_reason =
                BarrierRejectReason::NestedTransaction;
            RestartContext nested = {};
            nested.baseline_generation = reboot->baseline_generation;
            nested.content_epoch = evidence->content_epoch;
            nested.owner_thread_id = evidence->current_thread_id;
            Emit(reboot, ColdRebootEventType::ColdRebootBarrierRejected,
                 &nested, ContentDomainId::FrontendVerification,
                 RestartPhase::BarrierEntered, DomainResult::Unsupported,
                 BarrierRejectReason::NestedTransaction);
            LatchRestartRequired(
                reboot, BarrierRejectReason::NestedTransaction,
                evidence->content_epoch);
            return ColdRebootOutcome::Rejected;
        }

        context.baseline_generation = reboot->baseline_generation;
        context.transaction_generation = reboot->preflight_prepared
            ? reboot->prepared_transaction_generation
            : AtomicIncrement64(&reboot->next_transaction_generation);
        context.content_epoch = evidence->content_epoch;
        context.owner_thread_id = evidence->current_thread_id;
        context.user = reboot;
        reboot->barrier_seen = true;
        reboot->mutation_started = false;
        reboot->last_reject_reason = BarrierRejectReason::None;
        StoreState(reboot, ColdRebootState::BarrierEntered);
        Emit(reboot, ColdRebootEventType::ColdRebootBarrierEnter, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::BarrierEntered, DomainResult::Succeeded,
             BarrierRejectReason::None, evidence->state,
             evidence->caller_return_rva);
        StoreState(reboot, ColdRebootState::Preflighting);
        Emit(reboot, ColdRebootEventType::ColdRebootPreflightBegin,
             &context, ContentDomainId::FrontendVerification,
             RestartPhase::Preflighting);

        StoreState(reboot, ColdRebootState::Mutating);
        Emit(reboot, ColdRebootEventType::ColdRebootMutationBegin,
             &context, ContentDomainId::FrontendVerification,
             RestartPhase::Mutating, DomainResult::Succeeded);
        result = ExecutePreparedRegistryTransaction(
            reboot->registry, &context);
        reboot->preflight_prepared = false;
        reboot->prepared_transaction_generation = 0;
        reboot->prepared_epoch = 0;
        reboot->last_registry_result = result;
        reboot->mutation_started = reboot->registry->mutation_started;
        reboot->completed_transaction_generation =
            context.transaction_generation;
    }
    if (result == RestartResult::PreflightFailed) {
        if (reboot->mutation_started) {
            reboot->last_reject_reason =
                BarrierRejectReason::PostMutationFailure;
            reboot->restart_required = true;
            StoreState(reboot, ColdRebootState::RuntimeFailed);
            Emit(reboot, ColdRebootEventType::ColdRebootFailed,
                 &context, ContentDomainId::FrontendVerification,
                 RestartPhase::RuntimeFailed, DomainResult::Failed,
                 BarrierRejectReason::PostMutationFailure);
            return ColdRebootOutcome::RuntimeFailed;
        }
        reboot->last_reject_reason =
            BarrierRejectReason::MandatoryPreflightFailed;
        Emit(reboot, ColdRebootEventType::ColdRebootFailed, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::Preflighting, DomainResult::Unsupported,
             BarrierRejectReason::MandatoryPreflightFailed);
        LatchRestartRequired(
            reboot, BarrierRejectReason::MandatoryPreflightFailed,
            evidence->content_epoch);
        return ColdRebootOutcome::PreflightFailed;
    }
    if (result == RestartResult::RuntimeFailed) {
        const BarrierRejectReason failure_reason =
            reboot->mutation_started
                ? BarrierRejectReason::PostMutationFailure
                : BarrierRejectReason::QuiesceCancelFailure;
        reboot->last_reject_reason = failure_reason;
        reboot->restart_required = true;
        StoreState(reboot, ColdRebootState::RuntimeFailed);
        Emit(reboot, ColdRebootEventType::ColdRebootFailed, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::RuntimeFailed, DomainResult::Failed,
             failure_reason);
        return ColdRebootOutcome::RuntimeFailed;
    }

    reboot->completed_epoch = evidence->content_epoch;
    reboot->show_menu_guard_armed = false;
    if (result == RestartResult::RuntimePartial) {
        StoreState(reboot, ColdRebootState::RuntimePartial);
        Emit(reboot, ColdRebootEventType::ColdRebootVerified, &context,
             ContentDomainId::FrontendVerification,
             RestartPhase::Verified, DomainResult::Succeeded,
             BarrierRejectReason::None, 1, 0);
        return ColdRebootOutcome::RuntimePartial;
    }
    StoreState(reboot, ColdRebootState::Verified);
    if (reboot->active_epoch_quarantined)
        (void)NoteQuarantinedCleanupVerified(
            reboot, evidence->content_epoch);
    Emit(reboot, ColdRebootEventType::ColdRebootVerified, &context,
         ContentDomainId::FrontendVerification,
         RestartPhase::Verified, DomainResult::Succeeded);
    return ColdRebootOutcome::Verified;
}

ShowMenuMainDisposition ObserveShowMenuMain(
    FrontendContentColdReboot* reboot, std::uint32_t content_epoch,
    std::uint32_t current_thread_id) noexcept
{
    FrontendCompletionEvidence evidence = {};
    evidence.armed_epoch = reboot
        ? static_cast<std::uint32_t>(reboot->armed_epoch) : 0;
    evidence.prepared_epoch = reboot ? reboot->prepared_epoch : 0;
    evidence.completed_epoch = reboot
        ? static_cast<std::uint32_t>(reboot->completed_epoch)
        : content_epoch;
    evidence.completed_transaction_generation = reboot
        ? reboot->completed_transaction_generation : 0;
    evidence.frontend_verified = reboot &&
        reboot->completed_epoch &&
        reboot->completed_epoch == content_epoch &&
        !reboot->restart_required;
    return ObserveShowMenuMain(
        reboot, &evidence, current_thread_id);
}

ShowMenuMainDisposition ObserveShowMenuMain(
    FrontendContentColdReboot* reboot,
    const FrontendCompletionEvidence* evidence,
    std::uint32_t current_thread_id) noexcept
{
    if (!reboot || !reboot->master_enabled)
        return ShowMenuMainDisposition::DelegateOriginal;
    FrontendCompletionEvidence observed = {};
    if (evidence) observed = *evidence;
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.transaction_generation =
        observed.completed_transaction_generation;
    context.content_epoch = observed.armed_epoch
        ? observed.armed_epoch
        : observed.prepared_epoch
            ? observed.prepared_epoch
            : observed.completed_epoch;
    context.owner_thread_id = current_thread_id;
    const bool no_outgoing_content =
        observed.active_custom_source_count == 0U &&
        observed.unconsumed_epoch_count == 0U &&
        observed.armed_epoch == 0U &&
        observed.prepared_epoch == 0U;
    const bool verified_transaction =
        observed.active_custom_source_count == 0U &&
        observed.unconsumed_epoch_count == 0U &&
        observed.completed_epoch != 0U &&
        observed.completed_transaction_generation != 0U &&
        observed.frontend_verified &&
        observed.prepared_epoch == 0U &&
        (!observed.armed_epoch ||
         observed.armed_epoch == observed.completed_epoch);
    if (reboot->restart_required ||
        (!no_outgoing_content && !verified_transaction)) {
        const BarrierRejectReason reason =
            reboot->last_reject_reason != BarrierRejectReason::None
                ? reboot->last_reject_reason
                : (observed.armed_epoch || observed.prepared_epoch)
                    ? BarrierRejectReason::PreflightNotPrepared
                    : BarrierRejectReason::FrontendContaminated;
        if (!reboot->restart_required)
            LatchRestartRequired(
                reboot, reason, context.content_epoch);
        if (reboot->no_level_failure_presentation_show_menu_attempt !=
            ~(std::uint64_t)0)
            ++reboot->no_level_failure_presentation_show_menu_attempt;
        if (!reboot->no_level_failure_presentation_scheduled) {
            reboot->no_level_failure_presentation_scheduled = true;
            reboot->no_level_failure_presentation_count = 1;
            reboot->no_level_failure_presentation_reason = reason;
            reboot->no_level_failure_presentation_thread =
                current_thread_id;
            Emit(reboot,
                 ColdRebootEventType::NoLevelFailurePresentationScheduled,
                 &context, ContentDomainId::FrontendVerification,
                 RestartPhase::RuntimeFailed, DomainResult::Failed,
                 reason,
                 reboot->no_level_failure_presentation_show_menu_attempt);
        }
        Emit(reboot,
             ColdRebootEventType::ColdRebootShowMenuMainBlocked,
             &context, ContentDomainId::FrontendVerification,
             RestartPhase::RuntimeFailed, DomainResult::Failed,
             reason);
        return ShowMenuMainDisposition::BlockRestartRequired;
    }
    Emit(reboot, ColdRebootEventType::ColdRebootShowMenuMainAllowed,
         &context, ContentDomainId::FrontendVerification,
         verified_transaction ? RestartPhase::Verified
                              : RestartPhase::Idle,
         DomainResult::Succeeded);
    return ShowMenuMainDisposition::DelegateOriginal;
}

FrontendLookupDisposition ObserveFirstFrontendResourceLookup(
    FrontendContentColdReboot* reboot,
    const FrontendCompletionEvidence* evidence,
    std::uint32_t current_thread_id) noexcept
{
    if (!reboot || !reboot->master_enabled || !evidence)
        return FrontendLookupDisposition::LatchRestartRequired;
    const bool clean_without_outgoing =
        evidence->active_custom_source_count == 0U &&
        evidence->unconsumed_epoch_count == 0U &&
        evidence->armed_epoch == 0U &&
        evidence->prepared_epoch == 0U;
    if (clean_without_outgoing && !reboot->restart_required)
        return FrontendLookupDisposition::AllowCleanFrontend;
    const bool verified_reset =
        evidence->active_custom_source_count == 0U &&
        evidence->unconsumed_epoch_count == 0U &&
        evidence->completed_epoch != 0U &&
        evidence->completed_transaction_generation != 0U &&
        evidence->frontend_verified &&
        evidence->prepared_epoch == 0U &&
        (!evidence->armed_epoch ||
         evidence->armed_epoch == evidence->completed_epoch) &&
        !reboot->restart_required;
    if (verified_reset)
        return FrontendLookupDisposition::AllowVerifiedReset;
    const std::uint32_t epoch = evidence->armed_epoch
        ? evidence->armed_epoch
        : evidence->prepared_epoch
            ? evidence->prepared_epoch
            : evidence->completed_epoch;
    LatchRestartRequired(
        reboot, BarrierRejectReason::FrontendLookupBeforeReset,
        epoch);
    (void)current_thread_id;
    return FrontendLookupDisposition::LatchRestartRequired;
}

void LatchRestartRequired(
    FrontendContentColdReboot* reboot,
    BarrierRejectReason reason,
    std::uint32_t content_epoch) noexcept
{
    if (!reboot) return;
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.transaction_generation =
        reboot->prepared_transaction_generation
            ? reboot->prepared_transaction_generation
            : reboot->completed_transaction_generation;
    context.content_epoch = content_epoch;
    if (content_epoch && !reboot->armed_epoch)
        reboot->armed_epoch = content_epoch;
    reboot->restart_required = true;
    reboot->show_menu_guard_armed = true;
    reboot->new_project_admission_blocked = true;
    reboot->cleanup_still_authorized = false;
    reboot->last_reject_reason =
        reason == BarrierRejectReason::None
            ? BarrierRejectReason::FrontendContaminated
            : reason;
    StoreState(reboot, ColdRebootState::RuntimeFailed);
    Emit(reboot, ColdRebootEventType::ColdRebootFailed, &context,
         ContentDomainId::FrontendVerification,
         RestartPhase::RuntimeFailed, DomainResult::Failed,
         reboot->last_reject_reason);
}

bool ClaimNoLevelFailurePresentation(
    FrontendContentColdReboot* reboot) noexcept
{
    return reboot && reboot->no_level_failure_presentation_scheduled &&
        dluma_atomic_compare_exchange(
            &reboot->no_level_failure_presentation_delivery_claimed,
            1, 0) == 0;
}

void NoteNoLevelFailurePresentation(
    FrontendContentColdReboot* reboot,
    bool delivered,
    std::uint32_t current_thread_id) noexcept
{
    if (!reboot ||
        dluma_atomic_compare_exchange(
            &reboot->no_level_failure_presentation_delivery_claimed,
            0, 0) == 0)
        return;
    reboot->failure_panel_delivered = delivered;
    RestartContext context = {};
    context.baseline_generation = reboot->baseline_generation;
    context.transaction_generation =
        reboot->completed_transaction_generation;
    context.content_epoch = reboot->armed_epoch
        ? static_cast<std::uint32_t>(reboot->armed_epoch)
        : reboot->quarantined_epoch;
    context.owner_thread_id = current_thread_id;
    Emit(reboot, ColdRebootEventType::NoLevelFailurePresentationDelivered,
         &context, ContentDomainId::FrontendVerification,
         RestartPhase::RuntimeFailed,
         delivered ? DomainResult::Succeeded : DomainResult::Failed,
         reboot->no_level_failure_presentation_reason,
         reboot->no_level_failure_presentation_count,
         reboot->no_level_failure_presentation_show_menu_attempt);
}

bool CanAdmitCustomProject(
    const FrontendContentColdReboot* reboot) noexcept
{
    return reboot && reboot->master_enabled && reboot->baseline_captured &&
        !reboot->restart_required &&
        !reboot->new_project_admission_blocked &&
        !reboot->active_epoch_quarantined && reboot->registry &&
        cold_reboot::CanAdmitCustomProject(reboot->registry);
}

ColdRebootState CurrentColdRebootState(
    const FrontendContentColdReboot* reboot) noexcept
{
    if (!reboot) return ColdRebootState::Disabled;
    return static_cast<ColdRebootState>(dluma_atomic_compare_exchange(
        const_cast<volatile LONG*>(&reboot->state), 0, 0));
}

const char* ColdRebootStateName(ColdRebootState state) noexcept
{
    switch (state) {
    case ColdRebootState::Disabled: return "Disabled";
    case ColdRebootState::AwaitingBaseline: return "AwaitingBaseline";
    case ColdRebootState::Armed: return "Armed";
    case ColdRebootState::BarrierEntered: return "BarrierEntered";
    case ColdRebootState::Preflighting: return "Preflighting";
    case ColdRebootState::Mutating: return "Mutating";
    case ColdRebootState::Verifying: return "Verifying";
    case ColdRebootState::Verified: return "Verified";
    case ColdRebootState::RuntimePartial: return "RuntimePartial";
    case ColdRebootState::RuntimeFailed: return "RuntimeFailed";
    default: return "Unknown";
    }
}

const char* ColdRebootEventTypeName(ColdRebootEventType type) noexcept
{
    switch (type) {
    case ColdRebootEventType::ColdRebootBaselineCaptured:
        return "ColdRebootBaselineCaptured";
    case ColdRebootEventType::ColdRebootBarrierEnter:
        return "ColdRebootBarrierEnter";
    case ColdRebootEventType::ColdRebootBarrierRejected:
        return "ColdRebootBarrierRejected";
    case ColdRebootEventType::ColdRebootPreflightBegin:
        return "ColdRebootPreflightBegin";
    case ColdRebootEventType::ColdRebootPreflightDomain:
        return "ColdRebootPreflightDomain";
    case ColdRebootEventType::ColdRebootMutationBegin:
        return "ColdRebootMutationBegin";
    case ColdRebootEventType::ColdRebootDomainQuiesce:
        return "ColdRebootDomainQuiesce";
    case ColdRebootEventType::ColdRebootDomainCancel:
        return "ColdRebootDomainCancel";
    case ColdRebootEventType::ColdRebootDomainReset:
        return "ColdRebootDomainReset";
    case ColdRebootEventType::ColdRebootDomainReload:
        return "ColdRebootDomainReload";
    case ColdRebootEventType::ColdRebootDomainVerify:
        return "ColdRebootDomainVerify";
    case ColdRebootEventType::ColdRebootVerified:
        return "ColdRebootVerified";
    case ColdRebootEventType::ColdRebootFailed:
        return "ColdRebootFailed";
    case ColdRebootEventType::ColdRebootShowMenuMainAllowed:
        return "ColdRebootShowMenuMainAllowed";
    case ColdRebootEventType::ColdRebootShowMenuMainBlocked:
        return "ColdRebootShowMenuMainBlocked";
    case ColdRebootEventType::TransitionAttestationQuarantined:
        return "TransitionAttestationQuarantined";
    case ColdRebootEventType::TransitionQuarantineCleared:
        return "TransitionQuarantineCleared";
    case ColdRebootEventType::NoLevelFailurePresentationScheduled:
        return "NoLevelFailurePresentationScheduled";
    case ColdRebootEventType::NoLevelFailurePresentationDelivered:
        return "NoLevelFailurePresentationDelivered";
    case ColdRebootEventType::ColdRebootCriticalOverflow:
        return "ColdRebootCriticalOverflow";
    default: return "Unknown";
    }
}

const char* BarrierRejectReasonName(BarrierRejectReason reason) noexcept
{
    switch (reason) {
    case BarrierRejectReason::None: return "none";
    case BarrierRejectReason::Disabled: return "disabled";
    case BarrierRejectReason::BaselineUnavailable:
        return "baseline_unavailable";
    case BarrierRejectReason::NotArmed: return "not_armed";
    case BarrierRejectReason::WrongState: return "wrong_state";
    case BarrierRejectReason::PleaseWaitInactive:
        return "please_wait_inactive";
    case BarrierRejectReason::PipelineWaitIncomplete:
        return "pipeline_wait_incomplete";
    case BarrierRejectReason::SessionStillAlive:
        return "session_still_alive";
    case BarrierRejectReason::ActiveLevelStillPresent:
        return "active_level_still_present";
    case BarrierRejectReason::CustomPlayerStillPresent:
        return "custom_player_still_present";
    case BarrierRejectReason::SourceResetNotReady:
        return "source_reset_not_ready";
    case BarrierRejectReason::NextProjectStagingStarted:
        return "next_project_staging_started";
    case BarrierRejectReason::WrongThread: return "wrong_thread";
    case BarrierRejectReason::Shutdown: return "shutdown";
    case BarrierRejectReason::SameProjectTransition:
        return "same_project_transition";
    case BarrierRejectReason::NestedTransaction:
        return "nested_transaction";
    case BarrierRejectReason::RepeatedEpoch: return "repeated_epoch";
    case BarrierRejectReason::PreflightNotPrepared:
        return "preflight_not_prepared";
    case BarrierRejectReason::MandatoryPreflightFailed:
        return "mandatory_preflight_failed";
    case BarrierRejectReason::QuiesceCancelFailure:
        return "quiesce_cancel_failure";
    case BarrierRejectReason::PostMutationFailure:
        return "post_mutation_failure";
    case BarrierRejectReason::CancelledPresentationCleanupFailed:
        return "cancelled_presentation_cleanup_failed";
    case BarrierRejectReason::DuplicateSourceOwnership:
        return "duplicate_source_ownership";
    case BarrierRejectReason::FrontendLookupBeforeReset:
        return "frontend_lookup_before_reset";
    case BarrierRejectReason::FrontendContaminated:
        return "frontend_contaminated";
    case BarrierRejectReason::TransitionAttestationFailed:
        return "transition_attestation_failed";
    default: return "unknown";
    }
}

const char* ColdRebootOutcomeName(ColdRebootOutcome outcome) noexcept
{
    switch (outcome) {
    case ColdRebootOutcome::NotApplicable: return "NotApplicable";
    case ColdRebootOutcome::Rejected: return "Rejected";
    case ColdRebootOutcome::PreflightFailed: return "PreflightFailed";
    case ColdRebootOutcome::Verified: return "Verified";
    case ColdRebootOutcome::RuntimePartial: return "RuntimePartial";
    case ColdRebootOutcome::RuntimeFailed: return "RuntimeFailed";
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot
