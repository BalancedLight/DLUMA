#define _HAS_EXCEPTIONS 0
#include "ItemInternPoolSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::item_pools {
namespace {
struct Map {std::uintptr_t root;std::uint32_t count,pad,group,pad2;};
struct List {std::uintptr_t head,tail;std::uint32_t count,pad;};
struct Text {std::uintptr_t data;std::uint32_t count,capacity;};
struct TreeNode {std::uintptr_t left,parent,right;std::uint32_t metadata,pad;};
bool Read(const curves::NativeCurveAccess& a,std::uintptr_t address,void* out,std::size_t n) noexcept {
    return address && address<=UINTPTR_MAX-n && a.read(a.user,address,out,n);
}
bool Fence(const curves::CurveReadFence& a,const curves::CurveReadFence& b) noexcept {
    return a.owner_generation && a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision &&
        a.authoritative_thread && b.authoritative_thread && a.access_closed && b.access_closed && a.readers_drained && b.readers_drained;
}
Result Strings(const curves::NativeCurveAccess& a,Snapshot* out,const Map& map) noexcept {
    if(map.count>out->string_capacity || map.count>kMaximumEntries)return Result::CapacityExceeded;
    if(!!map.count!=!!map.root)return Result::InvalidGraph;
    struct Pending {std::uintptr_t node,parent;std::uint64_t low,high;};Pending stack[128];std::size_t depth=0;
    if(map.root)stack[depth++]={map.root,0,0,UINT64_C(0x100000000)};
    while(depth){const auto p=stack[--depth];if(out->string_count>=map.count || p.node<16)return Result::InvalidGraph;
        auto& entry=out->strings[out->string_count];entry={};TreeNode node={};Text value={};
        if(!Read(a,p.node,&node,sizeof(node)) || !Read(a,p.node-16,&entry.hash,4) || !Read(a,p.node-8,&entry.value,8) ||
            !Read(a,entry.value,&value,sizeof(value)))return Result::ReadFailed;
        if(node.parent!=p.parent || entry.hash<p.low || entry.hash>=p.high || value.count>value.capacity || value.capacity>kMaximumStringBytes ||
            (value.count && !value.data))return Result::InvalidGraph;
        entry.node=p.node;entry.characters=value.data;entry.length=value.count;entry.capacity=value.capacity;
        entry.offset=static_cast<std::uint32_t>(out->used);std::memcpy(entry.links,&node,sizeof(node));
        for(std::size_t i=0;i<out->string_count;++i)if(out->strings[i].node==p.node || out->strings[i].value==entry.value)return Result::InvalidGraph;
        if(value.data){const auto size=static_cast<std::size_t>(value.count)+1;if(size>out->capacity-out->used)return Result::CapacityExceeded;
            if(!Read(a,value.data,out->bytes+out->used,size))return Result::ReadFailed;
            if(out->bytes[out->used+size-1] || std::memchr(out->bytes+out->used,0,size-1))return Result::InvalidGraph;out->used+=size;}
        ++out->string_count;
        if(node.right){if(depth>=128)return Result::InvalidGraph;stack[depth++]={node.right,p.node,static_cast<std::uint64_t>(entry.hash)+1,p.high};}
        if(node.left){if(depth>=128)return Result::InvalidGraph;stack[depth++]={node.left,p.node,p.low,entry.hash};}
    }
    return out->string_count==map.count?Result::Succeeded:Result::InvalidGraph;
}
Result Vectors(const curves::NativeCurveAccess& a,Snapshot* out,const List& list) noexcept {
    if(list.count>out->vector_capacity || list.count>kMaximumEntries)return Result::CapacityExceeded;
    if(!!list.count!=!!list.head || !!list.count!=!!list.tail)return Result::InvalidGraph;
    std::uintptr_t next=list.head,previous=0;
    while(next){if(next<16 || out->vector_count>=list.count)return Result::InvalidGraph;auto& entry=out->vectors[out->vector_count];entry={};entry.node=next;
        std::uint32_t value[4]={};
        if(!Read(a,next-16,value,sizeof(value)) || !Read(a,next,&entry.next,8) || !Read(a,next+8,&entry.previous,8))return Result::ReadFailed;
        std::memcpy(entry.values,value,sizeof(entry.values));entry.padding=value[3];
        if(entry.previous!=previous)return Result::InvalidGraph;previous=next;next=entry.next;++out->vector_count;
    }
    return out->vector_count==list.count && previous==list.tail?Result::Succeeded:Result::InvalidGraph;
}
bool Valid(const Snapshot& s) noexcept {
    if(!s.complete || !s.strings || !s.vectors || !s.bytes || s.string_count>s.string_capacity || s.vector_count>s.vector_capacity ||
        s.string_count>kMaximumEntries || s.vector_count>kMaximumEntries || s.used>s.capacity)return false;
    for(std::size_t i=0;i<s.string_count;++i){const auto& e=s.strings[i];if(e.offset>s.used || (e.characters && static_cast<std::size_t>(e.length)+1>s.used-e.offset))return false;}return true;
}
}
Result Capture(const curves::NativeCurveAccess& a,Snapshot* out) noexcept {
    if(!out || !out->strings || !out->vectors || !out->bytes || !out->string_capacity || !out->vector_capacity || !out->capacity ||
        out->capacity>kMaximumStringBytes || !a.read || !a.fence)return Result::InvalidArgument;
    out->complete=false;out->string_count=out->vector_count=out->used=0;
    if(!a.module_base || a.module_base>UINTPTR_MAX-0x2082000 || std::memcmp(a.module_sha256,curves::kPlayerGameDllSha256,65))return Result::ProfileMismatch;
    if(!a.fence(a.user,&out->fence) || !Fence(out->fence,out->fence))return Result::FenceRejected;
    Map map={},after_map={};List list={},after_list={};curves::CurveReadFence after={};
    if(!Read(a,a.module_base+kStringPoolRva,&map,sizeof(map)) || !Read(a,a.module_base+kVectorPoolRva,&list,sizeof(list)))return Result::ReadFailed;
    std::memcpy(out->string_header,&map,sizeof(map));std::memcpy(out->vector_header,&list,sizeof(list));
    auto result=Strings(a,out,map);if(result!=Result::Succeeded)return result;result=Vectors(a,out,list);if(result!=Result::Succeeded)return result;
    if(!Read(a,a.module_base+kStringPoolRva,&after_map,sizeof(after_map)) || !Read(a,a.module_base+kVectorPoolRva,&after_list,sizeof(after_list)) ||
        !a.fence(a.user,&after))return Result::ReadFailed;
    if(std::memcmp(&map,&after_map,sizeof(map)) || std::memcmp(&list,&after_list,sizeof(list)) || !Fence(out->fence,after))return Result::Changed;
    // Re-read payloads too: root/link stability alone does not prove that an
    // existing interned value remained immutable while it was copied.
    std::uint8_t bytes[256];
    for(std::size_t i=0;i<out->string_count;++i){const auto& e=out->strings[i];Text text={};TreeNode node={};std::uintptr_t value=0;std::uint32_t hash=0;
        if(!Read(a,e.node,&node,sizeof(node)) || !Read(a,e.node-16,&hash,4) || !Read(a,e.node-8,&value,8) || !Read(a,e.value,&text,sizeof(text)))return Result::ReadFailed;
        if(hash!=e.hash || value!=e.value || text.data!=e.characters || text.count!=e.length || text.capacity!=e.capacity || std::memcmp(&node,e.links,sizeof(node)))return Result::Changed;
        for(std::size_t done=0;e.characters && done<=e.length;){const auto left=static_cast<std::size_t>(e.length)+1-done;const auto count=left<sizeof(bytes)?left:sizeof(bytes);
            if(!Read(a,e.characters+done,bytes,count))return Result::ReadFailed;if(std::memcmp(bytes,out->bytes+e.offset+done,count))return Result::Changed;done+=count;}}
    for(std::size_t i=0;i<out->vector_count;++i){const auto& e=out->vectors[i];std::uint8_t node[32];
        if(!Read(a,e.node-16,node,sizeof(node)))return Result::ReadFailed;
        if(std::memcmp(node,e.values,sizeof(e.values)) || std::memcmp(node+12,&e.padding,4) ||
            std::memcmp(node+16,&e.next,8) || std::memcmp(node+24,&e.previous,8))return Result::Changed;}
    if(!a.fence(a.user,&after) || !Fence(out->fence,after))return Result::Changed;out->complete=true;return Result::Succeeded;
}
Difference CompareBaseline(const Snapshot& baseline,const Snapshot& current) noexcept {
    if(!Valid(baseline) || !Valid(current) || baseline.fence.owner_generation!=current.fence.owner_generation)return {Result::InvalidArgument,0,0};
    if(baseline.string_count>current.string_count || baseline.vector_count>current.vector_count)return {Result::Changed,0,0};
    for(std::size_t i=0;i<baseline.string_count;++i){const auto& b=baseline.strings[i];const StringEntry* match=nullptr;
        for(std::size_t j=0;j<current.string_count;++j)if(current.strings[j].hash==b.hash){match=current.strings+j;break;}
        if(!match || match->node!=b.node || match->value!=b.value || match->characters!=b.characters || match->length!=b.length || match->capacity!=b.capacity ||
            (b.characters && std::memcmp(baseline.bytes+b.offset,current.bytes+match->offset,static_cast<std::size_t>(b.length)+1)))return {Result::Changed,0,0};}
    for(std::size_t i=0;i<baseline.vector_count;++i)if(baseline.vectors[i].node!=current.vectors[i].node ||
        std::memcmp(baseline.vectors[i].values,current.vectors[i].values,12))return {Result::Changed,0,0};
    return {Result::Succeeded,current.string_count-baseline.string_count,current.vector_count-baseline.vector_count};
}
} // namespace dluma::fixes::custom_map_asset_isolation::item_pools
