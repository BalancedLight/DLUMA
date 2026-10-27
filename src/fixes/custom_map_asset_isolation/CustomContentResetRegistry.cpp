#define _HAS_EXCEPTIONS 0

#include "CustomContentResetRegistry.h"

#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::content {
namespace {

class RegistryGuard {
public:
    explicit RegistryGuard(CustomContentResetRegistry* registry) noexcept
        : registry_(registry), locked_(false)
    {
        if (registry_ &&
            dluma_atomic_compare_exchange(&registry_->lock, 1, 0) == 0)
            locked_ = true;
    }

    ~RegistryGuard()
    {
        if (locked_) dluma_atomic_exchange(&registry_->lock, 0);
    }

    bool locked() const noexcept { return locked_; }

private:
    CustomContentResetRegistry* registry_;
    bool locked_;
};

void CopyText(char* output, std::size_t capacity,
              const char* input) noexcept
{
    if (!output || !capacity) return;
    std::size_t index = 0;
    if (input) {
        while (index + 1 < capacity && input[index]) {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = 0;
}

ContentResetResult Combine(ContentResetResult aggregate,
                           ContentResetResult current) noexcept
{
    if (current == ContentResetResult::Failed ||
        aggregate == ContentResetResult::Failed)
        return ContentResetResult::Failed;
    if (current == ContentResetResult::Partial ||
        aggregate == ContentResetResult::Partial)
        return ContentResetResult::Partial;
    if (current == ContentResetResult::Unsupported ||
        aggregate == ContentResetResult::Unsupported)
        return ContentResetResult::Unsupported;
    if (current == ContentResetResult::Restored ||
        aggregate == ContentResetResult::Restored)
        return ContentResetResult::Restored;
    return ContentResetResult::NotApplicable;
}

std::uint32_t CountMatches(const CustomContentResetAdapter& adapter,
                           const ContentResetContext* context) noexcept
{
    if (!context || !context->records || !context->record_count ||
        !adapter.supports)
        return 0;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < context->record_count; ++index)
        if (adapter.supports(adapter.user, &context->records[index]))
            ++count;
    return count;
}

bool StoreAdapterState(CustomContentResetRegistry* registry,
                       std::uint32_t index, std::uint32_t adapter_id,
                       const ContentResetAdapterState& state) noexcept
{
    RegistryGuard guard(registry);
    if (!guard.locked() || index >= registry->count ||
        registry->adapters[index].id != adapter_id)
        return false;
    registry->states[index] = state;
    return true;
}

ContentResetResult InvokePhase(CustomContentResetRegistry* registry,
                               const ContentResetContext* context,
                               ContentResetPhase phase) noexcept
{
    CustomContentResetAdapter adapters[kMaximumResetAdapters];
    ContentResetAdapterState states[kMaximumResetAdapters];
    std::uint32_t count = 0;
    {
        RegistryGuard guard(registry);
        if (!guard.locked() || !registry->sealed)
            return ContentResetResult::Failed;
        count = registry->count;
        std::memcpy(adapters, registry->adapters,
                    sizeof(CustomContentResetAdapter) * count);
        std::memcpy(states, registry->states,
                    sizeof(ContentResetAdapterState) * count);
    }
    /* Resolve ownership for the complete context before invoking any phase
     * callback.  Two adapters claiming the same record is an ownership
     * ambiguity, not permission to run both destructive owners in registry
     * order. */
    bool ambiguous_adapters[kMaximumResetAdapters] = {};
    if (phase != ContentResetPhase::BaselineCapture && context &&
        context->records) {
        for (std::uint32_t record_index = 0;
             record_index < context->record_count; ++record_index) {
            std::uint32_t matching_indices[kMaximumResetAdapters] = {};
            std::uint32_t matching_count = 0;
            for (std::uint32_t adapter_index = 0;
                 adapter_index < count; ++adapter_index) {
                const CustomContentResetAdapter& adapter =
                    adapters[adapter_index];
                if (!adapter.enabled ||
                    !states[adapter_index].definition_valid ||
                    !adapter.supports ||
                    !adapter.supports(
                        adapter.user,
                        &context->records[record_index]))
                    continue;
                matching_indices[matching_count++] = adapter_index;
            }
            if (matching_count > 1) {
                for (std::uint32_t match = 0; match < matching_count;
                     ++match)
                    ambiguous_adapters[matching_indices[match]] = true;
            }
        }
    }
    ContentResetResult aggregate = ContentResetResult::NotApplicable;
    for (std::uint32_t index = 0; index < count; ++index) {
        CustomContentResetAdapter& adapter = adapters[index];
        ContentResetAdapterState& state = states[index];
        if (!adapter.enabled || !state.definition_valid) {
            aggregate = Combine(aggregate,
                                ContentResetResult::Unsupported);
            continue;
        }

        if (phase != ContentResetPhase::BaselineCapture) {
            state.matched_records = CountMatches(adapter, context);
            if (!state.matched_records) continue;
            if (ambiguous_adapters[index]) {
                if (phase == ContentResetPhase::Prepare)
                    state.prepare_result =
                        ContentResetResult::Unsupported;
                else if (phase == ContentResetPhase::Reset)
                    state.reset_result =
                        ContentResetResult::Unsupported;
                else if (phase == ContentResetPhase::Verify)
                    state.verify_result =
                        ContentResetResult::Unsupported;
                if (!StoreAdapterState(registry, index, adapter.id,
                                       state))
                    return ContentResetResult::Failed;
                aggregate = Combine(aggregate,
                                    ContentResetResult::Unsupported);
                continue;
            }
            const bool prerequisite_satisfied =
                phase == ContentResetPhase::Prepare
                    ? state.baseline_captured
                : phase == ContentResetPhase::Reset
                    ? state.prepare_result == ContentResetResult::Restored
                : phase == ContentResetPhase::Verify
                    ? state.reset_result == ContentResetResult::Restored
                    : true;
            if (!prerequisite_satisfied) {
                if (phase == ContentResetPhase::Prepare)
                    state.prepare_result =
                        ContentResetResult::Unsupported;
                else if (phase == ContentResetPhase::Reset)
                    state.reset_result =
                        ContentResetResult::Unsupported;
                else if (phase == ContentResetPhase::Verify)
                    state.verify_result =
                        ContentResetResult::Unsupported;
                if (!StoreAdapterState(registry, index, adapter.id,
                                       state))
                    return ContentResetResult::Failed;
                aggregate = Combine(aggregate,
                                    ContentResetResult::Unsupported);
                continue;
            }
            if (!adapter.destructive_owner_validated &&
                (phase == ContentResetPhase::Prepare ||
                 phase == ContentResetPhase::Reset)) {
                if (phase == ContentResetPhase::Prepare)
                    state.prepare_result =
                        ContentResetResult::Unsupported;
                else
                    state.reset_result =
                        ContentResetResult::Unsupported;
                if (!StoreAdapterState(registry, index, adapter.id,
                                       state))
                    return ContentResetResult::Failed;
                aggregate = Combine(aggregate,
                                    ContentResetResult::Unsupported);
                continue;
            }
        }

        ContentResetPhaseFn function = nullptr;
        switch (phase) {
        case ContentResetPhase::BaselineCapture:
            function = adapter.capture_baseline;
            break;
        case ContentResetPhase::Prepare:
            function = adapter.prepare_for_reset;
            break;
        case ContentResetPhase::Reset:
            function = adapter.reset_to_baseline;
            break;
        case ContentResetPhase::Verify:
            function = adapter.verify_baseline;
            break;
        }
        const ContentResetResult result = function
            ? function(adapter.user, context)
            : ContentResetResult::Unsupported;
        switch (phase) {
        case ContentResetPhase::BaselineCapture:
            state.capture_result = result;
            state.baseline_captured = result == ContentResetResult::Restored;
            break;
        case ContentResetPhase::Prepare: state.prepare_result = result; break;
        case ContentResetPhase::Reset: state.reset_result = result; break;
        case ContentResetPhase::Verify: state.verify_result = result; break;
        }
        if (!StoreAdapterState(registry, index, adapter.id, state))
            return ContentResetResult::Failed;
        aggregate = Combine(aggregate, result);
    }
    return aggregate;
}

}  // namespace

void Initialize(CustomContentResetRegistry* registry) noexcept
{
    if (registry) std::memset(registry, 0, sizeof(*registry));
}

bool ValidateAdapterDefinition(
    const CustomContentResetAdapter* adapter) noexcept
{
    if (!adapter || !adapter->id || !adapter->name[0] ||
        !adapter->supports || !adapter->capture_baseline ||
        !adapter->reset_to_baseline || !adapter->verify_baseline)
        return false;
    switch (adapter->semantics) {
    case ContentResetSemantics::Replace:
        return true;
    case ContentResetSemantics::Merge:
        return adapter->authoritative_clear_available;
    case ContentResetSemantics::OneShot:
        return adapter->authoritative_reinitialize_available;
    case ContentResetSemantics::Snapshot:
        return adapter->logical_snapshot_available;
    default:
        return false;
    }
}

bool RegisterAdapter(CustomContentResetRegistry* registry,
                     const CustomContentResetAdapter* adapter) noexcept
{
    RegistryGuard guard(registry);
    if (!guard.locked() || registry->sealed || !adapter ||
        registry->count >= kMaximumResetAdapters)
        return false;
    for (std::uint32_t index = 0; index < registry->count; ++index)
        if (registry->adapters[index].id == adapter->id)
            return false;
    const std::uint32_t index = registry->count++;
    registry->adapters[index] = *adapter;
    ContentResetAdapterState* state = &registry->states[index];
    std::memset(state, 0, sizeof(*state));
    state->id = adapter->id;
    state->semantics = adapter->semantics;
    state->enabled = adapter->enabled;
    state->definition_valid = ValidateAdapterDefinition(adapter);
    state->capture_result = ContentResetResult::NotApplicable;
    state->prepare_result = ContentResetResult::NotApplicable;
    state->reset_result = ContentResetResult::NotApplicable;
    state->verify_result = ContentResetResult::NotApplicable;
    CopyText(state->name, sizeof(state->name), adapter->name);
    return state->definition_valid;
}

void SealRegistry(CustomContentResetRegistry* registry) noexcept
{
    RegistryGuard guard(registry);
    if (guard.locked()) registry->sealed = true;
}

ContentResetResult CaptureBaselines(
    CustomContentResetRegistry* registry) noexcept
{
    const ContentResetContext empty = {};
    return InvokePhase(registry, &empty,
                       ContentResetPhase::BaselineCapture);
}

ContentResetResult PrepareForReset(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept
{
    return InvokePhase(registry, context, ContentResetPhase::Prepare);
}

ContentResetResult ResetToBaseline(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept
{
    return InvokePhase(registry, context, ContentResetPhase::Reset);
}

ContentResetResult VerifyBaseline(
    CustomContentResetRegistry* registry,
    const ContentResetContext* context) noexcept
{
    return InvokePhase(registry, context, ContentResetPhase::Verify);
}

bool FindSupportingAdapter(
    CustomContentResetRegistry* registry,
    const CustomFileLoadRecord* record,
    std::uint32_t* adapter_id) noexcept
{
    if (adapter_id) *adapter_id = 0;
    if (!registry || !record) return false;
    CustomContentResetAdapter adapters[kMaximumResetAdapters];
    std::uint32_t count = 0;
    {
        RegistryGuard guard(registry);
        if (!guard.locked()) return false;
        count = registry->count;
        std::memcpy(adapters, registry->adapters,
                    sizeof(CustomContentResetAdapter) * count);
    }
    std::uint32_t found = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const CustomContentResetAdapter& adapter =
            adapters[index];
        if (!adapter.enabled || !adapter.supports ||
            !adapter.supports(adapter.user, record))
            continue;
        if (found) return false;
        found = adapter.id;
    }
    if (adapter_id) *adapter_id = found;
    return found != 0;
}

std::size_t SnapshotAdapterStates(
    CustomContentResetRegistry* registry,
    ContentResetAdapterState* output,
    std::size_t capacity) noexcept
{
    if (!output || !capacity) return 0;
    RegistryGuard guard(registry);
    if (!guard.locked()) return 0;
    const std::size_t count = registry->count < capacity
        ? registry->count : capacity;
    std::memcpy(output, registry->states,
                sizeof(ContentResetAdapterState) * count);
    return count;
}

const char* ResetResultName(ContentResetResult result) noexcept
{
    switch (result) {
    case ContentResetResult::NotApplicable: return "NotApplicable";
    case ContentResetResult::Restored: return "Restored";
    case ContentResetResult::Partial: return "Partial";
    case ContentResetResult::Unsupported: return "Unsupported";
    case ContentResetResult::Failed: return "Failed";
    default: return "Unknown";
    }
}

const char* ResetSemanticsName(ContentResetSemantics semantics) noexcept
{
    switch (semantics) {
    case ContentResetSemantics::Replace: return "Replace";
    case ContentResetSemantics::Merge: return "Merge";
    case ContentResetSemantics::OneShot: return "OneShot";
    case ContentResetSemantics::Snapshot: return "Snapshot";
    default: return "Unknown";
    }
}

}  // namespace dluma::fixes::custom_map_asset_isolation::content
