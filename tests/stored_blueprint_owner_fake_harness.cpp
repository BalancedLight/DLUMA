#include "fixes/custom_map_asset_isolation/StoredBlueprintOwner.h"
#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
#include <memory>
namespace b=dluma::fixes::custom_map_asset_isolation::stored_blueprints;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
using R=b::Result;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
struct Node {std::uint64_t key;std::uintptr_t value,left,parent,right;std::uint32_t color,pad;};
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    b::Owner owner={};b::BlueprintCopy copies[4]={};b::Binding work[8]={};b::Access access={};b::Fence fence={};
    c::CurveBorrowBoundary borrowers;c::CurveNativeScope scope={};DlumaGameFrameAuthorityState authority={};
    alignas(8) unsigned char game[0x600]={},session[0x7BF0]={},state[0x480]={},context[4360]={},items[2][192]={},blueprints[2][176]={};
    Node nodes[2]={};Header store={};unsigned writes=0,fail_write=0,fence_reads=0;bool change_fence=false;
    Fixture();~Fixture(){if(scope.active)c::EndTypedCurveNativeScope(&borrowers,GetCurrentThreadId(),&scope);}
    std::uintptr_t param(unsigned i)const{std::uintptr_t value=0;std::memcpy(&value,items[i]+64,8);return value;}
    template<class T> void put(unsigned char* p,const T& value){std::memcpy(p,&value,sizeof(value));}
};
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* object,std::size_t size){auto start=reinterpret_cast<std::uintptr_t>(object);
    if(p<start || n>size || p-start>size-n)return false;std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n)noexcept{
    auto& f=*static_cast<Fixture*>(user);if(p==f.base+0x1D07F88 && n==8){auto game=reinterpret_cast<std::uintptr_t>(f.game);std::memcpy(out,&game,8);return true;}
    return Copy(p,out,n,f.game,sizeof(f.game))||Copy(p,out,n,f.session,sizeof(f.session))||Copy(p,out,n,f.state,sizeof(f.state))||
        Copy(p,out,n,f.context,sizeof(f.context))||Copy(p,out,n,f.items,sizeof(f.items))||Copy(p,out,n,f.blueprints,sizeof(f.blueprints))||
        Copy(p,out,n,f.nodes,sizeof(f.nodes))||Copy(p,out,n,&f.store,sizeof(f.store));
}
bool Fence(void* user,b::Fence* out)noexcept{auto& f=*static_cast<Fixture*>(user);*out=f.fence;if(f.change_fence && ++f.fence_reads>1)++out->owner_revision;return true;}
bool Write(void* user,std::uintptr_t address,std::uintptr_t before,std::uintptr_t after)noexcept{
    auto& f=*static_cast<Fixture*>(user);if(++f.writes==f.fail_write)return false;
    for(unsigned i=0;i<2;++i)if(address==reinterpret_cast<std::uintptr_t>(f.items[i]+64)){if(f.param(i)!=before)return false;f.put(f.items[i]+64,after);return true;}return false;
}
Fixture::Fixture(){
    const auto tid=GetCurrentThreadId();authority.exact_route_identity_seeded=1;authority.game_frame_authority_frozen=1;
    authority.game_frame_authority_proof=DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF;authority.exact_route_serialization_token=55;
    authority.exact_route_game_subobject=0x1000;authority.exact_route_game_primary=0x2000;authority.exact_route_caller_module_base=0x3000;authority.exact_route_caller_rva=0x4000;
    auto& t=authority.trace[0];t.sequence=10;t.thread_id=static_cast<LONG>(tid);t.game_subobject=0x1000;t.game_primary=0x2000;t.caller_module_base=0x3000;t.caller_rva=0x4000;
    t.serialization_token=55;t.callback_entry_qpc=100;t.concurrent_depth=1;t.recursive_depth=1;t.route_proof=DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF;
    t.authority_result=DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH;authority.trace_count=1;authority.trace_write_index=1;authority.last_route_sequence=10;authority.current_dispatch_thread_id=static_cast<LONG>(tid);
    c::BeginTypedCurveNativeScope(&borrowers,&authority,tid,110,7,c::TypedCurveCaller::NativeSessionTeardown,&scope);
    fence.game_di=reinterpret_cast<std::uintptr_t>(game);fence.session=reinterpret_cast<std::uintptr_t>(session);fence.blueprint_store=reinterpret_cast<std::uintptr_t>(&store);
    fence.players[0]={reinterpret_cast<std::uintptr_t>(state),3};fence.player_count=1;fence.epoch=2;fence.thread_id=tid;fence.teardown_generation=7;fence.frame_route_token=55;fence.owner_revision=9;
    put(game+0x508,fence.session);put(session+0x7BE0,fence.blueprint_store);put(state,base+0x17E2FF8);put(state+0x468,reinterpret_cast<std::uintptr_t>(context));
    Header h={reinterpret_cast<std::uintptr_t>(items),2,2};put(context+24,h);
    for(unsigned i=0;i<2;++i){put(items[i]+24,base+b::kItemContextVtableRva);put(items[i]+64,reinterpret_cast<std::uintptr_t>(blueprints[i]));
        for(unsigned j=16;j<176;++j)blueprints[i][j]=static_cast<unsigned char>(j+i);
        put(blueprints[i],base+b::kBlueprintVtableRva);put(blueprints[i]+8,static_cast<std::uint64_t>(11*(i+1)));
        nodes[i].key=11*(i+1);nodes[i].value=reinterpret_cast<std::uintptr_t>(blueprints[i]);}
    nodes[0].right=reinterpret_cast<std::uintptr_t>(&nodes[1].left);nodes[1].parent=reinterpret_cast<std::uintptr_t>(&nodes[0].left);
    store={reinterpret_cast<std::uintptr_t>(&nodes[0].left),2,0};access.gamedll_base=base;std::memcpy(access.sha256,c::kPlayerGameDllSha256,65);
    access.read=Read;access.compare_pointer=Write;access.fence=Fence;access.user=this;access.borrowers=&borrowers;access.teardown_scope=&scope;
    b::Initialize(&owner,copies,4,work,8);
}
bool CopiesSurviveOriginalStorageDestruction(){auto f=std::make_unique<Fixture>();CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::Succeeded);
    CHECK(f->owner.copy_count==2 && f->writes==2);CHECK(!std::memcmp(reinterpret_cast<void*>(f->param(0)),f->blueprints[0],176));
    std::memset(f->blueprints,0,sizeof(f->blueprints));std::uint64_t id=0;CHECK(b::ValidateOwnedBlueprint(&f->owner,f->param(0),&id) && id==11);
    CHECK(b::ValidateOwnedBlueprint(&f->owner,f->param(1),&id) && id==22);return true;}
