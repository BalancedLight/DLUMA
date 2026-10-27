#ifndef DLUMA_CAPABILITY_REGISTRY_H
#define DLUMA_CAPABILITY_REGISTRY_H

#include "../../sdk/c/dluma_api.h"

#define DLUMA_CAPABILITY_REGISTRY_MAX_ID 255U
#define DLUMA_CAPABILITY_STATUS_MAX_BYTES 127U

typedef struct DlumaCapabilityEntry {
    uint32_t configured;
    uint32_t ready;
    char status[DLUMA_CAPABILITY_STATUS_MAX_BYTES + 1U];
} DlumaCapabilityEntry;

typedef struct DlumaCapabilityRegistry {
    uint32_t struct_size;
    uint32_t struct_version;
    DlumaCapabilityEntry entries[DLUMA_CAPABILITY_REGISTRY_MAX_ID + 1U];
} DlumaCapabilityRegistry;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_capability_registry_initialize(DlumaCapabilityRegistry* registry);
DlumaStatus dluma_capability_registry_set(
    DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    int configured,
    int ready,
    DlumaStringView status);
int dluma_capability_registry_ready(
    const DlumaCapabilityRegistry* registry,
    uint32_t capability_id);
DlumaStatus dluma_capability_registry_query(
    const DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    DlumaCapabilityQueryResult* result);

#ifdef __cplusplus
}
#endif

#endif
