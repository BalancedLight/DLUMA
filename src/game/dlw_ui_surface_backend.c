#include "../dlw_common.h"
#include "../dlw_path_security.h"
#include "dlw_hud_surface_backend.h"

/* The screen and graphics bindings remain disabled until both Player routes
 * have been exercised with the live HUD and their cleanup verified. */
#define DLUMA_SURFACE_BACKEND_VERIFIED 0
#define SURFACE_LIMIT 4U
#define SURFACE_EDGE_LIMIT 2048U
typedef struct SurfaceString { const char *data; unsigned size, capacity; } SurfaceString;
typedef struct SurfaceVec3 { float x, y, z; } SurfaceVec3;
typedef int (__fastcall *RegisterScreen)(void *, const SurfaceString *, unsigned, int, unsigned char, const SurfaceString *, unsigned);
typedef void *(__fastcall *ScreenFn)(void *, int);
typedef void *(__fastcall *FindElement)(void *, const SurfaceString *, unsigned char);
typedef void *(__fastcall *GetOwner)(void *);
typedef int (__fastcall *GetScreenId)(void *);
typedef const SurfaceString *(__fastcall *GetScript)(void *);
typedef void (__fastcall *ScreenShow)(void *, int, void *);
typedef void (__fastcall *ScreenHide)(void *, int);
typedef unsigned char (__fastcall *ScreenUnregister)(void *, int);
typedef void *(__fastcall *TextureCreate)(void *, const SurfaceString *, int, int, unsigned, unsigned char, unsigned);
typedef void (__fastcall *TextureUpdate)(void *, void *, const void *, int, unsigned, int, unsigned, unsigned, unsigned, unsigned, unsigned);
typedef unsigned char (__fastcall *TextureRelease)(void *, void *);
typedef void (__fastcall *LoadUiTexture)(void *, const SurfaceString *, const SurfaceString *, int, int, int, int, int, int, int, int, unsigned char, SurfaceString *);
typedef int (__fastcall *UnloadUiTexture)(void *, const SurfaceString *);
typedef unsigned char (__fastcall *UiTextureLoaded)(void *, const SurfaceString *);
typedef unsigned char (__fastcall *SetImageTexture)(void *, const SurfaceString *);
typedef void (__fastcall *SetPos)(void *, const SurfaceVec3 *);
typedef void (__fastcall *SetSize)(void *, float);
typedef unsigned char (__fastcall *IsVisible)(void *);
typedef void (__fastcall *SetVisible)(void *, unsigned char);
typedef int (__fastcall *Lookup)(void *, const SurfaceString *);

