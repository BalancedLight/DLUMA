#include "NativeRetainedResourceOwner.h"
#include <cstring>
#include <limits>

namespace dluma::fixes::custom_map_asset_isolation::retained {
namespace {
bool Same(Identity a, Identity b) noexcept
{ return a.object == b.object && a.generation == b.generation && a.kind == b.kind; }
bool ValidIdentity(Identity a) noexcept
{ return a.object && a.generation && a.kind != Kind::Unknown; }
bool Terminated(const char* name) noexcept
{
    for (std::size_t i = 0; i < kNameCapacity; ++i) if (!name[i]) return true;
    return false;
}
bool SameInput(const Input& a, const Input& b) noexcept
{
    return a.file == b.file && a.source == b.source &&
        a.file_generation == b.file_generation && a.source_generation == b.source_generation &&
        a.content_bytes == b.content_bytes && a.epoch == b.epoch && a.realm == b.realm &&
        !std::memcmp(a.content_sha256, b.content_sha256, sizeof(a.content_sha256));
}
bool ValidInput(const Input& a) noexcept
{
    unsigned char bits = 0;
    for (const auto byte : a.content_sha256) bits |= byte;
    return a.file && a.source && a.file_generation && a.source_generation && bits &&
        ((a.realm == Realm::Global && !a.epoch) || (a.realm == Realm::Project && a.epoch));
}
const Record* Find(const Ledger& l, Identity id) noexcept
{
    for (std::size_t i = 0; i < l.count; ++i)
        if (l.records[i].life != Life::Empty && Same(l.records[i].identity, id)) return &l.records[i];
    return nullptr;
}
Record* Find(Ledger& l, Identity id) noexcept
{ return const_cast<Record*>(Find(static_cast<const Ledger&>(l), id)); }
const NativeState* Find(const Inventory& inv, std::uintptr_t object) noexcept
{
    for (std::size_t i = 0; i < inv.count; ++i)
        if (inv.objects[i].object == object) return &inv.objects[i];
    return nullptr;
}
bool ValidLedger(const Ledger& l) noexcept
{ return l.records && l.capacity && l.capacity <= kMaximumResources && l.count <= l.capacity; }
bool ValidFence(const Fence& f) noexcept
{
    return f.transaction && f.owner_generation && f.thread_id && f.epoch &&
        f.authoritative_post_destroy && f.producers_closed && f.readers_drained &&
        f.native_lock_owned && f.worker_completions_drained;
}
bool SameFence(const Fence& a, const Fence& b) noexcept
{
    return ValidFence(a) && ValidFence(b) && a.transaction == b.transaction &&
        a.owner_generation == b.owner_generation && a.mutation_revision == b.mutation_revision &&
        a.epoch == b.epoch && a.thread_id == b.thread_id;
}
bool SameBaselineState(const NativeState& a, const NativeState& b) noexcept
{
    // Reference counts may fall with natural level destruction. The object,
    // native input/definition, callback and backend identities may not change.
    return a.manager == b.manager && a.object == b.object && a.vtable == b.vtable &&
        a.definition == b.definition && a.backend == b.backend && a.deferred == b.deferred &&
        a.callback == b.callback && a.callback_context == b.callback_context && a.kind == b.kind &&
        a.flags == b.flags &&
        !std::memcmp(a.name, b.name, sizeof(a.name));
}
Result ValidInventory(const Inventory& inv) noexcept
{
    if (!inv.complete || !inv.manager || inv.kind == Kind::Unknown || !inv.objects ||
        !inv.capacity || inv.capacity > kMaximumResources || inv.count > inv.capacity)
        return Result::CoverageIncomplete;
    for (std::size_t i = 0; i < inv.count; ++i) {
        const auto& object = inv.objects[i];
        if (!object.object || !object.vtable || object.manager != inv.manager ||
            object.kind != inv.kind || !Terminated(object.name))
            return Result::CoverageIncomplete;
        for (std::size_t j = 0; j < i; ++j)
            if (inv.objects[j].object == object.object) return Result::AddressReused;
    }
    return Result::Succeeded;
}
Result Reconcile(const Ledger& ledger, const Inventory& inventory) noexcept
{
    if (!ValidLedger(ledger) || ledger.coverage_lost || !ledger.baseline_captured)
        return Result::CoverageIncomplete;
    const Result valid = ValidInventory(inventory);
    if (valid != Result::Succeeded) return valid;
    for (std::size_t i = 0; i < inventory.count; ++i) {
        Identity id = {};
        if (FindIdentity(&ledger, inventory.objects[i].object, inventory.kind, &id) != Result::Succeeded)
            return Result::UnknownObject;
        const Record* record = Find(ledger, id);
        if (!record || record->life != Life::Live) return Result::PendingDestruction;
        if (!record->baseline && !record->input_count) return Result::InvalidOrigin;
        if (record->baseline && (record->project || !SameBaselineState(
            record->baseline_state, inventory.objects[i]))) return Result::BaselineChanged;
    }
    for (std::size_t i = 0; i < ledger.count; ++i) {
        const Record& record = ledger.records[i];
        if (record.identity.kind != inventory.kind || record.life == Life::Empty) continue;
        const NativeState* current = Find(inventory, record.identity.object);
        if (record.baseline && record.baseline_state.manager == inventory.manager &&
            (record.life != Life::Live || !current)) return Result::BaselineChanged;
        if (record.project && record.life == Life::Destroying) return Result::PendingDestruction;
        if (record.project && record.life == Life::Live && !current) return Result::CoverageIncomplete;
        if (record.life == Life::Ended && current) {
            Identity active = {};
            if (FindIdentity(&ledger, record.identity.object, inventory.kind, &active) != Result::Succeeded ||
                Same(active, record.identity)) return Result::AddressReused;
        }
    }
    return Result::Succeeded;
}
Result Releasable(const Record& record, const NativeState& state) noexcept
{
    if (!record.project || record.baseline || record.global) return Result::BaselineChanged;
    if (state.references) return Result::NativeReferencesLive;
    if (state.deferred) return Result::DeferredResourceLive;
    if (state.callback || state.callback_context) return Result::CallbackRetained;
    return state.kind == Kind::Texture ? Result::Succeeded : Result::UnsupportedRelease;
}
}

void Initialize(Ledger* ledger, Record* records, std::size_t capacity) noexcept
{
    if (!ledger) return;
    *ledger = {};
    if (!records || !capacity || capacity > kMaximumResources) return;
    std::memset(records, 0, capacity * sizeof(*records));
    ledger->records = records; ledger->capacity = capacity;
}
void LoseCoverage(Ledger* ledger) noexcept
{ if (ledger) { ledger->coverage_lost = true; ++ledger->revision; } }
Result FindIdentity(const Ledger* ledger, std::uintptr_t object, Kind kind, Identity* out) noexcept
{
    if (out) *out = {};
    if (!ledger || !ValidLedger(*ledger) || !out || !object || kind == Kind::Unknown)
        return Result::InvalidArgument;
    for (std::size_t i = 0; i < ledger->count; ++i) {
        const Record& record = ledger->records[i];
        if ((record.life == Life::Live || record.life == Life::Destroying) &&
            record.identity.object == object && record.identity.kind == kind) {
            if (out->generation) return Result::AddressReused;
            *out = record.identity;
        }
    }
    return out->generation ? Result::Succeeded : Result::UnknownObject;
}
Result Constructed(Ledger* ledger, std::uintptr_t object, Kind kind, Identity* out) noexcept
{
    if (out) *out = {};
    if (!ledger || !ValidLedger(*ledger) || !object || kind == Kind::Unknown || !out)
        return Result::InvalidArgument;
    Identity existing = {};
    if (FindIdentity(ledger, object, kind, &existing) != Result::UnknownObject) {
        LoseCoverage(ledger); return Result::AddressReused;
    }
    std::size_t slot = 0;
    while (slot < ledger->count && ledger->records[slot].life != Life::Empty) ++slot;
    if (slot == ledger->capacity || ledger->next_generation == (std::numeric_limits<std::uint64_t>::max)()) {
        LoseCoverage(ledger); return Result::Capacity;
    }
    Record& record = ledger->records[slot]; record = {};
    record.identity = {object, ++ledger->next_generation, kind}; record.life = Life::Live;
    if (slot == ledger->count) ++ledger->count;
    ++ledger->revision; *out = record.identity; return Result::Succeeded;
}
Result RetainInput(Ledger* ledger, Identity id, const Input& input) noexcept
{
    if (!ledger || !ValidLedger(*ledger) || !ValidIdentity(id)) return Result::InvalidArgument;
    if (!ValidInput(input)) { LoseCoverage(ledger); return Result::InvalidOrigin; }
    Record* record = Find(*ledger, id);
    if (!record || record->life != Life::Live) { LoseCoverage(ledger); return Result::StaleGeneration; }
    for (std::size_t i = 0; i < record->input_count; ++i)
        if (SameInput(record->inputs[i], input)) return Result::Succeeded;
    if (record->input_count == kMaximumInputs) { LoseCoverage(ledger); return Result::Capacity; }
    record->inputs[record->input_count++] = input;
    record->project |= input.realm == Realm::Project;
    record->global |= input.realm == Realm::Global;
    ++ledger->revision; return Result::Succeeded;
}
Result BeginDestruction(Ledger* ledger, Identity id) noexcept
{
    if (!ledger || !ValidLedger(*ledger)) return Result::InvalidArgument;
    Record* record = Find(*ledger, id);
    if (!record || record->life != Life::Live) { LoseCoverage(ledger); return Result::StaleGeneration; }
    record->life = Life::Destroying; ++ledger->revision; return Result::Succeeded;
}
Result EndDestruction(Ledger* ledger, Identity id) noexcept
{
    if (!ledger || !ValidLedger(*ledger)) return Result::InvalidArgument;
    Record* record = Find(*ledger, id);
    if (!record || record->life != Life::Destroying) { LoseCoverage(ledger); return Result::StaleGeneration; }
    record->life = Life::Ended; ++ledger->revision; return Result::Succeeded;
}
Result CaptureBaseline(Ledger* ledger, const Inventory& inventory, std::uint64_t generation) noexcept
{
    if (!ledger || !ValidLedger(*ledger) || ledger->baseline_captured || !generation || ledger->coverage_lost)
        return Result::InvalidArgument;
    Result result = ValidInventory(inventory);
    if (result != Result::Succeeded) return result;
    // The clean-startup gate has no custom epoch or destruction transaction.
    if (inventory.fence.epoch || inventory.fence.transaction || !inventory.fence.owner_generation ||
        !inventory.fence.thread_id || !inventory.fence.producers_closed || !inventory.fence.readers_drained ||
        !inventory.fence.native_lock_owned || !inventory.fence.worker_completions_drained)
        return Result::FenceUnavailable;
    for (std::size_t i = 0; i < ledger->count; ++i)
        if (ledger->records[i].project) return Result::BaselineChanged;
    std::size_t missing = 0;
    for (std::size_t i = 0; i < inventory.count; ++i) {
        Identity id = {};
        result = FindIdentity(ledger, inventory.objects[i].object, inventory.kind, &id);
        if (result == Result::UnknownObject) ++missing;
        else if (result != Result::Succeeded) return result;
    }
    if (missing > ledger->capacity - ledger->count ||
        missing > (std::numeric_limits<std::uint64_t>::max)() - ledger->next_generation) return Result::Capacity;
    for (std::size_t i = 0; i < inventory.count; ++i) {
        Identity id = {};
        result = FindIdentity(ledger, inventory.objects[i].object, inventory.kind, &id);
        if (result == Result::UnknownObject)
            result = Constructed(ledger, inventory.objects[i].object, inventory.kind, &id);
        if (result != Result::Succeeded) return result;
        Record* record = Find(*ledger, id);
        if (record->life != Life::Live) return Result::PendingDestruction;
        record->baseline = true; record->global = true;
        record->baseline_state = inventory.objects[i];
    }
    ledger->baseline_generation = generation; ledger->baseline_captured = true;
    ++ledger->revision; return Result::Succeeded;
}
Result PreflightWithPackTextures(const Ledger& ledger,const Inventory& inventory,
    const NativeState* pack_textures,std::size_t count)noexcept
{
    if(count>kMaximumResources||(count&&!pack_textures)||!ValidFence(inventory.fence))return Result::FenceUnavailable;
    const auto reconciled=Reconcile(ledger,inventory);if(reconciled!=Result::Succeeded)return reconciled;
    for(std::size_t i=0;i<inventory.count;++i){const auto& state=inventory.objects[i];Identity id={};
        if(FindIdentity(&ledger,state.object,inventory.kind,&id)!=Result::Succeeded)return Result::UnknownObject;
        const auto* record=Find(ledger,id);if(!record||!record->project)continue;
        if(!state.deferred){const auto result=Releasable(*record,state);if(result!=Result::Succeeded)return result;continue;}
        if(record->baseline||record->global)return Result::BaselineChanged;
        if(state.references)return Result::NativeReferencesLive;
        if(state.callback||state.callback_context)return Result::CallbackRetained;
        bool matched=false;
        for(std::size_t j=0;j<count;++j)if(state.kind==Kind::Texture&&SameBaselineState(state,pack_textures[j])&&
            state.references==pack_textures[j].references)matched=true;
        if(!matched)return Result::DeferredResourceLive;
    }
    return Result::Succeeded;
}
Result Prepare(const Ledger& ledger, const Inventory& inventory, Plan* plan) noexcept
{
    if (!plan || !plan->resources || !plan->capacity || plan->capacity > kMaximumResources)
        return Result::InvalidArgument;
    plan->prepared = false; plan->completed = false; plan->mutation_started = false; plan->count = 0;
    if (!ValidFence(inventory.fence)) return Result::FenceUnavailable;
    Result result = Reconcile(ledger, inventory);
    if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < inventory.count; ++i) {
        Identity id = {};
        result = FindIdentity(&ledger, inventory.objects[i].object, inventory.kind, &id);
        if (result != Result::Succeeded) return result;
        const Record& record = *Find(ledger, id);
        if (!record.project) continue;
        result = Releasable(record, inventory.objects[i]);
        if (result != Result::Succeeded) return result;
        if (plan->count == plan->capacity) return Result::Capacity;
        plan->resources[plan->count++] = id;
    }
    plan->ledger_revision = ledger.revision; plan->baseline_generation = ledger.baseline_generation;
    plan->fence = inventory.fence; plan->manager = inventory.manager; plan->kind = inventory.kind;
    plan->prepared = true; return Result::Succeeded;
}
Result Execute(Ledger* ledger, const Operations& ops, Inventory* scratch, Plan* plan) noexcept
{
    if (!ledger || !scratch || !plan || !plan->prepared || plan->completed || !ops.capture ||
        !ops.fence || !ops.release || plan->ledger_revision != ledger->revision ||
        plan->baseline_generation != ledger->baseline_generation) return Result::StaleGeneration;
    Fence now = {};
    if (!ops.fence(ops.user, &now) || !SameFence(now, plan->fence)) return Result::FenceUnavailable;
    Result result = ops.capture(ops.user, scratch);
    if (result != Result::Succeeded) return result;
    if (!SameFence(scratch->fence, now) || scratch->manager != plan->manager || scratch->kind != plan->kind)
        return Result::InventoryChanged;
    result = Reconcile(*ledger, *scratch);
    if (result != Result::Succeeded) return result;
    // Revalidate the entire plan before the first mutation.
    for (std::size_t i = 0; i < plan->count; ++i) {
        const Record* record = Find(*ledger, plan->resources[i]);
        const NativeState* state = Find(*scratch, plan->resources[i].object);
        if (!record || !state || record->life != Life::Live) return Result::StaleGeneration;
        result = Releasable(*record, *state);
        if (result != Result::Succeeded) return result;
    }
    for (std::size_t i = 0; i < plan->count; ++i) {
        Fence before = {};
        if (!ops.fence(ops.user, &before) || !SameFence(before, now)) return Result::FenceUnavailable;
        const NativeState* state = Find(*scratch, plan->resources[i].object);
        if (!state) return Result::InventoryChanged;
        const Record* current_record = Find(*ledger, plan->resources[i]);
        if (!current_record || current_record->life != Life::Live) return Result::StaleGeneration;
        result = Releasable(*current_record, *state);
        if (result != Result::Succeeded) return result;
        const NativeState copied = *state;
        plan->mutation_started = true;
        result = ops.release(ops.user, copied);
        if (result != Result::Succeeded) return result;
        const Record* ended = Find(*ledger, plan->resources[i]);
        if (!ended || ended->life != Life::Ended) return Result::PendingDestruction;
        // Native destruction changes the mutation revision. Other authority
        // coordinates must remain the same across this exact original return.
        if (!ops.fence(ops.user, &now) || !ValidFence(now) || now.transaction != before.transaction ||
            now.owner_generation != before.owner_generation || now.thread_id != before.thread_id ||
            now.epoch != before.epoch || now.mutation_revision <= before.mutation_revision)
            return Result::FenceUnavailable;
        result = ops.capture(ops.user, scratch);
        if (result != Result::Succeeded) return result;
        if (!SameFence(now, scratch->fence) || Find(*scratch, copied.object))
            return Result::VerificationFailed;
        result = Reconcile(*ledger, *scratch);
        if (result != Result::Succeeded) return result;
    }
    plan->fence = now; plan->ledger_revision = ledger->revision;
    result = Verify(*ledger, *scratch, *plan);
    if (result == Result::Succeeded) {
        for (std::size_t i = 0; i < ledger->count; ++i) {
            Record& record = ledger->records[i];
            if (record.identity.kind == plan->kind && record.life == Life::Ended && record.project)
                record.verified_retirement_transaction = plan->fence.transaction;
        }
        ledger->verified_transaction = plan->fence.transaction;
        ledger->verified_revision = ledger->revision;
        plan->completed = true;
    }
    return result;
}
Result Verify(const Ledger& ledger, const Inventory& inv, const Plan& plan) noexcept
{
    if (!plan.prepared || plan.baseline_generation != ledger.baseline_generation ||
        plan.ledger_revision != ledger.revision || !SameFence(plan.fence, inv.fence) ||
        plan.manager != inv.manager || plan.kind != inv.kind) return Result::StaleGeneration;
    Result result = Reconcile(ledger, inv);
    if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < ledger.count; ++i) {
        const Record& record = ledger.records[i];
        if (record.identity.kind == inv.kind && record.project &&
            record.life != Life::Ended && record.life != Life::Empty) return Result::VerificationFailed;
    }
    return Result::Succeeded;
}
Result CanRetireSource(const Ledger& ledger, std::uintptr_t source, std::uint64_t generation,
                      std::uint64_t transaction) noexcept
{
    if (!ValidLedger(ledger) || !source || !generation || !transaction || ledger.coverage_lost || !ledger.baseline_captured)
        return Result::CoverageIncomplete;
    for (std::size_t i = 0; i < ledger.count; ++i) {
        const Record& record = ledger.records[i];
        for (std::size_t j = 0; j < record.input_count; ++j)
            if (record.inputs[j].source == source && record.inputs[j].source_generation == generation) {
                if (record.life != Life::Ended && record.life != Life::Empty) return Result::NativeReferencesLive;
                if (record.verified_retirement_transaction != transaction) return Result::VerificationFailed;
            }
    }
    if (ledger.verified_transaction != transaction || ledger.verified_revision != ledger.revision)
        return Result::VerificationFailed;
    return Result::Succeeded;
}
Result ReclaimEnded(Ledger* ledger, std::uintptr_t source, std::uint64_t generation,
                    std::uint64_t transaction) noexcept
{
    if (!ledger) return Result::InvalidArgument;
    Result result = CanRetireSource(*ledger, source, generation, transaction);
    if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < ledger->count; ++i) {
        Record& record = ledger->records[i];
        if (record.life != Life::Ended || record.baseline || record.global) continue;
        // One source receipt cannot retire another source's retained input.
        for (std::size_t j = record.input_count; j > 0; --j) {
            if (record.inputs[j - 1].source != source || record.inputs[j - 1].source_generation != generation) continue;
            for (std::size_t k = j; k < record.input_count; ++k) record.inputs[k - 1] = record.inputs[k];
            --record.input_count;
        }
        if (!record.input_count) record = {};
    }
    ++ledger->revision;
    ledger->verified_revision = ledger->revision;
    return Result::Succeeded;
}
const char* ResultName(Result result) noexcept
{
    switch (result) {
#define DLUMA_RETAINED_RESULT(name) case Result::name: return #name
    DLUMA_RETAINED_RESULT(Succeeded); DLUMA_RETAINED_RESULT(InvalidArgument); DLUMA_RETAINED_RESULT(Capacity);
    DLUMA_RETAINED_RESULT(CoverageIncomplete); DLUMA_RETAINED_RESULT(StaleGeneration); DLUMA_RETAINED_RESULT(AddressReused);
    DLUMA_RETAINED_RESULT(UnknownObject); DLUMA_RETAINED_RESULT(InvalidOrigin); DLUMA_RETAINED_RESULT(BaselineChanged);
    DLUMA_RETAINED_RESULT(FenceUnavailable); DLUMA_RETAINED_RESULT(InventoryChanged); DLUMA_RETAINED_RESULT(NativeReferencesLive);
    DLUMA_RETAINED_RESULT(DeferredResourceLive); DLUMA_RETAINED_RESULT(CallbackRetained); DLUMA_RETAINED_RESULT(UnsupportedRelease);
    DLUMA_RETAINED_RESULT(NativeReleaseFailed); DLUMA_RETAINED_RESULT(VerificationFailed); DLUMA_RETAINED_RESULT(PendingDestruction);
#undef DLUMA_RETAINED_RESULT
    }
    return "Unknown";
}
const char* KindName(Kind kind) noexcept
{
    switch (kind) {
#define DLUMA_RETAINED_KIND(name) case Kind::name: return #name
    DLUMA_RETAINED_KIND(Unknown); DLUMA_RETAINED_KIND(Mesh); DLUMA_RETAINED_KIND(Animation);
    DLUMA_RETAINED_KIND(FaceExpression); DLUMA_RETAINED_KIND(Cloth); DLUMA_RETAINED_KIND(Physics);
    DLUMA_RETAINED_KIND(MaterialPack); DLUMA_RETAINED_KIND(Material); DLUMA_RETAINED_KIND(Texture);
    DLUMA_RETAINED_KIND(Effect); DLUMA_RETAINED_KIND(Frontend);
#undef DLUMA_RETAINED_KIND
    }
    return "Unknown";
}
} // namespace
