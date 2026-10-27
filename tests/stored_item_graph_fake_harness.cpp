#include "fixes/custom_map_asset_isolation/StoredItemDescriptorGraph.h"
#include <cstdio>
#include <cstring>
#include <memory>
namespace g=dluma::fixes::custom_map_asset_isolation::stored_item_graph;
namespace b=dluma::fixes::custom_map_asset_isolation::stored_blueprints;
namespace p=dluma::fixes::custom_map_asset_isolation::player_stored_progress;
using F=g::Failure;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
struct Node {std::int32_t key;std::uint32_t pad;Header entries;std::uint64_t allocator;std::uintptr_t links[3];};
static_assert(offsetof(Node,links)==32,"exact DLC map node payload");
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000,descriptor_vt=base+0x1400000;
    alignas(8) unsigned char state[0x480]={},context[4360]={},item[192]={},lost[120]={},descriptors[3][2048]={};
    char names[3][16]={"base_item","upgrade_item","modifier_item"},lost_names[3][16]={"saved_item","saved_upgrade","saved_modifier"};
    std::uintptr_t pointers[3]={},modifiers[2]={};Header lost_modifier={};Node node={};std::uint32_t initialized=1;
    std::uintptr_t id_route=base+0xEBEF30;bool change_fence=false;unsigned fence_reads=0;std::uintptr_t forbidden=0;bool forbidden_read=false;
    std::uintptr_t kind_route=base+0x2390,bullet_route=base+0x1481A0,ammo_route=base+0x1481A0,value_route=base+0x1481A0;
    b::Owner blueprint_owner={};b::BlueprintCopy blueprint[1]={};b::Binding work[1]={};
    p::Reader reader={};p::Fence fence={};g::Scratch scratch={};g::Proof proof={};
    Fixture();template<class T>void put(unsigned char* at,const T& value){std::memcpy(at,&value,sizeof(value));}
    bool capture(){return g::Capture(reader,&blueprint_owner,reinterpret_cast<std::uintptr_t>(state),1,&scratch,&proof);}
};
bool Copy(std::uintptr_t address,void* out,std::size_t n,const void* object,std::size_t size){auto begin=reinterpret_cast<std::uintptr_t>(object);
    if(address<begin || n>size || address-begin>size-n)return false;std::memcpy(out,reinterpret_cast<const void*>(address),n);return true;}
