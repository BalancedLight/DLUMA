#define _HAS_EXCEPTIONS 0

#include "../src/fixes/custom_map_asset_isolation/PlayerGameplayParamsRestartAdapter.h"

#include <cstdio>
#include <cstring>

namespace player =
    dluma::fixes::custom_map_asset_isolation::cold_reboot::player_restart;
namespace cold = dluma::fixes::custom_map_asset_isolation::cold_reboot;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

constexpr std::uint32_t kPropertyIds[
    player::kRequiredPlayerPropertyCount] = {
        player::kLowHealthEffectThresholdPropertyId,
        player::kStaminaWarningLevelPropertyId,
        player::kMaxStaminaPropertyId,
        player::kMoveSprintSpeedPropertyId,
        player::kMaxHealthPropertyId,
        player::kBaseMaxHealthPropertyId,
        player::kHealthCriticalLevelPropertyId,
        player::kHealthRegenerationDelayPropertyId,
        player::kHealthRegenerationMaxLevelPropertyId,
        player::kHealthRegenerationTimeToMaxPropertyId,
        player::kCameraDefaultFovPropertyId,
        player::kKickStaminaUsagePropertyId,
        player::kCameraCarFovPropertyId,
        player::kFovSprintModifierPropertyId,
        player::kFovAmortizerModifierPropertyId,
};

bool IsUnchangedFovControl(std::uint32_t property_id) noexcept
{
    return property_id == player::kCameraCarFovPropertyId ||
        property_id == player::kFovSprintModifierPropertyId ||
        property_id == player::kFovAmortizerModifierPropertyId;
}

struct FakeState {
    std::uintptr_t pointer;
    std::uint64_t generation;
    player::PlayerStateRestartSnapshot baseline;
    player::PlayerStateRestartSnapshot current;
    std::uint32_t initialize_calls;
};

struct FakeWorld {
    player::PlayerGameplayParamsRestartAdapter* adapter;
    player::PlayerRestartEnvironment environment;
    FakeState states[4];
    std::uint32_t state_count;
    std::uint32_t environment_read_calls;
    std::uint32_t snapshot_calls;
    std::uint32_t reset_calls;
    std::uint32_t order[64];
    std::uint32_t snapshot_observed_threads[64];
    std::uint32_t order_count;
    player::PlayerEnvironmentReadPurpose last_environment_purpose;
    std::uint32_t startup_semantic_environment_reads;
    std::uint32_t post_destroy_semantic_environment_reads;
    std::uint32_t restart_preflight_environment_reads;
    std::uint32_t restart_verify_environment_reads;
    std::uint32_t reject_environment_on_call;
    std::uint32_t reject_snapshot_on_call;
    std::uint32_t semantic_mismatch_on_snapshot_call;
    bool mutation_trace;
    bool sabotage_provider;
    bool sabotage_invariant;
    bool sabotage_changed_property;
    bool change_cache_generation_after_snapshot_window;
    bool change_inventory_revision_after_snapshot_window;
    bool semantic_mismatch_on_pass2;
    std::uint32_t mutate_inventory_on_environment_read;
    std::uint32_t observed_post_destroy_pass_count;
    std::uint32_t observed_post_destroy_passes[16];
    std::uint32_t observed_post_destroy_owner_counts[16];
    std::uint32_t observed_post_destroy_threads[16];
    std::uint64_t observed_post_destroy_inventory_revisions[16];
    std::uintptr_t observed_post_destroy_first_owners[16];
    std::uintptr_t observed_post_destroy_first_states[16];
};

struct RegistryHarnessWorld {
    cold::ContentDomainRestartRegistry registry;
    FakeWorld* player_world;
    std::uint32_t vfs_reset_calls;
    std::uint32_t frontend_verify_calls;
};

player::PlayerContentProviderIdentity Provider(
    bool base, std::uint32_t seed) noexcept
{
    player::PlayerContentProviderIdentity provider = {};
    provider.source = base ? 0x800000U + seed : 0x900000U + seed;
    provider.normalized_path_fingerprint =
        (base ? 0xB0000000ULL : 0xC0000000ULL) + seed;
    provider.source_registry_generation = 77U;
    provider.content_epoch = base ? 0U : 7U;
    provider.source_record_index = base ? 3U : 11U;
    provider.source_inventory_ordinal = base ? 2U : 19U;
    provider.observed = true;
    provider.exact_source = true;
    provider.base_provider = base;
    provider.source_registry_generation_observed = true;
    return provider;
}

player::PlayerStateRestartSnapshot MakeSnapshot(
    std::uintptr_t pointer, std::uint64_t generation,
    std::uint32_t seed, std::uint64_t cache_generation,
    bool base) noexcept
{
    player::PlayerStateRestartSnapshot snapshot = {};
    snapshot.state = pointer;
    snapshot.state_generation = generation;
    snapshot.cache_generation = cache_generation;
    snapshot.player_variables = 0x500000U + seed * 0x100U;
    snapshot.progression_selector_fingerprint = 0x1010U + seed;
    snapshot.class_selector_fingerprint = 0x2020U + seed;
    snapshot.skill_membership_fingerprint = 0x3030U + seed;
    snapshot.regular_buff_fingerprint = 0x4040U + seed;
    snapshot.timed_buff_fingerprint = 0x5050U + seed;
    snapshot.inventory_fingerprint = 0x6060U + seed;
    snapshot.equipment_fingerprint = 0x7070U + seed;
    snapshot.quest_fingerprint = 0x8080U + seed;
    snapshot.appearance_fingerprint =
        (base ? 0xA000U : 0xA100U) + seed;
    snapshot.appearance_provider = Provider(base, seed);
    snapshot.body_readable = true;
    snapshot.row_table_readable = true;
    snapshot.player_variables_readable = true;
    snapshot.selectors_complete = true;
    snapshot.preservation_invariants_complete = true;
    snapshot.appearance_complete = true;
    snapshot.player_semantic_snapshot_complete = true;
    snapshot.player_restart_snapshot_complete = true;
    snapshot.snapshot_complete = true;
    for (std::uint32_t index = 0;
         index < player::kPlayerLogicalTypeCount; ++index) {
        player::PlayerDefinitionRowSnapshot& row = snapshot.rows[index];
        row.definition_vector =
            (base ? 0x1000000U : 0x2000000U) + seed * 0x10000U +
            index * 0x100U;
        row.definition_fingerprint =
            (base ? 0x3000000U : 0x4000000U) + seed * 0x100U + index;
        row.earned_experience = 1000U + seed * 10U + index;
        row.unknown_100 = 100U;
        row.trailing_qword = 0U;
        row.cache_generation = cache_generation;
        row.definition_count = base ? 379U + index : 385U + index;
        row.current_level = static_cast<std::uint16_t>(index + seed);
        row.maximum_level = static_cast<std::uint16_t>(
            (base ? 25U : 31U) + index);
        row.provider = Provider(base, seed);
        row.cache_membership_evidence =
            player::PlayerCacheMembershipEvidence::ProvenMember;
        row.membership_observation_sequence =
            1000U + seed * 100U + index;
        row.row_readable = true;
        row.definition_readable = true;
        row.cache_generation_observed = true;
        row.cache_observer_available = true;
        row.cache_inventory_complete = true;
        row.member_of_live_cache = true;
        row.semantic_complete = true;
    }
    for (std::uint32_t index = 0;
         index < player::kRequiredPlayerPropertyCount; ++index) {
        player::PlayerPropertySnapshot& property =
            snapshot.properties[index];
        property.property_id = kPropertyIds[index];
        property.value_bits = base
            ? 0x41000000U + index + seed
            : 0x42000000U + index + seed;
        property.cache_generation = cache_generation;
        property.provider = Provider(base, seed);
        property.available = true;
    }
    return snapshot;
}

FakeState* FindState(FakeWorld* world, std::uintptr_t pointer,
                     std::uint64_t generation = 0) noexcept
{
    if (!world) return nullptr;
    for (std::uint32_t index = 0; index < world->state_count; ++index) {
        if (world->states[index].pointer == pointer &&
            (!generation || world->states[index].generation == generation))
            return &world->states[index];
    }
    return nullptr;
}

void RecordOrder(FakeWorld* world, std::uint32_t value) noexcept
{
    if (world && world->order_count < 64U)
        world->order[world->order_count++] = value;
}

std::uint32_t FindOrder(const FakeWorld& world,
                        std::uint32_t value) noexcept
{
    for (std::uint32_t index = 0; index < world.order_count; ++index)
        if (world.order[index] == value) return index;
    return UINT32_MAX;
}

cold::DomainResult RegistryPass(
    void*, const cold::RestartContext*) noexcept
{
    return cold::DomainResult::Succeeded;
}

cold::DomainResult RegistryVfsReset(
    void* user, const cold::RestartContext* context) noexcept
{
    RegistryHarnessWorld* harness =
        static_cast<RegistryHarnessWorld*>(user);
    if (!harness || !harness->player_world ||
        !cold::MarkRegistryMutationStarted(
            &harness->registry, context))
        return cold::DomainResult::Failed;
    ++harness->vfs_reset_calls;
    RecordOrder(harness->player_world, 50U);
    return cold::DomainResult::Succeeded;
}

cold::DomainResult RegistryPlayerVerify(
    void* user, const cold::RestartContext* context) noexcept
{
    player::PlayerGameplayParamsRestartAdapter* adapter =
        static_cast<player::PlayerGameplayParamsRestartAdapter*>(user);
    const cold::DomainResult result =
        player::VerifyPlayerGameplayParamsRestart(adapter, context);
    if (result == cold::DomainResult::Succeeded && adapter) {
        FakeWorld* world = static_cast<FakeWorld*>(
            adapter->operations_user);
        RecordOrder(world, 500U);
    }
    return result;
}

cold::DomainResult RegistryFrontendVerify(
    void* user, const cold::RestartContext*) noexcept
{
    RegistryHarnessWorld* harness =
        static_cast<RegistryHarnessWorld*>(user);
    if (!harness || !harness->player_world ||
        !harness->player_world->adapter ||
        !harness->player_world->adapter->verified ||
        harness->vfs_reset_calls != 1U)
        return cold::DomainResult::Failed;
    ++harness->frontend_verify_calls;
    RecordOrder(harness->player_world, 600U);
    return cold::DomainResult::Succeeded;
}

cold::ContentDomainAdapter RegistryPassAdapter(
    cold::ContentDomainId id, const char* name,
    cold::ContentDomainMask dependencies, bool mandatory,
    bool destructive, void* user) noexcept
{
    cold::ContentDomainAdapter adapter = {};
    adapter.id = id;
    if (name) {
        const std::size_t length = std::strlen(name);
        if (length < sizeof(adapter.name))
            std::memcpy(adapter.name, name, length + 1U);
    }
    adapter.dependency_mask = dependencies;
    adapter.enabled = true;
    adapter.mandatory = mandatory;
    adapter.destructive = destructive;
    adapter.user = user;
    adapter.capture_baseline = &RegistryPass;
    adapter.preflight = &RegistryPass;
    adapter.quiesce = &RegistryPass;
    adapter.cancel_quiesce = &RegistryPass;
    adapter.reset = &RegistryPass;
    adapter.reload = &RegistryPass;
    adapter.verify = &RegistryPass;
    return adapter;
}

