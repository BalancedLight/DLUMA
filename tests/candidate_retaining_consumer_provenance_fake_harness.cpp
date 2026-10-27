#include "fixes/custom_map_asset_isolation/CandidateRetainingConsumerProvenance.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace provenance =
    dluma::fixes::custom_map_asset_isolation::provenance;
namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;

using provenance::Bf8CandidatePromotionDecision;
using provenance::CandidateDomainTouchProvenance;
using provenance::CandidateLedgerResult;
using provenance::CandidateOwnerCorrelation;
using provenance::CandidateRetainingConsumerHandle;
using provenance::CandidateRetainingConsumerInput;
using provenance::CandidateRetainingConsumerLedger;
using provenance::CandidateRetainingConsumerRecord;
using provenance::CandidateRetainingObserver;
using provenance::CandidateRetentionClassification;
using provenance::CandidateRetentionEvidence;

static_assert(std::is_trivial<
                  CandidateRetainingConsumerLedger>::value,
              "candidate ledger must remain trivial POD");
static_assert(std::is_standard_layout<
                  CandidateRetainingConsumerLedger>::value,
              "candidate ledger must remain standard-layout POD");
static_assert(std::is_trivial<
                  CandidateRetainingConsumerRecord>::value,
              "candidate records must remain trivial POD");