bool Read(void* user,std::uintptr_t at,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fixture*>(user);if(f.forbidden && at>=f.forbidden && at<f.forbidden+176)f.forbidden_read=true;
    if(at==f.base+0x1E10584 && n==4){std::memcpy(out,&f.initialized,4);return true;}
    if(at==f.base+0x1E0EC38 && n==8){const auto value=reinterpret_cast<std::uintptr_t>(f.node.links);std::memcpy(out,&value,8);return true;}
    if(at==f.descriptor_vt+432 && n==8){std::memcpy(out,&f.id_route,8);return true;}
    if(at==f.descriptor_vt+408 && n==8){const auto value=f.base+0x79F220;std::memcpy(out,&value,8);return true;}
    if(n==8){std::uintptr_t value=0;
        if(at==f.descriptor_vt+448)value=f.base+0x694500;else if(at==f.descriptor_vt+600)value=f.kind_route;
        else if(at==f.descriptor_vt+624)value=f.bullet_route;else if(at==f.descriptor_vt+640)value=f.ammo_route;else if(at==f.descriptor_vt+2760)value=f.value_route;
        if(value){std::memcpy(out,&value,8);return true;}}
    const std::size_t slots[]={432,448,600,624,640,2760,2904,3176,3184,3192,3328};
    const std::uintptr_t routes[]={0x6C96F0,0x6C9840,0x6C9880,0x6C9870,0x6C95E0,0x6C8270,0x106B80,0x6C7F30,0x694AA0,0xF88490,0x6C98D0};
    for(std::size_t i=0;i<sizeof(slots)/sizeof(slots[0]);++i)if(at==f.base+b::kItemContextVtableRva+slots[i] && n==8){const auto value=f.base+routes[i];std::memcpy(out,&value,8);return true;}
    return Copy(at,out,n,f.state,sizeof(f.state))||Copy(at,out,n,f.context,sizeof(f.context))||Copy(at,out,n,f.item,sizeof(f.item))||
        Copy(at,out,n,f.lost,sizeof(f.lost))||Copy(at,out,n,f.descriptors,sizeof(f.descriptors))||Copy(at,out,n,f.names,sizeof(f.names))||
        Copy(at,out,n,f.lost_names,sizeof(f.lost_names))||Copy(at,out,n,f.pointers,sizeof(f.pointers))||Copy(at,out,n,f.modifiers,sizeof(f.modifiers))||
        Copy(at,out,n,&f.lost_modifier,sizeof(f.lost_modifier))||Copy(at,out,n,&f.node,sizeof(f.node));
}
bool Fence(void* user,p::Fence* out)noexcept{auto& f=*static_cast<Fixture*>(user);*out=f.fence;if(f.change_fence && ++f.fence_reads>1)++out->mutation_revision;return true;}
Fixture::Fixture(){
    put(state,base+0x17E2FF8);put(state+0x468,reinterpret_cast<std::uintptr_t>(context));Header live={reinterpret_cast<std::uintptr_t>(item),1,1};put(context+24,live);
    put(item+24,base+b::kItemContextVtableRva);
    for(unsigned i=0;i<3;++i){put(descriptors[i],descriptor_vt);const auto length=static_cast<std::uint32_t>(std::strlen(names[i]));
        Header name={reinterpret_cast<std::uintptr_t>(names[i]),length,15};put(descriptors[i]+24,name);put(descriptors[i]+88,static_cast<std::uint32_t>(0x20000+i));
        pointers[i]=reinterpret_cast<std::uintptr_t>(descriptors[i]);}
    node.entries={reinterpret_cast<std::uintptr_t>(pointers),3,3};put(item+32,pointers[0]);put(item+56,pointers[1]);modifiers[0]=pointers[2];
    Header mods={reinterpret_cast<std::uintptr_t>(modifiers),1,2};put(item+72,mods);
    fence.player_state=reinterpret_cast<std::uintptr_t>(state);fence.owner_generation=1;fence.mutation_revision=9;fence.thread_id=GetCurrentThreadId();fence.exclusive_preservation_owner=true;
    reader.gamedll_base=base;reader.read=Read;reader.read_fence=Fence;reader.user=this;reader.exact_image_and_section_routes=true;
    b::Initialize(&blueprint_owner,blueprint,1,work,1);
}
bool QualifiedDescriptorsWithoutNativeCalls(){auto f=std::make_unique<Fixture>();CHECK(f->capture());CHECK(f->proof.complete && f->proof.live_items==1 && f->proof.descriptor_count==3);return true;}
bool MissingDlcOwnershipRefuses(){auto f=std::make_unique<Fixture>();f->pointers[1]=f->pointers[0];CHECK(!f->capture());CHECK(f->proof.failure==F::DescriptorUnowned);return true;}
bool UnknownGetterRouteRefuses(){auto f=std::make_unique<Fixture>();f->id_route++;CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidDescriptor);return true;}
bool UninitializedManagerNeverRunsLazyConstructor(){auto f=std::make_unique<Fixture>();f->initialized=0;CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidDescriptor);return true;}
bool NullLiveParentRefuses(){auto f=std::make_unique<Fixture>();f->put(f->item+32,std::uintptr_t{0});CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidDescriptor);return true;}
bool UnownedBlueprintRefusesWithoutDereference(){auto f=std::make_unique<Fixture>();f->forbidden=0x90000000;f->put(f->item+64,f->forbidden);CHECK(!f->capture());
    CHECK(f->proof.failure==F::UnownedBlueprint && !f->forbidden_read);return true;}
