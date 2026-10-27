#define _HAS_EXCEPTIONS 0
#include "ItemAggregateRebuild.h"
#include "CurveBaselineSnapshot.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::item_aggregate {
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct Node {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
bool Frozen(const crafting_snapshot::Snapshot& s,std::uintptr_t address,void* out,std::size_t n) noexcept {
    for(std::size_t i=0;i<s.span_count;++i){const auto& span=s.spans[i];
        if(address<span.address || n>span.size || address-span.address>span.size-n)continue;
        if(span.offset>s.used || span.size>s.used-span.offset)return false;
        std::memcpy(out,s.bytes+span.offset+(address-span.address),n);return true;}
    return false;
}
bool Valid(const Baseline& b) noexcept {
    return b.complete && b.fence.initial_pristine && b.fence.manager && b.fence.owner_generation &&
        b.fence.baseline_generation && b.level_count<=kMaximumLevels;
}
bool Read(const curves::NativeCurveAccess& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Same(const curves::CurveReadFence& a,const curves::CurveReadFence& b) noexcept {
    return a.owner_generation && a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision &&
        a.authoritative_thread && b.authoritative_thread && a.access_closed && b.access_closed && a.readers_drained && b.readers_drained;
}
void Put(std::uint8_t* out,std::size_t offset,std::uint32_t value) noexcept {std::memcpy(out+offset,&value,4);}
bool Less(const Baseline& b,const std::int32_t* levels,unsigned left,unsigned right) noexcept {
    const bool l=b.effects[left].present && (b.effects[left].flags&16),r=b.effects[right].present && (b.effects[right].flags&16);
    return l!=r?l:levels[left]!=levels[right]?levels[left]>levels[right]:left<right;
}
void SelectElemental(const Baseline& b,unsigned type,std::uint32_t requested,Derived& d) noexcept {
    if(!requested || !b.elementals[type].present)return;
    const Level* selected=nullptr;
    for(std::size_t i=0;i<b.level_count;++i){const auto& l=b.levels[i];
        if(l.type==type && l.key>=1 && l.key<=requested && (!selected || l.key>selected->key))selected=&l;}
    if(!selected)return;
    d.elemental_levels[type]=selected->identity;
    d.masks[0]|=b.elementals[type].masks[0];d.masks[1]|=b.elementals[type].masks[1];
}
}
bool CaptureFrozenBaseline(const crafting_snapshot::Snapshot& s,Baseline* out) noexcept {
    if(!out || !s.complete || !s.fence.initial_pristine || !s.spans || !s.bytes ||
        s.span_count>s.span_capacity || s.used>s.capacity)return false;
    std::memset(out,0,sizeof(*out));out->fence=s.fence;
    struct Pending {std::uintptr_t node,parent;};Pending stack[128];
    for(unsigned map_index=0;map_index<2;++map_index){Header map={};
        if(!Frozen(s,s.fence.manager+24*map_index,&map,sizeof(map)) || map.count>65536 || (!!map.count!=!!map.data))return false;
        std::size_t depth=0,seen=0;if(map.data)stack[depth++]={map.data,0};
        while(depth){const auto next=stack[--depth];Node node={};std::int32_t type=0;const auto prefix=map_index?296U:200U;
            if(++seen>map.count || next.node<prefix || !Frozen(s,next.node,&node,sizeof(node)) || node.parent!=next.parent ||
                !Frozen(s,next.node-prefix,&type,4))return false;
            const auto object=next.node-prefix+8;
            if(!map_index && type>=0 && type<static_cast<std::int32_t>(kEffectTypes)){
                auto& e=out->effects[type];std::uint8_t active=0;Header levels={};
                if(e.present || !Frozen(s,object+72,&active,1) || active>1 || !Frozen(s,object+84,&e.flags,4) ||
                    !Frozen(s,object+88,&e.opposite,4) || !Frozen(s,object+176,e.masks,16) || !Frozen(s,object+96,&levels,sizeof(levels)) ||
                    levels.count>levels.capacity || levels.count>65536)return false;
                e.present=true;e.active=active!=0;e.level_count=levels.count;
                for(std::size_t i=0;i<levels.count && i<8;++i)if(!Frozen(s,levels.data+56*i,e.levels[i],56))return false;
            }else if(map_index && type>=0 && type<static_cast<std::int32_t>(kElementalTypes)){
                auto& e=out->elementals[type];Header levels={};
                if(e.present || !Frozen(s,object+88,e.masks,16) || !Frozen(s,object+104,&levels,sizeof(levels)) ||
                    levels.count>65536 || (!!levels.count!=!!levels.data))return false;
                e.present=true;Pending children[128];std::size_t child_depth=0,child_seen=0;
                if(levels.data)children[child_depth++]={levels.data,0};
                while(child_depth){const auto p=children[--child_depth];Node links={};Level level={};
                    level.type=static_cast<unsigned>(type);level.identity=p.node-160;
                    if(++child_seen>levels.count || p.node<168 || out->level_count>=kMaximumLevels ||
                        !Frozen(s,p.node,&links,sizeof(links)) || links.parent!=p.parent || !Frozen(s,p.node-168,&level.key,4) ||
                        !Frozen(s,level.identity+112,&level.reported_level,4))return false;
                    for(std::size_t i=0;i<out->level_count;++i)if(out->levels[i].identity==level.identity ||
                        (out->levels[i].type==level.type && out->levels[i].key==level.key))return false;
                    out->levels[out->level_count++]=level;
                    if(links.right){if(child_depth>=128)return false;children[child_depth++]={links.right,p.node};}
                    if(links.left){if(child_depth>=128)return false;children[child_depth++]={links.left,p.node};}
                }
                if(child_seen!=levels.count)return false;
            }
            if(node.right){if(depth>=128)return false;stack[depth++]={node.right,next.node};}
            if(node.left){if(depth>=128)return false;stack[depth++]={node.left,next.node};}
        }
        if(seen!=map.count)return false;
    }
    out->complete=true;return Valid(*out);
}
Result CaptureSource(const curves::NativeCurveAccess& a,const Baseline& baseline,std::uintptr_t object,Source* out) noexcept {
    if(!out || !object || object>UINTPTR_MAX-656 || !a.module_base || a.module_base>UINTPTR_MAX-0x2082000 ||
        !a.read || !a.fence || std::memcmp(a.module_sha256,curves::kPlayerGameDllSha256,65))return Result::InvalidArgument;
    std::memset(out,0,sizeof(*out));if(!Valid(baseline))return Result::InvalidBaseline;
    curves::CurveReadFence before={},after={};if(!a.fence(a.user,&before) || !Same(before,before))return Result::InvalidSource;
    std::uintptr_t vt=0;if(!Read(a,object,&vt,8))return Result::InvalidSource;
    const bool plan=vt==a.module_base+0x162D4C8;
    if(!plan && vt!=a.module_base+0x162B6C8 && vt!=a.module_base+0x162C508)return Result::InvalidSource;
    struct Route {unsigned slot;std::uintptr_t part,plan;};
    constexpr Route routes[]={{0,0x6B0100,0x6B2080},{128,0x6B0B30,0x6B1070},
        {3352,0x6B09B0,0x1481A0},{3360,0x6B0990,0x2390},{3368,0x6B0970,0x1481A0}};
    for(const auto& route:routes){std::uintptr_t actual=0;if(!Read(a,vt+route.slot,&actual,8) || actual!=a.module_base+(plan?route.plan:route.part))return Result::InvalidSource;}
    std::uintptr_t pointers[kElementalTypes]={},again[kElementalTypes]={};const auto offset=plan?432U:392U;
    if(object>UINTPTR_MAX-offset-sizeof(pointers) || !Read(a,object+offset,pointers,sizeof(pointers)))return Result::InvalidSource;
    for(unsigned type=0;type<kElementalTypes;++type){if(!pointers[type])continue;const Level* found=nullptr;
        for(std::size_t i=0;i<baseline.level_count;++i)if(baseline.levels[i].identity==pointers[type] && baseline.levels[i].type==type){found=baseline.levels+i;break;}
        std::uint32_t actual=0;if(!found || !Read(a,pointers[type]+112,&actual,4) || actual!=found->reported_level)return Result::InvalidSource;
        out->elemental_levels[type]=actual;
    }
    if(!plan){Header h={},check={};if(!Read(a,object+560,&h,sizeof(h)) || h.count>h.capacity || h.count>kMaximumSourceEffects || h.capacity>65536 ||
            (h.count && (!h.data || h.data>UINTPTR_MAX-8ULL*h.count)))return Result::InvalidSource;
        if(h.count && !Read(a,h.data,out->effects,8ULL*h.count))return Result::InvalidSource;out->effect_count=h.count;
        Pair repeated[kMaximumSourceEffects];if((h.count && (!Read(a,h.data,repeated,8ULL*h.count) || std::memcmp(repeated,out->effects,8ULL*h.count))) ||
            !Read(a,object+560,&check,sizeof(check)) || std::memcmp(&h,&check,sizeof(h)))return Result::InvalidSource;
    }
    if(!Read(a,object+offset,again,sizeof(again)) || std::memcmp(pointers,again,sizeof(pointers)) || !a.fence(a.user,&after) || !Same(before,after))return Result::InvalidSource;
    return Result::Succeeded;
}
Result Build(const Baseline& b,const Source* sources,std::size_t count,const Source* upgrade,const Accumulator& a,Derived* out) noexcept {
    if(!out || (count && !sources) || count>kMaximumSources || !a.apply || !a.gamedll_base ||
        std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65))return Result::InvalidArgument;
    if(!Valid(b))return Result::InvalidBaseline;
    Derived d={};Put(d.parameters,0,UINT32_MAX);for(unsigned offset=20;offset<=44;offset+=4)Put(d.parameters,offset,0x3f800000);
    std::int32_t effects[kEffectTypes]={};
    for(unsigned type=0;type<kElementalTypes;++type){std::uint64_t sum=0;
        for(std::size_t i=0;i<count;++i){sum+=sources[i].elemental_levels[type];if(sum>INT32_MAX)return Result::Overflow;}
        SelectElemental(b,type,static_cast<std::uint32_t>(sum),d);
    }
    for(std::size_t i=0;i<count;++i){const auto& s=sources[i];if(s.effect_count>kMaximumSourceEffects)return Result::InvalidSource;
        for(std::size_t j=0;j<s.effect_count;++j){const auto pair=s.effects[j];if(pair.type<0 || pair.type>=static_cast<std::int32_t>(kEffectTypes))continue;
            const auto& effect=b.effects[pair.type];if(effect.present && (effect.flags&16) && j>=4)continue;
            const auto sum=static_cast<std::int64_t>(effects[pair.type])+pair.level;
            if(sum<INT32_MIN || sum>INT32_MAX)return Result::Overflow;effects[pair.type]=static_cast<std::int32_t>(sum);}
    }
    for(unsigned i=0;i<kEffectTypes;++i){const auto& e=b.effects[i];
        if(!e.present || !(e.flags&16) || effects[i]<1 || e.opposite<0 || e.opposite>=static_cast<std::int32_t>(kEffectTypes) || effects[e.opposite]<1)continue;
        const auto cancel=effects[i]<effects[e.opposite]?effects[i]:effects[e.opposite];effects[i]-=cancel;effects[e.opposite]-=cancel;
    }
    unsigned order[kEffectTypes]={},used=0;
    for(unsigned type=0;type<kEffectTypes;++type)if(effects[type]>0){unsigned pos=used++;
        while(pos && Less(b,effects,type,order[pos-1])){order[pos]=order[pos-1];--pos;}order[pos]=type;}
    for(unsigned i=0;i<used;++i){const auto type=order[i];const auto& e=b.effects[type];if(!e.present || !e.active || !(e.flags&2) || !e.level_count)continue;
        const auto requested=static_cast<unsigned>(effects[type]);const auto maximum=e.level_count<8?e.level_count:8;
        const auto level=requested<maximum?requested:maximum;if(!level)continue;
        const float weight=static_cast<float>(requested)/static_cast<float>(level);unsigned change=0;
        if(!a.apply(a.user,d.parameters,type,e.levels[level-1],weight,&change) || change>2)return Result::NativeFailed;
        d.effects[d.effect_count++]={static_cast<std::int32_t>(type),effects[type]};
        if(change){if(e.opposite>=0 && e.opposite<static_cast<std::int32_t>(kEffectTypes) && b.effects[e.opposite].present){
                d.masks[2]&=~b.effects[e.opposite].masks[0];d.masks[3]&=~b.effects[e.opposite].masks[1];}
            if(change==2){d.masks[2]|=e.masks[0];d.masks[3]|=e.masks[1];}}
    }
    if(upgrade)for(unsigned type=0;type<kElementalTypes;++type)SelectElemental(b,type,upgrade->elemental_levels[type],d);
    *out=d;return Result::Succeeded;
}
bool NativeApply(void* user,std::uint8_t* target,unsigned type,const std::uint8_t* level,float weight,unsigned* change) noexcept {
    const auto* a=static_cast<const Accumulator*>(user);
    if(!a || !target || !level || !change || !a->gamedll_base || a->gamedll_base>UINTPTR_MAX-0x2082000 ||
        type>=kEffectTypes || !(weight>0.0f) || weight>static_cast<float>(INT32_MAX) || std::memcmp(a->sha256,curves::kPlayerGameDllSha256,65))return false;
    constexpr std::uint8_t prefix[]={0x48,0x83,0xec,0x58,0x45,0x33,0xc9,0x44,0x0f,0x29,0x4c,0x24,0x10,0x83,0xfa,0x2c};
    __try {const auto function=a->gamedll_base+0x2EFE10;if(std::memcmp(reinterpret_cast<const void*>(function),prefix,sizeof(prefix)))return false;
        *change=reinterpret_cast<unsigned(__fastcall*)(void*,unsigned,const void*,float)>(function)(target,type,level,weight);return *change<=2;}
    __except(1){return false;}
}
} // namespace dluma::fixes::custom_map_asset_isolation::item_aggregate
