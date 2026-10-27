#define _HAS_EXCEPTIONS 0

#include "StartupBaselineCaptureState.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::cold_reboot {
namespace {

void IncrementSaturating(std::uint64_t* value) noexcept
{
    if (value && *value != UINT64_MAX) ++*value;
}

void IncrementSaturating(std::uint32_t* value) noexcept
{
    if (value && *value != UINT32_MAX) ++*value;
}

bool ValidState(StartupB0State state) noexcept
{
    return static_cast<std::uint8_t>(state) <=
        static_cast<std::uint8_t>(StartupB0State::PermanentFailure);
}

bool ValidTickSource(StartupB0TickSource source) noexcept
{
    return static_cast<std::uint8_t>(source) <=
        static_cast<std::uint8_t>(StartupB0TickSource::FallbackTimer);
}

bool ValidBlocker(StartupB0Blocker blocker) noexcept
{
    return static_cast<std::uint8_t>(blocker) <=
        static_cast<std::uint8_t>(StartupB0Blocker::
                                      PermanentCaptureFailure);
}

bool ValidDomain(StartupB0Domain domain) noexcept
{
    return static_cast<std::uint8_t>(domain) <
        static_cast<std::uint8_t>(StartupB0Domain::Count);
}

bool ValidDomainResult(StartupB0DomainCaptureResult result) noexcept
{
    return static_cast<std::uint8_t>(result) <=
        static_cast<std::uint8_t>(StartupB0DomainCaptureResult::
                                      PermanentFailure);
}

bool TerminalState(StartupB0State state) noexcept
{
    return state == StartupB0State::CompleteAuthorized ||
        state == StartupB0State::MissedBeforeFirstCustomEpoch ||
        state == StartupB0State::PermanentFailure;
}

bool IrreversibleBlocker(StartupB0Blocker blocker) noexcept
{
    return blocker == StartupB0Blocker::ActiveCustomEpoch ||
        blocker == StartupB0Blocker::ActiveCustomSource ||
        blocker == StartupB0Blocker::LocalLuaPresent ||
        blocker == StartupB0Blocker::NonFrontendLevelActive ||
        blocker == StartupB0Blocker::ReturnedFrontendIneligible;
}

void TouchDiagnostics(StartupB0Session* session) noexcept
{
    if (session)
        IncrementSaturating(
            &session->diagnostics.startup_b0_diagnostic_revision);
}

void SetState(StartupB0Session* session, StartupB0State state) noexcept
{
    if (!session || session->diagnostics.startup_b0_state == state)
        return;
    session->diagnostics.startup_b0_state = state;
    IncrementSaturating(&session->diagnostics.startup_b0_state_revision);
    TouchDiagnostics(session);
}

void RecordBlocker(StartupB0Session* session,
                   StartupB0Blocker blocker) noexcept
{
    if (!session || blocker == StartupB0Blocker::None ||
        !ValidBlocker(blocker))
        return;
    StartupB0Diagnostics& diagnostics = session->diagnostics;
    if (diagnostics.startup_b0_first_blocker == StartupB0Blocker::None)
        diagnostics.startup_b0_first_blocker = blocker;
    if (diagnostics.startup_b0_last_blocker != blocker) {
        diagnostics.startup_b0_last_blocker = blocker;
        TouchDiagnostics(session);
    }
}

void RecordTransientRetry(StartupB0Session* session,
                          StartupB0Blocker blocker) noexcept
{
    if (!session || !StartupB0BlockerIsTransient(blocker)) return;
    IncrementSaturating(
        &session->diagnostics.startup_b0_transient_retry_count);
    RecordBlocker(session, blocker);
    TouchDiagnostics(session);
}

StartupB0State PartialState(const StartupB0Session& session) noexcept
{
    return session.diagnostics.startup_b0_ready_domain_mask
        ? StartupB0State::PartialDiagnostic
        : StartupB0State::CapturingDomains;
}

void RefreshCompletion(StartupB0Session* session) noexcept
{
    if (!session || TerminalState(session->diagnostics.startup_b0_state))
        return;
    StartupB0Diagnostics& diagnostics = session->diagnostics;
    const StartupB0DomainMask required =
        diagnostics.startup_b0_required_domain_mask;
    const bool full = required &&
        (diagnostics.startup_b0_ready_domain_mask & required) == required;
    if (diagnostics.startup_b0_full_b0_captured != full) {
        diagnostics.startup_b0_full_b0_captured = full;
        TouchDiagnostics(session);
    }
    if (!full) {
        diagnostics.startup_b0_cold_reboot_baseline_authorized = false;
        SetState(session, PartialState(*session));
        return;
    }
    if (diagnostics.startup_b0_cold_reboot_baseline_authorized)
        SetState(session, StartupB0State::CompleteAuthorized);
    else {
        SetState(session, StartupB0State::PartialDiagnostic);
        RecordBlocker(session,
                      StartupB0Blocker::RegistryBaselineIncomplete);
    }
}

StartupB0Blocker DefaultDomainBlocker(
    StartupB0Domain domain,
    StartupB0DomainCaptureResult result) noexcept
{
    if (result == StartupB0DomainCaptureResult::PermanentFailure)
        return StartupB0Blocker::PermanentCaptureFailure;
    if (domain == StartupB0Domain::VfsSourceInventory) {
        if (result == StartupB0DomainCaptureResult::RetryBusy)
            return StartupB0Blocker::SourceRegistryBusy;
        if (result == StartupB0DomainCaptureResult::RetryChanged)
            return StartupB0Blocker::SourceRegistryChanged;
        return StartupB0Blocker::SourceInventoryIncomplete;
    }
    if (domain == StartupB0Domain::Player)
        return StartupB0Blocker::PlayerSnapshotIncomplete;
    if (domain == StartupB0Domain::Audio)
        return StartupB0Blocker::AudioInventoryIncomplete;
    if (domain == StartupB0Domain::GlobalData)
        return StartupB0Blocker::GlobalDataIncomplete;
    if (domain == StartupB0Domain::FrontendResources)
        return StartupB0Blocker::FrontendResourcesIncomplete;
    if (domain == StartupB0Domain::LoaderState)
        return StartupB0Blocker::LoaderStateIncomplete;
    return StartupB0Blocker::InvalidDomainSnapshot;
}

void MirrorDomainResult(StartupB0Session* session, StartupB0Domain domain,
                        StartupB0DomainCaptureResult result) noexcept
{
    if (!session) return;
    StartupB0Diagnostics& diagnostics = session->diagnostics;
    switch (domain) {
    case StartupB0Domain::VfsSourceInventory:
        diagnostics.startup_b0_source_inventory_result = result;
        break;
    case StartupB0Domain::Player:
        diagnostics.startup_b0_player_snapshot_result = result;
        break;
    case StartupB0Domain::Audio:
        diagnostics.startup_b0_audio_snapshot_result = result;
        break;
    case StartupB0Domain::GlobalData:
        diagnostics.startup_b0_global_snapshot_result = result;
        break;
    case StartupB0Domain::FrontendResources:
        diagnostics.startup_b0_frontend_snapshot_result = result;
        break;
    case StartupB0Domain::LoaderState:
        diagnostics.startup_b0_loader_pak_result = result;
        diagnostics.startup_b0_loader_xui_result = result;
        break;
    default:
        break;
    }
}

StartupB0AdvanceResult MakeAdvanceResult(
    const StartupB0Session* session, StartupB0State previous_state,
    std::uint64_t previous_revision, StartupB0Blocker blocker,
    bool admission_ready) noexcept
{
    StartupB0AdvanceResult result = {};
    result.previous_state = previous_state;
    result.current_state = session
        ? session->diagnostics.startup_b0_state
        : StartupB0State::PermanentFailure;
    result.blocker = blocker;
    result.state_changed = result.current_state != previous_state;
    result.diagnostics_changed = session &&
        session->diagnostics.startup_b0_diagnostic_revision !=
            previous_revision;
    result.admission_ready = admission_ready;
    result.window_closed = session &&
        session->diagnostics.startup_b0_window_closed;
    result.log_state_change = result.state_changed;
    return result;
}

}  // namespace

