#include "fixes/custom_map_asset_isolation/PostDestroyPreflightReport.h"

#include <cstdio>
#include <cstring>

namespace report =
    dluma::fixes::custom_map_asset_isolation::post_destroy_report;
namespace cold =
    dluma::fixes::custom_map_asset_isolation::cold_reboot;
namespace player = cold::player_restart;
namespace audio =
    dluma::fixes::custom_map_asset_isolation::audio_minimum;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);       \
            return false;                                                    \
        }                                                                    \
    } while (0)

bool TestRuntimeFourMaskDecodesBothDomains() noexcept
{
    report::PostDestroyPreflightInput input = {};
    input.player_assessment =
        player::PlayerGameplayParamsTouchAssessment::Incomplete;
    input.player_failure =
        player::PlayerRestartFailure::StateSnapshotFailed;
    input.player_baseline_kind =
        player::PlayerStartupBaselineKind::Unavailable;
    input.audio_assessment.disposition =
        audio::AudioMinimumDisposition::Incomplete;
    input.audio_assessment.reason_mask =
        audio::AudioMinimumReasonBaselineUnavailable |
        audio::AudioMinimumReasonSemanticSnapshotIncomplete;
    input.audio_assessment.script_observation_count = 8U;
    input.audio_assessment.bank_observation_count = 12U;
    const report::PostDestroyPreflightReport result =
        report::BuildPostDestroyPreflightReport(input);
    CHECK(result.blocking_domain_mask == 131074U);
    CHECK(result.blocker_count == 2U);
    CHECK(std::strcmp(
        result.first_blocker,
        "player_b0_baseline_not_committed") == 0);
    CHECK(result.audio_script_observation_count == 8U);
    CHECK(result.audio_bank_observation_count == 12U);
    CHECK((result.audio_missing_prerequisites &
           report::AudioMissingSemanticBaseline) != 0U);
    CHECK(!result.transaction_preparation_attempted);
    return true;
}

bool TestTouchedResetUnavailableIsExactAndPremutation() noexcept
{
    report::PostDestroyPreflightInput input = {};
    input.player_assessment =
        player::PlayerGameplayParamsTouchAssessment::
            TouchedPersistentOwner;
    input.player_failure =
        player::PlayerRestartFailure::TouchedResetUnavailable;
    input.player_baseline_kind =
        player::PlayerStartupBaselineKind::Absent;
    input.audio_assessment.disposition =
        audio::AudioMinimumDisposition::Touched;
    input.audio_assessment.reason_mask =
        audio::AudioMinimumReasonBankTreeDelta |
        audio::AudioMinimumReasonExactReleaseUnavailable |
        audio::AudioMinimumReasonBaselineReplayUnavailable;
    input.audio_assessment.bank_delta = true;
    input.player_absent_reset_failure =
        dluma::fixes::custom_map_asset_isolation::player_absent_reset::
            PlayerAbsentResetFailure::OwnerRouteUnvalidated;
    const report::PostDestroyPreflightReport result =
        report::BuildPostDestroyPreflightReport(input);
    CHECK(result.blocking_domain_mask == 131074U);
    CHECK(std::strcmp(
        result.first_blocker,
        "player_persistent_owner_release_path_unvalidated") == 0);
    CHECK((result.audio_missing_prerequisites &
           report::AudioMissingExactRelease) != 0U);
    CHECK((result.audio_missing_prerequisites &
           report::AudioMissingBaselineReplay) != 0U);
    CHECK((result.audio_missing_prerequisites &
           report::AudioMissingBankReleaseReplayPlan) != 0U);
    CHECK(!result.transaction_preparation_attempted);
    return true;
}

bool TestResolvedDomainsAuthorizePreparationMarker() noexcept
{
    report::PostDestroyPreflightInput input = {};
    input.player_assessment =
        player::PlayerGameplayParamsTouchAssessment::UntouchedAbsent;
    input.player_baseline_kind =
        player::PlayerStartupBaselineKind::Absent;
    input.audio_assessment.disposition =
        audio::AudioMinimumDisposition::Untouched;
    input.audio_assessment.hook_plan_complete = true;
    input.audio_assessment.event_fence_complete = true;
    report::PostDestroyPreflightReport result =
        report::BuildPostDestroyPreflightReport(input);
    CHECK(result.blocking_domain_mask == 0U);
    CHECK(result.blocker_count == 0U);
    CHECK(std::strcmp(result.first_blocker, "none") == 0);
    report::NotePostDestroyTransactionPreparationAttempted(&result);
    CHECK(result.transaction_preparation_attempted);
    return true;
}

