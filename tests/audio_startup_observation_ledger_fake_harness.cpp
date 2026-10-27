#define _HAS_EXCEPTIONS 0

#include "fixes/custom_map_asset_isolation/AudioStartupObservationLedger.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

namespace startup =
    dluma::fixes::custom_map_asset_isolation::audio_startup;

static_assert(std::is_trivially_copyable<
                  startup::AudioStartupObservationLedger>::value,
              "startup observation ledger must remain bounded POD");
static_assert(std::is_standard_layout<
                  startup::AudioStartupObservationLedger>::value,
              "startup observation ledger must remain standard-layout POD");
static_assert(std::is_trivially_copyable<
                  startup::AudioStartupObservationSnapshot>::value,
              "drained startup observation must remain bounded POD");

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

startup::AudioStartupScriptObservation MakeScript(
    std::uint64_t sequence, const char* logical_path) noexcept
{
    startup::AudioStartupScriptObservation observation = {};
    observation.enter_sequence = sequence * 2U;
    observation.exit_sequence = observation.enter_sequence + 1U;
    observation.loader = 0x1000U +
        static_cast<std::uintptr_t>(sequence);
    observation.provider.source = 0x2000U +
        static_cast<std::uintptr_t>(sequence);
    observation.provider.source_registry_generation = 7U;
    observation.provider.source_registry_generation_observed = true;
    observation.provider.exact_source = true;
    observation.provider.path_complete = true;
    observation.request_complete = true;
    observation.parse_succeeded = true;
    std::strcpy(observation.logical_path, logical_path);
    std::strcpy(observation.provider.path,
                "X:\\dying light\\dw\\data\\audio\\audio.scr");
    return observation;
}

startup::AudioStartupBankObservation MakeBank(
    std::uint64_t sequence, startup::AudioStartupBankOwner owner,
    const char* name) noexcept
{
    startup::AudioStartupBankObservation observation = {};
    observation.enter_sequence = sequence * 2U;
    observation.exit_sequence = observation.enter_sequence + 1U;
    observation.scope_generation = sequence + 100U;
    observation.manager = 0x3000U;
    observation.result = 0;
    observation.owner = owner;
    observation.provider_outcome =
        startup::AudioStartupProviderOutcome::NaturalOpenResolvedPath;
    observation.provider.source = 0x4000U;
    observation.provider.source_registry_generation = 7U;
    observation.provider.source_registry_generation_observed = true;
    observation.provider.exact_source = true;
    observation.provider.path_complete = true;
    observation.bank_name_complete = true;
    observation.localized_observed = true;
    observation.observation_complete = true;
    std::strcpy(observation.bank_name, name);
    std::strcpy(observation.provider.path,
                "X:\\dying light\\dw\\data\\audio\\menu.bank");
    return observation;
}

bool CaseInitializeAndHookPlanGate()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    CHECK(ledger.revision == 1);
    CHECK(ledger.coverage_started_before_audio_singletons);
    CHECK(!ledger.hook_plan_complete);
    CHECK(startup::AudioStartupObservationLossEmpty(ledger.loss));

    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    CHECK(ledger.hook_plan_complete);
    CHECK(ledger.revision == 2);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    CHECK(ledger.revision == 2);
    return true;
}

bool CaseRecordsAndCapturesExactPodData()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, false);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    const startup::AudioStartupScriptObservation script =
        MakeScript(10, "data\\audio\\audio.scr");
    const startup::AudioStartupBankObservation fmod =
        MakeBank(11, startup::AudioStartupBankOwner::Fmod, "menu_main");
    startup::AudioStartupBankObservation speech =
        MakeBank(12, startup::AudioStartupBankOwner::Speech, "speech_en");
    speech.localized = true;
    CHECK(startup::RecordAudioStartupScript(&ledger, script));
    CHECK(startup::RecordAudioStartupBank(&ledger, fmod));
    CHECK(startup::RecordAudioStartupBank(&ledger, speech));

    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &snapshot));
    CHECK(snapshot.revision == 5);
    CHECK(!snapshot.coverage_started_before_audio_singletons);
    CHECK(snapshot.hook_plan_complete);
    CHECK(snapshot.script_count == 1);
    CHECK(snapshot.bank_count == 2);
    CHECK(std::strcmp(snapshot.scripts[0].logical_path,
                      "data\\audio\\audio.scr") == 0);
    CHECK(snapshot.banks[0].owner ==
          startup::AudioStartupBankOwner::Fmod);
    CHECK(snapshot.banks[1].owner ==
          startup::AudioStartupBankOwner::Speech);
    CHECK(snapshot.banks[1].localized);
    CHECK(snapshot.banks[1].localized_observed);
    CHECK(startup::AudioStartupObservationLossEmpty(snapshot.loss));

    ledger.scripts[0].logical_path[0] = 'X';
    CHECK(snapshot.scripts[0].logical_path[0] == 'd');
    return true;
}