bool InitializeStartupB0Session(
    StartupB0Session* session, std::uint64_t session_generation,
    StartupB0DomainMask required_domain_mask,
    std::uint64_t stable_duration_qpc,
    std::uint64_t maximum_candidate_age_qpc) noexcept
{
    if (!session) return false;
    std::memset(session, 0, sizeof(*session));
    session->diagnostics.startup_b0_state = StartupB0State::NeverSeen;
    session->diagnostics.startup_b0_required_domain_mask =
        required_domain_mask & kStartupB0AllDomainMask;
    session->session_generation = session_generation;
    session->stable_duration_qpc = stable_duration_qpc;
    session->maximum_candidate_age_qpc = maximum_candidate_age_qpc;
    if (!session_generation || !stable_duration_qpc ||
        maximum_candidate_age_qpc < stable_duration_qpc ||
        !session->diagnostics.startup_b0_required_domain_mask ||
        required_domain_mask !=
            session->diagnostics.startup_b0_required_domain_mask) {
        session->diagnostics.startup_b0_state =
            StartupB0State::PermanentFailure;
        session->diagnostics.startup_b0_first_blocker =
            StartupB0Blocker::PermanentCaptureFailure;
        session->diagnostics.startup_b0_last_blocker =
            StartupB0Blocker::PermanentCaptureFailure;
        session->diagnostics.startup_b0_state_revision = 1;
        session->diagnostics.startup_b0_diagnostic_revision = 1;
        return false;
    }
    return true;
}

bool InitializeStartupB0PlayerAudioRetryGate(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t stable_frontend_ms,
    std::uint64_t retry_window_ms) noexcept
{
    if (!gate || !stable_frontend_ms || !retry_window_ms ||
        stable_frontend_ms > UINT64_MAX - retry_window_ms)
        return false;
    gate->retry_started_ms = stable_frontend_ms;
    gate->retry_deadline_ms =
        stable_frontend_ms + retry_window_ms;
    gate->permanent_failure =
        StartupB0PlayerAudioPermanentFailure::None;
    gate->retry_expired = false;
    gate->attempts_stopped = false;
    return true;
}

bool StartupB0PlayerAudioRetryAllowed(
    const StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms) noexcept
{
    return gate && gate->retry_deadline_ms &&
        !gate->attempts_stopped &&
        now_ms < gate->retry_deadline_ms;
}

bool ExpireStartupB0PlayerAudioRetry(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms,
    StartupB0PlayerAudioPermanentFailure failure) noexcept
{
    if (!gate || !gate->retry_deadline_ms ||
        gate->attempts_stopped ||
        failure == StartupB0PlayerAudioPermanentFailure::None ||
        now_ms < gate->retry_deadline_ms)
        return false;
    gate->retry_expired = true;
    gate->attempts_stopped = true;
    gate->permanent_failure = failure;
    return true;
}

bool dluma_expire_unresolved_player_audio_b0_on_frame(
    StartupB0PlayerAudioRetryGate* gate,
    std::uint64_t now_ms,
    bool authoritative_on_frame,
    bool player_ready,
    bool audio_ready,
    StartupB0PlayerAudioPermanentFailure failure) noexcept
{
    if (!authoritative_on_frame || (player_ready && audio_ready))
        return false;
    return ExpireStartupB0PlayerAudioRetry(
        gate, now_ms, failure);
}

