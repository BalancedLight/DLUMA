#include "../src/fixes/custom_map_asset_isolation/AudioSoundPropertySnapshot.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace audio = dluma::fixes::custom_map_asset_isolation::audio_sound_properties;
namespace {
constexpr std::uintptr_t base = 0x10000000, owner = base + 0xD00000;
constexpr std::uintptr_t bank = owner + audio::kSoundBankOffset;
constexpr std::uintptr_t node_a = base + 0xC00000, node_z = base + 0xC01000;
constexpr std::uintptr_t node_extra = base + 0xC02000, alias_node = base + 0xC03000;
struct Tree { std::uintptr_t root; unsigned count, pad, tag, pad2; };
struct Link { std::uintptr_t left, parent, right; unsigned color, pad; };
struct Name { std::uintptr_t data; unsigned length, capacity; };
struct Holder { unsigned mask, pad; std::uintptr_t data; unsigned short offsets[29]; };
struct Fixture {
    std::vector<unsigned char> memory = std::vector<unsigned char>(0xE00000);
    audio::Property bp[8] = {}, cp[8] = {};
    audio::Alias ba[8] = {}, ca[8] = {};
    audio::Snapshot baseline = {}, current = {};
    bool fenced = true, changed = false, removable = true, fail_store = false, borrowers_drained = true;
    unsigned fence_reads = 0, mutation_calls = 0;
    Fixture() {
        baseline.properties = bp; baseline.aliases = ba;
        baseline.property_capacity = baseline.alias_capacity = 8;
        current = baseline; current.properties = cp; current.aliases = ca;
        Put(bank, base + audio::kSoundBankVtableRva);
        for (unsigned i = 0; i < 29; ++i) { Put(base + audio::kPropertySchemaRva + i * 24, (unsigned char)2); Put(base + audio::kPropertySchemaRva + i * 24 + 1, (unsigned char)1); }
        Property(node_a, "a", 10); Property(node_z, "z", 20);
        Put(node_a + 96, Link{0, 0, node_z + 96, 1, 0});
        Put(node_z + 96, Link{0, node_a + 96, 0, 0, 0});
        Put(bank + 88, Tree{node_a + 96, 2, 0, 10, 0});
        Alias("tone", node_a + 16);
        audio::Value fallback = {}; Store(base + audio::kDefaultPropertyRva, fallback);
    }
    template<typename T> void Put(std::uintptr_t at, const T& value) { std::memcpy(memory.data() + at - base, &value, sizeof(value)); }
    template<typename T> T Get(std::uintptr_t at) { T result; std::memcpy(&result, memory.data() + at - base, sizeof(result)); return result; }
    void Text(std::uintptr_t at, const char* text) {
        const auto data = at + 256; const auto length = static_cast<unsigned>(std::strlen(text));
        Put(at, Name{data, length, length}); std::memcpy(memory.data() + data - base, text, length + 1);
    }
    void Store(std::uintptr_t holder, const audio::Value& value) {
        Holder h = {}; h.mask = value.mask; h.data = holder + 1024;
        unsigned short offset = 0;
        for (unsigned i = 0; i < 29; ++i) { h.offsets[i] = value.mask & (1U << i) ? offset : 0xffff; if (value.mask & (1U << i)) offset += 4; }
        Put(holder, h); std::memcpy(memory.data() + h.data - base, value.data, value.length);
    }
    void Property(std::uintptr_t at, const char* name, unsigned value) {
        Text(at, name); audio::Value v = {}; v.mask = 1; v.length = 4; std::memcpy(v.data, &value, 4); Store(at + 16, v);
    }
    void Alias(const char* name, std::uintptr_t holder) {
        Text(alias_node, name); Put(alias_node + 16, holder); Put(alias_node + 24, Link{0, 0, 0, 1, 0}); Put(bank + 112, Tree{alias_node + 24, 1, 0, 10, 0});
    }
    void AddExtra() {
        Property(node_extra, "m", 30);
        Put(node_a + 96, Link{0, 0, node_extra + 96, 1, 0});
        Put(node_extra + 96, Link{0, node_a + 96, node_z + 96, 0, 0});
        Put(node_z + 96, Link{0, node_extra + 96, 0, 1, 0});
        Put(bank + 88, Tree{node_a + 96, 3, 0, 10, 0});
    }
    audio::Access Access() { return {this, &Read, &Fence, base}; }
    audio::RestoreOperations Operations() { return {this, &CanRemove, &Clear, &StoreNative, &Remove, &Register}; }
    static bool Read(void* user, std::uintptr_t at, void* output, std::size_t count) noexcept {
        auto& f = *static_cast<Fixture*>(user); if (at < base || at - base > f.memory.size() || count > f.memory.size() - (at - base)) return false;
        std::memcpy(output, f.memory.data() + at - base, count); return true;
    }
    static bool Fence(void* user, audio::Fence* out) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.fence_reads;
        *out = {owner, 3, f.changed ? f.fence_reads : 1, 7, f.fenced, f.fenced, f.borrowers_drained}; return true;
    }
    static bool CanRemove(void* user, const audio::Property&) noexcept { return static_cast<Fixture*>(user)->removable; }
    static bool Clear(void* user, std::uintptr_t at) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.mutation_calls; if (at != bank) return false;
        f.Put(bank + 112, Tree{0, 0, 0, 10, 0}); return true;
    }
    static bool StoreNative(void* user, std::uintptr_t at, const audio::Value& value) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.mutation_calls; if (f.fail_store) return false; f.Store(at, value); return true;
    }
    static bool Remove(void* user, std::uintptr_t at, const audio::Property& value) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.mutation_calls; if (at != bank || value.holder != node_extra + 16) return false;
        f.Put(node_a + 96, Link{0, 0, node_z + 96, 1, 0}); f.Put(node_z + 96, Link{0, node_a + 96, 0, 0, 0});
        f.Put(bank + 88, Tree{node_a + 96, 2, 0, 10, 0}); return true;
    }
    static bool Register(void* user, std::uintptr_t at, const char* alias, const char* group) noexcept {
        auto& f = *static_cast<Fixture*>(user); ++f.mutation_calls; if (at != bank || std::strcmp(group, "a")) return false;
        f.Alias(alias, node_a + 16); return true;
    }
};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
}
int main() {
    {
        Fixture f; CHECK(audio::Capture(f.Access(), &f.baseline) == audio::Result::Succeeded);
        f.AddExtra(); f.Property(node_a, "a", 55); f.Alias("changed", node_extra + 16);
        CHECK(audio::Restore(f.Access(), f.baseline, &f.current, f.Operations()) == audio::Result::Succeeded);
        CHECK(audio::SameValues(f.baseline, f.current, true)); CHECK(f.current.properties[0].holder == node_a + 16);
    }
    {
        Fixture f; CHECK(audio::Capture(f.Access(), &f.baseline) == audio::Result::Succeeded);
        f.AddExtra(); f.removable = false;
        CHECK(audio::Restore(f.Access(), f.baseline, &f.current, f.Operations()) == audio::Result::ExtraConsumer); CHECK(f.mutation_calls == 0);
    }
    {
        Fixture f; CHECK(audio::Capture(f.Access(), &f.baseline) == audio::Result::Succeeded);
        f.Property(node_extra, "a", 10); f.Put(node_extra + 96, Link{0, 0, node_z + 96, 1, 0});
        f.Put(node_z + 96, Link{0, node_extra + 96, 0, 0, 0}); f.Put(bank + 88, Tree{node_extra + 96, 2, 0, 10, 0}); f.Alias("tone", node_extra + 16);
        CHECK(audio::Restore(f.Access(), f.baseline, &f.current, f.Operations()) == audio::Result::BaselineHolderMissing); CHECK(f.mutation_calls == 0);
    }
    {
        Fixture f; f.changed = true; CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::Changed); CHECK(!f.current.complete);
    }
    {
        Fixture f; f.fenced = false; CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::FenceUnavailable);
    }
    {
        Fixture f; f.Put(node_a + 96, Link{node_a + 96, 0, node_z + 96, 1, 0}); CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::InvalidTree);
    }
    {
        Fixture f; f.Put(alias_node + 16, node_extra + 16); CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::InvalidAlias);
    }
    {
        Fixture f; auto holder = f.Get<Holder>(node_a + 16); holder.offsets[0] = 2; f.Put(node_a + 16, holder);
        CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::InvalidProperty);
    }
    {
        Fixture f; f.current.property_capacity = 1; CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::Capacity);
    }
    {
        Fixture f; CHECK(audio::Capture(f.Access(), &f.baseline) == audio::Result::Succeeded); auto overlap = f.baseline;
        CHECK(audio::Restore(f.Access(), f.baseline, &overlap, f.Operations()) == audio::Result::InvalidArgument); CHECK(f.mutation_calls == 0);
    }
    {
        Fixture f; CHECK(audio::Capture(f.Access(), &f.baseline) == audio::Result::Succeeded); f.fail_store = true;
        CHECK(audio::Restore(f.Access(), f.baseline, &f.current, f.Operations()) == audio::Result::NativeCallFailed);
    }
    {
        Fixture f; f.Put(base + audio::kPropertySchemaRva, (unsigned char)3);
        CHECK(audio::Capture(f.Access(), &f.current) == audio::Result::InvalidSchema);
    }
    {
        Fixture f; f.borrowers_drained=false;
        CHECK(audio::CaptureReadOnly(f.Access(), &f.baseline) == audio::Result::Succeeded);
        CHECK(!f.baseline.fence.borrowed_values_drained);
        CHECK(audio::Restore(f.Access(), f.baseline, &f.current, f.Operations()) == audio::Result::FenceUnavailable);
        CHECK(f.mutation_calls == 0);
    }
    {
        Fixture f; f.fenced=false;
        CHECK(audio::CaptureReadOnly(f.Access(), &f.current) == audio::Result::FenceUnavailable);
        CHECK(!f.current.complete);
    }
    {
        Fixture f;const auto link=base+0xC04008, common=base+0xC05000;
        f.Put(bank+8,link);f.Put(bank+24,1U);f.Put(link-8,common);f.Put(link,std::uintptr_t{0});f.Put(common+96,node_a+16);
        CHECK(audio::CaptureReadOnly(f.Access(), &f.baseline) == audio::Result::Succeeded);
        audio::CommonBorrower borrowers[4]={};std::size_t count=0;
        CHECK(audio::CaptureCommonBorrowers(f.Access(),f.baseline,borrowers,4,&count)==audio::Result::Succeeded);
        CHECK(count==1&&borrowers[0].object==common&&borrowers[0].property_holder==node_a+16);
        f.Put(common+96,node_extra+16);
        CHECK(audio::CaptureCommonBorrowers(f.Access(),f.baseline,borrowers,4,&count)==audio::Result::InvalidProperty);
        f.Put(common+96,node_a+16);f.Put(link,link);
        CHECK(audio::CaptureCommonBorrowers(f.Access(),f.baseline,borrowers,4,&count)==audio::Result::InvalidTree);
    }
    std::puts("PASS: 15 sound-property ownership scenarios"); return 0;
}
