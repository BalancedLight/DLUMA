#define _HAS_EXCEPTIONS 0
#include "NativeXuiRegistrySnapshot.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::xui {
namespace {
struct Header { std::uintptr_t data; std::uint32_t count, capacity; };
struct Stamp { Header vectors[3]; };
constexpr std::uintptr_t kManagerVtable = 0x9173E8, kRegisterVtable = 0x918F98;
constexpr std::uintptr_t kEntryVtables[] = {0x918F80, 0x918F68, 0x918F40};
constexpr std::size_t kStrides[] = {32, 64, 80};
bool Read(const Access& a, std::uintptr_t p, void* out, std::size_t n) noexcept
{
    return p && n && p <= UINTPTR_MAX - n && a.read(a.user, p, out, n);
}
bool Same(const Fence& a, const Fence& b) noexcept
{
    return a.manager == b.manager && a.generation == b.generation &&
        a.revision == b.revision && a.frame_token == b.frame_token &&
        a.thread_id == b.thread_id;
}
bool ValidFence(const Fence& f) noexcept
{
    return f.manager && f.manager <= UINTPTR_MAX - 224 &&
        f.generation && f.frame_token && f.thread_id;
}
bool ValidVector(const Header& h, std::size_t stride, std::size_t limit) noexcept
{
    return h.count <= h.capacity && h.count <= limit &&
        h.capacity <= 1048576 && (!h.count ||
        (h.data && h.count <= (UINTPTR_MAX - h.data) / stride));
}
bool Range(std::uintptr_t p, std::size_t n, std::uintptr_t q, std::size_t m) noexcept
{
    return p <= UINTPTR_MAX - n && q <= UINTPTR_MAX - m &&
        (p + n <= q || q + m <= p);
}
Result OwnerStamp(const Access& a, const Fence& f, Stamp* stamp) noexcept
{
    std::uintptr_t table = 0;
    if (!Read(a, f.manager, &table, 8)) return Result::ReadFailed;
    if (table != a.engine_base + kManagerVtable) return Result::WrongOwnerType;
    if (!Read(a, f.manager + 144, &table, 8)) return Result::ReadFailed;
    if (table != a.engine_base + kRegisterVtable) return Result::WrongOwnerType;
    for (unsigned i = 0; i < 3; ++i) {
        if (!Read(a, f.manager + 152 + 24 * i, &stamp->vectors[i], sizeof(Header)))
            return Result::ReadFailed;
        if (!ValidVector(stamp->vectors[i], kStrides[i], kMaximumEntries))
            return Result::InvalidVector;
    }
    return Result::Succeeded;
}
Result CopyString(const Access& a, const Header& h, Snapshot* out,
    std::uint32_t* offset, std::uint32_t* bytes, bool verify,
    std::size_t* cursor) noexcept
{
    if (h.count > h.capacity || h.count > kMaximumNameBytes ||
        h.capacity > kMaximumStringBytes || (h.count && !h.data))
        return Result::InvalidString;
    const std::size_t need = static_cast<std::size_t>(h.count) + 1;
    if (*cursor > out->string_capacity || need > out->string_capacity - *cursor)
        return Result::CapacityExceeded;
    if (verify && (*offset != *cursor || *bytes != h.count)) return Result::Changed;
    if (!verify) { *offset = static_cast<std::uint32_t>(*cursor); *bytes = h.count; }
    // Bounded stack scratch avoids retaining the native pointer even for
    // strings that cross pages or alias another entry's native storage.
    char chunk[256];
    for (std::size_t done = 0; done < need;) {
        const std::size_t n = need - done < sizeof(chunk) ? need - done : sizeof(chunk);
        if (h.data) {
            if (done > UINTPTR_MAX - h.data || !Read(a, h.data + done, chunk, n))
                return Result::ReadFailed;
        } else std::memset(chunk, 0, n);
        for (std::size_t j = 0; j < n; ++j)
            if ((done + j == h.count) != (chunk[j] == 0)) return Result::InvalidString;
        if (verify) {
            if (std::memcmp(out->strings + *cursor + done, chunk, n)) return Result::Changed;
        } else std::memcpy(out->strings + *cursor + done, chunk, n);
        done += n;
    }
    *cursor += need;
    return Result::Succeeded;
}
bool SameEntry(const Entry& a, const Entry& b, bool loaded) noexcept
{
    return a.kind == b.kind && a.id == b.id && a.name_offset == b.name_offset &&
        a.name_bytes == b.name_bytes && a.template_offset == b.template_offset &&
        a.template_bytes == b.template_bytes && a.pack_offset == b.pack_offset &&
        a.pack_count == b.pack_count && a.category == b.category &&
        a.screen_flags == b.screen_flags &&
        (!loaded || (a.skin_loaded == b.skin_loaded &&
            !std::memcmp(a.skin_layout, b.skin_layout, sizeof(a.skin_layout))));
}
Result Pass(const Access& a, const Stamp& stamp, Snapshot* out, bool verify) noexcept
{
    std::size_t entry_cursor = 0, pack_cursor = 0, string_cursor = 0;
    for (unsigned kind = 0; kind < 3; ++kind) {
        const auto& vector = stamp.vectors[kind];
        for (std::uint32_t i = 0; i < vector.count; ++i) {
            if (entry_cursor == out->entry_capacity) return Result::CapacityExceeded;
            const auto address = vector.data + kStrides[kind] * i;
            std::uint8_t native[80] = {};
            if (!Read(a, address, native, kStrides[kind])) return Result::ReadFailed;
            std::uintptr_t vtable = 0;
            std::memcpy(&vtable, native, 8);
            if (vtable != a.engine_base + kEntryVtables[kind]) return Result::InvalidEntry;
            Entry current = {};
            current.kind = static_cast<EntryKind>(kind);
            Header name = {}; std::memcpy(&name, native + 8, sizeof(name));
            current.name_offset = static_cast<std::uint32_t>(string_cursor);
            current.name_bytes = name.count;
            auto r = CopyString(a, name, out, &current.name_offset, &current.name_bytes,
                verify, &string_cursor);
            if (r != Result::Succeeded) return r;
            if (kind == 0) {
                std::int16_t id = 0; std::memcpy(&id, native + 24, 2); current.id = id;
            } else {
                Header packs = {}; std::memcpy(&packs, native + 24, sizeof(packs));
                if (!ValidVector(packs, 4, kMaximumPackIds)) return Result::InvalidVector;
                if (pack_cursor > out->pack_capacity || packs.count > out->pack_capacity - pack_cursor)
                    return Result::CapacityExceeded;
                current.pack_offset = static_cast<std::uint32_t>(pack_cursor);
                current.pack_count = packs.count;
                for (std::uint32_t j = 0; j < packs.count; ++j) {
                    std::uint32_t id = 0;
                    if (!Read(a, packs.data + 4ULL * j, &id, 4)) return Result::ReadFailed;
                    if (verify && out->pack_ids[pack_cursor] != id) return Result::Changed;
                    if (!verify) out->pack_ids[pack_cursor] = id;
                    ++pack_cursor;
                }
                if (kind == 1) {
                    std::memcpy(current.skin_layout, native + 48, sizeof(current.skin_layout));
                    current.skin_loaded = native[60];
                    if (current.skin_loaded > 1) return Result::InvalidEntry;
                } else {
                    std::memcpy(&current.id, native + 48, 4);
                    Header target = {}; std::memcpy(&target, native + 56, sizeof(target));
                    current.template_offset = static_cast<std::uint32_t>(string_cursor);
                    current.template_bytes = target.count;
                    r = CopyString(a, target, out, &current.template_offset,
                        &current.template_bytes, verify, &string_cursor);
                    if (r != Result::Succeeded) return r;
                    std::memcpy(&current.category, native + 72, 2);
                    current.screen_flags = native[74] & 3;
                }
            }
            if (verify && !SameEntry(current, out->entries[entry_cursor], true)) return Result::Changed;
            if (!verify) out->entries[entry_cursor] = current;
            ++entry_cursor;
        }
    }
    if (verify && (entry_cursor != out->entry_count || pack_cursor != out->pack_count ||
        string_cursor != out->string_bytes)) return Result::Changed;
    out->entry_count = entry_cursor; out->pack_count = pack_cursor; out->string_bytes = string_cursor;
    return Result::Succeeded;
}
} // namespace
Result Capture(const Access& a, Snapshot* out) noexcept
{
    if (!out) return Result::InvalidArgument;
    out->complete = false; out->entry_count = out->pack_count = out->string_bytes = 0;
    out->fence = {};
    if (!a.engine_base || a.engine_base > UINTPTR_MAX - 0xE98000 || !a.read || !a.fence ||
        !out->entries || !out->entry_capacity || out->entry_capacity > kMaximumEntries ||
        !out->pack_ids || !out->pack_capacity || out->pack_capacity > kMaximumPackIds ||
        !out->strings || !out->string_capacity || out->string_capacity > kMaximumStringBytes)
        return out->result = Result::InvalidArgument;
    const auto ep = reinterpret_cast<std::uintptr_t>(out->entries);
    const auto pp = reinterpret_cast<std::uintptr_t>(out->pack_ids);
    const auto sp = reinterpret_cast<std::uintptr_t>(out->strings);
    const auto en = out->entry_capacity * sizeof(Entry), pn = out->pack_capacity * 4;
    if (ep % alignof(Entry) || pp % alignof(std::uint32_t) || !Range(ep,en,pp,pn) ||
        !Range(ep,en,sp,out->string_capacity) || !Range(pp,pn,sp,out->string_capacity) ||
        !Range(reinterpret_cast<std::uintptr_t>(out),sizeof(*out),ep,en) ||
        !Range(reinterpret_cast<std::uintptr_t>(out),sizeof(*out),pp,pn) ||
        !Range(reinterpret_cast<std::uintptr_t>(out),sizeof(*out),sp,out->string_capacity))
        return out->result = Result::InvalidArgument;
    if (std::memcmp(a.sha256,kPlayerEngineSha256,sizeof(kPlayerEngineSha256)))
        return out->result = Result::ProfileMismatch;
    Fence before = {}, after = {}; Stamp stamp = {}, check = {};
    if (!a.fence(a.user,&before) || !ValidFence(before)) return out->result = Result::FenceUnavailable;
    auto r = OwnerStamp(a,before,&stamp);
    if (r == Result::Succeeded) r = Pass(a,stamp,out,false);
    if (r == Result::Succeeded) r = OwnerStamp(a,before,&check);
    if (r == Result::Succeeded && std::memcmp(&stamp,&check,sizeof(stamp))) r = Result::Changed;
    if (r == Result::Succeeded) r = Pass(a,stamp,out,true);
    if (r == Result::Succeeded) r = OwnerStamp(a,before,&check);
    if (r == Result::Succeeded && (std::memcmp(&stamp,&check,sizeof(stamp)) ||
        !a.fence(a.user,&after) || !Same(before,after))) r = Result::Changed;
    out->result = r; out->complete = r == Result::Succeeded;
    if (out->complete) out->fence = before;
    return r;
}
bool Equal(const Snapshot& a,const Snapshot& b,bool loaded) noexcept
{
    if (!a.complete || !b.complete || a.entry_count != b.entry_count || a.pack_count != b.pack_count ||
        a.string_bytes != b.string_bytes || !a.entries || !b.entries || !a.pack_ids || !b.pack_ids ||
        !a.strings || !b.strings || a.entry_count > a.entry_capacity || b.entry_count > b.entry_capacity ||
        a.pack_count > a.pack_capacity || b.pack_count > b.pack_capacity ||
        a.string_bytes > a.string_capacity || b.string_bytes > b.string_capacity) return false;
    for (std::size_t i=0;i<a.entry_count;++i) if (!SameEntry(a.entries[i],b.entries[i],loaded)) return false;
    return !std::memcmp(a.pack_ids,b.pack_ids,a.pack_count*4) &&
        !std::memcmp(a.strings,b.strings,a.string_bytes);
}
const char* ResultName(Result r) noexcept
{
    switch(r) {
    case Result::Succeeded:return "succeeded";
    case Result::InvalidArgument:return "invalid_argument";
    case Result::ProfileMismatch:return "profile_mismatch";
    case Result::FenceUnavailable:return "xui_writer_fence_unavailable";
    case Result::WrongOwnerType:return "xui_owner_type_mismatch";
    case Result::InvalidVector:return "xui_registry_vector_invalid";
    case Result::InvalidEntry:return "xui_registry_entry_invalid";
    case Result::InvalidString:return "xui_registry_string_invalid";
    case Result::CapacityExceeded:return "xui_registry_capacity";
    case Result::ReadFailed:return "xui_registry_read_failed";
    case Result::Changed:return "xui_registry_changed";
    }
    return "unknown";
}

