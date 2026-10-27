#include "NativeXuiCompiledSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::xui_compiled {
namespace {
using R=xui::Result;
struct Header {std::uintptr_t data;std::uint32_t count,capacity;};
struct Map {std::uintptr_t head,tail,buckets[256];std::uint32_t count;};
struct Stamp {Header tables,ranges;Map maps[2];std::uint32_t next;};
bool Read(const xui::Access& a,std::uintptr_t address,void* out,std::size_t bytes)noexcept {
    return address&&bytes&&address<=UINTPTR_MAX-bytes&&a.read(a.user,address,out,bytes);
}
bool Vector(Header h,std::size_t stride,std::size_t maximum)noexcept {
    return h.count<=h.capacity&&h.count<=maximum&&h.capacity<=kRecords&&
        (!h.count||(h.data&&h.count<=(UINTPTR_MAX-h.data)/stride));
}
bool Same(const xui::Fence& a,const xui::Fence& b)noexcept {
    return a.manager==b.manager&&a.generation==b.generation&&a.revision==b.revision&&
        a.frame_token==b.frame_token&&a.thread_id==b.thread_id;
}
R ReadStamp(const xui::Access& a,const xui::Fence& f,Stamp* out)noexcept {
    std::uintptr_t vtable=0;
    if(!Read(a,f.manager,&vtable,8))return R::ReadFailed;
    if(vtable!=a.engine_base+0x9173E8)return R::WrongOwnerType;
    const auto base=f.manager+1048;
    if(!Read(a,base+2112,&out->tables,sizeof(Header))||!Read(a,base,&out->ranges,sizeof(Header))||
        !Read(a,base+2136,&out->next,4))return R::ReadFailed;
    if(!Vector(out->tables,8,kTables)||!Vector(out->ranges,24,kRanges))return R::InvalidVector;
    for(unsigned index=0;index<2;++index){auto& map=out->maps[index];const auto offset=base+(index?24:2144);
        if(!Read(a,offset,&map.head,8)||!Read(a,offset+8,&map.tail,8)||!Read(a,offset+16,&map.count,4)||
            !Read(a,offset+32,map.buckets,sizeof(map.buckets)))return R::ReadFailed;
        if(map.count>kSymbols||static_cast<bool>(map.head)!=static_cast<bool>(map.count)||
            static_cast<bool>(map.tail)!=static_cast<bool>(map.count))return R::InvalidVector;}
    return R::Succeeded;
}
R Pass(const xui::Access& a,const Stamp& stamp,Snapshot* out,bool verify,bool dry=false)noexcept {
    if(stamp.tables.count>out->table_capacity||stamp.ranges.count>out->range_capacity||
        static_cast<std::size_t>(stamp.maps[0].count)+stamp.maps[1].count>out->symbol_capacity)return R::CapacityExceeded;
    std::size_t record_cursor=0;
    for(std::uint32_t n=0;n<stamp.tables.count;++n){
        Table value={};Header rows={};
        if(!Read(a,stamp.tables.data+8ULL*n,&value.address,8)||!value.address||value.address>UINTPTR_MAX-40||
            !Read(a,value.address,&rows,sizeof(rows))||!Read(a,value.address+24,&value.first_word,4)||
            !Read(a,value.address+28,&value.last_word,4)||!Read(a,value.address+32,&value.id,4)||
            !Read(a,value.address+36,&value.binding_count,4))return R::ReadFailed;
        if(!Vector(rows,8,kRecords))return R::InvalidVector;
        if(rows.count>out->record_capacity-record_cursor)return R::CapacityExceeded;
        value.record_offset=static_cast<std::uint32_t>(record_cursor);value.record_count=rows.count;
        for(std::uint32_t j=0;j<rows.count;++j){Record row={};
            if(!Read(a,rows.data+8ULL*j,&row.address,8)||!Read(a,row.address,row.words,sizeof(row.words)))return R::ReadFailed;
            if(verify){const auto& old=out->records[record_cursor];
                if(old.address!=row.address||std::memcmp(old.words,row.words,sizeof(row.words)))return R::Changed;}
            else if(!dry)out->records[record_cursor]=row;
            ++record_cursor;
        }
        if(verify){const auto& old=out->tables[n];
            if(old.address!=value.address||old.id!=value.id||old.record_offset!=value.record_offset||
                old.record_count!=value.record_count||old.first_word!=value.first_word||old.last_word!=value.last_word||
                old.binding_count!=value.binding_count)return R::Changed;}
        else if(!dry)out->tables[n]=value;
    }
    std::size_t strings=0,symbol_cursor=0;
    for(std::uint32_t n=0;n<stamp.ranges.count;++n){AnimationRange value={};Header name={};
        value.address=stamp.ranges.data+24ULL*n;
        if(!Read(a,value.address,&value.start,2)||!Read(a,value.address+2,&value.end,2)||
            !Read(a,value.address+4,&value.id,2)||!Read(a,value.address+8,&name,sizeof(name)))return R::ReadFailed;
        if(!Vector(name,1,xui::kMaximumNameBytes))return R::InvalidString;
        value.name_storage=name.data;value.string_offset=static_cast<std::uint32_t>(strings);value.string_bytes=name.count;
        const auto bytes=static_cast<std::size_t>(name.count)+1;
        if(bytes>out->string_capacity-strings)return R::CapacityExceeded;
        for(std::size_t j=0;j<bytes;++j){char ch=0;
            if(name.data){if(!Read(a,name.data+j,&ch,1))return R::ReadFailed;}else if(name.count)return R::InvalidString;
            if((j==name.count)!=(ch==0))return R::InvalidString;
            if(verify){if(out->strings[strings+j]!=ch)return R::Changed;}else if(!dry)out->strings[strings+j]=ch;}
        if(verify){const auto& old=out->ranges[n];
            if(old.address!=value.address||old.name_storage!=value.name_storage||old.start!=value.start||old.end!=value.end||
                old.id!=value.id||old.string_offset!=value.string_offset||old.string_bytes!=value.string_bytes)return R::Changed;}
        else if(!dry)out->ranges[n]=value;
        strings+=bytes;
    }
    for(unsigned map_index=0;map_index<2;++map_index){const auto& map=stamp.maps[map_index];
    std::uintptr_t node=map.head,previous=0,first_bucket[256]={};
    for(std::uint32_t n=0;n<map.count;++n){Header name={};Symbol value={};value.map=static_cast<MapKind>(map_index);
        std::uintptr_t next=0,back=0;std::uint32_t hash=0;
        if(node<32||node>UINTPTR_MAX-16||!Read(a,node-32,&name,sizeof(name))||!Read(a,node-16,&value.value,2)||
            !Read(a,node-8,&hash,4)||!Read(a,node,&next,8)||!Read(a,node+8,&back,8))return R::ReadFailed;
        if(back!=previous||hash>255||!Vector(name,1,xui::kMaximumNameBytes))return R::InvalidEntry;
        const auto bytes=static_cast<std::size_t>(name.count)+1;
        if(bytes>out->string_capacity-strings)return R::CapacityExceeded;
        value.node=node;value.string_offset=static_cast<std::uint32_t>(strings);value.string_bytes=name.count;
        unsigned char actual_hash=1;
        for(std::size_t j=0;j<bytes;++j){char c=0;
            if(name.data){if(!Read(a,name.data+j,&c,1))return R::ReadFailed;}
            else if(name.count)return R::InvalidString;
            if((j==name.count)!=(c==0))return R::InvalidString;
            if(c)actual_hash=static_cast<unsigned char>(static_cast<unsigned char>(c)^(33*actual_hash));
            if(verify){if(out->strings[strings+j]!=c)return R::Changed;}
            else if(!dry)out->strings[strings+j]=c;
        }
        if(actual_hash!=hash)return R::InvalidEntry;
        if(!first_bucket[hash])first_bucket[hash]=node;
        if(verify){const auto& old=out->symbols[symbol_cursor];
            if(old.node!=value.node||old.string_offset!=value.string_offset||old.string_bytes!=value.string_bytes||
                old.value!=value.value||old.map!=value.map)return R::Changed;}
        else if(!dry)out->symbols[symbol_cursor]=value;
        ++symbol_cursor;strings+=bytes;previous=node;node=next;
    }
    if(node||previous!=map.tail||std::memcmp(first_bucket,map.buckets,sizeof(first_bucket)))return R::InvalidVector;
    }
    if(verify&&(record_cursor!=out->record_count||strings!=out->string_bytes||out->next_symbol_id!=stamp.next))return R::Changed;
    if(!dry){out->table_count=stamp.tables.count;out->range_count=stamp.ranges.count;out->record_count=record_cursor;out->symbol_count=symbol_cursor;
        out->string_bytes=strings;out->next_symbol_id=stamp.next;}
    return R::Succeeded;
}
bool Range(std::uintptr_t a,std::size_t n,std::uintptr_t b,std::size_t m)noexcept {
    return a<=UINTPTR_MAX-n&&b<=UINTPTR_MAX-m&&(a+n<=b||b+m<=a);
}
bool ValidStorage(const Snapshot& out)noexcept {
    const std::uintptr_t pointers[]={reinterpret_cast<std::uintptr_t>(out.tables),reinterpret_cast<std::uintptr_t>(out.records),
        reinterpret_cast<std::uintptr_t>(out.symbols),reinterpret_cast<std::uintptr_t>(out.strings),reinterpret_cast<std::uintptr_t>(&out),
        reinterpret_cast<std::uintptr_t>(out.ranges)};
    const std::size_t bytes[]={out.table_capacity*sizeof(Table),out.record_capacity*sizeof(Record),
        out.symbol_capacity*sizeof(Symbol),out.string_capacity,sizeof(out),out.range_capacity*sizeof(AnimationRange)};
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<i;++j)if(!Range(pointers[i],bytes[i],pointers[j],bytes[j]))return false;
    return pointers[0]%alignof(Table)==0&&pointers[1]%alignof(Record)==0&&pointers[2]%alignof(Symbol)==0&&
        pointers[5]%alignof(AnimationRange)==0;
}
struct ProtectedAccess {const xui::Access* original;const Snapshot* output;};
bool ProtectedRead(void* user,std::uintptr_t address,void* output,std::size_t bytes)noexcept {
    const auto& p=*static_cast<ProtectedAccess*>(user);const auto& s=*p.output;
    if(!Range(address,bytes,reinterpret_cast<std::uintptr_t>(&s),sizeof(s))||
        !Range(address,bytes,reinterpret_cast<std::uintptr_t>(s.tables),s.table_capacity*sizeof(Table))||
        !Range(address,bytes,reinterpret_cast<std::uintptr_t>(s.records),s.record_capacity*sizeof(Record))||
        !Range(address,bytes,reinterpret_cast<std::uintptr_t>(s.symbols),s.symbol_capacity*sizeof(Symbol))||
        !Range(address,bytes,reinterpret_cast<std::uintptr_t>(s.ranges),s.range_capacity*sizeof(AnimationRange))||
        !Range(address,bytes,reinterpret_cast<std::uintptr_t>(s.strings),s.string_capacity))return false;
    return p.original->read(p.original->user,address,output,bytes);
}
bool ProtectedFence(void* user,xui::Fence* out)noexcept {
    const auto& p=*static_cast<ProtectedAccess*>(user);return p.original->fence(p.original->user,out);
}
bool ValidComplete(const Snapshot& s)noexcept {
    if(!s.complete||!s.tables||!s.records||!s.symbols||!s.strings||!s.fence.manager||!s.fence.generation||
        s.table_count>s.table_capacity||s.table_capacity>kTables||s.record_count>s.record_capacity||s.record_capacity>kRecords||
        s.symbol_count>s.symbol_capacity||s.symbol_capacity>kSymbols||s.string_bytes>s.string_capacity||s.string_capacity>kStringBytes||
        s.range_count>s.range_capacity||s.range_capacity>kRanges||(s.range_count&&!s.ranges))return false;
    std::size_t rows=0,strings=0;
    for(std::size_t n=0;n<s.table_count;++n){const auto& t=s.tables[n];
        if(!t.address||t.record_offset!=rows||t.record_count>s.record_count-rows)return false;
        rows+=t.record_count;}
    for(std::size_t n=0;n<s.range_count;++n){const auto& row=s.ranges[n];
        if(!row.address||row.string_offset!=strings||row.string_bytes>=s.string_bytes-strings||s.strings[strings+row.string_bytes])return false;
        strings+=static_cast<std::size_t>(row.string_bytes)+1;}
    for(std::size_t n=0;n<s.symbol_count;++n){const auto& symbol=s.symbols[n];
        if(!symbol.node||symbol.string_offset!=strings||symbol.string_bytes>=s.string_bytes-strings||
            s.strings[strings+symbol.string_bytes])return false;
        strings+=static_cast<std::size_t>(symbol.string_bytes)+1;}
    return rows==s.record_count&&strings==s.string_bytes;
}
}
xui::Result Capture(const xui::Access& original,Snapshot* out)noexcept {
    if(!out)return R::InvalidArgument;out->complete=false;out->fence={};
    out->table_count=out->range_count=out->record_count=out->symbol_count=out->string_bytes=0;
    if(!original.engine_base||original.engine_base>UINTPTR_MAX-0xE98000||!original.read||!original.fence||!out->tables||!out->records||!out->symbols||!out->strings||
        !out->table_capacity||out->table_capacity>kTables||out->range_capacity>kRanges||(out->range_capacity&&!out->ranges)||
        !out->record_capacity||out->record_capacity>kRecords||
        !out->symbol_capacity||out->symbol_capacity>kSymbols||!out->string_capacity||out->string_capacity>kStringBytes||
        !ValidStorage(*out))return out->result=R::InvalidArgument;
    if(std::memcmp(original.sha256,xui::kPlayerEngineSha256,sizeof(original.sha256)))return out->result=R::ProfileMismatch;
    ProtectedAccess protected_access={&original,out};xui::Access a=original;
    a.read=&ProtectedRead;a.fence=&ProtectedFence;a.user=&protected_access;
    xui::Fence before={},after={};Stamp first={},second={};
    if(!a.fence(a.user,&before)||!before.manager||before.manager>UINTPTR_MAX-5320||
        !before.generation||!before.thread_id||!before.frame_token)return out->result=R::FenceUnavailable;
    auto result=ReadStamp(a,before,&first);
    // Validate every native source range before any output payload is written.
    // This also rejects a buffer that aliases a later table/string allocation.
    if(result==R::Succeeded)result=Pass(a,first,out,false,true);
    if(result==R::Succeeded)result=Pass(a,first,out,false);
    if(result==R::Succeeded)result=ReadStamp(a,before,&second);
    if(result==R::Succeeded&&std::memcmp(&first,&second,sizeof(first)))result=R::Changed;
    if(result==R::Succeeded)result=Pass(a,first,out,true);
    if(result==R::Succeeded)result=ReadStamp(a,before,&second);
    if(result==R::Succeeded&&(std::memcmp(&first,&second,sizeof(first))||!a.fence(a.user,&after)||!Same(before,after)))result=R::Changed;
    out->result=result;out->complete=result==R::Succeeded;if(out->complete)out->fence=before;return result;
}
bool BaselinePreserved(const Snapshot& b,const Snapshot& c)noexcept {
    if(!ValidComplete(b)||!ValidComplete(c)||b.table_count>c.table_count||b.range_count>c.range_count||
        b.record_count>c.record_count||b.symbol_count>c.symbol_count||
        b.fence.manager!=c.fence.manager||b.fence.generation!=c.fence.generation||c.next_symbol_id<b.next_symbol_id)return false;
    for(std::size_t n=0;n<b.table_count;++n){const auto& l=b.tables[n];const auto& r=c.tables[n];
        if(l.address!=r.address||l.id!=r.id||l.record_count!=r.record_count||l.first_word!=r.first_word||l.last_word!=r.last_word)return false;
        for(std::size_t j=0;j<l.record_count;++j){const auto& lr=b.records[l.record_offset+j];const auto& rr=c.records[r.record_offset+j];
            if(lr.address!=rr.address||std::memcmp(lr.words,rr.words,sizeof(lr.words)))return false;}
    }
    for(std::size_t n=0;n<b.range_count;++n){const auto& l=b.ranges[n];const auto& r=c.ranges[n];
        // Native vector growth may relocate inline rows; their independently
        // owned names and values remain the immutable baseline identities.
        if(l.name_storage!=r.name_storage||l.start!=r.start||l.end!=r.end||l.id!=r.id||l.string_bytes!=r.string_bytes||
            std::memcmp(b.strings+l.string_offset,c.strings+r.string_offset,l.string_bytes+1))return false;}
    for(std::size_t n=0;n<b.symbol_count;++n){const auto& l=b.symbols[n];bool found=false;
        for(std::size_t j=0;j<c.symbol_count;++j){const auto& r=c.symbols[j];
            if(l.map==r.map&&l.string_bytes==r.string_bytes&&!std::memcmp(b.strings+l.string_offset,c.strings+r.string_offset,l.string_bytes+1)){
                if(found||l.node!=r.node||l.value!=r.value)return false;found=true;}}
        if(!found)return false;
    }
    return true;
}
} // namespace
