#define _HAS_EXCEPTIONS 0

#include "PersistentConsumerLedger.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::persistent {
namespace {

std::size_t DomainIndex(PersistentConsumerDomain domain) noexcept
{
    const std::size_t index = static_cast<std::size_t>(domain);
    const std::size_t count =
        static_cast<std::size_t>(PersistentConsumerDomain::Count);
    return index < count ? index : 0;
}

bool TextEquals(const char* left, const char* right) noexcept
{
    if (!left || !right) return left == right;
    for (std::size_t index = 0; index < kPersistentPathCapacity;
         ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == '\0') return true;
    }
    return true;
}

void RecordLoss(PersistentConsumerLedger* ledger,
                PersistentConsumerDomain domain,
                std::uint64_t sequence) noexcept
{
    if (!ledger) return;
    PersistentDomainLoss& loss = ledger->losses[DomainIndex(domain)];
    dluma_atomic_increment(reinterpret_cast<volatile LONG*>(&loss.count));
    if (!loss.first_sequence) loss.first_sequence = sequence;
    if (sequence > loss.last_sequence) loss.last_sequence = sequence;
}

void RecordOptionalLoss(OptionalObserverLoss* loss,
                        std::uint64_t sequence) noexcept
{
    if (!loss) return;
    dluma_atomic_increment(reinterpret_cast<volatile LONG*>(&loss->count));
    if (!loss->first_sequence) loss->first_sequence = sequence;
    if (sequence > loss->last_sequence) loss->last_sequence = sequence;
}

bool AcquireOptionalLock(volatile LONG* lock,
                         OptionalObserverLoss* loss,
                         std::uint64_t sequence) noexcept
{
    if (dluma_atomic_compare_exchange(lock, 1, 0) == 0) return true;
    RecordOptionalLoss(loss, sequence);
    return false;
}

bool SameIdentity(const PersistentConsumerObservation& left,
                  const PersistentConsumerObservation& right) noexcept
{
    const bool definition_generation_equal =
        (left.domain != PersistentConsumerDomain::DefaultLevels &&
         left.domain != PersistentConsumerDomain::GameplayParameters &&
         left.domain != PersistentConsumerDomain::EffectiveFov) ||
        left.object_generation == right.object_generation;
    return left.domain == right.domain && left.epoch == right.epoch &&
           left.source_record_index == right.source_record_index &&
           left.consumer_rva == right.consumer_rva &&
           left.consumer == right.consumer &&
           left.subject_id == right.subject_id &&
           definition_generation_equal &&
           TextEquals(left.logical_path, right.logical_path);
}

bool FindLatestLocked(PersistentConsumerLedger* ledger,
                      PersistentConsumerDomain domain,
                      bool filter_epoch, std::uint32_t epoch,
                      const char* logical_path,
                      PersistentConsumerObservation* observation) noexcept
{
    bool found = false;
    std::uint64_t sequence = 0;
    for (std::uint32_t index = 0; index < ledger->count; ++index) {
        const PersistentConsumerObservation& candidate =
            ledger->records[index];
        if (candidate.domain != domain ||
            (filter_epoch && candidate.epoch != epoch) ||
            (logical_path &&
             !TextEquals(candidate.logical_path, logical_path)) ||
            candidate.sequence < sequence)
            continue;
        *observation = candidate;
        sequence = candidate.sequence;
        found = true;
    }
    return found;
}

}  // namespace

void Initialize(PersistentConsumerLedger* ledger) noexcept
{
    if (ledger) std::memset(ledger, 0, sizeof(*ledger));
}

bool Record(PersistentConsumerLedger* ledger,
            const PersistentConsumerObservation& observation) noexcept
{
    if (!ledger) return false;
    if (dluma_atomic_compare_exchange(&ledger->lock, 1, 0) != 0) {
        RecordLoss(ledger, observation.domain, observation.sequence);
        return false;
    }
    for (std::uint32_t index = 0; index < ledger->count; ++index) {
        PersistentConsumerObservation& existing = ledger->records[index];
        if (SameIdentity(existing, observation)) {
            existing = observation;
            dluma_atomic_exchange(&ledger->lock, 0);
            return true;
        }
    }
    if (ledger->count >= kPersistentConsumerCapacity) {
        RecordLoss(ledger, observation.domain, observation.sequence);
        dluma_atomic_exchange(&ledger->lock, 0);
        return false;
    }
    ledger->records[ledger->count++] = observation;
    dluma_atomic_exchange(&ledger->lock, 0);
    return true;
}

