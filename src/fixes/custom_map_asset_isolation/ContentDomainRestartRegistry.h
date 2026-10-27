#ifndef DLUMA_FIXES_CONTENT_DOMAIN_RESTART_REGISTRY_H
#define DLUMA_FIXES_CONTENT_DOMAIN_RESTART_REGISTRY_H

#include "../../dlw_win32.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {

constexpr std::size_t kMaximumContentDomainAdapters = 24;
constexpr std::size_t kContentDomainNameCapacity = 64;

enum class ContentDomainId : std::uint8_t {
    VfsSources = 0,
    PlayerGameplayParams,
    LocalizedTexts,
    Curves,
    CharacterDefinitions,
    AnimationEventDefinitions,
    BuffDefinitions,
    LifePlaceDefinitions,
    VehicleStateMachine,
    CollectableDefinitions,
    AppearanceDefinitions,
    ItemDefinitions,
    SkillDefinitions,
    QuestHubs,
    Quests,
    ContentOnDemand,
    /* Custom-backed persistent observations whose exact owning manager has
     * not yet been resolved.  Touching this domain deliberately prevents a
     * subset reset until an explicit adapter replaces the fail-closed owner. */
    UnclassifiedPersistentConsumers,
    Audio,
    FrontendVerification,
    /* Native pack/skin/screen catalogs and compiled XUI entry consumers.
     * Kept separate from loader widget state and the final admission check. */
    NativeFrontendEntries,
    NativeMaterialDefinitions,
    NativeRetainedResources,
    Count,
};

static_assert(static_cast<std::uint8_t>(ContentDomainId::Count) < 32U,
              "ContentDomainMask must represent every domain");

using ContentDomainMask = std::uint32_t;

constexpr ContentDomainMask DomainBit(ContentDomainId id) noexcept
{
    return static_cast<ContentDomainMask>(1U)
        << static_cast<std::uint8_t>(id);
}

enum class RestartPhase : std::uint8_t {
    Idle,
    BaselineCaptured,
    BarrierEntered,
    Preflighting,
    Quiescing,
    Cancelling,
    Mutating,
    Reloading,
    Verifying,
    Verified,
    RuntimeFailed,
    RetiringProviders,
};

enum class DomainResult : std::uint8_t {
    NotRun,
    Succeeded,
    Unsupported,
    Failed,
};

/* Ordinary producers and authenticated reset replay use different entry
 * points. Naming ResetOwnedReplay in the ordinary entry never grants access. */
enum class RetainingObservationOrigin : std::uint8_t {
    ExternalProducer = 0,
    ResetOwnedReplay,
};

/* POD token spanning the native original and all post-call classification.
 * Callers must Begin before the original and End exactly once afterwards. */
struct RetainingObservationToken {
    std::uintptr_t registry_identity;
    LONGLONG admission_revision;
    ContentDomainId domain;
    RetainingObservationOrigin origin;
    bool active;
    bool admitted;
    std::uint64_t replay_permit_generation;
    std::uint32_t replay_operation_index;
};

constexpr std::size_t kMaximumReplayOperations = 256;
constexpr std::size_t kReplayLogicalPathCapacity = 512;
enum class ReplayIdentityKind : std::uint8_t { InMemoryOwner, FileContent };
struct ReplayOperationIdentity {
    ContentDomainId domain;
    ReplayIdentityKind identity_kind;
    std::uint32_t operation_id;
    std::uint32_t ordinal;
    std::uint32_t parent_ordinal;
    std::uintptr_t owner;
    std::uintptr_t provider;
    std::uint64_t provider_generation;
    std::uint8_t content_sha256[32];
    char logical_path[kReplayLogicalPathCapacity];
};
struct RegistryReplayPermitToken {
    std::uintptr_t registry_identity;
    std::uint64_t generation;
    std::uint64_t transaction_generation;
};
struct ReplayFileAuthority {
    std::uintptr_t registry_identity;
    std::uint64_t permit_generation, observation, transaction_generation;
    std::uint32_t content_epoch, owner_thread_id;
};
struct RegistryReplayPermit {
    ReplayOperationIdentity operations[kMaximumReplayOperations];
    std::uint32_t active_stack[kMaximumReplayOperations];
    std::uint64_t generation;
    std::uint64_t transaction_generation;
    std::uint32_t content_epoch;
    std::uint32_t owner_thread_id;
    std::uint32_t count;
    std::uint32_t entered;
    std::uint32_t completed;
    std::uint32_t depth;
    ContentDomainId domain;
    RestartPhase phase;
    bool active;
    bool failed;
};

enum class RestartResult : std::uint8_t {
    NotRun,
    Verified,
    RuntimePartial,
    PreflightFailed,
    RuntimeFailed,
};

enum class RegistryEventType : std::uint8_t {
    BaselineCapture,
    Preflight,
    Quiesce,
    Cancel,
    Reset,
    Reload,
    Verify,
    TransactionResult,
    RetireProviders,
};

