#include "dlw_player_infection_detour.h"
#include "../fixes/custom_map_asset_isolation/NativeDetourBranch.h"
namespace dluma::game::infection {
namespace d=fixes::custom_map_asset_isolation::detour;
namespace {
constexpr unsigned char prefix[]={0x48,0x8B,0xC4,0x55,0x41,0x56,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x78,0x48,0x89,0x58,0x08};
constexpr unsigned char first[]={1,13,3,0,13,0xE2,6,0xE0,4,0x50,0,0};
constexpr unsigned char second[]={0x21,16,8,0,16,0xD4,14,0,12,0xC4,21,0,8,0x74,20,0,4,0x34,18,0,0x60,0x3E,0xB5,0,0x6D,0x3E,0xB5,0,0x3C,0xB4,0xC5,1};
void Jump(unsigned char* out,std::uintptr_t destination)noexcept
{out[0]=0xFF;out[1]=0x25;std::memset(out+2,0,4);std::memcpy(out+6,&destination,8);}
}
bool PrepareRespawnDetourPlan(const d::EntryUnwindImage& image,PlanHost host,bool exact_profile,
    std::uintptr_t hook,std::uintptr_t relay,RespawnDetourPlan* out)noexcept
{
    constexpr std::uint32_t rva=0xB53E60;
    if(!out||!exact_profile||(host!=PlanHost::PlayerStandalone&&host!=PlanHost::EditorPlaytest)||
        image.size!=34086912||!image.base||image.base>UINTPTR_MAX-image.size||
        ((image.base+rva)&15)||!hook||!relay||relay>UINTPTR_MAX-29)return false;
    RespawnDetourPlan plan={};unsigned char a[sizeof(first)]={},b[sizeof(second)]={};
    if(!d::EntryUnwindRead(image,rva,plan.original,sizeof(plan.original))||
        std::memcmp(plan.original,prefix,sizeof(prefix))||
        !d::EntryUnwindRead(image,0x1C5B43C,a,sizeof(a))||std::memcmp(a,first,sizeof(a))||
        !d::EntryUnwindRead(image,0x1C5B41C,b,sizeof(b))||std::memcmp(b,second,sizeof(b)))return false;
    std::size_t unwind_size=0;
    if(!d::CopyEntryUnwind(image,rva,17,plan.unwind,sizeof(plan.unwind),&unwind_size))return false;
    std::memset(plan.patch,0x90,sizeof(plan.patch));Jump(plan.patch,hook);
    std::memcpy(plan.relay,plan.original,17);
    if(!d::BuildComplementaryConditionalTransfer(plan.relay+17,12,relay+17,image.base+rva+17))return false;
    plan.code_size=29;plan.unwind_size=static_cast<std::uint32_t>(unwind_size);
    *out=plan;return true;
}
}
