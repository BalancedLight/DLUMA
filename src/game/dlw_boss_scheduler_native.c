#include "dlw_boss_scheduler_native.h"

DlumaBossNativeResult dluma_boss_native_pre_forward(DlumaBossSchedulerRegistry *scheduler, uint64_t owner, uint64_t token, uint64_t now, uint32_t random_choice, uint32_t random_delay, const DlumaBossNativeCallbacks *callbacks, void *context)
{
    DlumaBossNativeResult result = DLUMA_BN_IDLE;
    DlumaBossChoice choice = DLUMA_BOSS_CHOICE_NONE;
    int stop;
    if (!callbacks || !callbacks->forward) return DLUMA_BN_INVALID;
    if (!callbacks->has_stop || !callbacks->attempt) {
        callbacks->forward(context);
        return DLUMA_BN_INVALID;
    }
    stop = callbacks->has_stop(context);
    if (stop > 0) {
        if (dluma_boss_registry_stop(scheduler, owner, token) == DLUMA_BS_OK)
            result = DLUMA_BN_STOPPED;
    } else if (stop == 0 && dluma_boss_registry_tick(scheduler, owner, token, now, random_choice, random_delay, &choice) == DLUMA_BS_OK) {
        /* Do not touch scheduler or a resolved child after this may reenter. */
        (void)callbacks->attempt(context, choice);
        result = DLUMA_BN_ATTEMPTED;
    }
    callbacks->forward(context);
    return result;
}
