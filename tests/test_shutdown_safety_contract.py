from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_orderly_exit_hooks_are_build_gated_and_nonblocking():
    shutdown = source("dlw_shutdown.c")
    assert "DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP 0x623397AAUL" in shutdown
    assert "DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE 0x02082000UL" in shutdown
    assert "DLUMA_IMPORT_DIRECTORY_OFFSET 0x78UL" in shutdown
    assert '"?ExitGame@IGame@@QEAAXXZ"' in shutdown
    assert "WM_CLOSE" in shutdown
    assert "SC_CLOSE" in shutdown

    request = shutdown.split(
        "static void dluma_request_quiesce_impl", 1
    )[1].split("void dluma_request_quiesce", 1)[0]
    assert "WaitForSingleObject" not in request
    assert "Sleep(" not in request
    assert "dluma_ui_quiesce();" in request
    assert "dluma_ui_quiesce_from_exit_game_owner(" in request
    assert "dluma_engine_quiesce(&g_dluma.engine);" in request
    assert request.index("dluma_ui_quiesce_from_exit_game_owner(") < request.index(
        "dluma_engine_quiesce(&g_dluma.engine);"
    )
    exit_hook = shutdown.split(
        "static void __fastcall dluma_exit_game_hook", 1
    )[1].split("static LRESULT CALLBACK", 1)[0]
    assert "observed_exit_owner_calls" in exit_hook
    assert "dluma_request_quiesce_impl(" in exit_hook

    ui = source("dlw_ui.c")
    quiesce = ui.split("static void dluma_ui_quiesce_impl", 1)[1]
    finalizer = quiesce.index(
        "dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner("
    )
    cancel_timer = quiesce.index("KillTimer(0, g_dluma.ui_timer_id)")
    assert finalizer < cancel_timer
    assert "dluma_custom_map_asset_trace_game_thread_finalize()" in quiesce[
        finalizer:cancel_timer
    ]
    assert "g_dluma_ui_teardown_failed" in quiesce[finalizer:cancel_timer]
    assert "g_dluma_ui_content_finalize_state" in quiesce[:cancel_timer]
    assert "finalize_state == 0" in quiesce[:cancel_timer]
    assert "finalize_state != 2" in quiesce[:cancel_timer]

    content = source(
        "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    )
    orderly = content.split(
        "static int FinalizeContentReportsOnGameThread(", 1
    )[1].split(
        'extern "C" DlumaStartupB0PumpStatus', 1
    )[0]
    assert "g_trace_stopping" in orderly
    assert "g_content_live" in orderly
    assert orderly.index("WaitForExitGameOwnerCalls(") < orderly.index(
        "FinalizeCustomContentReports()"
    )
    assert "self_count_mismatch" in content
    assert "other_active_calls_timeout" in content
    assert "PinLoaderIfNeeded(true)" in orderly
    assert "diagnostic_reports_finalized" in orderly
    assert "ActiveConsumerCall" not in orderly

    native = source("dlw_native_gameplay.c")
    native_quiesce = native.split("void dluma_native_gameplay_quiesce", 1)[1].split(
        "void dluma_native_gameplay_stop", 1
    )[0]
    assert "dluma_native_lock(" not in native_quiesce
    assert "Sleep(" not in native_quiesce
    assert "WaitForSingleObject" not in native_quiesce
    assert "dluma_native_try_lock(&g_dluma_interactive_lifecycle_lock)" in native_quiesce

    engine = source("dlw_engine.c")
    localization = source("dlw_localization.c")
    assert '"?GethWnd@IGame@@QEBAPEAUHWND__@@XZ"' in engine
    assert "DLUMA_GET_LOCALE_ID_RVA 0x00261620UL" in localization
    assert "DlumaGetLocaleIdFn" in localization
    assert "GetVoiceLocaleID" not in engine + localization


def test_exit_game_finalizer_excludes_only_the_current_owner():
    content = source(
        "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.cpp"
    )
    header = source(
        "fixes/custom_map_asset_isolation/CustomMapAssetIsolationFix.h"
    )
    wait = content.split("bool WaitForExitGameOwnerCalls(", 1)[1].split(
        "void PublishSimple", 1
    )[0]
    assert "ClassifyExitGameFinalizationCounts(counts)" in wait
    assert "g_current_thread_hook_call_depth" in wait
    assert "g_current_thread_consumer_call_depth" in wait
    assert '"self_count_mismatch"' in wait
    assert '"other_active_calls_timeout"' in wait
    assert "expected_exit_owner_calls != 1" in header
    assert (
        "counts.hook_active_calls == counts.current_thread_hook_calls"
        in header
    )
    assert (
        "counts.consumer_active_calls ==\n"
        "               counts.current_thread_consumer_calls" in header
    )


def test_worker_shutdown_uses_a_wake_event():
    host = source("dlw_host.c")
    assert "CreateEventW(0, TRUE, FALSE, 0)" in host
    assert "SetEvent(g_dluma.stop_event)" in source("dlw_shutdown.c")
    worker = host.split("DWORD WINAPI dluma_worker_thread", 1)[1].split(
        "int dluma_host_start", 1
    )[0]
    assert "dluma_wait_for_stop(1000)" in worker
    assert "dluma_wait_for_stop(g_dluma.config.poll_interval_ms)" in worker
    assert "Sleep(1000)" not in worker
    assert "Sleep(g_dluma.config.poll_interval_ms)" not in worker


def test_crash_dialog_wraps_instead_of_replacing_dump_semantics():
    crash = source("dlw_crash.c")
    wrapper = crash.split("static int __cdecl dluma_dump_function_wrapper", 2)[2].split(
        "int dluma_crash_reporting_start", 1
    )[0]
    assert "original(exception_code, exception_pointers)" in wrapper
    assert "result == DLUMA_DUMP_RESULT_FATAL" in wrapper
    assert "dluma_crash_has_reportable_exception" in wrapper
    assert "return result;" in wrapper
    assert "MessageBoxW" in crash
    assert "dluma_crash_reporting_start_editor_filter" in crash
    assert "AddVectoredExceptionHandler" not in crash
    assert "CrashQuit" not in crash


def test_crash_dialog_configuration_uses_game_native_catalogs():
    ini = (
        ROOT
        / "config"
        / "dluma.ini"
    ).read_text(encoding="utf-8")
    assert "[crash_dialog]" in ini
    assert "enabled=1" in ini
    assert "random_titles=0" in ini
    assert "locale=" not in ini
    crash = source("dlw_crash.c")
    assert "dluma_read_file_utf8(g_dluma.locale_path" not in crash
    assert "dluma_crash_localization_refresh" in crash
    catalog = (ROOT / "PakAssets/Locale/En/loader_texts_all.scr").read_text(encoding="utf-8")
    assert 'String("DLUMA_Crash_Title_01"' in catalog
    assert not (ROOT / "config/dluma_locales.ini").exists()
