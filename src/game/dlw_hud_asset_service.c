#include "dlw_hud_asset_service.h"
#include "dlw_xui_document_native.h"
#include "dlw_texture_swap_native.h"
#include "../dlw_lua_project_exit.h"
#include "../dlw_common.h"
#include <string.h>

static int dluma_game_copy_asset_path(char* out,unsigned capacity,DlumaStringView value)
{
    uint64_t i;
    if(!out || !capacity || !value.data || !value.size || value.size>=capacity ||
       !dluma_is_readable(value.data,(SIZE_T)value.size)) return 0;
    for(i=0;i<value.size;++i) if((unsigned char)value.data[i]<32U || value.data[i]=='\\' || value.data[i]==':') return 0;
    memcpy(out,value.data,(size_t)value.size); out[value.size]=0; return 1;
}

static DlumaStatus dluma_game_document_error(int reason)
{
    switch(reason) {
    case DLUMA_XUI_DOCUMENT_OK: return DLUMA_STATUS_OK;
    case DLUMA_XUI_DOCUMENT_INVALID: return DLUMA_STATUS_INVALID_ARGUMENT;
    case DLUMA_XUI_DOCUMENT_SCOPE_LOST: return DLUMA_STATUS_STALE_HANDLE;
    case DLUMA_XUI_DOCUMENT_BUSY: case DLUMA_XUI_DOCUMENT_UNAVAILABLE: return DLUMA_STATUS_UNAVAILABLE;
    default: return DLUMA_STATUS_INTERNAL_ERROR;
    }
}

