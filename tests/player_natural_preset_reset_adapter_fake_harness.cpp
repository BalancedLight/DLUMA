#include "fixes/custom_map_asset_isolation/PlayerNaturalPresetResetAdapter.h"

#include <cstdio>
#include <cstring>

namespace natural =
    dluma::fixes::custom_map_asset_isolation::
        player_natural_reset;

namespace {

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::printf(                                                   \
                "FAIL line %d: %s\n", __LINE__, #condition);              \
            return false;                                                  \
        }                                                                  \
    } while (0)

natural::FingerprintBlock Block(
    std::uint64_t seed) noexcept
{
    natural::FingerprintBlock block = {};
    block.fingerprint = seed;
    block.count = seed + 10U;
    block.complete = true;
    return block;
}

natural::PreservationSnapshot Preservation() noexcept
{
    natural::PreservationSnapshot snapshot = {};
    snapshot.local_owner_slot = 0x1000U;
    snapshot.local_owner_pointer = 0x2000U;
    snapshot.local_owner_generation = 11U;
    snapshot.character_owner_slot = 0x1100U;
    snapshot.character_owner_pointer = 0x3000U;
    snapshot.character_owner_generation = 12U;
    snapshot.inventory_tree = Block(1U);
    snapshot.inventory_item_vector = Block(2U);
    snapshot.equipped_items = Block(3U);
    snapshot.quick_slots = Block(4U);
    snapshot.money_and_stash = Block(5U);
    snapshot.skills_xp_levels_legend = Block(6U);
    snapshot.quest_progression_flags = Block(7U);
    snapshot.appearances_and_perks = Block(8U);
    snapshot.collectibles_and_rewards = Block(9U);
    snapshot.buffs_and_status = Block(10U);
    snapshot.selected_character_identity = Block(11U);
    snapshot.character_owner_rows = Block(12U);
    snapshot.audio_semantics = Block(13U);
    snapshot.vfs_source_set = Block(14U);
    snapshot.complete = true;
    return snapshot;
}

natural::TargetIdentity Target(
    std::uintptr_t address, std::uint32_t rva) noexcept
{
    natural::TargetIdentity target = {};
    target.address = address;
    target.rva = rva;
    target.expected_rva = rva;
    target.full_image_hash_valid = true;
    target.timestamp_and_size_valid = true;
    target.prologue_valid = true;
    return target;
}

natural::PlanInput Fixture() noexcept
{
    natural::PlanInput input = {};
    input.remove_precached_target =
        Target(0x180BB84A0ULL, 0x00BB84A0U);
    input.container_remove_target =
        Target(0x18045AC60ULL, 0x0045AC60U);
    input.find_target =
        Target(0x18045AB80ULL, 0x0045AB80U);
    input.expected_local_owner_slot = 0x1000U;
    input.observed_local_owner_slot_pass1 = 0x1000U;
    input.observed_local_owner_slot_pass2 = 0x1000U;
    input.local_owner_pointer_pass1 = 0x2000U;
    input.local_owner_pointer_pass2 = 0x2000U;
    input.local_owner_generation_pass1 = 11U;
    input.local_owner_generation_pass2 = 11U;
    input.expected_character_owner_slot = 0x1100U;
    input.observed_character_owner_slot_pass1 = 0x1100U;
    input.observed_character_owner_slot_pass2 = 0x1100U;
    input.character_owner_pointer_pass1 = 0x3000U;
    input.character_owner_pointer_pass2 = 0x3000U;
    input.character_owner_generation_pass1 = 12U;
    input.character_owner_generation_pass2 = 12U;
    input.inventory_revision_pass1 = 77U;
    input.inventory_revision_pass2 = 77U;
    input.baseline_generation = 1U;
    input.content_generation = 9U;
    input.transition_generation = 4U;
    input.source_set_identity = 0xABCDEFU;
    input.token_generation = 31U;
    input.expected_thread_id = 44U;
    input.current_thread_id = 44U;
    input.changed_mask = 0xFFU;
    input.supported_windows_149_profile = true;
    input.token_consumed = true;
    input.owner_set_stable = true;
    input.other_owner_unchanged = true;
    input.lock_and_reentrancy_contract_proven = true;
    input.preservation = Preservation();
    for (std::size_t index = 0;
         index < natural::kPlayerPresetRowCount; ++index) {
        input.baseline_rows[index].kind =
            natural::RowKind::AbsentPresetRow;
        input.baseline_rows[index].complete = true;
        input.post_rows[index].preset = 0x4000U + index * 0x100U;
        input.post_rows[index].definition_fingerprint =
            0x5000U + index;
        input.post_rows[index].definition_count =
            static_cast<std::uint32_t>(index + 1U);
        input.post_rows[index].kind =
            natural::RowKind::PresetRow;
        input.post_rows[index].complete = true;
        input.character_rows_before[index].preset =
            0x8000U + index * 0x100U;
        input.character_rows_before[index].
            definition_fingerprint = 0x9000U + index;
        input.character_rows_before[index].definition_count =
            static_cast<std::uint32_t>(index + 20U);
        input.character_rows_before[index].kind =
            natural::RowKind::PresetRow;
        input.character_rows_before[index].complete = true;
        input.character_rows_after[index] =
            input.character_rows_before[index];
    }
    return input;
}

