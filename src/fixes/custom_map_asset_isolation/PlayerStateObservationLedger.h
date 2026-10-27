#ifndef DLUMA_FIXES_PLAYER_STATE_OBSERVATION_LEDGER_H
#define DLUMA_FIXES_PLAYER_STATE_OBSERVATION_LEDGER_H

#include "../../dlw_win32.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::playerstate {

constexpr std::size_t kOwnerCapacity = 64;
constexpr std::size_t kDefinitionVectorCapacity = 256;
constexpr std::size_t kApplicationCapacity = 128;
constexpr std::size_t kMembershipCapacity = 512;
constexpr std::size_t kSideEffectCapacity = 256;
constexpr std::size_t kAppearanceCapacity = 64;
constexpr std::size_t kNaturalClearCapacity = 64;
constexpr std::size_t kRowCount = 8;
constexpr std::size_t kSelectorCapacity = 192;
constexpr std::size_t kProviderCapacity = 1024;

enum class OwnerKind : std::uint8_t {
    Unknown,
    LocalProcessGlobal,
    CharacterManagerGlobal,
    PlayerDi,
    TutorialPlayerDi,
    OtherRttiObserved,
};

enum class CacheMutationKind : std::uint8_t {
    Append,
    RecordRebuild,
    WholeCacheReset,
};

enum class MembershipDisposition : std::uint8_t {
    LiveGeneration,
    Rebound,
    DestroyedGeneration,
    Unknown,
};

enum class SideEffectKind : std::uint8_t {
    SetProgressTemplateEqual,
    SetProgressTemplateChanged,
    Clear,
    RemovePrecached,
    InitializeVariables,
    ReloadPlayerScripts,
    RecreateRepresentation,
    PlayerDiCreate,
    PlayerDiDestroy,
};

struct DefinitionRowSnapshot {
    std::uintptr_t definition_vector;
    std::uint64_t fingerprint;
    std::uint32_t definition_count;
    std::uint16_t current_level;
    std::uint16_t maximum_level;
    bool row_pointer_readable;
    bool vector_header_readable;
    bool bounded_partial_fingerprint_available;
    bool deep_definition_complete;
    bool preset_absent;
};

struct StateRowsSnapshot {
    std::uintptr_t player_variables;
    DefinitionRowSnapshot rows[kRowCount];
    std::uint64_t combined_fingerprint;
    bool row_table_readable;
    bool player_variables_readable;
    bool all_row_pointers_readable;
    bool all_vector_headers_readable;
    bool all_bounded_partial_fingerprints_available;
    bool deep_definition_complete;
};

struct OwnerRecord {
    std::uintptr_t pointer;
    std::uint64_t generation;
    OwnerKind owner_kind;
    std::uintptr_t owner_object;
    std::uint64_t creation_sequence;
    std::uint64_t destruction_sequence;
    bool live;
};

struct ProviderIdentity {
    std::uintptr_t source;
    std::uint64_t source_registry_generation;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t successful_add_ordinal;
    bool request_observed;
    bool open_succeeded;
    bool exact_source;
    bool source_registry_generation_observed;
    char logical_path[kSelectorCapacity];
    char provider[kProviderCapacity];
    std::uint8_t consumed_input_sha256[32];
    std::uint64_t consumed_input_bytes;
    std::uint64_t native_file_generation;
    std::uint64_t native_source_generation;
    std::uint32_t consumed_input_format;
    bool consumed_input_complete;
};

struct DefinitionVectorRecord {
    std::uintptr_t pointer;
    std::uint64_t cache_generation;
    std::uint64_t sequence;
    std::uint64_t fingerprint;
    std::uint32_t definition_count;
    std::uint32_t logical_type;
    std::uint16_t maximum_level;
    ProviderIdentity provider;
    std::uint32_t provider_request_count;
    std::uint32_t provider_success_count;
    bool record_identity_complete;
    bool provider_complete;
    ProviderIdentity inputs[3];
    std::uint32_t input_count;
    bool inputs_complete;
};

struct StateApplication {
    std::uint64_t sequence;
    std::uint64_t state_generation;
    std::uint64_t template_generation;
    std::uint64_t cache_generation;
    std::uint32_t content_epoch;
    std::uintptr_t state;
    std::uintptr_t player_variables;
    bool selectors_changed;
    bool clear_observed;
    bool initialize_observed;
    bool same_selector_provider_changed;
    bool provider_change_rebuild_skipped;
    bool stale_progression_definition_retained;
    bool post_selection_clear_observed;
    bool post_selection_clear_type0_retained;
    std::uint64_t post_selection_clear_sequence;
    char progression_selector[kSelectorCapacity];
    char class_selector[kSelectorCapacity];
    ProviderIdentity provider;
    ProviderIdentity prior_provider;
    std::uint32_t prior_content_epoch;
    StateRowsSnapshot rows;
};

struct CacheMembershipRecord {
    std::uint64_t sequence;
    std::uint64_t state_generation;
    std::uint64_t old_cache_generation;
    std::uint64_t new_cache_generation;
    std::uintptr_t state;
    std::uint32_t row_index;
    std::uintptr_t before_pointer;
    std::uintptr_t after_pointer;
    std::uint64_t before_fingerprint;
    std::uint32_t before_definition_count;
    MembershipDisposition disposition;
};

struct SideEffectObservation {
    std::uint64_t sequence;
    std::uint64_t state_generation;
    std::uint32_t content_epoch;
    std::uintptr_t state;
    std::uintptr_t owner_object;
    SideEffectKind kind;
    bool entering;
};

