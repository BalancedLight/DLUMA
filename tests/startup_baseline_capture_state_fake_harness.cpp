#include "fixes/custom_map_asset_isolation/StartupBaselineCaptureState.h"
#include "dlw_native_companion_source_owner.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;

namespace {

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,  \
                         #expression);                                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

constexpr std::uint64_t kSessionGeneration = 41;
constexpr std::uint64_t kStableDuration = 100;
constexpr std::uint64_t kMaximumCandidateAge = 500;
constexpr std::uint64_t kFrameRouteToken = 0xD1A6A7E000000001ULL;
constexpr std::uintptr_t kFrontendLevel = 0x0000000181010000ULL;
constexpr std::uintptr_t kFrontendVtable = 0x0000000182020000ULL;

cold::StartupB0AdmissionSample FrontendSample(
    std::uint64_t qpc, cold::StartupB0TickSource tick_source =
                           cold::StartupB0TickSource::OnFrame) noexcept
{
    cold::StartupB0AdmissionSample sample = {};
    sample.sequence = qpc + 1;
    sample.qpc = qpc;
    sample.frame_route_token = kFrameRouteToken;
    sample.thread_id = 77;
    sample.tick_source = tick_source;
    sample.query_succeeded = true;
    sample.frontend_active = true;
    sample.validated_frame_route = true;
    sample.frontend_level_pointer = kFrontendLevel;
    sample.frontend_level_vtable = kFrontendVtable;
    sample.player_inventory_authoritative = true;
    return sample;
}

cold::StartupB0AdmissionSample FailedFrontendSample(
    std::uint64_t qpc,
    std::uintptr_t readable_vtable = kFrontendVtable) noexcept
{
    cold::StartupB0AdmissionSample sample = FrontendSample(qpc);
    sample.query_succeeded = false;
    sample.frontend_active = false;
    sample.frontend_level_pointer = 0;
    sample.frontend_level_vtable = 0;
    sample.latched_frontend_identity_readable = true;
    sample.latched_frontend_vtable = readable_vtable;
    return sample;
}

cold::StartupB0Session NewSession() noexcept
{
    cold::StartupB0Session session = {};
    (void)cold::InitializeStartupB0Session(
        &session, kSessionGeneration, cold::kStartupB0AllDomainMask,
        kStableDuration, kMaximumCandidateAge);
    return session;
}

bool ReachStableAdmission(cold::StartupB0Session* session,
                          std::uint64_t first_qpc = 1000) noexcept
{
    cold::StartupB0AdmissionSample sample = FrontendSample(first_qpc);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(session, &sample);
    CHECK(advance.current_state == cold::StartupB0State::FrontendCandidate);
    CHECK(!advance.admission_ready);
    CHECK(advance.log_state_change);

    sample = FrontendSample(first_qpc + kStableDuration - 1);
    advance = cold::AdvanceStartupB0Admission(session, &sample);
    CHECK(advance.current_state ==
          cold::StartupB0State::WaitingForStability);
    CHECK(!advance.admission_ready);

    sample = FrontendSample(first_qpc + kStableDuration);
    advance = cold::AdvanceStartupB0Admission(session, &sample);
    CHECK(advance.current_state == cold::StartupB0State::CapturingDomains);
    CHECK(advance.admission_ready);
    CHECK(session->diagnostics.startup_b0_attempt_count == 1);
    return true;
}

bool AcceptDomain(cold::StartupB0Session* session,
                  cold::StartupB0Domain domain,
                  std::uint64_t identity) noexcept
{
    CHECK(cold::RecordStartupB0DomainResult(
              session, domain, cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, identity + 100,
              kSessionGeneration, identity) ==
          cold::StartupB0DomainRecordResult::AcceptedReady);
    return true;
}

bool TestPODAndInitialization() noexcept
{
    static_assert(std::is_trivial<cold::StartupB0Session>::value,
                  "session must remain POD/trivial");
    static_assert(std::is_standard_layout<cold::StartupB0Session>::value,
                  "session must remain standard layout");
    static_assert(
        std::is_trivially_copyable<cold::StartupB0Diagnostics>::value,
        "diagnostics must remain trivially copyable");

    cold::StartupB0Session session = {};
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration, cold::kStartupB0AllDomainMask,
        kStableDuration, kMaximumCandidateAge));
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::NeverSeen);
    CHECK(session.diagnostics.startup_b0_ready_domain_mask == 0);
    CHECK(!session.diagnostics.startup_b0_full_b0_captured);
    CHECK(!session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::VfsSourceInventory,
              cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, 1, kSessionGeneration,
              1) == cold::StartupB0DomainRecordResult::Rejected);

    cold::StartupB0Session invalid = {};
    CHECK(!cold::InitializeStartupB0Session(
        &invalid, 0, cold::kStartupB0AllDomainMask, kStableDuration,
        kMaximumCandidateAge));
    CHECK(invalid.diagnostics.startup_b0_state ==
          cold::StartupB0State::PermanentFailure);
    return true;
}

