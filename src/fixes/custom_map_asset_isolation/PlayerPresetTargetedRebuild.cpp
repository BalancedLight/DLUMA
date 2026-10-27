#define _HAS_EXCEPTIONS 0

#include "PlayerPresetTargetedRebuild.h"

#include <cstring>
#include <limits>

namespace dluma::fixes::custom_map_asset_isolation::playerpreset {
namespace {

bool Acquire(PendingRepairStore* store) noexcept
{
    return store &&
        dluma_atomic_compare_exchange(&store->lock, 1, 0) == 0;
}

void Release(PendingRepairStore* store) noexcept
{
    dluma_atomic_exchange(&store->lock, 0);
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

void SetFailure(PreflightFailure* output,
                PreflightFailure value) noexcept
{
    if (output) *output = value;
}

}  // namespace

void Initialize(PendingRepairStore* store) noexcept
{
    if (!store) return;
    std::memset(store, 0, sizeof(*store));
}

bool PublishPendingRepair(
    PendingRepairStore* store,
    PendingPlayerPresetProviderRepair* token) noexcept
{
    if (!store || !token || !Acquire(store)) return false;
    token->token_generation = ++store->next_generation;
    token->consumed = false;
    store->token = *token;
    store->available = true;
    Release(store);
    return true;
}

bool PeekPendingRepair(
    PendingRepairStore* store,
    PendingPlayerPresetProviderRepair* output) noexcept
{
    if (!store || !output || !Acquire(store)) return false;
    const bool available = store->available && !store->token.consumed;
    if (available) *output = store->token;
    Release(store);
    return available;
}

bool ConsumePendingRepair(
    PendingRepairStore* store, std::uint64_t token_generation,
    PendingPlayerPresetProviderRepair* output) noexcept
{
    if (!store || !token_generation || !Acquire(store)) return false;
    const bool matches = store->available && !store->token.consumed &&
        store->token.token_generation == token_generation;
    if (matches) {
        store->token.consumed = true;
        if (output) *output = store->token;
        store->available = false;
    }
    Release(store);
    return matches;
}

void ExpirePendingRepair(PendingRepairStore* store) noexcept
{
    if (!store || !Acquire(store)) return;
    store->available = false;
    store->token.consumed = true;
    Release(store);
}

bool TokenMatchesLoad(
    const PendingPlayerPresetProviderRepair& token,
    std::uintptr_t player_state, std::uint64_t player_state_generation,
    std::uintptr_t player_variables, std::uint32_t content_epoch,
    std::uint64_t cache_generation, std::uint32_t thread_id,
    const char* progression_selector, const char* class_selector,
    PreflightFailure* failure) noexcept
{
    SetFailure(failure, PreflightFailure::None);
    if (token.consumed) {
        SetFailure(failure, PreflightFailure::TokenAlreadyConsumed);
        return false;
    }
    if (token.player_state != player_state) {
        SetFailure(failure, PreflightFailure::StateMismatch);
        return false;
    }
    if (!player_state_generation ||
        token.player_state_generation != player_state_generation) {
        SetFailure(failure, PreflightFailure::StateGenerationMismatch);
        return false;
    }
    if (token.player_variables != player_variables) {
        SetFailure(failure, PreflightFailure::StateMismatch);
        return false;
    }
    if (token.owner_thread_id != thread_id) {
        SetFailure(failure, PreflightFailure::ThreadMismatch);
        return false;
    }
    if (token.current_content_epoch != content_epoch) {
        SetFailure(failure, PreflightFailure::ContentEpochMismatch);
        return false;
    }
    if (token.cache_generation != cache_generation) {
        SetFailure(failure, PreflightFailure::CacheGenerationMismatch);
        return false;
    }
    if (!TextEqual(token.progression_selector, progression_selector,
                   kSelectorCapacity) ||
        !TextEqual(token.class_selector, class_selector,
                   kSelectorCapacity)) {
        SetFailure(failure, PreflightFailure::SelectorMismatch);
        return false;
    }
    if (!token.selector_equal || !token.content_identity_changed) {
        SetFailure(failure, PreflightFailure::SelectorMismatch);
        return false;
    }
    if (!token.old_data_source_inactive) {
        SetFailure(failure, PreflightFailure::OldDataSourceStillActive);
        return false;
    }
    if (!token.current_epoch_sources_ready) {
        SetFailure(failure,
                   PreflightFailure::CurrentEpochSourcesIncomplete);
        return false;
    }
    return true;
}

bool MapDefinitionVectorToValidatedCacheRecord(
    std::uintptr_t definition_vector, std::uint32_t logical_type,
    const CacheVectorBounds& bounds, std::uintptr_t* record,
    PreflightFailure* failure) noexcept
{
    if (record) *record = 0;
    SetFailure(failure, PreflightFailure::None);
    if (!record || !definition_vector || !bounds.descriptor_readable ||
        bounds.logical_type != logical_type || !bounds.records ||
        !bounds.count || bounds.count > bounds.capacity ||
        bounds.count > 4096U) {
        SetFailure(failure, PreflightFailure::CacheVectorInvalid);
        return false;
    }
    if (bounds.records >
        std::numeric_limits<std::uintptr_t>::max() -
            kPresetDefinitionVectorOffset) {
        SetFailure(failure, PreflightFailure::CacheVectorInvalid);
        return false;
    }
    const std::uintptr_t first_vector =
        bounds.records + kPresetDefinitionVectorOffset;
    if (definition_vector < first_vector) {
        SetFailure(failure, PreflightFailure::RecordMembershipMissing);
        return false;
    }
    const std::uintptr_t delta = definition_vector - first_vector;
    if ((delta % kPresetRecordSize) != 0) {
        SetFailure(failure, PreflightFailure::RecordMembershipMissing);
        return false;
    }
    const std::uintptr_t index = delta / kPresetRecordSize;
    if (index >= bounds.count ||
        index > (std::numeric_limits<std::uintptr_t>::max() -
                 bounds.records) / kPresetRecordSize) {
        SetFailure(failure, PreflightFailure::RecordMembershipMissing);
        return false;
    }
    *record = bounds.records + index * kPresetRecordSize;
    return *record + kPresetDefinitionVectorOffset == definition_vector;
}

bool AppendValidatedRecord(
    TargetedRebuildPlan* plan, std::uint32_t row_index,
    std::uintptr_t record, std::uintptr_t definition_vector,
    std::uint32_t logical_type, PreflightFailure* failure) noexcept
{
    SetFailure(failure, PreflightFailure::None);
    if (!plan || row_index >= kLogicalTypeCount ||
        logical_type >= kLogicalTypeCount || !record ||
        record + kPresetDefinitionVectorOffset != definition_vector) {
        SetFailure(failure, PreflightFailure::RecordMembershipMissing);
        return false;
    }
    for (std::uint32_t index = 0; index < plan->distinct_count; ++index) {
        if (plan->records[index].record != record) continue;
        if (plan->records[index].definition_vector != definition_vector ||
            plan->records[index].logical_type != logical_type) {
            SetFailure(failure,
                       PreflightFailure::RecordMembershipAmbiguous);
            return false;
        }
        plan->row_to_record[row_index] = static_cast<std::uint8_t>(index);
        return true;
    }
    if (plan->distinct_count >= kLogicalTypeCount) {
        SetFailure(failure, PreflightFailure::RecordMembershipAmbiguous);
        return false;
    }
    std::uint32_t insert = plan->distinct_count;
    while (insert &&
           plan->records[insert - 1].logical_type > logical_type) {
        plan->records[insert] = plan->records[insert - 1];
        --insert;
    }
    plan->records[insert] = {record, definition_vector, logical_type};
    ++plan->distinct_count;
    for (std::uint32_t row = 0; row < row_index; ++row) {
        if (plan->row_to_record[row] >= insert)
            ++plan->row_to_record[row];
    }
    plan->row_to_record[row_index] = static_cast<std::uint8_t>(insert);
    return true;
}

const char* PreflightFailureName(PreflightFailure failure) noexcept
{
    switch (failure) {
    case PreflightFailure::None: return "none";
    case PreflightFailure::FeatureDisabled: return "feature_disabled";
    case PreflightFailure::UnsupportedImage: return "unsupported_image";
    case PreflightFailure::NoPendingToken: return "no_pending_token";
    case PreflightFailure::TokenAlreadyConsumed: return "already_consumed";
    case PreflightFailure::StateMismatch: return "player_state_mismatch";
    case PreflightFailure::StateGenerationMismatch:
        return "player_state_generation_mismatch";
    case PreflightFailure::ThreadMismatch: return "thread_mismatch";
    case PreflightFailure::SelectorMismatch: return "selector_mismatch";
    case PreflightFailure::ContentEpochMismatch:
        return "content_epoch_mismatch";
    case PreflightFailure::CacheGenerationMismatch:
        return "cache_generation_mismatch";
    case PreflightFailure::OldDataSourceStillActive:
        return "old_data_source_still_active";
    case PreflightFailure::CurrentEpochSourcesIncomplete:
        return "current_epoch_sources_incomplete";
    case PreflightFailure::WrongLoadCaller: return "wrong_load_caller";
    case PreflightFailure::Shutdown: return "shutdown";
    case PreflightFailure::MutationScopeActive:
        return "mutation_scope_active";
    case PreflightFailure::RowUnreadable: return "row_unreadable";
    case PreflightFailure::CacheContainerUnreadable:
        return "cache_container_unreadable";
    case PreflightFailure::CacheVectorInvalid:
        return "cache_vector_invalid";
    case PreflightFailure::RecordMembershipMissing:
        return "record_membership_missing";
    case PreflightFailure::RecordMembershipAmbiguous:
        return "record_membership_ambiguous";
    case PreflightFailure::RecordTypeMismatch:
        return "record_type_mismatch";
    case PreflightFailure::RecordSelectorMismatch:
        return "record_selector_mismatch";
    case PreflightFailure::LiveStateMembershipIncomplete:
        return "live_state_membership_incomplete";
    case PreflightFailure::TraceEvidenceUnavailable:
        return "trace_evidence_unavailable";
    default: return "unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::playerpreset
