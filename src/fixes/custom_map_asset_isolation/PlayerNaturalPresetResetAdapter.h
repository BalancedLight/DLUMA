#ifndef DLUMA_PLAYER_NATURAL_PRESET_RESET_ADAPTER_H
#define DLUMA_PLAYER_NATURAL_PRESET_RESET_ADAPTER_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::
    player_natural_reset {

constexpr std::size_t kPlayerPresetRowCount = 8U;
constexpr std::size_t kSourceRemovalCount = 3U;

enum class Phase : std::uint8_t {
    Unavailable,
    Validated,
    SnapshotReady,
    PlanPrepared,
    ExecutionAuthorized,
    Executed,
    Verified,
    Failed,
};

enum class Failure : std::uint8_t {
    None,
    InvalidArgument,
    UnsupportedImage,
    TargetUnvalidated,
    ContainerRemoveTargetUnvalidated,
    FindTargetUnvalidated,
    OwnerSlotMismatch,
    OwnerGenerationMismatch,
    CharacterOwnerMismatch,
    TokenConflict,
    SessionActive,
    LevelActive,
    LocalPlayerDiActive,
    LiveGameplayConsumer,
    WrongThread,
    OwnerSetUnstable,
    InventoryRevisionChanged,
    PreservationSnapshotIncomplete,
    B0RowsNotAbsent,
    ChangedMaskIncomplete,
    CharacterOwnerChanged,
    TimelineLoss,
    CacheObserverLoss,
    ShutdownConflict,
    LockReentrancyUnproven,
    PlanNotPrepared,
    AudioPlanUnprepared,
    TransactionNotAtomic,
    PlanIdentityChanged,
    ExecutionNotAuthorized,
    InvocationFailed,
    VerificationFailed,
    ClearObserved,
    RepresentationRecreated,
    PlayerDiCreated,
};

enum class RowKind : std::uint8_t {
    Unknown,
    AbsentPresetRow,
    PresetRow,
};

enum class SourceKind : std::uint8_t {
    AssetsPc,
    DataDirectory,
    ProjectRoot,
};

struct TargetIdentity {
    std::uintptr_t address;
    std::uint32_t rva;
    std::uint32_t expected_rva;
    bool full_image_hash_valid;
    bool timestamp_and_size_valid;
    bool prologue_valid;
};

struct FingerprintBlock {
    std::uint64_t fingerprint;
    std::uint64_t count;
    bool complete;
};

struct RowSnapshot {
    std::uintptr_t preset;
    std::uint64_t definition_fingerprint;
    std::uint32_t definition_count;
    RowKind kind;
    bool complete;
};

struct PreservationSnapshot {
    std::uintptr_t local_owner_slot;
    std::uintptr_t local_owner_pointer;
    std::uint64_t local_owner_generation;
    std::uintptr_t character_owner_slot;
    std::uintptr_t character_owner_pointer;
    std::uint64_t character_owner_generation;
    FingerprintBlock inventory_tree;
    FingerprintBlock inventory_item_vector;
    FingerprintBlock equipped_items;
    FingerprintBlock quick_slots;
    FingerprintBlock money_and_stash;
    FingerprintBlock skills_xp_levels_legend;
    FingerprintBlock quest_progression_flags;
    FingerprintBlock appearances_and_perks;
    FingerprintBlock collectibles_and_rewards;
    FingerprintBlock buffs_and_status;
    FingerprintBlock selected_character_identity;
    FingerprintBlock character_owner_rows;
    FingerprintBlock audio_semantics;
    FingerprintBlock vfs_source_set;
    bool complete;
};

struct PlanInput {
    TargetIdentity remove_precached_target;
    TargetIdentity container_remove_target;
    TargetIdentity find_target;
    std::uintptr_t expected_local_owner_slot;
    std::uintptr_t observed_local_owner_slot_pass1;
    std::uintptr_t observed_local_owner_slot_pass2;
    std::uintptr_t local_owner_pointer_pass1;
    std::uintptr_t local_owner_pointer_pass2;
    std::uint64_t local_owner_generation_pass1;
    std::uint64_t local_owner_generation_pass2;
    std::uintptr_t expected_character_owner_slot;
    std::uintptr_t observed_character_owner_slot_pass1;
    std::uintptr_t observed_character_owner_slot_pass2;
    std::uintptr_t character_owner_pointer_pass1;
    std::uintptr_t character_owner_pointer_pass2;
    std::uint64_t character_owner_generation_pass1;
    std::uint64_t character_owner_generation_pass2;
    std::uint64_t inventory_revision_pass1;
    std::uint64_t inventory_revision_pass2;
    std::uint64_t baseline_generation;
    std::uint64_t content_generation;
    std::uint64_t transition_generation;
    std::uint64_t source_set_identity;
    std::uint64_t token_generation;
    std::uint32_t expected_thread_id;
    std::uint32_t current_thread_id;
    std::uint32_t changed_mask;
    std::uintptr_t current_session;
    std::uintptr_t active_level;
    std::uintptr_t local_player_di;
    RowSnapshot baseline_rows[kPlayerPresetRowCount];
    RowSnapshot post_rows[kPlayerPresetRowCount];
    RowSnapshot character_rows_before[kPlayerPresetRowCount];
    RowSnapshot character_rows_after[kPlayerPresetRowCount];
    PreservationSnapshot preservation;
    bool supported_windows_149_profile;
    bool token_consumed;
    bool token_conflict;
    bool live_gameplay_consumer;
    bool owner_set_stable;
    bool other_owner_unchanged;
    bool critical_timeline_loss;
    bool cache_observer_loss;
    bool shutdown_or_external_quiesce;
    bool lock_and_reentrancy_contract_proven;
};

struct AuthorizationInput {
    std::uint64_t plan_identity;
    bool audio_plan_prepared;
    bool atomic_transaction_coordinator_proven;
    bool all_preconditions_revalidated;
};

struct AtomicExecutionRequest {
    std::uint64_t plan_identity;
    std::uintptr_t player_state;
    SourceKind removal_order[kSourceRemovalCount];
};

struct AtomicExecutionResult {
    std::uint64_t transaction_generation;
    std::uint32_t player_reset_execution_count;
    std::uint32_t player_mutation_count;
    std::uint32_t audio_mutation_count;
    std::uint32_t vfs_removal_count;
    std::uint32_t clear_call_count;
    std::uint32_t representation_recreation_count;
    std::uint32_t player_di_create_count;
    SourceKind removal_order[kSourceRemovalCount];
    RowSnapshot local_rows[kPlayerPresetRowCount];
    RowSnapshot character_rows[kPlayerPresetRowCount];
    PreservationSnapshot preservation;
    bool committed_atomically;
    bool cache_rebuilt_from_baseline_only;
    bool no_removed_project_provider;
    bool frontend_equals_baseline;
};

using ExecuteAtomicTransactionFn = bool (*)(
    void* user, const AtomicExecutionRequest* request,
    AtomicExecutionResult* result) noexcept;

struct Adapter {
    Phase phase;
    Failure last_failure;
    std::uint64_t plan_identity;
    std::uintptr_t player_state;
    std::uint32_t preparation_attempt_count;
    std::uint32_t authorization_attempt_count;
    std::uint32_t execution_authorized_count;
    std::uint32_t execution_count;
    std::uint32_t player_mutation_count;
    std::uint32_t audio_mutation_count;
    std::uint32_t vfs_removal_count;
    std::uint64_t transaction_generation;
    std::uint32_t clear_call_count;
    std::uint32_t representation_recreation_count;
    std::uint32_t player_di_create_count;
    RowSnapshot baseline_rows[kPlayerPresetRowCount];
    RowSnapshot character_rows_before[kPlayerPresetRowCount];
    PreservationSnapshot preservation_before;
};

void Initialize(Adapter* adapter) noexcept;
bool ValidateAndPrepare(
    Adapter* adapter, const PlanInput& input) noexcept;
bool AuthorizeExecution(
    Adapter* adapter, const AuthorizationInput& input) noexcept;
bool ExecuteAndVerify(
    Adapter* adapter, ExecuteAtomicTransactionFn execute,
    void* user) noexcept;

bool PreservationSnapshotComplete(
    const PreservationSnapshot& snapshot) noexcept;
bool PreservationSnapshotsEqual(
    const PreservationSnapshot& left,
    const PreservationSnapshot& right) noexcept;
const char* PhaseName(Phase phase) noexcept;
const char* FailureName(Failure failure) noexcept;
const char* SourceKindName(SourceKind source) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::
   // player_natural_reset

#endif
