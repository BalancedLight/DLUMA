#include "dlw_frontend_level.h"

#include "dlw_common.h"
#include "dlw_custom_main_menu.h"

int dluma_frontend_level_name_is_selected_custom(const char *map_name)
{
    const char *selected = dluma_custom_main_menu_selected_map();
    return map_name && map_name[0] && selected && selected[0] &&
           dluma_stricmp(map_name, selected) == 0;
}

int dluma_frontend_level_name_is_frontend(const char *map_name)
{
    return map_name && map_name[0] &&
           (dluma_stricmp(map_name, "menu_lvl_new") == 0 ||
            dluma_stricmp(map_name, "menu_lvl") == 0 ||
            dluma_frontend_level_name_is_selected_custom(map_name));
}