bool SharedSourceGetsOneOwnedCopy(){auto f=std::make_unique<Fixture>();f->put(f->items[1]+64,f->param(0));
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::Succeeded);CHECK(f->owner.copy_count==1 && f->param(0)==f->param(1));return true;}
bool RepeatedPinDoesNotReplaceOwnedPayload(){auto f=std::make_unique<Fixture>();CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::Succeeded);auto first=f->param(0);
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::Succeeded);CHECK(f->owner.copy_count==2 && f->writes==2 && first==f->param(0));return true;}
bool WrongScopeCannotPin(){auto f=std::make_unique<Fixture>();CHECK(c::EndTypedCurveNativeScope(&f->borrowers,GetCurrentThreadId(),&f->scope)==c::CurveBorrowResult::Succeeded);
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::WrongScope && !f->writes);return true;}
bool ScopeCopiedTokenDoesNotRemainActive(){auto f=std::make_unique<Fixture>();auto stale=f->scope;c::EndTypedCurveNativeScope(&f->borrowers,GetCurrentThreadId(),&f->scope);f->access.teardown_scope=&stale;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::WrongScope);return true;}
bool WrongBlueprintOwnerRefuses(){auto f=std::make_unique<Fixture>();f->nodes[0].value=reinterpret_cast<std::uintptr_t>(f->blueprints[1]);
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::UnownedBlueprint);CHECK(!f->writes && !f->owner.copy_count);return true;}
bool CorruptSourceParentRefuses(){auto f=std::make_unique<Fixture>();f->nodes[1].parent=0;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::UnownedBlueprint);CHECK(!f->writes);return true;}
bool CapacityFailurePrecedesWrites(){auto f=std::make_unique<Fixture>();f->owner.copy_capacity=1;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::CapacityExceeded);CHECK(!f->writes && !f->owner.copy_count);return true;}
bool OwnerChangePrecedesWrites(){auto f=std::make_unique<Fixture>();f->change_fence=true;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::OwnerChanged && !f->writes);return true;}
bool FailedWriteRetainsPublishedCopies(){auto f=std::make_unique<Fixture>();f->fail_write=2;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::WriteFailed);CHECK(f->owner.copy_count==2);
    unsigned redirected=0;for(unsigned i=0;i<2;++i)if(b::ValidateOwnedBlueprint(&f->owner,f->param(i)))++redirected;CHECK(redirected==1);return true;}
bool OwnedPayloadTamperRefuses(){auto f=std::make_unique<Fixture>();CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::Succeeded);f->copies[0].native[40]^=1;
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::OwnedPayloadChanged);return true;}
bool NonItemRecordRefuses(){auto f=std::make_unique<Fixture>();f->put(f->items[0]+24,f->base+b::kItemContextVtableRva+8);
    CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::InvalidGraph && !f->writes);return true;}
bool WrongProfileRefuses(){auto f=std::make_unique<Fixture>();f->access.sha256[0]='0';CHECK(b::PinBeforeSessionDestroy(&f->owner,f->access)==R::ProfileMismatch);return true;}
}
int main(){bool(*tests[])()={CopiesSurviveOriginalStorageDestruction,SharedSourceGetsOneOwnedCopy,RepeatedPinDoesNotReplaceOwnedPayload,WrongScopeCannotPin,
    ScopeCopiedTokenDoesNotRemainActive,WrongBlueprintOwnerRefuses,CorruptSourceParentRefuses,CapacityFailurePrecedesWrites,OwnerChangePrecedesWrites,
    FailedWriteRetainsPublishedCopies,OwnedPayloadTamperRefuses,NonItemRecordRefuses,WrongProfileRefuses};
    for(auto test:tests)if(!test())return 1;std::printf("Stored blueprint owner: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
