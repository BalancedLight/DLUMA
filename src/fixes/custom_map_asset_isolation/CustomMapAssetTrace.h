#ifndef DLUMA_FIXES_CUSTOM_MAP_ASSET_TRACE_H
#define DLUMA_FIXES_CUSTOM_MAP_ASSET_TRACE_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::trace {

constexpr std::size_t kEventTextCapacity = 192;
constexpr std::size_t kCallerModuleCapacity = 64;
constexpr std::size_t kQueueCapacity = 512;
constexpr std::size_t kCriticalTraceReservedCapacity = 32;
constexpr std::size_t kMaxWatchedPaths = 8;
constexpr std::size_t kMaxSummaryTransitions = 32;
constexpr std::size_t kMaxSummaryWatchedOpens = 64;
constexpr std::size_t kMaxSummaryPackPointers = 8;
constexpr std::size_t kMaxSummaryRollbackBarriers = 32;
constexpr std::size_t kMaxSummaryScriptObservations = 64;
constexpr std::size_t kMaximumObservedCampaignMaps = 16;
constexpr std::size_t kCampaignTextCapacity = 128;
constexpr std::size_t kMaxSummaryCampaignLoads = 24;
constexpr std::size_t kMaxSummaryPackReleases = 32;
constexpr std::size_t kMaxSummaryPacksPerRelease = 16;
constexpr std::size_t kTraceLossDomainCount = 14;
constexpr std::uint32_t kTraceFormatVersion = 4;
constexpr std::size_t kTransactionFailureReasonCapacity = 192;

enum class AssetTraceSourceKind : std::uint16_t {
    Unknown,
    ProjectRoot,
    DataDirectory,
    DataWorkshopPak,
    LocaleDirectory,
    LocalePak,
    SpeechDirectory,
    SpeechPak,
    AssetsPc,
    UnknownProjectSource,
};

enum class AssetTraceStagingPhase : std::uint16_t {
    Unknown,
    CustomContentStaging,
    TransitionActive,
    MapActive,
    Frontend,
};

enum class AssetTraceLabelSource : std::uint16_t {
    Unknown,
    OnDestroyBinding,
    TransitionBinding,
    ActiveMapAnnotation,
};

enum AssetTraceAttributionFlag : std::uint16_t {
    AssetTraceAttributionNone = 0,
    AssetTraceAttributionExactSource = 1U << 0,
    AssetTraceAttributionSourceActive = 1U << 1,
    AssetTraceAttributionEpochActive = 1U << 2,
    AssetTraceAttributionEpochAccepting = 1U << 3,
};

enum AssetTraceDropDomain : std::uint32_t {
    AssetTraceDropNone = 0,
    AssetTraceDropTransition = 1U << 0,
    AssetTraceDropFilesystem = 1U << 1,
    AssetTraceDropWatchedOpen = 1U << 2,
    AssetTraceDropResource = 1U << 3,
    AssetTraceDropRollback = 1U << 4,
    AssetTraceDropParserCache = 1U << 5,
    AssetTraceDropAnnotation = 1U << 6,
    AssetTraceDropDiagnostic = 1U << 7,
    AssetTraceDropCampaign = 1U << 8,
    AssetTraceDropPackCollection = 1U << 9,
    AssetTraceDropPackDynamic = 1U << 10,
    AssetTraceDropBroadFileOpen = 1U << 11,
    AssetTraceDropMaterial = 1U << 12,
    AssetTraceDropPlayerProgression = 1U << 13,
};

struct AssetTraceLossDomainSummary {
    std::uint32_t domain_mask;
    std::uint32_t count;
    std::uint64_t first_known_sequence;
    std::uint64_t last_known_sequence;
    bool sequence_unknown;
};

enum class CampaignSnapshotPhase : std::uint8_t {
    Unknown,
    BeforeLoad,
    AfterLoad,
    Frontend,
};

enum class CampaignLoadPersistenceOutcome : std::uint8_t {
    NewCampaignObject,
    ReusedEmptyCampaign,
    ReusedClearedCampaign,
    ReusedReplacedCampaign,
    ReusedMergedCampaign,
    ReusedAppendedCampaign,
    DifferentSelectedCampaign,
    PriorEpochEntriesRemain,
    NaturalResetObserved,
    ObservationIncomplete,
};

enum class PackCollectionState : std::uint8_t {
    Empty,
    ReadableNonempty,
    CountOutOfBounds,
    DataPointerUnreadable,
    ElementUnreadable,
    LayoutValidationFailed,
    SnapshotTruncated,
    Unknown,
};

