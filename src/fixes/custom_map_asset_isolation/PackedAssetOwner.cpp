#include "PackedAssetOwner.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::packed {
namespace {
bool Valid(const Ledger& l) noexcept
{ return l.records && l.capacity && l.capacity <= kMaximumPacks && l.count <= l.capacity; }
bool Same(Key a, Key b) noexcept
{ return a.address == b.address && a.generation == b.generation && a.slot == b.slot; }
Origin* Record(Ledger& l, Key key) noexcept
{
    if (!key.address || !key.generation || key.slot >= l.count) return nullptr;
    Origin& r = l.records[key.slot]; return !r.empty && Same(r.key, key) ? &r : nullptr;
}
const Origin* Record(const Ledger& l, Key key) noexcept
{ return Record(const_cast<Ledger&>(l), key); }
bool Ended(const Origin& r) noexcept
{ return r.destructor_returned && (!r.registered || r.unregistered); }
const NativePack* Native(const Snapshot& s, std::uintptr_t pack) noexcept
{
    for (std::size_t i = 0; i < s.pack_count; ++i) if (s.packs[i].object == pack) return &s.packs[i];
    return nullptr;
}
bool TextureOnly(const NativePack& p) noexcept
{
    return !(p.resource_types[0] & ~((UINT64_C(1) << 32) | (UINT64_C(1) << 33) | (UINT64_C(1) << 34))) &&
        !p.resource_types[1] && !p.resource_types[2] && !p.resource_types[3] && !p.mesh_count;
}
bool QueueEmpty(const NativePack& p) noexcept
{ return !p.queued_entries[0] && !p.queued_entries[1] && !p.queued_entries[2] && !p.queued_entries[3]; }
bool FenceValid(const Fence& f) noexcept
{
    return f.transaction && f.epoch && f.owner_generation && f.thread_id && f.authoritative_post_destroy &&
        f.producers_closed && f.consumers_drained && f.renderer_lock_owned && f.worker_lock_owned;
}
bool SameAuthority(const Fence& a, const Fence& b) noexcept
{
    return FenceValid(a) && FenceValid(b) && a.transaction == b.transaction && a.epoch == b.epoch &&
        a.owner_generation == b.owner_generation && a.thread_id == b.thread_id;
}
Result Shape(const Snapshot& s) noexcept
{
    if (!s.complete || !s.runtime || !s.runtime_vtable || !s.packs || !s.textures ||
        !s.pack_capacity || s.pack_capacity > kMaximumPacks || s.pack_count > s.pack_capacity ||
        !s.texture_capacity || s.texture_capacity > kMaximumPackTextures || s.texture_count > s.texture_capacity)
        return Result::Incomplete;
    std::size_t textures = 0;
    for (std::size_t i = 0; i < s.pack_count; ++i) {
        const NativePack& pack = s.packs[i];
        if (!pack.object || pack.physical_group_count > kMaximumPhysicalGroups || pack.texture_begin != textures ||
            pack.texture_count > s.texture_count - textures) return Result::Incomplete;
        textures += pack.texture_count;
        for (std::size_t j = 0; j < i; ++j) if (s.packs[j].object == pack.object) return Result::StaleIdentity;
        for (std::size_t j = pack.texture_begin; j < textures; ++j) {
            const retained::NativeState& texture = s.textures[j];
            if (!texture.object || texture.kind != retained::Kind::Texture || texture.deferred != pack.object)
                return Result::Incomplete;
            for (std::size_t k = 0; k < j; ++k)
                if (s.textures[k].object == texture.object) return Result::StaleIdentity;
        }
    }
    return textures == s.texture_count ? Result::Succeeded : Result::Incomplete;
}
Result Reconcile(const Ledger& l, const Snapshot& s) noexcept
{
    if (!Valid(l) || l.lost || !l.baseline_captured) return Result::Incomplete;
    Result result = Shape(s); if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < s.pack_count; ++i) {
        Key key = {};
        if (FindActive(&l, s.packs[i].object, &key) != Result::Succeeded) return Result::UnknownPack;
        const Origin& r = *Record(l, key);
        if (r.destructor_entered || r.destructor_returned || r.unregistered) return Result::PendingNativeCall;
        if (r.pending_inputs) return Result::PendingInput;
        if (r.baseline && (r.project || r.baseline_metadata != s.packs[i].metadata)) return Result::BaselineChanged;
        if (!r.baseline && !r.input_count) return Result::PendingInput;
    }
    for (std::size_t i = 0; i < l.count; ++i) {
        const Origin& r = l.records[i]; if (r.empty) continue;
        if (r.unclassified_stream_epoch) return Result::UnclassifiedStreamOwner;
        if (r.pending_inputs) return Result::PendingInput;
        const NativePack* native = Native(s, r.key.address);
        if (r.baseline && (Ended(r) || !native || native->metadata != r.baseline_metadata)) return Result::BaselineChanged;
        if (!Ended(r) && !native) return Result::Incomplete;
        if (Ended(r) && native) {
            Key active = {};
            if (FindActive(&l, r.key.address, &active) != Result::Succeeded || Same(active, r.key)) return Result::StaleIdentity;
        }
    }
    return Result::Succeeded;
}
Result Eligible(const Origin& origin, const retained::Ledger& textures,
                const Snapshot& snapshot, const NativePack& pack) noexcept
{
    if (!origin.project || origin.baseline || origin.global) return Result::BaselineChanged;
    if (!origin.load_completed || !origin.registered || !pack.metadata || origin.pending_inputs)
        return Result::PendingNativeCall;
    if (pack.patch) return Result::UnsupportedPatch;
    // Texture registration can submit an independent render-device streaming
    // job retaining this pack. The four OnDemand lists do not close that owner.
    if ((pack.flags & 0x02000000U) && !pack.renderer_streams_closed) return Result::PendingNativeCall;
    if (!TextureOnly(pack)) return Result::UnsupportedResourceType;
    if (pack.references != pack.texture_count) return Result::NativeReferencesLive;
    for (std::size_t i = 0; i < pack.texture_count; ++i) {
        const auto& state = snapshot.textures[pack.texture_begin + i];
        if (state.references || state.callback || state.callback_context) return Result::NativeReferencesLive;
        retained::Identity identity = {};
        if (retained::FindIdentity(&textures, state.object, retained::Kind::Texture, &identity) != retained::Result::Succeeded)
            return Result::UnknownTexture;
        bool owned = false;
        for (std::size_t j = 0; j < textures.count; ++j) {
            const auto& r = textures.records[j];
            if (r.identity.object == identity.object && r.identity.generation == identity.generation)
                owned = r.life == retained::Life::Live && r.project && !r.baseline && !r.global;
        }
        if (!owned) return Result::BaselineChanged;
    }
    return textures.coverage_lost ? Result::Incomplete : Result::Succeeded;
}
bool InputValid(const retained::Input& in) noexcept
{
    unsigned bits = 0; for (auto value : in.content_sha256) bits |= value;
    return in.file && in.file_generation && in.source && in.source_generation && bits &&
        ((in.realm == retained::Realm::Project && in.epoch) || (in.realm == retained::Realm::Global && !in.epoch));
}
}
void Initialize(Ledger* l, Origin* records, std::size_t capacity) noexcept
{
    if (!l) return; *l = {};
    if (!records || !capacity || capacity > kMaximumPacks) return;
    std::memset(records, 0, sizeof(*records) * capacity); l->records = records; l->capacity = capacity;
    for (std::size_t i = 0; i < capacity; ++i) records[i].empty = true;
}
void LoseCoverage(Ledger* l) noexcept { if (l) { l->lost = true; ++l->revision; } }
Result FindActive(const Ledger* l, std::uintptr_t address, Key* out) noexcept
{
    if (out) *out = {};
    if (!l || !Valid(*l) || !address || !out) return Result::InvalidArgument;
    for (std::size_t i = 0; i < l->count; ++i) {
        const auto& r = l->records[i];
        if (!r.empty && r.key.address == address && !Ended(r)) {
            if (out->generation) return Result::StaleIdentity;
            *out = r.key;
        }
    }
    return out->generation ? Result::Succeeded : Result::UnknownPack;
}
Result Constructed(Ledger* l, std::uintptr_t address, Key* out) noexcept
{
    if (!l || !Valid(*l) || !out || !address) return Result::InvalidArgument; *out = {};
    Key active = {};
    if (FindActive(l, address, &active) != Result::UnknownPack) { LoseCoverage(l); return Result::StaleIdentity; }
    std::size_t slot = 0; while (slot < l->count && !l->records[slot].empty) ++slot;
    if (slot == l->capacity || l->next_generation == UINT64_MAX) { LoseCoverage(l); return Result::Capacity; }
    Origin& r = l->records[slot]; r = {}; r.key = {address, ++l->next_generation, static_cast<std::uint32_t>(slot)};
    if (slot == l->count) ++l->count; ++l->revision; *out = r.key; return Result::Succeeded;
}
Result RetainInput(Ledger* l, Key key, const retained::Input& input) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument;
    Origin* r = Record(*l, key);
    if (!r || (r->destructor_entered && !r->pending_inputs) || !InputValid(input)) { LoseCoverage(l); return Result::StaleIdentity; }
    for (std::size_t i = 0; i < r->input_count; ++i) {
        const auto& previous = r->inputs[i];
        if (previous.file == input.file && previous.file_generation == input.file_generation) {
            if (previous.source != input.source || previous.source_generation != input.source_generation ||
                previous.content_bytes != input.content_bytes || previous.realm != input.realm ||
                std::memcmp(previous.content_sha256, input.content_sha256, 32)) { LoseCoverage(l); return Result::StaleIdentity; }
            return Result::Succeeded;
        }
    }
    if (r->input_count == retained::kMaximumInputs) { LoseCoverage(l); return Result::Capacity; }
    r->inputs[r->input_count++] = input; r->project |= input.realm == retained::Realm::Project;
    // A global provider used to compile a new project asset is not itself a
    // global consumer. Global ownership is fixed at actual load completion.
    ++l->revision; return Result::Succeeded;
}
Result ObserveUnclassifiedStream(Ledger* l,Key key,std::uint32_t epoch)noexcept
{
    if(!l||!Valid(*l)||!epoch)return Result::InvalidArgument;auto* origin=Record(*l,key);
    if(!origin){LoseCoverage(l);return Result::StaleIdentity;}
    origin->unclassified_stream_epoch=epoch;++l->revision;return Result::Succeeded;
}
Result InputPending(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || r->destructor_entered || r->pending_inputs == UINT32_MAX) { LoseCoverage(l); return Result::StaleIdentity; }
    ++r->pending_inputs; ++l->revision; return Result::Succeeded;
}
Result InputCompleted(Ledger* l, Key key, const retained::Input& in) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || !r->pending_inputs) { LoseCoverage(l); return Result::StaleIdentity; }
    Result result = RetainInput(l, key, in); if (result != Result::Succeeded) return result;
    --r->pending_inputs;
    if (!r->pending_inputs && r->load_completed && r->input_count && !r->project) r->global = true;
    ++l->revision; return Result::Succeeded;
}
Result Registered(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || r->registered || r->destructor_entered) { LoseCoverage(l); return Result::StaleIdentity; }
    r->registered = true; ++l->revision; return Result::Succeeded;
}
Result Loaded(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || !r->registered || r->destructor_entered) { LoseCoverage(l); return Result::StaleIdentity; }
    r->load_completed = true;
    // A later project contribution to a previously global load must retain
    // this global classification and block selective destruction.
    if (r->input_count && !r->project && !r->pending_inputs) r->global = true;
    ++l->revision; return Result::Succeeded;
}
Result DestructorEntered(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || r->destructor_entered) { LoseCoverage(l); return Result::StaleIdentity; }
    r->destructor_entered = true; ++l->revision; return Result::Succeeded;
}
Result DestructorReturned(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || !r->destructor_entered || r->destructor_returned) { LoseCoverage(l); return Result::StaleIdentity; }
    r->destructor_returned = true; ++l->revision; return Result::Succeeded;
}
Result Unregistered(Ledger* l, Key key) noexcept
{
    if (!l || !Valid(*l)) return Result::InvalidArgument; Origin* r = Record(*l, key);
    if (!r || !r->registered || r->unregistered) { LoseCoverage(l); return Result::StaleIdentity; }
    r->unregistered = true; ++l->revision; return Result::Succeeded;
}
Result CaptureBaseline(Ledger* l, const Snapshot& s, std::uint64_t generation) noexcept
{
    if (!l || !Valid(*l) || l->baseline_captured || l->lost || !generation) return Result::InvalidArgument;
    Result result = Shape(s); if (result != Result::Succeeded) return result;
    if (s.fence.epoch || s.fence.transaction || !s.fence.owner_generation || !s.fence.thread_id ||
        !s.fence.producers_closed || !s.fence.consumers_drained || !s.fence.renderer_lock_owned ||
        !s.fence.worker_lock_owned) return Result::FenceUnavailable;
    for (std::size_t i = 0; i < l->count; ++i)
        if (!l->records[i].empty && (l->records[i].project || l->records[i].pending_inputs)) return Result::PendingInput;
    std::size_t available=l->capacity-l->count,missing=0;
    for(std::size_t i=0;i<l->count;++i)if(l->records[i].empty)++available;
    for(std::size_t i=0;i<s.pack_count;++i){
        Key key={};result=FindActive(l,s.packs[i].object,&key);
        if(result==Result::UnknownPack)++missing;
        else if(result!=Result::Succeeded)return result;
    }
    if(missing>available)return Result::Capacity;
    for (std::size_t i = 0; i < s.pack_count; ++i) {
        Key key = {}; result = FindActive(l, s.packs[i].object, &key);
        if (result == Result::UnknownPack) result = Constructed(l, s.packs[i].object, &key);
        if (result != Result::Succeeded) return result;
        Origin* r = Record(*l, key); r->baseline = r->global = true; r->load_completed = true;
        r->registered = s.packs[i].metadata != 0; r->baseline_metadata = s.packs[i].metadata;
    }
    l->baseline_generation = generation; l->baseline_captured = true; ++l->revision; return Result::Succeeded;
}
Result Prepare(const Ledger& l, const retained::Ledger& textures, const Snapshot& s, Plan* plan) noexcept
{
    if (!plan || !plan->items || !plan->capacity || plan->capacity > kMaximumPacks) return Result::InvalidArgument;
    plan->prepared = plan->mutation_started = plan->completed = false; plan->count = 0;
    if (!FenceValid(s.fence)) return Result::FenceUnavailable;
    Result result = Reconcile(l, s); if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < s.pack_count; ++i) {
        Key key = {}; (void)FindActive(&l, s.packs[i].object, &key);
        const Origin& origin = *Record(l, key); if (!origin.project) continue;
        result = Eligible(origin, textures, s, s.packs[i]); if (result != Result::Succeeded) return result;
        if (plan->count == plan->capacity) return Result::Capacity;
        plan->items[plan->count++] = {key, s.packs[i].references};
    }
    plan->fence = s.fence; plan->ledger_revision = l.revision; plan->baseline_generation = l.baseline_generation;
    plan->runtime = s.runtime; plan->prepared = true; return Result::Succeeded;
}
Result Reset(Ledger* l, retained::Ledger* textures, const Operations& ops, Snapshot* scratch, Plan* plan) noexcept
{
    if (!l || !textures || !scratch || !plan || !plan->prepared || plan->completed || !ops.capture ||
        !ops.evict || !ops.release || plan->ledger_revision != l->revision ||
        plan->baseline_generation != l->baseline_generation) return Result::StaleIdentity;
    Result result = ops.capture(ops.user, scratch); if (result != Result::Succeeded) return result;
    if (!SameAuthority(plan->fence, scratch->fence) || plan->fence.mutation_revision != scratch->fence.mutation_revision ||
        plan->runtime != scratch->runtime) return Result::FenceUnavailable;
    result = Reconcile(*l, *scratch); if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < plan->count; ++i) {
        const auto& item = plan->items[i]; const Origin* origin = Record(*l, item.pack);
        const NativePack* native = Native(*scratch, item.pack.address);
        if (!origin || !native || native->references != item.release_count) return Result::StaleIdentity;
        result = Eligible(*origin, *textures, *scratch, *native); if (result != Result::Succeeded) return result;
    }
    for (std::size_t i = 0; i < plan->count; ++i) {
        const PlanItem item = plan->items[i]; plan->mutation_started = true;
        result = ops.evict(ops.user, item.pack); if (result != Result::Succeeded) return result;
        result = ops.capture(ops.user, scratch); if (result != Result::Succeeded) return result;
        if (!SameAuthority(plan->fence, scratch->fence)) return Result::FenceUnavailable;
        const NativePack* current = Native(*scratch, item.pack.address);
        const Origin* origin = Record(*l, item.pack);
        if (!origin || !current || !QueueEmpty(*current) || current->references != item.release_count)
            return Result::QueueStillRetained;
        result = Eligible(*origin, *textures, *scratch, *current); if (result != Result::Succeeded) return result;
        // The native platform texture list contributes one owning reference per
        // texture. An empty registered project pack already has zero references.
        const std::uint32_t calls = item.release_count ? item.release_count : 1;
        for (std::uint32_t j = 0; j < calls; ++j) {
            result = ops.release(ops.user, item.pack, item.release_count == 0);
            if (result != Result::Succeeded) return result;
            result = ops.capture(ops.user, scratch); if (result != Result::Succeeded) return result;
            if (!SameAuthority(plan->fence, scratch->fence)) return Result::FenceUnavailable;
            current = Native(*scratch, item.pack.address);
            if (j + 1 < calls) {
                if (!current || current->references != item.release_count - j - 1 || !QueueEmpty(*current))
                    return Result::VerificationFailed;
            } else {
                origin = Record(*l, item.pack);
                if (!origin || !Ended(*origin) || current) return Result::VerificationFailed;
            }
        }
        result = Reconcile(*l, *scratch); if (result != Result::Succeeded) return result;
    }
    for (std::size_t i = 0; i < l->count; ++i) {
        Origin& origin = l->records[i]; if (origin.empty || !origin.project) continue;
        if (!Ended(origin)) return Result::VerificationFailed;
        origin.verified_transaction = plan->fence.transaction;
    }
    l->verified_transaction = plan->fence.transaction; l->verified_revision = l->revision;
    plan->fence = scratch->fence; plan->ledger_revision = l->revision; plan->completed = true;
    return Result::Succeeded;
}
Result CanRetireSource(const Ledger& l, std::uintptr_t source, std::uint64_t generation,
                      std::uint64_t transaction) noexcept
{
    if (!Valid(l) || l.lost || !l.baseline_captured || !source || !generation || !transaction ||
        l.verified_transaction != transaction || l.verified_revision != l.revision) return Result::Incomplete;
    for (std::size_t i = 0; i < l.count; ++i) {
        const Origin& r = l.records[i]; if (r.empty) continue;
        if (r.unclassified_stream_epoch) return Result::UnclassifiedStreamOwner;
        for (std::size_t j = 0; j < r.input_count; ++j)
            if (r.inputs[j].source == source && r.inputs[j].source_generation == generation &&
                (!Ended(r) || r.pending_inputs || r.verified_transaction != transaction)) return Result::NativeReferencesLive;
    }
    return Result::Succeeded;
}
Result ReclaimRetired(Ledger* l, std::uintptr_t source, std::uint64_t generation,
                     std::uint64_t transaction) noexcept
{
    if (!l) return Result::InvalidArgument;
    Result result = CanRetireSource(*l, source, generation, transaction); if (result != Result::Succeeded) return result;
    for (std::size_t i = 0; i < l->count; ++i) {
        Origin& r = l->records[i]; if (r.empty || !Ended(r) || r.baseline || r.global) continue;
        for (std::size_t j = r.input_count; j; --j) {
            if (r.inputs[j - 1].source != source || r.inputs[j - 1].source_generation != generation) continue;
            for (std::size_t k = j; k < r.input_count; ++k) r.inputs[k - 1] = r.inputs[k];
            --r.input_count;
        }
        bool project_input_left=false;
        for(std::size_t j=0;j<r.input_count;++j)project_input_left|=r.inputs[j].realm==retained::Realm::Project;
        // Global input bytes may have participated in compiling this ended
        // project pack. Its bookkeeping need not wait for global providers to
        // retire after every project input has independently retired.
        if (!project_input_left) { r = {}; r.empty = true; }
    }
    ++l->revision; l->verified_revision = l->revision; return Result::Succeeded;
}
const char* ResultName(Result result) noexcept
{
    switch (result) {
#define DLUMA_PACK_RESULT(n) case Result::n: return #n
    DLUMA_PACK_RESULT(Succeeded); DLUMA_PACK_RESULT(InvalidArgument); DLUMA_PACK_RESULT(Capacity);
    DLUMA_PACK_RESULT(Incomplete); DLUMA_PACK_RESULT(UnknownPack); DLUMA_PACK_RESULT(UnknownTexture);
    DLUMA_PACK_RESULT(StaleIdentity); DLUMA_PACK_RESULT(BaselineChanged); DLUMA_PACK_RESULT(NativeReferencesLive);
    DLUMA_PACK_RESULT(UnsupportedResourceType); DLUMA_PACK_RESULT(UnsupportedPatch); DLUMA_PACK_RESULT(PendingInput);
    DLUMA_PACK_RESULT(PendingNativeCall); DLUMA_PACK_RESULT(FenceUnavailable); DLUMA_PACK_RESULT(NativeFailure);
    DLUMA_PACK_RESULT(VerificationFailed); DLUMA_PACK_RESULT(QueueStillRetained); DLUMA_PACK_RESULT(UnclassifiedStreamOwner);
#undef DLUMA_PACK_RESULT
    }
    return "Unknown";
}
} // namespace
