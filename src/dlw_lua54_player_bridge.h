#ifndef DLUMA_LUA54_PLAYER_BRIDGE_H
#define DLUMA_LUA54_PLAYER_BRIDGE_H

#include "dlw_lua54_api.h"

typedef struct lua_State lua_State;

typedef struct DlumaLua54PlayerBridgeContext {
    lua_State* state;
    const DlumaLua54HostCallbacks* host;
    void* host_user;
    uint32_t scope;
    uint32_t map_generation;
    uint64_t owner_id;
    uint32_t active;
} DlumaLua54PlayerBridgeContext;

/* The bridge is Lua marshalling only. It receives an explicit, narrow context
 * and invokes generated POD operations through the shared host dispatcher. */
void dluma_lua54_player_bridge_register(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context,
    int dluma_table_index);
void dluma_lua54_player_bridge_teardown(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context);

#endif