bool ReadEnvironment(void* user,
                     player::PlayerEnvironmentReadPurpose purpose,
                     player::PlayerRestartEnvironment* output) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !output) return false;
    ++world->environment_read_calls;
    world->last_environment_purpose = purpose;
    switch (purpose) {
    case player::PlayerEnvironmentReadPurpose::StartupSemanticBaseline:
        ++world->startup_semantic_environment_reads;
        break;
    case player::PlayerEnvironmentReadPurpose::
            PostDestroySemanticAssessment:
        ++world->post_destroy_semantic_environment_reads;
        break;
    case player::PlayerEnvironmentReadPurpose::RestartMutationPreflight:
        ++world->restart_preflight_environment_reads;
        break;
    case player::PlayerEnvironmentReadPurpose::RestartMutationVerify:
        ++world->restart_verify_environment_reads;
        break;
    default:
        break;
    }
    if (world->mutation_trace) RecordOrder(world, 300U);
    *output = world->environment;
    if (world->reject_environment_on_call ==
        world->environment_read_calls)
        return false;
    if (world->mutate_inventory_on_environment_read ==
            world->environment_read_calls &&
        world->adapter) {
        (void)player::ObservePlayerStateConstructed(
            world->adapter, 0x7E0000U);
    }
    return true;
}

bool SnapshotState(
    void* user, std::uintptr_t state, std::uint64_t generation,
    player::PlayerStateRestartSnapshot* output) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    FakeState* found = FindState(world, state, generation);
    if (!found || !output) return false;
    if (world->snapshot_calls < 64U)
        world->snapshot_observed_threads[world->snapshot_calls] =
            world->environment.current_thread_id;
    ++world->snapshot_calls;
    if (world->mutation_trace) RecordOrder(world, 400U);
    *output = found->current;
    if (world->reject_snapshot_on_call == world->snapshot_calls) {
        output->failure_stage =
            player::PlayerStateSnapshotFailureStage::
                RowDefinitionTraversalIncomplete;
        output->failed_row_index = 3U;
        output->raw_rows[3].failure_reason = 0x4000U;
        output->raw_rows[3].definition_traversal.first_failure =
            player::PlayerDefinitionTraversalFailure::
                PropertySpanUnreadable;
        return false;
    }
    if (world->semantic_mismatch_on_pass2 &&
        world->snapshot_calls == world->state_count + 1U)
        output->rows[0].definition_fingerprint ^= 1U;
    if (world->semantic_mismatch_on_snapshot_call ==
        world->snapshot_calls)
        output->rows[0].definition_fingerprint ^= 1U;
    if (world->change_cache_generation_after_snapshot_window &&
        world->snapshot_calls == world->state_count)
        ++world->environment.cache_generation;
    if (world->change_inventory_revision_after_snapshot_window &&
        world->snapshot_calls == world->state_count && world->adapter) {
        const std::uintptr_t pointer = 0x7F0000U;
        const std::uint64_t observed =
            player::ObservePlayerStateConstructed(world->adapter, pointer);
        if (observed)
            (void)player::ClassifyPlayerStateOwner(
                world->adapter, pointer, observed,
                player::PlayerStateOwnerKind::OtherRttiObserved,
                0x7F1000U);
    }
    return true;
}

void ObservePostDestroySemanticPass(
    void* user, std::uint32_t pass,
    const player::PlayerRestartEnvironment* environment,
    const player::PlayerStateInventoryRecord* owners,
    const player::PlayerStateRestartSnapshot* states,
    std::uint32_t owner_count,
    std::uint64_t inventory_revision) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !environment || !owners || !states || !owner_count)
        return;
    const std::uint32_t index =
        world->observed_post_destroy_pass_count;
    if (index < 16U) {
        world->observed_post_destroy_passes[index] = pass;
        world->observed_post_destroy_owner_counts[index] = owner_count;
        world->observed_post_destroy_threads[index] =
            environment->current_thread_id;
        world->observed_post_destroy_inventory_revisions[index] =
            inventory_revision;
        world->observed_post_destroy_first_owners[index] =
            owners[0].pointer;
        world->observed_post_destroy_first_states[index] =
            states[0].state;
    }
    ++world->observed_post_destroy_pass_count;
}

void ResetGlobalCache(void* user, std::uintptr_t container) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || container != world->environment.gameplay_params_container)
        return;
    ++world->reset_calls;
    RecordOrder(world, 100U);
    ++world->environment.cache_generation;
    world->mutation_trace = true;
}

void InitializeVariables(void* user, std::uintptr_t state) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    FakeState* found = FindState(world, state);
    if (!found) return;
    ++found->initialize_calls;
    RecordOrder(world, 200U + static_cast<std::uint32_t>(found->generation));

    const player::PlayerStateRestartSnapshot preserved = found->current;
    found->current = found->baseline;
    found->current.cache_generation =
        world->environment.cache_generation;
    found->current.player_variables = preserved.player_variables;
    found->current.progression_selector_fingerprint =
        preserved.progression_selector_fingerprint;
    found->current.class_selector_fingerprint =
        preserved.class_selector_fingerprint;
    found->current.skill_membership_fingerprint =
        preserved.skill_membership_fingerprint;
    found->current.regular_buff_fingerprint =
        preserved.regular_buff_fingerprint;
    found->current.timed_buff_fingerprint =
        preserved.timed_buff_fingerprint;
    found->current.inventory_fingerprint = preserved.inventory_fingerprint;
    found->current.equipment_fingerprint = preserved.equipment_fingerprint;
    found->current.quest_fingerprint = preserved.quest_fingerprint;
    for (std::uint32_t index = 0;
         index < player::kPlayerLogicalTypeCount; ++index) {
        found->current.rows[index].definition_vector +=
            world->environment.cache_generation * 0x100000U;
        found->current.rows[index].cache_generation =
            world->environment.cache_generation;
        found->current.rows[index].earned_experience =
            preserved.rows[index].earned_experience;
        found->current.rows[index].current_level =
            preserved.rows[index].current_level;
    }
    for (std::uint32_t index = 0;
         index < player::kRequiredPlayerPropertyCount; ++index)
        found->current.properties[index].cache_generation =
            world->environment.cache_generation;
    if (world->sabotage_changed_property) {
        for (std::uint32_t index = 0;
             index < player::kRequiredPlayerPropertyCount; ++index) {
            if (found->current.properties[index].property_id ==
                player::kHealthRegenerationDelayPropertyId) {
                found->current.properties[index].value_bits ^= 1U;
                break;
            }
        }
    }
    if (world->sabotage_provider)
        found->current.rows[0].provider.source ^= 0x100U;
    if (world->sabotage_invariant)
        ++found->current.skill_membership_fingerprint;
}

player::PlayerGameplayParamsSnapshotOperations SnapshotOperations() noexcept
{
    player::PlayerGameplayParamsSnapshotOperations operations = {};
    operations.read_environment = &ReadEnvironment;
    operations.snapshot_state = &SnapshotState;
    operations.environment_layout_validated = true;
    operations.state_snapshot_layout_validated = true;
    operations.observe_post_destroy_semantic_pass =
        &ObservePostDestroySemanticPass;
    return operations;
}

player::PlayerGameplayParamsMutationOperations MutationOperations(
    bool production_enabled = true) noexcept
{
    player::PlayerGameplayParamsMutationOperations operations = {};
    operations.reset_global_cache = &ResetGlobalCache;
    operations.initialize_variables = &InitializeVariables;
    operations.global_cache_reset_route_validated = true;
    operations.initialize_variables_route_validated = true;
    operations.production_enabled = production_enabled;
    return operations;
}

cold::RestartContext BaselineContext() noexcept
{
    cold::RestartContext context = {};
    context.baseline_generation = 1;
    context.owner_thread_id = 77;
    return context;
}

cold::RestartContext TransactionContext(std::uint64_t generation = 9) noexcept
{
    cold::RestartContext context = {};
    context.baseline_generation = 1;
    context.transaction_generation = generation;
    context.content_epoch = 7;
    context.owner_thread_id = 77;
    return context;
}

bool PrepareWorldInventory(
    FakeWorld* world,
    player::PlayerGameplayParamsRestartAdapter* adapter,
    const player::PlayerGameplayParamsMutationOperations*
        mutation_operations) noexcept
{
    if (!world || !adapter) return false;
    std::memset(world, 0, sizeof(*world));
    world->adapter = adapter;
    world->environment.gameplay_params_container = 0x600000U;
    world->environment.gameplay_cache_fingerprint = 0xA11CE55ULL;
    world->environment.game_di = 0x700000U;
    world->environment.cache_generation = 1;
    world->environment.current_thread_id = 77;
    world->environment.supported_image = true;
    world->environment.gameplay_params_container_readable = true;
    world->environment.game_di_readable = true;
    world->environment.global_owner_slots_readable = true;
    world->environment.inventory_scan_complete = true;

    const player::PlayerGameplayParamsSnapshotOperations snapshot_operations =
        SnapshotOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        adapter, &snapshot_operations, mutation_operations, world);
    if (!player::BeginPlayerStateInventoryCoverage(
            adapter, true, true, true))
        return false;

    const std::uintptr_t pointers[2] = {0x100000U, 0x200000U};
    for (std::uint32_t index = 0; index < 2U; ++index) {
        const std::uint64_t generation =
            player::ObservePlayerStateConstructed(adapter, pointers[index]);
        if (!generation) return false;
        const player::PlayerStateOwnerKind owner = index == 0
            ? player::PlayerStateOwnerKind::LocalProcessGlobal
            : player::PlayerStateOwnerKind::CharacterManagerGlobal;
        if (!player::ClassifyPlayerStateOwner(
                adapter, pointers[index], generation, owner,
                0x710000U + index * 0x100U))
            return false;
        FakeState& state = world->states[world->state_count++];
        state.pointer = pointers[index];
        state.generation = generation;
        state.baseline = MakeSnapshot(
            state.pointer, generation, index + 1U,
            world->environment.cache_generation, true);
        state.current = state.baseline;
    }
    world->environment.local_process_global_state = pointers[0];
    world->environment.character_manager_global_state = pointers[1];
    return true;
}

bool CaptureBaseline(
    player::PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    const cold::RestartContext baseline = BaselineContext();
    return player::CapturePlayerGameplayParamsBaseline(
        adapter, &baseline) == cold::DomainResult::Succeeded;
}

bool Setup(FakeWorld* world,
           player::PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    const player::PlayerGameplayParamsMutationOperations mutation_operations =
        MutationOperations();
    return PrepareWorldInventory(
               world, adapter, &mutation_operations) &&
        CaptureBaseline(adapter);
}

player::PlayerGameplayParamsTouchAssessment AssessPlayerTouch(
    player::PlayerGameplayParamsRestartAdapter* adapter) noexcept;

bool SetupAbsent(
    FakeWorld* world,
    player::PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    if (!world || !adapter) return false;
    std::memset(world, 0, sizeof(*world));
    world->adapter = adapter;
    world->environment.gameplay_params_container = 0x600000U;
    world->environment.game_di = 0x700000U;
    world->environment.cache_generation = 1U;
    world->environment.gameplay_cache_fingerprint = 0xA11CE55ULL;
    world->environment.current_thread_id = 77U;
    world->environment.supported_image = true;
    world->environment.gameplay_params_container_readable = true;
    world->environment.game_di_readable = true;
    world->environment.global_owner_slots_readable = true;
    world->environment.inventory_scan_complete = true;
    const player::PlayerGameplayParamsSnapshotOperations snapshots =
        SnapshotOperations();
    const player::PlayerGameplayParamsMutationOperations mutations =
        MutationOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        adapter, &snapshots, &mutations, world);
    return player::BeginPlayerStateInventoryCoverage(
               adapter, true, true, true) &&
        CaptureBaseline(adapter);
}

