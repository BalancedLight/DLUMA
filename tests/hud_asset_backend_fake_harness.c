#include <assert.h>
#include <string.h>
#include "../src/game/dlw_hud_asset_service.c"
DlumaRuntime g_dluma;
static int frame=1,doc_calls,texture_calls; static uint32_t generation=4;
static DlumaTextureSwapResult texture_stop_result=DLUMA_TEXTURE_SWAP_OK;
static uint32_t texture_state=DLUMA_TEXTURE_SERVICE_READY;
int dluma_is_readable(const void* p,SIZE_T n) { (void)n; return p!=0; }
int dluma_engine_game_frame_dispatch_active_on_current_thread(const DlumaEngineState* p) { (void)p; return frame; }
unsigned int dluma_lua_runtime_current_map_generation(void) { return generation; }
int dluma_lua_runtime_copy_project_root(unsigned int map,WCHAR* out,unsigned int cap)
{ if(map!=generation || cap<3)return 0; out[0]=L'X';out[1]=L':';out[2]=0;return 1; }
int dluma_xui_document_service_begin(uint64_t owner,uint32_t gen,uint32_t map,const wchar_t* root,const char* a,const char* b,uint64_t* token)
{ assert(owner==9&&gen==7&&map==4&&root[0]==L'X'&&!strcmp(a,"hud/a.xui")&&!strcmp(b,"hud/b.xui")); ++doc_calls;*token=100;return 0; }
int dluma_xui_document_service_status(uint64_t o,uint32_t g,uint32_t m,uint64_t t,DlumaXuiDocumentStatus* s)
{ assert(o==9&&g==7&&m==4&&t==100);s->token=t;s->state=4;s->reason=0;return 0; }
int dluma_xui_document_service_restore(uint64_t o,uint32_t g,uint32_t m,uint64_t t) { (void)o;(void)g;(void)m;return t==100?0:1; }
int dluma_xui_document_service_cancel(uint64_t o,uint32_t g,uint32_t m,uint64_t t) { return dluma_xui_document_service_restore(o,g,m,t); }
DlumaTextureSwapResult dluma_texture_service_begin(uint64_t o,uint32_t g,uint32_t m,const DlumaTextureSwapPair* p,unsigned n,uint64_t* t)
{ unsigned i;assert(o==9&&g==7&&m==4&&n==11);for(i=0;i<n;i++){assert(!strcmp(p[i].original,"stock")&&!strcmp(p[i].replacement,"custom"));}++texture_calls;*t=101;return DLUMA_TEXTURE_SWAP_OK; }
DlumaTextureSwapResult dluma_texture_service_status(uint64_t o,uint32_t g,uint32_t m,uint64_t t,DlumaTextureServiceStatus* s)
{ (void)o;(void)g;(void)m;assert(t==101);s->token=t;s->state=texture_state;s->reason=0;s->ready_count=22;s->asset_count=22;return DLUMA_TEXTURE_SWAP_OK; }
DlumaTextureSwapResult dluma_texture_service_stop(uint64_t o,uint32_t g,uint32_t m,uint64_t t)
{ (void)o;(void)g;(void)m;return t==101?texture_stop_result:DLUMA_TEXTURE_SWAP_FOREIGN; }
static DlumaStringView view(const char* s) { DlumaStringView v={s,(uint64_t)strlen(s)};return v; }
int main(void)
{
 DlumaModOwner owner={0}; DlumaHudDocumentSwapRequest doc={0}; DlumaTokenResult out={0}; DlumaTokenRequest token={0};
 DlumaHudDocumentStatusResult ds={0}; DlumaHudFlashbackTextureStatusResult ts={0}; DlumaHudFlashbackTextureRequest tex={0};
 DlumaStringView originals[11],replacements[11]; DlumaBoolResult boolean={0}; unsigned i;
 owner.owner_id=9;owner.generation=7;owner.map_generation=4;
 doc.original_document=view("hud/a.xui");doc.replacement_document=view("hud/b.xui");
 assert(dluma_game_document_invoke(&owner,14009,&doc,&out)==DLUMA_STATUS_OK && out.token==100 && doc_calls==1);
 token.token=100;assert(dluma_game_document_invoke(&owner,14011,&token,&ds)==DLUMA_STATUS_OK && ds.token==100 && ds.state==4);
 assert(dluma_game_document_invoke(&owner,14010,&token,&boolean)==DLUMA_STATUS_OK && boolean.value==1);
 doc.original_document.data="a\0b";doc.original_document.size=3;
 assert(dluma_game_document_invoke(&owner,14009,&doc,&out)==DLUMA_STATUS_INVALID_ARGUMENT && doc_calls==1);
 for(i=0;i<11;i++){originals[i]=view("stock");replacements[i]=view("custom");}
 tex.originals=originals;tex.replacements=replacements;tex.count=10;
 assert(dluma_game_texture_invoke(&owner,14013,&tex,&out)==DLUMA_STATUS_INVALID_ARGUMENT && texture_calls==0);
 tex.count=11;assert(dluma_game_texture_invoke(&owner,14013,&tex,&out)==DLUMA_STATUS_OK && out.token==101 && texture_calls==1);
 token.token=101;assert(dluma_game_texture_invoke(&owner,14014,&token,&ts)==DLUMA_STATUS_OK && ts.state==2 && ts.ready_count==22);
 assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_OK && boolean.value==1);
 texture_stop_result=DLUMA_TEXTURE_SWAP_RETRY;texture_state=DLUMA_TEXTURE_SERVICE_RESTORING;boolean.value=0;
 assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_OK && boolean.value==1);
 assert(dluma_game_texture_invoke(&owner,14014,&token,&ts)==DLUMA_STATUS_OK && ts.state==DLUMA_TEXTURE_SERVICE_RESTORING);
 texture_state=DLUMA_TEXTURE_SERVICE_STOPPED;
 assert(dluma_game_texture_invoke(&owner,14014,&token,&ts)==DLUMA_STATUS_OK && ts.state==DLUMA_TEXTURE_SERVICE_STOPPED);
 texture_stop_result=DLUMA_TEXTURE_SWAP_BUSY;boolean.value=1;
 assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_UNAVAILABLE && boolean.value==0);
 token.token=102;boolean.value=1;
 assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_FOREIGN_HANDLE && boolean.value==0);
 token.token=101;texture_stop_result=DLUMA_TEXTURE_SWAP_STALE;
 assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_STALE_HANDLE && boolean.value==0);
 frame=0;assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_WRONG_THREAD);frame=1;
 owner.map_generation=3;assert(dluma_game_texture_invoke(&owner,14015,&token,&boolean)==DLUMA_STATUS_STALE_HANDLE);
 return 0;
}
