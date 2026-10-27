#define _HAS_EXCEPTIONS 0
#include "NativeArchiveContent.h"
extern "C" {
#include "../../dlw_common.h"
}
#include <cstring>
#include <intrin.h>

namespace dluma::fixes::custom_map_asset_isolation::archive_content {
namespace {
constexpr std::uintptr_t kRead=0x24970,kSeek=0x24AE0,kVtable=0x449D8;
struct Stamp {
    std::uintptr_t source,member;
    std::uint64_t cursor;
    std::uint32_t initialized,bytes;
    unsigned char descriptor[28];
};
bool Separate(std::uintptr_t a,std::size_t n,std::uintptr_t b,std::size_t m)noexcept {
    return a<=UINTPTR_MAX-n && b<=UINTPTR_MAX-m && (a+n<=b || b+m<=a);
}
bool Memory(const Access& a,std::uintptr_t p,void* out,std::size_t n)noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Snapshot(const Access& a,const file_lifetime::FileRecord& f,Stamp* out)noexcept {
    std::uintptr_t table=0;
    if(!a.authorize(a.user,f) || f.file>UINTPTR_MAX-82000 ||
       !Memory(a,f.file,&table,8) || table!=a.filesystem_base+kVtable ||
       !Memory(a,f.file+24,&out->source,8) || out->source!=f.source.address ||
       !Memory(a,f.file+32,&out->member,8) || !out->member ||
       !Memory(a,f.file+40,&out->initialized,4) ||
       !Memory(a,f.file+81992,&out->cursor,8) ||
       !Memory(a,out->member,out->descriptor,sizeof(out->descriptor))) return false;
    std::memcpy(&out->bytes,out->descriptor+16,4);
    return true;
}
bool SameDefinition(const Stamp& a,const Stamp& b)noexcept {
    return a.source==b.source && a.member==b.member && a.bytes==b.bytes &&
        !std::memcmp(a.descriptor,b.descriptor,sizeof(a.descriptor));
}
}
bool ValidateNativeAccess(const Access& a)noexcept {
    if(!a.exact_filesystem_profile || !a.read || !a.filesystem_base ||
       a.filesystem_base>UINTPTR_MAX-kVtable-8) return false;
    constexpr unsigned char read_prefix[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    constexpr unsigned char seek_prefix[]={0x4C,0x8B,0xD2,0x4C,0x8B,0xD9,0x41,0x83,0xF8,0x01,0x75,0x09,0x4C,0x03,0x91,0x48};
    unsigned char bytes[16]={};
    return Memory(a,a.filesystem_base+kRead,bytes,sizeof(bytes)) && !std::memcmp(bytes,read_prefix,sizeof(bytes)) &&
        Memory(a,a.filesystem_base+kSeek,bytes,sizeof(bytes)) && !std::memcmp(bytes,seek_prefix,sizeof(bytes));
}
std::int64_t NativeRead(std::uintptr_t base,std::uintptr_t file,void* data,std::size_t bytes)noexcept {
    using Fn=std::int64_t(__fastcall*)(std::uintptr_t,void*,std::size_t);
    return reinterpret_cast<Fn>(base+kRead)(file,data,bytes);
}
bool NativeSeek(std::uintptr_t base,std::uintptr_t file,std::uint64_t offset)noexcept {
    using Fn=unsigned char(__fastcall*)(std::uintptr_t,std::uint64_t,int);
    return reinterpret_cast<Fn>(base+kSeek)(file,offset,0)!=0;
}
Result CaptureFresh(const Access& a,const file_lifetime::FileRecord& f,std::uint64_t maximum,
    void* scratch,std::size_t capacity,Digest* out)noexcept {
    if(!out)return Result::InvalidArgument;
    *out={};out->file=f;out->result=Result::InvalidArgument;
    if(!a.exact_filesystem_profile || !a.read || !a.authorize || !a.filesystem_base ||
       a.filesystem_base>UINTPTR_MAX-kVtable-8 || !maximum || !scratch || !capacity || capacity>kChunkBytes ||
       !f.file || !f.generation || !f.root || !f.source.address || !f.source.generation ||
       f.kind!=file_lifetime::FileKind::Zip || f.life!=file_lifetime::Life::Live ||
       static_cast<bool>(a.read_file)!=static_cast<bool>(a.seek_file) ||
       !Separate(reinterpret_cast<std::uintptr_t>(scratch),capacity,reinterpret_cast<std::uintptr_t>(out),sizeof(*out)) ||
       !Separate(f.file,0x140A8,reinterpret_cast<std::uintptr_t>(scratch),capacity) ||
       !Separate(f.file,0x140A8,reinterpret_cast<std::uintptr_t>(out),sizeof(*out)))return out->result;
    if(!a.authorize(a.user,f))return out->result=Result::Unauthorized;
    if(!a.read_file && !ValidateNativeAccess(a))return out->result=Result::WrongType;
    Stamp before={};
    if(!Snapshot(a,f,&before))return out->result=Result::WrongType;
    if(before.cursor || before.initialized)return out->result=Result::NotFresh;
    if(before.bytes>maximum)return out->result=Result::TooLarge;
    out->bytes=before.bytes;
    DlumaSha256Context hash={};dluma_sha256_init(&hash);
    std::uint64_t consumed=0;
    bool read_started=false;
    auto result=Result::Succeeded;
    while(consumed<before.bytes) {
        if(!a.authorize(a.user,f)){result=Result::Unauthorized;break;}
        const auto remaining=before.bytes-consumed;
        const auto wanted=static_cast<std::size_t>(remaining<capacity?remaining:capacity);
        read_started=true;
        const auto got=a.read_file?a.read_file(a.user,f.file,scratch,wanted):NativeRead(a.filesystem_base,f.file,scratch,wanted);
        if(got<=0 || static_cast<std::uint64_t>(got)>wanted){result=Result::ReadFailed;break;}
        dluma_sha256_update(&hash,scratch,static_cast<std::size_t>(got));consumed+=static_cast<std::uint64_t>(got);
    }
    Stamp after={};
    const bool same_object=Snapshot(a,f,&after) && after.source==before.source && after.member==before.member;
    if(result==Result::Succeeded && (!same_object || !SameDefinition(before,after) || after.cursor!=consumed))
        result=Result::Changed;
    if(read_started) {
        // Never call a saved native pointer after ownership was lost. Such a
        // failure is not a restored cursor and cannot publish a digest.
        const bool restored=same_object && a.authorize(a.user,f) &&
            (a.seek_file?a.seek_file(a.user,f.file,0):NativeSeek(a.filesystem_base,f.file,0));
        Stamp restored_stamp={};
        out->cursor_restored=restored && Snapshot(a,f,&restored_stamp) &&
            restored_stamp.source==before.source && restored_stamp.member==before.member && !restored_stamp.cursor;
        if(!out->cursor_restored)result=Result::RestoreFailed;
        else if(result==Result::Succeeded && !SameDefinition(before,restored_stamp))result=Result::Changed;
    } else out->cursor_restored=same_object && !after.cursor;
    if(result==Result::Succeeded && out->cursor_restored) {
        dluma_sha256_final(&hash,out->sha256);out->complete=true;
    }
    out->result=result;return result;
}
bool Publish(Cache* cache,std::uint32_t slot,const Digest& value)noexcept {
    if(!cache || slot>=file_lifetime::kFiles || !value.file.generation ||
       !value.file.root || value.file.kind!=file_lifetime::FileKind::Zip ||
       (value.complete && (value.result!=Result::Succeeded || !value.cursor_restored)))return false;
    while(cache->lock.test_and_set(std::memory_order_acquire))_mm_pause();
    const bool newer=cache->entries[slot].file.generation<value.file.generation;
    if(newer)cache->entries[slot]=value;
    cache->lock.clear(std::memory_order_release);return newer;
}
bool Copy(Cache* cache,const file_lifetime::FileRecord& file,Digest* out)noexcept {
    if(!cache || !out || !file.file || !file.generation || !file.root ||
       !Separate(reinterpret_cast<std::uintptr_t>(cache),sizeof(*cache),reinterpret_cast<std::uintptr_t>(out),sizeof(*out)))return false;
    *out={};bool found=false;
    while(cache->lock.test_and_set(std::memory_order_acquire))_mm_pause();
    for(const auto& entry:cache->entries) {
        const auto& key=entry.file;
        if(key.file==file.file && key.generation==file.generation && key.root==file.root && key.kind==file.kind &&
           key.source.address==file.source.address && key.source.generation==file.source.generation) {
            *out=entry;found=true;break;
        }
    }
    cache->lock.clear(std::memory_order_release);return found;
}
} // namespace
