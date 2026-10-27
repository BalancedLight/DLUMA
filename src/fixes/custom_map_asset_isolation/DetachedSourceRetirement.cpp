#define _HAS_EXCEPTIONS 0
#include "DetachedSourceRetirement.h"
#include <cstring>
#include <limits>

namespace dluma::fixes::custom_map_asset_isolation::provider_retirement {
namespace {
class Guard {
public:
    explicit Guard(volatile LONG* lock) noexcept : lock_(lock), held_(false) {
        if (lock_) held_ = dluma_atomic_compare_exchange(lock_, 1, 0) == 0;
    }
    ~Guard() { if (held_) dluma_atomic_exchange(lock_, 0); }
    bool held() const noexcept { return held_; }
private:
    volatile LONG* lock_;
    bool held_;
};
bool ValidIdentity(const SourceIdentity& key) noexcept {
    return key.source && key.source_vtable && key.original_registry_link &&
        key.source_generation && key.owner_generation && key.epoch &&
        key.successful_add_ordinal && key.canonical_path[0] &&
        std::memchr(key.canonical_path, 0, sizeof(key.canonical_path));
}
bool HandleEqual(SourceHandle a, SourceHandle b) noexcept {
    return a.ledger_lifetime == b.ledger_lifetime &&
        a.entry_generation == b.entry_generation && a.index == b.index;
}
SourceRecord* Find(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle) noexcept {
    if (!ledger || !handle.ledger_lifetime || !handle.entry_generation ||
        handle.ledger_lifetime != ledger->lifetime_generation ||
        handle.index >= kMaximumRetirementSources) return nullptr;
    SourceRecord* record = &ledger->sources[handle.index];
    return record->phase != SourcePhase::Empty &&
        record->entry_generation == handle.entry_generation ? record : nullptr;
}
RetirementResult Lost(DetachedSourceRetirementLedger* ledger) noexcept {
    if (ledger) dluma_atomic_exchange(&ledger->observation_lost, 1);
    return RetirementResult::ObservationLost;
}
bool IsLost(DetachedSourceRetirementLedger* ledger) noexcept {
    return dluma_atomic_compare_exchange(&ledger->observation_lost, 0, 0) != 0;
}
bool Advance(std::uint64_t* value) noexcept {
    if (*value == (std::numeric_limits<std::uint64_t>::max)()) return false;
    ++*value;
    return true;
}
RetirementResult SetResult(SourceRecord* record, RetirementResult result) noexcept {
    if (record) record->last_result = result;
    return result;
}
bool ValidContext(const RetirementContext& context, const SourceRecord& record) noexcept {
    return context.transaction_generation && context.source_registry_generation &&
        context.through_epoch >= record.identity.epoch;
}
}  // namespace

RetirementResult InitializeLedger(DetachedSourceRetirementLedger* ledger,
    std::uint64_t lifetime) noexcept {
    if (!ledger || !lifetime) return RetirementResult::InvalidArgument;
    Guard guard(&ledger->lock);
    if (!guard.held()) return RetirementResult::Busy;
    if (ledger->lifetime_generation)
        return ledger->lifetime_generation == lifetime
            ? RetirementResult::AlreadyComplete : RetirementResult::GenerationMismatch;
    ledger->lifetime_generation = lifetime;
    return RetirementResult::Succeeded;
}
bool ExactSourceIdentityEqual(const SourceIdentity& a,
    const SourceIdentity& b) noexcept {
    return ValidIdentity(a) && ValidIdentity(b) && a.source == b.source &&
        a.source_vtable == b.source_vtable &&
        a.original_registry_link == b.original_registry_link &&
        a.source_generation == b.source_generation &&
        a.owner_generation == b.owner_generation && a.epoch == b.epoch &&
        a.successful_add_ordinal == b.successful_add_ordinal &&
        std::strlen(a.canonical_path) == std::strlen(b.canonical_path) &&
        std::memcmp(a.canonical_path, b.canonical_path,
            std::strlen(a.canonical_path)) == 0;
}
RetirementResult RegisterSource(DetachedSourceRetirementLedger* ledger,
    const SourceIdentity* key, SourceOwnership ownership, SourceHandle* handle) noexcept {
    if (!ledger || !key || !handle || !ValidIdentity(*key))
        return RetirementResult::InvalidArgument;
    *handle = {};
    if (ownership != SourceOwnership::ExclusivePathCreated)
        return RetirementResult::OwnershipRejected;
    Guard guard(&ledger->lock);
    if (!guard.held()) return RetirementResult::Busy;
    if (!ledger->lifetime_generation) return RetirementResult::InvalidArgument;
    if (IsLost(ledger)) return RetirementResult::ObservationLost;
    std::size_t vacant = kMaximumRetirementSources;
    for (std::size_t index = 0; index < kMaximumRetirementSources; ++index) {
        const SourceRecord& record = ledger->sources[index];
        if (record.phase == SourcePhase::Empty || record.phase == SourcePhase::Destroyed) {
            if (vacant == kMaximumRetirementSources) vacant = index;
        }
        if (record.phase == SourcePhase::Empty) continue;
        if (ExactSourceIdentityEqual(record.identity, *key)) {
            if (record.phase == SourcePhase::Destroyed)
                return RetirementResult::IdentityConflict;
            *handle = {ledger->lifetime_generation, record.entry_generation,
                static_cast<std::uint32_t>(index)};
            return RetirementResult::AlreadyComplete;
        }
        if (record.identity.source == key->source) {
            if (record.phase != SourcePhase::Destroyed ||
                key->source_generation <= record.identity.source_generation)
                return RetirementResult::IdentityConflict;
        }
    }
    if (vacant == kMaximumRetirementSources ||
        !Advance(&ledger->next_entry_generation)) return RetirementResult::CapacityExceeded;
    SourceRecord& record = ledger->sources[vacant];
    record = {};
    record.identity = *key;
    record.entry_generation = ledger->next_entry_generation;
    record.phase = SourcePhase::Mounted;
    record.last_result = RetirementResult::Succeeded;
    *handle = {ledger->lifetime_generation, record.entry_generation,
        static_cast<std::uint32_t>(vacant)};
    return RetirementResult::Succeeded;
}
RetirementResult SnapshotSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle, SourceRecord* output) noexcept {
    if (!ledger || !output) return RetirementResult::InvalidArgument;
    Guard guard(&ledger->lock);
    if (!guard.held()) return RetirementResult::Busy;
    const SourceRecord* record = Find(ledger, handle);
    if (!record) return RetirementResult::StaleHandle;
    *output = *record;
    return RetirementResult::Succeeded;
}
RetirementResult SnapshotRetirementSummary(
    DetachedSourceRetirementLedger* ledger, RetirementSummary* output) noexcept {
    if (!ledger || !output) return RetirementResult::InvalidArgument;
    Guard guard(&ledger->lock);
    if (!guard.held()) return RetirementResult::Busy;
    *output = {};
    output->observation_lost = IsLost(ledger);
    for (const SourceRecord& source : ledger->sources) {
        output->active_consumers += source.active_consumers;
        switch (source.phase) {
        case SourcePhase::Mounted: ++output->mounted; break;
        case SourcePhase::Detached: ++output->detached; break;
        case SourcePhase::Quarantined: ++output->quarantined; break;
        case SourcePhase::Detaching:
        case SourcePhase::Destroying: ++output->in_progress; break;
        case SourcePhase::Destroyed: ++output->destroyed; break;
        default: break;
        }
    }
    return RetirementResult::Succeeded;
}
RetirementResult BeginSourceConsumer(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle, ConsumerToken* token) noexcept {
    if (!ledger || !token) return RetirementResult::InvalidArgument;
    if (token->reservation) return RetirementResult::InvalidArgument;
    *token = {};
    Guard guard(&ledger->lock);
    if (!guard.held()) return Lost(ledger);
    SourceRecord* record = Find(ledger, handle);
    if (!record) return RetirementResult::StaleHandle;
    if (IsLost(ledger)) return RetirementResult::ObservationLost;
    if (!Advance(&record->consumer_revision)) return Lost(ledger);
    if (record->phase != SourcePhase::Mounted) {
        record->late_consumer_latched = true;
        return SetResult(record, RetirementResult::LateConsumer);
    }
    for (std::size_t index = 0; index < kMaximumSourceConsumers; ++index) {
        ConsumerReservation& reservation = ledger->consumers[index];
        if (reservation.active) continue;
        if (!Advance(&ledger->next_reservation)) return Lost(ledger);
        reservation = {handle, ledger->next_reservation, true};
        ++record->active_consumers;
        *token = {handle, reservation.reservation, static_cast<std::uint32_t>(index)};
        return RetirementResult::Succeeded;
    }
    return Lost(ledger);
}
RetirementResult EndSourceConsumer(DetachedSourceRetirementLedger* ledger,
    ConsumerToken* token) noexcept {
    if (!ledger || !token || !token->reservation ||
        token->index >= kMaximumSourceConsumers) return RetirementResult::InvalidArgument;
    Guard guard(&ledger->lock);
    if (!guard.held()) return Lost(ledger);
    ConsumerReservation& reservation = ledger->consumers[token->index];
    SourceRecord* record = Find(ledger, token->source);
    if (!record || !reservation.active || reservation.reservation != token->reservation ||
        !HandleEqual(reservation.source, token->source)) return RetirementResult::StaleHandle;
    if (!record->active_consumers || !Advance(&record->consumer_revision)) return Lost(ledger);
    --record->active_consumers;
    reservation.active = false;
    *token = {};
    return RetirementResult::Succeeded;
}
RetirementResult ObserveLateSourceConsumer(DetachedSourceRetirementLedger* ledger,
    SourceHandle source) noexcept {
    if (!ledger) return RetirementResult::InvalidArgument;
    Guard guard(&ledger->lock);
    if (!guard.held()) return Lost(ledger);
    SourceRecord* record = Find(ledger, source);
    if (!record) return RetirementResult::StaleHandle;
    record->late_consumer_latched = true;
    if (!Advance(&record->consumer_revision)) return Lost(ledger);
    return SetResult(record, RetirementResult::LateConsumer);
}

