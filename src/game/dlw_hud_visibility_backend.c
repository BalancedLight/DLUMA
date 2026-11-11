#include "../dlw_common.h"
#include "../dlw_path_security.h"
#include "../dlw_dual_host_runtime.h"
#include "dlw_hud_visibility_backend.h"
#include "dlw_hud_resolver_gate.h"
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
#include "dlw_xui_diagnostic_gate.h"
#include "../dlw_project_owner_identity.h"
#include "../dlw_lua_project_exit.h"
#include "../bindings/dlw_player_actor_binding.h"
#include "../fixes/custom_map_asset_isolation/NativeXuiObservation.h"
#include "../fixes/custom_map_asset_isolation/NativeXuiReceiverBridge.h"
#endif
#include <intrin.h>
#include <string.h>
#pragma intrinsic(_InterlockedCompareExchange128)

/* IUIElement::SetVisible's complete Windows x64 dispatch thunk. Its only
 * relative branch stays inside this 23-byte body, including in the relay. */
static const BYTE g_visible_code[23] = {
    0x48,0x8b,0x01,0x84,0xd2,0x74,0x09,0x33,0xd2,0x48,0xff,0xa0,
    0x58,0x04,0,0,0x48,0xff,0xa0,0x60,0x04,0,0};
typedef struct HudString { const char* data; unsigned size, capacity; } HudString;
typedef void (__fastcall *HudVisible)(void*, unsigned char);
typedef void* (__fastcall *HudScreen)(void*, int);
typedef int (__fastcall *HudLookup)(void*, const HudString*);
typedef void* (__fastcall *HudPointer)(void*);
typedef const HudString* (__fastcall *HudName)(void*);
typedef int (__fastcall *HudCount)(void*);
typedef void* (__fastcall *HudChild)(void*, int);
typedef unsigned char (__fastcall *HudIsVisible)(void*);
typedef unsigned char (__fastcall *HudFromClass)(void*, const char*);
typedef void (__fastcall *HudSetPosition)(void*, const float*);
typedef void (__fastcall *HudSetOpacity)(void*, float);
typedef void (__fastcall *HudSetColor)(void*, const float*);
typedef void (__fastcall *HudSetText)(void*, const HudString*);
typedef unsigned char (__fastcall *HudStartAnimation)(void*, const HudString*, unsigned char);
typedef struct HudNode {
    void* element; void* object; void* root;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    uint64_t diagnostic_instance_generation,diagnostic_load_serial;
#endif
    char screen[DLUMA_HUD_SCREEN_CAPACITY];
    char id[DLUMA_HUD_TARGET_CAPACITY];
    char path[DLUMA_HUD_TARGET_CAPACITY];
} HudNode;
typedef struct HudTracked {
    void* element; void* object; void* root;
    char screen[DLUMA_HUD_SCREEN_CAPACITY];
    uint64_t reset_generation;
    unsigned char requested, hidden, next_hidden, next_show;
} HudTracked;
/* Stock HUD documents can contain over four thousand elements. Keep the
 * traversal bounded while allowing their complete tree to be resolved. */
#define HUD_NODE_LIMIT 8192U
#define HUD_NODE_BUCKETS 16384U
#define HUD_NODE_PROBE_LIMIT 64U
#define HUD_TRACK_LIMIT 128U
#define HUD_DIAGNOSTIC_REGION_LIMIT 128U
/* Current-frame lookup alone does not prove that game-owned elements survive
 * a concurrent map teardown. Keep new direct mutations gated until that
 * lifetime boundary is qualified for the selected host. */
#define DLUMA_HUD_ELEMENT_LIFETIME_VERIFIED 0
static struct {
    HMODULE engine; HMODULE checked_engine; BYTE* target; BYTE patch[16]; HudVisible original;
    HudScreen screen; HudLookup lookup; HudPointer owner, object;
    HudName script, name; HudCount screen_id, count; HudChild child;
    HudIsVisible visible; HudFromClass from_class; HudPointer visual;
    HudSetPosition set_position;
    HudSetOpacity set_opacity; HudSetColor set_color;
    HudSetText set_text; HudStartAnimation start_animation;
    volatile LONG lock, stopping, active;
    DlumaHudResolverGate resolver_gate;
    uint64_t reset_generation;
    unsigned char reset_generation_exhausted;
    unsigned char document_reload_active;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiDiagnosticFacts diagnostic;
    uintptr_t diagnostic_game_module;
    char diagnostic_subtree[DLUMA_HUD_TARGET_CAPACITY];
    char diagnostic_screen[DLUMA_HUD_SCREEN_CAPACITY];
    char diagnostic_target[DLUMA_HUD_TARGET_CAPACITY];
    DlumaXuiDiagnosticBudget diagnostic_budget;
    void* diagnostic_level;
    uintptr_t diagnostic_manager;
    uint64_t diagnostic_manager_generation;
    unsigned char diagnostic_traversal;
    struct { uintptr_t begin,end; } diagnostic_regions[HUD_DIAGNOSTIC_REGION_LIMIT];
    unsigned diagnostic_region_count;
#endif
    int installed; void* level;
    unsigned node_count, tracked_count;
    unsigned node_index[HUD_NODE_BUCKETS]; /* node index + 1; zero is vacant */
    HudNode nodes[HUD_NODE_LIMIT]; HudTracked tracked[HUD_TRACK_LIMIT];
} g_hud;

static void lock(void) { while(dluma_atomic_compare_exchange(&g_hud.lock,1,0)) _mm_pause(); }
static void unlock(void) { dluma_atomic_exchange(&g_hud.lock,0); }

#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
#if defined(DLUMA_XUI_DIAGNOSTIC_TEST)
extern HMODULE dluma_hud_diagnostic_test_game_module(void);
extern int dluma_hud_diagnostic_test_export_matches(int operation);
static HMODULE dluma_hud_diagnostic_game_module(void) { return dluma_hud_diagnostic_test_game_module(); }
#else
static HMODULE dluma_hud_diagnostic_game_module(void) { return GetModuleHandleW(L"gamedll_x64_rwdi.dll"); }
#endif
static int dluma_hud_diagnostic_ascii_path(const WCHAR* in,char* out,unsigned int capacity)
{
    unsigned int i;
    if(!in || !out || !capacity || !in[0]) return 0;
    out[0]=0;
    for(i=0;in[i];++i) {
        WCHAR c=in[i];
        if(i+1U>=capacity || !((c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z') ||
            (c>=L'0' && c<=L'9') || c==L'_' || c==L'-' || c==L'.' || c==L'/')) { out[0]=0; return 0; }
        if(c==L'/' && i && in[i-1U]==L'/') { out[0]=0; return 0; }
        out[i]=(char)c;
    }
    out[i]=0;
    return out[0]!='/' && out[i-1]!='/';
}
static DlumaXuiDiagnosticFacts dluma_hud_diagnostic_current(void)
{
    DlumaXuiDiagnosticFacts facts;
    uintptr_t game_module;
    lock(); facts=g_hud.diagnostic; game_module=g_hud.diagnostic_game_module; unlock();
    facts.developer=g_dluma.config.developer_mode!=0;
    facts.active_frame=dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)!=0;
    facts.exact_caller=dluma_engine_game_frame_active_caller_matches(&g_dluma.engine,g_hud.engine,0x251434U)!=0;
    facts.current_generation=(facts.active_frame && facts.exact_caller) ?
        dluma_lua_runtime_current_map_generation() : 0;
    if(!game_module || (uintptr_t)dluma_hud_diagnostic_game_module()!=game_module) facts.exact_profile=0;
    return facts;
}
#endif

