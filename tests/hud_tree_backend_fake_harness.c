#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../src/fixes/custom_map_asset_isolation/NativeXuiObservation.h"
#include "../src/fixes/custom_map_asset_isolation/NativeXuiReceiverBridge.h"
#include "../src/game/dlw_hud_visibility_backend.c"

DlumaRuntime g_dluma;
static unsigned children_count, child_reads, name_reads;
static unsigned visibility_writes;
static int frame_ready, reset_in_write;
static HudString names[9000];
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
static int fake_profile_ok=1, fake_caller_ok=1;
static ULONGLONG fake_sequence=1;
static int fake_fixed_sequence;
static int fake_observer_mode;
static unsigned fake_observer_calls;
static uint64_t fake_observer_first_generation,fake_observer_last_generation;
static unsigned fake_map_generation=7;
static unsigned char fake_engine_image[0x91E800];
static HMODULE fake_engine=(HMODULE)fake_engine_image;
static unsigned fake_position_calls;
static unsigned fake_opacity_calls,fake_color_calls;
static int fake_view_class;
static struct { uintptr_t* vtable; unsigned char data[0x420]; } fake_diag_objects[3];
static uintptr_t fake_level_words[2],fake_level_data[175],fake_manager[44],fake_screen_iface[8],fake_ui_iface[3][4],fake_vector[1];
int dluma_xui_native_frame(void** level,uintptr_t* manager)
{ if(fake_observer_mode==1)return 0;*level=fake_level_words;*manager=(uintptr_t)fake_manager;return 1; }
uint64_t dluma_xui_native_manager_generation(uintptr_t manager)
{ return manager==(uintptr_t)fake_manager ? 99U : 0U; }
int dluma_xui_observe_borrowed_receiver(const void* receiver,const void* level,const void* manager,uint64_t generation,DlumaXuiReceiverObservation* out)
{ ++fake_observer_calls;if(fake_observer_calls==1)fake_observer_first_generation=generation;fake_observer_last_generation=generation;
  memset(out,0,sizeof(*out));if(fake_observer_mode==2 || (fake_observer_mode==4 && fake_observer_calls>=4) || !receiver || level!=fake_level_words || manager!=fake_manager || (generation && generation!=200U))return 0;
  out->manager_generation=(fake_observer_mode==5 || (fake_observer_mode==6 && fake_observer_calls>=4)) ? 98U : 99U;out->instance_generation=200U;out->load_serial=9;out->exact_input=fake_observer_mode!=3;return 1; }
static int fake_control_class;
static unsigned fake_class_calls;
static unsigned fake_visual_calls;
static int fake_nested_tree;
static int fake_branch_tree;
static unsigned index_of(void* p);
static void* __fastcall fake_diag_object(void* p) { return &fake_diag_objects[index_of(p)]; }
static const HudString* __fastcall fake_diag_name(void* p) {
    unsigned i; for(i=0;i<3;i++)if(p==&fake_diag_objects[i])return &names[i]; return 0;
}
HMODULE dluma_hud_diagnostic_test_game_module(void) { return (HMODULE)2; }
int dluma_hud_diagnostic_test_export_matches(int operation) { return operation>=1 && operation<=3; }
int dluma_engine_game_frame_active_caller_matches(DlumaEngineState* s,HMODULE m,ULONG_PTR r)
{ (void)s; return fake_caller_ok && m==fake_engine && r==0x251434; }
ULONGLONG dluma_engine_game_frame_active_caller_sequence(DlumaEngineState* s,HMODULE m,ULONG_PTR r)
{ if(!frame_ready || !dluma_engine_game_frame_active_caller_matches(s,m,r)) return 0;
  if(!fake_fixed_sequence) ++fake_sequence; return fake_sequence; }
