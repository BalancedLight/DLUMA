#include "dlw_boss_scheduler_registry.h"
#include <stddef.h>

static DlumaBossSchedulerSlot *dluma_br_find(DlumaBossSchedulerRegistry *r, uint64_t owner, uint64_t token)
{
    unsigned i;
    if (!r || !owner || !token) return NULL;
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        if (r->slots[i].owner_key == owner && r->slots[i].token == token) return &r->slots[i];
    return NULL;
}

DlumaBossSchedulerResult dluma_boss_registry_start(DlumaBossSchedulerRegistry *r, uint64_t owner, uint64_t now, uint32_t random, uint64_t *token)
{
    unsigned i;
    DlumaBossSchedulerSlot *free_slot = NULL;
    uint64_t inner = 0;
    DlumaBossSchedulerResult result;
    if (token) *token = 0;
    if (!r || !owner || !token) return DLUMA_BS_INVALID;
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i) {
        if (r->slots[i].owner_key == owner) return DLUMA_BS_BUSY;
        if (!r->slots[i].owner_key && !free_slot) free_slot = &r->slots[i];
    }
    if (!free_slot) return DLUMA_BS_BUSY;
    if (r->next_token == UINT64_MAX) return DLUMA_BS_EXHAUSTED;
    result = dluma_boss_scheduler_start(&free_slot->scheduler, now, random, &inner);
    if (result != DLUMA_BS_OK) return result;
    free_slot->owner_key = owner;
    free_slot->inner_token = inner;
    free_slot->token = ++r->next_token;
    *token = free_slot->token;
    return DLUMA_BS_OK;
}

DlumaBossSchedulerResult dluma_boss_registry_tick(DlumaBossSchedulerRegistry *r, uint64_t owner, uint64_t token, uint64_t now, uint32_t random_choice, uint32_t random_delay, DlumaBossChoice *choice)
{
    DlumaBossSchedulerSlot *slot;
    DlumaBossSchedulerResult result;
    if (choice) *choice = DLUMA_BOSS_CHOICE_NONE;
    if (!r || !owner || !token || !choice) return DLUMA_BS_INVALID;
    slot = dluma_br_find(r, owner, token);
    if (!slot) return DLUMA_BS_STALE;
    result = dluma_boss_scheduler_tick(&slot->scheduler, slot->inner_token, now, random_choice, random_delay, choice);
    if (result == DLUMA_BS_EXHAUSTED) slot->owner_key = slot->token = slot->inner_token = 0;
    return result;
}

DlumaBossSchedulerResult dluma_boss_registry_stop(DlumaBossSchedulerRegistry *r, uint64_t owner, uint64_t token)
{
    DlumaBossSchedulerSlot *slot;
    DlumaBossSchedulerResult result;
    if (!r || !owner || !token) return DLUMA_BS_INVALID;
    slot = dluma_br_find(r, owner, token);
    if (!slot) return DLUMA_BS_STALE;
    result = dluma_boss_scheduler_stop(&slot->scheduler, slot->inner_token);
    if (result == DLUMA_BS_OK) slot->owner_key = slot->token = slot->inner_token = 0;
    return result;
}