void dluma_hud_visibility_backend_diagnostic_context(const wchar_t* project,const char* map,uint32_t generation)
{
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiDiagnosticFacts facts; DlumaDirectoryIdentity expected,actual;
    DlumaPlayerActorColdProfile profile; HMODULE game;
    WCHAR enabled[4],configured_project[DLUMA_MAX_WPATH],configured_map[DLUMA_MAX_MAP_NAME],map_wide[DLUMA_MAX_MAP_NAME];
    WCHAR subtree[DLUMA_HUD_TARGET_CAPACITY],screen[DLUMA_HUD_SCREEN_CAPACITY];
    char subtree_ascii[DLUMA_HUD_TARGET_CAPACITY],screen_ascii[DLUMA_HUD_SCREEN_CAPACITY];
    DWORD e,p,m,s,n; unsigned int i; int screen_valid=1;
    memset(&facts,0,sizeof(facts)); memset(&profile,0,sizeof(profile)); subtree_ascii[0]=screen_ascii[0]=0;
    if(!project || !project[0] || !map || !map[0] || !generation ||
       !dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) {
        lock(); g_hud.diagnostic=facts; g_hud.diagnostic_game_module=0; g_hud.diagnostic_subtree[0]=0; g_hud.diagnostic_screen[0]=0; unlock();
        return;
    }
    facts.compiled=1; facts.developer=g_dluma.config.developer_mode!=0;
    e=GetPrivateProfileStringW(L"diagnostics",L"xui_private_enable",L"",enabled,DLUMA_ARRAY_COUNT(enabled),g_dluma.config_path);
    p=GetPrivateProfileStringW(L"diagnostics",L"xui_project",L"",configured_project,DLUMA_ARRAY_COUNT(configured_project),g_dluma.config_path);
    m=GetPrivateProfileStringW(L"diagnostics",L"xui_map",L"",configured_map,DLUMA_ARRAY_COUNT(configured_map),g_dluma.config_path);
    s=GetPrivateProfileStringW(L"diagnostics",L"xui_subtree",L"",subtree,DLUMA_ARRAY_COUNT(subtree),g_dluma.config_path);
    n=GetPrivateProfileStringW(L"diagnostics",L"xui_screen",L"",screen,DLUMA_ARRAY_COUNT(screen),g_dluma.config_path);
    if(!n || n>=DLUMA_ARRAY_COUNT(screen)-1U || !dluma_hud_diagnostic_ascii_path(screen,screen_ascii,sizeof(screen_ascii))) screen_valid=0;
    for(i=0;screen_ascii[i];i++) if(screen_ascii[i]=='/') screen_valid=0;
    if(i<4U || dluma_stricmp(screen_ascii+i-4U,".xui")) screen_valid=0;
    facts.opted_in=e==1U && enabled[0]==L'1';
    if(facts.developer && facts.opted_in && project && project[0] && map && map[0] && generation &&
       p && p<DLUMA_ARRAY_COUNT(configured_project)-1U && m && m<DLUMA_ARRAY_COUNT(configured_map)-1U &&
       screen_valid && s && s<DLUMA_ARRAY_COUNT(subtree)-1U && dluma_hud_diagnostic_ascii_path(subtree,subtree_ascii,sizeof(subtree_ascii)) &&
       dluma_utf8_to_wide(map,map_wide,DLUMA_ARRAY_COUNT(map_wide))) {
        facts.map_name_matches=dluma_wstricmp(configured_map,map_wide)==0;
        facts.project_identity_matches=dluma_open_directory_identity(configured_project,&expected) &&
            dluma_open_directory_identity(project,&actual) && dluma_directory_identity_equal(&expected,&actual) &&
            dluma_validate_map_anchor_under_project(project,map,&actual);
        game=dluma_hud_diagnostic_game_module();
        facts.exact_profile=game && dluma_player_actor_binding_probe_cold_profile((uintptr_t)game,&profile) &&
            profile.gamedll_base==(uintptr_t)game;
        facts.captured_generation=generation;
    }
    lock(); g_hud.diagnostic=facts; g_hud.diagnostic_game_module=profile.gamedll_base;
    dluma_str_copy(g_hud.diagnostic_subtree,sizeof(g_hud.diagnostic_subtree),subtree_ascii);
    dluma_str_copy(g_hud.diagnostic_screen,sizeof(g_hud.diagnostic_screen),screen_ascii); unlock();
#else
    (void)project; (void)map; (void)generation;
#endif
}

int dluma_hud_visibility_backend_diagnostic_owner_allowed(uint32_t scope,uint32_t generation)
{
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiDiagnosticFacts facts=dluma_hud_diagnostic_current();
    return dluma_xui_diagnostic_owner_allowed(&facts,scope,generation);
#else
    (void)scope; (void)generation; return 0;
#endif
}
static void dluma_hud_apply_impl(const DlumaHudVisibilityRule* rules,unsigned count);
static void dluma_hud_nodes_clear(void)
{
    g_hud.node_count=0; memset(g_hud.node_index,0,sizeof(g_hud.node_index));
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    g_hud.diagnostic_region_count=0;
#endif
}

/* A bounded per-traversal page-region cache; this is a readability check,
 * not a lifetime pin. SEH still handles changes after a cached query. */
static int dluma_hud_readable(const void* pointer,SIZE_T size)
{
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    uintptr_t current=(uintptr_t)pointer,last; unsigned i;
    if(!pointer || !size || current>UINTPTR_MAX-(size-1U)) return 0;
    last=current+size-1U;
    while(current<=last) {
        uintptr_t end=0;
        for(i=0;i<g_hud.diagnostic_region_count;i++)
            if(current>=g_hud.diagnostic_regions[i].begin && current<g_hud.diagnostic_regions[i].end) {
                end=g_hud.diagnostic_regions[i].end; break;
            }
        if(!end) {
            MEMORY_BASIC_INFORMATION info; DWORD protection; uintptr_t begin;
            if(g_hud.diagnostic_region_count>=HUD_DIAGNOSTIC_REGION_LIMIT ||
               VirtualQuery((const void*)current,&info,sizeof(info))!=sizeof(info)) return 0;
            protection=info.Protect & 0xffU; begin=(uintptr_t)info.BaseAddress;
            if(info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
               !(protection==PAGE_READONLY || protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
                 protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY) ||
               !info.RegionSize || begin>UINTPTR_MAX-info.RegionSize) return 0;
            end=begin+info.RegionSize;
            if(current<begin || current>=end) return 0;
            g_hud.diagnostic_regions[g_hud.diagnostic_region_count].begin=begin;
            g_hud.diagnostic_regions[g_hud.diagnostic_region_count++].end=end;
        }
        if(last<end) return 1;
        current=end;
    }
    return 1;
#else
    return dluma_is_readable(pointer,size);
#endif
}
/* Identity lookup is bounded independently of the native tree's shape. A
 * collision-heavy or cyclic tree is rejected, never traversed quadratically. */