bool TestFrameRouteAndTickProvenance() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample fallback = FrontendSample(
        100, cold::StartupB0TickSource::FallbackTimer);
    /* A window timer remains diagnostic and cannot establish frame-route
     * authority, even when it happens to run on a later dispatcher. */
    fallback.thread_id = 77;
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &fallback);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_frame_route_token == 0);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 0);
    CHECK(session.diagnostics.startup_b0_fallback_timer_tick_count == 1);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::Unknown);
    CHECK(session.diagnostics.startup_b0_last_diagnostic_tick_source ==
          cold::StartupB0TickSource::FallbackTimer);

    cold::StartupB0AdmissionSample worker = FrontendSample(
        150, cold::StartupB0TickSource::Unknown);
    worker.validated_frame_route = false;
    advance = cold::AdvanceStartupB0Admission(&session, &worker);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(session.diagnostics.startup_b0_frame_route_token == 0);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 0);

    cold::StartupB0AdmissionSample frame = FrontendSample(200);
    advance = cold::AdvanceStartupB0Admission(&session, &frame);
    CHECK(advance.current_state ==
          cold::StartupB0State::FrontendCandidate);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);
    CHECK(session.diagnostics.startup_b0_last_dispatch_thread == 77);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 200);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::OnFrame);

    worker = FrontendSample(225, cold::StartupB0TickSource::Unknown);
    worker.validated_frame_route = false;
    advance = cold::AdvanceStartupB0Admission(&session, &worker);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 200);
    CHECK(session.diagnostics.startup_b0_attempt_count == 0);

    fallback = FrontendSample(
        250, cold::StartupB0TickSource::FallbackTimer);
    fallback.thread_id = 77;
    advance = cold::AdvanceStartupB0Admission(&session, &fallback);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 200);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::OnFrame);
    CHECK(session.diagnostics.startup_b0_last_diagnostic_tick_source ==
          cold::StartupB0TickSource::FallbackTimer);

    cold::StartupB0AdmissionSample migrated = FrontendSample(300);
    migrated.thread_id = 88;
    advance = cold::AdvanceStartupB0Admission(&session, &migrated);
    CHECK(advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_last_dispatch_thread == 88);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::OnFrame);
    CHECK(session.diagnostics.startup_b0_on_frame_tick_count == 2);
    CHECK(session.diagnostics.startup_b0_fallback_timer_tick_count == 2);
    CHECK(session.diagnostics.startup_b0_first_blocker ==
          cold::StartupB0Blocker::NotGameThread);
    return true;
}

bool TestOnFrameClaimsRouteBeforeLevelQuerySucceeds() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = {};
    sample.sequence = 101;
    sample.qpc = 100;
    sample.frame_route_token = kFrameRouteToken;
    sample.thread_id = 77;
    sample.tick_source = cold::StartupB0TickSource::OnFrame;
    sample.validated_frame_route = true;

    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker ==
          cold::StartupB0Blocker::FrontendQueryFailed);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::OnFrame);
    CHECK(session.diagnostics.startup_b0_query_failure_count == 1);

    cold::StartupB0AdmissionSample wrong_timer = FrontendSample(
        250, cold::StartupB0TickSource::FallbackTimer);
    wrong_timer.thread_id = 88;
    advance = cold::AdvanceStartupB0Admission(&session, &wrong_timer);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);

    /* A later callback may migrate to another worker while retaining the
     * same exact serialized route token. */
    sample.sequence = 201;
    sample.qpc = 200;
    sample.thread_id = 88;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker ==
          cold::StartupB0Blocker::FrontendQueryFailed);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);
    CHECK(session.diagnostics.startup_b0_last_dispatch_thread == 88);
    CHECK(session.diagnostics.startup_b0_query_failure_count == 2);

    sample = FrontendSample(300);
    sample.thread_id = 99;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);
    CHECK(session.diagnostics.startup_b0_last_dispatch_thread == 99);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 300);
    return true;
}

bool TestTimerRemainsDiagnosticAfterFrameRouteLatch() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample frame = FrontendSample(100);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &frame);
    CHECK(advance.current_state ==
          cold::StartupB0State::FrontendCandidate);

    cold::StartupB0AdmissionSample timer = FrontendSample(
        150, cold::StartupB0TickSource::FallbackTimer);

    advance = cold::AdvanceStartupB0Admission(&session, &timer);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);
    CHECK(session.diagnostics.startup_b0_tick_source ==
          cold::StartupB0TickSource::OnFrame);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 100);
    return true;
}

bool TestTransactionCoreRequiresPlayerAndAudioBaselines() noexcept
{
    cold::StartupB0Session session = {};
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration,
        cold::kStartupB0TransactionCoreDomainMask,
        kStableDuration, kMaximumCandidateAge));
    CHECK(ReachStableAdmission(&session, 700));

    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::Player,
              cold::StartupB0DomainCaptureResult::Incomplete,
              cold::StartupB0Blocker::PlayerSnapshotIncomplete, 801,
              kSessionGeneration, 0) ==
          cold::StartupB0DomainRecordResult::RecordedDiagnostic);
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x701));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x702));

    const cold::StartupB0DomainMask core =
        cold::kStartupB0TransactionCoreDomainMask;
    const cold::StartupB0DomainMask player = cold::StartupB0DomainBit(
        cold::StartupB0Domain::Player);
    const cold::StartupB0DomainMask audio = cold::StartupB0DomainBit(
        cold::StartupB0Domain::Audio);
    CHECK(session.diagnostics.startup_b0_required_domain_mask == core);
    CHECK((session.diagnostics.startup_b0_ready_domain_mask & core) != core);
    CHECK((session.diagnostics.startup_b0_ready_domain_mask & player) == 0);
    CHECK((session.diagnostics.startup_b0_ready_domain_mask & audio) == 0);
    CHECK(!session.diagnostics.startup_b0_full_b0_captured);
    CHECK(!cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));

    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Player, 0x703));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Audio, 0x704));
    CHECK((session.diagnostics.startup_b0_ready_domain_mask & core) == core);
    CHECK(session.diagnostics.startup_b0_full_b0_captured);
    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CompleteAuthorized);
    return true;
}