namespace {
bool LockManagers(ManagerLedger* ledger) noexcept
{
    if (!ledger) return false;
    if (ledger->lock.test_and_set(std::memory_order_acquire)) {
        ledger->lost.store(true,std::memory_order_release);
        return false;
    }
    return true;
}
void UnlockManagers(ManagerLedger* ledger) noexcept { ledger->lock.clear(std::memory_order_release); }
bool CurrentManagerToken(ManagerLedger* l,const ManagerToken* t,ManagerLife phase) noexcept
{
    return t && t->active && t->ledger == reinterpret_cast<std::uintptr_t>(l) &&
        t->index < kMaximumManagers && t->phase == phase &&
        l->records[t->index].generation == t->generation && l->records[t->index].life == phase;
}
}
bool SealManagerStartup(ManagerLedger* l) noexcept
{
    if (!LockManagers(l)) return false;
    const bool valid = !l->lost.load() && !l->startup_sealed && !l->next_generation && !l->revision;
    if (valid) l->startup_sealed = true;
    UnlockManagers(l); return valid;
}
bool BeginManagerConstruction(ManagerLedger* l,std::uintptr_t manager,std::uintptr_t level,
    std::uint32_t epoch,ManagerToken* t) noexcept
{
    if (!t || t->active || !LockManagers(l)) return false;
    std::size_t slot = kMaximumManagers;
    bool duplicate = false;
    for (std::size_t i=0;i<kMaximumManagers;++i) {
        const auto& r=l->records[i];
        const bool ended = r.life == ManagerLife::Empty || r.life == ManagerLife::Ended;
        if (ended && slot == kMaximumManagers) slot=i;
        if (!ended && r.manager == manager) duplicate=true;
    }
    const bool valid = l->startup_sealed && !l->lost.load() && manager && level &&
        slot < kMaximumManagers && !duplicate && l->next_generation != UINT64_MAX && l->revision != UINT64_MAX;
    if (valid) {
        const auto generation=++l->next_generation;
        l->records[slot]={manager,level,generation,epoch,ManagerLife::Constructing};
        *t={reinterpret_cast<std::uintptr_t>(l),generation,static_cast<std::uint32_t>(slot),ManagerLife::Constructing,true};
        ++l->revision;
    } else l->lost.store(true);
    UnlockManagers(l); return valid;
}
bool EndManagerConstruction(ManagerLedger* l,ManagerToken* t,const Access& a,std::uintptr_t returned) noexcept
{
    if (!LockManagers(l)) return false;
    bool valid = CurrentManagerToken(l,t,ManagerLife::Constructing) && a.read && a.engine_base &&
        a.engine_base <= UINTPTR_MAX-0xE98000 && !std::memcmp(a.sha256,kPlayerEngineSha256,65);
    if (valid) {
        const auto& r=l->records[t->index]; std::uintptr_t table=0,level=0;
        valid = returned == r.manager && r.manager <= UINTPTR_MAX-144 &&
            Read(a,r.manager,&table,8) && table == a.engine_base+kManagerVtable &&
            Read(a,r.manager+136,&level,8) && level == r.level;
    }
    if (valid && l->revision != UINT64_MAX) {
        l->records[t->index].life=ManagerLife::Live; ++l->revision; t->active=false;
    } else { l->lost.store(true); valid=false; }
    UnlockManagers(l); return valid;
}
bool BeginManagerDestruction(ManagerLedger* l,std::uintptr_t manager,ManagerToken* t) noexcept
{
    if (!t || t->active || !LockManagers(l)) return false;
    std::size_t found=kMaximumManagers;
    for (std::size_t i=0;i<kMaximumManagers;++i)
        if (l->records[i].manager == manager && l->records[i].life == ManagerLife::Live) {
            if (found != kMaximumManagers) { l->lost.store(true); break; }
            found=i;
        }
    const bool valid = l->startup_sealed && !l->lost.load() && found < kMaximumManagers && l->revision != UINT64_MAX;
    if (valid) {
        auto& r=l->records[found]; r.life=ManagerLife::Destroying; ++l->revision;
        *t={reinterpret_cast<std::uintptr_t>(l),r.generation,static_cast<std::uint32_t>(found),ManagerLife::Destroying,true};
    } else l->lost.store(true);
    UnlockManagers(l); return valid;
}
bool EndManagerDestruction(ManagerLedger* l,ManagerToken* t) noexcept
{
    if (!LockManagers(l)) return false;
    const bool valid = CurrentManagerToken(l,t,ManagerLife::Destroying) && l->revision != UINT64_MAX;
    if (valid) {
        l->records[t->index].life=ManagerLife::Ended; ++l->revision; t->active=false;
    } else l->lost.store(true);
    UnlockManagers(l); return valid;
}
bool LookupManager(ManagerLedger* l,std::uintptr_t manager,ManagerRecord* out)noexcept
{
    if(!out||!manager||!LockManagers(l))return false;*out={};
    bool found=false;
    if(l->startup_sealed&&!l->lost.load())for(const auto& record:l->records)
        if(record.manager==manager&&record.life!=ManagerLife::Empty&&record.life!=ManagerLife::Ended){
            if(found){found=false;break;}*out=record;found=true;}
    UnlockManagers(l);return found;
}
bool CopyManagerInventory(ManagerLedger* l,ManagerRecord* out,std::size_t capacity,
    std::size_t* count,std::uint64_t* revision) noexcept
{
    if (count) *count=0;
    if (revision) *revision=0;
    if (!l || !out || !count || !revision || capacity > kMaximumManagers ||
        !Range(reinterpret_cast<std::uintptr_t>(l),sizeof(*l),reinterpret_cast<std::uintptr_t>(out),capacity*sizeof(*out)) ||
        l->lock.test_and_set(std::memory_order_acquire)) return false;
    bool valid = l->startup_sealed && !l->lost.load();
    for (std::size_t i=0;i<kMaximumManagers && valid;++i) {
        const auto& r=l->records[i];
        if (r.life == ManagerLife::Empty || r.life == ManagerLife::Ended) continue;
        if (r.life != ManagerLife::Live || *count == capacity) { valid=false; break; }
        out[(*count)++]=r;
    }
    if (valid) *revision=l->revision; else *count=0;
    UnlockManagers(l); return valid;
}
namespace {
struct ConsumerHeads {
    std::uintptr_t active, dead, input_owner, input_head;
    std::uint32_t active_count, dead_count;
};
Result ReadConsumerHeads(const Access& a, const Fence& f, ConsumerHeads* h) noexcept
{
    std::uintptr_t table = 0;
    if (!Read(a, f.manager, &table, sizeof(table))) return Result::ReadFailed;
    if (table != a.engine_base + kManagerVtable) return Result::WrongOwnerType;
    if (!Read(a,f.manager+24,&h->active,8) || !Read(a,f.manager+32,&h->active_count,4) ||
        !Read(a,f.manager+40,&h->dead,8) || !Read(a,f.manager+48,&h->dead_count,4) ||
        !Read(a,a.engine_base+0xACA848,&h->input_owner,8)) return Result::ReadFailed;
    if (h->active_count > kMaximumEntries || h->dead_count > kMaximumEntries ||
        static_cast<bool>(h->active) != static_cast<bool>(h->active_count) ||
        static_cast<bool>(h->dead) != static_cast<bool>(h->dead_count) ||
        (h->active && h->active==h->dead)) return Result::InvalidVector;
    h->input_head = 0;
    if (h->input_owner && (h->input_owner > UINTPTR_MAX-18872 ||
        !Read(a,h->input_owner+18864,&h->input_head,8))) return Result::ReadFailed;
    return Result::Succeeded;
}
bool SameConsumerHeads(const ConsumerHeads& a,const ConsumerHeads& b) noexcept
{
    return a.active==b.active && a.dead==b.dead && a.active_count==b.active_count &&
        a.dead_count==b.dead_count && a.input_owner==b.input_owner && a.input_head==b.input_head;
}
Result ReadConsumers(const Access& a,const ConsumerHeads& h,Consumers* out,bool verify) noexcept
{
    const auto total=static_cast<std::size_t>(h.active_count)+h.dead_count;
    if (total>out->capacity) return Result::CapacityExceeded;
    std::size_t index=0;
    for (unsigned dead=0;dead<2;++dead) {
        auto object=dead?h.dead:h.active;
        std::uintptr_t previous=0;
        const auto count=dead?h.dead_count:h.active_count;
        for (std::uint32_t i=0;i<count;++i) {
            Consumer row={}; row.object=object; row.awaiting_destruction=dead!=0;
            std::uintptr_t next=0, back=0;
            if (!object || object>UINTPTR_MAX-65 ||
                !Range(object,65,reinterpret_cast<std::uintptr_t>(out),sizeof(*out)) ||
                !Range(object,65,reinterpret_cast<std::uintptr_t>(out->objects),out->capacity*sizeof(Consumer)))
                return Result::InvalidEntry;
            if (!Read(a,object,&row.vtable,8) || !Read(a,object+16,&row.interface_object,8) ||
                !Read(a,object+48,&next,8) || !Read(a,object+56,&back,8) ||
                !Read(a,object+64,&row.flags,1)) return Result::ReadFailed;
            if (!row.vtable || back!=previous || (row.flags&3)!=(dead?2:0)) return Result::InvalidEntry;
            if (verify) {
                const auto& saved=out->objects[index];
                if (saved.object!=row.object || saved.interface_object!=row.interface_object ||
                    saved.vtable!=row.vtable || saved.flags!=row.flags ||
                    saved.awaiting_destruction!=row.awaiting_destruction) return Result::Changed;
            } else out->objects[index]=row;
            ++index; previous=object; object=next;
        }
        if (object) return Result::InvalidEntry;
    }
    std::size_t input_refs=0,visited=0;
    for (auto node=h.input_head;node;) {
        std::uintptr_t receiver=0,next=0;
        if (node<8 || ++visited>kMaximumEntries) return Result::InvalidEntry;
        if (!Read(a,node-8,&receiver,8) || !Read(a,node,&next,8)) return Result::ReadFailed;
        if (receiver==out->fence.manager+8) ++input_refs;
        node=next;
    }
    if (verify && out->input_receiver_references!=input_refs) return Result::Changed;
    if (!verify) {
        out->count=total; out->active_count=h.active_count; out->dead_count=h.dead_count;
        out->input_receiver_references=input_refs;
    }
    return Result::Succeeded;
}
}
Result CaptureConsumers(const Access& a,Consumers* out) noexcept
{
    if (!out) return Result::InvalidArgument;
    out->complete=false; out->count=out->active_count=out->dead_count=out->input_receiver_references=0;
    out->result=Result::InvalidArgument;
    if (!a.read || !a.fence || !a.engine_base || a.engine_base>UINTPTR_MAX-0xACA850 ||
        !out->objects || !out->capacity || out->capacity>kMaximumEntries ||
        !Range(reinterpret_cast<std::uintptr_t>(out),sizeof(*out),
            reinterpret_cast<std::uintptr_t>(out->objects),out->capacity*sizeof(Consumer))) return out->result;
    if (std::memcmp(a.sha256,kPlayerEngineSha256,65)) return out->result=Result::ProfileMismatch;
    Fence f={},after={};
    if (!a.fence(a.user,&f) || !ValidFence(f) ||
        !Range(f.manager,224,reinterpret_cast<std::uintptr_t>(out),sizeof(*out)) ||
        !Range(f.manager,224,reinterpret_cast<std::uintptr_t>(out->objects),out->capacity*sizeof(Consumer)))
        return out->result=Result::FenceUnavailable;
    out->fence=f;
    ConsumerHeads initial={},current={};
    auto result=ReadConsumerHeads(a,f,&initial);
    if (result==Result::Succeeded) result=ReadConsumers(a,initial,out,false);
    if (result==Result::Succeeded) result=ReadConsumerHeads(a,f,&current);
    if (result==Result::Succeeded && !SameConsumerHeads(initial,current)) result=Result::Changed;
    if (result==Result::Succeeded) result=ReadConsumers(a,current,out,true);
    if (result==Result::Succeeded) result=ReadConsumerHeads(a,f,&current);
    if (result==Result::Succeeded && (!SameConsumerHeads(initial,current) ||
        !a.fence(a.user,&after) || !Same(f,after))) result=Result::Changed;
    out->result=result; out->complete=result==Result::Succeeded;
    if (!out->complete) out->count=out->active_count=out->dead_count=out->input_receiver_references=0;
    return result;
}
} // namespace dluma::fixes::custom_map_asset_isolation::xui
