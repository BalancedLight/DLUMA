#ifndef DLUMA_MAIN_MENU_NATIVE_PRESENTATION_H
#define DLUMA_MAIN_MENU_NATIVE_PRESENTATION_H

/*
 * Exact Windows Player 1.49 main-menu integration.  This subsystem owns only
 * the two validated MenuMainDI vtable slots.  It deliberately delegates all
 * ticker animation and queuing to the stock embedded CNewsFeedGroup.
 */
void dluma_ui_first_launch_main_menu_post(void *menu_main);

int dluma_main_menu_native_presentation_start(void);
int dluma_main_menu_native_presentation_stop(void);
int dluma_main_menu_native_presentation_is_installed(void);

#endif