bool TestAuthorizedCoreSurvivesOptionalDiagnosticRetry() noexcept
{
    using Admission = cold::StartupB0ProjectAdmissionDisposition;
    cold::StartupB0Session session = {};
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration,
        cold::kStartupB0TransactionCoreDomainMask,
        kStableDuration, kMaximumCandidateAge));
    CHECK(ReachStableAdmission(&session, 900));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x711));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x712));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Player, 0x713));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Audio, 0x714));
    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));

    const std::uint64_t authorized_state_revision =
        session.diagnostics.startup_b0_state_revision;
    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::GlobalData,
              cold::StartupB0DomainCaptureResult::Incomplete,
              cold::StartupB0Blocker::GlobalDataIncomplete, 901,
              kSessionGeneration, 0) ==
          cold::StartupB0DomainRecordResult::RecordedDiagnostic);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CompleteAuthorized);
    CHECK(session.diagnostics.startup_b0_state_revision ==
          authorized_state_revision);
    CHECK(session.diagnostics.startup_b0_full_b0_captured);
    CHECK(session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              session.diagnostics.startup_b0_state,
              true, true, true) == Admission::Allow);

    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::GlobalData,
              cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, 0,
              kSessionGeneration, 0x715) ==
          cold::StartupB0DomainRecordResult::RecordedDiagnostic);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CompleteAuthorized);
    CHECK(session.diagnostics.startup_b0_state_revision ==
          authorized_state_revision);

    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::GlobalData, 0x716));
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CompleteAuthorized);
    CHECK(session.diagnostics.startup_b0_state_revision ==
          authorized_state_revision);
    return true;
}

bool TestCompleteDomainsRemainAdmissibleForRegistryRetry() noexcept
{
    cold::StartupB0Session session = NewSession();
    CHECK(ReachStableAdmission(&session, 1500));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x301));
    CHECK(AcceptDomain(&session, cold::StartupB0Domain::Player, 0x302));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x303));
    CHECK(AcceptDomain(&session, cold::StartupB0Domain::Audio, 0x304));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::GlobalData, 0x305));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::FrontendResources, 0x306));
    CHECK(session.diagnostics.startup_b0_full_b0_captured);
    CHECK(!session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::PartialDiagnostic);

    const std::uint64_t attempts =
        session.diagnostics.startup_b0_attempt_count;
    cold::StartupB0AdmissionSample retry = FrontendSample(1700);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &retry);
    CHECK(advance.admission_ready);
    CHECK(advance.blocker ==
          cold::StartupB0Blocker::RegistryBaselineIncomplete);
    CHECK(session.diagnostics.startup_b0_attempt_count == attempts + 1);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::PartialDiagnostic);

    retry.qpc = 1800;
    retry.sequence = 1801;
    retry.thread_id = 88;
    advance = cold::AdvanceStartupB0Admission(&session, &retry);
    CHECK(advance.admission_ready);
    CHECK(advance.blocker ==
          cold::StartupB0Blocker::RegistryBaselineIncomplete);
    CHECK(session.diagnostics.startup_b0_attempt_count == attempts + 2);
    CHECK(session.diagnostics.startup_b0_full_b0_captured);

    retry.qpc = 1900;
    retry.sequence = 1901;
    retry.tick_source = cold::StartupB0TickSource::OnFrame;
    advance = cold::AdvanceStartupB0Admission(&session, &retry);
    CHECK(advance.admission_ready);
    CHECK(advance.blocker ==
          cold::StartupB0Blocker::RegistryBaselineIncomplete);
    CHECK(session.diagnostics.startup_b0_attempt_count == attempts + 3);

    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));
    retry.qpc = 2000;
    retry.sequence = 2001;
    advance = cold::AdvanceStartupB0Admission(&session, &retry);
    CHECK(!advance.admission_ready);
    CHECK(advance.current_state ==
          cold::StartupB0State::CompleteAuthorized);
    return true;
}