RetirementResult DetachSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle, const RetirementContext* context,
    const RetirementCallbacks* callbacks) noexcept {
    if (!ledger || !context || !callbacks || !callbacks->verify || !callbacks->detach)
        return RetirementResult::InvalidArgument;
    if (!callbacks->exact_detach_without_destroy_validated)
        return RetirementResult::EvidenceUnavailable;
    Guard operation(&ledger->mutation_lock);
    if (!operation.held()) return RetirementResult::Busy;
    SourceIdentity identity = {};
    {
        Guard guard(&ledger->lock);
        if (!guard.held()) return RetirementResult::Busy;
        SourceRecord* record = Find(ledger, handle);
        if (!record) return RetirementResult::StaleHandle;
        if (!ValidContext(*context, *record)) return RetirementResult::GenerationMismatch;
        if (record->phase == SourcePhase::Detached || record->phase == SourcePhase::Destroyed)
            return record->detach_transaction_generation == context->transaction_generation
                ? RetirementResult::AlreadyComplete : RetirementResult::GenerationMismatch;
        if (record->phase != SourcePhase::Mounted) return RetirementResult::InvalidPhase;
        if (IsLost(ledger)) return RetirementResult::ObservationLost;
        if (record->late_consumer_latched) return RetirementResult::LateConsumer;
        record->phase = SourcePhase::Detaching; // closes admission before native unlink
        identity = record->identity;
    }
    std::uint64_t generation = 0;
    const bool verified = callbacks->verify(callbacks->user, &identity,
        SourceLocation::Mounted, &generation) &&
        generation == context->source_registry_generation;
    if (!verified) {
        Guard guard(&ledger->lock);
        if (!guard.held()) return Lost(ledger);
        SourceRecord* record = Find(ledger, handle);
        record->phase = SourcePhase::Mounted;
        return SetResult(record, RetirementResult::VerificationFailed);
    }
    {
        Guard guard(&ledger->lock);
        if (!guard.held()) return Lost(ledger);
        SourceRecord* record = Find(ledger, handle);
        if (IsLost(ledger) || record->late_consumer_latched) {
            record->phase = SourcePhase::Mounted;
            return SetResult(record, IsLost(ledger) ? RetirementResult::ObservationLost
                : RetirementResult::LateConsumer);
        }
        ++record->detach_attempts;
    }
    const MutationOutcome outcome = callbacks->detach(callbacks->user, &identity);
    generation = 0;
    const bool detached = outcome == MutationOutcome::Succeeded &&
        callbacks->verify(callbacks->user, &identity, SourceLocation::Detached, &generation) &&
        generation > context->source_registry_generation;
    Guard guard(&ledger->lock);
    if (!guard.held()) return Lost(ledger); // retain the in-progress entry on lost observation
    SourceRecord* record = Find(ledger, handle);
    if (outcome == MutationOutcome::FailedWithoutMutation) {
        record->phase = SourcePhase::Mounted;
        return SetResult(record, RetirementResult::MutationFailed);
    }
    record->detach_transaction_generation = context->transaction_generation;
    record->detached_registry_generation = generation;
    if (!detached || IsLost(ledger) || record->late_consumer_latched) {
        record->phase = SourcePhase::Quarantined;
        return SetResult(record, RetirementResult::MutationUncertain);
    }
    record->phase = SourcePhase::Detached;
    return SetResult(record, RetirementResult::Succeeded);
}

