#include "fixes/custom_map_asset_isolation/NativeRetainedResourceOwner.h"
#include "fixes/custom_map_asset_isolation/NativeRetainedResourceWindows.h"
#include <cstdio>
#include <cstring>

namespace r = dluma::fixes::custom_map_asset_isolation::retained;
namespace w = r::windows;
#define REQUIRE(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); return false; } } while (0)

struct Fixture {
    r::Record records[12] = {};
    r::Ledger ledger = {};
    r::NativeState native[12] = {}, scratch_objects[12] = {};
    r::Inventory inventory = {}, scratch = {};
    r::Identity plan_items[12] = {};
    r::Plan plan = {};
    r::Fence current = {};
    unsigned releases = 0;
    bool skip_end = false, skip_removal = false, change_next_reference = false;
    Fixture() {
        r::Initialize(&ledger, records, 12);
        current.owner_generation = 10; current.mutation_revision = 1; current.thread_id = 7;
        current.producers_closed = current.readers_drained = current.native_lock_owned = current.worker_completions_drained = true;
        inventory.objects = native; inventory.capacity = 12; inventory.manager = 0x2000;
        inventory.kind = r::Kind::Texture; inventory.complete = true; inventory.fence = current;
        scratch.objects = scratch_objects; scratch.capacity = 12;
        plan.resources = plan_items; plan.capacity = 12;
        native[0] = Make(0x3000); native[0].references = 4;
        inventory.count = 1;
    }
    r::NativeState Make(std::uintptr_t address) const {
        r::NativeState state = {}; state.manager = inventory.manager; state.object = address;
        state.vtable = 0x4000; state.kind = r::Kind::Texture;
        std::snprintf(state.name, sizeof(state.name), "asset_%zu.dds", static_cast<std::size_t>(address));
        return state;
    }
    bool Baseline() {
        if (r::CaptureBaseline(&ledger, inventory, 1) != r::Result::Succeeded) return false;
        current.transaction = 10; current.epoch = 2; current.authoritative_post_destroy = true;
        inventory.fence = current; return true;
    }
    r::Identity Add(std::uintptr_t object, std::uintptr_t source = 0x8000, std::uint64_t generation = 1) {
        native[inventory.count++] = Make(object);
        r::Identity identity = {};
        (void)r::Constructed(&ledger, object, r::Kind::Texture, &identity);
        (void)r::RetainInput(&ledger, identity, Input(source, generation)); return identity;
    }
    static r::Input Input(std::uintptr_t source, std::uint64_t generation) {
        r::Input input = {}; input.file = 0x7000; input.file_generation = 3;
        input.source = source; input.source_generation = generation; input.content_bytes = 128;
        input.content_sha256[0] = 0x45; input.epoch = 2; input.realm = r::Realm::Project; return input;
    }
    void Remove(std::uintptr_t object) {
        for (std::size_t i = 0; i < inventory.count; ++i) if (native[i].object == object) {
            for (std::size_t j = i + 1; j < inventory.count; ++j) native[j - 1] = native[j];
            --inventory.count; return;
        }
    }
    static bool Fence(void* user, r::Fence* out) noexcept {
        *out = static_cast<Fixture*>(user)->current; return true;
    }
    static r::Result Capture(void* user, r::Inventory* out) noexcept {
        auto& f = *static_cast<Fixture*>(user);
        std::memcpy(out->objects, f.native, sizeof(f.native)); out->count = f.inventory.count;
        out->manager = f.inventory.manager; out->kind = f.inventory.kind;
        out->fence = f.current; out->complete = true; return r::Result::Succeeded;
    }
    static r::Result Release(void* user, const r::NativeState& state) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.releases;
        r::Identity identity = {};
        if (r::FindIdentity(&f.ledger, state.object, state.kind, &identity) != r::Result::Succeeded)
            return r::Result::UnknownObject;
        if (r::BeginDestruction(&f.ledger, identity) != r::Result::Succeeded) return r::Result::StaleGeneration;
        if (!f.skip_removal) f.Remove(state.object);
        if (!f.skip_end) (void)r::EndDestruction(&f.ledger, identity);
        ++f.current.mutation_revision;
        if (f.change_next_reference && f.inventory.count > 1) f.native[1].references = 1;
        return r::Result::Succeeded;
    }
    r::Result Prepare() { inventory.fence = current; return r::Prepare(ledger, inventory, &plan); }
    r::Result Execute() { return r::Execute(&ledger, {this, Capture, Fence, Release}, &scratch, &plan); }
};

