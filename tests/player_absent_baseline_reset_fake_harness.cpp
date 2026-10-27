#include "fixes/custom_map_asset_isolation/PlayerAbsentBaselineReset.h"

#include <cstdio>
#include <cstring>

namespace reset =
    dluma::fixes::custom_map_asset_isolation::player_absent_reset;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

struct FakeWorld {
    reset::PlayerAbsentSnapshot snapshot;
    std::uint32_t capture_calls;
    std::uint32_t retire_calls;
    std::uint32_t restore_calls;
};

bool Capture(void* user, reset::PlayerAbsentSnapshot* output) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !output) return false;
    ++world->capture_calls;
    *output = world->snapshot;
    return true;
}

bool Retire(
    void* user, const reset::PlayerAbsentOwnerRecord* owner) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !owner || !world->snapshot.owner_count)
        return false;
    reset::PlayerAbsentOwnerRecord& last =
        world->snapshot.owners[world->snapshot.owner_count - 1U];
    if (std::memcmp(&last, owner, sizeof(last)) != 0)
        return false;
    if (last.state == world->snapshot.local_process_global_state)
        world->snapshot.local_process_global_state = 0;
    if (last.state == world->snapshot.character_manager_global_state)
        world->snapshot.character_manager_global_state = 0;
    std::memset(&last, 0, sizeof(last));
    --world->snapshot.owner_count;
    ++world->snapshot.inventory_revision;
    ++world->retire_calls;
    return true;
}

bool RestoreCache(void* user, std::uint64_t fingerprint) noexcept
{
    FakeWorld* world = static_cast<FakeWorld*>(user);
    if (!world || !fingerprint) return false;
    world->snapshot.gameplay_cache_fingerprint = fingerprint;
    ++world->restore_calls;
    return true;
}

reset::PlayerAbsentSnapshot Absent() noexcept
{
    reset::PlayerAbsentSnapshot snapshot = {};
    snapshot.inventory_revision = 1U;
    snapshot.gameplay_cache_fingerprint = 0xA11CEU;
    snapshot.global_slots_readable = true;
    snapshot.inventory_complete = true;
    snapshot.complete = true;
    return snapshot;
}

reset::PlayerAbsentResetAuthorization Authorization() noexcept
{
    reset::PlayerAbsentResetAuthorization authorization = {};
    authorization.current_thread_id = 7U;
    authorization.game_thread_id = 7U;
    authorization.owner_route_validated = true;
    authorization.cache_route_validated = true;
    authorization.owner_lock_held = true;
    authorization.cache_lock_held = true;
    authorization.outgoing_sources_removed = true;
    return authorization;
}

bool TestExactResetReturnsToAbsentBaseline() noexcept
{
    FakeWorld world = {};
    world.snapshot = Absent();
    const reset::PlayerAbsentResetOperations operations = {
        &world, &Capture, &Retire, &RestoreCache};
    reset::PlayerAbsentResetAdapter adapter = {};
    reset::InitializePlayerAbsentResetAdapter(&adapter, &operations);
    CHECK(reset::CapturePlayerAbsentBaseline(&adapter, 1U));

    world.snapshot.owner_count = 2U;
    world.snapshot.owners[0] = {
        0x1000U, 0x5000U, 1U,
        reset::PlayerAbsentOwnerKind::LocalProcessGlobal, true};
    world.snapshot.owners[1] = {
        0x2000U, 0x6000U, 2U,
        reset::PlayerAbsentOwnerKind::CharacterManagerGlobal, true};
    world.snapshot.local_process_global_state = 0x1000U;
    world.snapshot.character_manager_global_state = 0x2000U;
    world.snapshot.inventory_revision = 3U;
    world.snapshot.gameplay_cache_fingerprint = 0xBEEFU;

    const reset::PlayerAbsentResetAuthorization authorization =
        Authorization();
    CHECK(reset::PreparePlayerAbsentReset(&adapter, authorization));
    CHECK(!adapter.mutation_started);
    CHECK(reset::ExecuteAndVerifyPlayerAbsentReset(
        &adapter, authorization));
    CHECK(adapter.verified);
    CHECK(world.retire_calls == 2U);
    CHECK(world.restore_calls == 1U);
    CHECK(world.snapshot.owner_count == 0U);
    CHECK(world.snapshot.local_process_global_state == 0U);
    CHECK(world.snapshot.character_manager_global_state == 0U);
    CHECK(world.snapshot.gameplay_cache_fingerprint ==
          adapter.baseline.gameplay_cache_fingerprint);
    return true;
}

