from __future__ import annotations

import importlib.util
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(text: str, signature: str, next_signature: str) -> str:
    return text.split(signature, 1)[1].split(next_signature, 1)[0]


def test_async_logger_has_fixed_mpsc_records_and_persistent_handles():
    header = source("src/dlw_log.h")
    logger = source("src/dlw_log.c")
    assert "DLUMA_LOG_QUEUE_CAPACITY 256U" in header
    assert "DLUMA_LOG_RECORD_CAPACITY 2048U" in header
    assert "enqueue_position" in logger
    assert "dequeue_position" in logger
    assert "active_producers" in logger
    assert "slot->sequence" in logger
    assert "dropped_lines" in logger
    assert "dropped " in logger
    assert "append_handle" in logger
    assert "emergency_handle" in logger


def test_logger_drains_partial_batches_without_waiting_for_another_wake():
    logger = source("src/dlw_log.c")
    writer = function_body(
        logger,
        "static DWORD WINAPI dluma_log_writer_thread",
        "int dluma_log_start",
    )
    assert "} while (dluma_log_has_pending());" in writer
    assert "} while (used == DLUMA_LOG_WRITER_BATCH_CAPACITY);" not in writer
    assert "g_dluma_log.active_producers == 0" in writer


def test_log_producers_do_not_open_close_sleep_or_flush_files():
    logger = source("src/dlw_log.c")
    producer = function_body(
        logger, "void dluma_log_line", "void dluma_log_pair"
    )
    enqueue = function_body(
        logger, "static int dluma_log_enqueue", "static unsigned int"
    )
    for forbidden in (
        "CreateFileW",
        "CloseHandle",
        "FlushFileBuffers",
        "Sleep(",
        "dluma_write_utf8_file",
    ):
        assert forbidden not in producer
        assert forbidden not in enqueue
    assert "dluma_log_enqueue(line, tick_ms, level, message)" in producer
    assert "dluma_log_history_store_publish" not in producer


def test_logger_shutdown_timeout_retains_every_dependency():
    logger = source("src/dlw_log.c")
    stop = function_body(
        logger, "int dluma_log_stop(void)", "void dluma_log_emergency"
    )
    assert "DLUMA_LOG_STOP_TIMEOUT_MS" in stop
    assert "result != WAIT_OBJECT_0" in stop
    assert "GET_MODULE_HANDLE_EX_FLAG_PIN" in stop
    timeout = stop.split("result != WAIT_OBJECT_0", 1)[1].split(
        "return 0;", 1
    )[0]
    assert "CloseHandle" not in timeout


def test_crash_context_never_uses_registry_or_normal_logger():
    crash = source("src/dlw_crash.c")
    native = source("src/dlw_native_mods.c")
    report = function_body(
        crash,
        "static void dluma_crash_write_report",
        "static void dluma_crash_finish_report",
    )
    copy = native.split(
        "int dluma_native_mods_copy_crash_context", 1
    )[1]
    assert "dluma_crash_emit" in report
    assert "dluma_log_line" not in report
    assert "dluma_native_lock" not in report
    assert "dluma_native_lock" not in copy
    assert "attempt < 2U" in copy


def test_discovery_scheduler_debounces_watches_and_has_bounded_sweeps():
    watch = source("src/dlw_discovery_watch.h")
    implementation = source("src/dlw_discovery_watch.c")
    host = source("src/dlw_host.c")
    assert "DLUMA_DISCOVERY_DEBOUNCE_MS 500ULL" in watch
    assert "10ULL * 60ULL * 1000ULL" in watch
    assert "60ULL * 1000ULL" in watch
    assert "FindFirstChangeNotificationW" in implementation
    assert "FindNextChangeNotification" in implementation
    assert "bWatchSubtree" not in implementation
    assert "DLUMA_DISCOVERY_NAME_FILTER" in implementation
    assert "DLUMA_DISCOVERY_CONTENT_FILTER" in implementation
    assert 'L"data\\\\scripts\\\\lua"' in implementation
    assert 'L"data\\\\maps"' in implementation
    assert 'L"out"' in implementation
    assert "dluma_discovery_watch_add(root, 1, 1)" in implementation
    assert "dluma_discovery_watch_add(project_root, 0, 0)" in implementation
    assert "dluma_discovery_watch_add(path, 0, 1)" in implementation
    assert "dlw_project_owner_identity.h" not in implementation
    assert "dluma_discovery_watch_requires_fallback" in host
    polled = host.split("if (dluma_discovery_watch_poll())", 1)[1].split(
        "}", 1
    )[0]
    assert "dluma_discovery_watch_sync_roots" in polled
    assert "DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY" in host
    assert "DLUMA_LUA_DISCOVERY_FALLBACK" in host


