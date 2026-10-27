#define _HAS_EXCEPTIONS 0
#include "ItemCurveBindings.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
struct Vector {std::uintptr_t data;std::uint32_t count,capacity,allocator,padding;};
bool Read(const NativeCurveAccess& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool SameFence(const CurveReadFence& a,const CurveReadFence& b) noexcept {
    return a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision &&
        a.authoritative_thread==b.authoritative_thread && a.access_closed==b.access_closed && a.readers_drained==b.readers_drained;
}
std::uint32_t Find(const CurveSnapshot& s,std::uintptr_t p,bool* found) noexcept {
    *found=true;if(!p)return kNullCurveId;
    for(std::size_t i=0;i<s.count;++i)if(s.records[i].native_identity==p)return static_cast<std::uint32_t>(i);
    *found=false;return kNullCurveId;
}
bool ValidIds(const ItemCurveBindingSnapshot& s,const CurveSnapshot& curves) noexcept {
    if(!s.complete || !s.owner_identity || !s.vector_identity ||
        s.fence.owner_generation!=curves.fence.owner_generation)return false;
    for(auto id:s.indexed_ids)if(id!=kNullCurveId && id>=curves.count)return false;
    for(auto id:s.scalar_ids)if(id!=kNullCurveId && id>=curves.count)return false;
    return true;
}
std::uintptr_t Pointer(const CurveSnapshot& s,std::uint32_t id) noexcept {
    return id==kNullCurveId?0:s.records[id].native_identity;
}
}  // namespace
CurveBindingResult CaptureItemCurveBindings(const NativeCurveAccess* a,const CurveSnapshot* curves,
    ItemCurveBindingSnapshot* out) noexcept {
    if(!a || !curves || !out || !a->read || !a->fence ||
        ValidateCurveSnapshot(curves)!=CurveResult::Succeeded)return CurveBindingResult::InvalidArgument;
    *out={};if(!a->module_base || a->module_base>UINTPTR_MAX-0x2000000U ||
        std::memcmp(a->module_sha256,kPlayerGameDllSha256,sizeof(kPlayerGameDllSha256)))return CurveBindingResult::ProfileMismatch;
    CurveReadFence before={},after={};
    if(!a->fence(a->user,&before) || !before.owner_generation)return CurveBindingResult::ReadFailed;
    if(!before.authoritative_thread || !before.access_closed || !before.readers_drained)return CurveBindingResult::AccessNotQuiescent;
    if(!SameFence(before,curves->fence))return CurveBindingResult::ChangedDuringCapture;
    const auto owner=a->module_base+kItemManagerRva;
    std::uintptr_t vtable=0,vtable_after=0;Vector vector={},vector_after={};
    std::uintptr_t indexed[6]={},scalars[5]={},indexed_after[6]={},scalars_after[5]={};
    if(!Read(*a,owner,&vtable,8) || !Read(*a,owner+0x218,&vector,sizeof(vector)))return CurveBindingResult::ReadFailed;
    if(vtable!=a->module_base+kItemManagerVtableRva)return CurveBindingResult::ProfileMismatch;
    if(vector.count!=6 || vector.capacity<6 || !vector.data)return CurveBindingResult::ShapeChanged;
    if(!Read(*a,vector.data,indexed,sizeof(indexed)) || !Read(*a,owner+0x230,scalars,sizeof(scalars)))return CurveBindingResult::ReadFailed;
    for(std::size_t i=0;i<6;++i){bool found=false;out->indexed_ids[i]=Find(*curves,indexed[i],&found);if(!found)return CurveBindingResult::UnknownCurveReference;}
    for(std::size_t i=0;i<5;++i){bool found=false;out->scalar_ids[i]=Find(*curves,scalars[i],&found);if(!found)return CurveBindingResult::UnknownCurveReference;}
    if(!Read(*a,owner,&vtable_after,8) || !Read(*a,owner+0x218,&vector_after,sizeof(vector_after)) ||
        !Read(*a,vector.data,indexed_after,sizeof(indexed_after)) || !Read(*a,owner+0x230,scalars_after,sizeof(scalars_after)) ||
        !a->fence(a->user,&after))return CurveBindingResult::ReadFailed;
    if(vtable!=vtable_after || std::memcmp(&vector,&vector_after,sizeof(vector)) ||
        std::memcmp(indexed,indexed_after,sizeof(indexed)) || std::memcmp(scalars,scalars_after,sizeof(scalars)) || !SameFence(before,after))
        return CurveBindingResult::ChangedDuringCapture;
    out->owner_identity=owner;out->vector_identity=vector.data;out->fence=after;out->complete=true;
    return CurveBindingResult::Succeeded;
}
CurveBindingResult PlanItemCurveBindingRestore(const CurveSnapshot* b,const ItemCurveBindingSnapshot* bb,
    const CurveSnapshot* c,const ItemCurveBindingSnapshot* cb,ItemCurveBindingRestorePlan* plan) noexcept {
    if(!b || !bb || !c || !cb || !plan)return CurveBindingResult::InvalidArgument;*plan={};
    const auto prefix=ClassifyCurvePrefix(b,c);
    if(prefix.kind!=CurvePrefixKind::Identical && prefix.kind!=CurvePrefixKind::AppendedTail)return CurveBindingResult::BaselineNotPreserved;
    if(!ValidIds(*bb,*b) || !ValidIds(*cb,*c) || bb->owner_identity!=cb->owner_identity ||
        !SameFence(cb->fence,c->fence))return CurveBindingResult::InvalidArgument;
    plan->owner_identity=cb->owner_identity;plan->vector_identity=cb->vector_identity;plan->fence=cb->fence;
    for(std::size_t i=0;i<6;++i){const auto expected=Pointer(*c,cb->indexed_ids[i]),desired=Pointer(*b,bb->indexed_ids[i]);
        if(expected!=desired)plan->writes[plan->count++]={cb->vector_identity+8*i,expected,desired};}
    for(std::size_t i=0;i<5;++i){const auto expected=Pointer(*c,cb->scalar_ids[i]),desired=Pointer(*b,bb->scalar_ids[i]);
        if(expected!=desired)plan->writes[plan->count++]={cb->owner_identity+0x230+8*i,expected,desired};}
    plan->prepared=true;return CurveBindingResult::Succeeded;
}
CurveBindingResult VerifyItemCurveBindings(const ItemCurveBindingSnapshot* b,const ItemCurveBindingSnapshot* c) noexcept {
    if(!b || !c || !b->complete || !c->complete)return CurveBindingResult::InvalidArgument;
    if(b->owner_identity!=c->owner_identity || b->fence.owner_generation!=c->fence.owner_generation ||
        std::memcmp(b->indexed_ids,c->indexed_ids,sizeof(b->indexed_ids)) || std::memcmp(b->scalar_ids,c->scalar_ids,sizeof(b->scalar_ids)))
        return CurveBindingResult::BaselineNotPreserved;
    return CurveBindingResult::Succeeded;
}
}  // namespace dluma::fixes::custom_map_asset_isolation::curves
