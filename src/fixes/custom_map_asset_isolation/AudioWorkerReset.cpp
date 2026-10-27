#define _HAS_EXCEPTIONS 0
#include "AudioWorkerReset.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset {
namespace {
class Guard {
public:
    explicit Guard(Command* command) noexcept : command_(command),
        acquired_(command && dluma_atomic_compare_exchange(&command->lock, 1, 0) == 0) {}
    ~Guard() { if (acquired_) dluma_atomic_exchange(&command_->lock, 0); }
    bool acquired() const noexcept { return acquired_; }
private: Command* command_; bool acquired_;
};
Phase ReadPhase(const Command* command) noexcept
{
    return command ? static_cast<Phase>(dluma_atomic_compare_exchange(
        const_cast<volatile LONG*>(&command->phase), 0, 0)) : Phase::Failed;
}
void SetPhase(Command* command, Phase phase) noexcept
{ dluma_atomic_exchange(&command->phase, static_cast<LONG>(phase)); }
void Fail(Command* command, Failure failure) noexcept
{
    dluma_atomic_exchange(&command->failure, static_cast<LONG>(failure));
    SetPhase(command, Phase::Failed);
}
bool Terminal(Phase phase) noexcept
{ return phase == Phase::Released || phase == Phase::Cancelled || phase == Phase::Failed; }
bool IsComplete(const Operations& operations) noexcept
{
    return operations.native_targets_validated && operations.try_lock &&
        operations.unlock && operations.validate_fence && operations.snapshot &&
        operations.mark_voices && operations.evict_except;
}
bool PlanValid(const Plan& plan) noexcept
{
    if (!plan.audio_owner || !plan.bank_manager || !plan.epoch ||
        !plan.transaction_generation || !plan.fence_identity ||
        !plan.deadline_tick || !plan.preserved_reference_fingerprint ||
        !plan.complete_content_provenance || !plan.all_retaining_owners_classified ||
        plan.keep_count > kMaximumKeptBanks) return false;
    for (std::uint32_t i = 0; i < plan.keep_count; ++i) {
        const KeptBank& bank = plan.keep[i];
        unsigned char content = 0;
        for (unsigned char byte : bank.content_sha256) content |= byte;
        if (!bank.name[0] || !std::memchr(bank.name, 0, sizeof(bank.name)) ||
            !bank.record || !bank.native_file || !bank.instance_generation ||
            !bank.provider_identity || !content) return false;
        /* Native keep membership is a case-sensitive hash of canonical names.
         * Reject collisions as well as duplicate names, rather than retain an
         * unrelated project bank through a hash collision. */
        auto hash = [](const char* name) noexcept {
            std::uint32_t value = 0;
            for (; *name; ++name) value = value * 41U +
                static_cast<std::uint32_t>(static_cast<std::int8_t>(*name));
            return value;
        };
        for (std::uint32_t j = 0; j < i; ++j)
            if (hash(bank.name) == hash(plan.keep[j].name)) return false;
    }
    return true;
}
bool CheckCancellation(Command* command, std::uint64_t tick) noexcept
{
    const bool expired = tick >= command->plan.deadline_tick;
    if (!expired && dluma_atomic_compare_exchange(&command->cancel_requested, 0, 0) == 0)
        return false;
    if (command->mutation_started)
        Fail(command, expired ? Failure::DeadlineExpired : Failure::CancellationAfterMutation);
    else {
        dluma_atomic_exchange(&command->failure, static_cast<LONG>(
            expired ? Failure::DeadlineExpired : Failure::None));
        SetPhase(command, Phase::Cancelled);
    }
    return true;
}
bool WorkerValid(Command* command, const WorkerFrame& frame) noexcept
{
    if (!frame.exact_native_route || !frame.route_identity ||
        !frame.current_thread_id || frame.audio_owner != command->plan.audio_owner) {
        Fail(command, Failure::WrongOwner); return false;
    }
    if (command->worker_route && (command->worker_route != frame.route_identity ||
        command->worker_thread != frame.current_thread_id)) {
        Fail(command, Failure::WrongWorker); return false;
    }
    return true;
}
bool SnapshotValid(Command* command, Snapshot* snapshot) noexcept
{
    if (!command->operations.validate_fence(command->operations.user, &command->plan)) {
        Fail(command, Failure::FenceChanged); return false;
    }
    if (!command->operations.snapshot(command->operations.user, &command->plan, snapshot) ||
        !snapshot->complete) { Fail(command, Failure::SnapshotIncomplete); return false; }
    if (snapshot->audio_owner != command->plan.audio_owner ||
        snapshot->bank_manager != command->plan.bank_manager ||
        snapshot->fence_identity != command->plan.fence_identity) {
        Fail(command, Failure::FenceChanged); return false;
    }
    if (!snapshot->preserved_bank_identities_match ||
        snapshot->preserved_reference_fingerprint != command->plan.preserved_reference_fingerprint) {
        Fail(command, Failure::ProtectedBankChanged); return false;
    }
    return true;
}
} // namespace

