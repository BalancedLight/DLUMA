#ifndef DLUMA_FIXES_PERSISTENT_CONSUMER_LEDGER_H
#define DLUMA_FIXES_PERSISTENT_CONSUMER_LEDGER_H

#include "CustomContentLedger.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::persistent {

constexpr std::size_t kPersistentConsumerCapacity = 256;
constexpr std::size_t kPersistentPathCapacity = 1024;
constexpr std::size_t kPersistentProviderCapacity = 2048;
constexpr std::size_t kPersistentTextCapacity = 192;
constexpr std::size_t kPersistentRelatedObjectCount = 8;
constexpr std::size_t kPersistentFovPropertyCount = 4;
constexpr std::size_t kStagedAssetsSourceCapacity = 64;
constexpr std::size_t kPostSupersessionOpenCapacity = 128;
constexpr std::size_t kMaterialReloadCapacity = 32;
constexpr std::size_t kMaterialIdentityCapacity = 256;
constexpr std::size_t kProgressionOverlayCapacity = 128;
constexpr std::size_t kPlayerStateSnapshotCapacity = 64;

enum class PersistentConsumerDomain : std::uint8_t {
    DefaultLevels,
    EffectiveFov,
    GameplayParameters,
    ScriptDeclaration,
    AudioData,
    AudioBank,
    MusicPreset,
    UiDeclaration,
    AssetsPcFile,
    AssetsPcResource,
    Count,
};

enum class PersistentConsumerResult : std::uint8_t {
    Observed,
    OpenFailed,
    Parsed,
    CacheHit,
    RegistrationConflict,
    LoadSucceeded,
    LoadFailed,
    NaturalUnload,
    NaturalReset,
    Unknown,
};

enum class PersistentConsumerDisposition : std::uint8_t {
    Unresolved,
    SourceRemovedFutureProviderRestored,
    PersistentOwnerNaturallyReset,
    PersistentOwnerRetained,
    LiveValueStale,
};

enum class DefaultLevelsOutcome : std::uint8_t {
    NaturallyRestored,
    DefinitionPersisted,
    DefinitionRestoredLiveValuePersisted,
    ProviderRestoredButManagerNotReloaded,
    ObservationIncomplete,
};

enum class StagedAssetsSourceState : std::uint8_t {
    Unknown,
    ActiveCurrent,
    ActiveSuperseded,
    Removed,
};

enum class MaterialIdentityAction : std::uint8_t {
    Observed,
    Constructed,
    CacheHit,
    Replaced,
    ReboundInPlace,
    Retired,
};

enum class MaterialObservationOutcome : std::uint8_t {
    NaturallyReplaced,
    SupersededSourceServedCurrentProject,
    PreopenResourceCachePersisted,
    PostopenResourceCachePersisted,
    DownstreamConsumerPersisted,
    ObservationIncomplete,
};

enum class ProgressionOverlayStage : std::uint8_t {
    Base,
    Progression,
    ClassProgression,
    Unknown,
};

enum class PlayerProgressionOutcome : std::uint8_t {
    NaturallyRestored,
    DefinitionPersisted,
    DefinitionRestoredLiveStatePersisted,
    ProviderRestoredManagerStale,
    ObservationIncomplete,
};

struct StagedAssetsSourceObservation {
    std::uint64_t sequence;
    std::uint32_t epoch;
    std::uint32_t current_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uintptr_t source;
    StagedAssetsSourceState state;
    bool exact_source;
    bool source_active;
    char source_path[kPersistentProviderCapacity];
    char project_root[kPersistentPathCapacity];
};

struct PostSupersessionAssetsOpen {
    std::uint64_t sequence;
    std::uint32_t old_epoch;
    std::uint32_t current_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uintptr_t source;
    std::uintptr_t file_handle;
    std::uintptr_t immediate_caller;
    std::uintptr_t highest_resolved_consumer;
    std::uint32_t caller_rva;
    bool exact_source;
    char logical_path[kPersistentPathCapacity];
    char provider[kPersistentProviderCapacity];
    char caller_module[kPersistentTextCapacity];
};

struct MaterialReloadObservation {
    std::uint64_t sequence;
    std::uint64_t observation_generation;
    std::uint32_t staged_content_epoch;
    std::uint32_t owner_thread_id;
    std::uintptr_t material_manager;
    std::uintptr_t old_local_pack;
    std::uintptr_t new_local_pack;
    std::uintptr_t local_mp_source;
    std::uint32_t local_mp_source_record;
    std::uint32_t local_mp_source_epoch;
    std::uint32_t old_material_count;
    std::uint32_t new_material_count;
    std::uint32_t rebind_attempt_count;
    std::uint32_t rebind_success_count;
    std::uint32_t global_fallback_count;
    bool old_pack_destructor_entered;
    bool old_pack_destructor_completed;
    bool manager_pointer_swapped;
    bool scope_complete;
    char local_mp_logical_path[kPersistentPathCapacity];
    char local_mp_provider[kPersistentProviderCapacity];
    char old_pack_path[kPersistentProviderCapacity];
    char new_pack_path[kPersistentProviderCapacity];
};