bool Rejects(
    natural::PlanInput input,
    natural::Failure expected) noexcept
{
    natural::Adapter adapter = {};
    CHECK(!natural::ValidateAndPrepare(&adapter, input));
    CHECK(adapter.last_failure == expected);
    CHECK(adapter.execution_count == 0U);
    CHECK(adapter.player_mutation_count == 0U);
    CHECK(adapter.audio_mutation_count == 0U);
    CHECK(adapter.vfs_removal_count == 0U);
    CHECK(adapter.transaction_generation == 0U);
    return true;
}

bool TestExactTargetAndBoundaryRejections() noexcept
{
    natural::PlanInput input = Fixture();
    input.remove_precached_target.full_image_hash_valid = false;
    CHECK(Rejects(input, natural::Failure::UnsupportedImage));
    input = Fixture();
    input.remove_precached_target.prologue_valid = false;
    CHECK(Rejects(input, natural::Failure::TargetUnvalidated));
    input = Fixture();
    ++input.remove_precached_target.rva;
    CHECK(Rejects(input, natural::Failure::TargetUnvalidated));
    input = Fixture();
    input.container_remove_target.prologue_valid = false;
    CHECK(Rejects(
        input,
        natural::Failure::ContainerRemoveTargetUnvalidated));
    input = Fixture();
    input.find_target.prologue_valid = false;
    CHECK(Rejects(
        input, natural::Failure::FindTargetUnvalidated));
    input = Fixture();
    input.current_thread_id = 45U;
    CHECK(Rejects(input, natural::Failure::WrongThread));
    input = Fixture();
    input.current_session = 1U;
    CHECK(Rejects(input, natural::Failure::SessionActive));
    input = Fixture();
    input.active_level = 1U;
    CHECK(Rejects(input, natural::Failure::LevelActive));
    input = Fixture();
    input.local_player_di = 1U;
    CHECK(Rejects(input, natural::Failure::LocalPlayerDiActive));
    input = Fixture();
    input.live_gameplay_consumer = true;
    CHECK(Rejects(
        input, natural::Failure::LiveGameplayConsumer));
    return true;
}

bool TestOwnerSnapshotAndTimelineRejections() noexcept
{
    natural::PlanInput input = Fixture();
    ++input.observed_local_owner_slot_pass2;
    CHECK(Rejects(input, natural::Failure::OwnerSlotMismatch));
    input = Fixture();
    ++input.local_owner_generation_pass2;
    CHECK(Rejects(
        input, natural::Failure::OwnerGenerationMismatch));
    input = Fixture();
    input.owner_set_stable = false;
    CHECK(Rejects(input, natural::Failure::OwnerSetUnstable));
    input = Fixture();
    ++input.inventory_revision_pass2;
    CHECK(Rejects(
        input, natural::Failure::InventoryRevisionChanged));
    input = Fixture();
    input.preservation.complete = false;
    CHECK(Rejects(
        input,
        natural::Failure::PreservationSnapshotIncomplete));
    input = Fixture();
    input.baseline_rows[4].kind =
        natural::RowKind::PresetRow;
    CHECK(Rejects(input, natural::Failure::B0RowsNotAbsent));
    input = Fixture();
    input.changed_mask = 0x7FU;
    CHECK(Rejects(
        input, natural::Failure::ChangedMaskIncomplete));
    input = Fixture();
    ++input.character_rows_after[2].definition_fingerprint;
    CHECK(Rejects(
        input, natural::Failure::CharacterOwnerChanged));
    input = Fixture();
    input.critical_timeline_loss = true;
    CHECK(Rejects(input, natural::Failure::TimelineLoss));
    input = Fixture();
    input.cache_observer_loss = true;
    CHECK(Rejects(input, natural::Failure::CacheObserverLoss));
    input = Fixture();
    input.lock_and_reentrancy_contract_proven = false;
    CHECK(Rejects(
        input,
        natural::Failure::LockReentrancyUnproven));
    return true;
}

