#define DLUMA_BOSS_FACTORY_LATE_TEST 1
#include "boss_actions_loader_fake_harness.c"

int main(int argc, char **argv)
{
    DlumaBossFactory fetched = dluma_boss_factory_hook;
    BYTE *relay = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    BYTE *target = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    unsigned char node[96] = {0}, cursor[896] = {0};
    Argument c = { NULL, "{}", NULL }, b = { &c, "handler", NULL }, a = { &b, "mod.example", NULL };
    Argument pc = { NULL, "FinalAmbient", NULL }, pb = { &pc, "ChatTemplate", NULL }, pa = { &pb, "FlashTemplate", NULL };
    DlumaBossInstruction *owned = NULL, *start = NULL;
    DlumaBossString label;
    DWORD old;
    MEMORY_BASIC_INFORMATION memory;
    unsigned lines;
    int caught = 0;
    assert(relay && target);
    dluma_boss_jump(relay, (ULONG_PTR)original);
    assert(VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &old));
    FlushInstructionCache(GetCurrentProcess(), relay, DLUMA_BOSS_JUMP);
    g_dluma_boss.trampoline = relay; g_dluma_boss.target = target; g_dluma_boss.original = (DlumaBossFactory)relay;
    g_dluma_boss.read_string = read_arg; g_dluma_boss.free_string = free; g_dluma_boss.malloc_string = malloc;
    g_dluma_boss.module = GetModuleHandleW(NULL);
    assert(dluma_boss_factory_pin_modules());
    g_dluma_boss.map_generation = 1;
    if (argc > 1 && !strcmp(argv[1], "owned")) {
        owned = make_instruction("CallMod", 3, &a, NULL); assert(owned && owned->valid);
        start = make_instruction("DLUMAPeriodicChoiceStart", 3, &pa, NULL); assert(start && start->valid);
    }
    memset(g_dluma_boss.patch, 0x90, DLUMA_BOSS_STOLEN);
    memcpy(target, g_dluma_boss.patch, DLUMA_BOSS_STOLEN);
    g_dluma_boss.installed = g_dluma_boss.ever_published = 1;
    g_dluma_boss.callbacks = 1; assert(!dluma_boss_actions_stop()); g_dluma_boss.callbacks = 0;
    assert(dluma_boss_actions_stop());
    assert(g_dluma_boss.trampoline == relay && g_dluma_boss.original == (DlumaBossFactory)relay);
    assert(VirtualQuery(relay, &memory, sizeof(memory)) && memory.State == MEM_COMMIT);
    assert(!memcmp(target, g_dluma_boss.patch, DLUMA_BOSS_STOLEN));
    lines = log_lines;
    if (owned) {
        owned->vtable->reset(owned); owned->vtable->execute(owned); assert(owned->done && !enqueued);
        owned->vtable->load(owned, NULL); assert(!owned->valid);
        start->vtable->reset(start); start->vtable->execute(start); assert(start->done && !start->periodic_token);
        memset(&label, 0, sizeof(label)); start->vtable->debug(start, &label); assert(label.data); free(label.data);
        owned->vtable->destroy(owned, 1); start->vtable->destroy(start, 1);
    }
    assert(!g_dluma_boss.instructions && log_lines == lines);
    *(const char **)(node + 56) = "UnknownInstruction"; *(void **)cursor = node;
    assert(fetched(cursor, NULL) == (void *)1 && originals == 1 && !g_dluma_boss.callbacks);
    *(const char **)(node + 56) = "CallMod";
    assert(fetched(cursor, NULL) == (void *)1 && originals == 2 && !g_dluma_boss.instructions);
    original_fault = 1;
    __try { (void)fetched(cursor, NULL); } __except (1) { caught = 1; }
    assert(caught && originals == 3 && !g_dluma_boss.callbacks);
    assert(!dluma_boss_actions_start(NULL));
    assert(g_dluma_boss.trampoline == relay && g_dluma_boss.original == (DlumaBossFactory)relay);
    puts("PASS: boss factory late entry"); return 0;
}
