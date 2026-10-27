#ifndef DLUMA_AUDIO_SEMANTIC_DELTA_H
#define DLUMA_AUDIO_SEMANTIC_DELTA_H

#include "AudioSemanticWindowsSnapshot.h"
#include "CustomContentLedger.h"

#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::audio_semantic {

constexpr std::size_t kAudioSemanticLogicalPathCapacity = 512U;

enum class AudioSemanticEpochDisposition : std::uint8_t {
    Incomplete,
    Untouched,
    Touched,
};

enum AudioSemanticDeltaReason : std::uint64_t {
    AudioSemanticDeltaReasonNone = 0,
    AudioSemanticDeltaReasonInvalidEpoch = 1ULL << 0,
    AudioSemanticDeltaReasonEpochOverlap = 1ULL << 1,
    AudioSemanticDeltaReasonObservationLoss = 1ULL << 2,
    AudioSemanticDeltaReasonHookPlanIncomplete = 1ULL << 3,
    AudioSemanticDeltaReasonScriptEvidenceIncomplete = 1ULL << 4,
    AudioSemanticDeltaReasonScriptRegistryDelta = 1ULL << 5,
    AudioSemanticDeltaReasonBankEvidenceIncomplete = 1ULL << 6,
    AudioSemanticDeltaReasonBankTreeDelta = 1ULL << 7,
    AudioSemanticDeltaReasonBankLoadRetainedStatePossible = 1ULL << 8,
    AudioSemanticDeltaReasonFailedBankNoInsertionUnproven = 1ULL << 9,
    AudioSemanticDeltaReasonPairedEvidenceMissing = 1ULL << 10,
    AudioSemanticDeltaReasonSpeechEvidenceIncomplete = 1ULL << 11,
};

struct AudioSemanticScriptObservation {
    std::uint32_t epoch;
    std::uintptr_t source;
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot before;
    audio_semantic_snapshot::AudioScriptRegistrySemanticSnapshot after;
    bool exact_source;
    bool logical_path_complete;
    bool parse_result_observed;
    bool parse_succeeded;
    char logical_path[kAudioSemanticLogicalPathCapacity];
};

struct AudioSemanticBankObservation {
    std::uint32_t epoch;
    std::uintptr_t source;
    audio_semantic_snapshot::AudioBankTreeSemanticSnapshot before;
    audio_semantic_snapshot::AudioBankTreeSemanticSnapshot after;
    std::int64_t reference_count_delta;
    int result;
    bool exact_source;
    bool bank_name_complete;
    bool result_observed;
    bool reference_count_delta_observed;
    bool failed_lookup_no_retained_state_proven;
    char bank_name[kAudioSemanticLogicalPathCapacity];
};

struct AudioSemanticEpochEvidence {
    std::uint32_t epoch;
    std::uint32_t script_observation_count;
    std::uint32_t bank_observation_count;
    std::uint32_t speech_observation_count;
    std::uint64_t reason_mask;
    AudioSemanticScriptObservation last_script;
    AudioSemanticBankObservation last_bank;
    bool initialized;
    bool observation_loss;
    bool semantic_delta;
    bool incomplete_evidence;
    bool bank_reference_evidence_incomplete;
    bool bank_reference_delta;
    bool full_path_attribution_required;
    bool full_path_attribution_incomplete;
    bool speech_evidence_incomplete;
    bool assessed;
};

struct AudioSemanticDeltaLedger {
    AudioSemanticEpochEvidence evidence;
    std::uint64_t generation;
    std::uint64_t retired_transaction;
    std::uint64_t retired_observation_count;
    std::uint32_t retired_epoch;
};

struct AudioSemanticEpochAssessment {
    AudioSemanticEpochDisposition disposition;
    std::uint32_t epoch;
    std::uint32_t script_observation_count;
    std::uint32_t bank_observation_count;
    std::uint32_t speech_observation_count;
    std::uint64_t reason_mask;
    AudioSemanticScriptObservation last_script;
    AudioSemanticBankObservation last_bank;
    bool hook_plan_complete;
};

void InitializeAudioSemanticDeltaLedger(
    AudioSemanticDeltaLedger* ledger) noexcept;
// Assessment is observational and cannot retire an epoch. Only the final
// verified transaction and closed-observer receipt permits record reuse.
bool RetireAudioSemanticEpoch(AudioSemanticDeltaLedger*,
    const content::VerifiedEpochRetirement&) noexcept;

bool RecordAudioSemanticScriptObservation(
    AudioSemanticDeltaLedger* ledger,
    const AudioSemanticScriptObservation& observation) noexcept;

bool RecordAudioSemanticBankObservation(
    AudioSemanticDeltaLedger* ledger,
    const AudioSemanticBankObservation& observation) noexcept;

bool RecordAudioSemanticSpeechObservation(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    bool evidence_complete) noexcept;

bool RecordAudioSemanticObservationLoss(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    AudioSemanticDeltaReason reason) noexcept;

AudioSemanticEpochAssessment AssessAudioSemanticEpoch(
    AudioSemanticDeltaLedger* ledger, std::uint32_t epoch,
    bool hook_plan_complete) noexcept;

const char* AudioSemanticEpochDispositionName(
    AudioSemanticEpochDisposition disposition) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::audio_semantic

#endif
