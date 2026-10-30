from __future__ import annotations

import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def block(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin : text.index(end, begin)]


def test_crash_core_native_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe",
            "/d",
            "/s",
            "/c",
            "call tools\\build_crash_core_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.returncode == 0, (
        f"Crash harness exited {completed.returncode}\n"
        f"{completed.stdout}{completed.stderr}"
    )
    assert "crash core fake harness passed" in completed.stdout
    assert "crash worker constrained-stack harness passed" in completed.stdout


def test_report_schema_is_bounded_and_detailed() -> None:
    crash = read("src/dlw_crash.c")
    for contract in (
        "#define DLUMA_CRASH_MAX_REPORT_BYTES (2UL * 1024UL * 1024UL)",
        "#define DLUMA_CRASH_MAX_MODULES 256U",
        "#define DLUMA_CRASH_MAX_FRAMES 64U",
        "#define DLUMA_CRASH_MAX_STACK_WORDS 64U",
        "#define DLUMA_CRASH_MAX_EXCEPTION_RECORDS 8U",
        '#define DLUMA_CRASH_REPORT_PREFIX L"dluma_crash_session_"',
        '"report_begin", "DLW_CRASH_REPORT_V1"',
        '"report_end", "DLW_CRASH_REPORT_V1"',
        "report_truncated=1 limit_bytes=2097152",
    ):
        assert contract in crash
    for field in (
        "time_utc",
        "time_local",
        "process_id",
        "thread_id",
        "fault_thread_id",
        "reporter_thread_id",
        "report_mode",
        "context_copy_status",
        "uptime_ms",
        "debugger_present",
        "loader_build_identity",
        "loader_git_head",
        "executable_path",
        "loader_path",
        "loader_log_path",
        "crash_info_path",
        "dump_path",
        "memory_total_physical",
        "process_kernel_100ns",
        "thread_kernel_100ns",
        "exception_name",
        "exception_code",
        "fault_classification",
        "fault_module_class",
        "fault_module_first_party",
        "fault_module_identity_validated",
        "fault_module_profile",
        "fault_location_is_not_root_cause_proof",
        "fault_module_path",
        "access_operation",
        "access_target",
        "in_page_status",
        "stack_unwind_status",
        "stack_sample_status",
        "module_snapshot_truncated",
        "stock_dump_result_available",
        "stock_callback_exception_code",
        "reporter_exception_code",
    ):
        assert f'"{field}"' in crash
    assert "exception_parameter record=" in crash
    for register in (
        "RAX",
        "RBX",
        "RCX",
        "RDX",
        "RSI",
        "RDI",
        "RBP",
        "RSP",
        "R8",
        "R9",
        "R10",
        "R11",
        "R12",
        "R13",
        "R14",
        "R15",
        "RIP",
        "DR0",
        "DR1",
        "DR2",
        "DR3",
        "DR6",
        "DR7",
    ):
        assert f'"{register}"' in crash
    for control in (
        '"context_flags"',
        '"mxcsr"',
        '"eflags"',
        '"seg_cs"',
        '"seg_ds"',
        '"seg_es"',
        '"seg_fs"',
        '"seg_gs"',
        '"seg_ss"',
    ):
        assert control in crash


