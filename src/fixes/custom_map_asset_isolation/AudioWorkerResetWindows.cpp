#define _HAS_EXCEPTIONS 0
#include "AudioWorkerResetWindows.h"
#include <cstring>
extern "C" {
__declspec(dllimport) BOOL WINAPI TryEnterCriticalSection(PVOID);
__declspec(dllimport) VOID WINAPI LeaveCriticalSection(PVOID);
}

namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset::windows {
namespace {
struct NativeStringView { const char* data; std::uint32_t size; std::uint32_t capacity; };
struct NativeStringVectorView {
    const NativeStringView* data;
    std::uint32_t size;
    std::uint32_t capacity;
    std::uint32_t allocator_tag;
    std::uint32_t padding;
};
static_assert(sizeof(NativeStringView) == 16U, "Native string input ABI");
static_assert(sizeof(NativeStringVectorView) == 24U, "Native vector input ABI");
using TryLock = bool (__fastcall*)();
using Unlock = void (__fastcall*)();
using Mark = void (__fastcall*)(void*);
using Evict = void (__fastcall*)(void*, const NativeStringVectorView*);
template<typename T> T Target(const Binding& binding, std::uintptr_t rva) noexcept
{ return reinterpret_cast<T>(binding.engine_base + rva); }
bool Bytes(Binding& binding, std::uintptr_t rva,
           const unsigned char* expected, std::size_t count) noexcept
{
    unsigned char actual[32] = {};
    return count <= sizeof(actual) && rva <= UINTPTR_MAX - binding.engine_base &&
        binding.read_memory(binding.read_user, binding.engine_base + rva, actual, count) &&
        std::memcmp(actual, expected, count) == 0;
}
bool Try(void* user) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    if (!binding || !binding->validated || !binding->try_reader_gate || !binding->leave_reader_gate ||
        !Target<TryLock>(*binding, kTrySoundLockRva)()) return false;
    if (!binding->try_reader_gate(binding->reader_gate_user)) {
        Target<Unlock>(*binding, kLeaveSoundLockRva)(); return false;
    }
    if (!TryEnterCriticalSection(reinterpret_cast<PVOID>(binding->engine_base + 0xAD5138U))) {
        binding->leave_reader_gate(binding->reader_gate_user);
        Target<Unlock>(*binding, kLeaveSoundLockRva)(); return false;
    }
    if (!TryEnterCriticalSection(reinterpret_cast<PVOID>(binding->engine_base + 0xAD5110U))) {
        LeaveCriticalSection(reinterpret_cast<PVOID>(binding->engine_base + 0xAD5138U));
        binding->leave_reader_gate(binding->reader_gate_user);
        Target<Unlock>(*binding, kLeaveSoundLockRva)(); return false;
    }
    return true;
}
void Leave(void* user) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    if (binding && binding->validated) {
        LeaveCriticalSection(reinterpret_cast<PVOID>(binding->engine_base + 0xAD5110U));
        LeaveCriticalSection(reinterpret_cast<PVOID>(binding->engine_base + 0xAD5138U));
        binding->leave_reader_gate(binding->reader_gate_user);
        Target<Unlock>(*binding, kLeaveSoundLockRva)();
    }
}
bool Fence(void* user, const Plan* plan) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    return binding && binding->validated && binding->integration_fence &&
        binding->integration_fence(binding->integration_user, plan);
}
bool SnapshotNative(void* user, const Plan* plan, Snapshot* output) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    if (!binding || !binding->validated || !plan || !output ||
        !binding->integration_snapshot || plan->audio_owner > UINTPTR_MAX - kDeferredVoiceCountOffset - sizeof(std::uint32_t))
        return false;
    std::uintptr_t vtable = 0, manager = 0, active_head = 0;
    std::uint32_t pending = 0, deferred = 0;
    const auto read = [&](std::uintptr_t offset, void* value, std::size_t size) noexcept {
        return binding->read_memory(binding->read_user, plan->audio_owner + offset, value, size);
    };
    if (!read(0, &vtable, sizeof(vtable)) || vtable != binding->engine_base + kFmodVtableRva ||
        !read(kBankManagerOffset, &manager, sizeof(manager)) || manager != plan->bank_manager ||
        !read(kPendingUnloadOffset, &pending, sizeof(pending)) || pending > 1U ||
        !read(kActiveVoiceHeadOffset, &active_head, sizeof(active_head)) ||
        !read(kDeferredVoiceCountOffset, &deferred, sizeof(deferred)) || deferred > 65536U ||
        !binding->integration_snapshot(binding->integration_user, plan, output)) return false;
    output->audio_owner = plan->audio_owner;
    output->bank_manager = manager;
    output->active_voices = active_head ? 1U : 0U; // Only the empty predicate is claimed.
    output->deferred_voices = deferred;
    output->native_unload_request_pending = pending != 0;
    return true;
}
bool MarkNative(void* user, std::uintptr_t owner) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    if (!binding || !binding->validated || !owner) return false;
    Target<Mark>(*binding, kMarkVoicesRva)(reinterpret_cast<void*>(owner));
    return true;
}
bool EvictNative(void* user, std::uintptr_t manager,
                 const KeptBank* banks, std::size_t count) noexcept
{
    auto* binding = static_cast<Binding*>(user);
    if (!binding || !binding->validated || !manager || count > kMaximumKeptBanks ||
        (count && !banks)) return false;
    NativeStringView names[kMaximumKeptBanks] = {};
    for (std::size_t index = 0; index < count; ++index) {
        const void* end = std::memchr(banks[index].name, 0, kBankNameCapacity);
        if (!end) return false;
        const auto length = static_cast<std::uint32_t>(
            static_cast<const char*>(end) - banks[index].name);
        names[index] = {banks[index].name, length, length};
    }
    const NativeStringVectorView view = {names, static_cast<std::uint32_t>(count),
        static_cast<std::uint32_t>(count), 11U, 0U};
    /* Called only on the validated audio worker after a complete original
     * update drained voices and async bank reads. This is the same manager
     * operation used by the native request path; never invoke its unbounded
     * main-thread wrapper or publish native queue fields ourselves. */
    Target<Evict>(*binding, kUnloadSourcesExceptRva)(reinterpret_cast<void*>(manager), &view);
    return true;
}
} // namespace
bool Validate(Binding* binding) noexcept
{
    if (!binding) return false;
    binding->validated = false;
    if (!binding->exact_player_engine_image || !binding->engine_base ||
        binding->engine_base > UINTPTR_MAX - 0xAD51A0U || !binding->read_memory) return false;
    static const unsigned char worker[] = {0x40,0x55,0x53,0x56,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc1,0x48,0x81,0xec,0xd8,0x00,0x00,0x00,0x48,0x8b,0xf1};
    static const unsigned char mark[] = {0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x8b,0x89,0x70,0x1e,0x00,0x00};
    static const unsigned char evict[] = {0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x53,0x56,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00,0x33,0xf6};
    static const unsigned char enter[] = {0x48,0x83,0xec,0x28,0x48,0x8d,0x0d,0x05,0x04,0x9e,0x00,0xff,0x15,0x0f,0x14,0x76,0x00};
    static const unsigned char leave[] = {0x48,0x8d,0x0d,0x19,0x04,0x9e,0x00,0x48,0xff,0x25,0x82,0x14,0x76,0x00};
    binding->validated = Bytes(*binding, kWorkerUpdateRva, worker, sizeof(worker)) &&
        Bytes(*binding, kMarkVoicesRva, mark, sizeof(mark)) &&
        Bytes(*binding, kUnloadSourcesExceptRva, evict, sizeof(evict)) &&
        Bytes(*binding, kTrySoundLockRva, enter, sizeof(enter)) &&
        Bytes(*binding, kLeaveSoundLockRva, leave, sizeof(leave));
    return binding->validated;
}
Operations MakeOperations(Binding* binding) noexcept
{
    if (!binding || !binding->validated || !binding->integration_fence || !binding->integration_snapshot ||
        !binding->try_reader_gate || !binding->leave_reader_gate) return {};
    return {binding, &Try, &Leave, &Fence, &SnapshotNative, &MarkNative, &EvictNative, true};
}

