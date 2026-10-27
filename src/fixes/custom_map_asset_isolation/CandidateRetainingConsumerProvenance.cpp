#define _HAS_EXCEPTIONS 0

#include "CandidateRetainingConsumerProvenance.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::provenance {
namespace {

using cold_reboot::ContentDomainId;
using cold_reboot::ContentDomainMask;
using cold_reboot::DomainBit;

class TryLedgerGuard {
public:
    explicit TryLedgerGuard(
        CandidateRetainingConsumerLedger* ledger) noexcept
        : ledger_(ledger), locked_(false)
    {
        if (ledger_ &&
            dluma_atomic_compare_exchange(&ledger_->lock, 1, 0) == 0)
            locked_ = true;
    }

    ~TryLedgerGuard()
    {
        if (locked_) dluma_atomic_exchange(&ledger_->lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    CandidateRetainingConsumerLedger* ledger_;
    bool locked_;
};

template <std::size_t Capacity>
bool CopyBounded(char (&output)[Capacity],
                 const char (&input)[Capacity]) noexcept
{
    static_assert(Capacity != 0, "bounded text needs a terminator");
    std::size_t index = 0;
    while (index + 1U < Capacity && input[index]) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
    return input[index] == 0;
}

bool CopyLiteral(char* output, std::size_t capacity,
                 const char* input) noexcept
{
    if (!output || !capacity || !input) return false;
    std::size_t index = 0;
    while (index + 1U < capacity && input[index]) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
    return input[index] == 0;
}

template <std::size_t Capacity>
bool HasBoundedText(const char (&text)[Capacity]) noexcept
{
    for (std::size_t index = 0; index < Capacity; ++index)
        if (!text[index]) return index != 0;
    return false;
}

bool IsDomainValid(ContentDomainId domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <
        static_cast<std::uint8_t>(ContentDomainId::Count);
}

bool IsObserverValid(CandidateRetainingObserver observer) noexcept
{
    return static_cast<std::uint8_t>(observer) <=
        static_cast<std::uint8_t>(
            CandidateRetainingObserver::OtherValidatedManager);
}

bool IsUnresolvedReasonValid(CandidateUnresolvedReason reason) noexcept
{
    return static_cast<std::uint8_t>(reason) <=
        static_cast<std::uint8_t>(
            CandidateUnresolvedReason::LateAfterBarrier);
}

bool IsCoverageValid(CandidateObserverCoverage coverage) noexcept
{
    return coverage == CandidateObserverCoverage::Complete ||
        coverage == CandidateObserverCoverage::Incomplete;
}

CandidateRetainingEpochState* FindEpoch(
    CandidateRetainingConsumerLedger* ledger,
    std::uint32_t epoch) noexcept
{
    if (!ledger || !epoch) return nullptr;
    for (std::uint32_t index = 0; index < ledger->epoch_count; ++index)
        if (ledger->epochs[index].active &&
            ledger->epochs[index].epoch == epoch)
            return &ledger->epochs[index];
    return nullptr;
}

const CandidateRetainingEpochState* FindEpoch(
    const CandidateRetainingConsumerLedger* ledger,
    std::uint32_t epoch) noexcept
{
    return FindEpoch(
        const_cast<CandidateRetainingConsumerLedger*>(ledger), epoch);
}

CandidateRetainingEpochState* FindOrCreateEpoch(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    std::uint64_t first_sequence) noexcept
{
    CandidateRetainingEpochState* existing = FindEpoch(ledger, epoch);
    if (existing) return existing;
    if(!ledger || epoch<=ledger->retired_through_epoch)return nullptr;
    std::uint32_t slot=0;
    while(slot<ledger->epoch_count && ledger->epochs[slot].active)++slot;
    if(slot==ledger->epoch_count) {
        if(slot>=kCandidateRetainingEpochCapacity)return nullptr;
        ++ledger->epoch_count;
    }
    CandidateRetainingEpochState* state = &ledger->epochs[slot];
    std::memset(state, 0, sizeof(*state));
    state->epoch = epoch;
    state->first_candidate_sequence = first_sequence;
    state->last_candidate_sequence = first_sequence;
    state->active = true;
    return state;
}

CandidateRetainingConsumerRecord* FindRecord(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle) noexcept
{
    if (!ledger || !handle || !handle->record_generation ||
        !handle->epoch ||
        handle->record_index >= ledger->record_count)
        return nullptr;
    CandidateRetainingConsumerRecord* record =
        &ledger->records[handle->record_index];
    return record->record_generation == handle->record_generation &&
           record->epoch == handle->epoch
        ? record : nullptr;
}

void LatchEpochLoss(CandidateRetainingConsumerLedger* ledger,
                    std::uint32_t epoch,
                    CandidateUnresolvedReason reason) noexcept
{
    if (!ledger) return;
    /* A lock-contention loss can race the BF8 reader that owns the lock.
     * Publish a process-wide fail-closed latch before the diagnostic counts;
     * an exact epoch slot remains useful provenance, but is not required for
     * safety in that race. */
    if (reason == CandidateUnresolvedReason::LedgerBusy) {
        dluma_atomic_exchange(&ledger->unattributed_loss_latched, 1);
        dluma_atomic_increment(&ledger->contention_loss_count);
    } else if (reason == CandidateUnresolvedReason::CapacityExceeded) {
        dluma_atomic_increment(&ledger->capacity_loss_count);
    } else if (reason == CandidateUnresolvedReason::LateAfterBarrier) {
        dluma_atomic_increment(&ledger->late_after_barrier_count);
    }
    dluma_atomic_increment(&ledger->dropped_observation_count);

    if (!epoch) {
        dluma_atomic_exchange(&ledger->unattributed_loss_latched, 1);
        return;
    }

    const LONG encoded_epoch = static_cast<LONG>(epoch);
    for (std::size_t index = 0;
         index < kCandidateRetainingLossEpochCapacity; ++index) {
        LONG observed = dluma_atomic_compare_exchange(
            &ledger->loss_epochs[index], encoded_epoch, encoded_epoch);
        if (observed == 0) {
            observed = dluma_atomic_compare_exchange(
                &ledger->loss_epochs[index], encoded_epoch, 0);
        }
        if (observed == 0 || observed == encoded_epoch) {
            dluma_atomic_increment(&ledger->loss_counts[index]);
            dluma_atomic_exchange(
                &ledger->loss_reasons[index],
                static_cast<LONG>(reason));
            return;
        }
    }
    dluma_atomic_exchange(&ledger->unattributed_loss_latched, 1);
}

std::uint32_t ReadEpochLossCount(
    const CandidateRetainingConsumerLedger* ledger,
    std::uint32_t epoch) noexcept
{
    if (!ledger) return 1U;
    std::uint32_t total =
        dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(
                &ledger->unattributed_loss_latched), 0, 0) != 0
        ? 1U : 0U;
    const LONG encoded_epoch = static_cast<LONG>(epoch);
    for (std::size_t index = 0;
         index < kCandidateRetainingLossEpochCapacity; ++index) {
        const LONG observed_epoch = dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->loss_epochs[index]),
            0, 0);
        if (observed_epoch != encoded_epoch) continue;
        const LONG count = dluma_atomic_compare_exchange(
            const_cast<volatile LONG*>(&ledger->loss_counts[index]),
            0, 0);
        if (count > 0) total += static_cast<std::uint32_t>(count);
    }
    return total;
}

