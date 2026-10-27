#include "PlayerPresetRowPublicationTimeline.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::player_publication {
namespace {

constexpr std::size_t ToIndex(Checkpoint checkpoint) noexcept
{
    return static_cast<std::size_t>(checkpoint);
}

void CopyText(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    output[0] = 0;
    if (!input) return;
    std::size_t index = 0;
    while (index + 1U < capacity && input[index]) {
        output[index] = input[index];
        ++index;
    }
    output[index] = 0;
}

void SaturatingIncrement(std::uint32_t* value) noexcept
{
    if (value && *value != UINT32_MAX) ++*value;
}

bool OwnerComplete(const OwnerEvidence& owner) noexcept
{
    if (owner.owner_kind != OwnerKind::LocalProcessGlobal &&
        owner.owner_kind != OwnerKind::CharacterManagerGlobal)
        return false;
    if (!owner.owner_slot || !owner.player_state ||
        !owner.owner_generation || !owner.exact_owner_slot ||
        !owner.generation_valid || !owner.live || !owner.readable ||
        !owner.stable ||
        owner.first_exact_failure != ExactFailure::None)
        return false;
    for (std::size_t row = 0; row < kRowCount; ++row) {
        const RowEvidence& item = owner.rows[row];
        if (item.row_index != row || !item.complete ||
            item.first_exact_failure != ExactFailure::None ||
            item.semantic_kind == RowSemanticKind::Incomplete ||
            item.cache_membership == CacheMembership::Conflict)
            return false;
        if (item.semantic_kind == RowSemanticKind::AbsentPresetRow) {
            if (item.preset || item.definition_count ||
                item.definition_fingerprint)
                return false;
        } else if (item.semantic_kind == RowSemanticKind::PresetRow) {
            if (!item.preset || !item.definition_count ||
                !item.definition_fingerprint)
                return false;
        }
    }
    return true;
}

const OwnerEvidence* FindOwner(
    const CheckpointRecord& record, OwnerKind kind) noexcept
{
    if (!record.captured || !record.complete) return nullptr;
    const OwnerEvidence* result = nullptr;
    for (std::size_t index = 0; index < kKnownOwnerCount; ++index) {
        const OwnerEvidence& owner = record.evidence.owners[index];
        if (owner.owner_kind != kind) continue;
        if (result) return nullptr;
        result = &owner;
    }
    return result;
}

bool RowEqual(const RowEvidence& left,
              const RowEvidence& right) noexcept
{
    return left.semantic_kind == right.semantic_kind &&
        left.preset == right.preset &&
        left.definition_count == right.definition_count &&
        left.definition_fingerprint == right.definition_fingerprint;
}

bool OwnerRowsEqual(const OwnerEvidence& left,
                    const OwnerEvidence& right) noexcept
{
    if (left.owner_kind != right.owner_kind ||
        left.owner_slot != right.owner_slot ||
        left.player_state != right.player_state ||
        left.owner_generation != right.owner_generation)
        return false;
    for (std::size_t row = 0; row < kRowCount; ++row)
        if (!RowEqual(left.rows[row], right.rows[row]))
            return false;
    return true;
}

bool IsPostC10Checkpoint(Checkpoint checkpoint) noexcept
{
    return checkpoint == Checkpoint::PostC10Pass1 ||
        checkpoint == Checkpoint::PostC10Pass2;
}

bool PostC10PairStable(const Timeline& timeline) noexcept
{
    const CheckpointRecord& first =
        timeline.checkpoints[ToIndex(Checkpoint::PostC10Pass1)];
    const CheckpointRecord& second =
        timeline.checkpoints[ToIndex(Checkpoint::PostC10Pass2)];
    if (!first.complete || !second.complete ||
        first.evidence.inventory_revision !=
            second.evidence.inventory_revision ||
        first.evidence.gameplay_container !=
            second.evidence.gameplay_container ||
        first.evidence.cache_generation !=
            second.evidence.cache_generation ||
        first.evidence.cache_fingerprint !=
            second.evidence.cache_fingerprint)
        return false;
    const OwnerKind kinds[kKnownOwnerCount] = {
        OwnerKind::LocalProcessGlobal,
        OwnerKind::CharacterManagerGlobal};
    for (std::size_t index = 0; index < kKnownOwnerCount; ++index) {
        const OwnerKind kind = kinds[index];
        const OwnerEvidence* left = FindOwner(first, kind);
        const OwnerEvidence* right = FindOwner(second, kind);
        if (!left || !right || !OwnerRowsEqual(*left, *right))
            return false;
    }
    return true;
}

const CheckpointRecord* LatestCompleteCheckpoint(
    const Timeline& timeline) noexcept
{
    const CheckpointRecord* result = nullptr;
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(Checkpoint::Count); ++index) {
        const CheckpointRecord& candidate = timeline.checkpoints[index];
        if (!candidate.complete) continue;
        if (IsPostC10Checkpoint(candidate.evidence.checkpoint) &&
            !PostC10PairStable(timeline))
            continue;
        if (!result ||
            candidate.evidence.trace_sequence >
                result->evidence.trace_sequence)
            result = &candidate;
    }
    return result;
}

