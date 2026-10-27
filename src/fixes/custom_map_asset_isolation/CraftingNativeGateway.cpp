#define _HAS_EXCEPTIONS 0
#include "CraftingNativeGateway.h"
#include "CurveBaselineSnapshot.h"
#include <cstring>
#include <intrin.h>
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
namespace dluma::fixes::custom_map_asset_isolation::crafting_gateway {
namespace {
constexpr Descriptor catalog[]={
    {Hook::GameConstructor,"CraftingGameConstructor",0x3BE5A0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},20},
    {Hook::GameDestructor,"CraftingGameDestructor",0x3BEFC0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x81,0xc1},20},
    {Hook::Rebuild,"CraftingManagerRebuild",0x3DA920,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x99,0xa0,0x0a,0x00},17},
    {Hook::DestroyManager,"CraftingManagerDestroy",0x3DA9C0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48},19},
    {Hook::Load,"CraftingManagerLoad",0x2F44C0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55},16},
    {Hook::Effects,"CraftingEffectsLoad",0x2F4800,{0x40,0x55,0x57,0x41,0x57,0x48,0x8d,0xac,0x24,0x60,0xfb,0xff,0xff,0x48,0x81,0xec},20},
    {Hook::Sets,"CraftingSetsLoad",0x2F4CE0,{0x40,0x55,0x57,0x41,0x54,0x48,0x8d,0xac,0x24,0xf0,0xfc,0xff,0xff,0x48,0x81,0xec},20},
    {Hook::Elementals,"CraftingElementalsLoad",0x2F4F90,{0x48,0x89,0x4c,0x24,0x08,0x55,0x57,0x48,0x8d,0xac,0x24,0x48,0xf8,0xff,0xff,0x48},22},
};
static_assert(sizeof(catalog)/sizeof(catalog[0])==kHookCount,"complete Crafting writer chain");
std::atomic<Gateway*> bridge{nullptr};
constexpr std::uint32_t publication_bit=0x80000000U;
struct Thread {Gateway* gateway;Hook kinds[32];std::uint32_t depth;bool owner_inspection;};
__declspec(thread) Thread thread={};
struct Call {Hook kind;std::uint32_t depth;bool outer,active,owns_thread;};
std::size_t Index(Hook h) noexcept {return static_cast<std::size_t>(h);}
bool Read(Gateway* g,std::uintptr_t p,void* out,std::size_t n) noexcept {return p && p<=UINTPTR_MAX-n && g->callbacks.read(g->callbacks.user,p,out,n);}
bool Parent(Hook h) noexcept {for(unsigned i=0;i+1<thread.depth;++i)if(thread.kinds[i]==h)return true;return false;}
bool CurrentManager(Gateway* g,void* self) noexcept {std::uintptr_t current=0;const auto game=g->game_di.load();
    return game && Read(g,game+0xAA0,&current,8) && current==reinterpret_cast<std::uintptr_t>(self);}
