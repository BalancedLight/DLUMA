#ifndef DLUMA_FIXES_PLAYER_GAMEPLAY_PARAMS_RESTART_ADAPTER_H
#define DLUMA_FIXES_PLAYER_GAMEPLAY_PARAMS_RESTART_ADAPTER_H

#include "ContentDomainRestartRegistry.h"
#include "PlayerGameplayParamsWindowsSnapshot.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart {

constexpr std::size_t kPlayerStateInventoryCapacity = 64;
constexpr std::size_t kPlayerLogicalTypeCount = 8;
constexpr std::size_t kRequiredPlayerPropertyCount = 15;

constexpr std::uint32_t kLowHealthEffectThresholdPropertyId = 103;
constexpr std::uint32_t kStaminaWarningLevelPropertyId = 209;
constexpr std::uint32_t kMaxStaminaPropertyId = 496;
constexpr std::uint32_t kMoveSprintSpeedPropertyId = 625;
constexpr std::uint32_t kMaxHealthPropertyId = 702;
constexpr std::uint32_t kBaseMaxHealthPropertyId = 706;
constexpr std::uint32_t kHealthCriticalLevelPropertyId = 707;
constexpr std::uint32_t kHealthRegenerationDelayPropertyId = 713;
constexpr std::uint32_t kHealthRegenerationMaxLevelPropertyId = 714;
constexpr std::uint32_t kHealthRegenerationTimeToMaxPropertyId = 715;
constexpr std::uint32_t kCameraDefaultFovPropertyId = 779;
constexpr std::uint32_t kKickStaminaUsagePropertyId = 823;
constexpr std::uint32_t kCameraCarFovPropertyId = 780;
constexpr std::uint32_t kFovSprintModifierPropertyId = 817;
constexpr std::uint32_t kFovAmortizerModifierPropertyId = 818;

enum class PlayerStateOwnerKind : std::uint8_t {
    Unknown,
    LocalProcessGlobal,
    CharacterManagerGlobal,
    PlayerDi,
    TutorialPlayerDi,
    OtherRttiObserved,
};

enum class PlayerRestartFailure : std::uint8_t {
    None,
    InvalidArgument,
    OperationsIncomplete,
    ExactRouteUnvalidated,
    MutationProductionDisabled,
    CoverageNotStarted,
    CoverageStartedLate,
    InventoryLoss,
    InventoryOverflow,
    InventoryBusy,
    InventoryEmpty,
    InventoryChanged,
    StateUnclassified,
    GlobalOwnerMissing,
    GlobalOwnerMismatch,
    LiveGameplayConsumer,
    SessionStillActive,
    ShutdownOrQuiescing,
    EnvironmentUnreadable,
    GameplayContainerUnavailable,
    GameDiUnavailable,
    BaselineMissing,
    AbsentBaselineUnproven,
    TouchedResetUnavailable,
    BaselineStateMissing,
    StateSnapshotFailed,
    StateSnapshotIncomplete,
    RequiredPropertyMissing,
    RequiredPropertyDuplicate,
    ProviderIncomplete,
    SelectorMismatch,
    TransactionAlreadyActive,
    TransactionAlreadyCompleted,
    TransactionMismatch,
    ThreadMismatch,
    MutationAlreadyStarted,
    CacheGenerationDidNotAdvance,
    InitializeCountMismatch,
    CacheMembershipMismatch,
    ProviderMismatch,
    PreservationInvariantMismatch,
    PropertyMismatch,
    VerificationFailed,
    SemanticRowInvalid,
    SemanticPropertyInvalid,
    CacheMembershipConflict,
    RestartCacheMembershipUnproven,
    RestartProviderUnproven,
    RestartInventoryOwnerUnproven,
    RestartEquipmentOwnerUnproven,
    RestartQuestOwnerUnproven,
};

enum class PlayerStartupBaselineKind : std::uint8_t {
    Unavailable,
    Absent,
    Snapshot,
};

enum class PlayerGameplayParamsTouchAssessment : std::uint8_t {
    UntouchedAbsent,
    UntouchedSnapshot,
    TouchedPersistentOwner,
    TouchedDefinitionDelta,
    Incomplete,
    /* Compatibility names for callers that only distinguish touched from
     * untouched. New production integration uses the exact values above. */
    Untouched = UntouchedSnapshot,
    Touched = TouchedDefinitionDelta,
};

