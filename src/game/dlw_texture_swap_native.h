#ifndef DLUMA_TEXTURE_SWAP_NATIVE_H
#define DLUMA_TEXTURE_SWAP_NATIVE_H
#include "dlw_texture_swap_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum DlumaTextureServiceState {
    DLUMA_TEXTURE_SERVICE_EMPTY=0, DLUMA_TEXTURE_SERVICE_PREPARING=1,
    DLUMA_TEXTURE_SERVICE_READY=2, DLUMA_TEXTURE_SERVICE_ACTIVE=3,
    DLUMA_TEXTURE_SERVICE_RESTORING=4, DLUMA_TEXTURE_SERVICE_STOPPED=5,
    DLUMA_TEXTURE_SERVICE_FAILED=6
} DlumaTextureServiceState;
typedef struct DlumaTextureServiceStatus {
    uint64_t token;
    uint32_t state,reason,ready_count,asset_count;
} DlumaTextureServiceStatus;
/* Names are copied synchronously. Exactly eleven disjoint ordered alias pairs
 * are accepted. Tokens are positive and never exceed INT64_MAX. Begin queues
 * bounded preflight; READY does not mean a native image was changed. */
DlumaTextureSwapResult dluma_texture_service_begin(uint64_t,uint32_t,uint32_t,
    const DlumaTextureSwapPair*,unsigned,uint64_t*);
DlumaTextureSwapResult dluma_texture_service_status(uint64_t,uint32_t,uint32_t,
    uint64_t,DlumaTextureServiceStatus*);
DlumaTextureSwapResult dluma_texture_service_stop(uint64_t,uint32_t,uint32_t,uint64_t);
/* A zero generation revokes every generation of this same nonzero owner. */
void dluma_texture_service_revoke(uint64_t,uint32_t);
void dluma_texture_service_pump(void);
/* A generation change alone is insufficient: caller must have observed actual
 * matching-map UI disposal before retiring a still-bound native image. */
void dluma_texture_service_map_destroyed(uint32_t);
int dluma_texture_service_shutdown(void);
int dluma_texture_native_ready(void);
int dluma_texture_service_available(void);
/* Pure admission check before a document rebuild. A bound image must first
 * complete texture restoration; lock contention also refuses admission. */
int dluma_texture_native_document_reload_ready(void);
/* Reserve the same texture lock's unbound state across a native document
 * rebuild. A successful begin must be paired with end on every exit path. */
int dluma_texture_native_begin_document_reload(void);
void dluma_texture_native_end_document_reload(void);
void dluma_texture_native_manager_destroying(uintptr_t,uint64_t);
void dluma_texture_native_manager_destroyed(uintptr_t,uint64_t);
#ifdef __cplusplus
}
#endif
#endif
