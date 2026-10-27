#include "dlw_game_frame_authority.h"
#include "fixes/custom_map_asset_isolation/StartupBaselineCaptureState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;

namespace {

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            std::fprintf(                                                   \
                stderr, "CHECK failed at line %d: %s\n",                  \
                __LINE__, #expression);                                     \
            return false;                                                   \
        }                                                                   \
    } while (0)

constexpr LONG kThreads[8] = {
    27108, 34156, 35496, 33728,
    32296, 32020, 24824, 24484};
constexpr std::uintptr_t kGameSubobject =
    0x000000000E0020A0ULL;
constexpr std::uintptr_t kGamePrimary =
    0x000000000E002090ULL;
constexpr std::uintptr_t kOtherGameSubobject =
    0x000000000F1020A0ULL;
constexpr std::uintptr_t kGlobalGameObject =
    0x000000000E008010ULL;
constexpr std::uintptr_t kGameInterface =
    0x000000000E002090ULL;
constexpr std::uintptr_t kGestureManager =
    0x000000000E100010ULL;
constexpr std::uintptr_t kCallerModule =
    0x0000000180000000ULL;
constexpr std::uintptr_t kCallerRva = 0x0034A51DULL;
constexpr std::uintptr_t kOtherCallerRva = 0x0034A6F1ULL;
constexpr std::uintptr_t kHookSlot =
    0x000000018157E6D0ULL;
constexpr std::uint64_t kSerializationToken =
    0xD1A6A7E000000001ULL;
constexpr std::uintptr_t kFrontendLevel =
    0x000000000F001000ULL;
constexpr std::uintptr_t kFrontendVtable =
    0x000000018A001000ULL;
constexpr std::uintptr_t kOtherFrontendLevel =
    0x000000000F002000ULL;
constexpr std::uint64_t kSessionGeneration = 31U;
constexpr unsigned int kDeltaBits = 0x3C888889U;

constexpr unsigned int kExactRouteProof =
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_SUPPORTED_BINARY |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_HOOK_SLOT |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_FRAME_SUBOBJECT |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_PRIMARY_OBJECT |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_READABLE_GAME_INTERFACE |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_EXACT_CALLER_ROUTE |
    DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE;

struct FakeRuntime {
    DlumaGameFrameAuthorityState authority;
    cold::StartupB0Session startup;
    std::uint64_t sequence;
    std::uint64_t dispatch_count;
    std::uint64_t native_service_count;
    std::uint64_t localization_count;
    std::uint64_t lua_count;
    std::uint64_t ui_count;
    std::uint64_t original_count;
    std::uint64_t b0_sample_count;
    std::uint64_t inventory_count;
    std::uint64_t classifier_count;
};

struct CallbackResult {
    DlumaGameFrameAuthorityObservation observation;
    DlumaGameFrameAuthorityDecision decision;
};

DlumaGameFrameFrontendProbe NullProbe(
    std::uint64_t monotonic_ms) noexcept
{
    DlumaGameFrameFrontendProbe probe = {};
    probe.result = DLUMA_GAME_FRAME_LEVEL_QUERY_NULL;
    probe.qpc = monotonic_ms * 100U;
    probe.monotonic_ms = monotonic_ms;
    return probe;
}

DlumaGameFrameFrontendProbe FrontendProbe(
    std::uint64_t monotonic_ms,
    std::uintptr_t pointer = kFrontendLevel,
    std::uintptr_t vtable = kFrontendVtable) noexcept
{
    DlumaGameFrameFrontendProbe probe = {};
    probe.result = DLUMA_GAME_FRAME_LEVEL_QUERY_SUCCESS;
    probe.level_pointer = pointer;
    probe.level_vtable = vtable;
    probe.qpc = monotonic_ms * 100U;
    probe.monotonic_ms = monotonic_ms;
    probe.frontend_identity_valid = 1U;
    std::strcpy(probe.level_name, "menu_lvl_new");
    return probe;
}

