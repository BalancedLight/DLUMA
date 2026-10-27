#include "fixes/custom_map_asset_isolation/CraftingNativeGateway.h"
#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <memory>
namespace g=dluma::fixes::custom_map_asset_isolation::crafting_gateway;
namespace o=dluma::fixes::custom_map_asset_isolation::crafting_owner;
namespace s=dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Fixture {static constexpr std::uintptr_t base=0x10000000;g::Gateway gate;o::Owner owner={};o::Manager managers[16]={};
    alignas(8) std::uint8_t game[0xAB0]={},roots[8][96]={};s::Span spans[16]={};std::uint8_t bytes[4096]={};
    std::atomic<unsigned> rebuilds{0},parsers{0},failures{0};std::atomic<bool> block{false},entered{false},release{false};
    std::uint32_t epoch=0;std::uint64_t transaction=0;bool patched=false,coverage=true,post_c10=false;unsigned deletes=0;std::atomic<unsigned> touches{0};bool args_ok=true;
    std::uintptr_t current()const{std::uintptr_t p=0;std::memcpy(&p,game+0xAA0,8);return p;}
};
Fixture* fixture=nullptr;
std::uintptr_t __fastcall Ctor(void* self){return reinterpret_cast<std::uintptr_t>(self);}
std::uintptr_t __fastcall Dtor(void*){return 0;}
std::uintptr_t __fastcall Delete(void*){++fixture->deletes;return 0;}
void __fastcall Parse(void* self,const void* path){auto& x=*fixture;++x.parsers;x.args_ok&=reinterpret_cast<std::uintptr_t>(self)==x.current() && path==reinterpret_cast<void*>(0x7000);}
void __fastcall Load(void* self){for(const auto h:{g::Hook::Effects,g::Hook::Sets,g::Hook::Elementals})for(unsigned i=0;i<2;++i)
    reinterpret_cast<void(__fastcall*)(void*,const void*)>(g::Wrapper(h))(self,reinterpret_cast<void*>(0x7000));}
void __fastcall Rebuild(void* self){auto& x=*fixture;if(x.block.exchange(false)){x.entered.store(true);while(!x.release.load())Sleep(1);}
    const auto before=x.current();if(before)reinterpret_cast<std::uintptr_t(__fastcall*)(void*)>(g::Wrapper(g::Hook::DestroyManager))(reinterpret_cast<void*>(before));
    const auto index=x.rebuilds.fetch_add(1);const auto manager=reinterpret_cast<std::uintptr_t>(x.roots[index]);
    x.args_ok&=self==x.game;std::memcpy(x.game+0xAA0,&manager,8);reinterpret_cast<void(__fastcall*)(void*)>(g::Wrapper(g::Hook::Load))(reinterpret_cast<void*>(manager));}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(p==x.base+0x1D07F88 && n==8){const auto value=reinterpret_cast<std::uintptr_t>(x.game);std::memcpy(out,&value,8);return true;}
    std::size_t count=0;const auto* catalog=g::Catalog(&count);
    for(std::size_t i=0;i<count;++i)if(p==x.base+catalog[i].rva){
        if(!x.patched && n==16){std::memcpy(out,catalog[i].prefix,16);return true;}
        if(x.patched && n==14){std::uint8_t patch[14]={0xff,0x25};const auto target=x.coverage?g::Wrapper(catalog[i].kind):0;
            std::memcpy(patch+6,&target,8);std::memcpy(out,patch,14);return true;}}
    const auto begin=reinterpret_cast<std::uintptr_t>(&x);if(p<begin || n>sizeof(x) || p-begin>sizeof(x)-n)return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Context(void* user,o::Context* out) noexcept {auto& x=*static_cast<Fixture*>(user);*out={};out->game_di=reinterpret_cast<std::uintptr_t>(x.game);
    out->current_manager=x.current();out->baseline_generation=11;out->transaction=x.transaction;out->epoch=x.epoch;out->thread_id=GetCurrentThreadId();
    out->initial_pristine=!x.epoch;out->post_c10_closed=x.post_c10;return true;}