def test_callback_preserves_stock_semantics_and_precedes_termination() -> None:
    crash = read("src/dlw_crash.c")
    wrapper = crash.rsplit(
        "static int __cdecl dluma_dump_function_wrapper(", 1
    )[1].split("static void dluma_crash_retain", 1)[0]
    worker = block(
        crash,
        "static DWORD WINAPI dluma_crash_reporter_thread",
        "static void dluma_crash_close_reporter_handles",
    )
    assert wrapper.count("original(exception_code, exception_pointers)") == 1
    assert wrapper.index("dluma_crash_signal_incident") < wrapper.index(
        "original(exception_code, exception_pointers)"
    )
    assert wrapper.index("dluma_crash_publish_stock_result") > wrapper.index(
        "original(exception_code, exception_pointers)"
    )
    assert "dluma_crash_write_report" not in wrapper
    assert "dluma_crash_show_dialog" not in wrapper
    assert worker.index("dluma_crash_write_report") < worker.index(
        "dluma_crash_finish_report"
    )
    assert worker.index("dluma_crash_finish_report") < worker.index(
        "dluma_crash_show_dialog"
    )
    assert "return result;" in wrapper
    assert "report_state, 1, 0" in wrapper
    assert "active_callbacks" in wrapper
    assert "popup_shown" in worker
    assert "g_dluma.config.crash_dialog_enabled" in worker
    core = read("src/dlw_crash_core.h")
    for code in (
        "0x80000003UL",
        "0x80000004UL",
        "0x40010006UL",
        "0x4001000AUL",
        "0x406D1388UL",
    ):
        assert code in core
    post_stock = wrapper.split(
        "original(exception_code, exception_pointers)", 1
    )[1]
    assert "result == DLUMA_DUMP_RESULT_FATAL || secondary_code" in post_stock


def test_fault_path_uses_only_preopened_and_preformatted_state() -> None:
    crash = read("src/dlw_crash.c")
    report = block(
        crash,
        "static void dluma_crash_write_report",
        "static void dluma_crash_finish_report",
    )
    wrapper = crash.rsplit(
        "static int __cdecl dluma_dump_function_wrapper(", 1
    )[1].split("static void dluma_crash_retain", 1)[0]
    capture = block(
        crash,
        "static void dluma_crash_capture_incident",
        "static int dluma_crash_capture_secondary_exception",
    )
    for forbidden in (
        "CreateFileW",
        "FindFirstFileW",
        "K32EnumProcessModules",
        "dluma_log_line",
        "dluma_log_pair",
        "dluma_native_lock",
        "dluma_managed_lock",
        "HeapAlloc",
        "malloc(",
        "LoadLibrary",
    ):
        assert forbidden not in report
        assert forbidden not in wrapper
        assert forbidden not in capture
    assert "dluma_crash_capture_modules" not in report
    assert "dluma_crash_capture_modules" not in wrapper
    assert "dluma_log_emergency" in crash
    assert "dluma_crash_write_dedicated" in crash
    assert "dluma_crash_emit_final_text" in crash
    assert "CreateThread(" in crash
    assert "DLUMA_CRASH_REPORTER_STACK_BYTES" in crash
    assert "WaitForSingleObject(" in wrapper
    assert "__try" in capture and "__except (1)" in capture
    assert "dluma_is_readable" not in capture
    emergency = block(
        crash,
        "static void dluma_crash_write_reporter_emergency",
        "static DWORD WINAPI dluma_crash_reporter_thread",
    )
    assert "reporter_exception_code" in emergency
    assert "reporter_exception_address" in emergency
    assert "dluma_log_emergency(g_dluma_crash.line);" in emergency
    for forbidden in ("sprintf", "snprintf", "malloc("):
        assert forbidden not in emergency


def test_reserved_reporter_is_armed_before_callback_ownership_and_drained() -> None:
    crash = read("src/dlw_crash.c")
    start = block(
        crash,
        "int dluma_crash_reporting_start(void)",
        "int dluma_crash_reporting_start_editor_filter",
    )
    stop = crash[crash.index("int dluma_crash_reporting_stop(void)") :]
    assert start.index("dluma_crash_reporter_start()") < start.index(
        "g_dluma_crash.set_dump_function(dluma_dump_function_wrapper)"
    )
    assert "DLUMA_CRASH_UNAVAILABLE_REPORTER_WORKER" in start
    assert "callback ownership was left unchanged" in start
    assert "dluma_crash_reporter_stop()" in start
    assert stop.index("g_dluma_crash.active_callbacks > 0") < stop.rindex(
        "dluma_crash_reporter_stop()"
    )
    assert stop.rindex("dluma_crash_reporter_stop()") < stop.rindex(
        "dluma_crash_close_empty_report()"
    )