bool CaseExternalMutationFenceAdvancesWithoutPayload()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    CHECK(startup::RecordAudioStartupMutationFence(&ledger));
    CHECK(ledger.revision == 3);
    CHECK(ledger.script_count == 0);
    CHECK(ledger.bank_count == 0);
    CHECK(startup::AudioStartupObservationLossEmpty(ledger.loss));

    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &snapshot));
    CHECK(snapshot.revision == 3);
    CHECK(snapshot.script_count == 0);
    CHECK(snapshot.bank_count == 0);
    return true;
}

bool CaseInvalidRecordsFailClosedAndAdvanceRevision()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    startup::AudioStartupScriptObservation script =
        MakeScript(20, "data\\audio\\audio.scr");
    script.request_complete = false;
    const std::uint64_t before_script =
        static_cast<std::uint64_t>(ledger.revision);
    CHECK(!startup::RecordAudioStartupScript(&ledger, script));
    CHECK(static_cast<std::uint64_t>(ledger.revision) ==
          before_script + 1U);

    startup::AudioStartupBankObservation bank =
        MakeBank(21, startup::AudioStartupBankOwner::Unknown, "unknown");
    const std::uint64_t before_bank =
        static_cast<std::uint64_t>(ledger.revision);
    CHECK(!startup::RecordAudioStartupBank(&ledger, bank));
    CHECK(static_cast<std::uint64_t>(ledger.revision) ==
          before_bank + 1U);
    CHECK(ledger.script_count == 0);
    CHECK(ledger.bank_count == 0);
    CHECK(ledger.loss.incomplete_observation == 2);
    CHECK(!startup::AudioStartupObservationLossEmpty(ledger.loss));
    return true;
}

bool CaseIncompleteBankIsRetainedButPoisonsAuthority()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    startup::AudioStartupBankObservation bank =
        MakeBank(30, startup::AudioStartupBankOwner::Fmod, "partial");
    bank.observation_complete = false;
    bank.provider_outcome =
        startup::AudioStartupProviderOutcome::ObservationIncomplete;
    CHECK(startup::RecordAudioStartupBank(&ledger, bank));
    CHECK(ledger.bank_count == 1);
    CHECK(ledger.loss.incomplete_observation == 1);
    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &snapshot));
    CHECK(snapshot.bank_count == 1);
    CHECK(!snapshot.banks[0].observation_complete);
    CHECK(!startup::AudioStartupObservationLossEmpty(snapshot.loss));
    return true;
}

bool CaseScriptCapacityIsBoundedAndNonBlocking()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    for (std::size_t index = 0;
         index < startup::kAudioStartupScriptCapacity; ++index) {
        const startup::AudioStartupScriptObservation observation =
            MakeScript(index + 1U, "data\\audio\\audio.scr");
        CHECK(startup::RecordAudioStartupScript(&ledger, observation));
    }
    const startup::AudioStartupScriptObservation overflow =
        MakeScript(1000, "data\\audio\\overflow.scr");
    const std::uint64_t before =
        static_cast<std::uint64_t>(ledger.revision);
    CHECK(!startup::RecordAudioStartupScript(&ledger, overflow));
    CHECK(ledger.script_count == startup::kAudioStartupScriptCapacity);
    CHECK(ledger.loss.script_overflow == 1);
    CHECK(static_cast<std::uint64_t>(ledger.revision) == before + 1U);
    return true;
}