Attribution ClassifyAttribution(
    const Timeline& timeline, const CheckpointEvidence& evidence,
    const RowEvidence& row, OwnerKind owner_kind,
    std::uint64_t owner_generation,
    std::uintptr_t player_state,
    std::uint32_t row_index) noexcept
{
    if (row.provider.confidence ==
            ProviderConfidence::ExactProjectSource &&
        row.provider.source && row.provider.content_epoch)
        return Attribution::ProjectSourceAttributed;
    if (timeline.project_admission_sequence &&
        evidence.trace_sequence <
            timeline.project_admission_sequence &&
        evidence.content_epoch == 0U &&
        evidence.active_custom_source_count == 0U &&
        evidence.stock_route_validated)
        return Attribution::StockBeforeProjectAdmission;
    if (timeline.project_admission_sequence &&
        evidence.trace_sequence >
            timeline.project_admission_sequence) {
        if (row.provider.confidence ==
                ProviderConfidence::ExactBaseSource)
            return Attribution::StockAfterAdmissionUnrelated;
        for (std::uint32_t index = 0;
             index < timeline.observer_event_count; ++index) {
            const ObserverEvent& observer =
                timeline.observer_events[index];
            if (!observer.exact_route ||
                observer.sequence <=
                    timeline.project_admission_sequence ||
                observer.sequence >
                    evidence.trace_sequence ||
                observer.player_state != player_state ||
                observer.owner_kind != owner_kind ||
                observer.owner_generation != owner_generation ||
                (observer.row != UINT32_MAX &&
                 observer.row != row_index) ||
                (observer.preset &&
                 observer.preset != row.preset) ||
                (observer.content_epoch &&
                 evidence.content_epoch &&
                 observer.content_epoch !=
                     evidence.content_epoch))
                continue;
            if (observer.route ==
                    ObserverRoute::LocalClientCreateRepresentation ||
                observer.route ==
                    ObserverRoute::SetProgressTemplate ||
                observer.route ==
                    ObserverRoute::InitializeVariables ||
                observer.route ==
                    ObserverRoute::SetVariablesToLevel ||
                observer.route ==
                    ObserverRoute::IgsObjectInitObject ||
                observer.route ==
                    ObserverRoute::PlayerDiOnCreate)
                return Attribution::ProjectTransitionAttributed;
        }
    }
    if (evidence.checkpoint ==
        Checkpoint::PlayerDiOnCreateReturn) {
        for (std::uint32_t index = 0;
             index < timeline.observer_event_count; ++index) {
            const ObserverEvent& observer =
                timeline.observer_events[index];
            if (observer.exact_route &&
                observer.route == ObserverRoute::PlayerDiOnCreate &&
                observer.sequence <= evidence.trace_sequence &&
                observer.player_state == player_state &&
                observer.owner_generation == owner_generation)
                return Attribution::NaturalPlayerCreationUnresolved;
        }
    }
    return Attribution::Incomplete;
}