unsigned int dluma_lua_runtime_current_map_generation(void) { return fake_map_generation; }
int dluma_player_actor_binding_probe_cold_profile(uintptr_t m,DlumaPlayerActorColdProfile* p)
{ if(!fake_profile_ok || m!=2)return 0; memset(p,0,sizeof(*p)); p->gamedll_base=2; return 1; }
int dluma_open_directory_identity(const WCHAR* p,DlumaDirectoryIdentity* out)
{ memset(out,0,sizeof(*out)); out->volume_serial=1; out->file_index_low=wcscmp(p,L"C:\\owned-project")?2:1; return 1; }
int dluma_directory_identity_equal(const DlumaDirectoryIdentity* a,const DlumaDirectoryIdentity* b)
{ return a->volume_serial==b->volume_serial && a->file_index_low==b->file_index_low; }
int dluma_validate_map_anchor_under_project(const WCHAR* p,const char* map,const DlumaDirectoryIdentity* id)
{ (void)p;(void)id; return !strcmp(map,"test_map"); }
int dluma_utf8_to_wide(const char* src,WCHAR* dst,unsigned int cap)
{ unsigned i; for(i=0;src[i] && i+1<cap;i++)dst[i]=(WCHAR)(unsigned char)src[i]; if(src[i])return 0; dst[i]=0; return 1; }
int dluma_wstricmp(const WCHAR* a,const WCHAR* b) { return _wcsicmp(a,b); }
static void __fastcall fake_position(void* p,const float* values) { (void)p;(void)values; ++fake_position_calls; }
static void __fastcall fake_opacity(void* p,float value) { (void)p;(void)value; ++fake_opacity_calls; }
static void __fastcall fake_color(void* p,const float* value) { (void)p;(void)value; ++fake_color_calls; }
static void fake_diag_code(void) {
    static const struct { unsigned rva; unsigned char code[32]; unsigned size; } traversal[]={
      {0x76E500,{0x48,0x8b,0x41,0x18,0xc3},5},{0x1850D0,{0x48,0x8b,0x41,0x08,0xc3},5},
      {0x5E03D0,{0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x16,0x48,0x8b,0x01},16},
      {0x5E0400,{0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x17,0x48,0x8b,0x01},16},
      {0x2BD430,{0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x90,0xb0,0x06,0,0,0x48,0x85,0xc0},16},
      {0x2BD450,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x8b,0xda,0xff,0x90,0xb0,0x06,0},16},
      {0x5E4110,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x28,0xf6,0x81,0x5a,2,0,0,0x10,0x74,5,0xe8,0xc0,0x10,4,0},32},
      {0x1B6030,{0x33,0xc0,0xc3},3},{0x5FC500,{0x48,0x8d,0x81,0x88,2,0,0,0xc3},8},
      {0x5FC520,{0x48,0x8d,0x81,0xa0,3,0,0,0xc3},8},{0x5FC850,{0x48,0x8d,0x81,0xa8,4,0,0,0xc3},8},
      {0x5FFF80,{0x48,0x8d,0x81,0x50,2,0,0,0xc3},8},{0x5FCC00,{0x48,0x8d,0x81,0x98,3,0,0,0xc3},8},
      {0x638C60,{0x48,0x8d,0x81,0x78,3,0,0,0xc3},8},{0x683C20,{0x48,0x8d,0x81,0x70,2,0,0,0xc3},8} };
    unsigned i;
    static const unsigned char thunk[]={0x48,0x8b,0x49,0x18,0x48,0x8b,0x01,0x48,0xff,0xa0,0xe8,0x05,0x00,0x00};
    static const unsigned char setter[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    static const unsigned char wrapper[]={0xf6,0x81,0xf0,0x02,0x00,0x00,0x04,0x74,0x10,0x48,0x8b,0x41,0x28,0x48,0x85,0xc0};
    static const unsigned char dirty[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x80,0x89,0x0d,0x02,0x00,0x00,0x01};
    static const unsigned char noop[]={0xc2,0,0};
    static const unsigned char opacity[]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x59,0x18,0x0f,0x29,0x74,0x24,0x20,0x0f};
    static const unsigned char color[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x18,0x48,0x8b};
    memcpy(fake_engine_image+0x5DF9D0,thunk,sizeof(thunk)); memcpy(fake_engine_image+0x606C20,setter,sizeof(setter));
    memcpy(fake_engine_image+0x601E70,wrapper,sizeof(wrapper)); memcpy(fake_engine_image+0x606D90,dirty,sizeof(dirty));
    memcpy(fake_engine_image+0x601EF0,wrapper,sizeof(wrapper)); memcpy(fake_engine_image+0x189A0,noop,sizeof(noop));
    memcpy(fake_engine_image+0x5E69C0,opacity,sizeof(opacity)); memcpy(fake_engine_image+0x5E6860,color,sizeof(color));
    for(i=0;i<sizeof(traversal)/sizeof(traversal[0]);++i)
        memcpy(fake_engine_image+traversal[i].rva,traversal[i].code,traversal[i].size);
}
#endif
static int repeat_child;
static int stale_file_lookup;
static char ids[9000][24];
static const HudString script_name={"HUD.xui",7,7};
static void* element(unsigned i) {
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(fake_nested_tree) return fake_ui_iface[i];
#endif
    return (void*)(uintptr_t)(0x10000U+16U*i);
}
static unsigned index_of(void* p) {
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(fake_nested_tree) return (unsigned)(((uintptr_t)p-(uintptr_t)fake_ui_iface)/sizeof(fake_ui_iface[0]));
#endif
    return (unsigned)(((uintptr_t)p-0x10000U)/16U);
}
static void* __fastcall fake_object(void* p) { return p; }
static const HudString* __fastcall fake_name(void* p) { ++name_reads; return &names[index_of(p)]; }
static int __fastcall fake_count(void* p) {
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(fake_nested_tree) return fake_branch_tree ? (index_of(p)==0 ? 2 : 0) : (index_of(p)<2 ? 1 : 0);
#endif
    return index_of(p)==0 ? (int)children_count : 0;
}
static void* __fastcall fake_child(void* p,int i)
{
    ++child_reads;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(fake_nested_tree) return element(fake_branch_tree ? (unsigned)i+1U : index_of(p)+1U);
#else
    (void)p;
#endif
    return element(repeat_child && i==1 ? 1U : (unsigned)i+1U);
}
static unsigned char __fastcall fake_class(void* p,const char* type) {
    (void)p;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    ++fake_class_calls;
    if(fake_view_class && !strcmp(type,"IUIElementView")) return 1;
    if(fake_control_class && !strcmp(type,"IUIElementControl")) return 1;
#else
    (void)type;
#endif
    return 0;
}
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
static void* __fastcall fake_visual(void* p) { (void)p; ++fake_visual_calls; return 0; }
#endif
static int __fastcall fake_lookup(void* p,const HudString* key) { (void)p; if(stale_file_lookup) return !strcmp(key->data,"HUD.xui")?6:!strcmp(key->data,"HUD")?7:-1; return 7; }
static void* __fastcall fake_screen(void* p,int id) { (void)p; if(stale_file_lookup && id==6)return 0; assert(id==7); return element(0); }
static const HudString* __fastcall fake_script(void* p) { (void)p; return &script_name; }
static int __fastcall fake_screen_id(void* p) { (void)p; return 7; }
static void __fastcall fake_visible_write(void* p,unsigned char visible) { (void)p; (void)visible; ++visibility_writes; if(reset_in_write) { reset_in_write=0; dluma_hud_visibility_backend_reset(); } }
static int reset_during_visitor(void* user,void* element_value)
{ (void)user; (void)element_value; dluma_hud_visibility_backend_reset(); return 1; }
__declspec(dllimport) void WINAPI RaiseException(DWORD,DWORD,DWORD,const ULONG_PTR*);
static int fault_during_visitor(void* user,void* element_value)
{ (void)user; (void)element_value; RaiseException(0xE0424242U,0,0,0); return 1; }
int dluma_is_readable(const void* p,SIZE_T size) { (void)size; return p!=0; }
void dluma_str_copy(char* dst,unsigned int cap,const char* src)
{ size_t n=strlen(src); assert(n<cap); memcpy(dst,src,n+1); }
int dluma_stricmp(const char* a,const char* b) { return _stricmp(a,b); }
int dluma_strcmp(const char* a,const char* b) { return strcmp(a,b); }
int dluma_sha256_file_hex(const WCHAR* p,ULONGLONG n,char out[DLUMA_SHA256_HEX_CAPACITY])
{ (void)p; (void)n; (void)out; return 0; }
void dluma_log_line(const char* level,const char* message) { (void)level; (void)message; }
int dluma_engine_query_session_level_identity(DlumaEngineState* state,char* map,unsigned int cap,void** level,ULONG_PTR* vt)
{ (void)state; (void)map; (void)cap; if(!frame_ready)return 0;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
  *level=fake_nested_tree ? fake_level_words : (void*)0x1234;
#else
  *level=(void*)0x1234;
#endif
  *vt=1; return 1; }
