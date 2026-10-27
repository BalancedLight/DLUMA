#define _HAS_EXCEPTIONS 0
#include "BlueprintBaselineRebuild.h"
#include "CurveBaselineSnapshot.h"
#include "CraftingManagerSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::blueprint_rebuild {
namespace {
constexpr std::size_t kMaximumNodes=4096;
struct Map {std::uintptr_t root;std::uint32_t count,pad,group,pad2;};
struct Node {std::uintptr_t left,parent,right;};
bool Read(const Access& a,std::uintptr_t p,void* out,std::size_t n) noexcept {return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);}
bool Same(const Fence& a,const Fence& b) noexcept {
    return a.game_di==b.game_di && a.manager==b.manager && a.owner_generation==b.owner_generation && a.revision==b.revision &&
        a.baseline_generation==b.baseline_generation && a.thread_id==b.thread_id && a.writers_closed && b.writers_closed &&
        a.initial_pristine && b.initial_pristine;
}
bool ReadEffects(const Access& a,const Fence& fence,Effect* effects) noexcept {
    Map map={};if(!Read(a,fence.manager,&map,sizeof(map)) || map.count>kMaximumNodes || (!!map.root!=!!map.count))return false;
    struct Pending {std::uintptr_t node,parent;std::int64_t low,high;};
    Pending stack[128];std::size_t depth=0,seen=0;
    if(map.root)stack[depth++]={map.root,0,INT64_MIN,INT64_MAX};
    while(depth){const auto p=stack[--depth];Node node={};std::int32_t key=0;
        if(++seen>map.count || p.node<200 || !Read(a,p.node,&node,sizeof(node)) || node.parent!=p.parent ||
            !Read(a,p.node-200,&key,4) || key<=p.low || key>=p.high)return false;
        if(key>=0 && static_cast<std::size_t>(key)<kEffectTypes){auto& effect=effects[key];std::uint8_t active=0;Map levels={};
            if(effect.present || !Read(a,p.node-192+72,&active,1) || active>1 || !Read(a,p.node-192+84,&effect.flags,4) ||
                !Read(a,p.node-192+96,&levels,sizeof(levels)) || levels.count>65536 || levels.count>levels.pad ||
                (levels.count && !levels.root))return false;
            effect.present=true;effect.active=active!=0;effect.level_count=levels.count;
            const auto count=levels.count<kEffectLevels?levels.count:kEffectLevels;
            for(std::size_t i=0;i<count;++i)if(!Read(a,levels.root+kLevelBytes*i,effect.levels[i],kLevelBytes))return false;
        }
        if(node.right){if(depth>=128)return false;stack[depth++]={node.right,p.node,key,p.high};}
        if(node.left){if(depth>=128)return false;stack[depth++]={node.left,p.node,p.low,key};}
    }
    Map after={};return seen==map.count && Read(a,fence.manager,&after,sizeof(after)) && !std::memcmp(&map,&after,sizeof(map));
}
void Put(std::uint8_t* p,std::size_t offset,std::uint32_t value) noexcept {std::memcpy(p+offset,&value,4);}
bool Less(const Baseline& b,const std::uint32_t* levels,unsigned a,unsigned d) noexcept {
    const bool first=b.effects[a].present && (b.effects[a].flags&16),second=b.effects[d].present && (b.effects[d].flags&16);
    return first!=second?first:(levels[a]!=levels[d]?levels[a]>levels[d]:a<d);
}
}
bool CaptureBaseline(const Access& a,Baseline* out) noexcept {
    if(!out || !a.read || !a.fence || !a.gamedll_base || std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65))return false;
    std::memset(out,0,sizeof(*out));if(!a.fence(a.user,&out->fence) || !out->fence.game_di || !out->fence.manager || !out->fence.owner_generation ||
        !out->fence.baseline_generation || !out->fence.thread_id || !out->fence.writers_closed || !out->fence.initial_pristine)return false;
    std::uintptr_t game=0,manager=0;
    if(!Read(a,a.gamedll_base+0x1D07F88,&game,8) || game!=out->fence.game_di ||
        !Read(a,game+0xAA0,&manager,8) || manager!=out->fence.manager || !ReadEffects(a,out->fence,out->effects))return false;
    Effect again[kEffectTypes];std::memset(again,0,sizeof(again));Fence after={};
    if(!ReadEffects(a,out->fence,again) || std::memcmp(out->effects,again,sizeof(again)) || !a.fence(a.user,&after) || !Same(out->fence,after))return false;
    out->complete=true;return true;
}
namespace {
bool FrozenRead(void* user,std::uintptr_t address,void* out,std::size_t size) noexcept {
    const auto* snapshot=static_cast<const crafting_snapshot::Snapshot*>(user);
    for(std::size_t i=0;i<snapshot->span_count;++i){const auto& span=snapshot->spans[i];
        if(address<span.address || size>span.size || address-span.address>span.size-size)continue;
        if(span.offset>snapshot->used || span.size>snapshot->used-span.offset)return false;
        std::memcpy(out,snapshot->bytes+span.offset+(address-span.address),size);return true;
    }
    return false;
}
}
bool CaptureFrozenBaseline(const crafting_snapshot::Snapshot& snapshot,Baseline* out) noexcept {
    if(!out || !snapshot.complete || !snapshot.fence.initial_pristine || !snapshot.fence.baseline_generation ||
        !snapshot.spans || !snapshot.bytes || snapshot.span_count>snapshot.span_capacity || snapshot.used>snapshot.capacity)return false;
    std::memset(out,0,sizeof(*out));out->fence=snapshot.fence;
    Access access={};access.read=FrozenRead;access.user=const_cast<crafting_snapshot::Snapshot*>(&snapshot);
    if(!ReadEffects(access,out->fence,out->effects))return false;out->complete=true;return true;
}
Result BuildPayload(const Baseline& baseline,const Accumulator& a,const std::uint8_t* captured,std::uint8_t* output) noexcept {
    if(!captured || !output || captured==output || !a.invoke || !a.gamedll_base ||
        std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65))return Result::InvalidArgument;
    const auto source=reinterpret_cast<std::uintptr_t>(captured),destination=reinterpret_cast<std::uintptr_t>(output);
    if(source>UINTPTR_MAX-176 || destination>UINTPTR_MAX-176 || (source<destination+176 && destination<source+176))return Result::InvalidArgument;
    if(!baseline.complete || !baseline.fence.initial_pristine || !baseline.fence.baseline_generation)return Result::InvalidBaseline;
    std::uint64_t id=0;std::uintptr_t vt=0;std::memcpy(&id,captured+8,8);std::memcpy(&vt,captured,8);
    if(vt!=a.gamedll_base+stored_blueprints::kBlueprintVtableRva || !id)return Result::InvalidId;
    std::uint32_t levels[kEffectTypes]={};auto encoded=id;
    for(unsigned i=0;i<8;++i){const auto byte=static_cast<unsigned>(encoded&255);const auto type=(byte&31)-1U;
        if(type<kEffectTypes)levels[type]=(byte>>5)+1U;encoded>>=8;}
    unsigned order[kEffectTypes]={},count=0;
    for(unsigned i=0;i<kEffectTypes;++i)if(levels[i]){unsigned pos=count++;
        while(pos && Less(baseline,levels,i,order[pos-1])){order[pos]=order[pos-1];--pos;}order[pos]=i;}
    if(!count)return Result::InvalidId;
    std::uint8_t built[176];std::memcpy(built,captured,sizeof(built));
    // Match constructor/init writes exactly; preserve native padding and ID.
    for(unsigned i=0;i<kEffectTypes;++i)Put(built,16+4*i,levels[i]);
    Put(built,104,UINT32_MAX);for(unsigned offset=108;offset<124;offset+=4)Put(built,offset,0);
    for(unsigned offset=124;offset<=148;offset+=4)Put(built,offset,0x3f800000U);
    for(unsigned offset=152;offset<=168;offset+=4)Put(built,offset,0);
    std::uint32_t sum=0;for(auto level:levels)sum+=level;Put(built,168,sum);
    for(unsigned i=0;i<count;++i){const auto type=order[i];const auto& effect=baseline.effects[type];const auto level=levels[type];
        if(!effect.present || !effect.active || !(effect.flags&2) || level>effect.level_count)continue;
        if(!a.invoke(a.user,built+104,type,effect.levels[level-1],1.0f))return Result::NativeFailed;
    }
    std::memcpy(output,built,sizeof(built));return Result::Succeeded;
}
Result BuildOwnedPlan(stored_blueprints::Owner* owner,const Baseline& baseline,const Accumulator& a,
    Rebuild* entries,std::size_t capacity,std::size_t* count) noexcept {
    if(!owner || !owner->initialized || !entries || !count)return Result::InvalidArgument;*count=0;
    if(owner->copy_count>capacity)return Result::CapacityExceeded;
    for(std::size_t i=0;i<owner->copy_count;++i){auto& copy=owner->copies[i];if(!copy.published)continue;
        if(!stored_blueprints::ValidateOwnedBlueprint(owner,reinterpret_cast<std::uintptr_t>(copy.native))){*count=0;return Result::PayloadChanged;}
        auto& entry=entries[*count];entry.target=&copy;std::memcpy(entry.expected,copy.captured,176);
        const auto result=BuildPayload(baseline,a,entry.expected,entry.desired);if(result!=Result::Succeeded){*count=0;return result;}++*count;
    }
    return Result::Succeeded;
}
bool NativeAccumulate(void* user,std::uint8_t* target,std::uint32_t type,const std::uint8_t* level,float weight) noexcept {
    const auto* a=static_cast<const Accumulator*>(user);
    if(!a || !a->gamedll_base || std::memcmp(a->sha256,curves::kPlayerGameDllSha256,65) || !target || !level || type>=kEffectTypes || weight!=1.0f)return false;
    constexpr std::uint8_t prefix[]={0x48,0x83,0xec,0x58,0x45,0x33,0xc9,0x44,0x0f,0x29,0x4c,0x24,0x10,0x83,0xfa,0x2c};
    __try {const auto native=a->gamedll_base+0x2EFE10;
        if(std::memcmp(reinterpret_cast<const void*>(native),prefix,sizeof(prefix)))return false;
        reinterpret_cast<std::uintptr_t(__fastcall*)(void*,unsigned,const void*,float)>(native)(target,type,level,weight);return true;}
    __except(1){return false;}
}
} // namespace dluma::fixes::custom_map_asset_isolation::blueprint_rebuild
