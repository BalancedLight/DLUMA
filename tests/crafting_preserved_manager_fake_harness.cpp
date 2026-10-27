#include "fixes/custom_map_asset_isolation/CraftingPreservedManager.h"
#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
namespace o=dluma::fixes::custom_map_asset_isolation::crafting_owner;
namespace s=dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Fixture {static constexpr std::uintptr_t base=0x10000000;std::uint8_t game[0xAB0]={},roots[4][96]={};
    o::Context context={};o::Access access={};o::Owner owner={};o::Manager managers[8]={};s::Span spans[64]={};std::uint8_t bytes[4096]={};
    unsigned native_calls=0,native_deletes=0,writes=0;std::uintptr_t native_receiver=0;
    bool valid_context=true,fail_write=false,fail_native=false,change_owner=false;
    Fixture();void project(){context.epoch=4;context.transaction=31;context.initial_pristine=false;}
};
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(p==x.base+0x1D07F88 && n==8){std::memcpy(out,&x.context.game_di,8);return true;}
    const auto begin=reinterpret_cast<std::uintptr_t>(&x);if(p<begin || n>sizeof(x) || p-begin>sizeof(x)-n)return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Context(void* user,o::Context* out) noexcept {auto& x=*static_cast<Fixture*>(user);*out=x.context;return x.valid_context;}
bool Publish(void*,bool) noexcept {return true;}
bool Compare(void* user,std::uintptr_t p,std::uintptr_t expected,std::uintptr_t desired) noexcept {auto& x=*static_cast<Fixture*>(user);++x.writes;
    if(x.fail_write || p!=x.context.game_di+0xAA0 || x.context.current_manager!=expected)return false;
    x.context.current_manager=desired;std::memcpy(x.game+0xAA0,&desired,8);++x.context.revision;return true;}
bool Rebuild(void* user,std::uintptr_t receiver) noexcept {auto& x=*static_cast<Fixture*>(user);x.native_receiver=receiver;++x.native_calls;
    if(x.fail_native)return false;if(x.context.current_manager)++x.native_deletes;
    x.context.current_manager=reinterpret_cast<std::uintptr_t>(x.roots[x.native_calls-1]);std::memcpy(x.game+0xAA0,&x.context.current_manager,8);++x.context.revision;
    if(x.change_owner)++x.context.game_di_generation;return true;}
Fixture::Fixture(){context={reinterpret_cast<std::uintptr_t>(game),0,3,7,11,0,0,13,true,true,false};access.gamedll_base=base;
    std::memcpy(access.sha256,c::kPlayerGameDllSha256,65);access.read=Read;access.context=Context;access.compare=Compare;access.original_rebuild=Rebuild;access.publish_baseline=Publish;access.user=this;
    const s::Snapshot storage={spans,64,0,bytes,sizeof(bytes)};(void)o::Initialize(&owner,access,managers,8,storage);}
bool Baseline(Fixture& x){CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);CHECK(o::CaptureBaseline(&x.owner)==o::Result::Succeeded);return true;}
bool PreservesAndRestoresRealRoot(){Fixture x;CHECK(Baseline(x));const auto b0=x.context.current_manager;x.project();
    CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);CHECK(x.native_calls==2 && !x.native_deletes && x.context.current_manager!=b0);
    o::Manager record={};CHECK(o::HeldManager(&x.owner,b0,&record) && record.baseline);x.context.post_c10_closed=true;
    CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::Succeeded && x.context.current_manager==b0);
    CHECK(o::HeldManager(&x.owner,reinterpret_cast<std::uintptr_t>(x.roots[1]),&record) && !record.baseline && record.epoch==4);return true;}
bool UnobservedRootCannotBecomeBaseline(){Fixture x;x.context.current_manager=reinterpret_cast<std::uintptr_t>(x.roots[0]);std::memcpy(x.game+0xAA0,&x.context.current_manager,8);
    CHECK(o::CaptureBaseline(&x.owner)==o::Result::UnknownManager);return true;}
bool ReturnedMenuCannotBecomeBaseline(){Fixture x;CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);x.context.initial_pristine=false;
    CHECK(o::CaptureBaseline(&x.owner)==o::Result::WrongScope);return true;}
bool RepeatedReloadRetainsEachOldGraph(){Fixture x;CHECK(Baseline(x));x.project();CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);
    const auto old=x.context.current_manager;CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);o::Manager record={};
    CHECK(x.native_calls==3 && !x.native_deletes && o::HeldManager(&x.owner,old,&record));return true;}
bool FailedTransferRecordsLostAuthority(){Fixture x;CHECK(Baseline(x));x.project();x.fail_write=true;
    CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::WriteFailed);CHECK(x.native_calls==2 && x.native_deletes==1 && x.owner.lost);
    x.context.post_c10_closed=true;CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::InvalidArgument);return true;}
bool MissingContextStillForwardsActualReceiver(){Fixture x;CHECK(Baseline(x));x.valid_context=false;const auto actual=std::uintptr_t{0x76543210};
    CHECK(o::RunNativeRebuild(&x.owner,actual)!=o::Result::Succeeded);CHECK(x.native_calls==2 && x.native_receiver==actual && x.owner.lost);return true;}
bool ChangedBaselineRefusesRestoration(){Fixture x;CHECK(Baseline(x));x.project();CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);
    x.roots[0][16]^=1;x.context.post_c10_closed=true;CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::BaselineChanged);return true;}
bool NativeFailureKeepsTransferredBaseline(){Fixture x;CHECK(Baseline(x));const auto baseline=x.context.current_manager;x.project();x.fail_native=true;
    CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::NativeFailed);o::Manager record={};
    CHECK(x.native_calls==2 && !x.native_deletes && o::HeldManager(&x.owner,baseline,&record));return true;}
bool OwnerGenerationReuseRefuses(){Fixture x;CHECK(Baseline(x));x.project();CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);
    x.context.post_c10_closed=true;++x.context.game_di_generation;CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::WrongScope);return true;}
bool SessionClosureIsRequired(){Fixture x;CHECK(Baseline(x));x.project();CHECK(o::RunNativeRebuild(&x.owner,x.context.game_di)==o::Result::Succeeded);
    CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::WrongScope);return true;}
bool FinalVerificationRejectsRootReplacement(){Fixture x;CHECK(Baseline(x));x.project();x.context.post_c10_closed=true;
    CHECK(o::RestoreBaselineRoot(&x.owner,31)==o::Result::Succeeded);x.context.current_manager=reinterpret_cast<std::uintptr_t>(x.roots[1]);std::memcpy(x.game+0xAA0,&x.context.current_manager,8);
    CHECK(o::VerifyBaselineRoot(&x.owner,31)==o::Result::OwnerChanged);return true;}
}
int main(){bool(*tests[])()={PreservesAndRestoresRealRoot,UnobservedRootCannotBecomeBaseline,ReturnedMenuCannotBecomeBaseline,RepeatedReloadRetainsEachOldGraph,
    FailedTransferRecordsLostAuthority,MissingContextStillForwardsActualReceiver,ChangedBaselineRefusesRestoration,NativeFailureKeepsTransferredBaseline,
    OwnerGenerationReuseRefuses,SessionClosureIsRequired,FinalVerificationRejectsRootReplacement};
    for(auto test:tests)if(!test())return 1;std::printf("Crafting preserved manager: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
