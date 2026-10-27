#define _HAS_EXCEPTIONS 0
#include "CraftingManagerSnapshot.h"
#include "CurveBaselineSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::crafting_snapshot {
namespace {
using Access=blueprint_rebuild::Access;using Fence=blueprint_rebuild::Fence;
constexpr std::uint32_t max_elements=65536;
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct Links {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
struct String {std::uintptr_t data;std::uint32_t count,capacity;};
enum class Kind:std::uint8_t {Effect,Elemental,ElementalLevel,Interaction,Set};
struct Reader {const Access& access;Snapshot& snapshot;Result result=Result::Succeeded;std::size_t nodes=0;};
bool Read(Reader& r,std::uintptr_t p,void* out,std::size_t n) noexcept {
    if(!p || p>UINTPTR_MAX-n || !r.access.read(r.access.user,p,out,n)){r.result=Result::ReadFailed;return false;}return true;
}
bool Invalid(Reader& r) noexcept {r.result=Result::InvalidGraph;return false;}
bool Add(Reader& r,std::uintptr_t address,std::size_t n) noexcept {
    if(!n)return true;auto& s=r.snapshot;
    if(!address || address>UINTPTR_MAX-n)return Invalid(r);
    if(s.span_count>=s.span_capacity || s.span_count>=kMaximumSpans || n>s.capacity-s.used || n>UINT32_MAX){r.result=Result::CapacityExceeded;return false;}
    // Every traversed buffer is owned, not an interior or shared pointer into
    // another object's allocation. Aliases would also invalidate native free.
    for(std::size_t i=0;i<s.span_count;++i){const auto& p=s.spans[i];if(address<p.address+p.size && p.address<address+n)return Invalid(r);}
    if(!Read(r,address,s.bytes+s.used,n))return false;
    s.spans[s.span_count++]={address,static_cast<std::uint32_t>(n),static_cast<std::uint32_t>(s.used)};s.used+=n;return true;
}
bool Text(Reader& r,std::uintptr_t address) noexcept {
    String s={};if(!Read(r,address,&s,sizeof(s)))return false;
    if(s.count>s.capacity || s.capacity>1024U*1024U || (s.capacity && !s.data))return Invalid(r);
    if(!s.data)return !s.count || Invalid(r);
    if(!Add(r,s.data,static_cast<std::size_t>(s.count)+1))return false;
    return !r.snapshot.bytes[r.snapshot.used-1] || Invalid(r);
}
bool Vector(Reader& r,std::uintptr_t address,std::size_t stride,Header* out) noexcept {
    if(!Read(r,address,out,sizeof(*out)))return false;
    if(out->count>out->capacity || out->capacity>max_elements || (out->capacity && !out->data))return Invalid(r);
    if(out->count && !Add(r,out->data,static_cast<std::size_t>(out->count)*stride))return false;return true;
}
bool Vtable(Reader& r,std::uintptr_t object,std::uintptr_t expected) noexcept {
    std::uintptr_t vt=0;return Read(r,object,&vt,8) && (vt==r.access.gamedll_base+expected || Invalid(r));
}
bool Tree(Reader&,std::uintptr_t,Kind) noexcept;
bool EffectLevel(Reader& r,std::uintptr_t object) noexcept {
    return Vtable(r,object,0x1561BF0) && Text(r,object+8) && Text(r,object+24);
}
bool ElementalLevel(Reader& r,std::uintptr_t object) noexcept {
    return Vtable(r,object,0x1561E28) && Text(r,object+8) && Text(r,object+24) && Tree(r,object+136,Kind::Interaction);
}
bool Definition(Reader& r,std::uintptr_t object,bool elemental) noexcept {
    if(!Vtable(r,object,elemental?0x1561E40:0x1561C08) || !Text(r,object+8) || !Text(r,object+24) || !Text(r,object+40))return false;
    if(elemental)return Tree(r,object+104,Kind::ElementalLevel) && ElementalLevel(r,object+128);
    Header levels={};if(!Vector(r,object+96,56,&levels) || !EffectLevel(r,object+120))return false;
    for(std::size_t i=0;i<levels.count;++i)if(!EffectLevel(r,levels.data+56*i))return false;return true;
}
bool Tree(Reader& r,std::uintptr_t address,Kind kind) noexcept {
    Header map={};if(!Read(r,address,&map,sizeof(map)))return false;
    if(map.count>max_elements || (!!map.count!=!!map.data))return Invalid(r);
    struct Pending {std::uintptr_t node,parent;std::int64_t low,high;};Pending stack[128];std::size_t depth=0,seen=0;
    if(map.data)stack[depth++]={map.data,0,INT64_MIN,INT64_MAX};
    const std::size_t prefix=kind==Kind::Effect?200:kind==Kind::Elemental?296:kind==Kind::ElementalLevel?168:kind==Kind::Interaction?32:56;
    while(depth){const auto next=stack[--depth];Links links={};std::int32_t key=0;
        if(++seen>map.count || ++r.nodes>max_elements || next.node<prefix || !Read(r,next.node,&links,sizeof(links)))return Invalid(r);
        if(links.parent!=next.parent)return Invalid(r);if(!Add(r,next.node-prefix,prefix+sizeof(links)))return false;
        if(kind!=Kind::Set){if(!Read(r,next.node-prefix,&key,4))return false;
            if(key<=next.low || key>=next.high)return Invalid(r);}
        const auto object=next.node-prefix+(kind==Kind::Set?16:8);
        if(kind==Kind::Effect || kind==Kind::Elemental){if(!Definition(r,object,kind==Kind::Elemental))return false;}
        else if(kind==Kind::ElementalLevel){if(!ElementalLevel(r,object))return false;}
        else if(kind==Kind::Interaction){Header interactions={};if(!Vector(r,object,40,&interactions))return false;
            for(std::size_t i=0;i<interactions.count;++i){Header modifiers={};if(!Vector(r,interactions.data+40*i+8,12,&modifiers))return false;}}
        else {Header effects={};if(!Text(r,next.node-prefix) || !Text(r,object) || !Vector(r,object+16,8,&effects))return false;}
        if(links.right){if(depth>=128)return Invalid(r);stack[depth++]={links.right,next.node,kind==Kind::Set?INT64_MIN:key,next.high};}
        if(links.left){if(depth>=128)return Invalid(r);stack[depth++]={links.left,next.node,next.low,kind==Kind::Set?INT64_MAX:key};}
    }
    return seen==map.count || Invalid(r);
}
bool SameOwner(const Fence& a,const Fence& b) noexcept {
    return a.game_di==b.game_di && a.manager==b.manager && a.owner_generation==b.owner_generation &&
        a.baseline_generation==b.baseline_generation && a.writers_closed && b.writers_closed;
}
bool ValidAccess(const Access& a) noexcept {return a.gamedll_base && a.gamedll_base<=UINTPTR_MAX-0x2082000 && a.read && a.fence &&
    !std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65);}
bool ValidFence(const Fence& f) noexcept {return f.game_di && f.manager && f.owner_generation && f.baseline_generation && f.thread_id && f.writers_closed;}
}
Result Capture(const Access& access,Snapshot* out) noexcept {
    if(!out || !out->spans || !out->bytes || !out->span_capacity || out->span_capacity>kMaximumSpans ||
        !out->capacity || out->capacity>kMaximumBytes)return Result::InvalidArgument;
    out->complete=false;out->span_count=out->used=0;if(!ValidAccess(access))return Result::ProfileMismatch;
    if(!access.fence(access.user,&out->fence) || !ValidFence(out->fence))return Result::FenceRejected;
    Reader reader={access,*out};const auto root=out->fence.manager;
    if(!Add(reader,root,96) || !Tree(reader,root,Kind::Effect) || !Tree(reader,root+24,Kind::Elemental) ||
        !Tree(reader,root+48,Kind::Elemental) || !Tree(reader,root+72,Kind::Set))return reader.result;
    out->complete=true;auto verified=Verify(access,*out);Fence after={};
    if(verified==Result::Succeeded && (!access.fence(access.user,&after) || !SameOwner(out->fence,after) ||
        out->fence.thread_id!=after.thread_id || out->fence.revision!=after.revision))verified=Result::FenceRejected;
    out->complete=verified==Result::Succeeded;return verified;
}
Result Verify(const Access& access,const Snapshot& snapshot) noexcept {
    if(!snapshot.complete || !snapshot.spans || !snapshot.bytes || !snapshot.span_count || snapshot.span_count>snapshot.span_capacity ||
        snapshot.used>snapshot.capacity || snapshot.capacity>kMaximumBytes)return Result::InvalidArgument;
    if(!ValidAccess(access))return Result::ProfileMismatch;Fence before={},after={};
    if(!access.fence(access.user,&before) || !ValidFence(before) || !SameOwner(snapshot.fence,before))return Result::FenceRejected;
    std::uint8_t bytes[4096];
    for(std::size_t i=0;i<snapshot.span_count;++i){const auto& span=snapshot.spans[i];
        if(!span.address || span.offset>snapshot.used || span.size>snapshot.used-span.offset || span.address>UINTPTR_MAX-span.size)return Result::InvalidArgument;
        for(std::size_t copied=0;copied<span.size;){const auto left=span.size-copied;const auto count=left<sizeof(bytes)?left:sizeof(bytes);
            if(!access.read(access.user,span.address+copied,bytes,count))return Result::ReadFailed;
            if(std::memcmp(bytes,snapshot.bytes+span.offset+copied,count))return Result::Changed;copied+=count;}
    }
    if(!access.fence(access.user,&after) || !SameOwner(before,after) || before.thread_id!=after.thread_id || before.revision!=after.revision)return Result::FenceRejected;
    return Result::Succeeded;
}
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_snapshot
