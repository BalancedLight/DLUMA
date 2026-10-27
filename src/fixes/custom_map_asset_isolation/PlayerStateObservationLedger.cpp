#define _HAS_EXCEPTIONS 0

#include "PlayerStateObservationLedger.h"

#include <cstring>
#include <intrin.h>

namespace dluma::fixes::custom_map_asset_isolation::playerstate {
namespace {

void TouchRevision(Ledger* ledger) noexcept
{
    if (ledger)
        _InterlockedIncrement64(
            reinterpret_cast<volatile LONGLONG*>(&ledger->revision));
}

bool Acquire(Ledger* ledger) noexcept
{
    if (!ledger) return false;
    if (dluma_atomic_compare_exchange(&ledger->lock, 1, 0) == 0) return true;
    dluma_atomic_increment(
        reinterpret_cast<volatile LONG*>(&ledger->loss.lock_contention));
    TouchRevision(ledger);
    return false;
}

void Release(Ledger* ledger) noexcept
{
    dluma_atomic_exchange(&ledger->lock, 0);
}

bool TextEqual(const char* left, const char* right,
               std::size_t capacity) noexcept
{
    if (!left || !right) return left == right;
    for (std::size_t index = 0; index < capacity; ++index) {
        unsigned char a = static_cast<unsigned char>(left[index]);
        unsigned char b = static_cast<unsigned char>(right[index]);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b + 32);
        if (a != b) return false;
        if (!a) return true;
    }
    return true;
}

int OwnerRank(OwnerKind kind) noexcept
{
    switch (kind) {
    case OwnerKind::LocalProcessGlobal: return 5;
    case OwnerKind::CharacterManagerGlobal: return 4;
    case OwnerKind::TutorialPlayerDi: return 3;
    case OwnerKind::PlayerDi: return 2;
    case OwnerKind::OtherRttiObserved: return 1;
    default: return 0;
    }
}

bool SameRows(const StateRowsSnapshot& left,
              const StateRowsSnapshot& right) noexcept
{
    if (!left.all_row_pointers_readable ||
        !right.all_row_pointers_readable ||
        !left.all_vector_headers_readable ||
        !right.all_vector_headers_readable ||
        !left.all_bounded_partial_fingerprints_available ||
        !right.all_bounded_partial_fingerprints_available ||
        left.player_variables != right.player_variables ||
        left.combined_fingerprint != right.combined_fingerprint)
        return false;
    for (std::size_t index = 0; index < kRowCount; ++index) {
        if (!ClassifyRowRetentionWithoutDeepSnapshot(
                left.rows[index], right.rows[index]))
            return false;
    }
    return true;
}

}  // namespace

void Initialize(Ledger* ledger) noexcept
{
    if (!ledger) return;
    std::memset(ledger, 0, sizeof(*ledger));
    ledger->cache_generation = 1;
}

std::uint64_t ObserveLiveState(Ledger* ledger, std::uintptr_t pointer,
                               OwnerKind owner_kind,
                               std::uintptr_t owner_object,
                               std::uint64_t sequence) noexcept
{
    if (!ledger || !pointer || !Acquire(ledger)) return 0;
    for (std::uint32_t index = 0; index < ledger->owner_count; ++index) {
        OwnerRecord& record = ledger->owners[index];
        if (record.pointer != pointer || !record.live) continue;
        if (OwnerRank(owner_kind) > OwnerRank(record.owner_kind)) {
            record.owner_kind = owner_kind;
            record.owner_object = owner_object;
            TouchRevision(ledger);
        } else if (!record.owner_object && owner_object) {
            record.owner_object = owner_object;
            TouchRevision(ledger);
        }
        const std::uint64_t generation = record.generation;
        Release(ledger);
        return generation;
    }
    if (ledger->owner_count >= kOwnerCapacity) {
        ++ledger->loss.owner_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return 0;
    }
    OwnerRecord& record = ledger->owners[ledger->owner_count++];
    std::memset(&record, 0, sizeof(record));
    record.pointer = pointer;
    record.generation = ++ledger->next_owner_generation;
    record.owner_kind = owner_kind;
    record.owner_object = owner_object;
    record.creation_sequence = sequence;
    record.live = true;
    TouchRevision(ledger);
    const std::uint64_t generation = record.generation;
    Release(ledger);
    return generation;
}

