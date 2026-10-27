#define _HAS_EXCEPTIONS 0

#include "AudioMinimumSemanticPlan.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_minimum {
namespace {

std::uint64_t IncrementNonzero(std::uint64_t value) noexcept
{
    ++value;
    return value ? value : 1U;
}

std::uint32_t IncrementSaturating(std::uint32_t value) noexcept
{
    return value == UINT32_MAX ? UINT32_MAX : value + 1U;
}

void TouchOwnerPublication(AudioMinimumOwnerIdentity* identity) noexcept
{
    if (identity)
        identity->hook_publication_generation =
            IncrementNonzero(identity->hook_publication_generation);
}

void RefreshOwnerExact(AudioMinimumOwnerIdentity* identity) noexcept
{
    if (!identity) return;
    identity->exact =
        !identity->conflict &&
        identity->audio_fmod_receiver &&
        identity->sound_bank_manager_receiver &&
        identity->singleton_or_owner_slot &&
        identity->supported_image_identity &&
        identity->route_fingerprint &&
        identity->audio_observation_count != 0U &&
        identity->manager_observation_count >= 2U &&
        identity->observer_revision_observed &&
        identity->event_fence_generation_observed;
}

bool SnapshotComplete(
    const AudioMinimumSemanticSnapshot& snapshot) noexcept
{
    return snapshot.complete &&
        snapshot.script.complete &&
        snapshot.banks.complete &&
        snapshot.script.registry_owner &&
        snapshot.script.semantic_fingerprint &&
        snapshot.banks.manager &&
        snapshot.banks.semantic_fingerprint &&
        snapshot.banks.identity_fingerprint &&
        snapshot.owner.exact && !snapshot.owner.conflict &&
        snapshot.owner.sound_bank_manager_receiver ==
            snapshot.banks.manager &&
        snapshot.observer_revision_observed &&
        snapshot.event_fence_generation_observed &&
        snapshot.observer_revision ==
            snapshot.owner.observer_revision &&
        snapshot.active_event_fence_generation ==
            snapshot.owner.event_fence_generation;
}

bool SnapshotPairStable(
    const AudioMinimumSemanticSnapshot& first,
    const AudioMinimumSemanticSnapshot& second) noexcept
{
    return SnapshotComplete(first) && SnapshotComplete(second) &&
        AudioMinimumOwnerIdentitiesEqual(
            first.owner, second.owner) &&
        first.observer_revision == second.observer_revision &&
        first.active_event_fence_generation ==
            second.active_event_fence_generation &&
        AudioMinimumScriptSnapshotsEqual(
            first.script, second.script) &&
        AudioMinimumBankSnapshotsEqual(first.banks, second.banks);
}

}  // namespace

void InitializeAudioMinimumOwnerPublication(
    AudioMinimumOwnerPublication* state,
    std::uint64_t supported_image_identity,
    std::uint64_t route_fingerprint) noexcept
{
    if (!state) return;
    std::memset(state, 0, sizeof(*state));
    state->identity.supported_image_identity =
        supported_image_identity;
    state->identity.route_fingerprint = route_fingerprint;
}

bool ObserveAudioMinimumAudioOwner(
    AudioMinimumOwnerPublication* state,
    std::uintptr_t audio_fmod_receiver,
    std::uintptr_t sound_bank_manager_receiver,
    std::uintptr_t singleton_or_owner_slot) noexcept
{
    if (!state || !audio_fmod_receiver ||
        !sound_bank_manager_receiver || !singleton_or_owner_slot)
        return false;
    AudioMinimumOwnerIdentity& identity = state->identity;
    if ((identity.audio_fmod_receiver &&
         identity.audio_fmod_receiver != audio_fmod_receiver) ||
        (identity.sound_bank_manager_receiver &&
         identity.sound_bank_manager_receiver !=
             sound_bank_manager_receiver) ||
        (identity.singleton_or_owner_slot &&
         identity.singleton_or_owner_slot != singleton_or_owner_slot)) {
        identity.conflict = true;
        TouchOwnerPublication(&identity);
        RefreshOwnerExact(&identity);
        return false;
    }
    identity.audio_fmod_receiver = audio_fmod_receiver;
    identity.sound_bank_manager_receiver =
        sound_bank_manager_receiver;
    identity.singleton_or_owner_slot = singleton_or_owner_slot;
    identity.audio_observation_count =
        IncrementSaturating(identity.audio_observation_count);
    /* The validated CAudioFMOD +0x1E28 field is an independent observation
     * of the same manager later received by SoundBankManagerFMOD::LoadBank. */
    identity.manager_observation_count =
        IncrementSaturating(identity.manager_observation_count);
    identity.observer_revision =
        IncrementNonzero(identity.observer_revision);
    identity.observer_revision_observed = true;
    TouchOwnerPublication(&identity);
    RefreshOwnerExact(&identity);
    return !identity.conflict;
}

