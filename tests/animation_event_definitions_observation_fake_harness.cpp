#define _HAS_EXCEPTIONS 0

#include "../src/fixes/custom_map_asset_isolation/AnimationEventDefinitionsObservation.h"

#include <cstdio>
#include <cstring>

using namespace dluma::fixes::custom_map_asset_isolation::animevents;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

constexpr char kInputHash[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char kTreeHash[] =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

template <std::size_t Capacity>
void SetText(char (&destination)[Capacity], const char* source) noexcept
{
    std::memset(destination, 0, Capacity);
    if (!source) return;
    std::size_t index = 0;
    while (index + 1 < Capacity && source[index]) {
        destination[index] = source[index];
        ++index;
    }
}

BeginEvidence MakeBegin(std::uint64_t sequence,
                        std::uint64_t count = 17) noexcept
{
    BeginEvidence evidence = {};
    evidence.sequence = sequence;
    evidence.manager = static_cast<std::uintptr_t>(
        0x00007FF700000000ULL + kManagerRootRva);
    evidence.caller_rva = kStartupCallRva;
    evidence.definition_count_before = count;
    evidence.load_target_validated = true;
    evidence.manager_root_matched = true;
    evidence.definition_count_before_readable = true;
    evidence.path_complete = true;
    SetText(evidence.path, kStartupPath);
    return evidence;
}

ProviderEvidence MakeProvider(std::uint64_t sequence,
                              const char* provider =
                                  "base-b0\\data") noexcept
{
    ProviderEvidence evidence = {};
    evidence.sequence = sequence;
    evidence.source = 0x9000;
    evidence.content_epoch = 1;
    evidence.source_record_index = 4;
    evidence.successful_add_ordinal = 7;
    evidence.request_observed = true;
    evidence.open_succeeded = true;
    evidence.exact_source = true;
    evidence.b0_provider = true;
    evidence.logical_path_complete = true;
    evidence.resolved_provider_complete = true;
    SetText(evidence.logical_path, kStartupPath);
    SetText(evidence.resolved_provider, provider);
    return evidence;
}

ParserEvidence MakeParser(std::uint64_t sequence,
                          bool success = true) noexcept
{
    ParserEvidence evidence = {};
    evidence.sequence = sequence;
    evidence.parser_registration_generation = success ? sequence + 100 : 0;
    evidence.result_object = success ? 0xB000 + sequence : 0;
    evidence.input_size = 4096;
    evidence.input_observed = true;
    evidence.input_hash_available = true;
    evidence.input_path_complete = true;
    SetText(evidence.input_path, kStartupPath);
    SetText(evidence.input_hash, kInputHash);
    return evidence;
}

FinishEvidence MakeFinish(std::uint64_t sequence,
                          std::uint64_t count = 19) noexcept
{
    FinishEvidence evidence = {};
    evidence.sequence = sequence;
    evidence.definition_count_after = count;
    evidence.definition_count_after_readable = true;
    evidence.function_return_observed = true;
    evidence.function_returned_true = true;
    return evidence;
}

void AddCanonicalEvidence(FinishEvidence* evidence,
                          const LoadToken& token) noexcept
{
    evidence->canonical_tree_fingerprint_generation = token.generation;
    evidence->canonical_tree_fingerprint_available = true;
    evidence->canonical_tree_fingerprint_pointer_free = true;
    SetText(evidence->canonical_tree_fingerprint, kTreeHash);
}

void AddConsumerInventory(FinishEvidence* evidence,
                          const LoadToken& token,
                          std::uint32_t count = 0) noexcept
{
    evidence->consumer_inventory_generation = token.generation;
    evidence->inventoried_consumer_count = count;
    evidence->consumer_inventory_complete = true;
}

bool AddSuccessfulCore(Ledger* ledger, const LoadToken& token,
                       std::uint64_t sequence,
                       const char* provider = "base-b0\\data") noexcept
{
    const ProviderEvidence provider_evidence =
        MakeProvider(sequence + 1, provider);
    const ParserEvidence parser_evidence = MakeParser(sequence + 2);
    return ObserveProvider(ledger, token, provider_evidence) &&
           ObserveParser(ledger, token, parser_evidence);
}

bool CaseExactProvenanceStillNeedsCanonicalFingerprint()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(10), &token));
    CHECK(AddSuccessfulCore(&ledger, token, 10));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(13), &result));
    CHECK(result.generation == token.generation);
    CHECK(result.exact_target_complete);
    CHECK(result.provider_provenance_complete);
    CHECK(result.parser_provenance_complete);
    CHECK(result.provenance_complete);
    CHECK(result.parser_succeeded);
    CHECK(result.startup_baseline_candidate);
    CHECK(!result.canonical_tree_fingerprint_complete);
    CHECK(!result.observation_complete);
    CHECK(result.disposition ==
          ObservationDisposition::CanonicalTreeFingerprintMissing);
    CHECK(std::strcmp(result.parser.input_hash, kInputHash) == 0);
    CHECK(result.parser.input_size == 4096);
    CHECK(result.begin.definition_count_before == 17);
    CHECK(result.finish.definition_count_after == 19);
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.observation_count == 1);
    CHECK(snapshot.observations[0].provider.content_epoch == 1);
    CHECK(snapshot.observations[0].parser.parser_registration_generation ==
          112);
    return true;
}

