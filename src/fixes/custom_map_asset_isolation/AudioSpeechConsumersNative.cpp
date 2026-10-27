#define _HAS_EXCEPTIONS 0
#include "AudioSpeechConsumersNative.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::audio_speech_consumers {
namespace {
bool Read(const Access& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Same(const Fence& a,const Fence& b) noexcept {
    return a.audio_owner==b.audio_owner && a.speech_manager==b.speech_manager &&
        a.owner_generation==b.owner_generation && a.transaction==b.transaction &&
        a.epoch==b.epoch && a.thread==b.thread && a.sound_lock_owned==b.sound_lock_owned &&
        a.speech_gate_owned==b.speech_gate_owned && a.producers_closed==b.producers_closed && a.post_c10==b.post_c10;
}
bool ReadHeaders(const Access& a,Snapshot* s) noexcept {
    const auto owner=s->fence.audio_owner,manager=s->fence.speech_manager;
    std::uintptr_t actual_audio=0,actual_speech=0;
    if(!Read(a,a.engine_base+0xAC8D78,&actual_audio,8)||actual_audio!=owner||
       !Read(a,a.engine_base+0xACCD38,&actual_speech,8)||actual_speech!=manager||
       !Read(a,owner+7872,&s->stream_data,8)||!Read(a,owner+7880,&s->streams,4)||
       !Read(a,owner+7884,&s->stream_capacity,4)||!Read(a,owner+7904,&s->stream_head,8))return false;
    if(!manager)return true;
    return Read(a,manager+144,&s->speech_data,8)&&Read(a,manager+152,&s->active_speech,4)&&
        Read(a,manager+156,&s->speech_capacity,4)&&Read(a,manager+112,&s->cache_head,8)&&
        Read(a,manager+120,&s->cache_tail,8)&&Read(a,manager+128,&s->cached_speech,4);
}
}
Result Capture(const Access& a,Snapshot* s) noexcept {
    if(!s)return Result::InvalidArgument;
    *s={};
    if(!a.exact_image||!a.read||!a.fence||!a.engine_base||a.engine_base>UINTPTR_MAX-0xACCE00)return Result::InvalidArgument;
    if(!a.fence(a.user,&s->fence)||!s->fence.audio_owner||!s->fence.owner_generation||!s->fence.thread||
       !s->fence.sound_lock_owned||!s->fence.speech_gate_owned)return Result::FenceUnavailable;
    if(!ReadHeaders(a,s))return Result::ReadFailed;
    if(s->streams>s->stream_capacity||s->stream_capacity>65536||
       s->active_speech>s->speech_capacity||s->speech_capacity>65536||s->cached_speech>kMaximumCachedSpeech||
       (s->streams&&!s->stream_data)||(s->active_speech&&!s->speech_data)||
       static_cast<bool>(s->stream_head)!=static_cast<bool>(s->streams)||
       static_cast<bool>(s->cache_head)!=static_cast<bool>(s->cached_speech)||
       static_cast<bool>(s->cache_tail)!=static_cast<bool>(s->cached_speech))return Result::InvalidGraph;
    std::uintptr_t link=s->cache_head,previous=0;
    for(std::uint32_t i=0;i<s->cached_speech;++i){
        std::uintptr_t next=0,prior=0,data=0;
        if(link<8||!Read(a,link-8,&data,8)||!data||!Read(a,link,&next,8)||!Read(a,link+8,&prior,8))return Result::ReadFailed;
        if(prior!=previous||next==link)return Result::InvalidGraph;
        unsigned char object[72]={};
        if(!Read(a,data,object,sizeof(object)) || object[8]>1)return Result::ReadFailed;
        constexpr unsigned offsets[]={16U,32U,48U};
        for(unsigned offset: offsets) {
            std::uintptr_t text=0;std::uint32_t length=0,capacity=0;
            std::memcpy(&text,object+offset,8);std::memcpy(&length,object+offset+8,4);std::memcpy(&capacity,object+offset+12,4);
            if(length>capacity || capacity>1024U*1024U || (capacity&&!text))return Result::InvalidGraph;
            unsigned char byte=0;if(text && !Read(a,text,&byte,1))return Result::ReadFailed;
        }
        std::uintptr_t tracks=0;std::memcpy(&tracks,object,8);
        if(tracks && object[8]) {
            std::uintptr_t banks=0;std::uint32_t count=0,capacity=0;
            if(!Read(a,s->fence.speech_manager+8,&banks,8)||!Read(a,s->fence.speech_manager+16,&count,4)||
                !Read(a,s->fence.speech_manager+20,&capacity,4))return Result::ReadFailed;
            if(count>capacity || capacity>4096 || (count&&!banks) || banks>UINTPTR_MAX-static_cast<std::uintptr_t>(count)*8)return Result::InvalidGraph;
            bool found=false;
            for(std::uint32_t j=0;j<count;++j){std::uintptr_t actual=0;
                if(!Read(a,banks+static_cast<std::uintptr_t>(j)*8,&actual,8))return Result::ReadFailed;
                found|=actual==tracks;
            }
            if(!found)return Result::InvalidGraph;
        } else if(tracks) {
            std::uintptr_t table=0;
            if(!Read(a,tracks,&table,8))return Result::ReadFailed;
            if(table!=a.engine_base+0x8E62B8)return Result::InvalidGraph;
        }
        previous=link;link=next;
    }
    if(link||previous!=s->cache_tail)return Result::InvalidGraph;
    Snapshot after={};after.fence=s->fence;
    Fence fence={};
    if(!ReadHeaders(a,&after)||!a.fence(a.user,&fence))return Result::ReadFailed;
    if(!Same(s->fence,fence)||s->stream_data!=after.stream_data||s->stream_head!=after.stream_head||
       s->streams!=after.streams||s->stream_capacity!=after.stream_capacity||s->speech_data!=after.speech_data||
       s->active_speech!=after.active_speech||s->speech_capacity!=after.speech_capacity||
       s->cache_head!=after.cache_head||s->cache_tail!=after.cache_tail||s->cached_speech!=after.cached_speech)return Result::Changed;
    s->complete=true;return Result::Succeeded;
}
Result Empty(const Snapshot& s) noexcept {
    if(!s.complete)return Result::InvalidArgument;
    if(s.streams||s.stream_head)return Result::UserStreams;
    if(s.active_speech)return Result::ActiveSpeech;
    return Result::Succeeded;
}
bool ValidateNativeTargets(const Access& a) noexcept {
    if(!a.exact_image||!a.engine_base||!a.read)return false;
    constexpr std::uintptr_t targets[]={kClearCachedSpeechRva,kUserStreamLookupRva};
    constexpr unsigned char prefixes[][16]={
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48},
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}};
    unsigned char bytes[16]={};
    for(unsigned i=0;i<2;++i)if(!Read(a,a.engine_base+targets[i],bytes,sizeof(bytes))||
        std::memcmp(bytes,prefixes[i],sizeof(bytes)))return false;
    return true;
}
Result ClearCached(const Access& a,Snapshot* result) noexcept {
    Snapshot before={};const auto capture=Capture(a,&before);
    if(capture!=Result::Succeeded)return capture;
    const auto empty=Empty(before);if(empty!=Result::Succeeded)return empty;
    const auto& f=before.fence;
    if(!f.transaction||!f.epoch||!f.producers_closed||!f.post_c10)return Result::FenceUnavailable;
    if(!ValidateNativeTargets(a))return Result::NativeFailure;
    if(before.cached_speech){
        // This native owner skips destructing SPB-borrowed tracks, releases
        // owned standalone tracks, and removes every cached CSpeechData node.
        __try { reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(a.engine_base+kClearCachedSpeechRva)(f.speech_manager+112); }
        __except(1){return Result::NativeFailure;}
    }
    Snapshot after={};const auto captured=Capture(a,&after);
    if(captured!=Result::Succeeded)return captured;
    if(!Same(before.fence,after.fence)||after.cached_speech||Empty(after)!=Result::Succeeded)return Result::Changed;
    if(result)*result=after;
    return Result::Succeeded;
}
} // namespace
