#include "fixes/custom_map_asset_isolation/ItemAuxiliarySnapshot.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
namespace aux=dluma::fixes::custom_map_asset_isolation::item_auxiliary;
namespace curves=dluma::fixes::custom_map_asset_isolation::curves;
struct Region {std::uintptr_t address;std::vector<std::uint8_t> bytes;};
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group=72,pad=0;};
struct Text {std::uintptr_t data;std::uint32_t count,capacity;};
struct Fixture {
    std::vector<Region> memory;
    std::vector<aux::Span> spans=std::vector<aux::Span>(2048);
    std::vector<std::uint8_t> storage=std::vector<std::uint8_t>(1024*1024);
    aux::Access access={};aux::Snapshot snapshot={};
    curves::CurveReadFence fence={1,1,true,true,true};
    std::uintptr_t next=0x50000000,fail=0,mutate=0,watch=0;
    unsigned reads=0,watch_reads=0,mutate_after=0;
    static constexpr std::uintptr_t base=0x10000000,root=base+aux::kManagerRva;
    Fixture(){memory.push_back({root,std::vector<std::uint8_t>(728)});
        memory.push_back({base+0x1D03450,std::vector<std::uint8_t>(24)});
        memory.push_back({base+0x1C9C390,std::vector<std::uint8_t>(24)});
        Put<std::uintptr_t>(root,base+0x161E3D8);
        access.native.module_base=base;access.native.read=Read;access.native.fence=Fence;access.native.user=this;
        std::memcpy(access.native.module_sha256,curves::kPlayerGameDllSha256,65);
        std::memcpy(access.engine_sha256,aux::kPlayerEngineSha256,65);access.manager=root;
        snapshot.spans=spans.data();snapshot.span_capacity=spans.size();snapshot.bytes=storage.data();snapshot.capacity=storage.size();}
    std::uintptr_t Alloc(std::size_t n){const auto p=next;next+=(n+31)&~std::uintptr_t(31);memory.push_back({p,std::vector<std::uint8_t>(n)});return p;}
    std::uint8_t* Bytes(std::uintptr_t p,std::size_t n){for(auto& r:memory)if(p>=r.address && p-r.address<=r.bytes.size() && n<=r.bytes.size()-(p-r.address))return r.bytes.data()+p-r.address;return nullptr;}
    template<class T>void Put(std::uintptr_t p,const T& value){auto bytes=Bytes(p,sizeof(value));if(!bytes)std::abort();std::memcpy(bytes,&value,sizeof(value));}
    Text String(const char* value){const auto n=std::strlen(value);const auto p=Alloc(n+1);std::memcpy(Bytes(p,n+1),value,n+1);return {p,static_cast<std::uint32_t>(n),static_cast<std::uint32_t>(n)};}
    std::uintptr_t Vector(std::uintptr_t header,std::size_t stride,unsigned count,unsigned capacity=0){if(!capacity)capacity=count;const auto p=Alloc(stride*capacity);Put(header,Header{p,count,capacity});return p;}
    std::uintptr_t Tree(std::uintptr_t header,std::size_t prefix){const auto p=Alloc(prefix+32);Put(header,Header{p+prefix,1,0});return p;}
    static bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {auto& f=*static_cast<Fixture*>(user);++f.reads;
        if(p==f.watch && ++f.watch_reads==f.mutate_after && f.mutate)f.Put<std::uint8_t>(f.mutate,'z');
        if(p==f.fail)return false;auto b=f.Bytes(p,n);if(!b)return false;std::memcpy(out,b,n);return true;}
    static bool Fence(void* user,curves::CurveReadFence* out) noexcept {*out=static_cast<Fixture*>(user)->fence;return true;}
    aux::Result Capture(){return aux::Capture(access,&snapshot);}
    std::uintptr_t Populate(){
        const auto effect=Tree(root+8,160);Put<std::int32_t>(effect,3);const auto val=effect+8;
        for(std::size_t off=8;off<=104;off+=24){const auto a=Vector(val+off,16,1);Put(a,String("effect"));}
        const auto near=Vector(val+128,24,1);Put(near,String("near"));Put<float>(near+16,2.5f);Put<std::uint8_t>(near+20,1);
        const auto pickup=Tree(root+32,40);Put(pickup,String("pickup"));const auto sound=Vector(pickup+16,16,1);Put(sound,String("sound"));
        const auto collision=Tree(root+56,40);Put(collision,String("collision"));const auto boxes=Vector(collision+16,40,1);Put<float>(boxes,1.25f);Put(boxes+24,String("bone"));
        const auto rows=Vector(root+176,208,1);for(const auto off:{0,24,40,56,72,88,128,152,168,184})Put(rows+off,String("row"));
        for(const auto off:{200,224,248}){const auto list=Vector(root+off,8,1);const auto field=Alloc(144);Put(list,field);
            Put<std::uintptr_t>(field+112,base+0x161FCF0);const auto options=Vector(field+120,24,1);Put<std::int32_t>(options,42);
            Put(options+8,String("option").data);Put(options+16,String("description").data);}
        for(const auto off:{272,296,320}){const auto texts=Vector(root+off,16,1);Put(texts,String("text"));}
        const auto cuts=Vector(root+344,40,5);Put<std::uint8_t>(cuts+199,3);
        const auto floats=Vector(root+512,4,61);Put<float>(floats+240,2.0f);
        for(const auto off:{608,632,656,680}){const auto node=Tree(root+off,8);Put<std::uint32_t>(node,7);Put<float>(node+4,2.0f);}
        const auto stringvalue=Tree(root+704,32);Put<std::int32_t>(stringvalue,1);Put(stringvalue+8,String("value"));
        const auto costs=Tree(base+0x1C9C390,16);Put<std::int32_t>(costs,4);Put<std::int32_t>(costs+4,2);
        const auto cost=Alloc(56);Put(costs+8,cost);Put<float>(cost,100.0f);
        for(const auto off:{8,32}){const auto pairs=Vector(cost+off,8,1);Put<std::int32_t>(pairs,0x20003);Put<std::int32_t>(pairs+4,2);}
        Put<std::uint32_t>(base+0x1D03450,4);return sound;
    }
};
unsigned tests=0;
void Check(bool value,const char* name){++tests;if(!value){std::printf("FAIL %s\n",name);std::exit(1);}}
int main(){
    {Fixture f;Check(f.Capture()==aux::Result::Succeeded,"empty auxiliary graph");}
    {Fixture f;f.Populate();Check(f.Capture()==aux::Result::Succeeded && f.snapshot.enum_fields==3 && f.snapshot.enum_options==3 && f.snapshot.part_color_records==1,"all owned paths captured");}
    {Fixture f;const auto p=f.Populate();Check(f.Capture()==aux::Result::Succeeded,"populated capture");Text t={};std::memcpy(&t,f.Bytes(p,16),16);f.Put<std::uint8_t>(t.data,'x');Check(aux::Verify(f.access,f.snapshot)==aux::Result::Changed,"nested sound mutation rejected");}
    {Fixture f;const auto list=f.Vector(Fixture::root+200,8,1);const auto field=f.Alloc(144);f.Put(list,field);const auto rows=f.Vector(field+120,24,1);const auto name=f.String("cached");f.Put(rows+8,name.data);
        Check(f.Capture()==aux::Result::Succeeded,"enum options captured");f.Put<std::uint8_t>(name.data,'x');Check(aux::Verify(f.access,f.snapshot)==aux::Result::Changed,"enum content mutation rejected");}
    {Fixture f;f.Populate();Check(f.Capture()==aux::Result::Succeeded,"part costs captured");f.Put<std::uint32_t>(Fixture::base+0x1D03450,9);Check(aux::Verify(f.access,f.snapshot)==aux::Result::Changed,"separate global repair mutation rejected");}
    {Fixture f;auto p=f.Tree(Fixture::root+608,8);f.Put<std::uintptr_t>(p+8,p+8);Check(f.Capture()==aux::Result::InvalidGraph,"cycle rejected");}
    {Fixture f;auto p=f.Tree(Fixture::root+608,8);f.Put<std::uintptr_t>(p+16,17);Check(f.Capture()==aux::Result::InvalidGraph,"wrong parent rejected");}
    {Fixture f;f.Tree(Fixture::root+608,8);f.Put<std::uint32_t>(Fixture::root+616,2);Check(f.Capture()==aux::Result::InvalidGraph,"incorrect node count rejected");}
    {Fixture f;auto a=f.Vector(Fixture::root+272,16,0,8);f.Put(Fixture::root+296,Header{a+64,0,2});Check(f.Capture()==aux::Result::InvalidGraph,"aliases in unused capacity rejected");}
    {Fixture f;auto p=f.Vector(Fixture::root+272,16,1);auto t=f.String("text");f.Put(p,t);f.Put<std::uint8_t>(t.data+4,'x');Check(f.Capture()==aux::Result::InvalidGraph,"unterminated TTL string rejected");}
    {Fixture f;f.Populate();f.watch=Fixture::base+0x1D03450;f.mutate=f.watch;f.mutate_after=2;Check(f.Capture()==aux::Result::Changed && !f.snapshot.complete,"capture rereads deep values");}
    {Fixture f;f.fence.readers_drained=false;Check(f.Capture()==aux::Result::FenceRejected,"borrower closure required");}
    {Fixture f;f.access.engine_sha256[0]='0';Check(f.Capture()==aux::Result::ProfileMismatch,"engine option layout requires exact profile");}
    {Fixture f;f.fail=Fixture::root+176;Check(f.Capture()==aux::Result::ReadFailed,"unreadable manager bytes rejected");}
    {Fixture f;f.snapshot.span_capacity=2;Check(f.Capture()==aux::Result::CapacityExceeded,"snapshot capacity bounded");}
    {Fixture f;Check(f.Capture()==aux::Result::Succeeded,"scoped snapshot captured");f.Put<std::uintptr_t>(Fixture::root+80,123);Check(aux::Verify(f.access,f.snapshot)==aux::Result::Succeeded,"primary graph explicitly separate");}
    {Fixture f;f.Put(Fixture::root+344,Header{0,6,5});Check(f.Capture()==aux::Result::InvalidGraph,"vector count exceeds capacity");}
    {Fixture f;Check(f.Capture()==aux::Result::Succeeded,"snapshot before newer read fence");++f.fence.mutation_revision;Check(aux::Verify(f.access,f.snapshot)==aux::Result::Succeeded,"later stable fence allowed");}
    {Fixture f;Check(f.Capture()==aux::Result::Succeeded,"snapshot before corrupt span");f.snapshot.spans[0].allocation_size=1;Check(aux::Verify(f.access,f.snapshot)==aux::Result::InvalidArgument,"corrupt archive span rejected");}
    {Fixture f;const auto owned=f.Tree(Fixture::root+32,40);f.Put(owned,f.String("pickup"));Check(f.Capture()==aux::Result::Succeeded && aux::ContainsBorrow(f.snapshot,aux::BorrowKind::PickupSounds,owned+16),"exact borrowed map value accepted");Check(!aux::ContainsBorrow(f.snapshot,aux::BorrowKind::PickupSounds,owned+17) && !aux::ContainsBorrow(f.snapshot,aux::BorrowKind::Effect,owned+16),"interior address and wrong map rejected");}
    std::printf("Item auxiliary snapshot: %u checks passed (20 scenarios)\n",tests);return 0;
}
