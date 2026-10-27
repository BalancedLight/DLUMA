#ifndef DLUMA_FIXES_FRONTEND_CONTENT_COLD_REBOOT_H
#define DLUMA_FIXES_FRONTEND_CONTENT_COLD_REBOOT_H

#include "ContentDomainRestartRegistry.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {

constexpr std::size_t kColdRebootCriticalEventCapacity = 256;
constexpr std::size_t kTransitionFailureReasonCapacity = 192;

enum class ColdRebootState : std::uint8_t {
    Disabled,
    AwaitingBaseline,
    Armed,
    BarrierEntered,
    Preflighting,
    Mutating,
    Verifying,
    Verified,
    RuntimePartial,
    RuntimeFailed,
};

enum class ColdRebootEventType : std::uint16_t {
    ColdRebootBaselineCaptured,
    ColdRebootBarrierEnter,
    ColdRebootBarrierRejected,
    ColdRebootPreflightBegin,
    ColdRebootPreflightDomain,
    ColdRebootMutationBegin,
    ColdRebootDomainQuiesce,
    ColdRebootDomainCancel,
    ColdRebootDomainReset,
    ColdRebootDomainReload,
    ColdRebootDomainVerify,
    ColdRebootVerified,
    ColdRebootFailed,
    ColdRebootShowMenuMainAllowed,
    ColdRebootShowMenuMainBlocked,
    TransitionAttestationQuarantined,
    TransitionQuarantineCleared,
    NoLevelFailurePresentationScheduled,
    NoLevelFailurePresentationDelivered,
    ColdRebootCriticalOverflow,
};

enum class BarrierRejectReason : std::uint8_t {
    None,
    Disabled,
    BaselineUnavailable,
    NotArmed,
    WrongState,
    PleaseWaitInactive,
    PipelineWaitIncomplete,
    SessionStillAlive,
    ActiveLevelStillPresent,
    CustomPlayerStillPresent,
    SourceResetNotReady,
    NextProjectStagingStarted,
    WrongThread,
    Shutdown,
    SameProjectTransition,
    NestedTransaction,
    RepeatedEpoch,
    PreflightNotPrepared,
    MandatoryPreflightFailed,
    QuiesceCancelFailure,
    PostMutationFailure,
    CancelledPresentationCleanupFailed,
    DuplicateSourceOwnership,
    FrontendLookupBeforeReset,
    FrontendContaminated,
    TransitionAttestationFailed,
};

enum class ColdRebootOutcome : std::uint8_t {
    NotApplicable,
    Rejected,
    PreflightFailed,
    Verified,
    RuntimePartial,
    RuntimeFailed,
};

enum class ShowMenuMainDisposition : std::uint8_t {
    DelegateOriginal,
    BlockRestartRequired,
};

enum class FrontendLookupDisposition : std::uint8_t {
    AllowCleanFrontend,
    AllowVerifiedReset,
    LatchRestartRequired,
};

struct FrontendCompletionEvidence {
    std::uint32_t active_custom_source_count;
    std::uint32_t unconsumed_epoch_count;
    std::uint32_t armed_epoch;
    std::uint32_t prepared_epoch;
    std::uint32_t completed_epoch;
    std::uint64_t completed_transaction_generation;
    bool frontend_verified;
};

struct ColdRebootBarrierEvidence {
    void* game_di;
    void* outgoing_session;
    void* active_level;
    void* custom_player_di;
    std::uint32_t state;
    std::uint32_t content_epoch;
    std::uint32_t current_thread_id;
    std::uint32_t game_thread_id;
    std::uintptr_t caller_return_rva;
    bool exact_state_five_owner;
    bool please_wait_active;
    bool pipeline_wait_returned;
    bool session_destroyed;
    bool source_reset_ready;
    bool next_project_staging_started;
    bool shutdown_requested;
    bool same_project_transition;
};

struct ColdRebootPreflightEvidence {
    void* game_di;
    std::uint32_t state;
    std::uint32_t content_epoch;
    std::uint32_t current_thread_id;
    std::uint32_t game_thread_id;
    std::uintptr_t caller_return_rva;
    bool exact_state_five_owner;
    bool please_wait_active;
    bool pipeline_wait_returned;
    bool next_project_staging_started;
    bool shutdown_requested;
    bool same_project_transition;
};

struct ColdRebootCriticalEvent {
    std::uint64_t sequence;
    std::uint64_t qpc;
    std::uint64_t baseline_generation;
    std::uint64_t transaction_generation;
    std::uint32_t thread_id;
    std::uint32_t content_epoch;
    ColdRebootEventType type;
    ContentDomainId domain;
    RestartPhase phase;
    DomainResult domain_result;
    BarrierRejectReason reason;
    std::uint64_t value0;
    std::uint64_t value1;
};

using ColdRebootReadQpcFn = std::uint64_t (*)(void* user) noexcept;
using ColdRebootReadThreadIdFn = std::uint32_t (*)(void* user) noexcept;

struct ColdRebootPlatform {
    ColdRebootReadQpcFn read_qpc;
    ColdRebootReadThreadIdFn read_thread_id;
    void* user;
};

