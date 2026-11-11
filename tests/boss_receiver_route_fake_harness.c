#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_boss_receiver_route.h"
static int fail_restore;
static int publish_stop;
static BOOL WINAPI fake_protect(LPVOID p, SIZE_T n, DWORD protect, DWORD *old)
{
    BOOL changed;
    if (fail_restore && protect != PAGE_READWRITE) return 0;
    changed = VirtualProtect(p, n, protect, old);
    if (changed && protect == PAGE_READWRITE && publish_stop) {
        publish_stop = 0;
        assert(!dluma_boss_receiver_route_stop());
    }
    return changed;
}
#define VirtualProtect fake_protect
#include "../src/game/dlw_boss_receiver_route.c"
#undef VirtualProtect

static unsigned begin_count, updates, ends, retires, forwarded;
__declspec(dllimport) VOID WINAPI RaiseException(DWORD, DWORD, DWORD, const ULONG_PTR *);
static int update_fault, original_fault;
static HANDLE update_entered, update_release;
static DlumaBossReceiverMethod thread_method;
static void **cells;
static int nested, stop_inside;
static void *owner_expected;
int dluma_is_readable(const void *p, SIZE_T n) { (void)n; return p != NULL; }
static void __fastcall original(void *owner)
{ assert(owner == owner_expected); ++forwarded; if (original_fault) RaiseException(0xe0ba0003U, 0, 0, NULL); }
static int begin(void *owner) { assert(owner == owner_expected); ++begin_count; return 1; }
static void update(void *owner) {
    ++updates;
    if (update_fault) RaiseException(0xe0ba0004U, 0, 0, NULL);
    if (update_entered) { assert(SetEvent(update_entered)); assert(WaitForSingleObject(update_release, 3000) == 0); }
    if (nested) {
        nested = 0;
        ((DlumaBossReceiverMethod)cells[0])(owner);
    }
    if (stop_inside) assert(!dluma_boss_receiver_route_stop());
}
static void end(void *owner) { assert(owner == owner_expected); ++ends; }
static void retire(void *owner) { assert(owner == owner_expected); ++retires; }
static DWORD WINAPI worker(void *owner) { thread_method(owner); return 0; }
int main(int argc, char **argv)
{
    const void *vtable = &cells;
    DlumaBossReceiverBinding binding;
    DlumaBossReceiverCallbacks callbacks = { begin, update, end, retire };
    DlumaBossReceiverMethod fetched;
    unsigned i; DWORD old;
    MEMORY_BASIC_INFORMATION memory;
    cells = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(cells);
    memset(&binding, 0, sizeof(binding)); owner_expected = &vtable;
    binding.module = GetModuleHandleW(NULL); binding.owner_vtable = vtable;
    for (i = 0; i < 3; ++i) { cells[i] = (void *)original; binding.cells[i] = &cells[i]; binding.originals[i] = original; }
    assert(VirtualProtect(cells, 4096, PAGE_READONLY, &old));
    if (argc > 1 && !strcmp(argv[1], "publish_stop")) {
        publish_stop = 1;
        assert(!dluma_boss_receiver_route_start(&binding, &callbacks));
        assert(!dluma_boss_receiver_route_ready() && dluma_boss_receiver_route_stop());
        for (i = 0; i < 3; ++i) assert(cells[i] == (void *)original);
        assert(VirtualQuery(cells, &memory, sizeof(memory)) && memory.Protect == PAGE_READONLY);
        puts("PASS: boss normal receiver route"); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "protection")) {
        fail_restore = 1;
        assert(!dluma_boss_receiver_route_start(&binding, &callbacks));
        fail_restore = 0;
        assert(dluma_boss_receiver_route_stop());
        assert(VirtualQuery(cells, &memory, sizeof(memory)) && memory.Protect == PAGE_READONLY);
        for (i = 0; i < 3; ++i) assert(cells[i] == (void *)original);
        puts("PASS: boss normal receiver route"); return 0;
    }
    assert(dluma_boss_receiver_route_start(&binding, &callbacks));
    assert(dluma_boss_receiver_route_ready()); fetched = (DlumaBossReceiverMethod)cells[0];
    if (argc > 1 && (!strcmp(argv[1], "update_fault") || !strcmp(argv[1], "original_fault"))) {
        int caught = 0;
        update_fault = !strcmp(argv[1], "update_fault"); original_fault = !update_fault;
        __try { fetched(owner_expected); } __except (1) { caught = 1; }
        assert(caught == original_fault && begin_count == 1 && updates == 1 && ends == 1 && forwarded == 1);
        assert(retires == (unsigned)update_fault && dluma_boss_receiver_route_stop());
        puts("PASS: boss normal receiver route"); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "concurrent")) {
        HANDLE thread;
        update_entered = CreateEventW(NULL, 1, 0, NULL); update_release = CreateEventW(NULL, 1, 0, NULL);
        assert(update_entered && update_release); thread_method = fetched;
        thread = CreateThread(NULL, 0, worker, owner_expected, 0, NULL); assert(thread);
        assert(WaitForSingleObject(update_entered, 3000) == 0);
        fetched(owner_expected); assert(updates == 1 && forwarded == 1);
        assert(!dluma_boss_receiver_route_stop()); assert(SetEvent(update_release));
        assert(WaitForSingleObject(thread, 3000) == 0); assert(dluma_boss_receiver_route_stop());
        assert(updates == 1 && begin_count == 1 && ends == 1 && forwarded == 2);
        CloseHandle(thread); CloseHandle(update_entered); CloseHandle(update_release);
        puts("PASS: boss normal receiver route"); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "foreign")) {
        assert(VirtualProtect(cells, 4096, PAGE_READWRITE, &old));
        cells[2] = (void *)end;
        assert(VirtualProtect(cells, 4096, old, &old));
        assert(!dluma_boss_receiver_route_ready());
        fetched(owner_expected);
        assert(!updates && forwarded == 1);
        assert(!dluma_boss_receiver_route_stop());
        assert(cells[2] == (void *)end);
        assert(VirtualQuery(cells, &memory, sizeof(memory)) && memory.Protect == PAGE_READONLY);
        puts("PASS: boss normal receiver route"); return 0;
    }
    nested = 1;
    fetched(owner_expected);
    assert(begin_count == 1 && updates == 1 && ends == 1 && forwarded == 2);
    ((DlumaBossReceiverMethod)cells[1])(owner_expected);
    assert(retires == 1);
    stop_inside = 1;
    fetched(owner_expected);
    assert(!dluma_boss_receiver_route_ready());
    assert(dluma_boss_receiver_route_stop());
    for (i = 0; i < 3; ++i) assert(cells[i] == (void *)original);
    /* A function pointer fetched before stop may enter afterwards. */
    fetched(owner_expected);
    assert(updates == 2 && ends == 2 && forwarded == 5);
    assert(!dluma_boss_receiver_route_start(&binding, &callbacks));
    puts("PASS: boss normal receiver route"); return 0;
}
