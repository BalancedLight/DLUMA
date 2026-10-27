#define _HAS_EXCEPTIONS 0
#include "CurveProcessCaches.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
constexpr CurveProcessCacheDescriptor catalog[]={
    {0x1E0807C,0x1E08080,"item_rand_distribution"},
    {0x1E128E0,0x1E128E4,"item_level_distribution"},
    {0x1E12838,0x1E1283C,"item_repair_percent_value_cost"},
    {0x1E12830,0x1E12834,"item_salvage_percent_value_cost"},
    {0x1E07E9C,0x1E07EA0,"repairParts_amount_mult"},
    {0x1E12FA4,0x1E12FA8,"xp_fighter_damage"},
};
constexpr std::uintptr_t weights_rva=0x1E12890,sum_rva=0x1DF4FA4,valid_rva=0x1DF4FA0;
bool Read(const NativeCurveAccess* a,std::uintptr_t rva,void* out,std::size_t n) noexcept {
    return a->module_base<=UINTPTR_MAX-rva-n && a->read(a->user,a->module_base+rva,out,n);
}
bool ReadValues(const NativeCurveAccess* a,CurveProcessCacheSnapshot* s) noexcept {
    for(std::size_t i=0;i<kCurveProcessCacheCount;++i)if(!Read(a,catalog[i].id_rva,&s->entries[i].id,4) ||
        !Read(a,catalog[i].flag_rva,&s->entries[i].flag,4) || s->entries[i].flag>1)return false;
    return Read(a,weights_rva,s->weights,sizeof(s->weights)) && Read(a,sum_rva,&s->weight_sum,4) &&
        Read(a,valid_rva,&s->weights_valid,1) && s->weights_valid<=1;
}
bool SameValues(const CurveProcessCacheSnapshot* a,const CurveProcessCacheSnapshot* b) noexcept {
    return !std::memcmp(a->entries,b->entries,sizeof(a->entries)) && !std::memcmp(a->weights,b->weights,sizeof(a->weights)) &&
        a->weight_sum==b->weight_sum && a->weights_valid==b->weights_valid;
}
bool SameFence(const CurveReadFence& a,const CurveReadFence& b) noexcept {
    return a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision &&
        a.access_closed && b.access_closed && a.readers_drained && b.readers_drained && a.authoritative_thread && b.authoritative_thread;
}
void Add(CurveProcessCachePlan* p,std::uintptr_t address,std::uint32_t expected,std::uint32_t desired,std::uint8_t size=4) noexcept {
    if(expected!=desired)p->writes[p->count++]={address,expected,desired,size};
}
}
const CurveProcessCacheDescriptor* CurveProcessCacheCatalog(std::size_t* n) noexcept {if(n)*n=kCurveProcessCacheCount;return catalog;}
bool CaptureCurveProcessCaches(const NativeCurveAccess* a,const CurveSnapshot* curves,CurveProcessCacheSnapshot* out) noexcept {
    if(!a || !a->read || !a->fence || !out || std::memcmp(a->module_sha256,kPlayerGameDllSha256,65) ||
        ValidateCurveSnapshot(curves)!=CurveResult::Succeeded)return false;
    *out={};CurveProcessCacheSnapshot again={};
    if(!a->fence(a->user,&out->fence) || !SameFence(curves->fence,out->fence) || !ReadValues(a,out))return false;
    for(std::size_t i=0;i<kCurveProcessCacheCount;++i){const auto& entry=out->entries[i];
        if(!entry.flag)continue;if(entry.id==-1)continue;
        if(entry.id<0 || static_cast<std::size_t>(entry.id)>=curves->count)return false;
        const auto& r=curves->records[entry.id];const auto n=std::strlen(catalog[i].name);
        if(r.name_bytes!=n || std::memcmp(curves->storage+r.name_offset,catalog[i].name,n))return false;
    }
    if(out->weights_valid && (!out->entries[1].flag || out->entries[1].id<0))return false;
    if(!ReadValues(a,&again) || !SameValues(out,&again) || !a->fence(a->user,&again.fence) || !SameFence(out->fence,again.fence))return false;
    out->complete=true;return true;
}
bool SameCurveProcessCaches(const CurveProcessCacheSnapshot* a,const CurveProcessCacheSnapshot* b) noexcept {
    return a && b && a->complete && b->complete && SameValues(a,b);
}
bool PlanCurveProcessCacheRestore(std::uintptr_t base,const CurveProcessCacheSnapshot* baseline,
    const CurveProcessCacheSnapshot* current,CurveProcessCachePlan* out) noexcept {
    if(!base || base>UINTPTR_MAX-0x2082000 || !baseline || !current || !baseline->complete || !current->complete || !out)return false;
    *out={};if(SameValues(baseline,current))return true;
    // The entire six native callers are closed. Invalidate the derived memo
    // before changing its payload and publish its captured valid byte last.
    Add(out,base+valid_rva,current->weights_valid,0,1);
    for(std::size_t i=0;i<kCurveProcessCacheCount;++i)Add(out,base+catalog[i].id_rva,
        static_cast<std::uint32_t>(current->entries[i].id),static_cast<std::uint32_t>(baseline->entries[i].id));
    for(std::size_t i=0;i<kCurveWeightCount;++i)Add(out,base+weights_rva+4*i,current->weights[i],baseline->weights[i]);
    Add(out,base+sum_rva,current->weight_sum,baseline->weight_sum);
    for(std::size_t i=0;i<kCurveProcessCacheCount;++i)Add(out,base+catalog[i].flag_rva,current->entries[i].flag,baseline->entries[i].flag);
    Add(out,base+valid_rva,0,baseline->weights_valid,1);return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::curves