struct ColdRebootCriticalQueue {
    volatile LONG lock;
    volatile LONGLONG next_sequence;
    volatile LONG dropped;
    ColdRebootCriticalEvent records[kColdRebootCriticalEventCapacity];
    std::uint32_t head;
    std::uint32_t count;
    volatile LONG overflow_latched;
};

struct FrontendContentColdReboot {
    volatile LONG transaction_active;
    volatile LONG state;
    volatile LONG no_level_failure_presentation_delivery_claimed;
    volatile LONGLONG next_transaction_generation;
    ContentDomainRestartRegistry* registry;
    ColdRebootCriticalQueue* critical_queue;
    ColdRebootPlatform platform;
    std::uint64_t baseline_generation;
    std::uint64_t armed_epoch;
    std::uint64_t completed_epoch;
    std::uint64_t completed_transaction_generation;
    std::uint64_t prepared_transaction_generation;
    std::uint64_t no_level_failure_presentation_count;
    std::uint64_t no_level_failure_presentation_show_menu_attempt;
    std::uint32_t prepared_epoch;
    std::uint32_t quarantined_epoch;
    std::uint32_t no_level_failure_presentation_thread;
    RestartResult last_registry_result;
    BarrierRejectReason last_reject_reason;
    BarrierRejectReason no_level_failure_presentation_reason;
    bool master_enabled;
    bool baseline_captured;
    bool barrier_seen;
    bool mutation_started;
    bool restart_required;
    bool show_menu_guard_armed;
    bool preflight_prepared;
    bool active_epoch_quarantined;
    bool transition_attestation_failure;
    bool new_project_admission_blocked;
    bool cleanup_still_authorized;
    bool no_level_failure_presentation_scheduled;
    bool failure_panel_delivered;
    char transition_attestation_failure_reason[
        kTransitionFailureReasonCapacity];
};

void InitializeCriticalQueue(ColdRebootCriticalQueue* queue) noexcept;
bool PublishCriticalEvent(ColdRebootCriticalQueue* queue,
                          const ColdRebootPlatform* platform,
                          const ColdRebootCriticalEvent* event) noexcept;
std::size_t DrainCriticalEvents(ColdRebootCriticalQueue* queue,
                                ColdRebootCriticalEvent* output,
                                std::size_t capacity) noexcept;

void InitializeColdReboot(FrontendContentColdReboot* reboot,
                          ContentDomainRestartRegistry* registry,
                          ColdRebootCriticalQueue* critical_queue,
                          const ColdRebootPlatform* platform,
                          bool master_enabled) noexcept;
RestartResult CaptureColdRebootBaseline(
    FrontendContentColdReboot* reboot,
    const RestartContext* context) noexcept;
bool ArmColdRebootForEpoch(FrontendContentColdReboot* reboot,
                           std::uint32_t content_epoch) noexcept;
bool AbandonArmedColdRebootBeforeMutation(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch) noexcept;
bool QuarantineActiveEpochForCleanup(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch,
    const char* failure_reason) noexcept;
bool NoteQuarantinedCleanupVerified(
    FrontendContentColdReboot* reboot,
    std::uint32_t content_epoch) noexcept;
ColdRebootOutcome PrepareColdRebootAtStateFive(
    FrontendContentColdReboot* reboot,
    const ColdRebootPreflightEvidence* evidence) noexcept;
ColdRebootOutcome EnterColdRebootBarrier(
    FrontendContentColdReboot* reboot,
    const ColdRebootBarrierEvidence* evidence) noexcept;
ShowMenuMainDisposition ObserveShowMenuMain(
    FrontendContentColdReboot* reboot, std::uint32_t content_epoch,
    std::uint32_t current_thread_id) noexcept;
ShowMenuMainDisposition ObserveShowMenuMain(
    FrontendContentColdReboot* reboot,
    const FrontendCompletionEvidence* evidence,
    std::uint32_t current_thread_id) noexcept;
FrontendLookupDisposition ObserveFirstFrontendResourceLookup(
    FrontendContentColdReboot* reboot,
    const FrontendCompletionEvidence* evidence,
    std::uint32_t current_thread_id) noexcept;
bool CanAdmitCustomProject(
    const FrontendContentColdReboot* reboot) noexcept;
void LatchRestartRequired(
    FrontendContentColdReboot* reboot,
    BarrierRejectReason reason,
    std::uint32_t content_epoch) noexcept;
bool ClaimNoLevelFailurePresentation(
    FrontendContentColdReboot* reboot) noexcept;
void NoteNoLevelFailurePresentation(
    FrontendContentColdReboot* reboot,
    bool delivered,
    std::uint32_t current_thread_id) noexcept;

ColdRebootState CurrentColdRebootState(
    const FrontendContentColdReboot* reboot) noexcept;
const char* ColdRebootStateName(ColdRebootState state) noexcept;
const char* ColdRebootEventTypeName(ColdRebootEventType type) noexcept;
const char* BarrierRejectReasonName(BarrierRejectReason reason) noexcept;
const char* ColdRebootOutcomeName(ColdRebootOutcome outcome) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot

#endif
