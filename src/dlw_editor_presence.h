#ifndef DLUMA_EDITOR_PRESENCE_H
#define DLUMA_EDITOR_PRESENCE_H

#include "dlw_common.h"
#include "core/dlw_dual_host.h"

typedef struct DlumaEditorPresencePlaytestLaunch {
    DlumaPlaytestHandoff handoff;
    WCHAR player_path[DLUMA_MAX_WPATH];
} DlumaEditorPresencePlaytestLaunch;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_editor_presence_start(void);
int dluma_editor_presence_stop(void);
int dluma_editor_presence_callbacks_drained(void);
void dluma_editor_presence_note_playtest_launch(
    const DlumaEditorPresencePlaytestLaunch* launch,
    const void* process_information);

#ifdef __cplusplus
}
#endif

#endif