void SetUnresolved(CandidateRetainingConsumerRecord* record,
                   CandidateUnresolvedReason reason,
                   const char* detail) noexcept
{
    if (!record) return;
    record->last_unresolved_reason = reason;
    CopyLiteral(record->last_unresolved_detail,
                sizeof(record->last_unresolved_detail),
                detail ? detail : CandidateUnresolvedReasonName(reason));
}

void CopyRecordIdentityToTouch(
    CandidateDomainTouchProvenance* touch,
    const CandidateRetainingConsumerRecord* record) noexcept
{
    if (!touch || !record) return;
    touch->record_generation = record->record_generation;
    touch->observer = record->observer;
    touch->observer_target = record->observer_target;
    touch->manager = record->manager;
    touch->owner_object = record->owner_object;
    touch->source = record->source;
    touch->exact_source = record->exact_source;
    touch->exact_provider = record->exact_provider;
    CopyBounded(touch->observer_name, record->observer_name);
    CopyBounded(touch->logical_name, record->logical_name);
    CopyBounded(touch->provider, record->provider);
}

void PromoteTrueTouch(
    CandidateRetainingConsumerLedger* ledger,
    CandidateRetainingEpochState* epoch_state, ContentDomainId domain,
    std::uint64_t touch_sequence,
    CandidateRetentionClassification classification,
    CandidateUnresolvedReason reason,
    const CandidateRetainingConsumerRecord* record,
    const char* detail) noexcept
{
    if (!ledger || !epoch_state || !IsDomainValid(domain)) return;
    const ContentDomainMask bit = DomainBit(domain);
    epoch_state->immutable_true_touch_mask |= bit;

    CandidateDomainTouchProvenance* touch =
        &ledger->domain_touches[static_cast<std::size_t>(domain)];
    if (touch->touched) return;

    std::memset(touch, 0, sizeof(*touch));
    touch->first_touch_sequence = touch_sequence;
    touch->epoch = epoch_state->epoch;
    touch->domain = domain;
    touch->classification = classification;
    touch->unresolved_reason = reason;
    touch->touched = true;
    if (record) {
        CopyRecordIdentityToTouch(touch, record);
    } else {
        touch->observer = CandidateRetainingObserver::Unknown;
        CopyLiteral(touch->observer_name,
                    sizeof(touch->observer_name), "observer-loss");
        CopyLiteral(touch->logical_name,
                    sizeof(touch->logical_name), "<unknown>");
        CopyLiteral(touch->provider,
                    sizeof(touch->provider), "<unknown>");
    }
    CopyLiteral(touch->unresolved_detail,
                sizeof(touch->unresolved_detail),
                detail ? detail : CandidateUnresolvedReasonName(reason));
}

