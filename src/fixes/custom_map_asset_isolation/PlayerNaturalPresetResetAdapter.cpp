#define _HAS_EXCEPTIONS 0

#include "PlayerNaturalPresetResetAdapter.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::
    player_natural_reset {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

bool BlockComplete(const FingerprintBlock& block) noexcept
{
    return block.complete && block.fingerprint != 0U;
}

bool BlockEqual(const FingerprintBlock& left,
                const FingerprintBlock& right) noexcept
{
    return left.complete == right.complete &&
        left.fingerprint == right.fingerprint &&
        left.count == right.count;
}

bool RowEqual(const RowSnapshot& left,
              const RowSnapshot& right) noexcept
{
    return left.preset == right.preset &&
        left.definition_fingerprint ==
            right.definition_fingerprint &&
        left.definition_count == right.definition_count &&
        left.kind == right.kind &&
        left.complete == right.complete;
}

bool RowArraysEqual(
    const RowSnapshot left[kPlayerPresetRowCount],
    const RowSnapshot right[kPlayerPresetRowCount]) noexcept
{
    for (std::size_t index = 0;
         index < kPlayerPresetRowCount; ++index)
        if (!RowEqual(left[index], right[index]))
            return false;
    return true;
}

bool TargetValid(const TargetIdentity& target) noexcept
{
    return target.address != 0U &&
        target.rva != 0U &&
        target.rva == target.expected_rva &&
        target.full_image_hash_valid &&
        target.timestamp_and_size_valid &&
        target.prologue_valid;
}

bool TargetProfileValid(
    const TargetIdentity& target) noexcept
{
    return target.full_image_hash_valid &&
        target.timestamp_and_size_valid;
}

bool Fail(Adapter* adapter, Failure failure,
          Phase phase) noexcept
{
    if (!adapter) return false;
    adapter->last_failure = failure;
    adapter->phase = phase;
    return false;
}

std::uint64_t Mix(std::uint64_t hash,
                  std::uint64_t value) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(value); ++index) {
        hash ^= static_cast<std::uint8_t>(
            value >> (index * 8U));
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t BuildPlanIdentity(
    const PlanInput& input) noexcept
{
    std::uint64_t hash = kFnvOffset;
    hash = Mix(hash, input.remove_precached_target.address);
    hash = Mix(hash, input.container_remove_target.address);
    hash = Mix(hash, input.find_target.address);
    hash = Mix(hash, input.local_owner_pointer_pass2);
    hash = Mix(hash, input.local_owner_generation_pass2);
    hash = Mix(hash, input.character_owner_pointer_pass2);
    hash = Mix(hash, input.character_owner_generation_pass2);
    hash = Mix(hash, input.baseline_generation);
    hash = Mix(hash, input.content_generation);
    hash = Mix(hash, input.transition_generation);
    hash = Mix(hash, input.source_set_identity);
    hash = Mix(hash, input.token_generation);
    hash = Mix(hash, input.preservation.vfs_source_set.fingerprint);
    for (std::size_t index = 0;
         index < kPlayerPresetRowCount; ++index) {
        hash = Mix(
            hash,
            input.post_rows[index].definition_fingerprint);
        hash = Mix(
            hash,
            input.post_rows[index].definition_count);
    }
    return hash ? hash : 1U;
}

bool AllBaselineRowsAbsent(
    const RowSnapshot rows[kPlayerPresetRowCount]) noexcept
{
    for (std::size_t index = 0;
         index < kPlayerPresetRowCount; ++index)
        if (!rows[index].complete ||
            rows[index].kind != RowKind::AbsentPresetRow ||
            rows[index].preset != 0U ||
            rows[index].definition_count != 0U ||
            rows[index].definition_fingerprint != 0U)
            return false;
    return true;
}

bool AllPostRowsPublished(
    const RowSnapshot rows[kPlayerPresetRowCount]) noexcept
{
    for (std::size_t index = 0;
         index < kPlayerPresetRowCount; ++index)
        if (!rows[index].complete ||
            rows[index].kind != RowKind::PresetRow ||
            rows[index].preset == 0U ||
            rows[index].definition_count == 0U ||
            rows[index].definition_fingerprint == 0U)
            return false;
    return true;
}

}  // namespace

