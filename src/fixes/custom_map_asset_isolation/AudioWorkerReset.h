#ifndef DLUMA_AUDIO_WORKER_RESET_H
#define DLUMA_AUDIO_WORKER_RESET_H

#include "../../dlw_win32.h"
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset {

constexpr std::size_t kMaximumKeptBanks = 192U;
constexpr std::size_t kBankNameCapacity = 256U;

enum class Phase : LONG {
    Idle, Queued, MarkingVoices, DrainingVoices, EvictingBanks,
    Released, Cancelled, Failed,
};
enum class Failure : LONG {
    None, InvalidPlan, Busy, WrongOwner, WrongWorker, FenceChanged,
    SnapshotIncomplete, NativeRequestPending, DeadlineExpired,
    CancellationAfterMutation, ProtectedBankChanged, ProjectBankRemains,
    VoiceReferencesRemain, NativeOperationFailed,
};

/* Bank membership is not a reference count. The native record's +0x40 word
 * is an asynchronous load state. Preserve record/file identity separately
 * from the caller's complete source-instance/reference inventory proof. */
struct KeptBank {
    char name[kBankNameCapacity];
    std::uintptr_t record;
    std::uintptr_t native_file;
    std::uint64_t instance_generation;
    std::uint64_t provider_identity;
    unsigned char content_sha256[32];
};
struct Plan {
    std::uintptr_t audio_owner;
    std::uintptr_t bank_manager;
    std::uint64_t epoch;
    std::uint64_t transaction_generation;
    std::uint64_t fence_identity;
    std::uint64_t deadline_tick;
    std::uint64_t preserved_reference_fingerprint;
    KeptBank keep[kMaximumKeptBanks];
    std::uint32_t keep_count;
    bool complete_content_provenance;
    bool all_retaining_owners_classified;
};
struct Snapshot {
    std::uintptr_t audio_owner;
    std::uintptr_t bank_manager;
    std::uint64_t fence_identity;
    std::uint64_t preserved_reference_fingerprint;
    std::uint32_t active_voices;
    std::uint32_t deferred_voices;
    std::uint32_t pending_bank_reads;
    std::uint32_t project_bank_count;
    std::uint32_t unreleased_source_instances;
    bool native_unload_request_pending;
    bool preserved_bank_identities_match;
    bool complete;
};
struct WorkerFrame {
    std::uintptr_t audio_owner;
    std::uint64_t route_identity;
    std::uint64_t original_completion_generation;
    std::uint64_t current_tick;
    std::uint32_t current_thread_id;
    bool exact_native_route;
    bool original_returned;
};
using TryLockFn = bool (*)(void*) noexcept;
using UnlockFn = void (*)(void*) noexcept;
using FenceFn = bool (*)(void*, const Plan*) noexcept;
using SnapshotFn = bool (*)(void*, const Plan*, Snapshot*) noexcept;
using MarkVoicesFn = bool (*)(void*, std::uintptr_t) noexcept;
using EvictBanksFn = bool (*)(void*, std::uintptr_t,
                            const KeptBank*, std::size_t) noexcept;
struct Operations {
    void* user;
    TryLockFn try_lock;
    UnlockFn unlock;
    FenceFn validate_fence;
    SnapshotFn snapshot;
    MarkVoicesFn mark_voices;
    EvictBanksFn evict_except;
    bool native_targets_validated;
};

/* The coordinator publishes one immutable request. Only the exact native
 * audio worker executes it. Timeout never authorizes provider retirement. */
struct Command {
    volatile LONG lock;
    volatile LONG phase;
    volatile LONG failure;
    volatile LONG cancel_requested;
    Plan plan;
    Operations operations;
    std::uint64_t worker_route;
    std::uint64_t mark_completion_generation;
    std::uint32_t worker_thread;
    bool mutation_started;
};

void Initialize(Command*, const Operations*) noexcept;
bool Enqueue(Command*, const Plan&) noexcept;
void BeforeOriginalUpdate(Command*, const WorkerFrame&) noexcept;
void AfterOriginalUpdate(Command*, const WorkerFrame&) noexcept;
Phase Poll(Command*, std::uint64_t current_tick) noexcept;
void RequestCancellation(Command*) noexcept;
/* Acknowledges bank and voice release only, never full Audio B0 replay. */
bool ProvidersCanRetire(const Command*) noexcept;
bool ResetCompletedCommand(Command*) noexcept;

} // namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset
#endif