def test_discovery_cache_keys_invalid_results_and_rechecks_containment():
    discovery = source("src/dlw_lua_discovery.c")
    assert "DlumaLuaMetadataCacheEntry" in discovery
    assert "parse_succeeded" in discovery
    assert "saw_header" in discovery
    assert "final_project_root" in discovery
    assert "last_write_high" in discovery
    cached = function_body(
        discovery,
        "static int dluma_lua_parse_metadata_cached",
        "static int dluma_lua_find_id",
    )
    assert cached.index("dluma_lua_probe_file_key") < cached.index(
        "dluma_lua_cache_find"
    )
    assert "dluma_lua_classify_global_review" in discovery
    assert "lua_discovery_source_bytes_read" in discovery
    assert "lua_discovery_projects_truncated" in discovery
    assert "lua_discovery_files_truncated" in discovery
    assert (
        "p < project_count && path_count < DLUMA_MAX_LUA_FILES"
        not in discovery
    )


def test_workshop_menu_reuses_unchanged_description_and_preview_metadata():
    menu = source("src/dlw_workshop_menu.c")
    assert "DlumaWorkshopLocalMetadataCacheEntry" in menu
    assert "dluma_workshop_local_metadata_cache_find" in menu
    assert "dluma_workshop_local_stamp_equal" in menu
    assert "final_project_root" in menu
    assert "description_stamp" in menu
    assert "preview_stamp" in menu


def test_discovery_removed_redundant_source_fnv_and_uses_crypto_fingerprint():
    discovery = source("src/dlw_lua_discovery.c")
    consent = source("src/dlw_global_consent.c")
    parser = function_body(
        discovery,
        "static int dluma_lua_parse_metadata(",
        "static int dluma_lua_parse_metadata_cached",
    )
    assert "1469598103934665603ULL" not in parser
    assert "dluma_sha256_bytes_hex" in parser
    assert "dluma_global_hex_prefix_u64(item->content_sha256)" in consent


def test_checked_fixed_buffer_path_helpers_reject_truncation():
    helper = source("src/dlw_path_security.c")
    assert helper.count("!length || length >= capacity") == 2
    assert "GetFinalPathNameByHandleW" in helper
    assert "GetModuleFileNameW" in helper


@pytest.mark.parametrize(
    "script_id",
    (
        "CON",
        "con.lua",
        "PrN.data",
        "AUX.meta",
        "NuL.x",
        "CLOCK$.cfg",
        "COM1.ext",
        "com9",
        "LPT1.any",
        "lPt9",
        ".leading",
        "trailing.",
        "trailing ",
        "a" * 128,
    ),
)
def test_reserved_and_boundary_lua_ids_are_rejected(script_id, tmp_path):
    validator_path = ROOT / "tools" / "validate_lua_metadata.py"
    spec = importlib.util.spec_from_file_location(
        "sample_lua_validator", validator_path
    )
    assert spec and spec.loader
    validator = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = validator
    spec.loader.exec_module(validator)
    path = (
        tmp_path
        / "data"
        / "scripts"
        / "lua"
        / "global"
        / "test.lua"
    )
    path.parent.mkdir(parents=True)
    path.write_text(
        "local mod = dlua.mod {\n"
        f'    id = "{script_id}",\n'
        '    name = "test",\n'
        '    version = "1.0.0",\n'
        '    scope = "global",\n'
        "}\n"
        "return mod\n",
        encoding="utf-8",
    )
    assert not validator.parse_metadata(path).valid


def test_build_and_deploy_manifests_are_explicit():
    project = source("DLUMA.vcxproj")
    batch = source("build_windows_msvc.bat")
    clang = source("build_windows_clang.bat")
    helper_msvc = source("tools/build_lua54_helper_msvc.bat")
    helper_clang = source("tools/build_lua54_helper_clang.bat")
    assert '<Target Name="PackageWorkshop"' in project
    assert '<Target Name="DeployLocalPackage"' in project
    assert '<Target Name="DeployLocal"' in project
    assert "Build;DeployLocalPackage" in project
    assert "verify_msvc_object_manifest.py" in project
    assert "verify_package_manifest.py" in project
    assert "deploy_local_package.py" in project
    assert "*.obj" not in batch
    assert "src\\*.c" not in batch
    assert "for /R" not in batch
    for script in (clang, helper_msvc, helper_clang):
        assert "*.obj" not in script
        assert "verify_object_set.py" in script
    assert "src\\*.c" not in clang
    assert "for /R" not in clang
    assert "list_vcxproj_sources.py" in clang
    assert "*.dll" not in project
    assert "*.pak" not in project

