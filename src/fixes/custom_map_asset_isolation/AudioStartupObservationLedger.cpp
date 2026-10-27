#define _HAS_EXCEPTIONS 0

#include "AudioStartupObservationLedger.h"

#include <cstring>
#include <intrin.h>

namespace dluma::fixes::custom_map_asset_isolation::audio_startup {
namespace {

std::uint32_t AtomicLoadCounter(const std::uint32_t* value) noexcept
{
    return value
        ? static_cast<std::uint32_t>(dluma_atomic_compare_exchange(
              reinterpret_cast<volatile LONG*>(
                  const_cast<std::uint32_t*>(value)),
              0, 0))
        : 0;
}

std::uint64_t AtomicLoadRevision(
    const AudioStartupObservationLedger* ledger) noexcept
{
    return ledger
        ? static_cast<std::uint64_t>(_InterlockedCompareExchange64(
              const_cast<volatile LONGLONG*>(&ledger->revision), 0, 0))
        : 0;
}

LONG AtomicLoadContentionMutations(
    const AudioStartupObservationLedger* ledger) noexcept
{
    return ledger
        ? dluma_atomic_compare_exchange(
              const_cast<volatile LONG*>(&ledger->contention_mutations),
              0, 0)
        : 0;
}

void Touch(AudioStartupObservationLedger* ledger) noexcept
{
    if (ledger)
        _InterlockedIncrement64(&ledger->revision);
}

void RecordLockContention(AudioStartupObservationLedger* ledger) noexcept
{
    if (!ledger) return;

    /* A failed main-lock acquisition cannot safely mutate the ordinary loss
     * POD under that lock.  Keep the mutation atomic and advertise its full
     * lifetime so a concurrent capture can reject the copy even if it samples
     * between the counter and revision increments. */
    dluma_atomic_increment(&ledger->contention_mutations);
    dluma_atomic_increment(
        reinterpret_cast<volatile LONG*>(&ledger->loss.lock_contention));
    Touch(ledger);
    dluma_atomic_decrement(&ledger->contention_mutations);
}

bool TryAcquire(AudioStartupObservationLedger* ledger) noexcept
{
    if (!ledger) return false;
    return dluma_atomic_compare_exchange(&ledger->lock, 1, 0) == 0;
}

bool AcquireForProducer(AudioStartupObservationLedger* ledger) noexcept
{
    if (TryAcquire(ledger)) return true;
    RecordLockContention(ledger);
    return false;
}

void Release(AudioStartupObservationLedger* ledger) noexcept
{
    dluma_atomic_exchange(&ledger->lock, 0);
}

bool MatchEpoch(AudioStartupObservationLedger* ledger,std::uint32_t epoch) noexcept
{
    if (!ledger->runtime_occurrences) return epoch == 0;
    if (!epoch || epoch <= ledger->retired_epoch || (ledger->epoch && epoch != ledger->epoch)) {
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(&ledger->loss.incomplete_observation));
        Touch(ledger); return false;
    }
    ledger->epoch = epoch; return true;
}

}  // namespace

void InitializeAudioStartupObservationLedger(
    AudioStartupObservationLedger* ledger,
    bool coverage_started_before_audio_singletons) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
    ledger->coverage_started_before_audio_singletons =
        coverage_started_before_audio_singletons;
    ledger->revision = 1;
}

void SetAudioStartupHookPlanComplete(
    AudioStartupObservationLedger* ledger, bool complete) noexcept
{
    if (!AcquireForProducer(ledger)) return;
    if (ledger->hook_plan_complete != complete) {
        ledger->hook_plan_complete = complete;
        Touch(ledger);
    }
    Release(ledger);
}