namespace {

CandidateRetainingConsumerLedger g_ledger;

bool Expect(bool condition, const char* expression,
            int line) noexcept
{
    if (condition) return true;
    std::printf("failure at line %d: %s\n", line, expression);
    return false;
}

#define EXPECT(value) \
    do { if (!Expect((value), #value, __LINE__)) return false; } while (0)

void CopyText(char* output, std::size_t capacity,
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
    output[index] = 0;
}

CandidateRetainingConsumerInput MakeCandidate(
    std::uint64_t sequence, std::uint32_t epoch,
    cold::ContentDomainId domain =
        cold::ContentDomainId::UnclassifiedPersistentConsumers) noexcept
{
    CandidateRetainingConsumerInput input = {};
    input.sequence = sequence;
    input.epoch = epoch;
    input.proposed_domain = domain;
    input.observer = CandidateRetainingObserver::BroadFilesystemOpen;
    input.observer_target = 0x1000U;
    input.source = 0x2000U + epoch;
    input.source_record_index = epoch + 3U;
    input.source_add_ordinal = epoch + 7U;
    input.exact_source = true;
    input.exact_provider = true;
    CopyText(input.observer_name, sizeof(input.observer_name),
             "fs::open");
    CopyText(input.logical_name, sizeof(input.logical_name),
             "data\\scripts\\default_levels.xml");
    CopyText(input.provider, sizeof(input.provider),
             "X:\\workshop\\project\\data");
    return input;
}

CandidateOwnerCorrelation MakeCorrelation(
    std::uint64_t sequence, cold::ContentDomainId domain,
    CandidateRetainingObserver observer =
        CandidateRetainingObserver::DefinitionManager) noexcept
{
    CandidateOwnerCorrelation correlation = {};
    correlation.sequence = sequence;
    correlation.resolved_domain = domain;
    correlation.observer = observer;
    correlation.observer_target = 0x3000U;
    correlation.manager = 0x4000U;
    correlation.owner_object = 0x5000U;
    correlation.source = 0x6000U;
    correlation.source_record_index = 9U;
    correlation.source_add_ordinal = 11U;
    correlation.exact_observer = true;
    correlation.exact_owner = true;
    correlation.exact_source = true;
    correlation.exact_provider = true;
    CopyText(correlation.observer_name,
             sizeof(correlation.observer_name),
             "validated_definition_manager");
    CopyText(correlation.logical_name,
             sizeof(correlation.logical_name),
             "data\\scripts\\default_levels.xml");
    CopyText(correlation.provider, sizeof(correlation.provider),
             "X:\\workshop\\project\\data");
    return correlation;
}

CandidateRetentionEvidence MakeEvidence(
    std::uint64_t sequence,
    CandidateRetentionClassification classification) noexcept
{
    CandidateRetentionEvidence evidence = {};
    evidence.sequence = sequence;
    evidence.classification = classification;
    evidence.ephemeral_completion_observed =
        classification == CandidateRetentionClassification::Ephemeral;
    evidence.natural_replacement_observed =
        classification ==
            CandidateRetentionClassification::NaturallyReplaced;
    evidence.frontend_replacement_contract_observed =
        classification == CandidateRetentionClassification::
            NaturallyReplacedByFrontend;
    evidence.retained_value_observed =
        classification == CandidateRetentionClassification::Retained;
    CopyText(evidence.detail, sizeof(evidence.detail),
             "validated fake evidence");
    return evidence;
}

bool RecordAndCorrelate(
    std::uint64_t sequence, std::uint32_t epoch,
    cold::ContentDomainId domain,
    CandidateRetainingConsumerHandle* handle,
    CandidateRetainingObserver observer =
        CandidateRetainingObserver::DefinitionManager) noexcept
{
    CandidateRetainingConsumerInput input = MakeCandidate(
        sequence, epoch, domain);
    if (provenance::RecordCandidateRetainingConsumer(
            &g_ledger, &input, handle) !=
        CandidateLedgerResult::Succeeded)
        return false;
    CandidateOwnerCorrelation correlation = MakeCorrelation(
        sequence + 1U, domain, observer);
    return provenance::CorrelateCandidateRetainingOwner(
               &g_ledger, handle, &correlation) ==
           CandidateLedgerResult::Succeeded;
}

bool Classify(const CandidateRetainingConsumerHandle* handle,
              std::uint64_t sequence,
              CandidateRetentionClassification classification) noexcept
{
    CandidateRetentionEvidence evidence = MakeEvidence(
        sequence, classification);
    return provenance::ClassifyCandidateRetainingConsumer(
               &g_ledger, handle, &evidence) ==
           CandidateLedgerResult::Succeeded;
}

bool TestBroadOpenRemainsCandidate() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput input = MakeCandidate(1, 7);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::Succeeded);

    CandidateRetainingConsumerRecord record = {};
    EXPECT(provenance::SnapshotCandidateRetainingEpoch(
               &g_ledger, 7, &record, 1, nullptr) == 1);
    EXPECT(record.classification ==
           CandidateRetentionClassification::Candidate);
    EXPECT(record.last_unresolved_reason ==
           provenance::CandidateUnresolvedReason::BroadOpenOnly);
    CandidateDomainTouchProvenance touch = {};
    EXPECT(provenance::SnapshotCandidateDomainTouches(
               &g_ledger, &touch, 1) == 0);
    return true;
}

bool TestUnknownAtBf8RemainsUnresolved() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput input = MakeCandidate(10, 9);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::Succeeded);
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 9, 20, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(!decision.promote_unclassified);
    EXPECT(decision.unknown_at_barrier_count == 1);
    EXPECT(!decision.all_candidates_resolved);
    EXPECT(decision.touch_mask_to_promote == 0);
    EXPECT((decision.unresolved_domain_mask & cold::DomainBit(
        cold::ContentDomainId::UnclassifiedPersistentConsumers)) != 0);

    CandidateRetainingConsumerRecord record = {};
    EXPECT(provenance::SnapshotCandidateRetainingEpoch(
               &g_ledger, 9, &record, 1, nullptr) == 1);
    EXPECT(record.classification ==
           CandidateRetentionClassification::UnknownAtBarrier);
    EXPECT(record.last_unresolved_reason ==
           provenance::CandidateUnresolvedReason::BarrierReached);
    return true;
}

bool TestExactOwnerRequiredBeforeClassification() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput input = MakeCandidate(30, 3);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::Succeeded);
    CandidateRetentionEvidence evidence = MakeEvidence(
        31, CandidateRetentionClassification::Retained);
    EXPECT(provenance::ClassifyCandidateRetainingConsumer(
               &g_ledger, &handle, &evidence) ==
           CandidateLedgerResult::ExactOwnerRequired);
    return true;
}

bool TestEphemeralOwnerDoesNotTouch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(RecordAndCorrelate(
        40, 4, cold::ContentDomainId::PlayerGameplayParams, &handle));
    EXPECT(Classify(&handle, 42,
                    CandidateRetentionClassification::Ephemeral));
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 4, 50, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.ephemeral_count == 1);
    EXPECT(decision.touch_mask_to_promote == 0);
    EXPECT(decision.all_candidates_resolved);
    return true;
}

