#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "dlw_player_service.h"

#include "../bindings/dlw_player_variables_binding.h"
#include "../bindings/dlw_player_actor_binding.h"

#include <float.h>
#include <stdint.h>
#include <string.h>

#define DLUMA_PLAYER_SERVICE_MAX_HANDLES 256U
#define DLUMA_PLAYER_SERVICE_MAX_OVERRIDES 128U
#define DLUMA_PLAYER_SERVICE_MAX_OVERRIDES_PER_OWNER 64U
#define DLUMA_PLAYER_HANDLE_KIND 2U

typedef struct DlumaPlayerServiceHandle {
    uint32_t active;
    uint32_t generation;
    uint64_t owner_id;
    uint64_t handle_id;
    void* player_state;
} DlumaPlayerServiceHandle;

typedef struct DlumaPlayerServiceOverride {
    uint32_t active;
    uint32_t engine_id;
    uint32_t map_generation;
    uint32_t reserved;
    uint64_t owner_id;
    uint64_t sequence;
    void* player_state;
    float original_base;
    float requested_base;
} DlumaPlayerServiceOverride;

static volatile LONG g_dluma_player_service_lock;
static uint64_t g_dluma_player_service_handle_sequence;
static uint64_t g_dluma_player_service_override_sequence;
static DlumaPlayerServiceHandle
    g_dluma_player_service_handles[DLUMA_PLAYER_SERVICE_MAX_HANDLES];
static DlumaPlayerServiceOverride
    g_dluma_player_service_overrides[DLUMA_PLAYER_SERVICE_MAX_OVERRIDES];

static int dluma_player_service_reset_definition_locked(
    const DlumaPlayerVariableDefinition* definition,
    uint64_t owner_id,
    int discard_on_failure);