const CachePublicationRecord* FindCachePublicationEvidence(
    const Timeline& timeline, const CheckpointEvidence& checkpoint,
    const OwnerEvidence& owner, const RowEvidence& row) noexcept
{
    const CachePublicationRecord* result = nullptr;
    for (std::uint32_t index = 0;
         index < timeline.cache_publication_count; ++index) {
        const CachePublicationRecord& candidate =
            timeline.cache_publications[index];
        if (!candidate.exact_route ||
            !candidate.classification_complete ||
            candidate.first_exact_failure != ExactFailure::None ||
            candidate.sequence > checkpoint.trace_sequence ||
            candidate.returned_preset != row.preset ||
            candidate.container != checkpoint.gameplay_container ||
            candidate.affected_row != row.row_index ||
            (candidate.affected_player_state &&
             candidate.affected_player_state != owner.player_state) ||
            (candidate.affected_owner_generation &&
             candidate.affected_owner_generation !=
                 owner.owner_generation) ||
            (candidate.affected_owner_kind != OwnerKind::Unknown &&
             candidate.affected_owner_kind != owner.owner_kind))
            continue;
        if (!result || candidate.sequence < result->sequence)
            result = &candidate;
    }
    return result;
}

}  // namespace

void Initialize(Timeline* timeline) noexcept
{
    if (timeline) std::memset(timeline, 0, sizeof(*timeline));
}

bool RecordCheckpoint(
    Timeline* timeline, const CheckpointEvidence& evidence) noexcept
{
    if (!timeline ||
        ToIndex(evidence.checkpoint) >=
            static_cast<std::size_t>(Checkpoint::Count)) {
        if (timeline)
            SaturatingIncrement(&timeline->loss.invalid_checkpoint);
        return false;
    }
    CheckpointRecord& destination =
        timeline->checkpoints[ToIndex(evidence.checkpoint)];
    if (destination.captured) {
        SaturatingIncrement(&timeline->loss.duplicate_checkpoint);
        return false;
    }
    const bool complete =
        evidence.trace_sequence && evidence.qpc &&
        evidence.thread_id && evidence.baseline_generation &&
        evidence.supported_image && evidence.environment_readable &&
        evidence.owner_set_stable && evidence.row_headers_stable &&
        evidence.first_exact_failure == ExactFailure::None &&
        OwnerComplete(evidence.owners[0]) &&
        OwnerComplete(evidence.owners[1]) &&
        evidence.owners[0].owner_kind !=
            evidence.owners[1].owner_kind;
    if (evidence.checkpoint == Checkpoint::FirstStableGameplayFrame) {
        if (timeline->first_stable_gameplay.terminal ||
            !complete)
            return false;
        timeline->first_stable_gameplay.capture_sequence =
            evidence.trace_sequence;
        timeline->first_stable_gameplay.last_failure =
            ExactFailure::None;
        timeline->first_stable_gameplay.terminal = true;
    }
    destination.evidence = evidence;
    destination.captured = true;
    destination.complete = complete;
    return complete;
}

void RecordCriticalContention(Timeline* timeline) noexcept
{
    if (timeline)
        SaturatingIncrement(
            &timeline->loss.critical_lock_contention);
}

bool HasCheckpoint(
    const Timeline& timeline, Checkpoint checkpoint) noexcept
{
    return ToIndex(checkpoint) <
               static_cast<std::size_t>(Checkpoint::Count) &&
        timeline.checkpoints[ToIndex(checkpoint)].captured;
}