struct MaterialResourceIdentityObservation {
    std::uint64_t sequence;
    std::uint64_t registration_generation;
    std::uint64_t observation_generation;
    std::uint32_t provider_epoch;
    std::uintptr_t object;
    std::uintptr_t prior_object;
    std::uintptr_t consumer;
    std::uint64_t logical_key;
    MaterialIdentityAction action;
    bool file_open_observed;
    char logical_name[kPersistentPathCapacity];
    char provider[kPersistentProviderCapacity];
};

struct ProgressionOverlayObservation {
    std::uint64_t sequence;
    std::uint64_t template_generation;
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uintptr_t player_state;
    std::uintptr_t preset;
    std::uintptr_t definition_vector;
    std::uintptr_t source;
    ProgressionOverlayStage stage;
    std::uint64_t definition_hash_before;
    std::uint64_t definition_hash_after;
    std::uint32_t definition_count_before;
    std::uint32_t definition_count_after;
    std::uint16_t maximum_level_before;
    std::uint16_t maximum_level_after;
    std::int32_t level_type;
    bool exact_source;
    bool observation_complete;
    char logical_path[kPersistentPathCapacity];
    char provider[kPersistentProviderCapacity];
};

struct PlayerStateSnapshot {
    std::uint64_t sequence;
    std::uint64_t template_generation;
    std::uint32_t epoch;
    std::uintptr_t player_state;
    std::uintptr_t player_variables;
    /* PlayerState+0x40 rows have 0x20-byte stride.  row+0x00 is the
     * definition-vector pointer returned by FindPlayerLevelPreset
     * (cache_record+0x20), not the complete cache record. */
    std::uintptr_t definition_vectors[kPersistentRelatedObjectCount];
    std::uint16_t current_levels[kPersistentRelatedObjectCount];
    std::uint16_t maximum_levels[kPersistentRelatedObjectCount];
    bool clear_observed;
    bool initialize_observed;
    bool effective_fov_available;
    bool observation_complete;
    char progression_template[kPersistentTextCapacity];
    char class_progression_template[kPersistentTextCapacity];
};

struct OptionalObserverLoss {
    std::uint32_t count;
    std::uint64_t first_sequence;
    std::uint64_t last_sequence;
};

struct PersistentConsumerObservation {
    std::uint64_t sequence;
    std::uint64_t transition_generation;
    std::uint32_t epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint32_t caller_rva;
    std::uint32_t consumer_rva;
    std::uint32_t subject_id;
    content::CustomSourceKind source_kind;
    PersistentConsumerDomain domain;
    PersistentConsumerResult result;
    PersistentConsumerDisposition disposition;
    std::uintptr_t source;
    std::uintptr_t file_handle;
    std::uintptr_t consumer;
    std::uintptr_t object;
    std::uint64_t object_generation;
    std::uint64_t value0;
    std::uint64_t value1;
    std::uint64_t definition_hash_before;
    std::uint64_t definition_hash_after;
    std::uint32_t definition_count_before;
    std::uint32_t definition_count_after;
    std::uint16_t maximum_level_before;
    std::uint16_t maximum_level_after;
    std::int32_t level_type;
    std::uintptr_t related_objects[kPersistentRelatedObjectCount];
    std::uint16_t current_levels[kPersistentRelatedObjectCount];
    std::uint16_t maximum_levels[kPersistentRelatedObjectCount];
    std::uint32_t fov_value_bits[kPersistentFovPropertyCount];
    std::uint8_t fov_present_mask;
    bool exact_source;
    bool observation_complete;
    bool persistence_possible;
    char logical_path[kPersistentPathCapacity];
    char provider[kPersistentProviderCapacity];
    char caller_module[kPersistentTextCapacity];
    char consumer_name[kPersistentTextCapacity];
    char logical_fingerprint[kPersistentTextCapacity];
};

struct PersistentDomainLoss {
    std::uint32_t count;
    std::uint64_t first_sequence;
    std::uint64_t last_sequence;
};

struct PersistentConsumerLedger {
    volatile LONG lock;
    std::uint32_t count;
    PersistentConsumerObservation records[kPersistentConsumerCapacity];
    PersistentDomainLoss
        losses[static_cast<std::size_t>(PersistentConsumerDomain::Count)];
    volatile LONG staged_assets_lock;
    std::uint32_t staged_assets_source_count;
    std::uint32_t post_supersession_open_count;
    StagedAssetsSourceObservation
        staged_assets_sources[kStagedAssetsSourceCapacity];
    PostSupersessionAssetsOpen
        post_supersession_opens[kPostSupersessionOpenCapacity];
    OptionalObserverLoss staged_assets_loss;
    volatile LONG material_lock;
    std::uint32_t material_reload_count;
    std::uint32_t material_identity_count;
    MaterialReloadObservation material_reloads[kMaterialReloadCapacity];
    MaterialResourceIdentityObservation
        material_identities[kMaterialIdentityCapacity];
    OptionalObserverLoss material_loss;
    volatile LONG player_lock;
    std::uint32_t progression_overlay_count;
    std::uint32_t player_state_snapshot_count;
    ProgressionOverlayObservation
        progression_overlays[kProgressionOverlayCapacity];
    PlayerStateSnapshot
        player_state_snapshots[kPlayerStateSnapshotCapacity];
    OptionalObserverLoss player_progression_loss;
};

