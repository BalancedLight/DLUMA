#define _HAS_EXCEPTIONS 0

#include "PlayerAbsentBaselineReset.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::player_absent_reset {
namespace {

bool SameSnapshot(const PlayerAbsentSnapshot& left,
                  const PlayerAbsentSnapshot& right) noexcept
{
    return left.complete && right.complete &&
        left.global_slots_readable && right.global_slots_readable &&
        left.inventory_complete && right.inventory_complete &&
        !left.observer_loss && !right.observer_loss &&
        left.owner_count == right.owner_count &&
        left.local_process_global_state ==
            right.local_process_global_state &&
        left.character_manager_global_state ==
            right.character_manager_global_state &&
        left.local_player_di == right.local_player_di &&
        left.active_session == right.active_session &&
        left.active_level == right.active_level &&
        left.inventory_revision == right.inventory_revision &&
        left.gameplay_cache_fingerprint ==
            right.gameplay_cache_fingerprint &&
        std::memcmp(
            left.owners, right.owners,
            sizeof(PlayerAbsentOwnerRecord) * left.owner_count) == 0;
}

bool IsAbsent(const PlayerAbsentSnapshot& snapshot,
              std::uint64_t cache_fingerprint) noexcept
{
    return snapshot.complete && snapshot.global_slots_readable &&
        snapshot.inventory_complete && !snapshot.observer_loss &&
        snapshot.owner_count == 0U &&
        !snapshot.local_process_global_state &&
        !snapshot.character_manager_global_state &&
        !snapshot.local_player_di &&
        !snapshot.active_session && !snapshot.active_level &&
        snapshot.gameplay_cache_fingerprint == cache_fingerprint &&
        cache_fingerprint != 0U;
}

bool CaptureStable(PlayerAbsentResetAdapter* adapter,
                   PlayerAbsentSnapshot* output) noexcept
{
    if (!adapter || !output || !adapter->operations.capture)
        return false;
    PlayerAbsentSnapshot first = {};
    PlayerAbsentSnapshot second = {};
    if (!adapter->operations.capture(
            adapter->operations.user, &first) ||
        !adapter->operations.capture(
            adapter->operations.user, &second) ||
        !SameSnapshot(first, second))
        return false;
    adapter->last_capture_first = first;
    adapter->last_capture_second = second;
    *output = second;
    return true;
}

bool AuthorizationComplete(
    const PlayerAbsentResetAuthorization& authorization) noexcept
{
    return authorization.current_thread_id &&
        authorization.current_thread_id ==
            authorization.game_thread_id &&
        authorization.owner_route_validated &&
        authorization.cache_route_validated &&
        authorization.owner_lock_held &&
        authorization.cache_lock_held &&
        authorization.outgoing_sources_removed &&
        !authorization.next_project_staging_started;
}

}  // namespace

void InitializePlayerAbsentResetAdapter(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetOperations* operations) noexcept
{
    if (!adapter) return;
    std::memset(adapter, 0, sizeof(*adapter));
    if (operations) adapter->operations = *operations;
}

bool CapturePlayerAbsentBaseline(
    PlayerAbsentResetAdapter* adapter,
    std::uint64_t baseline_generation) noexcept
{
    if (!adapter || !baseline_generation ||
        adapter->baseline_captured) {
        if (adapter)
            adapter->last_failure =
                PlayerAbsentResetFailure::InvalidArgument;
        return false;
    }
    PlayerAbsentSnapshot captured = {};
    if (!CaptureStable(adapter, &captured)) {
        adapter->last_failure =
            PlayerAbsentResetFailure::SnapshotIncomplete;
        return false;
    }
    if (!IsAbsent(
            captured, captured.gameplay_cache_fingerprint)) {
        adapter->last_failure =
            captured.observer_loss
                ? PlayerAbsentResetFailure::ObserverLoss
                : PlayerAbsentResetFailure::BaselineUnavailable;
        return false;
    }
    adapter->baseline_capture_first = adapter->last_capture_first;
    adapter->baseline_capture_second = adapter->last_capture_second;
    adapter->baseline = captured;
    adapter->baseline_generation = baseline_generation;
    adapter->baseline_captured = true;
    adapter->last_failure = PlayerAbsentResetFailure::None;
    return true;
}