static void dluma_player_service_lock(void)
{
    while (InterlockedCompareExchange(
               &g_dluma_player_service_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_player_service_unlock(void)
{
    InterlockedExchange(&g_dluma_player_service_lock, 0);
}

static DlumaStringView dluma_player_service_view(const char* value)
{
    DlumaStringView view;
    view.data = value;
    view.size = value ? (uint64_t)strlen(value) : 0U;
    return view;
}

static void dluma_player_service_set_capability(
    DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    int configured,
    int ready,
    const char* status)
{
    (void)dluma_capability_registry_set(
        registry,
        capability_id,
        configured,
        ready,
        dluma_player_service_view(status));
}

void dluma_player_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry)
{
    int configured;
    int ready;
    if (!registry) return;
    configured = dluma_player_variables_binding_configured();
    ready = configured && dluma_player_variables_binding_ready();
    dluma_player_service_set_capability(
        registry,
        DLUMA_CAPABILITY_PLAYER_READ,
        configured,
        ready,
        ready
            ? "ready"
            : "exact-build PlayerVariables binding unavailable");
    dluma_player_service_set_capability(
        registry,
        DLUMA_CAPABILITY_PLAYER_TRANSFORM,
        0,
        0,
        "planned exact-build player transform binding");
    ready = dluma_player_actor_binding_vitals_ready();
    dluma_player_service_set_capability(
        registry, DLUMA_CAPABILITY_PLAYER_VITALS, 1, ready,
        ready ? "ready" : "exact-build PlayerDI vitals binding unavailable");
}

static int dluma_player_service_copy_view(
    char* output,
    uint32_t capacity,
    DlumaStringView value)
{
    if (!output || !capacity || value.size >= capacity ||
        (!value.data && value.size))
        return 0;
    if (value.size)
        memcpy(output, value.data, (size_t)value.size);
    output[value.size] = 0;
    return 1;
}

static DlumaStatus dluma_player_service_write_buffer(
    DlumaStringBuffer* output,
    const char* value)
{
    uint64_t length;
    if (!output || !value) return DLUMA_STATUS_INVALID_ARGUMENT;
    length = (uint64_t)strlen(value);
    output->required = length + 1U;
    if (!output->data || output->capacity < output->required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    memcpy(output->data, value, (size_t)length);
    output->data[length] = 0;
    return DLUMA_STATUS_OK;
}

static uint64_t dluma_player_service_next_handle_id(void)
{
    uint64_t value = ++g_dluma_player_service_handle_sequence;
    if (!value) value = ++g_dluma_player_service_handle_sequence;
    return value ^ UINT64_C(0xD17A000000000000);
}

static DlumaStatus dluma_player_service_local(
    DlumaModOwner* owner,
    DlumaObjectResult* result)
{
    uint32_t index;
    DlumaPlayerServiceHandle* available = 0;
    DlumaStatus status;
    void* player_state;
    int retire = 0;
    if (!owner || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!dluma_player_variables_binding_local_state(&player_state))
        return DLUMA_STATUS_UNAVAILABLE;
    status = dluma_player_service_write_buffer(
        &result->name, "local_player");
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_player_service_write_buffer(
        &result->class_name, "PlayerState");
    if (status != DLUMA_STATUS_OK) return status;

    dluma_player_service_lock();
    /* A player handle identifies both a map generation and its PlayerState.
     * Retire the owner's prior overrides before issuing a replacement. */
    for (index = 0; index < DLUMA_PLAYER_SERVICE_MAX_HANDLES; ++index) {
        DlumaPlayerServiceHandle* entry = &g_dluma_player_service_handles[index];
        if (entry->active && entry->owner_id == owner->owner_id &&
            (entry->generation != owner->map_generation || entry->player_state != player_state)) {
            retire = 1;
            break;
        }
    }
    if (!retire) {
        for (index = 0; index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES; ++index) {
            DlumaPlayerServiceOverride* entry = &g_dluma_player_service_overrides[index];
            if (entry->active && entry->owner_id == owner->owner_id &&
                (entry->map_generation != owner->map_generation || entry->player_state != player_state)) {
                retire = 1;
                break;
            }
        }
    }
    if (retire) {
        for (index = 0; index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES; ++index) {
            DlumaPlayerServiceOverride* entry = &g_dluma_player_service_overrides[index];
            const DlumaPlayerVariableDefinition* definition;
            if (!entry->active || entry->owner_id != owner->owner_id) continue;
            definition = dluma_player_variables_binding_definition_id(entry->engine_id);
            if (!definition || !dluma_player_service_reset_definition_locked(
                    definition, owner->owner_id, 0)) {
                dluma_player_service_unlock();
                return DLUMA_STATUS_UNAVAILABLE;
            }
        }
        for (index = 0; index < DLUMA_PLAYER_SERVICE_MAX_HANDLES; ++index) {
            DlumaPlayerServiceHandle* entry = &g_dluma_player_service_handles[index];
            if (entry->active && entry->owner_id == owner->owner_id) {
                int counted = entry->generation == owner->map_generation;
                memset(entry, 0, sizeof(*entry));
                if (counted && owner->active_handles) --owner->active_handles;
            }
        }
    }
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_HANDLES;
         ++index) {
        DlumaPlayerServiceHandle* entry =
            &g_dluma_player_service_handles[index];
        if (entry->active &&
            entry->owner_id == owner->owner_id &&
            entry->generation == owner->map_generation &&
            entry->player_state == player_state) {
            result->handle.owner_id = owner->owner_id;
            result->handle.handle_id = entry->handle_id;
            result->handle.generation = owner->map_generation;
            result->handle.kind = DLUMA_PLAYER_HANDLE_KIND;
            dluma_player_service_unlock();
            return DLUMA_STATUS_OK;
        }
        if (!entry->active && !available) available = entry;
    }
    if (!available ||
        (owner->max_handles &&
         owner->active_handles >= owner->max_handles)) {
        dluma_player_service_unlock();
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    memset(available, 0, sizeof(*available));
    available->active = 1U;
    available->owner_id = owner->owner_id;
    available->generation = owner->map_generation;
    available->player_state = player_state;
    available->handle_id = dluma_player_service_next_handle_id();
    result->handle.owner_id = owner->owner_id;
    result->handle.handle_id = available->handle_id;
    result->handle.generation = owner->map_generation;
    result->handle.kind = DLUMA_PLAYER_HANDLE_KIND;
    ++owner->active_handles;
    dluma_player_service_unlock();
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_player_service_validate_handle_locked(
    const DlumaModOwner* owner,
    DlumaObjectHandle handle)
{
    uint32_t index;
    if (!owner || handle.kind != DLUMA_PLAYER_HANDLE_KIND)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (handle.owner_id != owner->owner_id)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    if (handle.generation != owner->map_generation)
        return DLUMA_STATUS_STALE_HANDLE;
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_HANDLES;
         ++index) {
        const DlumaPlayerServiceHandle* entry =
            &g_dluma_player_service_handles[index];
        if (!entry->active || entry->handle_id != handle.handle_id)
            continue;
        if (entry->owner_id != owner->owner_id)
            return DLUMA_STATUS_FOREIGN_HANDLE;
        if (entry->generation != owner->map_generation)
            return DLUMA_STATUS_STALE_HANDLE;
        {
            void* current_state = 0;
            if (!dluma_player_variables_binding_local_state(&current_state))
                return DLUMA_STATUS_UNAVAILABLE;
            if (entry->player_state != current_state)
                return DLUMA_STATUS_STALE_HANDLE;
        }
        return DLUMA_STATUS_OK;
    }
    return DLUMA_STATUS_STALE_HANDLE;
}

static DlumaStatus dluma_player_service_validate_handle(
    const DlumaModOwner* owner,
    DlumaObjectHandle handle)
{
    DlumaStatus status;
    dluma_player_service_lock();
    status = dluma_player_service_validate_handle_locked(owner, handle);
    dluma_player_service_unlock();
    return status;
}

static DlumaStatus dluma_player_service_variable_names(
    DlumaStringListResult* result)
{
    uint32_t count;
    uint32_t index;
    if (!result) return DLUMA_STATUS_INVALID_ARGUMENT;
    count = dluma_player_variables_binding_definition_count();
    result->count = count;
    if (!result->items || result->capacity < count)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    for (index = 0; index < count; ++index) {
        const DlumaPlayerVariableDefinition* definition =
            dluma_player_variables_binding_definition_at(index);
        DlumaStatus status = dluma_player_service_write_buffer(
            &result->items[index], definition->name);
        if (status != DLUMA_STATUS_OK) return status;
    }
    return DLUMA_STATUS_OK;
}

static const DlumaPlayerVariableDefinition*
dluma_player_service_definition(DlumaStringView name)
{
    char exact_name[65];
    if (!dluma_player_service_copy_view(
            exact_name, (uint32_t)sizeof(exact_name), name))
        return 0;
    return dluma_player_variables_binding_definition_named(exact_name);
}

static uint32_t dluma_player_service_override_count_locked(
    uint64_t owner_id)
{
    uint32_t index;
    uint32_t count = 0;
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        if (g_dluma_player_service_overrides[index].active &&
            g_dluma_player_service_overrides[index].owner_id == owner_id)
            ++count;
    }
    return count;
}

static int dluma_player_service_is_overridden_locked(
    uint64_t owner_id,
    uint32_t engine_id)
{
    uint32_t index;
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        const DlumaPlayerServiceOverride* entry =
            &g_dluma_player_service_overrides[index];
        if (entry->active &&
            entry->owner_id == owner_id &&
            entry->engine_id == engine_id)
            return 1;
    }
    return 0;
}

static DlumaStatus dluma_player_service_variable_get(
    DlumaModOwner* owner,
    const DlumaPlayerVariableRequest* request,
    DlumaPlayerVariableResult* result)
{
    const DlumaPlayerVariableDefinition* definition;
    DlumaPlayerVariableSnapshot snapshot;
    DlumaStatus status;
    if (!owner || !request || !result)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_player_service_validate_handle(
        owner, request->player);
    if (status != DLUMA_STATUS_OK) return status;
    definition = dluma_player_service_definition(request->name);
    if (!definition) return DLUMA_STATUS_NOT_FOUND;
    memset(&snapshot, 0, sizeof(snapshot));
    if (!dluma_player_variables_binding_read(definition, &snapshot))
        return DLUMA_STATUS_UNAVAILABLE;
    status = dluma_player_service_write_buffer(
        &result->name, definition->name);
    if (status != DLUMA_STATUS_OK) return status;
    result->value = snapshot.value;
    result->minimum = definition->minimum;
    result->maximum = definition->maximum;
    dluma_player_service_lock();
    result->overridden = dluma_player_service_is_overridden_locked(
        owner->owner_id, definition->engine_id);
    dluma_player_service_unlock();
    return DLUMA_STATUS_OK;
}

static int dluma_player_service_apply_winner_locked(
    const DlumaPlayerVariableDefinition* definition)
{
    uint32_t index;
    DlumaPlayerServiceOverride* winner = 0;
    DlumaPlayerVariableSnapshot snapshot;
    void* player_state = 0;
    if (!definition ||
        !dluma_player_variables_binding_local_state(&player_state))
        return 0;
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        DlumaPlayerServiceOverride* candidate =
            &g_dluma_player_service_overrides[index];
        if (candidate->active &&
            candidate->engine_id == definition->engine_id &&
            candidate->player_state == player_state &&
            (!winner || candidate->sequence > winner->sequence))
            winner = candidate;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    return winner &&
           dluma_player_variables_binding_read(definition, &snapshot) &&
           snapshot.player_state == winner->player_state &&
           dluma_player_variables_binding_write_base(
               &snapshot, winner->requested_base);
}

static int dluma_player_service_restore_original_locked(
    const DlumaPlayerVariableDefinition* definition,
    void* player_state,
    float original_base)
{
    DlumaPlayerVariableSnapshot snapshot;
    void* active_state = 0;
    if (!definition || !player_state ||
        !dluma_player_variables_binding_local_state(&active_state) ||
        active_state != player_state)
        return 0;
    memset(&snapshot, 0, sizeof(snapshot));
    return dluma_player_variables_binding_read(definition, &snapshot) &&
           snapshot.player_state == player_state &&
           dluma_player_variables_binding_write_base(
               &snapshot, original_base);
}

static int dluma_player_service_reset_definition_locked(
    const DlumaPlayerVariableDefinition* definition,
    uint64_t owner_id,
    int discard_on_failure)
{
    uint32_t index;
    DlumaPlayerServiceOverride* original = 0;
    DlumaPlayerServiceOverride* winner = 0;
    void* player_state = 0;
    int found = 0;
    int restored;
    if (!definition || !owner_id) return 0;
    restored = dluma_player_variables_binding_local_state(&player_state);
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        DlumaPlayerServiceOverride* entry =
            &g_dluma_player_service_overrides[index];
        if (!entry->active || entry->engine_id != definition->engine_id)
            continue;
        if (entry->owner_id == owner_id) {
            found = 1;
            if (entry->player_state == player_state &&
                (!original || entry->sequence < original->sequence))
                original = entry;
        } else if (entry->player_state == player_state &&
                   (!winner || entry->sequence > winner->sequence)) {
            winner = entry;
        }
    }
    if (!found) return 1;
    if (original) {
        if (!winner && (definition->engine_id == 969u || definition->engine_id == 970u)) {
            DlumaPlayerVariableSnapshot current;
            memset(&current, 0, sizeof(current));
            if (!dluma_player_variables_binding_read(definition, &current) ||
                current.player_state != player_state)
                restored = 0;
            else if (current.base != original->requested_base) {
                /* A later game change owns this base value now. */
                restored = 1;
            } else {
                restored = dluma_player_service_restore_original_locked(
                    definition, player_state, original->original_base);
            }
        } else {
            restored = dluma_player_service_restore_original_locked(
                definition, player_state,
                winner ? winner->requested_base : original->original_base);
        }
    }
    /* Keep the caller's record until restoration succeeds so an explicit
     * reset can be retried. Final revocation still discards obsolete owners. */
    if (!restored && !discard_on_failure) return 0;
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        DlumaPlayerServiceOverride* entry =
            &g_dluma_player_service_overrides[index];
        if (entry->active && entry->engine_id == definition->engine_id &&
            entry->owner_id == owner_id)
            memset(entry, 0, sizeof(*entry));
    }
    return restored;
}

