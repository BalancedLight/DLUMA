#include "../src/dlw_animation_refresh_runtime.h"
#include "../src/core/dlw_animation_refresh.h"
#include "../src/dlw_dual_host_runtime.h"
#include "../src/dlw_log.h"
#include "../src/dlw_project_owner_identity.h"

#include <stdio.h>
#include <string.h>

__declspec(dllimport) BOOL WINAPI CreateHardLinkW(
    const WCHAR* new_file_name,
    const WCHAR* existing_file_name,
    void* security_attributes);
__declspec(dllimport) DWORD WINAPI GetTempPathW(
    DWORD buffer_length, WCHAR* buffer);

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

DlumaRuntime g_dluma;

static DlumaDualHostRuntimeSnapshot g_test_host_snapshot;

const DlumaDualHostRuntimeSnapshot* dluma_dual_host_runtime_snapshot(void)
{
    return &g_test_host_snapshot;
}

void dluma_log_pair(const char* level, const char* label, const char* value)
{
    (void)level;
    (void)label;
    (void)value;
}

/* Deterministically models a reparse component without requiring Developer
 * Mode or elevated symbolic-link privileges on the test machine. Every other
 * path delegates to the real filesystem API. */
static DWORD WINAPI test_get_file_attributes_w(const WCHAR* path)
{
    if (path && dluma_wcontains_icase(path, L"simulated_reparse"))
        return FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT;
    return GetFileAttributesW(path);
}

/* Expose this translation unit's private runtime helpers only inside this
 * standalone executable. The production source and public ABI remain
 * unchanged. */
#define GetFileAttributesW test_get_file_attributes_w
#define static
#include "../src/dlw_animation_refresh_runtime.c"
#undef static
#undef GetFileAttributesW

extern volatile LONG g_test_animation_pack_swap_calls;
extern DWORD g_test_animation_pack_swap_thread_id;
extern int g_test_animation_pack_swap_result;
extern unsigned int g_test_animation_pack_swap_rollback_attempted;
extern unsigned int g_test_animation_pack_swap_rollback_succeeded;
extern WCHAR g_test_animation_pack_swap_path[DLUMA_MAX_WPATH];
extern HANDLE g_test_animation_pack_swap_started_event;
extern HANDLE g_test_animation_pack_swap_continue_event;
extern volatile LONG g_test_animation_pack_shutdown_calls;
extern DWORD g_test_animation_pack_shutdown_thread_id;
extern int g_test_animation_pack_retained;

typedef struct TestPlayerDispatchContext {
    WCHAR runtime_pack[DLUMA_MAX_WPATH];
    DlumaAnimationRefreshResult result;
    ULONGLONG timeout_ms;
    int succeeded;
    DWORD worker_thread_id;
} TestPlayerDispatchContext;

typedef struct TestPlayerPumpContext {
    DWORD thread_id;
} TestPlayerPumpContext;

typedef struct TestPlayerShutdownContext {
    int succeeded;
    DWORD thread_id;
} TestPlayerShutdownContext;

static DWORD WINAPI test_player_dispatch_worker(void* raw_context)
{
    TestPlayerDispatchContext* context =
        (TestPlayerDispatchContext*)raw_context;
    if (!context) return 1U;
    context->worker_thread_id = GetCurrentThreadId();
    context->succeeded = dluma_animation_refresh_player_dispatch(
        context->runtime_pack, &context->result, context->timeout_ms);
    return 0U;
}

static DWORD WINAPI test_player_pump_worker(void* raw_context)
{
    TestPlayerPumpContext* context =
        (TestPlayerPumpContext*)raw_context;
    if (!context) return 1U;
    context->thread_id = GetCurrentThreadId();
    dluma_animation_refresh_runtime_player_game_thread_pump();
    return 0U;
}

static DWORD WINAPI test_player_shutdown_worker(void* raw_context)
{
    TestPlayerShutdownContext* context =
        (TestPlayerShutdownContext*)raw_context;
    if (!context) return 1U;
    context->thread_id = GetCurrentThreadId();
    context->succeeded =
        dluma_animation_refresh_runtime_player_game_thread_shutdown();
    return 0U;
}

