#define _HAS_EXCEPTIONS 0

#include "AudioSemanticDelta.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_semantic {
namespace {

void AddReason(AudioSemanticEpochEvidence* evidence,
               AudioSemanticDeltaReason reason) noexcept
{
    if (evidence)
        evidence->reason_mask |= static_cast<std::uint64_t>(reason);
}

bool PrepareEpoch(AudioSemanticDeltaLedger* ledger,
                  std::uint32_t epoch) noexcept
{
    if (!ledger || !epoch || epoch <= ledger->retired_epoch) return false;
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    if (!evidence.initialized || evidence.epoch == epoch)
        goto initialize;
    {
        AddReason(
            &evidence, AudioSemanticDeltaReasonEpochOverlap);
        evidence.observation_loss = true;
        evidence.incomplete_evidence = true;
        return false;
    }

initialize:
    if (!evidence.initialized) {
        evidence.initialized = true;
        evidence.epoch = epoch;
        ++ledger->generation;
        if (!ledger->generation) ++ledger->generation;
    }
    return true;
}

bool ScriptSnapshotsEqual(
    const audio_semantic_snapshot::
        AudioScriptRegistrySemanticSnapshot& before,
    const audio_semantic_snapshot::
        AudioScriptRegistrySemanticSnapshot& after) noexcept
{
    return before.registry_owner == after.registry_owner &&
        before.definition_count == after.definition_count &&
        before.path_count == after.path_count &&
        before.procedure_count == after.procedure_count &&
        before.property_group_count == after.property_group_count &&
        before.semantic_fingerprint == after.semantic_fingerprint;
}

bool BankSnapshotsEqual(
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& before,
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& after)
    noexcept
{
    return before.manager == after.manager &&
        before.bank_count == after.bank_count &&
        before.semantic_fingerprint == after.semantic_fingerprint &&
        before.identity_fingerprint == after.identity_fingerprint &&
        before.matching_node_count == after.matching_node_count &&
        before.matching_node == after.matching_node &&
        before.matching_native_file == after.matching_native_file;
}

bool BoundedTextComplete(const char* text) noexcept
{
    if (!text || !text[0]) return false;
    for (std::size_t index = 0;
         index < kAudioSemanticLogicalPathCapacity; ++index)
        if (!text[index]) return true;
    return false;
}

}  // namespace

void InitializeAudioSemanticDeltaLedger(
    AudioSemanticDeltaLedger* ledger) noexcept
{
    if (ledger) std::memset(ledger, 0, sizeof(*ledger));
}

bool RetireAudioSemanticEpoch(AudioSemanticDeltaLedger* ledger,
    const content::VerifiedEpochRetirement& receipt) noexcept
{
    if (!ledger || !receipt.epoch || !receipt.transaction_verified ||
        !receipt.observer_admission_closed || !receipt.verified_transaction_token ||
        !receipt.observer_quiescence_token || receipt.pending_observer_callbacks ||
        receipt.external_active_references || receipt.quarantined ||
        receipt.verified_transaction_token <= ledger->retired_transaction ||
        !ledger->evidence.initialized || ledger->evidence.epoch != receipt.epoch ||
        !ledger->evidence.assessed || ledger->evidence.observation_loss ||
        ledger->evidence.incomplete_evidence) return false;
    ledger->retired_observation_count += ledger->evidence.script_observation_count +
        static_cast<std::uint64_t>(ledger->evidence.bank_observation_count) + ledger->evidence.speech_observation_count;
    ledger->retired_epoch = receipt.epoch;
    ledger->retired_transaction = receipt.verified_transaction_token;
    std::memset(&ledger->evidence, 0, sizeof(ledger->evidence));
    return true;
}

bool RecordAudioSemanticScriptObservation(
    AudioSemanticDeltaLedger* ledger,
    const AudioSemanticScriptObservation& observation) noexcept
{
    if (!PrepareEpoch(ledger, observation.epoch)) return false;
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    if (evidence.script_observation_count != UINT32_MAX)
        ++evidence.script_observation_count;
    evidence.last_script = observation;
    const bool complete =
        observation.logical_path_complete &&
        BoundedTextComplete(observation.logical_path) &&
        observation.parse_result_observed &&
        observation.before.complete &&
        observation.after.complete;
    if (!complete) {
        evidence.incomplete_evidence = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonScriptEvidenceIncomplete);
        return true;
    }
    const bool registry_changed = !ScriptSnapshotsEqual(
        observation.before, observation.after);
    if (registry_changed) {
        evidence.semantic_delta = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonScriptRegistryDelta);
        if (observation.parse_succeeded) {
            evidence.full_path_attribution_required = true;
            if (!observation.exact_source || !observation.source) {
                evidence.full_path_attribution_incomplete = true;
                evidence.incomplete_evidence = true;
                AddReason(
                    &evidence,
                    AudioSemanticDeltaReasonScriptEvidenceIncomplete);
            }
        }
    }
    return true;
}