static DlumaStatus dluma_player_service_variable_set(
    DlumaModOwner* owner,
    const DlumaPlayerVariableSetRequest* request,
    DlumaBoolResult* result)
{
    const DlumaPlayerVariableDefinition* definition;
    DlumaPlayerVariableSnapshot snapshot;
    DlumaPlayerServiceOverride* selected = 0;
    DlumaPlayerServiceOverride backup;
    uint32_t index;
    float requested;
    DlumaStatus status;
    if (!owner || !request || !result)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_player_service_validate_handle(
        owner, request->player);
    if (status != DLUMA_STATUS_OK) return status;
    definition = dluma_player_service_definition(request->name);
    if (!definition) return DLUMA_STATUS_NOT_FOUND;
    if (request->value != request->value ||
        request->value > FLT_MAX || request->value < -FLT_MAX ||
        request->value < definition->minimum ||
        request->value > definition->maximum)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if ((definition->engine_id == 969u || definition->engine_id == 970u) &&
        request->value != 0.0 && request->value != 1.0)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    requested = (float)request->value;
    memset(&snapshot, 0, sizeof(snapshot));
    if (!dluma_player_variables_binding_read(definition, &snapshot))
        return DLUMA_STATUS_UNAVAILABLE;

    dluma_player_service_lock();
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        DlumaPlayerServiceOverride* entry =
            &g_dluma_player_service_overrides[index];
        if (entry->active &&
            entry->owner_id == owner->owner_id &&
            entry->engine_id == definition->engine_id &&
            entry->player_state == snapshot.player_state) {
            selected = entry;
            break;
        }
        if (!entry->active && !selected) selected = entry;
    }
    if (!selected ||
        (!selected->active &&
         dluma_player_service_override_count_locked(owner->owner_id) >=
             DLUMA_PLAYER_SERVICE_MAX_OVERRIDES_PER_OWNER)) {
        dluma_player_service_unlock();
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    backup = *selected;
    if (!selected->active) {
        float original_base = snapshot.base;
        uint64_t earliest = UINT64_MAX;
        for (index = 0;
             index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
             ++index) {
            const DlumaPlayerServiceOverride* prior =
                &g_dluma_player_service_overrides[index];
            if (prior->active &&
                prior->engine_id == definition->engine_id &&
                prior->player_state == snapshot.player_state &&
                prior->sequence < earliest) {
                earliest = prior->sequence;
                original_base = prior->original_base;
            }
        }
        memset(selected, 0, sizeof(*selected));
        selected->active = 1U;
        selected->engine_id = definition->engine_id;
        selected->owner_id = owner->owner_id;
        selected->player_state = snapshot.player_state;
        selected->map_generation = owner->map_generation;
        selected->original_base = original_base;
    }
    selected->requested_base = requested;
    selected->sequence = ++g_dluma_player_service_override_sequence;
    if (!selected->sequence)
        selected->sequence = ++g_dluma_player_service_override_sequence;
    if (!dluma_player_service_apply_winner_locked(definition)) {
        *selected = backup;
        if (backup.active)
            (void)dluma_player_service_apply_winner_locked(definition);
        dluma_player_service_unlock();
        return DLUMA_STATUS_UNAVAILABLE;
    }
    result->value = 1U;
    dluma_player_service_unlock();
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_player_service_variable_reset(
    DlumaModOwner* owner,
    const DlumaPlayerVariableRequest* request,
    DlumaBoolResult* result)
{
    const DlumaPlayerVariableDefinition* definition;
    DlumaStatus status;
    if (!owner || !request || !result)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_player_service_validate_handle(
        owner, request->player);
    if (status != DLUMA_STATUS_OK) return status;
    definition = dluma_player_service_definition(request->name);
    if (!definition) return DLUMA_STATUS_NOT_FOUND;
    dluma_player_service_lock();
    if (!dluma_player_service_reset_definition_locked(
            definition, owner->owner_id, 0)) {
        dluma_player_service_unlock();
        return DLUMA_STATUS_UNAVAILABLE;
    }
    dluma_player_service_unlock();
    result->value = 1U;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_player_service_vitals(
    DlumaModOwner* owner, uint32_t operation_id,
    const DlumaObjectRequest* request, void* result)
{
    DlumaPlayerVitalsSnapshot snapshot;
    DlumaStatus status;
    if (!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_player_service_validate_handle(owner, request->handle);
    if (status != DLUMA_STATUS_OK) return status;
    if (!dluma_player_actor_binding_read_vitals(&snapshot))
        return DLUMA_STATUS_UNAVAILABLE;
    status = dluma_player_service_validate_handle(owner, request->handle);
    if (status != DLUMA_STATUS_OK) return status;
    if (operation_id == 3007U)
        ((DlumaBoolResult*)result)->value = snapshot.alive;
    else
        ((DlumaFloatResult*)result)->value = operation_id == 3005U
            ? snapshot.health : snapshot.max_health;
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_player_service_invoke(
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result)
{
    switch (operation_id) {
    case 3001U:
        return dluma_player_service_local(
            owner, (DlumaObjectResult*)result);
    case 3002U:
    case 3003U:
    case 3004U:
        return DLUMA_STATUS_UNAVAILABLE;
    case 3005U:
    case 3006U:
    case 3007U:
        return dluma_player_service_vitals(
            owner, operation_id, (const DlumaObjectRequest*)request, result);
    case 3008U:
        return dluma_player_service_variable_names(
            (DlumaStringListResult*)result);
    case 3009U:
        return dluma_player_service_variable_get(
            owner,
            (const DlumaPlayerVariableRequest*)request,
            (DlumaPlayerVariableResult*)result);
    case 3010U:
        return dluma_player_service_variable_set(
            owner,
            (const DlumaPlayerVariableSetRequest*)request,
            (DlumaBoolResult*)result);
    case 3011U:
        return dluma_player_service_variable_reset(
            owner,
            (const DlumaPlayerVariableRequest*)request,
            (DlumaBoolResult*)result);
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}

void dluma_player_service_revoke_owner(uint64_t owner_id)
{
    uint32_t index;
    uint32_t engine_ids[DLUMA_PLAYER_SERVICE_MAX_OVERRIDES];
    uint32_t engine_id_count = 0;
    if (!owner_id) return;
    dluma_player_service_lock();
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_HANDLES;
         ++index) {
        if (g_dluma_player_service_handles[index].active &&
            g_dluma_player_service_handles[index].owner_id == owner_id)
            memset(
                &g_dluma_player_service_handles[index],
                0,
                sizeof(g_dluma_player_service_handles[index]));
    }
    for (index = 0;
         index < DLUMA_PLAYER_SERVICE_MAX_OVERRIDES;
         ++index) {
        const DlumaPlayerServiceOverride* entry =
            &g_dluma_player_service_overrides[index];
        uint32_t scan;
        if (!entry->active || entry->owner_id != owner_id) continue;
        for (scan = 0; scan < engine_id_count; ++scan) {
            if (engine_ids[scan] == entry->engine_id) break;
        }
        if (scan == engine_id_count)
            engine_ids[engine_id_count++] = entry->engine_id;
    }
    for (index = 0; index < engine_id_count; ++index) {
        const DlumaPlayerVariableDefinition* definition =
            dluma_player_variables_binding_definition_id(
                engine_ids[index]);
        if (definition)
            (void)dluma_player_service_reset_definition_locked(
                definition, owner_id, 1);
    }
    dluma_player_service_unlock();
}