static int test_wait_for_player_dispatch_state(
    LONG expected,
    DWORD timeout_ms)
{
    ULONGLONG started = GetTickCount64();
    while (GetTickCount64() - started < timeout_ms) {
        LONG state = dluma_atomic_compare_exchange(
            &g_dluma_animation_refresh_player_dispatch.state, 0, 0);
        if (state == expected) return 1;
        Sleep(1);
    }
    return 0;
}

static int test_make_directory(const WCHAR* path)
{
    return CreateDirectoryW(path, 0) || GetLastError() == 183UL;
}

static int test_join(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* root,
    const WCHAR* relative)
{
    return dluma_checked_path_join(output, capacity, root, relative);
}

static int test_write_file(
    const WCHAR* path,
    const void* data,
    DWORD size)
{
    HANDLE file = CreateFileW(
        path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    DWORD written = 0;
    int result;
    if (file == INVALID_HANDLE_VALUE) return 0;
    result = WriteFile(file, data, size, &written, 0) && written == size;
    CloseHandle(file);
    return result;
}

static int test_create_root(WCHAR root[DLUMA_MAX_WPATH])
{
    WCHAR temporary[DLUMA_MAX_WPATH];
    WCHAR name[128];
    WCHAR number_wide[32];
    char number[32];
    DWORD length = GetTempPathW(DLUMA_ARRAY_COUNT(temporary), temporary);
    if (!length || length >= DLUMA_ARRAY_COUNT(temporary)) return 0;
    dluma_wstr_copy(
        name, DLUMA_ARRAY_COUNT(name),
        L"dluma-animation-refresh-runtime-security-");
    dluma_u64_to_text(
        ((ULONGLONG)GetCurrentProcessId() << 32) ^ GetTickCount64(),
        number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(
            number, number_wide, DLUMA_ARRAY_COUNT(number_wide)))
        return 0;
    dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), number_wide);
    return test_join(root, DLUMA_MAX_WPATH, temporary, name) &&
        CreateDirectoryW(root, 0);
}

static int test_role_and_aggregate_bounds(void)
{
    CHECK(dluma_animation_refresh_artifact_maximum(
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT) == 1ULL * 1024ULL * 1024ULL);
    CHECK(dluma_animation_refresh_artifact_maximum(
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT) == 16ULL * 1024ULL * 1024ULL);
    CHECK(dluma_animation_refresh_artifact_maximum(
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION) == 256ULL * 1024ULL * 1024ULL);
    CHECK(dluma_animation_refresh_artifact_maximum(
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK) == 1024ULL * 1024ULL * 1024ULL);
    CHECK(dluma_animation_refresh_artifact_maximum(
        (DlumaAnimationRefreshArtifactRole)99) == 0);
    CHECK(DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES ==
        2ULL * 1024ULL * 1024ULL * 1024ULL);
    CHECK(DLUMA_ANIMATION_REFRESH_MAX_REQUESTS_PER_POLL == 128U);
    return 1;
}

static int test_reparse_and_hardlink_refusal(
    const WCHAR* root,
    WCHAR data_directory[DLUMA_MAX_WPATH],
    WCHAR regular_path[DLUMA_MAX_WPATH],
    WCHAR hardlink_path[DLUMA_MAX_WPATH],
    WCHAR safe_directory[DLUMA_MAX_WPATH],
    WCHAR leaf_directory[DLUMA_MAX_WPATH])
{
    WCHAR resolved[DLUMA_MAX_WPATH];
    DlumaAnimationRefreshOwnedBytes owned;
    static const char payload[] = "bounded animation payload";
    CHECK(test_join(
        data_directory, DLUMA_MAX_WPATH, root, L"data") &&
        test_make_directory(data_directory));
    CHECK(test_join(
        regular_path, DLUMA_MAX_WPATH, data_directory, L"clip.anm2") &&
        test_join(
            hardlink_path, DLUMA_MAX_WPATH, data_directory,
            L"clip-hardlink.anm2"));
    CHECK(test_write_file(
        regular_path, payload, (DWORD)(sizeof(payload) - 1U)));
    CHECK(CreateHardLinkW(hardlink_path, regular_path, 0));
    memset(&owned, 0, sizeof(owned));
    CHECK(!dluma_animation_refresh_read_owned(
        root, "data/clip.anm2", 1024U, &owned));
    CHECK(!owned.data && owned.size == 0U);
    CHECK(DeleteFileW(hardlink_path));
    CHECK(dluma_animation_refresh_read_owned(
        root, "data/clip.anm2", 1024U, &owned));
    CHECK(owned.size == sizeof(payload) - 1U);
    CHECK(owned.sha256[0] != 0);
    dluma_animation_refresh_free_owned(&owned);

    CHECK(test_join(
        safe_directory, DLUMA_MAX_WPATH, root, L"safe") &&
        test_make_directory(safe_directory));
    CHECK(test_join(
        leaf_directory, DLUMA_MAX_WPATH, safe_directory, L"leaf") &&
        test_make_directory(leaf_directory));
    CHECK(dluma_animation_refresh_no_reparse_components(
        root, "safe/leaf", 1, resolved, DLUMA_ARRAY_COUNT(resolved)));
    CHECK(!dluma_animation_refresh_no_reparse_components(
        root, "safe/simulated_reparse", 1,
        resolved, DLUMA_ARRAY_COUNT(resolved)));
    return 1;
}