bool CaseCanonicalFingerprintStillNeedsConsumerInventory()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(20), &token));
    CHECK(AddSuccessfulCore(&ledger, token, 20));
    FinishEvidence finish = MakeFinish(23);
    AddCanonicalEvidence(&finish, token);
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, finish, &result));
    CHECK(result.canonical_tree_fingerprint_complete);
    CHECK(!result.consumer_inventory_evidence_complete);
    CHECK(!result.observation_complete);
    CHECK(result.disposition ==
          ObservationDisposition::ConsumerInventoryIncomplete);
    return true;
}

bool CaseFutureCompleteEvidencePassesEveryGate()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(30), &token));
    CHECK(AddSuccessfulCore(&ledger, token, 30));
    FinishEvidence finish = MakeFinish(33);
    AddCanonicalEvidence(&finish, token);
    AddConsumerInventory(&finish, token, 3);
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, finish, &result));
    CHECK(result.observation_complete);
    CHECK(result.finish.inventoried_consumer_count == 3);
    CHECK(result.disposition == ObservationDisposition::Complete);
    CHECK(std::strcmp(DispositionName(result.disposition), "complete") == 0);
    return true;
}

bool CaseTrueReturnCannotHideNullParserResult()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(40, 23), &token));
    CHECK(ObserveProvider(&ledger, token, MakeProvider(41)));
    CHECK(ObserveParser(&ledger, token, MakeParser(42, false)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(43, 23), &result));
    CHECK(result.finish.function_returned_true);
    CHECK(!result.parser_succeeded);
    CHECK(result.function_return_disagrees_with_parser);
    CHECK(result.definition_count_retained_after_parser_failure);
    CHECK(!result.observation_complete);
    CHECK(result.disposition ==
          ObservationDisposition::ParserFailedStateRetained);
    return true;
}

bool CaseMissingParserResultRemainsUnknown()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(50), &token));
    CHECK(ObserveProvider(&ledger, token, MakeProvider(51)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(52), &result));
    CHECK(!result.parser_observed);
    CHECK(!result.parser_succeeded);
    CHECK(result.disposition ==
          ObservationDisposition::ParserResultMissing);
    return true;
}

bool CaseInvalidTargetNullParserDoesNotClaimRetention()
{
    Ledger ledger = {};
    Initialize(&ledger);
    BeginEvidence begin = MakeBegin(55, 23);
    begin.load_target_validated = false;
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, begin, &token));
    CHECK(ObserveProvider(&ledger, token, MakeProvider(56)));
    CHECK(ObserveParser(&ledger, token, MakeParser(57, false)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(58, 23), &result));
    CHECK(!result.exact_target_complete);
    CHECK(!result.provenance_complete);
    CHECK(!result.definition_count_retained_after_parser_failure);
    CHECK(result.disposition ==
          ObservationDisposition::ProvenanceIncomplete);
    return true;
}

bool CaseMissingProviderCannotProduceB0Provenance()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(60), &token));
    CHECK(ObserveParser(&ledger, token, MakeParser(61)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(62), &result));
    CHECK(!result.provider_provenance_complete);
    CHECK(!result.provenance_complete);
    CHECK(result.disposition ==
          ObservationDisposition::ProvenanceIncomplete);
    return true;
}

