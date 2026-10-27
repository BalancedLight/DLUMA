"""Compile the actual retirement functions without unrelated native loaders.

The generated headers are build outputs: structs and function bodies are copied
verbatim from production source, so the harness cannot silently test a second
implementation. No game binaries, project assets or machine paths are fixtures.
"""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/project-host-exit-fake'

def function(source, name):
    matches = list(re.finditer(r'^[\w* \t]+\b'+re.escape(name)+r'\s*\(', source, re.M))
    for match in matches:
        brace = source.find('{', match.end())
        semicolon = source.find(';', match.end())
        if brace < 0 or 0 <= semicolon < brace: continue
        depth = 0
        for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[{}]', source[brace:]):
            value = token[0]
            if value not in {'{','}'}: continue
            depth += 1 if value == '{' else -1
            if depth == 0: return source[match.start():brace+token.end()]
    raise ValueError('Function definition missing: '+name)

OUT.mkdir(parents=True, exist_ok=True)
for host in ['native','managed']:
    if host == 'native':
        path = ROOT/'src/dlw_native_mods.c'
        text = path.read_text()
        types = text[text.index('typedef const DlwNativeModApiV2*'):text.index('static DlwNativeRegistry')]
        prefix = '#include "dlw_native_mods.h"\n#define DLW_NATIVE_MOD_MAX_EVENTS 256U\n'
        globals_ = 'static DlwNativeRegistry g_dlw_native_mods;\nstatic volatile LONG g_dlw_native_project_exit_gate;\nstatic volatile LONG g_dlw_native_map_exit_frozen;\nstatic unsigned int g_dlw_native_map_exit_generation;\n'
        names = ['dlw_native_unlock','dlw_native_scope_string','dlw_native_set_error',
                 'dlw_native_mods_revoke_project_for_exit','dlw_native_mods_revoke_project_for_exit_ex',
                 'dlw_native_mods_freeze_map_exit','dlw_native_mods_suspend_global_map',
                 'dlw_native_mods_bind_frontend_after_exit','dlw_native_mods_resume_frontend_after_exit']
    else:
        path = ROOT/'src/hosts/dlw_managed_host.c'
        text = path.read_text()
        types = text[text.index('typedef struct DlwManagedStringView'):text.index('static DlwManagedRegistry')]
        prefix = '#include "hosts/dlw_managed_host.h"\n#define DLW_MANAGED_MAX_EVENTS 256U\n'
        globals_ = 'static DlwManagedRegistry g_dlw_managed;\nstatic volatile LONG g_dlw_managed_project_exit_gate;\nstatic volatile LONG g_dlw_managed_map_exit_frozen;\nstatic unsigned int g_dlw_managed_map_exit_generation;\n'
        names = ['dlw_managed_unlock_value','dlw_managed_set_error',
                 'dlw_managed_host_revoke_project_for_exit','dlw_managed_host_revoke_project_for_exit_ex',
                 'dlw_managed_host_freeze_map_exit','dlw_managed_host_suspend_global_map',
                 'dlw_managed_host_bind_frontend_after_exit','dlw_managed_host_resume_frontend_after_exit']
    includes = '#include "core/dlw_api_runtime.h"\n#include "game/dlw_game_backend.h"\n'
    bodies = '\n\n'.join(function(text, name) for name in names)
    output = f'/* Production source SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()} */\n'+prefix+includes+types+globals_+bodies+'\n'
    (OUT/f'project_exit_{host}.generated.h').write_text(output)

path=ROOT/'src/dlw_lua_runtime.c'
text=path.read_text()
types=text[text.index('#define DLW_LUA_RUNTIME_MAX_STATES'):text.index('static DlwLuaRuntimeState')]
prefix='#include "dlw_lua_project_exit.h"\n#include "dlw_project_map_exit.h"\n#include "dlw_native_mods.h"\n#include "hosts/dlw_managed_host.h"\n#include "dlw_quest_phase_player.h"\n#include "dlw_native_gameplay.h"\n#include "dlw_lua54_api.h"\n#include "dlw_quest_hud.h"\n#include "core/dlw_api_runtime.h"\n#include "game/dlw_game_backend.h"\n'
globals_='static DlwLuaRuntimeState g_dlw_lua_runtime;\nstatic unsigned int g_dlw_lua_project_exit_gate;\nstatic volatile LONG g_dlw_map_exit_active,g_dlw_map_exit_lock;\nstatic DlwProjectMapExitReceipt g_dlw_map_exit_receipt;\nstatic DlwLuaRuntimeSlot* g_dlw_exit_notification_slot;\nstatic uint32_t g_dlw_exit_notification_kind;\n'
prototypes='static int dlw_lua_runtime_dispatch_slot(DlwLuaRuntimeSlot*,uint32_t,const char*,double,int);\nstatic void dlw_lua_runtime_clear_quest_snapshots_for_owner(uint64_t);\n'
names=['dlw_lua_runtime_unlock','dlw_lua_runtime_destroy_slot','dlw_lua_runtime_current_map_generation',
       'dlw_lua_runtime_revoke_local_scopes_for_exit','dlw_lua_runtime_revoke_local_scopes_for_exit_ex',
       'dlw_map_exit_key_equal','dlw_project_runtime_snapshot_outgoing_map',
       'dlw_project_runtime_begin_outgoing_map','dlw_project_runtime_finish_outgoing_map','dlw_project_runtime_resume_frontend']
output=f'/* Production source SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()} */\n'+prefix+types+globals_+prototypes+'\n\n'.join(function(text,n) for n in names)+'\n'
(OUT/'project_exit_lua.generated.h').write_text(output)

path=ROOT/'src/game/dlw_game_host_context.c';text=path.read_text()
types=text[text.index('#define DLW_GAME_HOST_CONTEXT_VERSION'):text.index('static volatile LONG g_dlw_game_host_token_generation')]
prefix='#include "game/dlw_game_host_context.h"\n#include "game/dlw_media_service.h"\n#include "game/dlw_player_service.h"\n#include "game/dlw_quest_hud_service.h"\n#include "dlw_native_gameplay.h"\n'
prototypes='static void dlw_game_host_quest_hud_clear(DlwGameHostState*);\n'
names=['dlw_game_host_state','dlw_game_host_context_suspend_map_for_exit',
       'dlw_game_host_context_bind_clean_frontend','dlw_game_host_context_needs_map_rebind','dlw_game_host_context_rebind_map']
output=f'/* Production source SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()} */\n'+prefix+types+prototypes+'\n\n'.join(function(text,n) for n in names)+'\n'
(OUT/'project_exit_context.generated.h').write_text(output)
