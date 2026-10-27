#ifndef DLUMA_GAME_HOST_CONTEXT_H
#define DLUMA_GAME_HOST_CONTEXT_H

#include "../core/dlw_mod_package_runtime.h"
#include "../core/dlw_mod_owner.h"
#include "../core/dlw_capability_registry.h"
#include "../dlw_companion_archive.h"
#include "../dlw_lua_config.h"

typedef void (*DlumaGameHostEventSinkFn)(
    void* user,
    uint32_t kind,
    uint64_t callback_id,
    uint64_t token,
    double number,
    const char* name);

typedef struct DlumaGameHostContextDefinition {
    uint32_t struct_size;
    uint32_t struct_version;
    DlumaModOwner* owner;
    const DlumaModPackageSnapshot* package;
    const DlumaModConfigIdentity* config_identity;
    const DlumaLuaConfigSchema* config_schema;
    const char* storage_key;
    void* expert_user;
    DlumaGameHostEventSinkFn event_sink;
    void* event_user;
} DlumaGameHostContextDefinition;

typedef struct DlumaGameHostContext {
    uint32_t struct_size;
    uint32_t struct_version;
    void* state;
} DlumaGameHostContext;

#ifdef __cplusplus
extern "C" {
#endif

DlumaStatus dluma_game_host_context_initialize(
    DlumaGameHostContext* context,
    const DlumaGameHostContextDefinition* definition);
void dluma_game_host_context_revoke(
    DlumaGameHostContext* context);
void dluma_game_host_context_advance_scope(
    DlumaGameHostContext* context);
/* Rebinds map-sensitive subscriptions while preserving process-scope timers,
 * configuration subscriptions, and immutable companion snapshots. Call only
 * after the global owner's map generation has advanced. */
void dluma_game_host_context_rebind_map(
    DlumaGameHostContext* context);
/* Checked outgoing-map suspension. Process timers, configuration callbacks
 * and module-owned companion snapshots survive; native map subscriptions and
 * media are detached while the outgoing native owner is still valid. */
int dluma_game_host_context_suspend_map_for_exit(DlumaGameHostContext* context);
void dluma_game_host_context_bind_clean_frontend(DlumaGameHostContext* context);
int dluma_game_host_context_needs_map_rebind(DlumaGameHostContext* context);
void dluma_game_host_context_pump(
    DlumaGameHostContext* context,
    double delta_seconds);
void dluma_game_host_context_notify_config_changed(
    DlumaGameHostContext* context,
    const char* key);
void* dluma_game_host_context_expert_user(
    DlumaGameHostContext* context);

void dluma_game_host_context_prepare_capabilities(
    DlumaGameHostContext* context,
    DlumaCapabilityRegistry* registry);

DlumaStatus dluma_game_host_context_invoke(
    DlumaGameHostContext* context,
    uint32_t operation_id,
    const void* request,
    void* result);
DlumaStatus dluma_game_host_context_resolve_companion(
    DlumaGameHostContext* context,
    uint64_t token,
    DlumaCompanionArchive** archive_out);

#ifdef __cplusplus
}
#endif

#endif
