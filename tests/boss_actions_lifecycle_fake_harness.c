#define DLUMA_BOSS_LIFECYCLE_TEST 1
#include "boss_actions_loader_fake_harness.c"

typedef struct FakeStr { const char *data; uint32_t size, capacity; } FakeStr;
typedef struct FakeChild { const void *vtable; uint64_t done; void *phase; FakeStr mode, group, extra; } FakeChild;
typedef struct FakePhase { void *owner; uint64_t unused; FakeStr name, target; void **children; uint32_t count, cap; } FakePhase;
typedef struct FakeBoss { unsigned char pad[88]; FakePhase *current; FakePhase **phases; uint32_t count, cap; } FakeBoss;
typedef struct Fixture { FakeBoss boss; FakePhase active, flash, chat; FakeChild a, b; void *cs[1], *ca[1], *cb[1]; FakePhase *phases[3]; } Fixture;
static DlumaBossVtable flash_vt, chatter_vt;
static int transition_vt;
static uint64_t fake_now;
static uint32_t fake_choice;
static int fake_available = 1;
int dluma_boss_periodic_host_available(void) { return fake_available; }
int dluma_boss_periodic_host_sample(void *owner, uint64_t *now, uint32_t *delay, uint32_t *choice, const void **flash, const void **chat, const void **transition)
{ (void)owner; *now = fake_now; *delay = 0; *choice = fake_choice; *flash = &flash_vt; *chat = &chatter_vt; *transition = &transition_vt; return 1; }
static FakeStr fs(const char *s) { FakeStr v = { s, (uint32_t)strlen(s), (uint32_t)strlen(s) }; return v; }
static void reset_reentrant(void *p) { DlumaBossInstruction *s = p; s->vtable->reset(s); }
static void destroy_reentrant(void *p) { DlumaBossInstruction *s = p; s->vtable->destroy(s, 1); }
static void fault_reentrant(void *p) { (void)p; RaiseException(0xe0ba0001U, 0, 0, NULL); }
static void stop_reentrant(void *p) { DlumaBossInstruction *s = p; s->vtable->execute(s); }
static void stop_flood(void *p) {
    DlumaBossInstruction *s = p; unsigned i;
    for (i = 0; i < DLUMA_BOSS_STOP_JOURNAL_CAPACITY + 1U; ++i) {
        s->vtable->reset(s); s->vtable->execute(s);
    }
}
static void setup(Fixture *f, DlumaBossInstruction *start)
{
    memset(f, 0, sizeof(*f));
    f->boss.current = &f->active; f->boss.phases = f->phases; f->boss.count = f->boss.cap = 3;
    f->phases[0] = &f->active; f->phases[1] = &f->flash; f->phases[2] = &f->chat;
    f->active.owner = f->flash.owner = f->chat.owner = &f->boss;
    f->active.name = fs("Active"); f->flash.name = fs("FlashTemplate"); f->chat.name = fs("ChatTemplate");
    f->active.children = f->cs; f->active.count = f->active.cap = 1; f->cs[0] = start; start->phase = &f->active;
    f->flash.children = f->ca; f->flash.count = f->flash.cap = 1; f->ca[0] = &f->a; f->a.phase = &f->flash; f->a.vtable = &flash_vt;
    f->chat.children = f->cb; f->chat.count = f->chat.cap = 1; f->cb[0] = &f->b; f->b.phase = &f->chat; f->b.vtable = &chatter_vt;
    f->b.mode = fs("Play"); f->b.group = fs("FinalAmbient");
}
#ifndef DLUMA_BOSS_PERIODIC_NATIVE_TEST
int main(void)
{
    Argument c = { NULL, "FinalAmbient", NULL }, b = { &c, "ChatTemplate", NULL }, a = { &b, "FlashTemplate", NULL };
    Fixture one, two; DlumaBossInstruction *s1, *s2, *stop;
    uint64_t first, second;
    g_dluma_boss.original = original; g_dluma_boss.read_string = read_arg;
    g_dluma_boss.free_string = free; g_dluma_boss.malloc_string = malloc;
    g_dluma_boss.map_generation = 1;
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    s2 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    setup(&one, s1); setup(&two, s2);
    s1->vtable->execute(s1); s2->vtable->execute(s2);
    dluma_boss_periodic_native_update(&one.boss);
    first = s1->periodic_token; second = s2->periodic_token;
    assert(first && second > first && s1->done && s2->done);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &one.active);
    stop->vtable->execute(stop); assert(stop->done && !dluma_boss_periodic_is_active(s1) && dluma_boss_periodic_is_active(s2));
    stop->vtable->destroy(stop, 1);
    s2->vtable->reset(s2); assert(!dluma_boss_periodic_is_active(s2));
    s2->vtable->execute(s2); assert(s2->periodic_token > second);
    s2->vtable->destroy(s2, 1); s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    one.chat.count = 2; s1->vtable->execute(s1); assert(s1->done && !s1->periodic_token);
    s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    fake_read_trigger = s1; fake_read_action = fault_reentrant;
    s1->vtable->execute(s1); assert(!s1->periodic_token && !s1->periodic_pins);
    s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    fake_read_trigger = s1; fake_read_action = reset_reentrant; fake_read_context = s1;
    s1->vtable->execute(s1); assert(!s1->periodic_token && !s1->done);
    s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    fake_read_trigger = s1; fake_read_action = destroy_reentrant; fake_read_context = s1;
    s1->vtable->execute(s1); /* deferred free happens after validation returns */
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &one.active);
    fake_read_trigger = s1; fake_read_action = stop_reentrant; fake_read_context = stop;
    s1->vtable->execute(s1); assert(stop->done && !s1->periodic_token);
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    s2 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    setup(&one, s1); setup(&two, s2); s2->vtable->execute(s2);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &two.active);
    fake_read_trigger = &one.active; fake_read_action = stop_reentrant; fake_read_context = stop;
    s1->vtable->execute(s1);
    assert(dluma_boss_periodic_is_active(s1) && !dluma_boss_periodic_is_active(s2));
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1); s2->vtable->destroy(s2, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &two.active);
    fake_read_trigger = &one.active; fake_read_action = stop_flood; fake_read_context = stop;
    s1->vtable->execute(s1); assert(!s1->periodic_token);
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    s2 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    setup(&one, s1); setup(&two, s2); s2->vtable->execute(s2);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &two.active);
    fake_read_trigger = s1; fake_read_action = stop_reentrant; fake_read_context = stop;
    s1->vtable->execute(s1);
    assert(dluma_boss_periodic_is_active(s1) && !dluma_boss_periodic_is_active(s2));
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1); s2->vtable->destroy(s2, 1);
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &one.active);
    fake_read_trigger = &one.active; fake_read_action = stop_reentrant; fake_read_context = stop;
    s1->vtable->execute(s1); assert(stop->done && !s1->periodic_token);
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1);
    g_dluma_boss_stop_sequence = UINT64_MAX;
    s1 = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, s1);
    stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &two.active);
    fake_read_trigger = &one.active; fake_read_action = stop_reentrant; fake_read_context = stop;
    s1->vtable->execute(s1); assert(!s1->periodic_token && g_dluma_boss_stop_exhausted);
    stop->vtable->destroy(stop, 1); s1->vtable->destroy(s1, 1);
    assert(g_dluma_boss.instructions == 0);
    puts("PASS: boss loader lifecycle"); return 0;
}
#endif