bool TestRuntimeThirteenAudioTouchRemainsExactAndPremutation() noexcept
{
    report::PostDestroyPreflightInput input = {};
    input.player_assessment =
        player::PlayerGameplayParamsTouchAssessment::
            UntouchedSnapshot;
    input.player_baseline_kind =
        player::PlayerStartupBaselineKind::Snapshot;
    input.audio_assessment.disposition =
        audio::AudioMinimumDisposition::Touched;
    input.audio_assessment.reason_mask =
        audio::AudioMinimumReasonScriptEvidenceIncomplete |
        audio::AudioMinimumReasonBankEvidenceIncomplete |
        audio::AudioMinimumReasonSpeechEvidenceIncomplete |
        audio::AudioMinimumReasonFullPathAttributionUnavailable |
        audio::AudioMinimumReasonScriptRegistryDelta |
        audio::AudioMinimumReasonBankTreeDelta |
        audio::AudioMinimumReasonExactReleaseUnavailable |
        audio::AudioMinimumReasonBaselineReplayUnavailable;
    input.audio_assessment.script_delta = true;
    input.audio_assessment.bank_delta = true;
    input.audio_assessment.bank_reference_delta = false;
    input.audio_assessment.hook_plan_complete = true;
    input.audio_assessment.event_fence_complete = true;
    const report::PostDestroyPreflightReport result =
        report::BuildPostDestroyPreflightReport(input);
    CHECK(result.audio_assessment ==
          audio::AudioMinimumDisposition::Touched);
    CHECK(result.audio_reason_mask == 0x7F80U);
    CHECK(result.audio_missing_prerequisites == 0x7F8U);
    CHECK(result.audio_script_delta);
    CHECK(result.audio_bank_tree_delta);
    CHECK(!result.audio_bank_reference_delta);
    CHECK(result.blocking_domain_mask ==
          cold::DomainBit(cold::ContentDomainId::Audio));
    CHECK(result.blocker_count == 1U);
    CHECK(std::strcmp(
        result.first_blocker,
        "audio_touched_script_registry_and_bank_tree_reset_unavailable") ==
          0);
    CHECK(!result.player_reset_plan_prepared);
    CHECK(!result.audio_script_reload_plan_prepared);
    CHECK(!result.audio_bank_plan_prepared);
    CHECK(!result.transaction_preparation_attempted);
    return true;
}

bool TestRuntimeNineteenPreparedPlayerMakesAudioFirstBlocker() noexcept
{
    report::PostDestroyPreflightInput input = {};
    input.player_assessment =
        player::PlayerGameplayParamsTouchAssessment::
            TouchedDefinitionDelta;
    input.player_baseline_kind =
        player::PlayerStartupBaselineKind::Absent;
    input.player_natural_reset_phase =
        dluma::fixes::custom_map_asset_isolation::
            player_natural_reset::Phase::PlanPrepared;
    input.player_natural_reset_plan_prepared = true;
    input.player_reset_plan_prepared = true;
    input.audio_assessment.disposition =
        audio::AudioMinimumDisposition::Touched;
    input.audio_assessment.reason_mask =
        audio::AudioMinimumReasonScriptRegistryDelta;
    input.audio_assessment.script_delta = true;
    input.audio_assessment.hook_plan_complete = true;
    input.audio_assessment.event_fence_complete = true;
    input.audio_script_reload_failure =
        dluma::fixes::custom_map_asset_isolation::
            audio_script_reload::
                AudioScriptReloadFailure::ReceiverUnvalidated;
    const report::PostDestroyPreflightReport result =
        report::BuildPostDestroyPreflightReport(input);
    CHECK(result.player_natural_reset_plan_prepared);
    CHECK(result.player_natural_reset_phase ==
          dluma::fixes::custom_map_asset_isolation::
              player_natural_reset::Phase::PlanPrepared);
    CHECK(result.blocking_domain_mask ==
          cold::DomainBit(cold::ContentDomainId::Audio));
    CHECK(result.blocker_count == 1U);
    CHECK(std::strcmp(
        result.first_blocker,
        "audio_script_reload_receiver_abi_unvalidated") == 0);
    CHECK(!result.transaction_preparation_attempted);
    return true;
}

}  // namespace

int main()
{
    if (!TestRuntimeFourMaskDecodesBothDomains() ||
        !TestTouchedResetUnavailableIsExactAndPremutation() ||
        !TestResolvedDomainsAuthorizePreparationMarker() ||
        !TestRuntimeThirteenAudioTouchRemainsExactAndPremutation() ||
        !TestRuntimeNineteenPreparedPlayerMakesAudioFirstBlocker())
        return 1;
    std::puts("post destroy preflight report fake harness: PASS");
    return 0;
}