bool TestStableBoundaryAndPointerChanges() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = FrontendSample(1000);
    CHECK(!cold::AdvanceStartupB0Admission(&session, &sample).
              admission_ready);

    sample = FrontendSample(1050);
    sample.query_succeeded = false;
    sample.frontend_active = false;
    sample.frontend_level_pointer = 0;
    sample.frontend_level_vtable = 0;
    CHECK(!cold::AdvanceStartupB0Admission(&session, &sample).
              admission_ready);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 1000);

    sample = FrontendSample(1099);
    CHECK(!cold::AdvanceStartupB0Admission(&session, &sample).
              admission_ready);

    sample = FrontendSample(1100);
    CHECK(cold::AdvanceStartupB0Admission(&session, &sample).
              admission_ready);
    const std::uint64_t attempts =
        session.diagnostics.startup_b0_attempt_count;

    sample = FrontendSample(1200);
    sample.frontend_level_pointer = kFrontendLevel + 0x1000;
    CHECK(!cold::AdvanceStartupB0Admission(&session, &sample).
              admission_ready);
    CHECK(session.diagnostics.startup_b0_frontend_pointer_change_count == 1);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 1200);
    CHECK(session.diagnostics.startup_b0_attempt_count == attempts);

    sample = FrontendSample(1250);
    sample.frontend_level_pointer = kFrontendLevel + 0x1000;
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 1200);
    CHECK(session.diagnostics.startup_b0_frontend_pointer_change_count == 1);

    sample = FrontendSample(1350);
    sample.frontend_level_pointer = kFrontendLevel + 0x1000;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.admission_ready);
    CHECK(advance.log_state_change);

    sample.qpc = 1450;
    sample.sequence = 1451;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.admission_ready);
    CHECK(!advance.state_changed);
    CHECK(!advance.log_state_change);
    return true;
}

bool TestOneSuccessThenFailuresCompletesStability() noexcept
{
    constexpr std::uint64_t kRuntimeStableDuration = 1500;
    cold::StartupB0Session session = {};
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration, cold::kStartupB0AllDomainMask,
        kRuntimeStableDuration, 5000));

    cold::StartupB0AdmissionSample sample = FrontendSample(1000);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);

    bool became_ready = false;
    for (std::uint64_t index = 1; index <= 16; ++index) {
        sample = FailedFrontendSample(1000 + index * 100);
        advance = cold::AdvanceStartupB0Admission(&session, &sample);
        became_ready |= advance.admission_ready;
    }
    CHECK(became_ready);
    CHECK(session.diagnostics.startup_b0_attempt_count >= 1);
    CHECK(session.diagnostics.startup_b0_query_success_count == 1);
    CHECK(session.diagnostics.startup_b0_query_failure_count == 16);

    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x901));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Player, 0x902));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::FrontendResources, 0x903));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x904));
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::VfsSourceInventory)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::Player)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::FrontendResources)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::LoaderState)].attempt_count > 0);
    return true;
}

bool TestFailedQueryCandidateSafety() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = FrontendSample(1000);
    (void)cold::AdvanceStartupB0Admission(&session, &sample);

    sample = FailedFrontendSample(1050);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_frontend_level_pointer ==
          kFrontendLevel);
    CHECK(session.diagnostics.startup_b0_frontend_level_vtable ==
          kFrontendVtable);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc == 1000);
    CHECK(session.last_valid_frontend_qpc == 1000);

    cold::StartupB0Session stale = {};
    CHECK(cold::InitializeStartupB0Session(
        &stale, kSessionGeneration, cold::kStartupB0AllDomainMask,
        1500, 5000));
    sample = FrontendSample(1000);
    (void)cold::AdvanceStartupB0Admission(&stale, &sample);
    sample = FailedFrontendSample(6001);
    advance = cold::AdvanceStartupB0Admission(&stale, &sample);
    CHECK(!advance.admission_ready);
    CHECK(stale.diagnostics.startup_b0_attempt_count == 0);

    cold::StartupB0Session changed = NewSession();
    sample = FrontendSample(1000);
    (void)cold::AdvanceStartupB0Admission(&changed, &sample);
    sample = FailedFrontendSample(1050, kFrontendVtable + 0x1000);
    advance = cold::AdvanceStartupB0Admission(&changed, &sample);
    CHECK(!advance.admission_ready);
    CHECK(changed.diagnostics.startup_b0_frontend_level_pointer == 0);
    CHECK(changed.diagnostics.startup_b0_stable_since_qpc == 0);

    cold::StartupB0Session custom = NewSession();
    sample = FrontendSample(1000);
    (void)cold::AdvanceStartupB0Admission(&custom, &sample);
    sample = FailedFrontendSample(1100);
    sample.active_custom_sources = 1;
    advance = cold::AdvanceStartupB0Admission(&custom, &sample);
    CHECK(advance.window_closed);
    CHECK(custom.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::ActiveCustomSource);

    cold::StartupB0Session migrating_thread = NewSession();
    sample = FrontendSample(1000);
    (void)cold::AdvanceStartupB0Admission(
        &migrating_thread, &sample);
    sample = FailedFrontendSample(1100);
    sample.thread_id = 88;
    advance = cold::AdvanceStartupB0Admission(
        &migrating_thread, &sample);
    CHECK(advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::None);
    CHECK(migrating_thread.diagnostics.
              startup_b0_last_dispatch_thread == 88);
    CHECK(migrating_thread.diagnostics.startup_b0_attempt_count == 1);
    return true;
}

