#ifndef DLUMA_POST_DESTROY_PREFLIGHT_REPORT_H
#define DLUMA_POST_DESTROY_PREFLIGHT_REPORT_H

#include "AudioMinimumSemanticPlan.h"
#include "AudioScriptReloadAdapter.h"
#include "ContentDomainRestartRegistry.h"
#include "PlayerAbsentBaselineReset.h"
#include "PlayerGameplayParamsRestartAdapter.h"
#include "PlayerNaturalPresetResetAdapter.h"

#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::post_destroy_report {

constexpr std::size_t kPostDestroyBlockerNameCapacity = 96U;

enum AudioMissingPrerequisite : std::uint64_t {
    AudioMissingNone = 0,
    AudioMissingMinimumHookPlan = 1ULL << 0,
    AudioMissingSemanticBaseline = 1ULL << 1,
    AudioMissingEventFence = 1ULL << 2,
    AudioMissingScriptEvidence = 1ULL << 3,
    AudioMissingBankEvidence = 1ULL << 4,
    AudioMissingSpeechEvidence = 1ULL << 5,
    AudioMissingFullPathAttribution = 1ULL << 6,
    AudioMissingExactRelease = 1ULL << 7,
    AudioMissingBaselineReplay = 1ULL << 8,
    AudioMissingScriptReloadReceiverAbi = 1ULL << 9,
    AudioMissingBankReleaseReplayPlan = 1ULL << 10,
};

struct PostDestroyPreflightInput {
    cold_reboot::player_restart::PlayerGameplayParamsTouchAssessment
        player_assessment;
    cold_reboot::player_restart::PlayerRestartFailure player_failure;
    cold_reboot::player_restart::PlayerStartupBaselineKind
        player_baseline_kind;
    audio_minimum::AudioMinimumAssessment audio_assessment;
    player_absent_reset::PlayerAbsentResetFailure
        player_absent_reset_failure;
    audio_script_reload::AudioScriptReloadFailure
        audio_script_reload_failure;
    player_natural_reset::Phase player_natural_reset_phase;
    player_natural_reset::Failure
        player_natural_reset_failure;
    bool player_reset_available;
    bool audio_reset_available;
    bool player_reset_plan_prepared;
    bool player_natural_reset_plan_prepared;
    bool audio_script_reload_plan_prepared;
    bool audio_bank_plan_prepared;
};

struct PostDestroyPreflightReport {
    cold_reboot::ContentDomainMask blocking_domain_mask;
    std::uint32_t blocker_count;
    cold_reboot::player_restart::PlayerGameplayParamsTouchAssessment
        player_assessment;
    cold_reboot::player_restart::PlayerRestartFailure player_failure;
    cold_reboot::player_restart::PlayerStartupBaselineKind
        player_baseline_kind;
    audio_minimum::AudioMinimumDisposition audio_assessment;
    std::uint64_t audio_reason_mask;
    std::uint64_t audio_missing_prerequisites;
    std::uint32_t audio_script_observation_count;
    std::uint32_t audio_bank_observation_count;
    player_absent_reset::PlayerAbsentResetFailure
        player_absent_reset_failure;
    audio_script_reload::AudioScriptReloadFailure
        audio_script_reload_failure;
    player_natural_reset::Phase player_natural_reset_phase;
    player_natural_reset::Failure
        player_natural_reset_failure;
    bool player_reset_plan_prepared;
    bool player_natural_reset_plan_prepared;
    bool audio_script_reload_plan_prepared;
    bool audio_bank_plan_prepared;
    bool audio_script_delta;
    bool audio_bank_tree_delta;
    bool audio_bank_reference_delta;
    bool transaction_preparation_attempted;
    char first_blocker[kPostDestroyBlockerNameCapacity];
};

PostDestroyPreflightReport BuildPostDestroyPreflightReport(
    const PostDestroyPreflightInput& input) noexcept;

void NotePostDestroyTransactionPreparationAttempted(
    PostDestroyPreflightReport* report) noexcept;

const char* FirstPostDestroyBlockerName(
    const PostDestroyPreflightReport& report) noexcept;

}  // namespace dluma::fixes::custom_map_asset_isolation::post_destroy_report

#endif