bool CaptureSnapshot(PersistentConsumerLedger* ledger,
                     PersistentConsumerSnapshot* snapshot) noexcept
{
    if (!ledger || !snapshot ||
        dluma_atomic_compare_exchange(&ledger->lock, 1, 0) != 0)
        return false;
    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->count = ledger->count;
    std::memcpy(snapshot->records, ledger->records,
                sizeof(ledger->records));
    std::memcpy(snapshot->losses, ledger->losses,
                sizeof(ledger->losses));
    dluma_atomic_exchange(&ledger->lock, 0);
    if (!AcquireOptionalLock(&ledger->staged_assets_lock,
                             &ledger->staged_assets_loss, 0))
        return false;
    snapshot->staged_assets_source_count =
        ledger->staged_assets_source_count;
    snapshot->post_supersession_open_count =
        ledger->post_supersession_open_count;
    std::memcpy(snapshot->staged_assets_sources,
                ledger->staged_assets_sources,
                sizeof(ledger->staged_assets_sources));
    std::memcpy(snapshot->post_supersession_opens,
                ledger->post_supersession_opens,
                sizeof(ledger->post_supersession_opens));
    snapshot->staged_assets_loss = ledger->staged_assets_loss;
    dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
    if (!AcquireOptionalLock(&ledger->material_lock,
                             &ledger->material_loss, 0))
        return false;
    snapshot->material_reload_count = ledger->material_reload_count;
    snapshot->material_identity_count = ledger->material_identity_count;
    std::memcpy(snapshot->material_reloads, ledger->material_reloads,
                sizeof(ledger->material_reloads));
    std::memcpy(snapshot->material_identities,
                ledger->material_identities,
                sizeof(ledger->material_identities));
    snapshot->material_loss = ledger->material_loss;
    dluma_atomic_exchange(&ledger->material_lock, 0);
    if (!AcquireOptionalLock(&ledger->player_lock,
                             &ledger->player_progression_loss, 0))
        return false;
    snapshot->progression_overlay_count =
        ledger->progression_overlay_count;
    snapshot->player_state_snapshot_count =
        ledger->player_state_snapshot_count;
    std::memcpy(snapshot->progression_overlays,
                ledger->progression_overlays,
                sizeof(ledger->progression_overlays));
    std::memcpy(snapshot->player_state_snapshots,
                ledger->player_state_snapshots,
                sizeof(ledger->player_state_snapshots));
    snapshot->player_progression_loss =
        ledger->player_progression_loss;
    dluma_atomic_exchange(&ledger->player_lock, 0);
    return true;
}

bool FindLatest(PersistentConsumerLedger* ledger,
                PersistentConsumerDomain domain, std::uint32_t epoch,
                PersistentConsumerObservation* observation) noexcept
{
    if (!ledger || !observation ||
        dluma_atomic_compare_exchange(&ledger->lock, 1, 0) != 0)
        return false;
    const bool found = FindLatestLocked(
        ledger, domain, true, epoch, nullptr, observation);
    dluma_atomic_exchange(&ledger->lock, 0);
    return found;
}

bool FindLatestAny(PersistentConsumerLedger* ledger,
                   PersistentConsumerDomain domain,
                   PersistentConsumerObservation* observation) noexcept
{
    if (!ledger || !observation ||
        dluma_atomic_compare_exchange(&ledger->lock, 1, 0) != 0)
        return false;
    const bool found = FindLatestLocked(
        ledger, domain, false, 0, nullptr, observation);
    dluma_atomic_exchange(&ledger->lock, 0);
    return found;
}