bool ObserveDestroyedState(Ledger* ledger, std::uintptr_t pointer,
                           std::uintptr_t owner_object,
                           std::uint64_t sequence,
                           std::uint64_t* generation) noexcept
{
    if (generation) *generation = 0;
    if (!ledger || !pointer || !Acquire(ledger)) return false;
    for (std::uint32_t reverse = ledger->owner_count; reverse; --reverse) {
        OwnerRecord& record = ledger->owners[reverse - 1];
        if (record.pointer != pointer || !record.live ||
            (owner_object && record.owner_object &&
             record.owner_object != owner_object))
            continue;
        record.live = false;
        record.destruction_sequence = sequence;
        TouchRevision(ledger);
        if (generation) *generation = record.generation;
        Release(ledger);
        return true;
    }
    Release(ledger);
    return false;
}

std::uint64_t FindLiveGeneration(Ledger* ledger,
                                 std::uintptr_t pointer) noexcept
{
    if (!ledger || !pointer || !Acquire(ledger)) return 0;
    std::uint64_t generation = 0;
    for (std::uint32_t reverse = ledger->owner_count; reverse; --reverse) {
        const OwnerRecord& record = ledger->owners[reverse - 1];
        if (record.pointer == pointer && record.live) {
            generation = record.generation;
            break;
        }
    }
    Release(ledger);
    return generation;
}

std::uint32_t CaptureLiveOwners(Ledger* ledger, OwnerRecord* output,
                                std::size_t capacity) noexcept
{
    if (!ledger || !output || !capacity || !Acquire(ledger)) return 0;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0;
         index < ledger->owner_count && count < capacity; ++index) {
        if (ledger->owners[index].live)
            output[count++] = ledger->owners[index];
    }
    Release(ledger);
    return count;
}

std::uint64_t CurrentCacheGeneration(Ledger* ledger) noexcept
{
    if (!ledger || !Acquire(ledger)) return 0;
    const std::uint64_t result = ledger->cache_generation;
    Release(ledger);
    return result;
}

std::uint64_t CurrentRevision(Ledger* ledger) noexcept
{
    if (!ledger || !Acquire(ledger)) return 0;
    const std::uint64_t result = ledger->revision;
    Release(ledger);
    return result;
}

std::uint64_t AdvanceCacheGeneration(Ledger* ledger,
                                     CacheMutationKind kind,
                                     std::uint64_t) noexcept
{
    if (!ledger || !Acquire(ledger)) return 0;
    switch (kind) {
    case CacheMutationKind::Append: ++ledger->cache_append_count; break;
    case CacheMutationKind::RecordRebuild:
        ++ledger->cache_rebuild_count;
        break;
    case CacheMutationKind::WholeCacheReset:
        ++ledger->cache_reset_count;
        break;
    }
    const std::uint64_t result = ++ledger->cache_generation;
    TouchRevision(ledger);
    Release(ledger);
    return result;
}

