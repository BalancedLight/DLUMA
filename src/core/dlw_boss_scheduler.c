#include "dlw_boss_scheduler.h"
#include <stddef.h>

#define DLUMA_BS_MIN_DELAY 10000u
#define DLUMA_BS_DELAY_RANGE 20000u

static DlumaBossSchedulerResult dluma_bs_current(DlumaBossScheduler *s, uint64_t token)
{
    if (!s || !token) return DLUMA_BS_INVALID;
    return s->active_token == token ? DLUMA_BS_OK : DLUMA_BS_STALE;
}

DlumaBossSchedulerResult dluma_boss_scheduler_start(DlumaBossScheduler *s, uint64_t now, uint32_t random, uint64_t *token)
{
    uint64_t delay;
    if (token) *token = 0;
    if (!s || !token || random > DLUMA_BS_DELAY_RANGE) return DLUMA_BS_INVALID;
    if (s->active_token) return DLUMA_BS_BUSY;
    delay = DLUMA_BS_MIN_DELAY + (uint64_t)random;
    if (s->next_token == UINT64_MAX || now > UINT64_MAX - delay) return DLUMA_BS_EXHAUSTED;
    if (now < s->last_ms) return DLUMA_BS_INVALID_TIME;
    s->active_token = ++s->next_token;
    s->last_ms = now;
    s->deadline_ms = now + delay;
    *token = s->active_token;
    return DLUMA_BS_OK;
}

DlumaBossSchedulerResult dluma_boss_scheduler_phase(DlumaBossScheduler *s, uint64_t token)
{
    return dluma_bs_current(s, token);
}

DlumaBossSchedulerResult dluma_boss_scheduler_tick(DlumaBossScheduler *s, uint64_t token, uint64_t now, uint32_t choice_random, uint32_t delay_random, DlumaBossChoice *choice)
{
    DlumaBossSchedulerResult result;
    uint64_t delay;
    if (choice) *choice = DLUMA_BOSS_CHOICE_NONE;
    if (!choice || choice_random > 1 || delay_random > DLUMA_BS_DELAY_RANGE) return DLUMA_BS_INVALID;
    result = dluma_bs_current(s, token);
    if (result != DLUMA_BS_OK) return result;
    if (now < s->last_ms) return DLUMA_BS_INVALID_TIME;
    s->last_ms = now;
    if (now < s->deadline_ms) return DLUMA_BS_NOT_DUE;
    delay = DLUMA_BS_MIN_DELAY + (uint64_t)delay_random;
    if (now > UINT64_MAX - delay) {
        s->active_token = 0;
        return DLUMA_BS_EXHAUSTED;
    }
    s->deadline_ms = now + delay;
    *choice = choice_random ? DLUMA_BOSS_CHOICE_CHATTER : DLUMA_BOSS_CHOICE_FLASHBACK;
    return DLUMA_BS_OK;
}

DlumaBossSchedulerResult dluma_boss_scheduler_stop(DlumaBossScheduler *s, uint64_t token)
{
    DlumaBossSchedulerResult result = dluma_bs_current(s, token);
    if (result == DLUMA_BS_OK) s->active_token = 0;
    return result;
}
DlumaBossSchedulerResult dluma_boss_scheduler_finish(DlumaBossScheduler *s, uint64_t token) { return dluma_boss_scheduler_stop(s, token); }
DlumaBossSchedulerResult dluma_boss_scheduler_reset(DlumaBossScheduler *s, uint64_t token) { return dluma_boss_scheduler_stop(s, token); }
DlumaBossSchedulerResult dluma_boss_scheduler_retire(DlumaBossScheduler *s, uint64_t token) { return dluma_boss_scheduler_stop(s, token); }