int dluma_engine_game_frame_dispatch_active_on_current_thread(const DlumaEngineState* state)
{ (void)state; return frame_ready; }
DlumaHostKind dluma_dual_host_runtime_kind(void) { return DLUMA_HOST_UNKNOWN; }
int dluma_get_module_path_checked(HMODULE module,WCHAR* path,unsigned int cap)
{ (void)module; (void)path; (void)cap; return 0; }

static void setup(unsigned count)
{
    unsigned i;
    memset(&g_hud,0,sizeof(g_hud));
    g_hud.object=fake_object; g_hud.name=fake_name; g_hud.count=fake_count;
    g_hud.child=fake_child; g_hud.from_class=fake_class;
    g_hud.lookup=fake_lookup; g_hud.screen=fake_screen; g_hud.owner=fake_object;
    g_hud.script=fake_script; g_hud.screen_id=fake_screen_id;
    children_count=count-1U; child_reads=name_reads=0; repeat_child=0;
    for(i=0;i<count;i++) {
        int n=sprintf_s(ids[i],sizeof(ids[i]),"Node%u",i);
        assert(n>0); names[i].data=ids[i]; names[i].size=(unsigned)n; names[i].capacity=(unsigned)n;
    }
}
int main(void)
{
    assert(dluma_hud_named_result(1,0)==-4);
    assert(dluma_hud_named_result(1,1)==1);
    assert(dluma_hud_named_result(-2,0)==-2);
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    {
        DWORD n=GetEnvironmentVariableW(L"DLUMA_XUI_TEST_CONFIG",g_dluma.config_path,DLUMA_ARRAY_COUNT(g_dluma.config_path));
        assert(n && n<DLUMA_ARRAY_COUNT(g_dluma.config_path));
        fake_diag_code();
        g_hud.engine=fake_engine;
        {
            uintptr_t level_words[2]={0}, level_data[175]={0}, manager[44]={0},
                      screen[128]={0}, ui[4]={0}, screen_interface[8]={0};
            uintptr_t* screens[2]={screen,screen}; void* selected=0;
            HudString* stored=(HudString*)((BYTE*)screen+968U);
            level_words[1]=(uintptr_t)level_data; level_data[174]=(uintptr_t)manager;
            manager[42]=(uintptr_t)screens; *(unsigned*)((BYTE*)manager+344U)=1;
            screen[0]=(uintptr_t)fake_engine+0x919AB8U; screen[2]=(uintptr_t)ui;
            screen[120]=(uintptr_t)screen_interface; ui[3]=(uintptr_t)screen;
            screen_interface[7]=(uintptr_t)screen; *stored=script_name;
            assert(dluma_hud_diagnostic_find_screen(level_words,"HUD.xui",&selected)==1 && selected==ui);
            *(unsigned*)((BYTE*)manager+344U)=2;
            assert(dluma_hud_diagnostic_find_screen(level_words,"HUD.xui",&selected)==-1);
            *(unsigned*)((BYTE*)manager+344U)=65;
            assert(dluma_hud_diagnostic_find_screen(level_words,"HUD.xui",&selected)==-2);
            *(unsigned*)((BYTE*)manager+344U)=1; screen_interface[7]=0;
            assert(dluma_hud_diagnostic_find_screen(level_words,"HUD.xui",&selected)==-2);
            screen_interface[7]=(uintptr_t)screen; stored->size=512;
            assert(dluma_hud_diagnostic_find_screen(level_words,"HUD.xui",&selected)==-2);
        }
        g_dluma.config.developer_mode=1; frame_ready=1;
        g_hud.installed=1; g_hud.engine=fake_engine; g_hud.set_position=fake_position;
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        assert(dluma_hud_visibility_backend_position_available());
        assert(dluma_hud_visibility_backend_diagnostic_owner_allowed(3U,7));
        assert(!dluma_hud_visibility_backend_diagnostic_owner_allowed(1U,7));
        assert(!dluma_hud_visibility_backend_diagnostic_owner_allowed(3U,8));
        dluma_hud_visibility_backend_diagnostic_context(0,0,0);
        assert(!dluma_hud_visibility_backend_position_available());
        frame_ready=0; dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        frame_ready=1; assert(!dluma_hud_visibility_backend_position_available());
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        assert(dluma_hud_visibility_backend_position_available());
        fake_caller_ok=0; assert(!dluma_hud_visibility_backend_position_available()); fake_caller_ok=1;
        fake_map_generation=8; assert(!dluma_hud_visibility_backend_position_available()); fake_map_generation=7;
        g_dluma.config.developer_mode=0; dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        assert(!dluma_hud_visibility_backend_position_available()); g_dluma.config.developer_mode=1;
        assert(WritePrivateProfileStringW(L"diagnostics",L"xui_private_enable",L"0",g_dluma.config_path));
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        assert(!dluma_hud_visibility_backend_position_available());
        assert(WritePrivateProfileStringW(L"diagnostics",L"xui_private_enable",L"1",g_dluma.config_path));
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","other_map",7);
        assert(!dluma_hud_visibility_backend_position_available());
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\other-project","test_map",7);
        assert(!dluma_hud_visibility_backend_position_available());
        fake_profile_ok=0; dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        assert(!dluma_hud_visibility_backend_position_available()); fake_profile_ok=1;
        dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
        {
            uintptr_t *root_vtable=(uintptr_t*)(fake_engine_image+0x919AB8), *child_vtable=(uintptr_t*)(fake_engine_image+0x908938);
            struct { uintptr_t* vtable; } root_object={root_vtable}, child_object={child_vtable};
            HudNode *root_node=&g_hud.nodes[0], *child_node=&g_hud.nodes[1];
            unsigned i;
            root_vtable[190]=(uintptr_t)fake_engine+0x189A0U;
            child_vtable[182]=(uintptr_t)fake_engine+0x606D90U;
            child_vtable[189]=(uintptr_t)fake_engine+0x606C20U;
            memset(root_node,0,sizeof(*root_node)); memset(child_node,0,sizeof(*child_node));
            root_node->element=(void*)100; root_node->root=(void*)100; root_node->object=&root_object;
            child_node->element=(void*)200; child_node->root=(void*)100; child_node->object=&child_object;
            strcpy_s(root_node->screen,sizeof(root_node->screen),"HUD.xui");
            strcpy_s(child_node->screen,sizeof(child_node->screen),"HUD.xui");
            strcpy_s(child_node->path,sizeof(child_node->path),"EffectPanel");
            g_hud.node_count=2;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==1);
            strcpy_s(child_node->screen,sizeof(child_node->screen),"Other.xui");
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-2);
            strcpy_s(child_node->screen,sizeof(child_node->screen),"HUD.xui");
            fake_engine_image[0x5DF9D0]^=1;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-2);
            fake_engine_image[0x5DF9D0]^=1;
            child_vtable[182]=(uintptr_t)fake_engine+0x638C70U;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-3);
            child_vtable[182]=(uintptr_t)fake_engine+0x606D90U;
            root_vtable[190]=(uintptr_t)fake_engine+0x6387E0U;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-3);
            root_vtable[190]=(uintptr_t)fake_engine+0x189A0U;
            child_vtable[189]=(uintptr_t)fake_engine+0x777U;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-3);
            child_vtable[189]=(uintptr_t)fake_engine+0x606C20U;
            for(i=2;i<258;i++) {
                HudNode* node=&g_hud.nodes[i]; *node=*child_node;
                sprintf_s(node->path,sizeof(node->path),"EffectPanel/Child%u",i);
            }
            g_hud.node_count=258;
            assert(dluma_hud_diagnostic_preflight(child_node,1)==-2);
        }
        {
            float pos[4]={1,2,3,1}; unsigned i;
            const uintptr_t offsets[3]={0x919AB8U,0x9111F8U,0x908938U};
            setup(3); frame_ready=1; g_hud.installed=1; g_hud.engine=fake_engine; g_hud.set_position=fake_position;
            g_hud.set_opacity=fake_opacity; g_hud.set_color=fake_color; fake_opacity_calls=fake_color_calls=0; fake_view_class=1;
            g_hud.object=fake_diag_object; g_hud.name=fake_diag_name; g_hud.visual=fake_visual; g_hud.level=fake_level_words; fake_position_calls=0; fake_nested_tree=1;
            for(i=0;i<3;i++) { fake_diag_objects[i].vtable=(uintptr_t*)(fake_engine_image+offsets[i]);
                memset(fake_diag_objects[i].vtable,0,233*sizeof(uintptr_t));
                fake_diag_objects[i].vtable[182]=(uintptr_t)fake_engine+0x606D90U;
                fake_diag_objects[i].vtable[189]=(uintptr_t)fake_engine+0x606C20U;
                fake_diag_objects[i].vtable[190]=(uintptr_t)fake_engine+0x189A0U;
                fake_diag_objects[i].vtable[146]=(uintptr_t)fake_engine+0x2BD430U;
                fake_diag_objects[i].vtable[214]=(uintptr_t)fake_engine+(i==0?0x5FCC00U:i==1?0x5FFF80U:0x1B6030U);
                fake_diag_objects[i].vtable[215]=(uintptr_t)fake_engine+0x2BD450U;
                fake_diag_objects[i].vtable[232]=(uintptr_t)fake_engine+0x189A0U; }
            fake_level_words[1]=(uintptr_t)fake_level_data; fake_level_data[174]=(uintptr_t)fake_manager;
            fake_manager[42]=(uintptr_t)fake_vector; *(unsigned*)((BYTE*)fake_manager+344U)=1;
            fake_vector[0]=(uintptr_t)&fake_diag_objects[0];
            for(i=0;i<3;i++) fake_ui_iface[i][3]=(uintptr_t)&fake_diag_objects[i];
            *(uintptr_t*)((BYTE*)&fake_diag_objects[0]+16U)=(uintptr_t)fake_ui_iface[0];
            *(uintptr_t*)((BYTE*)&fake_diag_objects[0]+960U)=(uintptr_t)fake_screen_iface;
            fake_screen_iface[7]=(uintptr_t)&fake_diag_objects[0];
            *(HudString*)((BYTE*)&fake_diag_objects[0]+968U)=script_name;
            for(i=1;i<3;i++) *(uintptr_t*)((BYTE*)&fake_diag_objects[i]+16U)=(uintptr_t)fake_ui_iface[i];
            fake_diag_objects[1].vtable[190]=(uintptr_t)fake_engine+0x6387E0U;
            strcpy_s(ids[1],sizeof(ids[1]),"EffectPanel"); names[1].size=names[1].capacity=11;
            strcpy_s(ids[2],sizeof(ids[2]),"Visual"); names[2].size=names[2].capacity=6;
            dluma_hud_visibility_backend_diagnostic_context(L"C:\\owned-project","test_map",7);
            fake_class_calls=0;
            fake_observer_mode=1;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=2;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=3;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=5;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=4; fake_observer_calls=0;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=6; fake_observer_calls=0;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_observer_mode=0; fake_visual_calls=0;
            fake_control_class=1; *((BYTE*)&fake_diag_objects[0]+0x25aU)=0x10;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0 && fake_visual_calls==0);
            *((BYTE*)&fake_diag_objects[0]+0x25aU)=0; fake_observer_calls=0;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==1 && fake_position_calls==1 && fake_visual_calls==1);
            assert(fake_class_calls==0);
            assert(fake_observer_calls>0 && fake_observer_first_generation==0 && fake_observer_last_generation==200U);
            fake_control_class=0; fake_position_calls=0;
            fake_branch_tree=1; fake_control_class=1; fake_visual_calls=0;
            fake_diag_objects[2].vtable=(uintptr_t*)(fake_engine_image+0x90E4F8U);
            *((BYTE*)&fake_diag_objects[2]+0x25aU)=0x10;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==1 && fake_position_calls==1 && fake_visual_calls==1);
            *((BYTE*)&fake_diag_objects[2]+0x25aU)=0; fake_diag_objects[2].vtable=(uintptr_t*)(fake_engine_image+0x908938U);
            fake_branch_tree=0; fake_control_class=0; fake_position_calls=0;
            fake_engine_image[0x2BD430]^=1;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_engine_image[0x2BD430]^=1;
            fake_diag_objects[2].vtable[214]=(uintptr_t)fake_engine+0x777U;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_diag_objects[2].vtable[214]=(uintptr_t)fake_engine+0x1B6030U;
            fake_ui_iface[2][3]=0;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            fake_ui_iface[2][3]=(uintptr_t)&fake_diag_objects[2];
            *(uintptr_t*)((BYTE*)&fake_diag_objects[2]+16U)=0;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-2 && fake_position_calls==0);
            *(uintptr_t*)((BYTE*)&fake_diag_objects[2]+16U)=(uintptr_t)fake_ui_iface[2];
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==1 && fake_position_calls==1);
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","screen",pos)==-2 && fake_position_calls==1);
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==1 && fake_opacity_calls==1);
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_COLOR,pos)==1 && fake_color_calls==1);
            *(unsigned*)((BYTE*)&fake_diag_objects[2]+344U)=0xfffffffeU;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==-2 && fake_opacity_calls==1);
            *(unsigned*)((BYTE*)&fake_diag_objects[2]+344U)=0;
            *(unsigned*)((BYTE*)&fake_diag_objects[2]+600U)=0xfffffffeU;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_COLOR,pos)==-2 && fake_color_calls==1);
            *(unsigned*)((BYTE*)&fake_diag_objects[2]+600U)=0;
            fake_engine_image[0x5E69C0]^=1;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==-2 && fake_opacity_calls==1);
            fake_engine_image[0x5E69C0]^=1; fake_engine_image[0x5E6860]^=1;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_COLOR,pos)==-2 && fake_color_calls==1);
            fake_engine_image[0x5E6860]^=1;
            fake_diag_objects[2].vtable=fake_diag_objects[1].vtable;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_COLOR,pos)==-3 && fake_color_calls==1);
            fake_diag_objects[2].vtable=(uintptr_t*)(fake_engine_image+0x908938U); fake_diag_objects[2].vtable[232]=(uintptr_t)fake_engine+0x777U;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==-3 && fake_opacity_calls==1);
            fake_diag_objects[2].vtable[232]=(uintptr_t)fake_engine+0x189A0U;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel/Visual",pos)==-3 && fake_position_calls==1);
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","Visual",pos)==0 && fake_position_calls==1);
            fake_diag_objects[1].vtable[182]=(uintptr_t)fake_engine+0x638C70U;
            assert(dluma_hud_visibility_backend_set_position(0,"HUD.xui","EffectPanel",pos)==-3 && fake_position_calls==1);
            fake_diag_objects[1].vtable[182]=(uintptr_t)fake_engine+0x606D90U;
            fake_fixed_sequence=1; ++fake_sequence;
            for(i=0;i<8;i++) assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==1);
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==-2);
            assert(fake_opacity_calls==9);
            ++fake_sequence;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==1);
            g_hud.diagnostic_budget.nodes=DLUMA_XUI_DIAGNOSTIC_NODE_LIMIT-1U;
            assert(dluma_hud_visibility_backend_set_visual(0,"HUD.xui","EffectPanel/Visual",DLUMA_XUI_VISUAL_OPACITY,pos)==-2);
            assert(fake_opacity_calls==10);
            fake_fixed_sequence=0;
            fake_nested_tree=0;
        }
        frame_ready=0;
    }
