#ifndef DLUMA_ANALYSIS_BRIDGE_H
#define DLUMA_ANALYSIS_BRIDGE_H

#include "dlw_common.h"

#define DLUMA_ANALYSIS_BRIDGE_PROTOCOL_MAJOR 1U
#define DLUMA_ANALYSIS_BRIDGE_PROTOCOL_MINOR 0U
#define DLUMA_ANALYSIS_BRIDGE_MAX_FRAME 16384U
#define DLUMA_ANALYSIS_BRIDGE_STATE_ID_CAPACITY 17U

#ifdef __cplusplus
extern "C" {
#endif

/* Starts one private process-lifetime pipe. Player hosts pass a nonzero
 * dispatcher flag; Editor hosts expose worker-only diagnostics. */
int dluma_analysis_bridge_start(int game_thread_dispatcher_available);
void dluma_analysis_bridge_quiesce(void);
int dluma_analysis_bridge_stop(void);
int dluma_analysis_bridge_stop_succeeded(void);

/* Called only by the existing serialized Player game-thread timer. It runs
 * at most one validated immutable request per frame. */
void dluma_analysis_bridge_game_thread_pump(void);

#ifdef __cplusplus
}
#endif

#endif