enum class PlayerEnvironmentReadPurpose : std::uint8_t {
    StartupSemanticBaseline,
    PostDestroySemanticAssessment,
    RestartMutationPreflight,
    RestartMutationVerify,
};

/* Worker IDs are phase-local evidence.  The serialized OnFrame route may
 * migrate between startup B0 and the later C10/SessionDI teardown chain. */
enum class PlayerThreadAuthorityKind : std::uint8_t {
    Unspecified,
    StartupB0CaptureOwner,
    C10SessionOwner,
    C10PostReturnOwner,
    RestartTransactionOwner,
};

struct PlayerThreadAuthorityEvidence {
    PlayerThreadAuthorityKind kind;
    std::uint32_t expected_thread_id;
    std::uint32_t historical_b0_thread_id;
    std::uint32_t c08_callback_thread_id;
    std::uint32_t current_c10_thread_id;
    std::uint32_t current_session_scope_thread_id;
    std::uint32_t post_c10_observed_thread_id;
    std::uint64_t pending_post_c10_token_generation;
    bool exact_scope_active;
};

/* The engine-facing callback writes this raw result before returning.  It is
 * diagnostic evidence only: adapter validation remains authoritative and a
 * callback rejection never becomes permission to infer a more specific
 * failure than the callback actually recorded. */
enum class PlayerEnvironmentCallbackFailure : std::uint8_t {
    None,
    InvalidArgument,
    ContextUnavailable,
    WrongThread,
    UnsupportedImage,
    ShutdownOrQuiescing,
    WindowsEnvironmentSnapshotRejected,
    WindowsEnvironmentSnapshotIncomplete,
    InventorySnapshotRejected,
    InventoryObservationLoss,
    LocalRepresentationUnreadable,
    GlobalOwnerSetIncomplete,
    CacheSnapshotRejected,
    CacheSnapshotChanged,
    CacheOwnerChanged,
    InventoryRevisionChanged,
};

enum class PlayerPostDestroyExactFailure : std::uint8_t {
    None,
    InvalidArgument,
    BaselineMissing,
    OperationsIncomplete,
    InventoryCaptureFailed,
    EnvironmentCallbackRejected,
    UnsupportedImage,
    ShutdownOrQuiescing,
    GameplayContainerUnreadable,
    GameDiUnreadable,
    GlobalOwnerSlotsUnreadable,
    InventoryScanIncomplete,
    CacheFingerprintUnavailable,
    ThreadMismatch,
    ActiveSessionStillPresent,
    ActiveLevelStillPresent,
    LocalPlayerStillPresent,
    LiveGameplayConsumerStillPresent,
    EnvironmentChangedBetweenPasses,
    InventoryChangedBetweenPasses,
    OwnerSetChangedBetweenPasses,
    BaselineStateMissing,
    StateSnapshotCallbackRejected,
    StateSnapshotSemanticIncomplete,
    StateChangedBetweenPasses,
};

enum class PlayerStateSnapshotFailureStage : std::uint8_t {
    None,
    CallbackRejected,
    BodyUnreadable,
    PlayerVariablesUnavailable,
    RowTableUnreadable,
    SelectorSnapshotIncomplete,
    PreservationFingerprintIncomplete,
    AppearanceSnapshotIncomplete,
    RowUnreadable,
    RowDefinitionUnreadable,
    RowDefinitionTraversalIncomplete,
    RowCacheMembershipMismatch,
    RowProviderIncomplete,
    RequiredPropertyMissing,
    RequiredPropertyDuplicate,
    PropertyProviderIncomplete,
    CacheGenerationMismatch,
    StateChangedBetweenPasses,
    SemanticRowInvalid,
    SemanticPropertyInvalid,
    CacheMembershipConflict,
    RestartCacheMembershipUnproven,
    RestartProviderUnproven,
    RestartInventoryOwnerUnproven,
    RestartEquipmentOwnerUnproven,
    RestartQuestOwnerUnproven,
};

enum class PlayerCacheMembershipEvidence : std::uint8_t {
    Unobserved,
    ProvenMember,
    ProvenNonMember,
    Conflict,
};

constexpr std::uint64_t kPlayerSemanticReasonRowInvalid = 1ULL << 48;
constexpr std::uint64_t kPlayerSemanticReasonPropertyInvalid = 1ULL << 49;
constexpr std::uint64_t kPlayerSemanticReasonCacheMembershipConflict =
    1ULL << 50;
constexpr std::uint64_t
    kPlayerRestartProvenanceReasonCacheMembershipUnproven = 1ULL << 40;