bool TestAuthoritativeAbsentBaselineAndAssessment() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(SetupAbsent(&world, &adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 0U);
    CHECK(adapter.baseline_kind ==
          player::PlayerStartupBaselineKind::Absent);
    CHECK(adapter.baseline_gameplay_cache_fingerprint ==
          world.environment.gameplay_cache_fingerprint);
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::UntouchedAbsent);
    CHECK(adapter.last_failure == player::PlayerRestartFailure::None);

    world.environment.gameplay_cache_fingerprint ^= 0x100U;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              TouchedDefinitionDelta);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::TouchedResetUnavailable);
    world.environment.gameplay_cache_fingerprint =
        adapter.baseline_gameplay_cache_fingerprint;

    const std::uintptr_t surviving = 0x330000U;
    const std::uint64_t generation =
        player::ObservePlayerStateConstructed(&adapter, surviving);
    CHECK(generation != 0U);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, surviving, generation,
        player::PlayerStateOwnerKind::LocalProcessGlobal, 0x710000U));
    world.environment.local_process_global_state = surviving;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              TouchedPersistentOwner);

    FakeWorld player_di_world = {};
    player::PlayerGameplayParamsRestartAdapter player_di_adapter = {};
    CHECK(SetupAbsent(&player_di_world, &player_di_adapter));
    const std::uintptr_t player_di_state = 0x440000U;
    const std::uint64_t player_di_generation =
        player::ObservePlayerStateConstructed(
            &player_di_adapter, player_di_state);
    CHECK(player_di_generation != 0U);
    CHECK(player::ClassifyPlayerStateOwner(
        &player_di_adapter, player_di_state,
        player_di_generation,
        player::PlayerStateOwnerKind::PlayerDi, 0x720000U));
    player_di_world.environment.local_player_di = 0x720000U;
    CHECK(AssessPlayerTouch(&player_di_adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              Incomplete);
    CHECK(player_di_adapter.post_destroy_diagnostics.
              final_exact_failure ==
          player::PlayerPostDestroyExactFailure::
              LocalPlayerStillPresent);
    return true;
}

bool TestAbsentBaselineDoesNotRequirePerStateSnapshots() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    std::memset(&world, 0, sizeof(world));
    world.adapter = &adapter;
    world.environment.gameplay_params_container = 0x600000U;
    world.environment.game_di = 0x700000U;
    world.environment.cache_generation = 1U;
    world.environment.gameplay_cache_fingerprint = 0xA11CE55ULL;
    world.environment.current_thread_id = 77U;
    world.environment.supported_image = true;
    world.environment.gameplay_params_container_readable = true;
    world.environment.game_di_readable = true;
    world.environment.global_owner_slots_readable = true;
    world.environment.inventory_scan_complete = true;
    player::PlayerGameplayParamsSnapshotOperations snapshots = {};
    snapshots.read_environment = &ReadEnvironment;
    snapshots.environment_layout_validated = true;
    const player::PlayerGameplayParamsMutationOperations mutations =
        MutationOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, &snapshots, &mutations, &world);
    CHECK(player::BeginPlayerStateInventoryCoverage(
        &adapter, true, true, true));
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_kind ==
          player::PlayerStartupBaselineKind::Absent);
    CHECK(world.snapshot_calls == 0U);
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::UntouchedAbsent);
    CHECK(world.snapshot_calls == 0U);

    player::PlayerGameplayParamsAbsentEvidence evidence = {};
    CHECK(player::CapturePlayerGameplayParamsAbsentEvidence(
        &adapter, &evidence));
    CHECK(evidence.complete);
    CHECK(evidence.live_state_count == 0U);
    CHECK(evidence.inventory_revision ==
          adapter.baseline_inventory_revision);
    CHECK(evidence.environment.gameplay_cache_fingerprint ==
          adapter.baseline_gameplay_cache_fingerprint);
    return true;
}

bool TestNonemptyBaselineStillRequiresPerStateSnapshots() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutations =
        MutationOperations();
    CHECK(PrepareWorldInventory(&world, &adapter, &mutations));
    adapter.snapshot_operations.snapshot_state = nullptr;
    adapter.snapshot_operations.state_snapshot_layout_validated = false;
    CHECK(!CaptureBaseline(&adapter));
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::OperationsIncomplete);
    CHECK(!adapter.baseline_captured);
    CHECK(world.snapshot_calls == 0U);
    return true;
}

bool TestAbsentBaselineRevisionAndObserverLossStayIncomplete() noexcept
{
    FakeWorld revision_world = {};
    player::PlayerGameplayParamsRestartAdapter revision_adapter = {};
    CHECK(SetupAbsent(&revision_world, &revision_adapter));
    revision_world.mutate_inventory_on_environment_read =
        revision_world.environment_read_calls + 1U;
    CHECK(AssessPlayerTouch(&revision_adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);

    FakeWorld loss_world = {};
    player::PlayerGameplayParamsRestartAdapter loss_adapter = {};
    CHECK(SetupAbsent(&loss_world, &loss_adapter));
    player::MarkPlayerStateInventoryLoss(&loss_adapter);
    CHECK(AssessPlayerTouch(&loss_adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(loss_adapter.last_failure ==
          player::PlayerRestartFailure::InventoryLoss);
    return true;
}

void ApplyCustomProvider(FakeWorld* world) noexcept
{
    ++world->environment.cache_generation;
    for (std::uint32_t index = 0; index < world->state_count; ++index) {
        FakeState& state = world->states[index];
        player::PlayerStateRestartSnapshot custom = MakeSnapshot(
            state.pointer, state.generation, index + 1U,
            world->environment.cache_generation, false);
        for (std::uint32_t row = 0;
             row < player::kPlayerLogicalTypeCount; ++row) {
            custom.rows[row].earned_experience =
                state.current.rows[row].earned_experience;
            custom.rows[row].current_level =
                state.current.rows[row].current_level;
        }
        custom.player_variables = state.current.player_variables;
        custom.progression_selector_fingerprint =
            state.current.progression_selector_fingerprint;
        custom.class_selector_fingerprint =
            state.current.class_selector_fingerprint;
        custom.skill_membership_fingerprint =
            state.current.skill_membership_fingerprint;
        custom.regular_buff_fingerprint =
            state.current.regular_buff_fingerprint;
        custom.timed_buff_fingerprint =
            state.current.timed_buff_fingerprint;
        custom.inventory_fingerprint = state.current.inventory_fingerprint;
        custom.equipment_fingerprint = state.current.equipment_fingerprint;
        custom.quest_fingerprint = state.current.quest_fingerprint;
        for (std::uint32_t property = 0;
             property < player::kRequiredPlayerPropertyCount; ++property) {
            if (IsUnchangedFovControl(
                    custom.properties[property].property_id))
                custom.properties[property].value_bits =
                    state.baseline.properties[property].value_bits;
        }
        state.current = custom;
    }
}

void ClearOptionalProgressionMetadata(
    player::PlayerStateRestartSnapshot* snapshot) noexcept
{
    if (!snapshot) return;
    std::memset(&snapshot->appearance_provider, 0,
                sizeof(snapshot->appearance_provider));
    for (std::uint32_t index = 0;
         index < player::kPlayerLogicalTypeCount; ++index)
        std::memset(&snapshot->rows[index].provider, 0,
                    sizeof(snapshot->rows[index].provider));
    for (std::uint32_t index = 0;
         index < player::kRequiredPlayerPropertyCount; ++index)
        std::memset(&snapshot->properties[index].provider, 0,
                    sizeof(snapshot->properties[index].provider));
}

void SetMembershipEvidence(
    player::PlayerStateRestartSnapshot* snapshot,
    player::PlayerCacheMembershipEvidence evidence) noexcept
{
    if (!snapshot) return;
    for (std::uint32_t index = 0;
         index < player::kPlayerLogicalTypeCount; ++index) {
        player::PlayerDefinitionRowSnapshot& row =
            snapshot->rows[index];
        row.cache_membership_evidence = evidence;
        row.member_of_live_cache =
            evidence ==
            player::PlayerCacheMembershipEvidence::ProvenMember;
        row.cache_observer_available =
            evidence !=
            player::PlayerCacheMembershipEvidence::Unobserved;
        row.cache_inventory_complete =
            evidence !=
            player::PlayerCacheMembershipEvidence::Unobserved;
        row.cache_generation_observed =
            evidence !=
            player::PlayerCacheMembershipEvidence::Unobserved;
        row.membership_observation_sequence =
            evidence ==
                    player::PlayerCacheMembershipEvidence::Unobserved
                ? 0U : 2000U + index;
        if (evidence ==
            player::PlayerCacheMembershipEvidence::Unobserved)
            row.cache_generation = 0;
    }
}

void RestoreCurrentPlayerSnapshots(FakeWorld* world) noexcept
{
    if (!world) return;
    for (std::uint32_t index = 0; index < world->state_count; ++index)
        world->states[index].current = world->states[index].baseline;
}

player::PlayerGameplayParamsTouchAssessment AssessPlayerTouch(
    player::PlayerGameplayParamsRestartAdapter* adapter) noexcept
{
    const cold::RestartContext context = TransactionContext();
    return player::AssessPlayerGameplayParamsTouch(adapter, &context);
}

player::PlayerGameplayParamsTouchAssessment AssessPlayerTouchWithAuthority(
    player::PlayerGameplayParamsRestartAdapter* adapter,
    const cold::RestartContext& context,
    const player::PlayerThreadAuthorityEvidence& authority) noexcept
{
    return player::AssessPlayerGameplayParamsTouch(
        adapter, &context, &authority);
}

bool RunThroughQuiesce(
    FakeWorld* world,
    player::PlayerGameplayParamsRestartAdapter* adapter,
    cold::RestartContext* context) noexcept
{
    ApplyCustomProvider(world);
    *context = TransactionContext();
    return player::PreflightPlayerGameplayParamsRestart(
               adapter, context) == cold::DomainResult::Succeeded &&
        player::QuiescePlayerGameplayParamsRestart(
               adapter, context) == cold::DomainResult::Succeeded;
}

bool TestSnapshotOnlyBaselineDoesNotRequireMutationOperations() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 2U);
    CHECK(adapter.baseline_kind ==
          player::PlayerStartupBaselineKind::Snapshot);
    CHECK(adapter.b0_diagnostics.pass1_owner_count == 2U);
    CHECK(adapter.b0_diagnostics.pass2_owner_count == 2U);
    CHECK(adapter.b0_diagnostics.owner_set_stable_across_passes);
    CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
    CHECK(adapter.b0_diagnostics.player_restart_snapshot_complete);
    CHECK(adapter.b0_diagnostics.states[0].owner_kind ==
          player::PlayerStateOwnerKind::LocalProcessGlobal);
    CHECK(adapter.b0_diagnostics.states[1].owner_kind ==
          player::PlayerStateOwnerKind::CharacterManagerGlobal);
    CHECK(adapter.b0_diagnostics.states[0].generation == 1U);
    CHECK(adapter.b0_diagnostics.states[1].generation == 2U);
    CHECK(adapter.snapshot_operations.read_environment == &ReadEnvironment);
    CHECK(adapter.snapshot_operations.snapshot_state == &SnapshotState);
    CHECK(adapter.mutation_operations.reset_global_cache == nullptr);
    CHECK(adapter.mutation_operations.initialize_variables == nullptr);
    CHECK(!adapter.mutation_operations.production_enabled);
    CHECK(world.environment_read_calls == 2U);
    CHECK(world.snapshot_calls == 4U);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    for (std::uint32_t state = 0;
         state < adapter.baseline_state_count; ++state) {
        for (std::uint32_t property = 0;
             property < player::kRequiredPlayerPropertyCount; ++property) {
            CHECK(adapter.baseline_states[state].properties[property].available);
            CHECK(adapter.baseline_states[state].properties[property].property_id ==
                  kPropertyIds[property]);
        }
    }
    return true;
}

bool TestSemanticBaselineCanBeReadyWithoutMutationOwnership() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutation =
        MutationOperations();
    CHECK(PrepareWorldInventory(&world, &adapter, &mutation));
    for (std::uint32_t index = 0; index < world.state_count; ++index) {
        player::PlayerStateRestartSnapshot& snapshot =
            world.states[index].current;
        snapshot.player_restart_snapshot_complete = false;
        snapshot.inventory_fingerprint = 0U;
        snapshot.equipment_fingerprint = 0U;
        snapshot.quest_fingerprint = 0U;
        ClearOptionalProgressionMetadata(&snapshot);
        world.states[index].baseline = snapshot;
    }
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_kind ==
          player::PlayerStartupBaselineKind::Snapshot);
    CHECK(adapter.b0_diagnostics.pass1_owner_count == 2U);
    CHECK(adapter.b0_diagnostics.pass2_owner_count == 2U);
    CHECK(adapter.b0_diagnostics.owner_set_stable_across_passes);
    CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
    CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              UntouchedSnapshot);
    CHECK(adapter.post_destroy_diagnostics.
          semantic_state_pair_stable);
    CHECK(!adapter.post_destroy_diagnostics.states[0].
          pass1_restart_snapshot_complete);
    CHECK(!adapter.post_destroy_diagnostics.states[0].
          pass2_restart_snapshot_complete);
    CHECK(!adapter.transaction_active);
    CHECK(!adapter.mutation_started);

    ++world.states[0].current.rows[0].earned_experience;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              TouchedDefinitionDelta);
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
              &adapter, &context) == cold::DomainResult::Failed);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::RestartProviderUnproven);
    CHECK(!adapter.transaction_active);
    CHECK(!adapter.mutation_started);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    return true;
}