bool CaseNonB0OrMismatchedProviderIsRejected()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(70), &token));
    ProviderEvidence provider = MakeProvider(71);
    provider.b0_provider = false;
    provider.exact_source = false;
    SetText(provider.logical_path, "OtherEventData.scr");
    CHECK(ObserveProvider(&ledger, token, provider));
    CHECK(ObserveParser(&ledger, token, MakeParser(72)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(73), &result));
    CHECK(!result.provider_provenance_complete);
    CHECK(!result.startup_baseline_candidate);
    CHECK(result.disposition ==
          ObservationDisposition::ProvenanceIncomplete);
    return true;
}

bool CaseMissingMemoryHashOrSizeIsRejected()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(80), &token));
    CHECK(ObserveProvider(&ledger, token, MakeProvider(81)));
    ParserEvidence parser = MakeParser(82);
    parser.input_hash_available = false;
    parser.input_size = 0;
    CHECK(ObserveParser(&ledger, token, parser));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(83), &result));
    CHECK(result.parser_succeeded);
    CHECK(!result.parser_provenance_complete);
    CHECK(!result.provenance_complete);
    return true;
}

bool CaseUnreadableCountsAreNeverComplete()
{
    Ledger ledger = {};
    Initialize(&ledger);
    BeginEvidence begin = MakeBegin(90);
    begin.definition_count_before_readable = false;
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, begin, &token));
    CHECK(AddSuccessfulCore(&ledger, token, 90));
    FinishEvidence finish = MakeFinish(93);
    finish.definition_count_after_readable = false;
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, finish, &result));
    CHECK(!result.count_evidence_complete);
    CHECK(!result.provenance_complete);
    return true;
}

bool CaseNestedLoadsRetainIndependentGenerations()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken first = {};
    LoadToken second = {};
    CHECK(BeginObservation(&ledger, MakeBegin(100), &first));
    CHECK(BeginObservation(&ledger, MakeBegin(200), &second));
    CHECK(first.generation != second.generation);
    CHECK(AddSuccessfulCore(&ledger, first, 100, "first-b0"));
    CHECK(AddSuccessfulCore(&ledger, second, 200, "second-b0"));
    CHECK(FinishObservation(&ledger, second, MakeFinish(203), nullptr));
    CHECK(FinishObservation(&ledger, first, MakeFinish(103), nullptr));
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.active_count == 0);
    CHECK(snapshot.observation_count == 2);
    CHECK(snapshot.observations[0].generation == second.generation);
    CHECK(snapshot.observations[1].generation == first.generation);
    CHECK(std::strcmp(snapshot.observations[0].provider.resolved_provider,
                      "second-b0") == 0);
    CHECK(std::strcmp(snapshot.observations[1].provider.resolved_provider,
                      "first-b0") == 0);
    return true;
}

bool CaseStaleTokenCannotModifyReusedSlot()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken stale = {};
    CHECK(BeginObservation(&ledger, MakeBegin(300), &stale));
    CHECK(AddSuccessfulCore(&ledger, stale, 300));
    CHECK(FinishObservation(&ledger, stale, MakeFinish(303), nullptr));
    LoadToken current = {};
    CHECK(BeginObservation(&ledger, MakeBegin(310), &current));
    CHECK(current.slot == stale.slot);
    CHECK(current.generation != stale.generation);
    CHECK(!ObserveProvider(&ledger, stale,
                           MakeProvider(311, "stale-provider")));
    CHECK(ObserveProvider(&ledger, current,
                          MakeProvider(312, "current-provider")));
    CHECK(ObserveParser(&ledger, current, MakeParser(313)));
    CHECK(FinishObservation(&ledger, current, MakeFinish(314), nullptr));
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.loss.stale_token == 1);
    CHECK(std::strcmp(snapshot.observations[1].provider.resolved_provider,
                      "current-provider") == 0);
    return true;
}

bool CaseDuplicateEvidenceIsAmbiguousAndFirstWins()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken token = {};
    CHECK(BeginObservation(&ledger, MakeBegin(400), &token));
    CHECK(ObserveProvider(&ledger, token,
                          MakeProvider(401, "first-provider")));
    CHECK(!ObserveProvider(&ledger, token,
                           MakeProvider(402, "second-provider")));
    ParserEvidence first_parser = MakeParser(403);
    CHECK(ObserveParser(&ledger, token, first_parser));
    CHECK(!ObserveParser(&ledger, token, MakeParser(404, false)));
    Observation result = {};
    CHECK(FinishObservation(&ledger, token, MakeFinish(405), &result));
    CHECK(result.provider_ambiguous);
    CHECK(result.parser_ambiguous);
    CHECK(!result.provenance_complete);
    CHECK(result.parser.result_object == first_parser.result_object);
    CHECK(std::strcmp(result.provider.resolved_provider,
                      "first-provider") == 0);
    CHECK(ledger.loss.duplicate_provider == 1);
    CHECK(ledger.loss.duplicate_parser == 1);
    return true;
}