constexpr std::uint64_t kPlayerRestartProvenanceReasonProviderUnproven =
    WindowsSnapshotReasonProviderIdentityUnresolved;
constexpr std::uint64_t
    kPlayerRestartProvenanceReasonInventoryOwnerUnproven =
        WindowsSnapshotReasonInventoryOwnerUnresolved;
constexpr std::uint64_t
    kPlayerRestartProvenanceReasonEquipmentOwnerUnproven =
        WindowsSnapshotReasonEquipmentOwnerUnresolved;
constexpr std::uint64_t kPlayerRestartProvenanceReasonQuestOwnerUnproven =
    WindowsSnapshotReasonQuestOwnerUnresolved;
constexpr std::uint64_t kPlayerWindowsRestartProvenanceReasonMask =
    kPlayerRestartProvenanceReasonProviderUnproven |
    kPlayerRestartProvenanceReasonInventoryOwnerUnproven |
    kPlayerRestartProvenanceReasonEquipmentOwnerUnproven |
    kPlayerRestartProvenanceReasonQuestOwnerUnproven;

/* Inventory coverage is authoritative only when the loader has proved that
 * its exact ctor/dtor routes were installed before the original game-script
 * initializer could create any PlayerState.  Keep that startup proof separate
 * from the later restart preflight: a null global pointer by itself is not a
 * complete allocation-history claim. */
enum class PlayerInventoryBootstrapFailure : std::uint8_t {
    None,
    InvalidArgument,
    UnsupportedImage,
    NotBeforeOriginalInitializer,
    CreatorSetUnvalidated,
    ConstructorRouteUnavailable,
    CompleteDestructorRouteUnavailable,
    GlobalSlotsUnreadable,
    ExistingGlobalState,
    HookActiveDuringBootstrap,
    PrecoverageObservationLoss,
    CoverageStartRejected,
};

struct PlayerInventoryBootstrapEvidence {
    std::uintptr_t before_local_process_global_state;
    std::uintptr_t before_character_manager_global_state;
    std::uintptr_t after_local_process_global_state;
    std::uintptr_t after_character_manager_global_state;
    std::uint32_t constructor_active_calls;
    std::uint32_t complete_destructor_active_calls;
    bool supported_image;
    bool before_original_game_script_initializer;
    bool closed_creator_set_validated;
    bool constructor_route_installed;
    bool complete_destructor_route_installed;
    bool before_global_slots_readable;
    bool after_global_slots_readable;
};

struct PlayerContentProviderIdentity {
    std::uintptr_t source;
    std::uint64_t normalized_path_fingerprint;
    std::uint64_t source_registry_generation;
    std::uint32_t content_epoch;
    std::uint32_t source_record_index;
    std::uint32_t source_inventory_ordinal;
    bool observed;
    bool exact_source;
    bool base_provider;
    /* Generation zero is a valid observed registry generation. */
    bool source_registry_generation_observed;
};

struct PlayerDefinitionRowSnapshot {
    std::uintptr_t definition_vector;
    std::uint64_t definition_fingerprint;
    std::uint64_t earned_experience;
    std::uint64_t trailing_qword;
    std::uint64_t cache_generation;
    std::uint32_t definition_count;
    std::uint32_t unknown_100;
    std::uint16_t current_level;
    std::uint16_t maximum_level;
    PlayerContentProviderIdentity provider;
    PlayerCacheMembershipEvidence cache_membership_evidence;
    std::uint64_t membership_observation_sequence;
    bool row_readable;
    bool definition_readable;
    bool cache_generation_observed;
    bool cache_observer_available;
    bool cache_inventory_complete;
    /* Compatibility projection. It is true only for ProvenMember and is
     * never interpreted as evidence when false. */
    bool member_of_live_cache;
    bool preset_absent;
    bool semantic_complete;
    /* Full ordered records stay in the bounded observation ledger. Keeping
     * only their canonical digest avoids multiplying large source identities
     * through every baseline/diagnostic/transaction snapshot. */
    std::uint8_t input_graph_sha256[32];
    std::uint32_t input_count;
    bool input_graph_complete;
    bool input_graph_all_baseline;
};

struct PlayerPropertySnapshot {
    std::uint32_t property_id;
    std::uint32_t value_bits;
    std::uint64_t cache_generation;
    PlayerContentProviderIdentity provider;
    bool available;
};