bool TestObservedZeroSourceRegistryGenerationIsValid() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
    for (std::uint32_t state = 0; state < world.state_count; ++state) {
        player::PlayerStateRestartSnapshot& snapshot =
            world.states[state].current;
        snapshot.appearance_provider.source_registry_generation = 0;
        CHECK(snapshot.appearance_provider.
              source_registry_generation_observed);
        for (std::uint32_t row = 0;
             row < player::kPlayerLogicalTypeCount; ++row) {
            snapshot.rows[row].provider.source_registry_generation = 0;
            CHECK(snapshot.rows[row].provider.
                  source_registry_generation_observed);
        }
        for (std::uint32_t property = 0;
             property < player::kRequiredPlayerPropertyCount; ++property) {
            snapshot.properties[property].provider.
                source_registry_generation = 0;
            CHECK(snapshot.properties[property].provider.
                  source_registry_generation_observed);
        }
    }
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == world.state_count);
    CHECK(adapter.baseline_states[0].appearance_provider.
          source_registry_generation == 0);
    return true;
}

bool TestProductionDisabledMutationRunsNoCallbacks() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutation_operations =
        MutationOperations(false);
    CHECK(PrepareWorldInventory(
        &world, &adapter, &mutation_operations));
    CHECK(CaptureBaseline(&adapter));
    const std::uint32_t environment_reads_before =
        world.environment_read_calls;
    const std::uint32_t snapshots_before = world.snapshot_calls;
    ApplyCustomProvider(&world);
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
              &adapter, &context) == cold::DomainResult::Failed);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::MutationProductionDisabled);
    CHECK(!adapter.transaction_active);
    CHECK(!adapter.mutation_started);
    CHECK(adapter.total_global_reset_calls == 0U);
    CHECK(adapter.total_initialize_calls == 0U);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    CHECK(world.environment_read_calls == environment_reads_before);
    CHECK(world.snapshot_calls == snapshots_before);
    return true;
}

bool TestSnapshotStableWindowMismatchIsRejectedAndRetryable() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
    world.change_cache_generation_after_snapshot_window = true;
    CHECK(!CaptureBaseline(&adapter));
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::InventoryChanged);
    CHECK(!adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 0U);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);

    world.change_cache_generation_after_snapshot_window = false;
    world.environment.cache_generation = 1U;
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 2U);
    CHECK(world.environment_read_calls == 4U);
    CHECK(world.snapshot_calls == 6U);
    return true;
}

bool TestDynamicPlayerDiStateParticipatesInCompleteBaseline() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));

    const std::uintptr_t pointer = 0x300000U;
    const std::uint64_t generation =
        player::ObservePlayerStateConstructed(&adapter, pointer);
    CHECK(generation != 0U);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, pointer, generation,
        player::PlayerStateOwnerKind::PlayerDi, 0x310000U));
    FakeState& dynamic = world.states[world.state_count++];
    dynamic.pointer = pointer;
    dynamic.generation = generation;
    dynamic.baseline = MakeSnapshot(
        pointer, generation, 3U, world.environment.cache_generation, true);
    dynamic.current = dynamic.baseline;

    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 3U);
    CHECK(world.snapshot_calls == 6U);
    CHECK(world.reset_calls == 0U);
    CHECK(dynamic.initialize_calls == 0U);
    return true;
}

bool TestSemanticBaselineRejectsMissingDataButAcceptsRestartOnlyGaps()
    noexcept
{
    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(player::ObservePlayerStateConstructed(
                  &adapter, 0x320000U) != 0U);
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::StateUnclassified);
        CHECK(!adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 0U);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.properties[0].property_id = 0U;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::RequiredPropertyMissing);
        CHECK(!adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 0U);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.properties[0].provider.observed = false;
        CHECK(CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::None);
        CHECK(adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 2U);
        CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
        CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.properties[0].provider.content_epoch = 7U;
        CHECK(CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::None);
        CHECK(adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 2U);
        CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
        CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        adapter.inventory[0].owner_object = 0U;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::StateUnclassified);
        CHECK(!adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 0U);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.progression_selector_fingerprint = 0U;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::StateSnapshotIncomplete);
        CHECK(!adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 0U);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.properties[0].provider
            .source_inventory_ordinal = 0U;
        CHECK(CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::None);
        CHECK(adapter.baseline_captured);
        CHECK(adapter.baseline_state_count == 2U);
        CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
        CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
        CHECK(world.reset_calls == 0U);
    }
    return true;
}

bool TestRuntime12SemanticMembershipEvidenceSplit() noexcept
{
    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        for (std::uint32_t state = 0;
             state < world.state_count; ++state) {
            player::PlayerStateRestartSnapshot& snapshot =
                world.states[state].current;
            SetMembershipEvidence(
                &snapshot,
                player::PlayerCacheMembershipEvidence::Unobserved);
            snapshot.player_restart_snapshot_complete = false;
            snapshot.inventory_fingerprint = 0;
            snapshot.equipment_fingerprint = 0;
            snapshot.quest_fingerprint = 0;
            snapshot.restart_provenance_reason_mask =
                player::kPlayerWindowsRestartProvenanceReasonMask;
            ClearOptionalProgressionMetadata(&snapshot);
            for (std::uint32_t property = 0;
                 property < player::kRequiredPlayerPropertyCount;
                 ++property)
                snapshot.properties[property].cache_generation = 0;
        }
        CHECK(CaptureBaseline(&adapter));
        CHECK(adapter.baseline_kind ==
              player::PlayerStartupBaselineKind::Snapshot);
        CHECK(adapter.baseline_state_count == 2U);
        CHECK(adapter.b0_diagnostics.pass1_owner_count == 2U);
        CHECK(adapter.b0_diagnostics.pass2_owner_count == 2U);
        CHECK(adapter.b0_diagnostics.state_snapshot_count == 2U);
        CHECK(adapter.b0_diagnostics.owner_set_stable_across_passes);
        CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
        CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
        CHECK(world.snapshot_calls == 4U);
        for (std::uint32_t state = 0; state < 2U; ++state) {
            const auto& diagnostics =
                adapter.b0_diagnostics.states[state];
            CHECK(diagnostics.snapshot_attempt_count == 2U);
            CHECK(diagnostics.pass1_semantic_snapshot_complete);
            CHECK(diagnostics.pass2_semantic_snapshot_complete);
            CHECK(!diagnostics.pass1_restart_snapshot_complete);
            CHECK(!diagnostics.pass2_restart_snapshot_complete);
            CHECK(diagnostics.pass1_semantic_reason_mask == 0U);
            CHECK(diagnostics.pass2_semantic_reason_mask == 0U);
            CHECK((diagnostics.pass1_restart_provenance_reason_mask &
                   player::
                       kPlayerRestartProvenanceReasonCacheMembershipUnproven) !=
                  0U);
            CHECK((diagnostics.pass1_restart_provenance_reason_mask &
                   player::kPlayerWindowsRestartProvenanceReasonMask) ==
                  player::kPlayerWindowsRestartProvenanceReasonMask);
            CHECK(diagnostics.pass1_failure_stage ==
                  player::PlayerStateSnapshotFailureStage::None);
            CHECK(diagnostics.pass2_failure_stage ==
                  player::PlayerStateSnapshotFailureStage::None);
            CHECK(diagnostics.pass1_restart_failure_stage ==
                  player::PlayerStateSnapshotFailureStage::
                      RestartCacheMembershipUnproven);
            CHECK(diagnostics.pass2_restart_failure_stage ==
                  player::PlayerStateSnapshotFailureStage::
                      RestartCacheMembershipUnproven);
            for (std::uint32_t row = 0;
                 row < player::kPlayerLogicalTypeCount; ++row) {
                const auto& first =
                    diagnostics.pass1_semantic_rows[row];
                const auto& second =
                    diagnostics.pass2_semantic_rows[row];
                CHECK(first.cache_membership_evidence ==
                      player::PlayerCacheMembershipEvidence::Unobserved);
                CHECK(second.cache_membership_evidence ==
                      player::PlayerCacheMembershipEvidence::Unobserved);
                CHECK(!first.member_of_live_cache);
                CHECK(!first.cache_generation_observed);
                CHECK(!first.cache_observer_available);
                CHECK(!first.cache_inventory_complete);
                CHECK(first.membership_observation_sequence == 0U);
                CHECK(first.definition_fingerprint ==
                      second.definition_fingerprint);
            }
        }
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        for (std::uint32_t state = 0;
             state < world.state_count; ++state)
            SetMembershipEvidence(
                &world.states[state].current,
                player::PlayerCacheMembershipEvidence::
                    ProvenNonMember);
        CHECK(CaptureBaseline(&adapter));
        CHECK(adapter.b0_diagnostics.player_semantic_snapshot_complete);
        CHECK(!adapter.b0_diagnostics.player_restart_snapshot_complete);
        CHECK(adapter.b0_diagnostics.states[0].
                  pass2_semantic_rows[0].cache_membership_evidence ==
              player::PlayerCacheMembershipEvidence::ProvenNonMember);
        CHECK(adapter.b0_diagnostics.states[0].
                  pass2_restart_failure_stage ==
              player::PlayerStateSnapshotFailureStage::
                  RestartCacheMembershipUnproven);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.rows[0].cache_membership_evidence =
            player::PlayerCacheMembershipEvidence::Conflict;
        world.states[0].current.rows[0].member_of_live_cache = false;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::CacheMembershipConflict);
        CHECK(adapter.b0_diagnostics.states[0].failure_stage ==
              player::PlayerStateSnapshotFailureStage::
                  CacheMembershipConflict);
        CHECK((adapter.b0_diagnostics.states[0].semantic_reason_mask &
               player::kPlayerSemanticReasonCacheMembershipConflict) !=
              0U);
        CHECK(!adapter.baseline_captured);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.states[0].current.properties[1].property_id =
            world.states[0].current.properties[0].property_id;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::RequiredPropertyDuplicate);
        CHECK(adapter.b0_diagnostics.states[0].failure_stage ==
              player::PlayerStateSnapshotFailureStage::
                  SemanticPropertyInvalid);
        CHECK((adapter.b0_diagnostics.states[0].semantic_reason_mask &
               player::kPlayerSemanticReasonPropertyInvalid) != 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        world.semantic_mismatch_on_pass2 = true;
        CHECK(!CaptureBaseline(&adapter));
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::InventoryChanged);
        CHECK(adapter.b0_diagnostics.states[0].failure_stage ==
              player::PlayerStateSnapshotFailureStage::
                  StateChangedBetweenPasses);
        CHECK(adapter.b0_diagnostics.states[0].snapshot_attempt_count == 2U);
        CHECK(!adapter.baseline_captured);
    }
    return true;
}

