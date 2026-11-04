#ifndef DLUMA_EDITOR_WIKI_H
#define DLUMA_EDITOR_WIKI_H
#include "dlw_editor_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_wiki_start(const WCHAR *theme_config);
int dluma_editor_wiki_stop(void);
int dluma_editor_wiki_callbacks_drained(void);
/* Shared admission for another qualified native dock using the Tips class.
 * The caller owns tag for the complete window lifetime, on its UI thread. */
int dluma_editor_wiki_admit_owned_content(HWND window, const void *tag);
void dluma_editor_wiki_release_owned_content(HWND window, const void *tag);
int dluma_editor_wiki_admit_show_slot(void **slot, const void *original);
typedef struct DlumaEditorDockRef {
    HWND main, content, pane;
    unsigned int id;
    const void *tag;
    void *native_class[6];
} DlumaEditorDockRef;
/* Reuses the admitted native docking factory and its MFC resource scope. */
int dluma_editor_dock_show(DlumaEditorDockRef *dock, HWND main,
    unsigned int id, const char *title, const void *tag, int visible);
#ifdef __cplusplus
}
#endif
#endif
