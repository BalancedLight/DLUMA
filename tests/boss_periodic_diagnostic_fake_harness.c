#define DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC 1
#define DLUMA_BOSS_FACTORY_LATE_TEST 1
#include "boss_actions_loader_fake_harness.c"
#include "../src/dlw_project_owner_identity.h"
__declspec(dllimport) BOOL WINAPI SetEnvironmentVariableW(LPCWSTR, LPCWSTR);
DlumaRuntime g_dluma;
static int frame_active = 1, identity_matches = 1, anchor_matches = 1;
int dluma_engine_game_frame_dispatch_active_on_current_thread(const DlumaEngineState *engine)
{ (void)engine; return frame_active; }
int dluma_open_directory_identity(const WCHAR *path, DlumaDirectoryIdentity *out)
{ if (!path || !path[0]) return 0; memset(out, 0, sizeof(*out)); out->volume_serial = 1; out->file_index_low = 7; return 1; }
int dluma_directory_identity_equal(const DlumaDirectoryIdentity *a, const DlumaDirectoryIdentity *b)
{ (void)a; (void)b; return identity_matches; }
int dluma_validate_map_anchor_under_project(const WCHAR *p, const char *m, const DlumaDirectoryIdentity *i)
{ (void)p; (void)m; (void)i; return anchor_matches; }
int dluma_valid_map_segment(const char *m) { return m && m[0] && !strchr(m, '/'); }
int dluma_utf8_to_wide(const char *s, WCHAR *out, unsigned int n)
{ unsigned int i; for(i=0;s && s[i] && i+1U<n;++i) out[i]=(WCHAR)s[i]; out[i]=0; return s && !s[i]; }
int dluma_wstricmp(const WCHAR *a, const WCHAR *b) { return _wcsicmp(a,b); }
int main(void)
{
    DlumaBossInstruction *start;
    uint64_t token = 0;
    Argument c = { NULL, "FinalAmbient", NULL }, b = { &c, "ChatTemplate", NULL }, a = { &b, "FlashTemplate", NULL };
    assert(!g_dluma_boss_periodic_reviewed);
    assert(SetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_DIAGNOSTIC", L"1"));
    assert(SetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_PROJECT", L"C:\\fixture"));
    assert(SetEnvironmentVariableW(L"DLUMA_BOSS_PERIODIC_MAP", L"fixture_map"));
    assert(!dluma_boss_periodic_diagnostic_configure());
    g_dluma.config.developer_mode = 1;
    assert(dluma_boss_periodic_diagnostic_configure());
    g_dluma_boss.map_generation = 1;
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "fixture_map");
    assert(dluma_boss_periodic_diagnostic_allowed());
    g_dluma.config.developer_mode = 0; assert(!dluma_boss_periodic_diagnostic_allowed());
    g_dluma.config.developer_mode = 1;
    dluma_boss_actions_set_periodic_context(2, L"C:\\fixture", "fixture_map");
    assert(!dluma_boss_periodic_diagnostic_allowed());
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "wrong_map");
    assert(!dluma_boss_periodic_diagnostic_allowed());
    identity_matches = 0;
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "fixture_map");
    assert(!dluma_boss_periodic_diagnostic_allowed()); identity_matches = 1;
    anchor_matches = 0;
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "fixture_map");
    assert(!dluma_boss_periodic_diagnostic_allowed()); anchor_matches = 1;
    frame_active = 0;
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "fixture_map");
    assert(!dluma_boss_periodic_diagnostic_allowed()); frame_active = 1;
    dluma_boss_actions_set_periodic_context(1, L"C:\\fixture", "fixture_map");
    assert(dluma_boss_periodic_diagnostic_allowed());
    dluma_boss_actions_on_map_generation_changed(2);
    assert(!dluma_boss_periodic_diagnostic_allowed());
    dluma_boss_actions_set_periodic_context(2, L"C:\\fixture", "fixture_map");
    assert(dluma_boss_periodic_diagnostic_allowed());
    g_dluma_boss.original = original; g_dluma_boss.read_string = read_arg;
    g_dluma_boss.free_string = free; g_dluma_boss.malloc_string = malloc;
    start = make_instruction("DLUMAPeriodicChoiceStart", 3, &a, NULL);
    assert(start && dluma_boss_registry_start(&g_dluma_boss_periodic_registry, 123, 0, 0, &token) == DLUMA_BS_OK);
    start->periodic_owner = 123; start->periodic_token = token;
    g_dluma_boss_periodic_starts[0] = start;
    g_dluma_boss_pending[0].owner = 123; g_dluma_boss_pending[0].references = 1;
    dluma_boss_actions_set_periodic_context(0, L"", "");
    assert(!dluma_boss_periodic_diagnostic_allowed() && !start->periodic_token);
    assert(g_dluma_boss_pending[0].revision == 1);
    memset(&g_dluma_boss_pending[0], 0, sizeof(g_dluma_boss_pending[0]));
    start->vtable->destroy(start, 1);
    dluma_boss_actions_on_map_generation_changed(0);
    assert(!dluma_boss_periodic_diagnostic_allowed());
    assert(!dluma_boss_periodic_available()); /* consent never substitutes for the real receiver route */
    puts("PASS: boss private periodic consent"); return 0;
}
