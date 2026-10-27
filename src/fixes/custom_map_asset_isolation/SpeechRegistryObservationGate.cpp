#define _HAS_EXCEPTIONS 0

#include "SpeechRegistryObservationGate.h"

extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockShared(PVOID lock);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockShared(PVOID lock);
__declspec(dllimport) BYTE WINAPI TryAcquireSRWLockExclusive(PVOID lock);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PVOID lock);
}

namespace dluma::fixes::custom_map_asset_isolation::speech_registry {
namespace {

constexpr LONG kSnapshotOwnerBit = (-2147483647L - 1L);
constexpr LONG kWriterCountMask = 0x7fffffffL;
constexpr LONGLONG kMaximumCounter = 0x7fffffffffffffffLL;

thread_local SpeechRegistryObservationGate* g_shared_writer_gate;
thread_local std::uint32_t g_shared_writer_depth;

LONG AtomicLoadState(
    const SpeechRegistryObservationGate* gate) noexcept
{
    return gate
        ? dluma_atomic_compare_exchange(
              const_cast<volatile LONG*>(&gate->admission_state), 0, 0)
        : 0;
}

std::uint64_t AtomicLoadCounter(const volatile LONGLONG* counter) noexcept
{
    return counter
        ? static_cast<std::uint64_t>(_InterlockedCompareExchange64(
              const_cast<volatile LONGLONG*>(counter), 0, 0))
        : 0;
}

void IncrementSaturating(volatile LONGLONG* counter) noexcept
{
    if (!counter) return;
    LONGLONG observed = _InterlockedCompareExchange64(counter, 0, 0);
    while (observed < kMaximumCounter) {
        const LONGLONG desired = observed + 1;
        const LONGLONG actual = _InterlockedCompareExchange64(
            counter, desired, observed);
        if (actual == observed) return;
        observed = actual;
    }
}

void ResetMutationToken(SpeechRegistryMutationToken* token) noexcept
{
    if (!token) return;
    token->gate = nullptr;
    token->active = false;
    token->owns_shared_lock = false;
}

void ResetSnapshotToken(SpeechRegistrySnapshotToken* token) noexcept
{
    if (!token) return;
    token->gate = nullptr;
    token->revision = 0;
    token->completed_mutation_count = 0;
    token->active = false;
}

void ResetRevisionCountSnapshot(
    SpeechRegistryRevisionCountSnapshot* output) noexcept
{
    if (!output) return;
    output->revision = 0;
    output->completed_mutation_count = 0;
    output->observed_entry_count = 0;
}

bool ReleaseSnapshotOwnership(
    SpeechRegistrySnapshotToken* token) noexcept
{
    if (!token || !token->active || !token->gate) return false;
    SpeechRegistryObservationGate* const gate = token->gate;
    const bool released = dluma_atomic_compare_exchange(
        &gate->admission_state, 0, kSnapshotOwnerBit) ==
        kSnapshotOwnerBit;
    ReleaseSRWLockExclusive(&gate->ownership_lock);
    ResetSnapshotToken(token);
    return released;
}

}  // namespace

void InitializeSpeechRegistryObservationGate(
    SpeechRegistryObservationGate* gate) noexcept
{
    if (!gate) return;
    gate->ownership_lock = nullptr;
    gate->admission_state = 0;
    gate->revision = 1;
    gate->completed_mutation_count = 0;
}

bool BeginSpeechRegistryMutation(
    SpeechRegistryObservationGate* gate,
    SpeechRegistryMutationToken* token) noexcept
{
    if (!gate || !token || token->active) return false;
    ResetMutationToken(token);

    const bool nested = g_shared_writer_gate == gate &&
        g_shared_writer_depth != 0U;
    if (!nested) {
        /* Production owns one gate. Reject an unsupported cross-gate nested
         * scope instead of losing which shared lock the thread must release. */
        if (g_shared_writer_depth != 0U) return false;
        AcquireSRWLockShared(&gate->ownership_lock);
        g_shared_writer_gate = gate;
    }

    const LONG admitted = dluma_atomic_increment(&gate->admission_state);
    if (admitted <= 0 ||
        (admitted & kSnapshotOwnerBit) != 0 ||
        (admitted & kWriterCountMask) == 0) {
        (void)dluma_atomic_decrement(&gate->admission_state);
        if (!nested) {
            g_shared_writer_gate = nullptr;
            ReleaseSRWLockShared(&gate->ownership_lock);
        }
        return false;
    }
    ++g_shared_writer_depth;
    token->gate = gate;
    token->active = true;
    token->owns_shared_lock = !nested;
    return true;
}

bool EndSpeechRegistryMutation(
    SpeechRegistryMutationToken* token) noexcept
{
    if (!token || !token->active || !token->gate) return false;
    SpeechRegistryObservationGate* const gate = token->gate;
    const LONG observed = AtomicLoadState(gate);
    if ((observed & kSnapshotOwnerBit) != 0 ||
        (observed & kWriterCountMask) == 0) {
        const bool nesting_valid = g_shared_writer_gate == gate &&
            g_shared_writer_depth != 0U;
        if (nesting_valid) --g_shared_writer_depth;
        if (token->owns_shared_lock) {
            if (nesting_valid && g_shared_writer_depth == 0U)
                g_shared_writer_gate = nullptr;
            ReleaseSRWLockShared(&gate->ownership_lock);
        }
        ResetMutationToken(token);
        return false;
    }

    /* Publish both counters before the last writer makes snapshots eligible. */
    IncrementSaturating(&gate->completed_mutation_count);
    IncrementSaturating(&gate->revision);
    const LONG remaining = dluma_atomic_decrement(&gate->admission_state);
    const bool nesting_valid = g_shared_writer_gate == gate &&
        g_shared_writer_depth != 0U;
    if (nesting_valid) --g_shared_writer_depth;
    if (token->owns_shared_lock) {
        if (nesting_valid && g_shared_writer_depth == 0U)
            g_shared_writer_gate = nullptr;
        ReleaseSRWLockShared(&gate->ownership_lock);
    }
    ResetMutationToken(token);
    return remaining >= 0 && nesting_valid;
}

bool TryAcquireSpeechRegistrySnapshot(
    SpeechRegistryObservationGate* gate,
    SpeechRegistrySnapshotToken* token) noexcept
{
    if (!gate || !token || token->active) return false;
    ResetSnapshotToken(token);

    if (!TryAcquireSRWLockExclusive(&gate->ownership_lock))
        return false;
    if (dluma_atomic_compare_exchange(
            &gate->admission_state, kSnapshotOwnerBit, 0) != 0) {
        ReleaseSRWLockExclusive(&gate->ownership_lock);
        return false;
    }

    token->gate = gate;
    token->revision = AtomicLoadCounter(&gate->revision);
    token->completed_mutation_count =
        AtomicLoadCounter(&gate->completed_mutation_count);
    token->active = true;
    return true;
}

bool CompleteSpeechRegistrySnapshot(
    SpeechRegistrySnapshotToken* token,
    std::uint32_t observed_entry_count,
    SpeechRegistryRevisionCountSnapshot* output) noexcept
{
    ResetRevisionCountSnapshot(output);
    if (!token || !token->active || !token->gate) return false;

    SpeechRegistryObservationGate* const gate = token->gate;
    const std::uint64_t revision = token->revision;
    const std::uint64_t completed_mutation_count =
        token->completed_mutation_count;
    const bool stable =
        AtomicLoadState(gate) == kSnapshotOwnerBit &&
        AtomicLoadCounter(&gate->revision) == revision &&
        AtomicLoadCounter(&gate->completed_mutation_count) ==
            completed_mutation_count;
    const bool released = ReleaseSnapshotOwnership(token);
    if (!stable || !released || !output) return false;

    output->revision = revision;
    output->completed_mutation_count = completed_mutation_count;
    output->observed_entry_count = observed_entry_count;
    return true;
}

void AbandonSpeechRegistrySnapshot(
    SpeechRegistrySnapshotToken* token) noexcept
{
    ReleaseSnapshotOwnership(token);
}

}  // namespace dluma::fixes::custom_map_asset_isolation::speech_registry
