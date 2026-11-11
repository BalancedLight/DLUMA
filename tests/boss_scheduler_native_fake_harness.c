#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/game/dlw_boss_scheduler_native.h"
#include "../src/core/dlw_boss_scheduler.c"
#include "../src/core/dlw_boss_scheduler_registry.c"
#include "../src/game/dlw_boss_scheduler_native.c"

typedef struct Fake {
    unsigned calls, forwards, stop, valid;
    DlumaBossChoice last;
} Fake;
static int has_stop(void *ctx) { return ((Fake *)ctx)->stop != 0; }
static int attempt(void *ctx, DlumaBossChoice choice)
{
    Fake *f = (Fake *)ctx;
    ++f->calls;
    f->last = choice;
    return f->valid != 0;
}
static void forward(void *ctx) { ++((Fake *)ctx)->forwards; }
int main(void)
{
    DlumaBossSchedulerRegistry scheduler = {0};
    DlumaBossNativeCallbacks callbacks = { has_stop, attempt, forward };
    Fake fake = {0};
    uint64_t token = 0;
    assert(dluma_boss_registry_start(&scheduler, 7, 0, 0, &token) == DLUMA_BS_OK);
    /* A victory stop in the current phase wins over a due pre-forward tick. */
    fake.stop = 1;
    assert(dluma_boss_native_pre_forward(&scheduler, 7, token, 10000, 0, 0, &callbacks, &fake) == DLUMA_BN_STOPPED);
    assert(fake.calls == 0 && fake.forwards == 1);
    assert(dluma_boss_registry_start(&scheduler, 7, 10000, 0, &token) == DLUMA_BS_OK);
    fake.stop = 0;
    /* Missing/failed child consumes the attempt; original still runs exactly once. */
    assert(dluma_boss_native_pre_forward(&scheduler, 7, token, 20000, 1, 0, &callbacks, &fake) == DLUMA_BN_ATTEMPTED);
    assert(fake.calls == 1 && fake.last == DLUMA_BOSS_CHOICE_CHATTER && fake.forwards == 2);
    fake.valid = 1;
    assert(dluma_boss_native_pre_forward(&scheduler, 7, token, 20000, 0, 0, &callbacks, &fake) == DLUMA_BN_IDLE);
    assert(fake.calls == 1 && fake.forwards == 3);
    assert(dluma_boss_native_pre_forward(&scheduler, 7, token + 1, 30000, 0, 0, &callbacks, &fake) == DLUMA_BN_IDLE);
    assert(fake.calls == 1 && fake.forwards == 4);
    puts("PASS: disabled boss callback orchestration");
    return 0;
}
