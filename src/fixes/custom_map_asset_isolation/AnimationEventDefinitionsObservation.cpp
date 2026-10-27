#define _HAS_EXCEPTIONS 0

#include "AnimationEventDefinitionsObservation.h"

#include <cstring>
#include <type_traits>

namespace dluma::fixes::custom_map_asset_isolation::animevents {
namespace {

static_assert(std::is_trivially_copyable<Observation>::value,
              "the hook-facing observation must remain trivially copyable");
static_assert(std::is_trivially_copyable<Ledger>::value,
              "the observation ledger must remain fixed-capacity POD");

bool Acquire(Ledger* ledger) noexcept
{
    if (!ledger) return false;
    if (dluma_atomic_compare_exchange(&ledger->lock, 1, 0) == 0) return true;
    dluma_atomic_increment(
        reinterpret_cast<volatile LONG*>(&ledger->loss.lock_contention));
    return false;
}

void Release(Ledger* ledger) noexcept
{
    dluma_atomic_exchange(&ledger->lock, 0);
}

bool HasTerminator(const char* text, std::size_t capacity) noexcept
{
    if (!text || !capacity) return false;
    for (std::size_t index = 0; index < capacity; ++index) {
        if (!text[index]) return true;
    }
    return false;
}

bool HasCompleteText(const char* text, std::size_t capacity,
                     bool producer_complete) noexcept
{
    return producer_complete && text && text[0] &&
           HasTerminator(text, capacity);
}

unsigned char FoldPathCharacter(unsigned char value) noexcept
{
    if (value == '\\') value = '/';
    if (value >= 'A' && value <= 'Z')
        value = static_cast<unsigned char>(value + ('a' - 'A'));
    return value;
}

bool PathsEqual(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    for (std::size_t index = 0; index < kPathCapacity; ++index) {
        const unsigned char a = FoldPathCharacter(
            static_cast<unsigned char>(left[index]));
        const unsigned char b = FoldPathCharacter(
            static_cast<unsigned char>(right[index]));
        if (a != b) return false;
        if (!a) return true;
    }
    return false;
}

bool IsHexCharacter(char value) noexcept
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

bool IsSha256(const char* value) noexcept
{
    if (!value) return false;
    for (std::size_t index = 0; index < 64; ++index) {
        if (!IsHexCharacter(value[index])) return false;
    }
    return value[64] == 0;
}

void SanitizeBegin(BeginEvidence* evidence) noexcept
{
    if (!evidence) return;
    const bool terminated = HasTerminator(evidence->path, kPathCapacity);
    evidence->path[kPathCapacity - 1] = 0;
    evidence->path_complete = evidence->path_complete && terminated;
}

void SanitizeProvider(ProviderEvidence* evidence) noexcept
{
    if (!evidence) return;
    const bool logical_terminated =
        HasTerminator(evidence->logical_path, kPathCapacity);
    const bool provider_terminated =
        HasTerminator(evidence->resolved_provider, kProviderCapacity);
    evidence->logical_path[kPathCapacity - 1] = 0;
    evidence->resolved_provider[kProviderCapacity - 1] = 0;
    evidence->logical_path_complete =
        evidence->logical_path_complete && logical_terminated;
    evidence->resolved_provider_complete =
        evidence->resolved_provider_complete && provider_terminated;
}

void SanitizeParser(ParserEvidence* evidence) noexcept
{
    if (!evidence) return;
    const bool path_terminated =
        HasTerminator(evidence->input_path, kPathCapacity);
    const bool hash_terminated =
        HasTerminator(evidence->input_hash, kSha256HexCapacity);
    evidence->input_path[kPathCapacity - 1] = 0;
    evidence->input_hash[kSha256HexCapacity - 1] = 0;
    evidence->input_path_complete =
        evidence->input_path_complete && path_terminated;
    evidence->input_hash_available =
        evidence->input_hash_available && hash_terminated;
}

void SanitizeFinish(FinishEvidence* evidence) noexcept
{
    if (!evidence) return;
    const bool fingerprint_terminated = HasTerminator(
        evidence->canonical_tree_fingerprint, kSha256HexCapacity);
    evidence->canonical_tree_fingerprint[kSha256HexCapacity - 1] = 0;
    evidence->canonical_tree_fingerprint_available =
        evidence->canonical_tree_fingerprint_available &&
        fingerprint_terminated;
}

ActiveLoad* ResolveToken(Ledger* ledger, const LoadToken& token) noexcept
{
    if (!ledger || !token.generation || token.slot >= kActiveLoadCapacity)
        return nullptr;
    ActiveLoad& active = ledger->active_loads[token.slot];
    if (!active.active ||
        active.observation.generation != token.generation)
        return nullptr;
    return &active;
}

void Evaluate(Observation* observation) noexcept
{
    if (!observation) return;
    const BeginEvidence& begin = observation->begin;
    const ProviderEvidence& provider = observation->provider;
    const ParserEvidence& parser = observation->parser;
    const FinishEvidence& finish = observation->finish;

    const bool begin_path_complete = HasCompleteText(
        begin.path, kPathCapacity, begin.path_complete);
    const bool expected_path = begin_path_complete &&
        PathsEqual(begin.path, kStartupPath);
    observation->exact_target_complete =
        begin.load_target_validated && begin.manager_root_matched &&
        begin.manager != 0;
    observation->count_evidence_complete =
        begin.definition_count_before_readable &&
        finish.definition_count_after_readable;
    observation->provider_provenance_complete =
        observation->provider_observed &&
        !observation->provider_ambiguous &&
        provider.request_observed && provider.open_succeeded &&
        provider.exact_source && provider.b0_provider &&
        provider.source != 0 && provider.content_epoch != 0 &&
        provider.source_record_index != kInvalidRecordIndex &&
        provider.successful_add_ordinal != 0 &&
        HasCompleteText(provider.logical_path, kPathCapacity,
                        provider.logical_path_complete) &&
        HasCompleteText(provider.resolved_provider, kProviderCapacity,
                        provider.resolved_provider_complete) &&
        begin_path_complete &&
        PathsEqual(begin.path, provider.logical_path);
    observation->parser_succeeded =
        observation->parser_observed && parser.result_object != 0;
    observation->parser_provenance_complete =
        observation->parser_observed &&
        !observation->parser_ambiguous && parser.input_observed &&
        parser.input_hash_available && parser.input_size != 0 &&
        HasCompleteText(parser.input_path, kPathCapacity,
                        parser.input_path_complete) &&
        begin_path_complete && PathsEqual(begin.path, parser.input_path) &&
        IsSha256(parser.input_hash) &&
        (!observation->parser_succeeded ||
         parser.parser_registration_generation != 0);
    observation->provenance_complete =
        observation->exact_target_complete && expected_path &&
        observation->count_evidence_complete &&
        finish.function_return_observed &&
        observation->provider_provenance_complete &&
        observation->parser_provenance_complete;
    observation->function_return_disagrees_with_parser =
        finish.function_return_observed &&
        finish.function_returned_true &&
        observation->parser_observed && !observation->parser_succeeded;
    observation->definition_count_retained_after_parser_failure =
        observation->parser_observed && !observation->parser_succeeded &&
        observation->provenance_complete &&
        begin.definition_count_before == finish.definition_count_after;
    observation->startup_baseline_candidate =
        observation->provenance_complete &&
        observation->parser_succeeded &&
        begin.caller_rva == kStartupCallRva;
    observation->canonical_tree_fingerprint_complete =
        finish.canonical_tree_fingerprint_available &&
        finish.canonical_tree_fingerprint_pointer_free &&
        finish.canonical_tree_fingerprint_generation ==
            observation->generation &&
        IsSha256(finish.canonical_tree_fingerprint);
    observation->consumer_inventory_evidence_complete =
        finish.consumer_inventory_complete &&
        finish.consumer_inventory_generation == observation->generation;
    observation->observation_complete =
        observation->record_storage_complete &&
        observation->provenance_complete &&
        observation->parser_succeeded &&
        observation->canonical_tree_fingerprint_complete &&
        observation->consumer_inventory_evidence_complete;

    if (!observation->parser_observed) {
        observation->disposition =
            ObservationDisposition::ParserResultMissing;
    } else if (!observation->parser_succeeded &&
               observation->provenance_complete) {
        observation->disposition =
            ObservationDisposition::ParserFailedStateRetained;
    } else if (!observation->provenance_complete) {
        observation->disposition =
            ObservationDisposition::ProvenanceIncomplete;
    } else if (!observation->record_storage_complete) {
        observation->disposition =
            ObservationDisposition::StorageCapacityExceeded;
    } else if (!observation->canonical_tree_fingerprint_complete) {
        observation->disposition =
            ObservationDisposition::CanonicalTreeFingerprintMissing;
    } else if (!observation->consumer_inventory_evidence_complete) {
        observation->disposition =
            ObservationDisposition::ConsumerInventoryIncomplete;
    } else {
        observation->disposition = ObservationDisposition::Complete;
    }
}

}  // namespace

void Initialize(Ledger* ledger) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
}

