#include "fixes/custom_map_asset_isolation/CraftingManagerSnapshot.h"
#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
namespace s=dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
namespace b=dluma::fixes::custom_map_asset_isolation::blueprint_rebuild;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct Text {std::uintptr_t data;std::uint32_t count,capacity;};
struct Links {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
template<std::size_t N>struct Node {std::int32_t key,pad;std::uint8_t body[N];Links links;};
struct SetNode {Text key;std::uint8_t body[40];Links links;};
template<class T>void Put(std::uint8_t* p,std::size_t offset,T value){std::memcpy(p+offset,&value,sizeof(value));}
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    Header manager[4]={};Node<192> effect={};Node<288> elemental={};Node<160> level={};Node<24> interaction={};SetNode set={};
    std::uint8_t effect_level[56]={},interaction_value[40]={};std::uint32_t modifiers[3]={1,0x3f800000,2},set_values[2]={0,1};
    char text[20][12]={};unsigned text_count=0;
    s::Span spans[128]={};std::uint8_t bytes[8192]={};s::Snapshot snapshot={spans,128,0,bytes,sizeof(bytes)};
    b::Access access={};b::Fence fence={};bool change_revision=false;unsigned fences=0;
    void string(std::uint8_t* p,std::size_t offset){auto& value=text[text_count++];std::strcpy(value,"entry");const Text t={reinterpret_cast<std::uintptr_t>(value),5,11};Put(p,offset,t);}
    void effectLevel(std::uint8_t* p){Put(p,0,base+0x1561BF0);string(p,8);string(p,24);p[40]=1;Put(p,52,0x40000000U);}
    void elementalLevel(std::uint8_t* p){Put(p,0,base+0x1561E28);string(p,8);string(p,24);p[40]=1;Put(p,136,Header{0,0,0,30,0});}
    Fixture();
};
bool Read(void* user,std::uintptr_t address,void* out,std::size_t n) noexcept {auto& x=*static_cast<Fixture*>(user);const auto start=reinterpret_cast<std::uintptr_t>(&x);
    if(address<start || n>sizeof(x) || address-start>sizeof(x)-n)return false;std::memcpy(out,reinterpret_cast<void*>(address),n);return true;}
bool Fence(void* user,b::Fence* out) noexcept {auto& x=*static_cast<Fixture*>(user);*out=x.fence;if(x.change_revision && x.fences++)++out->revision;return true;}
Fixture::Fixture(){
    effect.key=0;Put(effect.body,0,base+0x1561C08);string(effect.body,8);string(effect.body,24);string(effect.body,40);effect.body[72]=1;
    Put(effect.body,96,Header{reinterpret_cast<std::uintptr_t>(effect_level),1,1,1,0});effectLevel(effect_level);effectLevel(effect.body+120);
    elemental.key=0;Put(elemental.body,0,base+0x1561E40);string(elemental.body,8);string(elemental.body,24);string(elemental.body,40);elemental.body[72]=1;
    Put(elemental.body,104,Header{reinterpret_cast<std::uintptr_t>(&level.links),1,0,30,0});elementalLevel(elemental.body+128);elementalLevel(level.body);
    level.key=1;Put(level.body,136,Header{reinterpret_cast<std::uintptr_t>(&interaction.links),1,0,30,0});interaction.key=4;
    Put(interaction.body,0,Header{reinterpret_cast<std::uintptr_t>(interaction_value),1,1,1,0});
    Put(interaction_value,8,Header{reinterpret_cast<std::uintptr_t>(modifiers),1,1,30,0});
    string(reinterpret_cast<std::uint8_t*>(&set.key),0);string(set.body,0);Put(set.body,16,Header{reinterpret_cast<std::uintptr_t>(set_values),1,1,30,0});
    manager[0]={reinterpret_cast<std::uintptr_t>(&effect.links),1,0,30,0};manager[1]={reinterpret_cast<std::uintptr_t>(&elemental.links),1,0,30,0};
    manager[2]={0,0,0,30,0};manager[3]={reinterpret_cast<std::uintptr_t>(&set.links),1,0,30,0};
    fence={0x5000,reinterpret_cast<std::uintptr_t>(manager),3,7,11,13,true,true};access.gamedll_base=base;std::memcpy(access.sha256,c::kPlayerGameDllSha256,65);
    access.read=Read;access.fence=Fence;access.user=this;
}
bool CapturesEveryOwnedLayer(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);CHECK(x.snapshot.span_count==26 && x.snapshot.used>1000);
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Succeeded);return true;}
bool DetectsNestedModifierChange(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);x.modifiers[1]^=1;
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Changed);return true;}
bool DetectsDefinitionStringChange(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);x.text[0][0]='E';
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Changed);return true;}
bool DetectsEffectSetChange(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);++x.set_values[1];
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Changed);return true;}
bool DetectsReplacedEqualBuffer(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);std::strcpy(x.text[19],x.text[0]);
    Put(x.effect.body,8,reinterpret_cast<std::uintptr_t>(x.text[19]));CHECK(s::Verify(x.access,x.snapshot)==s::Result::Changed);return true;}
