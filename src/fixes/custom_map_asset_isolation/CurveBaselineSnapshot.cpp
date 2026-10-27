#define _HAS_EXCEPTIONS 0
#include "CurveBaselineSnapshot.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
struct VectorHeader {std::uintptr_t data;std::uint32_t count,capacity,allocator,padding;};
struct SplineLayout {
    std::uintptr_t vtable,samples;
    std::uint32_t sample_count,sample_capacity,sample_allocator,padding28;
    std::uint32_t bounds[4],requested_samples,padding52;
    std::uintptr_t name;
    std::uint32_t name_count,name_capacity,sample_sum,padding76;
    std::uintptr_t points;
    std::uint32_t point_count,point_capacity,point_allocator,padding100,logical_count,padding108;
};
static_assert(sizeof(SplineLayout)==0x70 && sizeof(CurvePoint)==12 && sizeof(VectorHeader)==24,"exact native layout");
static_assert(offsetof(SplineLayout,name)==56 && offsetof(SplineLayout,points)==80 &&
    offsetof(SplineLayout,logical_count)==104,"exact native offsets");
bool Read(const NativeCurveAccess& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Finite(std::uint32_t bits) noexcept {return (bits&0x7f800000U)!=0x7f800000U;}
float Float(std::uint32_t bits) noexcept {float v=0;std::memcpy(&v,&bits,4);return v;}
bool EqualFence(const CurveReadFence& a,const CurveReadFence& b) noexcept {
    return a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision &&
        a.authoritative_thread==b.authoritative_thread && a.access_closed==b.access_closed && a.readers_drained==b.readers_drained;
}
CurveResult Fence(const NativeCurveAccess& a,CurveReadFence* out) noexcept {
    *out={};if(!a.fence(a.user,out) || !out->owner_generation)return CurveResult::ReadFailed;
    return out->authoritative_thread && out->access_closed && out->readers_drained ? CurveResult::Succeeded : CurveResult::AccessNotQuiescent;
}
bool Range(std::size_t offset,std::size_t n,std::size_t size) noexcept {return offset<=size && n<=size-offset;}
bool Overlap(const void* a,std::size_t an,const void* b,std::size_t bn) noexcept {
    auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    return x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn || (x<y+bn && y<x+an);
}
bool SameName(const char* a,const char* b) noexcept {
    for(;;){unsigned char x=static_cast<unsigned char>(*a++),y=static_cast<unsigned char>(*b++);
        if(x>='a' && x<='z')x&=0xdf;if(y>='a' && y<='z')y&=0xdf;
        if(x!=y)return false;if(!x)return true;}
}
CurveResult StoreRead(const NativeCurveAccess& a,std::uintptr_t p,std::size_t n,
    CurveSnapshot* out,std::uint32_t* offset) noexcept {
    const auto aligned=(out->storage_used+3U)&~std::size_t(3U);
    if(!Range(aligned,n,out->storage_capacity))return CurveResult::CapacityExceeded;
    if(n && !Read(a,p,out->storage+aligned,n))return CurveResult::ReadFailed;
    *offset=static_cast<std::uint32_t>(aligned);out->storage_used=aligned+n;return CurveResult::Succeeded;
}
bool EqualRecord(const CurveSnapshot& a,const CurveRecord& x,const CurveSnapshot& b,const CurveRecord& y) noexcept {
    return x.native_name==y.native_name && x.native_samples==y.native_samples && x.native_points==y.native_points &&
        x.name_bytes==y.name_bytes && x.sample_count==y.sample_count && x.point_count==y.point_count &&
        x.requested_samples==y.requested_samples && x.sample_sum_bits==y.sample_sum_bits &&
        !std::memcmp(x.bounds_bits,y.bounds_bits,sizeof(x.bounds_bits)) &&
        !std::memcmp(a.storage+x.name_offset,b.storage+y.name_offset,x.name_bytes+1U) &&
        !std::memcmp(a.storage+x.samples_offset,b.storage+y.samples_offset,4U*x.sample_count) &&
        !std::memcmp(a.storage+x.points_offset,b.storage+y.points_offset,sizeof(CurvePoint)*x.point_count);
}
void Put16(std::uint8_t*& out,std::uint32_t n) noexcept {*out++=static_cast<std::uint8_t>(n>>8);*out++=static_cast<std::uint8_t>(n);}
void Put32(std::uint8_t*& out,std::uint32_t n) noexcept {
    *out++=static_cast<std::uint8_t>(n>>24);*out++=static_cast<std::uint8_t>(n>>16);*out++=static_cast<std::uint8_t>(n>>8);*out++=static_cast<std::uint8_t>(n);
}
}  // namespace

