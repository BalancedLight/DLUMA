"""Execute the production identity reader with controlled owner/native seams."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_session_identity_rejects_invalid_and_changed_owners(tmp_path):
    engine = (ROOT / "src/dlw_engine.c").read_text(encoding="utf-8")
    reader = engine[engine.index("static DlumaEngineLevelQueryResult dluma_engine_read_level_identity("):
                    engine.index("DlumaEngineLevelQueryResult dluma_engine_query_level_identity(")]
    session = engine[engine.index("int dluma_engine_query_session_level_identity("):
                     engine.index("int dluma_engine_query_level(")]
    source = tmp_path / "identity.c"
    source.write_text(r'''
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define DLUMA_MAX_MAP_NAME 128
#define DLUMA_ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
typedef enum { DLUMA_ENGINE_LEVEL_QUERY_INVALID, DLUMA_ENGINE_LEVEL_QUERY_NULL,
    DLUMA_ENGINE_LEVEL_QUERY_SUCCESS } DlumaEngineLevelQueryResult;
typedef struct { int ready; const char* (*get_level_name)(void*); } DlumaEngineState;
static int authorized = 1, available = 1, changed, calls, throw_name;
static ULONG_PTR vtable[2] = {1,1};
static ULONG_PTR fake_level[1], replacement[1];
static const char* map = "test_map";
static int dluma_is_readable(const void* p, size_t n) { (void)n; return p != 0; }
static void dluma_str_copy(char* out, unsigned int cap, const char* in) { strncpy_s(out, cap, in, _TRUNCATE); }
static void dluma_normalize_map_name(const char* in, char* out, unsigned int cap) { dluma_str_copy(out, cap, in); }
static int dluma_frontend_level_name_is_frontend(const char* s) { return !strcmp(s, "menu"); }
static int dluma_engine_game_frame_dispatch_active_on_current_thread(DlumaEngineState* s) { return s && authorized; }
static int dluma_player_media_adapter_get_session_level(void** out) {
    ++calls; *out = available ? (changed && calls > 1 ? replacement : fake_level) : 0; return available;
}
static const char* get_name(void* p) {
    (void)p; if (throw_name) RaiseException(0xE0000001, 0, 0, 0); return map;
}
''' + reader + session + r'''
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    DlumaEngineState state = {1, get_name}; char name[128];
    void* observed = 0; ULONG_PTR observed_vtable = 0;
    fake_level[0] = (ULONG_PTR)vtable; replacement[0] = (ULONG_PTR)vtable;
    CHECK(dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    CHECK(observed == fake_level && observed_vtable == (ULONG_PTR)vtable && !strcmp(name,"test_map"));
    calls = 0; changed = 1;
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    CHECK(!observed && !observed_vtable && !name[0]);
    changed = 0; available = 0;
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    available = 1; map = "menu";
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    map = "test_map"; authorized = 0; calls = 0;
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable) && !calls);
    authorized = 1; fake_level[0] = 0;
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    fake_level[0] = (ULONG_PTR)vtable; throw_name = 1;
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    CHECK(!observed && !name[0]); throw_name = 0; map = "";
    CHECK(!dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    map = "test_map";
    CHECK(dluma_engine_query_session_level_identity(&state,name,sizeof(name),&observed,&observed_vtable));
    puts("session level identity: ok"); return 0;
}
''', encoding="utf-8")
    executable = tmp_path / "identity.exe"
    command = subprocess.list2cmdline([
        "cl.exe", "/nologo", "/TC", "/W4", "/WX", "/O2",
        str(source), f"/Fo{tmp_path / 'identity.obj'}", f"/Fe{executable}",
    ])
    compiled = subprocess.run(
        ["cmd.exe", "/d", "/c", "call tools\\setup_msvc.bat && " + command],
        cwd=ROOT, capture_output=True, text=True, timeout=180,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
    assert ran.returncode == 0, ran.stdout + ran.stderr


def test_null_session_fallback_does_not_authorize_startup_admission():
    ui = (ROOT / "src/dlw_ui.c").read_text(encoding="utf-8")
    start = ui.index("/* Workshop sessions can keep GetActiveLevel null.")
    fallback = ui[start:ui.index("frontend_identity_latched =", start)]
    assert "query_result == DLUMA_ENGINE_LEVEL_QUERY_NULL" in fallback
    assert "dluma_ui_publish_level_snapshot(" in fallback
    assert "query_result =" not in fallback.replace("query_result ==", "")
    assert "dluma_custom_map_content_game_thread_tick" not in fallback
    bind = ui[ui.index("static int dluma_ui_bind_info_panel(void)"):]
    assert bind.index("if (!g_dluma_info_module) return 0;") < bind.index("g_dluma_info_bind_attempted = 1;")
