from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def test_lua_owner_tracks_only_authoritative_serialized_frame(tmp_path):
    source = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    prefix = source.split("void dluma_lua_runtime_game_thread_pump(void)", 1)[1]
    prefix = prefix.split("/* Intent commands", 1)[0]
    harness = r"""
#include <assert.h>
#include <stdint.h>
#include "core/dlw_mod_owner.h"
#define DLUMA_LUA_RUNTIME_MAX_STATES 3
#define DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE 1
typedef struct { int occupied, teardown_pending; DlumaModOwner api_owner; } DlumaLuaRuntimeSlot;
static struct { int stop_requested, engine; } g_dluma;
static struct { int pump_active; DlumaLuaRuntimeSlot slots[3]; } g_dluma_lua_runtime;
static volatile int g_dluma_map_exit_active;
static int route, authority;
static uint32_t thread_id;
static int dluma_engine_game_frame_dispatch_active_on_current_thread(void* engine) { (void)engine; return route; }
static int dluma_engine_game_frame_authority_status(void* engine, uint64_t now) { (void)engine; (void)now; return authority; }
static uint64_t GetTickCount64(void) { return 100; }
static uint32_t GetCurrentThreadId(void) { return thread_id; }
static int dluma_atomic_compare_exchange(volatile int* p, int value, int expected) { int old=*p; if(old==expected)*p=value; return old; }
void pump(void)
""" + prefix + r"""
}
int main(void) {
    DlumaModOwner* owner=&g_dluma_lua_runtime.slots[0].api_owner;
    g_dluma_lua_runtime.slots[0].occupied=1;
    owner->state=DLUMA_MOD_OWNER_ACTIVE;
    owner->scope=DLUMA_SCOPE_MAP;
    owner->game_thread_id=11;
    thread_id=22;
    route=authority=1;
    g_dluma_map_exit_active=1; pump();
    assert(owner->game_thread_id==11 && !g_dluma_lua_runtime.pump_active);
    g_dluma_map_exit_active=0;
    g_dluma.stop_requested=1; pump();
    assert(owner->game_thread_id==11 && !g_dluma_lua_runtime.pump_active);
    g_dluma.stop_requested=0;
    route=0; authority=1; pump();
    assert(owner->game_thread_id==11);
    route=1; authority=0; pump();
    assert(owner->game_thread_id==11);
    authority=1; g_dluma_lua_runtime.pump_active=1; pump();
    assert(owner->game_thread_id==11);
    g_dluma_lua_runtime.pump_active=0; pump();
    assert(owner->game_thread_id==22);
    assert(dluma_mod_owner_require(owner, 4U, 0, 1, 11)==DLUMA_STATUS_WRONG_THREAD);
    assert(dluma_mod_owner_require(owner, 4U, 0, 1, 22)==DLUMA_STATUS_OK);
    g_dluma_lua_runtime.pump_active=0;
    g_dluma_lua_runtime.slots[0].teardown_pending=1; thread_id=33; pump();
    assert(owner->game_thread_id==22);
    g_dluma_lua_runtime.pump_active=0;
    g_dluma_lua_runtime.slots[0].teardown_pending=0;
    owner->state=DLUMA_MOD_OWNER_REVOKED; pump();
    assert(owner->game_thread_id==22);
    return 0;
}
"""
    c_file = tmp_path / "thread_migration.c"
    exe = tmp_path / "thread_migration.exe"
    c_file.write_text(harness, encoding="utf-8")
    command = (
        'cmd.exe /d /s /c "call tools\\setup_msvc.bat && '
        f'cl /nologo /W0 /MT /I src /I sdk/c "{c_file}" '
        f'src/core/dlw_mod_owner.c /Fo"{tmp_path}\\\\" /Fe"{exe}"'
        '"'
    )
    compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    subprocess.run([str(exe)], check=True, timeout=10)

def test_lua_capability_refresh_tracks_readiness_and_preserves_host_services(tmp_path):
    source = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    refresh = source.split("static void dluma_lua_runtime_refresh_api_capabilities(void)", 1)[1]
    refresh = refresh.split("void dluma_lua_runtime_game_thread_pump(void)", 1)[0]
    pump = source.split("void dluma_lua_runtime_game_thread_pump(void)", 1)[1]
    assert pump.index("dluma_lua_runtime_take_pending();") < pump.index("dluma_lua_runtime_refresh_api_capabilities();")
    assert pump.index("dluma_lua_runtime_refresh_api_capabilities();") < pump.index("dluma_lua_runtime_dispatch_queued_events();")
    harness = r"""
#include <assert.h>
#define DLUMA_LUA_RUNTIME_MAX_STATES 4
#define DLUMA_MOD_OWNER_ACTIVE 1
typedef struct { int world, hud, host; } Registry;
typedef struct { int state; } Owner;
typedef struct { int occupied, teardown_pending; Owner api_owner; Registry api_capabilities; int api_host; } DlumaLuaRuntimeSlot;
static struct { DlumaLuaRuntimeSlot slots[4]; } g_dluma_lua_runtime;
static int world_ready, hud_ready;
static void dluma_game_backend_prepare_capabilities(Registry* r) {
    r->world=world_ready; r->hud=hud_ready; r->host=0;
}
static void dluma_game_host_context_prepare_capabilities(int* host, Registry* r) { r->host=*host; }
void refresh(void)
""" + refresh + r"""
int main(void) {
    DlumaLuaRuntimeSlot* slot=&g_dluma_lua_runtime.slots[0];
    slot->occupied=1; slot->api_owner.state=1; slot->api_host=7;
    refresh(); assert(!slot->api_capabilities.world && !slot->api_capabilities.hud);
    assert(slot->api_capabilities.host==7);
    world_ready=1; hud_ready=1; refresh();
    assert(slot->api_capabilities.world && slot->api_capabilities.hud && slot->api_capabilities.host==7);
    world_ready=0; hud_ready=0; refresh();
    assert(!slot->api_capabilities.world && !slot->api_capabilities.hud && slot->api_capabilities.host==7);
    world_ready=1;
    slot->teardown_pending=1; refresh(); assert(!slot->api_capabilities.world);
    slot->teardown_pending=0; slot->api_owner.state=0; refresh(); assert(!slot->api_capabilities.world);
    slot->api_owner.state=1; slot->occupied=0; refresh(); assert(!slot->api_capabilities.world);
    return 0;
}
"""
    c_file = tmp_path / "capability_refresh.c"
    exe = tmp_path / "capability_refresh.exe"
    c_file.write_text(harness, encoding="utf-8")
    command = (
        'cmd.exe /d /s /c "call tools\\setup_msvc.bat && '
        f'cl /nologo /W4 /WX /MT "{c_file}" /Fo"{tmp_path}\\\\" /Fe"{exe}"'
        '"'
    )
    compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    subprocess.run([str(exe)], check=True, timeout=10)
