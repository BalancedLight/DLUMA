#ifndef DLUMA_CUSTOM_MAIN_MENU_MUSIC_H
#define DLUMA_CUSTOM_MAIN_MENU_MUSIC_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_CMM_MUSIC_RESOLVE_PERMANENT (-1)
#define DLUMA_CMM_MUSIC_RESOLVE_RETRY 0
#define DLUMA_CMM_MUSIC_RESOLVE_READY 1

void dluma_custom_main_menu_music_reset(void);
void dluma_custom_main_menu_music_retry_preparation(void);
int dluma_custom_main_menu_music_prepare(void *main_menu_module);
void dluma_custom_main_menu_music_note_normal_cue(const char *cue_name);
int dluma_custom_main_menu_music_reconcile(const char *cue_name);
int dluma_custom_main_menu_music_release_screen_override(void);
int dluma_custom_main_menu_music_shutdown(void);
int dluma_custom_main_menu_music_retire_generation(void);
int dluma_custom_main_menu_music_resolve_main_cue(
    void *main_menu_module,
    const char **cue_name_out);
int dluma_custom_main_menu_music_resolve_main_cue_state(
    void *main_menu_module,
    const char **cue_name_out);
int dluma_custom_main_menu_music_resolve_credits_cue(
    void *main_menu_module,
    const char **cue_name_out);

#ifdef __cplusplus
}
#endif

#endif