bool TestNaturalReplacementDoesNotTouch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(RecordAndCorrelate(
        60, 5, cold::ContentDomainId::ContentOnDemand, &handle));
    EXPECT(Classify(
        &handle, 62,
        CandidateRetentionClassification::NaturallyReplaced));
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 5, 70, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.naturally_replaced_count == 1);
    EXPECT(!decision.promote_unclassified);
    EXPECT(decision.touch_mask_to_promote == 0);
    return true;
}

bool TestLocalMaterialContractMustCompleteBeforeBf8() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle replaced = {};
    EXPECT(RecordAndCorrelate(
        71, 51, cold::ContentDomainId::ContentOnDemand, &replaced,
        CandidateRetainingObserver::LocalMaterialManager));
    EXPECT(Classify(
        &replaced, 73,
        CandidateRetentionClassification::NaturallyReplacedByFrontend));
    Bf8CandidatePromotionDecision complete = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 51, 74, &complete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(complete.naturally_replaced_count == 1);
    EXPECT(complete.touch_mask_to_promote == 0);

    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle wrong_owner = {};
    EXPECT(RecordAndCorrelate(
        75, 52, cold::ContentDomainId::ContentOnDemand, &wrong_owner));
    CandidateRetentionEvidence invalid_contract = MakeEvidence(
        77,
        CandidateRetentionClassification::NaturallyReplacedByFrontend);
    EXPECT(provenance::ClassifyCandidateRetainingConsumer(
               &g_ledger, &wrong_owner, &invalid_contract) ==
           CandidateLedgerResult::EvidenceIncomplete);

    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle pending = {};
    EXPECT(RecordAndCorrelate(
        78, 53, cold::ContentDomainId::ContentOnDemand, &pending,
        CandidateRetainingObserver::LocalMaterialManager));
    Bf8CandidatePromotionDecision incomplete = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 53, 80, &incomplete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(incomplete.unknown_at_barrier_count == 1);
    EXPECT(!incomplete.promote_unclassified);
    EXPECT((incomplete.unresolved_domain_mask & cold::DomainBit(
                cold::ContentDomainId::ContentOnDemand)) != 0);
    EXPECT((incomplete.touch_mask_to_promote & cold::DomainBit(
                cold::ContentDomainId::ContentOnDemand)) == 0);
    EXPECT(std::strcmp(provenance::CandidateRetainingObserverName(
                          CandidateRetainingObserver::LocalMaterialManager),
                      "local_material_manager") == 0);
    return true;
}

bool TestRetainedOwnerProducesImmutableTrueTouch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(RecordAndCorrelate(
        80, 6, cold::ContentDomainId::PlayerGameplayParams, &handle));
    EXPECT(Classify(&handle, 82,
                    CandidateRetentionClassification::Retained));

    CandidateRetentionEvidence contradictory = MakeEvidence(
        83, CandidateRetentionClassification::NaturallyReplaced);
    EXPECT(provenance::ClassifyCandidateRetainingConsumer(
               &g_ledger, &handle, &contradictory) ==
           CandidateLedgerResult::InvalidTransition);

    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 6, 90, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.retained_count == 1);
    EXPECT(!decision.promote_unclassified);
    EXPECT(decision.touch_mask_to_promote == cold::DomainBit(
        cold::ContentDomainId::PlayerGameplayParams));

    CandidateDomainTouchProvenance touch = {};
    EXPECT(provenance::SnapshotCandidateDomainTouches(
               &g_ledger, &touch, 1) == 1);
    EXPECT(touch.touched);
    EXPECT(touch.first_touch_sequence == 82);
    EXPECT(touch.epoch == 6);
    EXPECT(touch.classification ==
           CandidateRetentionClassification::Retained);
    return true;
}