struct PlayerStateRestartSnapshot {
    std::uintptr_t state;
    std::uintptr_t owner_slot;
    std::uint64_t state_generation;
    std::uintptr_t player_variables;
    std::uint64_t progression_selector_fingerprint;
    std::uint64_t class_selector_fingerprint;
    std::uint64_t skill_membership_fingerprint;
    std::uint64_t regular_buff_fingerprint;
    std::uint64_t timed_buff_fingerprint;
    std::uint64_t inventory_fingerprint;
    std::uint64_t equipment_fingerprint;
    std::uint64_t quest_fingerprint;
    std::uint64_t appearance_fingerprint;
    /* Actual user values, separate from replaceable derived definition/perk
     * state. Zero remains unavailable rather than an invented empty value. */
    std::uint64_t intrinsic_user_fingerprints[9];
    std::uint64_t cached_context_fingerprints[9];
    std::uint32_t cached_context_available_mask;
    std::uint32_t unqualified_owned_user_graphs;
    bool intrinsic_user_fields_complete;
    bool cached_context_sections_complete;
    bool owned_user_graph_complete;
    PlayerContentProviderIdentity appearance_provider;
    PlayerDefinitionRowSnapshot rows[kPlayerLogicalTypeCount];
    PlayerPropertySnapshot properties[kRequiredPlayerPropertyCount];
    std::uint64_t memory_layout_reason;
    std::uint64_t provider_reason;
    std::uint64_t semantic_reason_mask;
    std::uint64_t restart_provenance_reason_mask;
    std::uint64_t cache_generation;
    std::uint32_t snapshot_attempt_count;
    std::uint32_t failed_row_index;
    std::uint32_t failed_property_id;
    PlayerStateOwnerKind owner_kind;
    PlayerStateSnapshotFailureStage failure_stage;
    PlayerStateSnapshotFailureStage restart_failure_stage;
    bool body_readable;
    bool row_table_readable;
    bool player_variables_readable;
    bool selectors_complete;
    bool preservation_invariants_complete;
    bool appearance_complete;
    bool player_semantic_snapshot_complete;
    bool player_restart_snapshot_complete;
    /* Compatibility: this continues to mean semantic capture completeness.
     * Mutation paths must require player_restart_snapshot_complete. */
    bool snapshot_complete;
    PlayerGameplayParamsWindowsRowSnapshot
        raw_rows[kPlayerLogicalTypeCount];
};

struct PlayerRestartEnvironment {
    std::uintptr_t gameplay_params_container;
    std::uintptr_t game_di;
    std::uintptr_t local_player_di;
    std::uintptr_t active_session;
    std::uintptr_t active_level;
    std::uintptr_t local_process_global_state;
    std::uintptr_t character_manager_global_state;
    std::uint64_t cache_generation;
    /* Stable semantic fingerprint of the complete gameplay-parameter cache
     * shape.  Unlike cache_generation, this proves an absent startup
     * baseline returned to the same definition content. */
    std::uint64_t gameplay_cache_fingerprint;
    /* Raw process-global Windows environment evidence captured by the
     * production callback.  It remains useful when the callback rejects. */
    std::uint64_t raw_environment_reason_mask;
    std::uint32_t current_thread_id;
    PlayerEnvironmentCallbackFailure callback_failure;
    bool supported_image;
    bool gameplay_params_container_readable;
    bool game_di_readable;
    bool global_owner_slots_readable;
    bool inventory_scan_complete;
    bool live_gameplay_consumer;
    bool shutdown_requested;
    bool quiescing;
};

struct PlayerStateInventoryRecord;

using ReadPlayerRestartEnvironmentFn = bool (*)(
    void* user, PlayerEnvironmentReadPurpose purpose,
    PlayerRestartEnvironment* output) noexcept;
using SnapshotPlayerStateForRestartFn = bool (*)(
    void* user, std::uintptr_t state, std::uint64_t state_generation,
    PlayerStateRestartSnapshot* output) noexcept;
using ResetGameplayParamsCacheFn = void (*)(
    void* user, std::uintptr_t gameplay_params_container) noexcept;
using InitializePlayerStateVariablesFn = void (*)(
    void* user, std::uintptr_t player_state) noexcept;
using ObservePostDestroySemanticPassFn = void (*)(
    void* user, std::uint32_t pass,
    const PlayerRestartEnvironment* environment,
    const PlayerStateInventoryRecord* owners,
    const PlayerStateRestartSnapshot* states,
    std::uint32_t owner_count,
    std::uint64_t inventory_revision) noexcept;

