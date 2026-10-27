import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_animation_refresh_runtime_security_native_harness():
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_animation_refresh_runtime_security_tests.bat",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert completed.stdout.count("[PASS]") == 10
    assert "Animation refresh runtime security cases passed: 10" in completed.stdout


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def test_result_publication_is_contained_atomic_and_non_replacing():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    body = _function_body(runtime, "dluma_animation_refresh_write_result(")

    create = body.index("CREATE_NEW")
    first_containment = body.index("dluma_project_regular_file_is_contained", create)
    write = body.index("WriteFile", first_containment)
    flush = body.index("FlushFileBuffers", write)
    second_containment = body.index("dluma_project_regular_file_is_contained", flush)
    move = body.index("MoveFileExW", second_containment)
    assert create < first_containment < write < flush < second_containment < move
    assert "FILE_FLAG_OPEN_REPARSE_POINT" in body
    assert "MOVEFILE_WRITE_THROUGH" in body
    assert "MOVEFILE_REPLACE_EXISTING" not in body
    assert "DeleteFileW(temporary)" in body
    assert "move_error == 183UL || move_error == 80UL" in body


def test_runtime_reads_refuse_reparse_and_multi_link_files_before_hashing():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    read_body = _function_body(runtime, "dluma_animation_refresh_read_owned(")
    hash_body = _function_body(runtime, "dluma_animation_refresh_hash_owned(")
    for body in (read_body, hash_body):
        assert "dluma_animation_refresh_no_reparse_components" in body
        assert "GetFinalPathNameByHandleW" in body
        assert "dluma_final_path_equal_or_under" in body
        assert "before.nNumberOfLinks != 1U" in body
        assert "FILE_ATTRIBUTE_REPARSE_POINT" in body
        assert "dluma_animation_refresh_info_equal(&before, &after)" in body


def test_role_and_aggregate_bounds_are_enforced_before_streaming_hash():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    assert re.search(
        r"DLUMA_ANIMATION_REFRESH_ALIAS_MAX_BYTES\s+\(1ULL\s*\*\s*1024ULL\s*\*\s*1024ULL\)",
        runtime,
    )
    assert "DLUMA_ANIMATION_REFRESH_SCRIPT_MAX_BYTES (16ULL * 1024ULL * 1024ULL)" in runtime
    assert "DLUMA_ANIMATION_REFRESH_ANIMATION_MAX_BYTES (256ULL * 1024ULL * 1024ULL)" in runtime
    assert "DLUMA_ANIMATION_REFRESH_RPACK_MAX_BYTES (1024ULL * 1024ULL * 1024ULL)" in runtime
    assert "DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES" in runtime
    process = _function_body(runtime, "dluma_animation_refresh_process_request(")
    aggregate_check = process.index(
        "aggregate_size >=\n            DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES"
    )
    remaining = process.index("remaining =", aggregate_check)
    clamp = process.index("if (artifact_maximum > remaining)", remaining)
    owned_hash = process.index("dluma_animation_refresh_hash_owned(", clamp)
    add = process.index("aggregate_size += artifact_size", owned_hash)
    assert aggregate_check < remaining < clamp < owned_hash < add


def test_editor_refresh_polling_is_wired_for_startup_and_each_session_begin():
    editor = (ROOT / "src/dlw_editor_host.c").read_text(encoding="utf-8")
    header = '#include "dlw_animation_refresh_runtime.h"'
    assert editor.count(header) == 1
    assert editor.count("dluma_animation_refresh_runtime_poll_project(") == 3

    watcher = _function_body(
        editor, "static DWORD WINAPI dluma_editor_animation_refresh_worker("
    )
    startup = _function_body(editor, "int dluma_editor_host_start(")
    session = _function_body(editor, "int dluma_editor_host_session_begin(")
    assert "WaitForSingleObject(stop_event, 500U)" in watcher
    assert "dluma_editor_launch_resolve_current_project" in watcher
    assert "dluma_animation_refresh_runtime_poll_project" in watcher
    assert "dluma_editor_animation_refresh_watcher_start" in session
    assert "dluma_editor_animation_refresh_watcher_stop" in editor
    assert "dluma_editor_launch_resolve_current_project" in startup
    assert "dluma_animation_refresh_runtime_poll_project" in startup
    assert "dluma_editor_launch_resolve_current_project" in session
    assert "dluma_animation_refresh_runtime_poll_project" in session


