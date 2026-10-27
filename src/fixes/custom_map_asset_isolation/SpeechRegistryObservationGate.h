#ifndef DLUMA_FIXES_SPEECH_REGISTRY_OBSERVATION_GATE_H
#define DLUMA_FIXES_SPEECH_REGISTRY_OBSERVATION_GATE_H

#include "../../dlw_win32.h"

#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::speech_registry {

/*
 * A reader/writer admission gate for the exact SpeechManager registry.
 *
 * Natural engine mutation hooks enter as writers.  Writers are deliberately
 * shared: nested and concurrent mutations can proceed without waiting for one
 * another.  A diagnostic snapshot is the only exclusive owner and can be
 * acquired only when the writer count is zero.  Snapshot acquisition never
 * waits.  A writer can wait only inside the Windows SRW primitive for the
 * bounded snapshot owner that was already admitted ahead of it; hook code
 * never performs a user-space spin loop.
 *
 * The gate and both tokens are fixed POD.  They own no memory and perform no
 * logging, file I/O, allocation, or engine calls.
 */
struct SpeechRegistryObservationGate {
    /* SRWLOCK is one opaque pointer in the Windows x64 ABI. */
    PVOID ownership_lock;
    /* High bit: snapshot owner. Low 31 bits: diagnostic writer count. */
    volatile LONG admission_state;
    /* Starts at one and advances once for every completed writer scope. */
    volatile LONGLONG revision;
    volatile LONGLONG completed_mutation_count;
};

struct SpeechRegistryMutationToken {
    SpeechRegistryObservationGate* gate;
    bool active;
    bool owns_shared_lock;
};

struct SpeechRegistrySnapshotToken {
    SpeechRegistryObservationGate* gate;
    std::uint64_t revision;
    std::uint64_t completed_mutation_count;
    bool active;
};

/* Pointer-free stamp retained after the exclusive snapshot has been copied. */
struct SpeechRegistryRevisionCountSnapshot {
    std::uint64_t revision;
    std::uint64_t completed_mutation_count;
    std::uint32_t observed_entry_count;
};

void InitializeSpeechRegistryObservationGate(
    SpeechRegistryObservationGate* gate) noexcept;

/*
 * Admits a natural registry mutation.  Concurrent and nested writers do not
 * exclude each other.  If a snapshot owns the gate, Windows parks/advises the
 * writer until that bounded reader releases it.  A successful token must be
 * ended once.
 */
bool BeginSpeechRegistryMutation(
    SpeechRegistryObservationGate* gate,
    SpeechRegistryMutationToken* token) noexcept;

/* Advances the revision/count and releases exactly one writer admission. */
bool EndSpeechRegistryMutation(
    SpeechRegistryMutationToken* token) noexcept;

/*
 * Attempts to acquire the sole snapshot ownership.  It returns immediately
 * when any writer or another snapshot is active.
 */
bool TryAcquireSpeechRegistrySnapshot(
    SpeechRegistryObservationGate* gate,
    SpeechRegistrySnapshotToken* token) noexcept;

/*
 * Publishes the stable revision plus the caller-observed registry entry count,
 * then releases snapshot ownership.  The caller must finish copying all
 * registry data before this call.
 */
bool CompleteSpeechRegistrySnapshot(
    SpeechRegistrySnapshotToken* token,
    std::uint32_t observed_entry_count,
    SpeechRegistryRevisionCountSnapshot* output) noexcept;

/* Releases a snapshot token without publishing a stamp. */
void AbandonSpeechRegistrySnapshot(
    SpeechRegistrySnapshotToken* token) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::speech_registry

#endif
