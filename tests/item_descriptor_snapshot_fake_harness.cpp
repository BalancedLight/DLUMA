#include "fixes/custom_map_asset_isolation/ItemDescriptorSnapshot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace ds=dluma::fixes::custom_map_asset_isolation::item_descriptors;
namespace aux=dluma::fixes::custom_map_asset_isolation::item_auxiliary;
namespace pools=dluma::fixes::custom_map_asset_isolation::item_pools;
namespace cs=dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
namespace curves=dluma::fixes::custom_map_asset_isolation::curves;
struct Header{std::uintptr_t data;std::uint32_t count,capacity,group=72,pad=0;};
struct Text{std::uintptr_t data;std::uint32_t count,capacity;};
struct Region{std::uintptr_t address;std::vector<std::uint8_t> bytes;};
struct Fixture{
    static constexpr std::uintptr_t base=0x10000000,manager=base+aux::kManagerRva;
    std::vector<Region> memory;std::uintptr_t next=0x50000000,primary=0,indices=0,dlc=0;
    curves::CurveReadFence fence={1,1,true,true,true};curves::NativeCurveAccess access={};
    std::vector<ds::Record> records=std::vector<ds::Record>(128);
    std::vector<aux::Span> spans=std::vector<aux::Span>(4096),aux_spans=std::vector<aux::Span>(256);
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(1024*1024),aux_bytes=std::vector<std::uint8_t>(65536);
    ds::Snapshot snapshot={};aux::Snapshot auxiliary={};pools::Snapshot pool={};cs::Snapshot crafting={};
    pools::StringEntry strings[2]={};pools::VectorEntry vectors[1]={};std::uint8_t pool_bytes[1]={},craft_bytes[96]={};cs::Span craft_span={0x70000000,96,0};
    unsigned count=0;
    Fixture(){Add(manager,728);Add(base+0x1D03450,24);Add(base+0x1C9C390,24);Add(base+0x1E10584,4);Put<std::uint32_t>(base+0x1E10584,1);Put<std::uintptr_t>(manager,base+0x161E3D8);
        access.module_base=base;access.read=Read;access.fence=Fence;access.user=this;std::memcpy(access.module_sha256,curves::kPlayerGameDllSha256,65);
        primary=Alloc(128*8);indices=Alloc(128*8);dlc=Alloc(64);Put(manager+80,Header{primary,0,128});Put(manager+104,Header{dlc+32,1,0});Put(dlc+8,Header{indices,0,128});
        snapshot.records=records.data();snapshot.record_capacity=records.size();snapshot.spans=spans.data();snapshot.span_capacity=spans.size();snapshot.bytes=bytes.data();snapshot.capacity=bytes.size();
        auxiliary.spans=aux_spans.data();auxiliary.span_capacity=aux_spans.size();auxiliary.bytes=aux_bytes.data();auxiliary.capacity=aux_bytes.size();
        pool.complete=true;pool.fence=fence;pool.strings=strings;pool.string_capacity=2;pool.vectors=vectors;pool.vector_capacity=1;pool.bytes=pool_bytes;pool.capacity=1;
        crafting.complete=true;crafting.spans=&craft_span;crafting.span_count=crafting.span_capacity=1;crafting.bytes=craft_bytes;crafting.used=crafting.capacity=96;
        crafting.fence.manager=craft_span.address;crafting.fence.owner_generation=1;crafting.fence.baseline_generation=1;crafting.fence.initial_pristine=crafting.fence.writers_closed=true;}
    void Add(std::uintptr_t p,std::size_t n){memory.push_back({p,std::vector<std::uint8_t>(n)});}
    std::uintptr_t Alloc(std::size_t n){const auto p=next;next+=(n+31)&~std::uintptr_t(31);Add(p,n);return p;}
    std::uint8_t* At(std::uintptr_t p,std::size_t n){for(auto& r:memory)if(p>=r.address && p-r.address<=r.bytes.size() && n<=r.bytes.size()-(p-r.address))return r.bytes.data()+p-r.address;return nullptr;}
    template<class T>void Put(std::uintptr_t p,const T& value){auto ptr=At(p,sizeof(value));if(!ptr)std::abort();std::memcpy(ptr,&value,sizeof(value));}
    Text String(const char* s){const auto length=std::strlen(s),p=Alloc(length+1);std::memcpy(At(p,length+1),s,length+1);return {p,static_cast<std::uint32_t>(length),static_cast<std::uint32_t>(length)};}
    std::uintptr_t Vector(std::uintptr_t at,unsigned n,unsigned stride){const auto p=Alloc(n*stride);Put(at,Header{p,n,n});return p;}
    std::uintptr_t Item(std::uintptr_t vt=0x1631A68,unsigned size=384){const auto p=Alloc(size);if(!At(base+vt,3616)){Add(base+vt,3616);Put<std::uintptr_t>(base+vt+408,base+0x79F220);Put<std::uintptr_t>(base+vt+432,base+0xEBEF30);}
        Put(p,base+vt);Put(p+24,String("item"));Put<std::uint32_t>(p+88,0x20000+count);Put(primary+8*count,p);Put(indices+8*count,p);++count;Put(manager+80,Header{primary,count,128});Put(dlc+8,Header{indices,count,128});return p;}
    static bool Read(void* user,std::uintptr_t p,void* out,std::size_t n)noexcept{auto data=static_cast<Fixture*>(user)->At(p,n);if(!data)return false;std::memcpy(out,data,n);return true;}
    static bool Fence(void* user,curves::CurveReadFence* out)noexcept{*out=static_cast<Fixture*>(user)->fence;return true;}
    ds::Result Capture(){aux::Access a={};a.native=access;a.manager=manager;std::memcpy(a.engine_sha256,aux::kPlayerEngineSha256,65);if(aux::Capture(a,&auxiliary)!=aux::Result::Succeeded)return ds::Result::InvalidArgument;
        const ds::Inputs inputs={&pool,&auxiliary,&crafting};return ds::Capture(access,manager,inputs,&snapshot);}
};
unsigned checks=0;
void Check(bool v,const char* message){++checks;if(!v){std::printf("FAIL %s\n",message);std::exit(1);}}
int main(){
    {Fixture f;Check(f.Capture()==ds::Result::Succeeded && !f.snapshot.count,"empty initialized primary table");}
    {Fixture f;const auto p=f.Item();Check(f.Capture()==ds::Result::Succeeded && ds::Contains(f.snapshot,p),"primary and DLC identity");}
    {Fixture f;const struct{std::uintptr_t vt;unsigned bytes;}types[]={{0x1625148,128},{0x1631A68,384},{0x161E408,392},{0x161F238,384},{0x1625F88,416},{0x1627C98,208},{0x1628AC8,408},{0x16299B8,592},{0x162A868,160},{0x162B6C8,656},{0x162D4C8,584},{0x162EB28,1872},{0x162FBD8,528},{0x16346A8,600},{0x1626DC8,608},{0x1636408,600},{0x1630A18,600},{0x16328B8,608},{0x16337E8,864},{0x1635598,936},{0x1637538,1128},{0x1638418,1216},{0x1639608,1320},{0x163A488,608},{0x163B2D8,400}};
        for(const auto& t:types)f.Item(t.vt,t.bytes);Check(f.Capture()==ds::Result::Succeeded && f.snapshot.count==25,"all assessed factory variants");}
    {Fixture f;const auto p=f.Item(0x162EB28,1872);const auto values=f.Vector(p+1336,2,16);f.Put<float>(values+28,2.5f);Check(f.Capture()==ds::Result::Succeeded,"owned firing mode scalar vector");f.Put<float>(values+28,3.5f);Check(ds::Verify(f.access,f.snapshot)==ds::Result::Changed,"deep firing mode change");}
    {Fixture f;const auto p=f.Item(0x1639608,1320);const auto value=f.String("liquid");f.Put(p+1288,value);Check(f.Capture()==ds::Result::Succeeded,"liquid derived text allocation");f.Put<std::uint8_t>(value.data,'x');Check(ds::Verify(f.access,f.snapshot)==ds::Result::Changed,"liquid deep text change");}
    {Fixture f;const auto p=f.Item(0x1625F88,416);const auto b=f.Item(0x1627C98,208);f.Put(p+400,b);Check(f.Capture()==ds::Result::Succeeded,"nested ammo borrows primary bullet");}
    {Fixture f;const auto p=f.Item(0x1625F88,416);f.Put<std::uintptr_t>(p+400,0x88880000);Check(f.Capture()==ds::Result::ForeignBorrow,"foreign descriptor pointer rejected");}
    {Fixture f;const auto p=f.Item();f.Put<std::uintptr_t>(p+192,0x88880000);Check(f.Capture()==ds::Result::ForeignBorrow,"foreign intern header rejected");}
    {Fixture f;const auto p=f.Item();const auto header=f.Alloc(16);f.strings[0].value=header;f.pool.string_count=1;f.Put(p+192,header);Check(f.Capture()==ds::Result::Succeeded,"observed intern header accepted");}
    {Fixture f;const auto p=f.Item();f.Put<std::uintptr_t>(p+312,Fixture::manager+32);Check(f.Capture()==ds::Result::ForeignBorrow,"aux map header is not its borrowed value");}
    {Fixture f;const auto p=f.Item(0x162D4C8,584);const auto v=f.Vector(p+208,1,16);const auto text=f.String("craft-plan-entry");f.Put(v,text);Check(f.Capture()==ds::Result::Succeeded,"CraftPlan208 owns TTL string vector");f.Put<std::uint8_t>(text.data,'x');Check(ds::Verify(f.access,f.snapshot)==ds::Result::Changed,"CraftPlan208 deep string mutation rejected");}
    {Fixture f;const auto p=f.Item();f.Put<std::uint32_t>(p+88,0x20001);Check(f.Capture()==ds::Result::ForeignBorrow,"mismatched native ID rejected");}
    {Fixture f;f.Item();f.Put<std::uint32_t>(f.dlc+16,0);Check(f.Capture()==ds::Result::InvalidGraph,"unindexed primary object rejected");}
    {Fixture f;const auto a=f.Item(),b=f.Item();Text name={};std::memcpy(&name,f.At(a+24,16),16);f.Put(b+24,name);Check(f.Capture()==ds::Result::InvalidGraph,"shared supposedly owned allocation rejected");}
    {Fixture f;const auto a=f.Item(),b=f.Item();f.Put(f.primary+8,a);Check(f.Capture()==ds::Result::InvalidGraph && b,"duplicate primary pointer rejected");}
    {Fixture f;f.Item();f.snapshot.record_capacity=0;Check(f.Capture()==ds::Result::InvalidArgument,"record capacity validated");}
    {Fixture f;f.Item();f.pool.fence.mutation_revision=2;Check(f.Capture()==ds::Result::FenceRejected,"joined metadata revisions must match");}
    {Fixture f;const auto p=f.Item(0x1628AC8,408);const auto rows=f.Vector(p+384,1,24);f.Put(rows,f.String("bundle"));f.Put<std::uint32_t>(rows+16,3);Check(f.Capture()==ds::Result::Succeeded,"owned bundle row strings");}
    {Fixture f;const auto p=f.Item(0x16299B8,592);const auto owned=f.Alloc(24);f.Put(p+560,owned);const auto pairs=f.Vector(owned,1,8);f.Put<std::uint32_t>(pairs,0x20000);Check(f.Capture()==ds::Result::Succeeded,"owned car cost header and pair data");}
    {Fixture f;const auto p=f.Item(0x16346A8,600);const auto words=f.Vector(p+552,2,2);f.Put<std::uint16_t>(words+2,9);Check(f.Capture()==ds::Result::Succeeded,"powerup word vector");}
    std::printf("Item descriptor snapshot: %u checks passed (20 scenarios)\n",checks);return 0;
}
