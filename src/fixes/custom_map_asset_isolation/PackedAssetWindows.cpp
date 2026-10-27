#include "PackedAssetWindows.h"
#include "../../dlw_win32.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::packed::windows {
namespace rw = retained::windows;
namespace {
using TrySection = int (WINAPI*)(void*);
using LeaveSection = void (WINAPI*)(void*);
using AcquireRenderer = void (__fastcall*)(void*, const char*);
using ReleaseRenderer = void (__fastcall*)(void*);
template<class T> T Export(const char* name) noexcept
{ return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(reinterpret_cast<LPCWSTR>(L"kernel32.dll")), name)); }
bool Read(const Binding& b, std::uintptr_t address, void* out, std::size_t bytes) noexcept
{ return b.read && out && address && bytes && bytes - 1 <= UINTPTR_MAX - address && b.read(b.read_user,address,out,bytes); }
template<class T> bool Field(const Binding& b, std::uintptr_t object, std::size_t offset, T* out) noexcept
{ return offset <= UINTPTR_MAX - object && Read(b,object+offset,out,sizeof(*out)); }
bool Prefix(const Binding& b, std::uintptr_t rva, const unsigned char* expected, std::size_t size) noexcept
{
    unsigned char bytes[32] = {}; return size <= sizeof(bytes) &&
        Read(b,b.engine_base+rva,bytes,size) && !std::memcmp(bytes,expected,size);
}
bool Name(const Binding& b, std::uintptr_t pointer, char* out) noexcept
{
    std::memset(out,0,retained::kNameCapacity);
    if (!pointer) return true;
    for (std::size_t i=0;i<retained::kNameCapacity;++i) {
        if (i>UINTPTR_MAX-pointer || !Read(b,pointer+i,out+i,1)) return false;
        if (!out[i]) return true;
    }
    return false;
}
NativePack* Find(Snapshot& snapshot, std::uintptr_t pack) noexcept
{ for(std::size_t i=0;i<snapshot.pack_count;++i) if(snapshot.packs[i].object==pack)return &snapshot.packs[i];return nullptr; }
bool SameFence(const Fence& a,const Fence& b) noexcept
{
    return a.transaction==b.transaction && a.owner_generation==b.owner_generation &&
        a.mutation_revision==b.mutation_revision && a.epoch==b.epoch && a.thread_id==b.thread_id &&
        a.authoritative_post_destroy==b.authoritative_post_destroy && a.producers_closed==b.producers_closed &&
        a.consumers_drained==b.consumers_drained && a.renderer_lock_owned==b.renderer_lock_owned &&
        a.worker_lock_owned==b.worker_lock_owned;
}
Result Texture(const Binding& b,std::uintptr_t object,std::uintptr_t pack,retained::NativeState* out) noexcept
{
    *out={};out->object=object;out->manager=b.engine_base+rw::kTextureManagerRva;out->kind=retained::Kind::Texture;
    std::uintptr_t name=0;std::uint32_t backend_format=0;
    if(!Field(b,object,0,&out->vtable) || out->vtable!=b.engine_base+rw::kTextureVtableRva ||
        !Field(b,object,8,&out->backend) || !Field(b,object,52,&out->flags) ||
        !Field(b,object,56,&backend_format)||(out->backend&&((backend_format>>24)&15)>2)||
        !Field(b,object,60,&out->references) || !Field(b,object,64,&out->callback_context) ||
        !Field(b,object,72,&out->callback) || !Field(b,object,80,&out->deferred) || out->deferred!=pack ||
        !Field(b,object,96,&name) || !Name(b,name,out->name))return Result::Incomplete;
    out->definition=backend_format;return Result::Succeeded;
}
Result Pack(const Binding& b,std::uintptr_t object,Snapshot* out) noexcept
{
    NativePack& p=out->packs[out->pack_count];p={};p.object=object;p.texture_begin=out->texture_count;
    std::uintptr_t groups=0;std::uint32_t group_capacity=0;
    if(!Field(b,object,0,&p.file) || !Field(b,object,16,&p.overlapped) || !Field(b,object,24,&p.flags) ||
        !Field(b,object,28,&p.references) || !Field(b,object,32,&p.load_flags) ||
        !Field(b,object,72,&groups) || !Field(b,object,80,&p.physical_group_count) ||
        !Field(b,object,84,&group_capacity) || p.physical_group_count>group_capacity ||
        p.physical_group_count>kMaximumPhysicalGroups || (p.physical_group_count && !groups) ||
        !Field(b,object,192,&p.metadata) || !Field(b,object,200,&p.patch))return Result::Incomplete;
    for(std::uint32_t i=0;i<p.physical_group_count;++i){
        std::uint32_t flags=0;if(!Field(b,groups,64ULL*i,&flags))return Result::Incomplete;
        const auto type=static_cast<std::uint8_t>(flags);p.resource_types[type/64]|=UINT64_C(1)<<(type%64);
    }
    if(p.metadata){
        std::uintptr_t named_link=0,key_pack=0,texture_vector=0;
        std::uint32_t texture_count=0,texture_capacity=0;
        if(!Field(b,p.metadata,64,&named_link) || named_link!=p.metadata+104 ||
            !Field(b,p.metadata,72,&key_pack) || key_pack!=object ||
            !Field(b,p.metadata,24,&p.mesh_count) || !Field(b,p.metadata,40,&texture_vector) ||
            !Field(b,p.metadata,48,&texture_count) || !Field(b,p.metadata,52,&texture_capacity) ||
            texture_count>texture_capacity || texture_count>out->texture_capacity-out->texture_count ||
            (texture_count && !texture_vector))return Result::Incomplete;
        for(std::uint32_t i=0;i<texture_count;++i){
            std::uintptr_t texture=0;if(!Field(b,texture_vector,8ULL*i,&texture))return Result::Incomplete;
            for(std::size_t j=0;j<out->texture_count;++j)
                if(out->textures[j].object==texture)return Result::StaleIdentity;
            Result result=Texture(b,texture,object,&out->textures[out->texture_count]);
            if(result!=Result::Succeeded)return result;++out->texture_count;
        }
        p.texture_count=texture_count;
    }
    ++out->pack_count;return Result::Succeeded;
}
Result Queues(const Binding& b,Snapshot* out) noexcept
{
    constexpr std::uintptr_t offsets[]={112,184,256,328};
    for(unsigned queue=0;queue<4;++queue){
        std::uintptr_t first=0,last=0;std::uint32_t count=0;
        if(!Field(b,b.runtime,offsets[queue],&first) || !Field(b,b.runtime,offsets[queue]+8,&last) ||
            !Field(b,b.runtime,offsets[queue]+16,&count) || count>kMaximumPackTextures ||
            ((!count)!=(!first)) || ((!count)!=(!last)))return Result::Incomplete;
        std::uintptr_t link=first,previous=0;
        for(std::uint32_t i=0;i<count;++i){
            if(!link || link<8)return Result::Incomplete;
            std::uintptr_t back=0,next=0,entry=0,pack=0;
            if(!Field(b,link,0,&next) || !Field(b,link,8,&back) || back!=previous ||
                !Read(b,link-8,&entry,8) || !Field(b,entry,24,&pack))return Result::Incomplete;
            NativePack* owner=Find(*out,pack);if(!owner)return Result::UnknownPack;
            ++owner->queued_entries[queue];previous=link;link=next;
        }
        if(link || previous!=last)return Result::Incomplete;
    }
    return Result::Succeeded;
}
bool CurrentKey(Binding& b,Key key) noexcept
{
    Key active={};return b.ledger && FindActive(b.ledger,key.address,&active)==Result::Succeeded &&
        active.slot==key.slot && active.generation==key.generation && key.slot<b.ledger->count &&
        b.ledger->records[key.slot].project && !b.ledger->records[key.slot].baseline &&
        !b.ledger->records[key.slot].global && !b.ledger->lost;
}
Result CaptureOperation(void* user,Snapshot* out) noexcept {return Capture(static_cast<Binding*>(user),out);}
Result EvictOperation(void* user,const Key& key) noexcept
{
    auto* b=static_cast<Binding*>(user);Fence fence={};
    if(!b || !ReadFence(b,&fence) || !fence.authoritative_post_destroy || !CurrentKey(*b,key) ||
        !b->authorize || !b->authorize(b->gateway_user,NativeOperation::Evict,key,true))return Result::FenceUnavailable;
    bool returned=false,streams=false,authority=false;
    if(b->stream_mutation&&!b->stream_mutation(b->stream_user,true,false)){
        (void)b->authorize(b->gateway_user,NativeOperation::Evict,key,false);return Result::FenceUnavailable;}
    __try{b->original_evict(reinterpret_cast<void*>(b->runtime),reinterpret_cast<void*>(key.address));returned=true;}
    __finally{streams=!b->stream_mutation||b->stream_mutation(b->stream_user,false,returned);
        authority=b->authorize(b->gateway_user,NativeOperation::Evict,key,false);}
    return returned&&streams&&authority?Result::Succeeded:Result::NativeFailure;
}
Result ReleaseOperation(void* user,const Key& key,bool zero) noexcept
{
    auto* b=static_cast<Binding*>(user);Fence fence={};std::uint32_t references=0;
    if(!b || !ReadFence(b,&fence) || !fence.authoritative_post_destroy || !CurrentKey(*b,key) ||
        !Field(*b,key.address,28,&references) || (zero?references!=0:references==0))return Result::FenceUnavailable;
    const auto operation=zero?NativeOperation::ReleaseEmptyPack:NativeOperation::ReleaseReference;
    if(!b->authorize || !b->authorize(b->gateway_user,operation,key,true))return Result::FenceUnavailable;
    bool returned=false,streams=false,authority=false;
    if(b->stream_mutation&&!b->stream_mutation(b->stream_user,true,false)){
        (void)b->authorize(b->gateway_user,operation,key,false);return Result::FenceUnavailable;}
    __try{
        if(zero)b->original_release_pack(reinterpret_cast<void*>(b->runtime),reinterpret_cast<void*>(key.address),false);
        else b->original_release_reference(reinterpret_cast<void*>(b->engine_base+kAssetManagerRva),reinterpret_cast<void*>(key.address));
        returned=true;
    }__finally{streams=!b->stream_mutation||b->stream_mutation(b->stream_user,false,returned);
        authority=b->authorize(b->gateway_user,operation,key,false);}
    return returned&&streams&&authority?Result::Succeeded:Result::NativeFailure;
}
}
Result Validate(Binding* b) noexcept
{
    if(!b || b->owner_thread)return Result::InvalidArgument;b->validated=false;
    if(!b->engine_base || b->engine_base>UINTPTR_MAX-0xE8D000 || !b->read || !b->producer_fence ||
        !b->authorize || !b->ledger || !b->texture_ledger ||
        std::memcmp(b->engine_sha256,rw::kPlayerEngineSha256,sizeof(rw::kPlayerEngineSha256)))return Result::Incomplete;
    // Entry identities are checked before any routing write. Hook installation
    // supplies original relays separately; the gateway verifies installed routes.
    constexpr unsigned char release_ref[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x0d,0xcd,0xc5,0x67,0x00,
        0xc7,0x44,0x24,0x38,0x00,0x00,0x00,0x00};
    constexpr unsigned char evict[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
    constexpr unsigned char release_pack[]={0x48,0x85,0xd2,0x0f,0x84,0xc9,0x00,0x00,0x00,
        0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    if(!Prefix(*b,kAssetReleaseRva,release_ref,sizeof(release_ref)) ||
        !Prefix(*b,kPackEvictRva,evict,sizeof(evict)) || !Prefix(*b,kPackReleaseRva,release_pack,sizeof(release_pack)))
        return Result::Incomplete;
    if(!b->original_evict)b->original_evict=reinterpret_cast<EvictPackFn>(b->engine_base+kPackEvictRva);
    if(!b->original_release_reference)b->original_release_reference=reinterpret_cast<ReleaseReferenceFn>(b->engine_base+kAssetReleaseRva);
    if(!b->original_release_pack)b->original_release_pack=reinterpret_cast<ReleasePackFn>(b->engine_base+kPackReleaseRva);
    b->validated=true;return Result::Succeeded;
}
Result EnterGate(Binding* b) noexcept
{
    if(!b || !b->validated || b->owner_thread)return Result::InvalidArgument;
    Fence before={};if(!b->producer_fence(b->gateway_user,&before) || !before.producers_closed ||
        !before.consumers_drained || before.thread_id!=GetCurrentThreadId())return Result::FenceUnavailable;
    const auto enter=Export<TrySection>("TryEnterCriticalSection");
    const auto leave=Export<LeaveSection>("LeaveCriticalSection");
    std::uintptr_t runtime=0,runtime_vtable=0,renderer=0,renderer_vtable=0,asset_vtable=0;
    unsigned char stopping=0,locked=0,manager_flags=0;
    if(!enter || !leave || !Field(*b,b->engine_base,kRuntimeGlobalRva,&runtime) ||
        !Field(*b,b->engine_base+rw::kTextureManagerRva,156,&manager_flags)||(manager_flags&8)||
        !Field(*b,runtime,0,&runtime_vtable) || runtime_vtable!=b->engine_base+kRuntimeVtableRva ||
        !Field(*b,runtime,568,&stopping) || stopping ||
        !Field(*b,b->engine_base,rw::kRendererGlobalRva,&renderer) || !Field(*b,renderer,0,&renderer_vtable) ||
        renderer_vtable!=b->engine_base+rw::kRendererVtableRva || !Field(*b,renderer,360,&locked) || locked!=1 ||
        !Field(*b,b->engine_base+kAssetManagerRva,0,&asset_vtable) || asset_vtable!=b->engine_base+kAssetManagerVtableRva)
        return Result::FenceUnavailable;
    // Native OnDemandEvictAllFromPack and the complete worker pass take this
    // outer worker section before renderer and individual queue sections.
    if(!enter(reinterpret_cast<void*>(runtime+424)))return Result::FenceUnavailable;
    b->runtime=runtime;b->renderer=renderer;b->owner_thread=GetCurrentThreadId();b->worker_section_owned=true;
    if(!enter(reinterpret_cast<void*>(renderer+272))){LeaveGate(b);return Result::FenceUnavailable;}
    b->renderer_section_owned=true;
    reinterpret_cast<AcquireRenderer>(b->engine_base+rw::kRendererAcquireRva)(reinterpret_cast<void*>(renderer),"DLUMA packed asset reset");
    b->renderer_acquired=true;
    if(!enter(reinterpret_cast<void*>(b->engine_base+rw::kTextureManagerRva+32))){LeaveGate(b);return Result::FenceUnavailable;}
    b->texture_section_owned=true;
    Fence after={};if(!ReadFence(b,&after) || before.transaction!=after.transaction ||
        before.epoch!=after.epoch || before.owner_generation!=after.owner_generation){LeaveGate(b);return Result::FenceUnavailable;}
    return Result::Succeeded;
}
void LeaveGate(Binding* b) noexcept
{
    if(!b || !b->owner_thread || b->owner_thread!=GetCurrentThreadId())return;
    const auto leave=Export<LeaveSection>("LeaveCriticalSection");if(!leave)return;
    if(b->texture_section_owned)leave(reinterpret_cast<void*>(b->engine_base+rw::kTextureManagerRva+32));
    if(b->renderer_acquired)reinterpret_cast<ReleaseRenderer>(b->engine_base+rw::kRendererReleaseRva)(reinterpret_cast<void*>(b->renderer));
    if(b->renderer_section_owned)leave(reinterpret_cast<void*>(b->renderer+272));
    if(b->worker_section_owned)leave(reinterpret_cast<void*>(b->runtime+424));
    b->texture_section_owned=b->renderer_acquired=b->renderer_section_owned=b->worker_section_owned=false;
    b->owner_thread=0;b->runtime=b->renderer=0;
}
bool ReadFence(Binding* b,Fence* out) noexcept
{
    if(!b || !out || !b->validated || !b->owner_thread || b->owner_thread!=GetCurrentThreadId() ||
        !b->worker_section_owned || !b->renderer_section_owned || !b->renderer_acquired || !b->texture_section_owned ||
        !b->producer_fence(b->gateway_user,out) || out->thread_id!=b->owner_thread ||
        !out->producers_closed || !out->consumers_drained)return false;
    std::uintptr_t runtime=0,renderer=0,vtable=0;std::uint32_t depth=0;unsigned char locked=0,manager_flags=0;
    if(!Field(*b,b->engine_base,kRuntimeGlobalRva,&runtime) || runtime!=b->runtime ||
        !Field(*b,b->engine_base+rw::kTextureManagerRva,156,&manager_flags)||(manager_flags&8)||
        !Field(*b,runtime,0,&vtable) || vtable!=b->engine_base+kRuntimeVtableRva ||
        !Field(*b,b->engine_base,rw::kRendererGlobalRva,&renderer) || renderer!=b->renderer ||
        !Field(*b,renderer,0,&vtable) || vtable!=b->engine_base+rw::kRendererVtableRva ||
        !Field(*b,renderer,316,&depth) || !depth || !Field(*b,renderer,360,&locked) || locked!=1)return false;
    out->renderer_lock_owned=true;out->worker_lock_owned=true;return true;
}
Result Capture(Binding* b,Snapshot* out) noexcept
{
    if(!out)return Result::InvalidArgument;out->complete=false;out->pack_count=out->texture_count=0;
    if(!b || !b->validated || !out->packs || !out->textures || !out->pack_capacity || out->pack_capacity>kMaximumPacks ||
        !out->texture_capacity || out->texture_capacity>kMaximumPackTextures)return Result::InvalidArgument;
    Fence before={},after={};if(!ReadFence(b,&before))return Result::FenceUnavailable;
    std::uintptr_t data=0,final_data=0;std::uint32_t count=0,capacity=0,final_count=0;
    if(!Field(*b,b->runtime,80,&data) || !Field(*b,b->runtime,88,&count) || !Field(*b,b->runtime,92,&capacity) ||
        count>capacity || count>out->pack_capacity || (count && !data))return Result::Capacity;
    for(std::uint32_t i=0;i<count;++i){
        std::uintptr_t pack=0;if(!Field(*b,data,8ULL*i,&pack) || !pack || Find(*out,pack))return Result::StaleIdentity;
        Result result=Pack(*b,pack,out);if(result!=Result::Succeeded)return result;
    }
    Result result=Queues(*b,out);if(result!=Result::Succeeded)return result;
    for(std::size_t i=0;i<out->pack_count;++i){auto& pack=out->packs[i];Key key={};
        if(b->stream_proof&&FindActive(b->ledger,pack.object,&key)==Result::Succeeded&&b->ledger->records[key.slot].project){
            pack.renderer_streams_closed=b->stream_proof(b->stream_user,pack,out->textures+pack.texture_begin,pack.texture_count);
            if(!pack.renderer_streams_closed)return Result::PendingNativeCall;
        }
    }
    if(!Field(*b,b->runtime,80,&final_data) || !Field(*b,b->runtime,88,&final_count) || final_data!=data || final_count!=count ||
        !ReadFence(b,&after) || !SameFence(before,after))return Result::VerificationFailed;
    out->runtime=b->runtime;out->runtime_vtable=b->engine_base+kRuntimeVtableRva;out->fence=after;out->complete=true;
    return Result::Succeeded;
}
Operations MakeOperations(Binding* b) noexcept {return {b,CaptureOperation,EvictOperation,ReleaseOperation};}
} // namespace
