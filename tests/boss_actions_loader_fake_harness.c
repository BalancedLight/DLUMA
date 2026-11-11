#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/dlw_boss_actions.c"
#include "../src/core/dlw_boss_scheduler.c"
#include "../src/core/dlw_boss_scheduler_registry.c"
#include "../src/game/dlw_boss_template_resolver.c"
#include "../src/game/dlw_boss_receiver_route.c"
#include "../src/core/dlw_engine_script_bootstrap.c"
__declspec(dllimport) VOID WINAPI RaiseException(DWORD, DWORD, DWORD, const ULONG_PTR *);

static unsigned originals, enqueued, generic_enqueued, unavailable, log_lines;
static int original_fault;
static int argument_fault;
#ifdef DLUMA_BOSS_LIFECYCLE_TEST
static const void *fake_read_trigger;
static void (*fake_read_action)(void *);
static void *fake_read_context;
static void (*fake_map_action)(void *);
static void *fake_map_context;
#endif
int dluma_build_identity_parse_pe_header(const BYTE *p, SIZE_T n, DWORD *t, DWORD *s) { (void)p;(void)n;(void)t;(void)s; return 0; }
int dluma_player_actor_binding_probe_cold_profile(uintptr_t m, DlumaPlayerActorColdProfile *p) { (void)m; memset(p, 0, sizeof(*p)); return 0; }
int dluma_isolation_snapshot_script_entry(DlumaIsolationScriptEntryObservation *p) { memset(p, 0, sizeof(*p)); return 0; }
int dluma_stricmp(const char *a, const char *b) { return _stricmp(a, b); }
int dluma_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int dluma_sha256_file_hex(const WCHAR *p, ULONGLONG n, char out[DLUMA_SHA256_HEX_CAPACITY]) { (void)p;(void)n;(void)out; return 0; }
int dluma_get_module_path_checked(HMODULE h, WCHAR *p, unsigned int n) { (void)h;(void)p;(void)n; return 0; }
void dluma_mod_handler_service_queue_map_changed(uint32_t g) {
    (void)g;
#ifdef DLUMA_BOSS_LIFECYCLE_TEST
    if (fake_map_action) { void (*action)(void *) = fake_map_action; fake_map_action = NULL; action(fake_map_context); }
#endif
}
void *dluma_alloc(SIZE_T n) { return malloc(n); }
void dluma_free(void *p) { free(p); }
int dluma_is_readable(const void *p, SIZE_T n) {
    (void)n;
#ifdef DLUMA_BOSS_LIFECYCLE_TEST
    if (p == fake_read_trigger && fake_read_action) {
        void (*action)(void *) = fake_read_action; fake_read_action = NULL; action(fake_read_context);
    }
#endif
    return p != NULL;
}
void dluma_log_line(const char *l, const char *m) { (void)l; ++log_lines; if (strstr(m, "unavailable")) ++unavailable; }
void dluma_log_pair(const char *l, const char *k, const char *v) { (void)l; (void)k; (void)v; }
void dluma_log_u64(const char *l, const char *k, ULONGLONG v) { (void)l; (void)k; (void)v; }
DlumaStatus dluma_mod_handler_service_enqueue(const char *a, const char *b, const char *c, uint32_t d, uint32_t e, const char *f, const char *g, const char *h)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; ++generic_enqueued; return DLUMA_STATUS_OK; }
DlumaStatus dluma_mod_handler_service_enqueue_boss(const char *a, const char *b, const char *c, uint32_t d, const char *g, const char *h)
{ (void)a;(void)b;(void)c;(void)d;(void)g;(void)h; ++enqueued; return DLUMA_STATUS_OK; }
int dluma_quest_phase_json_canonicalize_object(const void *p, unsigned int n, char *out, unsigned int cap, unsigned int *size, DlumaQuestPhaseDiagnostic *d)
{ (void)d; if (n + 1 > cap) return 0; memcpy(out, p, n); out[n] = 0; *size = n; return 1; }
static void *original(void *s, void *p) { (void)s; (void)p; ++originals; if (original_fault) RaiseException(0xe0ba0005U, 0, 0, NULL); return (void *)1; }
static DlumaBossString *read_arg(DlumaBossParameter *p, DlumaBossString *out)
{
    if (argument_fault) RaiseException(0xe0ba0006U, 0, 0, NULL);
    const char *value = *(const char **)((unsigned char *)p->element + 8);
    size_t n = strlen(value); out->data = malloc(n + 1); memcpy(out->data, value, n + 1);
    out->size = out->capacity = (unsigned int)n; return out;
}
typedef struct Argument { struct Argument *next; const char *value; void *padding; } Argument;
static void *make_instruction(const char *tag, unsigned count, Argument *arg, void *phase)
{
    unsigned char node[96] = {0}, cursor[896] = {0};
    *(const char **)(node + 56) = tag; *(Argument **)(node + 72) = arg;
    *(unsigned *)(node + 88) = count; *(void **)cursor = node;
    return dluma_boss_factory_hook(cursor, phase);
}
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && !defined(DLUMA_BOSS_FACTORY_LATE_TEST)
int main(void)
{
    Argument c = { NULL, "FinalAmbient", NULL }, b = { &c, "ChatTemplate", NULL }, a = { &b, "FlashTemplate", NULL };
    Argument jc = { NULL, "{}", NULL }, jb = { &jc, "handler", NULL }, ja = { &jb, "mod.example", NULL };
    DlumaBossInstruction *x;
    DlumaBossString label = {0};
    int caught = 0;
    g_dluma_boss.original = original; g_dluma_boss.read_string = read_arg;
    g_dluma_boss.free_string = free; g_dluma_boss.malloc_string = malloc;
    assert(!dluma_boss_actions_start(NULL) && !dluma_boss_actions_start_cold(NULL, 0) && !dluma_boss_actions_start_cold(NULL, 1));
    assert(make_instruction("UnknownInstruction", 0, NULL, NULL) == (void *)1 && originals == 1);
    original_fault = 1;
    __try { (void)make_instruction("UnknownInstruction", 0, NULL, NULL); } __except (1) { caught = 1; }
    assert(caught && !g_dluma_boss.callbacks); original_fault = 0; caught = 0;
    argument_fault = 1;
    __try { (void)make_instruction("CallMod", 3, &ja, NULL); } __except (1) { caught = 1; }
    assert(caught && !g_dluma_boss.callbacks && !g_dluma_boss.instructions); argument_fault = 0;
    x = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    assert(x && x != (void *)1 && x->valid);
    x->vtable->debug(x, &label); assert(label.data && strstr(label.data, "DLUMAPeriodicChoiceStart")); free(label.data);
    x->vtable->execute(x); assert(x->done && unavailable && !x->periodic_token && !enqueued);
    x->vtable->reset(x); assert(!x->done); x->vtable->destroy(x, 1);
    x = make_instruction("DLUMAPeriodicChoiceStop", 0, NULL, NULL);
    assert(x && x != (void *)1 && x->valid); x->vtable->execute(x); assert(x->done); x->vtable->destroy(x, 1);
    x = make_instruction("DLUMAPeriodicChoiceStart", 2, &a, NULL);
    assert(x && !x->valid); x->vtable->execute(x); assert(x->done); x->vtable->destroy(x, 1);
    b.value = "FlashTemplate"; x = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    assert(x && !x->valid); x->vtable->destroy(x, 1); b.value = "ChatTemplate";
    c.value = "Bad Group"; x = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    assert(x && !x->valid); x->vtable->destroy(x, 1); c.value = "FinalAmbient";
    x = make_instruction("DLUMAPeriodicChoiceStop", 1, &a, NULL);
    assert(x && !x->valid); x->vtable->execute(x); assert(x->done); x->vtable->destroy(x, 1);
    x = make_instruction("CallMod", 3, &ja, NULL); assert(x && x->valid);
    g_dluma_boss.map_generation = 1; x->vtable->execute(x); assert(x->done && enqueued == 1 && generic_enqueued == 0);
    x->vtable->destroy(x, 1);
    assert(make_instruction("DLUMAPeriodicChoiceStartSuffix", 0, NULL, NULL) == (void *)1);
    assert(originals == 3 && g_dluma_boss.instructions == 0);
    puts("PASS: boss loader opcodes"); return 0;
}
#endif