bool CloseStartupB0Window(
    StartupB0Session* session,
    StartupB0Blocker irreversible_reason) noexcept
{
    if (!session || !IrreversibleBlocker(irreversible_reason) ||
        !ValidState(session->diagnostics.startup_b0_state))
        return false;
    if (session->diagnostics.startup_b0_state ==
            StartupB0State::CompleteAuthorized)
        return false;
    if (session->diagnostics.startup_b0_state ==
            StartupB0State::PermanentFailure)
        return false;
    if (session->diagnostics.startup_b0_state ==
            StartupB0State::MissedBeforeFirstCustomEpoch)
        return true;
    session->diagnostics.startup_b0_window_closed = true;
    RecordBlocker(session, irreversible_reason);
    SetState(session, StartupB0State::MissedBeforeFirstCustomEpoch);
    TouchDiagnostics(session);
    return true;
}

StartupB0AdvanceResult AdvanceStartupB0Admission(
    StartupB0Session* session,
    const StartupB0AdmissionSample* sample) noexcept
{
    const StartupB0State previous_state = session
        ? session->diagnostics.startup_b0_state
        : StartupB0State::PermanentFailure;
    const std::uint64_t previous_revision = session
        ? session->diagnostics.startup_b0_diagnostic_revision : 0;
    if (!session || !sample || !session->session_generation ||
        !session->stable_duration_qpc ||
        !ValidState(previous_state) ||
        !ValidTickSource(sample ? sample->tick_source
                                : StartupB0TickSource::Unknown) ||
        (sample && !ValidBlocker(sample->transient_blocker))) {
        if (session)
            FailStartupB0Permanently(
                session, StartupB0Blocker::PermanentCaptureFailure);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::PermanentCaptureFailure, false);
    }
    if (TerminalState(previous_state))
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::None, false);

    StartupB0Diagnostics& diagnostics = session->diagnostics;
    diagnostics.startup_b0_last_diagnostic_tick_source =
        sample->tick_source;
    if (sample->tick_source == StartupB0TickSource::OnFrame)
        IncrementSaturating(&diagnostics.startup_b0_on_frame_tick_count);
    else if (sample->tick_source == StartupB0TickSource::FallbackTimer)
        IncrementSaturating(
            &diagnostics.startup_b0_fallback_timer_tick_count);

    /* Exact staging observations close the startup window independently of
     * whether the active-level query happened to succeed on this pump. */
    diagnostics.startup_b0_current_epoch = sample->current_epoch;
    diagnostics.startup_b0_active_custom_sources =
        sample->active_custom_sources;
    diagnostics.startup_b0_local_lua_states = sample->local_lua_states;
    StartupB0Blocker close_reason = StartupB0Blocker::None;
    if (sample->current_epoch)
        close_reason = StartupB0Blocker::ActiveCustomEpoch;
    else if (sample->active_custom_sources)
        close_reason = StartupB0Blocker::ActiveCustomSource;
    else if (sample->local_lua_states)
        close_reason = StartupB0Blocker::LocalLuaPresent;
    else if (sample->returned_frontend)
        close_reason = StartupB0Blocker::ReturnedFrontendIneligible;
    if (close_reason != StartupB0Blocker::None) {
        if (sample->query_succeeded)
            IncrementSaturating(
                &diagnostics.startup_b0_query_success_count);
        else
            IncrementSaturating(
                &diagnostics.startup_b0_query_failure_count);
        (void)CloseStartupB0Window(session, close_reason);
        TouchDiagnostics(session);
        return MakeAdvanceResult(
            session, previous_state, previous_revision, close_reason, false);
    }

    /* GameDI::OnFrame establishes one exact serialized route independently
     * of which worker executes a callback. A Windows thread ID is never an
     * immutable capture owner. */
    const bool on_frame_route_proven =
        sample->tick_source == StartupB0TickSource::OnFrame &&
        sample->validated_frame_route &&
        sample->frame_route_token != 0;
    if (on_frame_route_proven) {
        if (diagnostics.startup_b0_frame_route_token &&
            diagnostics.startup_b0_frame_route_token !=
                sample->frame_route_token) {
            RecordTransientRetry(session, StartupB0Blocker::NotGameThread);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::NotGameThread, false);
        }
        if (!diagnostics.startup_b0_frame_route_token) {
            diagnostics.startup_b0_frame_route_token =
                sample->frame_route_token;
            diagnostics.startup_b0_tick_source = StartupB0TickSource::OnFrame;
            TouchDiagnostics(session);
        }
        diagnostics.startup_b0_last_dispatch_thread =
            sample->thread_id;
    }

    const bool authoritative_source =
        sample->tick_source == StartupB0TickSource::OnFrame;
    if (!authoritative_source ||
        !sample->validated_frame_route ||
        !sample->frame_route_token ||
        !diagnostics.startup_b0_frame_route_token ||
        diagnostics.startup_b0_frame_route_token !=
            sample->frame_route_token) {
        if (sample->query_succeeded)
            IncrementSaturating(&diagnostics.startup_b0_query_success_count);
        else
            IncrementSaturating(&diagnostics.startup_b0_query_failure_count);
        RecordTransientRetry(session, StartupB0Blocker::NotGameThread);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::NotGameThread, false);
    }

    if (sample->query_succeeded) {
        IncrementSaturating(&diagnostics.startup_b0_query_success_count);
        diagnostics.startup_b0_source_registry_reported_count =
            sample->source_registry_reported_count;
        diagnostics.startup_b0_source_registry_observed_count =
            sample->source_registry_observed_count;
        diagnostics.startup_b0_source_inventory_result =
            sample->source_inventory_result;
        diagnostics.startup_b0_player_inventory_authoritative =
            sample->player_inventory_authoritative;
        diagnostics.startup_b0_player_snapshot_result =
            sample->player_snapshot_result;
        diagnostics.startup_b0_audio_snapshot_result =
            sample->audio_snapshot_result;
        diagnostics.startup_b0_global_snapshot_result =
            sample->global_snapshot_result;
        diagnostics.startup_b0_frontend_snapshot_result =
            sample->frontend_snapshot_result;
        diagnostics.startup_b0_loader_pak_result =
            sample->loader_pak_result;
        diagnostics.startup_b0_loader_xui_result =
            sample->loader_xui_result;
        TouchDiagnostics(session);
    } else {
        IncrementSaturating(&diagnostics.startup_b0_query_failure_count);
    }

    if (sample->query_succeeded && sample->non_frontend_level_active)
        close_reason = StartupB0Blocker::NonFrontendLevelActive;
    if (close_reason != StartupB0Blocker::None) {
        (void)CloseStartupB0Window(session, close_reason);
        return MakeAdvanceResult(
            session, previous_state, previous_revision, close_reason, false);
    }

    const bool candidate_state =
        diagnostics.startup_b0_state == StartupB0State::FrontendCandidate ||
        diagnostics.startup_b0_state == StartupB0State::WaitingForStability ||
        diagnostics.startup_b0_state == StartupB0State::CapturingDomains ||
        diagnostics.startup_b0_state == StartupB0State::PartialDiagnostic;
    bool use_latched_candidate = false;
    if (!sample->query_succeeded) {
        const bool candidate_present = candidate_state &&
            diagnostics.startup_b0_frontend_level_pointer &&
            diagnostics.startup_b0_frontend_level_vtable &&
            diagnostics.startup_b0_stable_since_qpc &&
            session->last_valid_frontend_qpc;
        const bool clock_valid = candidate_present &&
            sample->qpc >= diagnostics.startup_b0_stable_since_qpc &&
            sample->qpc >= session->last_valid_frontend_qpc;
        const bool candidate_fresh = clock_valid &&
            sample->qpc - session->last_valid_frontend_qpc <=
                session->maximum_candidate_age_qpc;
        const bool vtable_matches =
            sample->latched_frontend_identity_readable &&
            sample->latched_frontend_vtable ==
                diagnostics.startup_b0_frontend_level_vtable;
        if (candidate_present &&
            sample->latched_frontend_identity_readable &&
            !vtable_matches) {
            IncrementSaturating(
                &diagnostics.startup_b0_frontend_pointer_change_count);
            diagnostics.startup_b0_frontend_level_pointer = 0;
            diagnostics.startup_b0_frontend_level_vtable = 0;
            diagnostics.startup_b0_stable_since_qpc = 0;
            session->last_valid_frontend_qpc = 0;
            SetState(session, StartupB0State::NeverSeen);
        }
        if (!candidate_fresh || !vtable_matches) {
            RecordBlocker(session, StartupB0Blocker::FrontendQueryFailed);
            TouchDiagnostics(session);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::FrontendQueryFailed, false);
        }
        use_latched_candidate = true;
    }

    if (sample->query_succeeded &&
        sample->tick_source == StartupB0TickSource::FallbackTimer) {
        const bool identity_latched = candidate_state &&
            diagnostics.startup_b0_frontend_level_pointer &&
            diagnostics.startup_b0_frontend_level_vtable &&
            diagnostics.startup_b0_stable_since_qpc &&
            session->last_valid_frontend_qpc;
        if (!identity_latched) {
            RecordTransientRetry(
                session, StartupB0Blocker::FrontendNotObserved);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::FrontendNotObserved, false);
        }
        if (!sample->frontend_active ||
            sample->frontend_level_pointer !=
                diagnostics.startup_b0_frontend_level_pointer ||
            sample->frontend_level_vtable !=
                diagnostics.startup_b0_frontend_level_vtable) {
            IncrementSaturating(
                &diagnostics.startup_b0_frontend_pointer_change_count);
            RecordTransientRetry(
                session, StartupB0Blocker::FrontendNotStable);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::FrontendNotStable, false);
        }
        session->last_valid_frontend_qpc = sample->qpc;
        use_latched_candidate = true;
    }

    if (!use_latched_candidate &&
        (!sample->frontend_active || !sample->frontend_level_pointer ||
         !sample->frontend_level_vtable)) {
        RecordBlocker(session, StartupB0Blocker::FrontendNotObserved);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::FrontendNotObserved, false);
    }

    if (!use_latched_candidate) {
        if (!diagnostics.startup_b0_first_frontend_tick_qpc)
            diagnostics.startup_b0_first_frontend_tick_qpc = sample->qpc;
        const bool identity_changed =
            diagnostics.startup_b0_frontend_level_pointer &&
            (diagnostics.startup_b0_frontend_level_pointer !=
                 sample->frontend_level_pointer ||
             diagnostics.startup_b0_frontend_level_vtable !=
                 sample->frontend_level_vtable);
        const bool new_identity =
            diagnostics.startup_b0_frontend_level_pointer !=
                sample->frontend_level_pointer ||
            diagnostics.startup_b0_frontend_level_vtable !=
                sample->frontend_level_vtable;
        if (identity_changed)
            IncrementSaturating(
                &diagnostics.startup_b0_frontend_pointer_change_count);
        diagnostics.startup_b0_frontend_level_pointer =
            sample->frontend_level_pointer;
        diagnostics.startup_b0_frontend_level_vtable =
            sample->frontend_level_vtable;
        session->last_valid_frontend_qpc = sample->qpc;

        if (new_identity) {
            diagnostics.startup_b0_stable_since_qpc = sample->qpc;
            SetState(session, StartupB0State::FrontendCandidate);
            RecordTransientRetry(
                session, StartupB0Blocker::FrontendNotStable);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::FrontendNotStable, false);
        }
    }

    if (!diagnostics.startup_b0_stable_since_qpc ||
        sample->qpc < diagnostics.startup_b0_stable_since_qpc) {
        diagnostics.startup_b0_stable_since_qpc = sample->qpc;
        SetState(session, StartupB0State::WaitingForStability);
        RecordTransientRetry(
            session, StartupB0Blocker::FrontendNotStable);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::FrontendNotStable, false);
    }
    if (sample->qpc - diagnostics.startup_b0_stable_since_qpc <
            session->stable_duration_qpc) {
        SetState(session, StartupB0State::WaitingForStability);
        RecordTransientRetry(
            session, StartupB0Blocker::FrontendNotStable);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            StartupB0Blocker::FrontendNotStable, false);
    }

    IncrementSaturating(&diagnostics.startup_b0_attempt_count);
    TouchDiagnostics(session);
    if (sample->transient_blocker != StartupB0Blocker::None) {
        if (!StartupB0BlockerIsTransient(sample->transient_blocker)) {
            FailStartupB0Permanently(
                session, StartupB0Blocker::PermanentCaptureFailure);
            return MakeAdvanceResult(
                session, previous_state, previous_revision,
                StartupB0Blocker::PermanentCaptureFailure, false);
        }
        SetState(session, PartialState(*session));
        RecordTransientRetry(session, sample->transient_blocker);
        return MakeAdvanceResult(
            session, previous_state, previous_revision,
            sample->transient_blocker, false);
    }

    RefreshCompletion(session);
    /* Domain acceptance and registry authorization are deliberately separate.
     * A complete immutable domain set still needs validated game-thread
     * admission while the registry baseline is transiently incomplete. */
    const bool ready = !TerminalState(diagnostics.startup_b0_state) &&
        !diagnostics.startup_b0_window_closed &&
        !diagnostics.startup_b0_cold_reboot_baseline_authorized;
    return MakeAdvanceResult(
        session, previous_state, previous_revision,
        diagnostics.startup_b0_full_b0_captured
            ? StartupB0Blocker::RegistryBaselineIncomplete
            : StartupB0Blocker::None,
        ready);
}