void Initialize(Adapter* adapter) noexcept
{
    if (!adapter) return;
    std::memset(adapter, 0, sizeof(*adapter));
    adapter->phase = Phase::Unavailable;
}

bool PreservationSnapshotComplete(
    const PreservationSnapshot& snapshot) noexcept
{
    return snapshot.complete &&
        snapshot.local_owner_slot &&
        snapshot.local_owner_pointer &&
        snapshot.local_owner_generation &&
        snapshot.character_owner_slot &&
        snapshot.character_owner_pointer &&
        snapshot.character_owner_generation &&
        BlockComplete(snapshot.inventory_tree) &&
        BlockComplete(snapshot.inventory_item_vector) &&
        BlockComplete(snapshot.equipped_items) &&
        BlockComplete(snapshot.quick_slots) &&
        BlockComplete(snapshot.money_and_stash) &&
        BlockComplete(snapshot.skills_xp_levels_legend) &&
        BlockComplete(snapshot.quest_progression_flags) &&
        BlockComplete(snapshot.appearances_and_perks) &&
        BlockComplete(snapshot.collectibles_and_rewards) &&
        BlockComplete(snapshot.buffs_and_status) &&
        BlockComplete(snapshot.selected_character_identity) &&
        BlockComplete(snapshot.character_owner_rows) &&
        BlockComplete(snapshot.audio_semantics) &&
        BlockComplete(snapshot.vfs_source_set);
}

bool PreservationSnapshotsEqual(
    const PreservationSnapshot& left,
    const PreservationSnapshot& right) noexcept
{
    return left.complete == right.complete &&
        left.local_owner_slot == right.local_owner_slot &&
        left.local_owner_pointer == right.local_owner_pointer &&
        left.local_owner_generation ==
            right.local_owner_generation &&
        left.character_owner_slot ==
            right.character_owner_slot &&
        left.character_owner_pointer ==
            right.character_owner_pointer &&
        left.character_owner_generation ==
            right.character_owner_generation &&
        BlockEqual(left.inventory_tree, right.inventory_tree) &&
        BlockEqual(
            left.inventory_item_vector,
            right.inventory_item_vector) &&
        BlockEqual(left.equipped_items, right.equipped_items) &&
        BlockEqual(left.quick_slots, right.quick_slots) &&
        BlockEqual(left.money_and_stash, right.money_and_stash) &&
        BlockEqual(
            left.skills_xp_levels_legend,
            right.skills_xp_levels_legend) &&
        BlockEqual(
            left.quest_progression_flags,
            right.quest_progression_flags) &&
        BlockEqual(
            left.appearances_and_perks,
            right.appearances_and_perks) &&
        BlockEqual(
            left.collectibles_and_rewards,
            right.collectibles_and_rewards) &&
        BlockEqual(
            left.buffs_and_status,
            right.buffs_and_status) &&
        BlockEqual(
            left.selected_character_identity,
            right.selected_character_identity) &&
        BlockEqual(
            left.character_owner_rows,
            right.character_owner_rows) &&
        BlockEqual(
            left.audio_semantics,
            right.audio_semantics) &&
        BlockEqual(
            left.vfs_source_set,
            right.vfs_source_set);
}