RetirementResult DestroyDetachedSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle, const ConsumerReleaseProof* proof,
    const RetirementCallbacks* callbacks) noexcept {
    if (!ledger || !proof || !callbacks || !callbacks->verify || !callbacks->destroy)
        return RetirementResult::InvalidArgument;
    if (!callbacks->exact_destroy_ownership_validated)
        return RetirementResult::EvidenceUnavailable;
    Guard operation(&ledger->mutation_lock);
    if (!operation.held()) return RetirementResult::Busy;
    SourceIdentity identity = {};
    {
        Guard guard(&ledger->lock);
        if (!guard.held()) return RetirementResult::Busy;
        SourceRecord* record = Find(ledger, handle);
        if (!record) return RetirementResult::StaleHandle;
        if (!ValidContext(proof->context, *record) ||
            proof->context.transaction_generation != record->detach_transaction_generation)
            return RetirementResult::GenerationMismatch;
        if (record->phase == SourcePhase::Destroyed) return RetirementResult::AlreadyComplete;
        if (record->phase != SourcePhase::Detached)
            return SetResult(record, RetirementResult::InvalidPhase);
        if (IsLost(ledger)) return SetResult(record, RetirementResult::ObservationLost);
        if (record->late_consumer_latched) return SetResult(record, RetirementResult::LateConsumer);
        if (record->active_consumers) return SetResult(record, RetirementResult::ActiveConsumers);
        if (!proof->all_participating_consumers_released ||
            proof->completed_release_generation != proof->context.transaction_generation ||
            proof->source_consumer_revision != record->consumer_revision ||
            proof->context.source_registry_generation < record->detached_registry_generation)
            return SetResult(record, RetirementResult::ConsumerReleaseIncomplete);
        identity = record->identity;
    }
    std::uint64_t generation = 0;
    if (!callbacks->verify(callbacks->user, &identity, SourceLocation::Detached, &generation) ||
        generation != proof->context.source_registry_generation)
        return RetirementResult::VerificationFailed;
    {
        Guard guard(&ledger->lock);
        if (!guard.held()) return Lost(ledger);
        SourceRecord* record = Find(ledger, handle);
        if (IsLost(ledger)) return RetirementResult::ObservationLost;
        if (record->late_consumer_latched) return RetirementResult::LateConsumer;
        if (record->active_consumers || record->consumer_revision != proof->source_consumer_revision)
            return RetirementResult::ConsumerReleaseIncomplete;
        record->phase = SourcePhase::Destroying;
        ++record->destroy_attempts;
    }
    const MutationOutcome outcome = callbacks->destroy(callbacks->user, &identity);
    Guard guard(&ledger->lock);
    if (!guard.held()) return Lost(ledger);
    SourceRecord* record = Find(ledger, handle);
    if (outcome == MutationOutcome::Succeeded) {
        record->phase = SourcePhase::Destroyed;
        return SetResult(record, IsLost(ledger) ? RetirementResult::ObservationLost :
            record->late_consumer_latched ? RetirementResult::LateConsumer : RetirementResult::Succeeded);
    }
    record->phase = outcome == MutationOutcome::FailedWithoutMutation
        ? SourcePhase::Detached : SourcePhase::Quarantined;
    return SetResult(record, outcome == MutationOutcome::FailedWithoutMutation
        ? RetirementResult::MutationFailed : RetirementResult::MutationUncertain);
}
const char* RetirementResultName(RetirementResult result) noexcept {
    switch (result) {
#define DLUMA_RETIREMENT_CASE(value) case RetirementResult::value: return #value
    DLUMA_RETIREMENT_CASE(Succeeded); DLUMA_RETIREMENT_CASE(AlreadyComplete);
    DLUMA_RETIREMENT_CASE(InvalidArgument); DLUMA_RETIREMENT_CASE(Busy);
    DLUMA_RETIREMENT_CASE(CapacityExceeded); DLUMA_RETIREMENT_CASE(OwnershipRejected);
    DLUMA_RETIREMENT_CASE(IdentityConflict); DLUMA_RETIREMENT_CASE(StaleHandle);
    DLUMA_RETIREMENT_CASE(GenerationMismatch); DLUMA_RETIREMENT_CASE(EvidenceUnavailable);
    DLUMA_RETIREMENT_CASE(VerificationFailed); DLUMA_RETIREMENT_CASE(ActiveConsumers);
    DLUMA_RETIREMENT_CASE(ConsumerReleaseIncomplete); DLUMA_RETIREMENT_CASE(LateConsumer);
    DLUMA_RETIREMENT_CASE(MutationFailed); DLUMA_RETIREMENT_CASE(MutationUncertain);
    DLUMA_RETIREMENT_CASE(ObservationLost); DLUMA_RETIREMENT_CASE(InvalidPhase);
#undef DLUMA_RETIREMENT_CASE
    }
    return "Unknown";
}
}  // namespace dluma::fixes::custom_map_asset_isolation::provider_retirement
