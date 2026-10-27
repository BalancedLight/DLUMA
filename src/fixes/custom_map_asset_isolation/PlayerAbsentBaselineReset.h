#ifndef DLUMA_PLAYER_ABSENT_BASELINE_RESET_H
#define DLUMA_PLAYER_ABSENT_BASELINE_RESET_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::player_absent_reset {

constexpr std::size_t kPlayerAbsentOwnerCapacity = 8U;

enum class PlayerAbsentOwnerKind : std::uint8_t {
    Unknown,
    LocalProcessGlobal,
    CharacterManagerGlobal,
    PlayerDi,
    TutorialPlayerDi,
};

enum class PlayerAbsentResetFailure : std::uint8_t {
    None,
    InvalidArgument,
    BaselineUnavailable,
    SnapshotIncomplete,
    SnapshotChanged,
    ObserverLoss,
    WrongThread,
    SessionStillActive,
    OwnerRouteUnvalidated,
    CacheRouteUnvalidated,
    OwnerIdentityInvalid,
    ResetBeforeSourceRemoval,
    NextProjectStagingStarted,
    RetireFailed,
    CacheRestoreFailed,
    VerificationFailed,
};

struct PlayerAbsentOwnerRecord {
    std::uintptr_t state;
    std::uintptr_t owner_slot;
    std::uint64_t generation;
    PlayerAbsentOwnerKind kind;
    bool exact;
};

struct PlayerAbsentSnapshot {
    PlayerAbsentOwnerRecord owners[kPlayerAbsentOwnerCapacity];
    std::uint32_t owner_count;
    std::uintptr_t local_process_global_state;
    std::uintptr_t character_manager_global_state;
    std::uintptr_t local_player_di;
    std::uintptr_t active_session;
    std::uintptr_t active_level;
    std::uint64_t inventory_revision;
    std::uint64_t gameplay_cache_fingerprint;
    bool global_slots_readable;
    bool inventory_complete;
    bool observer_loss;
    bool complete;
};

using CapturePlayerAbsentSnapshotFn = bool (*)(
    void* user, PlayerAbsentSnapshot* output) noexcept;
using RetireExactPlayerOwnerFn = bool (*)(
    void* user, const PlayerAbsentOwnerRecord* owner) noexcept;
using RestorePlayerCacheFn = bool (*)(
    void* user, std::uint64_t baseline_cache_fingerprint) noexcept;

struct PlayerAbsentResetOperations {
    void* user;
    CapturePlayerAbsentSnapshotFn capture;
    RetireExactPlayerOwnerFn retire_exact_owner;
    RestorePlayerCacheFn restore_cache;
};

struct PlayerAbsentResetAuthorization {
    std::uint32_t current_thread_id;
    std::uint32_t game_thread_id;
    bool owner_route_validated;
    bool cache_route_validated;
    bool owner_lock_held;
    bool cache_lock_held;
    bool outgoing_sources_removed;
    bool next_project_staging_started;
};

struct PlayerAbsentResetAdapter {
    PlayerAbsentResetOperations operations;
    PlayerAbsentSnapshot baseline;
    PlayerAbsentSnapshot planned;
    PlayerAbsentSnapshot baseline_capture_first;
    PlayerAbsentSnapshot baseline_capture_second;
    PlayerAbsentSnapshot last_capture_first;
    PlayerAbsentSnapshot last_capture_second;
    std::uint64_t baseline_generation;
    PlayerAbsentResetFailure last_failure;
    bool baseline_captured;
    bool prepared;
    bool mutation_started;
    bool verified;
};

void InitializePlayerAbsentResetAdapter(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetOperations* operations) noexcept;
bool CapturePlayerAbsentBaseline(
    PlayerAbsentResetAdapter* adapter,
    std::uint64_t baseline_generation) noexcept;
bool PreparePlayerAbsentReset(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetAuthorization& authorization) noexcept;
bool ExecuteAndVerifyPlayerAbsentReset(
    PlayerAbsentResetAdapter* adapter,
    const PlayerAbsentResetAuthorization& authorization) noexcept;
const char* PlayerAbsentResetFailureName(
    PlayerAbsentResetFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::player_absent_reset

#endif