static int test_exact_host_gate_and_defaults(void)
{
    int exact = -1;
    memset(&g_dluma, 0, sizeof(g_dluma));
    memset(&g_test_host_snapshot, 0, sizeof(g_test_host_snapshot));
    CHECK(!dluma_animation_refresh_configured_enabled());
    CHECK(dluma_animation_refresh_configured_route() ==
        DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK);

    g_test_host_snapshot.initialized = 1U;
    g_test_host_snapshot.classification.exact_profile = 1U;
    g_test_host_snapshot.evidence.canonical_image_verified = 1U;
    g_test_host_snapshot.classification.kind = DLUMA_HOST_CHROME_EDITOR;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_EDITOR && exact == 1);
    g_test_host_snapshot.classification.kind = DLUMA_HOST_PLAYER_STANDALONE;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_PLAYER && exact == 1);
    g_test_host_snapshot.classification.kind =
        DLUMA_HOST_PLAYER_EDITOR_PLAYTEST;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_PLAYER && exact == 1);
    g_test_host_snapshot.classification.forwarding_only = 1U;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED && exact == 0);
    g_test_host_snapshot.classification.forwarding_only = 0U;
    g_test_host_snapshot.evidence.canonical_image_verified = 0U;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED && exact == 0);
    g_test_host_snapshot.evidence.canonical_image_verified = 1U;
    g_test_host_snapshot.classification.kind = DLUMA_HOST_UNKNOWN;
    CHECK(dluma_animation_refresh_actual_host(&exact) ==
        DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED && exact == 0);
    return 1;
}

