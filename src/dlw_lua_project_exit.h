#ifndef DLUMA_LUA_PROJECT_EXIT_H
#define DLUMA_LUA_PROJECT_EXIT_H
#include "dlw_project_exit_authority.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaLuaProjectExitDrain {
    unsigned int expected_generation;
    unsigned int observed_generation;
    unsigned int owner_thread_confirmed;
    unsigned int accepted;
    unsigned int complete;
    unsigned int local_slots;
    unsigned int remaining_slots;
    unsigned int active_callbacks;
    char blocker[96];
} DlumaLuaProjectExitDrain;

/* Only project/map Lua callback ownership is certified. Managed/native host
 * drains and retained media/provider references require separate proofs.
 * Never runs user unload handlers at a provider-retirement boundary. */
unsigned int dluma_lua_runtime_current_map_generation(void);
int dluma_lua_runtime_revoke_local_scopes_for_exit(
    unsigned int expected_generation, DlumaLuaProjectExitDrain *out);
int dluma_lua_runtime_revoke_local_scopes_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority *authority,
    DlumaLuaProjectExitDrain *out);

#ifdef __cplusplus
}
#endif
#endif
