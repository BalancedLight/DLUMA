#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstring>

// Exercise the OS unwinder on a synthetic interrupted relay. This verifies
// stack recovery and preserved registers without executing a game function.
extern "C" bool CheckRendererRelayUnwind(const unsigned char* codes,std::size_t bytes) noexcept
{
    if(!codes||bytes>64)return false;
    alignas(16) unsigned char image[256];std::memset(image,0x90,sizeof(image));
    std::memcpy(image+128,codes,bytes);RUNTIME_FUNCTION function={0,64,128};
    const DWORD offsets[]={11,15,19};
    for(const DWORD offset:offsets){
        alignas(16) DWORD64 stack[32]={};stack[10]=0xAA551234;stack[11]=0x12345678;stack[15]=0xCC553456;
        CONTEXT context={};context.Rsp=reinterpret_cast<DWORD64>(stack);context.Rip=reinterpret_cast<DWORD64>(image)+offset;
        context.Rbx=0x11;context.Rbp=0x22;PVOID handler=nullptr;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(image),context.Rip,&function,&context,&handler,&frame,nullptr);
        if(context.Rip!=stack[11]||context.Rsp!=reinterpret_cast<DWORD64>(stack+12)||
           context.Rbx!=(offset>=15?stack[15]:0x11)||context.Rbp!=(offset>=19?stack[10]:0x22))return false;
    }
    return true;
}