bool RecordAudioStartupScript(
    AudioStartupObservationLedger* ledger,
    const AudioStartupScriptObservation& observation) noexcept
{
    if (!AcquireForProducer(ledger)) return false;
    if (ledger->continuation) {
        const bool recorded = RecordAudioStartupScript(ledger->continuation, observation);
        Touch(ledger); Release(ledger); return recorded;
    }
    if (!MatchEpoch(ledger, observation.epoch)) { Release(ledger); return false; }
    if (!observation.enter_sequence || !observation.exit_sequence ||
        observation.exit_sequence <= observation.enter_sequence ||
        !observation.request_complete ||
        !observation.logical_path[0]) {
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.incomplete_observation));
        Touch(ledger);
        Release(ledger);
        return false;
    }
    if (!ledger->hook_plan_complete)
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.prepublication_observation));
    if (ledger->script_count >= kAudioStartupScriptCapacity) {
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.script_overflow));
        Touch(ledger);
        Release(ledger);
        return false;
    }
    ledger->scripts[ledger->script_count++] = observation;
    Touch(ledger);
    Release(ledger);
    return true;
}

bool RecordAudioStartupBank(
    AudioStartupObservationLedger* ledger,
    const AudioStartupBankObservation& observation) noexcept
{
    if (!AcquireForProducer(ledger)) return false;
    if (ledger->continuation) {
        const bool recorded = RecordAudioStartupBank(ledger->continuation, observation);
        Touch(ledger); Release(ledger); return recorded;
    }
    if (!MatchEpoch(ledger, observation.epoch)) { Release(ledger); return false; }
    if (!observation.enter_sequence || !observation.exit_sequence ||
        observation.exit_sequence <= observation.enter_sequence ||
        !observation.bank_name_complete ||
        !observation.bank_name[0] ||
        observation.owner == AudioStartupBankOwner::Unknown) {
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.incomplete_observation));
        Touch(ledger);
        Release(ledger);
        return false;
    }
    if (!ledger->hook_plan_complete)
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.prepublication_observation));
    if (ledger->bank_count >= kAudioStartupBankCapacity) {
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.bank_overflow));
        Touch(ledger);
        Release(ledger);
        return false;
    }
    if (!observation.observation_complete)
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.incomplete_observation));
    ledger->banks[ledger->bank_count++] = observation;
    Touch(ledger);
    Release(ledger);
    return true;
}

bool RecordAudioStartupScriptDepthOverflow(
    AudioStartupObservationLedger* ledger) noexcept
{
    if (!AcquireForProducer(ledger)) return false;
    dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
        &ledger->loss.script_depth_overflow));
    if (!ledger->hook_plan_complete)
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.prepublication_observation));
    Touch(ledger);
    Release(ledger);
    return true;
}

bool RecordAudioStartupMutationFence(
    AudioStartupObservationLedger* ledger) noexcept
{
    if (!AcquireForProducer(ledger)) return false;
    if (!ledger->hook_plan_complete)
        dluma_atomic_increment(reinterpret_cast<volatile LONG*>(
            &ledger->loss.prepublication_observation));
    Touch(ledger);
    Release(ledger);
    return true;
}

bool CaptureAudioStartupObservationSnapshot(
    AudioStartupObservationLedger* ledger,
    AudioStartupObservationSnapshot* output) noexcept
{
    if (!ledger || !output) return false;
    std::memset(output, 0, sizeof(*output));
    /* A diagnostic reader collision does not lose a producer observation.
     * Fail immediately and retry without poisoning the loss latch. */
    if (!TryAcquire(ledger)) return false;

    const LONG contention_mutations_before =
        AtomicLoadContentionMutations(ledger);
    const std::uint64_t revision_before = AtomicLoadRevision(ledger);
    const std::uint32_t lock_contention_before =
        AtomicLoadCounter(&ledger->loss.lock_contention);
    if (contention_mutations_before != 0) {
        Release(ledger);
        return false;
    }

    output->script_count = ledger->script_count;
    output->bank_count = ledger->bank_count;
    output->loss.lock_contention = lock_contention_before;
    output->loss.script_overflow =
        AtomicLoadCounter(&ledger->loss.script_overflow);
    output->loss.bank_overflow =
        AtomicLoadCounter(&ledger->loss.bank_overflow);
    output->loss.incomplete_observation =
        AtomicLoadCounter(&ledger->loss.incomplete_observation);
    output->loss.prepublication_observation =
        AtomicLoadCounter(&ledger->loss.prepublication_observation);
    output->loss.script_depth_overflow =
        AtomicLoadCounter(&ledger->loss.script_depth_overflow);
    output->coverage_started_before_audio_singletons =
        ledger->coverage_started_before_audio_singletons;
    output->hook_plan_complete = ledger->hook_plan_complete;
    std::memcpy(output->scripts, ledger->scripts,
                sizeof(ledger->scripts));
    std::memcpy(output->banks, ledger->banks,
                sizeof(ledger->banks));

    const std::uint32_t lock_contention_after =
        AtomicLoadCounter(&ledger->loss.lock_contention);
    const std::uint64_t revision_after = AtomicLoadRevision(ledger);
    const LONG contention_mutations_after =
        AtomicLoadContentionMutations(ledger);
    if (contention_mutations_after != 0 ||
        lock_contention_after != lock_contention_before ||
        revision_after != revision_before) {
        std::memset(output, 0, sizeof(*output));
        Release(ledger);
        return false;
    }

    output->revision = revision_after;
    Release(ledger);
    return true;
}