void RebuildDecision(
    CandidateRetainingConsumerLedger* ledger,
    CandidateRetainingEpochState* epoch_state) noexcept
{
    Bf8CandidatePromotionDecision decision = {};
    decision.barrier_sequence = epoch_state->barrier_sequence;
    decision.epoch = epoch_state->epoch;
    decision.observer_loss_count = ReadEpochLossCount(
        ledger, epoch_state->epoch);
    decision.required_observer_coverage_mask =
        ledger->required_observer_coverage_mask;
    decision.observer_coverage_complete_mask =
        epoch_state->observer_coverage_complete_mask;
    decision.observer_coverage_incomplete_mask =
        epoch_state->observer_coverage_incomplete_mask;

    for (std::uint32_t index = 0; index < ledger->record_count; ++index) {
        const CandidateRetainingConsumerRecord* record =
            &ledger->records[index];
        if (record->epoch != epoch_state->epoch) continue;
        ++decision.record_count;
        switch (record->classification) {
        case CandidateRetentionClassification::Ephemeral:
            ++decision.ephemeral_count;
            break;
        case CandidateRetentionClassification::NaturallyReplaced:
        case CandidateRetentionClassification::NaturallyReplacedByFrontend:
            ++decision.naturally_replaced_count;
            break;
        case CandidateRetentionClassification::Retained:
            ++decision.retained_count;
            break;
        case CandidateRetentionClassification::Candidate:
        case CandidateRetentionClassification::UnknownAtBarrier:
            ++decision.unknown_at_barrier_count;
            decision.unresolved_domain_mask |= DomainBit(record->domain);
            break;
        default:
            ++decision.unknown_at_barrier_count;
            decision.unresolved_domain_mask |= DomainBit(record->domain);
            break;
        }
    }

    if (decision.observer_loss_count)
        decision.unresolved_domain_mask |= DomainBit(
            ContentDomainId::UnclassifiedPersistentConsumers);
    decision.unresolved_domain_mask |=
        decision.observer_coverage_incomplete_mask;
    decision.unresolved_domain_mask |=
        decision.required_observer_coverage_mask &
        ~decision.observer_coverage_complete_mask;

    epoch_state->observer_loss_count = decision.observer_loss_count;
    decision.immutable_true_touch_mask =
        epoch_state->immutable_true_touch_mask;
    decision.touch_mask_to_promote =
        epoch_state->immutable_true_touch_mask;
    decision.promote_unclassified =
        (decision.touch_mask_to_promote & DomainBit(
            ContentDomainId::UnclassifiedPersistentConsumers)) != 0;
    decision.all_candidates_resolved =
        decision.unknown_at_barrier_count == 0 &&
        decision.observer_loss_count == 0 &&
        decision.unresolved_domain_mask == 0;
    decision.preflight_allowed =
        decision.all_candidates_resolved;
    decision.observer_loss_latched =
        decision.observer_loss_count != 0;
    decision.epoch_sealed = epoch_state->sealed;
    epoch_state->decision = decision;
}

void MakeConservativeBusyDecision(
    std::uint32_t epoch, std::uint64_t sequence,
    Bf8CandidatePromotionDecision* output) noexcept
{
    if (!output) return;
    std::memset(output, 0, sizeof(*output));
    output->epoch = epoch;
    output->barrier_sequence = sequence;
    output->observer_loss_count = 1;
    output->unresolved_domain_mask = DomainBit(
        ContentDomainId::UnclassifiedPersistentConsumers);
    output->observer_loss_latched = true;
}

}  // namespace

void InitializeCandidateRetainingConsumerLedger(
    CandidateRetainingConsumerLedger* ledger) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
    ledger->next_record_generation = 1;
}

