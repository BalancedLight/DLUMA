#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>
#include "../src/fixes/custom_map_asset_isolation/NativeDetourBranch.h"
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
extern "C" bool CheckUiEntryUnwind(const unsigned char* codes,std::size_t count)noexcept{
 if(!codes||count>64)return false;
 auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!image)return false;
 const unsigned char prefix[]={0x40,0x57,0x48,0x83,0xEC,0x50,0x48,0x89,0x6C,0x24,0x68,0x4C,0x89,0x64,0x24,0x78};
 std::memcpy(image,prefix,16);std::memcpy(image+128,codes,count);
 bool ok=d::BuildComplementaryConditionalTransfer(image+16,12,reinterpret_cast<std::uintptr_t>(image)+16,reinterpret_cast<std::uintptr_t>(image)+512);
 auto* function=reinterpret_cast<RUNTIME_FUNCTION*>(image+256);*function={0,28,128};
 const bool registered=ok&&RtlAddFunctionTable(function,1,reinterpret_cast<DWORD64>(image))!=0;ok=ok&&registered;
 DWORD old=0;ok=ok&&VirtualProtect(image,4096,PAGE_EXECUTE_READ,&old)!=0;
 const DWORD pcs[]={0,2,6,11,16,22},flags[]={0,0x40};
 for(const auto pc:pcs)for(const auto flag:flags){if(!ok)break;
  alignas(16) DWORD64 stack[96]={};const auto entry=reinterpret_cast<DWORD64>(stack+64);
  stack[63]=0x77777777;stack[64]=0x12345678;stack[66]=0x55555555;stack[68]=0xCCCCCCCC;
  CONTEXT c={};c.Rip=reinterpret_cast<DWORD64>(image)+pc;c.Rsp=entry-(pc>=2?8:0)-(pc>=6?80:0);c.EFlags=flag;
  c.Rdi=pc>=2?0xAA:stack[63];c.Rbp=pc>=11?0xBB:stack[66];c.R12=pc>=16?0xCC:stack[68];
  c.Xmm6.Low=0x1111;c.Xmm6.High=0x2222;c.Xmm7.Low=0x3333;c.Xmm7.High=0x4444;c.Xmm8.Low=0x5555;c.Xmm8.High=0x6666;
  PVOID handler=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(image),c.Rip,function,&c,&handler,&frame,nullptr);
  ok=c.Rip==stack[64]&&c.Rsp==entry+8&&c.Rdi==stack[63]&&c.Rbp==stack[66]&&c.R12==stack[68]&&
   c.Xmm6.Low==0x1111&&c.Xmm6.High==0x2222&&c.Xmm7.Low==0x3333&&c.Xmm7.High==0x4444&&c.Xmm8.Low==0x5555&&c.Xmm8.High==0x6666;
 }
 if(registered&&!RtlDeleteFunctionTable(function))return false;
 return VirtualFree(image,0,MEM_RELEASE)!=0&&ok;
}
