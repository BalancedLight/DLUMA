#define _HAS_EXCEPTIONS 0
#include "ItemDescriptorSnapshot.h"
#include <cstring>
#include <initializer_list>
namespace dluma::fixes::custom_map_asset_isolation::item_descriptors {
namespace {
struct Header {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct String {std::uintptr_t data;std::uint32_t count,capacity;};
struct Node {std::uintptr_t left,parent,right;std::uint32_t color,pad;};
enum class Kind:std::uint8_t {Base,Inventory,Docket,Voucher,Ammo,Bullet,Bundle,Car,Collectable,CraftPart,CraftPlan,
    Firearm,Flashlight,Powerup,Battery,Stamina,Suppressant,Medkit,Melee,Shield,Throwable,Flare,Liquid,UseTrigger,Utility};
struct Type {std::uintptr_t vtable;std::uint32_t bytes;Kind kind;};
constexpr Type types[]={
    {0x1625148,128,Kind::Base},{0x1631A68,384,Kind::Inventory},{0x161E408,392,Kind::Docket},{0x161F238,384,Kind::Voucher},
    {0x1625F88,416,Kind::Ammo},{0x1627C98,208,Kind::Bullet},{0x1628AC8,408,Kind::Bundle},{0x16299B8,592,Kind::Car},
    {0x162A868,160,Kind::Collectable},{0x162B6C8,656,Kind::CraftPart},{0x162D4C8,584,Kind::CraftPlan},
    {0x162EB28,1872,Kind::Firearm},{0x162FBD8,528,Kind::Flashlight},{0x16346A8,600,Kind::Powerup},
    {0x1626DC8,608,Kind::Battery},{0x1636408,600,Kind::Stamina},{0x1630A18,600,Kind::Suppressant},{0x16328B8,608,Kind::Medkit},
    {0x16337E8,864,Kind::Melee},{0x1635598,936,Kind::Shield},{0x1637538,1128,Kind::Throwable},{0x1638418,1216,Kind::Flare},
    {0x1639608,1320,Kind::Liquid},{0x163A488,608,Kind::UseTrigger},{0x163B2D8,400,Kind::Utility}};
enum class MapKind:std::uint8_t {TextScalar,InternPair,Dlc,NameIndex};
struct Reader {const curves::NativeCurveAccess& access;const Inputs& inputs;Snapshot& out;Result result=Result::Succeeded;};
Record* Find(Snapshot& s,std::uintptr_t p) noexcept {
    if(!s.records || s.count>s.record_capacity)return nullptr;std::size_t first=0,last=s.count;
    while(first<last){const auto middle=first+(last-first)/2;if(s.records[middle].identity<p)first=middle+1;else last=middle;}
    return first<s.count && s.records[first].identity==p?s.records+first:nullptr;
}
bool Fail(Reader& r,Result code=Result::InvalidGraph) noexcept {r.result=code;return false;}
bool Read(Reader& r,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && r.access.read(r.access.user,p,out,n)?true:Fail(r,Result::ReadFailed);
}
bool Add(Reader& r,std::uintptr_t p,std::size_t n,std::size_t extent=0) noexcept {
    if(!extent)extent=n;if(!extent)return true;
    if(!p || p>UINTPTR_MAX-extent || n>extent)return Fail(r);
    auto& s=r.out;if(s.span_count>=s.span_capacity || n>s.capacity-s.used || extent>UINT32_MAX)return Fail(r,Result::CapacityExceeded);
    if(n && !Read(r,p,s.bytes+s.used,n))return false;
    s.spans[s.span_count++]={p,static_cast<std::uint32_t>(n),static_cast<std::uint32_t>(extent),static_cast<std::uint32_t>(s.used)};s.used+=n;return true;
}
template<class T,class Less>void Sort(T* values,std::size_t count,Less less) noexcept {
    const auto sift=[values,less](std::size_t root,std::size_t end) noexcept {
        while(root<end/2){auto child=root*2+1;if(child+1<end && less(values[child],values[child+1]))++child;
            if(!less(values[root],values[child]))break;const T temp=values[root];values[root]=values[child];values[child]=temp;root=child;}
    };
    for(std::size_t i=count/2;i;--i)sift(i-1,count);
    for(std::size_t end=count;end>1;){--end;const T temp=values[0];values[0]=values[end];values[end]=temp;sift(0,end);}
}
bool Text(Reader& r,std::uintptr_t at) noexcept {
    String s={};if(!Read(r,at,&s,sizeof(s)))return false;
    if(s.count>s.capacity || s.capacity>65535 || (s.capacity && !s.data))return Fail(r);
    if(!s.data)return !s.count || Fail(r);
    const auto offset=r.out.used;if(!Add(r,s.data,static_cast<std::size_t>(s.count)+1,static_cast<std::size_t>(s.capacity)+1))return false;
    return !r.out.bytes[offset+s.count] && !std::memchr(r.out.bytes+offset,0,s.count) || Fail(r);
}
bool Vector(Reader& r,std::uintptr_t at,std::size_t stride,Header& h) noexcept {
    if(!Read(r,at,&h,sizeof(h)))return false;
    if(h.count>h.capacity || h.capacity>65536 || (h.capacity && !h.data))return Fail(r);
    return Add(r,h.data,static_cast<std::size_t>(h.count)*stride,static_cast<std::size_t>(h.capacity)*stride);
}
bool Values(Reader& r,std::uintptr_t at,std::size_t stride) noexcept {Header h={};return Vector(r,at,stride,h);}
bool Texts(Reader& r,std::uintptr_t at,std::size_t stride=16,std::size_t offset=0) noexcept {
    Header h={};if(!Vector(r,at,stride,h))return false;
    for(std::size_t i=0;i<h.count;++i)if(!Text(r,h.data+i*stride+offset))return false;return true;
}
bool Intern(Reader& r,std::uintptr_t at,bool vector=false) noexcept {
    std::uintptr_t p=0;if(!Read(r,at,&p,8))return false;if(!p)return true;const auto& pool=*r.inputs.pools;
    if(vector){for(std::size_t i=0;i<pool.vector_count;++i)if(pool.vectors[i].node>=16 && pool.vectors[i].node-16==p)return true;}
    else for(std::size_t i=0;i<pool.string_count;++i)if(pool.strings[i].value==p)return true;
    return Fail(r,Result::ForeignBorrow);
}
bool Frozen(const crafting_snapshot::Snapshot& s,std::uintptr_t at,void* out,std::size_t n) noexcept {
    for(std::size_t i=0;i<s.span_count;++i){const auto& span=s.spans[i];
        if(at<span.address || n>span.size || at-span.address>span.size-n || span.offset>s.used || span.size>s.used-span.offset)continue;
        std::memcpy(out,s.bytes+span.offset+at-span.address,n);return true;}
    return false;
}
bool CraftingLevel(Reader& r,std::uintptr_t at,unsigned map_index,unsigned type=UINT32_MAX) noexcept {
    std::uintptr_t value=0;if(!Read(r,at,&value,8))return false;if(!value)return true;
    const auto& s=*r.inputs.crafting;Header map={};if(!Frozen(s,s.fence.manager+24*map_index,&map,sizeof(map)) || map.count>65536)return Fail(r,Result::ForeignBorrow);
    std::uintptr_t stack[128];std::size_t depth=0,seen=0;if(map.data)stack[depth++]=map.data;
    while(depth){const auto node=stack[--depth];Node links={};std::uint32_t key=0;
        if(++seen>map.count || node<296 || !Frozen(s,node,&links,sizeof(links)) || !Frozen(s,node-296,&key,4))return Fail(r,Result::ForeignBorrow);
        if(type==UINT32_MAX || key==type){Header levels={};if(!Frozen(s,node-288+104,&levels,sizeof(levels)) || levels.count>65536)return Fail(r,Result::ForeignBorrow);
            std::uintptr_t pending[128];std::size_t used=0,n=0;if(levels.data)pending[used++]=levels.data;
            while(used){const auto child=pending[--used];Node child_links={};if(++n>levels.count || child<168 || !Frozen(s,child,&child_links,sizeof(child_links)))return Fail(r,Result::ForeignBorrow);
                if(child-160==value)return true;
                if(child_links.right){if(used>=128)return Fail(r);pending[used++]=child_links.right;}
                if(child_links.left){if(used>=128)return Fail(r);pending[used++]=child_links.left;}}
        }
        if(links.right){if(depth>=128)return Fail(r);stack[depth++]=links.right;}
        if(links.left){if(depth>=128)return Fail(r);stack[depth++]=links.left;}
    }
    return Fail(r,Result::ForeignBorrow);
}
bool Primary(Reader& r,std::uintptr_t at) noexcept {std::uintptr_t p=0;
    return Read(r,at,&p,8) && (!p || Find(r.out,p) || Fail(r,Result::ForeignBorrow));}
bool Aux(Reader& r,std::uintptr_t at,item_auxiliary::BorrowKind kind) noexcept {std::uintptr_t p=0;
    return Read(r,at,&p,8) && (!p || item_auxiliary::ContainsBorrow(*r.inputs.auxiliary,kind,p) || Fail(r,Result::ForeignBorrow));}
bool Tree(Reader& r,std::uintptr_t at,MapKind kind) noexcept {
    Header h={};if(!Read(r,at,&h,sizeof(h)))return false;if(h.count>65536 || (!!h.count!=!!h.data))return Fail(r);
    const std::size_t prefix=kind==MapKind::Dlc?32:24;
    struct Pending {std::uintptr_t node,parent;};Pending stack[128];std::size_t depth=0,seen=0;
    if(h.data)stack[depth++]={h.data,0};
    while(depth){const auto p=stack[--depth];Node n={};if(++seen>h.count || p.node<prefix || !Read(r,p.node,&n,sizeof(n)))return Fail(r);
        if(n.parent!=p.parent || !Add(r,p.node-prefix,prefix+32))return Fail(r);
        if(kind==MapKind::TextScalar || kind==MapKind::NameIndex){if(!Text(r,p.node-24))return false;
            if(kind==MapKind::NameIndex && !Primary(r,p.node-8))return false;
        }else if(kind==MapKind::InternPair){if(!Intern(r,p.node-16) || !Intern(r,p.node-8))return false;
        }else {Header entries={};std::int32_t key=0;if(!Read(r,p.node-32,&key,4) || key< -2 || key>32765 || !Vector(r,p.node-24,8,entries))return Fail(r);
            for(std::size_t i=0;i<entries.count;++i){std::uintptr_t desc=0;std::uint32_t id=0;
                if(!Read(r,entries.data+8*i,&desc,8) || !Find(r.out,desc) || !Read(r,desc+88,&id,4) ||
                    id!=(static_cast<std::uint32_t>(key+2)<<16 | static_cast<std::uint32_t>(i)))return Fail(r,Result::ForeignBorrow);
                if(++Find(r.out,desc)->index_count!=1)return Fail(r);}
        }
        if(n.right){if(depth>=128)return Fail(r);stack[depth++]={n.right,p.node};}
        if(n.left){if(depth>=128)return Fail(r);stack[depth++]={n.left,p.node};}
    }
    return seen==h.count || Fail(r);
}
bool OwnedHeader(Reader& r,std::uintptr_t at,bool map) noexcept {
    std::uintptr_t p=0;if(!Read(r,at,&p,8))return false;if(!p)return true;
    return Add(r,p,24) && (map?Tree(r,p,MapKind::TextScalar):Values(r,p,8));
}
bool Repair(Reader& r,std::uintptr_t at) noexcept {return OwnedHeader(r,at+8,false) && OwnedHeader(r,at+16,true);}
bool Descriptor(Reader& r,const Record& record,const Type& type) noexcept {
    const auto p=record.identity;const auto kind=type.kind;
    if(!Text(r,p+24))return false;for(std::size_t off=40;off<=80;off+=8)if(!Intern(r,p+off))return false;
    const bool inventory=kind!=Kind::Base && kind!=Kind::Bullet && kind!=Kind::Collectable && kind!=Kind::CraftPlan;
    if(inventory){if(!Text(r,p+176))return false;for(std::size_t off=192;off<=304;off+=8)if(!Intern(r,p+off))return false;
        for(std::size_t off=320;off<=344;off+=8)if(!Intern(r,p+off,true))return false;
        if(!Aux(r,p+312,item_auxiliary::BorrowKind::PickupSounds))return false;}
    const bool weapon=kind==Kind::Firearm || kind==Kind::Melee || kind==Kind::Shield || kind==Kind::Throwable || kind==Kind::Flare || kind==Kind::Liquid;
    if(weapon){if(!Repair(r,p+384) || !Tree(r,p+424,MapKind::InternPair) || !Aux(r,p+536,item_auxiliary::BorrowKind::Effect))return false;
        for(const auto off:{472U,488U,504U,520U,752U,768U})if(!Text(r,p+off))return false;
        for(std::size_t off=544;off<=568;off+=8)if(!Intern(r,p+off))return false;}
    if(kind==Kind::Melee || kind==Kind::Shield){if(!Text(r,p+848))return false;
        if(kind==Kind::Shield && (!Text(r,p+888) || !Text(r,p+904)))return false;}
    if(kind==Kind::Throwable || kind==Kind::Flare || kind==Kind::Liquid){
        for(std::size_t off=880;off<=1104;off+=16)if(!Text(r,p+off))return false;
        if(kind==Kind::Flare)for(std::size_t off=1128;off<=1192;off+=16)if(!Text(r,p+off))return false;
        if(kind==Kind::Liquid)for(std::size_t off=1208;off<=1288;off+=16)if(!Text(r,p+off))return false;
    }
    switch(kind){
    case Kind::Ammo:return Intern(r,p+392) && Primary(r,p+400);
    case Kind::Bullet:
        for(std::size_t off=152;off<=176;off+=8)if(!Intern(r,p+off))return false;
        return Intern(r,p+200) && CraftingLevel(r,p+136,2);
    case Kind::Bundle:return Texts(r,p+384,24);
    case Kind::Collectable:return Text(r,p+144);
    case Kind::Car:return Repair(r,p+384) && Values(r,p+440,4) && Texts(r,p+464,24,8) && Texts(r,p+488) &&
        Values(r,p+512,8) && Values(r,p+536,8) && OwnedHeader(r,p+560,false) && OwnedHeader(r,p+568,true) && Intern(r,p+576);
    case Kind::CraftPart:
        for(unsigned i=0;i<17;++i)if(!CraftingLevel(r,p+392+8*i,1,i))return false;
        return Values(r,p+560,8);
    case Kind::CraftPlan:{
        if(!Repair(r,p+128))return false;
        for(const auto off:{168U,280U,304U,320U,400U})if(!Text(r,p+off))return false;
        if(!Texts(r,p+184) || !Texts(r,p+208) || !Values(r,p+232,8) || !Values(r,p+256,8) ||
            !Texts(r,p+336) || !Values(r,p+360,4) || !Intern(r,p+416))return false;
        for(unsigned i=0;i<17;++i)if(!CraftingLevel(r,p+432+8*i,1,i))return false;
        return true;}
    case Kind::Firearm:
        for(unsigned i=0;i<3;++i)if(!Values(r,p+1080+256*i,16))return false;
        for(const auto off:{1648U,1656U,1664U,1672U,1680U,1688U,1696U,1704U,1720U,1728U,1736U,1744U,1776U})if(!Intern(r,p+off))return false;
        return Primary(r,p+1768);
    case Kind::Powerup:case Kind::Battery:case Kind::Stamina:case Kind::Suppressant:case Kind::Medkit:case Kind::UseTrigger:
        for(std::size_t off=384;off<=552;off+=24)if(!Values(r,p+off,2))return false;
        return Text(r,p+576) && (kind!=Kind::UseTrigger || Intern(r,p+600));
    default:return true;
    }
}
bool SameFence(const curves::CurveReadFence& a,const curves::CurveReadFence& b,bool revision=true) noexcept {
    return a.owner_generation && a.owner_generation==b.owner_generation && (!revision || a.mutation_revision==b.mutation_revision) &&
        a.authoritative_thread && b.authoritative_thread && a.access_closed && b.access_closed && a.readers_drained && b.readers_drained;
}
}
bool Contains(const Snapshot& s,std::uintptr_t p) noexcept {
    if(!s.complete || !s.records || s.count>s.record_capacity)return false;std::size_t first=0,last=s.count;
    while(first<last){const auto middle=first+(last-first)/2;if(s.records[middle].identity<p)first=middle+1;else last=middle;}
    return first<s.count && s.records[first].identity==p;
}
Result Capture(const curves::NativeCurveAccess& a,std::uintptr_t manager,const Inputs& inputs,Snapshot* out) noexcept {
    if(!out || !out->records || !out->spans || !out->bytes || !out->record_capacity || out->record_capacity>kMaximumDescriptors ||
        !out->span_capacity || out->span_capacity>kMaximumSpans || !out->capacity || out->capacity>kMaximumBytes || !a.read || !a.fence ||
        !manager || manager>UINTPTR_MAX-728)return Result::InvalidArgument;
    out->complete=false;out->count=out->span_count=out->used=0;out->unqualified_offset=0;out->manager=manager;
    if(!a.module_base || a.module_base>UINTPTR_MAX-0x2082000 || std::memcmp(a.module_sha256,curves::kPlayerGameDllSha256,65))return Result::ProfileMismatch;
    if(!a.fence(a.user,&out->fence) || !SameFence(out->fence,out->fence) || !inputs.pools || !inputs.auxiliary || !inputs.crafting ||
        !inputs.pools->complete || !inputs.auxiliary->complete || !inputs.crafting->complete || inputs.auxiliary->manager!=manager ||
        !SameFence(inputs.pools->fence,out->fence) || !SameFence(inputs.auxiliary->fence,out->fence))return Result::FenceRejected;
    const auto& pools=*inputs.pools;const auto& auxiliary=*inputs.auxiliary;const auto& crafting=*inputs.crafting;
    if(!pools.strings || !pools.vectors || !pools.bytes || pools.string_count>pools.string_capacity || pools.vector_count>pools.vector_capacity ||
        pools.string_count>item_pools::kMaximumEntries || pools.vector_count>item_pools::kMaximumEntries || pools.used>pools.capacity ||
        !auxiliary.spans || !auxiliary.bytes || auxiliary.span_count>auxiliary.span_capacity || auxiliary.used>auxiliary.capacity ||
        !crafting.spans || !crafting.bytes || crafting.span_count>crafting.span_capacity || crafting.used>crafting.capacity)return Result::InvalidArgument;
    Reader r={a,inputs,*out};std::uintptr_t vt=0;std::uint32_t initialized=0;Header primary={};
    if(!Read(r,a.module_base+0x1E10584,&initialized,4) || !(initialized&1) || !Read(r,manager,&vt,8) || vt!=a.module_base+0x161E3D8)return Result::InvalidGraph;
    if(!Add(r,manager,8) || !Add(r,manager+80,96) || !Vector(r,manager+80,8,primary))return r.result;
    if(primary.count>out->record_capacity)return Result::CapacityExceeded;
    for(std::size_t i=0;i<primary.count;++i){Record record={};std::uintptr_t vtable=0;
        if(!Read(r,primary.data+8*i,&record.identity,8) || !Read(r,record.identity,&vtable,8))return r.result;
        const Type* type=nullptr;for(const auto& candidate:types)if(vtable==a.module_base+candidate.vtable){type=&candidate;break;}
        if(!type)return Result::UnsupportedType;record.size=type->bytes;record.vector_index=static_cast<std::uint32_t>(i);
        std::uintptr_t id_route=0,name_route=0;
        if(record.identity>UINTPTR_MAX-record.size || !Read(r,vtable+432,&id_route,8) || id_route!=a.module_base+0xEBEF30 ||
            !Read(r,vtable+408,&name_route,8) || name_route!=a.module_base+0x79F220 || !Read(r,record.identity+88,&record.id,4))return Result::InvalidGraph;
        if(!Add(r,record.identity,record.size))return r.result;out->records[out->count++]=record;
    }
    Sort(out->records,out->count,[](const Record& x,const Record& y) noexcept {return x.identity<y.identity;});
    for(std::size_t i=1;i<out->count;++i)if(out->records[i-1].identity==out->records[i].identity)return Result::InvalidGraph;
    for(std::size_t i=0;i<out->count;++i){std::uintptr_t vtable=0;if(!Read(r,out->records[i].identity,&vtable,8))return r.result;
        const Type* type=nullptr;for(const auto& candidate:types)if(vtable==a.module_base+candidate.vtable){type=&candidate;break;}
        if(!type || !Descriptor(r,out->records[i],*type))return type?r.result:Result::UnsupportedType;}
    if(!Tree(r,manager+104,MapKind::Dlc) || !Tree(r,manager+128,MapKind::NameIndex) || !Tree(r,manager+152,MapKind::NameIndex))return r.result;
    for(std::size_t i=0;i<out->count;++i)if(out->records[i].index_count!=1)return Result::InvalidGraph;
    Sort(out->spans,out->span_count,[](const item_auxiliary::Span& x,const item_auxiliary::Span& y) noexcept {return x.address<y.address;});
    for(std::size_t i=1;i<out->span_count;++i)if(out->spans[i-1].address+out->spans[i-1].allocation_size>out->spans[i].address)return Result::InvalidGraph;
    out->complete=true;auto result=Verify(a,*out);curves::CurveReadFence after={};
    if(result==Result::Succeeded && (!a.fence(a.user,&after) || !SameFence(out->fence,after)))result=Result::Changed;
    out->complete=result==Result::Succeeded;return result;
}
Result Verify(const curves::NativeCurveAccess& a,const Snapshot& s) noexcept {
    if(!s.complete || !s.spans || !s.bytes || !s.records || s.count>s.record_capacity || s.span_count>s.span_capacity ||
        s.span_capacity>kMaximumSpans || s.used>s.capacity || s.capacity>kMaximumBytes || !a.read || !a.fence)return Result::InvalidArgument;
    if(!a.module_base || std::memcmp(a.module_sha256,curves::kPlayerGameDllSha256,65))return Result::ProfileMismatch;
    curves::CurveReadFence before={},after={};if(!a.fence(a.user,&before) || !SameFence(s.fence,before,false))return Result::FenceRejected;
    std::uint8_t buffer[4096];
    for(std::size_t i=0;i<s.span_count;++i){const auto& span=s.spans[i];if(span.offset>s.used || span.size>s.used-span.offset || !span.address ||
            span.size>span.allocation_size || span.address>UINTPTR_MAX-span.allocation_size)return Result::InvalidArgument;
        for(std::size_t copied=0;copied<span.size;){const auto left=span.size-copied,n=left<sizeof(buffer)?left:sizeof(buffer);
            if(!a.read(a.user,span.address+copied,buffer,n))return Result::ReadFailed;
            if(std::memcmp(buffer,s.bytes+span.offset+copied,n))return Result::Changed;copied+=n;}
    }
    return a.fence(a.user,&after) && SameFence(before,after)?Result::Succeeded:Result::FenceRejected;
}
} // namespace dluma::fixes::custom_map_asset_isolation::item_descriptors