bool ValidateAndPrepare(
    Adapter* adapter, const PlanInput& input) noexcept
{
    if (!adapter)
        return false;
    Initialize(adapter);
    ++adapter->preparation_attempt_count;
    if (!input.supported_windows_149_profile ||
        !TargetProfileValid(input.remove_precached_target) ||
        !TargetProfileValid(input.container_remove_target) ||
        !TargetProfileValid(input.find_target))
        return Fail(
            adapter, Failure::UnsupportedImage,
            Phase::Unavailable);
    if (!TargetValid(input.remove_precached_target))
        return Fail(
            adapter, Failure::TargetUnvalidated,
            Phase::Unavailable);
    if (!TargetValid(input.container_remove_target))
        return Fail(
            adapter,
            Failure::ContainerRemoveTargetUnvalidated,
            Phase::Unavailable);
    if (!TargetValid(input.find_target))
        return Fail(
            adapter, Failure::FindTargetUnvalidated,
            Phase::Unavailable);
    adapter->phase = Phase::Validated;

    if (!input.expected_local_owner_slot ||
        input.observed_local_owner_slot_pass1 !=
            input.expected_local_owner_slot ||
        input.observed_local_owner_slot_pass2 !=
            input.expected_local_owner_slot)
        return Fail(
            adapter, Failure::OwnerSlotMismatch,
            Phase::Failed);
    if (!input.local_owner_pointer_pass1 ||
        input.local_owner_pointer_pass1 !=
            input.local_owner_pointer_pass2 ||
        !input.local_owner_generation_pass1 ||
        input.local_owner_generation_pass1 !=
            input.local_owner_generation_pass2)
        return Fail(
            adapter, Failure::OwnerGenerationMismatch,
            Phase::Failed);
    if (!input.expected_character_owner_slot ||
        input.observed_character_owner_slot_pass1 !=
            input.expected_character_owner_slot ||
        input.observed_character_owner_slot_pass2 !=
            input.expected_character_owner_slot ||
        !input.character_owner_pointer_pass1 ||
        input.character_owner_pointer_pass1 !=
            input.character_owner_pointer_pass2 ||
        !input.character_owner_generation_pass1 ||
        input.character_owner_generation_pass1 !=
            input.character_owner_generation_pass2)
        return Fail(
            adapter, Failure::CharacterOwnerMismatch,
            Phase::Failed);
    if (!input.token_consumed || !input.token_generation ||
        input.token_conflict)
        return Fail(
            adapter, Failure::TokenConflict,
            Phase::Failed);
    if (input.current_session)
        return Fail(
            adapter, Failure::SessionActive,
            Phase::Failed);
    if (input.active_level)
        return Fail(
            adapter, Failure::LevelActive,
            Phase::Failed);
    if (input.local_player_di)
        return Fail(
            adapter, Failure::LocalPlayerDiActive,
            Phase::Failed);
    if (input.live_gameplay_consumer)
        return Fail(
            adapter, Failure::LiveGameplayConsumer,
            Phase::Failed);
    if (!input.expected_thread_id ||
        input.current_thread_id != input.expected_thread_id)
        return Fail(
            adapter, Failure::WrongThread,
            Phase::Failed);
    if (!input.owner_set_stable)
        return Fail(
            adapter, Failure::OwnerSetUnstable,
            Phase::Failed);
    if (!input.inventory_revision_pass1 ||
        input.inventory_revision_pass1 !=
            input.inventory_revision_pass2)
        return Fail(
            adapter, Failure::InventoryRevisionChanged,
            Phase::Failed);
    if (!PreservationSnapshotComplete(input.preservation))
        return Fail(
            adapter,
            Failure::PreservationSnapshotIncomplete,
            Phase::Failed);
    adapter->phase = Phase::SnapshotReady;
    if (!AllBaselineRowsAbsent(input.baseline_rows))
        return Fail(
            adapter, Failure::B0RowsNotAbsent,
            Phase::Failed);
    if (input.changed_mask != 0xFFU ||
        !AllPostRowsPublished(input.post_rows))
        return Fail(
            adapter, Failure::ChangedMaskIncomplete,
            Phase::Failed);
    if (!input.other_owner_unchanged ||
        !RowArraysEqual(
            input.character_rows_before,
            input.character_rows_after))
        return Fail(
            adapter, Failure::CharacterOwnerChanged,
            Phase::Failed);
    if (input.critical_timeline_loss)
        return Fail(
            adapter, Failure::TimelineLoss,
            Phase::Failed);
    if (input.cache_observer_loss)
        return Fail(
            adapter, Failure::CacheObserverLoss,
            Phase::Failed);
    if (input.shutdown_or_external_quiesce)
        return Fail(
            adapter, Failure::ShutdownConflict,
            Phase::Failed);
    if (!input.lock_and_reentrancy_contract_proven)
        return Fail(
            adapter,
            Failure::LockReentrancyUnproven,
            Phase::Unavailable);

    adapter->plan_identity = BuildPlanIdentity(input);
    adapter->player_state =
        input.local_owner_pointer_pass2;
    std::memcpy(
        adapter->baseline_rows, input.baseline_rows,
        sizeof(adapter->baseline_rows));
    std::memcpy(
        adapter->character_rows_before,
        input.character_rows_before,
        sizeof(adapter->character_rows_before));
    adapter->preservation_before = input.preservation;
    adapter->last_failure = Failure::None;
    adapter->phase = Phase::PlanPrepared;
    return true;
}