def test_report_directory_and_retention_are_fail_closed() -> None:
    crash = read("src/dlw_crash.c")
    prepare = block(
        crash,
        "static int dluma_crash_prepare_paths",
        "static int dluma_crash_open_report_file",
    )
    prune = block(
        crash,
        "static void dluma_crash_prune_reports",
        "static int dluma_crash_prepare_paths",
    )
    opening = block(
        crash,
        "static int dluma_crash_open_report_file",
        "static void dluma_crash_close_empty_report",
    )
    close = block(
        crash,
        "static void dluma_crash_close_empty_report",
        "static void dluma_crash_capture_system",
    )
    assert "dluma_runtime" in prepare and 'L"crashes"' in prepare
    assert "dluma_get_final_path_checked" in prepare
    assert "dluma_crash_path_is_under" in prepare
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in prune
    assert "dluma_crash_core_report_name_matches" in prune
    assert "DLUMA_CRASH_KEEP_REPORTS 10U" in crash
    assert "if (!size)" in prune and "DeleteFileW(full)" in prune
    assert "CREATE_NEW" in opening
    assert "dluma_get_final_path_checked" in opening
    assert "dluma_crash_path_is_under" in opening
    assert "GetFileSizeEx" in close
    assert "size.QuadPart == 0" in close


def test_startup_and_shutdown_order_cover_all_mod_runtimes() -> None:
    exports = read("src/dlw_exports.c")
    player_start = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    start = player_start.index("dluma_crash_reporting_start()")
    assert player_start.index("dluma_load_config()") < start
    for later in (
        "dluma_native_mods_discover()",
        "dluma_managed_mods_discover()",
        "dluma_native_mods_pre_game_init",
        "dluma_managed_host_start()",
        "dluma_host_start()",
    ):
        assert start < player_start.index(later)
    shutdown_export = exports.split(
        "static __int64 dluma_player_shutdown(void)", 1
    )[1].split('extern "C" __declspec(dllexport) void*', 1)[0]
    shutdown = shutdown_export.index("dluma_crash_reporting_stop()")
    assert shutdown_export.index("g_original_shutdown()") < shutdown
    assert shutdown < shutdown_export.index("dluma_log_stop()", shutdown)
    assert "if (crash_stopped && !dluma_log_stop())" in shutdown_export
    assert "normal logger state is retained until process exit" in shutdown_export
    initialize_failure = exports.split(
        "Original InitializeGameScriptDLL returned null.", 1
    )[1].split("return 0;", 1)[0]
    assert "if (dluma_crash_reporting_stop())" in initialize_failure
    assert "dluma_log_stop()" in initialize_failure
    assert "dluma_crash_reporting_refresh();" in exports
    host = read("src/dlw_host.c")
    ui = read("src/dlw_ui.c")
    assert "dluma_crash_reporting_stop" not in host
    assert "dluma_crash_reporting_start" not in ui
    assert '"map_active"' in host
    assert '"map_unloaded"' in host


def test_editor_crash_handler_is_process_lifetime_and_armed_before_editor_mods() -> None:
    editor = read("src/dlw_editor_host.c")
    start = editor.split("int dluma_editor_host_start(void)", 1)[1].split(
        "int dluma_editor_host_session_begin", 1
    )[0]
    crash_start = start.index("dluma_crash_reporting_start()")
    assert start.index("dluma_load_config()") < crash_start
    for later in (
        "dluma_analysis_bridge_start(0)",
        "dluma_native_mods_discover()",
        "dluma_native_mods_editor_load()",
        "dluma_editor_launch_hook_start()",
        "dluma_quest_editor_start()",
    ):
        assert crash_start < start.index(later)
    assert "Editor crash diagnostics were unavailable" in start
    assert '"host_start", 0, 0' in start
    assert start.index('"host_start", 0, 0') < crash_start
    observer_start = start.index("dluma_editor_exit_observer_start()")
    assert crash_start < observer_start
    for later in (
        "dluma_analysis_bridge_start(0)",
        "dluma_native_mods_discover()",
        "dluma_native_mods_editor_load()",
    ):
        assert observer_start < start.index(later)

    crash = read("src/dlw_crash.c")
    crash_start_impl = block(
        crash,
        "int dluma_crash_reporting_start(void)",
        "int dluma_crash_reporting_stop(void)",
    )
    assert "state != 0 && state != 3" in crash_start_impl
    assert "!current && !g_dluma_crash.editor_host" in crash_start_impl
    assert "Chrome Editor's empty dump callback slot" in crash_start_impl
    assert crash_start_impl.index("GetModuleHandleW") < crash_start_impl.index(
        "dluma_crash_prepare_paths()"
    )

    exports = read("src/dlw_exports.c")
    editor_shutdown = exports.split("static __int64 dluma_editor_shutdown(void)", 1)[
        1
    ].split("static void* dluma_player_initialize(void)", 1)[0]
    assert "dluma_crash_reporting_stop" not in editor_shutdown
    assert "dluma_log_stop" not in editor_shutdown
    assert "dluma_editor_exit_observer" not in editor_shutdown
    assert '"session_unloaded"' in editor_shutdown
    assert '"session_unload_failed"' in editor_shutdown