bool TestMigratingOnFrameLatchedIdentityReplay() noexcept
{
    constexpr std::uint64_t kRuntimeStableDuration = 1500;
    cold::StartupB0Session session = {};
    const cold::StartupB0DomainMask transaction_ready_mask =
        cold::kStartupB0TransactionCoreDomainMask;
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration, transaction_ready_mask,
        kRuntimeStableDuration, 5000));

    cold::StartupB0AdmissionSample sample = FrontendSample(100);
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.blocker == cold::StartupB0Blocker::FrontendNotStable);
    CHECK(session.diagnostics.startup_b0_frame_route_token ==
          kFrameRouteToken);

    for (std::uint64_t index = 0; index < 63; ++index) {
        sample = FailedFrontendSample(200 + index * 20U);
        sample.thread_id =
            static_cast<std::uint32_t>(77U + index % 8U);
        advance = cold::AdvanceStartupB0Admission(&session, &sample);
        CHECK(!advance.admission_ready);
        CHECK(advance.blocker ==
              cold::StartupB0Blocker::FrontendNotStable);
    }

    sample = FailedFrontendSample(100 + kRuntimeStableDuration);
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_attempt_count >= 1);
    CHECK(session.diagnostics.startup_b0_stable_since_qpc != 0);
    CHECK(session.diagnostics.startup_b0_on_frame_tick_count == 65);
    CHECK(session.diagnostics.startup_b0_fallback_timer_tick_count == 0);
    CHECK(session.diagnostics.startup_b0_query_failure_count == 64);
    CHECK(session.diagnostics.startup_b0_query_success_count == 1);

    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x801));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Player, 0x802));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x803));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Audio, 0x804));
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::VfsSourceInventory)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::Player)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::LoaderState)].attempt_count > 0);
    CHECK(session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::Audio)].attempt_count > 0);
    CHECK((session.diagnostics.startup_b0_ready_domain_mask &
           transaction_ready_mask) == transaction_ready_mask);
    CHECK(session.diagnostics.startup_b0_full_b0_captured);
    CHECK(session.session_generation > 0);
    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));
    CHECK(session.diagnostics.startup_b0_cold_reboot_baseline_authorized);
    return true;
}

bool TestTransientBlockersRetry() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = FrontendSample(2000);
    (void)cold::AdvanceStartupB0Admission(&session, &sample);
    sample = FrontendSample(2100);
    sample.transient_blocker = cold::StartupB0Blocker::LedgerBusy;
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(advance.blocker == cold::StartupB0Blocker::LedgerBusy);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CapturingDomains);
    const std::uint64_t retries =
        session.diagnostics.startup_b0_transient_retry_count;

    sample.qpc = 2200;
    sample.sequence = 2201;
    sample.transient_blocker =
        cold::StartupB0Blocker::SourceRegistryChanged;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_transient_retry_count ==
          retries + 1);

    sample.qpc = 2300;
    sample.sequence = 2301;
    sample.transient_blocker = cold::StartupB0Blocker::None;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_state !=
          cold::StartupB0State::PermanentFailure);
    return true;
}

bool TestSourceOwnerPublicationRetryAndPermanentFailure() noexcept
{
    cold::StartupB0Session transient = NewSession();
    CHECK(ReachStableAdmission(&transient, 2400));
    CHECK(cold::RecordStartupB0SourceOwnerFailure(
        &transient,
        DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED,
        true, 1000U, 100U));
    CHECK(transient.diagnostics.
              startup_b0_last_transient_source_failure ==
          DLUMA_COMPANION_ARCHIVE_OWNER_OWNER_SNAPSHOT_NOT_PUBLISHED);
    CHECK(transient.diagnostics.
              startup_b0_source_owner_retry_count == 1U);
    CHECK(transient.diagnostics.
              startup_b0_source_owner_retry_deadline == 1100U);
    CHECK(transient.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::SourceOwnerPublicationPending);
    CHECK(transient.diagnostics.startup_b0_state !=
          cold::StartupB0State::PermanentFailure);
    CHECK(AcceptDomain(
        &transient, cold::StartupB0Domain::VfsSourceInventory,
        0x901U));
    CHECK(transient.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::VfsSourceInventory)].ready);

    cold::StartupB0Session permanent = NewSession();
    CHECK(ReachStableAdmission(&permanent, 2600));
    CHECK(!cold::RecordStartupB0SourceOwnerFailure(
        &permanent,
        DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH,
        false, 2000U, 100U));
    CHECK(permanent.diagnostics.startup_b0_state ==
          cold::StartupB0State::PermanentFailure);
    CHECK(permanent.diagnostics.startup_b0_permanent_failure);
    CHECK(permanent.diagnostics.
              startup_b0_first_permanent_source_failure ==
          DLUMA_COMPANION_ARCHIVE_OWNER_MODULE_HASH_MISMATCH);
    CHECK(permanent.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::PermanentSourceOwnerFailure);
    CHECK(permanent.diagnostics.startup_b0_source_inventory_result ==
          cold::StartupB0DomainCaptureResult::PermanentFailure);

    cold::StartupB0Session expired = NewSession();
    CHECK(ReachStableAdmission(&expired, 2800));
    CHECK(cold::RecordStartupB0SourceOwnerFailure(
        &expired,
        DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED,
        true, 3000U, 100U));
    CHECK(!cold::RecordStartupB0SourceOwnerFailure(
        &expired,
        DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED,
        true, 3101U, 100U));
    CHECK(expired.diagnostics.startup_b0_state ==
          cold::StartupB0State::PermanentFailure);
    CHECK(expired.diagnostics.
              startup_b0_first_permanent_source_failure ==
          DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED);
    return true;
}