bool ObserveAudioMinimumBankOwner(
    AudioMinimumOwnerPublication* state,
    std::uintptr_t sound_bank_manager_receiver) noexcept
{
    if (!state || !sound_bank_manager_receiver) return false;
    AudioMinimumOwnerIdentity& identity = state->identity;
    if (identity.sound_bank_manager_receiver &&
        identity.sound_bank_manager_receiver !=
            sound_bank_manager_receiver) {
        identity.conflict = true;
        TouchOwnerPublication(&identity);
        RefreshOwnerExact(&identity);
        return false;
    }
    identity.sound_bank_manager_receiver =
        sound_bank_manager_receiver;
    identity.manager_observation_count =
        IncrementSaturating(identity.manager_observation_count);
    identity.observer_revision =
        IncrementNonzero(identity.observer_revision);
    identity.observer_revision_observed = true;
    TouchOwnerPublication(&identity);
    RefreshOwnerExact(&identity);
    return !identity.conflict;
}

bool CompleteAudioMinimumOwnerOperation(
    AudioMinimumOwnerPublication* state) noexcept
{
    if (!state) return false;
    AudioMinimumOwnerIdentity& identity = state->identity;
    if (!identity.audio_fmod_receiver &&
        !identity.sound_bank_manager_receiver)
        return false;
    identity.event_fence_generation =
        IncrementNonzero(identity.event_fence_generation);
    identity.event_fence_generation_observed = true;
    TouchOwnerPublication(&identity);
    RefreshOwnerExact(&identity);
    return !identity.conflict;
}

bool SnapshotAudioMinimumOwnerIdentity(
    const AudioMinimumOwnerPublication* state,
    AudioMinimumOwnerIdentity* output) noexcept
{
    if (!state || !output) return false;
    *output = state->identity;
    RefreshOwnerExact(output);
    return output->exact;
}

bool AudioMinimumOwnerIdentitiesEqual(
    const AudioMinimumOwnerIdentity& left,
    const AudioMinimumOwnerIdentity& right) noexcept
{
    return left.audio_fmod_receiver == right.audio_fmod_receiver &&
        left.sound_bank_manager_receiver ==
            right.sound_bank_manager_receiver &&
        left.singleton_or_owner_slot ==
            right.singleton_or_owner_slot &&
        left.hook_publication_generation ==
            right.hook_publication_generation &&
        left.route_fingerprint == right.route_fingerprint &&
        left.supported_image_identity ==
            right.supported_image_identity &&
        left.observer_revision == right.observer_revision &&
        left.event_fence_generation ==
            right.event_fence_generation &&
        left.audio_observation_count ==
            right.audio_observation_count &&
        left.manager_observation_count ==
            right.manager_observation_count &&
        left.observer_revision_observed ==
            right.observer_revision_observed &&
        left.event_fence_generation_observed ==
            right.event_fence_generation_observed &&
        left.exact == right.exact &&
        left.conflict == right.conflict;
}

bool AudioMinimumScriptSnapshotsEqual(
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& left,
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& right)
    noexcept
{
    return left.complete && right.complete &&
        left.registry_owner == right.registry_owner &&
        left.semantic_fingerprint == right.semantic_fingerprint &&
        left.definition_count == right.definition_count &&
        left.path_count == right.path_count &&
        left.procedure_count == right.procedure_count &&
        left.property_group_count == right.property_group_count &&
        left.property_tree_raw_root ==
            right.property_tree_raw_root &&
        left.property_tree_raw_count ==
            right.property_tree_raw_count &&
        left.property_tree_raw_padding ==
            right.property_tree_raw_padding &&
        left.property_tree_raw_allocator_tag ==
            right.property_tree_raw_allocator_tag &&
        left.property_tree_configured_bound ==
            right.property_tree_configured_bound &&
        left.property_tree_header_validation_result ==
            right.property_tree_header_validation_result;
}

bool AudioMinimumBankSnapshotsEqual(
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& left,
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& right)
    noexcept
{
    return left.complete && right.complete &&
        left.manager == right.manager &&
        left.tree_root == right.tree_root &&
        left.bank_count == right.bank_count &&
        left.semantic_fingerprint == right.semantic_fingerprint &&
        left.identity_fingerprint == right.identity_fingerprint;
}

void InitializeAudioMinimumSemanticState(
    AudioMinimumSemanticState* state) noexcept
{
    if (state) std::memset(state, 0, sizeof(*state));
}

bool CaptureAudioMinimumSemanticBaseline(
    AudioMinimumSemanticState* state,
    std::uint64_t baseline_generation,
    const AudioMinimumSemanticSnapshot& first,
    const AudioMinimumSemanticSnapshot& second,
    const AudioMinimumOperationCoverage& coverage) noexcept
{
    if (!state || !baseline_generation ||
        state->baseline_captured ||
        !coverage.minimum_hook_plan_complete ||
        coverage.observer_loss ||
        !coverage.event_fence_complete ||
        !SnapshotPairStable(first, second))
        return false;
    state->baseline = second;
    state->baseline.baseline_generation = baseline_generation;
    state->baseline_captured = true;
    ++state->capture_generation;
    if (!state->capture_generation) ++state->capture_generation;
    return true;
}

