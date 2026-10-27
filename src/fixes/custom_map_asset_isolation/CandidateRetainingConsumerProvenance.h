#ifndef DLUMA_FIXES_CANDIDATE_RETAINING_CONSUMER_PROVENANCE_H
#define DLUMA_FIXES_CANDIDATE_RETAINING_CONSUMER_PROVENANCE_H

#include "ContentDomainRestartRegistry.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dluma::fixes::custom_map_asset_isolation::provenance {

/* This ledger is bounded. Records remain immutable until verified retirement.
 * Hook-facing inputs contain their own bounded text storage, so recording an
 * observation never follows an engine string pointer or allocates memory. */
constexpr std::size_t kCandidateRetainingEpochCapacity = 64;
constexpr std::size_t kCandidateRetainingRecordCapacity = 256;
constexpr std::size_t kCandidateRetainingObserverCapacity = 64;
constexpr std::size_t kCandidateRetainingLogicalNameCapacity = 256;
constexpr std::size_t kCandidateRetainingProviderCapacity = 512;
constexpr std::size_t kCandidateRetainingReasonCapacity = 128;
constexpr std::size_t kCandidateRetainingLossEpochCapacity = 64;

enum class CandidateRetentionClassification : std::uint8_t {
    Candidate = 0,
    Ephemeral,
    NaturallyReplaced,
    /* A narrow CMaterialMgr local-pack slot contract: the candidate is
     * replaced by the next frontend reload after BF8. This does not assert
     * that the candidate object has already been destroyed. */
    NaturallyReplacedByFrontend,
    Retained,
    UnknownAtBarrier,
};

enum class CandidateRetainingObserver : std::uint8_t {
    Unknown = 0,
    BroadFilesystemOpen,
    FilesystemOwner,
    PlayerGameplayParams,
    DefinitionManager,
    ContentOnDemand,
    LocalMaterialManager,
    AudioDataManager,
    AudioBankManager,
    FrontendResourceManager,
    OtherValidatedManager,
};

enum class CandidateUnresolvedReason : std::uint8_t {
    None = 0,
    BroadOpenOnly,
    AwaitingOwnerCorrelation,
    LogicalIdentityUnavailable,
    ExactObserverUnavailable,
    ExactManagerUnavailable,
    ExactProviderUnavailable,
    RetentionLifetimeUnproven,
    InvalidEvidence,
    SequenceRegression,
    ObserverLoss,
    CapacityExceeded,
    LedgerBusy,
    BarrierReached,
    LateAfterBarrier,
};

enum class CandidateLedgerResult : std::uint8_t {
    Succeeded = 0,
    InvalidArguments,
    LedgerBusy,
    EpochCapacityExceeded,
    RecordCapacityExceeded,
    EpochSealed,
    StaleHandle,
    InvalidTransition,
    ExactOwnerRequired,
    EvidenceIncomplete,
};

enum class CandidateObserverCoverage : std::uint8_t {
    Unknown = 0,
    Complete,
    Incomplete,
};

struct CandidateRetainingConsumerHandle {
    std::uint64_t record_generation;
    std::uint32_t epoch;
    std::uint32_t record_index;
};

/* The caller must safely populate and terminate every text field before it
 * enters a hook-facing Record call.  The ledger copies only these arrays. */
struct CandidateRetainingConsumerInput {
    std::uint64_t sequence;
    std::uint32_t epoch;
    cold_reboot::ContentDomainId proposed_domain;
    CandidateRetainingObserver observer;
    std::uintptr_t observer_target;
    std::uintptr_t manager;
    std::uintptr_t owner_object;
    std::uintptr_t source;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    bool exact_source;
    bool exact_provider;
    char observer_name[kCandidateRetainingObserverCapacity];
    char logical_name[kCandidateRetainingLogicalNameCapacity];
    char provider[kCandidateRetainingProviderCapacity];
};

/* Correlation is intentionally distinct from the broad-open candidate.  A
 * terminal classification is rejected until an independently validated
 * manager or owner object has been correlated. */
struct CandidateOwnerCorrelation {
    std::uint64_t sequence;
    cold_reboot::ContentDomainId resolved_domain;
    CandidateRetainingObserver observer;
    std::uintptr_t observer_target;
    std::uintptr_t manager;
    std::uintptr_t owner_object;
    std::uintptr_t source;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    bool exact_observer;
    bool exact_owner;
    bool exact_source;
    bool exact_provider;
    char observer_name[kCandidateRetainingObserverCapacity];
    char logical_name[kCandidateRetainingLogicalNameCapacity];
    char provider[kCandidateRetainingProviderCapacity];
};