def test_editor_shutdown_intent_suppresses_only_the_popup() -> None:
    crash = read("src/dlw_crash.c")
    observer = read("src/dlw_editor_exit_observer.c")
    wrapper = crash.rsplit(
        "static int __cdecl dluma_dump_function_wrapper", 1
    )[1].split("static void dluma_crash_retain", 1)[0]
    worker = block(
        crash,
        "static DWORD WINAPI dluma_crash_reporter_thread",
        "static void dluma_crash_close_reporter_handles",
    )
    report = block(
        crash,
        "static void dluma_crash_write_report",
        "static void dluma_crash_finish_report",
    )
    for field in (
        "editor_exit_intent",
        "editor_exit_reason",
        "editor_exit_generation",
        "editor_exit_observed_ms",
        "editor_exit_elapsed_ms",
        "crash_dialog_suppressed",
        "editor_shutdown",
    ):
        assert field in report
    assert "dluma_crash_signal_incident" in wrapper
    assert "original(exception_code, exception_pointers)" in wrapper
    assert "return result;" in wrapper
    assert "!g_dluma_crash.dialog_suppressed_editor_shutdown" in worker
    assert wrapper.index("original(exception_code, exception_pointers)") < wrapper.index(
        "dluma_crash_publish_stock_result"
    )
    assert "attempt < 2U" in observer
    snapshot_copy = block(
        observer,
        "int dluma_editor_exit_observer_copy_snapshot",
        "static BOOL CALLBACK dluma_editor_exit_find_window",
    )
    for forbidden in ("Sleep(", "malloc(", "HeapAlloc", "_lock("):
        assert forbidden not in snapshot_copy


def test_editor_exit_observer_is_cancellation_safe_and_process_lifetime() -> None:
    observer = read("src/dlw_editor_exit_observer.c")
    window_proc = block(
        observer,
        "static LRESULT CALLBACK dluma_editor_exit_window_proc",
        "int dluma_editor_exit_observer_start",
    )
    assert "WM_QUERYENDSESSION" not in window_proc
    for message in ("WM_CLOSE", "WM_SYSCOMMAND", "SC_CLOSE", "WM_ENDSESSION"):
        assert message in window_proc
    assert "WM_NCDESTROY" in window_proc
    assert window_proc.index("dluma_editor_exit_intent_begin(") < window_proc.index(
        "CallWindowProcW(original"
    )
    assert "!IsWindow(window) || !IsWindowVisible(window)" in window_proc
    assert "dluma_editor_exit_intent_resolve" in window_proc
    assert "DLUMA_EDITOR_EXIT_REASON_ALT_F4" in window_proc
    assert "DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE" in window_proc
    assert "DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END" in window_proc
    assert "EnumWindows(" in observer
    assert "GetCurrentProcessId()" in observer
    assert "GetWindow(window, GW_OWNER)" in observer
    assert "IsWindowVisible(window)" in observer
    assert "process_id != candidate.process_id" in observer
    observer_start = observer.split("int dluma_editor_exit_observer_start", 1)[1]
    assert "installed_predecessor = (WNDPROC)SetWindowLongPtrW" in observer_start
    assert "if (!installed_predecessor)" in observer_start
    assert (
        "g_dluma_editor_exit_observer.original = installed_predecessor"
        in observer_start
    )
    assert ") != observed" not in observer_start
    assert "state.observer_active = 0U" not in observer
    assert "dluma_editor_exit_observer_stop" not in observer


