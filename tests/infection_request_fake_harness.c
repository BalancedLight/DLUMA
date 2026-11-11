#include "../src/core/dlw_infection_request.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Fake {
    DlumaInfectionFrame frame;
    int present, exact, active, code, controller, matches;
    int captures, invokes, changed_before, changed_after, failed_after;
    int counter_changed_before, invoke_failed, done_missing, increment;
    int stack_valid;
    int host_kind;
} Fake;
static int capture(void *user, DlumaInfectionFrame *out)
{
    Fake *f = user; f->captures++;
    if ((f->host_kind != 2 && f->host_kind != 3) ||
        !f->present || !f->exact || !f->active || !f->code ||
        !f->controller || !f->matches || (f->failed_after && f->captures == 3)) return 0;
    *out = f->frame;
    if (f->changed_before && f->captures == 2) out->player++;
    if (f->counter_changed_before && f->captures == 2) out->counter++;
    if (f->changed_after && f->captures == 3) out->controller++;
    return 1;
}
static int invoke(void *user, void *instruction, size_t size)
{
    Fake *f = user; const uint64_t *words = instruction;
    f->invokes++;
    f->stack_valid = size == 24 && !((uintptr_t)instruction & 7u) &&
        !words[0] && !words[1] && !words[2];
    if (f->invoke_failed) return 0;
    f->frame.counter += (uint32_t)f->increment;
    if (!f->done_missing) ((unsigned char *)instruction)[8] = 1;
    return 1;
}
static void setup(Fake *f)
{
    memset(f, 0, sizeof(*f));
    f->present = f->exact = f->active = f->code = f->controller = f->matches = 1;
    f->frame.game = 1; f->frame.session = 2; f->frame.logical = 3;
    f->frame.membership = 4; f->frame.player = 5; f->frame.controller = 6;
    f->frame.level = 7; f->frame.frame_receiver = 8; f->frame.level_vtable = 9;
    f->frame.frame_token = 10;
    f->frame.counter = 42; f->increment = 1;
    f->host_kind = 2;
}
int main(void)
{
    Fake f; DlumaInfectionRequestDriver d = {&f, capture, invoke};
    unsigned i;
    setup(&f);
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_ACCEPTED);
    CHECK(f.invokes == 1 && f.frame.counter == 43 && f.stack_valid);
    setup(&f); f.host_kind = 0;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_UNAVAILABLE);
    CHECK(f.invokes == 0);
    setup(&f); f.host_kind = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_UNAVAILABLE);
    CHECK(f.invokes == 0);
    setup(&f); f.host_kind = 3;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_ACCEPTED);
    for (i = 0; i < 6; ++i) {
        setup(&f);
        switch (i) {
        case 0: f.present = 0; break;
        case 1: f.exact = 0; break;
        case 2: f.active = 0; break;
        case 3: f.code = 0; break;
        case 4: f.controller = 0; break;
        default: f.matches = 0; break;
        }
        CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_UNAVAILABLE);
        CHECK(f.invokes == 0);
    }
    setup(&f); f.frame.controller = 0;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_UNAVAILABLE);
    CHECK(f.invokes == 0);
    setup(&f); f.changed_before = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED);
    CHECK(f.invokes == 0);
    setup(&f); f.counter_changed_before = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED);
    CHECK(f.invokes == 0);
    setup(&f); f.changed_after = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED);
    CHECK(f.invokes == 1);
    setup(&f); f.failed_after = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_CONTEXT_CHANGED);
    CHECK(f.invokes == 1);
    setup(&f); f.invoke_failed = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_NATIVE_FAILED);
    CHECK(f.invokes == 1 && f.frame.counter == 42);
    setup(&f); f.increment = 0;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_NATIVE_FAILED);
    CHECK(f.invokes == 1);
    setup(&f); f.increment = 2;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_NATIVE_FAILED);
    setup(&f); f.done_missing = 1;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_NATIVE_FAILED);
    setup(&f); f.frame.counter = UINT32_MAX;
    CHECK(dluma_infection_request_perform(&d) == DLUMA_PLAYER_INFECTION_ACCEPTED);
    CHECK(f.frame.counter == 0 && f.stack_valid);
    puts("PASS: original infection request admission and acknowledgment"); return 0;
}