struct CandidateRetentionEvidence {
    std::uint64_t sequence;
    CandidateRetentionClassification classification;
    bool ephemeral_completion_observed;
    bool natural_replacement_observed;
    bool frontend_replacement_contract_observed;
    bool natural_unload_observed;
    bool retained_value_observed;
    char detail[kCandidateRetainingReasonCapacity];
};

struct CandidateUnresolvedEvidence {
    CandidateUnresolvedReason reason;
    char detail[kCandidateRetainingReasonCapacity];
};

struct CandidateRetainingConsumerRecord {
    std::uint64_t record_generation;
    std::uint64_t first_candidate_sequence;
    std::uint64_t owner_correlation_sequence;
    std::uint64_t classification_sequence;
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    cold_reboot::ContentDomainId domain;
    CandidateRetainingObserver observer;
    CandidateRetentionClassification classification;
    CandidateUnresolvedReason last_unresolved_reason;
    std::uintptr_t observer_target;
    std::uintptr_t manager;
    std::uintptr_t owner_object;
    std::uintptr_t source;
    bool exact_observer;
    bool exact_owner_correlated;
    bool exact_source;
    bool exact_provider;
    bool ephemeral_completion_observed;
    bool natural_replacement_observed;
    bool frontend_replacement_contract_observed;
    bool natural_unload_observed;
    bool retained_value_observed;
    char observer_name[kCandidateRetainingObserverCapacity];
    char logical_name[kCandidateRetainingLogicalNameCapacity];
    char provider[kCandidateRetainingProviderCapacity];
    char last_unresolved_detail[kCandidateRetainingReasonCapacity];
};

/* The first proven touch for each concrete domain is immutable.  Later
 * naturally-replaced candidates cannot erase or rewrite it. */
struct CandidateDomainTouchProvenance {
    std::uint64_t first_touch_sequence;
    std::uint64_t record_generation;
    std::uint32_t epoch;
    cold_reboot::ContentDomainId domain;
    CandidateRetainingObserver observer;
    CandidateRetentionClassification classification;
    CandidateUnresolvedReason unresolved_reason;
    std::uintptr_t observer_target;
    std::uintptr_t manager;
    std::uintptr_t owner_object;
    std::uintptr_t source;
    bool touched;
    bool exact_source;
    bool exact_provider;
    char observer_name[kCandidateRetainingObserverCapacity];
    char logical_name[kCandidateRetainingLogicalNameCapacity];
    char provider[kCandidateRetainingProviderCapacity];
    char unresolved_detail[kCandidateRetainingReasonCapacity];
};

struct Bf8CandidatePromotionDecision {
    std::uint64_t barrier_sequence;
    std::uint32_t epoch;
    std::uint32_t record_count;
    std::uint32_t ephemeral_count;
    std::uint32_t naturally_replaced_count;
    std::uint32_t retained_count;
    std::uint32_t unknown_at_barrier_count;
    std::uint32_t observer_loss_count;
    cold_reboot::ContentDomainMask immutable_true_touch_mask;
    cold_reboot::ContentDomainMask touch_mask_to_promote;
    cold_reboot::ContentDomainMask observer_coverage_complete_mask;
    cold_reboot::ContentDomainMask observer_coverage_incomplete_mask;
    cold_reboot::ContentDomainMask unresolved_domain_mask;
    cold_reboot::ContentDomainMask required_observer_coverage_mask;
    bool promote_unclassified;
    bool all_candidates_resolved;
    bool preflight_allowed;
    bool observer_loss_latched;
    bool epoch_sealed;
};

struct CandidateRetainingEpochState {
    std::uint64_t first_candidate_sequence;
    std::uint64_t last_candidate_sequence;
    std::uint64_t barrier_sequence;
    std::uint32_t epoch;
    std::uint32_t record_count;
    std::uint32_t observer_loss_count;
    cold_reboot::ContentDomainMask immutable_true_touch_mask;
    cold_reboot::ContentDomainMask observer_coverage_complete_mask;
    cold_reboot::ContentDomainMask observer_coverage_incomplete_mask;
    bool active;
    bool barrier_evaluated;
    bool sealed;
    Bf8CandidatePromotionDecision decision;
};

