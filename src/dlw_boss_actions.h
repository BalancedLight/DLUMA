#ifndef DLUMA_BOSS_ACTIONS_H
#define DLUMA_BOSS_ACTIONS_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_boss_actions_start(HMODULE gamedll);
/* Call only at the lexical pre-original-initializer site. The integer alone
 * grants no permission: exact Player/A18 profile, fresh outer export entry,
 * caller thread and the shared strict bootstrap proof are required. Retail
 * and Editor cold profiles remain unsupported. Legacy start() refuses. */
int dluma_boss_actions_start_cold(HMODULE gamedll, int before_original_initializer);
void dluma_boss_actions_on_map_generation_changed(unsigned int map_generation);
/* Internal authoritative context publication only; never a mod-facing grant.
 * Ordinary builds are a no-op. Private diagnostics validate the configured
 * project directory identity, map and generation; loss revokes pending work. */
void dluma_boss_actions_set_periodic_context(unsigned int map_generation,
    const WCHAR *project_root, const char *map_name);
/* Stop means custom work is quiescent, not that engine instructions are gone.
 * After factory publication the loader, original module, relay and late
 * instruction methods remain pinned until process exit. Stock shutdown may
 * therefore destroy its instructions after this returns one. Zero retains the
 * full dependent services while an admitted custom callback is still active. */
int dluma_boss_actions_stop(void);
int dluma_boss_actions_started(void);

#ifdef __cplusplus
}
#endif
#endif