bool RejectsAliasDoubleOwnership(){Fixture x;Put(x.effect.body,24,reinterpret_cast<std::uintptr_t>(x.text[0]));
    CHECK(s::Capture(x.access,&x.snapshot)==s::Result::InvalidGraph);return true;}
bool RejectsCyclicMap(){Fixture x;x.level.links.left=reinterpret_cast<std::uintptr_t>(&x.level.links);
    CHECK(s::Capture(x.access,&x.snapshot)!=s::Result::Succeeded);return true;}
bool RejectsWrongParent(){Fixture x;x.level.links.parent=reinterpret_cast<std::uintptr_t>(&x.elemental.links);
    CHECK(s::Capture(x.access,&x.snapshot)==s::Result::InvalidGraph);return true;}
bool RejectsMissingStringTerminator(){Fixture x;x.text[0][5]='x';CHECK(s::Capture(x.access,&x.snapshot)==s::Result::InvalidGraph);return true;}
bool RejectsWrongNativeType(){Fixture x;Put(x.level.body,0,Fixture::base+0x1561BF0);CHECK(s::Capture(x.access,&x.snapshot)==s::Result::InvalidGraph);return true;}
bool RejectsCapacityExhaustion(){Fixture x;x.snapshot.capacity=100;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::CapacityExceeded);return true;}
bool RejectsWriterOverlap(){Fixture x;x.change_revision=true;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::FenceRejected);return true;}
bool AllowsSerializedThreadMigration(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);++x.fence.thread_id;++x.fence.revision;x.fence.initial_pristine=false;
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Succeeded);return true;}
bool RejectsOwnerGenerationReuse(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);++x.fence.owner_generation;
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::FenceRejected);return true;}
bool EmptyManagerIsValidShape(){Fixture x;std::memset(x.manager,0,sizeof(x.manager));CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);
    CHECK(x.snapshot.span_count==1 && x.snapshot.used==96);return true;}
bool FrozenBlueprintInputsIgnoreLaterLiveChanges(){Fixture x;CHECK(s::Capture(x.access,&x.snapshot)==s::Result::Succeeded);
    Put(x.effect_level,52,0x40400000U);b::Baseline frozen={};CHECK(b::CaptureFrozenBaseline(x.snapshot,&frozen));
    std::uint32_t value=0;std::memcpy(&value,frozen.effects[0].levels[0]+52,4);CHECK(value==0x40000000U);
    CHECK(s::Verify(x.access,x.snapshot)==s::Result::Changed);return true;}
}
int main(){bool(*tests[])()={CapturesEveryOwnedLayer,DetectsNestedModifierChange,DetectsDefinitionStringChange,DetectsEffectSetChange,DetectsReplacedEqualBuffer,
    RejectsAliasDoubleOwnership,RejectsCyclicMap,RejectsWrongParent,RejectsMissingStringTerminator,RejectsWrongNativeType,RejectsCapacityExhaustion,
    RejectsWriterOverlap,AllowsSerializedThreadMigration,RejectsOwnerGenerationReuse,EmptyManagerIsValidShape,FrozenBlueprintInputsIgnoreLaterLiveChanges};
    for(auto test:tests)if(!test())return 1;std::printf("Crafting manager snapshot: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
