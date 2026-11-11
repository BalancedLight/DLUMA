#ifndef DLW_BOSS_SCHEDULER_H
#define DLW_BOSS_SCHEDULER_H

#include <stdint.h>

typedef enum DlumaBossSchedulerResult {
    DLUMA_BS_OK, DLUMA_BS_NOT_DUE, DLUMA_BS_STALE, DLUMA_BS_BUSY,
    DLUMA_BS_INVALID, DLUMA_BS_INVALID_TIME, DLUMA_BS_EXHAUSTED
} DlumaBossSchedulerResult;
typedef enum DlumaBossChoice {
    DLUMA_BOSS_CHOICE_NONE, DLUMA_BOSS_CHOICE_FLASHBACK, DLUMA_BOSS_CHOICE_CHATTER
} DlumaBossChoice;
typedef struct DlumaBossScheduler {
    uint64_t next_token, active_token, deadline_ms, last_ms;
} DlumaBossScheduler;

/* Zero initialize once per process lifetime; the serialized host owns this
 * copied-data state. Never clear it to reuse tokens. Inputs are monotonic
 * milliseconds and caller-supplied random values: choice 0..1 and delay
 * offset 0..20000 milliseconds. No native object or child pointer is stored.
 * A successful tick consumes the choice and schedules from now before the
 * caller attempts an effect. Failure or uncertain acceptance must not cause
 * automatic retry or fallback. Output does not mean an effect was applied. */
DlumaBossSchedulerResult dluma_boss_scheduler_start(DlumaBossScheduler *, uint64_t now_ms, uint32_t delay_random, uint64_t *token);
DlumaBossSchedulerResult dluma_boss_scheduler_phase(DlumaBossScheduler *, uint64_t token);
DlumaBossSchedulerResult dluma_boss_scheduler_tick(DlumaBossScheduler *, uint64_t token, uint64_t now_ms, uint32_t choice_random, uint32_t delay_random, DlumaBossChoice *choice);
DlumaBossSchedulerResult dluma_boss_scheduler_stop(DlumaBossScheduler *, uint64_t token);
DlumaBossSchedulerResult dluma_boss_scheduler_finish(DlumaBossScheduler *, uint64_t token);
DlumaBossSchedulerResult dluma_boss_scheduler_reset(DlumaBossScheduler *, uint64_t token);
DlumaBossSchedulerResult dluma_boss_scheduler_retire(DlumaBossScheduler *, uint64_t token);
#endif