bool TestInventoryRevisionChangeDuringBaselineIsRejectedAndRetryable()
    noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
    world.change_inventory_revision_after_snapshot_window = true;
    CHECK(!CaptureBaseline(&adapter));
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::InventoryChanged);
    CHECK(!adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 0U);
    CHECK(world.reset_calls == 0U);

    const std::uint64_t injected_generation =
        player::FindLivePlayerStateGeneration(&adapter, 0x7F0000U);
    CHECK(injected_generation != 0U);
    std::uint64_t destroyed_generation = 0;
    CHECK(player::ObservePlayerStateDestroyed(
        &adapter, 0x7F0000U, &destroyed_generation));
    CHECK(destroyed_generation == injected_generation);
    world.change_inventory_revision_after_snapshot_window = false;
    world.snapshot_calls = 0U;
    CHECK(CaptureBaseline(&adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 2U);
    return true;
}

bool TestVerifiedRestartIsOneSynchronousMutation() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(&world, &adapter, &context));
    CHECK(player::ResetPlayerGameplayParams(
              &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(player::ReloadPlayerGameplayParams(
              &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(player::VerifyPlayerGameplayParamsRestart(
              &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(world.reset_calls == 1U);
    CHECK(adapter.transaction_global_reset_calls == 1U);
    CHECK(adapter.transaction_initialize_calls == 2U);
    CHECK(world.states[0].initialize_calls == 1U);
    CHECK(world.states[1].initialize_calls == 1U);
    CHECK(world.order_count >= 6U);
    CHECK(world.order[0] == 100U);
    CHECK(world.order[1] == 201U);
    CHECK(world.order[2] == 202U);
    CHECK(world.order[3] == 300U);
    CHECK(world.order[4] == 400U);
    CHECK(adapter.after_cache_generation == 3U);
    CHECK(adapter.completed_transaction_generation == 9U);
    CHECK(adapter.verified);
    return true;
}

bool TestRegistryBackedTouchedPlayerTransactionReachesVerified() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter player_adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutation =
        MutationOperations();
    CHECK(PrepareWorldInventory(
        &world, &player_adapter, &mutation));

    RegistryHarnessWorld harness = {};
    harness.player_world = &world;
    cold::InitializeRegistry(&harness.registry);

    cold::ContentDomainAdapter vfs = RegistryPassAdapter(
        cold::ContentDomainId::VfsSources, "VfsSources", 0, true,
        true, &harness);
    vfs.reset = &RegistryVfsReset;
    cold::ContentDomainAdapter player_adapter_definition =
        player::MakePlayerGameplayParamsContentDomainAdapter(
            &player_adapter,
            cold::DomainBit(cold::ContentDomainId::VfsSources), false);
    player_adapter_definition.verify = &RegistryPlayerVerify;
    cold::ContentDomainAdapter frontend = RegistryPassAdapter(
        cold::ContentDomainId::FrontendVerification,
        "FrontendVerification",
        cold::DomainBit(cold::ContentDomainId::VfsSources), true,
        false, &harness);
    frontend.verify = &RegistryFrontendVerify;

    CHECK(cold::RegisterAdapter(&harness.registry, &vfs));
    CHECK(cold::RegisterAdapter(
        &harness.registry, &player_adapter_definition));
    CHECK(cold::RegisterAdapter(&harness.registry, &frontend));
    CHECK(cold::SealRegistry(&harness.registry));

    const cold::RestartContext baseline = BaselineContext();
    CHECK(cold::CaptureRegistryBaseline(
              &harness.registry, &baseline) ==
          cold::RestartResult::Verified);
    CHECK(player_adapter.baseline_captured);
    CHECK(!player_adapter_definition.mandatory);

    ApplyCustomProvider(&world);
    CHECK(cold::MarkDomainTouched(
        &harness.registry,
        cold::ContentDomainId::PlayerGameplayParams));
    const cold::RestartContext transaction = TransactionContext();
    CHECK(cold::RunRegistryTransaction(
              &harness.registry, &transaction) ==
          cold::RestartResult::Verified);

    CHECK(harness.vfs_reset_calls == 1U);
    CHECK(world.reset_calls == 1U);
    CHECK(world.states[0].initialize_calls == 1U);
    CHECK(world.states[1].initialize_calls == 1U);
    CHECK(harness.frontend_verify_calls == 1U);
    CHECK(player_adapter.verified);
    CHECK(harness.registry.last_result == cold::RestartResult::Verified);
    CHECK(harness.registry.phase == cold::RestartPhase::Verified);

    const std::uint32_t vfs_reset = FindOrder(world, 50U);
    const std::uint32_t player_reset = FindOrder(world, 100U);
    const std::uint32_t state_zero_initialize = FindOrder(world, 201U);
    const std::uint32_t state_one_initialize = FindOrder(world, 202U);
    const std::uint32_t player_verify = FindOrder(world, 500U);
    const std::uint32_t frontend_verify = FindOrder(world, 600U);
    CHECK(vfs_reset != UINT32_MAX);
    CHECK(vfs_reset < player_reset);
    CHECK(player_reset < state_zero_initialize);
    CHECK(state_zero_initialize < state_one_initialize);
    CHECK(state_one_initialize < player_verify);
    CHECK(player_verify < frontend_verify);
    return true;
}

bool TestPlayerTouchAssessmentUsesDirectB1VsB0WithoutProgressionHooks()
    noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutation =
        MutationOperations();
    CHECK(PrepareWorldInventory(&world, &adapter, &mutation));
    adapter.snapshot_operations.
        allow_unobserved_progression_trace_metadata = true;
    for (std::uint32_t index = 0; index < world.state_count; ++index) {
        ClearOptionalProgressionMetadata(&world.states[index].baseline);
        world.states[index].current = world.states[index].baseline;
    }
    CHECK(CaptureBaseline(&adapter));

    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Untouched);

    world.states[0].current.progression_selector_fingerprint ^= 1U;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Touched);
    RestoreCurrentPlayerSnapshots(&world);

    world.states[0].current.rows[0].definition_fingerprint ^= 1U;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Touched);
    RestoreCurrentPlayerSnapshots(&world);

    world.states[0].current.properties[0].value_bits ^= 1U;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Touched);
    RestoreCurrentPlayerSnapshots(&world);

    world.states[0].current.rows[0].provider = Provider(false, 91U);
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              UntouchedSnapshot);
    RestoreCurrentPlayerSnapshots(&world);

    world.states[0].current.appearance_fingerprint ^= 1U;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Touched);
    RestoreCurrentPlayerSnapshots(&world);

    world.states[0].current.appearance_provider = Provider(false, 92U);
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::
              UntouchedSnapshot);
    RestoreCurrentPlayerSnapshots(&world);

    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Untouched);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    return true;
}

bool TestRuntime14PostDestroySemanticCaptureContract() noexcept
{
    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        const std::uint32_t reads_before =
            world.environment_read_calls;
        const std::uint32_t snapshots_before = world.snapshot_calls;
        CHECK(!world.environment.active_session);
        CHECK(!world.environment.active_level);
        CHECK(!world.environment.local_player_di);
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  UntouchedSnapshot);
        CHECK(world.environment_read_calls == reads_before + 2U);
        CHECK(world.post_destroy_semantic_environment_reads == 2U);
        CHECK(world.snapshot_calls == snapshots_before + 4U);
        CHECK(world.last_environment_purpose ==
              player::PlayerEnvironmentReadPurpose::
                  PostDestroySemanticAssessment);
        const auto& diagnostics = adapter.post_destroy_diagnostics;
        CHECK(diagnostics.assessment_attempt_count == 1U);
        CHECK(diagnostics.content_epoch == 7U);
        CHECK(diagnostics.owner_thread_id == 77U);
        CHECK(diagnostics.pass1.environment_callback_attempted);
        CHECK(diagnostics.pass1.environment_callback_returned);
        CHECK(diagnostics.pass2.environment_callback_attempted);
        CHECK(diagnostics.pass2.environment_callback_returned);
        CHECK(diagnostics.pass1.owner_count == 2U);
        CHECK(diagnostics.pass2.owner_count == 2U);
        CHECK(diagnostics.environment_pair_stable);
        CHECK(diagnostics.inventory_pair_stable);
        CHECK(diagnostics.owner_set_matches_baseline_pass1);
        CHECK(diagnostics.owner_set_matches_baseline_pass2);
        CHECK(diagnostics.semantic_state_pair_stable);
        CHECK(diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::None);
        CHECK(diagnostics.states[0].pass1_callback_returned);
        CHECK(diagnostics.states[0].pass2_callback_returned);
        CHECK(!adapter.mutation_operations.production_enabled);
        CHECK(!adapter.mutation_started);
        CHECK(world.reset_calls == 0U);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.reject_environment_on_call =
            world.environment_read_calls + 1U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  EnvironmentCallbackRejected);
        CHECK(adapter.post_destroy_diagnostics.pass1.
              environment_callback_attempted);
        CHECK(!adapter.post_destroy_diagnostics.pass1.
              environment_callback_returned);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.global_owner_slots_readable = false;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  GlobalOwnerSlotsUnreadable);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.inventory_scan_complete = false;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  InventoryScanIncomplete);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.gameplay_params_container = 0;
        world.environment.gameplay_params_container_readable = false;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::
                  GameplayContainerUnavailable);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  GameplayContainerUnreadable);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.game_di = 0;
        world.environment.game_di_readable = false;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::GameDiUnavailable);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  GameDiUnreadable);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.active_session = 0xA00000U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  ActiveSessionStillPresent);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.environment.active_level = 0xB00000U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  ActiveLevelStillPresent);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        ++world.states[0].current.properties[0].value_bits;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedDefinitionDelta);
        CHECK(adapter.post_destroy_diagnostics.
              semantic_state_pair_stable);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        ++world.states[0].current.rows[0].definition_count;
        ++world.states[0].current.rows[0].definition_fingerprint;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedDefinitionDelta);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        adapter.inventory[0].owner_object ^= 0x10U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedPersistentOwner);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        ++adapter.inventory[0].generation;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedPersistentOwner);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        adapter.inventory[0].pointer ^= 0x1000U;
        world.environment.local_process_global_state =
            adapter.inventory[0].pointer;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedPersistentOwner);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        adapter.inventory[0].owner_kind =
            player::PlayerStateOwnerKind::OtherRttiObserved;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::
                  TouchedPersistentOwner);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.reject_snapshot_on_call = world.snapshot_calls + 1U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        const auto& diagnostics = adapter.post_destroy_diagnostics;
        CHECK(diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  StateSnapshotCallbackRejected);
        CHECK(diagnostics.first_failed_owner_index == 0U);
        CHECK(diagnostics.first_failed_owner_kind ==
              player::PlayerStateOwnerKind::LocalProcessGlobal);
        CHECK(diagnostics.first_failed_row == 3U);
        CHECK(diagnostics.states[0].pass1_rows[3].raw_failure_reason ==
              0x4000U);
        CHECK(diagnostics.states[0].pass1_rows[3].
              definition_traversal_failure ==
              player::PlayerDefinitionTraversalFailure::
                  PropertySpanUnreadable);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.reject_snapshot_on_call =
            world.snapshot_calls + world.state_count + 1U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        const auto& diagnostics = adapter.post_destroy_diagnostics;
        CHECK(diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  StateSnapshotCallbackRejected);
        CHECK(diagnostics.first_failed_owner_index == 0U);
        CHECK(diagnostics.states[0].pass1_callback_returned);
        CHECK(diagnostics.states[0].pass2_callback_attempted);
        CHECK(!diagnostics.states[0].pass2_callback_returned);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.states[0].current.rows[2].semantic_complete = false;
        world.states[0].current.failure_stage =
            player::PlayerStateSnapshotFailureStage::
                RowDefinitionTraversalIncomplete;
        world.states[0].current.failed_row_index = 2U;
        world.states[0].current.semantic_reason_mask =
            player::kPlayerSemanticReasonRowInvalid;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  StateSnapshotSemanticIncomplete);
        CHECK(adapter.post_destroy_diagnostics.first_failed_row == 2U);
        CHECK(adapter.post_destroy_diagnostics.
              first_failed_state_stage ==
              player::PlayerStateSnapshotFailureStage::
                  RowDefinitionTraversalIncomplete);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.states[0].current.properties[0].available = false;
        world.states[0].current.failure_stage =
            player::PlayerStateSnapshotFailureStage::
                SemanticPropertyInvalid;
        world.states[0].current.failed_property_id =
            kPropertyIds[0];
        world.states[0].current.semantic_reason_mask =
            player::kPlayerSemanticReasonPropertyInvalid;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.
              first_failed_property_id == kPropertyIds[0]);
        CHECK(adapter.post_destroy_diagnostics.
              first_failed_state_stage ==
              player::PlayerStateSnapshotFailureStage::
                  SemanticPropertyInvalid);
    }

    {
        FakeWorld world = {};
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
        CHECK(CaptureBaseline(&adapter));
        world.semantic_mismatch_on_snapshot_call =
            world.snapshot_calls + world.state_count + 1U;
        CHECK(AssessPlayerTouch(&adapter) ==
              player::PlayerGameplayParamsTouchAssessment::Incomplete);
        CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
              player::PlayerPostDestroyExactFailure::
                  StateChangedBetweenPasses);
        CHECK(!adapter.post_destroy_diagnostics.
              semantic_state_pair_stable);
        CHECK(adapter.post_destroy_diagnostics.
              first_failed_state_stage ==
              player::PlayerStateSnapshotFailureStage::
                  StateChangedBetweenPasses);
    }

    return true;
}

