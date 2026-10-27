#include "dlw_mod_owner.h"

#include <string.h>

static int dluma_mod_owner_string_valid(DlumaStringView value)
{
    uint64_t index;
    if (!value.data || !value.size ||
        value.size > DLUMA_MOD_OWNER_MAX_ID_BYTES)
        return 0;
    for (index = 0; index < value.size; ++index) {
        unsigned char byte = (unsigned char)value.data[index];
        if (!byte || byte < 0x20U || byte == 0x7FU) return 0;
    }
    return 1;
}

static int dluma_mod_owner_language_valid(uint32_t language)
{
    return language == DLUMA_MOD_LANGUAGE_LUA ||
           language == DLUMA_MOD_LANGUAGE_CSHARP ||
           language == DLUMA_MOD_LANGUAGE_NATIVE;
}

static int dluma_mod_owner_scope_valid(uint32_t scope)
{
    return scope == DLUMA_SCOPE_GLOBAL ||
           scope == DLUMA_SCOPE_PROJECT ||
           scope == DLUMA_SCOPE_MAP;
}

DlumaStatus dluma_mod_owner_initialize(
    DlumaModOwner* owner,
    const DlumaModOwnerDefinition* definition)
{
    uint32_t index;
    if (!owner || !definition ||
        definition->struct_size < sizeof(*definition) ||
        definition->struct_version != DLUMA_MOD_OWNER_STRUCT_VERSION ||
        !definition->owner_id ||
        !dluma_mod_owner_language_valid(definition->language) ||
        !dluma_mod_owner_scope_valid(definition->scope) ||
        !dluma_mod_owner_string_valid(definition->stable_id) ||
        definition->access_count > DLUMA_MOD_OWNER_MAX_ACCESS ||
        (definition->access_count && !definition->access_ids))
        return DLUMA_STATUS_INVALID_ARGUMENT;

    memset(owner, 0, sizeof(*owner));
    owner->struct_size = (uint32_t)sizeof(*owner);
    owner->struct_version = DLUMA_MOD_OWNER_STRUCT_VERSION;
    owner->owner_id = definition->owner_id;
    owner->language = definition->language;
    owner->scope = definition->scope;
    owner->map_generation = definition->map_generation;
    owner->game_thread_id = definition->game_thread_id;
    owner->state = DLUMA_MOD_OWNER_ACTIVE;
    owner->generation = 1U;
    owner->access_count = definition->access_count;
    owner->max_handles = definition->max_handles;
    owner->max_subscriptions = definition->max_subscriptions;
    owner->max_timers = definition->max_timers;
    owner->max_companion_artifacts =
        definition->max_companion_artifacts;
    memcpy(
        owner->stable_id,
        definition->stable_id.data,
        (size_t)definition->stable_id.size);
    owner->stable_id[definition->stable_id.size] = 0;

    for (index = 0; index < definition->access_count; ++index) {
        uint32_t scan;
        uint32_t access_id = definition->access_ids[index];
        if (!access_id) {
            memset(owner, 0, sizeof(*owner));
            return DLUMA_STATUS_INVALID_ARGUMENT;
        }
        for (scan = 0; scan < index; ++scan) {
            if (owner->access_ids[scan] == access_id) {
                memset(owner, 0, sizeof(*owner));
                return DLUMA_STATUS_INVALID_ARGUMENT;
            }
        }
        owner->access_ids[index] = access_id;
    }
    return DLUMA_STATUS_OK;
}

void dluma_mod_owner_begin_revoke(DlumaModOwner* owner)
{
    if (!owner || owner->state != DLUMA_MOD_OWNER_ACTIVE) return;
    owner->state = DLUMA_MOD_OWNER_REVOKING;
}

void dluma_mod_owner_finish_revoke(DlumaModOwner* owner)
{
    if (!owner ||
        (owner->state != DLUMA_MOD_OWNER_REVOKING &&
         owner->state != DLUMA_MOD_OWNER_ACTIVE))
        return;
    owner->active_handles = 0;
    owner->active_subscriptions = 0;
    owner->active_timers = 0;
    owner->active_companion_artifacts = 0;
    ++owner->generation;
    if (!owner->generation) owner->generation = 1U;
    owner->state = DLUMA_MOD_OWNER_REVOKED;
}

void dluma_mod_owner_advance_scope(
    DlumaModOwner* owner,
    uint32_t map_generation)
{
    if (!owner || owner->state != DLUMA_MOD_OWNER_ACTIVE) return;
    ++owner->generation;
    if (!owner->generation) ++owner->generation;
    owner->map_generation = map_generation;
    owner->active_handles = 0;
    owner->active_subscriptions = 0;
    owner->active_timers = 0;
    owner->active_companion_artifacts = 0;
}

int dluma_mod_owner_has_access(
    const DlumaModOwner* owner,
    uint32_t access_id)
{
    uint32_t index;
    if (!access_id) return 1;
    if (!owner) return 0;
    for (index = 0; index < owner->access_count; ++index) {
        if (owner->access_ids[index] == access_id) return 1;
    }
    return 0;
}

int dluma_mod_owner_scope_allowed(
    const DlumaModOwner* owner,
    uint32_t scope_mask)
{
    uint32_t bit;
    if (!owner) return 0;
    if (owner->scope == DLUMA_SCOPE_GLOBAL)
        bit = 1U;
    else if (owner->scope == DLUMA_SCOPE_PROJECT)
        bit = 2U;
    else if (owner->scope == DLUMA_SCOPE_MAP)
        bit = 4U;
    else
        return 0;
    return (scope_mask & bit) != 0;
}

DlumaStatus dluma_mod_owner_require(
    const DlumaModOwner* owner,
    uint32_t scope_mask,
    uint32_t access_id,
    uint32_t requires_game_thread,
    uint32_t current_thread_id)
{
    if (!owner || owner->state != DLUMA_MOD_OWNER_ACTIVE)
        return DLUMA_STATUS_OWNER_REVOKED;
    if (!dluma_mod_owner_scope_allowed(owner, scope_mask))
        return DLUMA_STATUS_SCOPE_DENIED;
    if (!dluma_mod_owner_has_access(owner, access_id))
        return DLUMA_STATUS_ACCESS_DENIED;
    if (requires_game_thread &&
        (!owner->game_thread_id ||
         owner->game_thread_id != current_thread_id))
        return DLUMA_STATUS_WRONG_THREAD;
    return DLUMA_STATUS_OK;
}