void RecordProjectAdmissionBoundary(
    Timeline* timeline, std::uint64_t sequence,
    std::uint32_t content_epoch) noexcept
{
    if (!timeline || !sequence) return;
    if (!timeline->project_admission_sequence ||
        sequence < timeline->project_admission_sequence) {
        timeline->project_admission_sequence = sequence;
        timeline->project_admission_epoch = content_epoch;
    }
}

bool RecordCachePublication(
    Timeline* timeline, const CachePublicationRecord& record) noexcept
{
    if (!timeline || !record.sequence || !record.exact_route)
        return false;
    if (timeline->cache_publication_count >=
        kCachePublicationCapacity) {
        SaturatingIncrement(
            &timeline->loss.cache_publication_overflow);
        return false;
    }
    CachePublicationRecord& destination =
        timeline->cache_publications[
            timeline->cache_publication_count++];
    destination = record;
    CopyText(destination.primary_name,
             sizeof(destination.primary_name),
             record.primary_name);
    CopyText(destination.secondary_name,
             sizeof(destination.secondary_name),
             record.secondary_name);
    CopyText(destination.provider.path,
             sizeof(destination.provider.path),
             record.provider.path);
    return true;
}

bool RecordObserverEvent(
    Timeline* timeline, const ObserverEvent& event) noexcept
{
    if (!timeline || !event.sequence || !event.exact_route)
        return false;
    if (timeline->observer_event_count >=
        kObserverEventCapacity) {
        SaturatingIncrement(
            &timeline->loss.observer_event_overflow);
        return false;
    }
    ObserverEvent& destination =
        timeline->observer_events[timeline->observer_event_count++];
    destination = event;
    CopyText(
        destination.base_template,
        sizeof(destination.base_template), event.base_template);
    CopyText(
        destination.progression_template,
        sizeof(destination.progression_template),
        event.progression_template);
    CopyText(
        destination.input_name,
        sizeof(destination.input_name), event.input_name);
    CopyText(
        destination.provider_path,
        sizeof(destination.provider_path), event.provider_path);
    return true;
}

void RecordRouteValidation(
    Timeline* timeline, ObserverRoute route, bool validated,
    ExactFailure rejection) noexcept
{
    if (!timeline ||
        static_cast<std::size_t>(route) >=
            static_cast<std::size_t>(ObserverRoute::Count))
        return;
    RouteValidationRecord& record =
        timeline->route_validations[static_cast<std::size_t>(route)];
    record.route = route;
    record.attempted = true;
    record.validated = validated;
    record.rejection = validated ? ExactFailure::None :
        rejection == ExactFailure::None
            ? ExactFailure::RouteRejected : rejection;
}

void RecordFirstStableGameplayAttempt(
    Timeline* timeline, ExactFailure failure,
    std::uint64_t eligible_sequence) noexcept
{
    if (!timeline || timeline->first_stable_gameplay.terminal)
        return;
    FirstStableGameplayState& state =
        timeline->first_stable_gameplay;
    SaturatingIncrement(&state.attempt_count);
    if (failure == ExactFailure::None && eligible_sequence &&
        !state.eligible_sequence)
        state.eligible_sequence = eligible_sequence;
    if (failure != ExactFailure::None) {
        if (state.first_failure == ExactFailure::None)
            state.first_failure = failure;
        state.last_failure = failure;
    } else {
        state.last_failure = ExactFailure::None;
    }
}

void FinalizeFirstStableGameplayUnavailable(
    Timeline* timeline, ExactFailure failure) noexcept
{
    if (!timeline || timeline->first_stable_gameplay.terminal ||
        HasCheckpoint(*timeline, Checkpoint::FirstStableGameplayFrame))
        return;
    FirstStableGameplayState& state =
        timeline->first_stable_gameplay;
    if (failure == ExactFailure::None)
        failure = state.last_failure == ExactFailure::None
            ? ExactFailure::StableLevelUnavailable
            : state.last_failure;
    if (state.first_failure == ExactFailure::None)
        state.first_failure = failure;
    state.last_failure = failure;
    state.terminal = true;
}

