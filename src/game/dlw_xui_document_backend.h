#ifndef DLUMA_XUI_DOCUMENT_BACKEND_H
#define DLUMA_XUI_DOCUMENT_BACKEND_H
#include <stddef.h>
#include <stdint.h>

#define DLUMA_XUI_DOCUMENT_PATH_CAPACITY 2048U
#define DLUMA_XUI_DOCUMENT_COMPONENTS 137U
#define DLUMA_XUI_DOCUMENT_MAX_BYTES (8U*1024U*1024U)

typedef enum DlumaXuiDocumentState {
    DLUMA_XUI_DOCUMENT_EMPTY=0, DLUMA_XUI_DOCUMENT_PREPARING,
    DLUMA_XUI_DOCUMENT_READY, DLUMA_XUI_DOCUMENT_APPLYING,
    DLUMA_XUI_DOCUMENT_APPLIED, DLUMA_XUI_DOCUMENT_RESTORING,
    DLUMA_XUI_DOCUMENT_RESTORED, DLUMA_XUI_DOCUMENT_FAILED,
    DLUMA_XUI_DOCUMENT_RECOVERY_REQUIRED, DLUMA_XUI_DOCUMENT_RELEASE_PENDING
} DlumaXuiDocumentState;
typedef enum DlumaXuiDocumentError {
    DLUMA_XUI_DOCUMENT_OK=0, DLUMA_XUI_DOCUMENT_INVALID,
    DLUMA_XUI_DOCUMENT_BUSY, DLUMA_XUI_DOCUMENT_UNAVAILABLE,
    DLUMA_XUI_DOCUMENT_SOURCE_FAILED, DLUMA_XUI_DOCUMENT_PREFLIGHT_FAILED,
    DLUMA_XUI_DOCUMENT_SCOPE_LOST, DLUMA_XUI_DOCUMENT_LOAD_FAILED,
    DLUMA_XUI_DOCUMENT_ROLLBACK_FAILED, DLUMA_XUI_DOCUMENT_RELEASE_FAILED,
    DLUMA_XUI_DOCUMENT_CANCELLED
} DlumaXuiDocumentError;
/* Internal actuator result. Refusal proves that stock native entry never
 * occurred; entered failure includes uncertain partial native mutation. */
typedef enum DlumaXuiDocumentRebuildResult {
    DLUMA_XUI_DOCUMENT_REBUILD_ENTERED_FAILURE=0,
    DLUMA_XUI_DOCUMENT_REBUILD_SUCCEEDED=1,
    DLUMA_XUI_DOCUMENT_REBUILD_REFUSED=2
} DlumaXuiDocumentRebuildResult;
typedef struct DlumaXuiDocumentIdentity {
    uint64_t owner, token;
    uint32_t owner_generation, map_generation;
} DlumaXuiDocumentIdentity;
typedef struct DlumaXuiDocumentBinding {
    uintptr_t manager;
    uint64_t manager_generation;
    DlumaXuiDocumentIdentity identity;
} DlumaXuiDocumentBinding;
/* A source is an opaque retained backend handle. Paths are copied, and remain
 * backed by the same immutable file until restoration or proven map disposal. */
typedef struct DlumaXuiDocumentSource {
    uintptr_t handle;
    char path[DLUMA_XUI_DOCUMENT_PATH_CAPACITY];
    unsigned char sha256[32];
    uint64_t bytes;
} DlumaXuiDocumentSource;
typedef struct DlumaXuiDocumentSnapshot {
    uintptr_t component_classes[DLUMA_XUI_DOCUMENT_COMPONENTS];
    int32_t component_screens[DLUMA_XUI_DOCUMENT_COMPONENTS];
    uint32_t location_count;
    uint64_t location_identity;
    uint64_t location_bound[64];
} DlumaXuiDocumentSnapshot;
typedef struct DlumaXuiDocumentOps {
    void* user;
    int (*acquire)(void*,const char*,DlumaXuiDocumentSource*);
    int (*compatible)(void*,const DlumaXuiDocumentSource*,const DlumaXuiDocumentSource*);
    int (*release)(void*,DlumaXuiDocumentSource*);
    int (*current)(void*,const DlumaXuiDocumentIdentity*);
    int (*snapshot)(void*,DlumaXuiDocumentSnapshot*);
    /* Rebuild uses the stock HUD owner lifecycle. It must prove consumed source,
     * logical HUD.xui, all previously occupied component roles and Location refs.
     * It returns failure after any uncertain entry; only rebuild(original) can
     * subsequently establish restoration. Never infer success from registration. */
    DlumaXuiDocumentRebuildResult (*rebuild)(void*,const DlumaXuiDocumentIdentity*,const DlumaXuiDocumentSource*,const DlumaXuiDocumentSnapshot*);
} DlumaXuiDocumentOps;
typedef struct DlumaXuiDocumentBackend {
    DlumaXuiDocumentOps ops;
    DlumaXuiDocumentIdentity identity;
    DlumaXuiDocumentSource original,replacement;
    DlumaXuiDocumentSnapshot before;
    DlumaXuiDocumentState state;
    DlumaXuiDocumentError error;
    unsigned char entered, cancel_requested, restore_requested, original_valid;
} DlumaXuiDocumentBackend;

#ifdef __cplusplus
extern "C" {
#endif
void dluma_xui_document_initialize(DlumaXuiDocumentBackend*,const DlumaXuiDocumentOps*);
int dluma_xui_document_binding_matches(const DlumaXuiDocumentBinding*,DlumaXuiDocumentIdentity,uintptr_t,uint64_t);
DlumaXuiDocumentError dluma_xui_document_prepare(DlumaXuiDocumentBackend*,DlumaXuiDocumentIdentity,const char*,const char*);
DlumaXuiDocumentError dluma_xui_document_apply(DlumaXuiDocumentBackend*);
DlumaXuiDocumentError dluma_xui_document_restore(DlumaXuiDocumentBackend*,DlumaXuiDocumentIdentity);
DlumaXuiDocumentError dluma_xui_document_cancel(DlumaXuiDocumentBackend*,DlumaXuiDocumentIdentity);
DlumaXuiDocumentError dluma_xui_document_release(DlumaXuiDocumentBackend*);
int dluma_xui_document_map_destroyed(DlumaXuiDocumentBackend*,uint32_t);
#ifdef __cplusplus
}
#endif
#endif
