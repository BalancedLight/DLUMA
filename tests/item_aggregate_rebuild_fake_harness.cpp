#include "fixes/custom_map_asset_isolation/ItemAggregateRebuild.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace ag=dluma::fixes::custom_map_asset_isolation::item_aggregate;
namespace curves=dluma::fixes::custom_map_asset_isolation::curves;
namespace cs=dluma::fixes::custom_map_asset_isolation::crafting_snapshot;
struct Calls {unsigned count=0,type[21]={},change=2;float weight[21]={};bool fail=false;
    static bool Apply(void* user,std::uint8_t* target,unsigned type,const std::uint8_t* level,float weight,unsigned* change) noexcept {
        auto& c=*static_cast<Calls*>(user);if(c.fail)return false;c.type[c.count]=type;c.weight[c.count++]=weight;
        float before=0,value=0;std::memcpy(&before,target+20,4);std::memcpy(&value,level+52,4);before+=(value-1.0f)*weight;
        std::memcpy(target+20,&before,4);*change=c.change;return true;}
};
struct Fixture {
    ag::Baseline baseline={};ag::Source sources[3]={},upgrade={};ag::Derived output={};Calls calls;ag::Accumulator accumulator={};
    Fixture(){baseline.complete=true;baseline.fence.initial_pristine=true;baseline.fence.manager=0x1000;baseline.fence.owner_generation=1;baseline.fence.baseline_generation=1;
        accumulator.gamedll_base=0x10000000;std::memcpy(accumulator.sha256,curves::kPlayerGameDllSha256,65);accumulator.apply=Calls::Apply;accumulator.user=&calls;}
    void Effect(unsigned type,unsigned flags=2,unsigned count=2,int opposite=-1){auto& e=baseline.effects[type];e.present=e.active=true;e.flags=flags;e.level_count=count;e.opposite=opposite;e.masks[0]=UINT64_C(1)<<type;e.masks[1]=UINT64_C(1)<<(type+21);
        for(unsigned i=0;i<count && i<8;++i){float value=1.5f+0.5f*i;std::memcpy(e.levels[i]+52,&value,4);}}
    void Elemental(unsigned type,unsigned key,std::uintptr_t identity,unsigned reported=0){baseline.elementals[type].present=true;baseline.elementals[type].masks[0]=8;baseline.elementals[type].masks[1]=16;
        baseline.levels[baseline.level_count++]={type,key,identity,reported?reported:key};}
    ag::Result Build(unsigned count=1,const ag::Source* extra=nullptr){return ag::Build(baseline,sources,count,extra,accumulator,&output);}
    float Value(){float v=0;std::memcpy(&v,output.parameters+20,4);return v;}
};
struct Region {std::uintptr_t address;std::vector<std::uint8_t> bytes;};
struct Memory {
    std::vector<Region> regions;curves::CurveReadFence fence={1,1,true,true,true};
    std::uintptr_t Add(std::uintptr_t p,std::size_t n){regions.push_back({p,std::vector<std::uint8_t>(n)});return p;}
    template<class T>void Put(std::uintptr_t p,const T& value){for(auto& r:regions)if(p>=r.address && p-r.address<=r.bytes.size() && sizeof(value)<=r.bytes.size()-(p-r.address)){std::memcpy(r.bytes.data()+p-r.address,&value,sizeof(value));return;}std::abort();}
    static bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {for(auto& r:static_cast<Memory*>(user)->regions)if(p>=r.address && p-r.address<=r.bytes.size() && n<=r.bytes.size()-(p-r.address)){std::memcpy(out,r.bytes.data()+p-r.address,n);return true;}return false;}
    static bool Fence(void* user,curves::CurveReadFence* out) noexcept {*out=static_cast<Memory*>(user)->fence;return true;}
};
unsigned checks=0;
void Check(bool value,const char* name){++checks;if(!value){std::printf("FAIL %s\n",name);std::exit(1);}}
int main(){
    {Fixture f;Check(f.Build(0)==ag::Result::Succeeded && f.Value()==1 && f.output.effect_count==0,"native defaults");std::uint32_t kind=0;std::memcpy(&kind,f.output.parameters,4);Check(kind==UINT32_MAX,"kind default");}
    {Fixture f;f.Effect(0);f.sources[0].effects[0]={0,6};f.sources[0].effect_count=1;Check(f.Build()==ag::Result::Succeeded && f.calls.weight[0]==3 && f.Value()==4 && f.output.effects[0].level==6,"clamped level keeps native ratio and original pair");}
    {Fixture f;f.Effect(0);f.Effect(1,18,2,0);f.sources[0].effects[0]={0,3};f.sources[0].effects[1]={1,2};f.sources[0].effect_count=2;Check(f.Build()==ag::Result::Succeeded && f.calls.count==1 && f.calls.type[0]==0 && f.output.effects[0].level==1,"opposite cancellation before application");}
    {Fixture f;f.Effect(0);f.Effect(1,18);f.sources[0].effects[0]={0,4};f.sources[0].effects[1]={1,1};f.sources[0].effect_count=2;Check(f.Build()==ag::Result::Succeeded && f.calls.type[0]==1 && f.calls.type[1]==0,"priority effect order");}
    {Fixture f;f.Effect(0);f.Effect(1);f.Effect(2);f.sources[0].effects[0]={2,2};f.sources[0].effects[1]={0,1};f.sources[0].effects[2]={1,2};f.sources[0].effect_count=3;Check(f.Build()==ag::Result::Succeeded && f.calls.type[0]==1 && f.calls.type[1]==2 && f.calls.type[2]==0,"level then type order");}
    {Fixture f;f.Effect(0,18);for(unsigned i=0;i<5;++i)f.sources[0].effects[i]={i==4?0:-1,1};f.sources[0].effect_count=5;Check(f.Build()==ag::Result::Succeeded && f.calls.count==0,"priority effects past fourth slot excluded");}
    {Fixture f;f.Effect(0);f.baseline.effects[0].active=false;f.sources[0].effects[0]={0,2};f.sources[0].effect_count=1;Check(f.Build()==ag::Result::Succeeded && !f.output.effect_count,"inactive baseline effect omitted");}
    {Fixture f;f.sources[0].effects[0]={0,2};f.sources[0].effect_count=1;Check(f.Build()==ag::Result::Succeeded && !f.calls.count,"missing baseline effect omitted");}
    {Fixture f;f.Effect(0);f.sources[0].effects[0]={0,1};f.sources[0].effect_count=1;Check(f.Build()==ag::Result::Succeeded && f.output.masks[2]==1,"sign crossing adds effect masks");}
    {Fixture f;f.Effect(0);f.Effect(1,2,2,0);f.sources[0].effects[0]={0,2};f.sources[0].effects[1]={1,1};f.sources[0].effect_count=2;Check(f.Build()==ag::Result::Succeeded && f.output.masks[2]==2,"later crossing clears opposing masks");}
    {Fixture f;f.Elemental(0,1,0x2000);f.Elemental(0,3,0x3000);f.sources[0].elemental_levels[0]=1;f.sources[1].elemental_levels[0]=3;Check(f.Build(2)==ag::Result::Succeeded && f.output.elemental_levels[0]==0x3000,"summed elemental levels select available floor");}
    {Fixture f;f.Elemental(0,1,0x2000);f.Elemental(0,3,0x3000);f.sources[0].elemental_levels[0]=3;f.upgrade.elemental_levels[0]=1;Check(f.Build(1,&f.upgrade)==ag::Result::Succeeded && f.output.elemental_levels[0]==0x2000 && f.output.masks[0]==8,"upgrade overrides selected level after modifiers");}
    {Fixture f;f.Effect(0);f.sources[0].effects[0]={0,INT32_MAX};f.sources[0].effect_count=1;f.sources[1].effects[0]={0,1};f.sources[1].effect_count=1;Check(f.Build(2)==ag::Result::Overflow,"signed effect sum overflow rejected");}
    {Fixture f;f.sources[0].elemental_levels[0]=UINT32_MAX;Check(f.Build()==ag::Result::Overflow,"elemental level overflow rejected");}
    {Fixture f;f.sources[0].effect_count=65;Check(f.Build()==ag::Result::InvalidSource,"effect count bounded");}
    {Fixture f;f.Effect(0);f.sources[0].effects[0]={0,1};f.sources[0].effect_count=1;f.calls.fail=true;std::memset(&f.output,0xa5,sizeof(f.output));auto before=f.output;Check(f.Build()==ag::Result::NativeFailed && !std::memcmp(&before,&f.output,sizeof(before)),"failed accumulation publishes no output");}
    {Fixture f;f.baseline.fence.initial_pristine=false;Check(f.Build()==ag::Result::InvalidBaseline,"returned menu cannot become baseline");}
    {Fixture f;Memory memory;constexpr auto base=UINT64_C(0x10000000),object=UINT64_C(0x50000000),level=UINT64_C(0x60000000),pairs=UINT64_C(0x70000000),vt=base+0x162B6C8;
        memory.Add(object,656);memory.Add(vt,3400);memory.Add(level,160);memory.Add(pairs,8);memory.Put(object,vt);memory.Put(object+392,level);memory.Put<std::uint32_t>(level+112,3);
        memory.Put(object+560,pairs);memory.Put<std::uint32_t>(object+568,1);memory.Put<std::uint32_t>(object+572,1);memory.Put(pairs,ag::Pair{0,2});
        for(const auto route:std::vector<std::pair<unsigned,unsigned>>{{0,0x6B0100},{128,0x6B0B30},{3352,0x6B09B0},{3360,0x6B0990},{3368,0x6B0970}})memory.Put<std::uintptr_t>(vt+route.first,base+route.second);
        f.Elemental(0,3,level);curves::NativeCurveAccess access={};access.module_base=base;access.read=Memory::Read;access.fence=Memory::Fence;access.user=&memory;std::memcpy(access.module_sha256,curves::kPlayerGameDllSha256,65);ag::Source source={};
        Check(ag::CaptureSource(access,f.baseline,object,&source)==ag::Result::Succeeded && source.elemental_levels[0]==3 && source.effects[0].level==2,"native source graph capture");
        memory.Put<std::uintptr_t>(object+392,level+8);Check(ag::CaptureSource(access,f.baseline,object,&source)==ag::Result::InvalidSource,"foreign elemental pointer rejected");}
    {Fixture f;Memory memory;constexpr auto manager=UINT64_C(0x1000),effect=UINT64_C(0x2000),effectlevels=UINT64_C(0x3000),elemental=UINT64_C(0x4000),level=UINT64_C(0x5000);
        memory.Add(manager,48);memory.Add(effect,232);memory.Add(effectlevels,56);memory.Add(elemental,328);memory.Add(level,200);
        memory.Put<std::uintptr_t>(manager,effect+200);memory.Put<std::uint32_t>(manager+8,1);memory.Put<std::uint8_t>(effect+8+72,1);memory.Put<std::uint32_t>(effect+8+84,2);
        memory.Put<std::uintptr_t>(effect+8+96,effectlevels);memory.Put<std::uint32_t>(effect+8+104,1);memory.Put<std::uint32_t>(effect+8+108,1);
        memory.Put<std::uintptr_t>(manager+24,elemental+296);memory.Put<std::uint32_t>(manager+32,1);memory.Put<std::uintptr_t>(elemental+8+104,level+168);memory.Put<std::uint32_t>(elemental+8+112,1);memory.Put<std::uint32_t>(level,3);memory.Put<std::uint32_t>(level+8+112,3);
        std::vector<cs::Span> spans;std::vector<std::uint8_t> bytes;for(const auto& r:memory.regions){spans.push_back({r.address,static_cast<std::uint32_t>(r.bytes.size()),static_cast<std::uint32_t>(bytes.size())});bytes.insert(bytes.end(),r.bytes.begin(),r.bytes.end());}
        cs::Snapshot frozen={};frozen.spans=spans.data();frozen.span_count=frozen.span_capacity=spans.size();frozen.bytes=bytes.data();frozen.used=frozen.capacity=bytes.size();frozen.complete=true;frozen.fence=f.baseline.fence;
        ag::Baseline out={};Check(ag::CaptureFrozenBaseline(frozen,&out) && out.effects[0].present && out.level_count==1 && out.levels[0].identity==level+8,"sealed graph inputs retain actual level identity");}
    std::printf("Item aggregate rebuild: %u checks passed (19 scenarios)\n",checks);return 0;
}
