#ifndef DLW_BOSS_SCHEDULER_REGISTRY_H
#define DLW_BOSS_SCHEDULER_REGISTRY_H
#include "dlw_boss_scheduler.h"

#define DLUMA_BOSS_REGISTRY_CAPACITY 8u
typedef struct DlumaBossSchedulerSlot {
    DlumaBossScheduler scheduler;
    uint64_t owner_key, token, inner_token;
} DlumaBossSchedulerSlot;
typedef struct DlumaBossSchedulerRegistry {
    uint64_t next_token;
    DlumaBossSchedulerSlot slots[DLUMA_BOSS_REGISTRY_CAPACITY];
} DlumaBossSchedulerRegistry;

/* Zero initialize once per process lifetime and serialize all accesses.
 * owner_key is an opaque, host-qualified identity, never dereferenced here.
 * The native host must invalidate it before owner destruction/reuse. Tokens
 * are unique only within this one process-lifetime registry. */
DlumaBossSchedulerResult dluma_boss_registry_start(DlumaBossSchedulerRegistry *, uint64_t owner_key, uint64_t now_ms, uint32_t delay_random, uint64_t *token);
DlumaBossSchedulerResult dluma_boss_registry_tick(DlumaBossSchedulerRegistry *, uint64_t owner_key, uint64_t token, uint64_t now_ms, uint32_t choice_random, uint32_t delay_random, DlumaBossChoice *choice);
DlumaBossSchedulerResult dluma_boss_registry_stop(DlumaBossSchedulerRegistry *, uint64_t owner_key, uint64_t token);
#endif
