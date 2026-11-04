#ifndef DLUMA_EDITOR_UI_H
#define DLUMA_EDITOR_UI_H

#if !defined(DLUMA_WIN32_H) && !defined(_WINDOWS_)
#include <windows.h>
#endif
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DLUMA_EDITOR_UI_BEFORE = 1,
    DLUMA_EDITOR_UI_AFTER = 2,
    DLUMA_EDITOR_UI_DESTROY = 3,
    DLUMA_EDITOR_UI_OWNER_EXIT = 1,
    DLUMA_EDITOR_UI_OWNER_PRESENCE = 2,
    DLUMA_EDITOR_UI_OWNER_GRAPH = 3,
    DLUMA_EDITOR_UI_OWNER_THEME = 4,
    DLUMA_EDITOR_UI_OWNER_SPLASH = 5,
    DLUMA_EDITOR_UI_OWNER_WIKI = 6,
    DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL = 7,
    DLUMA_EDITOR_UI_SLOT_FEATURE = 0,
    DLUMA_EDITOR_UI_SLOT_CONTROLS = 1,
    DLUMA_EDITOR_UI_SLOT_SCINTILLA = 2,
    DLUMA_EDITOR_UI_SLOT_PROPERTIES = 3,
    DLUMA_EDITOR_UI_ENROLLED = 0x83D0,
    DLUMA_EDITOR_UI_REFRESH = 0x83D1
};

typedef int (*DlumaEditorUiListener)(
    HWND window, unsigned int stage, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT *result, void *context);
/* Read-only observation of nested native sends during an owned dispatch.
 * The observer cannot consume messages and shares the subscription lifetime. */
typedef void (*DlumaEditorUiObserver)(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, void *context);

/* Start arms the service; activation begins enrollment after subscribers are ready. */
int dluma_editor_ui_start(void);
int dluma_editor_ui_activate(void);
/* Nonblocking: 0 means owner-thread cleanup is still pending. */
int dluma_editor_ui_stop(void);
int dluma_editor_ui_callbacks_drained(void);
int dluma_editor_ui_subscribe(unsigned int owner,
                             DlumaEditorUiListener listener, void *context);
int dluma_editor_ui_subscribe_observed(unsigned int owner,
    DlumaEditorUiListener listener, DlumaEditorUiObserver observer, void *context);
/* No new notifications are delivered after unsubscribe. The context must
 * remain alive until it returns 1. Subscribers owning window state must first
 * restore it or keep their listener active through broker stop/DESTROY. */
int dluma_editor_ui_unsubscribe(unsigned int owner);
int dluma_editor_ui_request_enroll(HWND window);
int dluma_editor_ui_request_refresh(void);
/* Scratch belongs to this dispatch, survives BEFORE -> AFTER, and nests safely.
 * It must not be retained after the callback returns. */
uint64_t *dluma_editor_ui_invocation_value(unsigned int owner);
/* Adapters sharing a subscription keep their dispatch data in separate slots. */
uint64_t *dluma_editor_ui_invocation_slot(unsigned int owner, unsigned int slot);
typedef void (*DlumaEditorUiCleanup)(uint64_t *slot);
/* Runs at dispatch end, including exception unwinding. A cleared slot is empty. */
int dluma_editor_ui_invocation_cleanup(unsigned int owner, unsigned int slot,
                                      DlumaEditorUiCleanup cleanup);

#ifdef __cplusplus
}
#endif
#endif