enum class PackCrossCheckOutcome : std::uint8_t {
    CollectionAndDynamicMatch,
    ValidatedEmptyCollection,
    DynamicPackMissingFromSnapshot,
    SnapshotPackWithoutDynamicRelease,
    LayoutObservationIncomplete,
};

enum CampaignSnapshotCompletenessFlag : std::uint32_t {
    CampaignSnapshotSelectedIndex = 1U << 0,
    CampaignSnapshotCampaignName = 1U << 1,
    CampaignSnapshotMaps = 1U << 2,
    CampaignSnapshotRewards = 1U << 3,
    CampaignSnapshotAfterRespawn = 1U << 4,
    CampaignSnapshotFlags = 1U << 5,
    CampaignSnapshotComplete = 1U << 6,
    CampaignSnapshotFingerprintPartial = 1U << 7,
};

struct BoundedCampaignMap {
    std::uint32_t registration_order;
    std::int32_t map_id;
    bool complete;
    char map_name[kCampaignTextCapacity];
    char level_name[kCampaignTextCapacity];
};

struct CampaignStateSnapshot {
    std::uint32_t content_epoch;
    std::uint32_t selected_campaign_index;
    std::uint32_t campaign_table_count;
    std::uint32_t map_count;
    std::uint32_t rewards_count;
    std::uint32_t after_respawn_count;
    std::uint32_t campaign_id;
    std::uint32_t campaign_flags;
    std::uint8_t campaign_flag0;
    std::uint8_t campaign_flag1;
    std::uint32_t completeness_flags;
    std::uintptr_t manager;
    std::uintptr_t selected_campaign;
    std::uintptr_t manifest_source;
    std::uintptr_t parser_result;
    std::uint64_t campaign_observation_generation;
    std::uint64_t logical_fingerprint;
    CampaignSnapshotPhase phase;
    bool manager_selected;
    char campaign_name[kCampaignTextCapacity];
    char campaign_source[kCampaignTextCapacity];
    char rewards[kCampaignTextCapacity];
    char after_respawn0[kCampaignTextCapacity];
    char after_respawn1[kCampaignTextCapacity];
    char project_root[kEventTextCapacity];
    char manifest_path[kEventTextCapacity];
    char manifest_provider[kEventTextCapacity];
    char manifest_hash[65];
    BoundedCampaignMap maps[kMaximumObservedCampaignMaps];
};

enum class WatchedScriptOutcome : std::uint16_t {
    Unknown,
    ParsedNew,
    ParsedReplaced,
    ParsedMerged,
    CacheHitSameEpoch,
    CacheHitPriorEpoch,
    RegistrationConflict,
    ParseFailed,
    CorrelationUnknown,
};

struct WatchedScriptClassificationSignals {
    bool correlation_exact;
    bool parser_entered;
    bool parser_succeeded;
    bool cache_hit;
    bool prior_epoch_object;
    bool registration_replaced;
    bool registration_merged;
    bool registration_conflict;
};

enum class AssetTraceEventType : std::uint16_t {
    ChangeActiveLevelEnter,
    ChangeActiveLevelBranch,
    ChangeActiveLevelExit,

    ChangeAfterFadeEnter,
    ChangeAfterFadePostUnload,
    ChangeAfterFadeExit,

    UnloadLevelEnter,
    UnloadLevelExit,

    DestroyActiveLevelEnter,
    DestroyActiveLevelExit,

    UpdateDLCContentEnter,
    UpdateDLCContentExit,

    LoadLevelEnter,
    LoadModuleBeginCall,
    LoadModuleBeginReturn,
    LoadModuleEndCall,
    LoadModuleEndReturn,
    LoadLevelExit,

    FsAddSourceEnter,
    FsAddSourceExit,
    FsRemoveSourceEnter,
    FsRemoveSourceExit,

    FsOpenWatchedEnter,
    FsOpenWatchedResult,

    GameLoadPackEnter,
    GameLoadPackExit,
    GameFreePackEnter,
    GameFreePackExit,

    AssetRegRpackEnter,
    AssetRegRpackExit,
    AssetUnregRpackEnter,
    AssetUnregRpackExit,
    AssetReleasePackEnter,
    AssetReleasePackExit,

    LevelReleasePacksEnter,
    LevelReleasePacksPack,
    LevelReleasePacksExit,

    OnDemandEvictPackEnter,
    OnDemandEvictPackExit,
    LevelPackInternalReleaseEnter,
    LevelPackInternalReleaseExit,
    LevelReleasePacksCrossCheck,