void Loss(Gateway* g,Hook h,crafting_owner::Result result) noexcept {g->lost.store(true);if(g->owner)g->owner->lost=true;g->callbacks.failure(g->callbacks.user,h,result);}
Call Enter(Gateway* g,Hook kind) noexcept {
    const auto tid=GetCurrentThreadId();g->pending.fetch_add(1);g->callbacks.potential_write(g->callbacks.user,kind);
    const bool outer=!thread.depth;bool owns_thread=false;
    if(outer){if(g->metadata_lease && g->writer_thread.load()==tid)Loss(g,kind,crafting_owner::Result::WrongScope);
        else {std::uint32_t empty=0;while(!g->writer_thread.compare_exchange_weak(empty,tid)){empty=0;Sleep(1);}owns_thread=true;}thread.gateway=g;}
    g->pending.fetch_sub(1);g->revision.fetch_add(1);
    if(thread.gateway!=g || thread.depth>=32){Loss(g,kind,crafting_owner::Result::WrongScope);return {kind,0,outer,false,owns_thread};}
    Call result={kind,thread.depth,outer,true,owns_thread};thread.kinds[thread.depth++]=kind;return result;
}
void Leave(Gateway* g,Call* call) noexcept {
    if(!call->active || thread.gateway!=g || thread.depth!=call->depth+1){Loss(g,call->kind,crafting_owner::Result::WrongScope);return;}
    --thread.depth;call->active=false;g->revision.fetch_add(1);
    if(call->outer){thread.gateway=nullptr;thread.owner_inspection=false;if(call->owns_thread)g->writer_thread.store(0);}
}
bool OwnerRead(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {return Read(static_cast<Gateway*>(user),p,out,n);}
bool OwnerContext(void* user,crafting_owner::Context* out) noexcept {
    auto* g=static_cast<Gateway*>(user);const auto tid=GetCurrentThreadId();
    const bool inspection=g->writer_thread.load()==tid && (g->metadata_lease ||
        (thread.gateway==g && thread.depth==1 && thread.kinds[0]==Hook::Rebuild && thread.owner_inspection));
    if(!inspection || (g->metadata_lease.load() && (g->pending.load()&~publication_bit)) || g->lost.load() || !g->game_di.load() || !g->game_generation.load() ||
        !g->callbacks.context(g->callbacks.user,out) || out->game_di!=g->game_di.load() || out->thread_id!=tid)return false;
    out->game_di_generation=g->game_generation.load();out->revision=g->revision.load();out->writers_closed=true;return true;
}
bool Compare(void* user,std::uintptr_t address,std::uintptr_t expected,std::uintptr_t desired) noexcept {
    auto* g=static_cast<Gateway*>(user);crafting_owner::Context context={};
    if(!OwnerContext(g,&context) || address!=context.game_di+0xAA0)return false;
    __try {if(_InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(address),static_cast<long long>(desired),static_cast<long long>(expected))!=static_cast<long long>(expected))return false;
        g->revision.fetch_add(1);return true;}
    __except(1){Loss(g,Hook::Rebuild,crafting_owner::Result::WriteFailed);return false;}
}
bool OriginalRebuild(void* user,std::uintptr_t receiver) noexcept {
    auto* g=static_cast<Gateway*>(user);
    if(thread.gateway!=g || !thread.depth || thread.kinds[thread.depth-1]!=Hook::Rebuild)return false;
    thread.owner_inspection=false;
    __try {reinterpret_cast<void(__fastcall*)(void*)>(g->originals[Index(Hook::Rebuild)])(reinterpret_cast<void*>(receiver));thread.owner_inspection=true;return true;}
    __except(1){thread.owner_inspection=true;return false;}
}
bool PublishBaseline(void* user,bool begin) noexcept {
    auto* g=static_cast<Gateway*>(user);
    if(g->writer_thread.load()!=GetCurrentThreadId() || !g->metadata_lease.load())return false;
    if(begin){if(g->lost.load())return false;std::uint32_t empty=0;return g->pending.compare_exchange_strong(empty,publication_bit);}
    return (g->pending.fetch_and(~publication_bit)&publication_bit)!=0;
}
#define ENTER(name) auto* g=bridge.load();auto call=Enter(g,Hook::name)
#define ORIGINAL(type,name) reinterpret_cast<type>(g->originals[Index(Hook::name)])
#define LEAVE() __finally{Leave(g,&call);}
std::uintptr_t __fastcall HookGameConstructor(void* self){ENTER(GameConstructor);
    __try {const auto result=ORIGINAL(std::uintptr_t(__fastcall*)(void*),GameConstructor)(self);
        if(result!=reinterpret_cast<std::uintptr_t>(self) || g->game_di.load())Loss(g,Hook::GameConstructor,crafting_owner::Result::OwnerChanged);
        else {g->game_di.store(result);g->game_generation.fetch_add(1);}return result;}LEAVE()}
std::uintptr_t __fastcall HookGameDestructor(void* self){ENTER(GameDestructor);
    __try {if(g->game_di.load()==reinterpret_cast<std::uintptr_t>(self)){g->game_di.store(0);g->game_generation.fetch_add(1);if(g->owner && g->owner->baseline_captured)g->owner->lost=true;}
        return ORIGINAL(std::uintptr_t(__fastcall*)(void*),GameDestructor)(self);}LEAVE()}
void __fastcall HookRebuild(void* self){DispatchRebuild(bridge.load(),reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(_ReturnAddress()));}
std::uintptr_t __fastcall HookDestroyManager(void* self){ENTER(DestroyManager);
    __try {if(!Parent(Hook::Rebuild) || !CurrentManager(g,self))Loss(g,Hook::DestroyManager,crafting_owner::Result::WrongScope);
        return ORIGINAL(std::uintptr_t(__fastcall*)(void*),DestroyManager)(self);}LEAVE()}
void __fastcall HookLoad(void* self){ENTER(Load);
    __try {if(!Parent(Hook::Rebuild) || !CurrentManager(g,self))Loss(g,Hook::Load,crafting_owner::Result::WrongScope);ORIGINAL(void(__fastcall*)(void*),Load)(self);}LEAVE()}
#define PARSER(name) void __fastcall Hook##name(void* self,const void* path){ENTER(name); \
    __try {if(!Parent(Hook::Rebuild) || !Parent(Hook::Load) || !CurrentManager(g,self))Loss(g,Hook::name,crafting_owner::Result::WrongScope); \
        ORIGINAL(void(__fastcall*)(void*,const void*),name)(self,path);}LEAVE()}
