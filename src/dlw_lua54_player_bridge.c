#include "dlw_lua54_player_bridge.h"

#include "../sdk/c/dluma_api.h"

#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <string.h>

#define DLUMA_PLAYER_HANDLE_METATABLE "DLUMA.Player.v1"
#define DLUMA_PLAYER_MAX_VARIABLE_NAMES 64U
#define DLUMA_PLAYER_VARIABLE_NAME_CAPACITY 65U

typedef struct DlumaLuaPlayerHandle {
    DlumaLua54PlayerBridgeContext* context;
    DlumaObjectHandle handle;
    uint32_t active;
} DlumaLuaPlayerHandle;

static char g_dluma_player_handle_registry_key;

/* lapi.c is compiled with lua_setglobal renamed to this symbol. */
LUA_API void dluma_real_lua_setglobal(lua_State* state, const char* name);

static int dluma_player_push_error(
    lua_State* state,
    const DlumaLua54Error* error,
    const char* fallback_code,
    const char* fallback_message)
{
    lua_pushnil(state);
    lua_newtable(state);
    lua_pushstring(
        state,
        error && error->code[0]
            ? error->code
            : fallback_code);
    lua_setfield(state, -2, "code");
    lua_pushstring(
        state,
        error && error->message[0]
            ? error->message
            : fallback_message);
    lua_setfield(state, -2, "message");
    lua_pushboolean(state, error ? error->retryable != 0 : 1);
    lua_setfield(state, -2, "retryable");
    lua_pushstring(
        state,
        error && error->capability[0]
            ? error->capability
            : "player.read");
    lua_setfield(state, -2, "capability");
    lua_pushstring(
        state,
        error && error->permission[0]
            ? error->permission
            : "player.read");
    lua_setfield(state, -2, "permission");
    return 2;
}

static int dluma_player_context_valid(
    const DlumaLua54PlayerBridgeContext* context,
    lua_State* state)
{
    return context && context->active &&
           context->state == state && context->host &&
           context->host->api_invoke;
}

static int dluma_player_invoke(
    DlumaLua54PlayerBridgeContext* context,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size,
    DlumaLua54Error* error)
{
    if (error) {
        memset(error, 0, sizeof(*error));
        error->struct_size = (uint32_t)sizeof(*error);
    }
    if (!context || !context->active || !context->host ||
        !context->host->api_invoke)
        return 0;
    return context->host->api_invoke(
        context->host_user,
        operation_id,
        request,
        request_size,
        result,
        result_size,
        error);
}

static void dluma_player_track_handle(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context,
    int handle_index)
{
    int registry_index;
    int handles_index;
    handle_index = lua_absindex(state, handle_index);
    lua_pushlightuserdata(state, &g_dluma_player_handle_registry_key);
    lua_rawget(state, LUA_REGISTRYINDEX);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushlightuserdata(state, &g_dluma_player_handle_registry_key);
        lua_pushvalue(state, -2);
        lua_rawset(state, LUA_REGISTRYINDEX);
    }
    registry_index = lua_absindex(state, -1);
    lua_pushlightuserdata(state, context);
    lua_rawget(state, registry_index);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_newtable(state);
        lua_pushstring(state, "k");
        lua_setfield(state, -2, "__mode");
        lua_setmetatable(state, -2);
        lua_pushlightuserdata(state, context);
        lua_pushvalue(state, -2);
        lua_rawset(state, registry_index);
    }
    handles_index = lua_absindex(state, -1);
    lua_pushvalue(state, handle_index);
    lua_pushboolean(state, 1);
    lua_rawset(state, handles_index);
    lua_pop(state, 2);
}

static void dluma_player_invalidate_handles(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context)
{
    int registry_index;
    int handles_index;
    if (!state || !context) return;
    lua_pushlightuserdata(state, &g_dluma_player_handle_registry_key);
    lua_rawget(state, LUA_REGISTRYINDEX);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    registry_index = lua_absindex(state, -1);
    lua_pushlightuserdata(state, context);
    lua_rawget(state, registry_index);
    if (!lua_istable(state, -1)) {
        lua_pop(state, 2);
        return;
    }
    handles_index = lua_absindex(state, -1);
    lua_pushnil(state);
    while (lua_next(state, handles_index) != 0) {
        DlumaLuaPlayerHandle* handle =
            (DlumaLuaPlayerHandle*)luaL_testudata(
                state, -2, DLUMA_PLAYER_HANDLE_METATABLE);
        if (handle) handle->active = 0U;
        lua_pop(state, 1);
    }
    lua_pushlightuserdata(state, context);
    lua_pushnil(state);
    lua_rawset(state, registry_index);
    lua_pop(state, 2);
}

