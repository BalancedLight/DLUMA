#define _HAS_EXCEPTIONS 0

#include "../src/fixes/custom_map_asset_isolation/PlayerPresetRowPublicationTimeline.h"

#include <cstdio>
#include <cstring>

namespace publication =
    dluma::fixes::custom_map_asset_isolation::player_publication;

namespace {

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                        #condition);                                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

publication::CheckpointEvidence MakeEvidence(
    publication::Checkpoint checkpoint, std::uint64_t sequence,
    std::uint32_t populated_local_mask) noexcept
{
    static constexpr std::uint32_t runtime18_definition_counts[] = {
        497U, 49U, 46U, 168U, 6U, 748U, 46U, 13U};
    publication::CheckpointEvidence evidence = {};
    evidence.checkpoint = checkpoint;
    evidence.trace_sequence = sequence;
    evidence.qpc = 1000U + sequence;
    evidence.baseline_generation = 1U;
    evidence.inventory_revision = 7U;
    evidence.cache_generation = 9U;
    evidence.cache_fingerprint = 0xCAFEU;
    evidence.gameplay_container = 0x600000U;
    evidence.thread_id = 77U;
    evidence.supported_image = true;
    evidence.environment_readable = true;
    evidence.owner_set_stable = true;
    evidence.row_headers_stable = true;
    evidence.stock_route_validated = true;

    publication::OwnerEvidence& local = evidence.owners[0];
    local.owner_kind = publication::OwnerKind::LocalProcessGlobal;
    local.owner_slot = 0x710000U;
    local.player_state = 0x100000U;
    local.owner_generation = 1U;
    local.exact_owner_slot = true;
    local.generation_valid = true;
    local.live = true;
    local.readable = true;
    local.stable = true;

    publication::OwnerEvidence& character = evidence.owners[1];
    character.owner_kind =
        publication::OwnerKind::CharacterManagerGlobal;
    character.owner_slot = 0x710100U;
    character.player_state = 0x200000U;
    character.owner_generation = 2U;
    character.exact_owner_slot = true;
    character.generation_valid = true;
    character.live = true;
    character.readable = true;
    character.stable = true;

    for (std::uint32_t row = 0; row < publication::kRowCount; ++row) {
        publication::RowEvidence& local_row = local.rows[row];
        local_row.row_index = row;
        local_row.complete = true;
        local_row.cache_membership =
            publication::CacheMembership::ProvenNonMember;
        if ((populated_local_mask & (1U << row)) != 0U) {
            local_row.semantic_kind =
                publication::RowSemanticKind::PresetRow;
            local_row.preset = 0x300000U + row * 0x100U;
            local_row.definition_count =
                runtime18_definition_counts[row];
            local_row.definition_fingerprint = 0xA000U + row;
            local_row.cache_membership =
                publication::CacheMembership::ProvenMember;
        } else {
            local_row.semantic_kind =
                publication::RowSemanticKind::AbsentPresetRow;
        }

        publication::RowEvidence& character_row =
            character.rows[row];
        character_row.row_index = row;
        character_row.complete = true;
        character_row.semantic_kind =
            publication::RowSemanticKind::PresetRow;
        character_row.preset = 0x400000U + row * 0x100U;
        character_row.definition_count = 100U + row;
        character_row.definition_fingerprint = 0xB000U + row;
        character_row.cache_membership =
            publication::CacheMembership::ProvenMember;
    }
    return evidence;
}

bool CaseAbsentB0AndFirstPublicationDetected() noexcept
{
    publication::Timeline timeline = {};
    publication::Initialize(&timeline);
    const publication::CheckpointEvidence baseline = MakeEvidence(
        publication::Checkpoint::StartupB0Commit, 10U, 0U);
    CHECK(publication::RecordCheckpoint(&timeline, baseline));
    const publication::CheckpointEvidence first = MakeEvidence(
        publication::Checkpoint::FirstStableGameplayFrame, 20U, 1U);
    CHECK(publication::RecordCheckpoint(&timeline, first));
    const publication::FirstPublication publication =
        publication::FindFirstPublication(timeline);
    CHECK(publication.available);
    CHECK(publication.row == 0U);
    CHECK(publication.owner_kind ==
          publication::OwnerKind::LocalProcessGlobal);
    CHECK(publication.baseline_kind ==
          publication::RowSemanticKind::AbsentPresetRow);
    CHECK(publication.post_kind ==
          publication::RowSemanticKind::PresetRow);
    CHECK(publication.post_definition_count == 497U);
    return true;
}

bool CaseAllRowsChangedAndOtherOwnerStable() noexcept
{
    publication::Timeline timeline = {};
    publication::Initialize(&timeline);
    CHECK(publication::RecordCheckpoint(
        &timeline, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    CHECK(publication::RecordCheckpoint(
        &timeline, MakeEvidence(
            publication::Checkpoint::PostC10Pass1, 30U, 0xFFU)));
    CHECK(publication::RecordCheckpoint(
        &timeline, MakeEvidence(
            publication::Checkpoint::PostC10Pass2, 31U, 0xFFU)));
    const publication::DeltaSummary summary =
        publication::BuildDeltaSummary(timeline);
    CHECK(summary.available);
    CHECK(summary.first_changed_owner ==
          publication::OwnerKind::LocalProcessGlobal);
    CHECK(summary.first_changed_row == 0U);
    CHECK(summary.rows_changed_mask == 0xFFU);
    CHECK(summary.all_rows_changed_same_direction);
    CHECK(summary.other_owner_unchanged);
    return true;
}

bool CaseAttributionRequiresExactOrderingAndProvider() noexcept
{
    publication::Timeline stock = {};
    publication::Initialize(&stock);
    CHECK(publication::RecordCheckpoint(
        &stock, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    CHECK(publication::RecordCheckpoint(
        &stock, MakeEvidence(
            publication::Checkpoint::BeforeProjectPremountOriginal,
            20U, 1U)));
    publication::RecordProjectAdmissionBoundary(&stock, 21U, 3U);
    CHECK(publication::FindFirstPublication(stock).attribution ==
          publication::Attribution::StockBeforeProjectAdmission);

    publication::Timeline after = {};
    publication::Initialize(&after);
    CHECK(publication::RecordCheckpoint(
        &after, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordProjectAdmissionBoundary(&after, 20U, 3U);
    publication::CheckpointEvidence after_mount = MakeEvidence(
        publication::Checkpoint::UpdateDlcContentReturn, 30U, 1U);
    after_mount.content_epoch = 3U;
    CHECK(publication::RecordCheckpoint(&after, after_mount));
    CHECK(publication::FindFirstPublication(after).attribution !=
          publication::Attribution::StockBeforeProjectAdmission);

    publication::Timeline project = {};
    publication::Initialize(&project);
    CHECK(publication::RecordCheckpoint(
        &project, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordProjectAdmissionBoundary(&project, 20U, 3U);
    publication::CheckpointEvidence exact_project = MakeEvidence(
        publication::Checkpoint::ChangeAfterFadeReturn, 30U, 1U);
    exact_project.content_epoch = 3U;
    exact_project.transition_generation = 9U;
    exact_project.owners[0].rows[0].provider.source = 0x900000U;
    exact_project.owners[0].rows[0].provider.content_epoch = 3U;
    exact_project.owners[0].rows[0].provider.confidence =
        publication::ProviderConfidence::ExactProjectSource;
    std::strcpy(
        exact_project.owners[0].rows[0].provider.path,
        "X:\\workshop\\project\\data");
    CHECK(publication::RecordCheckpoint(&project, exact_project));
    CHECK(publication::FindFirstPublication(project).attribution ==
          publication::Attribution::ProjectSourceAttributed);

    publication::Timeline transition = {};
    publication::Initialize(&transition);
    CHECK(publication::RecordCheckpoint(
        &transition, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordProjectAdmissionBoundary(
        &transition, 20U, 3U);
    publication::ObserverEvent event = {};
    event.sequence = 25U;
    event.player_state = 0x100000U;
    event.owner_generation = 1U;
    event.content_epoch = 3U;
    event.row = 0U;
    event.owner_kind =
        publication::OwnerKind::LocalProcessGlobal;
    event.route =
        publication::ObserverRoute::SetVariablesToLevel;
    event.exact_route = true;
    CHECK(publication::RecordObserverEvent(
        &transition, event));
    publication::CheckpointEvidence transitioned = MakeEvidence(
        publication::Checkpoint::ChangeAfterFadeReturn, 30U, 1U);
    transitioned.content_epoch = 3U;
    CHECK(publication::RecordCheckpoint(
        &transition, transitioned));
    CHECK(publication::FindFirstPublication(transition).attribution ==
          publication::Attribution::ProjectTransitionAttributed);

    publication::Timeline cached = {};
    publication::Initialize(&cached);
    CHECK(publication::RecordCheckpoint(
        &cached, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordProjectAdmissionBoundary(&cached, 20U, 3U);
    publication::CachePublicationRecord cache = {};
    cache.sequence = 25U;
    cache.container = 0x600000U;
    cache.returned_preset = 0x300000U;
    cache.affected_player_state = 0x100000U;
    cache.affected_owner_generation = 1U;
    cache.affected_row = 0U;
    cache.affected_owner_kind =
        publication::OwnerKind::LocalProcessGlobal;
    cache.provider.source = 0x900000U;
    cache.provider.content_epoch = 3U;
    cache.provider.confidence =
        publication::ProviderConfidence::ExactProjectSource;
    cache.exact_route = true;
    cache.classification_complete = true;
    CHECK(publication::RecordCachePublication(&cached, cache));
    publication::CheckpointEvidence cached_post = MakeEvidence(
        publication::Checkpoint::ChangeAfterFadeReturn, 30U, 1U);
    cached_post.content_epoch = 3U;
    CHECK(publication::RecordCheckpoint(&cached, cached_post));
    CHECK(publication::FindFirstPublication(cached).attribution ==
          publication::Attribution::ProjectSourceAttributed);

    publication::Timeline unproven_stock = {};
    publication::Initialize(&unproven_stock);
    CHECK(publication::RecordCheckpoint(
        &unproven_stock, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::CheckpointEvidence unproven = MakeEvidence(
        publication::Checkpoint::BeforeProjectPremountOriginal,
        20U, 1U);
    unproven.stock_route_validated = false;
    CHECK(publication::RecordCheckpoint(
        &unproven_stock, unproven));
    publication::RecordProjectAdmissionBoundary(
        &unproven_stock, 21U, 3U);
    CHECK(publication::FindFirstPublication(
              unproven_stock).attribution ==
          publication::Attribution::Incomplete);
    return true;
}

bool CaseEqualCrossOwnerFingerprintsDoNotChangeOwnership() noexcept
{
    publication::Timeline timeline = {};
    publication::Initialize(&timeline);
    CHECK(publication::RecordCheckpoint(
        &timeline, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::CheckpointEvidence post = MakeEvidence(
        publication::Checkpoint::FirstStableGameplayFrame, 20U, 1U);
    post.owners[0].rows[0].definition_fingerprint =
        post.owners[1].rows[0].definition_fingerprint;
    CHECK(publication::RecordCheckpoint(&timeline, post));
    const publication::FirstPublication first =
        publication::FindFirstPublication(timeline);
    CHECK(first.available);
    CHECK(first.owner_kind ==
          publication::OwnerKind::LocalProcessGlobal);
    return true;
}

bool CaseUnstableGenerationAndDeadOwnerFailClosed() noexcept
{
    publication::Timeline unstable = {};
    publication::Initialize(&unstable);
    CHECK(publication::RecordCheckpoint(
        &unstable, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::CheckpointEvidence bad = MakeEvidence(
        publication::Checkpoint::FirstStableGameplayFrame, 20U, 1U);
    bad.row_headers_stable = false;
    bad.first_exact_failure =
        publication::ExactFailure::UnstableTwoPassSnapshot;
    CHECK(!publication::RecordCheckpoint(&unstable, bad));
    CHECK(!publication::FindFirstPublication(unstable).available);

    publication::Timeline generation = {};
    publication::Initialize(&generation);
    CHECK(publication::RecordCheckpoint(
        &generation, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::CheckpointEvidence replaced = MakeEvidence(
        publication::Checkpoint::FirstStableGameplayFrame, 20U, 1U);
    replaced.owners[0].owner_generation = 99U;
    CHECK(publication::RecordCheckpoint(&generation, replaced));
    CHECK(!publication::FindFirstPublication(generation).available);

    publication::Timeline destroyed = {};
    publication::Initialize(&destroyed);
    CHECK(publication::RecordCheckpoint(
        &destroyed, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::CheckpointEvidence dead = MakeEvidence(
        publication::Checkpoint::FirstStableGameplayFrame, 20U, 1U);
    dead.owners[0].live = false;
    dead.owners[0].readable = false;
    dead.owners[0].first_exact_failure =
        publication::ExactFailure::OwnerDestroyed;
    CHECK(!publication::RecordCheckpoint(&destroyed, dead));
    CHECK(!publication::FindFirstPublication(destroyed).available);
    return true;
}

bool CaseOptionalCapacityLossPreservesCriticalRecords() noexcept
{
    publication::Timeline timeline = {};
    publication::Initialize(&timeline);
    publication::CachePublicationRecord cache = {};
    cache.sequence = 1U;
    cache.exact_route = true;
    for (std::size_t index = 0;
         index < publication::kCachePublicationCapacity; ++index) {
        cache.sequence = index + 1U;
        CHECK(publication::RecordCachePublication(&timeline, cache));
    }
    cache.sequence = 1000U;
    CHECK(!publication::RecordCachePublication(&timeline, cache));
    CHECK(timeline.loss.cache_publication_overflow == 1U);

    publication::ObserverEvent event = {};
    event.exact_route = true;
    for (std::size_t index = 0;
         index < publication::kObserverEventCapacity; ++index) {
        event.sequence = index + 1U;
        CHECK(publication::RecordObserverEvent(&timeline, event));
    }
    event.sequence = 1000U;
    CHECK(!publication::RecordObserverEvent(&timeline, event));
    CHECK(timeline.loss.observer_event_overflow == 1U);

    CHECK(publication::RecordCheckpoint(
        &timeline, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 2000U, 0U)));
    CHECK(publication::HasCheckpoint(
        timeline, publication::Checkpoint::StartupB0Commit));
    publication::RecordCriticalContention(&timeline);
    CHECK(timeline.loss.critical_lock_contention == 1U);
    CHECK(publication::HasCheckpoint(
        timeline, publication::Checkpoint::StartupB0Commit));
    return true;
}

bool CaseStableGameplayCheckpointHasOneExactOutcome() noexcept
{
    publication::Timeline unavailable = {};
    publication::Initialize(&unavailable);
    CHECK(publication::RecordCheckpoint(
        &unavailable, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordFirstStableGameplayAttempt(
        &unavailable,
        publication::ExactFailure::LocalPlayerDiUnavailable,
        0U);
    publication::FinalizeFirstStableGameplayUnavailable(
        &unavailable, publication::ExactFailure::None);
    const publication::CheckpointRecord& missing =
        unavailable.checkpoints[static_cast<std::size_t>(
            publication::Checkpoint::FirstStableGameplayFrame)];
    CHECK(!missing.captured);
    CHECK(!missing.complete);
    CHECK(missing.evidence.trace_sequence == 0U);
    CHECK(missing.evidence.owners[0].player_state == 0U);
    CHECK(unavailable.first_stable_gameplay.attempt_count == 1U);
    CHECK(unavailable.first_stable_gameplay.first_failure ==
          publication::ExactFailure::LocalPlayerDiUnavailable);
    CHECK(unavailable.first_stable_gameplay.last_failure ==
          publication::ExactFailure::LocalPlayerDiUnavailable);
    CHECK(unavailable.first_stable_gameplay.terminal);

    publication::Timeline captured = {};
    publication::Initialize(&captured);
    CHECK(publication::RecordCheckpoint(
        &captured, MakeEvidence(
            publication::Checkpoint::StartupB0Commit, 10U, 0U)));
    publication::RecordFirstStableGameplayAttempt(
        &captured, publication::ExactFailure::None, 20U);
    CHECK(publication::RecordCheckpoint(
        &captured, MakeEvidence(
            publication::Checkpoint::FirstStableGameplayFrame,
            20U, 0xFFU)));
    CHECK(captured.first_stable_gameplay.attempt_count == 1U);
    CHECK(captured.first_stable_gameplay.eligible_sequence == 20U);
    CHECK(captured.first_stable_gameplay.capture_sequence == 20U);
    CHECK(captured.first_stable_gameplay.terminal);
    CHECK(!publication::RecordCheckpoint(
        &captured, MakeEvidence(
            publication::Checkpoint::FirstStableGameplayFrame,
            21U, 0xFFU)));
    return true;
}

bool CaseAllEightRowPublicationsRetainExactEvidence() noexcept
{
    publication::Timeline timeline = {};
    publication::Initialize(&timeline);
    for (std::uint32_t row = 0;
         row < publication::kRowCount; ++row) {
        publication::ObserverEvent event = {};
        event.sequence = 100U + row;
        event.route =
            publication::ObserverRoute::RowPublicationStore;
        event.subphase =
            publication::WriterSubphase::RowPublicationStore;
        event.player_state = 0x100000U;
        event.owner_slot = 0x710000U;
        event.owner_generation = 1U;
        event.row = row;
        event.preset = 0x300000U + row * 0x100U;
        event.newly_inserted = row == 0U;
        event.reused = row != 0U;
        event.provider_source_handle = 0x900000U;
        event.provider_source_ordinal = 4U;
        event.provider_confidence =
            publication::ProviderConfidence::
                ExactProjectSource;
        event.exact_route = true;
        CHECK(publication::RecordObserverEvent(
            &timeline, event));
    }
    CHECK(timeline.observer_event_count ==
          publication::kRowCount);
    for (std::uint32_t row = 0;
         row < publication::kRowCount; ++row) {
        CHECK(timeline.observer_events[row].row == row);
        CHECK(timeline.observer_events[row].preset != 0U);
        CHECK(
            timeline.observer_events[row].provider_confidence ==
            publication::ProviderConfidence::
                ExactProjectSource);
    }
    return true;
}

}  // namespace

int main()
{
    const bool passed =
        CaseAbsentB0AndFirstPublicationDetected() &&
        CaseAllRowsChangedAndOtherOwnerStable() &&
        CaseAttributionRequiresExactOrderingAndProvider() &&
        CaseEqualCrossOwnerFingerprintsDoNotChangeOwnership() &&
        CaseUnstableGenerationAndDeadOwnerFailClosed() &&
        CaseOptionalCapacityLossPreservesCriticalRecords() &&
        CaseStableGameplayCheckpointHasOneExactOutcome() &&
        CaseAllEightRowPublicationsRetainExactEvidence();
    if (!passed) return 1;
    std::puts(
        "player preset row publication timeline fake harness passed");
    return 0;
}