static CandidateLedgerResult FinishCandidateEpoch(CandidateRetainingConsumerLedger* ledger,
    const cold_reboot::ContentDomainRestartRegistry* registry,const cold_reboot::RestartContext* context,
    const cold_reboot::RegistryFinalizationToken* token,bool mutate) noexcept
{
    if(!ledger || !cold_reboot::RegistryFinalizationIsCurrent(registry,context,token))
        return CandidateLedgerResult::InvalidArguments;
    TryLedgerGuard guard(ledger);
    if(!guard.locked())return CandidateLedgerResult::LedgerBusy;
    auto* epoch=FindEpoch(ledger,context->content_epoch);
    if(!epoch || context->content_epoch<=ledger->retired_through_epoch ||
       context->transaction_generation<=ledger->retired_transaction)return CandidateLedgerResult::StaleHandle;
    RebuildDecision(ledger,epoch);
    if(!epoch->sealed || !epoch->barrier_evaluated || !epoch->decision.preflight_allowed ||
       !epoch->decision.all_candidates_resolved || epoch->decision.observer_loss_latched ||
       epoch->decision.unresolved_domain_mask ||
       (epoch->immutable_true_touch_mask&~token->verified_mask))return CandidateLedgerResult::EvidenceIncomplete;
    std::uint64_t retired=0;
    for(std::uint32_t i=0;i<ledger->record_count;++i) {
        const auto& record=ledger->records[i];if(record.epoch!=context->content_epoch)continue;
        if((record.classification==CandidateRetentionClassification::Retained ||
            record.classification==CandidateRetentionClassification::NaturallyReplacedByFrontend) &&
           !(token->verified_mask&DomainBit(record.domain)))return CandidateLedgerResult::EvidenceIncomplete;
        ++retired;
    }
    if(!cold_reboot::RegistryFinalizationIsCurrent(registry,context,token))return CandidateLedgerResult::InvalidArguments;
    if(!mutate)return CandidateLedgerResult::Succeeded;
    for(std::uint32_t i=0;i<ledger->record_count;++i)
        if(ledger->records[i].epoch==context->content_epoch)std::memset(&ledger->records[i],0,sizeof(ledger->records[i]));
    for(auto& touch:ledger->domain_touches)
        if(touch.epoch==context->content_epoch)std::memset(&touch,0,sizeof(touch));
    std::memset(epoch,0,sizeof(*epoch));
    ledger->retired_through_epoch=context->content_epoch;
    ledger->retired_transaction=context->transaction_generation;
    ++ledger->retired_epochs;ledger->retired_records+=retired;
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult RetireCandidateEpoch(CandidateRetainingConsumerLedger* ledger,
    const cold_reboot::ContentDomainRestartRegistry* registry,const cold_reboot::RestartContext* context,
    const cold_reboot::RegistryFinalizationToken* token) noexcept
{return FinishCandidateEpoch(ledger,registry,context,token,true);}

CandidateLedgerResult CheckCandidateEpochRetirement(CandidateRetainingConsumerLedger* ledger,
    const cold_reboot::ContentDomainRestartRegistry* registry,const cold_reboot::RestartContext* context,
    const cold_reboot::RegistryFinalizationToken* token) noexcept
{return FinishCandidateEpoch(ledger,registry,context,token,false);}

bool SetRequiredCandidateObserverCoverageMask(
    CandidateRetainingConsumerLedger* ledger,
    ContentDomainMask required_mask) noexcept
{
    if (!ledger) return false;
    const ContentDomainMask valid_mask =
        (static_cast<ContentDomainMask>(1U) <<
         static_cast<std::uint8_t>(ContentDomainId::Count)) - 1U;
    if (!required_mask || (required_mask & ~valid_mask) != 0)
        return false;
    TryLedgerGuard guard(ledger);
    if (!guard.locked() || ledger->record_count ||
        ledger->epoch_count)
        return false;
    ledger->required_observer_coverage_mask = required_mask;
    return true;
}

CandidateLedgerResult RecordCandidateDomainObserverCoverage(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    std::uint64_t sequence, ContentDomainId domain,
    CandidateObserverCoverage coverage) noexcept
{
    if (!ledger || !epoch || !sequence || !IsDomainValid(domain) ||
        !IsCoverageValid(coverage))
        return CandidateLedgerResult::InvalidArguments;
    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(
            ledger, epoch, CandidateUnresolvedReason::LedgerBusy);
        return CandidateLedgerResult::LedgerBusy;
    }
    CandidateRetainingEpochState* epoch_state =
        FindOrCreateEpoch(ledger, epoch, sequence);
    if (!epoch_state) {
        if(epoch<=ledger->retired_through_epoch)return CandidateLedgerResult::StaleHandle;
        LatchEpochLoss(
            ledger, epoch, CandidateUnresolvedReason::CapacityExceeded);
        return CandidateLedgerResult::EpochCapacityExceeded;
    }
    if (epoch_state->sealed) {
        LatchEpochLoss(
            ledger, epoch, CandidateUnresolvedReason::LateAfterBarrier);
        RebuildDecision(ledger, epoch_state);
        return CandidateLedgerResult::EpochSealed;
    }
    const ContentDomainMask bit = DomainBit(domain);
    if (coverage == CandidateObserverCoverage::Incomplete) {
        epoch_state->observer_coverage_incomplete_mask |= bit;
        epoch_state->observer_coverage_complete_mask &= ~bit;
    } else if ((epoch_state->observer_coverage_incomplete_mask & bit) == 0) {
        epoch_state->observer_coverage_complete_mask |= bit;
    }
    if (!epoch_state->first_candidate_sequence ||
        sequence < epoch_state->first_candidate_sequence)
        epoch_state->first_candidate_sequence = sequence;
    if (sequence > epoch_state->last_candidate_sequence)
        epoch_state->last_candidate_sequence = sequence;
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult RecordCandidateRetainingConsumer(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerInput* input,
    CandidateRetainingConsumerHandle* output) noexcept
{
    if (output) std::memset(output, 0, sizeof(*output));
    if (!ledger || !input || !output || !input->epoch ||
        !input->sequence || !IsDomainValid(input->proposed_domain) ||
        !IsObserverValid(input->observer))
        return CandidateLedgerResult::InvalidArguments;

    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(ledger, input->epoch,
                       CandidateUnresolvedReason::LedgerBusy);
        return CandidateLedgerResult::LedgerBusy;
    }

    CandidateRetainingEpochState* epoch_state =
        FindOrCreateEpoch(ledger, input->epoch, input->sequence);
    if (!epoch_state) {
        if(input->epoch<=ledger->retired_through_epoch)return CandidateLedgerResult::StaleHandle;
        LatchEpochLoss(ledger, input->epoch,
                       CandidateUnresolvedReason::CapacityExceeded);
        return CandidateLedgerResult::EpochCapacityExceeded;
    }
    if (epoch_state->sealed) {
        LatchEpochLoss(ledger, input->epoch,
                       CandidateUnresolvedReason::LateAfterBarrier);
        RebuildDecision(ledger, epoch_state);
        return CandidateLedgerResult::EpochSealed;
    }
    std::uint32_t record_index=0;
    while(record_index<ledger->record_count && ledger->records[record_index].record_generation)++record_index;
    if (record_index >= kCandidateRetainingRecordCapacity || ledger->next_record_generation==UINT64_MAX) {
        LatchEpochLoss(ledger, input->epoch,
                       CandidateUnresolvedReason::CapacityExceeded);
        return CandidateLedgerResult::RecordCapacityExceeded;
    }

    if(record_index==ledger->record_count)++ledger->record_count;
    CandidateRetainingConsumerRecord* record =
        &ledger->records[record_index];
    std::memset(record, 0, sizeof(*record));
    record->record_generation = ledger->next_record_generation++;
    if (!record->record_generation)
        record->record_generation = ledger->next_record_generation++;
    record->first_candidate_sequence = input->sequence;
    record->epoch = input->epoch;
    record->source_record_index = input->source_record_index;
    record->source_add_ordinal = input->source_add_ordinal;
    record->domain = input->proposed_domain;
    record->observer = input->observer;
    record->classification =
        CandidateRetentionClassification::Candidate;
    record->observer_target = input->observer_target;
    record->manager = input->manager;
    record->owner_object = input->owner_object;
    record->source = input->source;
    record->exact_source = input->exact_source && input->source != 0;
    const bool observer_name_complete = CopyBounded(
        record->observer_name, input->observer_name);
    const bool logical_name_complete = CopyBounded(
        record->logical_name, input->logical_name);
    const bool provider_complete = CopyBounded(
        record->provider, input->provider);
    record->exact_provider = input->exact_provider &&
        provider_complete && record->provider[0] != 0;

    if (input->observer ==
        CandidateRetainingObserver::BroadFilesystemOpen) {
        SetUnresolved(record, CandidateUnresolvedReason::BroadOpenOnly,
                      "broad filesystem open has no retained owner");
    } else if (!logical_name_complete || !record->logical_name[0]) {
        SetUnresolved(
            record, CandidateUnresolvedReason::LogicalIdentityUnavailable,
            "logical identity unavailable");
    } else if (!observer_name_complete) {
        SetUnresolved(
            record, CandidateUnresolvedReason::ExactObserverUnavailable,
            "observer label was not bounded");
    } else if (input->exact_provider &&
               (!provider_complete || !record->provider[0])) {
        SetUnresolved(
            record, CandidateUnresolvedReason::ExactProviderUnavailable,
            "exact provider was not bounded");
    } else {
        SetUnresolved(
            record, CandidateUnresolvedReason::AwaitingOwnerCorrelation,
            "awaiting exact manager or owner correlation");
    }

    ++epoch_state->record_count;
    if (!epoch_state->first_candidate_sequence ||
        input->sequence < epoch_state->first_candidate_sequence)
        epoch_state->first_candidate_sequence = input->sequence;
    if (input->sequence > epoch_state->last_candidate_sequence)
        epoch_state->last_candidate_sequence = input->sequence;

    output->record_generation = record->record_generation;
    output->epoch = record->epoch;
    output->record_index = record_index;
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult CorrelateCandidateRetainingOwner(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateOwnerCorrelation* correlation) noexcept
{
    if (!ledger || !handle || !correlation || !correlation->sequence ||
        !IsDomainValid(correlation->resolved_domain) ||
        !IsObserverValid(correlation->observer))
        return CandidateLedgerResult::InvalidArguments;

    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(ledger, handle->epoch,
                       CandidateUnresolvedReason::LedgerBusy);
        return CandidateLedgerResult::LedgerBusy;
    }
    CandidateRetainingConsumerRecord* record =
        FindRecord(ledger, handle);
    if (!record) return CandidateLedgerResult::StaleHandle;
    CandidateRetainingEpochState* epoch_state =
        FindEpoch(ledger, record->epoch);
    if (!epoch_state || epoch_state->sealed)
        return CandidateLedgerResult::EpochSealed;
    if (record->classification !=
        CandidateRetentionClassification::Candidate)
        return CandidateLedgerResult::InvalidTransition;
    if (correlation->sequence < record->first_candidate_sequence) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::SequenceRegression,
                      "owner correlation precedes candidate");
        return CandidateLedgerResult::EvidenceIncomplete;
    }

    const bool exact_observer = correlation->exact_observer &&
        correlation->observer != CandidateRetainingObserver::Unknown &&
        correlation->observer !=
            CandidateRetainingObserver::BroadFilesystemOpen;
    const bool exact_owner = correlation->exact_owner &&
        (correlation->manager != 0 ||
         correlation->owner_object != 0);
    if (!exact_observer) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::ExactObserverUnavailable,
                      "validated observer is required");
        return CandidateLedgerResult::ExactOwnerRequired;
    }
    if (!exact_owner) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::ExactManagerUnavailable,
                      "exact manager or owner object is required");
        return CandidateLedgerResult::ExactOwnerRequired;
    }
    if (correlation->exact_source && !correlation->source) {
        SetUnresolved(record, CandidateUnresolvedReason::InvalidEvidence,
                      "exact source flag has no source pointer");
        return CandidateLedgerResult::EvidenceIncomplete;
    }
    if (correlation->exact_provider &&
        !HasBoundedText(correlation->provider)) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::ExactProviderUnavailable,
                      "exact provider is unavailable");
        return CandidateLedgerResult::EvidenceIncomplete;
    }

    record->owner_correlation_sequence = correlation->sequence;
    record->domain = correlation->resolved_domain;
    record->observer = correlation->observer;
    record->observer_target = correlation->observer_target;
    record->manager = correlation->manager;
    record->owner_object = correlation->owner_object;
    if (correlation->source) record->source = correlation->source;
    if (correlation->source_record_index)
        record->source_record_index = correlation->source_record_index;
    if (correlation->source_add_ordinal)
        record->source_add_ordinal = correlation->source_add_ordinal;
    record->exact_observer = true;
    record->exact_owner_correlated = true;
    record->exact_source = correlation->exact_source;
    record->exact_provider = correlation->exact_provider;
    CopyBounded(record->observer_name, correlation->observer_name);
    if (HasBoundedText(correlation->logical_name))
        CopyBounded(record->logical_name, correlation->logical_name);
    if (HasBoundedText(correlation->provider))
        CopyBounded(record->provider, correlation->provider);
    SetUnresolved(record,
                  CandidateUnresolvedReason::RetentionLifetimeUnproven,
                  "owner correlated; lifetime classification pending");
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult UpdateCandidateUnresolvedReason(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateUnresolvedEvidence* evidence) noexcept
{
    if (!ledger || !handle || !evidence ||
        evidence->reason == CandidateUnresolvedReason::None ||
        !IsUnresolvedReasonValid(evidence->reason))
        return CandidateLedgerResult::InvalidArguments;
    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(ledger, handle->epoch,
                       CandidateUnresolvedReason::LedgerBusy);
        return CandidateLedgerResult::LedgerBusy;
    }
    CandidateRetainingConsumerRecord* record = FindRecord(ledger, handle);
    if (!record) return CandidateLedgerResult::StaleHandle;
    const CandidateRetainingEpochState* epoch_state =
        FindEpoch(ledger, record->epoch);
    if (!epoch_state || epoch_state->sealed)
        return CandidateLedgerResult::EpochSealed;
    if (record->classification !=
        CandidateRetentionClassification::Candidate)
        return CandidateLedgerResult::InvalidTransition;
    record->last_unresolved_reason = evidence->reason;
    if (!CopyBounded(record->last_unresolved_detail,
                     evidence->detail)) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::InvalidEvidence,
                      "unresolved detail was not bounded");
        return CandidateLedgerResult::EvidenceIncomplete;
    }
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult ClassifyCandidateRetainingConsumer(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateRetentionEvidence* evidence) noexcept
{
    if (!ledger || !handle || !evidence || !evidence->sequence)
        return CandidateLedgerResult::InvalidArguments;
    if (evidence->classification !=
            CandidateRetentionClassification::Ephemeral &&
        evidence->classification !=
            CandidateRetentionClassification::NaturallyReplaced &&
        evidence->classification !=
            CandidateRetentionClassification::NaturallyReplacedByFrontend &&
        evidence->classification !=
            CandidateRetentionClassification::Retained)
        return CandidateLedgerResult::InvalidTransition;

    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(ledger, handle->epoch,
                       CandidateUnresolvedReason::LedgerBusy);
        return CandidateLedgerResult::LedgerBusy;
    }
    CandidateRetainingConsumerRecord* record = FindRecord(ledger, handle);
    if (!record) return CandidateLedgerResult::StaleHandle;
    CandidateRetainingEpochState* epoch_state =
        FindEpoch(ledger, record->epoch);
    if (!epoch_state || epoch_state->sealed)
        return CandidateLedgerResult::EpochSealed;
    if (record->classification !=
        CandidateRetentionClassification::Candidate)
        return CandidateLedgerResult::InvalidTransition;
    if (!record->exact_observer ||
        !record->exact_owner_correlated) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::ExactManagerUnavailable,
                      "classification requires exact owner correlation");
        return CandidateLedgerResult::ExactOwnerRequired;
    }
    if (evidence->sequence < record->owner_correlation_sequence) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::SequenceRegression,
                      "classification precedes owner correlation");
        return CandidateLedgerResult::EvidenceIncomplete;
    }

    bool sufficient = false;
    switch (evidence->classification) {
    case CandidateRetentionClassification::Ephemeral:
        sufficient = evidence->ephemeral_completion_observed;
        break;
    case CandidateRetentionClassification::NaturallyReplaced:
        sufficient = evidence->natural_replacement_observed ||
                     evidence->natural_unload_observed;
        break;
    case CandidateRetentionClassification::NaturallyReplacedByFrontend:
        sufficient = evidence->frontend_replacement_contract_observed &&
            record->observer ==
                CandidateRetainingObserver::LocalMaterialManager &&
            record->domain == ContentDomainId::ContentOnDemand;
        break;
    case CandidateRetentionClassification::Retained:
        sufficient = evidence->retained_value_observed;
        break;
    default:
        break;
    }
    if (!sufficient) {
        SetUnresolved(record,
                      CandidateUnresolvedReason::InvalidEvidence,
                      "terminal classification lacks required evidence");
        return CandidateLedgerResult::EvidenceIncomplete;
    }

    record->classification = evidence->classification;
    record->classification_sequence = evidence->sequence;
    record->ephemeral_completion_observed =
        evidence->ephemeral_completion_observed;
    record->natural_replacement_observed =
        evidence->natural_replacement_observed;
    record->frontend_replacement_contract_observed =
        evidence->frontend_replacement_contract_observed;
    record->natural_unload_observed =
        evidence->natural_unload_observed;
    record->retained_value_observed =
        evidence->retained_value_observed;
    record->last_unresolved_reason = CandidateUnresolvedReason::None;
    CopyBounded(record->last_unresolved_detail, evidence->detail);

    if (record->classification ==
        CandidateRetentionClassification::Retained) {
        PromoteTrueTouch(
            ledger, epoch_state, record->domain,
            evidence->sequence,
            CandidateRetentionClassification::Retained,
            CandidateUnresolvedReason::None, record,
            "exact retained owner observed");
    }
    return CandidateLedgerResult::Succeeded;
}