FirstPublication FindFirstPublication(const Timeline& timeline) noexcept
{
    FirstPublication result = {};
    result.attribution = Attribution::Incomplete;
    const CheckpointRecord& baseline =
        timeline.checkpoints[ToIndex(Checkpoint::StartupB0Commit)];
    const OwnerEvidence* baseline_owner =
        FindOwner(baseline, OwnerKind::LocalProcessGlobal);
    if (!baseline_owner) return result;

    const CheckpointRecord* candidates[
        static_cast<std::size_t>(Checkpoint::Count)] = {};
    std::size_t count = 0;
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(Checkpoint::Count); ++index) {
        const CheckpointRecord& candidate =
            timeline.checkpoints[index];
        if (!candidate.complete ||
            candidate.evidence.checkpoint ==
                Checkpoint::StartupB0Commit ||
            candidate.evidence.trace_sequence <=
                baseline.evidence.trace_sequence)
            continue;
        if (IsPostC10Checkpoint(candidate.evidence.checkpoint) &&
            !PostC10PairStable(timeline))
            continue;
        candidates[count++] = &candidate;
    }
    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1U; right < count; ++right) {
            if (candidates[right]->evidence.trace_sequence <
                candidates[left]->evidence.trace_sequence) {
                const CheckpointRecord* swap = candidates[left];
                candidates[left] = candidates[right];
                candidates[right] = swap;
            }
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const CheckpointRecord& candidate = *candidates[index];
        const OwnerEvidence* owner =
            FindOwner(candidate, OwnerKind::LocalProcessGlobal);
        if (!owner ||
            owner->owner_generation !=
                baseline_owner->owner_generation ||
            owner->player_state != baseline_owner->player_state ||
            owner->owner_slot != baseline_owner->owner_slot)
            continue;
        for (std::size_t row_index = 0;
             row_index < kRowCount; ++row_index) {
            const RowEvidence& before =
                baseline_owner->rows[row_index];
            const RowEvidence& after = owner->rows[row_index];
            if (before.semantic_kind !=
                    RowSemanticKind::AbsentPresetRow ||
                after.semantic_kind !=
                    RowSemanticKind::PresetRow)
                continue;
            result.available = true;
            result.checkpoint = candidate.evidence.checkpoint;
            result.sequence = candidate.evidence.trace_sequence;
            result.thread_id = candidate.evidence.thread_id;
            result.owner_kind = owner->owner_kind;
            result.owner_generation = owner->owner_generation;
            result.row = static_cast<std::uint32_t>(row_index);
            result.baseline_kind = before.semantic_kind;
            result.post_kind = after.semantic_kind;
            result.baseline_preset = before.preset;
            result.post_preset = after.preset;
            result.baseline_definition_count =
                before.definition_count;
            result.post_definition_count =
                after.definition_count;
            result.baseline_fingerprint =
                before.definition_fingerprint;
            result.post_fingerprint =
                after.definition_fingerprint;
            result.provider = after.provider;
            result.cache_membership = after.cache_membership;
            if (result.provider.confidence ==
                    ProviderConfidence::Unobserved) {
                const CachePublicationRecord* cache =
                    FindCachePublicationEvidence(
                        timeline, candidate.evidence, *owner, after);
                if (cache)
                    result.provider = cache->provider;
            }
            RowEvidence attributed_row = after;
            attributed_row.provider = result.provider;
            result.attribution = ClassifyAttribution(
                timeline, candidate.evidence, attributed_row,
                owner->owner_kind, owner->owner_generation,
                owner->player_state,
                static_cast<std::uint32_t>(row_index));
            return result;
        }
    }
    return result;
}