bool TestIncrementalImmutableDomains() noexcept
{
    cold::StartupB0Session session = NewSession();
    CHECK(ReachStableAdmission(&session, 3000));

    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0x101));
    CHECK(AcceptDomain(&session, cold::StartupB0Domain::Player, 0x102));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0x103));
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::PartialDiagnostic);
    CHECK(!session.diagnostics.startup_b0_full_b0_captured);
    CHECK(!session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);

    const std::size_t vfs_index = static_cast<std::size_t>(
        cold::StartupB0Domain::VfsSourceInventory);
    const std::uint64_t accepted_identity =
        session.domains[vfs_index].accepted_snapshot_identity;
    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::VfsSourceInventory,
              cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, 999, kSessionGeneration,
              0x999) == cold::StartupB0DomainRecordResult::AlreadyReady);
    CHECK(session.domains[vfs_index].accepted_snapshot_identity ==
          accepted_identity);

    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::Audio,
              cold::StartupB0DomainCaptureResult::Incomplete,
              cold::StartupB0Blocker::AudioInventoryIncomplete, 0,
              kSessionGeneration, 0) ==
          cold::StartupB0DomainRecordResult::RecordedDiagnostic);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::PartialDiagnostic);
    CHECK(!session.diagnostics.startup_b0_full_b0_captured);

    CHECK(AcceptDomain(&session, cold::StartupB0Domain::Audio, 0x104));
    CHECK(AcceptDomain(&session, cold::StartupB0Domain::GlobalData, 0x105));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::FrontendResources, 0x106));
    CHECK(session.diagnostics.startup_b0_full_b0_captured);
    CHECK(!session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::PartialDiagnostic);
    CHECK(!cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration + 1));
    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));
    CHECK(session.diagnostics.startup_b0_state ==
          cold::StartupB0State::CompleteAuthorized);
    CHECK(session.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    return true;
}

bool TestPlayerAuthorityAndDomainRetry() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = FrontendSample(4000);
    sample.player_inventory_authoritative = false;
    (void)cold::AdvanceStartupB0Admission(&session, &sample);
    sample.qpc = 4100;
    sample.sequence = 4101;
    (void)cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::Player,
              cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, 51, kSessionGeneration,
              0x200) ==
          cold::StartupB0DomainRecordResult::RecordedDiagnostic);
    CHECK(!session.domains[static_cast<std::size_t>(
              cold::StartupB0Domain::Player)].ready);
    CHECK(session.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::PlayerInventoryIncomplete);

    sample.qpc = 4200;
    sample.sequence = 4201;
    sample.player_inventory_authoritative = true;
    (void)cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(AcceptDomain(&session, cold::StartupB0Domain::Player, 0x201));
    return true;
}

bool TestIrreversibleCloseAndReturnedFrontendFreeze() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = FrontendSample(5000);
    (void)cold::AdvanceStartupB0Admission(&session, &sample);
    sample.qpc = 5050;
    sample.sequence = 5051;
    sample.active_custom_sources = 1;
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.window_closed);
    CHECK(advance.current_state ==
          cold::StartupB0State::MissedBeforeFirstCustomEpoch);
    CHECK(session.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::ActiveCustomSource);
    const cold::StartupB0Diagnostics frozen = session.diagnostics;

    sample = FrontendSample(9000);
    sample.returned_frontend = true;
    advance = cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(!advance.admission_ready);
    CHECK(session.diagnostics.startup_b0_query_success_count ==
          frozen.startup_b0_query_success_count);
    CHECK(cold::RecordStartupB0DomainResult(
              &session, cold::StartupB0Domain::VfsSourceInventory,
              cold::StartupB0DomainCaptureResult::Ready,
              cold::StartupB0Blocker::None, 100, kSessionGeneration,
              100) == cold::StartupB0DomainRecordResult::SessionClosed);

    cold::StartupB0Session direct = NewSession();
    CHECK(cold::CloseStartupB0Window(
        &direct, cold::StartupB0Blocker::LocalLuaPresent));
    CHECK(direct.diagnostics.startup_b0_state ==
          cold::StartupB0State::MissedBeforeFirstCustomEpoch);
    CHECK(!cold::CloseStartupB0Window(
        &direct, cold::StartupB0Blocker::LedgerBusy));

    cold::StartupB0Session local_lua = NewSession();
    sample = FrontendSample(5100);
    sample.local_lua_states = 1;
    advance = cold::AdvanceStartupB0Admission(&local_lua, &sample);
    CHECK(advance.window_closed);
    CHECK(local_lua.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::LocalLuaPresent);

    cold::StartupB0Session non_frontend = NewSession();
    sample = FrontendSample(5200);
    sample.frontend_active = false;
    sample.non_frontend_level_active = true;
    advance = cold::AdvanceStartupB0Admission(&non_frontend, &sample);
    CHECK(advance.window_closed);
    CHECK(non_frontend.diagnostics.startup_b0_last_blocker ==
          cold::StartupB0Blocker::NonFrontendLevelActive);
    return true;
}