StartupB0DomainRecordResult RecordStartupB0DomainResult(
    StartupB0Session* session, StartupB0Domain domain,
    StartupB0DomainCaptureResult result, StartupB0Blocker blocker,
    std::uint64_t capture_sequence, std::uint64_t capture_generation,
    std::uint64_t snapshot_identity) noexcept
{
    if (!session || !ValidDomain(domain) || !ValidDomainResult(result) ||
        result == StartupB0DomainCaptureResult::NotAttempted ||
        !ValidBlocker(blocker))
        return StartupB0DomainRecordResult::Rejected;
    const bool authorized_optional_fill =
        session->diagnostics.startup_b0_state ==
            StartupB0State::CompleteAuthorized &&
        (session->diagnostics.startup_b0_required_domain_mask &
         StartupB0DomainBit(domain)) == 0;
    if (TerminalState(session->diagnostics.startup_b0_state) &&
        !authorized_optional_fill)
        return StartupB0DomainRecordResult::SessionClosed;
    if (!authorized_optional_fill &&
        session->diagnostics.startup_b0_state !=
            StartupB0State::CapturingDomains &&
        session->diagnostics.startup_b0_state !=
            StartupB0State::PartialDiagnostic)
        return StartupB0DomainRecordResult::Rejected;
    const std::size_t index = static_cast<std::size_t>(domain);
    StartupB0DomainReadiness& readiness = session->domains[index];
    if (readiness.ready)
        return StartupB0DomainRecordResult::AlreadyReady;

    IncrementSaturating(&readiness.attempt_count);
    readiness.result = result;
    MirrorDomainResult(session, domain, result);
    TouchDiagnostics(session);

    if (result == StartupB0DomainCaptureResult::PermanentFailure) {
        if (blocker == StartupB0Blocker::None ||
            StartupB0BlockerIsTransient(blocker))
            blocker = StartupB0Blocker::PermanentCaptureFailure;
        readiness.last_blocker = blocker;
        FailStartupB0Permanently(session, blocker);
        return StartupB0DomainRecordResult::RecordedDiagnostic;
    }

    if (result != StartupB0DomainCaptureResult::Ready) {
        if (blocker == StartupB0Blocker::None)
            blocker = DefaultDomainBlocker(domain, result);
        if (!StartupB0BlockerIsTransient(blocker))
            blocker = DefaultDomainBlocker(domain, result);
        readiness.last_blocker = blocker;
        RecordTransientRetry(session, blocker);
        /*
         * CompleteAuthorized is the terminal admission decision for the
         * required transaction core. Optional domains may continue filling
         * diagnostics after that decision, but a transient optional miss
         * cannot revoke or oscillate the already authorized core baseline.
         */
        if (!authorized_optional_fill)
            SetState(session, PartialState(*session));
        return StartupB0DomainRecordResult::RecordedDiagnostic;
    }

    if (!capture_sequence || capture_generation !=
            session->session_generation || !snapshot_identity ||
        (domain == StartupB0Domain::Player &&
         !session->diagnostics.
              startup_b0_player_inventory_authoritative)) {
        readiness.result = StartupB0DomainCaptureResult::Incomplete;
        readiness.last_blocker =
            domain == StartupB0Domain::Player
                ? StartupB0Blocker::PlayerInventoryIncomplete
                : StartupB0Blocker::InvalidDomainSnapshot;
        MirrorDomainResult(
            session, domain, StartupB0DomainCaptureResult::Incomplete);
        RecordTransientRetry(session, readiness.last_blocker);
        if (!authorized_optional_fill)
            SetState(session, PartialState(*session));
        return StartupB0DomainRecordResult::RecordedDiagnostic;
    }

    readiness.ready = true;
    readiness.last_blocker = StartupB0Blocker::None;
    readiness.accepted_sequence = capture_sequence;
    readiness.accepted_generation = capture_generation;
    readiness.accepted_snapshot_identity = snapshot_identity;
    session->diagnostics.startup_b0_ready_domain_mask |=
        StartupB0DomainBit(domain);
    RefreshCompletion(session);
    TouchDiagnostics(session);
    return StartupB0DomainRecordResult::AcceptedReady;
}

