from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_native_and_managed_dispatch_reject_events_from_previous_map(tmp_path):
    native = (ROOT / "src/dlw_native_mods.c").read_text(encoding="utf-8")
    managed = (ROOT / "src/hosts/dlw_managed_host.c").read_text(encoding="utf-8")
    # Execute the production guards up to their first dispatch-side effect.
    native_guard = native.split("static void dluma_native_dispatch_queued_event(\n", 1)[1]
    native_guard = native_guard.split("    entry->lifecycle_phase =", 1)[0]
    managed_guard = managed.rsplit("static void dluma_managed_dispatch_event(\n", 1)[1]
    managed_guard = managed_guard.split("    memset(&event_value", 1)[0]
    harness = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "core/dlw_mod_owner.h"
#define DLUMA_NATIVE_PHASE_READY 1
#define DLUMA_MANAGED_PHASE_READY 1
#define DLUMA_MANAGED_DIAGNOSTIC_CAPACITY 256
typedef uint32_t DWORD;
typedef int DlumaManagedEventV1;
typedef struct { uint64_t owner_id; uint32_t map_generation; } QueuedEvent;
typedef QueuedEvent DlumaNativeQueuedEvent;
typedef QueuedEvent DlumaManagedQueuedEvent;
typedef struct {
    int lifecycle_phase, scope_active;
    struct { int event; } callbacks;
    struct { int active; } snapshot;
    struct { struct { uint32_t scope; } manifest; } package;
    DlumaModOwner api_owner;
} Entry;
typedef Entry DlumaNativeModEntry;
typedef Entry DlumaManagedModEntry;
static int accepted, lock_depth;
static volatile int g_dluma_native_map_exit_frozen, g_dluma_native_project_exit_gate;
static volatile int g_dluma_managed_map_exit_frozen, g_dluma_managed_project_exit_gate;
static void dluma_native_lock(void) { ++lock_depth; }
static void dluma_native_unlock(void) { --lock_depth; }
static void dluma_native_dispatch_queued_event(
""" + native_guard + r"""
    ++accepted;
    dluma_native_unlock();
}
static void dluma_managed_dispatch_event(
""" + managed_guard + r"""
    ++accepted;
}
static void check_dispatch(void (*dispatch)(Entry*, const QueuedEvent*)) {
    Entry entry;
    QueuedEvent event;
    memset(&entry, 0, sizeof(entry));
    memset(&event, 0, sizeof(event));
    entry.lifecycle_phase = 1;
    entry.scope_active = entry.snapshot.active = entry.callbacks.event = 1;
    entry.api_owner.state = DLUMA_MOD_OWNER_ACTIVE;
    entry.package.manifest.scope = entry.api_owner.scope = DLUMA_SCOPE_GLOBAL;
    entry.api_owner.owner_id = event.owner_id = 19;
    entry.api_owner.map_generation = event.map_generation = 7;
    accepted = 0;
    dispatch(&entry, &event);
    assert(accepted == 1 && lock_depth == 0);
    /* The mod stays active across a map switch while the event was queued. */
    dluma_mod_owner_advance_scope(&entry.api_owner, 8);
    dispatch(&entry, &event);
    assert(accepted == 1 && lock_depth == 0);
    event.map_generation = 8;
    dispatch(&entry, &event);
    assert(accepted == 2 && lock_depth == 0);
    event.owner_id = 20;
    dispatch(&entry, &event);
    assert(accepted == 2 && lock_depth == 0);
    event.owner_id = 19;
    dluma_mod_owner_begin_revoke(&entry.api_owner);
    dispatch(&entry, &event);
    assert(accepted == 2 && lock_depth == 0);
    dispatch(0, &event);
    dispatch(&entry, 0);
    assert(accepted == 2 && lock_depth == 0);
    entry.api_owner.state = DLUMA_MOD_OWNER_ACTIVE;
    entry.scope_active = entry.snapshot.active = 0;
    dispatch(&entry, &event);
    assert(accepted == 2 && lock_depth == 0);
}
static void check_exit_gates(
    void (*dispatch)(Entry*, const QueuedEvent*),
    volatile int* frozen, volatile int* project_gate) {
    const uint32_t scopes[] = { DLUMA_SCOPE_GLOBAL, DLUMA_SCOPE_PROJECT, DLUMA_SCOPE_MAP };
    unsigned int index;
    for (index = 0; index < sizeof(scopes) / sizeof(scopes[0]); ++index) {
        Entry entry;
        QueuedEvent event;
        memset(&entry, 0, sizeof(entry));
        memset(&event, 0, sizeof(event));
        entry.lifecycle_phase = 1;
        entry.scope_active = entry.snapshot.active = entry.callbacks.event = 1;
        entry.api_owner.state = DLUMA_MOD_OWNER_ACTIVE;
        entry.package.manifest.scope = entry.api_owner.scope = scopes[index];
        entry.api_owner.owner_id = event.owner_id = 19;
        entry.api_owner.map_generation = event.map_generation = 7;
        accepted = 0;
        *frozen = *project_gate = 0;
        dispatch(&entry, &event);
        assert(accepted == 1 && lock_depth == 0);
        /* Begin freezes every owner, including global owners. */
        *frozen = 1;
        dispatch(&entry, &event);
        assert(accepted == 1 && lock_depth == 0);
        *project_gate = 1;
        dispatch(&entry, &event);
        assert(accepted == 1 && lock_depth == 0);
        /* After the freeze, the project gate still rejects outgoing scopes. */
        *frozen = 0;
        dispatch(&entry, &event);
        assert(accepted == 1 + (scopes[index] == DLUMA_SCOPE_GLOBAL) && lock_depth == 0);
        *project_gate = 0;
        dispatch(&entry, &event);
        assert(accepted == 2 + (scopes[index] == DLUMA_SCOPE_GLOBAL) && lock_depth == 0);
    }
}
int main(void) {
    check_dispatch(dluma_native_dispatch_queued_event);
    check_dispatch(dluma_managed_dispatch_event);
    check_exit_gates(dluma_native_dispatch_queued_event,
        &g_dluma_native_map_exit_frozen, &g_dluma_native_project_exit_gate);
    check_exit_gates(dluma_managed_dispatch_event,
        &g_dluma_managed_map_exit_frozen, &g_dluma_managed_project_exit_gate);
    return 0;
}
"""
    source = tmp_path / "event_generation.c"
    executable = tmp_path / "event_generation.exe"
    source.write_text(harness, encoding="utf-8")
    command = (
        'cmd.exe /d /s /c "call tools\\setup_msvc.bat && '
        f'cl /nologo /W0 /MT /I src /I sdk/c "{source}" '
        f'src/core/dlw_mod_owner.c /Fo"{tmp_path}\\\\" /Fe"{executable}"'
        '"'
    )
    compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    subprocess.run([str(executable)], check=True, timeout=10)


@pytest.mark.parametrize("stem,state,frozen", [
    ("dluma_native_mods", "g_dluma_native_mods", "g_dluma_native_map_exit_frozen"),
    ("hosts/dluma_managed_host", "g_dluma_managed", "g_dluma_managed_map_exit_frozen"),
])
def test_mod_frame_preparation_tracks_authority_and_readiness(tmp_path, stem, state, frozen):
    name = stem.split("/")[-1]
    text = (ROOT / "src" / (stem + ".c")).read_text(encoding="utf-8")
    preparation = text.split(f"void {name}_prepare_game_frame(void)", 1)[1]
    preparation = preparation.split(f"void {name}_game_thread_pump(", 1)[0]
    harness = r"""