DeltaSummary BuildDeltaSummary(const Timeline& timeline) noexcept
{
    DeltaSummary result = {};
    const CheckpointRecord& baseline =
        timeline.checkpoints[ToIndex(Checkpoint::StartupB0Commit)];
    const CheckpointRecord* latest =
        LatestCompleteCheckpoint(timeline);
    if (!baseline.complete || !latest ||
        latest == &baseline)
        return result;
    const OwnerKind kinds[kKnownOwnerCount] = {
        OwnerKind::LocalProcessGlobal,
        OwnerKind::CharacterManagerGlobal};
    for (std::size_t owner_index = 0;
         owner_index < kKnownOwnerCount; ++owner_index) {
        const OwnerEvidence* before =
            FindOwner(baseline, kinds[owner_index]);
        const OwnerEvidence* after =
            FindOwner(*latest, kinds[owner_index]);
        if (!before || !after ||
            before->owner_generation != after->owner_generation ||
            before->player_state != after->player_state ||
            before->owner_slot != after->owner_slot)
            return result;
        std::uint32_t mask = 0;
        bool same_direction = true;
        for (std::size_t row = 0; row < kRowCount; ++row) {
            if (RowEqual(before->rows[row], after->rows[row]))
                continue;
            mask |= 1U << row;
            const bool absent_to_preset =
                before->rows[row].semantic_kind ==
                    RowSemanticKind::AbsentPresetRow &&
                after->rows[row].semantic_kind ==
                    RowSemanticKind::PresetRow;
            if (!absent_to_preset) same_direction = false;
            if (!result.available) {
                result.available = true;
                result.first_changed_owner = kinds[owner_index];
                result.first_changed_row =
                    static_cast<std::uint32_t>(row);
                result.baseline_kind =
                    before->rows[row].semantic_kind;
                result.post_kind =
                    after->rows[row].semantic_kind;
                result.baseline_preset =
                    before->rows[row].preset;
                result.post_preset =
                    after->rows[row].preset;
                result.baseline_definition_count =
                    before->rows[row].definition_count;
                result.post_definition_count =
                    after->rows[row].definition_count;
                result.baseline_fingerprint =
                    before->rows[row].definition_fingerprint;
                result.post_fingerprint =
                    after->rows[row].definition_fingerprint;
            }
        }
        if (result.available &&
            result.first_changed_owner == kinds[owner_index]) {
            result.rows_changed_mask = mask;
            result.all_rows_changed_same_direction =
                mask == 0xFFU && same_direction;
            const OwnerKind other_kind =
                owner_index == 0U
                    ? OwnerKind::CharacterManagerGlobal
                    : OwnerKind::LocalProcessGlobal;
            const OwnerEvidence* other_before =
                FindOwner(baseline, other_kind);
            const OwnerEvidence* other_after =
                FindOwner(*latest, other_kind);
            result.other_owner_unchanged =
                other_before && other_after &&
                OwnerRowsEqual(*other_before, *other_after);
            return result;
        }
    }
    return result;
}

const char* CheckpointName(Checkpoint checkpoint) noexcept
{
    switch (checkpoint) {
    case Checkpoint::StartupB0Commit:
        return "startup_b0_commit";
    case Checkpoint::BeforeProjectPremountOriginal:
        return "before_project_premount_original";
    case Checkpoint::UpdateDlcContentReturn:
        return "update_dlc_content_return";
    case Checkpoint::ChangeAfterFadeEntry:
        return "change_after_fade_entry";
    case Checkpoint::ChangeAfterFadeReturn:
        return "change_after_fade_return";
    case Checkpoint::PlayerDiOnCreateReturn:
        return "playerdi_oncreate_return";
    case Checkpoint::FirstStableGameplayFrame:
        return "first_stable_gameplay_frame";
    case Checkpoint::C08Entry:
        return "c08_entry";
    case Checkpoint::C10Entry:
        return "c10_entry";
    case Checkpoint::SessionOnDestroyEntry:
        return "session_on_destroy_entry";
    case Checkpoint::SessionOnDestroyOriginalReturn:
        return "session_on_destroy_original_return";
    case Checkpoint::PostC10Pass1:
        return "post_c10_pass1";
    case Checkpoint::PostC10Pass2:
        return "post_c10_pass2";
    default:
        return "unknown";
    }
}