CandidateLedgerResult EvaluateCandidateRetainingConsumersAtBf8(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    std::uint64_t barrier_sequence,
    Bf8CandidatePromotionDecision* output) noexcept
{
    if (!output) return CandidateLedgerResult::InvalidArguments;
    std::memset(output, 0, sizeof(*output));
    if (!ledger || !epoch || !barrier_sequence)
        return CandidateLedgerResult::InvalidArguments;

    TryLedgerGuard guard(ledger);
    if (!guard.locked()) {
        LatchEpochLoss(ledger, epoch,
                       CandidateUnresolvedReason::LedgerBusy);
        MakeConservativeBusyDecision(epoch, barrier_sequence, output);
        return CandidateLedgerResult::LedgerBusy;
    }

    CandidateRetainingEpochState* epoch_state =
        FindOrCreateEpoch(ledger, epoch, 0);
    if (!epoch_state) {
        if(epoch<=ledger->retired_through_epoch)return CandidateLedgerResult::StaleHandle;
        LatchEpochLoss(ledger, epoch,
                       CandidateUnresolvedReason::CapacityExceeded);
        MakeConservativeBusyDecision(epoch, barrier_sequence, output);
        return CandidateLedgerResult::EpochCapacityExceeded;
    }

    if (!epoch_state->barrier_evaluated) {
        epoch_state->barrier_sequence = barrier_sequence;
        for (std::uint32_t index = 0;
             index < ledger->record_count; ++index) {
            CandidateRetainingConsumerRecord* record =
                &ledger->records[index];
            if (record->epoch != epoch ||
                record->classification !=
                    CandidateRetentionClassification::Candidate)
                continue;
            record->classification =
                CandidateRetentionClassification::UnknownAtBarrier;
            record->classification_sequence = barrier_sequence;
            if (barrier_sequence < record->first_candidate_sequence) {
                SetUnresolved(
                    record,
                    CandidateUnresolvedReason::SequenceRegression,
                    "BF8 sequence precedes candidate");
            } else {
                SetUnresolved(
                    record, CandidateUnresolvedReason::BarrierReached,
                    "exact retention owner unresolved at BF8");
            }
        }
        epoch_state->barrier_evaluated = true;
        epoch_state->sealed = true;
    }
    RebuildDecision(ledger, epoch_state);
    *output = epoch_state->decision;
    return CandidateLedgerResult::Succeeded;
}