def test_editor_exit_intent_native_harness() -> None:
    completed = subprocess.run(
        [
            "cmd.exe", "/d", "/s", "/c",
            "call tools\\build_editor_exit_intent_fake_tests.bat",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=90,
    )
    assert completed.returncode == 0, (
        f"Editor exit-intent harness exited {completed.returncode}\n"
        f"{completed.stdout}{completed.stderr}"
    )
    assert "Editor exit-intent fake cases passed: 5" in completed.stdout


@pytest.mark.parametrize(
    "harness_test",
    [test_crash_core_native_harness, test_editor_exit_intent_native_harness],
)
def test_native_harness_failures_include_diagnostics(monkeypatch, harness_test):
    completed = subprocess.CompletedProcess(
        args=[], returncode=7,
        stdout="compiler diagnostic\n", stderr="worker diagnostic\n",
    )
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: completed)
    with pytest.raises(AssertionError) as failure:
        harness_test()
    assert "exited 7" in str(failure.value)
    assert "compiler diagnostic" in str(failure.value)
    assert "worker diagnostic" in str(failure.value)



def test_editor_host_snapshot_is_preformatted_double_buffered_and_additive() -> None:
    crash = read("src/dlw_crash.c")
    publication = block(
        crash,
        "void dluma_crash_publish_host_context",
        "void dluma_crash_reporting_refresh",
    )
    for field in (
        "host_kind",
        "host_exact_profile",
        "host_classification_reason",
        "editor_session_phase",
        "editor_current_generation",
        "editor_last_generation",
    ):
        assert field in publication
    assert "host_contexts[2]" in crash
    assert "host_context_active" in publication
    assert "host_context_sequence" in publication
    assert "dluma_crash_core_ascii_equal_icase(host_kind, \"chrome_editor\")" in publication

    emit = block(
        crash,
        "static void dluma_crash_emit_component_contexts",
        "static void dluma_crash_write_report",
    )
    assert "attempt < 2U" in emit
    assert "before == after" in emit
    for forbidden in ("Sleep(", "HeapAlloc", "malloc(", "_lock("):
        assert forbidden not in emit
    assert '"report_begin", "DLW_CRASH_REPORT_V1"' in crash


def test_editor_session_transitions_refresh_crash_context() -> None:
    editor = read("src/dlw_editor_host.c")
    exports = read("src/dlw_exports.c")
    for phase in (
        "session_initializing",
        "session_active",
        "session_quiescing",
        "session_shutdown_failed",
        "session_ended",
    ):
        assert f'"{phase}"' in editor
    assert "last_session_generation" in editor
    assert "dluma_crash_reporting_refresh();" in editor
    editor_initialize = exports.split("static void* dluma_editor_initialize", 1)[1].split(
        "static __int64 dluma_editor_shutdown", 1
    )[0]
    assert editor_initialize.count('"session_initialize_failed"') == 3
    assert '"session_shutdown_rejected"' in exports


def test_registry_snapshots_are_bounded_and_lock_free_to_copy() -> None:
    sources = {
        "native": read("src/dlw_native_mods.c"),
        "managed": read("src/hosts/dlw_managed_host.c"),
        "lua": read("src/dlw_lua_runtime.c"),
    }
    assert "[32U * 1024U]" in sources["native"]
    assert "(32U * 1024U)" in sources["managed"]
    assert "(32u * 1024u)" in sources["lua"]
    for name, signature in (
        ("native", "int dluma_native_mods_copy_crash_context"),
        ("managed", "int dluma_managed_mods_copy_crash_context"),
        ("lua", "int dluma_lua_runtime_copy_crash_context"),
    ):
        copy = sources[name].split(signature, 1)[1].split("\n}\n", 1)[0]
        assert "attempt < 2U" in copy
        assert "sequence" in copy
        assert "_lock(" not in copy
        assert "Sleep(" not in copy
    crash = read("src/dlw_crash.c")
    assert "module_sets[2]" in crash
    assert "runtime_contexts[2]" in crash
    assert "module_snapshot_active" in crash
    assert "runtime_context_active" in crash