const char* OwnerKindName(OwnerKind kind) noexcept
{
    switch (kind) {
    case OwnerKind::LocalProcessGlobal:
        return "local_process_global";
    case OwnerKind::CharacterManagerGlobal:
        return "character_manager_global";
    default:
        return "unknown";
    }
}

const char* RowSemanticKindName(RowSemanticKind kind) noexcept
{
    switch (kind) {
    case RowSemanticKind::AbsentPresetRow:
        return "AbsentPresetRow";
    case RowSemanticKind::PresetRow:
        return "PresetRow";
    default:
        return "Incomplete";
    }
}

const char* CacheMembershipName(CacheMembership membership) noexcept
{
    switch (membership) {
    case CacheMembership::ProvenMember:
        return "proven_member";
    case CacheMembership::ProvenNonMember:
        return "proven_nonmember";
    case CacheMembership::Conflict:
        return "conflict";
    default:
        return "unobserved";
    }
}

const char* ProviderConfidenceName(
    ProviderConfidence confidence) noexcept
{
    switch (confidence) {
    case ProviderConfidence::ObservedUnresolved:
        return "observed_unresolved";
    case ProviderConfidence::ExactBaseSource:
        return "exact_base_source";
    case ProviderConfidence::ExactProjectSource:
        return "exact_project_source";
    default:
        return "unobserved";
    }
}

const char* ExactFailureName(ExactFailure failure) noexcept
{
    switch (failure) {
    case ExactFailure::None: return "none";
    case ExactFailure::InvalidArgument: return "invalid_argument";
    case ExactFailure::UnsupportedImage: return "unsupported_image";
    case ExactFailure::EnvironmentUnreadable:
        return "environment_unreadable";
    case ExactFailure::InventoryUnavailable:
        return "inventory_unavailable";
    case ExactFailure::OwnerSlotUnreadable:
        return "owner_slot_unreadable";
    case ExactFailure::OwnerMissing: return "owner_missing";
    case ExactFailure::OwnerGenerationChanged:
        return "owner_generation_changed";
    case ExactFailure::OwnerDestroyed: return "owner_destroyed";
    case ExactFailure::OwnerUnreadable: return "owner_unreadable";
    case ExactFailure::StateSnapshotRejected:
        return "state_snapshot_rejected";
    case ExactFailure::RowSnapshotIncomplete:
        return "row_snapshot_incomplete";
    case ExactFailure::UnstableTwoPassSnapshot:
        return "unstable_two_pass_snapshot";
    case ExactFailure::CacheSnapshotChanged:
        return "cache_snapshot_changed";
    case ExactFailure::TimelineBusy: return "timeline_busy";
    case ExactFailure::CapacityLost: return "capacity_lost";
    case ExactFailure::ProjectNotAdmitted:
        return "project_not_admitted";
    case ExactFailure::StableLevelUnavailable:
        return "stable_level_unavailable";
    case ExactFailure::LocalPlayerDiUnavailable:
        return "local_playerdi_unavailable";
    case ExactFailure::PlayerDiOnCreateIncomplete:
        return "playerdi_oncreate_incomplete";
    case ExactFailure::CallerMismatch: return "caller_mismatch";
    case ExactFailure::ReceiverMismatch: return "receiver_mismatch";
    case ExactFailure::CallSiteMismatch: return "callsite_mismatch";
    case ExactFailure::ReentrancyConflict:
        return "reentrancy_conflict";
    case ExactFailure::RouteRejected: return "route_rejected";
    default: return "unknown";
    }
}