#else
    assert(!dluma_hud_visibility_backend_position_available());
#endif
    setup(4050);
    stale_file_lookup=1;
    assert(scan_screen("HUD.xui") && g_hud.node_count==4050);
    stale_file_lookup=0;
    setup(4050);
    assert(add_node(element(0),element(0),"HUD.xui","",0));
    assert(g_hud.node_count==4050 && child_reads==4049 && name_reads==4050);
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    assert(g_hud.diagnostic_region_count>0 && g_hud.diagnostic_region_count<64);
#endif
    assert(!strcmp(g_hud.nodes[4049].path,"Node4049"));
    {
        DlumaHudVisibilityRule rule={0}; HudNode* found=0;
        strcpy_s(rule.screen,sizeof(rule.screen),"HUD.xui");
        strcpy_s(rule.target,sizeof(rule.target),"Node4049");
        assert(resolve(&rule,&found)==1 && found==&g_hud.nodes[4049]);
        strcpy_s(g_hud.nodes[1].id,sizeof(g_hud.nodes[1].id),"Node4049");
        assert(resolve(&rule,&found)==-1);
    }
    setup(8192);
    assert(scan_screen("HUD.xui") && g_hud.node_count==8192);
    assert(child_reads==8191 && name_reads==8192);
    setup(8193);
    assert(!scan_screen("HUD.xui") && g_hud.node_count==0);
    assert(child_reads==8192 && name_reads==8192);
    children_count=3;
    assert(scan_screen("HUD.xui") && g_hud.node_count==4);
    setup(9000); children_count=8192;
    assert(add_node(element(8999),element(8999),"Other.xui","",0));
    assert(!scan_screen("HUD.xui") && g_hud.node_count==1);
    assert(g_hud.nodes[0].element==element(8999));
    children_count=4;
    assert(scan_screen("HUD.xui") && g_hud.node_count==6);
    setup(4); repeat_child=1;
    assert(!add_node(element(0),element(0),"HUD.xui","",0));
    setup(4); frame_ready=1; visibility_writes=0; g_hud.installed=1; g_hud.original=fake_visible_write;
    g_hud.level=(void*)0x1234; g_hud.tracked_count=1;
    g_hud.tracked[0].element=element(1); g_hud.tracked[0].object=element(1); g_hud.tracked[0].root=element(0);
    strcpy_s(g_hud.tracked[0].screen,sizeof(g_hud.tracked[0].screen),"HUD.xui");
    g_hud.tracked[0].hidden=1; g_hud.tracked[0].requested=1;
    assert(dluma_hud_visibility_backend_visit("HUD.xui","Node1",0,reset_during_visitor,0)==-4);
    assert(visibility_writes==0 && g_hud.resolver_gate.reset_pending && g_hud.tracked_count==1);
    assert(dluma_hud_visibility_backend_visit("HUD.xui","Node1",0,reset_during_visitor,0)==-2);
    dluma_hud_visibility_backend_apply(0,0);
    assert(visibility_writes==0 && !g_hud.resolver_gate.reset_pending && g_hud.tracked_count==0);
    setup(4); visibility_writes=0; g_hud.installed=1; g_hud.original=fake_visible_write;
    g_hud.level=(void*)0x1234; g_hud.reset_generation=UINT64_MAX; g_hud.tracked_count=1;
    g_hud.tracked[0].element=element(1); g_hud.tracked[0].object=element(1); g_hud.tracked[0].root=element(0);
    g_hud.tracked[0].reset_generation=UINT64_MAX;
    strcpy_s(g_hud.tracked[0].screen,sizeof(g_hud.tracked[0].screen),"HUD.xui");
    assert(dluma_hud_visibility_backend_visit("HUD.xui","Node1",0,reset_during_visitor,0)==-4);
    dluma_hud_visibility_backend_apply(0,0);
    assert(visibility_writes==0 && g_hud.reset_generation_exhausted && g_hud.tracked_count==0);
    setup(4); visibility_writes=0; g_hud.installed=1; g_hud.original=fake_visible_write;
    g_hud.level=(void*)0x1234;
    assert(dluma_hud_visibility_backend_visit("HUD.xui","Node1",0,fault_during_visitor,0)==-4);
    assert(visibility_writes==0 && !g_hud.resolver_gate.active);
    setup(4); visibility_writes=0; reset_in_write=1; g_hud.installed=1; g_hud.original=fake_visible_write;
    g_hud.level=(void*)0x1234; g_hud.tracked_count=2;
    g_hud.tracked[0].element=element(1); g_hud.tracked[0].object=element(1); g_hud.tracked[0].root=element(0);
    strcpy_s(g_hud.tracked[0].screen,sizeof(g_hud.tracked[0].screen),"HUD.xui");
    g_hud.tracked[0].hidden=1; g_hud.tracked[0].requested=1;
    g_hud.tracked[1]=g_hud.tracked[0]; g_hud.tracked[1].element=element(2); g_hud.tracked[1].object=element(2);
    dluma_hud_visibility_backend_apply(0,0);
    assert(visibility_writes==1 && g_hud.resolver_gate.reset_pending && g_hud.tracked_count==1);
    dluma_hud_visibility_backend_apply(0,0);
    assert(visibility_writes==1 && !g_hud.resolver_gate.reset_pending && g_hud.tracked_count==0);
    setup(4); g_hud.installed=1; g_hud.tracked_count=1; g_hud.reset_generation=12;
    assert(dluma_hud_resolver_enter(0));
    assert(!dluma_hud_visibility_backend_begin_document_reload());
    assert(g_hud.tracked_count==1 && g_hud.reset_generation==12);
    dluma_hud_resolver_leave();
    g_hud.active=1;
    assert(!dluma_hud_visibility_backend_begin_document_reload());
    assert(g_hud.tracked_count==1 && g_hud.reset_generation==12);
    g_hud.active=0;
    assert(dluma_hud_visibility_backend_begin_document_reload());
    assert(g_hud.tracked_count==0 && g_hud.reset_generation==13 && g_hud.resolver_gate.active);
    assert(!dluma_hud_resolver_enter(0));
    dluma_hud_visibility_backend_end_document_reload();
    assert(!g_hud.resolver_gate.active);
    assert(dluma_hud_visibility_backend_begin_document_reload());
    dluma_hud_visibility_backend_reset();
    assert(g_hud.resolver_gate.reset_pending);
    dluma_hud_visibility_backend_end_document_reload();
    dluma_hud_visibility_backend_apply(0,0);
    assert(!g_hud.resolver_gate.active && !g_hud.resolver_gate.reset_pending);
    assert(dluma_hud_visibility_backend_begin_document_reload());
    dluma_hud_visibility_backend_end_document_reload();
    g_hud.reset_generation=UINT64_MAX;
    assert(!dluma_hud_visibility_backend_begin_document_reload());
    assert(!g_hud.resolver_gate.active && g_hud.reset_generation_exhausted);
    frame_ready=0;
    setup(9000);
    assert(!add_node(element(0),element(0),"HUD.xui","",0));
    assert(child_reads<=8192 && name_reads<=8192);
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    {
        int local=1; memset(g_hud.diagnostic_regions,0,sizeof(g_hud.diagnostic_regions));
        g_hud.diagnostic_region_count=HUD_DIAGNOSTIC_REGION_LIMIT;
        assert(!dluma_hud_readable(&local,sizeof(local)));
    }
#endif
    puts("actual HUD backend large-tree and duplicate checks passed");
    return 0;
}