bool TestStagingClosesEvenWhenFrontendQueryFails() noexcept
{
    for (int case_index = 0; case_index != 2; ++case_index) {
        cold::StartupB0Session session = NewSession();
        cold::StartupB0AdmissionSample sample = {};
        sample.sequence = 10;
        sample.qpc = 10;
        sample.thread_id = 77;
        sample.tick_source = cold::StartupB0TickSource::OnFrame;
        if (case_index == 0)
            sample.current_epoch = 9;
        else
            sample.active_custom_sources = 1;
        const cold::StartupB0AdvanceResult advance =
            cold::AdvanceStartupB0Admission(&session, &sample);
        CHECK(advance.window_closed);
        CHECK(advance.current_state ==
              cold::StartupB0State::MissedBeforeFirstCustomEpoch);
        CHECK(session.diagnostics.startup_b0_query_failure_count == 1);
        CHECK(session.diagnostics.startup_b0_last_blocker ==
              (case_index == 0
                   ? cold::StartupB0Blocker::ActiveCustomEpoch
                   : cold::StartupB0Blocker::ActiveCustomSource));
    }
    return true;
}

bool TestHundredsOfConsecutiveOnFrameSamples() noexcept
{
    cold::StartupB0Session session = {};
    CHECK(cold::InitializeStartupB0Session(
        &session, kSessionGeneration,
        cold::kStartupB0TransactionCoreDomainMask,
        kStableDuration, kMaximumCandidateAge));
    for (std::uint64_t index = 0; index < 512; ++index) {
        const cold::StartupB0AdmissionSample sample =
            FrontendSample(1000 + index);
        const cold::StartupB0AdvanceResult advance =
            cold::AdvanceStartupB0Admission(&session, &sample);
        CHECK(advance.current_state !=
              cold::StartupB0State::MissedBeforeFirstCustomEpoch);
        CHECK(advance.current_state !=
              cold::StartupB0State::PermanentFailure);
    }
    CHECK(session.diagnostics.startup_b0_on_frame_tick_count == 512);
    CHECK(cold::StartupB0StateRequiresOnFramePump(
        session.diagnostics.startup_b0_state));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::VfsSourceInventory, 0xA01));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::LoaderState, 0xA02));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Player, 0xA03));
    CHECK(AcceptDomain(
        &session, cold::StartupB0Domain::Audio, 0xA04));
    CHECK(cold::AuthorizeStartupB0RegistryBaseline(
        &session, kSessionGeneration));
    CHECK(!cold::StartupB0StateRequiresOnFramePump(
        session.diagnostics.startup_b0_state));
    return true;
}

bool TestPlayerAudioDeadlineExpiresOnAuthoritativeFrameExactlyOnce() noexcept
{
    cold::StartupB0PlayerAudioRetryGate gate = {};
    constexpr std::uint64_t kStartMs = 1000;
    constexpr std::uint64_t kWindowMs = 10000;
    constexpr std::uint64_t kDeadlineMs = kStartMs + kWindowMs;
    using Failure = cold::StartupB0PlayerAudioPermanentFailure;

    CHECK(cold::InitializeStartupB0PlayerAudioRetryGate(
        &gate, kStartMs, kWindowMs));
    CHECK(gate.retry_started_ms == kStartMs);
    CHECK(gate.retry_deadline_ms == kDeadlineMs);
    CHECK(cold::StartupB0PlayerAudioRetryAllowed(
        &gate, kDeadlineMs - 1));
    CHECK(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, kDeadlineMs - 1, true, false, false,
        Failure::PlayerGlobalStateSnapshotFailed));

    /* A timer/fallback observation at the deadline cannot close B0. */
    CHECK(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, kDeadlineMs, false, false, false,
        Failure::PlayerGlobalStateSnapshotFailed));
    CHECK(!gate.retry_expired);
    CHECK(!gate.attempts_stopped);

    /* The active-level query result is deliberately absent from this pure
     * OnFrame gate. An authoritative frame must expire the unresolved pair
     * even when the caller's active-level query failed. */
    const bool active_level_query_succeeded = false;
    CHECK(!active_level_query_succeeded);
    CHECK(cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, kDeadlineMs, true, false, false,
        Failure::PlayerGlobalStateSnapshotFailed));
    CHECK(gate.retry_expired);
    CHECK(gate.attempts_stopped);
    CHECK(gate.permanent_failure ==
          Failure::PlayerGlobalStateSnapshotFailed);
    CHECK(!cold::StartupB0PlayerAudioRetryAllowed(
        &gate, kDeadlineMs + 1));

    /* The terminal transition is one-shot and keeps the first reason. */
    CHECK(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &gate, kDeadlineMs + 1, true, false, false,
        Failure::AudioScriptSnapshotFailed));
    CHECK(gate.permanent_failure ==
          Failure::PlayerGlobalStateSnapshotFailed);

    cold::StartupB0PlayerAudioRetryGate complete = {};
    CHECK(cold::InitializeStartupB0PlayerAudioRetryGate(
        &complete, kStartMs, kWindowMs));
    CHECK(!cold::dluma_expire_unresolved_player_audio_b0_on_frame(
        &complete, kDeadlineMs, true, true, true,
        Failure::AudioScriptSnapshotFailed));
    CHECK(!complete.retry_expired);
    CHECK(!complete.attempts_stopped);
    return true;
}