bool TestInvalidOwnerFailsBeforeMutation() noexcept
{
    FakeWorld world = {};
    world.snapshot = Absent();
    const reset::PlayerAbsentResetOperations operations = {
        &world, &Capture, &Retire, &RestoreCache};
    reset::PlayerAbsentResetAdapter adapter = {};
    reset::InitializePlayerAbsentResetAdapter(&adapter, &operations);
    CHECK(reset::CapturePlayerAbsentBaseline(&adapter, 1U));
    world.snapshot.owner_count = 1U;
    world.snapshot.owners[0] = {
        0x1000U, 0x5000U, 1U,
        reset::PlayerAbsentOwnerKind::LocalProcessGlobal, false};
    world.snapshot.local_process_global_state = 0x1000U;
    CHECK(!reset::PreparePlayerAbsentReset(
        &adapter, Authorization()));
    CHECK(adapter.last_failure ==
          reset::PlayerAbsentResetFailure::OwnerIdentityInvalid);
    CHECK(!adapter.mutation_started);
    CHECK(world.retire_calls == 0U);
    CHECK(world.restore_calls == 0U);
    return true;
}

bool TestPreparationPrecedesRemovalAndDestructorIsInsufficient() noexcept
{
    FakeWorld world = {};
    world.snapshot = Absent();
    const reset::PlayerAbsentResetOperations operations = {
        &world, &Capture, &Retire, &RestoreCache};
    reset::PlayerAbsentResetAdapter adapter = {};
    reset::InitializePlayerAbsentResetAdapter(&adapter, &operations);
    CHECK(reset::CapturePlayerAbsentBaseline(&adapter, 1U));
    world.snapshot.owner_count = 1U;
    world.snapshot.owners[0] = {
        0x1000U, 0x5000U, 1U,
        reset::PlayerAbsentOwnerKind::LocalProcessGlobal, true};
    world.snapshot.local_process_global_state = 0x1000U;
    reset::PlayerAbsentResetAuthorization authorization =
        Authorization();
    authorization.outgoing_sources_removed = false;
    CHECK(reset::PreparePlayerAbsentReset(&adapter, authorization));
    CHECK(adapter.prepared);
    CHECK(!adapter.mutation_started);
    CHECK(!reset::ExecuteAndVerifyPlayerAbsentReset(
        &adapter, authorization));
    CHECK(!adapter.mutation_started);
    CHECK(world.retire_calls == 0U);

    FakeWorld no_release_world = {};
    no_release_world.snapshot = Absent();
    const reset::PlayerAbsentResetOperations destructor_only = {
        &no_release_world, &Capture, nullptr, &RestoreCache};
    reset::PlayerAbsentResetAdapter no_release = {};
    reset::InitializePlayerAbsentResetAdapter(
        &no_release, &destructor_only);
    CHECK(reset::CapturePlayerAbsentBaseline(&no_release, 1U));
    no_release_world.snapshot = world.snapshot;
    CHECK(!reset::PreparePlayerAbsentReset(
        &no_release, authorization));
    CHECK(no_release.last_failure ==
          reset::PlayerAbsentResetFailure::OwnerRouteUnvalidated);
    CHECK(!no_release.mutation_started);
    return true;
}

}  // namespace

int main()
{
    if (!TestExactResetReturnsToAbsentBaseline() ||
        !TestInvalidOwnerFailsBeforeMutation() ||
        !TestPreparationPrecedesRemovalAndDestructorIsInsufficient())
        return 1;
    std::puts("player absent baseline reset fake harness: PASS");
    return 0;
}