FakeRuntime NewRuntime() noexcept
{
    FakeRuntime runtime = {};
    dluma_game_frame_authority_initialize(
        &runtime.authority, 1000U,
        DLUMA_GAME_FRAME_AUTHORITY_STARTUP_TIMEOUT_MS);
    runtime.authority.exact_route_hook_slot = kHookSlot;
    (void)cold::InitializeStartupB0Session(
        &runtime.startup, kSessionGeneration,
        cold::kStartupB0TransactionCoreDomainMask,
        1U, 500U);
    return runtime;
}

DlumaGameFrameAuthorityObservation MakeObservation(
    FakeRuntime* runtime,
    LONG thread_id,
    std::uint64_t monotonic_ms,
    const DlumaGameFrameFrontendProbe& pre_probe,
    std::uintptr_t game_subobject = kGameSubobject,
    std::uintptr_t caller_rva = kCallerRva,
    unsigned int concurrent_depth = 1U,
    unsigned int recursive_depth = 1U,
    std::uint64_t serialization_token =
        kSerializationToken) noexcept
{
    DlumaGameFrameAuthorityObservation observation = {};
    observation.sequence = ++runtime->sequence;
    observation.monotonic_ms = monotonic_ms;
    observation.callback_entry_qpc = monotonic_ms * 100U + 1U;
    observation.thread_id = thread_id;
    observation.game_subobject = game_subobject;
    observation.game_primary = game_subobject - 0x10U;
    observation.gesture_manager = kGestureManager;
    observation.delta_bits = kDeltaBits;
    observation.recursive_depth = recursive_depth;
    observation.concurrent_depth = concurrent_depth;
    observation.global_game_object = kGlobalGameObject;
    observation.game_interface = kGameInterface;
    observation.caller_return_address =
        kCallerModule + caller_rva;
    observation.caller_module_base = kCallerModule;
    observation.caller_rva = caller_rva;
    observation.serialization_token = serialization_token;
    std::strcpy(
        observation.caller_module_name,
        "engine_x64_rwdi.dll");
    observation.route_proof = kExactRouteProof;
    observation.pre_original_probe = pre_probe;
    return observation;
}

void PumpB0(
    FakeRuntime* runtime,
    const DlumaGameFrameAuthorityObservation& observation,
    const DlumaGameFrameAuthorityDecision& decision) noexcept
{
    ++runtime->b0_sample_count;
    cold::StartupB0AdmissionSample sample = {};
    sample.sequence = observation.sequence;
    sample.qpc = observation.monotonic_ms;
    sample.frame_route_token = decision.serialization_token;
    sample.thread_id =
        static_cast<std::uint32_t>(observation.thread_id);
    sample.tick_source = cold::StartupB0TickSource::OnFrame;
    sample.query_succeeded = true;
    sample.frontend_active = true;
    sample.validated_frame_route = true;
    sample.frontend_level_pointer = kFrontendLevel;
    sample.frontend_level_vtable = kFrontendVtable;
    sample.player_inventory_authoritative = true;
    sample.source_registry_reported_count = 4U;
    sample.source_registry_observed_count = 4U;
    sample.source_inventory_result =
        cold::StartupB0DomainCaptureResult::Ready;
    sample.loader_pak_result =
        cold::StartupB0DomainCaptureResult::Ready;
    sample.loader_xui_result =
        cold::StartupB0DomainCaptureResult::Ready;
    const cold::StartupB0AdvanceResult advance =
        cold::AdvanceStartupB0Admission(
            &runtime->startup, &sample);
    if (!advance.admission_ready || runtime->inventory_count)
        return;
    ++runtime->inventory_count;
    ++runtime->classifier_count;
    (void)cold::RecordStartupB0DomainResult(
        &runtime->startup,
        cold::StartupB0Domain::VfsSourceInventory,
        cold::StartupB0DomainCaptureResult::Ready,
        cold::StartupB0Blocker::None,
        observation.sequence, kSessionGeneration, 0xA1U);
    (void)cold::RecordStartupB0DomainResult(
        &runtime->startup,
        cold::StartupB0Domain::Player,
        cold::StartupB0DomainCaptureResult::Ready,
        cold::StartupB0Blocker::None,
        observation.sequence, kSessionGeneration, 0xB2U);
    (void)cold::RecordStartupB0DomainResult(
        &runtime->startup,
        cold::StartupB0Domain::LoaderState,
        cold::StartupB0DomainCaptureResult::Ready,
        cold::StartupB0Blocker::None,
        observation.sequence, kSessionGeneration, 0xC3U);
    (void)cold::RecordStartupB0DomainResult(
        &runtime->startup,
        cold::StartupB0Domain::Audio,
        cold::StartupB0DomainCaptureResult::Ready,
        cold::StartupB0Blocker::None,
        observation.sequence, kSessionGeneration, 0xD4U);
    (void)cold::AuthorizeStartupB0RegistryBaseline(
        &runtime->startup, kSessionGeneration);
}