bool CaseActiveLedgerCapacityIsBounded()
{
    Ledger ledger = {};
    Initialize(&ledger);
    LoadToken tokens[kActiveLoadCapacity] = {};
    for (std::uint32_t index = 0; index < kActiveLoadCapacity; ++index)
        CHECK(BeginObservation(&ledger, MakeBegin(500 + index),
                               &tokens[index]));
    LoadToken overflow = {};
    CHECK(!BeginObservation(&ledger, MakeBegin(600), &overflow));
    CHECK(!overflow.generation);
    CHECK(ledger.active_count == kActiveLoadCapacity);
    CHECK(ledger.loss.active_load_overflow == 1);
    return true;
}

bool CaseCompletedLedgerCapacityIsBounded()
{
    Ledger ledger = {};
    Initialize(&ledger);
    for (std::uint32_t index = 0; index < kObservationCapacity; ++index) {
        LoadToken token = {};
        CHECK(BeginObservation(&ledger, MakeBegin(700 + index * 4), &token));
        CHECK(AddSuccessfulCore(&ledger, token, 700 + index * 4));
        CHECK(FinishObservation(&ledger, token,
                                MakeFinish(703 + index * 4), nullptr));
    }
    LoadToken overflow = {};
    CHECK(BeginObservation(&ledger, MakeBegin(1000), &overflow));
    CHECK(AddSuccessfulCore(&ledger, overflow, 1000));
    Observation result = {};
    CHECK(!FinishObservation(&ledger, overflow, MakeFinish(1003), &result));
    CHECK(!result.record_storage_complete);
    CHECK(!result.observation_complete);
    CHECK(result.disposition ==
          ObservationDisposition::StorageCapacityExceeded);
    CHECK(ledger.observation_count == kObservationCapacity);
    CHECK(ledger.loss.observation_overflow == 1);
    return true;
}

bool CaseLockContentionIsNonBlockingAndCounted()
{
    Ledger ledger = {};
    Initialize(&ledger);
    ledger.lock = 1;
    LoadToken token = {};
    CHECK(!BeginObservation(&ledger, MakeBegin(1100), &token));
    CHECK(!token.generation);
    CHECK(ledger.loss.lock_contention == 1);
    ledger.lock = 0;
    Snapshot snapshot = {};
    CHECK(CaptureSnapshot(&ledger, &snapshot));
    CHECK(snapshot.observation_count == 0);
    return true;
}

}  // namespace

int main()
{
    struct TestCase { const char* name; bool (*run)(); };
    const TestCase tests[] = {
        {"exact provenance needs canonical fingerprint",
         CaseExactProvenanceStillNeedsCanonicalFingerprint},
        {"canonical fingerprint needs consumer inventory",
         CaseCanonicalFingerprintStillNeedsConsumerInventory},
        {"future complete evidence", CaseFutureCompleteEvidencePassesEveryGate},
        {"true return with null parser", CaseTrueReturnCannotHideNullParserResult},
        {"missing parser result", CaseMissingParserResultRemainsUnknown},
        {"invalid target null parser",
         CaseInvalidTargetNullParserDoesNotClaimRetention},
        {"missing provider", CaseMissingProviderCannotProduceB0Provenance},
        {"non-b0 provider", CaseNonB0OrMismatchedProviderIsRejected},
        {"missing memory hash", CaseMissingMemoryHashOrSizeIsRejected},
        {"unreadable counts", CaseUnreadableCountsAreNeverComplete},
        {"nested generations", CaseNestedLoadsRetainIndependentGenerations},
        {"stale token", CaseStaleTokenCannotModifyReusedSlot},
        {"duplicate evidence", CaseDuplicateEvidenceIsAmbiguousAndFirstWins},
        {"active capacity", CaseActiveLedgerCapacityIsBounded},
        {"completed capacity", CaseCompletedLedgerCapacityIsBounded},
        {"nonblocking lock", CaseLockContentionIsNonBlockingAndCounted},
    };
    std::uint32_t passed = 0;
    for (const TestCase& test : tests) {
        if (!test.run()) return 1;
        ++passed;
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf("animation event definition observation cases passed: %u\n",
                passed);
    return 0;
}