bool CaseBankCapacityIsBoundedAndNonBlocking()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    for (std::size_t index = 0;
         index < startup::kAudioStartupBankCapacity; ++index) {
        const startup::AudioStartupBankObservation observation =
            MakeBank(index + 1U, startup::AudioStartupBankOwner::Fmod,
                     "bounded");
        CHECK(startup::RecordAudioStartupBank(&ledger, observation));
    }
    const startup::AudioStartupBankObservation overflow =
        MakeBank(1000, startup::AudioStartupBankOwner::Speech,
                 "overflow");
    const std::uint64_t before =
        static_cast<std::uint64_t>(ledger.revision);
    CHECK(!startup::RecordAudioStartupBank(&ledger, overflow));
    CHECK(ledger.bank_count == startup::kAudioStartupBankCapacity);
    CHECK(ledger.loss.bank_overflow == 1);
    CHECK(static_cast<std::uint64_t>(ledger.revision) == before + 1U);
    return true;
}

bool CaseContentionDropsImmediatelyAndInvalidatesRevision()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    const startup::AudioStartupScriptObservation observation =
        MakeScript(50, "data\\audio\\audio.scr");
    ledger.lock = 1;
    const std::uint64_t before =
        static_cast<std::uint64_t>(ledger.revision);
    CHECK(!startup::RecordAudioStartupScript(&ledger, observation));
    CHECK(ledger.loss.lock_contention == 1);
    CHECK(static_cast<std::uint64_t>(ledger.revision) == before + 1U);
    startup::AudioStartupObservationSnapshot blocked = {};
    std::memset(&blocked, 0xA5, sizeof(blocked));
    CHECK(!startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &blocked));
    CHECK(ledger.loss.lock_contention == 1);
    CHECK(static_cast<std::uint64_t>(ledger.revision) == before + 1U);
    CHECK(blocked.revision == 0);
    CHECK(blocked.script_count == 0);
    CHECK(blocked.loss.lock_contention == 0);
    ledger.lock = 0;

    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &snapshot));
    CHECK(snapshot.script_count == 0);
    CHECK(snapshot.loss.lock_contention == 1);
    return true;
}

bool CaseSnapshotReaderContentionDoesNotPoisonLoss()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);

    dluma_atomic_exchange(&ledger.lock, 1);
    const std::uint64_t revision_before =
        static_cast<std::uint64_t>(ledger.revision);
    const std::uint32_t contention_before = ledger.loss.lock_contention;

    startup::AudioStartupObservationSnapshot blocked = {};
    std::memset(&blocked, 0xA5, sizeof(blocked));
    CHECK(!startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &blocked));
    CHECK(static_cast<std::uint64_t>(ledger.revision) == revision_before);
    CHECK(ledger.loss.lock_contention == contention_before);
    CHECK(blocked.revision == 0);
    CHECK(blocked.script_count == 0);
    CHECK(blocked.loss.lock_contention == 0);

    dluma_atomic_exchange(&ledger.lock, 0);
    startup::AudioStartupObservationSnapshot stable = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &stable));
    CHECK(stable.revision == revision_before);
    CHECK(startup::AudioStartupObservationLossEmpty(stable.loss));
    return true;
}

bool CaseInFlightContentionMutationRejectsCaptureDeterministically()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    dluma_atomic_exchange(&ledger.contention_mutations, 1);

    startup::AudioStartupObservationSnapshot blocked = {};
    std::memset(&blocked, 0xA5, sizeof(blocked));
    CHECK(!startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &blocked));
    CHECK(blocked.revision == 0);
    CHECK(blocked.script_count == 0);
    CHECK(blocked.loss.lock_contention == 0);

    dluma_atomic_exchange(&ledger.contention_mutations, 0);
    startup::AudioStartupObservationSnapshot stable = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &stable));
    CHECK(stable.revision == 1);
    CHECK(startup::AudioStartupObservationLossEmpty(stable.loss));
    return true;
}

bool CasePrepublicationObservationPoisonsAuthority()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    CHECK(startup::RecordAudioStartupScript(
        &ledger, MakeScript(70, "data\\audio\\early.scr")));
    CHECK(ledger.loss.prepublication_observation == 1U);
    CHECK(!startup::AudioStartupObservationLossEmpty(ledger.loss));
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &snapshot));
    CHECK(snapshot.loss.prepublication_observation == 1U);
    return true;
}