static int test_result_atomic_coalescing(
    const WCHAR* root,
    WCHAR dot_directory[DLUMA_MAX_WPATH],
    WCHAR refresh_directory[DLUMA_MAX_WPATH],
    WCHAR results_directory[DLUMA_MAX_WPATH],
    WCHAR result_path[DLUMA_MAX_WPATH])
{
    static const char request_id[] = "0123456789abcdef0123456789abcdef";
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest manifest;
    DlumaAnimationRefreshResult result;
    DlumaAnimationRefreshOwnedBytes first;
    DlumaAnimationRefreshOwnedBytes second;
    WCHAR result_name[64];
    WCHAR request_wide[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char first_sha[DLUMA_SHA256_HEX_CAPACITY];
    int published = 0;
    memset(&request, 0, sizeof(request));
    memset(&manifest, 0, sizeof(manifest));
    memset(&result, 0, sizeof(result));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(first_sha, 0, sizeof(first_sha));

    CHECK(test_join(
        dot_directory, DLUMA_MAX_WPATH, root, L".dl-reanimated") &&
        test_make_directory(dot_directory));
    CHECK(test_join(
        refresh_directory, DLUMA_MAX_WPATH, dot_directory,
        L"animation-refresh") && test_make_directory(refresh_directory));
    CHECK(test_join(
        results_directory, DLUMA_MAX_WPATH, refresh_directory,
        L"results") && test_make_directory(results_directory));
    CHECK(dluma_utf8_to_wide(
        request_id, request_wide, DLUMA_ARRAY_COUNT(request_wide)));
    dluma_wstr_copy(result_name, DLUMA_ARRAY_COUNT(result_name), request_wide);
    dluma_wstr_append(result_name, DLUMA_ARRAY_COUNT(result_name), L".json");
    CHECK(test_join(
        result_path, DLUMA_MAX_WPATH, results_directory, result_name));

    dluma_str_copy(
        request.deployment_id, DLUMA_ARRAY_COUNT(request.deployment_id),
        "0123456789abcdef01234567");
    memset(request.manifest_sha256, 'a', 64U);
    request.manifest_sha256[64] = 0;
    request.route = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    manifest.artifact_count = 2U;
    dluma_str_copy(
        manifest.artifacts[0].relative_path,
        DLUMA_ARRAY_COUNT(manifest.artifacts[0].relative_path),
        "data/characters/sample/sample.ascr");
    dluma_str_copy(
        manifest.artifacts[1].relative_path,
        DLUMA_ARRAY_COUNT(manifest.artifacts[1].relative_path),
        "data/characters/animations/sample.anm2");
    result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_DISABLED;
    result.filesystem_refresh = DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED;
    CHECK(dluma_animation_refresh_write_result(
        root, request_id, &request,
        DLUMA_ANIMATION_REFRESH_HOST_EDITOR, &manifest, &result,
        &published));
    CHECK(published);
    CHECK(dluma_project_regular_file_is_contained(root, result_path));
    CHECK(dluma_animation_refresh_read_owned(
        root,
        ".dl-reanimated/animation-refresh/results/"
        "0123456789abcdef0123456789abcdef.json",
        DLUMA_ANIMATION_REFRESH_RESULT_MAX_BYTES, &first));
    CHECK(strstr(first.data, "\"status\":\"Refused\"") != 0);
    CHECK(strstr(first.data, "\"hostIdentity\":\"Editor\"") != 0);
    CHECK(strstr(first.data, "\"resolvedFileCount\":2") != 0);
    CHECK(strstr(first.data, "\"livePlaybackProven\":false") != 0);
    dluma_str_copy(first_sha, DLUMA_ARRAY_COUNT(first_sha), first.sha256);

    /* A racing duplicate request must coalesce to the already-published
     * immutable result, never replace it. */
    result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_RELOAD_FAILED;
    published = -1;
    CHECK(dluma_animation_refresh_write_result(
        root, request_id, &request,
        DLUMA_ANIMATION_REFRESH_HOST_PLAYER, &manifest, &result,
        &published));
    CHECK(!published);
    CHECK(dluma_animation_refresh_read_owned(
        root,
        ".dl-reanimated/animation-refresh/results/"
        "0123456789abcdef0123456789abcdef.json",
        DLUMA_ANIMATION_REFRESH_RESULT_MAX_BYTES, &second));
    CHECK(dluma_stricmp(first_sha, second.sha256) == 0);
    CHECK(strstr(second.data, "\"hostIdentity\":\"Editor\"") != 0);
    CHECK(strstr(second.data, "reload_failed") == 0);
    dluma_animation_refresh_free_owned(&first);
    dluma_animation_refresh_free_owned(&second);
    return 1;
}

static int test_deterministic_coalescing_marker(
    const WCHAR* root,
    WCHAR marker_path[DLUMA_MAX_WPATH])
{
    DlumaAnimationRefreshRequest first;
    DlumaAnimationRefreshRequest same_key;
    DlumaAnimationRefreshRequest changed;
    char relative[256];
    memset(&first, 0, sizeof(first));
    memset(&same_key, 0, sizeof(same_key));
    memset(&changed, 0, sizeof(changed));
    memset(relative, 0, sizeof(relative));
    dluma_str_copy(
        first.request_id, DLUMA_ARRAY_COUNT(first.request_id),
        "11111111111111111111111111111111");
    dluma_str_copy(
        first.deployment_id, DLUMA_ARRAY_COUNT(first.deployment_id),
        "0123456789abcdef01234567");
    memset(first.manifest_sha256, 'a', 64U);
    first.manifest_sha256[64] = 0;
    first.target_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    first.route = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    CHECK(dluma_animation_refresh_coalesce_relative(
        &first, relative, DLUMA_ARRAY_COUNT(relative)));
    CHECK(dluma_animation_refresh_join_relative(
        root, relative, marker_path, DLUMA_MAX_WPATH));
    CHECK(dluma_animation_refresh_coalesce_state(root, &first) == 0);
    CHECK(dluma_animation_refresh_write_coalesce_marker(root, &first));
    CHECK(dluma_project_regular_file_is_contained(root, marker_path));
    CHECK(dluma_animation_refresh_coalesce_state(root, &first) == 1);

    same_key = first;
    dluma_str_copy(
        same_key.request_id, DLUMA_ARRAY_COUNT(same_key.request_id),
        "22222222222222222222222222222222");
    CHECK(dluma_animation_refresh_coalesce_state(root, &same_key) == 2);
    /* A racing request with the same deterministic key observes the existing
     * immutable marker and cannot replace its original request identity. */
    CHECK(dluma_animation_refresh_write_coalesce_marker(root, &same_key));
    CHECK(dluma_animation_refresh_coalesce_state(root, &first) == 1);
    CHECK(dluma_animation_refresh_coalesce_state(root, &same_key) == 2);

    changed = first;
    changed.route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    CHECK(dluma_animation_refresh_coalesce_state(root, &changed) == 0);
    changed = first;
    changed.manifest_sha256[0] = 'b';
    CHECK(dluma_animation_refresh_coalesce_state(root, &changed) == 0);
    return 1;
}

static int test_only_accepted_results_coalesce(void)
{
    DlumaAnimationRefreshResult result;
    memset(&result, 0, sizeof(result));
    CHECK(!dluma_animation_refresh_should_write_coalesce_marker(0, &result));
    CHECK(!dluma_animation_refresh_should_write_coalesce_marker(1, &result));
    result.accepted = 1U;
    CHECK(!dluma_animation_refresh_should_write_coalesce_marker(0, &result));
    CHECK(dluma_animation_refresh_should_write_coalesce_marker(1, &result));
    CHECK(!dluma_animation_refresh_should_write_coalesce_marker(1, 0));
    return 1;
}

static void test_reset_player_dispatch(void)
{
    memset(&g_dluma_animation_refresh_player_dispatch, 0,
        sizeof(g_dluma_animation_refresh_player_dispatch));
    g_dluma_animation_refresh_player_owner_thread_id = 0;
    g_test_animation_pack_swap_calls = 0;
    g_test_animation_pack_swap_thread_id = 0;
    g_test_animation_pack_swap_result = 0;
    g_test_animation_pack_swap_rollback_attempted = 0U;
    g_test_animation_pack_swap_rollback_succeeded = 0U;
    memset(g_test_animation_pack_swap_path, 0,
        sizeof(g_test_animation_pack_swap_path));
    g_test_animation_pack_swap_started_event = 0;
    g_test_animation_pack_swap_continue_event = 0;
    g_test_animation_pack_shutdown_calls = 0;
    g_test_animation_pack_shutdown_thread_id = 0;
    g_test_animation_pack_retained = 0;
}

static int test_player_pack_swap_runs_only_on_game_thread_pump(void)
{
    static const WCHAR original_pack[] =
        L"C:\\synthetic-project\\runtime\\animations.rpack";
    static const WCHAR overwritten_source[] =
        L"C:\\synthetic-project\\runtime\\overwritten.rpack";
    TestPlayerDispatchContext context;
    DlumaAnimationRefreshResult competing_result;
    DlumaAnimationRefreshResult timeout_result;
    HANDLE worker;
    DWORD pump_thread_id = GetCurrentThreadId();

    test_reset_player_dispatch();
    memset(&context, 0, sizeof(context));
    memset(&competing_result, 0, sizeof(competing_result));
    memset(&timeout_result, 0, sizeof(timeout_result));
    g_test_animation_pack_swap_result = 1;
    dluma_wstr_copy(
        context.runtime_pack, DLUMA_ARRAY_COUNT(context.runtime_pack),
        original_pack);
    context.timeout_ms = 2000ULL;
    worker = CreateThread(
        0, 0, test_player_dispatch_worker, &context, 0, 0);
    CHECK(worker != 0);
    CHECK(test_wait_for_player_dispatch_state(
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING, 2000U));

    /* The single bounded slot rejects a second producer without replacing
     * the already-published request. The source can change after publication
     * because the queue owns its deep copy. */
    CHECK(!dluma_animation_refresh_player_dispatch(
        overwritten_source, &competing_result, 100ULL));
    CHECK(competing_result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    dluma_wstr_copy(
        context.runtime_pack, DLUMA_ARRAY_COUNT(context.runtime_pack),
        overwritten_source);
    dluma_animation_refresh_runtime_player_game_thread_pump();
    CHECK(WaitForSingleObject(worker, 2000U) == WAIT_OBJECT_0);
    CHECK(CloseHandle(worker));
    CHECK(context.succeeded);
    CHECK(context.worker_thread_id != pump_thread_id);
    CHECK(g_test_animation_pack_swap_calls == 1);
    CHECK(g_test_animation_pack_swap_thread_id == pump_thread_id);
    CHECK(g_test_animation_pack_swap_thread_id != context.worker_thread_id);
    CHECK(dluma_wstricmp(g_test_animation_pack_swap_path, original_pack) == 0);
    CHECK(context.result.pack_registration ==
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(context.result.fallback_mount ==
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(context.result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_NONE);
    CHECK(g_dluma_animation_refresh_player_dispatch.state ==
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE);

    /* A failed swap is returned to the waiting worker with rollback detail. */
    memset(&context, 0, sizeof(context));
    dluma_wstr_copy(
        context.runtime_pack, DLUMA_ARRAY_COUNT(context.runtime_pack),
        original_pack);
    context.timeout_ms = 2000ULL;
    g_test_animation_pack_swap_result = 0;
    g_test_animation_pack_swap_rollback_attempted = 1U;
    g_test_animation_pack_swap_rollback_succeeded = 1U;
    worker = CreateThread(
        0, 0, test_player_dispatch_worker, &context, 0, 0);
    CHECK(worker != 0);
    CHECK(test_wait_for_player_dispatch_state(
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING, 2000U));
    dluma_animation_refresh_runtime_player_game_thread_pump();
    CHECK(WaitForSingleObject(worker, 2000U) == WAIT_OBJECT_0);
    CHECK(CloseHandle(worker));
    CHECK(!context.succeeded);
    CHECK(g_test_animation_pack_swap_calls == 2);
    CHECK(context.result.pack_registration ==
        DLUMA_ANIMATION_REFRESH_STAGE_FAILED);
    CHECK(context.result.rollback == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(context.result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED);

    /* Without a game-thread consumer, a pending request is cancelled after
     * its bounded wait and cannot execute later. */
    CHECK(!dluma_animation_refresh_player_dispatch(
        original_pack, &timeout_result, 1ULL));
    CHECK(timeout_result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    CHECK(g_dluma_animation_refresh_player_dispatch.state ==
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE);
    CHECK(g_test_animation_pack_swap_calls == 2);
    return 1;
}

static int test_running_player_dispatch_timeout_is_acknowledged(void)
{
    static const WCHAR runtime_pack[] =
        L"C:\\synthetic-project\\runtime\\delayed.rpack";
    TestPlayerDispatchContext dispatch_context;
    TestPlayerPumpContext pump_context;
    DlumaAnimationRefreshResult competing_result;
    HANDLE dispatch_worker;
    HANDLE pump_worker;

    test_reset_player_dispatch();
    memset(&dispatch_context, 0, sizeof(dispatch_context));
    memset(&pump_context, 0, sizeof(pump_context));
    memset(&competing_result, 0, sizeof(competing_result));
    g_test_animation_pack_swap_result = 1;
    g_test_animation_pack_swap_started_event =
        CreateEventW(0, TRUE, FALSE, 0);
    g_test_animation_pack_swap_continue_event =
        CreateEventW(0, TRUE, FALSE, 0);
    CHECK(g_test_animation_pack_swap_started_event != 0);
    CHECK(g_test_animation_pack_swap_continue_event != 0);
    dluma_wstr_copy(
        dispatch_context.runtime_pack,
        DLUMA_ARRAY_COUNT(dispatch_context.runtime_pack),
        runtime_pack);
    dispatch_context.timeout_ms = 50ULL;
    dispatch_worker = CreateThread(
        0, 0, test_player_dispatch_worker, &dispatch_context, 0, 0);
    CHECK(dispatch_worker != 0);
    CHECK(test_wait_for_player_dispatch_state(
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING, 2000U));
    pump_worker = CreateThread(
        0, 0, test_player_pump_worker, &pump_context, 0, 0);
    CHECK(pump_worker != 0);
    CHECK(WaitForSingleObject(
        g_test_animation_pack_swap_started_event, 2000U) ==
        WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(dispatch_worker, 2000U) == WAIT_OBJECT_0);
    CHECK(!dispatch_context.succeeded);
    CHECK(dispatch_context.result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    CHECK(g_dluma_animation_refresh_player_dispatch.state ==
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED);
    CHECK(!dluma_animation_refresh_player_dispatch(
        runtime_pack, &competing_result, 100ULL));
    CHECK(g_dluma_animation_refresh_player_dispatch.state ==
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED);
    CHECK(SetEvent(g_test_animation_pack_swap_continue_event));
    CHECK(WaitForSingleObject(pump_worker, 2000U) == WAIT_OBJECT_0);
    CHECK(g_test_animation_pack_swap_calls == 1);
    CHECK(g_test_animation_pack_swap_thread_id == pump_context.thread_id);
    CHECK(g_dluma_animation_refresh_player_dispatch.state ==
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE);
    CHECK(CloseHandle(dispatch_worker));
    CHECK(CloseHandle(pump_worker));
    CHECK(CloseHandle(g_test_animation_pack_swap_started_event));
    CHECK(CloseHandle(g_test_animation_pack_swap_continue_event));
    g_test_animation_pack_swap_started_event = 0;
    g_test_animation_pack_swap_continue_event = 0;
    return 1;
}

static int test_player_pack_shutdown_requires_owner_thread(void)
{
    TestPlayerShutdownContext context;
    HANDLE worker;
    DWORD owner_thread_id = GetCurrentThreadId();

    test_reset_player_dispatch();
    memset(&context, 0, sizeof(context));
    g_test_animation_pack_retained = 1;
    CHECK(!dluma_animation_refresh_runtime_player_game_thread_shutdown());
    CHECK(g_test_animation_pack_shutdown_calls == 0);
    dluma_animation_refresh_runtime_player_game_thread_pump();
    CHECK((DWORD)g_dluma_animation_refresh_player_owner_thread_id ==
        owner_thread_id);
    worker = CreateThread(
        0, 0, test_player_shutdown_worker, &context, 0, 0);
    CHECK(worker != 0);
    CHECK(WaitForSingleObject(worker, 2000U) == WAIT_OBJECT_0);
    CHECK(CloseHandle(worker));
    CHECK(!context.succeeded);
    CHECK(context.thread_id != owner_thread_id);
    CHECK(g_test_animation_pack_shutdown_calls == 0);
    g_dluma_animation_refresh_player_dispatch.state =
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING;
    CHECK(!dluma_animation_refresh_runtime_player_game_thread_shutdown());
    CHECK(g_test_animation_pack_shutdown_calls == 0);
    g_dluma_animation_refresh_player_dispatch.state =
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE;
    CHECK(dluma_animation_refresh_runtime_player_game_thread_shutdown());
    CHECK(g_test_animation_pack_shutdown_calls == 1);
    CHECK(g_test_animation_pack_shutdown_thread_id == owner_thread_id);
    CHECK(!g_test_animation_pack_retained);
    return 1;
}

static int test_request_filename_gate(void)
{
    char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    CHECK(dluma_animation_refresh_request_name(
        L"0123456789abcdef0123456789abcdef.json", request_id));
    CHECK(dluma_stricmp(
        request_id, "0123456789abcdef0123456789abcdef") == 0);
    CHECK(!dluma_animation_refresh_request_name(
        L"0123456789ABCDEF0123456789ABCDEF.json", request_id));
    CHECK(!dluma_animation_refresh_request_name(
        L"../0123456789abcdef0123456789abcdef.json", request_id));
    CHECK(!dluma_animation_refresh_request_name(L"short.json", request_id));
    return 1;
}

int main(void)
{
    WCHAR root[DLUMA_MAX_WPATH];
    WCHAR data_directory[DLUMA_MAX_WPATH];
    WCHAR regular_path[DLUMA_MAX_WPATH];
    WCHAR hardlink_path[DLUMA_MAX_WPATH];
    WCHAR safe_directory[DLUMA_MAX_WPATH];
    WCHAR leaf_directory[DLUMA_MAX_WPATH];
    WCHAR dot_directory[DLUMA_MAX_WPATH];
    WCHAR refresh_directory[DLUMA_MAX_WPATH];
    WCHAR results_directory[DLUMA_MAX_WPATH];
    WCHAR result_path[DLUMA_MAX_WPATH];
    WCHAR coalesce_marker_path[DLUMA_MAX_WPATH];
    int passed = 0;
    memset(root, 0, sizeof(root));
    memset(data_directory, 0, sizeof(data_directory));
    memset(regular_path, 0, sizeof(regular_path));
    memset(hardlink_path, 0, sizeof(hardlink_path));
    memset(safe_directory, 0, sizeof(safe_directory));
    memset(leaf_directory, 0, sizeof(leaf_directory));
    memset(dot_directory, 0, sizeof(dot_directory));
    memset(refresh_directory, 0, sizeof(refresh_directory));
    memset(results_directory, 0, sizeof(results_directory));
    memset(result_path, 0, sizeof(result_path));
    memset(coalesce_marker_path, 0, sizeof(coalesce_marker_path));

    if (test_role_and_aggregate_bounds()) {
        puts("[PASS] role and aggregate bounds");
        ++passed;
    }
    if (!test_create_root(root)) {
        printf("[FAIL] temporary root creation: %lu (%ls)\n", GetLastError(), root);
        return 1;
    }
    if (test_reparse_and_hardlink_refusal(
            root, data_directory, regular_path, hardlink_path,
            safe_directory, leaf_directory)) {
        puts("[PASS] reparse and hardlink refusal");
        ++passed;
    }
    if (test_exact_host_gate_and_defaults()) {
        puts("[PASS] exact host gate and defaults");
        ++passed;
    }
    if (test_result_atomic_coalescing(
            root, dot_directory, refresh_directory,
            results_directory, result_path)) {
        puts("[PASS] result atomic coalescing");
        ++passed;
    }
    if (test_deterministic_coalescing_marker(
            root, coalesce_marker_path)) {
        puts("[PASS] deterministic coalescing marker");
        ++passed;
    }
    if (test_only_accepted_results_coalesce()) {
        puts("[PASS] only accepted results coalesce");
        ++passed;
    }
    if (test_player_pack_swap_runs_only_on_game_thread_pump()) {
        puts("[PASS] player pack swap uses game-thread pump");
        ++passed;
    }
    if (test_running_player_dispatch_timeout_is_acknowledged()) {
        puts("[PASS] running player dispatch timeout is acknowledged");
        ++passed;
    }
    if (test_player_pack_shutdown_requires_owner_thread()) {
        puts("[PASS] player pack shutdown requires owner thread");
        ++passed;
    }
    if (test_request_filename_gate()) {
        puts("[PASS] request filename gate");
        ++passed;
    }

    if (result_path[0]) DeleteFileW(result_path);
    if (coalesce_marker_path[0]) DeleteFileW(coalesce_marker_path);
    if (regular_path[0]) DeleteFileW(regular_path);
    if (hardlink_path[0]) DeleteFileW(hardlink_path);
    if (results_directory[0]) RemoveDirectoryW(results_directory);
    if (refresh_directory[0]) RemoveDirectoryW(refresh_directory);
    if (dot_directory[0]) RemoveDirectoryW(dot_directory);
    if (leaf_directory[0]) RemoveDirectoryW(leaf_directory);
    if (safe_directory[0]) RemoveDirectoryW(safe_directory);
    if (data_directory[0]) RemoveDirectoryW(data_directory);
    if (root[0]) RemoveDirectoryW(root);
    printf("Animation refresh runtime security cases passed: %d\n", passed);
    return passed == 10 ? 0 : 1;
}