static int dluma_hud_node_slot(void* element,unsigned* slot)
{
    uintptr_t key=(uintptr_t)element;
    unsigned i,bucket;
    key>>=4; key^=key>>33; key*=(uintptr_t)0xff51afd7ed558ccdULL; key^=key>>33;
    bucket=(unsigned)key&(HUD_NODE_BUCKETS-1U);
    for(i=0;i<HUD_NODE_PROBE_LIMIT;i++) {
        unsigned at=(bucket+i)&(HUD_NODE_BUCKETS-1U),entry=g_hud.node_index[at];
        if(!entry) { *slot=at; return 0; }
        if(entry>g_hud.node_count) return -1;
        if(g_hud.nodes[entry-1U].element==element) { *slot=at; return 1; }
    }
    return -1;
}
static void dluma_hud_nodes_rollback(unsigned count)
{
    unsigned i;
    g_hud.node_count=count; memset(g_hud.node_index,0,sizeof(g_hud.node_index));
    for(i=0;i<count;i++) {
        unsigned slot;
        if(dluma_hud_node_slot(g_hud.nodes[i].element,&slot)!=0) {
            dluma_hud_nodes_clear(); return;
        }
        g_hud.node_index[slot]=i+1U;
    }
}
static void dluma_hud_reset_locked(void)
{ g_hud.tracked_count=0; dluma_hud_nodes_clear(); g_hud.level=0; }
static void dluma_hud_reset_now(void)
{ lock(); dluma_hud_reset_locked(); unlock(); }
static int dluma_hud_resolver_enter(int cleanup)
{
    int entered;
    lock();
    entered=g_hud.installed && (cleanup || !g_dluma.stop_requested) &&
        dluma_hud_gate_enter(&g_hud.resolver_gate,cleanup);
    unlock();
    return entered;
}
static void dluma_hud_resolver_leave(void)
{
    /* A setter can reenter map teardown. Never query or touch game-owned UI
     * here; the next admitted frame handles any pending reset. */
    lock(); dluma_hud_gate_leave(&g_hud.resolver_gate); unlock();
}
static void __fastcall intercept(void* element, unsigned char visible)
{
    unsigned i; unsigned char effective=visible;
    dluma_atomic_increment(&g_hud.active);
    lock();
    if(!g_hud.stopping) for(i=0;i<g_hud.tracked_count;i++) {
        HudTracked* t=&g_hud.tracked[i];
        if(t->element==element) { t->requested=!!visible; effective=!t->hidden; break; }
    }
    unlock();
    g_hud.original(element,effective);
    dluma_atomic_decrement(&g_hud.active);
}
static int exchange(const BYTE* expected,const BYTE* replacement)
{
    __declspec(align(16)) LONGLONG before[2],after[2]; DWORD old=0,ignored=0; int changed;
    if(!VirtualProtect(g_hud.target,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(before,expected,16); memcpy(after,replacement,16);
    changed=_InterlockedCompareExchange128((volatile LONGLONG*)g_hud.target,after[1],after[0],before)!=0;
    if(changed) FlushInstructionCache(GetCurrentProcess(),g_hud.target,16);
    VirtualProtect(g_hud.target,16,old,&ignored);
    return changed;
}
int dluma_hud_visibility_backend_initialize(void)
{
    HMODULE engine=GetModuleHandleW(L"engine_x64_rwdi.dll"); WCHAR path[DLUMA_MAX_WPATH];
    char hash[DLUMA_SHA256_HEX_CAPACITY]; BYTE* relay; DWORD old; uintptr_t address;
    if(g_hud.installed) return !g_hud.stopping && engine==g_hud.engine;
    if(g_hud.stopping || !engine) return 0;
    /* Capability refresh runs once per owner each frame. A rejected engine
     * must not trigger another disk hash until a different module is loaded. */
    lock();
    if(g_hud.checked_engine==engine) { unlock(); return 0; }
    g_hud.checked_engine=engine;
    unlock();
    if(!dluma_get_module_path_checked(engine,path,DLUMA_ARRAY_COUNT(path)) ||
       !dluma_sha256_file_hex(path,64ULL*1024ULL*1024ULL,hash)) return 0;
    /* Each profile was checked against its own PE export table and bytes. */
    if(dluma_stricmp(hash,"a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c") &&
       dluma_stricmp(hash,"24c8bc14d65efe282e4cdae678d0230d71167afa37f8611715e5084f9733b361") &&
       dluma_stricmp(hash,"5b8090bfc3d3a25a24da1846a3aafd65c97e0d1e700b5dfebfc7132737b6bff6")) return 0;
#define BIND(field,type,symbol) g_hud.field=(type)GetProcAddress(engine,symbol); if(!g_hud.field) return 0
    BIND(target,BYTE*,"?SetVisible@IUIElement@@QEAAX_N@Z");
    BIND(screen,HudScreen,"?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z");
    BIND(lookup,HudLookup,"?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z");
    BIND(owner,HudPointer,"?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ");
    BIND(object,HudPointer,"?ToCUIElement@IUIElement@@QEBAPEAVCUIElement@@XZ");
    BIND(script,HudName,"?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ");
    BIND(name,HudName,"?GetId@CObject@gui@@QEBAAEBV?$string_base@D@ttl@@XZ");
    BIND(screen_id,HudCount,"?GetScreenId@IUIScreen@@QEBAHXZ");
    BIND(count,HudCount,"?GetChildrenCount@IUIElement@@QEAAHXZ");
    BIND(child,HudChild,"?GetChildren@IUIElement@@QEAAPEAV1@H@Z");
    BIND(visible,HudIsVisible,"?IsVisible@IUIElement@@QEBA_NXZ");
    BIND(from_class,HudFromClass,"?FromClass@CRTTIObject@@QEBA_NPEBD@Z");
    BIND(visual,HudPointer,"?GetVisual@IUIElementControl@@QEAAPEAVIUIVisual@@XZ");
#undef BIND
    /* This immediate mutation is qualified separately for the Player A18
     * engine profile; other visibility profiles do not inherit it. */
    if((dluma_dual_host_runtime_kind()==DLUMA_HOST_PLAYER_STANDALONE ||
        dluma_dual_host_runtime_kind()==DLUMA_HOST_PLAYER_EDITOR_PLAYTEST) &&
       !dluma_stricmp(hash,"a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c")) {
        g_hud.set_position=(HudSetPosition)GetProcAddress(engine,"?SetRelativePos@IUIElement@@QEAAXAEBVvec3@@@Z");
        g_hud.set_opacity=(HudSetOpacity)GetProcAddress(engine,"?SetOpacity@IUIElement@@UEAAXM@Z");
        g_hud.set_color=(HudSetColor)GetProcAddress(engine,"?SetColor@IUIElementView@@QEAAXAEBVvec4@@@Z");
        g_hud.set_text=(HudSetText)GetProcAddress(engine,"?SetText@IUIText@@UEAAXAEBV?$string_base@D@ttl@@@Z");
        g_hud.start_animation=(HudStartAnimation)GetProcAddress(engine,"?StartAnimation@IUIElementTimeLine@@QEAA_NAEBV?$string_base@D@ttl@@_N@Z");
    }
    if(((uintptr_t)g_hud.target&15U) || !dluma_is_readable(g_hud.target,sizeof(g_visible_code)) ||
       memcmp(g_hud.target,g_visible_code,sizeof(g_visible_code))) return 0;
    relay=(BYTE*)VirtualAlloc(0,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); if(!relay) return 0;
    memcpy(relay,g_visible_code,sizeof(g_visible_code));
    if(!VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&old) ||
       !FlushInstructionCache(GetCurrentProcess(),relay,sizeof(g_visible_code))) { VirtualFree(relay,0,MEM_RELEASE); return 0; }
    g_hud.original=(HudVisible)relay; g_hud.engine=engine;
    memset(g_hud.patch,0x90,16); g_hud.patch[0]=0xff; g_hud.patch[1]=0x25;
    memset(g_hud.patch+2,0,4); address=(uintptr_t)intercept; memcpy(g_hud.patch+6,&address,8);
    if(!exchange(g_visible_code,g_hud.patch)) { VirtualFree(relay,0,MEM_RELEASE); g_hud.original=0; return 0; }
    g_hud.installed=1; return 1;
}
static int string_copy(char* out,unsigned capacity,const HudString* text)
{
    if(!text || !dluma_hud_readable(text,sizeof(*text)) || text->size>=capacity ||
       (text->size && (!text->data || !dluma_hud_readable(text->data,text->size)))) return 0;
    if(text->size) memcpy(out,text->data,text->size); out[text->size]=0; return 1;
}
static int level(void** out)
{
    char name[512]; ULONG_PTR vt;
    return dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) &&
        dluma_engine_query_session_level_identity(&g_dluma.engine,name,sizeof(name),out,&vt) && *out;
}
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
static int dluma_hud_diagnostic_traversal_object(void* object);
static int dluma_hud_diagnostic_traversal_code(void);
static int dluma_hud_diagnostic_traits(void* object,void* element);
#endif
static int add_node(void* element,void* root,const char* screen,const char* parent,unsigned depth)
{
    HudNode* n; unsigned i,index,slot; int children,is_control;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiReceiverObservation observation;
    if(g_hud.diagnostic_traversal && !dluma_xui_diagnostic_budget_visit(&g_hud.diagnostic_budget)) return 0;
#endif
    if(!element || depth>48 || g_hud.node_count==HUD_NODE_LIMIT ||
       dluma_hud_node_slot(element,&slot)!=0) return 0;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    memset(&observation,0,sizeof(observation));
    if(g_hud.diagnostic_traversal &&
       (!g_hud.diagnostic_level || !g_hud.diagnostic_manager || !g_hud.diagnostic_manager_generation ||
        !dluma_xui_observe_borrowed_receiver(element,g_hud.diagnostic_level,(void*)g_hud.diagnostic_manager,
            0,&observation) ||
        observation.manager_generation!=g_hud.diagnostic_manager_generation || !observation.instance_generation)) return 0;
#endif
    index=g_hud.node_count++; n=&g_hud.nodes[index]; memset(n,0,sizeof(*n));
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(g_hud.diagnostic_traversal) { n->diagnostic_instance_generation=observation.instance_generation; n->diagnostic_load_serial=observation.load_serial; }
#endif
    n->element=element; n->root=root; n->object=g_hud.object(element);
    g_hud.node_index[slot]=index+1U;
    if(!n->object || !string_copy(n->id,sizeof(n->id),g_hud.name(n->object))) return 0;
    dluma_str_copy(n->screen,sizeof(n->screen),screen);
    if(element!=root) {
        size_t a=strlen(parent),b=strlen(n->id);
        if(a+(a?1:0)+b>=sizeof(n->path)) return 0;
        memcpy(n->path,parent,a); if(a && b) n->path[a++]='/'; memcpy(n->path+a,n->id,b+1);
    }
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(g_hud.diagnostic_traversal) {
        size_t path_length=strlen(n->path),target_length=strlen(g_hud.diagnostic_target);
        size_t common=path_length<target_length ? path_length : target_length;
        if(common && memcmp(n->path,g_hud.diagnostic_target,common)) return 1;
        if(path_length<target_length && path_length && g_hud.diagnostic_target[path_length]!='/') return 1;
        if(path_length>target_length && n->path[target_length]!='/') return 1;
        if(!observation.exact_input || !observation.load_serial) return 0;
        if(!dluma_hud_diagnostic_traversal_object(n->object) ||
           dluma_hud_diagnostic_traits(n->object,element)<0) return 0;
    }
#endif
    children=g_hud.count(element); if(children<0 || children>(int)HUD_NODE_LIMIT) return 0;
    for(i=0;i<(unsigned)children;i++) {
        void* child=g_hud.child(element,(int)i);
        if(!child) continue;
        if(!add_node(child,root,screen,n->path,depth+1)) return 0;
    }
    /* Controls keep their authored visual outside the ordinary child list. */
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(g_hud.diagnostic_traversal) is_control=(dluma_hud_diagnostic_traits(n->object,element)&1)!=0;
    else
#endif
    is_control=g_hud.from_class(element,"IUIElementControl")!=0;
    if(is_control) {
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
        /* GetVisual otherwise lazily constructs/updates the visual. The
         * diagnostic only borrows an already-materialized visual. */
        if(g_hud.diagnostic_traversal &&
           (!dluma_hud_readable((BYTE*)n->object+0x25aU,1) ||
            (*((BYTE*)n->object+0x25aU)&0x10U))) return 0;
#endif
        void* visual=g_hud.visual(element);
        if(visual) {
            unsigned visual_slot; int seen=dluma_hud_node_slot(visual,&visual_slot);
            if(seen<0 || (!seen && !add_node(visual,root,screen,n->path,depth+1))) return 0;
        }
    }
    return 1;
}
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
/* Active screens only: no native name search, registration fallback, or
 * virtual getter. Both engine-owned interfaces must point back to the same
 * exact CUIScreen before its element interface can be borrowed. */
