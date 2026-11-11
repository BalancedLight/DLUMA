#ifndef DLUMA_XUI_DOCUMENT_NATIVE_H
#define DLUMA_XUI_DOCUMENT_NATIVE_H
#include "dlw_xui_document_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Implemented beside the existing shared XUI observer. A ready result requires
 * its original startup fence, current routes and exact serialized Player frame. */
int dluma_xui_document_native_ready(void);
int dluma_xui_document_native_configured(void);
int dluma_xui_document_native_bind(const DlumaXuiDocumentIdentity*);
int dluma_xui_document_native_current(void*,const DlumaXuiDocumentIdentity*);
int dluma_xui_document_native_snapshot(void*,DlumaXuiDocumentSnapshot*);
DlumaXuiDocumentRebuildResult dluma_xui_document_native_rebuild(void*,const DlumaXuiDocumentIdentity*,const DlumaXuiDocumentSource*,const DlumaXuiDocumentSnapshot*);

typedef struct DlumaXuiDocumentStatus {
    uint64_t token;
    uint32_t state,reason;
} DlumaXuiDocumentStatus;
int dluma_xui_document_service_available(void);
int dluma_xui_document_service_begin(uint64_t,uint32_t,uint32_t,const wchar_t* project_root,const char* original_document,const char* replacement_document,uint64_t* token);
int dluma_xui_document_service_status(uint64_t,uint32_t,uint32_t,uint64_t,DlumaXuiDocumentStatus*);
int dluma_xui_document_service_restore(uint64_t,uint32_t,uint32_t,uint64_t);
int dluma_xui_document_service_cancel(uint64_t,uint32_t,uint32_t,uint64_t);
void dluma_xui_document_service_revoke(uint64_t,uint32_t);
void dluma_xui_document_service_pump(void);
/* Only a completed exact owner-destruction observation may call this. */
void dluma_xui_document_service_map_destroyed(uint32_t);
int dluma_xui_document_service_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