def test_partial_write_loops_cover_both_log_destinations() -> None:
    logger = read("src/dlw_log.c")
    emergency = block(logger, "void dluma_log_emergency", "void dluma_log_line")
    assert "while (offset < length && attempts < 4U)" in emergency
    assert "written == 0" in emergency
    crash = read("src/dlw_crash.c")
    assert "dluma_crash_core_write_all(" in crash
    assert "written != length" in crash


def test_popup_setting_and_localized_report_path_contract() -> None:
    ini = read(
        "config/dluma.ini"
    )
    assert "controls only the popup" in ini
    assert "dluma_runtime\\crashes" in ini
    locale_root = ROOT / "PakAssets" / "Locale"
    catalogs = sorted(locale_root.glob("*/loader_texts_all.scr"))
    assert len(catalogs) == 17
    for catalog in catalogs:
        text = catalog.read_text(encoding="utf-8")
        assert text.count('String("DLUMA_Crash_Label_InfoLog"') == 1
        assert text.count('String("DLUMA_Crash_Body_Windows"') == 1
        assert text.count('String("DLUMA_Crash_Editor_NeutralTitle"') == 1
        assert text.count('String("DLUMA_Crash_Editor_Context"') == 1
        assert text.count('String("DLUMA_Crash_Editor_Note"') == 1
        body = next(
            line
            for line in text.splitlines()
            if line.startswith('String("DLUMA_Crash_Body_Loader"')
        )
        assert len(body) > len('String("DLUMA_Crash_Body_Loader", "")')
        first_party_body = next(
            line
            for line in text.splitlines()
            if line.startswith('String("DLUMA_Crash_Body_Game"')
        ).lower()
        assert "overlay" not in first_party_body
        assert "injector" not in first_party_body
    crash = read("src/dlw_crash.c")
    assert '"DLUMA_Crash_Label_InfoLog"' in crash
    assert "g_dluma_crash.report_path" in crash
    assert "g_dluma_crash.text.editor_context" in crash
    assert "g_dluma_crash.text.editor_note" in crash
    assert "g_dluma_crash.editor_host" in crash


def test_crash_module_classification_is_exact_and_first_party_wording_is_causal() -> None:
    crash = read("src/dlw_crash.c")
    core = read("src/dlw_crash_core.h")
    for profile in (
        (
            'L"gamedll_x64_rwdi.dll"',
            "0x623397AAUL",
            "0x02082000UL",
            "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95",
        ),
        (
            'L"engine_x64_rwdi.dll"',
            "0x623395D0UL",
            "0x00E8D000UL",
            "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C",
        ),
        (
            'L"filesystem_x64_rwdi.dll"',
            "0x623394CDUL",
            "0x00060000UL",
            "CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D",
        ),
        (
            'L"imagelib_x64_rwdi.dll"',
            "0x623394DCUL",
            "0x00068000UL",
            "33FB6905C7F2CA587CC13F1A7CF16D17D245CB1FE739CC029D54529DF4A2DEE2",
        ),
    ):
        for token in profile:
            assert token in crash
    assert "dluma_sha256_file_hex(" in crash
    assert "dluma_crash_core_game_profile_basename_matches(" in crash
    assert "unsupported_game_module_identity" in core
    for category in (
        "GameExecutable",
        "GameModule",
        "LoaderModule",
        "WindowsSystemModule",
        "KnownThirdPartyModule",
        "UnknownLoadedModule",
        "UnmappedAddress",
    ):
        assert f'"{category}"' in core
    defaults = block(
        crash,
        "static void dluma_crash_defaults",
        "void dluma_crash_localization_refresh",
    )
    game_body = defaults.split(
        "text_state->body_game", 2
    )[2].split("text_state->body_windows", 1)[0].lower()
    assert "faulting instruction" in game_body
    assert "does not by itself prove" in game_body
    assert "overlay" not in game_body
    assert "injector" not in game_body


