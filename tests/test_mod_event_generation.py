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
