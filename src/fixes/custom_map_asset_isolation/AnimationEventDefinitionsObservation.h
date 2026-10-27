#ifndef DLUMA_FIXES_ANIMATION_EVENT_DEFINITIONS_OBSERVATION_H
#define DLUMA_FIXES_ANIMATION_EVENT_DEFINITIONS_OBSERVATION_H

#include "../../dlw_win32.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::animevents {

/* Exact DyingLightPlayer gamedll target validated by the B0 safe slice. */
constexpr std::uint32_t kSupportedGamedllTimestamp = 0x623397AAU;
constexpr std::uint32_t kSupportedGamedllImageSize = 0x02082000U;
constexpr char kSupportedGamedllSha256[] =
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
constexpr std::uint32_t kLoadDefinitionsRva = 0x00C18C00U;
constexpr std::uint32_t kLoadDefinitionsEndRva = 0x00C19349U;
constexpr std::uint32_t kLoadDefinitionsStolenLength = 18U;
constexpr std::uint32_t kManagerRootRva = 0x01DF47B8U;
constexpr std::uint32_t kManagerDefinitionCountOffset = 0x08U;
constexpr std::uint32_t kStartupCallRva = 0x003BFBC4U;
constexpr std::uint32_t kParserCallRva = 0x00C18CC1U;
constexpr std::uint32_t kParserTargetRva = 0x012C64C0U;
constexpr std::uint32_t kTreeReleaseCallRva = 0x00C18D8AU;
constexpr std::uint32_t kTreeReleaseTargetRva = 0x0039FD70U;
constexpr std::uint32_t kFactoryCallRva = 0x00C19175U;
constexpr std::uint32_t kFactoryTargetRva = 0x00C19350U;
constexpr std::uint32_t kConsumerGetterRva = 0x00C19560U;
constexpr char kStartupPath[] = "AnimEventData.scr";
constexpr char kLoadDefinitionsAbi[] =
    "bool/byte __fastcall(manager* RCX, ttl::string_base<char> const* RDX)";
constexpr std::uint8_t kLoadDefinitionsPrefix[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56,
    0x57, 0x41, 0x55, 0x48, 0x81, 0xEC, 0x60, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x02, 0x45, 0x33, 0xED,
    0x48, 0x8D, 0x35, 0x11, 0x90, 0x84, 0x00, 0x48,
    0x85, 0xC0, 0x48, 0x8B, 0xE9, 0x44, 0x89, 0xAC,
    0x24, 0x98, 0x01, 0x00, 0x00, 0x48, 0x0F, 0x45,
};

constexpr std::size_t kPathCapacity = 192;
constexpr std::size_t kProviderCapacity = 512;
constexpr std::size_t kSha256HexCapacity = 65;
constexpr std::size_t kActiveLoadCapacity = 8;
constexpr std::size_t kObservationCapacity = 64;
constexpr std::uint32_t kInvalidRecordIndex = 0xFFFFFFFFU;

enum class ObservationDisposition : std::uint8_t {
    Unknown,
    ParserResultMissing,
    ParserFailedStateRetained,
    ProvenanceIncomplete,
    CanonicalTreeFingerprintMissing,
    ConsumerInventoryIncomplete,
    StorageCapacityExceeded,
    Complete,
};

struct LoadToken {
    std::uint64_t generation;
    std::uint32_t slot;
};

struct BeginEvidence {
    std::uint64_t sequence;
    std::uintptr_t manager;
    std::uint32_t caller_rva;
    std::uint64_t definition_count_before;
    bool load_target_validated;
    bool manager_root_matched;
    bool definition_count_before_readable;
    bool path_complete;
    char path[kPathCapacity];
};

struct ProviderEvidence {
    std::uint64_t sequence;
    std::uintptr_t source;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t successful_add_ordinal;
    bool request_observed;
    bool open_succeeded;
    bool exact_source;
    bool b0_provider;
    bool logical_path_complete;
    bool resolved_provider_complete;
    char logical_path[kPathCapacity];
    char resolved_provider[kProviderCapacity];
};

struct ParserEvidence {
    std::uint64_t sequence;
    std::uint64_t parser_registration_generation;
    std::uintptr_t result_object;
    std::uint64_t input_size;
    bool input_observed;
    bool input_hash_available;
    bool input_path_complete;
    char input_path[kPathCapacity];
    /* SHA-256 of the exact parser memory input, not of the descriptor tree. */
    char input_hash[kSha256HexCapacity];
};

struct FinishEvidence {
    std::uint64_t sequence;
    std::uint64_t definition_count_after;
    std::uint64_t canonical_tree_fingerprint_generation;
    std::uint64_t consumer_inventory_generation;
    std::uint32_t inventoried_consumer_count;
    bool definition_count_after_readable;
    bool function_return_observed;
    /* Diagnostic only: this exact routine also returns true on parser null. */
    bool function_returned_true;
    bool canonical_tree_fingerprint_available;
    bool canonical_tree_fingerprint_pointer_free;
    bool consumer_inventory_complete;
    char canonical_tree_fingerprint[kSha256HexCapacity];
};

struct Observation {
    std::uint64_t generation;
    BeginEvidence begin;
    ProviderEvidence provider;
    ParserEvidence parser;
    FinishEvidence finish;
    ObservationDisposition disposition;
    bool provider_observed;
    bool parser_observed;
    bool provider_ambiguous;
    bool parser_ambiguous;
    bool exact_target_complete;
    bool count_evidence_complete;
    bool provider_provenance_complete;
    bool parser_provenance_complete;
    bool provenance_complete;
    bool parser_succeeded;
    bool function_return_disagrees_with_parser;
    bool definition_count_retained_after_parser_failure;
    bool startup_baseline_candidate;
    bool canonical_tree_fingerprint_complete;
    bool consumer_inventory_evidence_complete;
    bool record_storage_complete;
    bool observation_complete;
};

struct ActiveLoad {
    bool active;
    Observation observation;
};

struct ObservationLoss {
    std::uint32_t lock_contention;
    std::uint32_t active_load_overflow;
    std::uint32_t observation_overflow;
    std::uint32_t stale_token;
    std::uint32_t duplicate_provider;
    std::uint32_t duplicate_parser;
};

struct Ledger {
    volatile LONG lock;
    std::uint64_t next_generation;
    std::uint32_t active_count;
    std::uint32_t observation_count;
    ActiveLoad active_loads[kActiveLoadCapacity];
    Observation observations[kObservationCapacity];
    ObservationLoss loss;
};

struct Snapshot {
    std::uint64_t next_generation;
    std::uint32_t active_count;
    std::uint32_t observation_count;
    ActiveLoad active_loads[kActiveLoadCapacity];
    Observation observations[kObservationCapacity];
    ObservationLoss loss;
};

/* Observation-only API: it exposes no owner action or state-replacement path. */
void Initialize(Ledger* ledger) noexcept;
bool BeginObservation(Ledger* ledger, const BeginEvidence& evidence,
                      LoadToken* token) noexcept;
bool ObserveProvider(Ledger* ledger, const LoadToken& token,
                     const ProviderEvidence& evidence) noexcept;
bool ObserveParser(Ledger* ledger, const LoadToken& token,
                   const ParserEvidence& evidence) noexcept;
bool FinishObservation(Ledger* ledger, const LoadToken& token,
                       const FinishEvidence& evidence,
                       Observation* result = nullptr) noexcept;
bool CaptureSnapshot(Ledger* ledger, Snapshot* snapshot) noexcept;

const char* DispositionName(ObservationDisposition disposition) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::animevents

#endif