/* Baseline capture is read-only and must be independently supportable.  It
 * must never become contingent on the availability of destructive routes. */
struct PlayerGameplayParamsSnapshotOperations {
    ReadPlayerRestartEnvironmentFn read_environment;
    SnapshotPlayerStateForRestartFn snapshot_state;
    bool environment_layout_validated;
    bool state_snapshot_layout_validated;
    /* Production may opt out of the optional progression-trace metadata
     * dependency while retaining the direct Windows state/row/property
     * snapshot contract.  In this mode only a completely zero, wholly
     * unobserved provider identity is accepted as unavailable evidence; a
     * partial identity remains invalid and no provider is synthesized.  A
     * zero cache generation is likewise accepted only as absent metadata.
     * Zero initialization keeps native harnesses and every existing caller
     * on the strict observed-metadata policy. */
    bool allow_unobserved_progression_trace_metadata;
    /* Optional read-only publication tap. It observes the exact immutable
     * pass data already captured by assessment and must never call a game
     * function or mutate an engine object. */
    ObservePostDestroySemanticPassFn observe_post_destroy_semantic_pass;
};

/* Mutation remains a separate, explicit capability.  Production code keeps
 * production_enabled false until the complete Player reset transaction has
 * independent runtime authorization.  Native fake harnesses may opt in. */
struct PlayerGameplayParamsMutationOperations {
    ResetGameplayParamsCacheFn reset_global_cache;
    InitializePlayerStateVariablesFn initialize_variables;
    bool global_cache_reset_route_validated;
    bool initialize_variables_route_validated;
    bool production_enabled;
};

struct PlayerStateInventoryRecord {
    std::uintptr_t pointer;
    std::uintptr_t owner_object;
    std::uint64_t generation;
    std::uint64_t creation_sequence;
    std::uint64_t destruction_sequence;
    PlayerStateOwnerKind owner_kind;
    bool live;
};

/* Read-only bridge used by the production absent-baseline owner.  This is
 * captured from the same ctor/dtor inventory and environment callback as the
 * registry-owned Player adapter, so the two models cannot disagree about
 * live owners, owner generations, cache identity, or observation loss. */
struct PlayerGameplayParamsAbsentEvidence {
    PlayerRestartEnvironment environment;
    PlayerStateInventoryRecord live_states[kPlayerStateInventoryCapacity];
    std::uint32_t live_state_count;
    std::uint64_t inventory_revision;
    bool complete;
};

struct PlayerRestartPlannedState {
    std::uintptr_t pointer;
    std::uint64_t generation;
    PlayerStateOwnerKind owner_kind;
    PlayerStateRestartSnapshot before;
    PlayerStateRestartSnapshot after_reset;
    std::uint32_t initialize_calls;
    bool after_reset_captured;
};

struct PlayerB0StateCaptureDiagnostics {
    std::uintptr_t state;
    std::uintptr_t owner_slot;
    std::uint64_t generation;
    std::uint64_t memory_layout_reason;
    std::uint64_t provider_reason;
    std::uint64_t semantic_reason_mask;
    std::uint64_t restart_provenance_reason_mask;
    std::uint64_t cache_generation;
    std::uint32_t snapshot_attempt_count;
    std::uint32_t failed_row_index;
    std::uint32_t failed_property_id;
    PlayerStateOwnerKind owner_kind;
    PlayerStateSnapshotFailureStage failure_stage;
    PlayerStateSnapshotFailureStage restart_failure_stage;
    PlayerGameplayParamsWindowsRowSnapshot
        pass1_rows[kPlayerLogicalTypeCount];
    PlayerGameplayParamsWindowsRowSnapshot
        pass2_rows[kPlayerLogicalTypeCount];
    std::uint32_t pass1_first_failed_row_index;
    std::uint32_t pass2_first_failed_row_index;
    std::uint64_t pass1_first_failed_row_reason;
    std::uint64_t pass2_first_failed_row_reason;
    PlayerDefinitionRowSnapshot pass1_semantic_rows[kPlayerLogicalTypeCount];
    PlayerDefinitionRowSnapshot pass2_semantic_rows[kPlayerLogicalTypeCount];
    std::uint64_t pass1_semantic_reason_mask;
    std::uint64_t pass2_semantic_reason_mask;
    std::uint64_t pass1_restart_provenance_reason_mask;
    std::uint64_t pass2_restart_provenance_reason_mask;
    PlayerStateSnapshotFailureStage pass1_failure_stage;
    PlayerStateSnapshotFailureStage pass2_failure_stage;
    PlayerStateSnapshotFailureStage pass1_restart_failure_stage;
    PlayerStateSnapshotFailureStage pass2_restart_failure_stage;
    bool pass1_semantic_snapshot_complete;
    bool pass2_semantic_snapshot_complete;
    bool pass1_restart_snapshot_complete;
    bool pass2_restart_snapshot_complete;
    bool snapshot_complete;
};

