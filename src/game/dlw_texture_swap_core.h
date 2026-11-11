#ifndef DLUMA_TEXTURE_SWAP_CORE_H
#define DLUMA_TEXTURE_SWAP_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DLUMA_TEXTURE_SWAP_MAX_PAIRS 32U
#define DLUMA_TEXTURE_SWAP_NAME_CAPACITY 128U

typedef struct DlumaTextureSwapPair { const char* original; const char* replacement; } DlumaTextureSwapPair;
typedef enum DlumaTextureSwapResult {
    DLUMA_TEXTURE_SWAP_OK, DLUMA_TEXTURE_SWAP_INVALID, DLUMA_TEXTURE_SWAP_NOT_READY,
    DLUMA_TEXTURE_SWAP_BUSY, DLUMA_TEXTURE_SWAP_FOREIGN, DLUMA_TEXTURE_SWAP_STALE,
    DLUMA_TEXTURE_SWAP_RETRY
} DlumaTextureSwapResult;
typedef struct DlumaTextureSwapEntry {
    char original[DLUMA_TEXTURE_SWAP_NAME_CAPACITY];
    char replacement[DLUMA_TEXTURE_SWAP_NAME_CAPACITY];
} DlumaTextureSwapEntry;
typedef struct DlumaTextureSwap {
    uint64_t owner, target_generation;
    uint32_t owner_generation, map_generation, count, state;
    DlumaTextureSwapEntry entries[DLUMA_TEXTURE_SWAP_MAX_PAIRS];
} DlumaTextureSwap;
/* Pure query: both sets must already be real and retained until retirement.
 * Resource acquisition and rollback belong to the backend, before begin. */
typedef int (*DlumaTextureSwapReady)(void*,const char*);
typedef int (*DlumaTextureSwapSet)(void*,const char*);
typedef enum DlumaTextureCallResult {
    DLUMA_TEXTURE_CALL_ORIGINAL, DLUMA_TEXTURE_CALL_REPLACED,
    DLUMA_TEXTURE_CALL_ROLLED_BACK, DLUMA_TEXTURE_CALL_RESTORE_PENDING,
    DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED, DLUMA_TEXTURE_CALL_FAILED
} DlumaTextureCallResult;
/* Caller serializes all operations, including the synchronous engine call. */
DlumaTextureSwapResult dluma_texture_swap_begin(DlumaTextureSwap*,uint64_t,uint32_t,uint32_t,const DlumaTextureSwapPair*,unsigned,DlumaTextureSwapReady,void*);
DlumaTextureSwapResult dluma_texture_swap_stop(DlumaTextureSwap*,uint64_t,uint32_t,uint32_t);
/* Run only within the native call while its image is valid. scoped must be
 * proven from the native callsite; target_generation must be a nonzero
 * lifetime token proven by the backend, never a pointer-derived guess.
 * The map generation must also match; zero target generation disables
 * remapping and cannot clear a pending restore. Original must
 * be a bounded, valid copy. The native wrapper forwards unscoped or unreadable
 * arguments directly to the original engine call. The callback is synchronous
 * and never retained. Caller serializes it with stop/begin. */
DlumaTextureCallResult dluma_texture_swap_dispatch(DlumaTextureSwap*,int,uint32_t,uint64_t,const char*,DlumaTextureSwapSet,void*);
/* Only after destruction of the matching map's UI has been confirmed. */
void dluma_texture_swap_map_destroyed(DlumaTextureSwap*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
