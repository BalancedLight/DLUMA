#include "fixes/custom_map_asset_isolation/ItemCurveBindings.h"
#include <cstdio>
#include <cstring>
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
using c::CurveBindingResult;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Snapshot {
    c::CurveRecord records[3]={};std::uint8_t bytes[128]={};
    c::CurveSnapshot s={records,3,bytes,sizeof(bytes),2,0,{3,1,true,true,true},0x4000,true};
    Snapshot(){
        const char* names[]={"first","second","tail"};
        for(unsigned i=0;i<3;++i){auto& r=records[i];r.native_identity=0x1000+0x1000*i;r.name_offset=static_cast<unsigned>(s.storage_used);
            r.name_bytes=static_cast<unsigned>(std::strlen(names[i]));std::memcpy(bytes+s.storage_used,names[i],r.name_bytes+1);s.storage_used+=r.name_bytes+1;
            r.points_offset=static_cast<unsigned>(s.storage_used);c::CurvePoint p={0,0,0};std::memcpy(bytes+s.storage_used,&p,sizeof(p));s.storage_used+=sizeof(p);
            r.point_count=1;r.samples_offset=r.points_offset;r.bounds_bits[1]=0x3f800000;r.bounds_bits[3]=0x3f800000;}
    }
};
struct Fake {
    static constexpr std::uintptr_t base=0x10000000,owner=base+c::kItemManagerRva;
    std::uint8_t memory[0x260]={};std::uintptr_t indexed[6]={0x1000,0x2000,0,0x1000,0x2000,0};
    c::CurveReadFence fence={3,1,true,true,true};unsigned reads=0,change_at=0;
    template<class T>void put(std::size_t at,T value){std::memcpy(memory+at,&value,sizeof(value));}
    Fake(){put(0,base+c::kItemManagerVtableRva);put(0x218,reinterpret_cast<std::uintptr_t>(indexed));put<std::uint32_t>(0x220,6);put<std::uint32_t>(0x224,6);
        const std::uintptr_t scalars[]={0x1000,0,0x2000,0,0x1000};std::memcpy(memory+0x230,scalars,sizeof(scalars));}
};
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fake*>(user);if(++f.reads==f.change_at)++f.fence.mutation_revision;
    if(p>=f.owner && n<=sizeof(f.memory) && p-f.owner<=sizeof(f.memory)-n){std::memcpy(out,f.memory+p-f.owner,n);return true;}
    auto b=reinterpret_cast<std::uintptr_t>(f.indexed);
    if(p>=b && n<=sizeof(f.indexed) && p-b<=sizeof(f.indexed)-n){std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}return false;
}
bool Fence(void* user,c::CurveReadFence* f) noexcept {*f=static_cast<Fake*>(user)->fence;return true;}
struct Fixture {
    Fake f;Snapshot b,s;c::NativeCurveAccess a={};c::ItemCurveBindingSnapshot bb={},cb={};
    Fixture(){a.module_base=f.base;std::strcpy(a.module_sha256,c::kPlayerGameDllSha256);a.read=Read;a.fence=Fence;a.user=&f;}
    CurveBindingResult baseline(){return c::CaptureItemCurveBindings(&a,&b.s,&bb);}
    CurveBindingResult current(){s.s.count=3;return c::CaptureItemCurveBindings(&a,&s.s,&cb);}
};
bool CapturesIdsAndNulls(){Fixture f;CHECK(f.baseline()==CurveBindingResult::Succeeded);CHECK(f.bb.indexed_ids[0]==0 && f.bb.indexed_ids[1]==1 && f.bb.indexed_ids[2]==c::kNullCurveId);CHECK(f.bb.scalar_ids[1]==c::kNullCurveId && f.bb.scalar_ids[4]==0);return true;}
bool PlansOnlyChangedBindings(){Fixture f;CHECK(f.baseline()==CurveBindingResult::Succeeded);f.f.indexed[1]=0x3000;f.f.put<std::uintptr_t>(0x238,0x3000);CHECK(f.current()==CurveBindingResult::Succeeded);
    c::ItemCurveBindingRestorePlan p={};CHECK(c::PlanItemCurveBindingRestore(&f.b.s,&f.bb,&f.s.s,&f.cb,&p)==CurveBindingResult::Succeeded);
    CHECK(p.prepared && p.count==2);CHECK(p.writes[0].address==reinterpret_cast<std::uintptr_t>(f.f.indexed)+8 && p.writes[0].expected==0x3000 && p.writes[0].desired==0x2000);
    CHECK(p.writes[1].address==f.f.owner+0x238 && p.writes[1].expected==0x3000 && !p.writes[1].desired);CHECK(f.f.indexed[1]==0x3000);return true;}
bool UnknownPointersRefuse(){Fixture f;f.f.indexed[0]=0xdead;CHECK(f.baseline()==CurveBindingResult::UnknownCurveReference && !f.bb.complete);return true;}
bool ShapeAndOwnerMustMatch(){Fixture f;f.f.put<std::uint32_t>(0x220,7);CHECK(f.baseline()==CurveBindingResult::ShapeChanged);f.f.put<std::uint32_t>(0x220,6);f.f.put<std::uintptr_t>(0,0);CHECK(f.baseline()==CurveBindingResult::ProfileMismatch);return true;}
bool NoRebindAfterReplacingB0(){Fixture f;CHECK(f.baseline()==CurveBindingResult::Succeeded);CHECK(f.current()==CurveBindingResult::Succeeded);f.s.records[0].native_identity=0x5000;
    c::ItemCurveBindingRestorePlan p={};CHECK(c::PlanItemCurveBindingRestore(&f.b.s,&f.bb,&f.s.s,&f.cb,&p)==CurveBindingResult::BaselineNotPreserved && !p.prepared);return true;}
bool FinalBindingVerificationIsExact(){Fixture f;CHECK(f.baseline()==CurveBindingResult::Succeeded);f.f.indexed[0]=0x3000;CHECK(f.current()==CurveBindingResult::Succeeded);CHECK(c::VerifyItemCurveBindings(&f.bb,&f.cb)==CurveBindingResult::BaselineNotPreserved);
    f.f.indexed[0]=0x1000;CHECK(f.current()==CurveBindingResult::Succeeded);CHECK(c::VerifyItemCurveBindings(&f.bb,&f.cb)==CurveBindingResult::Succeeded);return true;}
bool JointFenceIsRequired(){Fixture f;f.f.fence.readers_drained=false;CHECK(f.baseline()==CurveBindingResult::AccessNotQuiescent);f.f.fence.readers_drained=true;f.f.change_at=f.f.reads+3;CHECK(f.baseline()==CurveBindingResult::ChangedDuringCapture);return true;}
bool CleanBindingsNeedNoWrites(){Fixture f;CHECK(f.baseline()==CurveBindingResult::Succeeded);CHECK(f.current()==CurveBindingResult::Succeeded);c::ItemCurveBindingRestorePlan p={};CHECK(c::PlanItemCurveBindingRestore(&f.b.s,&f.bb,&f.s.s,&f.cb,&p)==CurveBindingResult::Succeeded && p.prepared && !p.count);return true;}
}
int main(){bool(*tests[])()={CapturesIdsAndNulls,PlansOnlyChangedBindings,UnknownPointersRefuse,ShapeAndOwnerMustMatch,NoRebindAfterReplacingB0,FinalBindingVerificationIsExact,JointFenceIsRequired,CleanBindingsNeedNoWrites};
    for(auto test:tests)if(!test())return 1;std::printf("Item curve bindings: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
