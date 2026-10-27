#ifndef DLUMA_FIXES_STARTUP_BASELINE_CAPTURE_STATE_H
#define DLUMA_FIXES_STARTUP_BASELINE_CAPTURE_STATE_H

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {

/*
 * Startup B0 is a single, process-lifetime capture session.  The session is
 * deliberately independent from the returned-frontend verifier: once custom
 * staging closes this window, no later frontend may populate or replace B0.
 *
 * This owner is fixed-size POD and single-writer.  Production must advance it
 * only from the validated game-thread pump.  A report consumer may copy it
 * only under the caller's existing game-thread/consumer serialization.
 */

enum class StartupB0State : std::uint8_t {
    NeverSeen,
    FrontendCandidate,
    WaitingForStability,
    CapturingDomains,
    PartialDiagnostic,
    CompleteAuthorized,
    MissedBeforeFirstCustomEpoch,
    PermanentFailure,
};

enum class StartupB0TickSource : std::uint8_t {
    Unknown,
    OnFrame,
    FallbackTimer,
};

enum class StartupB0Blocker : std::uint8_t {
    None,
    NotGameThread,
    FrontendNotObserved,
    FrontendQueryFailed,
    FrontendNotStable,
    ActiveCustomEpoch,
    ActiveCustomSource,
    LocalLuaPresent,
    NonFrontendLevelActive,
    LedgerBusy,
    SourceRegistryBusy,
    SourceRegistryChanged,
    SourceInventoryIncomplete,
    PlayerInventoryIncomplete,
    PlayerSnapshotIncomplete,
    AudioInventoryIncomplete,
    GlobalDataIncomplete,
    FrontendResourcesIncomplete,
    LoaderStateIncomplete,
    RegistryBaselineIncomplete,
    SourceOwnerPublicationPending,
    ReturnedFrontendIneligible,
    InvalidDomainSnapshot,
    OnFrameOwnershipLost,
    PermanentSourceOwnerFailure,
    PermanentCaptureFailure,
    AudioMinimumHookPlanIncomplete,
    AudioMinimumSemanticSnapshotIncomplete,
};

enum class StartupB0Domain : std::uint8_t {
    VfsSourceInventory,
    Player,
    LoaderState,
    Audio,
    GlobalData,
    FrontendResources,
    Count,
};

enum class StartupB0DomainCaptureResult : std::uint8_t {
    NotAttempted,
    Ready,
    RetryBusy,
    RetryChanged,
    Incomplete,
    PermanentFailure,
};

enum class StartupB0DomainRecordResult : std::uint8_t {
    Rejected,
    RecordedDiagnostic,
    AcceptedReady,
    AlreadyReady,
    SessionClosed,
};

constexpr std::size_t kStartupB0DomainCount =
    static_cast<std::size_t>(StartupB0Domain::Count);
using StartupB0DomainMask = std::uint32_t;

constexpr StartupB0DomainMask StartupB0DomainBit(
    StartupB0Domain domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <
                   static_cast<std::uint8_t>(StartupB0Domain::Count)
        ? (1U << static_cast<std::uint8_t>(domain))
        : 0U;
}

constexpr StartupB0DomainMask kStartupB0AllDomainMask =
    (1U << static_cast<std::uint8_t>(StartupB0Domain::Count)) - 1U;

/* Startup admission requires exact source inventory/classification,
 * loader/frontend-core identity, a resolved Player baseline kind, and the
 * minimum Audio semantic baseline from one generation. A live PlayerState is
 * still optional at the main menu because an authoritative stable zero-owner
 * inventory commits PlayerStartupBaselineKind::Absent. */
constexpr StartupB0DomainMask kStartupB0TransactionCoreDomainMask =
    StartupB0DomainBit(StartupB0Domain::VfsSourceInventory) |
    StartupB0DomainBit(StartupB0Domain::Player) |
    StartupB0DomainBit(StartupB0Domain::LoaderState) |
    StartupB0DomainBit(StartupB0Domain::Audio);

/* Global Data and frontend-resource diagnostics remain optional until exact
 * outgoing-epoch attribution promotes them. Player and Audio are no longer
 * optional because an admitted project must have a resolvable exit plan. */

enum class StartupB0ProjectAdmissionDisposition : std::uint8_t {
    Defer,
    Allow,
    BlockRestart,
};

enum class StartupB0PlayerAudioPermanentFailure : std::uint8_t {
    None,
    PlayerGlobalStateSnapshotFailed,
    AudioManagerIdentityUnresolved,
    AudioScriptSnapshotFailed,
    AudioBankSnapshotFailed,
    AudioObserverRevisionUnpublished,
    AudioEventFenceUnpublished,
};

struct StartupB0PlayerAudioRetryGate {
    std::uint64_t retry_started_ms;
    std::uint64_t retry_deadline_ms;
    StartupB0PlayerAudioPermanentFailure permanent_failure;
    bool retry_expired;
    bool attempts_stopped;
};

enum class StartupB0WorkerOnFrameInvariantFailure : std::uint8_t {
    None,
    OnFrameActiveLevelQueryDiffers,
    B0PumpStopped,
    LevelSnapshotStale,
    QueryResultDiscarded,
    ThreadOwnershipValidationFailed,
};

constexpr std::uint64_t kStartupB0WorkerOnFrameInvariantFrameThreshold =
    120U;

struct StartupB0WorkerOnFrameInvariantEvidence {
    std::uint64_t frames_since_worker_frontend;
    std::uint64_t pump_calls_since_worker_frontend;
    std::uint64_t query_calls_since_worker_frontend;
    std::uint64_t query_successes_since_worker_frontend;
    std::uint64_t query_nulls_since_worker_frontend;
    std::uint64_t query_invalids_since_worker_frontend;
    bool worker_frontend_seen;
    bool on_frame_frontend_seen;
    bool level_snapshot_stale;
    bool thread_ownership_validation_failed;
};

struct StartupB0AdmissionSample {
    std::uint64_t sequence;
    std::uint64_t qpc;
    std::uint64_t frame_route_token;
    std::uint32_t thread_id;
    StartupB0TickSource tick_source;

    bool query_succeeded;
    bool frontend_active;
    bool validated_frame_route;
    bool non_frontend_level_active;
    bool returned_frontend;
    bool latched_frontend_identity_readable;

    std::uintptr_t frontend_level_pointer;
    std::uintptr_t frontend_level_vtable;
    std::uintptr_t latched_frontend_vtable;
    std::uint32_t current_epoch;
    std::uint32_t active_custom_sources;
    std::uint32_t local_lua_states;

    std::uint32_t source_registry_reported_count;
    std::uint32_t source_registry_observed_count;
    StartupB0DomainCaptureResult source_inventory_result;
    bool player_inventory_authoritative;
    StartupB0DomainCaptureResult player_snapshot_result;
    StartupB0DomainCaptureResult audio_snapshot_result;
    StartupB0DomainCaptureResult global_snapshot_result;
    StartupB0DomainCaptureResult frontend_snapshot_result;
    StartupB0DomainCaptureResult loader_pak_result;
    StartupB0DomainCaptureResult loader_xui_result;

    /* A caller-supplied, read-only admission result such as ledger or source
     * registry contention.  Custom-staging close conditions above always
     * take precedence over this transient reason. */
    StartupB0Blocker transient_blocker;
};

struct StartupB0DomainReadiness {
    StartupB0DomainCaptureResult result;
    StartupB0Blocker last_blocker;
    std::uint32_t attempt_count;
    std::uint64_t accepted_sequence;
    std::uint64_t accepted_generation;
    std::uint64_t accepted_snapshot_identity;
    bool ready;
};

/* These names intentionally mirror the deterministic report keys. */
struct StartupB0Diagnostics {
    StartupB0State startup_b0_state;
    std::uint64_t startup_b0_attempt_count;
    std::uint64_t startup_b0_transient_retry_count;
    /* The immutable source and exact serialized frame-route token that
     * established capture. The Windows dispatcher is diagnostic only and may
     * migrate between callbacks. */
    StartupB0TickSource startup_b0_tick_source;
    StartupB0TickSource startup_b0_last_diagnostic_tick_source;
    std::uint64_t startup_b0_query_success_count;
    std::uint64_t startup_b0_query_failure_count;
    std::uintptr_t startup_b0_frontend_level_pointer;
    std::uintptr_t startup_b0_frontend_level_vtable;
    std::uint64_t startup_b0_frontend_pointer_change_count;
    std::uint64_t startup_b0_first_frontend_tick_qpc;
    std::uint64_t startup_b0_stable_since_qpc;
    StartupB0Blocker startup_b0_first_blocker;
    StartupB0Blocker startup_b0_last_blocker;
    std::uint64_t startup_b0_frame_route_token;
    std::uint32_t startup_b0_last_dispatch_thread;
    std::uint32_t startup_b0_current_epoch;
    std::uint32_t startup_b0_active_custom_sources;
    std::uint32_t startup_b0_local_lua_states;
    std::uint32_t startup_b0_source_registry_reported_count;
    std::uint32_t startup_b0_source_registry_observed_count;
    StartupB0DomainCaptureResult startup_b0_source_inventory_result;
    bool startup_b0_player_inventory_authoritative;
    StartupB0DomainCaptureResult startup_b0_player_snapshot_result;
    StartupB0DomainCaptureResult startup_b0_audio_snapshot_result;
    StartupB0DomainCaptureResult startup_b0_global_snapshot_result;
    StartupB0DomainCaptureResult startup_b0_frontend_snapshot_result;
    StartupB0DomainCaptureResult startup_b0_loader_pak_result;
    StartupB0DomainCaptureResult startup_b0_loader_xui_result;
    StartupB0DomainMask startup_b0_ready_domain_mask;
    StartupB0DomainMask startup_b0_required_domain_mask;

    std::uint64_t startup_b0_on_frame_tick_count;
    std::uint64_t startup_b0_fallback_timer_tick_count;
    std::uint64_t startup_b0_authority_loss_count;
    std::uint32_t startup_b0_first_permanent_source_failure;
    std::uint32_t startup_b0_last_transient_source_failure;
    std::uint64_t startup_b0_source_owner_retry_count;
    std::uint64_t startup_b0_source_owner_retry_deadline;
    std::uint64_t startup_b0_state_revision;
    std::uint64_t startup_b0_diagnostic_revision;
    bool startup_b0_full_b0_captured;
    bool startup_b0_cold_reboot_baseline_authorized;
    bool startup_b0_window_closed;
    bool startup_b0_permanent_failure;
};

struct StartupB0Session {
    StartupB0Diagnostics diagnostics;
    StartupB0DomainReadiness domains[kStartupB0DomainCount];
    std::uint64_t session_generation;
    std::uint64_t stable_duration_qpc;
    std::uint64_t maximum_candidate_age_qpc;
    std::uint64_t last_valid_frontend_qpc;
};

struct StartupB0AdvanceResult {
    StartupB0State previous_state;
    StartupB0State current_state;
    StartupB0Blocker blocker;
    bool state_changed;
    bool diagnostics_changed;
    bool admission_ready;
    bool window_closed;
    /* Production logging is intentionally keyed only to this flag. */
    bool log_state_change;
};

bool InitializeStartupB0Session(
    StartupB0Session* session, std::uint64_t session_generation,
    StartupB0DomainMask required_domain_mask,
    std::uint64_t stable_duration_qpc,
    std::uint64_t maximum_candidate_age_qpc) noexcept;

bool InitializeStartupB0PlayerAudioRetryGate(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t stable_frontend_ms,
    std::uint64_t retry_window_ms) noexcept;
bool StartupB0PlayerAudioRetryAllowed(
    const StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms) noexcept;
bool ExpireStartupB0PlayerAudioRetry(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms,
    StartupB0PlayerAudioPermanentFailure failure) noexcept;
bool dluma_expire_unresolved_player_audio_b0_on_frame(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms,
    bool authoritative_on_frame,
    bool player_ready,
    bool audio_ready,
    StartupB0PlayerAudioPermanentFailure failure) noexcept;

StartupB0AdvanceResult AdvanceStartupB0Admission(
    StartupB0Session* session,
    const StartupB0AdmissionSample* sample) noexcept;

/* Call this at the first exact staging observation, rather than waiting for
 * a later frontend poll to infer that the startup window was missed. */
bool CloseStartupB0Window(
    StartupB0Session* session,
    StartupB0Blocker irreversible_reason) noexcept;

StartupB0DomainRecordResult RecordStartupB0DomainResult(
    StartupB0Session* session, StartupB0Domain domain,
    StartupB0DomainCaptureResult result, StartupB0Blocker blocker,
    std::uint64_t capture_sequence, std::uint64_t capture_generation,
    std::uint64_t snapshot_identity) noexcept;

/* Source-owner publication may briefly trail the VFS observation that caused
 * it. A retryable reason receives one bounded startup window; a permanent
 * reason, or expiration of that window, closes B0 deterministically. */
bool RecordStartupB0SourceOwnerFailure(
    StartupB0Session* session, std::uint32_t validation_result,
    bool retryable, std::uint64_t now_ms,
    std::uint64_t retry_window_ms) noexcept;

/* Registry authorization is intentionally separate from domain readiness.
 * It cannot become true until every required immutable domain snapshot is
 * accepted for this exact startup session generation. */
bool AuthorizeStartupB0RegistryBaseline(
    StartupB0Session* session,
    std::uint64_t capture_generation) noexcept;

void FailStartupB0Permanently(
    StartupB0Session* session,
    StartupB0Blocker blocker) noexcept;

bool RecordStartupB0AuthorityLoss(
    StartupB0Session* session) noexcept;

bool SnapshotStartupB0Diagnostics(
    const StartupB0Session* session,
    StartupB0Diagnostics* output) noexcept;

bool StartupB0BlockerIsTransient(StartupB0Blocker blocker) noexcept;
bool StartupB0StateRequiresOnFramePump(StartupB0State state) noexcept;
StartupB0ProjectAdmissionDisposition EvaluateStartupB0ProjectAdmission(
    StartupB0State state, bool isolation_enabled, bool tracking_live,
    bool registry_admission_allowed) noexcept;
StartupB0WorkerOnFrameInvariantFailure
ClassifyStartupB0WorkerOnFrameInvariant(
    const StartupB0WorkerOnFrameInvariantEvidence* evidence) noexcept;
const char* StartupB0StateName(StartupB0State state) noexcept;
const char* StartupB0TickSourceName(StartupB0TickSource source) noexcept;
const char* StartupB0BlockerName(StartupB0Blocker blocker) noexcept;
const char* StartupB0DomainName(StartupB0Domain domain) noexcept;
const char* StartupB0DomainCaptureResultName(
    StartupB0DomainCaptureResult result) noexcept;
const char* StartupB0WorkerOnFrameInvariantFailureName(
    StartupB0WorkerOnFrameInvariantFailure failure) noexcept;
const char* StartupB0PlayerAudioPermanentFailureName(
    StartupB0PlayerAudioPermanentFailure failure) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot

#endif