bool TestProjectAdmissionPolicy() noexcept
{
    using Admission = cold::StartupB0ProjectAdmissionDisposition;
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::NeverSeen, true, true, false) ==
          Admission::Defer);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::PartialDiagnostic,
              true, true, false) == Admission::Defer);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::CompleteAuthorized,
              true, true, true) == Admission::Allow);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::CompleteAuthorized,
              true, true, false) == Admission::BlockRestart);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::PermanentFailure,
              true, true, false) == Admission::BlockRestart);
    CHECK(cold::EvaluateStartupB0ProjectAdmission(
              cold::StartupB0State::NeverSeen,
              false, false, false) == Admission::Allow);
    return true;
}

bool TestWorkerOnFrameInvariantReasons() noexcept
{
    using Failure = cold::StartupB0WorkerOnFrameInvariantFailure;
    cold::StartupB0WorkerOnFrameInvariantEvidence evidence = {};
    evidence.worker_frontend_seen = true;
    evidence.frames_since_worker_frontend =
        cold::kStartupB0WorkerOnFrameInvariantFrameThreshold + 1;

    evidence.thread_ownership_validation_failed = true;
    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::ThreadOwnershipValidationFailed);
    evidence.thread_ownership_validation_failed = false;

    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::B0PumpStopped);
    evidence.pump_calls_since_worker_frontend = 1;
    evidence.level_snapshot_stale = true;
    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::LevelSnapshotStale);
    evidence.level_snapshot_stale = false;

    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::QueryResultDiscarded);
    evidence.query_calls_since_worker_frontend = 1;
    evidence.query_successes_since_worker_frontend = 1;
    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::OnFrameActiveLevelQueryDiffers);

    evidence.query_successes_since_worker_frontend = UINT64_MAX;
    evidence.query_nulls_since_worker_frontend = UINT64_MAX;
    evidence.query_invalids_since_worker_frontend = UINT64_MAX;
    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::QueryResultDiscarded);

    evidence.on_frame_frontend_seen = true;
    CHECK(cold::ClassifyStartupB0WorkerOnFrameInvariant(&evidence) ==
          Failure::None);
    CHECK(std::strcmp(
              cold::StartupB0WorkerOnFrameInvariantFailureName(
                  Failure::ThreadOwnershipValidationFailed),
              "thread_ownership_validation_failed") == 0);
    return true;
}

bool TestQueryDiagnosticsAndNames() noexcept
{
    cold::StartupB0Session session = NewSession();
    cold::StartupB0AdmissionSample sample = {};
    sample.sequence = 1;
    sample.qpc = 1;
    sample.thread_id = 77;
    sample.tick_source = cold::StartupB0TickSource::FallbackTimer;
    cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(&session, &sample);
    CHECK(advance.blocker == cold::StartupB0Blocker::NotGameThread);
    CHECK(session.diagnostics.startup_b0_query_failure_count == 1);
    CHECK(session.diagnostics.startup_b0_first_blocker ==
          cold::StartupB0Blocker::NotGameThread);

    cold::StartupB0Diagnostics snapshot = {};
    CHECK(cold::SnapshotStartupB0Diagnostics(&session, &snapshot));
    CHECK(snapshot.startup_b0_query_failure_count == 1);
    CHECK(std::strcmp(cold::StartupB0StateName(snapshot.startup_b0_state),
                      "NeverSeen") == 0);
    CHECK(std::strcmp(cold::StartupB0TickSourceName(
                          snapshot.startup_b0_tick_source),
                      "Unknown") == 0);
    CHECK(std::strcmp(cold::StartupB0TickSourceName(
                          snapshot.startup_b0_last_diagnostic_tick_source),
                      "fallback_timer") == 0);
    CHECK(std::strcmp(cold::StartupB0BlockerName(
                          cold::StartupB0Blocker::SourceRegistryChanged),
                      "source_registry_changed") == 0);
    return true;
}

}  // namespace

int main()
{
    if (!TestPODAndInitialization() ||
        !TestFrameRouteAndTickProvenance() ||
        !TestOnFrameClaimsRouteBeforeLevelQuerySucceeds() ||
        !TestTimerRemainsDiagnosticAfterFrameRouteLatch() ||
        !TestTransactionCoreRequiresPlayerAndAudioBaselines() ||
        !TestAuthorizedCoreSurvivesOptionalDiagnosticRetry() ||
        !TestCompleteDomainsRemainAdmissibleForRegistryRetry() ||
        !TestStableBoundaryAndPointerChanges() ||
        !TestOneSuccessThenFailuresCompletesStability() ||
        !TestFailedQueryCandidateSafety() ||
        !TestMigratingOnFrameLatchedIdentityReplay() ||
        !TestTransientBlockersRetry() ||
        !TestSourceOwnerPublicationRetryAndPermanentFailure() ||
        !TestIncrementalImmutableDomains() ||
        !TestPlayerAuthorityAndDomainRetry() ||
        !TestIrreversibleCloseAndReturnedFrontendFreeze() ||
        !TestStagingClosesEvenWhenFrontendQueryFails() ||
        !TestHundredsOfConsecutiveOnFrameSamples() ||
        !TestPlayerAudioDeadlineExpiresOnAuthoritativeFrameExactlyOnce() ||
        !TestProjectAdmissionPolicy() ||
        !TestWorkerOnFrameInvariantReasons() ||
        !TestQueryDiagnosticsAndNames())
        return 1;
    std::puts("startup baseline capture state fake harness passed");
    return 0;
}