bool RecordDefinitionVector(Ledger* ledger,
                            const DefinitionVectorRecord& record) noexcept
{
    if (!ledger || !record.pointer || !Acquire(ledger)) return false;
    for (std::uint32_t reverse = ledger->definition_vector_count;
         reverse; --reverse) {
        DefinitionVectorRecord& existing =
            ledger->definition_vectors[reverse - 1];
        if (existing.pointer == record.pointer &&
            existing.cache_generation == record.cache_generation) {
            const bool same_definition = existing.fingerprint == record.fingerprint &&
                existing.definition_count == record.definition_count;
            ProviderIdentity retained_inputs[3] = {};
            std::memcpy(retained_inputs, existing.inputs, sizeof(retained_inputs));
            const std::uint32_t retained_input_count = existing.input_count;
            const bool retained_inputs_complete = existing.inputs_complete;
            const ProviderIdentity retained_provider = existing.provider;
            const std::uint32_t retained_request_count =
                existing.provider_request_count;
            const std::uint32_t retained_success_count =
                existing.provider_success_count;
            const bool retained_complete = existing.provider_complete;
            const bool retained_identity_complete =
                existing.record_identity_complete;
            const std::uint32_t retained_logical_type =
                existing.logical_type;
            const std::uint16_t retained_maximum_level =
                existing.maximum_level;
            existing = record;
            if (!record.record_identity_complete && retained_identity_complete && same_definition) {
                existing.logical_type = retained_logical_type;
                existing.maximum_level = retained_maximum_level;
                existing.record_identity_complete = true;
            }
            if (!record.inputs_complete && retained_inputs_complete && same_definition) {
                std::memcpy(existing.inputs, retained_inputs, sizeof(existing.inputs));
                existing.input_count = retained_input_count;
                existing.inputs_complete = true;
            }
            if (!record.provider_complete && retained_complete) {
                existing.provider = retained_provider;
                existing.provider_request_count = retained_request_count;
                existing.provider_success_count = retained_success_count;
                existing.provider_complete = true;
                existing.logical_type = retained_logical_type;
                existing.maximum_level = retained_maximum_level;
                existing.record_identity_complete =
                    retained_identity_complete;
            }
            TouchRevision(ledger);
            Release(ledger);
            return true;
        }
    }
    if (ledger->definition_vector_count >= kDefinitionVectorCapacity) {
        ++ledger->loss.vector_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    ledger->definition_vectors[ledger->definition_vector_count++] = record;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool RecordApplication(Ledger* ledger, StateApplication* application) noexcept
{
    if (!ledger || !application || !Acquire(ledger)) return false;
    const StateApplication* previous = nullptr;
    for (std::uint32_t reverse = ledger->application_count;
         reverse; --reverse) {
        const StateApplication& candidate = ledger->applications[reverse - 1];
        if (candidate.state_generation == application->state_generation &&
            candidate.state == application->state) {
            previous = &candidate;
            break;
        }
    }
    if (previous && !application->selectors_changed &&
        application->content_epoch != previous->content_epoch &&
        TextEqual(application->progression_selector,
                  previous->progression_selector, kSelectorCapacity) &&
        TextEqual(application->class_selector,
                  previous->class_selector, kSelectorCapacity) &&
        previous->provider.open_succeeded && previous->provider.exact_source &&
        !application->provider.request_observed) {
        application->same_selector_provider_changed = true;
        application->prior_content_epoch = previous->content_epoch;
        application->prior_provider = previous->provider;
        if (!application->clear_observed &&
            !application->initialize_observed &&
            SameRows(application->rows, previous->rows)) {
            application->provider_change_rebuild_skipped = true;
            application->stale_progression_definition_retained = true;
        }
    }
    if (ledger->application_count >= kApplicationCapacity) {
        ++ledger->loss.application_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    ledger->applications[ledger->application_count++] = *application;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool RecordMembership(Ledger* ledger,
                      const CacheMembershipRecord& membership) noexcept
{
    if (!ledger || !Acquire(ledger)) return false;
    if (ledger->membership_count >= kMembershipCapacity) {
        ++ledger->loss.membership_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    ledger->memberships[ledger->membership_count++] = membership;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool RecordSideEffect(Ledger* ledger,
                      const SideEffectObservation& observation) noexcept
{
    if (!ledger || !Acquire(ledger)) return false;
    if (ledger->side_effect_count >= kSideEffectCapacity) {
        ++ledger->loss.side_effect_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    ledger->side_effects[ledger->side_effect_count++] = observation;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool RecordAppearance(Ledger* ledger,
                      const AppearanceObservation& input) noexcept
{
    if (!ledger || !Acquire(ledger)) return false;
    if (ledger->appearance_count >= kAppearanceCapacity) {
        ++ledger->loss.appearance_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    AppearanceObservation observation = input;
    for (std::uint32_t reverse = ledger->appearance_count;
         reverse; --reverse) {
        const AppearanceObservation& previous =
            ledger->appearances[reverse - 1];
        if (previous.state_generation != observation.state_generation)
            continue;
        observation.provider_changed =
            previous.provider.open_succeeded &&
            observation.provider.open_succeeded &&
            (previous.provider.source != observation.provider.source ||
             !TextEqual(previous.provider.provider,
                        observation.provider.provider,
                        kProviderCapacity));
        break;
    }
    ledger->appearances[ledger->appearance_count++] = observation;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool RecordNaturalClear(Ledger* ledger,
                        NaturalClearObservation* observation) noexcept
{
    if (!ledger || !observation || !Acquire(ledger)) return false;
    if (ledger->natural_clear_count >= kNaturalClearCapacity) {
        ++ledger->loss.natural_clear_overflow;
        TouchRevision(ledger);
        Release(ledger);
        return false;
    }
    const DefinitionRowSnapshot& before_type0 = observation->before.rows[0];
    const DefinitionRowSnapshot& after_type0 = observation->after.rows[0];
    observation->type0_pointer_retained =
        before_type0.row_pointer_readable &&
        after_type0.row_pointer_readable &&
        before_type0.definition_vector != 0 &&
        before_type0.definition_vector == after_type0.definition_vector;
    observation->type0_content_retained =
        ClassifyRowRetentionWithoutDeepSnapshot(before_type0, after_type0);
    observation->all_content_retained =
        observation->before.all_row_pointers_readable &&
        observation->after.all_row_pointers_readable &&
        observation->before.all_vector_headers_readable &&
        observation->after.all_vector_headers_readable &&
        observation->before.all_bounded_partial_fingerprints_available &&
        observation->after.all_bounded_partial_fingerprints_available &&
        observation->before.player_variables ==
            observation->after.player_variables &&
        observation->before.combined_fingerprint ==
            observation->after.combined_fingerprint;
    for (std::uint32_t reverse = ledger->application_count;
         reverse; --reverse) {
        StateApplication& application = ledger->applications[reverse - 1];
        if (application.state != observation->state ||
            application.state_generation != observation->state_generation ||
            application.content_epoch != observation->content_epoch ||
            application.sequence >= observation->enter_sequence)
            continue;
        observation->correlated_application_sequence = application.sequence;
        if (!application.same_selector_provider_changed) break;
        observation->selector_equal_provider_change_correlated = true;
        application.post_selection_clear_observed = true;
        application.post_selection_clear_sequence =
            observation->exit_sequence;
        const DefinitionRowSnapshot& selected_type0 =
            application.rows.rows[0];
        const bool selected_type0_retained =
            selected_type0.row_pointer_readable &&
            before_type0.row_pointer_readable &&
            after_type0.row_pointer_readable &&
            selected_type0.vector_header_readable &&
            before_type0.vector_header_readable &&
            after_type0.vector_header_readable &&
            selected_type0.bounded_partial_fingerprint_available &&
            before_type0.bounded_partial_fingerprint_available &&
            after_type0.bounded_partial_fingerprint_available &&
            selected_type0.definition_vector != 0 &&
            selected_type0.definition_vector ==
                before_type0.definition_vector &&
            selected_type0.definition_vector ==
                after_type0.definition_vector &&
            selected_type0.fingerprint == before_type0.fingerprint &&
            selected_type0.fingerprint == after_type0.fingerprint &&
            selected_type0.definition_count ==
                before_type0.definition_count &&
            selected_type0.definition_count ==
                after_type0.definition_count;
        application.post_selection_clear_type0_retained =
            selected_type0_retained;
        if (!application.initialize_observed && selected_type0_retained &&
            application.cache_generation == ledger->cache_generation) {
            observation->rebuild_skipped_after_clear = true;
            observation->stale_type0_retained_after_clear = true;
            application.provider_change_rebuild_skipped = true;
            application.stale_progression_definition_retained = true;
        }
        break;
    }
    ledger->natural_clears[ledger->natural_clear_count++] = *observation;
    TouchRevision(ledger);
    Release(ledger);
    return true;
}

bool ClassifyRowRetentionWithoutDeepSnapshot(
    const DefinitionRowSnapshot& before,
    const DefinitionRowSnapshot& after) noexcept
{
    return before.row_pointer_readable && after.row_pointer_readable &&
        before.vector_header_readable && after.vector_header_readable &&
        before.bounded_partial_fingerprint_available &&
        after.bounded_partial_fingerprint_available &&
        before.preset_absent == after.preset_absent &&
        (before.preset_absent || before.definition_vector != 0) &&
        before.definition_vector == after.definition_vector &&
        before.definition_count == after.definition_count &&
        before.fingerprint == after.fingerprint;
}

bool CaptureSnapshot(Ledger* ledger, Snapshot* snapshot) noexcept
{
    if (!ledger || !snapshot || !Acquire(ledger)) return false;
    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->revision = ledger->revision;
    snapshot->cache_generation = ledger->cache_generation;
    snapshot->cache_append_count = ledger->cache_append_count;
    snapshot->cache_rebuild_count = ledger->cache_rebuild_count;
    snapshot->cache_reset_count = ledger->cache_reset_count;
    snapshot->owner_count = ledger->owner_count;
    snapshot->definition_vector_count = ledger->definition_vector_count;
    snapshot->application_count = ledger->application_count;
    snapshot->membership_count = ledger->membership_count;
    snapshot->side_effect_count = ledger->side_effect_count;
    snapshot->appearance_count = ledger->appearance_count;
    snapshot->natural_clear_count = ledger->natural_clear_count;
    std::memcpy(snapshot->owners, ledger->owners, sizeof(snapshot->owners));
    std::memcpy(snapshot->definition_vectors, ledger->definition_vectors,
                sizeof(snapshot->definition_vectors));
    std::memcpy(snapshot->applications, ledger->applications,
                sizeof(snapshot->applications));
    std::memcpy(snapshot->memberships, ledger->memberships,
                sizeof(snapshot->memberships));
    std::memcpy(snapshot->side_effects, ledger->side_effects,
                sizeof(snapshot->side_effects));
    std::memcpy(snapshot->appearances, ledger->appearances,
                sizeof(snapshot->appearances));
    std::memcpy(snapshot->natural_clears, ledger->natural_clears,
                sizeof(snapshot->natural_clears));
    snapshot->loss = ledger->loss;
    Release(ledger);
    return true;
}

const char* OwnerKindName(OwnerKind kind) noexcept
{
    switch (kind) {
    case OwnerKind::LocalProcessGlobal: return "local_process_global";
    case OwnerKind::CharacterManagerGlobal: return "character_manager_global";
    case OwnerKind::PlayerDi: return "playerdi";
    case OwnerKind::TutorialPlayerDi: return "tutorial_playerdi";
    case OwnerKind::OtherRttiObserved: return "other_rtti_observed";
    default: return "unknown";
    }
}

const char* MembershipDispositionName(MembershipDisposition value) noexcept
{
    switch (value) {
    case MembershipDisposition::LiveGeneration: return "live_generation";
    case MembershipDisposition::Rebound: return "rebound";
    case MembershipDisposition::DestroyedGeneration:
        return "destroyed_generation";
    default: return "unknown";
    }
}

const char* SideEffectKindName(SideEffectKind kind) noexcept
{
    switch (kind) {
    case SideEffectKind::SetProgressTemplateEqual:
        return "set_progress_template_equal";
    case SideEffectKind::SetProgressTemplateChanged:
        return "set_progress_template_changed";
    case SideEffectKind::Clear: return "clear";
    case SideEffectKind::RemovePrecached: return "remove_precached";
    case SideEffectKind::InitializeVariables: return "initialize_variables";
    case SideEffectKind::ReloadPlayerScripts: return "reload_player_scripts";
    case SideEffectKind::RecreateRepresentation:
        return "recreate_representation";
    case SideEffectKind::PlayerDiCreate: return "playerdi_create";
    case SideEffectKind::PlayerDiDestroy: return "playerdi_destroy";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::playerstate