void Initialize(Command* command, const Operations* operations) noexcept
{
    if (!command) return;
    std::memset(command, 0, sizeof(*command));
    if (operations) command->operations = *operations;
}
bool Enqueue(Command* command, const Plan& plan) noexcept
{
    Guard guard(command);
    if (!guard.acquired() || ReadPhase(command) != Phase::Idle) return false;
    if (!PlanValid(plan) || !IsComplete(command->operations)) {
        dluma_atomic_exchange(&command->failure, static_cast<LONG>(Failure::InvalidPlan));
        return false;
    }
    command->plan = plan;
    command->worker_route = 0;
    command->worker_thread = 0;
    command->mark_completion_generation = 0;
    command->mutation_started = false;
    dluma_atomic_exchange(&command->failure, 0);
    dluma_atomic_exchange(&command->cancel_requested, 0);
    SetPhase(command, Phase::Queued);
    return true;
}
void BeforeOriginalUpdate(Command* command, const WorkerFrame& frame) noexcept
{
    Guard guard(command);
    if (!guard.acquired() || ReadPhase(command) != Phase::Queued) return;
    if (CheckCancellation(command, frame.current_tick) || !WorkerValid(command, frame)) return;
    if (!command->operations.try_lock(command->operations.user)) return;
    Snapshot snapshot = {};
    if (SnapshotValid(command, &snapshot)) {
        if (snapshot.native_unload_request_pending)
            Fail(command, Failure::NativeRequestPending);
        else {
            command->worker_route = frame.route_identity;
            command->worker_thread = frame.current_thread_id;
            command->mark_completion_generation = frame.original_completion_generation;
            command->mutation_started = true;
            SetPhase(command, Phase::MarkingVoices);
            if (!command->operations.mark_voices(command->operations.user, frame.audio_owner))
                Fail(command, Failure::NativeOperationFailed);
            else SetPhase(command, Phase::DrainingVoices);
        }
    }
    command->operations.unlock(command->operations.user);
}
void AfterOriginalUpdate(Command* command, const WorkerFrame& frame) noexcept
{
    Guard guard(command);
    if (!guard.acquired() || ReadPhase(command) != Phase::DrainingVoices) return;
    if (CheckCancellation(command, frame.current_tick) || !WorkerValid(command, frame)) return;
    if (!frame.original_returned ||
        frame.original_completion_generation <= command->mark_completion_generation) return;
    if (!command->operations.try_lock(command->operations.user)) return;
    Snapshot before = {};
    if (SnapshotValid(command, &before)) {
        if (before.native_unload_request_pending)
            Fail(command, Failure::NativeRequestPending);
        else if (!before.active_voices && !before.deferred_voices && !before.pending_bank_reads &&
                 !before.unreleased_source_instances) {
            SetPhase(command, Phase::EvictingBanks);
            if (!command->operations.evict_except(command->operations.user,
                    command->plan.bank_manager, command->plan.keep, command->plan.keep_count))
                Fail(command, Failure::NativeOperationFailed);
            else {
                Snapshot after = {};
                if (SnapshotValid(command, &after)) {
                    if (after.project_bank_count) Fail(command, Failure::ProjectBankRemains);
                    else if (after.active_voices || after.deferred_voices || after.pending_bank_reads ||
                             after.unreleased_source_instances ||
                             after.native_unload_request_pending)
                        Fail(command, Failure::VoiceReferencesRemain);
                    else SetPhase(command, Phase::Released);
                }
            }
        }
    }
    command->operations.unlock(command->operations.user);
}
Phase Poll(Command* command, std::uint64_t current_tick) noexcept
{
    Guard guard(command);
    if (!guard.acquired()) return ReadPhase(command);
    const Phase phase = ReadPhase(command);
    if (phase != Phase::Idle && !Terminal(phase))
        (void)CheckCancellation(command, current_tick);
    return ReadPhase(command);
}
void RequestCancellation(Command* command) noexcept
{ if (command) dluma_atomic_exchange(&command->cancel_requested, 1); }
bool ProvidersCanRetire(const Command* command) noexcept
{ return ReadPhase(command) == Phase::Released; }
bool ResetCompletedCommand(Command* command) noexcept
{
    Guard guard(command);
    if (!guard.acquired() || (ReadPhase(command) != Phase::Released &&
        ReadPhase(command) != Phase::Cancelled)) return false;
    command->plan = {};
    SetPhase(command, Phase::Idle);
    return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset
