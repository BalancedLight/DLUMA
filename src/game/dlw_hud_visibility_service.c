#include "dlw_hud_visibility_service.h"
#include "dlw_hud_visibility_backend.h"
#include "dlw_xui_element_core.h"
#include "../dlw_common.h"
#include <string.h>

typedef struct HudOverride {
    uint64_t owner;
    uint32_t owner_generation,map_generation;
    char selector[DLUMA_HUD_TARGET_CAPACITY];
    DlumaHudVisibilityRule rule;
} HudOverride;
static HudOverride g_rules[DLUMA_HUD_RULE_LIMIT];
static void* g_rule_lock;
static uint32_t g_surface_mask;
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void* lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void* lock);
static const char* const g_friendly[]={"health","stamina","radar","weapons","crosshair","reload"};
static const char* const g_authored[]={"HudHealthIndicator","HudStamina","HudRadar","HudPrimaryWeaponIndicator","HudSecondaryWeaponIndicator","HudWeaponSelection","HudCrosshair","HudReloadNotify"};
static const unsigned g_bits[]={1,2,4,8,8,8,16,32};

static int copy_view(char* out,unsigned capacity,DlumaStringView view,int empty)
{
    size_t i;
    if(view.size>=capacity || (!view.size && !empty) || (view.size && (!view.data || !dluma_is_readable(view.data,(SIZE_T)view.size)))) return 0;
    for(i=0;i<(size_t)view.size;i++) if((unsigned char)view.data[i]<32 || view.data[i]=='\\' || view.data[i]==':') return 0;
    if(view.size) memcpy(out,view.data,(size_t)view.size); out[view.size]=0;
    return 1;
}
static int copy_name(char* out,unsigned capacity,DlumaStringView view)
{
    size_t i;
    if(!view.data || !view.size || view.size>=capacity || !dluma_is_readable(view.data,(SIZE_T)view.size)) return 0;
    for(i=0;i<(size_t)view.size;i++) if((unsigned char)view.data[i]<32) return 0;
    memcpy(out,view.data,(size_t)view.size); out[view.size]=0; return 1;
}
static unsigned expand(const char* target,const char** ids)
{
    unsigned i,index=0;
    for(i=0;i<6;i++) if(!dluma_strcmp(target,g_friendly[i])) break;
    if(i==6) { ids[0]=target; return 1; }
    { unsigned bit=1U<<i,j; for(j=0;j<8;j++) if(g_bits[j]==bit) ids[index++]=g_authored[j]; }
    return index;
}
void dluma_hud_visibility_prepare_capabilities(DlumaCapabilityRegistry* registry)
{
    static const char message[]="HUD visibility unavailable";
    static const char element_message[]="XUI element controls unavailable";
    DlumaStringView reason={message,sizeof(message)-1U}; int ready=dluma_hud_visibility_backend_initialize();
    if(ready) { reason.data=0; reason.size=0; }
    if(registry) dluma_capability_registry_set(registry,DLUMA_CAPABILITY_HUD_VISIBILITY,ready,ready,reason);
    if(registry) {
        int position_ready=ready && dluma_hud_visibility_backend_position_available();
        DlumaStringView position_reason={element_message,sizeof(element_message)-1U};
        if(position_ready) { position_reason.data=0; position_reason.size=0; }
        dluma_capability_registry_set(registry,DLUMA_CAPABILITY_HUD_POSITION,position_ready,position_ready,position_reason);
        position_ready=ready && dluma_hud_visibility_backend_visual_available();
        position_reason.data=position_ready ? 0 : element_message;
        position_reason.size=position_ready ? 0 : sizeof(element_message)-1U;
        dluma_capability_registry_set(registry,DLUMA_CAPABILITY_HUD_VISUAL,position_ready,position_ready,position_reason);
        position_ready=ready && dluma_hud_visibility_backend_named_available(DLUMA_XUI_TEXT_KEY);
        position_reason.data=position_ready ? 0 : element_message;
        position_reason.size=position_ready ? 0 : sizeof(element_message)-1U;
        dluma_capability_registry_set(registry,DLUMA_CAPABILITY_HUD_TEXT_KEY,position_ready,position_ready,position_reason);
        position_ready=ready && dluma_hud_visibility_backend_named_available(DLUMA_XUI_ANIMATION);
        position_reason.data=position_ready ? 0 : element_message;
        position_reason.size=position_ready ? 0 : sizeof(element_message)-1U;
        dluma_capability_registry_set(registry,DLUMA_CAPABILITY_HUD_ANIMATION,position_ready,position_ready,position_reason);
    }
}
void dluma_hud_visibility_pump(void)
{
    DlumaHudVisibilityRule rules[DLUMA_HUD_RULE_LIMIT]; unsigned i,count=0; uint32_t mask;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return;
    AcquireSRWLockExclusive(&g_rule_lock);
    for(i=0;i<DLUMA_HUD_RULE_LIMIT;i++) if(g_rules[i].owner) rules[count++]=g_rules[i].rule;
    mask=g_surface_mask;
    for(i=0;i<8 && count<DLUMA_HUD_RULE_LIMIT;i++) if(mask&g_bits[i]) {
        DlumaHudVisibilityRule* rule=&rules[count++]; memset(rule,0,sizeof(*rule));
        dluma_str_copy(rule->screen,sizeof(rule->screen),"HUD.xui");
        dluma_str_copy(rule->target,sizeof(rule->target),g_authored[i]); rule->hidden=1;
    }
    ReleaseSRWLockExclusive(&g_rule_lock);
    dluma_hud_visibility_backend_apply(rules,count);
}
DlumaStatus dluma_hud_visibility_invoke(DlumaModOwner* owner,uint32_t operation_id,const void* request,void* result)
{
    const DlumaHudVisibilityRequest* r=(const DlumaHudVisibilityRequest*)request;
    char screen[DLUMA_HUD_SCREEN_CAPACITY],target[DLUMA_HUD_TARGET_CAPACITY];
    const char* ids[3]; unsigned count,i,j,free_count=0,owned=0; DlumaHudVisibilityRule candidate;
    if(!owner || !request || !result || operation_id<14001U || operation_id>14008U) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return DLUMA_STATUS_WRONG_THREAD;
    if(!copy_view(screen,sizeof(screen),r->screen,1) || !copy_view(target,sizeof(target),r->target,0)) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(!screen[0]) dluma_str_copy(screen,sizeof(screen),"HUD.xui");
    else {
        size_t length=strlen(screen);
        if(length<4 || dluma_stricmp(screen+length-4,".xui")) {
            if(length+4>=sizeof(screen)) return DLUMA_STATUS_INVALID_ARGUMENT;
            memcpy(screen+length,".xui",5);
        }
    }
    if(strchr(screen,'/') || !dluma_strcmp(target,"/") || strstr(target,"//") || target[0]=='/' || target[strlen(target)-1]=='/') return DLUMA_STATUS_INVALID_ARGUMENT;
    if(operation_id==14004U) {
        const DlumaHudPositionRequest* position_request=(const DlumaHudPositionRequest*)request;
        DlumaXuiElementPositionBackend backend={0,dluma_hud_visibility_backend_set_position};
        float position[3]={position_request->position.x,position_request->position.y,position_request->position.z};
        DlumaXuiElementResult status;
        if(!dluma_hud_visibility_backend_initialize() || !dluma_hud_visibility_backend_position_available() ||
            !dluma_hud_visibility_backend_diagnostic_owner_allowed(owner->scope,owner->map_generation)) return DLUMA_STATUS_UNAVAILABLE;
        status=dluma_xui_element_set_position(&backend,screen,target,position);
        ((DlumaBoolResult*)result)->value=status==DLUMA_XUI_ELEMENT_OK;
        if(status==DLUMA_XUI_ELEMENT_OK) return DLUMA_STATUS_OK;
        if(status==DLUMA_XUI_ELEMENT_UNCERTAIN) return DLUMA_STATUS_INTERNAL_ERROR;
        if(status==DLUMA_XUI_ELEMENT_NOT_FOUND) return DLUMA_STATUS_NOT_FOUND;
        if(status==DLUMA_XUI_ELEMENT_INVALID || status==DLUMA_XUI_ELEMENT_AMBIGUOUS || status==DLUMA_XUI_ELEMENT_WRONG_TYPE) return DLUMA_STATUS_INVALID_ARGUMENT;
        return DLUMA_STATUS_UNAVAILABLE;
    }
    if(operation_id==14005U || operation_id==14006U) {
        const DlumaHudOpacityRequest* opacity=(const DlumaHudOpacityRequest*)request;
        const DlumaHudColorRequest* color=(const DlumaHudColorRequest*)request;
        DlumaXuiElementVisualBackend backend={0,dluma_hud_visibility_backend_set_visual};
        float values[4]; DlumaXuiElementResult status;
        DlumaXuiVisualProperty property=operation_id==14005U ? DLUMA_XUI_VISUAL_OPACITY : DLUMA_XUI_VISUAL_COLOR;
        if(property==DLUMA_XUI_VISUAL_OPACITY) values[0]=opacity->opacity;
        else { values[0]=color->red; values[1]=color->green; values[2]=color->blue; values[3]=color->alpha; }
        if(!dluma_hud_visibility_backend_initialize() || !dluma_hud_visibility_backend_visual_available() ||
            !dluma_hud_visibility_backend_diagnostic_owner_allowed(owner->scope,owner->map_generation)) return DLUMA_STATUS_UNAVAILABLE;
        status=dluma_xui_element_set_visual(&backend,screen,target,property,values);
        ((DlumaBoolResult*)result)->value=status==DLUMA_XUI_ELEMENT_OK;
        if(status==DLUMA_XUI_ELEMENT_OK) return DLUMA_STATUS_OK;
        if(status==DLUMA_XUI_ELEMENT_UNCERTAIN) return DLUMA_STATUS_INTERNAL_ERROR;
        if(status==DLUMA_XUI_ELEMENT_NOT_FOUND) return DLUMA_STATUS_NOT_FOUND;
        if(status==DLUMA_XUI_ELEMENT_INVALID || status==DLUMA_XUI_ELEMENT_AMBIGUOUS || status==DLUMA_XUI_ELEMENT_WRONG_TYPE) return DLUMA_STATUS_INVALID_ARGUMENT;
        return DLUMA_STATUS_UNAVAILABLE;
    }
    if(operation_id==14007U || operation_id==14008U) {
        const DlumaHudNamedRequest* named_request=(const DlumaHudNamedRequest*)request;
        DlumaXuiElementNamedBackend backend={0,dluma_hud_visibility_backend_apply_named};
        DlumaXuiNamedAction action=operation_id==14007U ? DLUMA_XUI_TEXT_KEY : DLUMA_XUI_ANIMATION;
        char name[128]; DlumaXuiElementResult status;
        if(!copy_name(name,sizeof(name),named_request->name)) return DLUMA_STATUS_INVALID_ARGUMENT;
        if(!dluma_hud_visibility_backend_initialize() || !dluma_hud_visibility_backend_named_available(action)) return DLUMA_STATUS_UNAVAILABLE;
        status=dluma_xui_element_apply_named(&backend,screen,target,action,name);
        ((DlumaBoolResult*)result)->value=status==DLUMA_XUI_ELEMENT_OK;
        if(status==DLUMA_XUI_ELEMENT_OK) return DLUMA_STATUS_OK;
        if(status==DLUMA_XUI_ELEMENT_UNCERTAIN) return DLUMA_STATUS_INTERNAL_ERROR;
        if(status==DLUMA_XUI_ELEMENT_NOT_FOUND) return DLUMA_STATUS_NOT_FOUND;
        if(status==DLUMA_XUI_ELEMENT_INVALID || status==DLUMA_XUI_ELEMENT_AMBIGUOUS || status==DLUMA_XUI_ELEMENT_WRONG_TYPE) return DLUMA_STATUS_INVALID_ARGUMENT;
        return DLUMA_STATUS_UNAVAILABLE;
    }
    count=expand(target,ids);
    memset(&candidate,0,sizeof(candidate)); dluma_str_copy(candidate.screen,sizeof(candidate.screen),screen);
    if(operation_id!=14003U) {
        if(!dluma_hud_visibility_backend_initialize()) return DLUMA_STATUS_UNAVAILABLE;
        for(i=0;i<count;i++) {
            int resolved; dluma_str_copy(candidate.target,sizeof(candidate.target),ids[i]);
            resolved=dluma_hud_visibility_backend_validate(&candidate);
            if(resolved==-1) return DLUMA_STATUS_INVALID_ARGUMENT;
            if(resolved==-2) return DLUMA_STATUS_LIMIT_EXCEEDED;
            if(!resolved) return DLUMA_STATUS_NOT_FOUND;
        }
    }
    AcquireSRWLockExclusive(&g_rule_lock);
    for(i=0;i<DLUMA_HUD_RULE_LIMIT;i++) {
        HudOverride* slot=&g_rules[i];
        if(!slot->owner) free_count++;
        else if(slot->owner==owner->owner_id && !dluma_strcmp(slot->selector,target) && !dluma_stricmp(slot->rule.screen,screen)) owned++;
    }
    /* Reserve eight entries for the existing surface suppression mask. */
    if(operation_id!=14003U && free_count+owned<count+8U) { ReleaseSRWLockExclusive(&g_rule_lock); return DLUMA_STATUS_LIMIT_EXCEEDED; }
    for(i=0;i<DLUMA_HUD_RULE_LIMIT;i++) if(g_rules[i].owner==owner->owner_id && !dluma_strcmp(g_rules[i].selector,target) && !dluma_stricmp(g_rules[i].rule.screen,screen)) memset(&g_rules[i],0,sizeof(g_rules[i]));
    if(operation_id!=14003U) for(j=0;j<count;j++) {
        for(i=0;i<DLUMA_HUD_RULE_LIMIT && g_rules[i].owner;i++) {}
        g_rules[i].owner=owner->owner_id; g_rules[i].owner_generation=owner->generation; g_rules[i].map_generation=owner->map_generation;
        dluma_str_copy(g_rules[i].selector,sizeof(g_rules[i].selector),target);
        g_rules[i].rule=candidate; dluma_str_copy(g_rules[i].rule.target,sizeof(g_rules[i].rule.target),ids[j]);
        g_rules[i].rule.hidden=operation_id==14002U;
    }
    ReleaseSRWLockExclusive(&g_rule_lock);
    dluma_hud_visibility_pump(); ((DlumaBoolResult*)result)->value=1;
    return DLUMA_STATUS_OK;
}
void dluma_hud_visibility_revoke_owner(uint64_t owner_id)
{
    unsigned i; AcquireSRWLockExclusive(&g_rule_lock);
    for(i=0;i<DLUMA_HUD_RULE_LIMIT;i++) if(g_rules[i].owner && (!owner_id || g_rules[i].owner==owner_id)) memset(&g_rules[i],0,sizeof(g_rules[i]));
    ReleaseSRWLockExclusive(&g_rule_lock); dluma_hud_visibility_pump();
}
void dluma_hud_visibility_surface_mask(uint32_t mask)
{
    AcquireSRWLockExclusive(&g_rule_lock); g_surface_mask=mask&63U; ReleaseSRWLockExclusive(&g_rule_lock);
    dluma_hud_visibility_pump();
}
void dluma_hud_visibility_reset_map(uint32_t generation)
{
    (void)generation; AcquireSRWLockExclusive(&g_rule_lock); memset(g_rules,0,sizeof(g_rules)); g_surface_mask=0; ReleaseSRWLockExclusive(&g_rule_lock);
    /* A map transition may reuse the live HUD. Restore its last game state
     * while the validated tree is still available, then retire references. */
    dluma_hud_visibility_pump();
    dluma_hud_visibility_backend_reset();
}
int dluma_hud_visibility_shutdown(void)
{
    dluma_hud_visibility_revoke_owner(0); dluma_hud_visibility_surface_mask(0);
    return dluma_hud_visibility_backend_shutdown();
}