static int dluma_hud_diagnostic_find_screen(void* level,const char* requested,void** result)
{
    uintptr_t native_level,manager,vector,screen,element,iface,base=(uintptr_t)g_hud.engine;
    unsigned count,i,matches=0; void* match=0; char name[512],bare[DLUMA_HUD_SCREEN_CAPACITY];
    const char* filename; size_t a,b;
    if(!result || !requested || !base || base>UINTPTR_MAX-0x919AB8U ||
       !dluma_hud_readable(level,16)) return -2;
    *result=0; native_level=((uintptr_t*)level)[1];
    if(native_level>UINTPTR_MAX-1400U || !dluma_hud_readable((void*)(native_level+1392U),sizeof(uintptr_t))) return -2;
    manager=*(uintptr_t*)(native_level+1392U);
    if(manager>UINTPTR_MAX-348U || !dluma_hud_readable((void*)(manager+336U),12U)) return -2;
    vector=*(uintptr_t*)(manager+336U); count=*(unsigned*)(manager+344U);
    if(count>64U || (count && (!vector || !dluma_hud_readable((void*)vector,count*sizeof(uintptr_t))))) return -2;
    b=strlen(requested); if(!b || b>=sizeof(bare)) return -2;
    for(i=0;i<count;i++) {
        screen=((uintptr_t*)vector)[i];
        if(screen>UINTPTR_MAX-1000U || !dluma_hud_readable((void*)screen,1000U) ||
           *(uintptr_t*)screen!=base+0x919AB8U) return -2;
        element=*(uintptr_t*)(screen+16U); iface=*(uintptr_t*)(screen+960U);
        if(!element || !iface || element>UINTPTR_MAX-32U || iface>UINTPTR_MAX-64U ||
           !dluma_hud_readable((void*)element,32U) || !dluma_hud_readable((void*)iface,64U) ||
           *(uintptr_t*)(element+24U)!=screen || *(uintptr_t*)(iface+56U)!=screen ||
           !string_copy(name,sizeof(name),(const HudString*)(screen+968U))) return -2;
        filename=name; for(a=0;name[a];a++) if(name[a]=='/' || name[a]=='\\') filename=name+a+1;
        if(dluma_stricmp(filename,requested)) {
            a=strlen(filename);
            if(b<4U || dluma_stricmp(requested+b-4U,".xui") || a!=b-4U) continue;
            memcpy(bare,requested,b-4U); bare[b-4U]=0;
            if(dluma_stricmp(filename,bare)) continue;
        }
        ++matches; match=(void*)element;
    }
    if(matches>1U) return -1;
    if(!matches) return 0;
    *result=match; return 1;
}
#endif
static int scan_screen(const char* name)
{
    HudString key={name,(unsigned)strlen(name),(unsigned)strlen(name)}; int id; void* root; void* owner;
    char script[512]; const char* base; unsigned i,start=g_hud.node_count;
    for(i=0;i<start;i++) if(!dluma_stricmp(g_hud.nodes[i].screen,name)) return 1;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(g_hud.diagnostic_traversal) {
        int found=dluma_hud_diagnostic_find_screen(g_hud.level,name,&root);
        if(found!=1) return 0;
        if(!add_node(root,root,name,"",0)) { dluma_hud_nodes_rollback(start); return 0; }
        return 1;
    }
#endif
    id=g_hud.lookup(g_hud.level,&key); root=id>=0?g_hud.screen(g_hud.level,id):0;
    if(!root) {
        size_t n=strlen(name); char scene[DLUMA_HUD_SCREEN_CAPACITY];
        /* Reloaded scenes use HUD while file registrations retain HUD.xui.
         * A stale file ID must not hide the current registered scene. */
        if(n>4U && n-4U<sizeof(scene) && !dluma_stricmp(name+n-4U,".xui")) {
            memcpy(scene,name,n-4U); scene[n-4U]=0;
            key.data=scene; key.size=(unsigned)(n-4U); key.capacity=key.size;
            id=g_hud.lookup(g_hud.level,&key); root=id>=0?g_hud.screen(g_hud.level,id):0;
        }
    }
    if(!root) return 1;
    owner=g_hud.owner(root);
    if(!owner || g_hud.screen_id(owner)!=id || !string_copy(script,sizeof(script),g_hud.script(owner))) return 0;
    base=script; for(i=0;script[i];i++) if(script[i]=='/' || script[i]=='\\') base=script+i+1;
    if(dluma_stricmp(base,name)) {
        size_t a=strlen(base),b=strlen(name);
        char bare[DLUMA_HUD_SCREEN_CAPACITY];
        if(b<4 || dluma_stricmp(name+b-4,".xui") || a!=b-4) return 0;
        memcpy(bare,name,b-4); bare[b-4]=0;
        if(dluma_stricmp(base,bare)) return 0;
    }
    if(!add_node(root,root,name,"",0)) { dluma_hud_nodes_rollback(start); return 0; }
    return 1;
}
static int resolve(const DlumaHudVisibilityRule* rule,HudNode** found)
{
    unsigned i,matches=0; int path=strchr(rule->target,'/')!=0;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(g_hud.diagnostic_traversal) path=1;
#endif
    *found=0;
    if(!scan_screen(rule->screen)) return -2;
    for(i=0;i<g_hud.node_count;i++) {
        HudNode* n=&g_hud.nodes[i]; if(dluma_stricmp(n->screen,rule->screen)) continue;
        if((!dluma_strcmp(rule->target,"screen") && n->element==n->root) ||
           (dluma_strcmp(rule->target,"screen") && !dluma_strcmp(path?n->path:n->id,rule->target))) { *found=n; matches++; }
    }
    return matches==1?1:matches? -1:0;
}
int dluma_hud_visibility_backend_validate(const DlumaHudVisibilityRule* rule)
{
    void* current=0; HudNode* found; int result=0;
    if(!rule || !dluma_hud_resolver_enter(0)) return 0;
    __try { if(level(&current)) { if(current!=g_hud.level) dluma_hud_reset_now(); g_hud.level=current; dluma_hud_nodes_clear(); result=resolve(rule,&found); } } __except(1) { result=0; }
    dluma_hud_resolver_leave();
    return result;
}
int dluma_hud_visibility_backend_position_available(void)
{
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiDiagnosticFacts facts=dluma_hud_diagnostic_current();
    return dluma_xui_diagnostic_ready(&facts) && g_hud.installed && !g_hud.stopping && g_hud.set_position != 0;
#else
    return 0;
#endif
}
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
/* Only the configured authored subtree is admitted; validate every object
 * whose dirty callback can be reached by the selected position setter. */