struct PersistentConsumerSnapshot {
    std::uint32_t count;
    PersistentConsumerObservation records[kPersistentConsumerCapacity];
    PersistentDomainLoss
        losses[static_cast<std::size_t>(PersistentConsumerDomain::Count)];
    std::uint32_t staged_assets_source_count;
    std::uint32_t post_supersession_open_count;
    StagedAssetsSourceObservation
        staged_assets_sources[kStagedAssetsSourceCapacity];
    PostSupersessionAssetsOpen
        post_supersession_opens[kPostSupersessionOpenCapacity];
    OptionalObserverLoss staged_assets_loss;
    std::uint32_t material_reload_count;
    std::uint32_t material_identity_count;
    MaterialReloadObservation material_reloads[kMaterialReloadCapacity];
    MaterialResourceIdentityObservation
        material_identities[kMaterialIdentityCapacity];
    OptionalObserverLoss material_loss;
    std::uint32_t progression_overlay_count;
    std::uint32_t player_state_snapshot_count;
    ProgressionOverlayObservation
        progression_overlays[kProgressionOverlayCapacity];
    PlayerStateSnapshot
        player_state_snapshots[kPlayerStateSnapshotCapacity];
    OptionalObserverLoss player_progression_loss;
};

void Initialize(PersistentConsumerLedger* ledger) noexcept;
bool Record(PersistentConsumerLedger* ledger,
            const PersistentConsumerObservation& observation) noexcept;
bool CaptureSnapshot(PersistentConsumerLedger* ledger,
                     PersistentConsumerSnapshot* snapshot) noexcept;
bool FindLatest(PersistentConsumerLedger* ledger,
                PersistentConsumerDomain domain, std::uint32_t epoch,
                PersistentConsumerObservation* observation) noexcept;
bool FindLatestAny(PersistentConsumerLedger* ledger,
                   PersistentConsumerDomain domain,
                   PersistentConsumerObservation* observation) noexcept;
bool FindLatestByPath(PersistentConsumerLedger* ledger,
                      PersistentConsumerDomain domain,
                      const char* logical_path,
                      PersistentConsumerObservation* observation) noexcept;
const char* DomainName(PersistentConsumerDomain domain) noexcept;
const char* ResultName(PersistentConsumerResult result) noexcept;
const char* DispositionName(
    PersistentConsumerDisposition disposition) noexcept;
DefaultLevelsOutcome ClassifyDefaultLevelsOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool custom_source_reset_completed) noexcept;
const char* DefaultLevelsOutcomeName(DefaultLevelsOutcome outcome) noexcept;
bool RecordStagedAssetsSource(
    PersistentConsumerLedger* ledger,
    const StagedAssetsSourceObservation& observation) noexcept;
bool MarkStagedAssetsEpochCurrent(
    PersistentConsumerLedger* ledger, std::uint32_t current_epoch,
    std::uint64_t sequence) noexcept;
bool MarkStagedAssetsSourceRemoved(
    PersistentConsumerLedger* ledger, std::uintptr_t source,
    std::uint32_t current_epoch, std::uint64_t sequence) noexcept;
bool RecordPostSupersessionOpen(
    PersistentConsumerLedger* ledger,
    const PostSupersessionAssetsOpen& observation) noexcept;
bool RecordMaterialReload(
    PersistentConsumerLedger* ledger,
    const MaterialReloadObservation& observation) noexcept;
bool RecordMaterialIdentity(
    PersistentConsumerLedger* ledger,
    const MaterialResourceIdentityObservation& observation) noexcept;
bool RecordProgressionOverlay(
    PersistentConsumerLedger* ledger,
    const ProgressionOverlayObservation& observation) noexcept;
bool RecordPlayerStateSnapshot(
    PersistentConsumerLedger* ledger,
    const PlayerStateSnapshot& observation) noexcept;
MaterialObservationOutcome ClassifyMaterialOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool material_observer_installed) noexcept;
PlayerProgressionOutcome ClassifyPlayerProgressionOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool player_observer_installed,
    bool custom_source_reset_completed) noexcept;
const char* StagedAssetsSourceStateName(
    StagedAssetsSourceState state) noexcept;
const char* MaterialIdentityActionName(
    MaterialIdentityAction action) noexcept;
const char* MaterialOutcomeName(
    MaterialObservationOutcome outcome) noexcept;
const char* ProgressionOverlayStageName(
    ProgressionOverlayStage stage) noexcept;
const char* PlayerProgressionOutcomeName(
    PlayerProgressionOutcome outcome) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::persistent

#endif
