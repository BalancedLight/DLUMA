#define _HAS_EXCEPTIONS 0
#include "ItemAuxiliarySnapshot.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::item_auxiliary {
namespace {
constexpr std::uint32_t kMaximumElements=65536,kMaximumText=65535;
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct TextHeader {std::uintptr_t data;std::uint32_t count,capacity;};
struct Links {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
enum class TreeKind:std::uint8_t {Effect,Pickup,Collision,Small,StringValue,PartColor};
struct Reader {const Access& access;Snapshot& snapshot;Result result=Result::Succeeded;};

bool Fail(Reader& r,Result result=Result::InvalidGraph) noexcept {r.result=result;return false;}
bool Read(Reader& r,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && r.access.native.read(r.access.native.user,p,out,n)
        ?true:Fail(r,Result::ReadFailed);
}
bool Add(Reader& r,std::uintptr_t p,std::size_t n,std::size_t allocation=0) noexcept {
    if(!allocation)allocation=n;
    if(!allocation)return true;
    if(!p || p>UINTPTR_MAX-allocation || n>allocation)return Fail(r);
    auto& s=r.snapshot;
    if(s.span_count>=s.span_capacity || n>s.capacity-s.used || allocation>UINT32_MAX)
        return Fail(r,Result::CapacityExceeded);
    for(std::size_t i=0;i<s.span_count;++i){const auto& prior=s.spans[i];
        if(p<prior.address+prior.allocation_size && prior.address<p+allocation)return Fail(r);}
    if(n && !Read(r,p,s.bytes+s.used,n))return false;
    s.spans[s.span_count++]={p,static_cast<std::uint32_t>(n),static_cast<std::uint32_t>(allocation),static_cast<std::uint32_t>(s.used)};
    s.used+=n;return true;
}
bool Text(Reader& r,std::uintptr_t p) noexcept {
    TextHeader h={};if(!Read(r,p,&h,sizeof(h)))return false;
    if(h.count>h.capacity || h.capacity>kMaximumText || (h.capacity && !h.data))return Fail(r);
    if(!h.data)return !h.count || Fail(r);
    const auto offset=r.snapshot.used;
    if(!Add(r,h.data,static_cast<std::size_t>(h.count)+1,static_cast<std::size_t>(h.capacity)+1))return false;
    return !r.snapshot.bytes[offset+h.count] && !std::memchr(r.snapshot.bytes+offset,0,h.count) || Fail(r);
}
bool CString(Reader& r,std::uintptr_t p) noexcept {
    if(!p)return true;
    // Read exactly through the terminator; a bounded chunk read could cross
    // the allocation boundary for these native new[] strings.
    std::uint8_t c=0;
    for(std::size_t n=0;n<=kMaximumText;++n){if(!Read(r,p+n,&c,1))return false;
        if(!c)return Add(r,p,n+1);}
    return Fail(r);
}
bool Vector(Reader& r,std::uintptr_t p,std::size_t stride,Header& out) noexcept {
    if(!Read(r,p,&out,sizeof(out)))return false;
    if(out.count>out.capacity || out.capacity>kMaximumElements || (out.capacity && !out.data))return Fail(r);
    return Add(r,out.data,static_cast<std::size_t>(out.count)*stride,static_cast<std::size_t>(out.capacity)*stride);
}
bool TextVector(Reader& r,std::uintptr_t p,std::size_t stride=16,std::size_t text_offset=0) noexcept {
    Header h={};if(!Vector(r,p,stride,h))return false;
    for(std::size_t i=0;i<h.count;++i)if(!Text(r,h.data+i*stride+text_offset))return false;
    return true;
}
bool EnumFields(Reader& r,std::uintptr_t p) noexcept {
    Header h={};if(!Vector(r,p,8,h))return false;
    for(std::size_t i=0;i<h.count;++i){std::uintptr_t field=0;
        if(!Read(r,h.data+8*i,&field,8) || !field || field>UINTPTR_MAX-144)return Fail(r);
        // The registration is borrowed process metadata. Only its native
        // CEnumInfo subobject and owned option buffers are read here.
        const auto info=field+112;
        Header rows={};if(!Add(r,info,32) || !Vector(r,info+8,24,rows))return false;
        ++r.snapshot.enum_fields;r.snapshot.enum_options+=rows.count;
        for(std::size_t j=0;j<rows.count;++j){std::uintptr_t name=0,description=0;
            if(!Read(r,rows.data+24*j+8,&name,8) || !Read(r,rows.data+24*j+16,&description,8) ||
                !CString(r,name) || !CString(r,description))return false;}
    }
    return true;
}
bool Tree(Reader& r,std::uintptr_t p,TreeKind kind) noexcept {
    Header map={};if(!Read(r,p,&map,sizeof(map)))return false;
    if(map.count>kMaximumElements || (!!map.count!=!!map.data))return Fail(r);
    const std::size_t prefix=kind==TreeKind::Effect?160:
        kind==TreeKind::Pickup || kind==TreeKind::Collision?40:
        kind==TreeKind::Small?8:kind==TreeKind::StringValue?32:16;
    struct Pending {std::uintptr_t node,parent;};Pending stack[128];std::size_t depth=0,seen=0;
    if(map.data)stack[depth++]={map.data,0};
    while(depth){const auto next=stack[--depth];Links links={};
        if(++seen>map.count || next.node<prefix)return Fail(r);
        if(!Read(r,next.node,&links,sizeof(links)) || links.parent!=next.parent)return Fail(r);
        if(!Add(r,next.node-prefix,prefix+sizeof(links)))return false;
        const auto value=next.node-prefix+8;
        if(kind==TreeKind::Effect){for(std::size_t offset=8;offset<=104;offset+=24)
                if(!TextVector(r,value+offset))return false;
            if(!TextVector(r,value+128,24))return false;
        }else if(kind==TreeKind::Pickup || kind==TreeKind::Collision){
            if(!Text(r,next.node-40) || !TextVector(r,next.node-24,
                kind==TreeKind::Pickup?16:40,kind==TreeKind::Pickup?0:24))return false;
        }else if(kind==TreeKind::StringValue){if(!Text(r,next.node-24))return false;
        }else if(kind==TreeKind::PartColor){std::uintptr_t owned=0;Header first={},second={};
            if(!Read(r,next.node-8,&owned,8) || !Add(r,owned,56) ||
                !Vector(r,owned+8,8,first) || !Vector(r,owned+32,8,second))return false;
            ++r.snapshot.part_color_records;
        }
        if(links.right){if(depth>=128)return Fail(r);stack[depth++]={links.right,next.node};}
        if(links.left){if(depth>=128)return Fail(r);stack[depth++]={links.left,next.node};}
    }
    return seen==map.count || Fail(r);
}
bool SameFence(const curves::CurveReadFence& a,const curves::CurveReadFence& b,bool revision) noexcept {
    return a.owner_generation && a.owner_generation==b.owner_generation &&
        (!revision || a.mutation_revision==b.mutation_revision) && a.authoritative_thread && b.authoritative_thread &&
        a.access_closed && b.access_closed && a.readers_drained && b.readers_drained;
}
bool Profile(const Access& a) noexcept {
    return a.native.module_base && a.native.module_base<=UINTPTR_MAX-0x2082000 && a.manager && a.manager<=UINTPTR_MAX-728 &&
        a.native.read && a.native.fence && !std::memcmp(a.native.module_sha256,curves::kPlayerGameDllSha256,65) &&
        !std::memcmp(a.engine_sha256,kPlayerEngineSha256,65);
}
}
Result Capture(const Access& access,Snapshot* out) noexcept {
    if(!out || !out->spans || !out->bytes || !out->span_capacity || out->span_capacity>kMaximumSpans ||
        !out->capacity || out->capacity>kMaximumBytes)return Result::InvalidArgument;
    out->complete=false;out->span_count=out->used=out->enum_fields=out->enum_options=out->part_color_records=0;
    if(!Profile(access))return Result::ProfileMismatch;
    if(!access.native.fence(access.native.user,&out->fence) || !SameFence(out->fence,out->fence,true))return Result::FenceRejected;
    out->manager=access.manager;Reader r={access,*out};const auto root=access.manager,base=access.native.module_base;
    std::uintptr_t vtable=0;if(!Read(r,root,&vtable,8))return r.result;
    if(vtable!=base+0x161E3D8)return Result::InvalidGraph;
    if(!Add(r,root,8) || !Add(r,root+8,72) || !Add(r,root+176,360) || !Add(r,root+608,120) ||
        !Add(r,base+0x1D03450,24) || !Add(r,base+0x1C9C390,24))return r.result;
    if(!Tree(r,root+8,TreeKind::Effect) || !Tree(r,root+32,TreeKind::Pickup) || !Tree(r,root+56,TreeKind::Collision))return r.result;
    Header rows={};if(!Vector(r,root+176,208,rows))return r.result;
    constexpr std::size_t text_offsets[]={0,24,40,56,72,88,128,152,168,184};
    for(std::size_t i=0;i<rows.count;++i)for(const auto offset:text_offsets)
        if(!Text(r,rows.data+208*i+offset))return r.result;
    for(std::size_t offset=200;offset<=248;offset+=24)if(!EnumFields(r,root+offset))return r.result;
    for(std::size_t offset=272;offset<=320;offset+=24)if(!TextVector(r,root+offset))return r.result;
    Header cuts={},floats={};if(!Vector(r,root+344,40,cuts) || !Vector(r,root+512,4,floats))return r.result;
    for(std::size_t offset=608;offset<=680;offset+=24)if(!Tree(r,root+offset,TreeKind::Small))return r.result;
    if(!Tree(r,root+704,TreeKind::StringValue) || !Tree(r,base+0x1C9C390,TreeKind::PartColor))return r.result;
    out->complete=true;auto result=Verify(access,*out);curves::CurveReadFence after={};
    if(result==Result::Succeeded && (!access.native.fence(access.native.user,&after) || !SameFence(out->fence,after,true)))result=Result::Changed;
    out->complete=result==Result::Succeeded;return result;
}
Result Verify(const Access& access,const Snapshot& snapshot) noexcept {
    if(!snapshot.complete || !snapshot.spans || !snapshot.bytes || !snapshot.span_count || snapshot.span_count>snapshot.span_capacity ||
        snapshot.span_capacity>kMaximumSpans || snapshot.used>snapshot.capacity || snapshot.capacity>kMaximumBytes)return Result::InvalidArgument;
    if(!Profile(access))return Result::ProfileMismatch;
    curves::CurveReadFence before={},after={};
    if(snapshot.manager!=access.manager || !access.native.fence(access.native.user,&before) || !SameFence(snapshot.fence,before,false))return Result::FenceRejected;
    std::uint8_t bytes[4096];
    for(std::size_t i=0;i<snapshot.span_count;++i){const auto& span=snapshot.spans[i];
        if(!span.address || span.size>span.allocation_size || span.address>UINTPTR_MAX-span.allocation_size ||
            span.offset>snapshot.used || span.size>snapshot.used-span.offset)return Result::InvalidArgument;
        for(std::size_t copied=0;copied<span.size;){const auto left=span.size-copied;const auto n=left<sizeof(bytes)?left:sizeof(bytes);
            if(!access.native.read(access.native.user,span.address+copied,bytes,n))return Result::ReadFailed;
            if(std::memcmp(bytes,snapshot.bytes+span.offset+copied,n))return Result::Changed;copied+=n;}
    }
    return access.native.fence(access.native.user,&after) && SameFence(before,after,true)?Result::Succeeded:Result::FenceRejected;
}
bool ContainsBorrow(const Snapshot& s,BorrowKind kind,std::uintptr_t value) noexcept {
    if(!s.complete || !s.manager || !value || !s.spans || !s.bytes || s.span_count>s.span_capacity || s.used>s.capacity)return false;
    const auto read=[&s](std::uintptr_t p,void* out,std::size_t n) noexcept {
        for(std::size_t i=0;i<s.span_count;++i){const auto& span=s.spans[i];
            if(p<span.address || n>span.size || p-span.address>span.size-n || span.offset>s.used || span.size>s.used-span.offset)continue;
            std::memcpy(out,s.bytes+span.offset+p-span.address,n);return true;}
        return false;
    };
    Header map={};const auto offset=kind==BorrowKind::Effect?8U:kind==BorrowKind::PickupSounds?32U:0U;
    if(!offset || !read(s.manager+offset,&map,sizeof(map)) || map.count>kMaximumElements || (!!map.count!=!!map.data))return false;
    const auto prefix=kind==BorrowKind::Effect?152U:24U;
    std::uintptr_t stack[128];std::size_t depth=0,seen=0;if(map.data)stack[depth++]=map.data;
    while(depth){const auto node=stack[--depth];Links links={};if(++seen>map.count || node<prefix || !read(node,&links,sizeof(links)))return false;
        if(node-prefix==value)return true;
        if(links.right){if(depth>=128)return false;stack[depth++]=links.right;}
        if(links.left){if(depth>=128)return false;stack[depth++]=links.left;}
    }
    return false;
}
} // namespace dluma::fixes::custom_map_asset_isolation::item_auxiliary