    ScriptLoaderWatchedEnter,
    ScriptLoaderWatchedCacheHit,
    ScriptLoaderWatchedParse,
    ScriptLoaderWatchedRegistrationConflict,
    ScriptLoaderWatchedExit,

    CampaignManagerLoadEnter,
    CampaignManagerSelection,
    CampaignManagerSlotSnapshot,
    CampaignStateBeforeLoad,
    CampaignSnapshotMap,
    CampaignAddMapEnter,
    CampaignAddMapExit,
    CampaignRewardsMutationObserved,
    CampaignAfterRespawnMutationObserved,
    CampaignStateAfterLoad,
    CampaignManagerLoadExit,
    CampaignNaturalInitEnter,
    CampaignNaturalInitExit,
    CampaignNaturalReloadEnter,
    CampaignNaturalReloadExit,
    CampaignNaturalClearEnter,
    CampaignNaturalClearExit,
    CampaignNaturalDestroyEnter,
    CampaignNaturalDestroyExit,
    CampaignFrontendSnapshot,
    CampaignCarryoverObserved,
    CampaignObservationUnknown,

    SessionOnDestroyEnter,
    OnDestroyOwnedUnloadObserved,
    OwnedFrontendUnloadCompleted,
    OwnedReleasePacksEnter,
    OwnedReleasePacksCompleted,
    OutgoingLevelReleaseObserved,
    SessionOnDestroyOriginalReturned,
    CancelledPresentationDetected,
    CancelledCleanupBegin,
    CancelledCleanupSourceRemoval,
    CancelledCleanupVerification,
    CancelledEpochConsumed,
    FreshPresentationEpochCreated,
    ChangeAfterFadeAttestation,
    PendingPostC10TokenPublished,
    C10OriginalReturned,
    PostC10SessionSlotSampled,
    PendingPostC10TokenConsumed,
    PostC10PreflightBegin,
    PostC10PlayerPass1,
    PostC10PlayerPass2,
    PostC10PlayerAssessment,
    PostC10AudioAssessment,
    PostC10TransactionPreparationResult,
    PostC10PreflightResult,
    C10ScopeCleared,
    FrontendSourceResetBegin,
    FrontendSourceResetEnd,
    FirstFrontendPackOrModuleLookup,
    FirstFrontendLookupDisposition,
    ShowMenuMainDisposition,
    FrontendLookupAfterUnboundOnDestroy,

    MaterialReloadEnter,
    MaterialLocalPackOpen,
    MaterialPackLoadEnter,
    MaterialPackLoadExit,
    MaterialPackSelected,
    MaterialExistingObjectRebind,
    MaterialGlobalFallback,
    MaterialOldPackDestructorEnter,
    MaterialOldPackDestructorExit,
    MaterialReloadExit,
    PostSupersessionAssetsOpen,

    PlayerTemplateSelection,
    ProgressionXmlLoadEnter,
    ProgressionXmlLoadExit,
    ProgressionDefinitionOverlay,
    PlayerVariablesAfterTemplateApply,
    PlayerStateOwnerObserved,
    PlayerStateDestroyed,
    PlayerStateClearEnter,
    PlayerStateClearExit,
    PlayerStateRemovePrecachedEnter,
    PlayerStateRemovePrecachedExit,
    PlayerStateInitializeEnter,
    PlayerStateInitializeExit,
    PlayerPresetCacheAppend,
    PlayerPresetRecordRebuild,
    PlayerPresetCacheResetEnter,
    PlayerPresetCacheResetExit,
    PlayerPresetMembershipClassified,
    SameSelectorProviderChanged,
    SameSelectorProviderChangeRebuildSkipped,
    StaleProgressionDefinitionRetained,
    PlayerAppearanceParseEnter,
    PlayerAppearanceParseExit,
    ReloadPlayerScriptsEnter,
    ReloadPlayerScriptsExit,
    RecreateRepresentationEnter,
    RecreateRepresentationExit,

    AudioScriptExecuteEnter,
    AudioScriptExecuteExit,
    SpeechRegistryMutationEnter,
    SpeechRegistryMutationExit,

    PlayerStateLoadEnter,
    PlayerStateLoadClearCall,
    PlayerStateLoadAfterClear,
    PlayerStateLoadExit,
    PlayerPresetProviderRepairPending,
    PlayerPresetRebuildPreflightFailed,
    PlayerPresetTargetRecordMembership,
    ExperimentalPlayerPresetRebuildBegin,
    ExperimentalPlayerPresetRecordRebuilt,
    ExperimentalPlayerPresetRebuildEnd,
    PlayerPresetNaturalClearApplied,
    PlayerPresetProviderRestored,
    SharedStateLiveValueUnknown,