bool TestRuntime16LifecycleLocalThreadAuthority() noexcept
{
    constexpr std::uint32_t kB0Thread = 29600U;
    constexpr std::uint32_t kExitThread = 33524U;
    constexpr std::uint32_t kWindowThread = 11111U;
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(PrepareWorldInventory(&world, &adapter, nullptr));
    world.environment.current_thread_id = kB0Thread;
    cold::RestartContext baseline = BaselineContext();
    baseline.owner_thread_id = kB0Thread;
    CHECK(player::CapturePlayerGameplayParamsBaseline(
              &adapter, &baseline) ==
          cold::DomainResult::Succeeded);
    CHECK(adapter.baseline_owner_thread_id == kB0Thread);
    CHECK(world.snapshot_calls == 4U);
    CHECK(world.observed_post_destroy_pass_count == 0U);
    for (std::uint32_t index = 0; index < 4U; ++index)
        CHECK(world.snapshot_observed_threads[index] == kB0Thread);

    world.environment.current_thread_id = kExitThread;
    cold::RestartContext exit = TransactionContext();
    exit.owner_thread_id = kExitThread;
    player::PlayerThreadAuthorityEvidence authority = {};
    authority.kind =
        player::PlayerThreadAuthorityKind::C10PostReturnOwner;
    authority.expected_thread_id = kExitThread;
    authority.historical_b0_thread_id = kB0Thread;
    authority.c08_callback_thread_id = kB0Thread + 100U;
    authority.current_c10_thread_id = kExitThread;
    authority.current_session_scope_thread_id = kExitThread;
    authority.post_c10_observed_thread_id = kExitThread;
    authority.pending_post_c10_token_generation = 17U;
    authority.exact_scope_active = true;
    const std::uint32_t snapshots_before = world.snapshot_calls;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::
              UntouchedSnapshot);
    const auto& accepted = adapter.post_destroy_diagnostics;
    CHECK(accepted.thread_authority_kind ==
          player::PlayerThreadAuthorityKind::C10PostReturnOwner);
    CHECK(accepted.historical_b0_thread_id == kB0Thread);
    CHECK(accepted.c08_callback_thread_id == kB0Thread + 100U);
    CHECK(accepted.current_c10_thread_id == kExitThread);
    CHECK(accepted.current_session_scope_thread_id == kExitThread);
    CHECK(accepted.post_c10_observed_thread_id == kExitThread);
    CHECK(accepted.pending_post_c10_token_generation == 17U);
    CHECK(accepted.pass1.environment_observed_thread == kExitThread);
    CHECK(accepted.pass1.environment_expected_thread == kExitThread);
    CHECK(accepted.pass1.thread_match);
    CHECK(accepted.pass2.environment_observed_thread == kExitThread);
    CHECK(accepted.pass2.environment_expected_thread == kExitThread);
    CHECK(accepted.pass2.thread_match);
    CHECK(world.observed_post_destroy_pass_count == 2U);
    CHECK(world.observed_post_destroy_passes[0] == 1U);
    CHECK(world.observed_post_destroy_passes[1] == 2U);
    CHECK(world.observed_post_destroy_owner_counts[0] == 2U);
    CHECK(world.observed_post_destroy_owner_counts[1] == 2U);
    CHECK(world.observed_post_destroy_threads[0] == kExitThread);
    CHECK(world.observed_post_destroy_threads[1] == kExitThread);
    CHECK(world.observed_post_destroy_inventory_revisions[0] ==
          world.observed_post_destroy_inventory_revisions[1]);
    CHECK(world.observed_post_destroy_first_owners[0] ==
          world.states[0].pointer);
    CHECK(world.observed_post_destroy_first_owners[1] ==
          world.states[0].pointer);
    CHECK(world.observed_post_destroy_first_states[0] ==
          world.states[0].pointer);
    CHECK(world.observed_post_destroy_first_states[1] ==
          world.states[0].pointer);
    CHECK(world.snapshot_calls == snapshots_before + 4U);
    for (std::uint32_t index = snapshots_before;
         index < world.snapshot_calls; ++index)
        CHECK(world.snapshot_observed_threads[index] == kExitThread);
    CHECK(!adapter.mutation_operations.production_enabled);
    CHECK(!adapter.mutation_started);
    CHECK(world.reset_calls == 0U);

    ++world.states[0].current.properties[0].value_bits;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::
              TouchedDefinitionDelta);
    world.states[0].current = world.states[0].baseline;

    world.environment.current_thread_id = kExitThread + 1U;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::ThreadMismatch);
    CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
          player::PlayerPostDestroyExactFailure::ThreadMismatch);
    CHECK(!adapter.post_destroy_diagnostics.pass1.thread_match);
    CHECK(adapter.post_destroy_diagnostics.pass1.
              environment_observed_thread == kExitThread + 1U);
    CHECK(adapter.post_destroy_diagnostics.pass1.
              environment_expected_thread == kExitThread);

    world.environment.current_thread_id = kExitThread;
    authority.current_session_scope_thread_id = kWindowThread;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
          player::PlayerPostDestroyExactFailure::ThreadMismatch);
    CHECK(!adapter.post_destroy_diagnostics.pass1.thread_match);
    CHECK(adapter.post_destroy_diagnostics.
              current_session_scope_thread_id == kWindowThread);

    authority.current_session_scope_thread_id = kExitThread;
    authority.post_c10_observed_thread_id = kWindowThread;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
          player::PlayerPostDestroyExactFailure::ThreadMismatch);

    authority.post_c10_observed_thread_id = kExitThread;
    authority.expected_thread_id = kWindowThread;
    CHECK(AssessPlayerTouchWithAuthority(
              &adapter, exit, authority) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(adapter.post_destroy_diagnostics.final_exact_failure ==
          player::PlayerPostDestroyExactFailure::ThreadMismatch);
    CHECK(adapter.post_destroy_diagnostics.pass1.
              environment_expected_thread == kWindowThread);
    CHECK(world.reset_calls == 0U);
    return true;
}

bool TestIncompleteCurrentPlayerSnapshotIsNotUntouched() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsMutationOperations mutation =
        MutationOperations();
    CHECK(PrepareWorldInventory(&world, &adapter, &mutation));
    CHECK(CaptureBaseline(&adapter));
    world.states[0].current.body_readable = false;
    CHECK(AssessPlayerTouch(&adapter) ==
          player::PlayerGameplayParamsTouchAssessment::Incomplete);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    return true;
}

bool TestPointerReuseGetsNewGeneration() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsSnapshotOperations snapshot_operations =
        SnapshotOperations();
    const player::PlayerGameplayParamsMutationOperations mutation_operations =
        MutationOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, &snapshot_operations, &mutation_operations, &world);
    CHECK(player::BeginPlayerStateInventoryCoverage(
        &adapter, true, true, true));
    const std::uint64_t first =
        player::ObservePlayerStateConstructed(&adapter, 0x1234U);
    CHECK(first == 1U);
    CHECK(player::FindLivePlayerStateGeneration(
        &adapter, 0x1234U) == first);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, 0x1234U, first,
        player::PlayerStateOwnerKind::OtherRttiObserved, 0x90U));
    std::uint64_t destroyed = 0;
    CHECK(player::ObservePlayerStateDestroyed(
        &adapter, 0x1234U, &destroyed));
    CHECK(destroyed == first);
    CHECK(player::FindLivePlayerStateGeneration(
        &adapter, 0x1234U) == 0U);
    const std::uint64_t second =
        player::ObservePlayerStateConstructed(&adapter, 0x1234U);
    CHECK(second == 2U);
    CHECK(second != first);
    CHECK(player::FindLivePlayerStateGeneration(
        &adapter, 0x1234U) == second);
    CHECK(!player::ClassifyPlayerStateOwner(
        &adapter, 0x1234U, first,
        player::PlayerStateOwnerKind::OtherRttiObserved, 0x90U));
    return true;
}

bool TestOwnerClassificationIsIdempotentAndConflictsLatch() noexcept
{
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, nullptr, nullptr, nullptr);
    CHECK(player::BeginPlayerStateInventoryCoverage(
        &adapter, true, true, true));
    const std::uintptr_t state = 0x330000U;
    const std::uintptr_t owner = 0x331000U;
    const std::uint64_t generation =
        player::ObservePlayerStateConstructed(&adapter, state);
    CHECK(generation != 0U);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, state, generation,
        player::PlayerStateOwnerKind::LocalProcessGlobal, owner));
    const std::uint64_t classified_revision = adapter.inventory_revision;
    const std::uint64_t classified_sequence = adapter.observation_sequence;

    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, state, generation,
        player::PlayerStateOwnerKind::LocalProcessGlobal, owner));
    CHECK(adapter.inventory_revision == classified_revision);
    CHECK(adapter.observation_sequence == classified_sequence);
    CHECK(dluma_atomic_compare_exchange(
              &adapter.inventory_loss_latched, 0, 0) == 0);

    CHECK(!player::ClassifyPlayerStateOwner(
        &adapter, state, generation,
        player::PlayerStateOwnerKind::CharacterManagerGlobal,
        owner + 0x100U));
    CHECK(dluma_atomic_compare_exchange(
              &adapter.inventory_loss_latched, 0, 0) != 0);
    CHECK(adapter.inventory_revision == classified_revision);
    CHECK(adapter.observation_sequence == classified_sequence);
    return true;
}