std::size_t SnapshotCandidateRetainingEpoch(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    CandidateRetainingConsumerRecord* records,
    std::size_t record_capacity,
    CandidateRetainingEpochState* epoch_state) noexcept
{
    if (!ledger || !epoch || (!records && record_capacity)) return 0;
    TryLedgerGuard guard(ledger);
    if (!guard.locked()) return 0;
    const CandidateRetainingEpochState* state = FindEpoch(ledger, epoch);
    if (!state) return 0;
    if (epoch_state) *epoch_state = *state;

    std::size_t copied = 0;
    for (std::uint32_t index = 0;
         index < ledger->record_count && copied < record_capacity; ++index) {
        if (ledger->records[index].epoch != epoch) continue;
        records[copied++] = ledger->records[index];
    }
    return copied;
}

std::size_t SnapshotCandidateDomainTouches(
    CandidateRetainingConsumerLedger* ledger,
    CandidateDomainTouchProvenance* output,
    std::size_t capacity) noexcept
{
    if (!ledger || (!output && capacity)) return 0;
    TryLedgerGuard guard(ledger);
    if (!guard.locked()) return 0;

    std::size_t copied = 0;
    const std::size_t domain_count = static_cast<std::size_t>(
        ContentDomainId::Count);
    for (std::size_t index = 0;
         index < domain_count && copied < capacity; ++index) {
        if (!ledger->domain_touches[index].touched) continue;
        output[copied++] = ledger->domain_touches[index];
    }
    return copied;
}