bool ReadNativeInventory(Binding* b,const Plan& plan,NativeInventory* output) noexcept
{
    if (output) output->complete = false;
    if (!b || !b->exact_player_engine_image || !b->read_memory || !output ||
        !output->sources || !output->source_capacity || output->source_capacity > 65536U ||
        !plan.audio_owner || !plan.bank_manager || plan.keep_count > kMaximumKeptBanks ||
        plan.audio_owner > UINTPTR_MAX-kBankManagerOffset-8 || plan.bank_manager > UINTPTR_MAX-176 ||
        b->engine_base > UINTPTR_MAX-kFmodVtableRva) return false;
    for(std::uint32_t i=0;i<plan.keep_count;++i)
        if(!plan.keep[i].name[0]||!std::memchr(plan.keep[i].name,0,kBankNameCapacity))return false;
    output->complete = false; output->source_count = 0; output->banks = output->instances = 0;
    output->pending_bank_reads = output->project_banks = output->unreleased_source_instances = 0;
    output->kept_source_fingerprint = 1469598103934665603ULL;
    output->kept_bank_addresses_match = true;
    const auto read = [&](std::uintptr_t at,void* value,std::size_t bytes) noexcept {
        return at && at <= UINTPTR_MAX - bytes && b->read_memory(b->read_user,at,value,bytes);
    };
    const auto hash = [](const char* text) noexcept {
        std::uint32_t value=0;for(std::size_t i=0;i<kBankNameCapacity&&text[i];++i)
            value=value*41U+static_cast<std::uint32_t>(static_cast<std::int8_t>(text[i]));return value;
    };
    const auto fingerprint = [&](std::uint64_t value) noexcept {
        for(unsigned i=0;i<8;++i){output->kept_source_fingerprint^=static_cast<unsigned char>(value);output->kept_source_fingerprint*=1099511628211ULL;value>>=8;}
    };
    struct Tree {std::uintptr_t root;std::uint32_t count,pad;};
    struct Links {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
    Tree banks={},sources={};std::uintptr_t vtable=0,manager=0;
    if(!read(plan.audio_owner,&vtable,8)||vtable!=b->engine_base+kFmodVtableRva||
       !read(plan.audio_owner+kBankManagerOffset,&manager,8)||manager!=plan.bank_manager||
       !read(manager,&banks,sizeof(banks))||banks.count>kMaximumKeptBanks||
       !read(manager+24,&sources,sizeof(sources))||sources.count>output->source_capacity)return false;
    std::uint32_t bank_hashes[kMaximumKeptBanks]={};
    std::uintptr_t bank_files[kMaximumKeptBanks]={};
    bool kept[kMaximumKeptBanks]={};
    const auto walk = [&](const Tree& tree,auto visit) noexcept {
        struct Node {std::uintptr_t link,parent;};Node stack[64]={};std::size_t depth=0;
        std::uintptr_t current=tree.root,parent=0;std::uint32_t visited=0,previous=0;bool have_previous=false;
        while(current||depth){
            while(current){if(depth==64)return false;Links links={};
                if(!read(current,&links,sizeof(links))||links.parent!=parent||links.color>1)return false;
                stack[depth++]={current,parent};parent=current;current=links.left;
            }
            const auto node=stack[--depth];Links links={};std::uint32_t key=0;
            if(++visited>tree.count||!read(node.link,&links,sizeof(links))||links.parent!=node.parent||
               !visit(node.link,&key)|| (have_previous&&static_cast<std::int32_t>(key)<=static_cast<std::int32_t>(previous)))return false;
            previous=key;have_previous=true;parent=node.link;current=links.right;
        }
        return visited==tree.count;
    };
    if(!walk(banks,[&](std::uintptr_t link,std::uint32_t* key) noexcept {
        if(link<0x48)return false;const auto record=link-0x48;std::uintptr_t file=0;std::uint32_t pending=0;
        if(!read(record,key,4)||!read(record+0x18,&file,8)||!read(record+0x40,&pending,4))return false;
        const auto index=output->banks++;bank_hashes[index]=*key;bank_files[index]=file;
        if(pending==1U)++output->pending_bank_reads;
        bool preserve=false;for(std::uint32_t i=0;i<plan.keep_count;++i)if(hash(plan.keep[i].name)==*key){
            preserve=true;kept[i]=true;
            if(plan.keep[i].record!=record||plan.keep[i].native_file!=file)output->kept_bank_addresses_match=false;
        }
        if(!preserve)++output->project_banks;return true;
    }))return false;
    for(std::uint32_t i=0;i<plan.keep_count;++i)if(!kept[i])output->kept_bank_addresses_match=false;
    if(!walk(sources,[&](std::uintptr_t link,std::uint32_t* key) noexcept {
        if(link<16)return false;SourceReference ref={};
        if(!read(link-16,key,4)||!read(link-8,&ref.source,8)||!ref.source||ref.source>UINTPTR_MAX-48||
           !read(ref.source+24,&ref.property_holder,8)||!read(ref.source+32,&ref.sound_hash,4)||
           !read(ref.source+36,&ref.bank_hash,4)||!read(ref.source+40,&ref.native_file,8)||
           ref.sound_hash!=*key||!ref.property_holder)return false;
        bool bank_found=false,preserve=false;
        for(std::uint32_t i=0;i<output->banks;++i)if(ref.bank_hash==bank_hashes[i]&&ref.native_file==bank_files[i])bank_found=true;
        if(!bank_found)return false;
        for(std::uint32_t i=0;i<plan.keep_count;++i)if(ref.bank_hash==hash(plan.keep[i].name))preserve=true;
        if(preserve){fingerprint(ref.source);fingerprint(ref.property_holder);fingerprint(ref.native_file);fingerprint(ref.sound_hash);fingerprint(ref.bank_hash);}
        output->sources[output->source_count++]=ref;return true;
    }))return false;
    std::uintptr_t instances=0;std::uint32_t count=0,capacity=0,payload=0;
    if(!read(manager+72,&instances,8)||!read(manager+80,&count,4)||!read(manager+84,&capacity,4)||
       count>capacity||capacity>65536U||(count&&!instances)||instances>UINTPTR_MAX-static_cast<std::uintptr_t>(count)*8||
       !read(manager+168,&payload,4)||payload>65536U)return false;
    output->pending_bank_reads+=payload;output->instances=count;
    for(std::uint32_t i=0;i<count;++i){std::uintptr_t instance=0,source=0;std::uint32_t references=0;unsigned char pin=0;
        if(!read(instances+static_cast<std::uintptr_t>(i)*8,&instance,8)||!instance||instance>UINTPTR_MAX-41||
           !read(instance,&source,8)||!read(instance+32,&references,4)||!read(instance+40,&pin,1)||pin>1||references<pin)return false;
        bool found=false;for(std::size_t j=0;j<output->source_count;++j)if(output->sources[j].source==source)found=true;
        if(!found)return false;
        if(references>pin)++output->unreleased_source_instances;
    }
    Tree final_banks={},final_sources={};std::uint32_t final_count=0,final_payload=0;
    if(!read(manager,&final_banks,sizeof(final_banks))||!read(manager+24,&final_sources,sizeof(final_sources))||
       !read(manager+80,&final_count,4)||!read(manager+168,&final_payload,4)||
       std::memcmp(&banks,&final_banks,sizeof(banks))||std::memcmp(&sources,&final_sources,sizeof(sources))||
       final_count!=count||final_payload!=payload)return false;
    output->complete=true;return true;
}

bool MakeWorkerFrame(Binding* b,std::uintptr_t owner,std::uintptr_t caller,
                     std::uint32_t thread,std::uint64_t tick,std::uint64_t completed,
                     bool returned,WorkerFrame* output) noexcept
{
    if (!b || !b->validated) { if (output) *output = {}; return false; }
    return ObserveWorkerFrame(b,owner,caller,thread,tick,completed,returned,output);
}
bool ObserveWorkerFrame(Binding* b,std::uintptr_t owner,std::uintptr_t caller,
                     std::uint32_t thread,std::uint64_t tick,std::uint64_t completed,
                     bool returned,WorkerFrame* output) noexcept
{
    if (!output) return false;
    *output = {};
    if (!b || !b->exact_player_engine_image || !owner || !thread || !b->read_memory ||
        b->engine_base > UINTPTR_MAX-0xAC8D80U ||
        caller != b->engine_base+kWorkerUpdateReturnRva) return false;
    static const unsigned char loop_call[] = {
        0x48,0x8b,0x0d,0x51,0x89,0x9b,0x00,0x48,0x8b,0x01,0xff,0x50,0x18};
    std::uintptr_t singleton = 0, vtable = 0, target = 0;
    if (!Bytes(*b,0x110420U,loop_call,sizeof(loop_call)) ||
        !b->read_memory(b->read_user,b->engine_base+0xAC8D78U,&singleton,sizeof(singleton)) ||
        singleton != owner || !b->read_memory(b->read_user,owner,&vtable,sizeof(vtable)) ||
        vtable != b->engine_base+kFmodVtableRva ||
        !b->read_memory(b->read_user,vtable+0x18U,&target,sizeof(target)) ||
        target != b->engine_base+kWorkerUpdateRva) return false;
    output->audio_owner = owner;
    output->route_identity = caller;
    output->original_completion_generation = completed;
    output->current_tick = tick;
    output->current_thread_id = thread;
    output->exact_native_route = true;
    output->original_returned = returned;
    return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::audio_worker_reset::windows