bool BeginObservation(Ledger* ledger, const BeginEvidence& evidence,
                      LoadToken* token) noexcept
{
    if (token) std::memset(token, 0, sizeof(*token));
    if (!ledger || !token || !Acquire(ledger)) return false;
    std::uint32_t slot = static_cast<std::uint32_t>(kActiveLoadCapacity);
    for (std::uint32_t index = 0; index < kActiveLoadCapacity; ++index) {
        if (!ledger->active_loads[index].active) {
            slot = index;
            break;
        }
    }
    if (slot >= kActiveLoadCapacity) {
        ++ledger->loss.active_load_overflow;
        Release(ledger);
        return false;
    }
    std::uint64_t generation = ++ledger->next_generation;
    if (!generation) generation = ++ledger->next_generation;
    ActiveLoad& active = ledger->active_loads[slot];
    std::memset(&active, 0, sizeof(active));
    active.active = true;
    active.observation.generation = generation;
    active.observation.begin = evidence;
    SanitizeBegin(&active.observation.begin);
    active.observation.disposition = ObservationDisposition::Unknown;
    ++ledger->active_count;
    token->generation = generation;
    token->slot = slot;
    Release(ledger);
    return true;
}

bool ObserveProvider(Ledger* ledger, const LoadToken& token,
                     const ProviderEvidence& evidence) noexcept
{
    if (!ledger || !Acquire(ledger)) return false;
    ActiveLoad* active = ResolveToken(ledger, token);
    if (!active) {
        ++ledger->loss.stale_token;
        Release(ledger);
        return false;
    }
    Observation& observation = active->observation;
    if (observation.provider_observed) {
        observation.provider_ambiguous = true;
        ++ledger->loss.duplicate_provider;
        Release(ledger);
        return false;
    }
    observation.provider = evidence;
    SanitizeProvider(&observation.provider);
    observation.provider_observed = true;
    Release(ledger);
    return true;
}

