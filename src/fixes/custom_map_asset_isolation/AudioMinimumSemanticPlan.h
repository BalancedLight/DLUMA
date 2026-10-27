#ifndef DLUMA_AUDIO_MINIMUM_SEMANTIC_PLAN_H
#define DLUMA_AUDIO_MINIMUM_SEMANTIC_PLAN_H

#include "AudioSemanticWindowsSnapshot.h"

#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_minimum {

enum class AudioMinimumDisposition : std::uint8_t {
    Incomplete,
    Untouched,
    Touched,
};

enum AudioMinimumReason : std::uint64_t {
    AudioMinimumReasonNone = 0,
    AudioMinimumReasonInvalidArgument = 1ULL << 0,
    AudioMinimumReasonBaselineUnavailable = 1ULL << 1,
    AudioMinimumReasonHookPlanIncomplete = 1ULL << 2,
    AudioMinimumReasonObserverLoss = 1ULL << 3,
    AudioMinimumReasonEventFenceUnavailable = 1ULL << 4,
    AudioMinimumReasonSemanticSnapshotIncomplete = 1ULL << 5,
    AudioMinimumReasonSnapshotChanged = 1ULL << 6,
    AudioMinimumReasonScriptEvidenceIncomplete = 1ULL << 7,
    AudioMinimumReasonBankEvidenceIncomplete = 1ULL << 8,
    AudioMinimumReasonSpeechEvidenceIncomplete = 1ULL << 9,
    AudioMinimumReasonFullPathAttributionUnavailable = 1ULL << 10,
    AudioMinimumReasonScriptRegistryDelta = 1ULL << 11,
    AudioMinimumReasonBankTreeDelta = 1ULL << 12,
    AudioMinimumReasonExactReleaseUnavailable = 1ULL << 13,
    AudioMinimumReasonBaselineReplayUnavailable = 1ULL << 14,
    AudioMinimumReasonBankReferenceDelta = 1ULL << 15,
    AudioMinimumReasonOwnerIdentityIncomplete = 1ULL << 16,
    AudioMinimumReasonOwnerIdentityConflict = 1ULL << 17,
    AudioMinimumReasonObserverRevisionUnpublished = 1ULL << 18,
    AudioMinimumReasonEventFenceUnpublished = 1ULL << 19,
};

struct AudioMinimumOwnerIdentity {
    std::uintptr_t audio_fmod_receiver;
    std::uintptr_t sound_bank_manager_receiver;
    std::uintptr_t singleton_or_owner_slot;
    std::uint64_t hook_publication_generation;
    std::uint64_t route_fingerprint;
    std::uint64_t supported_image_identity;
    std::uint64_t observer_revision;
    std::uint64_t event_fence_generation;
    std::uint32_t audio_observation_count;
    std::uint32_t manager_observation_count;
    bool observer_revision_observed;
    bool event_fence_generation_observed;
    bool exact;
    bool conflict;
};

struct AudioMinimumOwnerPublication {
    AudioMinimumOwnerIdentity identity;
};

struct AudioMinimumOperationCoverage {
    std::uint32_t script_observation_count;
    std::uint32_t bank_observation_count;
    std::uint32_t speech_observation_count;
    bool minimum_hook_plan_complete;
    bool observer_loss;
    bool event_fence_complete;
    bool script_evidence_complete;
    bool bank_evidence_complete;
    bool bank_reference_evidence_complete;
    bool bank_reference_delta_detected;
    bool speech_evidence_complete;
    bool full_path_attribution_required;
    bool full_path_attribution_complete;
    bool exact_release_available;
    bool baseline_replay_available;
};

struct AudioMinimumSemanticSnapshot {
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot script;
    audio_semantic_snapshot::AudioBankTreeSemanticSnapshot banks;
    AudioMinimumOwnerIdentity owner;
    std::uint64_t baseline_generation;
    std::uint64_t observer_revision;
    std::uint64_t active_event_fence_generation;
    bool observer_revision_observed;
    bool event_fence_generation_observed;
    bool complete;
};

struct AudioMinimumSemanticState {
    AudioMinimumSemanticSnapshot baseline;
    std::uint64_t capture_generation;
    bool baseline_captured;
};

struct AudioMinimumAssessment {
    AudioMinimumDisposition disposition;
    std::uint64_t reason_mask;
    std::uint64_t baseline_generation;
    std::uint32_t script_observation_count;
    std::uint32_t bank_observation_count;
    std::uint32_t speech_observation_count;
    bool hook_plan_complete;
    bool event_fence_complete;
    bool script_delta;
    bool bank_delta;
    bool bank_reference_delta;
};

void InitializeAudioMinimumSemanticState(
    AudioMinimumSemanticState* state) noexcept;

void InitializeAudioMinimumOwnerPublication(
    AudioMinimumOwnerPublication* state,
    std::uint64_t supported_image_identity,
    std::uint64_t route_fingerprint) noexcept;
bool ObserveAudioMinimumAudioOwner(
    AudioMinimumOwnerPublication* state,
    std::uintptr_t audio_fmod_receiver,
    std::uintptr_t sound_bank_manager_receiver,
    std::uintptr_t singleton_or_owner_slot) noexcept;
bool ObserveAudioMinimumBankOwner(
    AudioMinimumOwnerPublication* state,
    std::uintptr_t sound_bank_manager_receiver) noexcept;
bool CompleteAudioMinimumOwnerOperation(
    AudioMinimumOwnerPublication* state) noexcept;
bool SnapshotAudioMinimumOwnerIdentity(
    const AudioMinimumOwnerPublication* state,
    AudioMinimumOwnerIdentity* output) noexcept;
bool AudioMinimumOwnerIdentitiesEqual(
    const AudioMinimumOwnerIdentity& left,
    const AudioMinimumOwnerIdentity& right) noexcept;

bool CaptureAudioMinimumSemanticBaseline(
    AudioMinimumSemanticState* state,
    std::uint64_t baseline_generation,
    const AudioMinimumSemanticSnapshot& first,
    const AudioMinimumSemanticSnapshot& second,
    const AudioMinimumOperationCoverage& coverage) noexcept;

AudioMinimumAssessment AssessAudioMinimumSemanticState(
    const AudioMinimumSemanticState* state,
    std::uint64_t baseline_generation,
    const AudioMinimumSemanticSnapshot& first,
    const AudioMinimumSemanticSnapshot& second,
    const AudioMinimumOperationCoverage& coverage) noexcept;

bool AudioMinimumScriptSnapshotsEqual(
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& left,
    const audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot& right)
    noexcept;
bool AudioMinimumBankSnapshotsEqual(
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& left,
    const audio_semantic_snapshot::AudioBankTreeSemanticSnapshot& right)
    noexcept;

const char* AudioMinimumDispositionName(
    AudioMinimumDisposition disposition) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_minimum

#endif
