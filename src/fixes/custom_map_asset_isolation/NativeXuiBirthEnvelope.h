#ifndef DLUMA_NATIVE_XUI_BIRTH_ENVELOPE_H
#define DLUMA_NATIVE_XUI_BIRTH_ENVELOPE_H
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::xui_birth {
constexpr std::uint8_t kOriginal[16]={0xff,0x90,0x70,0x03,0,0,0xeb,0x06,0xff,0x90,0x68,0x03,0,0,0x8b,0xcf};
struct Envelope {std::uint8_t patched[16],thunks[32];};
inline bool Build(const std::uint8_t* original,std::uintptr_t site,std::uintptr_t relay,
                  std::uintptr_t runtime,std::uintptr_t editor,Envelope* output)noexcept
{
    if(!original||!output||!site||(site&15)||!relay||!runtime||!editor||
       site>UINTPTR_MAX-16||relay>UINTPTR_MAX-32||std::memcmp(original,kOriginal,16))return false;
    Envelope next={};std::memcpy(next.patched,original,16);std::memset(next.thunks,0x90,32);
    for(unsigned branch=0;branch<2;++branch){const auto offset=branch*8U;const auto after=site+offset+5;
        const auto destination=relay+branch*16;std::int32_t relative=0;
        if(destination>=after){auto distance=destination-after;if(distance>INT32_MAX)return false;relative=static_cast<std::int32_t>(distance);}
        else {auto distance=after-destination;if(distance>static_cast<std::uintptr_t>(INT32_MAX)+1)return false;
            relative=distance==static_cast<std::uintptr_t>(INT32_MAX)+1?INT32_MIN:-static_cast<std::int32_t>(distance);}
        next.patched[offset]=0xe8;std::memcpy(next.patched+offset+1,&relative,4);next.patched[offset+5]=0x90;
        auto* thunk=next.thunks+branch*16;thunk[0]=0xff;thunk[1]=0x25;std::memset(thunk+2,0,4);
        const auto target=branch?runtime:editor;std::memcpy(thunk+6,&target,8);
    }
    *output=next;return true;
}
}
#endif