AudioMinimumAssessment AssessAudioMinimumSemanticState(
    const AudioMinimumSemanticState* state,
    std::uint64_t baseline_generation,
    const AudioMinimumSemanticSnapshot& first,
    const AudioMinimumSemanticSnapshot& second,
    const AudioMinimumOperationCoverage& coverage) noexcept
{
    AudioMinimumAssessment result = {};
    result.disposition = AudioMinimumDisposition::Incomplete;
    result.baseline_generation = baseline_generation;
    result.script_observation_count =
        coverage.script_observation_count;
    result.bank_observation_count =
        coverage.bank_observation_count;
    result.speech_observation_count =
        coverage.speech_observation_count;
    result.hook_plan_complete =
        coverage.minimum_hook_plan_complete;
    result.event_fence_complete = coverage.event_fence_complete;
    if (!state || !baseline_generation) {
        result.reason_mask = AudioMinimumReasonInvalidArgument;
        return result;
    }
    if (!state->baseline_captured ||
        state->baseline.baseline_generation != baseline_generation) {
        result.reason_mask = AudioMinimumReasonBaselineUnavailable;
        return result;
    }
    if (!coverage.minimum_hook_plan_complete)
        result.reason_mask |= AudioMinimumReasonHookPlanIncomplete;
    if (coverage.observer_loss)
        result.reason_mask |= AudioMinimumReasonObserverLoss;
    if (!coverage.event_fence_complete)
        result.reason_mask |= AudioMinimumReasonEventFenceUnavailable;
    if (!SnapshotComplete(first) || !SnapshotComplete(second))
        result.reason_mask |=
            AudioMinimumReasonSemanticSnapshotIncomplete;
    else if (!SnapshotPairStable(first, second))
        result.reason_mask |= AudioMinimumReasonSnapshotChanged;
    if (result.reason_mask != AudioMinimumReasonNone)
        return result;

    result.script_delta = !AudioMinimumScriptSnapshotsEqual(
        state->baseline.script, second.script);
    result.bank_delta = !AudioMinimumBankSnapshotsEqual(
        state->baseline.banks, second.banks);
    result.bank_reference_delta =
        coverage.bank_reference_delta_detected;

    if (coverage.script_observation_count != 0U &&
        !coverage.script_evidence_complete)
        result.reason_mask |= AudioMinimumReasonScriptEvidenceIncomplete;
    if (coverage.bank_observation_count != 0U &&
        !coverage.bank_evidence_complete)
        result.reason_mask |= AudioMinimumReasonBankEvidenceIncomplete;
    if (coverage.speech_observation_count != 0U &&
        !coverage.speech_evidence_complete)
        result.reason_mask |= AudioMinimumReasonSpeechEvidenceIncomplete;

    /* Tree equality does not prove reference equality. An observed successful
     * cache hit may retain a reference without changing the tree, so every
     * bank operation needs either an exact reference delta or a validated
     * no-retention path before an equal B0/post tree can be called untouched. */
    if (coverage.bank_observation_count != 0U &&
        !coverage.bank_reference_evidence_complete)
        result.reason_mask |= AudioMinimumReasonBankEvidenceIncomplete;
    if (coverage.full_path_attribution_required &&
        !coverage.full_path_attribution_complete)
        result.reason_mask |=
            AudioMinimumReasonFullPathAttributionUnavailable;

    if (result.script_delta)
        result.reason_mask |= AudioMinimumReasonScriptRegistryDelta;
    if (result.bank_delta)
        result.reason_mask |= AudioMinimumReasonBankTreeDelta;
    if (result.bank_reference_delta)
        result.reason_mask |= AudioMinimumReasonBankReferenceDelta;
    if (result.script_delta || result.bank_delta ||
        result.bank_reference_delta) {
        if (!coverage.exact_release_available)
            result.reason_mask |= AudioMinimumReasonExactReleaseUnavailable;
        if (!coverage.baseline_replay_available)
            result.reason_mask |= AudioMinimumReasonBaselineReplayUnavailable;
        result.disposition = AudioMinimumDisposition::Touched;
        return result;
    }
    if (result.reason_mask != AudioMinimumReasonNone)
        return result;
    result.disposition = AudioMinimumDisposition::Untouched;
    return result;
}

const char* AudioMinimumDispositionName(
    AudioMinimumDisposition disposition) noexcept
{
    switch (disposition) {
    case AudioMinimumDisposition::Incomplete:
        return "Incomplete";
    case AudioMinimumDisposition::Untouched:
        return "Untouched";
    case AudioMinimumDisposition::Touched:
        return "Touched";
    default:
        return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_minimum