bool RecordStartupB0SourceOwnerFailure(
    StartupB0Session* session, std::uint32_t validation_result,
    bool retryable, std::uint64_t now_ms,
    std::uint64_t retry_window_ms) noexcept
{
    if (!session || !validation_result ||
        TerminalState(session->diagnostics.startup_b0_state))
        return false;

    StartupB0Diagnostics& diagnostics = session->diagnostics;
    StartupB0DomainReadiness& source_readiness =
        session->domains[static_cast<std::size_t>(
            StartupB0Domain::VfsSourceInventory)];
    IncrementSaturating(&source_readiness.attempt_count);
    if (retryable) {
        diagnostics.startup_b0_last_transient_source_failure =
            validation_result;
        IncrementSaturating(
            &diagnostics.startup_b0_source_owner_retry_count);
        if (!diagnostics.startup_b0_source_owner_retry_deadline) {
            diagnostics.startup_b0_source_owner_retry_deadline =
                retry_window_ms > UINT64_MAX - now_ms
                    ? UINT64_MAX : now_ms + retry_window_ms;
        }
        if (now_ms <=
                diagnostics.startup_b0_source_owner_retry_deadline) {
            source_readiness.result =
                StartupB0DomainCaptureResult::RetryBusy;
            source_readiness.last_blocker =
                StartupB0Blocker::SourceOwnerPublicationPending;
            MirrorDomainResult(
                session, StartupB0Domain::VfsSourceInventory,
                source_readiness.result);
            RecordTransientRetry(
                session,
                StartupB0Blocker::SourceOwnerPublicationPending);
            SetState(session, PartialState(*session));
            return true;
        }
    }

    if (!diagnostics.startup_b0_first_permanent_source_failure)
        diagnostics.startup_b0_first_permanent_source_failure =
            validation_result;
    source_readiness.result =
        StartupB0DomainCaptureResult::PermanentFailure;
    source_readiness.last_blocker =
        StartupB0Blocker::PermanentSourceOwnerFailure;
    MirrorDomainResult(
        session, StartupB0Domain::VfsSourceInventory,
        source_readiness.result);
    diagnostics.startup_b0_permanent_failure = true;
    FailStartupB0Permanently(
        session, StartupB0Blocker::PermanentSourceOwnerFailure);
    return false;
}

