#ifndef DLUMA_MOD_OWNER_H
#define DLUMA_MOD_OWNER_H

#include "../../sdk/c/dluma_api.h"

#define DLUMA_MOD_OWNER_STRUCT_VERSION 1U
#define DLUMA_MOD_OWNER_MAX_ACCESS 64U
#define DLUMA_MOD_OWNER_MAX_ID_BYTES 127U

typedef enum DlumaModLanguage {
    DLUMA_MOD_LANGUAGE_LUA = 1,
    DLUMA_MOD_LANGUAGE_CSHARP = 2,
    DLUMA_MOD_LANGUAGE_NATIVE = 3
} DlumaModLanguage;

typedef enum DlumaModOwnerState {
    DLUMA_MOD_OWNER_EMPTY = 0,
    DLUMA_MOD_OWNER_ACTIVE = 1,
    DLUMA_MOD_OWNER_REVOKING = 2,
    DLUMA_MOD_OWNER_REVOKED = 3
} DlumaModOwnerState;

typedef struct DlumaModOwnerDefinition {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t owner_id;
    uint32_t language;
    uint32_t scope;
    uint32_t map_generation;
    uint32_t game_thread_id;
    DlumaStringView stable_id;
    const uint32_t* access_ids;
    uint32_t access_count;
    uint32_t max_handles;
    uint32_t max_subscriptions;
    uint32_t max_timers;
    uint32_t max_companion_artifacts;
} DlumaModOwnerDefinition;

typedef struct DlumaModOwner {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t owner_id;
    uint32_t language;
    uint32_t scope;
    uint32_t map_generation;
    uint32_t game_thread_id;
    uint32_t state;
    uint32_t generation;
    uint32_t access_count;
    uint32_t access_ids[DLUMA_MOD_OWNER_MAX_ACCESS];
    uint32_t max_handles;
    uint32_t max_subscriptions;
    uint32_t max_timers;
    uint32_t max_companion_artifacts;
    uint32_t active_handles;
    uint32_t active_subscriptions;
    uint32_t active_timers;
    uint32_t active_companion_artifacts;
    char stable_id[DLUMA_MOD_OWNER_MAX_ID_BYTES + 1U];
} DlumaModOwner;

#ifdef __cplusplus
extern "C" {
#endif

DlumaStatus dluma_mod_owner_initialize(
    DlumaModOwner* owner,
    const DlumaModOwnerDefinition* definition);
void dluma_mod_owner_begin_revoke(DlumaModOwner* owner);
void dluma_mod_owner_finish_revoke(DlumaModOwner* owner);
void dluma_mod_owner_advance_scope(
    DlumaModOwner* owner,
    uint32_t map_generation);
int dluma_mod_owner_has_access(
    const DlumaModOwner* owner,
    uint32_t access_id);
int dluma_mod_owner_scope_allowed(
    const DlumaModOwner* owner,
    uint32_t scope_mask);
DlumaStatus dluma_mod_owner_require(
    const DlumaModOwner* owner,
    uint32_t scope_mask,
    uint32_t access_id,
    uint32_t requires_game_thread,
    uint32_t current_thread_id);

#ifdef __cplusplus
}
#endif

#endif