DlumaStatus dluma_game_document_invoke(DlumaModOwner* owner,uint32_t operation,const void* request,void* result)
{
    uint32_t map; uint64_t token; int reason;
    if(!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return DLUMA_STATUS_WRONG_THREAD;
    map=dluma_lua_runtime_current_map_generation();
    if(!map || owner->map_generation!=map) return DLUMA_STATUS_STALE_HANDLE;
    if(operation==14009U) {
        const DlumaHudDocumentSwapRequest* in=(const DlumaHudDocumentSwapRequest*)request;
        DlumaTokenResult* out=(DlumaTokenResult*)result;
        char original[DLUMA_XUI_DOCUMENT_PATH_CAPACITY],replacement[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
        WCHAR root[DLUMA_MAX_WPATH]; token=0; out->token=0;
        if(!dluma_game_copy_asset_path(original,sizeof(original),in->original_document) ||
           !dluma_game_copy_asset_path(replacement,sizeof(replacement),in->replacement_document)) return DLUMA_STATUS_INVALID_ARGUMENT;
        if(!dluma_lua_runtime_copy_project_root(map,root,DLUMA_ARRAY_COUNT(root))) return DLUMA_STATUS_UNAVAILABLE;
        reason=dluma_xui_document_service_begin(owner->owner_id,owner->generation,map,root,original,replacement,&token);
        if(reason==DLUMA_XUI_DOCUMENT_OK) { if(!token || token>INT64_MAX) return DLUMA_STATUS_INTERNAL_ERROR; out->token=token; }
        return dluma_game_document_error(reason);
    }
    token=((const DlumaTokenRequest*)request)->token;
    if(!token || token>INT64_MAX) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(operation==14011U) {
        DlumaXuiDocumentStatus status; DlumaHudDocumentStatusResult* out=(DlumaHudDocumentStatusResult*)result;
        memset(&status,0,sizeof(status));
        reason=dluma_xui_document_service_status(owner->owner_id,owner->generation,map,token,&status);
        if(reason==DLUMA_XUI_DOCUMENT_OK) { out->token=status.token; out->state=status.state; out->reason=status.reason; }
    } else {
        DlumaBoolResult* out=(DlumaBoolResult*)result; out->value=0;
        reason=operation==14010U ? dluma_xui_document_service_restore(owner->owner_id,owner->generation,map,token) :
               operation==14012U ? dluma_xui_document_service_cancel(owner->owner_id,owner->generation,map,token) : DLUMA_XUI_DOCUMENT_INVALID;
        if(reason==DLUMA_XUI_DOCUMENT_OK) out->value=1;
    }
    return dluma_game_document_error(reason);
}

static DlumaStatus dluma_game_texture_error(DlumaTextureSwapResult reason)
{
    switch(reason) {
    case DLUMA_TEXTURE_SWAP_OK: return DLUMA_STATUS_OK;
    case DLUMA_TEXTURE_SWAP_INVALID: return DLUMA_STATUS_INVALID_ARGUMENT;
    case DLUMA_TEXTURE_SWAP_FOREIGN: return DLUMA_STATUS_FOREIGN_HANDLE;
    case DLUMA_TEXTURE_SWAP_STALE: return DLUMA_STATUS_STALE_HANDLE;
    case DLUMA_TEXTURE_SWAP_NOT_READY: case DLUMA_TEXTURE_SWAP_BUSY: case DLUMA_TEXTURE_SWAP_RETRY: return DLUMA_STATUS_UNAVAILABLE;
    default: return DLUMA_STATUS_INTERNAL_ERROR;
    }
}
DlumaStatus dluma_game_texture_invoke(DlumaModOwner* owner,uint32_t operation,const void* request,void* result)
{
    uint32_t map; uint64_t token; DlumaTextureSwapResult reason;
    if(!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return DLUMA_STATUS_WRONG_THREAD;
    map=dluma_lua_runtime_current_map_generation();
    if(!map || owner->map_generation!=map) return DLUMA_STATUS_STALE_HANDLE;
    if(operation==14013U) {
        const DlumaHudFlashbackTextureRequest* in=(const DlumaHudFlashbackTextureRequest*)request;
        DlumaTokenResult* out=(DlumaTokenResult*)result;
        DlumaTextureSwapPair pairs[11]; char names[22][DLUMA_TEXTURE_SWAP_NAME_CAPACITY]; unsigned i;
        out->token=0; token=0;
        if(in->count!=11U || !in->originals || !in->replacements ||
           !dluma_is_readable(in->originals,11U*sizeof(DlumaStringView)) ||
           !dluma_is_readable(in->replacements,11U*sizeof(DlumaStringView))) return DLUMA_STATUS_INVALID_ARGUMENT;
        for(i=0;i<11U;++i) {
            if(!dluma_game_copy_asset_path(names[i],sizeof(names[i]),in->originals[i]) ||
               !dluma_game_copy_asset_path(names[i+11U],sizeof(names[i+11U]),in->replacements[i])) return DLUMA_STATUS_INVALID_ARGUMENT;
            pairs[i].original=names[i]; pairs[i].replacement=names[i+11U];
        }
        reason=dluma_texture_service_begin(owner->owner_id,owner->generation,map,pairs,11U,&token);
        if(reason==DLUMA_TEXTURE_SWAP_OK) { if(!token || token>INT64_MAX) return DLUMA_STATUS_INTERNAL_ERROR; out->token=token; }
        return dluma_game_texture_error(reason);
    }
    token=((const DlumaTokenRequest*)request)->token;
    if(!token || token>INT64_MAX) return DLUMA_STATUS_INVALID_ARGUMENT;
    if(operation==14014U) {
        DlumaTextureServiceStatus status; DlumaHudFlashbackTextureStatusResult* out=(DlumaHudFlashbackTextureStatusResult*)result;
        memset(&status,0,sizeof(status));
        reason=dluma_texture_service_status(owner->owner_id,owner->generation,map,token,&status);
        if(reason==DLUMA_TEXTURE_SWAP_OK) { out->token=status.token; out->state=status.state; out->reason=status.reason; out->ready_count=status.ready_count; out->asset_count=status.asset_count; }
    } else {
        DlumaBoolResult* out=(DlumaBoolResult*)result; out->value=0;
        reason=operation==14015U ? dluma_texture_service_stop(owner->owner_id,owner->generation,map,token) : DLUMA_TEXTURE_SWAP_INVALID;
        /* RETRY here means Stop accepted the request and cleanup is pending;
         * callers observe completion through status, without reissuing Stop. */
        if(reason==DLUMA_TEXTURE_SWAP_OK || reason==DLUMA_TEXTURE_SWAP_RETRY) {
            out->value=1; return DLUMA_STATUS_OK;
        }
    }
    return dluma_game_texture_error(reason);
}
