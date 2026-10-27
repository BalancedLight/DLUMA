#ifndef DLUMA_ANIMATION_REFRESH_RUNTIME_H
#define DLUMA_ANIMATION_REFRESH_RUNTIME_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Performs bounded project-owned filesystem admission and emits a receipt.
 * No engine operation is enabled until its exact ABI and owner thread have
 * independent static evidence. */
void dluma_animation_refresh_runtime_poll_project(
    const WCHAR* project_root);

/* Forces one bounded scan from an already validated active Editor project.
 * Returns nonzero only when at least one previously unreceipted request was
 * consumed. It never performs a manifest-less reload. */
int dluma_animation_refresh_runtime_poll_project_now(
    const WCHAR* project_root,
    const char* command_source);

/* Consumes at most one bounded ProjectRPack registration request. The only
 * production caller is the established Player game-thread pump. */
void dluma_animation_refresh_runtime_player_game_thread_pump(void);

/* Releases a retained Player animation pack only when called by the thread
 * previously established by the Player game-thread pump. */
int dluma_animation_refresh_runtime_player_game_thread_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