void Touch(void* user,g::Hook) noexcept {auto& x=*static_cast<Fixture*>(user);if(x.epoch)++x.touches;}
void Failure(void* user,g::Hook,o::Result) noexcept {++static_cast<Fixture*>(user)->failures;}
bool Run(){auto owned=std::make_unique<Fixture>();auto& x=*owned;fixture=&x;
    const std::uintptr_t originals[]={reinterpret_cast<std::uintptr_t>(&Ctor),reinterpret_cast<std::uintptr_t>(&Dtor),reinterpret_cast<std::uintptr_t>(&Rebuild),
        reinterpret_cast<std::uintptr_t>(&Delete),reinterpret_cast<std::uintptr_t>(&Load),reinterpret_cast<std::uintptr_t>(&Parse),reinterpret_cast<std::uintptr_t>(&Parse),reinterpret_cast<std::uintptr_t>(&Parse)};
    const g::Callbacks callbacks={Read,Context,Touch,Failure,&x};CHECK(g::Initialize(&x.gate,x.base,c::kPlayerGameDllSha256,callbacks,originals,g::kHookCount));
    o::Access access={};CHECK(g::MakeOwnerAccess(&x.gate,&access));const s::Snapshot storage={x.spans,16,0,x.bytes,sizeof(x.bytes)};
    CHECK(o::Initialize(&x.owner,access,x.managers,16,storage));CHECK(g::Bind(&x.gate,&x.owner));x.patched=true;CHECK(g::VerifyCoverage(&x.gate));
    CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)(void*)>(g::Wrapper(g::Hook::GameConstructor))(x.game)==reinterpret_cast<std::uintptr_t>(x.game));
    CHECK(x.gate.game_generation.load()==1);
    g::DispatchRebuild(&x.gate,reinterpret_cast<std::uintptr_t>(x.game),x.base+0x3BF9B5);CHECK(x.rebuilds==1 && x.parsers==6 && !x.failures);
    g::Lease lease={};CHECK(g::Close(&x.gate,&lease));
    // Model the reservation as the first native entry operation, before its
    // writer can reach the gate or inspect any manager argument.
    x.gate.pending.fetch_add(1);CHECK(o::CaptureBaseline(&x.owner)!=o::Result::Succeeded && !x.owner.baseline_captured);
    CHECK(!access.publish_baseline(access.user,true));x.gate.pending.fetch_sub(1);
    CHECK(access.publish_baseline(access.user,true));x.gate.pending.fetch_add(1);
    CHECK(access.publish_baseline(access.user,false) && x.gate.pending.load()==1);x.gate.pending.fetch_sub(1);
    CHECK(o::CaptureBaseline(&x.owner)==o::Result::Succeeded);CHECK(g::Open(&x.gate,lease));
    x.epoch=4;x.transaction=31;x.block.store(true);
    std::thread first([&](){g::DispatchRebuild(&x.gate,reinterpret_cast<std::uintptr_t>(x.game),x.base+0x3D646E);});
    const auto start=GetTickCount64();while(!x.entered.load() && GetTickCount64()-start<2000)Sleep(1);
    std::thread second([&](){g::DispatchRebuild(&x.gate,reinterpret_cast<std::uintptr_t>(x.game),x.base+0x3D646E);});
    while(!x.gate.pending.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool pending=x.gate.pending.load()==1 && x.rebuilds==1;const bool close_refused=!g::Close(&x.gate,&lease);
    x.release.store(true);first.join();second.join();
    CHECK(pending && close_refused && x.rebuilds==3 && x.parsers==18 && !x.failures && !x.deletes && x.args_ok);
    o::Manager held={};CHECK(o::HeldManager(&x.owner,reinterpret_cast<std::uintptr_t>(x.roots[0]),&held));
    CHECK(o::HeldManager(&x.owner,reinterpret_cast<std::uintptr_t>(x.roots[1]),&held));
    x.post_c10=true;CHECK(g::Close(&x.gate,&lease));CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::Succeeded);CHECK(g::Open(&x.gate,lease));
    CHECK(x.current()==reinterpret_cast<std::uintptr_t>(x.roots[0]) && x.touches>0);
    x.coverage=false;CHECK(!g::Close(&x.gate,&lease));x.coverage=true;
    // A caller outside the proved rebuild->load->parser chain forwards, loses
    // production authority and retains every already-held graph.
    reinterpret_cast<void(__fastcall*)(void*,const void*)>(g::Wrapper(g::Hook::Effects))(reinterpret_cast<void*>(x.current()),reinterpret_cast<void*>(0x7000));
    CHECK(x.parsers==19 && x.failures==1 && x.gate.lost.load() && x.owner.lost && !g::Close(&x.gate,&lease));
    CHECK(o::HeldManager(&x.owner,reinterpret_cast<std::uintptr_t>(x.roots[1]),&held));return true;}
}
int main(){if(!Run())return 1;std::puts("Crafting native gateway: nested ABI, native owner generation, queued enclosing writer, held-root restore, coverage and unknown-path scenarios passed");return 0;}
