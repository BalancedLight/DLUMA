#include "fixes/custom_map_asset_isolation/AudioMinimumSemanticPlan.h"

#include <cstdio>

namespace minimum =
    dluma::fixes::custom_map_asset_isolation::audio_minimum;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

minimum::AudioMinimumSemanticSnapshot Snapshot(
    std::uint32_t definitions, std::uint32_t paths,
    std::uint32_t banks, std::uint64_t script_fingerprint,
    std::uint64_t bank_fingerprint,
    std::uint64_t bank_identity = 0xC001U) noexcept
{
    minimum::AudioMinimumSemanticSnapshot result = {};
    result.script.registry_owner = 0x1000U;
    result.script.definition_count = definitions;
    result.script.path_count = paths;
    result.script.procedure_count = 27U;
    result.script.property_group_count = 41U;
    result.script.semantic_fingerprint = script_fingerprint;
    result.script.failure_stage =
        dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot::
            AudioScriptSemanticStage::Complete;
    result.script.attempted = true;
    result.script.complete = true;
    result.banks.manager = 0x2000U;
    result.banks.bank_count = banks;
    result.banks.semantic_fingerprint = bank_fingerprint;
    result.banks.identity_fingerprint = bank_identity;
    result.banks.failure_stage =
        dluma::fixes::custom_map_asset_isolation::audio_semantic_snapshot::
            AudioBankSemanticStage::Complete;
    result.banks.attempted = true;
    result.banks.complete = true;
    minimum::AudioMinimumOwnerPublication owner = {};
    minimum::InitializeAudioMinimumOwnerPublication(
        &owner, 0x180000001ULL, 0xA7100U);
    (void)minimum::ObserveAudioMinimumAudioOwner(
        &owner, 0x3000U, result.banks.manager, 0x4000U);
    (void)minimum::ObserveAudioMinimumBankOwner(
        &owner, result.banks.manager);
    (void)minimum::CompleteAudioMinimumOwnerOperation(&owner);
    (void)minimum::SnapshotAudioMinimumOwnerIdentity(
        &owner, &result.owner);
    result.observer_revision = result.owner.observer_revision;
    result.active_event_fence_generation =
        result.owner.event_fence_generation;
    result.observer_revision_observed = true;
    result.event_fence_generation_observed = true;
    result.complete = true;
    return result;
}

minimum::AudioMinimumOperationCoverage Coverage() noexcept
{
    minimum::AudioMinimumOperationCoverage result = {};
    result.minimum_hook_plan_complete = true;
    result.event_fence_complete = true;
    result.script_evidence_complete = true;
    result.bank_evidence_complete = true;
    result.bank_reference_evidence_complete = true;
    result.speech_evidence_complete = false;
    return result;
}

bool TestMinimumBaselineNeedsNoSpeechForNoSpeechEpoch() noexcept
{
    minimum::AudioMinimumSemanticState state = {};
    minimum::InitializeAudioMinimumSemanticState(&state);
    const minimum::AudioMinimumSemanticSnapshot baseline =
        Snapshot(138U, 8U, 10U, 0x1111U, 0x2222U);
    minimum::AudioMinimumOperationCoverage coverage = Coverage();
    CHECK(minimum::CaptureAudioMinimumSemanticBaseline(
        &state, 1U, baseline, baseline, coverage));
    const minimum::AudioMinimumAssessment assessment =
        minimum::AssessAudioMinimumSemanticState(
            &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Untouched);
    CHECK(assessment.reason_mask == minimum::AudioMinimumReasonNone);
    return true;
}

bool TestOperationScopedEvidence() noexcept
{
    minimum::AudioMinimumSemanticState state = {};
    minimum::InitializeAudioMinimumSemanticState(&state);
    const minimum::AudioMinimumSemanticSnapshot baseline =
        Snapshot(138U, 8U, 10U, 0x1111U, 0x2222U);
    minimum::AudioMinimumOperationCoverage coverage = Coverage();
    CHECK(minimum::CaptureAudioMinimumSemanticBaseline(
        &state, 1U, baseline, baseline, coverage));

    coverage.script_observation_count = 1U;
    coverage.script_evidence_complete = false;
    minimum::AudioMinimumAssessment assessment =
        minimum::AssessAudioMinimumSemanticState(
            &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonScriptEvidenceIncomplete) != 0U);

    coverage.script_observation_count = 0U;
    coverage.script_evidence_complete = true;
    coverage.bank_observation_count = 1U;
    coverage.bank_evidence_complete = false;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonBankEvidenceIncomplete) != 0U);

    coverage.bank_observation_count = 0U;
    coverage.bank_evidence_complete = true;
    coverage.speech_observation_count = 1U;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonSpeechEvidenceIncomplete) != 0U);
    coverage.speech_evidence_complete = true;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Untouched);

    coverage.speech_observation_count = 0U;
    coverage.bank_observation_count = 1U;
    coverage.bank_reference_evidence_complete = false;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonBankEvidenceIncomplete) != 0U);
    return true;
}

