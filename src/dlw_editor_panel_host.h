#ifndef DLUMA_EDITOR_PANEL_HOST_H
#define DLUMA_EDITOR_PANEL_HOST_H
#include "dlw_editor_ui.h"

typedef struct DlumaEditorPanelRef {
    uint32_t size, version;
    uint64_t token;
    HWND panel_window;
    HWND content_window;
} DlumaEditorPanelRef;

enum {
    DLUMA_EDITOR_PANEL_VISIBILITY = 1,
    DLUMA_EDITOR_PANEL_RESIZED = 2,
    DLUMA_EDITOR_PANEL_CLOSING = 3,
    DLUMA_EDITOR_PANEL_CHILD_EXITED = 4,
    DLUMA_EDITOR_PANEL_FOCUS = 5
};

/* All entry points and notifications run on the native owner's UI thread.
 * VISIBILITY argument is 1 for the panel or 0 for the existing source view.
 * A notification borrows the reference only for that call. */
typedef void (*DlumaEditorPanelNotify)(const DlumaEditorPanelRef *panel,
    unsigned int event, unsigned int argument, void *context);

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize out->size and out->version to sizeof(*out) and 1. */
int dluma_editor_panel_host_create(HWND owner, HWND source,
    const WCHAR *source_title, const WCHAR *panel_title,
    DlumaEditorPanelNotify notify, void *context, DlumaEditorPanelRef *out);
/* Call after stock layout. The source keeps its native parent/notifications. */
int dluma_editor_panel_host_layout(const DlumaEditorPanelRef *panel);
int dluma_editor_panel_host_show(const DlumaEditorPanelRef *panel, int visible);
/* The companion creates its own WS_CHILD HWND under content_window. Bind
 * validates that parent and the authenticated child process; it never reparents
 * an arbitrary top-level window. process is borrowed and duplicated internally. */
int dluma_editor_panel_host_bind(const DlumaEditorPanelRef *panel,
    HWND child, DWORD expected_process_id, HANDLE process);
/* Returns the active binding generation, or zero if unbound or invalid. */
uint64_t dluma_editor_panel_host_binding_generation(const DlumaEditorPanelRef *panel);
int dluma_editor_panel_host_unbind(const DlumaEditorPanelRef *panel);
int dluma_editor_panel_host_close(const DlumaEditorPanelRef *panel);
/* After all hosts have closed, wait for asynchronous wait cleanup to drain. */
int dluma_editor_panel_host_callbacks_drained(void);

/* Graph selection posts this registered message to the bound child. WPARAM is
 * the panel token and LPARAM is the current binding generation (both x64). */
#define DLUMA_EDITOR_PANEL_FOCUS_MESSAGE_NAME L"DLUMA.EditorPanel.Focus.1"

#ifdef __cplusplus
}
#endif
#endif
