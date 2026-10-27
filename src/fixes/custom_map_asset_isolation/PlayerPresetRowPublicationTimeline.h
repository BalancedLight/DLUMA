#ifndef DLUMA_FIXES_PLAYER_PRESET_ROW_PUBLICATION_TIMELINE_H
#define DLUMA_FIXES_PLAYER_PRESET_ROW_PUBLICATION_TIMELINE_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::player_publication {

constexpr std::size_t kKnownOwnerCount = 2U;
constexpr std::size_t kRowCount = 8U;
constexpr std::size_t kProviderPathCapacity = 384U;
constexpr std::size_t kPresetNameCapacity = 192U;
constexpr std::size_t kCachePublicationCapacity = 64U;
constexpr std::size_t kObserverEventCapacity = 256U;

enum class Checkpoint : std::uint8_t {
    StartupB0Commit,
    BeforeProjectPremountOriginal,
    UpdateDlcContentReturn,
    ChangeAfterFadeEntry,
    ChangeAfterFadeReturn,
    PlayerDiOnCreateReturn,
    FirstStableGameplayFrame,
    C08Entry,
    C10Entry,
    SessionOnDestroyEntry,
    SessionOnDestroyOriginalReturn,
    PostC10Pass1,
    PostC10Pass2,
    Count,
};

enum class OwnerKind : std::uint8_t {
    Unknown,
    LocalProcessGlobal,
    CharacterManagerGlobal,
};

enum class RowSemanticKind : std::uint8_t {
    Incomplete,
    AbsentPresetRow,
    PresetRow,
};

enum class CacheMembership : std::uint8_t {
    Unobserved,
    ProvenMember,
    ProvenNonMember,
    Conflict,
};

enum class ProviderConfidence : std::uint8_t {
    Unobserved,
    ObservedUnresolved,
    ExactBaseSource,
    ExactProjectSource,
};

enum class ExactFailure : std::uint8_t {
    None,
    InvalidArgument,
    UnsupportedImage,
    EnvironmentUnreadable,
    InventoryUnavailable,
    OwnerSlotUnreadable,
    OwnerMissing,
    OwnerGenerationChanged,
    OwnerDestroyed,
    OwnerUnreadable,
    StateSnapshotRejected,
    RowSnapshotIncomplete,
    UnstableTwoPassSnapshot,
    CacheSnapshotChanged,
    TimelineBusy,
    CapacityLost,
    ProjectNotAdmitted,
    StableLevelUnavailable,
    LocalPlayerDiUnavailable,
    PlayerDiOnCreateIncomplete,
    CallerMismatch,
    ReceiverMismatch,
    CallSiteMismatch,
    ReentrancyConflict,
    RouteRejected,
};

enum class Attribution : std::uint8_t {
    StockBeforeProjectAdmission,
    StockAfterAdmissionUnrelated,
    ProjectSourceAttributed,
    ProjectTransitionAttributed,
    NaturalPlayerCreationUnresolved,
    Incomplete,
};

enum class ObserverRoute : std::uint8_t {
    LocalClientCreateRepresentation,
    CachePlayerLevelPreset,
    FindPlayerLevelPreset,
    SetProgressTemplate,
    RemovePrecachedLevelPresets,
    InitializeVariables,
    SetVariablesToLevel,
    IgsObjectInitObject,
    PlayerDiOnCreate,
    RowPublicationStore,
    Count,
};

enum class WriterSubphase : std::uint8_t {
    Unspecified,
    CreateRepresentationEntry,
    CreateRepresentationBeforeSetProgressTemplate,
    SetProgressTemplateEntry,
    SetProgressTemplateBeforeInitializeVariables,
    InitializeVariablesEntry,
    CachePlayerLevelPresetEntry,
    CachePlayerLevelPresetReturn,
    FindPlayerLevelPresetReturn,
    RowPublicationStore,
    InitializeVariablesReturn,
    SetProgressTemplateReturn,
    CreateRepresentationAfterSetProgressTemplate,
    CreateRepresentationBeforeInitObject,
    PlayerDiOnCreateEntry,
    PlayerDiOnCreateReturn,
};

enum class CallerModule : std::uint8_t {
    Unknown,
    GameDll,
    Engine,
    FileSystem,
    ImageLib,
};