CurveResult ValidateCurveSnapshot(const CurveSnapshot* s) noexcept {
    if(!s || !s->complete || !s->records || !s->storage || s->count>s->record_capacity ||
        s->count>kMaximumCurves || s->record_capacity>kMaximumCurves || s->storage_used>s->storage_capacity ||
        s->storage_capacity>kMaximumCurveStorage || !s->manager_identity)return CurveResult::InvalidSnapshot;
    for(std::size_t i=0;i<s->count;++i){
        const auto& r=s->records[i];
        if(!r.native_identity || r.name_bytes>kMaximumCurveName ||
            !Range(r.name_offset,static_cast<std::size_t>(r.name_bytes)+1,s->storage_used) ||
            !Range(r.samples_offset,4ULL*r.sample_count,s->storage_used) ||
            !Range(r.points_offset,sizeof(CurvePoint)*static_cast<std::size_t>(r.point_count),s->storage_used) ||
            !r.point_count || r.point_count>65535 || r.sample_count>65535 || r.requested_samples>65535)
            return CurveResult::InvalidSnapshot;
        const char* name=reinterpret_cast<const char*>(s->storage+r.name_offset);
        if(name[r.name_bytes] || std::memchr(name,0,r.name_bytes))return CurveResult::InvalidSnapshot;
        for(auto bits:r.bounds_bits)if(!Finite(bits))return CurveResult::InvalidSnapshot;
        if(Float(r.bounds_bits[1])<Float(r.bounds_bits[0]) || Float(r.bounds_bits[3])<Float(r.bounds_bits[2]) ||
            !Finite(r.sample_sum_bits))return CurveResult::InvalidSnapshot;
        for(std::uint32_t j=0;j<r.sample_count;++j){std::uint32_t bits=0;
            std::memcpy(&bits,s->storage+r.samples_offset+4U*j,4);if(!Finite(bits))return CurveResult::InvalidSnapshot;}
        for(std::uint32_t j=0;j<r.point_count;++j){CurvePoint p={};
            std::memcpy(&p,s->storage+r.points_offset+sizeof(p)*j,sizeof(p));
            if(!Finite(p.x_bits) || !Finite(p.y_bits))return CurveResult::InvalidSnapshot;
            if(j){CurvePoint before={};std::memcpy(&before,s->storage+r.points_offset+sizeof(p)*(j-1),sizeof(before));
                if(Float(p.x_bits)<Float(before.x_bits))return CurveResult::InvalidSnapshot;}}
        for(std::size_t j=0;j<i;++j)if(s->records[j].native_identity==r.native_identity ||
            SameName(name,reinterpret_cast<const char*>(s->storage+s->records[j].name_offset)))return CurveResult::InvalidSnapshot;
    }
    return CurveResult::Succeeded;
}
CurveResult CaptureCurveSnapshot(const NativeCurveAccess* a,CurveSnapshot* s) noexcept {
    if(!a || !s || !a->read || !a->fence || !s->records || !s->storage ||
        !s->record_capacity || s->record_capacity>kMaximumCurves || !s->storage_capacity ||
        s->storage_capacity>kMaximumCurveStorage ||
        Overlap(s->records,s->record_capacity*sizeof(CurveRecord),s->storage,s->storage_capacity))return CurveResult::InvalidArgument;
    s->complete=false;s->count=0;s->storage_used=0;
    if(!a->module_base || a->module_base>UINTPTR_MAX-0x2000000U ||
        std::memcmp(a->module_sha256,kPlayerGameDllSha256,sizeof(kPlayerGameDllSha256)))return CurveResult::ProfileMismatch;
    CurveReadFence first={},last={};auto status=Fence(*a,&first);if(status!=CurveResult::Succeeded)return status;
    VectorHeader v={},check={};std::uintptr_t manager=0,manager_check=0,vtable=0;
    if(!Read(*a,a->module_base+kManagerRva,&manager,8) || !Read(*a,manager,&vtable,8) ||
        !Read(*a,a->module_base+kVectorRva,&v,sizeof(v)))return CurveResult::ReadFailed;
    if(vtable!=a->module_base+kManagerVtableRva)return CurveResult::ProfileMismatch;
    if(v.count>v.capacity || v.count>s->record_capacity || (v.count && !v.data))return CurveResult::CapacityExceeded;
    s->manager_identity=manager;
    for(std::uint32_t i=0;i<v.count;++i){
        std::uintptr_t object=0,object_check=0;SplineLayout native={},after={};
        if(!Read(*a,v.data+8ULL*i,&object,8) || !Read(*a,object,&native,sizeof(native)))return CurveResult::ReadFailed;
        if(native.vtable!=a->module_base+kSplineVtableRva)return CurveResult::UnsupportedType;
        if(native.sample_count>native.sample_capacity || native.point_count>native.point_capacity ||
            native.name_count>native.name_capacity || native.name_count>kMaximumCurveName ||
            native.point_count!=native.logical_count || !native.point_count || native.point_count>65535 ||
            native.sample_count>65535 || native.requested_samples>65535)return CurveResult::InvalidShape;
        CurveRecord record={};record.native_identity=object;record.name_bytes=native.name_count;
        record.native_name=native.name;record.native_samples=native.samples;record.native_points=native.points;
        record.sample_count=native.sample_count;record.point_count=native.point_count;
        record.requested_samples=native.requested_samples;record.sample_sum_bits=native.sample_sum;
        std::memcpy(record.bounds_bits,native.bounds,sizeof(record.bounds_bits));
        if(!native.name && !native.name_count){
            if(s->storage_used==s->storage_capacity)return CurveResult::CapacityExceeded;
            record.name_offset=static_cast<std::uint32_t>(s->storage_used);s->storage[s->storage_used++]=0;
        } else {status=StoreRead(*a,native.name,native.name_count+1U,s,&record.name_offset);if(status!=CurveResult::Succeeded)return status;}
        status=StoreRead(*a,native.samples,4ULL*native.sample_count,s,&record.samples_offset);if(status!=CurveResult::Succeeded)return status;
        status=StoreRead(*a,native.points,sizeof(CurvePoint)*native.point_count,s,&record.points_offset);if(status!=CurveResult::Succeeded)return status;
        if(!Read(*a,object,&after,sizeof(after)) || !Read(*a,v.data+8ULL*i,&object_check,8))return CurveResult::ReadFailed;
        if(object!=object_check || std::memcmp(&native,&after,sizeof(native)))return CurveResult::ChangedDuringCapture;
        s->records[s->count++]=record;
    }
    if(!Read(*a,a->module_base+kVectorRva,&check,sizeof(check)) ||
        !Read(*a,a->module_base+kManagerRva,&manager_check,8))return CurveResult::ReadFailed;
    status=Fence(*a,&last);if(status!=CurveResult::Succeeded)return status;
    if(manager!=manager_check || std::memcmp(&v,&check,sizeof(v)) || !EqualFence(first,last))return CurveResult::ChangedDuringCapture;
    s->fence=last;s->complete=true;status=ValidateCurveSnapshot(s);
    if(status!=CurveResult::Succeeded)s->complete=false;return status;
}
CurvePrefixAssessment ClassifyCurvePrefix(const CurveSnapshot* b,const CurveSnapshot* c) noexcept {
    CurvePrefixAssessment result={CurvePrefixKind::InvalidSnapshot,0,0,0};
    if(ValidateCurveSnapshot(b)!=CurveResult::Succeeded || ValidateCurveSnapshot(c)!=CurveResult::Succeeded)return result;
    if(c->count<b->count){result.kind=CurvePrefixKind::BaselineMissing;result.first_difference=c->count;return result;}
    if(b->manager_identity!=c->manager_identity || b->fence.owner_generation!=c->fence.owner_generation){result.kind=CurvePrefixKind::BaselineIdentityChanged;return result;}
    for(std::size_t i=0;i<b->count;++i){
        if(b->records[i].native_identity!=c->records[i].native_identity){result.kind=CurvePrefixKind::BaselineIdentityChanged;result.first_difference=i;return result;}
        if(!EqualRecord(*b,b->records[i],*c,c->records[i])){result.kind=CurvePrefixKind::BaselineValueChanged;result.first_difference=i;return result;}
    }
    result.kind=c->count==b->count?CurvePrefixKind::Identical:CurvePrefixKind::AppendedTail;
    result.tail_begin=b->count;result.tail_count=c->count-b->count;result.first_difference=b->count;return result;
}
CurveResult MeasureCurveBaselineBinary(const CurveSnapshot* s,std::size_t* bytes_out) noexcept {
    if(!bytes_out)return CurveResult::InvalidArgument;*bytes_out=0;
    auto result=ValidateCurveSnapshot(s);if(result!=CurveResult::Succeeded)return result;
    std::size_t size=0;
    for(std::size_t i=0;i<s->count;++i){const auto& r=s->records[i];
        const std::size_t bytes=23ULL+r.name_bytes+9ULL*r.point_count;
        if(bytes>SIZE_MAX-size)return CurveResult::BinaryLimitExceeded;size+=bytes;
        for(std::uint32_t j=0;j<r.point_count;++j){CurvePoint p={};std::memcpy(&p,s->storage+r.points_offset+sizeof(p)*j,sizeof(p));
            if(p.type < -128 || p.type>127)return CurveResult::BinaryLimitExceeded;}}
    *bytes_out=size;return CurveResult::Succeeded;
}
CurveResult EncodeCurveBaseline(const CurveSnapshot* s,std::uint8_t* output,std::size_t capacity,std::size_t* written) noexcept {
    if(!written)return CurveResult::InvalidArgument;*written=0;
    std::size_t size=0;const auto result=MeasureCurveBaselineBinary(s,&size);
    if(result!=CurveResult::Succeeded)return result;
    if(size>capacity)return CurveResult::CapacityExceeded;
    if(size && (!output || Overlap(output,size,s->storage,s->storage_capacity) ||
        Overlap(output,size,s->records,s->record_capacity*sizeof(CurveRecord))))return CurveResult::InvalidArgument;
    auto* out=output;
    for(std::size_t i=0;i<s->count;++i){const auto& r=s->records[i];
        *out++=0;Put16(out,r.name_bytes);std::memcpy(out,s->storage+r.name_offset,r.name_bytes);out+=r.name_bytes;
        Put16(out,r.requested_samples);for(auto bits:r.bounds_bits)Put32(out,bits);Put16(out,r.point_count);
        for(std::uint32_t j=0;j<r.point_count;++j){CurvePoint p={};std::memcpy(&p,s->storage+r.points_offset+sizeof(p)*j,sizeof(p));
            *out++=static_cast<std::uint8_t>(p.type);Put32(out,p.x_bits);Put32(out,p.y_bits);}
    }
    *written=size;return CurveResult::Succeeded;
}
}  // namespace dluma::fixes::custom_map_asset_isolation::curves