bool AudioStartupObservationLossEmpty(
    const AudioStartupObservationLoss& loss) noexcept
{
    return !loss.lock_contention && !loss.script_overflow &&
        !loss.bank_overflow && !loss.incomplete_observation &&
        !loss.prepublication_observation && !loss.script_depth_overflow;
}

bool SealAudioStartupObservationLedger(AudioStartupObservationLedger* ledger,
    AudioStartupObservationLedger* runtime,std::uint64_t revision) noexcept
{
    if (!ledger || !runtime || ledger == runtime || !revision || !TryAcquire(ledger)) return false;
    const bool same = ledger->continuation == runtime && ledger->sealed_revision == revision;
    if (same) { Release(ledger); return true; }
    if (ledger->continuation || ledger->runtime_occurrences || AtomicLoadRevision(ledger) != revision ||
        AtomicLoadContentionMutations(ledger) || !AudioStartupObservationLossEmpty(ledger->loss) ||
        !ledger->hook_plan_complete || !ledger->coverage_started_before_audio_singletons || runtime->revision) {
        Release(ledger); return false;
    }
    InitializeAudioStartupObservationLedger(runtime, true);
    runtime->hook_plan_complete = true; runtime->runtime_occurrences = true;
    ledger->sealed_revision = revision; ledger->continuation = runtime;
    Release(ledger); return true;
}

bool RetireAudioRuntimeObservations(AudioStartupObservationLedger* ledger,
    std::uint64_t revision,const content::VerifiedEpochRetirement& receipt) noexcept
{
    if (!ledger || !ledger->runtime_occurrences || !revision || !receipt.epoch ||
        !receipt.transaction_verified || !receipt.observer_admission_closed ||
        !receipt.verified_transaction_token || !receipt.observer_quiescence_token ||
        receipt.pending_observer_callbacks || receipt.external_active_references || receipt.quarantined ||
        !TryAcquire(ledger)) return false;
    if ((ledger->epoch && receipt.epoch != ledger->epoch) || receipt.epoch <= ledger->retired_epoch ||
        (!ledger->epoch && (ledger->script_count || ledger->bank_count)) ||
        receipt.verified_transaction_token <= ledger->retired_transaction ||
        revision != AtomicLoadRevision(ledger) || AtomicLoadContentionMutations(ledger) ||
        !AudioStartupObservationLossEmpty(ledger->loss)) { Release(ledger); return false; }
    ledger->retired_scripts += ledger->script_count; ledger->retired_banks += ledger->bank_count;
    ledger->retired_epoch = receipt.epoch; ledger->retired_transaction = receipt.verified_transaction_token;
    ledger->epoch = 0; ledger->script_count = ledger->bank_count = 0;
    std::memset(ledger->scripts, 0, sizeof(ledger->scripts));
    std::memset(ledger->banks, 0, sizeof(ledger->banks));
    Touch(ledger); Release(ledger); return true;
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_startup
