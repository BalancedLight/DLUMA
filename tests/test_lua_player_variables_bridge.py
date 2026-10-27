from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "src" / "dlw_lua54_player_bridge.c"
BINDING = ROOT / "src" / "bindings" / "dlw_player_variables_binding.c"
SERVICE = ROOT / "src" / "game" / "dlw_player_service.c"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_bridge_is_exact_build_and_pointer_free_at_lua_boundary():
    source = text(BRIDGE)
    binding = text(BINDING)
    for token in (
        "0x623395D0UL",
        "0x00E8D000UL",
        "0x623397AAUL",
        "0x02082000UL",
        "DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA 0x01D07698UL",
        "DLUMA_PLAYER_STATE_VARIABLES_OFFSET 0x00D0UL",
        "DLUMA_PLAYER_FLOAT_VARIABLE_VTABLE_RVA 0x017EB5F0UL",
    ):
        assert token in binding
    assert "GetModuleHandleW" not in source
    assert "DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA" not in source
    assert "context->host->api_invoke" in source
    assert "DlumaObjectHandle handle;" in source
    assert "lua_pushlightuserdata(state, player_state)" not in source
    assert "lua_pushlightuserdata(state, variables)" not in source
    assert "lua_pushlightuserdata(state, variable)" not in source
    assert "const DlumaLua54HostCallbacks* host;" in text(
        ROOT / "src" / "dlw_lua54_player_bridge.h"
    )


def test_only_named_allowlisted_variables_are_exposed():
    source = text(BINDING)
    expected = {
        "low_health_effect_threshold",
        "stamina_warning_level",
        "max_stamina",
        "move_sprint_speed",
        "max_health",
        "base_max_health",
        "health_critical_level",
        "health_regeneration_delay",
        "health_regeneration_max_level",
        "health_regeneration_time_to_max",
        "camera_default_fov",
        "camera_car_fov",
        "fov_sprint_modifier",
        "fov_amortizer_modifier",
        "kick_stamina_usage",
    }
    for name in expected:
        assert f'"{name}"' in source
    assert "dluma_player_variables_binding_definition_named" in source
    assert "dluma_player_string_equal" in source
    assert "lua_State" not in source


def test_writes_are_scoped_bounded_and_reversible():
    bridge = text(BRIDGE)
    service = text(SERVICE)
    binding = text(BINDING)
    assert "context->map_generation" in bridge
    assert "handle->handle.owner_id != handle->context->owner_id" in bridge
    assert "DLUMA_PLAYER_SERVICE_MAX_OVERRIDES_PER_OWNER" in service
    assert "request->value < definition->minimum" in service
    assert "request->value > definition->maximum" in service
    assert "dluma_player_service_reset_definition_locked" in service
    assert "dluma_player_service_revoke_owner" in service
    assert "dluma_player_service_apply_winner_locked" in service
    assert "DLUMA_PLAYER_GET_FVALUE_RVA 0x00BD2100UL" in binding
    assert "get_fvalue_prefix" in binding
    assert "snapshot->value = g_dluma_player_get_fvalue" in binding
    assert "variable_table[definition->engine_id]" in binding
    assert "dluma_player_variables_binding_write_base" in binding
    assert "lua_State" not in service


def test_binding_rejects_nonfinite_derived_value_without_mutation():
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_player_variables_binding_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert "player variables transactional write: ok" in completed.stdout




def test_watch_api_uses_existing_bounded_timer_service():
    source = text(BRIDGE)
    assert 'lua_getfield(state, -1, "every")' in source
    assert "watch interval must be 0.05..60 seconds" in source
    for field in (
        '"previous"',
        '"value"',
        '"minimum"',
        '"maximum"',
        '"overridden"',
        '"map_generation"',
    ):
        assert field in source


def test_helper_builds_compile_the_bridge_and_rename_lua_setglobal():
    for relative in (
        "tools/build_lua54_helper_clang.bat",
        "tools/build_lua54_helper_msvc.bat",
    ):
        source = text(ROOT / relative)
        assert "/Dlua_setglobal=dluma_real_lua_setglobal" in source
        assert '"src\\dlw_lua54_player_bridge.c"' in source
        assert "dluma_lua54_player_bridge.obj" in source


def test_clang_builds_support_debug_without_staging_release_artifacts():
    main = text(ROOT / "build_windows_clang.bat")
    helper = text(ROOT / "tools" / "build_lua54_helper_clang.bat")
    for source, configuration in ((main, "DLUMA_CLANG_CONFIG"),
                                  (helper, "DLUMA_LUA_CONFIG")):
        assert 'set "' + configuration + '=%~1"' in source
        assert '"Debug"' in source
        assert "/MTd /Od /Zi" in source
        assert 'set "' + configuration + '=Release"' in source
    assert 'if /I "%DLUMA_CLANG_CONFIG%"=="Release" (' in main


def test_helper_registers_and_releases_the_player_bridge_explicitly():
    helper = text(ROOT / "src" / "dlw_lua54_helper.c")
    bridge = text(BRIDGE)

    build = helper[
        helper.index("static int dluma_helper_build_api") :
        helper.index("static void dluma_helper_service_timers")
    ]
    destroy = helper[
        helper.index("static int dluma_helper_destroy_script_impl") :
        helper.index("static int dluma_helper_destroy_script(void*")
    ]

    assert '#include "dlw_lua54_player_bridge.h"' in helper
    assert "state, &script->player_bridge, root);" in build
    assert build.index("dluma_lua54_player_bridge_register") < build.index(
        'lua_setglobal(state, "dluma")'
    )
    assert "script->state, &script->player_bridge);" in destroy
    assert destroy.index("dluma_lua54_player_bridge_teardown") < destroy.index(
        "lua_close(script->state);"
    )
    assert "dluma_player_track_handle" in bridge
    assert "dluma_player_invalidate_handles" in bridge
    assert '"stale_handle"' in bridge
    assert 'luaL_error(state, "PlayerVariables handle is no longer valid")' not in bridge
    assert "GetModuleHandleW" not in bridge
    assert "dluma_player_variables_binding" not in bridge


def test_main_loader_builds_exclude_lua_helper_only_translation_units():
    msvc = text(ROOT / "build_windows_msvc.bat")
    project = text(ROOT / "DLUMA.vcxproj")
    assert "msbuild.exe DLUMA.vcxproj" in msvc
    assert "*.obj" not in msvc
    for unit in ("dlw_lua54_player_bridge.c", "dlw_lua54_stdlib.c"):
        assert f'<ClCompile Include="src\\{unit}"' not in project
    source = text(ROOT / "build_windows_clang.bat")
    for unit in ("dlw_lua54_player_bridge.c", "dlw_lua54_stdlib.c"):
        assert unit not in source
    assert "list_vcxproj_sources.py" in source
    assert "verify_object_set.py" in source
    assert "*.obj" not in source


def test_docs_cover_read_write_watch_and_restore():
    docs = text(ROOT / "docs/api/1.0/API_REFERENCE.md")
    for token in (
        "dluma.player.local_player",
        "player:set_variable",
        "player:reset_variable",
    ):
        assert token in docs
