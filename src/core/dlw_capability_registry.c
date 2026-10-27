#include "dlw_capability_registry.h"

#include <string.h>

static int dluma_capability_status_valid(DlumaStringView status)
{
    uint64_t index;
    if ((!status.data && status.size) ||
        status.size > DLUMA_CAPABILITY_STATUS_MAX_BYTES)
        return 0;
    for (index = 0; index < status.size; ++index) {
        if (!status.data[index]) return 0;
    }
    return 1;
}

static DlumaStatus dluma_capability_write_buffer(
    DlumaStringBuffer* output,
    const char* value)
{
    uint64_t length = 0;
    if (!output || !value) return DLUMA_STATUS_INVALID_ARGUMENT;
    while (value[length]) ++length;
    output->required = length + 1U;
    if (!output->data || output->capacity < output->required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    memcpy(output->data, value, (size_t)length);
    output->data[length] = 0;
    return DLUMA_STATUS_OK;
}

void dluma_capability_registry_initialize(DlumaCapabilityRegistry* registry)
{
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
    registry->struct_size = (uint32_t)sizeof(*registry);
    registry->struct_version = 1U;
}

DlumaStatus dluma_capability_registry_set(
    DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    int configured,
    int ready,
    DlumaStringView status)
{
    DlumaCapabilityEntry* entry;
    if (!registry || !capability_id ||
        capability_id > DLUMA_CAPABILITY_REGISTRY_MAX_ID ||
        !dluma_capability_status_valid(status) ||
        (ready && !configured))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    entry = &registry->entries[capability_id];
    memset(entry, 0, sizeof(*entry));
    entry->configured = configured ? 1U : 0U;
    entry->ready = ready ? 1U : 0U;
    if (status.size) {
        memcpy(entry->status, status.data, (size_t)status.size);
        entry->status[status.size] = 0;
    }
    return DLUMA_STATUS_OK;
}

int dluma_capability_registry_ready(
    const DlumaCapabilityRegistry* registry,
    uint32_t capability_id)
{
    if (!registry || !capability_id ||
        capability_id > DLUMA_CAPABILITY_REGISTRY_MAX_ID)
        return 0;
    return registry->entries[capability_id].ready != 0;
}

DlumaStatus dluma_capability_registry_query(
    const DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    DlumaCapabilityQueryResult* result)
{
    const DlumaCapabilityEntry* entry;
    DlumaStatus status;
    if (!registry || !result ||
        result->struct_size < sizeof(*result) ||
        result->struct_version != 1U ||
        !capability_id ||
        capability_id > DLUMA_CAPABILITY_REGISTRY_MAX_ID)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    entry = &registry->entries[capability_id];
    if (!entry->configured && !entry->status[0])
        return DLUMA_STATUS_NOT_FOUND;
    result->configured = entry->configured;
    result->ready = entry->ready;
    status = dluma_capability_write_buffer(&result->status, entry->status);
    return status;
}
