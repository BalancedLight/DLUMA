#ifndef DLUMA_MAIN_MENU_ANIMATION_H
#define DLUMA_MAIN_MENU_ANIMATION_H

#include "dlw_common.h"

int dluma_main_menu_animation_start(void);
void dluma_main_menu_animation_tick(
    void *background, void *actor, int enabled, ULONGLONG now);
int dluma_main_menu_animation_prepare_model_change(void *actor, ULONGLONG now);
void dluma_main_menu_animation_forget(void *background);
void dluma_main_menu_animation_before_destroy(void *background);
void dluma_main_menu_animation_after_destroy(void *background);
int dluma_main_menu_animation_stop(void);

#endif