player::PlayerInventoryBootstrapEvidence CompleteBootstrapEvidence() noexcept
{
    player::PlayerInventoryBootstrapEvidence evidence = {};
    evidence.supported_image = true;
    evidence.before_original_game_script_initializer = true;
    evidence.closed_creator_set_validated = true;
    evidence.constructor_route_installed = true;
    evidence.complete_destructor_route_installed = true;
    evidence.before_global_slots_readable = true;
    evidence.after_global_slots_readable = true;
    return evidence;
}

bool TestExactStartupBootstrapAuthorizesCoverage() noexcept
{
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, nullptr, nullptr, nullptr);
    player::PlayerInventoryBootstrapEvidence evidence =
        CompleteBootstrapEvidence();
    CHECK(player::BeginPlayerStateInventoryCoverageFromBootstrap(
        &adapter, &evidence));
    CHECK(adapter.coverage_started);
    CHECK(adapter.coverage_started_before_first_allocation);
    CHECK(adapter.constructor_route_validated);
    CHECK(adapter.complete_destructor_route_validated);
    CHECK(adapter.bootstrap_failure ==
          player::PlayerInventoryBootstrapFailure::None);
    CHECK(player::ObservePlayerStateConstructed(
              &adapter, 0xABCDU) == 1U);
    return true;
}

bool TestIncompleteStartupBootstrapFailsClosed() noexcept
{
    using Failure = player::PlayerInventoryBootstrapFailure;
    struct Case {
        Failure expected;
        unsigned int selector;
    };
    const Case cases[] = {
        {Failure::UnsupportedImage, 1U},
        {Failure::NotBeforeOriginalInitializer, 2U},
        {Failure::CreatorSetUnvalidated, 3U},
        {Failure::ConstructorRouteUnavailable, 4U},
        {Failure::CompleteDestructorRouteUnavailable, 5U},
        {Failure::GlobalSlotsUnreadable, 6U},
        {Failure::ExistingGlobalState, 7U},
        {Failure::HookActiveDuringBootstrap, 8U},
    };
    for (const Case& test : cases) {
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        player::InitializePlayerGameplayParamsRestartAdapter(
            &adapter, nullptr, nullptr, nullptr);
        player::PlayerInventoryBootstrapEvidence evidence =
            CompleteBootstrapEvidence();
        switch (test.selector) {
        case 1: evidence.supported_image = false; break;
        case 2:
            evidence.before_original_game_script_initializer = false;
            break;
        case 3: evidence.closed_creator_set_validated = false; break;
        case 4: evidence.constructor_route_installed = false; break;
        case 5: evidence.complete_destructor_route_installed = false; break;
        case 6: evidence.after_global_slots_readable = false; break;
        case 7: evidence.after_local_process_global_state = 0x1234U; break;
        case 8: evidence.constructor_active_calls = 1U; break;
        default: return false;
        }
        CHECK(!player::BeginPlayerStateInventoryCoverageFromBootstrap(
            &adapter, &evidence));
        CHECK(!adapter.coverage_started);
        CHECK(adapter.bootstrap_failure == test.expected);
        CHECK(player::PlayerInventoryBootstrapFailureName(
                  test.expected)[0] != 0);
    }

    player::PlayerGameplayParamsRestartAdapter lost = {};
    player::InitializePlayerGameplayParamsRestartAdapter(
        &lost, nullptr, nullptr, nullptr);
    player::MarkPlayerStateInventoryLoss(&lost);
    player::PlayerInventoryBootstrapEvidence evidence =
        CompleteBootstrapEvidence();
    CHECK(!player::BeginPlayerStateInventoryCoverageFromBootstrap(
        &lost, &evidence));
    CHECK(lost.bootstrap_failure ==
          Failure::PrecoverageObservationLoss);
    CHECK(!lost.coverage_started);
    return true;
}

bool TestCoverageAndInventoryFailuresArePreMutation() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsSnapshotOperations snapshot_operations =
        SnapshotOperations();
    const player::PlayerGameplayParamsMutationOperations mutation_operations =
        MutationOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, &snapshot_operations, &mutation_operations, &world);
    CHECK(!player::BeginPlayerStateInventoryCoverage(
        &adapter, false, true, true));
    const std::uint64_t diagnostic_generation =
        player::ObservePlayerStateConstructed(&adapter, 0x4444U);
    CHECK(diagnostic_generation == 1U);
    CHECK(player::FindLivePlayerStateGeneration(
        &adapter, 0x4444U) == diagnostic_generation);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, 0x4444U, diagnostic_generation,
        player::PlayerStateOwnerKind::PlayerDi, 0x5555U));
    const cold::RestartContext baseline = BaselineContext();
    CHECK(player::CapturePlayerGameplayParamsBaseline(
        &adapter, &baseline) == cold::DomainResult::Failed);
    CHECK(world.reset_calls == 0U);

    player::PlayerGameplayParamsRestartAdapter complete = {};
    CHECK(Setup(&world, &complete));
    player::MarkPlayerStateInventoryLoss(&complete);
    ApplyCustomProvider(&world);
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &complete, &context) == cold::DomainResult::Failed);
    CHECK(world.reset_calls == 0U);
    return true;
}

bool TestInventoryOverflowIsBoundedAndLatched() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    const player::PlayerGameplayParamsSnapshotOperations snapshot_operations =
        SnapshotOperations();
    const player::PlayerGameplayParamsMutationOperations mutation_operations =
        MutationOperations();
    player::InitializePlayerGameplayParamsRestartAdapter(
        &adapter, &snapshot_operations, &mutation_operations, &world);
    CHECK(player::BeginPlayerStateInventoryCoverage(
        &adapter, true, true, true));
    for (std::uint32_t index = 0;
         index < player::kPlayerStateInventoryCapacity; ++index) {
        const std::uintptr_t pointer = 0x10000U + index * 0x100U;
        const std::uint64_t generation =
            player::ObservePlayerStateConstructed(&adapter, pointer);
        CHECK(generation == index + 1U);
        CHECK(player::ClassifyPlayerStateOwner(
            &adapter, pointer, generation,
            player::PlayerStateOwnerKind::OtherRttiObserved, index));
    }
    CHECK(player::ObservePlayerStateConstructed(
        &adapter, 0x999000U) == 0U);
    CHECK(adapter.inventory_count ==
          player::kPlayerStateInventoryCapacity);
    CHECK(dluma_atomic_compare_exchange(
        &adapter.inventory_overflow_latched, 0, 0) != 0);
    CHECK(dluma_atomic_compare_exchange(
        &adapter.inventory_loss_latched, 0, 0) != 0);
    return true;
}

bool TestLiveConsumerAndIncompleteStateFailPreflight() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    ApplyCustomProvider(&world);
    world.environment.local_player_di = 0xDEADU;
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::LiveGameplayConsumer);
    CHECK(world.reset_calls == 0U);

    FakeWorld incomplete_world = {};
    player::PlayerGameplayParamsRestartAdapter incomplete = {};
    CHECK(Setup(&incomplete_world, &incomplete));
    ApplyCustomProvider(&incomplete_world);
    incomplete_world.states[0].current.rows[4].definition_readable = false;
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &incomplete, &context) == cold::DomainResult::Failed);
    CHECK(incomplete.last_failure ==
          player::PlayerRestartFailure::SemanticRowInvalid);
    CHECK(incomplete_world.reset_calls == 0U);
    return true;
}

bool TestRequiredPropertySetIsExact() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    ApplyCustomProvider(&world);
    world.states[0].current.properties[4].property_id =
        player::kFovSprintModifierPropertyId;
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(adapter.last_failure ==
              player::PlayerRestartFailure::RequiredPropertyDuplicate ||
          adapter.last_failure ==
              player::PlayerRestartFailure::RequiredPropertyMissing);
    CHECK(world.reset_calls == 0U);
    return true;
}

bool TestInventoryChangeAfterQuiescePreventsMutation() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(&world, &adapter, &context));
    const std::uint64_t generation =
        player::ObservePlayerStateConstructed(&adapter, 0x300000U);
    CHECK(generation != 0U);
    CHECK(player::ClassifyPlayerStateOwner(
        &adapter, 0x300000U, generation,
        player::PlayerStateOwnerKind::OtherRttiObserved, 0x44U));
    CHECK(player::ResetPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(world.reset_calls == 0U);
    return true;
}

bool TestPreparedTransactionCanCancelOrBeSuperseded() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(&world, &adapter, &context));
    cold::RestartContext wrong_thread = context;
    wrong_thread.owner_thread_id = 12;
    CHECK(!player::CancelPreparedPlayerGameplayParamsRestart(
        &adapter, &wrong_thread));
    CHECK(adapter.transaction_active);
    world.environment.shutdown_requested = true;
    world.environment.quiescing = true;
    world.environment.active_session = 0xA0U;
    world.environment.active_level = 0xB0U;
    world.environment.current_thread_id = 12;
    const cold::ContentDomainAdapter definition =
        player::MakePlayerGameplayParamsContentDomainAdapter(
            &adapter, cold::DomainBit(cold::ContentDomainId::VfsSources),
            true);
    CHECK(definition.cancel_quiesce(
        definition.user, &context) == cold::DomainResult::Succeeded);
    CHECK(!adapter.transaction_active);
    CHECK(world.reset_calls == 0U);
    world.environment.shutdown_requested = false;
    world.environment.quiescing = false;
    world.environment.active_session = 0;
    world.environment.active_level = 0;
    world.environment.current_thread_id = 77;

    cold::RestartContext first = TransactionContext(10);
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &first) == cold::DomainResult::Succeeded);
    CHECK(player::QuiescePlayerGameplayParamsRestart(
        &adapter, &first) == cold::DomainResult::Succeeded);
    cold::RestartContext superseding = TransactionContext(11);
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &superseding) == cold::DomainResult::Succeeded);
    CHECK(adapter.transaction_generation == 11U);
    CHECK(player::QuiescePlayerGameplayParamsRestart(
        &adapter, &superseding) == cold::DomainResult::Succeeded);
    CHECK(player::CancelPreparedPlayerGameplayParamsRestart(
        &adapter, &superseding));
    CHECK(world.reset_calls == 0U);
    return true;
}

bool TestCompletedGenerationCannotMutateAgain() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(&world, &adapter, &context));
    CHECK(player::ResetPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(player::ReloadPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(player::VerifyPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Succeeded);
    ApplyCustomProvider(&world);
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(adapter.last_failure ==
          player::PlayerRestartFailure::TransactionAlreadyCompleted);
    CHECK(world.reset_calls == 1U);
    return true;
}

bool TestThreadAndSelectorIdentityArePreflightRequirements() noexcept
{
    FakeWorld thread_world = {};
    player::PlayerGameplayParamsRestartAdapter thread_adapter = {};
    CHECK(Setup(&thread_world, &thread_adapter));
    ApplyCustomProvider(&thread_world);
    thread_world.environment.current_thread_id = 12;
    const cold::RestartContext context = TransactionContext();
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &thread_adapter, &context) == cold::DomainResult::Failed);
    CHECK(thread_adapter.last_failure ==
          player::PlayerRestartFailure::ThreadMismatch);
    CHECK(thread_world.reset_calls == 0U);

    FakeWorld selector_world = {};
    player::PlayerGameplayParamsRestartAdapter selector_adapter = {};
    CHECK(Setup(&selector_world, &selector_adapter));
    ApplyCustomProvider(&selector_world);
    ++selector_world.states[0].current.progression_selector_fingerprint;
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &selector_adapter, &context) == cold::DomainResult::Failed);
    CHECK(selector_adapter.last_failure ==
          player::PlayerRestartFailure::SelectorMismatch);
    CHECK(selector_world.reset_calls == 0U);
    return true;
}

