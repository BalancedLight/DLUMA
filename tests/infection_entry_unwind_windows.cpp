#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>
#include "../src/fixes/custom_map_asset_isolation/NativeDetourBranch.h"
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
extern "C" bool CheckInfectionEntryUnwind(const unsigned char* codes,std::size_t count,const unsigned char* relay,std::size_t code_size) noexcept
{
    if(!codes||count>64||!relay||code_size!=29)return false;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return false;
    std::memset(image,0x90,64);std::memcpy(image,relay,code_size);std::memcpy(image+128,codes,count);
    if(!d::BuildComplementaryConditionalTransfer(image+17,12,reinterpret_cast<std::uintptr_t>(image)+17,reinterpret_cast<std::uintptr_t>(image)+512)) {
        VirtualFree(image,0,MEM_RELEASE);return false;
    }
    auto* function=reinterpret_cast<RUNTIME_FUNCTION*>(image+256);*function={0,29,128};
    bool registered=RtlAddFunctionTable(function,1,reinterpret_cast<DWORD64>(image))!=0;
    bool ok=registered;DWORD old=0;
    ok=ok&&VirtualProtect(image,4096,PAGE_EXECUTE_READ,&old)!=0;
    DWORD64 found_base=0;
    if(ok)ok=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(image)+17,&found_base,nullptr)==function&&found_base==reinterpret_cast<DWORD64>(image);
    const DWORD offsets[]={0,3,4,6,9,13,17,23};
    const DWORD flag_values[]={0,0x40};
    for(const DWORD offset:offsets){
      for(const DWORD flags:flag_values){
        if(!ok)break;
        alignas(16) DWORD64 stack[96]={};auto entry=reinterpret_cast<DWORD64>(stack+64);
        stack[62]=0x14141414;stack[63]=0x55555555;stack[64]=0x12345678;stack[65]=0x33333333;
        CONTEXT context={};context.Rsp=entry-(offset>=4?8:0)-(offset>=6?8:0)-(offset>=13?120:0);
        context.Rip=reinterpret_cast<DWORD64>(image)+offset;
        context.EFlags=flags;
        context.Rbp=offset>=4?0xAA:stack[63];context.R14=offset>=6?0xBB:stack[62];context.Rbx=offset>=17?0xCC:stack[65];
        PVOID handler=nullptr;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(image),context.Rip,function,&context,&handler,&frame,nullptr);
        ok=context.Rip==stack[64]&&context.Rsp==entry+8&&context.Rbp==stack[63]&&context.R14==stack[62]&&context.Rbx==stack[65];
      }
    }
    if(registered&&!RtlDeleteFunctionTable(function))return false; // retain until process exit on unregister failure
    return VirtualFree(image,0,MEM_RELEASE)!=0&&ok;
}
extern "C" bool CheckComplementaryTransferExecution() noexcept
{
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return false;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const unsigned char compare[]={0x83,0xF9,0};
    std::memcpy(image,compare,3);
    bool ok=d::BuildComplementaryConditionalTransfer(image+3,12,base+3,base+64);
    const unsigned char flags_return[]={0x9C,0x58,0xC3};std::memcpy(image+64,flags_return,3);
    image[128]=0x49;image[129]=0xBB;const std::uint64_t marker=0xA1B2C3D4E5F60718ULL;
    std::memcpy(image+130,&marker,8);std::memcpy(image+138,compare,3);
    ok=ok&&d::BuildComplementaryConditionalTransfer(image+141,12,base+141,base+192);
    const unsigned char register_return[]={0x4C,0x89,0xD8,0xC3};std::memcpy(image+192,register_return,4);
    DWORD old=0;ok=ok&&VirtualProtect(image,4096,PAGE_EXECUTE_READ,&old)!=0&&FlushInstructionCache(GetCurrentProcess(),image,4096)!=0;
    if(ok){
        using Function=std::uint64_t(__fastcall*)(int);
        auto flags=reinterpret_cast<Function>(image),reg=reinterpret_cast<Function>(image+128);
        ok=(flags(0)&0x40)!=0&&(flags(1)&0x40)==0&&reg(0)==marker&&reg(1)==marker;
    }
    return VirtualFree(image,0,MEM_RELEASE)!=0&&ok;
}