bool RecordAudioSemanticBankObservation(
    AudioSemanticDeltaLedger* ledger,
    const AudioSemanticBankObservation& observation) noexcept
{
    if (!PrepareEpoch(ledger, observation.epoch)) return false;
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    if (evidence.bank_observation_count != UINT32_MAX)
        ++evidence.bank_observation_count;
    evidence.last_bank = observation;
    const bool complete =
        observation.bank_name_complete &&
        BoundedTextComplete(observation.bank_name) &&
        observation.result_observed &&
        observation.before.complete &&
        observation.after.complete;
    if (!complete) {
        evidence.incomplete_evidence = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonBankEvidenceIncomplete);
        return true;
    }
    const bool tree_changed = !BankSnapshotsEqual(
        observation.before, observation.after);
    if (tree_changed) {
        evidence.semantic_delta = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonBankTreeDelta);
    }
    if (observation.result >= 0) {
        if (tree_changed) {
            evidence.full_path_attribution_required = true;
            if (!observation.exact_source || !observation.source) {
                evidence.full_path_attribution_incomplete = true;
                evidence.incomplete_evidence = true;
                AddReason(
                    &evidence,
                    AudioSemanticDeltaReasonBankEvidenceIncomplete);
            }
        }
        if (!observation.reference_count_delta_observed) {
            evidence.bank_reference_evidence_incomplete = true;
            evidence.incomplete_evidence = true;
            evidence.semantic_delta = true;
            AddReason(
                &evidence,
                AudioSemanticDeltaReasonBankLoadRetainedStatePossible);
        } else if (observation.reference_count_delta != 0) {
            evidence.bank_reference_delta = true;
            evidence.semantic_delta = true;
            AddReason(
                &evidence,
                AudioSemanticDeltaReasonBankLoadRetainedStatePossible);
        }
        return true;
    }
    if (!observation.reference_count_delta_observed ||
        observation.reference_count_delta != 0 ||
        !observation.failed_lookup_no_retained_state_proven ||
        observation.before.matching_node_count != 0U ||
        observation.after.matching_node_count != 0U) {
        evidence.incomplete_evidence = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonFailedBankNoInsertionUnproven);
    }
    return true;
}

bool RecordAudioSemanticSpeechObservation(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    bool evidence_complete) noexcept
{
    if (!PrepareEpoch(ledger, epoch)) return false;
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    if (evidence.speech_observation_count != UINT32_MAX)
        ++evidence.speech_observation_count;
    if (!evidence_complete) {
        evidence.speech_evidence_incomplete = true;
        evidence.incomplete_evidence = true;
        AddReason(
            &evidence,
            AudioSemanticDeltaReasonSpeechEvidenceIncomplete);
    }
    return true;
}

bool RecordAudioSemanticObservationLoss(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    AudioSemanticDeltaReason reason) noexcept
{
    if (!PrepareEpoch(ledger, epoch)) return false;
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    evidence.observation_loss = true;
    evidence.incomplete_evidence = true;
    AddReason(&evidence, AudioSemanticDeltaReasonObservationLoss);
    AddReason(&evidence, reason);
    return true;
}

AudioSemanticEpochAssessment AssessAudioSemanticEpoch(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    bool hook_plan_complete) noexcept
{
    AudioSemanticEpochAssessment result = {};
    result.disposition = AudioSemanticEpochDisposition::Incomplete;
    result.epoch = epoch;
    result.hook_plan_complete = hook_plan_complete;
    if (!ledger || !epoch) {
        result.reason_mask =
            AudioSemanticDeltaReasonInvalidEpoch;
        return result;
    }
    if (!PrepareEpoch(ledger, epoch)) {
        result.reason_mask =
            ledger->evidence.reason_mask |
            AudioSemanticDeltaReasonEpochOverlap;
        return result;
    }
    AudioSemanticEpochEvidence& evidence = ledger->evidence;
    evidence.assessed = true;
    result.script_observation_count =
        evidence.script_observation_count;
    result.bank_observation_count =
        evidence.bank_observation_count;
    result.speech_observation_count =
        evidence.speech_observation_count;
    result.reason_mask = evidence.reason_mask;
    result.last_script = evidence.last_script;
    result.last_bank = evidence.last_bank;
    if (evidence.semantic_delta) {
        result.disposition =
            AudioSemanticEpochDisposition::Touched;
        return result;
    }
    if (!hook_plan_complete) {
        result.reason_mask |=
            AudioSemanticDeltaReasonHookPlanIncomplete;
        return result;
    }
    if (evidence.observation_loss ||
        evidence.incomplete_evidence)
        return result;
    if (evidence.script_observation_count == 0U &&
        evidence.bank_observation_count == 0U) {
        result.disposition =
            AudioSemanticEpochDisposition::Untouched;
        return result;
    }
    result.disposition =
        AudioSemanticEpochDisposition::Untouched;
    return result;
}

const char* AudioSemanticEpochDispositionName(
    AudioSemanticEpochDisposition disposition) noexcept
{
    switch (disposition) {
    case AudioSemanticEpochDisposition::Incomplete:
        return "Incomplete";
    case AudioSemanticEpochDisposition::Untouched:
        return "Untouched";
    case AudioSemanticEpochDisposition::Touched:
        return "Touched";
    default:
        return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_semantic
