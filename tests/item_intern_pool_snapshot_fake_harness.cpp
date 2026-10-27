#include "fixes/custom_map_asset_isolation/ItemInternPoolSnapshot.h"
#include <cstdio>
#include <cstring>
namespace p=dluma::fixes::custom_map_asset_isolation::item_pools;
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Map {std::uintptr_t root;std::uint32_t count,pad,group,pad2;};
struct List {std::uintptr_t head,tail;std::uint32_t count,pad;};
struct Text {std::uintptr_t data;std::uint32_t count,capacity;};
struct Node {std::uint32_t key,pad;std::uintptr_t value,left,parent,right;std::uint32_t meta,pad2;};
struct Vector {std::uint32_t values[3],pad;std::uintptr_t next,previous;};
struct Storage {p::StringEntry strings[4]={};p::VectorEntry vectors[4]={};std::uint8_t bytes[128]={};p::Snapshot snapshot={strings,4,0,vectors,4,0,bytes,sizeof(bytes)};};
struct Fixture {static constexpr std::uintptr_t base=0x10000000;Map map={};List list={};Node nodes[2]={};Vector vectors[2]={};Text texts[2]={};char names[2][8]={"alpha","beta"};
    c::NativeCurveAccess access={};c::CurveReadFence fence={3,7,true,true,true};Storage baseline,current;unsigned fences=0;bool changing=false;
    Fixture();void append(bool rotate=false){map.count=2;
        if(rotate){map.root=reinterpret_cast<std::uintptr_t>(&nodes[1].left);nodes[1].left=reinterpret_cast<std::uintptr_t>(&nodes[0].left);nodes[0].parent=map.root;}
        else {nodes[0].right=reinterpret_cast<std::uintptr_t>(&nodes[1].left);nodes[1].parent=map.root;}
        vectors[0].next=reinterpret_cast<std::uintptr_t>(&vectors[1].next);vectors[1].previous=list.tail;list.tail=vectors[0].next;list.count=2;}
};
bool Read(void* user,std::uintptr_t at,void* out,std::size_t n) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(at==x.base+p::kStringPoolRva && n==24){std::memcpy(out,&x.map,n);return true;}if(at==x.base+p::kVectorPoolRva && n==24){std::memcpy(out,&x.list,n);return true;}
    const auto begin=reinterpret_cast<std::uintptr_t>(&x);if(at<begin || n>sizeof(x) || at-begin>sizeof(x)-n)return false;
    std::memcpy(out,reinterpret_cast<void*>(at),n);return true;}
bool Fence(void* user,c::CurveReadFence* out) noexcept {auto& x=*static_cast<Fixture*>(user);*out=x.fence;if(x.changing && x.fences++)++out->mutation_revision;return true;}
Fixture::Fixture(){for(unsigned i=0;i<2;++i){texts[i]={reinterpret_cast<std::uintptr_t>(names[i]),static_cast<std::uint32_t>(std::strlen(names[i])),7};
        nodes[i].key=i+1;nodes[i].value=reinterpret_cast<std::uintptr_t>(&texts[i]);vectors[i].values[0]=0x3f800000U+i;}
    map={reinterpret_cast<std::uintptr_t>(&nodes[0].left),1,0,30,0};list={reinterpret_cast<std::uintptr_t>(&vectors[0].next),reinterpret_cast<std::uintptr_t>(&vectors[0].next),1,0};
    access.module_base=base;std::memcpy(access.module_sha256,c::kPlayerGameDllSha256,65);access.read=Read;access.fence=Fence;access.user=this;}
bool CaptureAndCompare(){Fixture x;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Succeeded);CHECK(p::Capture(x.access,&x.current.snapshot)==p::Result::Succeeded);
    CHECK(p::CompareBaseline(x.baseline.snapshot,x.current.snapshot).result==p::Result::Succeeded);return true;}
bool AppendsAndTreeRotationsPreserveValues(){Fixture x;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Succeeded);x.append(true);
    CHECK(p::Capture(x.access,&x.current.snapshot)==p::Result::Succeeded);const auto d=p::CompareBaseline(x.baseline.snapshot,x.current.snapshot);
    CHECK(d.result==p::Result::Succeeded && d.appended_strings==1 && d.appended_vectors==1);return true;}
bool ChangedStringRefuses(){Fixture x;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Succeeded);x.names[0][0]='A';CHECK(p::Capture(x.access,&x.current.snapshot)==p::Result::Succeeded);
    CHECK(p::CompareBaseline(x.baseline.snapshot,x.current.snapshot).result==p::Result::Changed);return true;}
bool ChangedVectorRefuses(){Fixture x;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Succeeded);++x.vectors[0].values[2];CHECK(p::Capture(x.access,&x.current.snapshot)==p::Result::Succeeded);
    CHECK(p::CompareBaseline(x.baseline.snapshot,x.current.snapshot).result==p::Result::Changed);return true;}
bool ReplacedValueIdentityRefuses(){Fixture x;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Succeeded);x.texts[1]=x.texts[0];x.nodes[0].value=reinterpret_cast<std::uintptr_t>(&x.texts[1]);
    CHECK(p::Capture(x.access,&x.current.snapshot)==p::Result::Succeeded);CHECK(p::CompareBaseline(x.baseline.snapshot,x.current.snapshot).result==p::Result::Changed);return true;}
bool CyclicVectorRefuses(){Fixture x;x.vectors[0].next=x.list.head;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::InvalidGraph);return true;}
bool CyclicTreeRefuses(){Fixture x;x.nodes[0].left=x.map.root;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::InvalidGraph);return true;}
bool DuplicateHeaderOwnerRefuses(){Fixture x;x.append();x.nodes[1].value=x.nodes[0].value;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::InvalidGraph);return true;}
bool ChangedFenceRefuses(){Fixture x;x.changing=true;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::Changed);return true;}
bool CapacityRefuses(){Fixture x;x.baseline.snapshot.capacity=3;CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::CapacityExceeded);return true;}
bool UnterminatedStringRefuses(){Fixture x;x.names[0][5]='x';CHECK(p::Capture(x.access,&x.baseline.snapshot)==p::Result::InvalidGraph);return true;}
}
int main(){bool(*tests[])()={CaptureAndCompare,AppendsAndTreeRotationsPreserveValues,ChangedStringRefuses,ChangedVectorRefuses,ReplacedValueIdentityRefuses,
    CyclicVectorRefuses,CyclicTreeRefuses,DuplicateHeaderOwnerRefuses,ChangedFenceRefuses,CapacityRefuses,UnterminatedStringRefuses};
    for(auto test:tests)if(!test())return 1;std::printf("Item intern pools: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