static int dluma_hud_diagnostic_traversal_object(void* object)
{
    static const struct { unsigned int table,children; } allowed[]={
        {0x908938U,0x1B6030U},{0x90CE98U,0x1B6030U},{0x90E4F8U,0x5FC500U},
        {0x90FE28U,0x1B6030U},{0x9111F8U,0x5FFF80U},{0x914E48U,0x1B6030U},
        {0x915938U,0x1B6030U},{0x919AB8U,0x5FCC00U},{0x91C4F8U,0x1B6030U},
        {0x91E098U,0x683C20U} };
    uintptr_t base=(uintptr_t)g_hud.engine, table; uintptr_t* slots; unsigned i;
    if(!base || !dluma_hud_readable(object,sizeof(uintptr_t))) return 0;
    table=*(uintptr_t*)object;
    if(table<base || !dluma_hud_readable((void*)table,1728U)) return 0;
    slots=(uintptr_t*)table;
    for(i=0;i<DLUMA_ARRAY_COUNT(allowed);++i) if(table-base==allowed[i].table)
        return slots[146]==base+0x2BD430U && slots[214]==base+allowed[i].children &&
               slots[215]==base+0x2BD450U;
    return 0;
}
/* Flags: control=1, view=2. The exact constructors establish these native
 * base layouts; the common CObject constructor establishes both backlinks. */