CallbackResult BeginCallback(
    FakeRuntime* runtime,
    LONG thread_id,
    std::uint64_t monotonic_ms,
    DlumaGameFrameFrontendProbe pre_probe,
    std::uintptr_t game_subobject = kGameSubobject,
    std::uintptr_t caller_rva = kCallerRva,
    unsigned int concurrent_depth = 1U,
    unsigned int recursive_depth = 1U,
    std::uint64_t serialization_token =
        kSerializationToken,
    bool run_loader_work = true) noexcept
{
    CallbackResult result = {};
    result.observation = MakeObservation(
        runtime, thread_id, monotonic_ms, pre_probe,
        game_subobject, caller_rva, concurrent_depth,
        recursive_depth, serialization_token);
    (void)dluma_game_frame_authority_status(
        &runtime->authority, monotonic_ms);
    result.decision = dluma_game_frame_authority_observe_pre(
        &runtime->authority, &result.observation);
    if (result.decision.run_loader_dispatch &&
        run_loader_work) {
        dluma_game_frame_authority_mark_loader_dispatch_started(
            &runtime->authority,
            result.decision.serialization_token, thread_id);
        ++runtime->dispatch_count;
        ++runtime->native_service_count;
        ++runtime->localization_count;
        ++runtime->lua_count;
        ++runtime->ui_count;
        PumpB0(runtime, result.observation, result.decision);
    }
    return result;
}

void FinishCallback(
    FakeRuntime* runtime,
    const CallbackResult& callback,
    const DlumaGameFrameFrontendProbe& post_probe) noexcept
{
    const std::uint64_t entry =
        callback.observation.callback_entry_qpc + 1U;
    const std::uint64_t returned = entry + 1U;
    dluma_game_frame_authority_note_post_original(
        &runtime->authority,
        callback.observation.sequence,
        callback.observation.thread_id,
        callback.observation.game_subobject,
        entry, returned, &post_probe);
    ++runtime->original_count;
    dluma_game_frame_authority_note_callback_return(
        &runtime->authority,
        callback.observation.sequence,
        callback.observation.thread_id,
        returned + 1U);
}

CallbackResult DriveCallback(
    FakeRuntime* runtime,
    LONG thread_id,
    std::uint64_t monotonic_ms,
    DlumaGameFrameFrontendProbe pre_probe,
    DlumaGameFrameFrontendProbe post_probe,
    std::uintptr_t game_subobject = kGameSubobject,
    std::uintptr_t caller_rva = kCallerRva,
    unsigned int concurrent_depth = 1U,
    unsigned int recursive_depth = 1U,
    std::uint64_t serialization_token =
        kSerializationToken,
    bool run_loader_work = true) noexcept
{
    const CallbackResult callback = BeginCallback(
        runtime, thread_id, monotonic_ms, pre_probe,
        game_subobject, caller_rva, concurrent_depth,
        recursive_depth, serialization_token,
        run_loader_work);
    FinishCallback(runtime, callback, post_probe);
    return callback;
}

bool LatchRoundRobinRoute(FakeRuntime* runtime) noexcept
{
    for (unsigned int index = 0; index < 8U; ++index) {
        const std::uint64_t now = 1000U + index * 16U;
        const DlumaGameFrameFrontendProbe post =
            index == 7U ? FrontendProbe(now) : NullProbe(now);
        (void)DriveCallback(
            runtime, kThreads[index], now,
            NullProbe(now), post);
    }
    DlumaGameFrameFrontendProbe confirmed = NullProbe(1128U);
    confirmed.pending_identity_still_readable = 1U;
    const CallbackResult latched = DriveCallback(
        runtime, kThreads[0], 1128U, confirmed,
        FrontendProbe(1128U));
    return latched.decision.run_loader_dispatch != 0 &&
           runtime->authority.
               game_frame_frontend_identity_latched != 0;
}