bool AuthorizeStartupB0RegistryBaseline(
    StartupB0Session* session,
    std::uint64_t capture_generation) noexcept
{
    if (!session || TerminalState(session->diagnostics.startup_b0_state) ||
        capture_generation != session->session_generation ||
        !session->diagnostics.startup_b0_full_b0_captured)
        return false;
    const StartupB0DomainMask required =
        session->diagnostics.startup_b0_required_domain_mask;
    if (!required ||
        (session->diagnostics.startup_b0_ready_domain_mask & required) !=
            required)
        return false;
    session->diagnostics.startup_b0_cold_reboot_baseline_authorized = true;
    SetState(session, StartupB0State::CompleteAuthorized);
    TouchDiagnostics(session);
    return true;
}

void FailStartupB0Permanently(
    StartupB0Session* session,
    StartupB0Blocker blocker) noexcept
{
    if (!session || TerminalState(session->diagnostics.startup_b0_state))
        return;
    if (!ValidBlocker(blocker) || blocker == StartupB0Blocker::None)
        blocker = StartupB0Blocker::PermanentCaptureFailure;
    session->diagnostics.startup_b0_cold_reboot_baseline_authorized = false;
    session->diagnostics.startup_b0_permanent_failure = true;
    RecordBlocker(session, blocker);
    SetState(session, StartupB0State::PermanentFailure);
    TouchDiagnostics(session);
}

bool RecordStartupB0AuthorityLoss(
    StartupB0Session* session) noexcept
{
    if (!session) return false;
    StartupB0Diagnostics& diagnostics = session->diagnostics;
    if (diagnostics.startup_b0_authority_loss_count != 0)
        return false;
    IncrementSaturating(
        &diagnostics.startup_b0_authority_loss_count);
    diagnostics.startup_b0_cold_reboot_baseline_authorized = false;
    diagnostics.startup_b0_window_closed = true;
    diagnostics.startup_b0_permanent_failure = true;
    RecordBlocker(
        session, StartupB0Blocker::OnFrameOwnershipLost);
    if (diagnostics.startup_b0_state !=
            StartupB0State::MissedBeforeFirstCustomEpoch)
        SetState(session, StartupB0State::PermanentFailure);
    TouchDiagnostics(session);
    return true;
}

bool SnapshotStartupB0Diagnostics(
    const StartupB0Session* session,
    StartupB0Diagnostics* output) noexcept
{
    if (!session || !output) return false;
    *output = session->diagnostics;
    return true;
}