static int dluma_hud_diagnostic_traits(void* object,void* element)
{
    uintptr_t base=(uintptr_t)g_hud.engine,table;
    if(!object || !element || !dluma_hud_readable(object,24U) || !dluma_hud_readable(element,32U) ||
       ((uintptr_t*)object)[2]!=(uintptr_t)element || ((uintptr_t*)element)[3]!=(uintptr_t)object) return -1;
    table=*(uintptr_t*)object; if(table<base) return -1;
    switch(table-base) {
    case 0x90E4F8U: case 0x919AB8U: return 1;
    case 0x908938U: case 0x90FE28U: case 0x914E48U: case 0x915938U: case 0x91C4F8U: return 2;
    case 0x90CE98U: case 0x9111F8U: case 0x91E098U: return 0;
    default: return -1;
    }
}
static int dluma_hud_diagnostic_traversal_code(void)
{
    static const BYTE to_object[]={0x48,0x8b,0x41,0x18,0xc3};
    static const BYTE name[]={0x48,0x8b,0x41,0x08,0xc3};
    static const BYTE count[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x16,0x48,0x8b,0x01};
    static const BYTE child[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x17,0x48,0x8b,0x01};
    static const BYTE count_impl[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x90,0xb0,0x06,0x00,0x00,0x48,0x85,0xc0};
    static const BYTE child_impl[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x8b,0xda,0xff,0x90,0xb0,0x06,0x00};
    static const BYTE visual[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x28,0xf6,0x81,0x5a,0x02,0x00,0x00,0x10,0x74,0x05,0xe8,0xc0,0x10,0x04,0x00};
    static const struct { unsigned int rva; BYTE code[8]; unsigned int size; } getters[]={
      {0x1B6030U,{0x33,0xc0,0xc3},3}, {0x5FC500U,{0x48,0x8d,0x81,0x88,0x02,0,0,0xc3},8},
      {0x5FC520U,{0x48,0x8d,0x81,0xa0,0x03,0,0,0xc3},8}, {0x5FC850U,{0x48,0x8d,0x81,0xa8,0x04,0,0,0xc3},8},
      {0x5FFF80U,{0x48,0x8d,0x81,0x50,0x02,0,0,0xc3},8}, {0x5FCC00U,{0x48,0x8d,0x81,0x98,0x03,0,0,0xc3},8},
      {0x638C60U,{0x48,0x8d,0x81,0x78,0x03,0,0,0xc3},8}, {0x683C20U,{0x48,0x8d,0x81,0x70,0x02,0,0,0xc3},8} };
    static const struct { unsigned int rva; const BYTE* code; SIZE_T size; } functions[]={
      {0x76E500U,to_object,sizeof(to_object)},{0x1850D0U,name,sizeof(name)},
      {0x5E03D0U,count,sizeof(count)},{0x5E0400U,child,sizeof(child)},
      {0x2BD430U,count_impl,sizeof(count_impl)},{0x2BD450U,child_impl,sizeof(child_impl)},
      {0x5E4110U,visual,sizeof(visual)} };
    uintptr_t base=(uintptr_t)g_hud.engine; unsigned i;
    if(!base || base>UINTPTR_MAX-0x76E510U) return 0;
#if !defined(DLUMA_XUI_DIAGNOSTIC_TEST)
    if((uintptr_t)g_hud.object!=base+0x76E500U || (uintptr_t)g_hud.name!=base+0x1850D0U ||
       (uintptr_t)g_hud.count!=base+0x5E03D0U || (uintptr_t)g_hud.child!=base+0x5E0400U ||
       (uintptr_t)g_hud.visual!=base+0x5E4110U) return 0;
#endif
    for(i=0;i<DLUMA_ARRAY_COUNT(functions);++i) {
        const void* p=(void*)(base+functions[i].rva);
        if(!dluma_hud_readable(p,functions[i].size) || memcmp(p,functions[i].code,functions[i].size)) return 0;
    }
    for(i=0;i<DLUMA_ARRAY_COUNT(getters);++i) {
        const void* p=(void*)(base+getters[i].rva);
        if(!dluma_hud_readable(p,getters[i].size) || memcmp(p,getters[i].code,getters[i].size)) return 0;
    }
    return 1;
}
static int dluma_hud_diagnostic_code_current(int operation)
{
    static const BYTE thunk[]={0x48,0x8b,0x49,0x18,0x48,0x8b,0x01,0x48,0xff,0xa0,0xe8,0x05,0x00,0x00};
    static const BYTE setter[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    static const BYTE wrapper[]={0xf6,0x81,0xf0,0x02,0x00,0x00,0x04,0x74,0x10,0x48,0x8b,0x41,0x28,0x48,0x85,0xc0};
    static const BYTE dirty[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x80,0x89,0x0d,0x02,0x00,0x00,0x01};
    static const BYTE noop[]={0xc2,0x00,0x00};
    static const BYTE opacity[]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x59,0x18,0x0f,0x29,0x74,0x24,0x20,0x0f};
    static const BYTE color[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x18,0x48,0x8b};
    struct Guard { unsigned int rva; const BYTE* bytes; SIZE_T size; };
    const struct Guard guards[]={
        {0x5DF9D0U,thunk,sizeof(thunk)}, {0x606C20U,setter,sizeof(setter)},
        {0x601E70U,wrapper,sizeof(wrapper)}, {0x606D90U,dirty,sizeof(dirty)},
        {0x601EF0U,wrapper,sizeof(wrapper)}, {0x189A0U,noop,sizeof(noop)} };
    uintptr_t engine=(uintptr_t)g_hud.engine; unsigned int i;
    if(!engine || engine>UINTPTR_MAX-0x606DA0U) return 0;
#if defined(DLUMA_XUI_DIAGNOSTIC_TEST)
    if(!dluma_hud_diagnostic_test_export_matches(operation)) return 0;
#else
    if((operation==1 && (uintptr_t)g_hud.set_position!=engine+0x5DF9D0U) ||
       (operation==2 && (uintptr_t)g_hud.set_opacity!=engine+0x5E69C0U) ||
       (operation==3 && (uintptr_t)g_hud.set_color!=engine+0x5E6860U)) return 0;
#endif
    if(operation==2 || operation==3) {
        const void* address=(const void*)(engine+(operation==2 ? 0x5E69C0U : 0x5E6860U));
        const BYTE* bytes=operation==2 ? opacity : color;
        SIZE_T size=operation==2 ? sizeof(opacity) : sizeof(color);
        return dluma_hud_readable(address,size) && !memcmp(address,bytes,size) &&
            dluma_hud_readable((const void*)(engine+0x189A0U),sizeof(noop)) &&
            !memcmp((const void*)(engine+0x189A0U),noop,sizeof(noop));
    }
    if(operation!=1) return 0;
    for(i=0;i<DLUMA_ARRAY_COUNT(guards);i++) {
        const void* address=(const void*)(engine+guards[i].rva);
        if(!dluma_hud_readable(address,guards[i].size) || memcmp(address,guards[i].bytes,guards[i].size)) return 0;
    }
    return 1;
}
static int dluma_hud_diagnostic_prefix_equal(const char* left,const char* right,size_t count)
{
    size_t i;
    for(i=0;i<count;i++) if(!left[i] || left[i]!=right[i]) return 0;
    return 1;
}
static int dluma_hud_diagnostic_class_allowed(uintptr_t engine,uintptr_t vtable)
{
    static const unsigned int classes[]={
        0x908938U, /* CUIAARectangle */ 0x90CE98U, /* CUIElement */
        0x90E4F8U, /* CUIElementControl */ 0x90FE28U, /* CUIElementView */
        0x9111F8U, /* CUIGroup */ 0x914E48U, /* CUIImage */
        0x915938U, /* CUIImageAdvancedTexture */ 0x919AB8U, /* CUIScreen */
        0x91C4F8U, /* CUIText */ 0x91E098U /* CUIVisual */ };
    unsigned int i;
    if(vtable<engine) return 0;
    for(i=0;i<DLUMA_ARRAY_COUNT(classes);i++) if(vtable-engine==classes[i]) return 1;
    return 0;
}
static int dluma_hud_diagnostic_preflight(const HudNode* target,int operation)
{
    char prefix[DLUMA_HUD_TARGET_CAPACITY],screen[DLUMA_HUD_SCREEN_CAPACITY]; size_t length,target_length,parent_length;
    const char* slash; const HudNode* parent=0; unsigned i,descendants=0,parent_matches=0;
    uintptr_t engine=(uintptr_t)g_hud.engine, vtable, fn;
    if(!target || !target->object || !engine || engine>UINTPTR_MAX-0x606D90U ||
       !dluma_hud_diagnostic_code_current(operation)) return -2;
    lock(); dluma_str_copy(prefix,sizeof(prefix),g_hud.diagnostic_subtree);
    dluma_str_copy(screen,sizeof(screen),g_hud.diagnostic_screen); unlock();
    if(!screen[0] || dluma_stricmp(screen,target->screen)) return -2;
    if(operation==2 || operation==3) {
        uintptr_t object=(uintptr_t)target->object;
        unsigned offset=operation==2 ? 344U : 600U;
        if(object>UINTPTR_MAX-offset-sizeof(unsigned) ||
           !dluma_hud_readable((const void*)(object+offset),sizeof(unsigned)) ||
           *(const unsigned*)(object+offset)>=0xfffffffeU) return -2;
    }
    length=strlen(prefix);
    if(!length || !dluma_hud_diagnostic_prefix_equal(target->path,prefix,length) ||
       (target->path[length] && target->path[length]!='/')) return -2;
    target_length=strlen(target->path);
    slash=0;
    for(i=0;i<target_length;i++) if(target->path[i]=='/') slash=target->path+i;
    parent_length=slash?(size_t)(slash-target->path):0;
    for(i=0;i<g_hud.node_count;i++) {
        const HudNode* node=&g_hud.nodes[i];
        if(node->root!=target->root || dluma_stricmp(node->screen,target->screen)) continue;
        if(dluma_hud_diagnostic_prefix_equal(node->path,target->path,target_length) &&
           (!node->path[target_length] || node->path[target_length]=='/')) {
            if(++descendants>256U || !node->object || !dluma_hud_readable(node->object,sizeof(void*))) return -2;
            vtable=*(const uintptr_t*)node->object;
            if(!vtable || !dluma_hud_diagnostic_class_allowed(engine,vtable) ||
               vtable>UINTPTR_MAX-1864U || !dluma_hud_readable((const void*)vtable,1864U)) return -3;
            fn=*(const uintptr_t*)(vtable+1456U);
            if(fn!=engine+0x606D90U && fn!=engine+0x601EF0U) return -3;
            if(node==target && operation==1) {
                fn=*(const uintptr_t*)(vtable+1512U);
                if(fn!=engine+0x606C20U && fn!=engine+0x601E70U) return -3;
            }
            if(node==target && operation==2 && *(const uintptr_t*)(vtable+1856U)!=engine+0x189A0U) return -3;
        }
        if((parent_length==0 && node->element==node->root) ||
           (parent_length && strlen(node->path)==parent_length && dluma_hud_diagnostic_prefix_equal(node->path,target->path,parent_length))) {
            parent=node; ++parent_matches;
        }
    }
    if(!descendants || parent_matches!=1 || !parent || !parent->object ||
       !dluma_hud_readable(parent->object,sizeof(void*))) return -2;
    if(operation!=1) return 1;
    vtable=*(const uintptr_t*)parent->object;
    if(!vtable || !dluma_hud_diagnostic_class_allowed(engine,vtable) ||
       vtable>UINTPTR_MAX-1528U || !dluma_hud_readable((const void*)vtable,1528U)) return -3;
    return *(const uintptr_t*)(vtable+1520U)==engine+0x189A0U ? 1 : -3;
}
#endif