bool AuthorizeExecution(
    Adapter* adapter, const AuthorizationInput& input) noexcept
{
    if (!adapter) return false;
    ++adapter->authorization_attempt_count;
    if (adapter->phase != Phase::PlanPrepared ||
        !adapter->plan_identity)
        return Fail(
            adapter, Failure::PlanNotPrepared,
            adapter->phase);
    if (!input.audio_plan_prepared) {
        adapter->last_failure =
            Failure::AudioPlanUnprepared;
        return false;
    }
    if (!input.atomic_transaction_coordinator_proven) {
        adapter->last_failure =
            Failure::TransactionNotAtomic;
        return false;
    }
    if (!input.all_preconditions_revalidated ||
        input.plan_identity != adapter->plan_identity) {
        adapter->last_failure =
            Failure::PlanIdentityChanged;
        return false;
    }
    adapter->last_failure = Failure::None;
    adapter->phase = Phase::ExecutionAuthorized;
    ++adapter->execution_authorized_count;
    return true;
}

bool ExecuteAndVerify(
    Adapter* adapter, ExecuteAtomicTransactionFn execute,
    void* user) noexcept
{
    if (!adapter || !execute)
        return Fail(
            adapter, Failure::InvalidArgument,
            Phase::Failed);
    if (adapter->phase != Phase::ExecutionAuthorized)
        return Fail(
            adapter, Failure::ExecutionNotAuthorized,
            adapter->phase);
    AtomicExecutionRequest request = {};
    request.plan_identity = adapter->plan_identity;
    request.player_state = adapter->player_state;
    request.removal_order[0] = SourceKind::AssetsPc;
    request.removal_order[1] = SourceKind::DataDirectory;
    request.removal_order[2] = SourceKind::ProjectRoot;
    AtomicExecutionResult result = {};
    if (!execute(user, &request, &result) ||
        !result.committed_atomically)
        return Fail(
            adapter, Failure::InvocationFailed,
            Phase::Failed);
    adapter->phase = Phase::Executed;
    adapter->execution_count =
        result.player_reset_execution_count;
    adapter->player_mutation_count =
        result.player_mutation_count;
    adapter->audio_mutation_count =
        result.audio_mutation_count;
    adapter->vfs_removal_count =
        result.vfs_removal_count;
    adapter->transaction_generation =
        result.transaction_generation;
    adapter->clear_call_count = result.clear_call_count;
    adapter->representation_recreation_count =
        result.representation_recreation_count;
    adapter->player_di_create_count =
        result.player_di_create_count;
    if (result.clear_call_count)
        return Fail(
            adapter, Failure::ClearObserved,
            Phase::Failed);
    if (result.representation_recreation_count)
        return Fail(
            adapter, Failure::RepresentationRecreated,
            Phase::Failed);
    if (result.player_di_create_count)
        return Fail(
            adapter, Failure::PlayerDiCreated,
            Phase::Failed);
    if (!result.transaction_generation ||
        result.player_reset_execution_count != 1U ||
        result.player_mutation_count != 1U ||
        result.audio_mutation_count != 1U ||
        result.vfs_removal_count != kSourceRemovalCount ||
        result.removal_order[0] != SourceKind::AssetsPc ||
        result.removal_order[1] != SourceKind::DataDirectory ||
        result.removal_order[2] != SourceKind::ProjectRoot ||
        !RowArraysEqual(
            adapter->baseline_rows, result.local_rows) ||
        !RowArraysEqual(
            adapter->character_rows_before,
            result.character_rows) ||
        !PreservationSnapshotsEqual(
            adapter->preservation_before,
            result.preservation) ||
        !result.cache_rebuilt_from_baseline_only ||
        !result.no_removed_project_provider ||
        !result.frontend_equals_baseline)
        return Fail(
            adapter, Failure::VerificationFailed,
            Phase::Failed);
    adapter->last_failure = Failure::None;
    adapter->phase = Phase::Verified;
    return true;
}

