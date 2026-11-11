#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../src/game/dlw_xui_document_native.h"
static DWORD g_main;
static uint32_t g_map=3;
static int g_rebuilds;
static HANDLE g_entered,g_continue;
static volatile LONG g_hold;
extern "C" void dluma_sha256_bytes_hex(const void*,size_t,char* out){if(InterlockedExchange(&g_hold,0)){SetEvent(g_entered);assert(WaitForSingleObject(g_continue,5000)==WAIT_OBJECT_0);}std::memset(out,'a',64);out[64]=0;}
extern "C" int dluma_xui_document_native_ready(){assert(GetCurrentThreadId()==g_main);return 1;}
extern "C" int dluma_xui_document_native_configured(){return 1;}
extern "C" int dluma_xui_document_native_bind(const DlumaXuiDocumentIdentity*){assert(GetCurrentThreadId()==g_main);return 1;}
extern "C" int dluma_xui_document_native_current(void*,const DlumaXuiDocumentIdentity* i){assert(GetCurrentThreadId()==g_main);return i->map_generation==g_map;}
extern "C" int dluma_xui_document_native_snapshot(void*,DlumaXuiDocumentSnapshot* s){assert(GetCurrentThreadId()==g_main);*s={};return 1;}
extern "C" DlumaXuiDocumentRebuildResult dluma_xui_document_native_rebuild(void*,const DlumaXuiDocumentIdentity*,const DlumaXuiDocumentSource*,const DlumaXuiDocumentSnapshot*){assert(GetCurrentThreadId()==g_main);++g_rebuilds;return DLUMA_XUI_DOCUMENT_REBUILD_SUCCEEDED;}
static void Write(const char* p,const char* s){FILE* f=nullptr;assert(!fopen_s(&f,p,"wb"));assert(std::fwrite(s,1,std::strlen(s),f)==std::strlen(s));assert(!std::fclose(f));}
static DlumaXuiDocumentStatus Await(uint64_t owner,uint32_t map,uint64_t token){DlumaXuiDocumentStatus s={};for(unsigned i=0;i<5000;++i){assert(dluma_xui_document_service_status(owner,1,map,token,&s)==0);if(s.state!=DLUMA_XUI_DOCUMENT_PREPARING)return s;Sleep(1);}assert(false);return s;}
static void Writable(){HANDLE h=CreateFileW(L"project\\data\\original.xui",GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(h!=INVALID_HANDLE_VALUE);assert(CloseHandle(h));}
int main(){
 g_main=GetCurrentThreadId();g_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);g_continue=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(g_entered&&g_continue);
 assert(CreateDirectoryW(L"project",nullptr));assert(CreateDirectoryW(L"project\\data",nullptr));
 const char* xml="<XuiCanvas><Properties><Width>1280</Width></Properties><XuiScene><Properties><Id>HUD_DI</Id><ClassOverride>HUDScreenBase</ClassOverride></Properties></XuiScene></XuiCanvas>";
 Write("project/data/original.xui",xml);Write("project/data/replacement.xui",xml);WCHAR project[2048];assert(GetFullPathNameW(L"project",2048,project,nullptr));uint64_t token=0;
 assert(dluma_xui_document_service_begin(7,1,3,project,"original.xui","replacement.xui",&token)==0&&token);assert(Await(7,3,token).state==DLUMA_XUI_DOCUMENT_READY);
 dluma_xui_document_service_pump();DlumaXuiDocumentStatus s={};assert(dluma_xui_document_service_status(7,1,3,token,&s)==0&&s.state==DLUMA_XUI_DOCUMENT_APPLIED);assert(g_rebuilds==1);
 assert(dluma_xui_document_service_restore(8,1,3,token)==DLUMA_XUI_DOCUMENT_INVALID);assert(dluma_xui_document_service_restore(7,1,3,token)==0);dluma_xui_document_service_pump();assert(dluma_xui_document_service_status(7,1,3,token,&s)==0&&s.state==DLUMA_XUI_DOCUMENT_RESTORED);Writable();
 // Actual native map destruction can race the worker's immutable preflight.
 InterlockedExchange(&g_hold,1);assert(dluma_xui_document_service_begin(8,1,3,project,"original.xui","replacement.xui",&token)==0);assert(WaitForSingleObject(g_entered,5000)==WAIT_OBJECT_0);
 dluma_xui_document_service_map_destroyed(3);g_map=4;SetEvent(g_continue);(void)Await(8,3,token);for(unsigned i=0;i<20;++i){dluma_xui_document_service_pump();Sleep(1);}Writable();assert(g_rebuilds==2);
 assert(dluma_xui_document_service_shutdown());assert(CloseHandle(g_entered)&&CloseHandle(g_continue));std::puts("XUI document service passed");
}
