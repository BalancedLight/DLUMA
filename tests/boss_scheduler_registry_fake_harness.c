#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/core/dlw_boss_scheduler.h"
#include "../src/core/dlw_boss_scheduler_registry.h"
#include "../src/core/dlw_boss_scheduler.c"
#include "../src/core/dlw_boss_scheduler_registry.c"
int main(void)
{
    DlumaBossSchedulerRegistry r = {0};
    uint64_t a = 0, b = 0, c = 0, ids[DLUMA_BOSS_REGISTRY_CAPACITY];
    DlumaBossChoice choice = DLUMA_BOSS_CHOICE_NONE;
    unsigned i;
    assert(dluma_boss_registry_start(&r, 7, 0, 0, &a) == DLUMA_BS_OK);
    assert(dluma_boss_registry_start(&r, 8, 0, 0, &b) == DLUMA_BS_OK && b > a);
    assert(dluma_boss_registry_start(&r, 7, 0, 0, &c) == DLUMA_BS_BUSY);
    assert(dluma_boss_registry_tick(&r, 8, a, 10000, 0, 0, &choice) == DLUMA_BS_STALE);
    assert(dluma_boss_registry_tick(&r, 7, a, 10000, 0, 0, &choice) == DLUMA_BS_OK && choice == DLUMA_BOSS_CHOICE_FLASHBACK);
    assert(dluma_boss_registry_stop(&r, 8, b) == DLUMA_BS_OK);
    assert(dluma_boss_registry_start(&r, 8, 10000, 0, &c) == DLUMA_BS_OK && c > b);
    assert(dluma_boss_registry_stop(&r, 8, b) == DLUMA_BS_STALE);
    assert(dluma_boss_registry_stop(&r, 7, a) == DLUMA_BS_OK);
    assert(dluma_boss_registry_stop(&r, 8, c) == DLUMA_BS_OK);
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        assert(dluma_boss_registry_start(&r, i + 1, 10000, 0, &ids[i]) == DLUMA_BS_OK);
    assert(dluma_boss_registry_start(&r, 100, 10000, 0, &a) == DLUMA_BS_BUSY);
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        assert(dluma_boss_registry_stop(&r, i + 1, ids[i]) == DLUMA_BS_OK);
    r.next_token = UINT64_MAX;
    assert(dluma_boss_registry_start(&r, 100, 10000, 0, &a) == DLUMA_BS_EXHAUSTED);
    puts("PASS: boss scenario registry");
    return 0;
}