const char* PhaseName(Phase phase) noexcept
{
    switch (phase) {
    case Phase::Unavailable: return "Unavailable";
    case Phase::Validated: return "Validated";
    case Phase::SnapshotReady: return "SnapshotReady";
    case Phase::PlanPrepared: return "PlanPrepared";
    case Phase::ExecutionAuthorized:
        return "ExecutionAuthorized";
    case Phase::Executed: return "Executed";
    case Phase::Verified: return "Verified";
    case Phase::Failed: return "Failed";
    default: return "Unknown";
    }
}

const char* FailureName(Failure failure) noexcept
{
    switch (failure) {
    case Failure::None: return "none";
    case Failure::InvalidArgument: return "invalid_argument";
    case Failure::UnsupportedImage: return "unsupported_image";
    case Failure::TargetUnvalidated:
        return "remove_precached_target_unvalidated";
    case Failure::ContainerRemoveTargetUnvalidated:
        return "container_remove_target_unvalidated";
    case Failure::FindTargetUnvalidated:
        return "find_target_unvalidated";
    case Failure::OwnerSlotMismatch: return "owner_slot_mismatch";
    case Failure::OwnerGenerationMismatch:
        return "owner_generation_mismatch";
    case Failure::CharacterOwnerMismatch:
        return "character_owner_mismatch";
    case Failure::TokenConflict: return "token_conflict";
    case Failure::SessionActive: return "session_active";
    case Failure::LevelActive: return "level_active";
    case Failure::LocalPlayerDiActive:
        return "local_playerdi_active";
    case Failure::LiveGameplayConsumer:
        return "live_gameplay_consumer";
    case Failure::WrongThread: return "wrong_thread";
    case Failure::OwnerSetUnstable: return "owner_set_unstable";
    case Failure::InventoryRevisionChanged:
        return "inventory_revision_changed";
    case Failure::PreservationSnapshotIncomplete:
        return "preservation_snapshot_incomplete";
    case Failure::B0RowsNotAbsent: return "b0_rows_not_absent";
    case Failure::ChangedMaskIncomplete:
        return "changed_mask_incomplete";
    case Failure::CharacterOwnerChanged:
        return "character_owner_changed";
    case Failure::TimelineLoss: return "timeline_loss";
    case Failure::CacheObserverLoss:
        return "cache_observer_loss";
    case Failure::ShutdownConflict: return "shutdown_conflict";
    case Failure::LockReentrancyUnproven:
        return "lock_reentrancy_unproven";
    case Failure::PlanNotPrepared: return "plan_not_prepared";
    case Failure::AudioPlanUnprepared:
        return "audio_plan_unprepared";
    case Failure::TransactionNotAtomic:
        return "transaction_not_atomic";
    case Failure::PlanIdentityChanged:
        return "plan_identity_changed";
    case Failure::ExecutionNotAuthorized:
        return "execution_not_authorized";
    case Failure::InvocationFailed: return "invocation_failed";
    case Failure::VerificationFailed:
        return "verification_failed";
    case Failure::ClearObserved: return "playerstate_clear_observed";
    case Failure::RepresentationRecreated:
        return "representation_recreated";
    case Failure::PlayerDiCreated: return "playerdi_created";
    default: return "unknown";
    }
}

const char* SourceKindName(SourceKind source) noexcept
{
    switch (source) {
    case SourceKind::AssetsPc: return "AssetsPc";
    case SourceKind::DataDirectory: return "DataDirectory";
    case SourceKind::ProjectRoot: return "ProjectRoot";
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::
   // player_natural_reset