struct ProviderEvidence {
    std::uintptr_t source;
    std::uint32_t source_ordinal;
    std::uint32_t content_epoch;
    ProviderConfidence confidence;
    char path[kProviderPathCapacity];
};

struct RowEvidence {
    std::uintptr_t preset;
    std::uint64_t definition_fingerprint;
    std::uint32_t definition_count;
    std::uint32_t row_index;
    RowSemanticKind semantic_kind;
    CacheMembership cache_membership;
    ProviderEvidence provider;
    ExactFailure first_exact_failure;
    bool complete;
};

struct OwnerEvidence {
    OwnerKind owner_kind;
    std::uintptr_t owner_slot;
    std::uintptr_t player_state;
    std::uint64_t owner_generation;
    RowEvidence rows[kRowCount];
    ExactFailure first_exact_failure;
    bool exact_owner_slot;
    bool generation_valid;
    bool live;
    bool readable;
    bool stable;
};

struct CheckpointEvidence {
    Checkpoint checkpoint;
    std::uint64_t trace_sequence;
    std::uint64_t qpc;
    std::uint64_t baseline_generation;
    std::uint64_t transition_generation;
    std::uint64_t inventory_revision;
    std::uint64_t cache_generation;
    std::uint64_t cache_fingerprint;
    std::uintptr_t gameplay_container;
    std::uint32_t thread_id;
    std::uint32_t content_epoch;
    std::uint32_t active_custom_source_count;
    OwnerEvidence owners[kKnownOwnerCount];
    ExactFailure first_exact_failure;
    bool supported_image;
    bool environment_readable;
    bool owner_set_stable;
    bool row_headers_stable;
    bool stock_route_validated;
};

struct CheckpointRecord {
    CheckpointEvidence evidence;
    bool captured;
    bool complete;
};

struct CachePublicationRecord {
    std::uint64_t sequence;
    std::uint64_t qpc;
    std::uint64_t input_identity_hash;
    std::uint64_t before_cache_generation;
    std::uint64_t after_cache_generation;
    std::uintptr_t container;
    std::uintptr_t returned_preset;
    std::uintptr_t affected_player_state;
    std::uint64_t affected_owner_generation;
    std::uint32_t thread_id;
    std::uint32_t content_epoch;
    std::uint32_t affected_row;
    OwnerKind affected_owner_kind;
    ProviderEvidence provider;
    bool newly_inserted;
    bool reused;
    bool exact_route;
    bool before_inventory_complete;
    bool after_inventory_complete;
    bool classification_complete;
    ExactFailure first_exact_failure;
    char primary_name[kPresetNameCapacity];
    char secondary_name[kPresetNameCapacity];
};

struct ObserverEvent {
    std::uint64_t sequence;
    std::uint64_t qpc;
    std::uint64_t baseline_generation;
    std::uint64_t content_generation;
    std::uint64_t transition_generation;
    std::uint64_t owner_generation;
    std::uint64_t base_template_identity_hash;
    std::uint64_t progression_template_identity_hash;
    std::uint64_t input_identity_hash;
    std::uint64_t before_cache_generation;
    std::uint64_t after_cache_generation;
    std::uint64_t before_cache_fingerprint;
    std::uint64_t after_cache_fingerprint;
    std::uintptr_t caller_address;
    std::uintptr_t caller_module_base;
    std::uintptr_t owner_slot;
    std::uintptr_t player_state;
    std::uintptr_t gameplay_container;
    std::uintptr_t preset;
    std::uintptr_t provider_source_handle;
    std::uint32_t thread_id;
    std::uint32_t content_epoch;
    std::uint32_t caller_rva;
    std::uint32_t return_rva;
    std::uint32_t provider_source_ordinal;
    std::uint32_t row;
    OwnerKind owner_kind;
    ObserverRoute route;
    WriterSubphase subphase;
    CallerModule caller_module;
    ProviderConfidence provider_confidence;
    ExactFailure first_exact_failure;
    bool newly_inserted;
    bool reused;
    bool exact_route;
    char base_template[kPresetNameCapacity];
    char progression_template[kPresetNameCapacity];
    char input_name[kPresetNameCapacity];
    char provider_path[kProviderPathCapacity];
};

struct RouteValidationRecord {
    ObserverRoute route;
    ExactFailure rejection;
    bool attempted;
    bool validated;
};