bool TestPersistentAndNaturallyRestoredDeltas() noexcept
{
    minimum::AudioMinimumSemanticState state = {};
    minimum::InitializeAudioMinimumSemanticState(&state);
    const minimum::AudioMinimumSemanticSnapshot baseline =
        Snapshot(138U, 8U, 10U, 0x1111U, 0x2222U);
    minimum::AudioMinimumOperationCoverage coverage = Coverage();
    CHECK(minimum::CaptureAudioMinimumSemanticBaseline(
        &state, 1U, baseline, baseline, coverage));

    coverage.script_observation_count = 8U;
    const minimum::AudioMinimumSemanticSnapshot script_delta =
        Snapshot(139U, 9U, 10U, 0x1112U, 0x2222U);
    minimum::AudioMinimumAssessment assessment =
        minimum::AssessAudioMinimumSemanticState(
            &state, 1U, script_delta, script_delta, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Touched);
    CHECK(assessment.script_delta);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonScriptRegistryDelta) != 0U);

    coverage.script_observation_count = 0U;
    coverage.bank_observation_count = 12U;
    const minimum::AudioMinimumSemanticSnapshot bank_delta =
        Snapshot(138U, 8U, 11U, 0x1111U, 0x2223U, 0xC002U);
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, bank_delta, bank_delta, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Touched);
    CHECK(assessment.bank_delta);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonBankTreeDelta) != 0U);

    coverage.bank_reference_evidence_complete = false;
    coverage.full_path_attribution_required = true;
    coverage.full_path_attribution_complete = false;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, bank_delta, bank_delta, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Touched);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonBankEvidenceIncomplete) != 0U);
    CHECK((assessment.reason_mask &
           minimum::
               AudioMinimumReasonFullPathAttributionUnavailable) != 0U);

    /* An operation may have changed the tree transiently. The post-destroy
     * decision is untouched when both stable passes return to exact B0. */
    coverage.bank_reference_evidence_complete = true;
    coverage.full_path_attribution_required = false;
    coverage.full_path_attribution_complete = true;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Untouched);

    coverage.bank_reference_delta_detected = true;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Touched);
    CHECK(assessment.bank_reference_delta);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonBankReferenceDelta) != 0U);
    return true;
}

bool TestEventFenceAndStableDoublePassAreMandatory() noexcept
{
    minimum::AudioMinimumSemanticState state = {};
    minimum::InitializeAudioMinimumSemanticState(&state);
    const minimum::AudioMinimumSemanticSnapshot baseline =
        Snapshot(138U, 8U, 10U, 0x1111U, 0x2222U);
    minimum::AudioMinimumOperationCoverage coverage = Coverage();
    CHECK(minimum::CaptureAudioMinimumSemanticBaseline(
        &state, 1U, baseline, baseline, coverage));
    coverage.event_fence_complete = false;
    minimum::AudioMinimumAssessment assessment =
        minimum::AssessAudioMinimumSemanticState(
            &state, 1U, baseline, baseline, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonEventFenceUnavailable) != 0U);

    coverage.event_fence_complete = true;
    minimum::AudioMinimumSemanticSnapshot changed = baseline;
    changed.active_event_fence_generation = 4U;
    changed.owner.event_fence_generation = 4U;
    assessment = minimum::AssessAudioMinimumSemanticState(
        &state, 1U, baseline, changed, coverage);
    CHECK(assessment.disposition ==
          minimum::AudioMinimumDisposition::Incomplete);
    CHECK((assessment.reason_mask &
           minimum::AudioMinimumReasonSnapshotChanged) != 0U);
    return true;
}

}  // namespace

int main()
{
    if (!TestMinimumBaselineNeedsNoSpeechForNoSpeechEpoch() ||
        !TestOperationScopedEvidence() ||
        !TestPersistentAndNaturallyRestoredDeltas() ||
        !TestEventFenceAndStableDoublePassAreMandatory())
        return 1;
    std::puts("audio minimum semantic fake harness: PASS");
    return 0;
}