struct CandidateRetainingConsumerLedger {
    volatile LONG lock;
    std::uint32_t record_count;
    std::uint32_t epoch_count;
    std::uint64_t next_record_generation;
    CandidateRetainingConsumerRecord
        records[kCandidateRetainingRecordCapacity];
    CandidateRetainingEpochState epochs[kCandidateRetainingEpochCapacity];
    CandidateDomainTouchProvenance domain_touches[
        static_cast<std::size_t>(cold_reboot::ContentDomainId::Count)];
    /* Hook-side lock/capacity losses are latched without waiting.  Epoch
     * slots are insert-only; saturation becomes an unattributed global loss
     * and therefore conservatively blocks every BF8 decision. */
    volatile LONG loss_epochs[kCandidateRetainingLossEpochCapacity];
    volatile LONG loss_counts[kCandidateRetainingLossEpochCapacity];
    volatile LONG loss_reasons[kCandidateRetainingLossEpochCapacity];
    volatile LONG unattributed_loss_latched;
    volatile LONG dropped_observation_count;
    volatile LONG contention_loss_count;
    volatile LONG capacity_loss_count;
    volatile LONG late_after_barrier_count;
    cold_reboot::ContentDomainMask required_observer_coverage_mask;
    std::uint32_t retired_through_epoch;
    std::uint64_t retired_transaction,retired_epochs,retired_records;
};

static_assert(std::is_trivial<
                  CandidateRetainingConsumerRecord>::value &&
              std::is_standard_layout<
                  CandidateRetainingConsumerRecord>::value,
              "candidate provenance records must remain POD");
static_assert(std::is_trivial<
                  CandidateRetainingConsumerLedger>::value &&
              std::is_standard_layout<
                  CandidateRetainingConsumerLedger>::value,
              "candidate provenance ledger must remain POD");

void InitializeCandidateRetainingConsumerLedger(
    CandidateRetainingConsumerLedger* ledger) noexcept;
// Reuse only a sealed, resolved epoch during the actual registry finalizer.
// Baseline coverage and loss evidence remain intact; stale handles cannot
// correlate a later record occupying the same slot.
CandidateLedgerResult RetireCandidateEpoch(CandidateRetainingConsumerLedger*,
    const cold_reboot::ContentDomainRestartRegistry*,const cold_reboot::RestartContext*,
    const cold_reboot::RegistryFinalizationToken*) noexcept;
CandidateLedgerResult CheckCandidateEpochRetirement(CandidateRetainingConsumerLedger*,
    const cold_reboot::ContentDomainRestartRegistry*,const cold_reboot::RestartContext*,
    const cold_reboot::RegistryFinalizationToken*) noexcept;

bool SetRequiredCandidateObserverCoverageMask(
    CandidateRetainingConsumerLedger* ledger,
    cold_reboot::ContentDomainMask required_mask) noexcept;

CandidateLedgerResult RecordCandidateDomainObserverCoverage(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    std::uint64_t sequence, cold_reboot::ContentDomainId domain,
    CandidateObserverCoverage coverage) noexcept;

CandidateLedgerResult RecordCandidateRetainingConsumer(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerInput* input,
    CandidateRetainingConsumerHandle* output) noexcept;

CandidateLedgerResult CorrelateCandidateRetainingOwner(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateOwnerCorrelation* correlation) noexcept;

CandidateLedgerResult UpdateCandidateUnresolvedReason(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateUnresolvedEvidence* evidence) noexcept;

CandidateLedgerResult ClassifyCandidateRetainingConsumer(
    CandidateRetainingConsumerLedger* ledger,
    const CandidateRetainingConsumerHandle* handle,
    const CandidateRetentionEvidence* evidence) noexcept;

/* This function never mutates ContentDomainRestartRegistry.  Its immutable
 * touch_mask_to_promote is the future BF8 owner's input to MarkDomainTouched,
 * retaining the existing mandatory | touched transaction rule. */
CandidateLedgerResult EvaluateCandidateRetainingConsumersAtBf8(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    std::uint64_t barrier_sequence,
    Bf8CandidatePromotionDecision* output) noexcept;

std::size_t SnapshotCandidateRetainingEpoch(
    CandidateRetainingConsumerLedger* ledger, std::uint32_t epoch,
    CandidateRetainingConsumerRecord* records,
    std::size_t record_capacity,
    CandidateRetainingEpochState* epoch_state) noexcept;

std::size_t SnapshotCandidateDomainTouches(
    CandidateRetainingConsumerLedger* ledger,
    CandidateDomainTouchProvenance* output,
    std::size_t capacity) noexcept;

const char* CandidateRetentionClassificationName(
    CandidateRetentionClassification classification) noexcept;
const char* CandidateRetainingObserverName(
    CandidateRetainingObserver observer) noexcept;
const char* CandidateUnresolvedReasonName(
    CandidateUnresolvedReason reason) noexcept;
const char* CandidateLedgerResultName(CandidateLedgerResult result) noexcept;
const char* CandidateObserverCoverageName(
    CandidateObserverCoverage coverage) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::provenance

#endif