static int dluma_hud_visibility_backend_visit_impl(const char* screen,const char* target,
    const char* required_class,DlumaHudElementVisitor visitor,void* user,int private_operation)
{
    DlumaHudVisibilityRule rule; void* current=0; HudNode* found=0; int result=-2,commit;
    volatile LONG native_entered=0;
    if(!screen || !target || !visitor || strlen(screen)>=sizeof(rule.screen) ||
       strlen(target)>=sizeof(rule.target) || !dluma_hud_resolver_enter(0)) return -2;
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(private_operation) {
        ULONGLONG sequence=dluma_engine_game_frame_active_caller_sequence(&g_dluma.engine,g_hud.engine,0x251434U);
        if(!dluma_xui_diagnostic_budget_claim(&g_hud.diagnostic_budget,sequence)) {
            dluma_hud_resolver_leave(); return -2;
        }
        if(!target[0] || !dluma_strcmp(target,"screen")) { dluma_hud_resolver_leave(); return -2; }
        dluma_str_copy(g_hud.diagnostic_target,sizeof(g_hud.diagnostic_target),target);
        g_hud.diagnostic_traversal=1;
    }
#endif
    __try {
        memset(&rule,0,sizeof(rule)); dluma_str_copy(rule.screen,sizeof(rule.screen),screen);
        dluma_str_copy(rule.target,sizeof(rule.target),target);
        if(level(&current)) {
        if(current!=g_hud.level) dluma_hud_reset_now();
        g_hud.level=current; dluma_hud_nodes_clear();
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
        if(private_operation) {
            void* observed_level=0; uintptr_t manager=0; uint64_t manager_generation=0;
            if(!dluma_hud_diagnostic_traversal_code() ||
               !dluma_xui_native_frame(&observed_level,&manager) || observed_level!=current || !manager ||
               !(manager_generation=dluma_xui_native_manager_generation(manager))) __leave;
            g_hud.diagnostic_level=observed_level; g_hud.diagnostic_manager=manager;
            g_hud.diagnostic_manager_generation=manager_generation;
        }
#endif
        result=resolve(&rule,&found);
        if(result==1) {
            if(!found || !found->element || !found->object) result=-2;
            else if(required_class && required_class[0] &&
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
                    (private_operation ? dluma_hud_diagnostic_traits(found->object,found->element)!=2 :
                     !g_hud.from_class(found->element,required_class))) result=-3;
#else
                    !g_hud.from_class(found->element,required_class)) result=-3;
#endif
            else {
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
                if(private_operation && (result=dluma_hud_diagnostic_preflight(found,private_operation))!=1) __leave;
                if(private_operation) {
                    void* level_now=0; uintptr_t manager_now=0; DlumaXuiReceiverObservation seen;
                    memset(&seen,0,sizeof(seen));
                    if(!dluma_xui_native_frame(&level_now,&manager_now) || level_now!=g_hud.diagnostic_level ||
                       manager_now!=g_hud.diagnostic_manager ||
                       dluma_xui_native_manager_generation(manager_now)!=g_hud.diagnostic_manager_generation ||
                       !dluma_xui_observe_borrowed_receiver(found->element,level_now,(void*)manager_now,
                           found->diagnostic_instance_generation,&seen) || !seen.exact_input ||
                       seen.manager_generation!=g_hud.diagnostic_manager_generation ||
                       seen.instance_generation!=found->diagnostic_instance_generation ||
                       seen.load_serial!=found->diagnostic_load_serial) { result=-2; __leave; }
                }
#else
                (void)private_operation;
#endif
                lock(); commit=!g_dluma.stop_requested && dluma_hud_gate_can_commit(&g_hud.resolver_gate); unlock();
                if(!commit) result=-2;
                else {
                    int visitor_ok;
                    native_entered=1;
                    visitor_ok=visitor(user,found->element);
                    lock(); commit=!g_dluma.stop_requested && dluma_hud_gate_can_commit(&g_hud.resolver_gate); unlock();
                    if(!visitor_ok || !commit) result=-4;
                }
            }
        } }
    } __except(1) { result=native_entered ? -4 : -2; }
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    if(private_operation) { g_hud.diagnostic_traversal=0; g_hud.diagnostic_target[0]=0;
        g_hud.diagnostic_level=0; g_hud.diagnostic_manager=0; g_hud.diagnostic_manager_generation=0; }
