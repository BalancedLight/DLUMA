#include "fixes/custom_map_asset_isolation/AudioSpeechConsumersNative.h"
#include <cstdio>
#include <cstring>
namespace speech=dluma::fixes::custom_map_asset_isolation::audio_speech_consumers;
namespace {
constexpr std::uintptr_t base=0x10000000,owner=0x20000000,manager=0x30000000,node=0x40000000,data=0x50000000;
struct Fake {
    unsigned char audio[8000]={},speech[192]={},cache[32]={},object[72]={};
    bool sound=true,gate=true,closed=true,post=true,change=false;
    unsigned fence_reads=0;
};
template<class T> void Put(unsigned char* p,std::size_t offset,T value){std::memcpy(p+offset,&value,sizeof(value));}
bool Read(void* user,std::uintptr_t at,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fake*>(user);
    const auto copy=[&](std::uintptr_t start,const unsigned char* bytes,std::size_t size) noexcept {
        if(at<start||at-start>size||n>size-static_cast<std::size_t>(at-start))return false;
        std::memcpy(out,bytes+(at-start),n);return true;
    };
    if(at==base+0xAC8D78 && n==8){std::memcpy(out,&owner,8);return true;}
    if(at==base+0xACCD38 && n==8){std::memcpy(out,&manager,8);return true;}
    constexpr unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48};
    if((at==base+speech::kClearCachedSpeechRva || at==base+speech::kUserStreamLookupRva) && n==sizeof(prefix)){
        std::memcpy(out,prefix,n);return true;
    }
    return copy(owner,f.audio,sizeof(f.audio))||copy(manager,f.speech,sizeof(f.speech))||
        copy(node,f.cache,sizeof(f.cache))||copy(data,f.object,sizeof(f.object));
}
bool Fence(void* user,speech::Fence* out) noexcept {
    auto& f=*static_cast<Fake*>(user);++f.fence_reads;
    *out={owner,manager,7+(f.change && f.fence_reads>1?1ULL:0ULL),8,9,10,f.sound,f.gate,f.closed,f.post};return true;
}
speech::Access Access(Fake& f){return {base,&Read,&Fence,&f,true};}
void OneCache(Fake& f){Put(f.speech,112,node+8);Put(f.speech,120,node+8);Put(f.speech,128,std::uint32_t{1});Put(f.cache,0,data);}
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
bool EmptyOwners(){Fake f;speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::Succeeded);CHECK(speech::Empty(s)==speech::Result::Succeeded);return true;}
bool GatesRequired(){Fake f;speech::Snapshot s={};f.gate=false;CHECK(speech::Capture(Access(f),&s)==speech::Result::FenceUnavailable);f.gate=true;f.sound=false;CHECK(speech::Capture(Access(f),&s)==speech::Result::FenceUnavailable);return true;}
bool LiveUserStreamBlocks(){Fake f;Put(f.audio,7872,node);Put(f.audio,7880,std::uint32_t{1});Put(f.audio,7884,std::uint32_t{1});Put(f.audio,7904,node);speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::Succeeded);CHECK(speech::Empty(s)==speech::Result::UserStreams);CHECK(speech::ClearCached(Access(f),&s)==speech::Result::UserStreams);return true;}
bool ActiveSpeechBlocks(){Fake f;Put(f.speech,144,node);Put(f.speech,152,std::uint32_t{1});Put(f.speech,156,std::uint32_t{1});speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::Succeeded);CHECK(speech::Empty(s)==speech::Result::ActiveSpeech);return true;}
bool CacheListObserved(){Fake f;OneCache(f);speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::Succeeded);CHECK(s.cached_speech==1);CHECK(speech::Empty(s)==speech::Result::Succeeded);return true;}
bool CyclicCacheRejected(){Fake f;OneCache(f);Put(f.cache,8,node+8);speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::InvalidGraph);return true;}
bool StaleGenerationRejected(){Fake f;f.change=true;speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::Changed);return true;}
bool BorrowedTracksMustBelongToBank(){Fake f;OneCache(f);Put(f.object,0,std::uintptr_t{0x60000000});f.object[8]=1;speech::Snapshot s={};CHECK(speech::Capture(Access(f),&s)==speech::Result::InvalidGraph);return true;}
bool NoMutationWithoutPostExit(){Fake f;OneCache(f);f.post=false;speech::Snapshot s={};CHECK(speech::ClearCached(Access(f),&s)==speech::Result::FenceUnavailable);f.post=true;f.closed=false;CHECK(speech::ClearCached(Access(f),&s)==speech::Result::FenceUnavailable);return true;}
bool EmptyResetRechecksActualFence(){Fake f;speech::Snapshot s={};CHECK(speech::ClearCached(Access(f),&s)==speech::Result::Succeeded);CHECK(s.complete && s.cached_speech==0);return true;}
}
int main(){struct Case{const char* name;bool(*run)();};const Case cases[]={
    {"empty owners",EmptyOwners},{"actual gates",GatesRequired},{"live user stream",LiveUserStreamBlocks},
    {"active speech",ActiveSpeechBlocks},{"cache list",CacheListObserved},{"cyclic cache",CyclicCacheRejected},
    {"owner generation",StaleGenerationRejected},{"borrowed tracks",BorrowedTracksMustBelongToBank},
    {"post-exit authority",NoMutationWithoutPostExit},{"empty reset fence",EmptyResetRechecksActualFence}};
    for(const auto& c:cases){if(!c.run())return 1;std::printf("PASS %s\n",c.name);}return 0;
}