bool TestRoundRobinSerializedRouteLatchesAfterPositiveEvidence() noexcept
{
    FakeRuntime runtime = NewRuntime();
    for (unsigned int index = 0; index < 8U; ++index) {
        const std::uint64_t now = 1000U + index * 16U;
        const CallbackResult callback = DriveCallback(
            &runtime, kThreads[index], now,
            NullProbe(now),
            index == 7U ? FrontendProbe(now)
                        : NullProbe(now));
        CHECK(callback.decision.run_loader_dispatch == 0U);
    }
    CHECK(runtime.authority.thread_count == 8U);
    CHECK(runtime.dispatch_count == 0U);
    CHECK(runtime.native_service_count == 0U);
    CHECK(runtime.localization_count == 0U);
    CHECK(runtime.lua_count == 0U);
    CHECK(runtime.ui_count == 0U);
    CHECK(runtime.authority.pending_post_sequence == 8U);
    CHECK(runtime.authority.game_frame_authority_frozen == 0);

    DlumaGameFrameFrontendProbe pre = NullProbe(1128U);
    pre.pending_identity_still_readable = 1U;
    const CallbackResult latched = DriveCallback(
        &runtime, kThreads[0], 1128U, pre,
        FrontendProbe(1128U));
    CHECK(latched.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_LATCHED);
    CHECK(latched.decision.run_loader_dispatch != 0U);
    CHECK(latched.decision.dispatch_thread_migrated != 0U);
    CHECK(runtime.authority.exact_route_game_subobject ==
          kGameSubobject);
    CHECK(runtime.authority.exact_route_game_primary ==
          kGamePrimary);
    CHECK(runtime.authority.exact_route_caller_module_base ==
          kCallerModule);
    CHECK(runtime.authority.exact_route_caller_rva ==
          kCallerRva);
    CHECK(runtime.authority.exact_route_hook_slot == kHookSlot);
    CHECK(runtime.authority.exact_route_serialization_token ==
          kSerializationToken);
    CHECK(runtime.authority.scheduler_migration_count == 8U);
    CHECK(runtime.authority.previous_callback_thread_id ==
          kThreads[7]);
    CHECK(runtime.authority.game_frame_authority_sequence == 9U);
    CHECK(runtime.authority.game_frame_loader_dispatch_started != 0);
    CHECK(runtime.authority.game_frame_registry_mutation_started == 0);
    CHECK(dluma_game_frame_authority_status(
              &runtime.authority, 1129U) ==
          DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE);
    CHECK(runtime.authority.trace_count == 9U);
    const DlumaGameFrameCallbackTrace& trace =
        runtime.authority.trace[8U];
    CHECK(trace.sequence == 9U);
    CHECK(trace.thread_id == kThreads[0]);
    CHECK(trace.previous_callback_thread_id == kThreads[7]);
    CHECK(trace.delta_bits == kDeltaBits);
    CHECK(trace.caller_rva == kCallerRva);
    CHECK(trace.serialization_token == kSerializationToken);
    CHECK(trace.original_entry_qpc != 0U);
    CHECK(trace.original_return_qpc > trace.original_entry_qpc);
    CHECK(trace.callback_duration_qpc != 0U);
    return true;
}