const char* CandidateRetentionClassificationName(
    CandidateRetentionClassification classification) noexcept
{
    switch (classification) {
    case CandidateRetentionClassification::Candidate:
        return "Candidate";
    case CandidateRetentionClassification::Ephemeral:
        return "Ephemeral";
    case CandidateRetentionClassification::NaturallyReplaced:
        return "NaturallyReplaced";
    case CandidateRetentionClassification::NaturallyReplacedByFrontend:
        return "NaturallyReplacedByFrontend";
    case CandidateRetentionClassification::Retained:
        return "Retained";
    case CandidateRetentionClassification::UnknownAtBarrier:
        return "UnknownAtBarrier";
    default:
        return "Unknown";
    }
}

const char* CandidateRetainingObserverName(
    CandidateRetainingObserver observer) noexcept
{
    switch (observer) {
    case CandidateRetainingObserver::Unknown: return "unknown";
    case CandidateRetainingObserver::BroadFilesystemOpen:
        return "broad_filesystem_open";
    case CandidateRetainingObserver::FilesystemOwner:
        return "filesystem_owner";
    case CandidateRetainingObserver::PlayerGameplayParams:
        return "player_gameplay_params";
    case CandidateRetainingObserver::DefinitionManager:
        return "definition_manager";
    case CandidateRetainingObserver::ContentOnDemand:
        return "content_on_demand";
    case CandidateRetainingObserver::LocalMaterialManager:
        return "local_material_manager";
    case CandidateRetainingObserver::AudioDataManager:
        return "audio_data_manager";
    case CandidateRetainingObserver::AudioBankManager:
        return "audio_bank_manager";
    case CandidateRetainingObserver::FrontendResourceManager:
        return "frontend_resource_manager";
    case CandidateRetainingObserver::OtherValidatedManager:
        return "other_validated_manager";
    default: return "invalid";
    }
}

