#ifndef DLUMA_EDITOR_WIKI_SESSION_H
#define DLUMA_EDITOR_WIKI_SESSION_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "dlw_editor_panel_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open, dispatch and close are called on the panel owner's UI thread. Open
 * queues runtime validation and launch; completion arrives through dispatch.
 * The owner must forward its messages to dispatch and close the session before
 * destroying the panel host. Drain must complete before unloading the module.
 * When dispatch returns 1, result is -1 for an asynchronous launch/handshake
 * failure and 0 for a processed binding request. */
int dluma_editor_wiki_session_open(HWND owner, const DlumaEditorPanelRef *panel,
                                   const WCHAR *theme_config);
int dluma_editor_wiki_session_dispatch(HWND owner, UINT message, WPARAM wparam,
                                       LPARAM lparam, LRESULT *result);
void dluma_editor_wiki_session_close(void);
int dluma_editor_wiki_session_callbacks_drained(void);

#ifdef __cplusplus
}
#endif

#endif