bool TestValidatedPlaytestHandoffLatchesFirstExactSerializedRoute() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(dluma_game_frame_authority_configure_validated_playtest_handoff(
              &runtime.authority, "dlua_example") != 0);
    CHECK(dluma_game_frame_authority_configure_validated_playtest_handoff(
              &runtime.authority, "dlua_example") != 0);
    CHECK(dluma_game_frame_authority_configure_validated_playtest_handoff(
              &runtime.authority, "other_map") == 0);
    CHECK(runtime.authority.game_frame_playtest_level_configured != 0);
    CHECK(runtime.authority.game_frame_playtest_handoff_validated != 0);
    const CallbackResult latched = DriveCallback(
        &runtime, kThreads[0], 1000U,
        NullProbe(1000U), NullProbe(1000U));
    CHECK(latched.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_LATCHED);
    CHECK(latched.decision.authority_selected != 0U);
    CHECK(latched.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.
              game_frame_playtest_handoff_latched != 0);
    CHECK(runtime.authority.
              game_frame_frontend_identity_latched == 0);
    CHECK((runtime.authority.game_frame_authority_proof &
           DLUMA_GAME_FRAME_AUTHORITY_PROOF_VALIDATED_PLAYTEST_HANDOFF) != 0);
    CHECK((runtime.authority.game_frame_authority_proof &
           DLUMA_GAME_FRAME_AUTHORITY_PROOF_SERIALIZED_ROUTE) != 0);
    CHECK((runtime.authority.game_frame_authority_proof &
           DLUMA_GAME_FRAME_AUTHORITY_PROOF_FRONTEND_IDENTITY) == 0);
    CHECK((runtime.authority.game_frame_authority_proof &
           DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF) ==
          DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF);
    CHECK(runtime.authority.repeating_route_count == 1U);
    CHECK(dluma_game_frame_authority_status(
              &runtime.authority, 1001U) ==
          DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE);
    CHECK(dluma_game_frame_authority_configure_validated_playtest_handoff(
              &runtime.authority, "dlua_example") == 0);
    return true;
}

bool TestEightAssignmentsNeverElectAWorker() noexcept
{
    FakeRuntime runtime = NewRuntime();
    for (unsigned int index = 0; index < 16U; ++index) {
        const std::uint64_t now = 1000U + index * 16U;
        (void)DriveCallback(
            &runtime, kThreads[index & 1U], now,
            NullProbe(now), NullProbe(now));
    }
    CHECK(runtime.authority.threads[0].callback_count == 8U);
    CHECK(runtime.authority.threads[1].callback_count == 8U);
    CHECK(runtime.authority.game_frame_frontend_identity_latched == 0);
    CHECK(runtime.dispatch_count == 0U);
    CHECK(runtime.authority.current_dispatch_thread_id == 0);
    return true;
}

bool TestMigratedDispatchAcceptsOnlyIdenticalRoute() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const std::uint64_t before = runtime.dispatch_count;
    const CallbackResult migrated = DriveCallback(
        &runtime, kThreads[5], 1144U,
        NullProbe(1144U), NullProbe(1144U));
    CHECK(migrated.decision.run_loader_dispatch != 0U);
    CHECK(migrated.decision.dispatch_thread_migrated != 0U);
    CHECK(runtime.dispatch_count == before + 1U);
    CHECK(runtime.authority.game_frame_authority_conflict == 0);
    return true;
}

bool TestDifferentCallerFailsClosed() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const std::uint64_t before = runtime.dispatch_count;
    const CallbackResult changed = DriveCallback(
        &runtime, kThreads[1], 1144U,
        NullProbe(1144U), NullProbe(1144U),
        kGameSubobject, kOtherCallerRva);
    CHECK(changed.decision.run_loader_dispatch == 0U);
    CHECK(changed.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CALLER_CHANGE);
    CHECK(runtime.authority.game_frame_authority_conflict != 0);
    CHECK(runtime.authority.game_frame_authority_conflict_reason ==
          DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CALLER_ROUTE_CHANGED);
    CHECK(runtime.dispatch_count == before);
    return true;
}

bool TestDifferentObjectFailsClosed() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const CallbackResult changed = DriveCallback(
        &runtime, kThreads[2], 1144U,
        NullProbe(1144U), NullProbe(1144U),
        kOtherGameSubobject);
    CHECK(changed.decision.run_loader_dispatch == 0U);
    CHECK(changed.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_OBJECT_CHANGE);
    CHECK(runtime.authority.game_frame_authority_conflict_reason ==
          DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_GAME_SUBOBJECT_CHANGED);
    return true;
}

bool TestSerializationTokenChangeFailsClosed() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const CallbackResult changed = DriveCallback(
        &runtime, kThreads[3], 1144U,
        NullProbe(1144U), NullProbe(1144U),
        kGameSubobject, kCallerRva, 1U, 1U,
        kSerializationToken + 1U);
    CHECK(changed.decision.run_loader_dispatch == 0U);
    CHECK(runtime.authority.game_frame_authority_conflict_reason ==
          DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_HOOK_ROUTE_IDENTITY_LOST);
    return true;
}

