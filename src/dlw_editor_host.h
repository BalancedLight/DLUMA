#ifndef DLUMA_EDITOR_HOST_H
#define DLUMA_EDITOR_HOST_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_editor_host_start(void);
int dluma_editor_host_session_begin(ULONGLONG generation);
int dluma_editor_host_session_end(ULONGLONG generation);
void dluma_editor_host_crash_context_update(
    const char *phase,
    ULONGLONG current_generation,
    ULONGLONG last_generation);
int dluma_editor_host_callbacks_drained(void);
int dluma_editor_host_callback_enter(void);
void dluma_editor_host_callback_leave(int thread_tracked);
int dluma_editor_host_current_thread_in_callback(void);

#ifdef __cplusplus
}
#endif

#endif
