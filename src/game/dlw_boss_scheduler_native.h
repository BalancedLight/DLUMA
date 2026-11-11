#ifndef DLW_BOSS_SCHEDULER_NATIVE_H
#define DLW_BOSS_SCHEDULER_NATIVE_H
#include "../core/dlw_boss_scheduler_registry.h"

typedef enum DlumaBossNativeResult {
    DLUMA_BN_IDLE, DLUMA_BN_STOPPED, DLUMA_BN_ATTEMPTED, DLUMA_BN_INVALID
} DlumaBossNativeResult;
typedef struct DlumaBossNativeCallbacks {
    /* Positive: current phase contains our Stop; zero: no Stop;
     * negative: could not safely inspect. */
    int (*has_stop)(void *context);
    /* Resolve, validate and attempt inside this call. A zero result may mean
     * absent, failed or uncertain; the core must never retry/fallback. */
    int (*attempt)(void *context, DlumaBossChoice choice);
    void (*forward)(void *context);
} DlumaBossNativeCallbacks;

/* Disabled orchestration seam only. No engine hook, memory traversal, native
 * borrow qualification, parser registration or game capability is supplied.
 * Context is a callback-scoped borrow and is never retained. Caller must
 * serialize state and supply a qualified receiver and exactly-once forward.
 * Native callback implementation must contain faults and reentrant cleanup. */
DlumaBossNativeResult dluma_boss_native_pre_forward(DlumaBossSchedulerRegistry *, uint64_t owner_key, uint64_t token, uint64_t now_ms, uint32_t choice_random, uint32_t delay_random, const DlumaBossNativeCallbacks *, void *context);
#endif