bool TestConcurrentCallbackFailsClosed() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const std::uint64_t before = runtime.dispatch_count;
    const CallbackResult concurrent = DriveCallback(
        &runtime, kThreads[4], 1144U,
        NullProbe(1144U), NullProbe(1144U),
        kGameSubobject, kCallerRva, 2U);
    CHECK(concurrent.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_CONCURRENT);
    CHECK(concurrent.decision.run_loader_dispatch == 0U);
    CHECK(runtime.authority.game_frame_authority_conflict_reason ==
          DLUMA_GAME_FRAME_AUTHORITY_CONFLICT_CONCURRENT_CALLBACK);
    CHECK(runtime.dispatch_count == before);
    return true;
}

bool TestRecursiveCallbackDoesNotDispatchTwice() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const std::uint64_t before = runtime.dispatch_count;
    const CallbackResult recursive = DriveCallback(
        &runtime, kThreads[0], 1144U,
        NullProbe(1144U), NullProbe(1144U),
        kGameSubobject, kCallerRva, 1U, 2U);
    CHECK(recursive.decision.result ==
          DLUMA_GAME_FRAME_AUTHORITY_RESULT_REJECTED_RECURSIVE);
    CHECK(recursive.decision.run_loader_dispatch == 0U);
    CHECK(runtime.dispatch_count == before);
    return true;
}

bool TestReadOnlyDiscoveryNeverMarksRegistryMutation() noexcept
{
    FakeRuntime runtime = NewRuntime();
    for (unsigned int index = 0; index < 8U; ++index) {
        const std::uint64_t now = 1000U + index * 16U;
        (void)DriveCallback(
            &runtime, kThreads[index], now,
            NullProbe(now),
            index == 7U ? FrontendProbe(now)
                        : NullProbe(now));
    }
    CHECK(runtime.authority.pending_post_sequence == 8U);
    CHECK(runtime.authority.game_frame_frontend_identity_latched == 0);
    CHECK(runtime.authority.game_frame_loader_dispatch_started == 0);
    CHECK(runtime.authority.game_frame_project_admitted == 0);
    CHECK(runtime.authority.game_frame_registry_mutation_started == 0);
    dluma_game_frame_authority_mark_registry_mutation_started(
        &runtime.authority);
    CHECK(runtime.authority.game_frame_registry_mutation_started == 0);
    return true;
}

bool TestValidatedFrontendReplacementRebindsWithoutChangingRoute() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const std::uint64_t dispatch_before = runtime.dispatch_count;
    const CallbackResult pending = DriveCallback(
        &runtime, kThreads[1], 1144U,
        FrontendProbe(
            1144U, kOtherFrontendLevel, kFrontendVtable),
        FrontendProbe(
            1144U, kOtherFrontendLevel, kFrontendVtable));
    CHECK(pending.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.pending_post_sequence != 0U);
    CHECK(runtime.authority.latched_frontend_pointer ==
          kFrontendLevel);
    const CallbackResult rebound = DriveCallback(
        &runtime, kThreads[2], 1160U,
        FrontendProbe(
            1160U, kOtherFrontendLevel, kFrontendVtable),
        FrontendProbe(
            1160U, kOtherFrontendLevel, kFrontendVtable));
    CHECK(rebound.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.game_frame_authority_conflict == 0);
    CHECK(runtime.authority.latched_frontend_pointer ==
          kOtherFrontendLevel);
    CHECK(runtime.authority.latched_frontend_vtable ==
          kFrontendVtable);
    CHECK(runtime.authority.frontend_identity_rebind_count == 1U);
    CHECK(runtime.authority.pending_post_sequence == 0U);
    CHECK(runtime.dispatch_count == dispatch_before + 2U);
    return true;
}

