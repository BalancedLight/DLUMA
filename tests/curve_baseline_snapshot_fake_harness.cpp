#include "fixes/custom_map_asset_isolation/CurveBaselineSnapshot.h"
#include <cstdio>
#include <cstring>
#include <utility>

namespace curves=dluma::fixes::custom_map_asset_isolation::curves;
using curves::CurveResult;using curves::CurvePrefixKind;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct NativeShape {
    std::uintptr_t vt,samples;std::uint32_t sample_count,sample_capacity,sample_allocator,pad28;
    std::uint32_t bounds[4],requested_samples,pad52;std::uintptr_t name;
    std::uint32_t name_count,name_capacity,sum,pad76;std::uintptr_t points;
    std::uint32_t point_count,point_capacity,point_allocator,pad100,logical_count,pad108;
};
struct NativeVector {std::uintptr_t data;std::uint32_t count,capacity,allocator,padding;};
struct Fake {
    static constexpr std::uintptr_t base=0x10000000;
    NativeShape shapes[4]={};curves::CurvePoint points[4][3]={};
    std::uint32_t samples[4][3]={};char names[4][64]={};std::uintptr_t objects[4]={};
    std::uintptr_t manager_vt=base+curves::kManagerVtableRva;
    std::uintptr_t manager=reinterpret_cast<std::uintptr_t>(&manager_vt);
    NativeVector vector={reinterpret_cast<std::uintptr_t>(objects),0,4,51,0};
    curves::CurveReadFence fence={3,1,true,true,true};unsigned reads=0,change_at=0;
    void add(const char* name){auto i=vector.count++;auto& s=shapes[i];std::strcpy(names[i],name);
        s.vt=base+curves::kSplineVtableRva;s.samples=reinterpret_cast<std::uintptr_t>(samples[i]);
        s.sample_count=2;s.sample_capacity=3;s.sample_allocator=51;s.requested_samples=2;
        s.bounds[0]=0;s.bounds[1]=0x40000000;s.bounds[2]=0xbf800000;s.bounds[3]=0x3f800000;
        s.name=reinterpret_cast<std::uintptr_t>(names[i]);s.name_count=static_cast<unsigned>(std::strlen(name));s.name_capacity=63;
        s.points=reinterpret_cast<std::uintptr_t>(points[i]);s.point_count=2;s.point_capacity=3;s.point_allocator=51;s.logical_count=2;
        samples[i][0]=0xbf800000;samples[i][1]=0x3f800000;s.sum=0;
        points[i][0]={0,0xbf800000,1};points[i][1]={0x40000000,0x3f800000,0};objects[i]=reinterpret_cast<std::uintptr_t>(&s);
    }
    Fake(){add("base_curve");add("global_extra");}
};
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* object,std::size_t size){
    auto b=reinterpret_cast<std::uintptr_t>(object);if(p<b || n>size || p-b>size-n)return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;
}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fake*>(user);if(++f.reads==f.change_at)++f.fence.mutation_revision;
    if(p==f.base+curves::kManagerRva && n==8){std::memcpy(out,&f.manager,n);return true;}
    if(p==f.base+curves::kVectorRva && n==sizeof(f.vector)){std::memcpy(out,&f.vector,n);return true;}
    return Copy(p,out,n,&f.manager_vt,8)||Copy(p,out,n,f.shapes,sizeof(f.shapes))||Copy(p,out,n,f.objects,sizeof(f.objects))||
        Copy(p,out,n,f.points,sizeof(f.points))||Copy(p,out,n,f.samples,sizeof(f.samples))||Copy(p,out,n,f.names,sizeof(f.names));
}
bool Fence(void* user,curves::CurveReadFence* out) noexcept {*out=static_cast<Fake*>(user)->fence;return true;}
struct Storage {
    curves::CurveRecord records[4]={};std::uint8_t bytes[2048]={};
    curves::CurveSnapshot s={records,4,bytes,sizeof(bytes)};
};
struct Fixture {
    Fake f;Storage baseline,current;curves::NativeCurveAccess access={};
    Fixture(){access.module_base=f.base;std::strcpy(access.module_sha256,curves::kPlayerGameDllSha256);access.read=Read;access.fence=Fence;access.user=&f;}
    CurveResult capture(Storage& s){return curves::CaptureCurveSnapshot(&access,&s.s);}
};
bool DeepOrderedBaseline(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);CHECK(f.baseline.s.count==2);
    const auto& r=f.baseline.records[1];CHECK(!std::strcmp(reinterpret_cast<const char*>(f.baseline.bytes+r.name_offset),"global_extra"));
    std::memset(f.f.names,0,sizeof(f.f.names));std::memset(f.f.points,0,sizeof(f.f.points));
    CHECK(!std::strcmp(reinterpret_cast<const char*>(f.baseline.bytes+r.name_offset),"global_extra"));
    curves::CurvePoint p={};std::memcpy(&p,f.baseline.bytes+r.points_offset,sizeof(p));CHECK(p.y_bits==0xbf800000 && p.type==1);return true;
}
bool AppendedTailIsSeparateFromPrefixChange(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);f.f.add("custom_tail");CHECK(f.capture(f.current)==CurveResult::Succeeded);
    auto a=curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s);CHECK(a.kind==CurvePrefixKind::AppendedTail && a.tail_begin==2 && a.tail_count==1);
    f.f.points[0][0].type=0;CHECK(f.capture(f.current)==CurveResult::Succeeded);a=curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s);
    CHECK(a.kind==CurvePrefixKind::BaselineValueChanged && a.first_difference==0);return true;
}
bool ReorderedAndReplacedObjectsAreNotAppendOnly(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);std::swap(f.f.objects[0],f.f.objects[1]);CHECK(f.capture(f.current)==CurveResult::Succeeded);
    CHECK(curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s).kind==CurvePrefixKind::BaselineIdentityChanged);
    std::swap(f.f.objects[0],f.f.objects[1]);++f.f.fence.owner_generation;CHECK(f.capture(f.current)==CurveResult::Succeeded);
    CHECK(curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s).kind==CurvePrefixKind::BaselineIdentityChanged);return true;
}
bool LostBaselineEntriesRefuse(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);--f.f.vector.count;CHECK(f.capture(f.current)==CurveResult::Succeeded);
    CHECK(curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s).kind==CurvePrefixKind::BaselineMissing);return true;
}
bool NativeBigEndianEncoding(){
    Fixture f;f.f.vector.count=1;std::strcpy(f.f.names[0],"a");f.f.shapes[0].name_count=1;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);
    std::uint8_t output[128]={};std::size_t written=99;
    CHECK(curves::EncodeCurveBaseline(&f.baseline.s,output,sizeof(output),&written)==CurveResult::Succeeded);
    const std::uint8_t expected[]={
        0,0,1,'a',0,2, 0,0,0,0, 0x40,0,0,0, 0xbf,0x80,0,0, 0x3f,0x80,0,0, 0,2,
        1, 0,0,0,0, 0xbf,0x80,0,0, 0, 0x40,0,0,0, 0x3f,0x80,0,0};
    CHECK(written==sizeof(expected) && !std::memcmp(output,expected,sizeof(expected)));return true;
}
bool BinaryIncludesGlobalAdditions(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);std::uint8_t output[256]={};std::size_t written=0;
    CHECK(curves::EncodeCurveBaseline(&f.baseline.s,output,sizeof(output),&written)==CurveResult::Succeeded);
    CHECK(!std::memcmp(output+3,"base_curve",10));auto second=23+10+18;
    CHECK(!std::memcmp(output+second+3,"global_extra",12));CHECK(written==2*(23+18)+10+12);return true;
}
bool InvalidShapeAndUnknownClassRefuse(){
    Fixture f;f.f.shapes[0].logical_count=1;CHECK(f.capture(f.baseline)==CurveResult::InvalidShape);
    f.f.shapes[0].logical_count=2;f.f.shapes[0].vt++;CHECK(f.capture(f.baseline)==CurveResult::UnsupportedType);
    --f.f.shapes[0].vt;f.f.points[0][1].x_bits=0xbf800000;CHECK(f.capture(f.baseline)==CurveResult::InvalidSnapshot);return true;
}
bool SnapshotBoundsAndNoPartialEncoding(){
    Fixture f;f.baseline.s.storage_capacity=4;CHECK(f.capture(f.baseline)==CurveResult::CapacityExceeded);CHECK(!f.baseline.s.complete);
    f.baseline.s.storage_capacity=sizeof(f.baseline.bytes);CHECK(f.capture(f.baseline)==CurveResult::Succeeded);
    std::uint8_t output[1]={0x55};std::size_t written=99;CHECK(curves::EncodeCurveBaseline(&f.baseline.s,output,1,&written)==CurveResult::CapacityExceeded);
    CHECK(written==0 && output[0]==0x55);return true;
}
bool NativeRevisionAndGateAreRequired(){
    Fixture f;f.f.fence.access_closed=false;CHECK(f.capture(f.baseline)==CurveResult::AccessNotQuiescent);
    f.f.fence.access_closed=true;f.f.change_at=f.f.reads+5;CHECK(f.capture(f.baseline)==CurveResult::ChangedDuringCapture);
    CHECK(!f.baseline.s.complete);return true;
}
bool GeneratedSamplesArePartOfDeepIdentity(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);f.f.samples[0][0]=0;
    CHECK(f.capture(f.current)==CurveResult::Succeeded);CHECK(curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s).kind==CurvePrefixKind::BaselineValueChanged);return true;
}
bool InvalidStorageOffsetsCannotBeEncoded(){
    Fixture f;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);f.baseline.records[0].points_offset=0xfffffff0;
    std::uint8_t output[256]={};std::size_t n=0;CHECK(curves::EncodeCurveBaseline(&f.baseline.s,output,sizeof(output),&n)==CurveResult::InvalidSnapshot);return true;
}
bool DuplicateNamesAndNonfiniteValuesRefuse(){
    Fixture f;std::strcpy(f.f.names[1],"BASE_CURVE");f.f.shapes[1].name_count=10;CHECK(f.capture(f.baseline)==CurveResult::InvalidSnapshot);
    Fixture g;g.f.samples[0][0]=0x7fc00000;CHECK(g.capture(g.baseline)==CurveResult::InvalidSnapshot);return true;
}
bool BinaryLimitsDoNotEraseTheDeepSnapshot(){
    Fixture f;f.f.points[0][0].type=130;CHECK(f.capture(f.baseline)==CurveResult::Succeeded);
    std::size_t bytes=99;CHECK(curves::MeasureCurveBaselineBinary(&f.baseline.s,&bytes)==CurveResult::BinaryLimitExceeded);
    CHECK(bytes==0 && f.baseline.s.complete);CHECK(f.capture(f.current)==CurveResult::Succeeded);
    CHECK(curves::ClassifyCurvePrefix(&f.baseline.s,&f.current.s).kind==CurvePrefixKind::Identical);return true;
}
}
int main(){const struct{const char* name;bool(*run)();} cases[]={
    {"deep ordered B0",DeepOrderedBaseline},{"append versus modified prefix",AppendedTailIsSeparateFromPrefixChange},
    {"identity/order/generation",ReorderedAndReplacedObjectsAreNotAppendOnly},{"lost B0 entries",LostBaselineEntriesRefuse},
    {"native binary byte format",NativeBigEndianEncoding},{"global additions",BinaryIncludesGlobalAdditions},
    {"shape/type validation",InvalidShapeAndUnknownClassRefuse},{"bounded preflight encoding",SnapshotBoundsAndNoPartialEncoding},
    {"actual read-gate contract",NativeRevisionAndGateAreRequired},{"generated samples",GeneratedSamplesArePartOfDeepIdentity},
    {"untrusted snapshot offsets",InvalidStorageOffsetsCannotBeEncoded},{"duplicates/nonfinite",DuplicateNamesAndNonfiniteValuesRefuse},
    {"binary limits are separate",BinaryLimitsDoNotEraseTheDeepSnapshot}};
    for(const auto& c:cases)if(!c.run()){std::printf("FAILED %s\n",c.name);return 1;}
    std::printf("Curve baseline snapshot: %zu scenarios passed\n",sizeof(cases)/sizeof(cases[0]));return 0;
}