struct FakeTransaction {
    natural::PlanInput fixture;
    bool called;
};

bool ExecuteAtomic(
    void* user,
    const natural::AtomicExecutionRequest* request,
    natural::AtomicExecutionResult* result) noexcept
{
    FakeTransaction* fake =
        static_cast<FakeTransaction*>(user);
    if (!fake || !request || !result)
        return false;
    fake->called = true;
    if (!request->plan_identity ||
        request->player_state !=
            fake->fixture.local_owner_pointer_pass2 ||
        request->removal_order[0] !=
            natural::SourceKind::AssetsPc ||
        request->removal_order[1] !=
            natural::SourceKind::DataDirectory ||
        request->removal_order[2] !=
            natural::SourceKind::ProjectRoot)
        return false;
    result->transaction_generation = 99U;
    result->player_reset_execution_count = 1U;
    result->player_mutation_count = 1U;
    result->audio_mutation_count = 1U;
    result->vfs_removal_count = 3U;
    std::memcpy(
        result->removal_order, request->removal_order,
        sizeof(result->removal_order));
    std::memcpy(
        result->local_rows, fake->fixture.baseline_rows,
        sizeof(result->local_rows));
    std::memcpy(
        result->character_rows,
        fake->fixture.character_rows_before,
        sizeof(result->character_rows));
    result->preservation = fake->fixture.preservation;
    result->committed_atomically = true;
    result->cache_rebuilt_from_baseline_only = true;
    result->no_removed_project_provider = true;
    result->frontend_equals_baseline = true;
    return true;
}

bool TestPreparedOnlyAndFutureAtomicTransaction() noexcept
{
    natural::PlanInput fixture = Fixture();
    natural::Adapter adapter = {};
    CHECK(natural::ValidateAndPrepare(&adapter, fixture));
    CHECK(adapter.phase == natural::Phase::PlanPrepared);
    CHECK(adapter.last_failure == natural::Failure::None);
    natural::AuthorizationInput authorization = {};
    authorization.plan_identity = adapter.plan_identity;
    authorization.atomic_transaction_coordinator_proven = true;
    authorization.all_preconditions_revalidated = true;
    CHECK(!natural::AuthorizeExecution(
        &adapter, authorization));
    CHECK(adapter.phase == natural::Phase::PlanPrepared);
    CHECK(adapter.last_failure ==
          natural::Failure::AudioPlanUnprepared);
    CHECK(adapter.execution_authorized_count == 0U);
    CHECK(adapter.execution_count == 0U);
    CHECK(adapter.player_mutation_count == 0U);
    CHECK(adapter.audio_mutation_count == 0U);
    CHECK(adapter.vfs_removal_count == 0U);
    CHECK(adapter.transaction_generation == 0U);

    authorization.audio_plan_prepared = true;
    CHECK(natural::AuthorizeExecution(
        &adapter, authorization));
    CHECK(adapter.phase ==
          natural::Phase::ExecutionAuthorized);
    FakeTransaction fake = {fixture, false};
    CHECK(natural::ExecuteAndVerify(
        &adapter, ExecuteAtomic, &fake));
    CHECK(fake.called);
    CHECK(adapter.phase == natural::Phase::Verified);
    CHECK(adapter.execution_count == 1U);
    CHECK(adapter.player_mutation_count == 1U);
    CHECK(adapter.audio_mutation_count == 1U);
    CHECK(adapter.vfs_removal_count == 3U);
    CHECK(adapter.transaction_generation == 99U);
    CHECK(adapter.clear_call_count == 0U);
    CHECK(adapter.representation_recreation_count == 0U);
    CHECK(adapter.player_di_create_count == 0U);
    return true;
}

}  // namespace

int main()
{
    if (!TestExactTargetAndBoundaryRejections() ||
        !TestOwnerSnapshotAndTimelineRejections() ||
        !TestPreparedOnlyAndFutureAtomicTransaction())
        return 1;
    std::puts(
        "player natural preset reset adapter fake harness: PASS");
    return 0;
}
