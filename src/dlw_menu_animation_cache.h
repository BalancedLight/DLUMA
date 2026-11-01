#ifndef DLUMA_MENU_ANIMATION_CACHE_H
#define DLUMA_MENU_ANIMATION_CACHE_H

#include "dlw_common.h"

int dluma_menu_animation_cache_materialize(WCHAR *out, unsigned int capacity);
void dluma_menu_animation_cache_cleanup(void);

#endif