struct FirstStableGameplayState {
    std::uint64_t eligible_sequence;
    std::uint64_t capture_sequence;
    std::uint32_t attempt_count;
    ExactFailure first_failure;
    ExactFailure last_failure;
    bool terminal;
};

struct TimelineLoss {
    std::uint32_t invalid_checkpoint;
    std::uint32_t duplicate_checkpoint;
    std::uint32_t critical_lock_contention;
    std::uint32_t cache_publication_overflow;
    std::uint32_t observer_event_overflow;
};

struct Timeline {
    CheckpointRecord checkpoints[
        static_cast<std::size_t>(Checkpoint::Count)];
    CachePublicationRecord cache_publications[
        kCachePublicationCapacity];
    ObserverEvent observer_events[kObserverEventCapacity];
    std::uint64_t project_admission_sequence;
    std::uint32_t project_admission_epoch;
    std::uint32_t cache_publication_count;
    std::uint32_t observer_event_count;
    RouteValidationRecord route_validations[
        static_cast<std::size_t>(ObserverRoute::Count)];
    FirstStableGameplayState first_stable_gameplay;
    TimelineLoss loss;
};

struct FirstPublication {
    bool available;
    Checkpoint checkpoint;
    std::uint64_t sequence;
    std::uint32_t thread_id;
    OwnerKind owner_kind;
    std::uint64_t owner_generation;
    std::uint32_t row;
    RowSemanticKind baseline_kind;
    RowSemanticKind post_kind;
    std::uintptr_t baseline_preset;
    std::uintptr_t post_preset;
    std::uint32_t baseline_definition_count;
    std::uint32_t post_definition_count;
    std::uint64_t baseline_fingerprint;
    std::uint64_t post_fingerprint;
    ProviderEvidence provider;
    CacheMembership cache_membership;
    Attribution attribution;
};

struct DeltaSummary {
    bool available;
    OwnerKind first_changed_owner;
    std::uint32_t first_changed_row;
    RowSemanticKind baseline_kind;
    RowSemanticKind post_kind;
    std::uintptr_t baseline_preset;
    std::uintptr_t post_preset;
    std::uint32_t baseline_definition_count;
    std::uint32_t post_definition_count;
    std::uint64_t baseline_fingerprint;
    std::uint64_t post_fingerprint;
    std::uint32_t rows_changed_mask;
    bool all_rows_changed_same_direction;
    bool other_owner_unchanged;
};

void Initialize(Timeline* timeline) noexcept;
bool RecordCheckpoint(
    Timeline* timeline, const CheckpointEvidence& evidence) noexcept;
void RecordCriticalContention(Timeline* timeline) noexcept;
bool HasCheckpoint(
    const Timeline& timeline, Checkpoint checkpoint) noexcept;
void RecordProjectAdmissionBoundary(
    Timeline* timeline, std::uint64_t sequence,
    std::uint32_t content_epoch) noexcept;
bool RecordCachePublication(
    Timeline* timeline, const CachePublicationRecord& record) noexcept;
bool RecordObserverEvent(
    Timeline* timeline, const ObserverEvent& event) noexcept;
void RecordRouteValidation(
    Timeline* timeline, ObserverRoute route, bool validated,
    ExactFailure rejection) noexcept;
void RecordFirstStableGameplayAttempt(
    Timeline* timeline, ExactFailure failure,
    std::uint64_t eligible_sequence) noexcept;
void FinalizeFirstStableGameplayUnavailable(
    Timeline* timeline, ExactFailure failure) noexcept;
FirstPublication FindFirstPublication(const Timeline& timeline) noexcept;
DeltaSummary BuildDeltaSummary(const Timeline& timeline) noexcept;

const char* CheckpointName(Checkpoint checkpoint) noexcept;
const char* OwnerKindName(OwnerKind kind) noexcept;
const char* RowSemanticKindName(RowSemanticKind kind) noexcept;
const char* CacheMembershipName(CacheMembership membership) noexcept;
const char* ProviderConfidenceName(ProviderConfidence confidence) noexcept;
const char* ExactFailureName(ExactFailure failure) noexcept;
const char* AttributionName(Attribution attribution) noexcept;
const char* ObserverRouteName(ObserverRoute route) noexcept;
const char* WriterSubphaseName(WriterSubphase subphase) noexcept;
const char* CallerModuleName(CallerModule module) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::player_publication

#endif