bool TestUnconfirmedFrontendMismatchDoesNotRebind() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const CallbackResult transient = DriveCallback(
        &runtime, kThreads[1], 1144U,
        FrontendProbe(
            1144U, kOtherFrontendLevel, kFrontendVtable),
        FrontendProbe(1144U));
    CHECK(transient.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.game_frame_authority_conflict == 0);
    CHECK(runtime.authority.latched_frontend_pointer ==
          kFrontendLevel);
    CHECK(runtime.authority.frontend_identity_rebind_count == 0U);
    CHECK(runtime.authority.pending_post_sequence == 0U);
    return true;
}

bool TestPositiveFrontendReachesInventoryAndClassifier() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    (void)DriveCallback(
        &runtime, kThreads[1], 1144U,
        FrontendProbe(1144U), FrontendProbe(1144U));
    CHECK(runtime.b0_sample_count == 2U);
    CHECK(runtime.startup.diagnostics.startup_b0_attempt_count > 0U);
    CHECK(runtime.startup.diagnostics.startup_b0_frame_route_token ==
          kSerializationToken);
    CHECK(runtime.startup.diagnostics.startup_b0_last_dispatch_thread ==
          static_cast<std::uint32_t>(kThreads[1]));
    CHECK(runtime.inventory_count == 1U);
    CHECK(runtime.classifier_count == 1U);
    CHECK(runtime.startup.domains[
              static_cast<std::size_t>(
                  cold::StartupB0Domain::VfsSourceInventory)].ready);
    CHECK(runtime.startup.domains[
              static_cast<std::size_t>(
                  cold::StartupB0Domain::Player)].ready);
    CHECK(runtime.startup.domains[
              static_cast<std::size_t>(
                  cold::StartupB0Domain::LoaderState)].ready);
    CHECK(runtime.startup.domains[
              static_cast<std::size_t>(
                  cold::StartupB0Domain::Audio)].ready);
    CHECK(runtime.startup.diagnostics.
              startup_b0_cold_reboot_baseline_authorized);
    return true;
}

bool TestFrontendPointersRefreshAndExpireWithinBound() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    (void)DriveCallback(
        &runtime, kThreads[1], 1200U,
        FrontendProbe(1200U), FrontendProbe(1200U));
    CHECK(runtime.authority.latched_frontend_observed_ms == 1200U);
    CHECK(runtime.authority.latched_frontend_pointer ==
          kFrontendLevel);
    CHECK(runtime.authority.latched_frontend_vtable ==
          kFrontendVtable);

    const DlumaGameFrameFrontendProbe timer_probe =
        FrontendProbe(1200U);
    dluma_game_frame_authority_note_message_timer_probe(
        &runtime.authority, 707U, &timer_probe);
    CHECK(runtime.authority.last_window_timer_probe.level_pointer ==
          kFrontendLevel);
    CHECK(dluma_game_frame_authority_status(
              &runtime.authority, 1451U) ==
          DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE);
    CHECK(runtime.authority.latched_frontend_pointer == 0U);
    CHECK(runtime.authority.latched_frontend_vtable == 0U);
    CHECK(runtime.authority.last_window_timer_probe.level_pointer == 0U);
    for (unsigned int index = 0;
         index < runtime.authority.trace_count; ++index) {
        CHECK(runtime.authority.trace[index].
                  pre_original_probe.level_pointer == 0U);
        CHECK(runtime.authority.trace[index].
                  post_original_probe.level_pointer == 0U);
    }

    const CallbackResult pending = DriveCallback(
        &runtime, kThreads[2], 1452U,
        FrontendProbe(1452U), FrontendProbe(1452U));
    CHECK(pending.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.pending_post_sequence != 0U);
    const CallbackResult rebound = DriveCallback(
        &runtime, kThreads[3], 1468U,
        FrontendProbe(1468U), FrontendProbe(1468U));
    CHECK(rebound.decision.run_loader_dispatch != 0U);
    CHECK(runtime.authority.game_frame_authority_conflict == 0);
    CHECK(runtime.authority.latched_frontend_pointer ==
          kFrontendLevel);
    CHECK(runtime.authority.frontend_identity_rebind_count == 1U);
    return true;
}