bool FindLatestByPath(PersistentConsumerLedger* ledger,
                      PersistentConsumerDomain domain,
                      const char* logical_path,
                      PersistentConsumerObservation* observation) noexcept
{
    if (!ledger || !logical_path || !observation ||
        dluma_atomic_compare_exchange(&ledger->lock, 1, 0) != 0)
        return false;
    const bool found = FindLatestLocked(
        ledger, domain, false, 0, logical_path, observation);
    dluma_atomic_exchange(&ledger->lock, 0);
    return found;
}

const char* DomainName(PersistentConsumerDomain domain) noexcept
{
    switch (domain) {
    case PersistentConsumerDomain::DefaultLevels: return "default_levels";
    case PersistentConsumerDomain::EffectiveFov: return "effective_fov";
    case PersistentConsumerDomain::GameplayParameters:
        return "gameplay_parameters";
    case PersistentConsumerDomain::ScriptDeclaration:
        return "script_declaration";
    case PersistentConsumerDomain::AudioData: return "audio_data";
    case PersistentConsumerDomain::AudioBank: return "audio_bank";
    case PersistentConsumerDomain::MusicPreset: return "music_preset";
    case PersistentConsumerDomain::UiDeclaration: return "ui_declaration";
    case PersistentConsumerDomain::AssetsPcFile: return "assets_pc_file";
    case PersistentConsumerDomain::AssetsPcResource:
        return "assets_pc_resource";
    default: return "unknown";
    }
}

const char* ResultName(PersistentConsumerResult result) noexcept
{
    switch (result) {
    case PersistentConsumerResult::Observed: return "observed";
    case PersistentConsumerResult::OpenFailed: return "open_failed";
    case PersistentConsumerResult::Parsed: return "parsed";
    case PersistentConsumerResult::CacheHit: return "cache_hit";
    case PersistentConsumerResult::RegistrationConflict:
        return "registration_conflict";
    case PersistentConsumerResult::LoadSucceeded: return "load_succeeded";
    case PersistentConsumerResult::LoadFailed: return "load_failed";
    case PersistentConsumerResult::NaturalUnload: return "natural_unload";
    case PersistentConsumerResult::NaturalReset: return "natural_reset";
    default: return "unknown";
    }
}

const char* DispositionName(
    PersistentConsumerDisposition disposition) noexcept
{
    switch (disposition) {
    case PersistentConsumerDisposition::SourceRemovedFutureProviderRestored:
        return "source_removed_future_provider_restored";
    case PersistentConsumerDisposition::PersistentOwnerNaturallyReset:
        return "persistent_owner_naturally_reset";
    case PersistentConsumerDisposition::PersistentOwnerRetained:
        return "persistent_owner_retained";
    case PersistentConsumerDisposition::LiveValueStale:
        return "live_value_stale";
    default: return "unresolved";
    }
}