    ActiveLevelObserved,
    QueueOverflow,
};

struct AssetTraceEvent {
    std::uint64_t sequence;
    std::uint64_t qpc;
    std::uint64_t transition_generation;

    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint32_t caller_rva;

    AssetTraceSourceKind source_kind;
    std::uint16_t attribution_flags;
    std::uint16_t update_dlc_depth;
    AssetTraceStagingPhase staging_phase;
    AssetTraceLabelSource label_source;
    WatchedScriptOutcome script_outcome;
    CampaignSnapshotPhase campaign_phase;
    std::uint8_t campaign_slot_selected;
    std::uint32_t dropped_domain_mask;
    std::uint64_t observation_generation;

    std::uint32_t thread_id;
    AssetTraceEventType type;

    std::uintptr_t object;
    std::uintptr_t first;
    std::uintptr_t second;
    std::uintptr_t third;

    std::uint64_t value0;
    std::uint64_t value1;
    std::uint32_t detail0;
    std::uint32_t detail1;
    std::uint32_t detail2;
    std::uint32_t detail3;

    char caller_module[kCallerModuleCapacity];
    char text0[kEventTextCapacity];
    char text1[kEventTextCapacity];
    char text2[kEventTextCapacity];
    char text3[kEventTextCapacity];
    char text4[kEventTextCapacity];
};

/* Payloads are prepared on the hook's stack. Publish stamps correlation data
 * and copies the POD record into the fixed queue; it performs no file I/O. */
struct AssetTraceEventPayload {
    AssetTraceEventType type;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint32_t caller_rva;
    AssetTraceSourceKind source_kind;
    std::uint16_t attribution_flags;
    std::uint16_t update_dlc_depth;
    AssetTraceStagingPhase staging_phase;
    AssetTraceLabelSource label_source;
    WatchedScriptOutcome script_outcome;
    CampaignSnapshotPhase campaign_phase;
    std::uint8_t campaign_slot_selected;
    std::uint32_t dropped_domain_mask;
    std::uint64_t observation_generation;
    std::uintptr_t object;
    std::uintptr_t first;
    std::uintptr_t second;
    std::uintptr_t third;
    std::uint64_t value0;
    std::uint64_t value1;
    std::uint32_t detail0;
    std::uint32_t detail1;
    std::uint32_t detail2;
    std::uint32_t detail3;
    char caller_module[kCallerModuleCapacity];
    char text0[kEventTextCapacity];
    char text1[kEventTextCapacity];
    char text2[kEventTextCapacity];
    char text3[kEventTextCapacity];
    char text4[kEventTextCapacity];
};

using AssetTraceReadQpc = std::uint64_t (*)() noexcept;
using AssetTraceReadThreadId = std::uint32_t (*)() noexcept;
using AssetTraceIsReadable = bool (*)(const void*, std::size_t) noexcept;
using AssetTraceCommit = void (*)(void*) noexcept;
using AssetTracePrepare = bool (*)(void*, AssetTraceEventPayload*) noexcept;

struct AssetTracePlatform {
    AssetTraceReadQpc read_qpc;
    AssetTraceReadThreadId read_thread_id;
    AssetTraceIsReadable is_readable;
};

enum class AssetTraceStringCopyResult : std::uint8_t {
    Copied,
    Truncated,
    Null,
    Unreadable,
    Unterminated,
};

struct AssetTraceWatchList {
    std::uint32_t count;
    char paths[kMaxWatchedPaths][kEventTextCapacity];
};

struct AssetTraceContextToken {
    std::uint64_t generation;
    std::uint64_t previous_generation;
};

/* value1 may carry these diagnostic-evidence bits. They label observations;
 * they never confer ownership or authorize cleanup. */
enum AssetTraceEvidenceFlag : std::uint64_t {
    AssetTraceEvidenceNone = 0,
    AssetTraceEvidenceOutgoing = 1ULL << 0,
    AssetTraceEvidenceDestination = 1ULL << 1,
    AssetTraceEvidenceOriginalSucceeded = 1ULL << 2,
    AssetTraceEvidenceAttributionUnknown = 1ULL << 3,
    AssetTraceEvidenceTransitionContext = 1ULL << 4,
};

enum class AssetTraceTriState : std::uint8_t {
    Unknown,
    No,
    Yes,
};

enum class AssetTraceScriptOutcome : std::uint8_t {
    Unknown,
    Parsed,
    CacheHit,
    RegistrationConflict,
};