bool TestPostMutationProviderAndInvariantFailuresLatch() noexcept
{
    FakeWorld provider_world = {};
    player::PlayerGameplayParamsRestartAdapter provider_adapter = {};
    CHECK(Setup(&provider_world, &provider_adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(
        &provider_world, &provider_adapter, &context));
    provider_world.sabotage_provider = true;
    CHECK(player::ResetPlayerGameplayParams(
        &provider_adapter, &context) == cold::DomainResult::Failed);
    CHECK(provider_adapter.last_failure ==
          player::PlayerRestartFailure::ProviderMismatch);
    CHECK(provider_adapter.runtime_failure_latched);
    CHECK(provider_world.reset_calls == 1U);
    CHECK(provider_world.states[0].initialize_calls == 1U);
    CHECK(provider_world.states[1].initialize_calls == 1U);

    FakeWorld invariant_world = {};
    player::PlayerGameplayParamsRestartAdapter invariant_adapter = {};
    CHECK(Setup(&invariant_world, &invariant_adapter));
    CHECK(RunThroughQuiesce(
        &invariant_world, &invariant_adapter, &context));
    invariant_world.sabotage_invariant = true;
    CHECK(player::ResetPlayerGameplayParams(
        &invariant_adapter, &context) == cold::DomainResult::Failed);
    CHECK(invariant_adapter.last_failure ==
          player::PlayerRestartFailure::PreservationInvariantMismatch);
    CHECK(invariant_adapter.runtime_failure_latched);

    FakeWorld property_world = {};
    player::PlayerGameplayParamsRestartAdapter property_adapter = {};
    CHECK(Setup(&property_world, &property_adapter));
    CHECK(RunThroughQuiesce(
        &property_world, &property_adapter, &context));
    property_world.sabotage_changed_property = true;
    CHECK(player::ResetPlayerGameplayParams(
        &property_adapter, &context) == cold::DomainResult::Failed);
    CHECK(property_adapter.last_failure ==
          player::PlayerRestartFailure::PropertyMismatch);
    CHECK(property_adapter.runtime_failure_latched);
    CHECK(property_world.reset_calls == 1U);
    CHECK(property_world.states[0].initialize_calls == 1U);
    CHECK(property_world.states[1].initialize_calls == 1U);
    return true;
}

bool TestPhaseLockContentionIsSideEffectFree() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    ApplyCustomProvider(&world);
    const cold::RestartContext baseline = BaselineContext();
    const cold::RestartContext context = TransactionContext();
    dluma_atomic_exchange(&adapter.phase_lock, 1);
    unsigned char before[sizeof(adapter)] = {};
    std::memcpy(before, &adapter, sizeof(adapter));
    CHECK(player::CapturePlayerGameplayParamsBaseline(
        &adapter, &baseline) == cold::DomainResult::Failed);
    CHECK(player::PreflightPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(player::QuiescePlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(player::ResetPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(player::ReloadPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(player::VerifyPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(!player::CancelPreparedPlayerGameplayParamsRestart(
        &adapter, &context));
    CHECK(player::CancelQuiescedPlayerGameplayParamsRestart(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(std::memcmp(before, &adapter, sizeof(adapter)) == 0);
    dluma_atomic_exchange(&adapter.phase_lock, 0);
    return true;
}

bool TestReadOnlyBaselineCanBeAbandonedBeforePreflight() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    CHECK(adapter.baseline_captured);
    CHECK(adapter.baseline_state_count == 2U);
    CHECK(adapter.baseline_generation == BaselineContext().baseline_generation);

    player::PlayerGameplayParamsRestartAdapter expected = {};
    std::memcpy(&expected, &adapter, sizeof(expected));
    std::memset(expected.baseline_states, 0,
                sizeof(expected.baseline_states));
    expected.baseline_state_count = 0;
    expected.baseline_inventory_revision = 0;
    expected.baseline_generation = 0;
    expected.baseline_gameplay_cache_fingerprint = 0;
    expected.baseline_owner_thread_id = 0;
    expected.baseline_kind =
        player::PlayerStartupBaselineKind::Unavailable;
    expected.baseline_captured = false;
    const std::uint32_t environment_reads_before =
        world.environment_read_calls;
    const std::uint32_t snapshots_before = world.snapshot_calls;

    CHECK(player::AbandonPlayerGameplayParamsBaseline(&adapter));
    CHECK(std::memcmp(&adapter, &expected, sizeof(adapter)) == 0);
    CHECK(world.environment_read_calls == environment_reads_before);
    CHECK(world.snapshot_calls == snapshots_before);
    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    return true;
}

bool TestBaselineAbandonRefusesActiveOrFailedPhases() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter baseline = {};
    CHECK(Setup(&world, &baseline));
    CHECK(!player::AbandonPlayerGameplayParamsBaseline(nullptr));

    enum class RefusalCase : std::uint8_t {
        Transaction,
        Preflight,
        Quiesce,
        MutationStarted,
        MutationInProgress,
        RuntimeFailure,
    };
    constexpr RefusalCase cases[] = {
        RefusalCase::Transaction,
        RefusalCase::Preflight,
        RefusalCase::Quiesce,
        RefusalCase::MutationStarted,
        RefusalCase::MutationInProgress,
        RefusalCase::RuntimeFailure,
    };
    for (const RefusalCase refusal : cases) {
        player::PlayerGameplayParamsRestartAdapter adapter = {};
        std::memcpy(&adapter, &baseline, sizeof(adapter));
        switch (refusal) {
        case RefusalCase::Transaction:
            adapter.transaction_active = true;
            break;
        case RefusalCase::Preflight:
            adapter.preflight_complete = true;
            break;
        case RefusalCase::Quiesce:
            adapter.quiesce_complete = true;
            break;
        case RefusalCase::MutationStarted:
            adapter.mutation_started = true;
            break;
        case RefusalCase::MutationInProgress:
            dluma_atomic_exchange(&adapter.mutation_in_progress, 1);
            break;
        case RefusalCase::RuntimeFailure:
            adapter.runtime_failure_latched = true;
            break;
        }
        unsigned char before[sizeof(adapter)] = {};
        std::memcpy(before, &adapter, sizeof(adapter));
        CHECK(!player::AbandonPlayerGameplayParamsBaseline(&adapter));
        CHECK(std::memcmp(before, &adapter, sizeof(adapter)) == 0);
    }

    player::PlayerGameplayParamsRestartAdapter contended = {};
    std::memcpy(&contended, &baseline, sizeof(contended));
    dluma_atomic_exchange(&contended.phase_lock, 1);
    unsigned char before[sizeof(contended)] = {};
    std::memcpy(before, &contended, sizeof(contended));
    CHECK(!player::AbandonPlayerGameplayParamsBaseline(&contended));
    CHECK(std::memcmp(before, &contended, sizeof(contended)) == 0);
    dluma_atomic_exchange(&contended.phase_lock, 0);

    CHECK(world.reset_calls == 0U);
    CHECK(world.states[0].initialize_calls == 0U);
    CHECK(world.states[1].initialize_calls == 0U);
    return true;
}

bool TestSecondResetInvocationDoesNotRepeatOperations() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    cold::RestartContext context = {};
    CHECK(RunThroughQuiesce(&world, &adapter, &context));
    CHECK(player::ResetPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Succeeded);
    CHECK(player::ResetPlayerGameplayParams(
        &adapter, &context) == cold::DomainResult::Failed);
    CHECK(world.reset_calls == 1U);
    CHECK(world.states[0].initialize_calls == 1U);
    CHECK(world.states[1].initialize_calls == 1U);
    return true;
}

bool TestDomainDefinitionUsesOnlyTheInjectedAdapter() noexcept
{
    FakeWorld world = {};
    player::PlayerGameplayParamsRestartAdapter adapter = {};
    CHECK(Setup(&world, &adapter));
    const cold::ContentDomainAdapter definition =
        player::MakePlayerGameplayParamsContentDomainAdapter(
            &adapter, cold::DomainBit(cold::ContentDomainId::VfsSources),
            true);
    CHECK(definition.id == cold::ContentDomainId::PlayerGameplayParams);
    CHECK(definition.user == &adapter);
    CHECK(definition.mandatory);
    CHECK(definition.destructive);
    CHECK(definition.cancel_quiesce ==
          &player::CancelQuiescedPlayerGameplayParamsRestart);
    CHECK(definition.reset == &player::ResetPlayerGameplayParams);
    CHECK(definition.reload == &player::ReloadPlayerGameplayParams);
    return true;
}

}  // namespace

int main()
{
    if (!TestAuthoritativeAbsentBaselineAndAssessment() ||
        !TestAbsentBaselineDoesNotRequirePerStateSnapshots() ||
        !TestNonemptyBaselineStillRequiresPerStateSnapshots() ||
        !TestAbsentBaselineRevisionAndObserverLossStayIncomplete() ||
        !TestSnapshotOnlyBaselineDoesNotRequireMutationOperations() ||
        !TestSemanticBaselineCanBeReadyWithoutMutationOwnership() ||
        !TestObservedZeroSourceRegistryGenerationIsValid() ||
        !TestProductionDisabledMutationRunsNoCallbacks() ||
        !TestSnapshotStableWindowMismatchIsRejectedAndRetryable() ||
        !TestDynamicPlayerDiStateParticipatesInCompleteBaseline() ||
        !TestSemanticBaselineRejectsMissingDataButAcceptsRestartOnlyGaps() ||
        !TestRuntime12SemanticMembershipEvidenceSplit() ||
        !TestInventoryRevisionChangeDuringBaselineIsRejectedAndRetryable() ||
        !TestVerifiedRestartIsOneSynchronousMutation() ||
        !TestRegistryBackedTouchedPlayerTransactionReachesVerified() ||
        !TestPlayerTouchAssessmentUsesDirectB1VsB0WithoutProgressionHooks() ||
        !TestRuntime14PostDestroySemanticCaptureContract() ||
        !TestRuntime16LifecycleLocalThreadAuthority() ||
        !TestIncompleteCurrentPlayerSnapshotIsNotUntouched() ||
        !TestPointerReuseGetsNewGeneration() ||
        !TestOwnerClassificationIsIdempotentAndConflictsLatch() ||
        !TestExactStartupBootstrapAuthorizesCoverage() ||
        !TestIncompleteStartupBootstrapFailsClosed() ||
        !TestCoverageAndInventoryFailuresArePreMutation() ||
        !TestInventoryOverflowIsBoundedAndLatched() ||
        !TestLiveConsumerAndIncompleteStateFailPreflight() ||
        !TestRequiredPropertySetIsExact() ||
        !TestInventoryChangeAfterQuiescePreventsMutation() ||
        !TestPreparedTransactionCanCancelOrBeSuperseded() ||
        !TestCompletedGenerationCannotMutateAgain() ||
        !TestThreadAndSelectorIdentityArePreflightRequirements() ||
        !TestPostMutationProviderAndInvariantFailuresLatch() ||
        !TestPhaseLockContentionIsSideEffectFree() ||
        !TestReadOnlyBaselineCanBeAbandonedBeforePreflight() ||
        !TestBaselineAbandonRefusesActiveOrFailedPhases() ||
        !TestSecondResetInvocationDoesNotRepeatOperations() ||
        !TestDomainDefinitionUsesOnlyTheInjectedAdapter())
        return 1;
    std::puts("player gameplay params restart adapter fake harness: PASS");
    return 0;
}