PARSER(Effects)
PARSER(Sets)
PARSER(Elementals)
#undef PARSER
#undef ENTER
#undef ORIGINAL
#undef LEAVE
}
const Descriptor* Catalog(std::size_t* count) noexcept {if(count)*count=kHookCount;return catalog;}
void DispatchRebuild(Gateway* g,std::uintptr_t receiver,std::uintptr_t caller) noexcept {
    if(!g || !g->initialized || !g->owner || !receiver)return;
    auto call=Enter(g,Hook::Rebuild);thread.owner_inspection=true;
    __try {if(caller!=g->base+0x3BF9B5 && caller!=g->base+0x3D646E)Loss(g,Hook::Rebuild,crafting_owner::Result::WrongScope);
        const auto result=crafting_owner::RunNativeRebuild(g->owner,receiver);
        if(result!=crafting_owner::Result::Succeeded)Loss(g,Hook::Rebuild,result);}
    __finally {Leave(g,&call);}
}
std::uintptr_t Wrapper(Hook h) noexcept {switch(h){
#define CASE(name) case Hook::name:return reinterpret_cast<std::uintptr_t>(&Hook##name)
    CASE(GameConstructor);CASE(GameDestructor);CASE(Rebuild);CASE(DestroyManager);CASE(Load);CASE(Effects);CASE(Sets);CASE(Elementals);
#undef CASE
    default:return 0;}}
bool Initialize(Gateway* g,std::uintptr_t base,const char* hash,const Callbacks& callbacks,const std::uintptr_t* originals,std::size_t count) noexcept {
    if(!g || g->initialized || !base || !hash || std::strlen(hash)!=64 || std::memcmp(hash,curves::kPlayerGameDllSha256,65) ||
        !callbacks.read || !callbacks.context || !callbacks.potential_write || !callbacks.failure || !originals || count!=kHookCount)return false;
    g->base=base;g->callbacks=callbacks;
    for(std::size_t i=0;i<count;++i){std::uint8_t bytes[16];if(!originals[i] || !Read(g,base+catalog[i].rva,bytes,16) || std::memcmp(bytes,catalog[i].prefix,16))return false;g->originals[i]=originals[i];}
    g->initialized=true;return true;
}
bool Bind(Gateway* g,crafting_owner::Owner* owner) noexcept {if(!g || !g->initialized || !owner || !owner->initialized)return false;
    Gateway* empty=nullptr;if(!bridge.compare_exchange_strong(empty,g) && empty!=g)return false;g->owner=owner;return true;}
bool MakeOwnerAccess(Gateway* g,crafting_owner::Access* out) noexcept {if(!g || !out || !g->initialized)return false;*out={};out->gamedll_base=g->base;
    std::memcpy(out->sha256,curves::kPlayerGameDllSha256,65);out->read=OwnerRead;out->context=OwnerContext;out->compare=Compare;
    out->original_rebuild=OriginalRebuild;out->publish_baseline=PublishBaseline;out->user=g;return true;}
bool VerifyCoverage(Gateway* g) noexcept {if(!g || !g->initialized)return false;
    for(const auto& d:catalog){std::uint8_t patch[14]={};std::uintptr_t target=0;if(!Read(g,g->base+d.rva,patch,14) ||
        patch[0]!=0xff || patch[1]!=0x25 || patch[2] || patch[3] || patch[4] || patch[5])return false;
        std::memcpy(&target,patch+6,8);if(target!=Wrapper(d.kind))return false;}return true;}
bool Close(Gateway* g,Lease* out) noexcept {if(!g || !out || !g->initialized || g->lost.load() || thread.depth || g->metadata_lease || !VerifyCoverage(g))return false;
    const auto tid=GetCurrentThreadId();std::uint32_t empty=0;if(!g->writer_thread.compare_exchange_strong(empty,tid))return false;
    if(g->pending.load()){g->writer_thread.store(0);return false;}
    g->metadata_lease=true;g->lease={g->lease_generation.fetch_add(1)+1,tid};*out=g->lease;return true;}
bool Open(Gateway* g,const Lease& lease) noexcept {if(!g || g->writer_thread.load()!=GetCurrentThreadId() || !g->metadata_lease ||
    !lease.generation || lease.generation!=g->lease.generation || lease.thread_id!=GetCurrentThreadId() || (g->pending.load()&publication_bit))return false;
    g->metadata_lease=false;g->lease={};g->writer_thread.store(0);return true;}
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_gateway
