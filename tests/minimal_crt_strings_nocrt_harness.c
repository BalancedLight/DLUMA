#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <stddef.h>

int strcmp(const char*,const char*);
int _stricmp(const char*,const char*);
int wcsncmp(const WCHAR*,const WCHAR*,size_t);
typedef int (__cdecl *Narrow)(const char*,const char*);
typedef int (__cdecl *Wide)(const WCHAR*,const WCHAR*,size_t);

void entry(void)
{
    Narrow volatile exact=strcmp,ascii=_stricmp;
    Wide volatile bounded=wcsncmp;
    SYSTEM_INFO system;
    BYTE* memory;
    WCHAR* edge;
    DWORD old=0;
    const unsigned char high[]={0x80,0},low[]={0x7f,0};
    const WCHAR upper[]={0xffff,0},lower[]={0x7fff,0};
    if(exact("","")||exact("HUD.xui","HUD.xui")||exact("a","aa")>=0||exact("b","a")<=0||exact((const char*)high,(const char*)low)<=0)ExitProcess(1);
    if(ascii("HUD.XUI","hud.xui")||ascii("A/z.XuI","a/Z.xui")||ascii("a","aa")>=0||ascii("b","a")<=0)ExitProcess(2);
    if(bounded((const WCHAR*)0,(const WCHAR*)0,0)||bounded(L"ab",L"ac",1)||bounded(L"ab",L"ac",2)>=0||
       bounded(L"ab",L"a",3)<=0||bounded(L"",L"",8)||bounded(upper,lower,1)<=0)ExitProcess(3);
    GetSystemInfo(&system);
    memory=(BYTE*)VirtualAlloc(0,(SIZE_T)system.dwPageSize*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!memory||!VirtualProtect(memory+system.dwPageSize,system.dwPageSize,PAGE_NOACCESS,&old))ExitProcess(4);
    edge=(WCHAR*)(memory+system.dwPageSize-2*sizeof(WCHAR));edge[0]=L'a';edge[1]=L'b';
    /* No terminator exists before the guard page: count, not a future NUL,
     * must bound the reader. */
    if(bounded(edge,L"abX",2)||bounded(edge,L"acX",2)>=0)ExitProcess(5);
    if(!VirtualFree(memory,0,MEM_RELEASE))ExitProcess(6);
    ExitProcess(0);
}
