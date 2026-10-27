#include "fixes/custom_map_asset_isolation/BlueprintBaselineRebuild.h"
#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
namespace b=dluma::fixes::custom_map_asset_isolation::blueprint_rebuild;
namespace s=dluma::fixes::custom_map_asset_isolation::stored_blueprints;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
template<class T>void Put(std::uint8_t* p,std::size_t o,T v){std::memcpy(p+o,&v,sizeof(v));}
template<class T>T Get(const std::uint8_t* p,std::size_t o){T v={};std::memcpy(&v,p+o,sizeof(v));return v;}
struct Map {std::uintptr_t root;std::uint32_t count,capacity,group,pad;};
struct Node {std::int32_t key,pad;std::uint8_t effect[192];std::uintptr_t left,parent,right,extra;};
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    std::uint8_t game[0xAB0]={},levels[8][56]={};Node node={};Map map={};b::Fence fence={};
    b::Access access={};b::Accumulator accumulator={};b::Baseline baseline={};
    std::uint32_t calls[21]={},call_count=0;bool fail_native=false,change_fence=false;unsigned fence_calls=0;
    Fixture();
};
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* data,std::size_t size){auto begin=reinterpret_cast<std::uintptr_t>(data);
    if(n>size || p<begin || p-begin>size-n)return false;std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(p==x.base+0x1D07F88 && n==8){std::memcpy(out,&x.fence.game_di,8);return true;}
    return Copy(p,out,n,x.game,sizeof(x.game)) || Copy(p,out,n,&x.map,sizeof(x.map)) || Copy(p,out,n,&x.node,sizeof(x.node)) || Copy(p,out,n,x.levels,sizeof(x.levels));}
bool Fence(void* user,b::Fence* out) noexcept {auto& x=*static_cast<Fixture*>(user);*out=x.fence;if(x.change_fence && x.fence_calls++)++out->revision;return true;}
bool Accumulate(void* user,std::uint8_t* target,std::uint32_t type,const std::uint8_t* level,float weight) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(weight!=1.0f || type>=21 || x.fail_native)return false;x.calls[x.call_count++]=type;
    if(type==0 || type==20)Put(target,type==0?20:44,(Get<float>(level,52)-1.0f)*weight+Get<float>(target,type==0?20:44));
    if(type==1)Put(target,24,((Get<float>(level,52)-1.0f)*weight+Get<float>(target,24))+weight*Get<float>(level,48));return true;}
Fixture::Fixture(){node.key=1;node.effect[72]=1;Put(node.effect,84,2U);
    for(unsigned i=0;i<8;++i){Put(levels[i],48,0.25f);Put(levels[i],52,2.0f);}
    const Map vector={reinterpret_cast<std::uintptr_t>(levels),8,8,1,0};std::memcpy(node.effect+96,&vector,sizeof(vector));
    map={reinterpret_cast<std::uintptr_t>(&node.left),1,0,30,0};fence={reinterpret_cast<std::uintptr_t>(game),reinterpret_cast<std::uintptr_t>(&map),3,9,11,7,true,true};
    Put(game,0xAA0,fence.manager);access.gamedll_base=base;std::memcpy(access.sha256,c::kPlayerGameDllSha256,65);access.read=Read;access.fence=Fence;access.user=this;
    accumulator.gamedll_base=base;std::memcpy(accumulator.sha256,c::kPlayerGameDllSha256,65);accumulator.invoke=Accumulate;accumulator.user=this;
}
void Payload(std::uint8_t* p,std::uint64_t id){std::memset(p,0xcd,176);Put(p,0,Fixture::base+s::kBlueprintVtableRva);Put(p,8,id);}
bool CapturesScalarInputs(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));CHECK(x.baseline.effects[1].present && x.baseline.effects[1].active);
    CHECK(Get<float>(x.baseline.effects[1].levels[7],52)==2.0f && !x.baseline.effects[0].present);return true;}