DefaultLevelsOutcome ClassifyDefaultLevelsOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool custom_source_reset_completed) noexcept
{
    const PersistentConsumerObservation* baseline_definition = nullptr;
    const PersistentConsumerObservation* custom_definition = nullptr;
    const PersistentConsumerObservation* restored_definition = nullptr;
    const PersistentConsumerObservation* baseline_fov = nullptr;
    const PersistentConsumerObservation* restored_fov = nullptr;
    for (std::uint32_t index = 0; index < snapshot.count; ++index) {
        const PersistentConsumerObservation& item = snapshot.records[index];
        if (!item.observation_complete) continue;
        if (item.domain == PersistentConsumerDomain::DefaultLevels &&
            item.definition_hash_after) {
            if (item.epoch == 0 && !baseline_definition)
                baseline_definition = &item;
            if (item.epoch != 0 &&
                (!custom_definition ||
                 item.sequence > custom_definition->sequence))
                custom_definition = &item;
        }
        if (item.domain == PersistentConsumerDomain::EffectiveFov &&
            item.subject_id == 779U && item.epoch == 0 &&
            (!baseline_fov || item.sequence < baseline_fov->sequence))
            baseline_fov = &item;
    }
    if (!baseline_definition || !custom_definition)
        return DefaultLevelsOutcome::ObservationIncomplete;
    for (std::uint32_t index = 0; index < snapshot.count; ++index) {
        const PersistentConsumerObservation& item = snapshot.records[index];
        if (!item.observation_complete || item.epoch != 0 ||
            item.sequence <= custom_definition->sequence)
            continue;
        if (item.domain == PersistentConsumerDomain::DefaultLevels &&
            item.definition_hash_after &&
            (!restored_definition ||
             item.sequence > restored_definition->sequence))
            restored_definition = &item;
    }
    if (!restored_definition)
        return custom_source_reset_completed
            ? DefaultLevelsOutcome::ProviderRestoredButManagerNotReloaded
            : DefaultLevelsOutcome::ObservationIncomplete;
    for (std::uint32_t index = 0; index < snapshot.count; ++index) {
        const PersistentConsumerObservation& item = snapshot.records[index];
        if (!item.observation_complete || item.epoch != 0 ||
            item.domain != PersistentConsumerDomain::EffectiveFov ||
            item.subject_id != 779U ||
            item.sequence <= restored_definition->sequence)
            continue;
        if (!restored_fov || item.sequence > restored_fov->sequence)
            restored_fov = &item;
    }
    if (restored_definition->definition_hash_after !=
        baseline_definition->definition_hash_after)
        return DefaultLevelsOutcome::DefinitionPersisted;
    if (!baseline_fov || !restored_fov)
        return DefaultLevelsOutcome::ObservationIncomplete;
    return restored_fov->value1 == baseline_fov->value1
        ? DefaultLevelsOutcome::NaturallyRestored
        : DefaultLevelsOutcome::DefinitionRestoredLiveValuePersisted;
}

const char* DefaultLevelsOutcomeName(DefaultLevelsOutcome outcome) noexcept
{
    switch (outcome) {
    case DefaultLevelsOutcome::NaturallyRestored:
        return "default_levels_naturally_restored";
    case DefaultLevelsOutcome::DefinitionPersisted:
        return "default_levels_definition_persisted";
    case DefaultLevelsOutcome::DefinitionRestoredLiveValuePersisted:
        return "default_levels_definition_restored_live_value_persisted";
    case DefaultLevelsOutcome::ProviderRestoredButManagerNotReloaded:
        return "default_levels_provider_restored_but_manager_not_reloaded";
    default:
        return "default_levels_observation_incomplete";
    }
}

bool RecordStagedAssetsSource(
    PersistentConsumerLedger* ledger,
    const StagedAssetsSourceObservation& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->staged_assets_lock, &ledger->staged_assets_loss,
            observation.sequence))
        return false;
    for (std::uint32_t index = 0;
         index < ledger->staged_assets_source_count; ++index) {
        StagedAssetsSourceObservation& existing =
            ledger->staged_assets_sources[index];
        if (existing.epoch == observation.epoch &&
            existing.source_record_index == observation.source_record_index) {
            existing = observation;
            dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
            return true;
        }
    }
    if (ledger->staged_assets_source_count >=
        kStagedAssetsSourceCapacity) {
        RecordOptionalLoss(&ledger->staged_assets_loss,
                           observation.sequence);
        dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
        return false;
    }
    ledger->staged_assets_sources[
        ledger->staged_assets_source_count++] = observation;
    dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
    return true;
}

bool RecordPostSupersessionOpen(
    PersistentConsumerLedger* ledger,
    const PostSupersessionAssetsOpen& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->staged_assets_lock, &ledger->staged_assets_loss,
            observation.sequence))
        return false;
    if (ledger->post_supersession_open_count >=
        kPostSupersessionOpenCapacity) {
        RecordOptionalLoss(&ledger->staged_assets_loss,
                           observation.sequence);
        dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
        return false;
    }
    ledger->post_supersession_opens[
        ledger->post_supersession_open_count++] = observation;
    dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
    return true;
}

