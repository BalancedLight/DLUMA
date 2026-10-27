#ifndef DLUMA_FIXES_NATIVE_DETOUR_BRANCH_H
#define DLUMA_FIXES_NATIVE_DETOUR_BRANCH_H
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::detour {
// The hook catalog must qualify an instruction boundary and a trailing short
// Jcc whose destination is outside the overwritten entry. Widen only that
// explicit branch; this helper is not a general instruction decoder.
inline bool ExpandTrailingConditionalBranch(std::uint8_t* code,std::size_t capacity,
    std::uintptr_t source,std::uintptr_t relay,std::size_t stolen,std::size_t branch,
    std::size_t* size) noexcept
{
    if(!code||!size||!source||!relay||!branch||branch>stolen||stolen-branch!=2||
        stolen>capacity||capacity-stolen<4||source>UINTPTR_MAX-stolen||
        relay>UINTPTR_MAX-(stolen+4)||code[branch]<0x70||code[branch]>0x7f)return false;
    const auto displacement=static_cast<std::int8_t>(code[branch+1]);
    const auto origin=source+stolen;
    if(displacement<0&&origin<static_cast<unsigned>(-static_cast<int>(displacement)))return false;
    if(displacement>0&&origin>UINTPTR_MAX-static_cast<unsigned>(displacement))return false;
    const auto destination=displacement<0?origin-static_cast<unsigned>(-static_cast<int>(displacement)):
        origin+static_cast<unsigned>(displacement);
    // Internal targets need instruction-by-instruction remapping, which this
    // deliberately bounded relocation does not authorize.
    if(destination>=source&&destination<source+stolen)return false;
    const auto after=relay+stolen+4;
    std::int32_t relative=0;
    if(destination>=after){if(destination-after>INT32_MAX)return false;relative=static_cast<std::int32_t>(destination-after);}
    else {const auto distance=after-destination;if(distance>static_cast<std::uintptr_t>(INT32_MAX)+1)return false;
        relative=distance==static_cast<std::uintptr_t>(INT32_MAX)+1?INT32_MIN:-static_cast<std::int32_t>(distance);}
    const auto condition=static_cast<std::uint8_t>(code[branch]&0x0f);
    code[branch]=0x0f;code[branch+1]=static_cast<std::uint8_t>(0x80|condition);
    std::memcpy(code+branch+2,&relative,sizeof(relative));*size=stolen+4;return true;
}
}
#endif