struct AssetTraceGenerationSummary {
    std::uint64_t generation;
    bool transition_entered;
    bool transition_exited;
    bool branch_observed;
    std::uint64_t branch_result;
    char selected_branch[64];

    bool destroy_active_level_entered;
    bool destroy_active_level_exited;
    bool update_dlc_content_entered;
    bool update_dlc_content_exited;
    bool unload_level_entered;
    bool unload_level_exited;
    bool any_source_remove_entered;
    bool outgoing_source_remove_succeeded;
    bool source_removal_attribution_unknown;
    bool level_release_packs_entered;
    bool level_release_packs_exited;
    bool outgoing_level_pack_observed;
    bool level_pack_attribution_unknown;
    bool outgoing_pack_evicted;

    std::uint64_t first_watched_lookup_sequence;
    std::uint64_t first_destination_lookup_sequence;
    std::uint64_t last_cleanup_sequence;
    AssetTraceScriptOutcome script_outcome;

    std::uint32_t outgoing_pack_pointer_count;
    bool pack_pointer_correlation_truncated;
    std::uintptr_t outgoing_pack_pointers[kMaxSummaryPackPointers];
};

struct AssetTraceWatchedResolution {
    std::uint64_t sequence;
    std::uint64_t generation;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_add_ordinal;
    std::uint32_t caller_rva;
    std::uintptr_t file_handle;
    std::uintptr_t source;
    AssetTraceSourceKind source_kind;
    AssetTraceStagingPhase staging_phase;
    std::uint16_t update_dlc_depth;
    std::uint64_t result;
    char caller_module[kCallerModuleCapacity];
    char logical_path[kEventTextCapacity];
    char resolved_source[kEventTextCapacity];
    char project_root[kEventTextCapacity];
};

struct AssetTraceScriptObservation {
    std::uint64_t sequence;
    std::uint64_t generation;
    std::uint64_t parser_observation_generation;
    std::uint64_t registry_generation;
    std::uint64_t open_sequence;
    std::uint64_t input_size;
    std::uint32_t content_epoch;
    std::uintptr_t source;
    std::uintptr_t existing_object;
    std::uintptr_t result_object;
    WatchedScriptOutcome outcome;
    char logical_path[kEventTextCapacity];
    char provider[kEventTextCapacity];
    char content_hash[kEventTextCapacity];
    char registry_key[kEventTextCapacity];
};

struct AssetTraceCampaignLoadSummary {
    std::uint64_t observation_generation;
    std::uint64_t transition_generation;
    std::uint32_t epoch;
    std::uint32_t selected_index_before;
    std::uint32_t selected_index_after;
    std::uint32_t table_count_before;
    std::uint32_t table_count_after;
    std::uint32_t map_count_before;
    std::uint32_t map_count_after;
    std::uint32_t rewards_count_before;
    std::uint32_t rewards_count_after;
    std::uint32_t respawn_count_before;
    std::uint32_t respawn_count_after;
    std::uintptr_t manager;
    std::uintptr_t selected_before;
    std::uintptr_t selected_after;
    std::uintptr_t parser_result;
    std::uintptr_t manifest_source;
    std::uint64_t fingerprint_before;
    std::uint64_t fingerprint_after;
    CampaignLoadPersistenceOutcome outcome;
    bool before_complete;
    bool after_complete;
    bool frontend_snapshot;
    bool maps_truncated;
    std::uint32_t map_count_stored;
    char campaign_name[kCampaignTextCapacity];
    char project_root[kEventTextCapacity];
    char manifest_path[kEventTextCapacity];
    char manifest_hash[65];
    char manifest_provider[kEventTextCapacity];
    BoundedCampaignMap maps[kMaximumObservedCampaignMaps];
};

struct AssetTracePackReleaseSummary {
    std::uint64_t observation_generation;
    std::uint64_t transition_generation;
    std::uint32_t epoch;
    std::uintptr_t level;
    PackCollectionState collection_state;
    PackCrossCheckOutcome cross_check;
    std::uint32_t static_count;
    std::uint32_t dynamic_eviction_count;
    std::uint32_t dynamic_release_count;
    bool collection_complete;
    bool dynamic_complete;
    bool pack_rows_truncated;
    std::uint32_t pack_count_stored;
    char map[kEventTextCapacity];
    char label_source[64];
    struct PackRow {
        std::uintptr_t pack;
        std::uint32_t collection_index;
        bool custom_project_path;
        char name[kEventTextCapacity];
    } packs[kMaxSummaryPacksPerRelease];
};