bool MarkStagedAssetsEpochCurrent(
    PersistentConsumerLedger* ledger, std::uint32_t current_epoch,
    std::uint64_t sequence) noexcept
{
    if (!ledger || !current_epoch || !AcquireOptionalLock(
            &ledger->staged_assets_lock, &ledger->staged_assets_loss,
            sequence))
        return false;
    for (std::uint32_t index = 0;
         index < ledger->staged_assets_source_count; ++index) {
        StagedAssetsSourceObservation& source =
            ledger->staged_assets_sources[index];
        source.current_epoch = current_epoch;
        if (source.source_active)
            source.state = source.epoch == current_epoch
                ? StagedAssetsSourceState::ActiveCurrent
                : StagedAssetsSourceState::ActiveSuperseded;
        source.sequence = sequence;
    }
    dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
    return true;
}

bool MarkStagedAssetsSourceRemoved(
    PersistentConsumerLedger* ledger, std::uintptr_t source,
    std::uint32_t current_epoch, std::uint64_t sequence) noexcept
{
    if (!ledger || !source || !AcquireOptionalLock(
            &ledger->staged_assets_lock, &ledger->staged_assets_loss,
            sequence))
        return false;
    bool found = false;
    for (std::uint32_t index = 0;
         index < ledger->staged_assets_source_count; ++index) {
        StagedAssetsSourceObservation& item =
            ledger->staged_assets_sources[index];
        if (item.source != source) continue;
        item.sequence = sequence;
        item.current_epoch = current_epoch;
        item.source_active = false;
        item.state = StagedAssetsSourceState::Removed;
        found = true;
    }
    dluma_atomic_exchange(&ledger->staged_assets_lock, 0);
    return found;
}

bool RecordMaterialReload(
    PersistentConsumerLedger* ledger,
    const MaterialReloadObservation& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->material_lock, &ledger->material_loss,
            observation.sequence))
        return false;
    for (std::uint32_t index = 0;
         index < ledger->material_reload_count; ++index) {
        MaterialReloadObservation& existing =
            ledger->material_reloads[index];
        if (existing.observation_generation ==
            observation.observation_generation) {
            existing = observation;
            dluma_atomic_exchange(&ledger->material_lock, 0);
            return true;
        }
    }
    if (ledger->material_reload_count >= kMaterialReloadCapacity) {
        RecordOptionalLoss(&ledger->material_loss, observation.sequence);
        dluma_atomic_exchange(&ledger->material_lock, 0);
        return false;
    }
    ledger->material_reloads[ledger->material_reload_count++] = observation;
    dluma_atomic_exchange(&ledger->material_lock, 0);
    return true;
}

bool RecordMaterialIdentity(
    PersistentConsumerLedger* ledger,
    const MaterialResourceIdentityObservation& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->material_lock, &ledger->material_loss,
            observation.sequence))
        return false;
    for (std::uint32_t index = 0;
         index < ledger->material_identity_count; ++index) {
        MaterialResourceIdentityObservation& existing =
            ledger->material_identities[index];
        if (existing.logical_key == observation.logical_key &&
            existing.registration_generation ==
                observation.registration_generation) {
            existing = observation;
            dluma_atomic_exchange(&ledger->material_lock, 0);
            return true;
        }
    }
    if (ledger->material_identity_count >= kMaterialIdentityCapacity) {
        RecordOptionalLoss(&ledger->material_loss, observation.sequence);
        dluma_atomic_exchange(&ledger->material_lock, 0);
        return false;
    }
    ledger->material_identities[
        ledger->material_identity_count++] = observation;
    dluma_atomic_exchange(&ledger->material_lock, 0);
    return true;
}

bool RecordProgressionOverlay(
    PersistentConsumerLedger* ledger,
    const ProgressionOverlayObservation& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->player_lock, &ledger->player_progression_loss,
            observation.sequence))
        return false;
    if (ledger->progression_overlay_count >=
        kProgressionOverlayCapacity) {
        RecordOptionalLoss(&ledger->player_progression_loss,
                           observation.sequence);
        dluma_atomic_exchange(&ledger->player_lock, 0);
        return false;
    }
    ledger->progression_overlays[
        ledger->progression_overlay_count++] = observation;
    dluma_atomic_exchange(&ledger->player_lock, 0);
    return true;
}