bool CaseScriptDepthOverflowIsLatched()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);
    CHECK(startup::RecordAudioStartupScriptDepthOverflow(&ledger));
    CHECK(ledger.loss.script_depth_overflow == 1U);
    CHECK(!startup::AudioStartupObservationLossEmpty(ledger.loss));
    return true;
}

struct ConcurrentLedgerContext {
    startup::AudioStartupObservationLedger* ledger;
    HANDLE start;
    volatile LONG started;
    volatile LONG complete;
    std::uint32_t iterations;
};

DWORD WINAPI RecordScriptsConcurrently(void* opaque)
{
    ConcurrentLedgerContext* context =
        static_cast<ConcurrentLedgerContext*>(opaque);
    if (!context || !context->ledger || !context->start) return 1;
    if (WaitForSingleObject(context->start, INFINITE) != WAIT_OBJECT_0)
        return 2;
    dluma_atomic_exchange(&context->started, 1);
    for (std::uint32_t index = 0; index < context->iterations; ++index) {
        const startup::AudioStartupScriptObservation observation =
            MakeScript(index + 1U, "data\\audio\\concurrent.scr");
        startup::RecordAudioStartupScript(context->ledger, observation);
        if ((index & 15U) == 0) Sleep(0);
    }
    dluma_atomic_exchange(&context->complete, 1);
    return 0;
}

bool SnapshotRevisionMatchesAllRecordedMutations(
    const startup::AudioStartupObservationSnapshot& snapshot) noexcept
{
    const std::uint64_t recorded_mutations =
        static_cast<std::uint64_t>(snapshot.script_count) +
        static_cast<std::uint64_t>(snapshot.loss.script_overflow) +
        static_cast<std::uint64_t>(snapshot.loss.lock_contention);
    return snapshot.revision == 2U + recorded_mutations;
}

bool CaseConcurrentContentionNeverReturnsTornSnapshot()
{
    startup::AudioStartupObservationLedger ledger = {};
    startup::InitializeAudioStartupObservationLedger(&ledger, true);
    startup::SetAudioStartupHookPlanComplete(&ledger, true);

    ConcurrentLedgerContext context = {};
    context.ledger = &ledger;
    context.start = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context.iterations = 20000;
    CHECK(context.start != nullptr);

    HANDLE worker = CreateThread(nullptr, 0, &RecordScriptsConcurrently,
                                 &context, 0, nullptr);
    CHECK(worker != nullptr);

    /* Make the first cross-thread failed acquisition deterministic. */
    dluma_atomic_exchange(&ledger.lock, 1);
    CHECK(SetEvent(context.start));
    while (!dluma_atomic_compare_exchange(&context.started, 0, 0)) Sleep(0);
    while (!dluma_atomic_compare_exchange(
        reinterpret_cast<volatile LONG*>(&ledger.loss.lock_contention),
        0, 0)) {
        Sleep(0);
    }
    dluma_atomic_exchange(&ledger.lock, 0);

    std::uint32_t successful_snapshots = 0;
    while (!dluma_atomic_compare_exchange(&context.complete, 0, 0)) {
        startup::AudioStartupObservationSnapshot snapshot = {};
        if (startup::CaptureAudioStartupObservationSnapshot(
                &ledger, &snapshot)) {
            CHECK(SnapshotRevisionMatchesAllRecordedMutations(snapshot));
            ++successful_snapshots;
        }
        Sleep(0);
    }
    CHECK(WaitForSingleObject(worker, 30000) == WAIT_OBJECT_0);

    startup::AudioStartupObservationSnapshot final_snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(
        &ledger, &final_snapshot));
    CHECK(SnapshotRevisionMatchesAllRecordedMutations(final_snapshot));
    CHECK(final_snapshot.loss.lock_contention != 0);
    CHECK(successful_snapshots != 0 || final_snapshot.script_count != 0);

    CHECK(CloseHandle(worker));
    CHECK(CloseHandle(context.start));
    return true;
}