struct AssetTraceRollbackBarrierSummary {
    std::uint32_t epoch;
    std::uint64_t on_destroy_enter_sequence;
    std::uint64_t owned_unload_sequence;
    std::uint64_t owned_unload_completed_sequence;
    std::uint64_t outgoing_release_sequence;
    std::uint32_t release_enter_count;
    std::uint32_t release_exit_count;
    std::uint64_t original_return_sequence;
    std::uint64_t reset_begin_sequence;
    std::uint64_t reset_end_sequence;
    std::uint64_t first_frontend_lookup_sequence;
    bool reset_succeeded;
    bool first_lookup_after_reset;
};

/*
 * Mandatory transaction evidence is maintained independently of the verbose
 * event queue.  Atomic counters and monotonic presence flags cannot be
 * displaced by optional campaign, material, preview, or enumeration events.
 */
struct AssetTraceTransactionEvidence {
    std::uint64_t premount_decision_count;
    std::uint64_t premount_allowed_count;
    std::uint64_t allowed_token_count;
    std::uint64_t project_root_binding_count;
    std::uint64_t data_directory_binding_count;
    std::uint64_t assets_pc_binding_count;
    std::uint64_t source_removal_count;
    std::uint64_t rollback_barrier_count;
    std::uint64_t frontend_reset_count;
    std::uint64_t frontend_verification_count;
    std::uint64_t last_token_generation;
    std::uint32_t last_bound_epoch;
    std::uint32_t last_admission_result;
    std::uint32_t observed_flags;
    std::uint64_t transaction_evidence_loss;
    char first_failure_reason[kTransactionFailureReasonCapacity];
};

/* Frozen immediately after route installation.  These bits describe what
 * this process image actually installed, not whether publication is still
 * admitted while the consumer is draining or shutdown is quiescing. */
struct InstalledObserverCapabilities {
    bool watched_open;
    bool parser;
    bool campaign;
    bool pack_collection;
    bool pack_dynamic;
    bool default_levels;
    bool effective_fov;
    bool assets_pc_file;
    bool assets_pc_resource;
    bool material_observer;
    bool player_progression_observer;
    bool audio_data;
    bool audio_bank;
};

struct CurrentObserverAdmission {
    bool hook_publication;
    bool content_tracking;
    bool trace_output;
    bool quiescing;
    bool material_observer_accepting_events;
    bool player_progression_observer_accepting_events;
};

struct CampaignObservationCompleteness {
    bool observer_installed;
    bool custom_complete;
    bool frontend_complete;
    bool base_complete;
    char frontend_state[64];
};

struct AssetTraceSummary {
    InstalledObserverCapabilities installed_capabilities;
    CurrentObserverAdmission current_admission;
    std::uint64_t report_generation;
    std::uint64_t trace_records_drained_through_sequence;
    std::uint64_t ledger_snapshot_sequence;
    std::uint64_t trace_summary_sequence;
    bool report_finalized;
    bool asset_manager_hook_available;
    bool on_demand_hook_available;
    bool script_hook_available;
    bool campaign_hook_available;
    bool internal_pack_release_hook_available;
    bool ownership_ledger_complete;
    bool source_identity_complete;
    bool source_reset_evidence_complete;
    bool broad_file_ledger_complete;
    bool queue_overflow_observed;
    bool trace_output_incomplete;
    bool transitions_truncated;
    bool watched_opens_truncated;
    bool script_observations_truncated;
    bool rollback_barriers_truncated;
    bool campaign_loads_truncated;
    bool campaign_observation_unknown;
    bool custom_campaign_load_observation_complete;
    bool frontend_campaign_lifecycle_observation_complete;
    bool base_campaign_observation_complete;
    char frontend_campaign_lifecycle_state[64];
    bool pack_releases_truncated;
    bool material_reload_entered;
    bool material_local_pack_opened;
    bool material_pack_selected;
    bool material_old_pack_destructor_completed;
    bool material_reload_exited;
    bool post_supersession_assets_opened;
    bool player_template_selected;
    bool progression_base_overlay_observed;
    bool progression_custom_overlay_observed;
    bool player_variables_after_template_observed;
    std::uint32_t material_rebind_count;
    std::uint32_t material_global_fallback_count;
    std::uint32_t progression_overlay_count;
    std::uint64_t overflow_dropped_events;
    std::uint64_t critical_trace_loss;
    std::uint64_t optional_trace_loss;
    AssetTraceTransactionEvidence transaction_evidence;
    std::uint32_t critical_trace_reserved_capacity;
    std::uint32_t dropped_domain_mask;
    std::uint64_t first_drop_sequence;
    std::uint64_t last_drop_sequence;
    bool drop_sequence_unknown;
    std::uint32_t broad_file_loss_count;
    std::uint64_t broad_file_first_loss_sequence;
    std::uint64_t broad_file_last_loss_sequence;
    char broad_file_loss_reason[64];
    AssetTraceLossDomainSummary loss_domains[kTraceLossDomainCount];
    std::uint32_t transition_count;
    std::uint32_t watched_open_count;
    std::uint32_t rollback_barrier_count;
    std::uint32_t script_observation_count;
    std::uint32_t campaign_load_count;
    std::uint32_t pack_release_count;
    AssetTraceGenerationSummary transitions[kMaxSummaryTransitions];
    AssetTraceWatchedResolution watched_opens[kMaxSummaryWatchedOpens];
    AssetTraceRollbackBarrierSummary
        rollback_barriers[kMaxSummaryRollbackBarriers];
    AssetTraceScriptObservation
        script_observations[kMaxSummaryScriptObservations];
    AssetTraceCampaignLoadSummary
        campaign_loads[kMaxSummaryCampaignLoads];
    AssetTracePackReleaseSummary
        pack_releases[kMaxSummaryPackReleases];
};