struct RestartContext {
    std::uint64_t baseline_generation;
    std::uint64_t transaction_generation;
    std::uint64_t sequence;
    std::uint32_t content_epoch;
    std::uint32_t owner_thread_id;
    void* user;
};

using ContentDomainPhaseFn = DomainResult (*)(
    void* user, const RestartContext* context) noexcept;
using RegistryEventSinkFn = void (*)(
    void* user, RegistryEventType event, ContentDomainId domain,
    RestartPhase phase, DomainResult result,
    const RestartContext* context) noexcept;

struct RegistryFinalizationToken {
    std::uintptr_t registry_identity;
    std::uint64_t transaction_generation;
    std::uint32_t content_epoch,owner_thread_id;
    ContentDomainMask required_mask,verified_mask;
    LONGLONG retaining_revision;
};
using RegistryFinalizeFn = bool (*)(void*,const RestartContext*,
    const RegistryFinalizationToken*) noexcept;

struct ContentDomainAdapter {
    ContentDomainId id;
    char name[kContentDomainNameCapacity];
    ContentDomainMask dependency_mask;
    bool enabled;
    bool mandatory;
    bool destructive;
    void* user;
    ContentDomainPhaseFn capture_baseline;
    ContentDomainPhaseFn preflight;
    ContentDomainPhaseFn quiesce;
    /* Optional reverse-order rollback for a quiesce callback that returned
     * Succeeded when the prepared transaction is cancelled before mutation.
     * A non-successful quiesce callback must be failure-atomic. */
    ContentDomainPhaseFn cancel_quiesce;
    ContentDomainPhaseFn reset;
    ContentDomainPhaseFn reload;
    ContentDomainPhaseFn verify;
    /* VFS-only: destroy detached providers after every participating consumer
     * reset has succeeded, and before any baseline reload can begin. Null is
     * reserved for adapters that do not retain detached provider ownership. */
    ContentDomainPhaseFn retire_providers;
};

struct ContentDomainAdapterState {
    ContentDomainId id;
    char name[kContentDomainNameCapacity];
    DomainResult baseline_result;
    DomainResult preflight_result;
    DomainResult quiesce_result;
    DomainResult cancel_result;
    DomainResult reset_result;
    DomainResult reload_result;
    DomainResult verify_result;
    DomainResult retire_providers_result;
    std::uint32_t baseline_calls;
    std::uint32_t preflight_calls;
    std::uint32_t quiesce_calls;
    std::uint32_t cancel_calls;
    std::uint32_t reset_calls;
    std::uint32_t reload_calls;
    std::uint32_t verify_calls;
    std::uint32_t retire_providers_calls;
    std::uint64_t verified_transaction_generation;
    std::uint64_t verified_baseline_generation;
    std::uint32_t verified_content_epoch;
    bool enabled;
    bool mandatory;
    bool touched;
    bool destructive;
    bool definition_valid;
};

struct ContentDomainRestartRegistry {
    volatile LONG lock;
    volatile LONG transaction_lock;
    ContentDomainAdapter adapters[kMaximumContentDomainAdapters];
    ContentDomainAdapterState states[kMaximumContentDomainAdapters];
    std::uint8_t order[kMaximumContentDomainAdapters];
    std::uint32_t count;
    std::uint32_t order_count;
    ContentDomainMask enabled_mask;
    ContentDomainMask mandatory_mask;
    /* Optional domains stay outside the transaction only while no custom
     * content has touched them.  Observers atomically promote a touched
     * domain into the required all-or-nothing set before preflight. */
    volatile LONG touched_mask;
    /* Lock-free observer/transaction handshake.  0 is idle, 1 is the
     * cancellable pre-mutation window, 2 is the irreversible mutation
     * window, 3 is final commit, and 4 records a producer that claimed the
     * commit window.  New observations in states 2-4 latch RuntimeFailed. */
    volatile LONG touch_transaction_state;
    volatile LONG late_touch_latched;
    volatile LONGLONG touch_revision;
    LONGLONG prepared_touch_revision;
    /* Retain-capable native calls reserve a slot before their original.  The
     * monotonically increasing revision detects even fast or repeated
     * same-domain calls whose active count has already returned to zero. */
    volatile LONG retaining_observation_active_calls;
    volatile LONGLONG retaining_observation_revision;
    LONGLONG prepared_retaining_observation_revision;
    ContentDomainMask baseline_ready_mask;
    ContentDomainMask prepared_participating_mask;
    ContentDomainMask prepared_required_mask;
    RestartPhase phase;
    RestartResult last_result;
    std::uint64_t completed_transaction_generation;
    std::uint64_t prepared_transaction_generation;
    std::uint32_t prepared_content_epoch;
    bool sealed;
    bool baseline_captured;
    bool mutation_started;
    bool transaction_prepared;
    bool prepared_optional_incomplete;
    volatile LONG runtime_failure_latched;
    volatile LONG admission_blocked;
    RegistryEventSinkFn event_sink;
    void* event_user;
    RegistryFinalizeFn finalize;
    void* finalize_user;
    RegistryReplayPermit replay_permit;
};