bool RecordPlayerStateSnapshot(
    PersistentConsumerLedger* ledger,
    const PlayerStateSnapshot& observation) noexcept
{
    if (!ledger || !AcquireOptionalLock(
            &ledger->player_lock, &ledger->player_progression_loss,
            observation.sequence))
        return false;
    for (std::uint32_t index = 0;
         index < ledger->player_state_snapshot_count; ++index) {
        PlayerStateSnapshot& existing =
            ledger->player_state_snapshots[index];
        if (existing.player_state == observation.player_state &&
            existing.template_generation ==
                observation.template_generation) {
            existing = observation;
            dluma_atomic_exchange(&ledger->player_lock, 0);
            return true;
        }
    }
    if (ledger->player_state_snapshot_count >=
        kPlayerStateSnapshotCapacity) {
        RecordOptionalLoss(&ledger->player_progression_loss,
                           observation.sequence);
        dluma_atomic_exchange(&ledger->player_lock, 0);
        return false;
    }
    ledger->player_state_snapshots[
        ledger->player_state_snapshot_count++] = observation;
    dluma_atomic_exchange(&ledger->player_lock, 0);
    return true;
}

MaterialObservationOutcome ClassifyMaterialOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool material_observer_installed) noexcept
{
    if (!material_observer_installed || snapshot.material_loss.count)
        return MaterialObservationOutcome::ObservationIncomplete;
    if (snapshot.post_supersession_open_count)
        return MaterialObservationOutcome::
            SupersededSourceServedCurrentProject;
    for (std::uint32_t index = 0;
         index < snapshot.material_identity_count; ++index) {
        const MaterialResourceIdentityObservation& identity =
            snapshot.material_identities[index];
        if (identity.action == MaterialIdentityAction::CacheHit)
            return identity.file_open_observed
                ? MaterialObservationOutcome::
                      PostopenResourceCachePersisted
                : MaterialObservationOutcome::
                      PreopenResourceCachePersisted;
        if (identity.action == MaterialIdentityAction::ReboundInPlace &&
            identity.prior_object &&
            identity.provider_epoch == 0)
            return MaterialObservationOutcome::
                DownstreamConsumerPersisted;
    }
    for (std::uint32_t index = 0;
         index < snapshot.material_reload_count; ++index) {
        const MaterialReloadObservation& reload =
            snapshot.material_reloads[index];
        if (reload.scope_complete && reload.old_local_pack &&
            reload.new_local_pack &&
            reload.old_local_pack != reload.new_local_pack &&
            reload.manager_pointer_swapped &&
            reload.old_pack_destructor_entered &&
            reload.old_pack_destructor_completed)
            return MaterialObservationOutcome::NaturallyReplaced;
    }
    return MaterialObservationOutcome::ObservationIncomplete;
}