const char* CandidateUnresolvedReasonName(
    CandidateUnresolvedReason reason) noexcept
{
    switch (reason) {
    case CandidateUnresolvedReason::None: return "none";
    case CandidateUnresolvedReason::BroadOpenOnly:
        return "broad_open_only";
    case CandidateUnresolvedReason::AwaitingOwnerCorrelation:
        return "awaiting_owner_correlation";
    case CandidateUnresolvedReason::LogicalIdentityUnavailable:
        return "logical_identity_unavailable";
    case CandidateUnresolvedReason::ExactObserverUnavailable:
        return "exact_observer_unavailable";
    case CandidateUnresolvedReason::ExactManagerUnavailable:
        return "exact_manager_unavailable";
    case CandidateUnresolvedReason::ExactProviderUnavailable:
        return "exact_provider_unavailable";
    case CandidateUnresolvedReason::RetentionLifetimeUnproven:
        return "retention_lifetime_unproven";
    case CandidateUnresolvedReason::InvalidEvidence:
        return "invalid_evidence";
    case CandidateUnresolvedReason::SequenceRegression:
        return "sequence_regression";
    case CandidateUnresolvedReason::ObserverLoss:
        return "observer_loss";
    case CandidateUnresolvedReason::CapacityExceeded:
        return "capacity_exceeded";
    case CandidateUnresolvedReason::LedgerBusy:
        return "ledger_busy";
    case CandidateUnresolvedReason::BarrierReached:
        return "barrier_reached";
    case CandidateUnresolvedReason::LateAfterBarrier:
        return "late_after_barrier";
    default: return "unknown";
    }
}

const char* CandidateLedgerResultName(CandidateLedgerResult result) noexcept
{
    switch (result) {
    case CandidateLedgerResult::Succeeded: return "succeeded";
    case CandidateLedgerResult::InvalidArguments:
        return "invalid_arguments";
    case CandidateLedgerResult::LedgerBusy: return "ledger_busy";
    case CandidateLedgerResult::EpochCapacityExceeded:
        return "epoch_capacity_exceeded";
    case CandidateLedgerResult::RecordCapacityExceeded:
        return "record_capacity_exceeded";
    case CandidateLedgerResult::EpochSealed: return "epoch_sealed";
    case CandidateLedgerResult::StaleHandle: return "stale_handle";
    case CandidateLedgerResult::InvalidTransition:
        return "invalid_transition";
    case CandidateLedgerResult::ExactOwnerRequired:
        return "exact_owner_required";
    case CandidateLedgerResult::EvidenceIncomplete:
        return "evidence_incomplete";
    default: return "unknown";
    }
}

const char* CandidateObserverCoverageName(
    CandidateObserverCoverage coverage) noexcept
{
    switch (coverage) {
    case CandidateObserverCoverage::Unknown: return "unknown";
    case CandidateObserverCoverage::Complete: return "complete";
    case CandidateObserverCoverage::Incomplete: return "incomplete";
    default: return "invalid";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::provenance
