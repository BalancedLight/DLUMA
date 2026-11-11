#include "../src/game/dlw_player_infection_detour.h"
#include "../src/fixes/custom_map_asset_isolation/NativeDetourBranch.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
namespace p=dluma::game::infection;
extern "C" bool CheckInfectionEntryUnwind(const unsigned char*,std::size_t,const unsigned char*,std::size_t) noexcept;
extern "C" bool CheckComplementaryTransferExecution() noexcept;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(34086912);
    std::size_t reads=0;
    static constexpr std::uintptr_t base=0x10000000;
    static constexpr std::uint32_t target=0xB53E60,directory=0x1E16000,count=98414;
    void Put(std::size_t at,std::uint32_t value){std::memcpy(bytes.data()+at,&value,4);}
    static bool Read(void* user,std::uintptr_t at,void* out,std::size_t size)noexcept{
        auto& f=*static_cast<Fixture*>(user);++f.reads;
        if(at<base||at-base>f.bytes.size()||size>f.bytes.size()-(at-base))return false;
        std::memcpy(out,f.bytes.data()+(at-base),size);return true;
    }
    d::EntryUnwindImage Image(){return {base,static_cast<std::uint32_t>(bytes.size()),131072,Read,this};}
    Fixture(){
        Put(0x3C,0x100);bytes[0x118]=0x0B;bytes[0x119]=2;
        Put(0x100+24+112+24,directory);Put(0x100+24+112+28,count*12);
        constexpr std::uint32_t position=count/2;
        for(std::uint32_t i=0;i<count;++i){
            const auto begin=i<position?target-32*(position-i):target+0x100+32*(i-position);
            Put(directory+12ULL*i,begin);Put(directory+12ULL*i+4,begin+16);Put(directory+12ULL*i+8,0x1000);
        }
        const std::uint32_t entries[]={target,target+13,0x1C5B43C,target+13,target+0xAA,0x1C5B41C};
        std::memcpy(bytes.data()+directory+12ULL*position,entries,sizeof(entries));
        const unsigned char prefix[]={0x48,0x8B,0xC4,0x55,0x41,0x56,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x78,0x48,0x89,0x58,0x08};
        std::memcpy(bytes.data()+target,prefix,sizeof(prefix));
        const unsigned char first[]={1,13,3,0,13,0xE2,6,0xE0,4,0x50,0,0};
        const unsigned char second[]={0x21,16,8,0,16,0xD4,14,0,12,0xC4,21,0,8,0x74,20,0,4,0x34,18,0};
        std::memcpy(bytes.data()+0x1C5B43C,first,sizeof(first));
        std::memcpy(bytes.data()+0x1C5B41C,second,sizeof(second));
        std::memcpy(bytes.data()+0x1C5B430,entries,12);
    }
};
int main(){
    Fixture f;auto image=f.Image();unsigned char unwind[64]={};std::size_t size=0;
    const unsigned char expected[]={1,17,5,0,17,0x34,18,0,13,0xE2,6,0xE0,4,0x50,0,0};
    CHECK(d::CopyEntryUnwind(image,Fixture::target,17,unwind,sizeof(unwind),&size));
    CHECK(size==sizeof(expected)&&!std::memcmp(unwind,expected,size));
    CHECK(f.reads<128);
    p::RespawnDetourPlan plan={},before={};
    constexpr std::uintptr_t relay_address=Fixture::base+0x4000000;
    CHECK(p::PrepareRespawnDetourPlan(image,p::PlanHost::PlayerStandalone,true,0x200000000ULL,relay_address,&plan));
    CHECK(plan.code_size==29&&plan.unwind_size==sizeof(expected));
    CHECK(CheckInfectionEntryUnwind(plan.unwind,plan.unwind_size,plan.relay,plan.code_size));
    CHECK(CheckComplementaryTransferExecution());
    CHECK(!std::memcmp(plan.unwind,expected,sizeof(expected)));
    CHECK(plan.patch[0]==0xFF&&plan.patch[1]==0x25&&plan.patch[14]==0x90&&plan.patch[15]==0x90);
    CHECK(!std::memcmp(plan.relay,plan.original,17));
    std::uintptr_t value=0;std::memcpy(&value,plan.patch+6,8);CHECK(value==0x200000000ULL);
    CHECK(plan.relay[17]==0x0F&&plan.relay[18]==0x84&&plan.relay[23]==0x0F&&plan.relay[24]==0x85);
    std::int32_t displacement=0;
    std::memcpy(&displacement,plan.relay+19,4);CHECK(relay_address+23+displacement==Fixture::base+Fixture::target+17);
    std::memcpy(&displacement,plan.relay+25,4);CHECK(relay_address+29+displacement==Fixture::base+Fixture::target+17);
    before=plan;
    CHECK(!p::PrepareRespawnDetourPlan(image,p::PlanHost::MainGame,true,1,2,&plan));
    CHECK(!std::memcmp(&plan,&before,sizeof(plan)));
    CHECK(!p::PrepareRespawnDetourPlan(image,p::PlanHost::PlayerStandalone,false,1,2,&plan));
    CHECK(!p::PrepareRespawnDetourPlan(image,p::PlanHost::Unknown,true,1,2,&plan));
    CHECK(!p::PrepareRespawnDetourPlan(image,p::PlanHost::PlayerStandalone,true,1,0x300000000ULL,&plan));
    unsigned char transfer[12];std::memset(transfer,0xA5,sizeof(transfer));
    CHECK(!d::BuildComplementaryConditionalTransfer(transfer,11,0x100000000ULL,0x100000100ULL));
    CHECK(!d::BuildComplementaryConditionalTransfer(transfer,12,0x100000000ULL,0x180000006ULL));
    for(auto b:transfer)CHECK(b==0xA5);
    image.maximum_functions=65536;size=99;
    CHECK(!d::CopyEntryUnwind(image,Fixture::target,17,unwind,sizeof(unwind),&size)&&!size);
    image=f.Image();
    f.bytes[0x1C5B41C]=0x11;size=99;
    CHECK(!d::CopyEntryUnwind(image,Fixture::target,17,unwind,sizeof(unwind),&size)&&!size);
    f.bytes[0x1C5B41C]=0x21;f.Put(0x1C5B430,Fixture::target+1);
    CHECK(!d::CopyEntryUnwind(image,Fixture::target,17,unwind,sizeof(unwind),&size));
    f.Put(0x1C5B430,Fixture::target);
    CHECK(!d::CopyEntryUnwind(image,Fixture::target+1,17,unwind,sizeof(unwind),&size));
    std::memset(unwind,0xA5,sizeof(unwind));size=99;
    CHECK(!d::CopyEntryUnwind(image,Fixture::target,17,unwind,15,&size)&&!size);
    for(auto b:unwind)CHECK(b==0xA5);
    f.bytes[Fixture::target]=0x90;
    CHECK(!p::PrepareRespawnDetourPlan(image,p::PlanHost::PlayerStandalone,true,1,2,&plan));
    CHECK(!std::memcmp(&plan,&before,sizeof(plan)));
    std::puts("PASS: inactive infection detour planner");return 0;
}