bool TestRetainedContentOnDemandTouchesExactDomain() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(RecordAndCorrelate(
        84, 61, cold::ContentDomainId::ContentOnDemand, &handle,
        CandidateRetainingObserver::ContentOnDemand));
    EXPECT(Classify(&handle, 86,
                    CandidateRetentionClassification::Retained));

    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 61, 90, &decision) ==
           CandidateLedgerResult::Succeeded);
    const cold::ContentDomainMask content_on_demand = cold::DomainBit(
        cold::ContentDomainId::ContentOnDemand);
    const cold::ContentDomainMask unclassified = cold::DomainBit(
        cold::ContentDomainId::UnclassifiedPersistentConsumers);
    EXPECT(decision.retained_count == 1);
    EXPECT(decision.unknown_at_barrier_count == 0);
    EXPECT(!decision.promote_unclassified);
    EXPECT(decision.all_candidates_resolved);
    EXPECT(decision.immutable_true_touch_mask == content_on_demand);
    EXPECT(decision.touch_mask_to_promote == content_on_demand);
    EXPECT((decision.touch_mask_to_promote & unclassified) == 0);

    CandidateDomainTouchProvenance touch = {};
    EXPECT(provenance::SnapshotCandidateDomainTouches(
               &g_ledger, &touch, 1) == 1);
    EXPECT(touch.touched);
    EXPECT(touch.domain == cold::ContentDomainId::ContentOnDemand);
    EXPECT(touch.observer == CandidateRetainingObserver::ContentOnDemand);
    EXPECT(touch.classification ==
           CandidateRetentionClassification::Retained);
    return true;
}

bool TestNaturalRecordCannotClearPriorTrueTouch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle retained = {};
    CandidateRetainingConsumerHandle replaced = {};
    EXPECT(RecordAndCorrelate(
        100, 8, cold::ContentDomainId::Audio, &retained));
    EXPECT(Classify(&retained, 102,
                    CandidateRetentionClassification::Retained));
    EXPECT(RecordAndCorrelate(
        103, 8, cold::ContentDomainId::Audio, &replaced));
    EXPECT(Classify(
        &replaced, 105,
        CandidateRetentionClassification::NaturallyReplaced));
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 8, 110, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT((decision.immutable_true_touch_mask &
            cold::DomainBit(cold::ContentDomainId::Audio)) != 0);
    EXPECT(decision.retained_count == 1);
    EXPECT(decision.naturally_replaced_count == 1);
    return true;
}

bool TestEpochsRemainIsolated() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput unresolved = MakeCandidate(120, 11);
    CandidateRetainingConsumerHandle unresolved_handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &unresolved, &unresolved_handle) ==
           CandidateLedgerResult::Succeeded);
    CandidateRetainingConsumerHandle natural = {};
    EXPECT(RecordAndCorrelate(
        121, 12, cold::ContentDomainId::ContentOnDemand, &natural));
    EXPECT(Classify(
        &natural, 123,
        CandidateRetentionClassification::NaturallyReplaced));

    Bf8CandidatePromotionDecision first = {};
    Bf8CandidatePromotionDecision second = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 11, 130, &first) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 12, 131, &second) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(!first.promote_unclassified);
    EXPECT(first.unresolved_domain_mask != 0);
    EXPECT(!second.promote_unclassified);
    EXPECT(second.touch_mask_to_promote == 0);
    return true;
}

bool TestRepeatedBf8DecisionIsDeterministic() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput input = MakeCandidate(140, 13);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::Succeeded);
    Bf8CandidatePromotionDecision first = {};
    Bf8CandidatePromotionDecision second = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 13, 150, &first) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 13, 999, &second) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(std::memcmp(&first, &second, sizeof(first)) == 0);
    return true;
}

bool TestRecordCapacityLossFailsClosed() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    std::uint64_t sequence = 1000;
    for (std::size_t index = 0;
         index < provenance::kCandidateRetainingRecordCapacity; ++index) {
        CandidateRetainingConsumerHandle handle = {};
        EXPECT(RecordAndCorrelate(
            sequence, 20,
            cold::ContentDomainId::ContentOnDemand, &handle));
        EXPECT(Classify(&handle, sequence + 2U,
                        CandidateRetentionClassification::Ephemeral));
        sequence += 3U;
    }
    CandidateRetainingConsumerInput overflow = MakeCandidate(sequence, 20);
    CandidateRetainingConsumerHandle overflow_handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &overflow, &overflow_handle) ==
           CandidateLedgerResult::RecordCapacityExceeded);
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 20, sequence + 1U, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.unknown_at_barrier_count == 0);
    EXPECT(decision.observer_loss_count != 0);
    EXPECT(!decision.promote_unclassified);
    EXPECT((decision.unresolved_domain_mask & cold::DomainBit(
                cold::ContentDomainId::UnclassifiedPersistentConsumers)) !=
           0);
    return true;
}

