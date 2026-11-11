#ifndef DLUMA_XUI_DOCUMENT_SOURCES_H
#define DLUMA_XUI_DOCUMENT_SOURCES_H
#include "dlw_xui_document_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Worker-thread operations. Absolute paths enter only after the service has
 * resolved and checked them against its pinned project data directory. */
int dluma_xui_document_source_acquire(void*,const char*,DlumaXuiDocumentSource*);
int dluma_xui_document_source_compatible(void*,const DlumaXuiDocumentSource*,const DlumaXuiDocumentSource*);
int dluma_xui_document_source_release(void*,DlumaXuiDocumentSource*);
#ifdef __cplusplus
}
#endif
#endif