static DlumaLuaPlayerHandle* dluma_player_check_handle(
    lua_State* state,
    int index)
{
    DlumaLuaPlayerHandle* handle =
        (DlumaLuaPlayerHandle*)luaL_checkudata(
            state, index, DLUMA_PLAYER_HANDLE_METATABLE);
    if (!handle || !handle->active || !handle->context ||
        !dluma_player_context_valid(handle->context, state) ||
        handle->handle.owner_id != handle->context->owner_id ||
        handle->handle.generation !=
            handle->context->map_generation)
        return 0;
    return handle;
}

static void dluma_player_initialize_no_request(DlumaNoRequest* request)
{
    memset(request, 0, sizeof(*request));
    request->struct_size = (uint32_t)sizeof(*request);
    request->struct_version = 1U;
}

static int dluma_player_local_player(lua_State* state)
{
    DlumaLua54PlayerBridgeContext* context =
        (DlumaLua54PlayerBridgeContext*)lua_touserdata(
            state, lua_upvalueindex(1));
    DlumaNoRequest request;
    DlumaObjectResult result;
    DlumaLua54Error error;
    DlumaLuaPlayerHandle* handle;
    char name[32];
    char class_name[32];
    if (!dluma_player_context_valid(context, state))
        return dluma_player_push_error(
            state, 0, "player_context_unavailable",
            "the shared player context is unavailable");
    dluma_player_initialize_no_request(&request);
    memset(&result, 0, sizeof(result));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.name.data = name;
    result.name.capacity = (uint64_t)sizeof(name);
    result.class_name.data = class_name;
    result.class_name.capacity = (uint64_t)sizeof(class_name);
    if (!dluma_player_invoke(
            context, 3001U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "local_player_unavailable",
            "no validated local player is active");
    handle = (DlumaLuaPlayerHandle*)lua_newuserdata(
        state, sizeof(*handle));
    memset(handle, 0, sizeof(*handle));
    handle->context = context;
    handle->handle = result.handle;
    handle->active = 1U;
    luaL_getmetatable(state, DLUMA_PLAYER_HANDLE_METATABLE);
    lua_setmetatable(state, -2);
    dluma_player_track_handle(state, context, -1);
    return 1;
}

static int dluma_player_variable_names(lua_State* state)
{
    DlumaLua54PlayerBridgeContext* context =
        (DlumaLua54PlayerBridgeContext*)lua_touserdata(
            state, lua_upvalueindex(1));
    DlumaNoRequest request;
    DlumaStringListResult result;
    DlumaStringBuffer items[DLUMA_PLAYER_MAX_VARIABLE_NAMES];
    char names[DLUMA_PLAYER_MAX_VARIABLE_NAMES]
              [DLUMA_PLAYER_VARIABLE_NAME_CAPACITY];
    DlumaLua54Error error;
    uint32_t index;
    if (!dluma_player_context_valid(context, state))
        return dluma_player_push_error(
            state, 0, "player_context_unavailable",
            "the shared player context is unavailable");
    dluma_player_initialize_no_request(&request);
    memset(&result, 0, sizeof(result));
    memset(items, 0, sizeof(items));
    memset(names, 0, sizeof(names));
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.items = items;
    result.capacity = DLUMA_PLAYER_MAX_VARIABLE_NAMES;
    for (index = 0; index < DLUMA_PLAYER_MAX_VARIABLE_NAMES; ++index) {
        items[index].data = names[index];
        items[index].capacity = DLUMA_PLAYER_VARIABLE_NAME_CAPACITY;
    }
    if (!dluma_player_invoke(
            context, 3008U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_variables_unavailable",
            "the shared PlayerVariables allowlist is unavailable");
    lua_newtable(state);
    for (index = 0; index < result.count; ++index) {
        lua_pushstring(state, names[index]);
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    return 1;
}

static int dluma_player_read_variable(
    DlumaLuaPlayerHandle* handle,
    const char* name,
    size_t name_size,
    DlumaPlayerVariableResult* result,
    DlumaLua54Error* error,
    char* result_name,
    uint32_t result_name_capacity)
{
    DlumaPlayerVariableRequest request;
    memset(&request, 0, sizeof(request));
    memset(result, 0, sizeof(*result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.player = handle->handle;
    request.name.data = name;
    request.name.size = (uint64_t)name_size;
    result->struct_size = (uint32_t)sizeof(*result);
    result->struct_version = 1U;
    result->name.data = result_name;
    result->name.capacity = result_name_capacity;
    return dluma_player_invoke(
        handle->context, 3009U,
        &request, (uint32_t)sizeof(request),
        result, (uint32_t)sizeof(*result), error);
}

static int dluma_player_variable(lua_State* state)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    size_t name_size = 0;
    const char* name = luaL_checklstring(state, 2, &name_size);
    DlumaPlayerVariableResult result;
    DlumaLua54Error error;
    char result_name[DLUMA_PLAYER_VARIABLE_NAME_CAPACITY];
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    if (!dluma_player_read_variable(
            handle, name, name_size, &result, &error,
            result_name, (uint32_t)sizeof(result_name)))
        return dluma_player_push_error(
            state, &error, "player_variable_unavailable",
            "the allowlisted PlayerVariable is unavailable");
    lua_newtable(state);
    lua_pushstring(state, result_name);
    lua_setfield(state, -2, "name");
    lua_pushnumber(state, result.value);
    lua_setfield(state, -2, "value");
    lua_pushnumber(state, result.minimum);
    lua_setfield(state, -2, "minimum");
    lua_pushnumber(state, result.maximum);
    lua_setfield(state, -2, "maximum");
    lua_pushboolean(state, result.overridden != 0);
    lua_setfield(state, -2, "overridden");
    return 1;
}

static int dluma_player_set_variable(lua_State* state)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    size_t name_size = 0;
    const char* name = luaL_checklstring(state, 2, &name_size);
    double value = luaL_checknumber(state, 3);
    DlumaPlayerVariableSetRequest request;
    DlumaBoolResult result;
    DlumaLua54Error error;
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.player = handle->handle;
    request.name.data = name;
    request.name.size = (uint64_t)name_size;
    request.value = value;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    if (!dluma_player_invoke(
            handle->context, 3010U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_variable_update_failed",
            "the shared player service rejected the override");
    lua_pushboolean(state, result.value != 0);
    return 1;
}

static int dluma_player_reset_variable(lua_State* state)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    size_t name_size = 0;
    const char* name = luaL_checklstring(state, 2, &name_size);
    DlumaPlayerVariableRequest request;
    DlumaBoolResult result;
    DlumaLua54Error error;
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.player = handle->handle;
    request.name.data = name;
    request.name.size = (uint64_t)name_size;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    if (!dluma_player_invoke(
            handle->context, 3011U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_variable_reset_failed",
            "the shared player service could not restore the override");
    lua_pushboolean(state, result.value != 0);
    return 1;
}

static int dluma_player_vector(
    lua_State* state,
    uint32_t operation_id)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    DlumaObjectRequest request;
    DlumaVec3Result result;
    DlumaLua54Error error;
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle = handle->handle;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    if (!dluma_player_invoke(
            handle->context, operation_id,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_transform_unavailable",
            "the player transform capability is unavailable");
    lua_newtable(state);
    lua_pushnumber(state, result.value.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, result.value.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, result.value.z);
    lua_setfield(state, -2, "z");
    return 1;
}

static int dluma_player_position(lua_State* state)
{
    return dluma_player_vector(state, 3002U);
}

static int dluma_player_look_direction(lua_State* state)
{
    return dluma_player_vector(state, 3003U);
}

static int dluma_player_body_direction(lua_State* state)
{
    return dluma_player_vector(state, 3004U);
}

static int dluma_player_float(
    lua_State* state,
    uint32_t operation_id)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    DlumaObjectRequest request;
    DlumaFloatResult result;
    DlumaLua54Error error;
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle = handle->handle;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    if (!dluma_player_invoke(
            handle->context, operation_id,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_vitals_unavailable",
            "the player vitals capability is unavailable");
    lua_pushnumber(state, result.value);
    return 1;
}

static int dluma_player_health(lua_State* state)
{
    return dluma_player_float(state, 3005U);
}

static int dluma_player_max_health(lua_State* state)
{
    return dluma_player_float(state, 3006U);
}

static int dluma_player_is_alive(lua_State* state)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    DlumaObjectRequest request;
    DlumaBoolResult result;
    DlumaLua54Error error;
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.handle = handle->handle;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    if (!dluma_player_invoke(
            handle->context, 3007U,
            &request, (uint32_t)sizeof(request),
            &result, (uint32_t)sizeof(result), &error))
        return dluma_player_push_error(
            state, &error, "player_vitals_unavailable",
            "the player alive-state capability is unavailable");
    lua_pushboolean(state, result.value != 0);
    return 1;
}

static int dluma_player_watch_emit(lua_State* state)
{
    DlumaLuaPlayerHandle* handle =
        (DlumaLuaPlayerHandle*)luaL_testudata(
            state, lua_upvalueindex(3),
            DLUMA_PLAYER_HANDLE_METATABLE);
    const char* name;
    size_t name_size;
    DlumaPlayerVariableResult result;
    DlumaLua54Error error;
    char result_name[DLUMA_PLAYER_VARIABLE_NAME_CAPACITY];
    int initialized;
    int immediate;
    double previous = 0.0;
    if (!handle || !handle->active || !handle->context ||
        !handle->context->active ||
        handle->handle.generation !=
            handle->context->map_generation)
        return 0;
    name = lua_tolstring(
        state, lua_upvalueindex(2), &name_size);
    if (!name || !dluma_player_read_variable(
            handle, name, name_size, &result, &error,
            result_name, (uint32_t)sizeof(result_name)))
        return 0;
    lua_pushvalue(state, lua_upvalueindex(4));
    lua_getfield(state, -1, "initialized");
    initialized = lua_toboolean(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, -1, "immediate");
    immediate = lua_toboolean(state, -1);
    lua_pop(state, 1);
    if (initialized) {
        lua_getfield(state, -1, "previous");
        previous = lua_tonumber(state, -1);
        lua_pop(state, 1);
    }
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, "initialized");
    lua_pushnumber(state, result.value);
    lua_setfield(state, -2, "previous");
    lua_pop(state, 1);
    if (!initialized && !immediate) return 0;
    lua_newtable(state);
    if (initialized) lua_pushnumber(state, previous);
    else lua_pushnil(state);
    lua_setfield(state, -2, "previous");
    lua_pushnumber(state, result.value);
    lua_setfield(state, -2, "value");
    lua_pushnumber(state, result.minimum);
    lua_setfield(state, -2, "minimum");
    lua_pushnumber(state, result.maximum);
    lua_setfield(state, -2, "maximum");
    lua_pushboolean(state, result.overridden != 0);
    lua_setfield(state, -2, "overridden");
    lua_pushinteger(
        state,
        (lua_Integer)handle->context->map_generation);
    lua_setfield(state, -2, "map_generation");
    lua_pushstring(state, result_name);
    lua_setfield(state, -2, "name");
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, -2);
    lua_call(state, 1, 0);
    return 0;
}

static int dluma_player_watch_variable(lua_State* state)
{
    DlumaLuaPlayerHandle* handle = dluma_player_check_handle(state, 1);
    size_t name_size = 0;
    const char* name = luaL_checklstring(state, 2, &name_size);
    double interval = 0.10;
    int immediate = 1;
    DlumaPlayerVariableResult probe;
    DlumaLua54Error error;
    char result_name[DLUMA_PLAYER_VARIABLE_NAME_CAPACITY];
    if (!handle)
        return dluma_player_push_error(
            state, 0, "stale_handle",
            "the local-player handle expired; reacquire it");
    if (!dluma_player_read_variable(
            handle, name, name_size, &probe, &error,
            result_name, (uint32_t)sizeof(result_name)))
        return dluma_player_push_error(
            state, &error, "player_variable_unavailable",
            "the allowlisted PlayerVariable is unavailable");
    luaL_checktype(state, 3, LUA_TFUNCTION);
    if (!lua_isnoneornil(state, 4)) {
        luaL_checktype(state, 4, LUA_TTABLE);
        lua_getfield(state, 4, "interval");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_getfield(state, 4, "every");
        }
        if (!lua_isnil(state, -1))
            interval = luaL_checknumber(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, 4, "immediate");
        if (!lua_isnil(state, -1))
            immediate = lua_toboolean(state, -1);
        lua_pop(state, 1);
    }
    if (interval < 0.05 || interval > 60.0)
        return dluma_player_push_error(
            state, 0, "player_watch_interval_invalid",
            "watch interval must be 0.05..60 seconds");
    lua_getglobal(state, "dluma");
    lua_getfield(state, -1, "timer");
    lua_getfield(state, -1, "every");
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, 0);
        return dluma_player_push_error(
            state, 0, "player_watch_unavailable",
            "the bounded Lua timer service is unavailable");
    }
    lua_pushnumber(state, interval);
    lua_pushvalue(state, 3);
    lua_pushlstring(state, name, name_size);
    lua_pushvalue(state, 1);
    lua_newtable(state);
    lua_pushboolean(state, immediate);
    lua_setfield(state, -2, "immediate");
    lua_pushcclosure(state, dluma_player_watch_emit, 4);
    if (lua_pcall(state, 2, 2, 0) != LUA_OK) {
        lua_settop(state, 0);
        return dluma_player_push_error(
            state, 0, "player_watch_failed",
            "the bounded Lua timer service rejected the watcher");
    }
    lua_remove(state, 1);
    lua_remove(state, 1);
    return 2;
}