bool StartupB0BlockerIsTransient(StartupB0Blocker blocker) noexcept
{
    switch (blocker) {
    case StartupB0Blocker::NotGameThread:
    case StartupB0Blocker::FrontendNotObserved:
    case StartupB0Blocker::FrontendQueryFailed:
    case StartupB0Blocker::FrontendNotStable:
    case StartupB0Blocker::LedgerBusy:
    case StartupB0Blocker::SourceRegistryBusy:
    case StartupB0Blocker::SourceRegistryChanged:
    case StartupB0Blocker::SourceInventoryIncomplete:
    case StartupB0Blocker::PlayerInventoryIncomplete:
    case StartupB0Blocker::PlayerSnapshotIncomplete:
    case StartupB0Blocker::AudioInventoryIncomplete:
    case StartupB0Blocker::AudioMinimumSemanticSnapshotIncomplete:
    case StartupB0Blocker::GlobalDataIncomplete:
    case StartupB0Blocker::FrontendResourcesIncomplete:
    case StartupB0Blocker::LoaderStateIncomplete:
    case StartupB0Blocker::RegistryBaselineIncomplete:
    case StartupB0Blocker::SourceOwnerPublicationPending:
    case StartupB0Blocker::InvalidDomainSnapshot:
        return true;
    default:
        return false;
    }
}

bool StartupB0StateRequiresOnFramePump(StartupB0State state) noexcept
{
    switch (state) {
    case StartupB0State::NeverSeen:
    case StartupB0State::FrontendCandidate:
    case StartupB0State::WaitingForStability:
    case StartupB0State::CapturingDomains:
    case StartupB0State::PartialDiagnostic:
        return true;
    case StartupB0State::CompleteAuthorized:
    case StartupB0State::MissedBeforeFirstCustomEpoch:
    case StartupB0State::PermanentFailure:
    default:
        return false;
    }
}

StartupB0ProjectAdmissionDisposition EvaluateStartupB0ProjectAdmission(
    StartupB0State state, bool isolation_enabled, bool tracking_live,
    bool registry_admission_allowed) noexcept
{
    if (!isolation_enabled)
        return StartupB0ProjectAdmissionDisposition::Allow;
    if (!tracking_live || !ValidState(state))
        return StartupB0ProjectAdmissionDisposition::BlockRestart;
    if (StartupB0StateRequiresOnFramePump(state))
        return StartupB0ProjectAdmissionDisposition::Defer;
    if (state == StartupB0State::CompleteAuthorized)
        return registry_admission_allowed
            ? StartupB0ProjectAdmissionDisposition::Allow
            : StartupB0ProjectAdmissionDisposition::BlockRestart;
    return StartupB0ProjectAdmissionDisposition::BlockRestart;
}

StartupB0WorkerOnFrameInvariantFailure
ClassifyStartupB0WorkerOnFrameInvariant(
    const StartupB0WorkerOnFrameInvariantEvidence* evidence) noexcept
{
    std::uint64_t accounted_queries;

    if (!evidence || !evidence->worker_frontend_seen ||
        evidence->on_frame_frontend_seen ||
        evidence->frames_since_worker_frontend <=
            kStartupB0WorkerOnFrameInvariantFrameThreshold)
        return StartupB0WorkerOnFrameInvariantFailure::None;
    if (evidence->thread_ownership_validation_failed)
        return StartupB0WorkerOnFrameInvariantFailure::
            ThreadOwnershipValidationFailed;
    if (!evidence->pump_calls_since_worker_frontend)
        return StartupB0WorkerOnFrameInvariantFailure::B0PumpStopped;
    if (evidence->level_snapshot_stale)
        return StartupB0WorkerOnFrameInvariantFailure::LevelSnapshotStale;
    if (evidence->query_successes_since_worker_frontend >
            evidence->query_calls_since_worker_frontend ||
        evidence->query_nulls_since_worker_frontend >
            evidence->query_calls_since_worker_frontend -
                evidence->query_successes_since_worker_frontend ||
        evidence->query_invalids_since_worker_frontend >
            evidence->query_calls_since_worker_frontend -
                evidence->query_successes_since_worker_frontend -
                evidence->query_nulls_since_worker_frontend)
        return StartupB0WorkerOnFrameInvariantFailure::
            QueryResultDiscarded;
    accounted_queries =
        evidence->query_successes_since_worker_frontend +
        evidence->query_nulls_since_worker_frontend +
        evidence->query_invalids_since_worker_frontend;
    if (!evidence->query_calls_since_worker_frontend ||
        accounted_queries < evidence->query_calls_since_worker_frontend ||
        evidence->query_invalids_since_worker_frontend)
        return StartupB0WorkerOnFrameInvariantFailure::
            QueryResultDiscarded;
    return StartupB0WorkerOnFrameInvariantFailure::
        OnFrameActiveLevelQueryDiffers;
}

const char* StartupB0StateName(StartupB0State state) noexcept
{
    switch (state) {
    case StartupB0State::NeverSeen: return "NeverSeen";
    case StartupB0State::FrontendCandidate: return "FrontendCandidate";
    case StartupB0State::WaitingForStability:
        return "WaitingForStability";
    case StartupB0State::CapturingDomains: return "CapturingDomains";
    case StartupB0State::PartialDiagnostic: return "PartialDiagnostic";
    case StartupB0State::CompleteAuthorized: return "CompleteAuthorized";
    case StartupB0State::MissedBeforeFirstCustomEpoch:
        return "MissedBeforeFirstCustomEpoch";
    case StartupB0State::PermanentFailure: return "PermanentFailure";
    default: return "Unknown";
    }
}

const char* StartupB0TickSourceName(StartupB0TickSource source) noexcept
{
    switch (source) {
    case StartupB0TickSource::Unknown: return "Unknown";
    case StartupB0TickSource::OnFrame: return "OnFrame";
    case StartupB0TickSource::FallbackTimer: return "fallback_timer";
    default: return "Unknown";
    }
}

