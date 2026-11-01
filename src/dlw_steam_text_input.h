#ifndef DLUMA_STEAM_TEXT_INPUT_H
#define DLUMA_STEAM_TEXT_INPUT_H

/* Borrow the game's Steam session. Capacity includes the UTF-8 terminator. */
int dluma_steam_text_open(const char *initial, unsigned int capacity,
                          const char *title, unsigned int generation);
/* 0 pending, 1 accepted, 2 cancelled, -1 unavailable or invalid result. */
int dluma_steam_text_poll(unsigned int generation, char *out, unsigned int capacity);
void dluma_steam_text_cancel(unsigned int generation);

#endif