def test_runtime_is_enabled_and_exact_host_gated():
    config = (ROOT / "config/dluma.ini").read_text(
        encoding="utf-8"
    )
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    assert re.search(
        r"\[AnimationRefresh\][\s\S]*?\nEnabled=1\n[\s\S]*?\nRoute=ProjectRPack\n",
        config,
    )
    host = _function_body(runtime, "dluma_animation_refresh_actual_host(")
    assert "classification.forwarding_only" in host
    assert "classification.exact_profile" in host
    assert "evidence.canonical_image_verified" in host
    assert "DLUMA_HOST_CHROME_EDITOR" in host
    assert "DLUMA_HOST_PLAYER_STANDALONE" in host
    assert "DLUMA_HOST_PLAYER_EDITOR_PLAYTEST" in host
    assert "DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED" in host
    assert "DyingLightGame.exe" not in runtime


def test_only_accepted_persisted_results_publish_coalesce_marker():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    process = _function_body(
        runtime, "static void dluma_animation_refresh_process_request("
    )
    predicate = _function_body(
        runtime,
        "static int dluma_animation_refresh_should_write_coalesce_marker(",
    )
    result = process.index(
        "result_written = dluma_animation_refresh_write_result("
    )
    accepted = process.index(
        "dluma_animation_refresh_should_write_coalesce_marker(", result
    )
    marker = process.index(
        "dluma_animation_refresh_write_coalesce_marker(", accepted
    )
    assert result < accepted < marker
    assert "result_written && result_published &&" in process[result:accepted]
    assert "result->accepted != 0U" in predicate


def test_player_runtime_pack_swap_is_marshaled_to_game_thread_pump():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    lua_runtime = (ROOT / "src/dlw_lua_runtime.c").read_text(
        encoding="utf-8"
    )
    callback = _function_body(
        runtime, "static int dluma_animation_refresh_player_runtime_pack("
    )
    dispatch = _function_body(
        runtime, "static int dluma_animation_refresh_player_dispatch("
    )
    pump = _function_body(
        runtime,
        "void dluma_animation_refresh_runtime_player_game_thread_pump(",
    )
    game_thread = _function_body(
        lua_runtime, "void dluma_lua_runtime_game_thread_pump("
    )
    assert "dluma_animation_pack_runtime_swap" not in callback
    assert "dluma_animation_refresh_player_dispatch(" in callback
    assert "DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_TIMEOUT_MS" in callback
    assert "Sleep(1)" in dispatch
    assert "DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED" in dispatch
    assert "DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED" in dispatch
    assert "dluma_animation_pack_runtime_swap(runtime_pack, &swap)" in pump
    assert "DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED" in pump
    assert "dluma_animation_refresh_runtime_player_game_thread_pump();" in game_thread


def test_player_pack_cleanup_is_owner_thread_only_and_precedes_teardown():
    runtime = (ROOT / "src/dlw_animation_refresh_runtime.c").read_text(
        encoding="utf-8"
    )
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    cleanup = _function_body(
        runtime,
        "int dluma_animation_refresh_runtime_player_game_thread_shutdown(",
    )
    player_shutdown = _function_body(exports, "static __int64 dluma_player_shutdown(")
    owner_check = cleanup.index("owner != (LONG)GetCurrentThreadId()")
    free_pack = cleanup.index("dluma_animation_pack_runtime_shutdown()")
    cleanup_call = player_shutdown.index(
        "dluma_animation_refresh_runtime_player_game_thread_shutdown()"
    )
    stop = player_shutdown.index(
        "dluma_atomic_exchange(&g_dluma.stop_requested, 1)"
    )
    original = player_shutdown.index("g_original_shutdown()")
    assert owner_check < free_pack
    assert "DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE" in cleanup
    assert cleanup_call < stop < original
