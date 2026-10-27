import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_editor_model_animation_guard_fake_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_editor_model_animation_guard_fake_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.stdout.count("[PASS]") == 10
    assert "Editor animation guard fake cases passed: 10" in completed.stdout


def test_editor_model_animation_fix_scoped_debug_and_release_compile():
    for configuration in ("Debug", "Release"):
        completed = subprocess.run(
            [
                "cmd.exe",
                "/d",
                "/s",
                "/c",
                "call tools\\build_editor_model_animation_fix_compile_check.bat "
                + configuration,
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            timeout=120,
        )
        assert (
            f"Editor animation fix scoped {configuration} compile passed."
            in completed.stdout
        )


def test_exact_editor_and_player_animation_bind_profiles_and_patch_contract():
    source = (ROOT / "src/dlw_editor_model_animation_fix.c").read_text(
        encoding="utf-8"
    )
    core = (ROOT / "src/core/dlw_editor_model_animation_guard.c").read_text(
        encoding="utf-8"
    )
    header = (
        ROOT / "src/core/dlw_editor_model_animation_guard.h"
    ).read_text(encoding="utf-8")

    assert "0x623396E1UL" in source
    assert "0x01E4A000UL" in source
    assert "CACD9C96BA5D8C35A23170DC8BE8029D" in source
    assert "0x623397AAUL" in source
    assert "0x02082000UL" in source
    assert "8E19C89C316E319E1E88D0C7A6F6A93" in source
    assert "0x623395D0UL" in source
    assert "0x00E8D000UL" in source
    assert "5B8090BFC3D3A25A24DA1846A3AAFD6" in source
    assert "0x623397B8UL" in source
    assert "0x00103000UL" in source
    assert "61D9DFB20CFA179C6D097537DA46BA9D" in source
    assert "CBC7DD7923778936341CDFB755474A0C7" in source
    assert "A18CA1103614947AE7FF956C6852F451" in source
    assert "DLUMA_EDITOR_MODEL_ANIMATION_HELPER_RVA 0x0075C9E0UL" in source
    assert "DLUMA_EDITOR_MODEL_ANIMATION_FAULT_RVA 0x0075CA16UL" in source
    assert "DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_IAT_RVA 0x013CCC20UL" in source
    assert "DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_ENGINE_RVA 0x0015B390UL" in source
    assert "0x40U, 0x53U, 0x48U, 0x81U, 0xECU, 0x90U" in source
    assert "0x4CU, 0x8BU, 0x08U" in source
    assert "DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE 16U" in source
    assert "DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE 14U" in header
    assert "GetModuleHandleW(0), &g_dluma_editor_identity" in source
    assert "GetModuleHandleW(0), &g_dluma_player_identity" in source
    assert "DLUMA_HOST_PLAYER_STANDALONE" in source
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in source
    assert "DyingLightGame.exe" not in source
    assert source.count("dluma_editor_model_animation_validate_module(") >= 4
    assert "output[0] = 0xFFU" in core
    assert "output[1] = 0x25U" in core


def test_trampoline_wx_and_ownership_aware_restore_contract():
    source = (ROOT / "src/dlw_editor_model_animation_fix.c").read_text(
        encoding="utf-8"
    )
    install = source.split(
        "static int dluma_editor_model_animation_install_detour", 1
    )[1].split(
        "static int dluma_editor_model_animation_validate_targets", 1
    )[0]
    restore = source.split(
        "int dluma_editor_model_animation_fix_restore(void)", 1
    )[1].split(
        "void dluma_editor_model_animation_fix_retain(void)", 1
    )[0]

    assert install.index("PAGE_READWRITE") < install.index("PAGE_EXECUTE_READ")
    assert "FlushInstructionCache" in install
    assert "PAGE_EXECUTE_READWRITE" in install
    assert "memcmp(target, g_dluma_editor_model_animation_fix.patch" in install
    assert "memcmp(g_dluma_editor_model_animation_fix.target" in restore
    assert "g_dluma_editor_model_animation_fix.patch" in restore
    assert restore.index("dluma_editor_model_animation_wait_active()") < restore.index(
        "VirtualProtect("
    )
    assert restore.index("VirtualProtect(") < restore.index(
        "dluma_editor_model_animation_free_trampoline()"
    )
    assert "dluma_editor_model_animation_mark_retained" in restore
    assert "GET_MODULE_HANDLE_EX_FLAG_PIN" in source
    assert "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS" in source


def test_fix_is_wired_to_editor_and_exact_player_without_ui_or_game_registration():
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    editor_initialize = exports.split("static void* dluma_editor_initialize", 1)[
        1
    ].split("static __int64 dluma_editor_shutdown", 1)[0]
    editor_shutdown = exports.split("static __int64 dluma_editor_shutdown", 1)[
        1
    ].split("static void* dluma_player_initialize", 1)[0]
    forwarding = exports.split("static void* dluma_forwarding_only_initialize", 1)[
        1
    ].split("static int dluma_editor_wait_callbacks_drained", 1)[0]
    player_initialize = exports.split("static void* dluma_player_initialize", 1)[
        1
    ].split("static __int64 dluma_player_shutdown", 1)[0]
    player_shutdown = exports.split("static __int64 dluma_player_shutdown", 1)[
        1
    ].split('extern "C" __declspec(dllexport) void*', 1)[0]

    assert "dluma_editor_model_animation_fix_install(" in editor_initialize
    assert editor_initialize.index(
        "dluma_dual_host_lifecycle_original_loaded"
    ) < editor_initialize.index("dluma_editor_model_animation_fix_install(")
    assert editor_initialize.index(
        "dluma_editor_model_animation_fix_install("
    ) < editor_initialize.index("g_original_initialize()")
    assert editor_shutdown.index("g_original_shutdown()") < editor_shutdown.index(
        "dluma_editor_model_animation_fix_restore()"
    )
    assert editor_shutdown.index(
        "dluma_editor_model_animation_fix_restore()"
    ) < editor_shutdown.index("dluma_release_original_game_script()")
    assert "dluma_editor_model_animation_fix_" not in forwarding
    assert "dluma_editor_model_animation_fix_install(" in player_initialize
    assert player_initialize.index(
        "dluma_resolve_original_game_script()"
    ) < player_initialize.index("dluma_editor_model_animation_fix_install(")
    assert player_initialize.index(
        "dluma_editor_model_animation_fix_install("
    ) < player_initialize.index("g_original_initialize()")
    assert player_shutdown.index(
        "dluma_editor_model_animation_fix_quiesce()"
    ) < player_shutdown.index("g_original_shutdown()")
    assert player_shutdown.index("g_original_shutdown()") < player_shutdown.index(
        "dluma_editor_model_animation_fix_restore()"
    )
    assert player_shutdown.index(
        "dluma_editor_model_animation_fix_restore()"
    ) < player_shutdown.index("dluma_crash_reporting_stop()")
    assert "src\\dlw_editor_model_animation_fix.c" in project
    assert "src\\core\\dlw_editor_model_animation_guard.c" in project

    for relative_path in (
        "src/fixes/FixRegistry.cpp",
        "src/dlw_ui.c",
        "src/dlw_localization.c",
        "src/dlw_lua_config.c",
    ):
        unrelated = (ROOT / relative_path).read_text(encoding="utf-8").lower()
        assert "editor_model_animation" not in unrelated
        assert "animation_bind_guard" not in unrelated
