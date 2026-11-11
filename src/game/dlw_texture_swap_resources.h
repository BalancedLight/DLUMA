#ifndef DLUMA_TEXTURE_SWAP_RESOURCES_H
#define DLUMA_TEXTURE_SWAP_RESOURCES_H
#include "dlw_texture_swap_core.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Acquisition returns a unique nonzero retained handle only after proving that
 * this exact alias remains bound to decoded, usable backing. On failure it
 * cleans up its own partial work. Release returns zero without consuming the
 * handle if it needs retry. Validation rechecks the exact retained alias and
 * backing in the same serialized native borrow, and must not mutate it.
 * All calls and callbacks are serialized and non-reentrant. */
typedef uintptr_t (*DlumaTextureAcquire)(void*, const char*);
typedef int (*DlumaTextureRelease)(void*, uintptr_t);
typedef int (*DlumaTextureValidate)(void*, uintptr_t, const char*);
/* Poll transfers any nonzero handle immediately, including while pending.
 * Return 1 ready, 0 pending, -1 terminal failure. Never replace an owned handle.
 * Release must also accept a partially prepared handle. */
typedef int (*DlumaTexturePoll)(void*,const char*,uintptr_t*);
typedef struct DlumaTextureResources {
    DlumaTextureSwap swap;
    DlumaTextureSwap preparing;
    uintptr_t handles[DLUMA_TEXTURE_SWAP_MAX_PAIRS * 2U];
    unsigned count;
    DlumaTextureRelease release;
    DlumaTextureValidate validate;
    void* user;
    DlumaTexturePoll poll;
    unsigned ready_count;
} DlumaTextureResources;
DlumaTextureSwapResult dluma_texture_resources_stage(DlumaTextureResources*,uint64_t,uint32_t,uint32_t,const DlumaTextureSwapPair*,unsigned,DlumaTexturePoll,DlumaTextureRelease,DlumaTextureValidate,void*);
DlumaTextureSwapResult dluma_texture_resources_pump(DlumaTextureResources*,unsigned budget);
DlumaTextureSwapResult dluma_texture_resources_cancel_stage(DlumaTextureResources*);
DlumaTextureSwapResult dluma_texture_resources_begin(DlumaTextureResources*,uint64_t,uint32_t,uint32_t,const DlumaTextureSwapPair*,unsigned,DlumaTextureAcquire,DlumaTextureRelease,DlumaTextureValidate,void*);
DlumaTextureSwapResult dluma_texture_resources_stop(DlumaTextureResources*,uint64_t,uint32_t,uint32_t);
DlumaTextureCallResult dluma_texture_resources_dispatch(DlumaTextureResources*,int,uint32_t,uint64_t,const char*,DlumaTextureSwapSet,void*);
void dluma_texture_resources_map_destroyed(DlumaTextureResources*,uint32_t);
/* Releases in reverse order only when the authoritative swap is retired. */
DlumaTextureSwapResult dluma_texture_resources_cleanup(DlumaTextureResources*);
#ifdef __cplusplus
}
#endif
#endif