static int dluma_player_gc(lua_State* state)
{
    DlumaLuaPlayerHandle* handle =
        (DlumaLuaPlayerHandle*)luaL_checkudata(
            state, 1, DLUMA_PLAYER_HANDLE_METATABLE);
    if (handle) handle->active = 0U;
    return 0;
}

static void dluma_player_register_metatable(lua_State* state)
{
    if (luaL_newmetatable(state, DLUMA_PLAYER_HANDLE_METATABLE)) {
        lua_pushcfunction(state, dluma_player_gc);
        lua_setfield(state, -2, "__gc");
        lua_newtable(state);
        lua_pushcfunction(state, dluma_player_variable);
        lua_setfield(state, -2, "variable");
        lua_pushcfunction(state, dluma_player_set_variable);
        lua_setfield(state, -2, "set_variable");
        lua_pushcfunction(state, dluma_player_reset_variable);
        lua_setfield(state, -2, "reset_variable");
        lua_pushcfunction(state, dluma_player_watch_variable);
        lua_setfield(state, -2, "watch_variable");
        lua_pushcfunction(state, dluma_player_health);
        lua_setfield(state, -2, "health");
        lua_pushcfunction(state, dluma_player_max_health);
        lua_setfield(state, -2, "max_health");
        lua_pushcfunction(state, dluma_player_is_alive);
        lua_setfield(state, -2, "is_alive");
        lua_pushcfunction(state, dluma_player_position);
        lua_setfield(state, -2, "position");
        lua_pushcfunction(state, dluma_player_look_direction);
        lua_setfield(state, -2, "look_direction");
        lua_pushcfunction(state, dluma_player_body_direction);
        lua_setfield(state, -2, "body_direction");
        lua_setfield(state, -2, "__index");
    }
    lua_pop(state, 1);
}

