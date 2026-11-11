#ifndef DLUMA_NATIVE_ENTRY_UNWIND_H
#define DLUMA_NATIVE_ENTRY_UNWIND_H
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::detour {
// Typed Win64 entry relays may use volatile R11, which is not an argument.
// Unlike FF25, this register-indirect transfer cannot be mistaken for an AMD64
// epilog by the native unwinder. It changes neither flags nor the stack.
inline bool BuildEntryRelayTransfer(unsigned char* output,std::size_t capacity,std::uintptr_t target)noexcept
{
    if(!output||capacity<14||!target)return false;
    output[0]=0x49;output[1]=0xbb;std::memcpy(output+2,&target,8);
    output[10]=0x41;output[11]=0xff;output[12]=0xe3;output[13]=0x90;return true;
}
using EntryUnwindReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
struct EntryUnwindImage {
    std::uintptr_t base;
    std::uint32_t size,maximum_functions;
    EntryUnwindReadFn read;
    void* user;
    bool allow_skipped_xmm=false;
};
inline bool EntryUnwindRead(const EntryUnwindImage& image,std::size_t rva,void* output,std::size_t bytes)noexcept
{
    return image.read&&image.base&&output&&bytes&&rva<=image.size&&bytes<=image.size-rva&&
        image.base<=UINTPTR_MAX-image.size&&image.read(image.user,image.base+rva,output,bytes);
}
template<class T>inline bool EntryUnwindField(const EntryUnwindImage& image,std::size_t rva,T* out)noexcept
{return EntryUnwindRead(image,rva,out,sizeof(*out));}

// Existing renderer merger, shared by explicitly qualified copied prologues.
// Supports only version1, no frame register/handler and <=4 contiguous ranges
// chained to their predecessor, with PUSH_NONVOL/ALLOC_SMALL/ALLOC_LARGE/
// SAVE_NONVOL. Explicitly opted-in XMM saves may be decoded/skipped ONLY after
// the copied prefix; active-prefix XMM restoration is unsupported. This is not
// a general instruction relocator or hook installer.
// Source image/profile and instruction boundaries are the caller's authority.
// On failure output is unchanged and *bytes is0.
inline bool CopyEntryUnwind(const EntryUnwindImage& image,std::uint32_t target,unsigned stolen,
    unsigned char* output,std::size_t capacity,std::size_t* bytes)noexcept
{
    if(bytes)*bytes=0;
    if(!output||!bytes||!stolen||stolen>255||target>=image.size||stolen>image.size-target||
        !image.maximum_functions||image.maximum_functions>131072)return false;
    std::uint32_t nt=0,directory=0,size=0;std::uint16_t magic=0;
    if(!EntryUnwindField(image,0x3C,&nt)||nt>0x100000||
        !EntryUnwindField(image,static_cast<std::size_t>(nt)+24,&magic)||magic!=0x20B||
        !EntryUnwindField(image,static_cast<std::size_t>(nt)+24+112+24,&directory)||
        !EntryUnwindField(image,static_cast<std::size_t>(nt)+24+112+28,&size)||
        !directory||!size||size%12||size/12>image.maximum_functions||directory>image.size||size>image.size-directory)return false;
    struct Runtime{std::uint32_t begin,end,unwind;};Runtime entry={};
    std::uint32_t first=0,last=size/12;
    while(first<last){const auto middle=first+(last-first)/2;Runtime candidate={};
        if(!EntryUnwindRead(image,directory+12ULL*middle,&candidate,sizeof(candidate)))return false;
        if(candidate.begin<target)first=middle+1;else last=middle;}
    struct Action{unsigned char offset,slots,data[6];};Action actions[28]={};
    std::size_t action_count=0,code_count=0;Runtime previous={};bool complete=false;
    for(unsigned segment=0;segment<4;++segment,++first){
        if(first==size/12||!EntryUnwindRead(image,directory+12ULL*first,&entry,sizeof(entry))||
            entry.begin!=(segment?previous.end:target)||entry.end<=entry.begin||entry.end>image.size||
            image.size<4||entry.unwind>image.size-4)return false;
        unsigned char header[4]={},codes[56]={};
        if(!EntryUnwindRead(image,entry.unwind,header,4)||header[0]!=(segment?0x21:1)||header[3]||header[2]>28)return false;
        const std::size_t native_bytes=((static_cast<std::size_t>(header[2])+1)&~std::size_t{1})*2;
        if(native_bytes>image.size-4-entry.unwind||
            (native_bytes&&!EntryUnwindRead(image,entry.unwind+4ULL,codes,native_bytes)))return false;
        if(segment){Runtime parent={};if(!EntryUnwindField(image,entry.unwind+4ULL+native_bytes,&parent)||
            parent.begin!=previous.begin||parent.end!=previous.end||parent.unwind!=previous.unwind)return false;}
        unsigned previous_offset=256;
        for(std::size_t code=0;code<header[2];){
            const unsigned offset=codes[2*code],op=codes[2*code+1]&15,info=codes[2*code+1]>>4;
            if(offset>header[1]||offset>previous_offset)return false;previous_offset=offset;
            const unsigned slots=op==0||op==2?1:op==4||op==8?2:op==9?3:op==1&&info<=1?2+info:0;
            if(!slots||code+slots>header[2])return false;
            const auto absolute=entry.begin-target+offset;
            if(op==8||op==9){
                if(!image.allow_skipped_xmm||absolute<=stolen)return false;
                code+=slots;continue;
            }
            if(absolute<=stolen){
                if(action_count==28||code_count+slots>28)return false;
                Action action={};action.offset=static_cast<unsigned char>(absolute);action.slots=static_cast<unsigned char>(slots);
                std::memcpy(action.data,codes+2*code,2*slots);action.data[0]=action.offset;
                std::size_t position=action_count;
                while(position&&actions[position-1].offset<action.offset){actions[position]=actions[position-1];--position;}
                actions[position]=action;++action_count;code_count+=slots;
            }
            code+=slots;
        }
        if(entry.end>=target+stolen){complete=true;break;}previous=entry;
    }
    const std::size_t length=4+((code_count+1)&~std::size_t{1})*2;
    if(!complete||length>capacity)return false;
    std::memset(output,0,length);output[0]=1;output[1]=static_cast<unsigned char>(stolen);output[2]=static_cast<unsigned char>(code_count);
    std::size_t cursor=4;
    for(std::size_t i=0;i<action_count;++i){std::memcpy(output+cursor,actions[i].data,2*actions[i].slots);cursor+=2*actions[i].slots;}
    *bytes=length;return true;
}
// Stack-neutral, non-epilog transfer bytes belong to the synthetic prolog.
// Action offsets stay relative to their unchanged source instructions.
inline bool CopyEntryRelayUnwind(const EntryUnwindImage& image,std::uint32_t target,unsigned stolen,
    unsigned relay_bytes,unsigned char* output,std::size_t capacity,std::size_t* bytes)noexcept
{
    if(bytes)*bytes=0;
    if(relay_bytes<stolen||relay_bytes>255)return false;
    unsigned char codes[64]={};std::size_t count=0;
    if(!CopyEntryUnwind(image,target,stolen,codes,sizeof(codes),&count)||count>capacity||!output||!bytes)return false;
    codes[1]=static_cast<unsigned char>(relay_bytes);std::memcpy(output,codes,count);*bytes=count;return true;
}
}
#endif