void InitializeRegistry(ContentDomainRestartRegistry* registry) noexcept;
void SetRegistryEventSink(ContentDomainRestartRegistry* registry,
                          RegistryEventSinkFn sink, void* user) noexcept;
bool ValidateAdapter(const ContentDomainAdapter* adapter) noexcept;
bool RegisterAdapter(ContentDomainRestartRegistry* registry,
                     const ContentDomainAdapter* adapter) noexcept;
bool SealRegistry(ContentDomainRestartRegistry* registry) noexcept;
// Configure before sealing. The callback runs after all required verification
// and the producer-commit CAS, before observation admission returns to Idle.
// It may retire verified bookkeeping, but cannot substitute for domain reset.
bool SetRegistryFinalizer(ContentDomainRestartRegistry*,RegistryFinalizeFn,void*) noexcept;
bool RegistryFinalizationIsCurrent(const ContentDomainRestartRegistry*,
    const RestartContext*,const RegistryFinalizationToken*) noexcept;
/* Read the context of the callback actually executing on this thread. The
 * prepared transaction tuple is intentionally absent during preflight and
 * quiesce. This observation does not grant replay or mutation authority. */
bool SnapshotRegistryCallbackContext(const ContentDomainRestartRegistry* registry,
    const RestartContext** context, ContentDomainId* domain,
    RestartPhase* phase) noexcept;
bool MarkDomainTouched(ContentDomainRestartRegistry* registry,
                       ContentDomainId id) noexcept;
bool DomainWasTouched(const ContentDomainRestartRegistry* registry,
                      ContentDomainId id) noexcept;
bool BeginRetainingObservation(
    ContentDomainRestartRegistry* registry, ContentDomainId id,
    RetainingObservationOrigin origin,
    RetainingObservationToken* token) noexcept;
bool EndRetainingObservation(
    ContentDomainRestartRegistry* registry,
    RetainingObservationToken* token) noexcept;

/* Only the currently executing registry reset/reload callback may issue a
 * permit. Expected operations are copied from its sealed B0 manifest. Native
 * observers must supply the actual resolved owner/provider/content identity;
 * a path match alone, TLS depth alone, or stale permit cannot authorize replay.
 * Operations enter in manifest order and return in native nesting order. */
bool BeginRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    const RestartContext* context, const ReplayOperationIdentity* operations,
    std::size_t count, RegistryReplayPermitToken* token) noexcept;
/* Returns only current callback authority, never expected operation fields.
 * The observer must still derive its own actual identity for admission. */
bool CurrentRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    ContentDomainId domain, RegistryReplayPermitToken* token) noexcept;
bool BeginRetainingReplayObservation(ContentDomainRestartRegistry* registry,
    const RegistryReplayPermitToken* permit,
    const ReplayOperationIdentity* observed, RetainingObservationToken* token) noexcept;
bool EndRegistryReplayPermit(ContentDomainRestartRegistry* registry,
    RegistryReplayPermitToken* token, bool native_succeeded) noexcept;

/* Export no expected path/provider/digest. The bridge supplies independently
 * observed file identity, matching the currently active FileContent operation.
 * The resulting authority expires with that operation and cannot cross threads. */
bool CaptureReplayFileAuthority(ContentDomainRestartRegistry*,
    const ReplayOperationIdentity* observed, ReplayFileAuthority*) noexcept;
bool ValidateReplayFileAuthority(ContentDomainRestartRegistry*,
    const ReplayFileAuthority*) noexcept;

RestartResult CaptureRegistryBaseline(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;
/* Adds newly available optional-domain B0 snapshots to the already captured
 * startup generation.  This is permitted only before any custom epoch/touch
 * or transaction exists; readiness is monotonic and existing B0 records are
 * never replaced. */
RestartResult ExtendRegistryBaseline(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context,
    ContentDomainMask requested_mask) noexcept;
RestartResult RunRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;
RestartResult PrepareRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;
RestartResult ExecutePreparedRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;
/* Called by the first destructive adapter immediately before its first
 * irreversible ownership change. A reset callback that rejects before this
 * marker remains a pre-mutation failure and is cancelled fail-open. */
bool MarkRegistryMutationStarted(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;
bool CancelPreparedRegistryTransaction(
    ContentDomainRestartRegistry* registry,
    const RestartContext* context) noexcept;

std::size_t SnapshotRegistryStates(
    ContentDomainRestartRegistry* registry,
    ContentDomainAdapterState* output, std::size_t capacity) noexcept;
bool CanAdmitCustomProject(
    const ContentDomainRestartRegistry* registry) noexcept;
/* A previous epoch's success cannot admit the current frontend. */
bool DomainRestoredInCurrentTransaction(
    const ContentDomainRestartRegistry* registry, ContentDomainId domain,
    const RestartContext* context) noexcept;

const char* ContentDomainName(ContentDomainId id) noexcept;
const char* RestartPhaseName(RestartPhase phase) noexcept;
const char* DomainResultName(DomainResult result) noexcept;
const char* RestartResultName(RestartResult result) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot

#endif
