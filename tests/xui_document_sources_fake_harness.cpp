#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/game/dlw_xui_document_sources.h"
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" void dluma_sha256_bytes_hex(const void*,size_t,char* out){std::memset(out,'a',64);out[64]=0;}
static void Write(const char* p,const char* s){FILE* f=nullptr;assert(fopen_s(&f,p,"wb")==0);assert(std::fwrite(s,1,std::strlen(s),f)==std::strlen(s));assert(std::fclose(f)==0);}
int main(int argc,char** argv){
 if(argc==3){DlumaXuiDocumentSource a={},b={};assert(dluma_xui_document_source_acquire(nullptr,argv[1],&a));assert(dluma_xui_document_source_acquire(nullptr,argv[2],&b));assert(dluma_xui_document_source_compatible(nullptr,&a,&b));assert(dluma_xui_document_source_release(nullptr,&b));assert(dluma_xui_document_source_release(nullptr,&a));std::puts("XUI document sources passed");return 0;}
 const char* original="<XuiCanvas><Properties><Width>1280</Width></Properties><XuiScene><Properties><Id>HUD_DI</Id><ClassOverride>HUDScreenBase</ClassOverride></Properties><AdvGroup><Properties><Id>Stock</Id><ClassOverride>HudBossHpBar</ClassOverride></Properties></AdvGroup></XuiScene></XuiCanvas>";
 const char* replacement="<XuiCanvas><Properties><Width>1280</Width></Properties><XuiScene><Properties><Id>HUD_DI</Id><ClassOverride>HUDScreenBase</ClassOverride></Properties><AdvGroup><Properties><Id>Stock</Id><ClassOverride>HudBossHpBar</ClassOverride></Properties></AdvGroup><MyText><Properties><Id>Effect</Id><ClassOverride>IUIText</ClassOverride></Properties></MyText></XuiScene></XuiCanvas>";
 Write("original.xui",original);Write("replacement.xui",replacement);
 char root[MAX_PATH];assert(GetCurrentDirectoryA(MAX_PATH,root));char a[MAX_PATH],b[MAX_PATH];assert(sprintf_s(a,"%s\\original.xui",root)>0);assert(sprintf_s(b,"%s\\replacement.xui",root)>0);
 DlumaXuiDocumentSource first={},second={};assert(dluma_xui_document_source_acquire(nullptr,a,&first));assert(dluma_xui_document_source_acquire(nullptr,b,&second));assert(dluma_xui_document_source_compatible(nullptr,&first,&second));
 HANDLE w=CreateFileA(a,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);assert(w==INVALID_HANDLE_VALUE);assert(GetLastError()==ERROR_SHARING_VIOLATION);
 assert(dluma_xui_document_source_release(nullptr,&second));assert(dluma_xui_document_source_release(nullptr,&first));
 Write("replacement.xui","<XuiCanvas><Properties><Width>1280</Width></Properties><XuiScene><Properties><Id>HUD_DI</Id><ClassOverride>HUDScreenBase</ClassOverride></Properties><AdvGroup><Properties><Id>Stock</Id><ClassOverride>HudBossHpBar</ClassOverride></Properties></AdvGroup><MyText><Properties><Id>Effect</Id><Font>missing-font</Font></Properties></MyText></XuiScene></XuiCanvas>");
 assert(dluma_xui_document_source_acquire(nullptr,a,&first));assert(dluma_xui_document_source_acquire(nullptr,b,&second));assert(!dluma_xui_document_source_compatible(nullptr,&first,&second));assert(dluma_xui_document_source_release(nullptr,&second));assert(dluma_xui_document_source_release(nullptr,&first));
 Write("replacement.xui","<XuiCanvas><XuiScene><Properties><Id>HUD_DI</Id><ClassOverride>HUDScreenBase</ClassOverride></Properties></XuiScene></XuiCanvas>");
 assert(dluma_xui_document_source_acquire(nullptr,a,&first));assert(dluma_xui_document_source_acquire(nullptr,b,&second));assert(!dluma_xui_document_source_compatible(nullptr,&first,&second));assert(dluma_xui_document_source_release(nullptr,&second));assert(dluma_xui_document_source_release(nullptr,&first));
 Write("replacement.xui","<!DOCTYPE XuiCanvas [<!ENTITY x SYSTEM 'file:///secret'>]><XuiCanvas>&x;</XuiCanvas>");assert(!dluma_xui_document_source_acquire(nullptr,b,&second));
 Write("replacement.xui","<XuiCanvas><XuiScene></XuiCanvas>");assert(!dluma_xui_document_source_acquire(nullptr,b,&second));assert(!dluma_xui_document_source_acquire(nullptr,"missing.xui",&second));
 std::puts("XUI document sources passed");
}
