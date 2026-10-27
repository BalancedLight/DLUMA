#ifndef DLUMA_PLAYER_STARTUP_H
#define DLUMA_PLAYER_STARTUP_H

#include "dlw_player_startup_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_player_startup_begin(void);
void dluma_player_startup_status(const char *status);
void dluma_player_startup_abort(const char *reason);
int dluma_player_startup_snapshot(DlumaPlayerStartupSnapshot *snapshot_out);

#ifdef __cplusplus
}
#endif

#endif