bool Success() {
    Fixture f; REQUIRE(f.Baseline()); const auto first = f.Add(0x5000); f.Add(0x5100);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::NativeReferencesLive);
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.plan.count == 2);
    REQUIRE(f.Execute() == r::Result::Succeeded); REQUIRE(f.plan.completed); REQUIRE(f.releases == 2);
    REQUIRE(f.inventory.count == 1 && f.native[0].object == 0x3000);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::Succeeded);
    REQUIRE(r::ReclaimEnded(&f.ledger, 0x8000, 1, f.current.transaction) == r::Result::Succeeded);
    const auto again = f.Add(0x5000);
    REQUIRE(first.generation != again.generation); return true;
}
bool Refs() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.native[1].references = 1;
    REQUIRE(f.Prepare() == r::Result::NativeReferencesLive); REQUIRE(!f.releases); return true; }
bool Pack() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.native[1].deferred = 0x9000;
    REQUIRE(f.Prepare() == r::Result::DeferredResourceLive); return true; }
bool Callback() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.native[1].callback = 0x9000;
    REQUIRE(f.Prepare() == r::Result::CallbackRetained); return true; }
bool Unknown() { Fixture f; REQUIRE(f.Baseline()); f.native[f.inventory.count++] = f.Make(0x5000);
    REQUIRE(f.Prepare() == r::Result::UnknownObject); return true; }
bool MissingOrigin() { Fixture f; REQUIRE(f.Baseline()); f.native[f.inventory.count++] = f.Make(0x5000);
    r::Identity id = {}; REQUIRE(r::Constructed(&f.ledger, 0x5000, r::Kind::Texture, &id) == r::Result::Succeeded);
    REQUIRE(f.Prepare() == r::Result::InvalidOrigin); return true; }
bool BaselineInput() { Fixture f; REQUIRE(f.Baseline()); r::Identity id = {};
    REQUIRE(r::FindIdentity(&f.ledger, 0x3000, r::Kind::Texture, &id) == r::Result::Succeeded);
    REQUIRE(r::RetainInput(&f.ledger, id, Fixture::Input(0x8000, 1)) == r::Result::Succeeded);
    REQUIRE(f.Prepare() == r::Result::BaselineChanged); return true; }
bool BaselineMutation() { Fixture f; REQUIRE(f.Baseline()); f.native[0].backend = 0x9000;
    REQUIRE(f.Prepare() == r::Result::BaselineChanged); return true; }
bool GlobalMixed() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000);
    auto input = Fixture::Input(0x9000, 1); input.realm = r::Realm::Global; input.epoch = 0;
    REQUIRE(r::RetainInput(&f.ledger, id, input) == r::Result::Succeeded);
    REQUIRE(f.Prepare() == r::Result::BaselineChanged); return true; }
bool MissingNative() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.Remove(0x5000);
    REQUIRE(f.Prepare() == r::Result::CoverageIncomplete); return true; }
bool NaturalDestruction() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000);
    REQUIRE(r::BeginDestruction(&f.ledger, id) == r::Result::Succeeded); f.Remove(0x5000);
    REQUIRE(r::EndDestruction(&f.ledger, id) == r::Result::Succeeded);
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.plan.count == 0);
    REQUIRE(f.Execute() == r::Result::Succeeded); REQUIRE(f.releases == 0); return true; }
bool MissingDestructorReturn() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.skip_end = true;
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::PendingDestruction);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::NativeReferencesLive); return true; }
bool FailedNativeUnlink() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.skip_removal = true;
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::VerificationFailed);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::VerificationFailed); return true; }
bool LateReferences() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); f.Add(0x5100); f.change_next_reference = true;
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::NativeReferencesLive);
    REQUIRE(f.releases == 1 && f.plan.mutation_started && !f.plan.completed); return true; }
bool StalePlan() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000); REQUIRE(f.Prepare() == r::Result::Succeeded);
    ++f.current.transaction; REQUIRE(f.Execute() == r::Result::FenceUnavailable); REQUIRE(!f.releases); return true; }
bool StaleRetirement() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000);
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::Succeeded);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction + 1) == r::Result::VerificationFailed);
    REQUIRE(r::ReclaimEnded(&f.ledger, 0x8000, 1, f.current.transaction + 1) == r::Result::VerificationFailed);
    REQUIRE(f.records[1].life == r::Life::Ended); return true; }
bool SourceRemovalBeforeVerify() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000);
    REQUIRE(r::BeginDestruction(&f.ledger, id) == r::Result::Succeeded); f.Remove(0x5000);
    REQUIRE(r::EndDestruction(&f.ledger, id) == r::Result::Succeeded);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::VerificationFailed); return true; }
bool ProducerAfterVerified() { Fixture f; REQUIRE(f.Baseline()); f.Add(0x5000);
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::Succeeded);
    r::Identity pending = {};
    REQUIRE(r::Constructed(&f.ledger, 0x5100, r::Kind::Texture, &pending) == r::Result::Succeeded);
    REQUIRE(r::CanRetireSource(f.ledger, 0x8000, 1, f.current.transaction) == r::Result::VerificationFailed);
    return true; }