typedef struct SurfaceSlot {
    uint64_t owner; uint32_t generation, width, height, edge, mask;
    void *image, *texture; unsigned char *pixels;
    char name[64], actual[128], alias[64];
    float x, y, draw_width, draw_height;
    unsigned char registered, ready, bound, visible, orphan;
} SurfaceSlot;
typedef struct SurfaceState {
    HMODULE engine, game; void *level, *root, *stock_root; int screen_id;
    uint32_t generation; unsigned char shown, orphan_scene;
    SurfaceSlot slots[SURFACE_LIMIT];
    void *stock_elements[8]; unsigned char stock_owned[8];
    RegisterScreen reg; ScreenFn load, find_screen; FindElement find_element; GetOwner get_owner;
    GetScreenId get_id; GetScript get_script; ScreenShow show; ScreenHide hide; ScreenUnregister unregister;
    TextureCreate create_texture; TextureUpdate update_texture; TextureRelease release_texture;
    LoadUiTexture load_ui; UnloadUiTexture unload_ui; UiTextureLoaded loaded;
    SetImageTexture set_texture; SetPos position; SetSize width, height;
    Lookup lookup; IsVisible is_visible; SetVisible set_visible;
} SurfaceState;
static SurfaceState g_surface;
static const char *const g_images[4] = {"I_DLUMASurface0", "I_DLUMASurface1", "I_DLUMASurface2", "I_DLUMASurface3"};
static const char *const g_names[4] = {"dluma_surface_0", "dluma_surface_1", "dluma_surface_2", "dluma_surface_3"};
static const char *const g_aliases[4] = {"dluma_surface_ui_0", "dluma_surface_ui_1", "dluma_surface_ui_2", "dluma_surface_ui_3"};
static const char *const g_stock[8] = {"HudHealthIndicator", "HudStamina", "HudRadar", "HudPrimaryWeaponIndicator", "HudSecondaryWeaponIndicator", "HudWeaponSelection", "HudCrosshair", "HudReloadNotify"};
static const uint32_t g_stock_bits[8] = {1,2,4,8,8,8,16,32};
static SurfaceString view(const char *s) { SurfaceString result; result.data=s; result.size=(unsigned)strlen(s); result.capacity=result.size; return result; }
static SurfaceSlot *slot(uint64_t owner, uint32_t generation) {
    unsigned i; for (i=0;i<SURFACE_LIMIT;i++) if (g_surface.slots[i].owner==owner && g_surface.slots[i].generation==generation) return &g_surface.slots[i]; return 0;
}
static int current_level(void **out) {
    char name[512]; ULONG_PTR vt=0;
    if (!out || !dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return 0;
    *out=0; return dluma_engine_query_session_level_identity(&g_dluma.engine,name,sizeof(name),out,&vt) && *out;
}
static int module_hash(HMODULE module, const char *a, const char *b) {
    WCHAR path[DLUMA_MAX_WPATH]; char hash[DLUMA_SHA256_HEX_CAPACITY];
    return module && dluma_get_module_path_checked(module,path,DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(path,64ULL*1024ULL*1024ULL,hash) && (!dluma_stricmp(hash,a) || !dluma_stricmp(hash,b));
}
static int initialize(void *level) {
    static const unsigned char create_code[] = {0x48,0x83,0xec,0x68,0x48,0x8b,0x02,0x48,0x8d,0x15,0x73,0xb1,0x5c,0x00};
    static const unsigned char update_code[] = {0x48,0x83,0xec,0x68,0x8b,0x84,0x24,0xc0,0x00,0x00,0x00};
    HMODULE e=GetModuleHandleW(L"engine_x64_rwdi.dll"), g=GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    BYTE *eb=(BYTE *)e, *gb=(BYTE *)g; void **vt;
    if (!module_hash(e,"A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C","5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6") ||
        !module_hash(g,"CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95","8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54")) return 0;
    if (!dluma_is_readable(level,sizeof(void *)) || !dluma_is_readable(eb+0x29c1c0,sizeof(create_code)) || !dluma_is_readable(eb+0x29c230,sizeof(update_code))) return 0;
    vt=*(void ***)level;
    if (vt!=(void **)(gb+0x1581c98) || !dluma_is_readable(vt+133,sizeof(void *)) || vt[133]!=gb+0x1481a0 ||
        memcmp(gb+0x1481a0,"\x33\xc0\xc3",3) || memcmp(eb+0x29c1c0,create_code,sizeof(create_code)) || memcmp(eb+0x29c230,update_code,sizeof(update_code))) return 0;
#define GET(field,type,name) g_surface.field=(type)GetProcAddress(e,name); if (!g_surface.field) return 0
    GET(reg,RegisterScreen,"?RegisterUIScreen@ILevel@@QEAAHAEBV?$string_base@D@ttl@@HH_N0H@Z");
    GET(load,ScreenFn,"?LoadUIScreen@ILevel@@QEAAPEAVIUIScreen@@H@Z");
    GET(find_screen,ScreenFn,"?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z");
    GET(find_element,FindElement,"?FindElement@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@_N@Z");
    GET(get_owner,GetOwner,"?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ");
    GET(get_id,GetScreenId,"?GetScreenId@IUIScreen@@QEBAHXZ");
    GET(get_script,GetScript,"?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ");
    GET(show,ScreenShow,"?ShowUIScreen@ILevel@@QEAAXHPEAVIUIData@@@Z");
    GET(hide,ScreenHide,"?HideUIScreen@ILevel@@QEAAXH@Z");
    GET(unregister,ScreenUnregister,"?UnRegisterUIScreen@ILevel@@QEAA_NH@Z");
    GET(load_ui,LoadUiTexture,"?LoadUITexture@ILevel@@QEAAXAEBV?$string_base@D@ttl@@0HHHHHHHH_NV23@@Z");
    GET(unload_ui,UnloadUiTexture,"?UnloadUITexture@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z");
    GET(loaded,UiTextureLoaded,"?IsUITextureLoaded@ILevel@@QEAA_NAEBV?$string_base@D@ttl@@@Z");
    GET(set_texture,SetImageTexture,"?SetTexture@IUIImage@@QEAA_NAEBV?$string_base@D@ttl@@@Z");
    GET(position,SetPos,"?SetRelativePos@IUIElement@@QEAAXAEBVvec3@@@Z");
    GET(width,SetSize,"?SetSizeX@IUIElement@@QEAAXM@Z");
    GET(height,SetSize,"?SetSizeY@IUIElement@@QEAAXM@Z");
    GET(lookup,Lookup,"?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z");
    GET(is_visible,IsVisible,"?IsVisible@IUIElement@@QEBA_NXZ");
    GET(set_visible,SetVisible,"?SetVisible@IUIElement@@QEAAX_N@Z");
#undef GET
    g_surface.engine=e; g_surface.game=g; g_surface.level=level;
    g_surface.create_texture=(TextureCreate)(eb+0x29c1c0); g_surface.update_texture=(TextureUpdate)(eb+0x29c230); g_surface.release_texture=(TextureRelease)(eb+0x29a9f0);
    return 1;
}
static int refresh(void) {
    void *root,*owner; const SurfaceString *script; char text[512]; const char *base; unsigned i;
    root=g_surface.find_screen(g_surface.level,g_surface.screen_id); if (!root) { g_surface.root=0; return 0; }
    owner=g_surface.get_owner(root); if (!owner || g_surface.get_id(owner)!=g_surface.screen_id) return 0;
    script=g_surface.get_script(owner); if (!script || !script->data || !script->size || script->size>=sizeof(text)) return 0;
    memcpy(text,script->data,script->size); text[script->size]=0; base=text;
    for (i=0;text[i];i++) if (text[i]=='/' || text[i]=='\\') base=text+i+1;
    if (dluma_stricmp(base,"DLUMA_SurfaceCanvas") && dluma_stricmp(base,"DLUMA_SurfaceCanvas.xui")) return 0;
    if (g_surface.root!=root) {
        g_surface.root=root; g_surface.shown=0;
        for(i=0;i<SURFACE_LIMIT;i++) { g_surface.slots[i].image=0; g_surface.slots[i].bound=0; }
    }
    for(i=0;i<SURFACE_LIMIT;i++) if (g_surface.slots[i].owner) {
        SurfaceString id=view(g_images[i]); void *image=g_surface.find_element(root,&id,1);
        if (g_surface.slots[i].image!=image) { g_surface.slots[i].image=image; g_surface.slots[i].bound=0; }
    }
    return 1;
}
static void stock(uint32_t mask) {
    SurfaceString name=view("HUD.xui"); int id=g_surface.lookup(g_surface.level,&name); void *root=id<0?0:g_surface.find_screen(g_surface.level,id); unsigned i;
    if (root) {
        void *owner=g_surface.get_owner(root); const SurfaceString *script=owner ? g_surface.get_script(owner) : 0;
        char text[512]; const char *base; unsigned j;
        if (!owner || g_surface.get_id(owner)!=id || !script || !script->data || !script->size || script->size>=sizeof(text)) root=0;
        else {
            memcpy(text,script->data,script->size); text[script->size]=0; base=text;
            for(j=0;text[j];j++) if(text[j]=='/' || text[j]=='\\') base=text+j+1;
            if(dluma_stricmp(base,"HUD") && dluma_stricmp(base,"HUD.xui")) root=0;
        }
    }
    if (root!=g_surface.stock_root) { memset(g_surface.stock_elements,0,sizeof(g_surface.stock_elements)); memset(g_surface.stock_owned,0,sizeof(g_surface.stock_owned)); g_surface.stock_root=root; }
    if (!root) return;
    for(i=0;i<8;i++) {
        SurfaceString key=view(g_stock[i]); void *item=g_surface.find_element(root,&key,1);
        if(item!=g_surface.stock_elements[i]) { g_surface.stock_elements[i]=item; g_surface.stock_owned[i]=0; }
        if(!item) continue;
        if(mask & g_stock_bits[i]) { if(g_surface.is_visible(item)) { g_surface.set_visible(item,0); g_surface.stock_owned[i]=1; } }
        else if(g_surface.stock_owned[i]) { if(!g_surface.is_visible(item)) g_surface.set_visible(item,1); g_surface.stock_owned[i]=0; }
    }
}
static uint32_t combined_mask(void) { uint32_t mask=0; unsigned i; if(!g_surface.shown) return 0; for(i=0;i<4;i++) if(g_surface.slots[i].owner && g_surface.slots[i].visible && g_surface.slots[i].bound) mask|=g_surface.slots[i].mask; return mask; }
static void screen_visibility(void) {
    unsigned i; int visible=0;
    for(i=0;i<4;i++) if(g_surface.slots[i].owner && g_surface.slots[i].visible && g_surface.slots[i].bound) visible=1;
    if(visible && !g_surface.shown) { g_surface.show(g_surface.level,g_surface.screen_id,0); g_surface.shown=1; }
    else if(!visible && g_surface.shown) { g_surface.hide(g_surface.level,g_surface.screen_id); g_surface.shown=0; }
}
static void abandon_empty(void *level) {
    unsigned i;
    for(i=0;i<4;i++) if(g_surface.slots[i].owner) return;
    if(level && level==g_surface.level && g_surface.screen_id>=0 && g_surface.unregister)
        (void)g_surface.unregister(level,g_surface.screen_id);
    memset(&g_surface,0,sizeof(g_surface));
}
int dluma_hud_backend_available(void) { return DLUMA_SURFACE_BACKEND_VERIFIED && dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine); }
int dluma_hud_backend_create(uint64_t owner,uint32_t width,uint32_t height,uint32_t generation) {
    SurfaceSlot *s=0; unsigned i; uint32_t edge=1; void *level=0; SurfaceString path,empty,name; const char *actual;
    if(!dluma_hud_backend_available() || !owner || !generation || !width || !height || width>SURFACE_EDGE_LIMIT || height>SURFACE_EDGE_LIMIT || !current_level(&level)) return 0;
    s=slot(owner,generation); if(s) { if(!s->orphan || !dluma_hud_backend_destroy(owner,generation)) return 0; s=0; }
    while(edge<width || edge<height) edge*=2;
    __try {
        if (g_surface.level && (g_surface.level!=level || g_surface.generation!=generation)) return 0;
        if (!g_surface.level) {
            if(!initialize(level)) { memset(&g_surface,0,sizeof(g_surface)); return 0; }
            g_surface.generation=generation; path=view("DLUMA_SurfaceCanvas.xui"); empty=view("");
            g_surface.screen_id=g_surface.reg(level,&path,0,0,0,&empty,0xffffffffu);
            if(g_surface.screen_id<0) { memset(&g_surface,0,sizeof(g_surface)); return 0; }
            g_surface.orphan_scene=1;
            if(!g_surface.load(level,g_surface.screen_id)) { abandon_empty(level); return 0; }
        }
        for(i=0;i<4 && g_surface.slots[i].owner;i++) {} if(i==4) return 0;
        s=&g_surface.slots[i]; memset(s,0,sizeof(*s)); s->owner=owner; s->generation=generation; s->width=width; s->height=height; s->edge=edge; s->orphan=1;
        dluma_str_copy(s->name,sizeof(s->name),g_names[i]); dluma_str_copy(s->alias,sizeof(s->alias),g_aliases[i]);
        s->pixels=(unsigned char *)dluma_alloc((SIZE_T)edge*edge*4); if(!s->pixels) { memset(s,0,sizeof(*s)); abandon_empty(level); return 0; }
        memset(s->pixels,0,(SIZE_T)edge*edge*4); name=view(s->name); s->texture=g_surface.create_texture(0,&name,(int)edge,(int)edge,1,0,2);
        if(!s->texture || !dluma_is_readable((BYTE *)s->texture+0x60,sizeof(void *))) { dluma_hud_backend_destroy(owner,generation); return 0; }
        actual=*(const char **)((BYTE *)s->texture+0x60);
        if(!actual) { dluma_hud_backend_destroy(owner,generation); return 0; }
        { unsigned j; for(j=0;j+1<sizeof(s->actual);j++) { if(!dluma_is_readable(actual+j,1)) break; s->actual[j]=actual[j]; if(!s->actual[j]) break; }
          if(!j || j+1>=sizeof(s->actual) || s->actual[j] || j<strlen(s->name) || memcmp(s->actual,s->name,strlen(s->name))) { dluma_hud_backend_destroy(owner,generation); return 0; } }
        s->draw_width=960; s->draw_height=720; s->x=160;
        if(!refresh() || !s->image) { dluma_hud_backend_destroy(owner,generation); return 0; }
        s->orphan=0; g_surface.orphan_scene=0; return 1;
    } __except(1) { if(s && s->owner==owner) (void)dluma_hud_backend_destroy(owner,generation); return 0; }
}
int dluma_hud_backend_update(uint64_t owner,uint32_t generation,const uint8_t *rgba,size_t bytes,uint32_t stride) {
    SurfaceSlot *s; void *level=0; SurfaceString alias,actual,material; unsigned y;
    if(!dluma_hud_backend_available() || !(s=slot(owner,generation)) || !rgba || stride<s->width*4 || (size_t)stride>SIZE_MAX/s->height || bytes<(size_t)stride*s->height || !current_level(&level) || level!=g_surface.level) return 0;
    __try {
        if(!refresh() || !s->image) { stock(0); return 0; }
        for(y=0;y<s->height;y++) memcpy(s->pixels+(size_t)y*s->edge*4,rgba+(size_t)y*stride,(size_t)s->width*4);
        g_surface.update_texture(0,s->texture,s->pixels,0,0,0,0,s->edge,s->edge,s->edge*4,32);
        alias=view(s->alias); actual=view(s->actual); memset(&material,0,sizeof(material));
        if(s->registered && s->ready && !g_surface.loaded(level,&alias)) { s->registered=0; s->ready=0; s->bound=0; }
        if(!s->registered) { g_surface.load_ui(level,&alias,&actual,(int)s->edge,(int)s->edge,0,0,(int)s->width,(int)s->height,0,0,0,&material); s->registered=1; }
        if(!g_surface.loaded(level,&alias)) { screen_visibility(); stock(combined_mask()); return 0; } s->ready=1;
        if(!s->bound) s->bound=g_surface.set_texture(s->image,&alias)!=0; if(!s->bound) { screen_visibility(); stock(combined_mask()); return 0; }
        { SurfaceVec3 p={s->x,s->y,0}; g_surface.width(s->image,s->draw_width); g_surface.height(s->image,s->draw_height); g_surface.position(s->image,&p); }
        g_surface.set_visible(s->image,s->visible);
        screen_visibility();
        stock(combined_mask()); return 1;
    } __except(1) { return 0; }
}
int dluma_hud_backend_layout(uint64_t owner,uint32_t generation,float x,float y,float width,float height) {
    SurfaceSlot *s; void *level=0;
    if(!dluma_hud_backend_available() || !(s=slot(owner,generation)) || !(x>=-8192 && x<=8192 && y>=-8192 && y<=8192 && width>0 && width<=8192 && height>0 && height<=8192) || !current_level(&level) || level!=g_surface.level) return 0;
    s->x=x; s->y=y; s->draw_width=width; s->draw_height=height;
    __try { if(refresh() && s->image && s->bound) { SurfaceVec3 p={x,y,0}; g_surface.width(s->image,width); g_surface.height(s->image,height); g_surface.position(s->image,&p); } } __except(1) { return 0; }
    return 1;
}
int dluma_hud_backend_visibility(uint64_t owner,uint32_t generation,int visible) {
    SurfaceSlot *s; void *level=0; if(!dluma_hud_backend_available() || !(s=slot(owner,generation)) || !current_level(&level) || level!=g_surface.level) return 0;
    s->visible=!!visible; __try { if(refresh()) { if(s->image && s->bound) g_surface.set_visible(s->image,s->visible); screen_visibility(); stock(combined_mask()); } else stock(0); } __except(1) { return 0; } return 1;
}
int dluma_hud_backend_suppress(uint64_t owner,uint32_t generation,uint32_t mask) {
    SurfaceSlot *s; void *level=0; if(!dluma_hud_backend_available() || !(s=slot(owner,generation)) || (mask & ~63u) || !current_level(&level) || level!=g_surface.level) return 0;
    s->mask=mask; __try { stock(combined_mask()); } __except(1) { return 0; } return 1;
}
int dluma_hud_backend_destroy(uint64_t owner,uint32_t generation) {
    SurfaceSlot *s; void *level=0; int same,engine_live,has_level; unsigned i,remaining=0;
    if(!(s=slot(owner,generation))) return 1;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return 0;
    engine_live=GetModuleHandleW(L"engine_x64_rwdi.dll")==g_surface.engine;
    has_level=current_level(&level);
    if(engine_live && !has_level) return 0;
    same=has_level && level==g_surface.level;
    for(i=0;i<4;i++) if(g_surface.slots[i].owner && &g_surface.slots[i]!=s) remaining++;
    __try {
        int valid_screen=same && refresh();
        if(valid_screen && s->image) g_surface.set_visible(s->image,0);
        s->visible=0; if(same) { if(valid_screen) screen_visibility(); stock(valid_screen?combined_mask():0); }
        if(same && s->registered) { SurfaceString alias=view(s->alias); (void)g_surface.unload_ui(level,&alias); s->registered=0; }
        if(engine_live && s->texture) { (void)g_surface.release_texture(0,s->texture); s->texture=0; }
        if(!remaining && same && refresh()) { if(g_surface.shown) { g_surface.hide(level,g_surface.screen_id); g_surface.shown=0; } (void)g_surface.unregister(level,g_surface.screen_id); }
    } __except(1) { return 0; }
    dluma_free(s->pixels); memset(s,0,sizeof(*s));
    if(!remaining) memset(&g_surface,0,sizeof(g_surface));
    return 1;
}
void dluma_hud_backend_pump(void) {
    unsigned i, remaining=0; void *level=0;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return;
    for(i=0;i<4;i++) if(g_surface.slots[i].owner && g_surface.slots[i].orphan)
        (void)dluma_hud_backend_destroy(g_surface.slots[i].owner,g_surface.slots[i].generation);
    for(i=0;i<4;i++) if(g_surface.slots[i].owner) remaining++;
    if(g_surface.orphan_scene && !remaining && current_level(&level)) {
        if(level==g_surface.level) { __try { abandon_empty(level); } __except(1) {} }
        else memset(&g_surface,0,sizeof(g_surface));
    }
}