struct AssetTraceHeader {
    std::uint32_t process_id;
    std::uint64_t qpc_frequency;
    std::uint64_t trace_start_qpc;
    char loader_version[64];
    char trace_start_time[64];
    char gamedll_identity[kEventTextCapacity];
    char engine_identity[kEventTextCapacity];
    char filesystem_identity[kEventTextCapacity];
    char enabled_hooks[4096];
};

void Initialize(const AssetTracePlatform* platform = nullptr) noexcept;
void Reset() noexcept;

void ClearPayload(AssetTraceEventPayload* payload,
                  AssetTraceEventType type) noexcept;
bool Publish(const AssetTraceEventPayload& payload) noexcept;
bool PublishWithSequence(const AssetTraceEventPayload& payload,
                         std::uint64_t* published_sequence) noexcept;
/* Publishes a boundary record and commits its associated hook-local state
 * before releasing the trace queue lock. The commit is always invoked exactly
 * once, including when queue contention or capacity prevents publication. */
bool PublishThenCommit(const AssetTraceEventPayload& payload,
                       AssetTraceCommit commit,
                       void* context) noexcept;
/* Runs a bounded state-classification callback and appends its prepared event
 * under the same queue lock. On contention the callback still runs, while a
 * successfully prepared event is counted as dropped so the trace is
 * explicitly incomplete rather than misleadingly reordered. */
bool PrepareThenPublish(AssetTraceEventPayload* payload,
                        AssetTracePrepare prepare,
                        void* context) noexcept;
bool Publish(AssetTraceEventType type, std::uintptr_t object,
             std::uintptr_t first, std::uintptr_t second,
             std::uint64_t value0, std::uint64_t value1,
             const char* text0, const char* text1) noexcept;
std::size_t Drain(AssetTraceEvent* output, std::size_t capacity) noexcept;
std::size_t PendingCount() noexcept;
std::uint64_t DroppedEventCount() noexcept;
std::uint64_t CriticalTraceLossCount() noexcept;
std::uint64_t OptionalTraceLossCount() noexcept;
void RecordPreMountDecision(
    bool allowed, std::uint64_t token_generation,
    std::uint32_t admission_result,
    const char* failure_reason) noexcept;
void RecordSourceBinding(
    std::uint32_t source_kind,
    std::uint64_t token_generation,
    std::uint32_t epoch,
    bool accepted,
    const char* failure_reason) noexcept;
bool SnapshotTransactionEvidence(
    AssetTraceTransactionEvidence* output) noexcept;
std::uint64_t TransactionEvidenceLossCount() noexcept;
std::size_t CriticalTraceReservedCapacity() noexcept;
bool IsCriticalEvent(AssetTraceEventType type) noexcept;

AssetTraceContextToken BeginTransitionContext() noexcept;
AssetTraceContextToken PushTransitionContext(std::uint64_t generation) noexcept;
void EndTransitionContext(const AssetTraceContextToken& token) noexcept;
std::uint64_t CurrentTransitionGeneration() noexcept;
std::uint64_t GlobalTransitionGeneration() noexcept;

AssetTraceStringCopyResult CopyReadableCString(
    char* output, std::size_t output_capacity, const char* source,
    std::size_t maximum_scan = 4096) noexcept;
