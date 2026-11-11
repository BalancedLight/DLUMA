#include "../src/fixes/custom_map_asset_isolation/NativeXuiBirthEnvelope.h"
#include <cassert>
#include <cstdio>
namespace birth=dluma::fixes::custom_map_asset_isolation::xui_birth;
int main(){
    birth::Envelope out={};const std::uintptr_t site=0x1802ac8c0,relay=0x180300000;
    assert(birth::Build(birth::kOriginal,site,relay,0x7ff000001000,0x7ff000002000,&out));
    assert(out.patched[0]==0xe8&&out.patched[5]==0x90&&out.patched[6]==0xeb&&out.patched[7]==6);
    assert(out.patched[8]==0xe8&&out.patched[13]==0x90&&out.patched[14]==0x8b&&out.patched[15]==0xcf);
    std::int32_t a=0,b=0;std::memcpy(&a,out.patched+1,4);std::memcpy(&b,out.patched+9,4);
    assert(static_cast<std::uintptr_t>(static_cast<std::int64_t>(site+5)+a)==relay);
    assert(static_cast<std::uintptr_t>(static_cast<std::int64_t>(site+13)+b)==relay+16);
    std::uintptr_t target=0;std::memcpy(&target,out.thunks+6,8);assert(target==0x7ff000002000);
    std::memcpy(&target,out.thunks+22,8);assert(target==0x7ff000001000);
    auto old=out;assert(!birth::Build(birth::kOriginal,site+1,relay,1,2,&out)&&!std::memcmp(&old,&out,sizeof(out)));
    assert(!birth::Build(birth::kOriginal,site,0x700000000000,1,2,&out));
    auto bad=birth::kOriginal[0];(void)bad;std::puts("XUI birth call envelope passed");
}