bool RejectsNonPristine(){Fixture x;x.fence.initial_pristine=false;CHECK(!b::CaptureBaseline(x.access,&x.baseline));return true;}
bool RejectsChangedFence(){Fixture x;x.change_fence=true;CHECK(!b::CaptureBaseline(x.access,&x.baseline));return true;}
bool RejectsTreeCycle(){Fixture x;x.node.left=x.map.root;CHECK(!b::CaptureBaseline(x.access,&x.baseline));return true;}
bool RejectsBadVectorCapacity(){Fixture x;Put(x.node.effect,108,2U);CHECK(!b::CaptureBaseline(x.access,&x.baseline));return true;}
bool RejectsWrongOwnerRoot(){Fixture x;Put(x.game,0xAA0,std::uintptr_t{0});CHECK(!b::CaptureBaseline(x.access,&x.baseline));return true;}
bool RebuildsNativeDefaultsAndKeepsId(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));std::uint8_t input[176],out[176]={};Payload(input,2);
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::Succeeded);
    CHECK(Get<std::uint64_t>(out,8)==2 && Get<std::uint32_t>(out,104)==UINT32_MAX && Get<float>(out,128)==2.25f && Get<float>(out,124)==1.0f);
    CHECK(Get<unsigned>(out,168)==1 && Get<unsigned>(out,20)==1 && Get<unsigned>(out,152)==0 && out[100]==0xcd && out[172]==0xcd);
    CHECK(Get<unsigned>(input,104)==0xcdcdcdcd && x.call_count==1 && x.calls[0]==1);return true;}
bool HigherEncodedByteWins(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));std::uint8_t input[176],out[176]={};Payload(input,0x6222);
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::Succeeded);CHECK(Get<unsigned>(out,20)==4 && Get<unsigned>(out,168)==4 && x.call_count==1);return true;}
bool NativePriorityThenLevelThenTypeOrder(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));
    x.baseline.effects[0]=x.baseline.effects[1];x.baseline.effects[20]=x.baseline.effects[1];x.baseline.effects[1].flags|=16;
    std::uint8_t input[176],out[176]={};Payload(input,0x550221);
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::Succeeded);CHECK(x.call_count==3 && x.calls[0]==1 && x.calls[1]==20 && x.calls[2]==0);return true;}
bool InactiveEffectDoesNotApply(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));x.baseline.effects[1].active=false;
    std::uint8_t input[176],out[176]={};Payload(input,2);CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::Succeeded);
    CHECK(!x.call_count && Get<float>(out,128)==1.0f && Get<unsigned>(out,20)==1);return true;}
bool UnavailableLevelDoesNotApply(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));x.baseline.effects[1].level_count=1;
    std::uint8_t input[176],out[176]={};Payload(input,0xe2);CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::Succeeded);
    CHECK(!x.call_count && Get<unsigned>(out,20)==8);return true;}
bool InvalidIdLeavesOutputUntouched(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));std::uint8_t input[176],out[176];Payload(input,~std::uint64_t{0});std::memset(out,0xa5,176);
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::InvalidId);for(auto v:out)CHECK(v==0xa5);return true;}
bool NativeFailureLeavesOutputUntouched(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));std::uint8_t input[176],out[176];Payload(input,2);std::memset(out,0xa5,176);x.fail_native=true;
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,out)==b::Result::NativeFailed);for(auto v:out)CHECK(v==0xa5);return true;}
bool OutputAliasRefuses(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));std::uint8_t input[184];Payload(input,2);
    CHECK(b::BuildPayload(x.baseline,x.accumulator,input,input+8)==b::Result::InvalidArgument);return true;}
bool PlanDoesNotMutatePublishedCopy(){Fixture x;CHECK(b::CaptureBaseline(x.access,&x.baseline));s::BlueprintCopy copy={};s::Binding binding={};s::Owner owner={};
    CHECK(s::Initialize(&owner,&copy,1,&binding,1));Payload(copy.native,2);std::memcpy(copy.captured,copy.native,176);copy.id=2;copy.published=true;owner.copy_count=1;
    b::Rebuild plan={};std::size_t count=0;CHECK(b::BuildOwnedPlan(&owner,x.baseline,x.accumulator,&plan,1,&count)==b::Result::Succeeded);
    CHECK(count==1 && plan.target==&copy && Get<float>(plan.desired,128)==2.25f && !std::memcmp(copy.native,copy.captured,176));
    copy.native[104]^=1;CHECK(b::BuildOwnedPlan(&owner,x.baseline,x.accumulator,&plan,1,&count)==b::Result::PayloadChanged);return true;}
}
int main(){bool(*tests[])()={CapturesScalarInputs,RejectsNonPristine,RejectsChangedFence,RejectsTreeCycle,RejectsBadVectorCapacity,RejectsWrongOwnerRoot,
    RebuildsNativeDefaultsAndKeepsId,HigherEncodedByteWins,NativePriorityThenLevelThenTypeOrder,InactiveEffectDoesNotApply,UnavailableLevelDoesNotApply,
    InvalidIdLeavesOutputUntouched,NativeFailureLeavesOutputUntouched,OutputAliasRefuses,PlanDoesNotMutatePublishedCopy};
    for(auto test:tests)if(!test())return 1;std::printf("Blueprint baseline rebuild: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
