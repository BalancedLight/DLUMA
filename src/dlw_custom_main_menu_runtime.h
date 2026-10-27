#ifndef DLUMA_CUSTOM_MAIN_MENU_RUNTIME_H
#define DLUMA_CUSTOM_MAIN_MENU_RUNTIME_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_custom_main_menu_runtime_start(HMODULE gamedll);
void dluma_custom_main_menu_runtime_tick(
    const char *map_name, void *level, ULONGLONG now_ms);
void dluma_custom_main_menu_runtime_note_source_retired(
    unsigned int reason, ULONGLONG frontend_generation);
/* Notify only after the outgoing level and project cleanup have completed. */
void dluma_custom_main_menu_runtime_frontend_ready(void);
int dluma_custom_main_menu_runtime_audio_ready(void);
void dluma_custom_main_menu_runtime_quiesce(void);
int dluma_custom_main_menu_runtime_stop(void);

#ifdef __cplusplus
}
#endif

#endif