bool PreparePlayerAbsentReset(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetAuthorization& authorization) noexcept
{
    if (!adapter || !adapter->baseline_captured ||
        !adapter->operations.capture) {
        if (adapter)
            adapter->last_failure =
                PlayerAbsentResetFailure::InvalidArgument;
        return false;
    }
    if (!authorization.current_thread_id ||
        authorization.current_thread_id !=
            authorization.game_thread_id)
        adapter->last_failure = PlayerAbsentResetFailure::WrongThread;
    else if (authorization.next_project_staging_started)
        adapter->last_failure =
            PlayerAbsentResetFailure::NextProjectStagingStarted;
    else {
        PlayerAbsentSnapshot current = {};
        if (!CaptureStable(adapter, &current)) {
            adapter->last_failure =
                PlayerAbsentResetFailure::SnapshotChanged;
            return false;
        }
        if (current.active_session || current.active_level ||
            current.local_player_di) {
            adapter->last_failure =
                PlayerAbsentResetFailure::SessionStillActive;
            return false;
        }
        if (current.owner_count > kPlayerAbsentOwnerCapacity) {
            adapter->last_failure =
                PlayerAbsentResetFailure::SnapshotIncomplete;
            return false;
        }
        for (std::uint32_t index = 0;
             index < current.owner_count; ++index) {
            const PlayerAbsentOwnerRecord& owner =
                current.owners[index];
            if (!owner.state || !owner.owner_slot ||
                !owner.generation || !owner.exact ||
                owner.kind == PlayerAbsentOwnerKind::Unknown) {
                adapter->last_failure =
                    PlayerAbsentResetFailure::OwnerIdentityInvalid;
                return false;
            }
        }
        if (current.owner_count != 0U &&
            (!authorization.owner_route_validated ||
             !authorization.owner_lock_held ||
             !adapter->operations.retire_exact_owner)) {
            adapter->last_failure =
                PlayerAbsentResetFailure::OwnerRouteUnvalidated;
            return false;
        }
        if (!authorization.cache_route_validated ||
            !authorization.cache_lock_held ||
            !adapter->operations.restore_cache) {
            adapter->last_failure =
                PlayerAbsentResetFailure::CacheRouteUnvalidated;
            return false;
        }
        adapter->planned = current;
        adapter->prepared = true;
        adapter->last_failure = PlayerAbsentResetFailure::None;
        return true;
    }
    return false;
}

bool ExecuteAndVerifyPlayerAbsentReset(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetAuthorization& authorization) noexcept
{
    if (!adapter || !adapter->prepared ||
        adapter->mutation_started || adapter->verified ||
        !adapter->operations.retire_exact_owner ||
        !adapter->operations.restore_cache ||
        !AuthorizationComplete(authorization)) {
        if (adapter)
            adapter->last_failure =
                PlayerAbsentResetFailure::InvalidArgument;
        return false;
    }
    adapter->mutation_started = true;
    for (std::uint32_t index = adapter->planned.owner_count;
         index != 0U; --index) {
        if (!adapter->operations.retire_exact_owner(
                adapter->operations.user,
                &adapter->planned.owners[index - 1U])) {
            adapter->last_failure =
                PlayerAbsentResetFailure::RetireFailed;
            return false;
        }
    }
    if (!adapter->operations.restore_cache(
            adapter->operations.user,
            adapter->baseline.gameplay_cache_fingerprint)) {
        adapter->last_failure =
            PlayerAbsentResetFailure::CacheRestoreFailed;
        return false;
    }
    PlayerAbsentSnapshot after = {};
    if (!CaptureStable(adapter, &after) ||
        !IsAbsent(
            after,
            adapter->baseline.gameplay_cache_fingerprint)) {
        adapter->last_failure =
            PlayerAbsentResetFailure::VerificationFailed;
        return false;
    }
    adapter->verified = true;
    adapter->last_failure = PlayerAbsentResetFailure::None;
    return true;
}

const char* PlayerAbsentResetFailureName(
    PlayerAbsentResetFailure failure) noexcept
{
    switch (failure) {
    case PlayerAbsentResetFailure::None: return "none";
    case PlayerAbsentResetFailure::InvalidArgument:
        return "invalid_argument";
    case PlayerAbsentResetFailure::BaselineUnavailable:
        return "baseline_unavailable";
    case PlayerAbsentResetFailure::SnapshotIncomplete:
        return "snapshot_incomplete";
    case PlayerAbsentResetFailure::SnapshotChanged:
        return "snapshot_changed";
    case PlayerAbsentResetFailure::ObserverLoss: return "observer_loss";
    case PlayerAbsentResetFailure::WrongThread: return "wrong_thread";
    case PlayerAbsentResetFailure::SessionStillActive:
        return "session_still_active";
    case PlayerAbsentResetFailure::OwnerRouteUnvalidated:
        return "owner_route_unvalidated";
    case PlayerAbsentResetFailure::CacheRouteUnvalidated:
        return "cache_route_unvalidated";
    case PlayerAbsentResetFailure::OwnerIdentityInvalid:
        return "owner_identity_invalid";
    case PlayerAbsentResetFailure::ResetBeforeSourceRemoval:
        return "reset_before_source_removal";
    case PlayerAbsentResetFailure::NextProjectStagingStarted:
        return "next_project_staging_started";
    case PlayerAbsentResetFailure::RetireFailed: return "retire_failed";
    case PlayerAbsentResetFailure::CacheRestoreFailed:
        return "cache_restore_failed";
    case PlayerAbsentResetFailure::VerificationFailed:
        return "verification_failed";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::player_absent_reset