const char* StartupB0BlockerName(StartupB0Blocker blocker) noexcept
{
    switch (blocker) {
    case StartupB0Blocker::None: return "none";
    case StartupB0Blocker::NotGameThread: return "not_game_thread";
    case StartupB0Blocker::FrontendNotObserved:
        return "frontend_not_observed";
    case StartupB0Blocker::FrontendQueryFailed:
        return "frontend_query_failed";
    case StartupB0Blocker::FrontendNotStable:
        return "frontend_not_stable";
    case StartupB0Blocker::ActiveCustomEpoch:
        return "active_custom_epoch";
    case StartupB0Blocker::ActiveCustomSource:
        return "active_custom_source";
    case StartupB0Blocker::LocalLuaPresent: return "local_lua_present";
    case StartupB0Blocker::NonFrontendLevelActive:
        return "non_frontend_level_active";
    case StartupB0Blocker::LedgerBusy: return "ledger_busy";
    case StartupB0Blocker::SourceRegistryBusy:
        return "source_registry_busy";
    case StartupB0Blocker::SourceRegistryChanged:
        return "source_registry_changed";
    case StartupB0Blocker::SourceInventoryIncomplete:
        return "source_inventory_incomplete";
    case StartupB0Blocker::PlayerInventoryIncomplete:
        return "player_inventory_incomplete";
    case StartupB0Blocker::PlayerSnapshotIncomplete:
        return "player_snapshot_incomplete";
    case StartupB0Blocker::AudioInventoryIncomplete:
        return "audio_inventory_incomplete";
    case StartupB0Blocker::GlobalDataIncomplete:
        return "global_data_incomplete";
    case StartupB0Blocker::FrontendResourcesIncomplete:
        return "frontend_resources_incomplete";
    case StartupB0Blocker::LoaderStateIncomplete:
        return "loader_state_incomplete";
    case StartupB0Blocker::RegistryBaselineIncomplete:
        return "registry_baseline_incomplete";
    case StartupB0Blocker::SourceOwnerPublicationPending:
        return "source_owner_publication_pending";
    case StartupB0Blocker::ReturnedFrontendIneligible:
        return "returned_frontend_ineligible";
    case StartupB0Blocker::InvalidDomainSnapshot:
        return "invalid_domain_snapshot";
    case StartupB0Blocker::OnFrameOwnershipLost:
        return "onframe_ownership_lost";
    case StartupB0Blocker::PermanentSourceOwnerFailure:
        return "permanent_source_owner_failure";
    case StartupB0Blocker::PermanentCaptureFailure:
        return "permanent_capture_failure";
    case StartupB0Blocker::AudioMinimumHookPlanIncomplete:
        return "audio_minimum_hook_plan_incomplete";
    case StartupB0Blocker::AudioMinimumSemanticSnapshotIncomplete:
        return "audio_minimum_semantic_snapshot_incomplete";
    default: return "unknown";
    }
}

const char* StartupB0DomainName(StartupB0Domain domain) noexcept
{
    switch (domain) {
    case StartupB0Domain::VfsSourceInventory:
        return "VfsSourceInventory";
    case StartupB0Domain::Player: return "Player";
    case StartupB0Domain::LoaderState: return "LoaderState";
    case StartupB0Domain::Audio: return "Audio";
    case StartupB0Domain::GlobalData: return "GlobalData";
    case StartupB0Domain::FrontendResources: return "FrontendResources";
    default: return "Unknown";
    }
}

const char* StartupB0DomainCaptureResultName(
    StartupB0DomainCaptureResult result) noexcept
{
    switch (result) {
    case StartupB0DomainCaptureResult::NotAttempted:
        return "NotAttempted";
    case StartupB0DomainCaptureResult::Ready: return "Ready";
    case StartupB0DomainCaptureResult::RetryBusy: return "RetryBusy";
    case StartupB0DomainCaptureResult::RetryChanged:
        return "RetryChanged";
    case StartupB0DomainCaptureResult::Incomplete: return "Incomplete";
    case StartupB0DomainCaptureResult::PermanentFailure:
        return "PermanentFailure";
    default: return "Unknown";
    }
}

const char* StartupB0WorkerOnFrameInvariantFailureName(
    StartupB0WorkerOnFrameInvariantFailure failure) noexcept
{
    switch (failure) {
    case StartupB0WorkerOnFrameInvariantFailure::None:
        return "none";
    case StartupB0WorkerOnFrameInvariantFailure::
             OnFrameActiveLevelQueryDiffers:
        return "onframe_active_level_query_differs";
    case StartupB0WorkerOnFrameInvariantFailure::B0PumpStopped:
        return "b0_pump_stopped";
    case StartupB0WorkerOnFrameInvariantFailure::LevelSnapshotStale:
        return "level_snapshot_stale";
    case StartupB0WorkerOnFrameInvariantFailure::QueryResultDiscarded:
        return "query_result_discarded";
    case StartupB0WorkerOnFrameInvariantFailure::
             ThreadOwnershipValidationFailed:
        return "thread_ownership_validation_failed";
    default:
        return "unknown";
    }
}

const char* StartupB0PlayerAudioPermanentFailureName(
    StartupB0PlayerAudioPermanentFailure failure) noexcept
{
    switch (failure) {
    case StartupB0PlayerAudioPermanentFailure::None:
        return "none";
    case StartupB0PlayerAudioPermanentFailure::
            PlayerGlobalStateSnapshotFailed:
        return "player_global_state_snapshot_failed";
    case StartupB0PlayerAudioPermanentFailure::
            AudioManagerIdentityUnresolved:
        return "audio_manager_identity_unresolved";
    case StartupB0PlayerAudioPermanentFailure::
            AudioScriptSnapshotFailed:
        return "audio_script_snapshot_failed";
    case StartupB0PlayerAudioPermanentFailure::
            AudioBankSnapshotFailed:
        return "audio_bank_snapshot_failed";
    case StartupB0PlayerAudioPermanentFailure::
            AudioObserverRevisionUnpublished:
        return "audio_observer_revision_unpublished";
    case StartupB0PlayerAudioPermanentFailure::
            AudioEventFenceUnpublished:
        return "audio_event_fence_unpublished";
    default:
        return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::cold_reboot