struct PlayerB0CaptureDiagnostics {
    std::uint64_t bootstrap_sample_sequence;
    std::uint64_t pass1_sequence;
    std::uint64_t pass2_sequence;
    std::uint64_t pass1_inventory_revision;
    std::uint64_t pass2_inventory_revision;
    std::uint64_t pass1_cache_fingerprint;
    std::uint64_t pass2_cache_fingerprint;
    std::uint32_t bootstrap_sample_owner_count;
    std::uint32_t pass1_owner_count;
    std::uint32_t pass2_owner_count;
    std::uint32_t state_snapshot_count;
    PlayerStartupBaselineKind selected_kind;
    bool owner_set_stable_across_passes;
    bool player_semantic_snapshot_complete;
    bool player_restart_snapshot_complete;
    PlayerB0StateCaptureDiagnostics
        states[kPlayerStateInventoryCapacity];
};

struct PlayerPostDestroyEnvironmentCaptureDiagnostics {
    PlayerRestartEnvironment environment;
    std::uint64_t inventory_revision;
    std::uint32_t owner_count;
    std::uint32_t environment_observed_thread;
    std::uint32_t environment_expected_thread;
    PlayerRestartFailure compatibility_failure;
    PlayerPostDestroyExactFailure exact_failure;
    bool environment_callback_attempted;
    bool environment_callback_returned;
    bool inventory_capture_complete;
    bool thread_match;
};

struct PlayerPostDestroyRowCaptureDiagnostics {
    std::uint64_t raw_failure_reason;
    std::uint64_t semantic_definition_fingerprint;
    std::uint32_t semantic_definition_count;
    std::uint32_t definition_traversal_failed_property_id;
    PlayerGameplayParamsWindowsRowHeaderReadResult
        header_read_result;
    PlayerGameplayParamsWindowsRowHeaderValidationResult
        header_validation_result;
    PlayerDefinitionTraversalFailure
        definition_traversal_failure;
    bool raw_semantic_complete;
    bool semantic_complete;
};

struct PlayerPostDestroyStateCaptureDiagnostics {
    std::uintptr_t state;
    std::uintptr_t owner_slot;
    std::uint64_t generation;
    PlayerStateOwnerKind owner_kind;
    PlayerPostDestroyRowCaptureDiagnostics
        pass1_rows[kPlayerLogicalTypeCount];
    PlayerPostDestroyRowCaptureDiagnostics
        pass2_rows[kPlayerLogicalTypeCount];
    std::uint64_t pass1_semantic_reason_mask;
    std::uint64_t pass2_semantic_reason_mask;
    std::uint64_t pass1_restart_provenance_reason_mask;
    std::uint64_t pass2_restart_provenance_reason_mask;
    std::uint64_t pass1_cache_generation;
    std::uint64_t pass2_cache_generation;
    std::uint32_t pass1_failed_row_index;
    std::uint32_t pass2_failed_row_index;
    std::uint32_t pass1_failed_property_id;
    std::uint32_t pass2_failed_property_id;
    PlayerStateSnapshotFailureStage pass1_failure_stage;
    PlayerStateSnapshotFailureStage pass2_failure_stage;
    PlayerStateSnapshotFailureStage pass1_restart_failure_stage;
    PlayerStateSnapshotFailureStage pass2_restart_failure_stage;
    bool pass1_callback_attempted;
    bool pass1_callback_returned;
    bool pass2_callback_attempted;
    bool pass2_callback_returned;
    bool pass1_semantic_snapshot_complete;
    bool pass2_semantic_snapshot_complete;
    bool pass1_restart_snapshot_complete;
    bool pass2_restart_snapshot_complete;
};

