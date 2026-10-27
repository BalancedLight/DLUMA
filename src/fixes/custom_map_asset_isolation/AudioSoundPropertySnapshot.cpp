#define _HAS_EXCEPTIONS 0
#include "AudioSoundPropertySnapshot.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_sound_properties {
namespace {
struct Tree { std::uintptr_t root; std::uint32_t count, padding, tag, padding2; };
struct Links { std::uintptr_t left, parent, right; std::uint32_t color, padding; };
struct Name { std::uintptr_t data; std::uint32_t length, capacity; };
struct Holder { std::uint32_t mask, padding; std::uintptr_t data; std::uint16_t offsets[kPropertyCount]; };
static_assert(sizeof(Tree) == 24 && sizeof(Links) == 32 && sizeof(Holder) == 80, "native layouts");
bool Read(const Access& a, std::uintptr_t address, void* out, std::size_t size) noexcept {
    return address && address <= UINTPTR_MAX - size && a.read(a.user, address, out, size);
}
bool ValidFence(const Fence& f, bool destructive = true) noexcept {
    return f.audio_owner && f.owner_generation && f.revision && f.thread_id &&
        f.sound_lock_owned && (!destructive || (f.producer_admission_closed && f.borrowed_values_drained));
}
bool Overlap(const void* a, std::size_t an, const void* b, std::size_t bn) noexcept {
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    return x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn || (x < y + bn && y < x + an);
}
bool ValidStorage(const Snapshot& s) noexcept {
    return s.properties && s.aliases && s.property_capacity && s.alias_capacity &&
        s.property_capacity <= kMaximumProperties && s.alias_capacity <= kMaximumAliases &&
        s.property_count <= s.property_capacity && s.alias_count <= s.alias_capacity &&
        !Overlap(s.properties, s.property_capacity * sizeof(Property), s.aliases, s.alias_capacity * sizeof(Alias));
}
bool SameFence(const Fence& a, const Fence& b, bool destructive = true) noexcept {
    return ValidFence(a, destructive) && ValidFence(b, destructive) && a.audio_owner == b.audio_owner &&
        a.owner_generation == b.owner_generation && a.revision == b.revision && a.thread_id == b.thread_id;
}
bool Text(const Access& a, std::uintptr_t at, char out[kNameBytes]) noexcept {
    Name name = {};
    if (!Read(a, at, &name, sizeof(name)) || !name.length || name.length >= kNameBytes ||
        name.capacity < name.length || name.capacity > 65536 ||
        !Read(a, name.data, out, name.length + 1) || out[name.length] ||
        std::memchr(out, 0, name.length)) return false;
    return true;
}
int Compare(const char* a, const char* b) noexcept {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
bool Schema(const Access& a, std::uint16_t sizes[kPropertyCount]) noexcept {
    std::size_t total = 0;
    for (std::size_t i = 0; i < kPropertyCount; ++i) {
        unsigned char type_count[2] = {};
        if (!Read(a, a.engine_base + kPropertySchemaRva + i * 24, type_count, 2) ||
            type_count[0] > 2 || !type_count[1]) return false;
        sizes[i] = static_cast<std::uint16_t>((type_count[0] ? 4 : 1) * type_count[1]);
        total += sizes[i];
    }
    return total <= kDataBytes;
}
bool ReadValue(const Access& a, std::uintptr_t at,
               const std::uint16_t sizes[kPropertyCount], Value* out) noexcept {
    Holder h = {};
    if (!Read(a, at, &h, sizeof(h)) || (h.mask >> kPropertyCount)) return false;
    *out = {}; out->mask = h.mask;
    for (std::size_t i = 0; i < kPropertyCount; ++i) {
        if (!(h.mask & (1U << i))) {
            if (h.offsets[i] != UINT16_MAX) return false;
            continue;
        }
        // The native allocator packs selected fields in schema order. A
        // displaced, overlapping, or out-of-range field is not accepted.
        if (h.offsets[i] != out->length || out->length + sizes[i] > kDataBytes ||
            h.data > UINTPTR_MAX - h.offsets[i] ||
            !Read(a, h.data + h.offsets[i], out->data + out->length, sizes[i])) return false;
        out->length = static_cast<std::uint16_t>(out->length + sizes[i]);
    }
    return true;
}
template<typename Visit>
Result Walk(const Access& a, std::uintptr_t header, std::size_t capacity,
            Visit visit, std::size_t* count) noexcept {
    Tree tree = {};
    if (!Read(a, header, &tree, sizeof(tree))) return Result::ReadFailed;
    if (tree.tag != 10 || (tree.root == 0) != (tree.count == 0)) return Result::InvalidTree;
    if (tree.count > capacity) return Result::Capacity;
    struct Frame { std::uintptr_t address; Links links; };
    Frame stack[128] = {};
    std::size_t depth = 0, entered = 0;
    std::uintptr_t current = tree.root, parent = 0;
    *count = 0;
    while (current || depth) {
        while (current) {
            if (depth == 128 || ++entered > tree.count) return Result::InvalidTree;
            Links links = {};
            if (!Read(a, current, &links, sizeof(links)) || links.parent != parent || links.color > 1)
                return Result::InvalidTree;
            stack[depth++] = {current, links};
            parent = current; current = links.left;
        }
        const Frame frame = stack[--depth];
        const auto result = visit(frame.address, *count);
        if (result != Result::Succeeded) return result;
        ++*count;
        parent = frame.address; current = frame.links.right;
    }
    Tree after = {};
    if (*count != tree.count || !Read(a, header, &after, sizeof(after)) ||
        after.root != tree.root || after.count != tree.count || after.tag != tree.tag) return Result::Changed;
    return Result::Succeeded;
}
bool EqualValue(const Value& a, const Value& b) noexcept {
    return a.mask == b.mask && a.length == b.length && a.length <= kDataBytes &&
        !std::memcmp(a.data, b.data, a.length);
}
const Property* Find(const Snapshot& s, const char* name) noexcept {
    for (std::size_t i = 0; i < s.property_count; ++i)
        if (!Compare(s.properties[i].name, name)) return &s.properties[i];
    return nullptr;
}
}

static Result CaptureImpl(const Access& a, Snapshot* out, bool destructive) noexcept {
    if (!out || !a.read || !a.fence || !a.engine_base ||
        a.engine_base > UINTPTR_MAX - kDefaultPropertyRva - sizeof(Holder) ||
        !ValidStorage(*out))
        return Result::InvalidArgument;
    out->complete = false; out->property_count = out->alias_count = 0;
    Fence before = {};
    if (!a.fence(a.user, &before) || !ValidFence(before, destructive) ||
        before.audio_owner > UINTPTR_MAX - kSoundBankOffset - 168) return Result::FenceUnavailable;
    const auto bank = before.audio_owner + kSoundBankOffset;
    std::uintptr_t vtable = 0;
    std::uint16_t sizes[kPropertyCount] = {};
    if (!Read(a, bank, &vtable, sizeof(vtable)) || vtable != a.engine_base + kSoundBankVtableRva)
        return Result::ReadFailed;
    if (!Schema(a, sizes)) return Result::InvalidSchema;
    if (!ReadValue(a, a.engine_base + kDefaultPropertyRva, sizes, &out->fallback)) return Result::InvalidProperty;
    auto result = Walk(a, bank + 88, out->property_capacity,
        [&](std::uintptr_t link, std::size_t index) noexcept {
            auto& p = out->properties[index]; p = {};
            if (link < 96 || !Text(a, link - 96, p.name) ||
                (index && Compare(out->properties[index - 1].name, p.name) >= 0)) return Result::InvalidTree;
            p.link = link; p.holder = link - 80;
            return ReadValue(a, p.holder, sizes, &p.value) ? Result::Succeeded : Result::InvalidProperty;
        }, &out->property_count);
    if (result != Result::Succeeded) return result;
    result = Walk(a, bank + 112, out->alias_capacity,
        [&](std::uintptr_t link, std::size_t index) noexcept {
            auto& alias = out->aliases[index]; alias = {}; alias.property_index = UINT32_MAX;
            std::uintptr_t holder = 0;
            if (link < 24 || !Text(a, link - 24, alias.name) ||
                !Read(a, link - 8, &holder, sizeof(holder)) ||
                (index && Compare(out->aliases[index - 1].name, alias.name) >= 0)) return Result::InvalidAlias;
            for (std::size_t i = 0; i < out->property_count; ++i)
                if (out->properties[i].holder == holder) alias.property_index = static_cast<std::uint32_t>(i);
            return alias.property_index != UINT32_MAX ? Result::Succeeded : Result::InvalidAlias;
        }, &out->alias_count);
    if (result != Result::Succeeded) return result;
    if (!Read(a, bank + 8, &out->common_sources_head, sizeof(out->common_sources_head)) ||
        !Read(a, bank + 24, &out->common_sources_count, sizeof(out->common_sources_count)) ||
        !Read(a, bank + 144, &out->common_banks_data, sizeof(out->common_banks_data)) ||
        !Read(a, bank + 152, &out->common_banks_count, sizeof(out->common_banks_count))) return Result::ReadFailed;
    Fence after = {};
    if (!a.fence(a.user, &after) || !SameFence(before, after, destructive)) return Result::Changed;
    out->fence = after; out->complete = true; return Result::Succeeded;
}
Result Capture(const Access& a, Snapshot* out) noexcept { return CaptureImpl(a,out,true); }
Result CaptureReadOnly(const Access& a, Snapshot* out) noexcept { return CaptureImpl(a,out,false); }
Result CaptureCommonBorrowers(const Access& a,const Snapshot& snapshot,CommonBorrower* output,
                             std::size_t capacity,std::size_t* count) noexcept {
    if(count)*count=0;
    if(!snapshot.complete||!count||!capacity||!output||capacity>65536||!a.fence||!a.read||
       snapshot.common_sources_count>capacity)return Result::InvalidArgument;
    Fence before={};
    if(!a.fence(a.user,&before)||!SameFence(before,snapshot.fence,false))return Result::FenceUnavailable;
    std::uintptr_t link=snapshot.common_sources_head;
    while(link){
        if(*count>=snapshot.common_sources_count||link<8)return Result::InvalidTree;
        CommonBorrower borrower={};std::uintptr_t next=0;
        if(!Read(a,link-8,&borrower.object,8)||!borrower.object||borrower.object>UINTPTR_MAX-104||
           !Read(a,borrower.object+96,&borrower.property_holder,8)||!Read(a,link,&next,8))return Result::ReadFailed;
        bool known=!borrower.property_holder||borrower.property_holder==a.engine_base+kDefaultPropertyRva;
        for(std::size_t i=0;i<snapshot.property_count;++i)if(snapshot.properties[i].holder==borrower.property_holder)known=true;
        if(!known)return Result::InvalidProperty;
        output[(*count)++]=borrower;link=next;
    }
    if(*count!=snapshot.common_sources_count)return Result::InvalidTree;
    const auto bank=snapshot.fence.audio_owner+kSoundBankOffset;
    std::uintptr_t final_head=0;std::uint32_t final_count=0;Fence after={};
    if(!Read(a,bank+8,&final_head,8)||!Read(a,bank+24,&final_count,4)||!a.fence(a.user,&after)||
       final_head!=snapshot.common_sources_head||final_count!=snapshot.common_sources_count||
       !SameFence(before,after,false))return Result::Changed;
    return Result::Succeeded;
}
bool SameValues(const Snapshot& a, const Snapshot& b, bool holders) noexcept {
    if (!ValidStorage(a) || !ValidStorage(b) || !a.complete || !b.complete || a.fence.audio_owner != b.fence.audio_owner ||
        a.fence.owner_generation != b.fence.owner_generation || a.property_count != b.property_count ||
        a.alias_count != b.alias_count || !EqualValue(a.fallback, b.fallback)) return false;
    for (std::size_t i = 0; i < a.property_count; ++i)
        if (Compare(a.properties[i].name, b.properties[i].name) ||
            (holders && a.properties[i].holder != b.properties[i].holder) ||
            !EqualValue(a.properties[i].value, b.properties[i].value)) return false;
    for (std::size_t i = 0; i < a.alias_count; ++i)
        if (Compare(a.aliases[i].name, b.aliases[i].name) ||
            a.aliases[i].property_index != b.aliases[i].property_index) return false;
    return true;
}
Result PreflightRestore(const Snapshot& baseline, const Snapshot& current,
                        const RestoreOperations& ops) noexcept {
    if (!ValidStorage(baseline) || !ValidStorage(current) || !baseline.complete || !current.complete ||
        !ops.can_remove_extra || !ops.clear_aliases ||
        !ops.store_value || !ops.remove_property || !ops.register_alias ||
        baseline.fence.audio_owner != current.fence.audio_owner ||
        baseline.fence.owner_generation != current.fence.owner_generation) return Result::InvalidArgument;
    for (std::size_t i = 0; i < baseline.property_count; ++i) {
        const auto* found = Find(current, baseline.properties[i].name);
        if (!found || found->holder != baseline.properties[i].holder) return Result::BaselineHolderMissing;
    }
    for (std::size_t i = 0; i < current.property_count; ++i)
        if (!Find(baseline, current.properties[i].name) && !ops.can_remove_extra(ops.user, current.properties[i]))
            return Result::ExtraConsumer;
    return Result::Succeeded;
}
Result Restore(const Access& a, const Snapshot& baseline, Snapshot* scratch,
               const RestoreOperations& ops) noexcept {
    if (!scratch || scratch == &baseline) return Result::InvalidArgument;
    if (!ValidStorage(baseline) || !ValidStorage(*scratch) ||
        Overlap(baseline.properties, baseline.property_capacity * sizeof(Property),
            scratch->properties, scratch->property_capacity * sizeof(Property)) ||
        Overlap(baseline.properties, baseline.property_capacity * sizeof(Property),
            scratch->aliases, scratch->alias_capacity * sizeof(Alias)) ||
        Overlap(baseline.aliases, baseline.alias_capacity * sizeof(Alias),
            scratch->properties, scratch->property_capacity * sizeof(Property)) ||
        Overlap(baseline.aliases, baseline.alias_capacity * sizeof(Alias),
            scratch->aliases, scratch->alias_capacity * sizeof(Alias))) return Result::InvalidArgument;
    auto result = Capture(a, scratch);
    if (result != Result::Succeeded) return result;
    result = PreflightRestore(baseline, *scratch, ops);
    if (result != Result::Succeeded) return result;
    const auto bank = scratch->fence.audio_owner + kSoundBankOffset;
    if (!ops.clear_aliases(ops.user, bank)) return Result::NativeCallFailed;
    for (std::size_t i = 0; i < scratch->property_count; ++i) {
        const auto& current = scratch->properties[i];
        if (!Find(baseline, current.name) &&
            (!ops.can_remove_extra(ops.user, current) || !ops.remove_property(ops.user, bank, current)))
            return Result::NativeCallFailed;
    }
    if (!ops.store_value(ops.user, a.engine_base + kDefaultPropertyRva, baseline.fallback))
        return Result::NativeCallFailed;
    for (std::size_t i = 0; i < baseline.property_count; ++i)
        if (!ops.store_value(ops.user, baseline.properties[i].holder, baseline.properties[i].value))
            return Result::NativeCallFailed;
    for (std::size_t i = 0; i < baseline.alias_count; ++i) {
        const auto& alias = baseline.aliases[i];
        if (alias.property_index >= baseline.property_count || !ops.register_alias(ops.user, bank,
                alias.name, baseline.properties[alias.property_index].name)) return Result::NativeCallFailed;
    }
    result = Capture(a, scratch);
    if (result != Result::Succeeded) return result;
    return SameValues(baseline, *scratch, true) ? Result::Succeeded : Result::VerificationFailed;
}
} // namespace