void dluma_lua54_player_bridge_register(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context,
    int dluma_table_index)
{
    int player_index;
    if (!state || !context ||
        !lua_istable(state, dluma_table_index))
        return;
    dluma_table_index = lua_absindex(state, dluma_table_index);
    dluma_player_register_metatable(state);
    lua_getfield(state, dluma_table_index, "player");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    player_index = lua_absindex(state, -1);
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, dluma_player_local_player, 1);
    lua_setfield(state, player_index, "local_player");
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, dluma_player_variable_names, 1);
    lua_setfield(state, player_index, "variable_names");
    lua_pushvalue(state, player_index);
    lua_setfield(state, dluma_table_index, "player");
    lua_pop(state, 1);
}

void dluma_lua54_player_bridge_teardown(
    lua_State* state,
    DlumaLua54PlayerBridgeContext* context)
{
    if (!context) return;
    dluma_player_invalidate_handles(state, context);
    context->active = 0U;
}

/* lapi.c is built with lua_setglobal privately renamed so the helper can
 * control the public symbol. Keep the helper and lauxlib callers ABI-stable
 * by forwarding that one renamed entry point from this companion unit. */
LUA_API void lua_setglobal(lua_State* state, const char* name)
{
    dluma_real_lua_setglobal(state, name);
}
