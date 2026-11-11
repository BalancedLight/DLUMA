#include "game/dlw_xui_document_backend.h"
#include "fixes/custom_map_asset_isolation/NativeXuiReceiverBridge.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <climits>
struct DocumentString {const char* data;std::uint32_t size,capacity;};
struct XuiDocumentInvocation {void* level;std::uintptr_t manager;std::uint64_t manager_generation;int screen;char owner_names[DLUMA_XUI_DOCUMENT_COMPONENTS][128];char hud_name[128];bool invalid;};
static std::uintptr_t root_element=0x1111,children[2]={0x2222,0x2222};
static int screens[2]={7,7},owner_screen,other_owner;
static std::uintptr_t native_owner[3]={0,0,reinterpret_cast<std::uintptr_t>(&root_element)};
static bool missing_root=false,missing_owner=false,mixed_owner=false,stale=false;
static bool overlay=false;
static DocumentString g_script_name={"HUD",3,3};
static DocumentString overlay_name={"HUD_Overlay",11,11};
static unsigned failure;
static int current_registration=8;
static bool ReadBytes(std::uintptr_t p,void* out,std::size_t n){std::memcpy(out,reinterpret_cast<void*>(p),n);return true;}
static void* find(void*,int){return missing_root?nullptr:&root_element;}
int lookup(void*,const DocumentString* name){if(name&&name->size==7&&!std::memcmp(name->data,"HUD.xui",7))return 7;
    if(name&&name->size==3&&!std::memcmp(name->data,"HUD",3))return current_registration;
    return name&&name->size==11&&!std::memcmp(name->data,"HUD_Overlay",11)?10:-1;}
static void* owner(void* element){
    if(element==&root_element)return nullptr; // A root has no enclosing owner.
    if(missing_owner)return nullptr;
    return (mixed_owner||overlay)&&element==&children[1]?&other_owner:&owner_screen;
}
static int screen_id(void* value){return value==&other_owner?screens[1]:screens[0];}
static const void* script(void* value){return overlay&&value==&other_owner?&overlay_name:&g_script_name;}
static const void* native_screen(const void*){return native_owner;}
static int dluma_stricmp(const char* a,const char* b){return _stricmp(a,b);}
static bool DocumentDiagnosticFailure(unsigned code,unsigned=UINT_MAX,std::uintptr_t=0,std::uintptr_t=0){failure=code;return false;}
extern "C" int dluma_xui_observe_borrowed_receiver(const void*,const void*,const void*,uint64_t,DlumaXuiReceiverObservation* out){out->manager_generation=stale?10:9;return 1;}
static bool Validate(const DlumaXuiDocumentSnapshot* before,const XuiDocumentInvocation& scope,std::uintptr_t hud){
#include "document_screen_validation.inl"
    return true;
}
static XuiDocumentInvocation* g_xui_document_invocation;
static int g_xui_compiled_attempt_status,original_calls;
struct ActiveHookCall{explicit ActiveHookCall(int){}};
static std::uintptr_t g_original_xui_compiled_attempt(void*,int){++original_calls;return 0xabc;}
#include "document_compiled_bypass.inl"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s (guard=%u)\n",__LINE__,#x,failure);return 1;}}while(0)
int main(){
    alignas(8) unsigned char hud[72+DLUMA_XUI_DOCUMENT_COMPONENTS*8]={};
    for(unsigned n=0;n<2;++n){auto p=reinterpret_cast<std::uintptr_t>(&children[n]);std::memcpy(hud+72+8*n,&p,8);}
    DlumaXuiDocumentSnapshot before={};before.component_classes[0]=before.component_classes[1]=0x2222;
    before.component_screens[0]=before.component_screens[1]=7;
    alignas(8) unsigned char manager[216]={},entries[3][80]={};auto entry_ptr=reinterpret_cast<std::uintptr_t>(entries);std::uint32_t count=3;
    std::memcpy(manager+200,&entry_ptr,8);std::memcpy(manager+208,&count,4);
    for(unsigned n=0;n<3;++n){DocumentString file={n==2?"HUD_Overlay":"HUD.xui",n==2?11U:7U,n==2?11U:7U};int id=n==0?7:n==1?8:10;
        std::memcpy(entries[n]+8,&file,sizeof(file));std::memcpy(entries[n]+48,&id,4);}
    XuiDocumentInvocation scope={reinterpret_cast<void*>(0x4000),reinterpret_cast<std::uintptr_t>(manager),9,7};const auto address=reinterpret_cast<std::uintptr_t>(hud);
    std::memcpy(scope.owner_names[0],"HUD",4);std::memcpy(scope.owner_names[1],"HUD",4);std::memcpy(scope.hud_name,"HUD",4);
    screens[0]=screens[1]=8; // Native ReloadHud re-registers the same logical screen.
    CHECK(Validate(&before,scope,address));
    overlay=true;screens[1]=10;before.component_screens[1]=9;std::memcpy(scope.owner_names[1],"HUD_Overlay",12);
    CHECK(Validate(&before,scope,address));screens[1]=11;CHECK(!Validate(&before,scope,address));
    overlay=false;screens[1]=8;before.component_screens[1]=7;std::memset(scope.owner_names[1],0,128);std::memcpy(scope.owner_names[1],"HUD",4);
    missing_root=true;CHECK(!Validate(&before,scope,address));missing_root=false;
    missing_owner=true;CHECK(!Validate(&before,scope,address));missing_owner=false;
    mixed_owner=true;CHECK(!Validate(&before,scope,address));mixed_owner=false;
    stale=true;CHECK(!Validate(&before,scope,address));stale=false;
    native_owner[2]=reinterpret_cast<std::uintptr_t>(&children[0]);CHECK(!Validate(&before,scope,address));native_owner[2]=reinterpret_cast<std::uintptr_t>(&root_element);
    screens[0]=9;CHECK(!Validate(&before,scope,address));screens[0]=8;
    current_registration=7;CHECK(!Validate(&before,scope,address));current_registration=8;
    children[0]=0x3333;CHECK(!Validate(&before,scope,address));children[0]=0x2222;
    g_script_name={"Other.xui",9,9};CHECK(!Validate(&before,scope,address));g_script_name={"HUD",3,3};
    CHECK(Validate(&before,scope,address));
    g_xui_document_invocation=&scope;
    CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(scope.manager),8)==0&&!original_calls);
    CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(scope.manager),7)==0&&!original_calls);
    CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(scope.manager),10)==0xabc&&original_calls==1);
    CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(0x2000),8)==0xabc&&original_calls==2);
    count=4097;std::memcpy(manager+208,&count,4);CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(scope.manager),8)==0xabc&&scope.invalid&&original_calls==3);
    g_xui_document_invocation=nullptr;CHECK(HookXuiCompiledAttempt(reinterpret_cast<void*>(scope.manager),8)==0xabc&&original_calls==4);
    std::puts("actual document logical registration and scoped compiled bypass passed");return 0;
}