struct NaturalClearObservation {
    std::uint64_t enter_sequence;
    std::uint64_t exit_sequence;
    std::uint64_t state_generation;
    std::uint64_t correlated_application_sequence;
    std::uint32_t content_epoch;
    std::uint32_t caller_rva;
    std::uintptr_t state;
    StateRowsSnapshot before;
    StateRowsSnapshot after;
    bool caller_in_gamedll;
    bool type0_pointer_retained;
    bool type0_content_retained;
    bool all_content_retained;
    bool selector_equal_provider_change_correlated;
    bool rebuild_skipped_after_clear;
    bool stale_type0_retained_after_clear;
};

struct AppearanceObservation {
    std::uint64_t sequence;
    std::uint64_t state_generation;
    std::uint32_t content_epoch;
    std::uintptr_t state;
    std::uint64_t before_fingerprint;
    std::uint64_t after_fingerprint;
    bool parse_observed;
    bool provider_changed;
    ProviderIdentity provider;
};

struct ObservationLoss {
    std::uint32_t lock_contention;
    std::uint32_t owner_overflow;
    std::uint32_t vector_overflow;
    std::uint32_t application_overflow;
    std::uint32_t membership_overflow;
    std::uint32_t side_effect_overflow;
    std::uint32_t appearance_overflow;
    std::uint32_t natural_clear_overflow;
};

struct Ledger {
    volatile LONG lock;
    std::uint64_t revision;
    std::uint64_t next_owner_generation;
    std::uint64_t cache_generation;
    std::uint32_t cache_append_count;
    std::uint32_t cache_rebuild_count;
    std::uint32_t cache_reset_count;
    std::uint32_t owner_count;
    std::uint32_t definition_vector_count;
    std::uint32_t application_count;
    std::uint32_t membership_count;
    std::uint32_t side_effect_count;
    std::uint32_t appearance_count;
    std::uint32_t natural_clear_count;
    OwnerRecord owners[kOwnerCapacity];
    DefinitionVectorRecord definition_vectors[kDefinitionVectorCapacity];
    StateApplication applications[kApplicationCapacity];
    CacheMembershipRecord memberships[kMembershipCapacity];
    SideEffectObservation side_effects[kSideEffectCapacity];
    AppearanceObservation appearances[kAppearanceCapacity];
    NaturalClearObservation natural_clears[kNaturalClearCapacity];
    ObservationLoss loss;
};

struct Snapshot {
    std::uint64_t revision;
    std::uint64_t cache_generation;
    std::uint32_t cache_append_count;
    std::uint32_t cache_rebuild_count;
    std::uint32_t cache_reset_count;
    std::uint32_t owner_count;
    std::uint32_t definition_vector_count;
    std::uint32_t application_count;
    std::uint32_t membership_count;
    std::uint32_t side_effect_count;
    std::uint32_t appearance_count;
    std::uint32_t natural_clear_count;
    OwnerRecord owners[kOwnerCapacity];
    DefinitionVectorRecord definition_vectors[kDefinitionVectorCapacity];
    StateApplication applications[kApplicationCapacity];
    CacheMembershipRecord memberships[kMembershipCapacity];
    SideEffectObservation side_effects[kSideEffectCapacity];
    AppearanceObservation appearances[kAppearanceCapacity];
    NaturalClearObservation natural_clears[kNaturalClearCapacity];
    ObservationLoss loss;
};

void Initialize(Ledger* ledger) noexcept;
std::uint64_t ObserveLiveState(Ledger* ledger, std::uintptr_t pointer,
                               OwnerKind owner_kind,
                               std::uintptr_t owner_object,
                               std::uint64_t sequence) noexcept;
bool ObserveDestroyedState(Ledger* ledger, std::uintptr_t pointer,
                           std::uintptr_t owner_object,
                           std::uint64_t sequence,
                           std::uint64_t* generation = nullptr) noexcept;
std::uint64_t FindLiveGeneration(Ledger* ledger,
                                 std::uintptr_t pointer) noexcept;
std::uint32_t CaptureLiveOwners(Ledger* ledger, OwnerRecord* output,
                                std::size_t capacity) noexcept;
std::uint64_t CurrentCacheGeneration(Ledger* ledger) noexcept;
std::uint64_t CurrentRevision(Ledger* ledger) noexcept;
std::uint64_t AdvanceCacheGeneration(Ledger* ledger,
                                     CacheMutationKind kind,
                                     std::uint64_t sequence) noexcept;
bool RecordDefinitionVector(Ledger* ledger,
                            const DefinitionVectorRecord& record) noexcept;
bool RecordApplication(Ledger* ledger, StateApplication* application) noexcept;
bool RecordMembership(Ledger* ledger,
                      const CacheMembershipRecord& membership) noexcept;
bool RecordSideEffect(Ledger* ledger,
                      const SideEffectObservation& observation) noexcept;
bool RecordAppearance(Ledger* ledger,
                      const AppearanceObservation& observation) noexcept;
bool RecordNaturalClear(Ledger* ledger,
                        NaturalClearObservation* observation) noexcept;
bool ClassifyRowRetentionWithoutDeepSnapshot(
    const DefinitionRowSnapshot& before,
    const DefinitionRowSnapshot& after) noexcept;
bool CaptureSnapshot(Ledger* ledger, Snapshot* snapshot) noexcept;

const char* OwnerKindName(OwnerKind kind) noexcept;
const char* MembershipDispositionName(MembershipDisposition value) noexcept;
const char* SideEffectKindName(SideEffectKind kind) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::playerstate

#endif
