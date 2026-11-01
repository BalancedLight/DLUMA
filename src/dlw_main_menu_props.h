#ifndef DLUMA_MAIN_MENU_PROPS_H
#define DLUMA_MAIN_MENU_PROPS_H

#include "dlw_common.h"
#include "dlw_main_menu_presentation_core.h"

int dluma_main_menu_props_start(void);
void dluma_main_menu_props_tick(
    void *background, void *actor, const DlumaMainMenuPropSet *set,
    ULONGLONG seed, ULONGLONG now);
int dluma_main_menu_props_before_body_change(void *actor, ULONGLONG now);
void dluma_main_menu_props_forget(void *background);
int dluma_main_menu_props_stop(void);

#endif