const char* AttributionName(Attribution attribution) noexcept
{
    switch (attribution) {
    case Attribution::StockBeforeProjectAdmission:
        return "StockBeforeProjectAdmission";
    case Attribution::StockAfterAdmissionUnrelated:
        return "StockAfterAdmissionUnrelated";
    case Attribution::ProjectSourceAttributed:
        return "ProjectSourceAttributed";
    case Attribution::ProjectTransitionAttributed:
        return "ProjectTransitionAttributed";
    case Attribution::NaturalPlayerCreationUnresolved:
        return "NaturalPlayerCreationUnresolved";
    default:
        return "Incomplete";
    }
}

const char* ObserverRouteName(ObserverRoute route) noexcept
{
    switch (route) {
    case ObserverRoute::LocalClientCreateRepresentation:
        return "LocalClientDI::CreateRepresentation";
    case ObserverRoute::CachePlayerLevelPreset:
        return "GameplayParamsContainer::CachePlayerLevelPreset";
    case ObserverRoute::FindPlayerLevelPreset:
        return "GameplayParamsContainer::FindPlayerLevelPreset";
    case ObserverRoute::SetProgressTemplate:
        return "PlayerState::SetProgressTemplate";
    case ObserverRoute::RemovePrecachedLevelPresets:
        return "PlayerState::RemovePrecachedLevelPresets";
    case ObserverRoute::InitializeVariables:
        return "PlayerState::InitializeVariables";
    case ObserverRoute::SetVariablesToLevel:
        return "PlayerState::SetVariablesToLevel";
    case ObserverRoute::IgsObjectInitObject:
        return "IGSObject::InitObject";
    case ObserverRoute::PlayerDiOnCreate:
        return "PlayerDI::OnCreate";
    case ObserverRoute::RowPublicationStore:
        return "PlayerState row publication store";
    default:
        return "unknown";
    }
}

const char* WriterSubphaseName(WriterSubphase subphase) noexcept
{
    switch (subphase) {
    case WriterSubphase::CreateRepresentationEntry:
        return "create_representation_entry";
    case WriterSubphase::CreateRepresentationBeforeSetProgressTemplate:
        return "create_representation_before_set_progress_template";
    case WriterSubphase::SetProgressTemplateEntry:
        return "set_progress_template_entry";
    case WriterSubphase::SetProgressTemplateBeforeInitializeVariables:
        return "set_progress_template_before_initialize_variables";
    case WriterSubphase::InitializeVariablesEntry:
        return "initialize_variables_entry";
    case WriterSubphase::CachePlayerLevelPresetEntry:
        return "cache_player_level_preset_entry";
    case WriterSubphase::CachePlayerLevelPresetReturn:
        return "cache_player_level_preset_return";
    case WriterSubphase::FindPlayerLevelPresetReturn:
        return "find_player_level_preset_return";
    case WriterSubphase::RowPublicationStore:
        return "row_publication_store";
    case WriterSubphase::InitializeVariablesReturn:
        return "initialize_variables_return";
    case WriterSubphase::SetProgressTemplateReturn:
        return "set_progress_template_return";
    case WriterSubphase::CreateRepresentationAfterSetProgressTemplate:
        return "create_representation_after_set_progress_template";
    case WriterSubphase::CreateRepresentationBeforeInitObject:
        return "create_representation_before_init_object";
    case WriterSubphase::PlayerDiOnCreateEntry:
        return "playerdi_oncreate_entry";
    case WriterSubphase::PlayerDiOnCreateReturn:
        return "playerdi_oncreate_return";
    default:
        return "unspecified";
    }
}

const char* CallerModuleName(CallerModule module) noexcept
{
    switch (module) {
    case CallerModule::GameDll: return "gamedll_x64_rwdi.dll";
    case CallerModule::Engine: return "engine_x64_rwdi.dll";
    case CallerModule::FileSystem: return "filesystem_x64_rwdi.dll";
    case CallerModule::ImageLib: return "imagelib_x64_rwdi.dll";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::player_publication
