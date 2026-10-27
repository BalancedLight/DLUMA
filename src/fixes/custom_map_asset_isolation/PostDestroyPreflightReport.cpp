#define _HAS_EXCEPTIONS 0

#include "PostDestroyPreflightReport.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::post_destroy_report {
namespace {

namespace player = cold_reboot::player_restart;
namespace audio = audio_minimum;

bool PlayerTouched(
    player::PlayerGameplayParamsTouchAssessment assessment) noexcept
{
    return assessment ==
            player::PlayerGameplayParamsTouchAssessment::
                TouchedPersistentOwner ||
        assessment ==
            player::PlayerGameplayParamsTouchAssessment::
                TouchedDefinitionDelta;
}

void CopyBlocker(PostDestroyPreflightReport* report,
                 const char* blocker) noexcept
{
    if (!report || report->first_blocker[0] || !blocker) return;
    std::size_t index = 0;
    while (blocker[index] &&
           index + 1U < sizeof(report->first_blocker)) {
        report->first_blocker[index] = blocker[index];
        ++index;
    }
    report->first_blocker[index] = 0;
}

const char* PlayerBlocker(
    const PostDestroyPreflightInput& input) noexcept
{
    if (input.player_assessment ==
        player::PlayerGameplayParamsTouchAssessment::Incomplete) {
        if (input.player_baseline_kind ==
            player::PlayerStartupBaselineKind::Unavailable) {
            if (input.player_failure ==
                player::PlayerRestartFailure::OperationsIncomplete)
                return "player_environment_operations_incomplete";
            if (input.player_failure ==
                player::PlayerRestartFailure::ExactRouteUnvalidated)
                return "player_inventory_routes_unvalidated";
            if (input.player_failure ==
                player::PlayerRestartFailure::AbsentBaselineUnproven)
                return "player_absent_zero_owner_predicate_failed";
            return "player_b0_baseline_not_committed";
        }
        if (input.player_failure ==
                player::PlayerRestartFailure::InventoryLoss ||
            input.player_failure ==
                player::PlayerRestartFailure::InventoryOverflow ||
            input.player_failure ==
                player::PlayerRestartFailure::InventoryChanged)
            return "player_observer_loss";
        return "player_snapshot_incomplete";
    }
    if (PlayerTouched(input.player_assessment) &&
        input.player_natural_reset_plan_prepared)
        return nullptr;
    if (PlayerTouched(input.player_assessment) &&
        !input.player_reset_available) {
        if (input.player_assessment ==
            player::PlayerGameplayParamsTouchAssessment::
                TouchedPersistentOwner)
            return "player_persistent_owner_release_path_unvalidated";
        return "player_gameplay_cache_restore_path_unvalidated";
    }
    return nullptr;
}

const char* AudioBlocker(
    const PostDestroyPreflightInput& input) noexcept
{
    const std::uint64_t reasons =
        input.audio_assessment.reason_mask;
    if (input.audio_assessment.disposition ==
        audio::AudioMinimumDisposition::Incomplete) {
        if ((reasons &
             audio::AudioMinimumReasonBaselineUnavailable) != 0U)
            return "audio_minimum_baseline_not_committed";
        if ((reasons &
             audio::AudioMinimumReasonHookPlanIncomplete) != 0U)
            return "audio_minimum_hook_plan_incomplete";
        if ((reasons & audio::AudioMinimumReasonObserverLoss) != 0U)
            return "audio_observer_loss";
        if ((reasons &
             audio::AudioMinimumReasonEventFenceUnavailable) != 0U)
            return "audio_event_fence_unavailable";
        if ((reasons &
             audio::AudioMinimumReasonSnapshotChanged) != 0U)
            return "audio_minimum_two_pass_snapshot_changed";
        return "audio_minimum_semantic_snapshot_incomplete";
    }
    if (input.audio_assessment.disposition ==
            audio::AudioMinimumDisposition::Touched &&
        !input.audio_reset_available) {
        if (input.audio_assessment.script_delta &&
            input.audio_assessment.bank_delta &&
            !input.audio_assessment.bank_reference_delta)
            return
                "audio_touched_script_registry_and_bank_tree_reset_unavailable";
        if ((reasons &
             audio::AudioMinimumReasonBankReferenceDelta) != 0U)
            return "audio_bank_reference_release_replay_unvalidated";
        if ((reasons &
             audio::AudioMinimumReasonBankTreeDelta) != 0U)
            return "audio_bank_tree_release_replay_unvalidated";
        if ((reasons &
             audio::AudioMinimumReasonScriptRegistryDelta) != 0U) {
            if (input.audio_script_reload_failure ==
                    audio_script_reload::
                        AudioScriptReloadFailure::
                            ReceiverUnvalidated ||
                input.audio_script_reload_failure ==
                    audio_script_reload::
                        AudioScriptReloadFailure::AbiUnvalidated)
                return "audio_script_reload_receiver_abi_unvalidated";
            if (input.audio_script_reload_failure ==
                    audio_script_reload::
                        AudioScriptReloadFailure::
                            AudioLockNotOwned)
                return "audio_script_reload_lock_unvalidated";
            return "audio_script_reload_plan_unavailable";
        }
        return "audio_exact_release_unavailable";
    }
    return nullptr;
}

}  // namespace