def test_no_competing_dump_or_symbol_owners_are_added() -> None:
    crash = read("src/dlw_crash.c")
    for forbidden in (
        "DbgHelp",
        "MiniDumpWriteDump",
        "AddVectoredExceptionHandler",
        "SymInitialize",
        "SymFromAddr",
    ):
        assert forbidden not in crash


def test_editor_only_unhandled_filter_preserves_prior_filter_and_result() -> None:
    crash = read("src/dlw_crash.c")
    editor_filter = crash.rsplit(
        "static LONG WINAPI dluma_editor_unhandled_exception_filter", 1
    )[1].split("static void dluma_crash_retain", 1)[0]
    install = block(
        crash,
        "int dluma_crash_reporting_start_editor_filter",
        "int dluma_crash_reporting_stop",
    )
    assert "original(exception_pointers)" in editor_filter
    assert "return result;" in editor_filter
    assert "dluma_crash_signal_incident" in editor_filter
    assert "dluma_crash_publish_stock_result" in editor_filter
    assert "dluma_crash_write_report" not in editor_filter
    assert "dluma_crash_show_dialog" not in editor_filter
    assert install.count("SetUnhandledExceptionFilter") == 2
    assert "g_dluma_crash.editor_host" in install
    assert "previous != dluma_editor_unhandled_exception_filter" in install
    assert "AddVectoredExceptionHandler" not in crash

    editor = read("src/dlw_editor_host.c")
    editor_start = editor.split(
        "int dluma_editor_host_start(void)", 1
    )[1].split("int dluma_editor_host_session_begin", 1)[0]
    assert "dluma_crash_reporting_start_editor_filter()" in editor_start

    exports = read("src/dlw_exports.c")
    player_start = exports.split(
        "static void* dluma_player_initialize(void)", 1
    )[1].split("static __int64 dluma_player_shutdown(void)", 1)[0]
    assert "dluma_crash_reporting_start_editor_filter" not in player_start
    editor_initialize = exports.split(
        "static void* dluma_editor_initialize(void)", 1
    )[1].split("static __int64 dluma_editor_shutdown(void)", 1)[0]
    assert "dluma_crash_reporting_start_editor_filter" in editor_initialize

    quest_editor = read("src/dlw_quest_editor.c")
    quest_initialize = quest_editor.split(
        "static __int64 __fastcall dluma_quest_editor_initialize_hook", 2
    )[2].split("static void __fastcall dluma_quest_editor_compile_hook", 1)[0]
    assert quest_initialize.index("original(context)") < quest_initialize.index(
        "dluma_crash_reporting_start_editor_filter()"
    )


def test_editor_exit_observer_retries_startup_and_accepts_frame_destruction() -> None:
    observer = read("src/dlw_editor_exit_observer.c")
    assert "message == WM_DESTROY || message == WM_NCDESTROY" in observer
    assert "DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED" in observer
    start = observer.split("int dluma_editor_exit_observer_start(void)", 1)[1]
    assert "&g_dluma_editor_exit_observer.start_state, -1" not in start
    presence = read("src/dlw_editor_presence.c")
    worker = presence.split("static DWORD WINAPI dluma_editor_presence_worker", 1)[1].split(
        "static void dluma_editor_presence_detach_windows", 1)[0]
    assert worker.index("dluma_editor_exit_observer_start()") < worker.index(
        "dluma_editor_presence_attach_main_window()")
    dispatch = observer.split("int dluma_editor_exit_observer_dispatch_command", 1)[1].split(
        "int dluma_editor_exit_observer_start", 1)[0]
    # A chained presence subclass must not disable editor bridge commands.
    assert "GetWindowLongPtrW" not in dispatch
    assert "process_id != GetCurrentProcessId()" in dispatch