PlayerProgressionOutcome ClassifyPlayerProgressionOutcome(
    const PersistentConsumerSnapshot& snapshot,
    bool player_observer_installed,
    bool custom_source_reset_completed) noexcept
{
    if (!player_observer_installed ||
        snapshot.player_progression_loss.count)
        return PlayerProgressionOutcome::ObservationIncomplete;
    const ProgressionOverlayObservation* baseline = nullptr;
    const ProgressionOverlayObservation* custom = nullptr;
    const ProgressionOverlayObservation* restored = nullptr;
    for (std::uint32_t index = 0;
         index < snapshot.progression_overlay_count; ++index) {
        const ProgressionOverlayObservation& item =
            snapshot.progression_overlays[index];
        if (!item.observation_complete) continue;
        if (item.epoch == 0 && item.stage == ProgressionOverlayStage::Base &&
            !baseline)
            baseline = &item;
        if (item.epoch != 0 &&
            (!custom || item.sequence > custom->sequence))
            custom = &item;
    }
    if (!baseline || !custom)
        return PlayerProgressionOutcome::ObservationIncomplete;
    for (std::uint32_t index = 0;
         index < snapshot.progression_overlay_count; ++index) {
        const ProgressionOverlayObservation& item =
            snapshot.progression_overlays[index];
        if (!item.observation_complete || item.epoch != 0 ||
            item.stage != ProgressionOverlayStage::Base ||
            item.sequence <= custom->sequence)
            continue;
        if (!restored || item.sequence > restored->sequence)
            restored = &item;
    }
    if (!restored)
        return custom_source_reset_completed
            ? PlayerProgressionOutcome::ProviderRestoredManagerStale
            : PlayerProgressionOutcome::ObservationIncomplete;
    if (restored->definition_hash_after !=
        baseline->definition_hash_after)
        return PlayerProgressionOutcome::DefinitionPersisted;
    for (std::uint32_t index = 0;
         index < snapshot.player_state_snapshot_count; ++index) {
        const PlayerStateSnapshot& state =
            snapshot.player_state_snapshots[index];
        if (state.sequence <= restored->sequence) continue;
        if (!state.effective_fov_available)
            return PlayerProgressionOutcome::ObservationIncomplete;
        return state.observation_complete
            ? PlayerProgressionOutcome::NaturallyRestored
            : PlayerProgressionOutcome::
                DefinitionRestoredLiveStatePersisted;
    }
    return PlayerProgressionOutcome::ObservationIncomplete;
}

const char* StagedAssetsSourceStateName(
    StagedAssetsSourceState state) noexcept
{
    switch (state) {
    case StagedAssetsSourceState::ActiveCurrent:
        return "active_current";
    case StagedAssetsSourceState::ActiveSuperseded:
        return "active_superseded";
    case StagedAssetsSourceState::Removed: return "removed";
    default: return "unknown";
    }
}

const char* MaterialIdentityActionName(
    MaterialIdentityAction action) noexcept
{
    switch (action) {
    case MaterialIdentityAction::Constructed: return "new_construction";
    case MaterialIdentityAction::CacheHit: return "cache_hit";
    case MaterialIdentityAction::Replaced: return "replacement";
    case MaterialIdentityAction::ReboundInPlace: return "in_place_rebind";
    case MaterialIdentityAction::Retired: return "old_object_retired";
    default: return "observed";
    }
}

const char* MaterialOutcomeName(
    MaterialObservationOutcome outcome) noexcept
{
    switch (outcome) {
    case MaterialObservationOutcome::NaturallyReplaced:
        return "local_material_pack_naturally_replaced";
    case MaterialObservationOutcome::SupersededSourceServedCurrentProject:
        return "superseded_assets_pc_source_served_current_project";
    case MaterialObservationOutcome::PreopenResourceCachePersisted:
        return "assets_pc_preopen_resource_cache_persisted";
    case MaterialObservationOutcome::PostopenResourceCachePersisted:
        return "assets_pc_postopen_resource_cache_persisted";
    case MaterialObservationOutcome::DownstreamConsumerPersisted:
        return "assets_pc_downstream_consumer_persisted";
    default: return "assets_pc_material_observation_incomplete";
    }
}

const char* ProgressionOverlayStageName(
    ProgressionOverlayStage stage) noexcept
{
    switch (stage) {
    case ProgressionOverlayStage::Base: return "base";
    case ProgressionOverlayStage::Progression: return "progression";
    case ProgressionOverlayStage::ClassProgression:
        return "class_progression";
    default: return "unknown";
    }
}

const char* PlayerProgressionOutcomeName(
    PlayerProgressionOutcome outcome) noexcept
{
    switch (outcome) {
    case PlayerProgressionOutcome::NaturallyRestored:
        return "player_progression_naturally_restored";
    case PlayerProgressionOutcome::DefinitionPersisted:
        return "player_progression_definition_persisted";
    case PlayerProgressionOutcome::DefinitionRestoredLiveStatePersisted:
        return "player_progression_live_state_persisted";
    case PlayerProgressionOutcome::ProviderRestoredManagerStale:
        return "player_progression_provider_restored_manager_stale";
    default: return "player_progression_observation_incomplete";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::persistent
