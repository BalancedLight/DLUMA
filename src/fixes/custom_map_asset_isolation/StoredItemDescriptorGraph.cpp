#define _HAS_EXCEPTIONS 0
#include "StoredItemDescriptorGraph.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::stored_item_graph {
namespace {
using Reader=player_stored_progress::Reader;using Fence=player_stored_progress::Fence;
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
struct VectorStamp {std::uintptr_t address;Header value;};
struct State {const Reader* reader;const stored_blueprints::Owner* blueprints;Scratch* scratch;Proof* proof;std::uint32_t reads;};
bool Fail(State& s,Failure f) noexcept {s.proof->failure=f;return false;}
bool Read(State& s,std::uintptr_t p,void* out,std::size_t n) noexcept {
    if(++s.reads>1000000)return Fail(s,Failure::Limit);
    return p && p<=UINTPTR_MAX-n && s.reader->read(s.reader->user,p,out,n);
}
bool SameFence(const Fence& a,const Fence& b) noexcept {
    return a.exclusive_preservation_owner && b.exclusive_preservation_owner && a.player_state==b.player_state &&
        a.owner_generation==b.owner_generation && a.mutation_revision==b.mutation_revision && a.thread_id==b.thread_id &&
        !a.current_session && !b.current_session && !a.local_player_di && !b.local_player_di;
}
bool Vector(State& s,std::uintptr_t at,std::size_t stride,Header* h) noexcept {
    return Read(s,at,h,sizeof(*h)) && h->count<=h->capacity && h->count<=4096 && h->capacity<=1048576 &&
        (!h->count || (h->data && h->count<=(UINTPTR_MAX-h->data)/stride));
}
bool String(State& s,std::uintptr_t at) noexcept {
    Header h={};if(!Read(s,at,&h,sizeof(h)) || h.count>h.capacity || h.count>65535 ||
        (h.count && !h.data) || (h.data && h.data>UINTPTR_MAX-h.count-1))return Fail(s,Failure::InvalidString);
    if(s.proof->string_bytes>kMaximumStringBytes-h.count)return Fail(s,Failure::Limit);
    if(!h.data){s.proof->string_bytes+=h.count;return true;}
    std::uint8_t bytes[256];std::size_t done=0;
    while(done<h.count){const auto size=h.count-done<sizeof(bytes)?h.count-done:sizeof(bytes);
        if(!Read(s,h.data+done,bytes,size) || std::memchr(bytes,0,size))return Fail(s,Failure::InvalidString);done+=size;}
    std::uint8_t end=1;Header after={};
    if(!Read(s,h.data+h.count,&end,1) || end || !Read(s,at,&after,sizeof(after)) || std::memcmp(&h,&after,sizeof(h)))return Fail(s,Failure::InvalidString);
    s.proof->string_bytes+=h.count;return true;
}
bool ResolveId(State& s,std::uint32_t id,std::uintptr_t expected) noexcept {
    if(id==UINT32_MAX)return false;std::uintptr_t node=0,seen[128]={};std::size_t n=0;
    if(!Read(s,s.reader->gamedll_base+0x1E0EC38,&node,8))return false;
    const auto key=(static_cast<std::int32_t>(id)>>16)-2;std::uintptr_t candidate=0;
    while(node){if(node<32 || n==128)return false;for(std::size_t i=0;i<n;++i)if(seen[i]==node)return false;seen[n++]=node;
        std::int32_t current=0;std::uintptr_t next=0;
        if(!Read(s,node-32,&current,4))return false;
        if(current>=key){candidate=node;if(!Read(s,node,&next,8))return false;}
        else if(!Read(s,node+16,&next,8))return false;node=next;}
    if(!candidate)return false;std::int32_t found=0;Header entries={};
    if(!Read(s,candidate-32,&found,4) || found!=key || !Read(s,candidate-24,&entries,sizeof(entries)) || entries.count>entries.capacity ||
        entries.count>65536 || (id&65535U)>=entries.count || !entries.data || entries.data>UINTPTR_MAX-8ULL*entries.count)return false;
    std::uintptr_t value=0;return Read(s,entries.data+8ULL*(id&65535U),&value,8) && value==expected;
}
bool Descriptor(State& s,std::uintptr_t object,bool nullable=true) noexcept {
    if(!object)return nullable?true:Fail(s,Failure::InvalidDescriptor);
    const auto first=(object>>4)%kDescriptorCacheSize;std::size_t empty=kDescriptorCacheSize;
    for(std::size_t i=0;i<kDescriptorCacheSize;++i){const auto slot=(first+i)%kDescriptorCacheSize;
        if(s.scratch->descriptors[slot]==object)return true;if(!s.scratch->descriptors[slot]){empty=slot;break;}}
    if(empty==kDescriptorCacheSize)return Fail(s,Failure::Limit);
    if(object>UINTPTR_MAX-128)return Fail(s,Failure::InvalidDescriptor);
    std::uintptr_t vt=0,id_route=0,name_route=0;std::uint32_t id=0,initialized=0;const auto base=s.reader->gamedll_base;
    if(!Read(s,base+0x1E10584,&initialized,4) || !(initialized&1))return Fail(s,Failure::InvalidDescriptor);
    if(!Read(s,object,&vt,8) || vt<base+0x13CC000 || vt>base+0x1C95000-440 || !Read(s,vt+432,&id_route,8) ||
        !Read(s,vt+408,&name_route,8) || id_route!=base+0xEBEF30 || name_route!=base+0x79F220 || !Read(s,object+88,&id,4))
        return Fail(s,Failure::InvalidDescriptor);
    if(!ResolveId(s,id,object))return Fail(s,Failure::DescriptorUnowned);
    if(!String(s,object+24))return false;
    s.scratch->descriptors[empty]=object;++s.proof->descriptor_count;return true;
}
bool Route(State& s,std::uintptr_t object,std::size_t slot,std::uintptr_t* route) noexcept {
    std::uintptr_t vt=0;const auto base=s.reader->gamedll_base;
    return Read(s,object,&vt,8) && vt>=base+0x13CC000 && vt<=base+0x1C95000-slot-8 && Read(s,vt+slot,route,8);
}
bool ScalarKind(State& s,std::uintptr_t object) noexcept {
    std::uintptr_t route=0;std::uint32_t value=0;const auto base=s.reader->gamedll_base;
    if(!Route(s,object,600,&route))return Fail(s,Failure::InvalidDescriptor);
    if(route==base+0x2390)return true;
    const std::uintptr_t offset=route==base+0xEBEA90?128:route==base+0x6B2B70?1756:0;
    return offset && object<=UINTPTR_MAX-offset-4 && Read(s,object+offset,&value,4) ? true:Fail(s,Failure::InvalidDescriptor);
}
bool PointerField(State& s,std::uintptr_t object,std::size_t slot,std::uintptr_t* value) noexcept {
    std::uintptr_t route=0;const auto base=s.reader->gamedll_base;*value=0;
    if(!Route(s,object,slot,&route))return Fail(s,Failure::InvalidDescriptor);
    if(route==base+0x1481A0)return true;
    const std::uintptr_t offset=route==base+0x6AC8B0?400:(slot==640 && route==base+0x6B2B80)?1768:0;
    if(!offset || object>UINTPTR_MAX-offset-8 || !Read(s,object+offset,value,8))return Fail(s,Failure::InvalidDescriptor);
    return Descriptor(s,*value);
}
bool SerializerParent(State& s,std::uintptr_t parent) noexcept {
    const auto base=s.reader->gamedll_base;std::uintptr_t route=0;std::uint32_t category=0;
    if(!Route(s,parent,448,&route) || route!=base+0x694500 || !Read(s,parent+120,&category,4))return Fail(s,Failure::InvalidDescriptor);
    if(!Route(s,parent,2760,&route))return Fail(s,Failure::InvalidDescriptor);
    if(route==base+0x6B6F90){std::uint8_t value=0;
        if(parent>UINTPTR_MAX-707 || !Read(s,parent+706,&value,1))return Fail(s,Failure::InvalidDescriptor);}
    else if(route!=base+0x1481A0)return Fail(s,Failure::InvalidDescriptor);
    std::uintptr_t kind_parent=0,ammo=0;
    // ItemDescAmmo's bullet (+400) supplies the seed kind; ItemDescFirearm's
    // default ammo (+1768) supplies the serialized ammo ID when no override is
    // present. These nested descriptors must be live manager entries too.
    return PointerField(s,parent,624,&kind_parent) && ScalarKind(s,kind_parent?kind_parent:parent) && PointerField(s,parent,640,&ammo);
}
bool WrapperRoutes(State& s) noexcept {
    struct Entry {std::size_t slot;std::uintptr_t rva;};
    constexpr Entry entries[]={{432,0x6C96F0},{448,0x6C9840},{600,0x6C9880},{624,0x6C9870},{640,0x6C95E0},
        {2760,0x6C8270},{2904,0x106B80},{3176,0x6C7F30},{3184,0x694AA0},{3192,0xF88490},{3328,0x6C98D0}};
    for(const auto& entry:entries){std::uintptr_t route=0;
        if(!Read(s,s.reader->gamedll_base+stored_blueprints::kItemContextVtableRva+entry.slot,&route,8) ||
            route!=s.reader->gamedll_base+entry.rva)return Fail(s,Failure::InvalidItemType);}
    return true;
}
bool LiveItem(State& s,std::uintptr_t item) noexcept {
    if(++s.proof->live_items>kMaximumItems)return Fail(s,Failure::Limit);
    std::uint8_t before[192],after[192];if(!Read(s,item,before,sizeof(before)))return Fail(s,Failure::InvalidItemType);
    std::uintptr_t vt=0;std::memcpy(&vt,before+24,8);if(vt!=s.reader->gamedll_base+stored_blueprints::kItemContextVtableRva)return Fail(s,Failure::InvalidItemType);
    const std::size_t offsets[]={32,48,56,152};
    for(auto offset:offsets){std::uintptr_t pointer=0;std::memcpy(&pointer,before+offset,8);if(!Descriptor(s,pointer,offset!=32))return false;}
    std::uintptr_t parent=0;std::memcpy(&parent,before+32,8);if(!SerializerParent(s,parent))return false;
    std::uintptr_t blueprint=0;std::memcpy(&blueprint,before+64,8);
    if(blueprint){if(!stored_blueprints::ValidateOwnedBlueprint(s.blueprints,blueprint))return Fail(s,Failure::UnownedBlueprint);++s.proof->blueprint_count;}
    Header modifiers={};if(!Vector(s,item+72,8,&modifiers))return Fail(s,Failure::InvalidVector);
    for(std::uint32_t i=0;i<modifiers.count;++i){std::uintptr_t pointer=0;
        if(!Read(s,modifiers.data+8ULL*i,&pointer,8))return Fail(s,Failure::InvalidDescriptor);if(!Descriptor(s,pointer))return false;}
    if(!Read(s,item,after,sizeof(after)) || std::memcmp(before,after,sizeof(before)))return Fail(s,Failure::Changed);
    return true;
}
bool LostItem(State& s,std::uintptr_t item) noexcept {
    if(++s.proof->lost_items>kMaximumItems)return Fail(s,Failure::Limit);
    std::uint8_t before[120],after[120];if(!Read(s,item,before,sizeof(before)))return Fail(s,Failure::InvalidItemType);
    if(!String(s,item+8) || !String(s,item+24))return false;
    std::uint32_t serialized_count=0;std::memcpy(&serialized_count,before+72,4);Header names={};
    if(!Vector(s,item+80,16,&names) || serialized_count>names.count)return Fail(s,Failure::InvalidVector);
    for(std::uint32_t i=0;i<serialized_count;++i)if(!String(s,names.data+16ULL*i))return false;
    if(!Read(s,item,after,sizeof(after)) || std::memcmp(before,after,sizeof(before)))return Fail(s,Failure::Changed);return true;
}
} // namespace
bool Capture(const player_stored_progress::Reader& reader,const stored_blueprints::Owner* blueprints,std::uintptr_t state,
    std::uint64_t generation,Scratch* scratch,Proof* out) noexcept {
    if(!scratch || !out)return false;*out={};std::memset(scratch,0,sizeof(*scratch));State s={&reader,blueprints,scratch,out,0};
    if(!reader.read || !reader.read_fence || !reader.exact_image_and_section_routes || !reader.gamedll_base ||
        reader.gamedll_base>UINTPTR_MAX-0x2082000 || !state || state>UINTPTR_MAX-0x470 || !generation)return Fail(s,Failure::InvalidArgument);
    out->state=state;out->generation=generation;Fence before={},after={};
    if(!reader.read_fence(reader.user,&before) || !before.exclusive_preservation_owner || before.player_state!=state ||
        before.owner_generation!=generation || before.thread_id!=GetCurrentThreadId() || before.current_session || before.local_player_di)
        return Fail(s,Failure::OwnerUnqualified);
    std::uintptr_t vt=0,context=0;if(!Read(s,state,&vt,8) || vt!=reader.gamedll_base+0x17E2FF8 || !Read(s,state+0x468,&context,8))return Fail(s,Failure::InvalidArgument);
    out->context=context;out->revision=before.mutation_revision;if(context>UINTPTR_MAX-4360)return Fail(s,Failure::InvalidVector);
    if(context && !WrapperRoutes(s))return false;
    VectorStamp vectors[30]={};std::size_t count=0;
    if(context){for(unsigned group=0;group<5;++group)for(unsigned slot=0;slot<3;++slot){
        const std::uintptr_t addresses[]={context+24+160ULL*group+24ULL*slot,context+824+120ULL*group+24ULL*slot};
        for(unsigned kind=0;kind<2;++kind){Header header={};const auto stride=kind?120U:192U;
            if(!Vector(s,addresses[kind],stride,&header))return Fail(s,Failure::InvalidVector);vectors[count++]={addresses[kind],header};
            for(std::uint32_t i=0;i<header.count;++i)if(kind?!LostItem(s,header.data+120ULL*i):!LiveItem(s,header.data+192ULL*i))return false;}}
        for(std::size_t i=0;i<count;++i){Header h={};if(!Read(s,vectors[i].address,&h,sizeof(h)) || std::memcmp(&h,&vectors[i].value,sizeof(h)))return Fail(s,Failure::Changed);}}
    std::uintptr_t current=0;
    if(!Read(s,state+0x468,&current,8) || current!=context || !reader.read_fence(reader.user,&after) || !SameFence(before,after))return Fail(s,Failure::Changed);
    out->complete=true;return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::stored_item_graph