bool TestLockContentionIsNonblockingAndFailsClosed() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    g_ledger.lock = 1;
    CandidateRetainingConsumerInput input = MakeCandidate(2000, 21);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::LedgerBusy);
    g_ledger.lock = 0;
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 21, 2001, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.observer_loss_latched);
    EXPECT(!decision.promote_unclassified);
    EXPECT((decision.unresolved_domain_mask & cold::DomainBit(
                cold::ContentDomainId::UnclassifiedPersistentConsumers)) !=
           0);
    return true;
}

bool TestLateCandidateCannotReopenSealedEpoch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    Bf8CandidatePromotionDecision before = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 22, 2100, &before) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(before.touch_mask_to_promote == 0);

    CandidateRetainingConsumerInput late = MakeCandidate(2101, 22);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &late, &handle) ==
           CandidateLedgerResult::EpochSealed);
    Bf8CandidatePromotionDecision after = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 22, 2200, &after) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(!after.promote_unclassified);
    EXPECT((after.unresolved_domain_mask & cold::DomainBit(
                cold::ContentDomainId::UnclassifiedPersistentConsumers)) !=
           0);
    EXPECT(after.observer_loss_latched);
    EXPECT(g_ledger.late_after_barrier_count == 1);
    return true;
}

bool TestStaleHandleCannotClassifyAnotherRecord() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(RecordAndCorrelate(
        2300, 23, cold::ContentDomainId::Audio, &handle));
    ++handle.record_generation;
    CandidateRetentionEvidence evidence = MakeEvidence(
        2302, CandidateRetentionClassification::Retained);
    EXPECT(provenance::ClassifyCandidateRetainingConsumer(
               &g_ledger, &handle, &evidence) ==
           CandidateLedgerResult::StaleHandle);
    return true;
}

bool TestExactProviderMustBeBounded() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerInput input = MakeCandidate(2400, 24);
    CandidateRetainingConsumerHandle handle = {};
    EXPECT(provenance::RecordCandidateRetainingConsumer(
               &g_ledger, &input, &handle) ==
           CandidateLedgerResult::Succeeded);
    CandidateOwnerCorrelation correlation = MakeCorrelation(
        2401, cold::ContentDomainId::PlayerGameplayParams);
    std::memset(correlation.provider, 'x', sizeof(correlation.provider));
    EXPECT(provenance::CorrelateCandidateRetainingOwner(
               &g_ledger, &handle, &correlation) ==
           CandidateLedgerResult::EvidenceIncomplete);
    return true;
}

bool TestEmptyEpochHasNoFalseTouch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    Bf8CandidatePromotionDecision decision = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 25, 2500, &decision) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(decision.record_count == 0);
    EXPECT(decision.all_candidates_resolved);
    EXPECT(!decision.promote_unclassified);
    EXPECT(decision.touch_mask_to_promote == 0);
    return true;
}