struct PlayerPostDestroyCaptureDiagnostics {
    std::uint64_t assessment_attempt_count;
    std::uint64_t assessment_sequence;
    std::uint64_t baseline_generation;
    std::uint32_t content_epoch;
    std::uint32_t owner_thread_id;
    std::uint32_t historical_b0_thread_id;
    std::uint32_t c08_callback_thread_id;
    std::uint32_t current_c10_thread_id;
    std::uint32_t current_session_scope_thread_id;
    std::uint32_t post_c10_observed_thread_id;
    std::uint64_t pending_post_c10_token_generation;
    PlayerThreadAuthorityKind thread_authority_kind;
    PlayerStartupBaselineKind baseline_kind;
    PlayerPostDestroyEnvironmentCaptureDiagnostics pass1;
    PlayerPostDestroyEnvironmentCaptureDiagnostics pass2;
    PlayerPostDestroyStateCaptureDiagnostics
        states[kPlayerStateInventoryCapacity];
    std::uint32_t state_count;
    std::uint32_t first_failed_owner_index;
    PlayerStateOwnerKind first_failed_owner_kind;
    std::uintptr_t first_failed_state;
    std::uint64_t first_failed_state_generation;
    PlayerStateSnapshotFailureStage first_failed_state_stage;
    std::uint32_t first_failed_row;
    std::uint32_t first_failed_property_id;
    std::uint64_t first_failed_semantic_reason_mask;
    PlayerGameplayParamsTouchAssessment final_assessment;
    PlayerRestartFailure final_failure;
    PlayerPostDestroyExactFailure final_exact_failure;
    bool environment_pair_stable;
    bool inventory_pair_stable;
    bool owner_set_matches_baseline_pass1;
    bool owner_set_matches_baseline_pass2;
    bool semantic_state_pair_stable;
};

struct PlayerGameplayParamsRestartAdapter {
    volatile LONG inventory_lock;
    volatile LONG phase_lock;
    volatile LONG inventory_loss_latched;
    volatile LONG inventory_overflow_latched;
    volatile LONG mutation_in_progress;
    PlayerGameplayParamsSnapshotOperations snapshot_operations;
    PlayerGameplayParamsMutationOperations mutation_operations;
    void* operations_user;
    PlayerStateInventoryRecord inventory[kPlayerStateInventoryCapacity];
    PlayerStateRestartSnapshot baseline_states[kPlayerStateInventoryCapacity];
    PlayerRestartPlannedState plan[kPlayerStateInventoryCapacity];
    PlayerB0CaptureDiagnostics b0_diagnostics;
    PlayerPostDestroyCaptureDiagnostics post_destroy_diagnostics;
    std::uint32_t inventory_count;
    std::uint32_t baseline_state_count;
    std::uint32_t plan_count;
    std::uint64_t next_state_generation;
    std::uint64_t observation_sequence;
    std::uint64_t inventory_revision;
    std::uint64_t baseline_inventory_revision;
    std::uint64_t baseline_generation;
    std::uint64_t baseline_gameplay_cache_fingerprint;
    std::uint64_t transaction_generation;
    std::uint64_t transaction_baseline_generation;
    std::uint64_t planned_inventory_revision;
    std::uint64_t before_cache_generation;
    std::uint64_t after_cache_generation;
    std::uint64_t completed_transaction_generation;
    std::uintptr_t planned_gameplay_params_container;
    std::uintptr_t planned_game_di;
    std::uintptr_t planned_local_process_global_state;
    std::uintptr_t planned_character_manager_global_state;
    std::uint32_t total_global_reset_calls;
    std::uint32_t total_initialize_calls;
    std::uint32_t transaction_global_reset_calls;
    std::uint32_t transaction_initialize_calls;
    std::uint32_t owner_thread_id;
    std::uint32_t baseline_owner_thread_id;
    std::uint32_t transaction_content_epoch;
    PlayerRestartFailure last_failure;
    PlayerInventoryBootstrapFailure bootstrap_failure;
    PlayerStartupBaselineKind baseline_kind;
    bool coverage_started;
    bool coverage_started_before_first_allocation;
    bool constructor_route_validated;
    bool complete_destructor_route_validated;
    bool baseline_captured;
    bool transaction_active;
    bool preflight_complete;
    bool quiesce_complete;
    bool mutation_started;
    bool reset_postconditions_complete;
    bool runtime_failure_latched;
    bool verified;
};

void InitializePlayerGameplayParamsRestartAdapter(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerGameplayParamsSnapshotOperations* snapshot_operations,
    const PlayerGameplayParamsMutationOperations* mutation_operations,
    void* operations_user) noexcept;

