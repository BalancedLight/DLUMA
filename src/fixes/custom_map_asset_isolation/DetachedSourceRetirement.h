#ifndef DLUMA_FIXES_DETACHED_SOURCE_RETIREMENT_H
#define DLUMA_FIXES_DETACHED_SOURCE_RETIREMENT_H

#include "../../dlw_win32.h"
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::provider_retirement {

constexpr std::size_t kMaximumRetirementSources = 128;
constexpr std::size_t kMaximumSourceConsumers = 256;
constexpr std::size_t kRetirementSourcePathCapacity = 512;

enum class SourceOwnership : std::uint8_t {
    Unknown, ExclusivePathCreated, Baseline, Shared, Borrowed,
};
enum class SourcePhase : std::uint8_t {
    Empty, Mounted, Detaching, Detached, Destroying, Destroyed, Quarantined,
};
enum class RetirementResult : std::uint8_t {
    Succeeded, AlreadyComplete, InvalidArgument, Busy, CapacityExceeded,
    OwnershipRejected, IdentityConflict, StaleHandle, GenerationMismatch,
    EvidenceUnavailable, VerificationFailed, ActiveConsumers,
    ConsumerReleaseIncomplete, LateConsumer, MutationFailed,
    MutationUncertain, ObservationLost, InvalidPhase,
};
enum class SourceLocation : std::uint8_t { Mounted, Detached };
enum class MutationOutcome : std::uint8_t {
    Succeeded,
    // This outcome is legal only when the callback proves it made no change.
    FailedWithoutMutation,
    // Includes exceptions/partial work. Never infer that destruction is safe.
    Uncertain,
};

struct SourceIdentity {
    std::uintptr_t source;
    std::uintptr_t source_vtable;
    std::uintptr_t original_registry_link;
    std::uint64_t source_generation;
    std::uint64_t owner_generation;
    std::uint32_t epoch;
    std::uint32_t successful_add_ordinal;
    char canonical_path[kRetirementSourcePathCapacity];
};

struct SourceHandle {
    std::uint64_t ledger_lifetime;
    std::uint64_t entry_generation;
    std::uint32_t index;
};
struct ConsumerToken {
    SourceHandle source;
    std::uint64_t reservation;
    std::uint32_t index;
};
struct RetirementContext {
    std::uint64_t transaction_generation;
    std::uint64_t source_registry_generation;
    std::uint32_t through_epoch;
};
struct ConsumerReleaseProof {
    RetirementContext context;
    // Must be the completed consumer-reset barrier of this same transaction,
    // before baseline reload. A final frontend verification is not required.
    std::uint64_t completed_release_generation;
    std::uint64_t source_consumer_revision;
    bool all_participating_consumers_released;
};

using VerifySourceFn = bool (*)(void* user, const SourceIdentity* identity,
    SourceLocation location, std::uint64_t* registry_generation) noexcept;
using MutateSourceFn = MutationOutcome (*)(void* user,
    const SourceIdentity* identity) noexcept;

struct RetirementCallbacks {
    VerifySourceFn verify;
    MutateSourceFn detach;
    MutateSourceFn destroy;
    void* user;
    // No native entry point is enabled here. The integrating exact-build
    // adapter may set these only after its evidence_assess is ready. In
    // particular detach must not call a path remover that also destroys.
    bool exact_detach_without_destroy_validated;
    bool exact_destroy_ownership_validated;
};

struct SourceRecord {
    SourceIdentity identity;
    std::uint64_t entry_generation;
    std::uint64_t detach_transaction_generation;
    std::uint64_t detached_registry_generation;
    std::uint64_t consumer_revision;
    std::uint32_t active_consumers;
    std::uint32_t detach_attempts;
    std::uint32_t destroy_attempts;
    SourcePhase phase;
    RetirementResult last_result;
    bool late_consumer_latched;
};
struct ConsumerReservation {
    SourceHandle source;
    std::uint64_t reservation;
    bool active;
};
struct DetachedSourceRetirementLedger {
    volatile LONG lock;
    volatile LONG mutation_lock;
    volatile LONG observation_lost;
    std::uint64_t lifetime_generation;
    std::uint64_t next_entry_generation;
    std::uint64_t next_reservation;
    SourceRecord sources[kMaximumRetirementSources];
    ConsumerReservation consumers[kMaximumSourceConsumers];
};

struct RetirementSummary {
    std::uint32_t mounted;
    std::uint32_t detached;
    std::uint32_t quarantined;
    std::uint32_t in_progress;
    std::uint32_t destroyed;
    std::uint32_t active_consumers;
    bool observation_lost;
};
RetirementResult SnapshotRetirementSummary(
    DetachedSourceRetirementLedger* ledger, RetirementSummary* output) noexcept;

// Pass zero-initialized storage once per process lifetime. Repeated calls
// with the same lifetime preserve every retained source and consumer token.
RetirementResult InitializeLedger(DetachedSourceRetirementLedger* ledger,
    std::uint64_t lifetime_generation) noexcept;
bool ExactSourceIdentityEqual(const SourceIdentity& left,
    const SourceIdentity& right) noexcept;
RetirementResult RegisterSource(DetachedSourceRetirementLedger* ledger,
    const SourceIdentity* identity, SourceOwnership ownership,
    SourceHandle* handle) noexcept;
RetirementResult SnapshotSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle handle, SourceRecord* output) noexcept;

// Reserve before a retain-capable original. A rejected reservation must not
// run that original. End is identity-checked; copied/stale tokens cannot
// decrement another consumer or a reused ledger slot.
RetirementResult BeginSourceConsumer(DetachedSourceRetirementLedger* ledger,
    SourceHandle source, ConsumerToken* token) noexcept;
RetirementResult EndSourceConsumer(DetachedSourceRetirementLedger* ledger,
    ConsumerToken* token) noexcept;
RetirementResult ObserveLateSourceConsumer(
    DetachedSourceRetirementLedger* ledger, SourceHandle source) noexcept;

RetirementResult DetachSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle source, const RetirementContext* context,
    const RetirementCallbacks* callbacks) noexcept;
RetirementResult DestroyDetachedSource(DetachedSourceRetirementLedger* ledger,
    SourceHandle source, const ConsumerReleaseProof* proof,
    const RetirementCallbacks* callbacks) noexcept;
const char* RetirementResultName(RetirementResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::provider_retirement
#endif