bool CaseSealedBaselineUsesVerifiedEpochStorage()
{
    startup::AudioStartupObservationLedger baseline = {}, runtime = {};
    startup::InitializeAudioStartupObservationLedger(&baseline, true);
    startup::SetAudioStartupHookPlanComplete(&baseline, true);
    const auto original = MakeScript(1, "audio/baseline.scr");
    CHECK(startup::RecordAudioStartupScript(&baseline, original));
    startup::AudioStartupObservationSnapshot snapshot = {};
    CHECK(startup::CaptureAudioStartupObservationSnapshot(&baseline, &snapshot));
    CHECK(startup::SealAudioStartupObservationLedger(&baseline, &runtime, snapshot.revision));
    for (std::uint32_t epoch=1; epoch<=20; ++epoch) {
        for (std::uint32_t i=0; i<20; ++i) {
            auto input=MakeScript(100+epoch*20+i,"audio/project.scr");input.epoch=epoch;
            CHECK(startup::RecordAudioStartupScript(&baseline,input));
        }
        CHECK(baseline.script_count==1&&runtime.script_count==20&&runtime.epoch==epoch);
        CHECK(!std::memcmp(&baseline.scripts[0],&original,sizeof(original)));
        CHECK(startup::CaptureAudioStartupObservationSnapshot(&runtime,&snapshot));
        dluma::fixes::custom_map_asset_isolation::content::VerifiedEpochRetirement receipt = {};
        receipt.epoch=epoch;receipt.verified_transaction_token=epoch;
        receipt.observer_quiescence_token=100+epoch;receipt.observer_admission_closed=true;
        CHECK(!startup::RetireAudioRuntimeObservations(&runtime,snapshot.revision,receipt));
        receipt.transaction_verified=true;
        CHECK(!startup::RetireAudioRuntimeObservations(&runtime,snapshot.revision-1,receipt));
        CHECK(startup::RetireAudioRuntimeObservations(&runtime,snapshot.revision,receipt));
        CHECK(!runtime.script_count&&!runtime.epoch);
        CHECK(!startup::RetireAudioRuntimeObservations(&runtime,snapshot.revision,receipt));
    }
    CHECK(runtime.retired_scripts==400&&runtime.retired_epoch==20&&baseline.script_count==1);
    auto late=MakeScript(900,"audio/late.scr");late.epoch=20;
    CHECK(!startup::RecordAudioStartupScript(&baseline,late));
    CHECK(runtime.loss.incomplete_observation==1&&baseline.script_count==1);
    return true;
}

}  // namespace

int main()
{
    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase cases[] = {
        {"initialize_and_hook_plan_gate", &CaseInitializeAndHookPlanGate},
        {"records_and_captures_exact_pod_data",
         &CaseRecordsAndCapturesExactPodData},
        {"external_mutation_fence_advances_without_payload",
         &CaseExternalMutationFenceAdvancesWithoutPayload},
        {"invalid_records_fail_closed_and_advance_revision",
         &CaseInvalidRecordsFailClosedAndAdvanceRevision},
        {"incomplete_bank_is_retained_but_poisons_authority",
         &CaseIncompleteBankIsRetainedButPoisonsAuthority},
        {"script_capacity_is_bounded_and_nonblocking",
         &CaseScriptCapacityIsBoundedAndNonBlocking},
        {"bank_capacity_is_bounded_and_nonblocking",
         &CaseBankCapacityIsBoundedAndNonBlocking},
        {"contention_drops_immediately_and_invalidates_revision",
         &CaseContentionDropsImmediatelyAndInvalidatesRevision},
        {"snapshot_reader_contention_does_not_poison_loss",
         &CaseSnapshotReaderContentionDoesNotPoisonLoss},
        {"in_flight_contention_mutation_rejects_capture_deterministically",
         &CaseInFlightContentionMutationRejectsCaptureDeterministically},
        {"prepublication_observation_poisons_authority",
         &CasePrepublicationObservationPoisonsAuthority},
        {"script_depth_overflow_is_latched",
         &CaseScriptDepthOverflowIsLatched},
        {"concurrent_contention_never_returns_torn_snapshot",
         &CaseConcurrentContentionNeverReturnsTornSnapshot},
        {"sealed_baseline_uses_verified_epoch_storage", &CaseSealedBaselineUsesVerifiedEpochStorage},
    };
    std::uint32_t passed = 0;
    for (const TestCase& test : cases) {
        if (!test.run()) return 1;
        ++passed;
        std::printf("[PASS] %s\n", test.name);
    }
    std::printf("audio startup observation cases passed: %u\n", passed);
    return 0;
}