#endif
    dluma_hud_resolver_leave();
    return result;
}
int dluma_hud_visibility_backend_visit(const char* screen,const char* target,
    const char* required_class,DlumaHudElementVisitor visitor,void* user)
{ return dluma_hud_visibility_backend_visit_impl(screen,target,required_class,visitor,user,0); }
static int dluma_hud_position_visit(void* user,void* element)
{
    g_hud.set_position(element,(const float*)user);
    return 1;
}
int dluma_hud_visibility_backend_set_position(void* unused,const char* screen,
    const char* target,const float position[3])
{
    (void)unused;
    if(!position || !dluma_hud_visibility_backend_position_available()) return -2;
    return dluma_hud_visibility_backend_visit_impl(screen,target,0,
        dluma_hud_position_visit,(void*)position,1);
}
int dluma_hud_visibility_backend_visual_available(void)
{
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
    DlumaXuiDiagnosticFacts facts=dluma_hud_diagnostic_current();
    return dluma_xui_diagnostic_ready(&facts) && g_hud.installed && !g_hud.stopping && g_hud.set_opacity && g_hud.set_color;
#else
    return 0;
#endif
}
typedef struct HudVisualCall { int property; const float* values; } HudVisualCall;
static int dluma_hud_visual_visit(void* user,void* element)
{
    const HudVisualCall* call=(const HudVisualCall*)user;
    if(call->property==1) g_hud.set_opacity(element,call->values[0]);
    else if(call->property==2) g_hud.set_color(element,call->values);
    else return 0;
    return 1;
}
int dluma_hud_visibility_backend_set_visual(void* unused,const char* screen,
    const char* target,DlumaXuiVisualProperty property,const float* values)
{
    HudVisualCall call={property,values}; (void)unused;
    if(!values || !dluma_hud_visibility_backend_visual_available() ||
       (property!=1 && property!=2)) return -2;
    return dluma_hud_visibility_backend_visit_impl(screen,target,
        property==2 ? "IUIElementView" : 0,dluma_hud_visual_visit,&call,property==2 ? 3 : 2);
}
int dluma_hud_visibility_backend_named_available(DlumaXuiNamedAction action)
{
    if(!DLUMA_HUD_ELEMENT_LIFETIME_VERIFIED || !g_hud.installed || g_hud.stopping) return 0;
    return action==DLUMA_XUI_TEXT_KEY ? g_hud.set_text!=0 :
           action==DLUMA_XUI_ANIMATION ? g_hud.start_animation!=0 : 0;
}
typedef struct HudNamedCall { DlumaXuiNamedAction action; HudString value; int accepted; } HudNamedCall;
static int dluma_hud_named_visit(void* user,void* element)
{
    HudNamedCall* call=(HudNamedCall*)user;
    if(call->action==DLUMA_XUI_TEXT_KEY) { g_hud.set_text(element,&call->value); call->accepted=1; }
    else if(call->action==DLUMA_XUI_ANIMATION) call->accepted=g_hud.start_animation(element,&call->value,0)!=0;
    else return 0;
    return 1;
}
static int dluma_hud_named_result(int result,int accepted)
{ return result==1 && !accepted ? -4 : result; }
int dluma_hud_visibility_backend_apply_named(void* unused,const char* screen,
    const char* target,DlumaXuiNamedAction action,const char* value)
{
    HudNamedCall call; int result; (void)unused;
    if(!value || !dluma_hud_visibility_backend_named_available(action)) return -2;
    memset(&call,0,sizeof(call)); call.action=action; call.value.data=value;
    call.value.size=(unsigned)strlen(value); call.value.capacity=call.value.size;
    result=dluma_hud_visibility_backend_visit(screen,target,
        action==DLUMA_XUI_TEXT_KEY ? "IUIText" : "IUIElementTimeLine",dluma_hud_named_visit,&call);
    /* False does not prove no side effects occurred after native entry. */
    return dluma_hud_named_result(result,call.accepted);
}
static void dluma_hud_apply_impl(const DlumaHudVisibilityRule* rules,unsigned count)
{
    unsigned i,j; void* current=0; HudNode* found;
    if(count>DLUMA_HUD_RULE_LIMIT ||
       (!count && !g_hud.tracked_count) || !level(&current)) return;
    __try {
        if(current!=g_hud.level) dluma_hud_reset_now();
        g_hud.level=current; dluma_hud_nodes_clear();
        for(i=0;i<count;i++) if(!scan_screen(rules[i].screen)) { dluma_hud_reset_now(); return; }
        /* Include screens with retired rules so restoration uses current nodes. */
        for(i=0;i<g_hud.tracked_count;i++) if(!scan_screen(g_hud.tracked[i].screen)) { dluma_hud_reset_now(); return; }
        lock();
        for(i=0;i<g_hud.tracked_count;) {
            HudTracked* t=&g_hud.tracked[i];
            for(j=0;j<g_hud.node_count;j++) if(g_hud.nodes[j].element==t->element && g_hud.nodes[j].object==t->object && g_hud.nodes[j].root==t->root) break;
            if(j==g_hud.node_count) { *t=g_hud.tracked[--g_hud.tracked_count]; continue; }
            t->next_hidden=t->next_show=0; i++;
        }
        unlock();
        for(i=0;i<count;i++) if(resolve(&rules[i],&found)==1) {
            unsigned char requested=0; int initial;
            lock();
            for(j=0;j<g_hud.tracked_count;j++) if(g_hud.tracked[j].element==found->element) break;
            initial=j==g_hud.tracked_count;
            unlock();
            if(initial) requested=g_hud.visible(found->element);
            lock();
            for(j=0;j<g_hud.tracked_count;j++) if(g_hud.tracked[j].element==found->element) break;
            if(j==g_hud.tracked_count && j<HUD_TRACK_LIMIT) {
                HudTracked* t=&g_hud.tracked[g_hud.tracked_count++]; memset(t,0,sizeof(*t));
                t->element=found->element; t->object=found->object; t->root=found->root;
                t->reset_generation=g_hud.reset_generation;
                dluma_str_copy(t->screen,sizeof(t->screen),found->screen);
                t->requested=requested;
            }
            if(j<HUD_TRACK_LIMIT) { if(rules[i].hidden) g_hud.tracked[j].next_hidden=1; else g_hud.tracked[j].next_show=1; }
            unlock();
        }
        for(i=0;i<g_hud.tracked_count;) {
            HudTracked copy; int retire;
            lock();
            if(g_hud.resolver_gate.reset_pending) { unlock(); return; }
            copy=g_hud.tracked[i]; retire=!copy.next_hidden && !copy.next_show;
            g_hud.tracked[i].hidden=copy.next_hidden;
            if(retire) g_hud.tracked[i]=g_hud.tracked[--g_hud.tracked_count]; else i++;
            unlock();
            g_hud.original(copy.element,retire?copy.requested:!copy.next_hidden);
        }
    } __except(1) { /* Drop references; never restore through an expired tree. */ dluma_hud_reset_now(); }
}
void dluma_hud_visibility_backend_apply(const DlumaHudVisibilityRule* rules,unsigned count)
{
    int entered,pending,cleanup,exhausted; unsigned i;
    lock();
    cleanup=count==0 || g_hud.resolver_gate.reset_pending;
    entered=g_hud.installed && (cleanup || !g_dluma.stop_requested) &&
        dluma_hud_gate_enter(&g_hud.resolver_gate,cleanup);
    pending=entered ? dluma_hud_gate_take_reset(&g_hud.resolver_gate) : 0;
    exhausted=g_hud.reset_generation_exhausted;
    if(pending) {
        /* A reset is a generation boundary. An address reused in the new map
         * is not evidence that it is the old HUD element. */
        for(i=0;i<g_hud.tracked_count;) {
            if(g_hud.reset_generation_exhausted || g_hud.tracked[i].reset_generation!=g_hud.reset_generation)
                g_hud.tracked[i]=g_hud.tracked[--g_hud.tracked_count];
            else ++i;
        }
    }
    unlock();
    if(!entered) return;
    if(pending) {
        dluma_hud_apply_impl(0,0);
        lock();
        if(g_hud.tracked_count) g_hud.resolver_gate.reset_pending=1;
        else dluma_hud_reset_locked();
        unlock();
    } else if(!exhausted) dluma_hud_apply_impl(rules,count);
    dluma_hud_resolver_leave();
}
void dluma_hud_visibility_backend_reset(void)
{
    lock();
    if(g_hud.reset_generation==UINT64_MAX) g_hud.reset_generation_exhausted=1;
    else ++g_hud.reset_generation;
    if(dluma_hud_gate_reset(&g_hud.resolver_gate)) dluma_hud_reset_locked();
    unlock();
}
int dluma_hud_visibility_backend_begin_document_reload(void)
{
    int admitted=0;
    lock();
    if(!g_hud.stopping && !g_dluma.stop_requested && !g_hud.reset_generation_exhausted &&
       dluma_atomic_compare_exchange(&g_hud.active,0,0)==0 &&
       !g_hud.document_reload_active && dluma_hud_gate_enter(&g_hud.resolver_gate,0)) {
        if(g_hud.reset_generation==UINT64_MAX) {
            g_hud.reset_generation_exhausted=1; dluma_hud_gate_leave(&g_hud.resolver_gate);
        } else {
            ++g_hud.reset_generation; dluma_hud_reset_locked();
            g_hud.document_reload_active=1; admitted=1;
        }
    }
    unlock(); return admitted;
}
int dluma_hud_visibility_backend_document_reload_active(void)
{
    int active;
    if(dluma_atomic_compare_exchange(&g_hud.lock,1,0))return 1;
    active=g_hud.document_reload_active!=0;
    unlock();return active;
}

void dluma_hud_visibility_backend_end_document_reload(void)
{
    lock();
    if(g_hud.document_reload_active) { g_hud.document_reload_active=0; dluma_hud_gate_leave(&g_hud.resolver_gate); }
    unlock();
}
int dluma_hud_visibility_backend_shutdown(void)
{
    int drained; unsigned tracked;
    if(!g_hud.installed) return dluma_atomic_compare_exchange(&g_hud.active,0,0)==0;
    lock(); g_hud.stopping=1; drained=dluma_hud_gate_seal(&g_hud.resolver_gate); unlock();
    if(!drained) return 0;
    dluma_hud_visibility_backend_apply(0,0);
    lock(); tracked=g_hud.tracked_count;
    drained=!tracked && dluma_hud_gate_retire(&g_hud.resolver_gate); unlock();
    if(tracked || !drained) {
        dluma_log_line("WARN","hud_visibility_game_frame_cleanup_pending_retained");
        return 0;
    }
    /* The relay stays allocated: a thread can already be executing its thunk. */
    if(exchange(g_hud.patch,g_visible_code)) g_hud.installed=0;
    dluma_hud_visibility_backend_reset();
    if(g_hud.installed) { dluma_log_line("WARN","hud_visibility_hook_restore_failed_retained"); return 0; }
    if(dluma_atomic_compare_exchange(&g_hud.active,0,0)) { dluma_log_line("WARN","hud_visibility_hook_active_retained"); return 0; }
    return 1;
}