bool TestAllResolvedRequiresConfiguredObserverCoverage() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    const cold::ContentDomainMask audio = cold::DomainBit(
        cold::ContentDomainId::Audio);
    const cold::ContentDomainMask player = cold::DomainBit(
        cold::ContentDomainId::PlayerGameplayParams);
    EXPECT(provenance::SetRequiredCandidateObserverCoverageMask(
        &g_ledger, audio | player));
    EXPECT(provenance::RecordCandidateDomainObserverCoverage(
               &g_ledger, 26, 2600,
               cold::ContentDomainId::Audio,
               provenance::CandidateObserverCoverage::Complete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(provenance::RecordCandidateDomainObserverCoverage(
               &g_ledger, 26, 2601,
               cold::ContentDomainId::PlayerGameplayParams,
               provenance::CandidateObserverCoverage::Incomplete) ==
           CandidateLedgerResult::Succeeded);
    Bf8CandidatePromotionDecision incomplete = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 26, 2602, &incomplete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(!incomplete.all_candidates_resolved);
    EXPECT(!incomplete.preflight_allowed);
    EXPECT((incomplete.observer_coverage_complete_mask & audio) != 0);
    EXPECT((incomplete.observer_coverage_incomplete_mask & player) != 0);
    EXPECT((incomplete.unresolved_domain_mask & player) != 0);
    EXPECT(incomplete.touch_mask_to_promote == 0);

    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    EXPECT(provenance::SetRequiredCandidateObserverCoverageMask(
        &g_ledger, audio | player));
    EXPECT(provenance::RecordCandidateDomainObserverCoverage(
               &g_ledger, 27, 2700,
               cold::ContentDomainId::Audio,
               provenance::CandidateObserverCoverage::Complete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(provenance::RecordCandidateDomainObserverCoverage(
               &g_ledger, 27, 2701,
               cold::ContentDomainId::PlayerGameplayParams,
               provenance::CandidateObserverCoverage::Complete) ==
           CandidateLedgerResult::Succeeded);
    Bf8CandidatePromotionDecision complete = {};
    EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(
               &g_ledger, 27, 2702, &complete) ==
           CandidateLedgerResult::Succeeded);
    EXPECT(complete.all_candidates_resolved);
    EXPECT(complete.preflight_allowed);
    EXPECT(complete.unresolved_domain_mask == 0);
    return true;
}

struct TestCase {
    const char* name;
    bool (*run)() noexcept;
};

}  // namespace

static cold::ContentDomainRestartRegistry retirement_registry;
static cold::DomainResult RetirementPhase(void*,const cold::RestartContext*) noexcept
{return cold::DomainResult::Succeeded;}
static cold::DomainResult RetirementMutation(void*,const cold::RestartContext* context) noexcept
{return cold::MarkRegistryMutationStarted(&retirement_registry,context)?cold::DomainResult::Succeeded:cold::DomainResult::Failed;}
static bool RetireCandidates(void*,const cold::RestartContext* context,const cold::RegistryFinalizationToken* token) noexcept
{
    const auto count=g_ledger.retired_records;
    if(provenance::CheckCandidateEpochRetirement(&g_ledger,&retirement_registry,context,token)!=CandidateLedgerResult::Succeeded ||
       g_ledger.retired_records!=count)return false;
    return provenance::RetireCandidateEpoch(&g_ledger,&retirement_registry,context,token)==CandidateLedgerResult::Succeeded;
}
static bool TestVerifiedRetirementReusesOnlyItsEpoch() noexcept
{
    provenance::InitializeCandidateRetainingConsumerLedger(&g_ledger);
    CandidateRetainingConsumerHandle other={};
    EXPECT(RecordAndCorrelate(1,10000,cold::ContentDomainId::LocalizedTexts,&other));
    EXPECT(Classify(&other,3,CandidateRetentionClassification::Ephemeral));
    const auto saved_other=g_ledger.records[other.record_index];
    cold::InitializeRegistry(&retirement_registry);
    cold::ContentDomainAdapter adapter={};adapter.id=cold::ContentDomainId::VfsSources;
    CopyText(adapter.name,sizeof(adapter.name),"VerifiedMetadata");adapter.enabled=adapter.mandatory=adapter.destructive=true;
    adapter.capture_baseline=adapter.preflight=adapter.quiesce=adapter.cancel_quiesce=adapter.reload=adapter.verify=&RetirementPhase;
    adapter.reset=&RetirementMutation;
    EXPECT(cold::RegisterAdapter(&retirement_registry,&adapter));
    EXPECT(cold::SetRegistryFinalizer(&retirement_registry,&RetireCandidates,nullptr));
    EXPECT(cold::SealRegistry(&retirement_registry));
    cold::RestartContext context={};context.baseline_generation=1;context.owner_thread_id=GetCurrentThreadId();
    EXPECT(cold::CaptureRegistryBaseline(&retirement_registry,&context)==cold::RestartResult::Verified);
    CandidateRetainingConsumerHandle stale={};
    for(std::uint32_t epoch=1;epoch<=80;++epoch) {
        for(std::uint32_t row=0;row<4;++row) {
            CandidateRetainingConsumerHandle current={};
            EXPECT(RecordAndCorrelate(100*epoch+3*row,epoch,cold::ContentDomainId::Audio,&current));
            EXPECT(Classify(&current,100*epoch+3*row+2,CandidateRetentionClassification::Ephemeral));
            if(row==0 && epoch>1){
                auto changed=MakeCorrelation(100*epoch+20,cold::ContentDomainId::Audio,CandidateRetainingObserver::AudioDataManager);
                EXPECT(provenance::CorrelateCandidateRetainingOwner(&g_ledger,&stale,&changed)==CandidateLedgerResult::StaleHandle);
            }
            if(row==0)stale=current;
        }
        Bf8CandidatePromotionDecision decision={};
        EXPECT(provenance::EvaluateCandidateRetainingConsumersAtBf8(&g_ledger,epoch,100*epoch+30,&decision)==CandidateLedgerResult::Succeeded);
        EXPECT(decision.preflight_allowed && decision.all_candidates_resolved);
        context.content_epoch=epoch;context.transaction_generation=epoch;
        EXPECT(provenance::RetireCandidateEpoch(&g_ledger,&retirement_registry,&context,nullptr)==CandidateLedgerResult::InvalidArguments);
        EXPECT(cold::RunRegistryTransaction(&retirement_registry,&context)==cold::RestartResult::Verified);
        EXPECT(!std::memcmp(&saved_other,&g_ledger.records[other.record_index],sizeof(saved_other)));
        auto late=MakeCandidate(100*epoch+50,epoch);CandidateRetainingConsumerHandle rejected={};
        EXPECT(provenance::RecordCandidateRetainingConsumer(&g_ledger,&late,&rejected)==CandidateLedgerResult::StaleHandle);
    }
    EXPECT(g_ledger.retired_epochs==80 && g_ledger.retired_records==320 && g_ledger.record_count==5 && g_ledger.epoch_count==2);
    EXPECT(!g_ledger.dropped_observation_count && !g_ledger.unattributed_loss_latched);return true;
}

int main()
{
    const TestCase cases[] = {
        {"broad_open_remains_candidate", TestBroadOpenRemainsCandidate},
        {"unknown_at_bf8_remains_unresolved",
         TestUnknownAtBf8RemainsUnresolved},
        {"exact_owner_required_before_classification",
         TestExactOwnerRequiredBeforeClassification},
        {"ephemeral_owner_does_not_touch", TestEphemeralOwnerDoesNotTouch},
        {"natural_replacement_does_not_touch",
         TestNaturalReplacementDoesNotTouch},
        {"local_material_contract_must_complete_before_bf8",
         TestLocalMaterialContractMustCompleteBeforeBf8},
        {"retained_owner_produces_immutable_true_touch",
         TestRetainedOwnerProducesImmutableTrueTouch},
        {"retained_content_on_demand_touches_exact_domain",
         TestRetainedContentOnDemandTouchesExactDomain},
        {"natural_record_cannot_clear_prior_true_touch",
         TestNaturalRecordCannotClearPriorTrueTouch},
        {"epochs_remain_isolated", TestEpochsRemainIsolated},
        {"repeated_bf8_decision_is_deterministic",
         TestRepeatedBf8DecisionIsDeterministic},
        {"record_capacity_loss_fails_closed",
         TestRecordCapacityLossFailsClosed},
        {"lock_contention_is_nonblocking_and_fails_closed",
         TestLockContentionIsNonblockingAndFailsClosed},
        {"late_candidate_cannot_reopen_sealed_epoch",
         TestLateCandidateCannotReopenSealedEpoch},
        {"stale_handle_cannot_classify_another_record",
         TestStaleHandleCannotClassifyAnotherRecord},
        {"exact_provider_must_be_bounded", TestExactProviderMustBeBounded},
        {"empty_epoch_has_no_false_touch", TestEmptyEpochHasNoFalseTouch},
        {"all_resolved_requires_configured_observer_coverage",
         TestAllResolvedRequiresConfiguredObserverCoverage},
        {"verified_retirement_reuses_only_its_epoch",TestVerifiedRetirementReusesOnlyItsEpoch},
    };

    std::size_t passed = 0;
    for (const TestCase& test : cases) {
        if (!test.run()) {
            std::printf("[FAIL] %s\n", test.name);
            return 1;
        }
        ++passed;
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf(
        "candidate retaining-consumer provenance cases passed: %zu\n",
        passed);
    return 0;
}