AssetTraceStringCopyResult CopyRawResourcePackPath(
    char* output, std::size_t output_capacity,
    const void* resource_data_pack) noexcept;

bool NormalizeWatchedPath(const char* input, char* output,
                          std::size_t output_capacity) noexcept;
void ResetWatchList(AssetTraceWatchList* watch_list) noexcept;
bool AddWatchedPath(AssetTraceWatchList* watch_list,
                    const char* logical_path) noexcept;
bool MatchWatchedPath(const AssetTraceWatchList& watch_list,
                      const char* logical_path,
                      std::uint32_t* matched_index = nullptr,
                      char* normalized_output = nullptr,
                      std::size_t normalized_capacity = 0) noexcept;

const char* EventTypeName(AssetTraceEventType type) noexcept;
const char* EventCategoryName(AssetTraceEventType type) noexcept;
const char* SourceKindName(AssetTraceSourceKind kind) noexcept;
const char* StagingPhaseName(AssetTraceStagingPhase phase) noexcept;
const char* LabelSourceName(AssetTraceLabelSource source) noexcept;
const char* WatchedScriptOutcomeName(WatchedScriptOutcome outcome) noexcept;
WatchedScriptOutcome ClassifyWatchedScriptObservation(
    const WatchedScriptClassificationSignals& signals) noexcept;
std::uint64_t FingerprintCampaignState(
    const CampaignStateSnapshot& snapshot) noexcept;
CampaignLoadPersistenceOutcome ClassifyCampaignPersistence(
    const CampaignStateSnapshot& before,
    const CampaignStateSnapshot& after) noexcept;
const char* CampaignPersistenceOutcomeName(
    CampaignLoadPersistenceOutcome outcome) noexcept;
const char* CampaignSnapshotPhaseName(CampaignSnapshotPhase phase) noexcept;
const char* PackCollectionStateName(PackCollectionState state) noexcept;
const char* PackCrossCheckOutcomeName(PackCrossCheckOutcome outcome) noexcept;
PackCrossCheckOutcome ClassifyPackCrossCheck(
    PackCollectionState state, std::uint32_t static_count,
    std::uint32_t dynamic_eviction_count,
    std::uint32_t dynamic_release_count,
    std::uint32_t dynamic_missing_from_snapshot,
    std::uint32_t static_missing_release,
    bool dynamic_observation_complete) noexcept;
std::uint32_t EventDropDomainMask(AssetTraceEventType type) noexcept;
bool FormatEventLine(const AssetTraceEvent& event, char* output,
                     std::size_t output_capacity,
                     std::size_t* written = nullptr) noexcept;
bool FormatHeaderLine(const AssetTraceHeader& header,
                      const AssetTraceWatchList& watch_list, char* output,
                      std::size_t output_capacity,
                      std::size_t* written = nullptr) noexcept;

void ResetSummary(AssetTraceSummary* summary,
                  bool on_demand_hook_available,
                  bool script_hook_available) noexcept;
void ConfigureSummaryObservers(
    AssetTraceSummary* summary, bool campaign_hook_available,
    bool internal_pack_release_hook_available) noexcept;
void ConfigureSummaryReportState(
    AssetTraceSummary* summary,
    const InstalledObserverCapabilities& capabilities,
    const CurrentObserverAdmission& admission,
    std::uint64_t report_generation, bool report_finalized,
    std::uint64_t trace_records_drained_through_sequence,
    std::uint64_t ledger_snapshot_sequence,
    std::uint64_t trace_summary_sequence) noexcept;
void ConfigureSummaryLedgerCompleteness(
    AssetTraceSummary* summary, bool ownership_ledger_complete,
    bool source_identity_complete, bool source_reset_evidence_complete,
    std::uint32_t broad_file_loss_count,
    std::uint64_t broad_file_first_loss_sequence,
    std::uint64_t broad_file_last_loss_sequence,
    const char* broad_file_loss_reason) noexcept;
void CaptureLossDomainStats(AssetTraceSummary* summary) noexcept;
CampaignObservationCompleteness EvaluateCampaignCompleteness(
    const AssetTraceSummary& summary) noexcept;
void ConsumeSummaryEvent(AssetTraceSummary* summary,
                         const AssetTraceEvent& event) noexcept;
AssetTraceTriState CleanupBeforeDestinationLookup(
    const AssetTraceGenerationSummary& summary) noexcept;
bool FormatSummary(const AssetTraceSummary& summary, char* output,
                   std::size_t output_capacity,
                   std::size_t* written = nullptr) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::trace

#endif
