from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def test_approved_lua_identity_is_bound_to_the_executed_buffer():
    discovery = source("dlw_lua_discovery.c")
    consent = source("dlw_global_consent.c")
    runtime = source("dlw_lua_runtime.c")
    package_runtime = source("core/dlw_mod_package_runtime.c")
    assert "dluma_lua_read_validated_source(" in discovery
    assert "dluma_sha256_bytes_hex(data, size, out->content_sha256);" in discovery
    assert "dluma_sha256_file_hex(item->file_path" not in consent
    assert "dluma_mod_package_discover(" in runtime
    assert "dluma_mod_package_read_artifact(" in runtime
    assert "source_artifact.sha256" in runtime
    assert "metadata->content_sha256" in runtime
    assert "lua_approved_source_snapshot_mismatch" in runtime
    assert runtime.index("source_artifact.sha256") < runtime.index("api.load_script")
    assert "approved lock bytes changed" in package_runtime
    assert "locked artifact bytes changed" in package_runtime
    assert "lock changed while reading artifact" in package_runtime


def test_discovery_rejects_reparse_entries_and_checks_final_containment():
    discovery = source("dlw_lua_discovery.c")
    path_security = source("dlw_path_security.c")
    assert "FILE_ATTRIBUTE_REPARSE_POINT" in discovery
    assert "dluma_get_final_path_checked" in discovery
    assert "GetFinalPathNameByHandleW" in path_security
    assert "GetFileInformationByHandle" in discovery
    assert "dluma_lua_final_path_within_root" in discovery
    infer = discovery[
        discovery.index("static unsigned char dluma_lua_infer_scope_from_path") :
        discovery.index("static int dluma_lua_parse_metadata")
    ]
    assert "dluma_lua_project_relative_path" in infer
    assert "dluma_wcontains_icase(path" not in infer


def test_storage_uses_collision_resistant_logical_identity():
    common = source("dlw_common.h")
    consent = source("dlw_global_consent.c")
    runtime = source("dlw_lua_runtime.c")
    assert "storage_key[DLUMA_SHA256_HEX_CAPACITY]" in common
    assert '"DLW_LOCAL_STORAGE_KEY_V1"' in consent
    storage = runtime[
        runtime.index("static int dluma_lua_runtime_storage_path") :
        runtime.index("static uint64_t dluma_lua_runtime_storage_usage")
    ]
    assert "metadata.storage_key" in storage
    assert "dluma_lua_runtime_sanitize_id" not in runtime
    assert "lua_legacy_storage_left_unclaimed" in storage


def test_allocator_and_oom_paths_are_nonallocating_when_exhausted():
    helper = source("dlw_lua54_helper.c")
    allocator = helper[
        helper.index("static void* dluma_helper_allocator") :
        helper.index("static int dluma_helper_panic")
    ]
    assert "effective_old_size = pointer ?" in allocator
    assert "memory_used = next_used" in allocator
    finish = helper[
        helper.index("static int dluma_helper_finish_protected") :
        helper.index("static int dluma_helper_call_ref")
    ]
    assert "luaL_traceback" not in finish
    assert "result == LUA_ERRMEM" in finish
    assert "lua_atpanic" in helper
    assert "not enough memory" in helper


def test_shutdown_timeout_pins_and_retains_dependencies():
    host = source("dlw_host.c")
    exports = source("dlw_exports.c")
    stop = host[host.index("int dluma_host_stop(void)") :]
    assert "worker_result != WAIT_OBJECT_0" in stop
    assert "dluma_host_retain_for_process_exit" in stop
    assert stop.index("worker_result != WAIT_OBJECT_0") < stop.index("CloseHandle(worker)")
    assert "dluma_lua_runtime_stop_succeeded" in stop
    assert "dluma_ui_stop_succeeded" in stop
    assert "if (loader_stopped) dluma_unmount_content_pack();" in exports


def test_original_game_dll_has_no_basename_search_fallback():
    exports = source("dlw_exports.c")
    finder = exports[
        exports.index("static HMODULE dluma_find_original_game_dll") :
        exports.index("static int dluma_resolve_original_game_script")
    ]
    assert 'LoadLibraryW(L"gamedll_x64_rwdi.dll")' not in finder
    assert "LoadLibraryExW" in exports
    assert "LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR" in exports
    assert "dluma_get_final_path_checked" in exports


def test_stack_cookie_exemption_is_limited_to_cookie_bootstrap_and_entrypoint():
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    msvc = (ROOT / "build_windows_msvc.bat").read_text(encoding="utf-8")
    assert '<ClCompile Include="src\\dlw_entry.c">' in project
    assert '<ClCompile Include="src\\dlw_security.c">' in project
    assert project.count("<BufferSecurityCheck>false</BufferSecurityCheck>") == 2
    assert "<BufferSecurityCheck>true</BufferSecurityCheck>" in project
    assert "<ControlFlowGuard>Guard</ControlFlowGuard>" in project
    assert "msbuild.exe DLUMA.vcxproj" in msvc
    assert "*.obj" not in msvc


def test_utility_boundaries_are_strict_and_overflow_safe():
    util = source("dlw_util.c")
    suffix = util[
        util.index("int dluma_wends_with_icase") : util.index("int dluma_is_space")
    ]
    assert suffix.index("if (!value || !suffix)") < suffix.index("wcslen(value)")
    parser = util[
        util.index("unsigned int dluma_parse_u32") : util.index("unsigned int dluma_u64_to_text")
    ]
    assert "0xFFFFFFFFUL - digit" in parser
    assert "any && !*p" in parser
    readable = util[
        util.index("int dluma_is_readable") : util.index("void dluma_sanitize_filename")
    ]
    assert "PAGE_EXECUTE ||" not in readable
    assert "PAGE_EXECUTE_READ" in readable
    assert "~(ULONG_PTR)0 - (bytes - 1U)" in readable