bool OwnedBlueprintQualifies(){auto f=std::make_unique<Fixture>();auto& copy=f->blueprint[0];f->put(copy.native,f->base+b::kBlueprintVtableRva);copy.id=7;f->put(copy.native+8,copy.id);
    std::memcpy(copy.captured,copy.native,176);copy.published=true;f->blueprint_owner.copy_count=1;f->put(f->item+64,reinterpret_cast<std::uintptr_t>(copy.native));
    CHECK(f->capture() && f->proof.blueprint_count==1);return true;}
bool ModifierBoundsRefuse(){auto f=std::make_unique<Fixture>();Header bad={reinterpret_cast<std::uintptr_t>(f->modifiers),3,2};f->put(f->item+72,bad);
    CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidVector);return true;}
bool InvalidStringTerminatorRefuses(){auto f=std::make_unique<Fixture>();f->names[0][9]='x';CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidString);return true;}
bool LostNestedStringsQualify(){auto f=std::make_unique<Fixture>();Header lost_header={reinterpret_cast<std::uintptr_t>(f->lost),1,1};f->put(f->context+824,lost_header);
    for(unsigned i=0;i<2;++i){Header name={reinterpret_cast<std::uintptr_t>(f->lost_names[i]),static_cast<std::uint32_t>(std::strlen(f->lost_names[i])),15};f->put(f->lost+8+16*i,name);}
    f->lost_modifier={reinterpret_cast<std::uintptr_t>(f->lost_names[2]),14,15};Header list={reinterpret_cast<std::uintptr_t>(&f->lost_modifier),1,1};
    f->put(f->lost+80,list);f->put(f->lost+72,std::uint32_t{1});CHECK(f->capture());CHECK(f->proof.lost_items==1);return true;}
bool LostSerializedCountCannotOverrunNames(){auto f=std::make_unique<Fixture>();Header lost_header={reinterpret_cast<std::uintptr_t>(f->lost),1,1};f->put(f->context+824,lost_header);
    f->put(f->lost+72,std::uint32_t{1});CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidVector);return true;}
bool ChangedOwnerFenceRefuses(){auto f=std::make_unique<Fixture>();f->change_fence=true;CHECK(!f->capture());CHECK(f->proof.failure==F::Changed);return true;}
bool QualifiedDefaultAmmoAndBullet(){auto f=std::make_unique<Fixture>();f->ammo_route=f->base+0x6B2B80;f->bullet_route=f->base+0x6AC8B0;f->kind_route=f->base+0xEBEA90;
    f->put(f->descriptors[0]+1768,f->pointers[1]);f->put(f->descriptors[0]+400,f->pointers[2]);CHECK(f->capture());return true;}
bool UnownedDefaultAmmoRefuses(){auto f=std::make_unique<Fixture>();f->ammo_route=f->base+0x6B2B80;f->put(f->descriptors[0]+1768,std::uintptr_t{0x9000});
    CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidDescriptor);return true;}
bool UnknownScalarGetterRefuses(){auto f=std::make_unique<Fixture>();f->value_route=f->base+0x1234;CHECK(!f->capture());CHECK(f->proof.failure==F::InvalidDescriptor);return true;}
}
int main(){bool(*tests[])()={QualifiedDescriptorsWithoutNativeCalls,MissingDlcOwnershipRefuses,UnknownGetterRouteRefuses,UninitializedManagerNeverRunsLazyConstructor,
    NullLiveParentRefuses,UnownedBlueprintRefusesWithoutDereference,OwnedBlueprintQualifies,ModifierBoundsRefuse,InvalidStringTerminatorRefuses,
    LostNestedStringsQualify,LostSerializedCountCannotOverrunNames,ChangedOwnerFenceRefuses,QualifiedDefaultAmmoAndBullet,UnownedDefaultAmmoRefuses,
    UnknownScalarGetterRefuses};for(auto test:tests)if(!test())return 1;
    std::printf("Stored item graph: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
