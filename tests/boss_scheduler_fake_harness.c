#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/core/dlw_boss_scheduler.h"
#include "../src/core/dlw_boss_scheduler.c"

int main(void)
{
    DlumaBossScheduler s = {0};
    uint64_t first = 0, second = 0, token = 0;
    DlumaBossChoice choice = DLUMA_BOSS_CHOICE_NONE;
    assert(dluma_boss_scheduler_start(&s, 100, 0, &first) == DLUMA_BS_OK);
    assert(first != 0);
    assert(dluma_boss_scheduler_start(&s, 100, 0, &token) == DLUMA_BS_BUSY);
    assert(dluma_boss_scheduler_phase(&s, first + 1) == DLUMA_BS_STALE);
    /* Many short phase changes do not reset the first 10-second deadline. */
    for (uint64_t now = 101; now < 10100; now += 199) {
        assert(dluma_boss_scheduler_phase(&s, first) == DLUMA_BS_OK);
        assert(dluma_boss_scheduler_tick(&s, first, now, 0, 0, &choice) == DLUMA_BS_NOT_DUE);
    }
    assert(dluma_boss_scheduler_tick(&s, first, 10100, 0, 0, &choice) == DLUMA_BS_OK);
    assert(choice == DLUMA_BOSS_CHOICE_FLASHBACK);
    /* The attempt was consumed before native acceptance: no immediate retry. */
    assert(dluma_boss_scheduler_tick(&s, first, 10100, 1, 0, &choice) == DLUMA_BS_NOT_DUE);
    /* Long pause produces one choice, then schedules from now, never a burst. */
    assert(dluma_boss_scheduler_tick(&s, first, 90000, 1, 20000, &choice) == DLUMA_BS_OK);
    assert(choice == DLUMA_BOSS_CHOICE_CHATTER);
    assert(dluma_boss_scheduler_tick(&s, first, 90000, 0, 0, &choice) == DLUMA_BS_NOT_DUE);
    assert(dluma_boss_scheduler_tick(&s, first, 89999, 0, 0, &choice) == DLUMA_BS_INVALID_TIME);
    assert(dluma_boss_scheduler_stop(&s, first) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_tick(&s, first, 120000, 0, 0, &choice) == DLUMA_BS_STALE);
    assert(dluma_boss_scheduler_start(&s, 89999, 0, &token) == DLUMA_BS_INVALID_TIME);
    assert(dluma_boss_scheduler_start(&s, 120000, 0, &second) == DLUMA_BS_OK && second > first);
    assert(dluma_boss_scheduler_stop(&s, first) == DLUMA_BS_STALE);
    assert(dluma_boss_scheduler_finish(&s, second) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_tick(&s, second, 130000, 0, 0, &choice) == DLUMA_BS_STALE);
    assert(dluma_boss_scheduler_start(&s, 130000, 0, &token) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_reset(&s, token) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_start(&s, 140000, 0, &token) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_retire(&s, token) == DLUMA_BS_OK);
    assert(dluma_boss_scheduler_start(&s, UINT64_MAX - 5, 0, &token) == DLUMA_BS_EXHAUSTED);
    assert(dluma_boss_scheduler_start(&s, 0, 20001, &token) == DLUMA_BS_INVALID);
    s.next_token = UINT64_MAX;
    assert(dluma_boss_scheduler_start(&s, 0, 0, &token) == DLUMA_BS_EXHAUSTED);
    puts("PASS: boss scheduler");
    return 0;
}