bool ObserveParser(Ledger* ledger, const LoadToken& token,
                   const ParserEvidence& evidence) noexcept
{
    if (!ledger || !Acquire(ledger)) return false;
    ActiveLoad* active = ResolveToken(ledger, token);
    if (!active) {
        ++ledger->loss.stale_token;
        Release(ledger);
        return false;
    }
    Observation& observation = active->observation;
    if (observation.parser_observed) {
        observation.parser_ambiguous = true;
        ++ledger->loss.duplicate_parser;
        Release(ledger);
        return false;
    }
    observation.parser = evidence;
    SanitizeParser(&observation.parser);
    observation.parser_observed = true;
    Release(ledger);
    return true;
}

bool FinishObservation(Ledger* ledger, const LoadToken& token,
                       const FinishEvidence& evidence,
                       Observation* result) noexcept
{
    if (result) std::memset(result, 0, sizeof(*result));
    if (!ledger || !Acquire(ledger)) return false;
    ActiveLoad* active = ResolveToken(ledger, token);
    if (!active) {
        ++ledger->loss.stale_token;
        Release(ledger);
        return false;
    }
    Observation finished = active->observation;
    finished.finish = evidence;
    SanitizeFinish(&finished.finish);
    finished.record_storage_complete =
        ledger->observation_count < kObservationCapacity;
    Evaluate(&finished);

    bool stored = false;
    if (finished.record_storage_complete) {
        ledger->observations[ledger->observation_count++] = finished;
        stored = true;
    } else {
        ++ledger->loss.observation_overflow;
    }
    if (result) *result = finished;
    std::memset(active, 0, sizeof(*active));
    if (ledger->active_count) --ledger->active_count;
    Release(ledger);
    return stored;
}

bool CaptureSnapshot(Ledger* ledger, Snapshot* snapshot) noexcept
{
    if (!ledger || !snapshot || !Acquire(ledger)) return false;
    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->next_generation = ledger->next_generation;
    snapshot->active_count = ledger->active_count;
    snapshot->observation_count = ledger->observation_count;
    std::memcpy(snapshot->active_loads, ledger->active_loads,
                sizeof(snapshot->active_loads));
    std::memcpy(snapshot->observations, ledger->observations,
                sizeof(snapshot->observations));
    snapshot->loss = ledger->loss;
    Release(ledger);
    return true;
}

const char* DispositionName(ObservationDisposition disposition) noexcept
{
    switch (disposition) {
    case ObservationDisposition::ParserResultMissing:
        return "parser_result_missing";
    case ObservationDisposition::ParserFailedStateRetained:
        return "parser_failed_state_retained";
    case ObservationDisposition::ProvenanceIncomplete:
        return "provenance_incomplete";
    case ObservationDisposition::CanonicalTreeFingerprintMissing:
        return "canonical_tree_fingerprint_missing";
    case ObservationDisposition::ConsumerInventoryIncomplete:
        return "consumer_inventory_incomplete";
    case ObservationDisposition::StorageCapacityExceeded:
        return "storage_capacity_exceeded";
    case ObservationDisposition::Complete:
        return "complete";
    default:
        return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::animevents