#include <assert.h>
#include <stdint.h>
#include "core/dlw_mod_owner.h"
#define DLUMA_NATIVE_PHASE_READY 1
#define DLUMA_MANAGED_PHASE_READY 1
#define DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE 1
typedef struct { int world, host; } Registry;
typedef struct {
    int lifecycle_phase;
    DlumaModOwner api_owner;
    Registry api_capabilities;
    int api_host;
} Entry;
typedef Entry DlumaNativeModEntry;
typedef Entry DlumaManagedModEntry;
static struct { int lock, count; Entry entries[3]; } REGISTRY;
static volatile int MAP_EXIT_FROZEN;
static struct { int engine; } g_dluma;
static int route, authority, lock_depth, ready;
static uint32_t current_thread;
static void dluma_native_lock(void) { ++lock_depth; }
static void dluma_native_unlock(void) { --lock_depth; }
static void dluma_managed_lock_value(int* lock) { (void)lock; ++lock_depth; }
static void dluma_managed_unlock_value(int* lock) { (void)lock; --lock_depth; }
static int dluma_engine_game_frame_dispatch_active_on_current_thread(void* e) { (void)e; return route; }
static int dluma_engine_game_frame_authority_status(void* e, uint64_t t) { (void)e; (void)t; return authority; }
static uint64_t GetTickCount64(void) { return 100; }
static uint32_t GetCurrentThreadId(void) { return current_thread; }
static void dluma_game_backend_prepare_capabilities(Registry* r) { r->world=ready; r->host=0; }
static void dluma_game_host_context_prepare_capabilities(int* h, Registry* r) { r->host=*h; }
void prepare(void)
""".replace("REGISTRY", state).replace("MAP_EXIT_FROZEN", frozen) + preparation + r"""
int main(void) {
    Entry* entry = &REGISTRY.entries[0];
    REGISTRY.count = 3;
    entry->lifecycle_phase = 1;
    entry->api_owner.state = DLUMA_MOD_OWNER_ACTIVE;
    entry->api_owner.scope = DLUMA_SCOPE_MAP;
    entry->api_owner.game_thread_id = 11;
    entry->api_host = 9;
    REGISTRY.entries[1] = *entry;
    REGISTRY.entries[1].api_owner.state = DLUMA_MOD_OWNER_REVOKED;
    REGISTRY.entries[2] = *entry;
    REGISTRY.entries[2].lifecycle_phase = 2;
    current_thread = 22; route = 0; authority = 1; ready = 1;
    prepare();
    assert(entry->api_owner.game_thread_id == 11 && !entry->api_capabilities.world);
    route = 1; authority = 0; prepare();
    assert(entry->api_owner.game_thread_id == 11 && !entry->api_capabilities.world);
    authority = 1; prepare();
    assert(entry->api_owner.game_thread_id == 22);
    assert(entry->api_capabilities.world == 1 && entry->api_capabilities.host == 9);
    assert(dluma_mod_owner_require(&entry->api_owner, 4U, 0, 1, 11) == DLUMA_STATUS_WRONG_THREAD);
    assert(dluma_mod_owner_require(&entry->api_owner, 4U, 0, 1, 22) == DLUMA_STATUS_OK);
    ready = 0; prepare();
    assert(!entry->api_capabilities.world && entry->api_capabilities.host == 9);
    MAP_EXIT_FROZEN = 1; current_thread = 33; ready = 1; prepare();
    assert(entry->api_owner.game_thread_id == 22);
    assert(!entry->api_capabilities.world && entry->api_capabilities.host == 9);
    assert(lock_depth == 0);
    MAP_EXIT_FROZEN = 0; prepare();
    assert(entry->api_owner.game_thread_id == 33);
    assert(entry->api_capabilities.world == 1 && entry->api_capabilities.host == 9);
    assert(REGISTRY.entries[1].api_owner.game_thread_id == 11);
    assert(REGISTRY.entries[2].api_owner.game_thread_id == 11);
    assert(!REGISTRY.entries[1].api_capabilities.world);
    assert(!REGISTRY.entries[2].api_capabilities.world);
    assert(lock_depth == 0);
    return 0;
}
""".replace("REGISTRY", state).replace("MAP_EXIT_FROZEN", frozen)
    source = tmp_path / "frame_preparation.c"
    executable = tmp_path / "frame_preparation.exe"
    source.write_text(harness, encoding="utf-8")
    command = (
        'cmd.exe /d /s /c "call tools\\setup_msvc.bat && '
        f'cl /nologo /W0 /MT /I src /I sdk/c "{source}" '
        f'src/core/dlw_mod_owner.c /Fo"{tmp_path}\\\\" /Fe"{executable}"'
        '"'
    )
    compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    subprocess.run([str(executable)], check=True, timeout=10)


def test_frame_preparation_precedes_scope_and_quest_callbacks():
    source = (ROOT / "src/dlw_lua_runtime.c").read_text(encoding="utf-8")
    pump = source.split("void dluma_lua_runtime_game_thread_pump(void)", 1)[1]
    transition = pump.index("dluma_lua_runtime_take_pending();")
    second_tick = pump.index("dluma_native_gameplay_game_thread_tick();", transition)
    quest_callbacks = pump.index("dluma_quest_phase_player_game_thread_pump();")
    for name in ("dluma_native_mods", "dluma_managed_host"):
        call = f"{name}_prepare_game_frame();"
        first = pump.index(call)
        second = pump.index(call, first + len(call))
        assert first < transition < second_tick < second < quest_callbacks
        assert quest_callbacks < pump.index(f"{name}_game_thread_pump(delta);")
