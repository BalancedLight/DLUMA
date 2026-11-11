#define DLUMA_BOSS_PERIODIC_NATIVE_TEST 1
#include "boss_actions_lifecycle_fake_harness.c"
static unsigned int resets, effects;
static DlumaBossInstruction *retire_on_reset, *destroy_on_execute;
static int fault_on_reset;
static void __fastcall effect_reset(DlumaBossInstruction *child) {
    (void)child; ++resets;
    if (fault_on_reset) { fault_on_reset = 0; RaiseException(0xe0ba0002U, 0, 0, NULL); }
    if (retire_on_reset) { retire_on_reset->vtable->reset(retire_on_reset); retire_on_reset = NULL; }
}
static void __fastcall effect_execute(DlumaBossInstruction *child) {
    (void)child; ++effects;
    if (destroy_on_execute) { destroy_on_execute->vtable->destroy(destroy_on_execute, 1); destroy_on_execute = NULL; }
}
static void map_reentry(void *unused) { (void)unused; dluma_boss_actions_on_map_generation_changed(2); }
static void availability_reentry(void *unused) { (void)unused; fake_available = 0; }
static void map_notification_start(void *context) { DlumaBossInstruction *start = context; start->vtable->execute(start); }
int main(void)
{
    Argument c = { NULL, "FinalAmbient", NULL }, b = { &c, "ChatTemplate", NULL }, a = { &b, "FlashTemplate", NULL };
    Fixture one;
    DlumaBossInstruction *start;
    g_dluma_boss.original = original; g_dluma_boss.read_string = read_arg;
    g_dluma_boss.free_string = free; g_dluma_boss.malloc_string = malloc;
    g_dluma_boss.map_generation = 1;
    flash_vt.reset = chatter_vt.reset = effect_reset;
    flash_vt.execute = chatter_vt.execute = effect_execute;
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    start->vtable->execute(start); assert(start->periodic_token);
    fake_now = 9999; dluma_boss_periodic_native_update(&one.boss); assert(!effects);
    fake_now = 10000; dluma_boss_periodic_native_update(&one.boss); assert(effects == 1 && resets == 1);
    dluma_boss_periodic_native_update(&one.boss); assert(effects == 1);
    start->vtable->reset(start);
    fake_now = 20000; dluma_boss_periodic_native_update(&one.boss); assert(effects == 1);
    start->vtable->destroy(start, 1);
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    start->vtable->execute(start); retire_on_reset = start;
    fake_now = 30000; dluma_boss_periodic_native_update(&one.boss); assert(effects == 1 && resets == 2 && !start->periodic_token);
    start->vtable->destroy(start, 1);
    fake_choice = 1;
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    start->vtable->execute(start); destroy_on_execute = start;
    fake_now = 40000; dluma_boss_periodic_native_update(&one.boss); assert(effects == 2 && resets == 3 && !g_dluma_boss.instructions);
    dluma_boss_periodic_native_update(&one.boss); assert(effects == 2);
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    g_dluma_boss.map_generation = 1;
    fake_read_trigger = &one.active; fake_read_action = map_reentry;
    start->vtable->execute(start); assert(!start->periodic_token);
    start->vtable->destroy(start, 1);
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    start->vtable->execute(start); fault_on_reset = 1;
    fake_now = 50000; dluma_boss_periodic_native_update(&one.boss);
    assert(effects == 2 && resets == 4 && !start->periodic_token && !start->periodic_pins);
    start->vtable->destroy(start, 1);
    {
        DlumaBossInstruction *stop;
        void *children[2];
        start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
        start->vtable->execute(start);
        stop = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, &one.active);
        children[0] = start; children[1] = stop; one.active.children = children; one.active.count = one.active.cap = 2;
        fake_now = 60000; dluma_boss_periodic_native_update(&one.boss);
        assert(effects == 2 && resets == 4 && !start->periodic_token);
        stop->vtable->destroy(stop, 1); start->vtable->destroy(start, 1);
    }
    {
        FakeChild *read_only; DWORD old;
        fake_choice = 0;
        start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
        read_only = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); assert(read_only);
        memcpy(read_only, &one.a, sizeof(*read_only)); one.ca[0] = read_only;
        assert(VirtualProtect(read_only, 4096, PAGE_READONLY, &old));
        start->vtable->execute(start); assert(start->periodic_token);
        fake_now = 70000; dluma_boss_periodic_native_update(&one.boss);
        assert(effects == 2 && resets == 4);
        start->vtable->destroy(start, 1); assert(VirtualFree(read_only, 0, MEM_RELEASE));
    }
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    fake_read_trigger = start; fake_read_action = availability_reentry;
    start->vtable->execute(start); assert(!start->periodic_token);
    fake_available = 1; start->vtable->destroy(start, 1);
    g_dluma_boss.map_generation = 1;
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    fake_map_action = map_notification_start; fake_map_context = start;
    dluma_boss_actions_on_map_generation_changed(2);
    assert(!start->periodic_token && g_dluma_boss.map_generation == 2);
    start->vtable->destroy(start, 1);
    g_dluma_boss.map_generation = 1;
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL); setup(&one, start);
    dluma_boss_actions_on_map_generation_changed(2);
    start->vtable->execute(start); assert(!start->periodic_token);
    start->vtable->destroy(start, 1);
    puts("PASS: boss periodic native effects"); return 0;
}