PostDestroyPreflightReport BuildPostDestroyPreflightReport(
    const PostDestroyPreflightInput& input) noexcept
{
    PostDestroyPreflightReport report = {};
    report.player_assessment = input.player_assessment;
    report.player_failure = input.player_failure;
    report.player_baseline_kind = input.player_baseline_kind;
    report.audio_assessment =
        input.audio_assessment.disposition;
    report.audio_reason_mask =
        input.audio_assessment.reason_mask;
    report.audio_script_observation_count =
        input.audio_assessment.script_observation_count;
    report.audio_bank_observation_count =
        input.audio_assessment.bank_observation_count;
    report.player_absent_reset_failure =
        input.player_absent_reset_failure;
    report.audio_script_reload_failure =
        input.audio_script_reload_failure;
    report.player_natural_reset_phase =
        input.player_natural_reset_phase;
    report.player_natural_reset_failure =
        input.player_natural_reset_failure;
    report.player_reset_plan_prepared =
        input.player_reset_plan_prepared;
    report.player_natural_reset_plan_prepared =
        input.player_natural_reset_plan_prepared;
    report.audio_script_reload_plan_prepared =
        input.audio_script_reload_plan_prepared;
    report.audio_bank_plan_prepared =
        input.audio_bank_plan_prepared;
    report.audio_script_delta =
        input.audio_assessment.script_delta;
    report.audio_bank_tree_delta =
        input.audio_assessment.bank_delta;
    report.audio_bank_reference_delta =
        input.audio_assessment.bank_reference_delta;

    const char* const player_blocker = PlayerBlocker(input);
    if (player_blocker) {
        report.blocking_domain_mask |= cold_reboot::DomainBit(
            cold_reboot::ContentDomainId::PlayerGameplayParams);
        ++report.blocker_count;
        CopyBlocker(&report, player_blocker);
    }
    const char* const audio_blocker = AudioBlocker(input);
    if (audio_blocker) {
        report.blocking_domain_mask |= cold_reboot::DomainBit(
            cold_reboot::ContentDomainId::Audio);
        ++report.blocker_count;
        CopyBlocker(&report, audio_blocker);
    }

    const std::uint64_t reasons =
        input.audio_assessment.reason_mask;
    if (!input.audio_assessment.hook_plan_complete)
        report.audio_missing_prerequisites |=
            AudioMissingMinimumHookPlan;
    if ((reasons &
         audio::AudioMinimumReasonBaselineUnavailable) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingSemanticBaseline;
    if (!input.audio_assessment.event_fence_complete)
        report.audio_missing_prerequisites |=
            AudioMissingEventFence;
    if ((reasons &
         audio::AudioMinimumReasonScriptEvidenceIncomplete) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingScriptEvidence;
    if ((reasons &
         audio::AudioMinimumReasonBankEvidenceIncomplete) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingBankEvidence;
    if ((reasons &
         audio::AudioMinimumReasonSpeechEvidenceIncomplete) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingSpeechEvidence;
    if ((reasons &
         audio::AudioMinimumReasonFullPathAttributionUnavailable) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingFullPathAttribution;
    if ((reasons &
         audio::AudioMinimumReasonExactReleaseUnavailable) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingExactRelease;
    if ((reasons &
         audio::AudioMinimumReasonBaselineReplayUnavailable) != 0U)
        report.audio_missing_prerequisites |=
            AudioMissingBaselineReplay;
    if (input.audio_assessment.script_delta &&
        !input.audio_script_reload_plan_prepared)
        report.audio_missing_prerequisites |=
            AudioMissingScriptReloadReceiverAbi;
    if ((input.audio_assessment.bank_delta ||
         input.audio_assessment.bank_reference_delta) &&
        !input.audio_bank_plan_prepared)
        report.audio_missing_prerequisites |=
            AudioMissingBankReleaseReplayPlan;
    if (!report.first_blocker[0])
        CopyBlocker(&report, "none");
    return report;
}

void NotePostDestroyTransactionPreparationAttempted(
    PostDestroyPreflightReport* report) noexcept
{
    if (report)
        report->transaction_preparation_attempted = true;
}

const char* FirstPostDestroyBlockerName(
    const PostDestroyPreflightReport& report) noexcept
{
    return report.first_blocker[0]
        ? report.first_blocker : "none";
}

}  // namespace dluma::fixes::custom_map_asset_isolation::post_destroy_report