bool MultipleSources() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000);
    REQUIRE(r::RetainInput(&f.ledger, id, Fixture::Input(0x9000, 2)) == r::Result::Succeeded);
    REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::Succeeded);
    REQUIRE(r::ReclaimEnded(&f.ledger, 0x8000, 1, f.current.transaction) == r::Result::Succeeded);
    REQUIRE(f.records[1].life == r::Life::Ended && f.records[1].input_count == 1);
    REQUIRE(r::ReclaimEnded(&f.ledger, 0x9000, 2, f.current.transaction) == r::Result::Succeeded);
    REQUIRE(f.records[1].life == r::Life::Empty); return true; }
bool UnmatchedGeneration() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000); ++id.generation;
    REQUIRE(r::BeginDestruction(&f.ledger, id) == r::Result::StaleGeneration);
    REQUIRE(f.Prepare() == r::Result::CoverageIncomplete); return true; }
bool BoundedLoss() { Fixture f; REQUIRE(f.Baseline()); auto id = f.Add(0x5000);
    for (std::size_t i = 1; i < r::kMaximumInputs; ++i)
        REQUIRE(r::RetainInput(&f.ledger, id, Fixture::Input(0x8000 + i, 1)) == r::Result::Succeeded);
    REQUIRE(r::RetainInput(&f.ledger, id, Fixture::Input(0x9900, 1)) == r::Result::Capacity);
    REQUIRE(f.Prepare() == r::Result::CoverageIncomplete); return true; }
bool GateRequired() { w::Binding binding = {}; r::NativeState objects[2] = {}; r::Inventory out = {};
    out.objects = objects; out.capacity = 2; binding.validated = true;
    REQUIRE(w::CaptureTextures(&binding, &out) == r::Result::FenceUnavailable);
    REQUIRE(!out.complete); return true; }
bool Cycles() { Fixture f; REQUIRE(f.Baseline());
    for (unsigned i = 0; i < 40; ++i) {
        f.current.transaction = 100 + i; f.current.epoch = 10 + i;
        f.Add(0x5000, 0x8000, i + 1);
        REQUIRE(f.Prepare() == r::Result::Succeeded); REQUIRE(f.Execute() == r::Result::Succeeded);
        REQUIRE(r::ReclaimEnded(&f.ledger, 0x8000, i + 1, f.current.transaction) == r::Result::Succeeded);
        REQUIRE(f.ledger.count <= 2 && f.inventory.count == 1);
    }
    return true;
}
int main() {
    struct Test { const char* name; bool (*run)(); };
    const Test tests[] = {{"targeted native owner reset",Success},{"native references",Refs},{"retained asset pack",Pack},
        {"callbacks",Callback},{"untracked inventory",Unknown},{"missing opened-input identity",MissingOrigin},
        {"baseline input override",BaselineInput},{"baseline mutation",BaselineMutation},{"global shared resource",GlobalMixed},
        {"disappearance is not release",MissingNative},{"natural destructor return",NaturalDestruction},
        {"unfinished destructor",MissingDestructorReturn},{"failed unlink",FailedNativeUnlink},
        {"late native reference",LateReferences},{"stale transaction",StalePlan},{"multi-source retirement",MultipleSources},
        {"stale retirement receipt",StaleRetirement},{"source retirement before owner verify",SourceRemovalBeforeVerify},
        {"producer after verification invalidates receipt",ProducerAfterVerified},
        {"stale object generation",UnmatchedGeneration},{"bounded coverage loss",BoundedLoss},
        {"actual native gate required",GateRequired},{"forty reusable source epochs",Cycles},
        {"full preflight accepts exact pack textures",+[]() -> bool {Fixture f;REQUIRE(f.Baseline());f.Add(0x5000);f.native[1].deferred=0xA000;
            REQUIRE(r::PreflightWithPackTextures(f.ledger,f.inventory,&f.native[1],1)==r::Result::Succeeded);REQUIRE(!f.releases);return true;}},
        {"full preflight rejects unmatched pack owner",+[]() -> bool {Fixture f;REQUIRE(f.Baseline());f.Add(0x5000);f.native[1].deferred=0xA000;
            auto other=f.native[1];other.deferred=0xB000;REQUIRE(r::PreflightWithPackTextures(f.ledger,f.inventory,&other,1)==r::Result::DeferredResourceLive);return true;}},
        {"full preflight checks live loose borrower",+[]() -> bool {Fixture f;REQUIRE(f.Baseline());f.Add(0x5000);f.native[1].references=1;
            REQUIRE(r::PreflightWithPackTextures(f.ledger,f.inventory,nullptr,0)==r::Result::NativeReferencesLive);return true;}}};
    for (const auto& test : tests) { if (!test.run()) { std::printf("FAILED: %s\n", test.name); return 1; } }
    std::printf("PASS %zu retained-resource scenarios\n", sizeof(tests) / sizeof(tests[0])); return 0;
}