bool TestTimerProbeIsDiagnosticAndSeparate() noexcept
{
    FakeRuntime runtime = NewRuntime();
    const DlumaGameFrameFrontendProbe timer =
        FrontendProbe(1000U);
    dluma_game_frame_authority_note_message_timer_probe(
        &runtime.authority, 707U, &timer);
    CHECK(runtime.authority.window_timer_probe_count == 1U);
    CHECK(runtime.authority.window_timer_query_success_count == 1U);
    CHECK(runtime.authority.game_frame_message_timer_thread_id == 707);
    CHECK(runtime.authority.game_frame_frontend_identity_latched == 0);
    CHECK(runtime.dispatch_count == 0U);
    dluma_game_frame_authority_note_message_timer_probe(
        &runtime.authority, 808U, &timer);
    CHECK(runtime.authority.game_frame_window_timer_owner_conflict != 0);
    CHECK(runtime.authority.game_frame_authority_conflict == 0);
    return true;
}

bool TestShutdownAcceptsRouteOrWindowOwnerOnly() noexcept
{
    FakeRuntime unproven = NewRuntime();
    const CallbackResult discovery = BeginCallback(
        &unproven, kThreads[0], 1000U, NullProbe(1000U));
    CHECK(dluma_game_frame_authority_can_finalize(
              &unproven.authority, kThreads[0], 1U, 0) == 0);
    FinishCallback(
        &unproven, discovery, NullProbe(1000U));

    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const CallbackResult active = BeginCallback(
        &runtime, kThreads[6], 1144U,
        FrontendProbe(1144U));
    CHECK(active.decision.run_loader_dispatch != 0U);
    CHECK(dluma_game_frame_authority_can_finalize(
              &runtime.authority, kThreads[6], 1U, 0) != 0);
    CHECK(dluma_game_frame_authority_can_finalize(
              &runtime.authority, 999U, 0U, 0) == 0);
    FinishCallback(
        &runtime, active, FrontendProbe(1144U));

    const DlumaGameFrameFrontendProbe window_probe =
        FrontendProbe(1160U);
    dluma_game_frame_authority_note_message_timer_probe(
        &runtime.authority, 707U, &window_probe);
    CHECK(dluma_game_frame_authority_can_finalize(
              &runtime.authority, 707U, 0U, 707U) != 0);
    CHECK(dluma_game_frame_authority_can_finalize(
              &runtime.authority, 808U, 0U, 707U) == 0);
    return true;
}

bool TestTopologyTraceIsBounded() noexcept
{
    FakeRuntime runtime = NewRuntime();
    CHECK(LatchRoundRobinRoute(&runtime));
    const unsigned int extra =
        DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY + 9U;
    for (unsigned int index = 0; index < extra; ++index) {
        const std::uint64_t now = 1144U + index * 2U;
        (void)DriveCallback(
            &runtime, kThreads[index % 8U], now,
            NullProbe(now), NullProbe(now));
    }
    CHECK(runtime.authority.trace_count ==
          DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY);
    CHECK(runtime.authority.trace_overwrite_count > 0U);
    CHECK(runtime.authority.thread_count == 8U);
    return true;
}

}  // namespace

int main()
{
    const bool passed =
        TestRoundRobinSerializedRouteLatchesAfterPositiveEvidence() &&
        TestValidatedPlaytestHandoffLatchesFirstExactSerializedRoute() &&
        TestEightAssignmentsNeverElectAWorker() &&
        TestMigratedDispatchAcceptsOnlyIdenticalRoute() &&
        TestDifferentCallerFailsClosed() &&
        TestDifferentObjectFailsClosed() &&
        TestSerializationTokenChangeFailsClosed() &&
        TestConcurrentCallbackFailsClosed() &&
        TestRecursiveCallbackDoesNotDispatchTwice() &&
        TestReadOnlyDiscoveryNeverMarksRegistryMutation() &&
        TestValidatedFrontendReplacementRebindsWithoutChangingRoute() &&
        TestUnconfirmedFrontendMismatchDoesNotRebind() &&
        TestPositiveFrontendReachesInventoryAndClassifier() &&
        TestFrontendPointersRefreshAndExpireWithinBound() &&
        TestTimerProbeIsDiagnosticAndSeparate() &&
        TestShutdownAcceptsRouteOrWindowOwnerOnly() &&
        TestTopologyTraceIsBounded();
    if (!passed) return 1;
    std::puts("game frame authority fake harness passed");
    return 0;
}