bool BeginPlayerStateInventoryCoverage(
    PlayerGameplayParamsRestartAdapter* adapter,
    bool started_before_first_allocation,
    bool constructor_route_validated,
    bool complete_destructor_route_validated) noexcept;

bool BeginPlayerStateInventoryCoverageFromBootstrap(
    PlayerGameplayParamsRestartAdapter* adapter,
    const PlayerInventoryBootstrapEvidence* evidence) noexcept;

std::uint64_t ObservePlayerStateConstructed(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state) noexcept;
bool ObservePlayerStateDestroyed(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state,
    std::uint64_t* destroyed_generation) noexcept;
bool ClassifyPlayerStateOwner(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state, std::uint64_t generation,
    PlayerStateOwnerKind owner_kind,
    std::uintptr_t owner_object) noexcept;
std::uint64_t FindLivePlayerStateGeneration(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uintptr_t player_state) noexcept;
void MarkPlayerStateInventoryLoss(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept;
void RecordPlayerB0BootstrapSample(
    PlayerGameplayParamsRestartAdapter* adapter,
    std::uint64_t sequence,
    std::uint32_t owner_count) noexcept;
/* Discards only a captured read-only baseline when its external publication
 * failed.  This is refused once any restart transaction, preflight, or
 * mutation is active, and it never reads or mutates engine-owned state. */
bool AbandonPlayerGameplayParamsBaseline(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept;
/* Revalidates the exact inventory revision/loss latches recorded at the
 * baseline commit while holding the inventory lock. */
bool PlayerGameplayParamsBaselineEvidenceStillCurrent(
    PlayerGameplayParamsRestartAdapter* adapter) noexcept;
bool CapturePlayerGameplayParamsAbsentEvidence(
    PlayerGameplayParamsRestartAdapter* adapter,
    PlayerGameplayParamsAbsentEvidence* output) noexcept;
/* The ContentDomainAdapter cancellation callback delegates here, so
 * CancelPreparedRegistryTransaction abandons the paired unmutated POD plan.
 * This bookkeeping path intentionally performs no engine reads. */
bool CancelPreparedPlayerGameplayParamsRestart(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context) noexcept;

DomainResult CapturePlayerGameplayParamsBaseline(
    void* user, const RestartContext* context) noexcept;
PlayerGameplayParamsTouchAssessment AssessPlayerGameplayParamsTouch(
    PlayerGameplayParamsRestartAdapter* adapter,
    const RestartContext* context,
    const PlayerThreadAuthorityEvidence* authority = nullptr) noexcept;
DomainResult PreflightPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept;
DomainResult QuiescePlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept;
DomainResult CancelQuiescedPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept;
DomainResult ResetPlayerGameplayParams(
    void* user, const RestartContext* context) noexcept;
DomainResult ReloadPlayerGameplayParams(
    void* user, const RestartContext* context) noexcept;
DomainResult VerifyPlayerGameplayParamsRestart(
    void* user, const RestartContext* context) noexcept;

ContentDomainAdapter MakePlayerGameplayParamsContentDomainAdapter(
    PlayerGameplayParamsRestartAdapter* adapter,
    ContentDomainMask dependency_mask, bool mandatory) noexcept;

const char* PlayerRestartFailureName(PlayerRestartFailure failure) noexcept;
const char* PlayerStartupBaselineKindName(
    PlayerStartupBaselineKind kind) noexcept;
const char* PlayerGameplayParamsTouchAssessmentName(
    PlayerGameplayParamsTouchAssessment assessment) noexcept;
const char* PlayerEnvironmentReadPurposeName(
    PlayerEnvironmentReadPurpose purpose) noexcept;
const char* PlayerThreadAuthorityKindName(
    PlayerThreadAuthorityKind kind) noexcept;
const char* PlayerEnvironmentCallbackFailureName(
    PlayerEnvironmentCallbackFailure failure) noexcept;
const char* PlayerPostDestroyExactFailureName(
    PlayerPostDestroyExactFailure failure) noexcept;
const char* PlayerInventoryBootstrapFailureName(
    PlayerInventoryBootstrapFailure failure) noexcept;
const char* PlayerStateSnapshotFailureStageName(
    PlayerStateSnapshotFailureStage stage) noexcept;
const char* PlayerCacheMembershipEvidenceName(
    PlayerCacheMembershipEvidence evidence) noexcept;
const char* PlayerStateOwnerKindName(
    PlayerStateOwnerKind kind) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart

#endif
