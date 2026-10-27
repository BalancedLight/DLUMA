#include "dlw_animation_refresh_runtime.h"

#include "core/dlw_animation_refresh.h"
#include "dlw_animation_pack_runtime.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_animation_bridge.h"
#include "dlw_log.h"
#include "dlw_project_owner_identity.h"

#include <string.h>

#define DLUMA_ANIMATION_REFRESH_RESULT_MAX_BYTES (32U * 1024U)
#define DLUMA_ANIMATION_REFRESH_MAX_REQUESTS_PER_POLL 128U
#define DLUMA_ANIMATION_REFRESH_POLL_INTERVAL_MS 500ULL
#define DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_TIMEOUT_MS 5000ULL
#define DLUMA_ANIMATION_REFRESH_ALIAS_MAX_BYTES (1ULL * 1024ULL * 1024ULL)
#define DLUMA_ANIMATION_REFRESH_SCRIPT_MAX_BYTES (16ULL * 1024ULL * 1024ULL)
#define DLUMA_ANIMATION_REFRESH_ANIMATION_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
#define DLUMA_ANIMATION_REFRESH_RPACK_MAX_BYTES (1024ULL * 1024ULL * 1024ULL)
#define DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES \
    (2ULL * 1024ULL * 1024ULL * 1024ULL)

typedef struct DlumaAnimationRefreshOwnedBytes {
    char* data;
    DWORD size;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
} DlumaAnimationRefreshOwnedBytes;

typedef struct DlumaAnimationRefreshRuntimeOperationsContext {
    const DlumaAnimationRefreshManifest* manifest;
    WCHAR project_root[DLUMA_MAX_WPATH];
    char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
} DlumaAnimationRefreshRuntimeOperationsContext;

typedef struct DlumaAnimationRefreshPlayerDispatch {
    volatile LONG state;
    DlumaAnimationRefreshRoute route;
    WCHAR runtime_pack[DLUMA_MAX_WPATH];
    DlumaAnimationRefreshResult result;
    unsigned int operation_succeeded;
    DWORD execution_thread_id;
} DlumaAnimationRefreshPlayerDispatch;

enum {
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE = 0,
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_WRITING = -1,
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING = 1,
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_RUNNING = 2,
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_COMPLETED = 3,
    DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED = 4
};

static volatile LONG g_dluma_animation_refresh_poll_lock;
static volatile LONG g_dluma_animation_refresh_player_owner_thread_id;
static ULONGLONG g_dluma_animation_refresh_last_poll_ms;
static DlumaAnimationRefreshPlayerDispatch
    g_dluma_animation_refresh_player_dispatch;


static int dluma_animation_refresh_info_equal(
    const BY_HANDLE_FILE_INFORMATION* left,
    const BY_HANDLE_FILE_INFORMATION* right)
{
    return left && right &&
        left->dwVolumeSerialNumber == right->dwVolumeSerialNumber &&
        left->nNumberOfLinks == right->nNumberOfLinks &&
        left->nFileIndexHigh == right->nFileIndexHigh &&
        left->nFileIndexLow == right->nFileIndexLow &&
        left->nFileSizeHigh == right->nFileSizeHigh &&
        left->nFileSizeLow == right->nFileSizeLow &&
        left->ftLastWriteTime.dwHighDateTime ==
            right->ftLastWriteTime.dwHighDateTime &&
        left->ftLastWriteTime.dwLowDateTime ==
            right->ftLastWriteTime.dwLowDateTime;
}

static int dluma_animation_refresh_join_relative(
    const WCHAR* root,
    const char* relative,
    WCHAR* output,
    unsigned int capacity)
{
    WCHAR wide[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    unsigned int index;
    if (!root || !root[0] ||
        !dluma_animation_refresh_relative_path_is_canonical(relative) ||
        !dluma_utf8_to_wide(relative, wide, DLUMA_ARRAY_COUNT(wide)))
        return 0;
    for (index = 0; wide[index]; ++index)
        if (wide[index] == L'/') wide[index] = L'\\';
    return dluma_checked_path_join(output, capacity, root, wide);
}

static int dluma_animation_refresh_runtime_pack_path(
    const DlumaAnimationRefreshRuntimeOperationsContext* runtime,
    WCHAR* output,
    unsigned int capacity)
{
    unsigned int index;
    unsigned int found = 0;
    if (!runtime || !runtime->manifest || !output || !capacity)
        return 0;
    output[0] = 0;
    for (index = 0;
         index < runtime->manifest->artifact_count;
         ++index) {
        const DlumaAnimationRefreshArtifact* artifact =
            &runtime->manifest->artifacts[index];
        if (artifact->role !=
            DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK)
            continue;
        if (found++ ||
            !dluma_animation_refresh_join_relative(
                runtime->project_root,
                artifact->relative_path,
                output,
                capacity))
            return 0;
    }
    return found == 1U && output[0] != 0;
}

static int dluma_animation_refresh_editor_selected_model(
    void* context,
    DlumaAnimationRefreshRoute route,
    DlumaAnimationRefreshResult* result)
{
    DlumaAnimationRefreshRuntimeOperationsContext* runtime =
        (DlumaAnimationRefreshRuntimeOperationsContext*)context;
    DlumaEditorAnimationReloadRequest request;
    if (!runtime || !runtime->manifest || !result) return 0;
    memset(&request, 0, sizeof(request));
    request.route = route;
    request.manifest = runtime->manifest;
    dluma_wstr_copy(
        request.project_root,
        DLUMA_ARRAY_COUNT(request.project_root),
        runtime->project_root);
    dluma_str_copy(
        request.request_id,
        DLUMA_ARRAY_COUNT(request.request_id),
        runtime->request_id);
    if (route == DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK &&
        !dluma_animation_refresh_runtime_pack_path(
            runtime,
            request.runtime_pack_path,
            DLUMA_ARRAY_COUNT(request.runtime_pack_path))) {
        result->pack_registration =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result->fallback_mount =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result->refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
        return 0;
    }
    return dluma_editor_animation_bridge_dispatch(&request, result);
}

static int dluma_animation_refresh_player_claim_owner_thread(void)
{
    LONG current = (LONG)GetCurrentThreadId();
    LONG owner = dluma_atomic_compare_exchange(
        &g_dluma_animation_refresh_player_owner_thread_id, 0, 0);
    if (!owner) {
        LONG previous = dluma_atomic_compare_exchange(
            &g_dluma_animation_refresh_player_owner_thread_id,
            current, 0);
        owner = previous ? previous : current;
    }
    return owner == current;
}

static int dluma_animation_refresh_player_dispatch(
    const WCHAR* runtime_pack,
    DlumaAnimationRefreshResult* result,
    ULONGLONG timeout_ms)
{
    DlumaAnimationRefreshPlayerDispatch* dispatch =
        &g_dluma_animation_refresh_player_dispatch;
    ULONGLONG started;
    LONG state;
    int succeeded;
    if (!runtime_pack || !runtime_pack[0] || !result || !timeout_ms)
        return 0;
    if (dluma_atomic_compare_exchange(
            &dispatch->state,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_WRITING,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE) !=
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE) {
        result->primary_refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
        result->refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
        return 0;
    }
    dispatch->route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    dluma_wstr_copy(
        dispatch->runtime_pack,
        DLUMA_ARRAY_COUNT(dispatch->runtime_pack),
        runtime_pack);
    dispatch->result = *result;
    dispatch->operation_succeeded = 0U;
    dispatch->execution_thread_id = 0;
    dluma_atomic_exchange(
        &dispatch->state,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING);
    started = GetTickCount64();
    for (;;) {
        state = dluma_atomic_compare_exchange(&dispatch->state, 0, 0);
        if (state == DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_COMPLETED)
            goto completed;
        if (GetTickCount64() - started >= timeout_ms) break;
        Sleep(1);
    }
    state = dluma_atomic_compare_exchange(
        &dispatch->state,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING);
    if (state == DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_RUNNING)
        state = dluma_atomic_compare_exchange(
            &dispatch->state,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_RUNNING);
    if (state == DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_COMPLETED ||
        dluma_atomic_compare_exchange(&dispatch->state, 0, 0) ==
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_COMPLETED)
        goto completed;
    result->primary_refusal =
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
    result->refusal =
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
    return 0;
completed:
    succeeded = dispatch->operation_succeeded != 0U;
    *result = dispatch->result;
    dluma_atomic_exchange(
        &dispatch->state,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE);
    return succeeded;
}

void dluma_animation_refresh_runtime_player_game_thread_pump(void)
{
    DlumaAnimationRefreshPlayerDispatch* dispatch =
        &g_dluma_animation_refresh_player_dispatch;
    DlumaAnimationRefreshResult result;
    DlumaAnimationPackSwap swap;
    WCHAR runtime_pack[DLUMA_MAX_WPATH];
    LONG publication;
    int succeeded = 0;
    if (!dluma_animation_refresh_player_claim_owner_thread()) return;
    if (dluma_atomic_compare_exchange(
            &dispatch->state,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_RUNNING,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING) !=
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_PENDING)
        return;
    result = dispatch->result;
    dluma_wstr_copy(
        runtime_pack, DLUMA_ARRAY_COUNT(runtime_pack),
        dispatch->runtime_pack);
    memset(&swap, 0, sizeof(swap));
    dispatch->execution_thread_id = GetCurrentThreadId();
    if (dispatch->route ==
            DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK &&
        runtime_pack[0] &&
        dluma_animation_pack_runtime_swap(runtime_pack, &swap)) {
        result.pack_registration =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
        result.fallback_mount =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
        result.primary_refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
        succeeded = 1;
    } else {
        result.pack_registration =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result.fallback_mount =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result.rollback = swap.rollback_attempted
            ? (swap.rollback_succeeded
                ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
                : DLUMA_ANIMATION_REFRESH_STAGE_FAILED)
            : DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED;
        result.primary_refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
        result.refusal = swap.rollback_attempted &&
            !swap.rollback_succeeded
            ? DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED
            : DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
    }
    dispatch->result = result;
    dispatch->operation_succeeded = succeeded ? 1U : 0U;
    publication = dluma_atomic_compare_exchange(
        &dispatch->state,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_COMPLETED,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_RUNNING);
    if (publication == DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED) {
        dispatch->runtime_pack[0] = 0;
        (void)dluma_atomic_compare_exchange(
            &dispatch->state,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE,
            DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_ABANDONED);
    }
}

int dluma_animation_refresh_runtime_player_game_thread_shutdown(void)
{
    LONG owner;
    LONG state;
    if (!dluma_animation_pack_runtime_retained()) return 1;
    owner = dluma_atomic_compare_exchange(
        &g_dluma_animation_refresh_player_owner_thread_id, 0, 0);
    if (!owner || owner != (LONG)GetCurrentThreadId()) return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_animation_refresh_player_dispatch.state, 0, 0);
    if (state != DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_FREE) return 0;
    dluma_animation_pack_runtime_shutdown();
    return !dluma_animation_pack_runtime_retained();
}

static int dluma_animation_refresh_player_runtime_pack(
    void* context,
    DlumaAnimationRefreshRoute route,
    DlumaAnimationRefreshResult* result)
{
    DlumaAnimationRefreshRuntimeOperationsContext* runtime =
        (DlumaAnimationRefreshRuntimeOperationsContext*)context;
    WCHAR runtime_pack[DLUMA_MAX_WPATH];
    if (!runtime || !runtime->manifest || !result ||
        route != DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK) {
        if (result) {
            result->primary_refusal =
                DLUMA_ANIMATION_REFRESH_REFUSAL_RAW_LOOSE_PLAYER_UNSUPPORTED;
            result->refusal =
                DLUMA_ANIMATION_REFRESH_REFUSAL_RAW_LOOSE_PLAYER_UNSUPPORTED;
        }
        return 0;
    }
    memset(runtime_pack, 0, sizeof(runtime_pack));
    if (!dluma_animation_refresh_runtime_pack_path(
            runtime, runtime_pack, DLUMA_ARRAY_COUNT(runtime_pack))) {
        result->primary_refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
        result->refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
        return 0;
    }
    return dluma_animation_refresh_player_dispatch(
        runtime_pack, result,
        DLUMA_ANIMATION_REFRESH_PLAYER_DISPATCH_TIMEOUT_MS);
}

static int dluma_animation_refresh_no_reparse_components(
    const WCHAR* root,
    const char* relative,
    int final_is_directory,
    WCHAR* output,
    unsigned int capacity)
{
    WCHAR wide[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    WCHAR current[DLUMA_MAX_WPATH];
    WCHAR segment[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    unsigned int source = 0;
    if (!root || !relative || !output ||
        !dluma_animation_refresh_relative_path_is_canonical(relative) ||
        !dluma_utf8_to_wide(relative, wide, DLUMA_ARRAY_COUNT(wide)))
        return 0;
    dluma_wstr_copy(current, DLUMA_ARRAY_COUNT(current), root);
    while (wide[source]) {
        unsigned int length = 0;
        DWORD attributes;
        while (wide[source] && wide[source] != L'/') {
            if (length + 1U >= DLUMA_ARRAY_COUNT(segment)) return 0;
            segment[length++] = wide[source++];
        }
        segment[length] = 0;
        if (wide[source] == L'/') ++source;
        if (!dluma_checked_path_join(
                current, DLUMA_ARRAY_COUNT(current), current, segment))
            return 0;
        attributes = GetFileAttributesW(current);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (wide[source] && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) ||
            (!wide[source] && final_is_directory &&
             !(attributes & FILE_ATTRIBUTE_DIRECTORY)) ||
            (!wide[source] && !final_is_directory &&
             (attributes & FILE_ATTRIBUTE_DIRECTORY)))
            return 0;
    }
    dluma_wstr_copy(output, capacity, current);
    return output[0] != 0;
}

static int dluma_animation_refresh_safe_directory(
    const WCHAR* root,
    const char* relative,
    WCHAR* output,
    unsigned int capacity)
{
    DlumaDirectoryIdentity root_identity;
    DlumaDirectoryIdentity directory_identity;
    return dluma_open_directory_identity(root, &root_identity) &&
        dluma_animation_refresh_no_reparse_components(
            root, relative, 1, output, capacity) &&
        dluma_open_directory_identity(output, &directory_identity) &&
        dluma_final_path_equal_or_under(
            directory_identity.final_path, root_identity.final_path);
}

static HANDLE dluma_animation_refresh_lock_safe_directory(
    const WCHAR* root,
    const char* relative,
    WCHAR* output,
    unsigned int capacity)
{
    DlumaDirectoryIdentity root_identity;
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION information;
    HANDLE directory;
    DWORD final_length;
    memset(&information, 0, sizeof(information));
    if (!dluma_animation_refresh_safe_directory(
            root, relative, output, capacity) ||
        !dluma_open_directory_identity(root, &root_identity))
        return INVALID_HANDLE_VALUE;
    directory = CreateFileW(
        output, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    final_length = GetFinalPathNameByHandleW(
        directory, final_path, DLUMA_ARRAY_COUNT(final_path), 0);
    if (!final_length || final_length >= DLUMA_ARRAY_COUNT(final_path) ||
        !GetFileInformationByHandle(directory, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_final_path_equal_or_under(
            final_path, root_identity.final_path)) {
        CloseHandle(directory);
        return INVALID_HANDLE_VALUE;
    }
    return directory;
}
static int dluma_animation_refresh_read_owned(
    const WCHAR* root,
    const char* relative,
    DWORD maximum,
    DlumaAnimationRefreshOwnedBytes* output)
{
    WCHAR lexical[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity root_identity;
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    DWORD final_length;
    DWORD read = 0;
    int result = 0;
    if (!root || !output || !maximum) return 0;
    memset(output, 0, sizeof(*output));
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (!dluma_open_directory_identity(root, &root_identity) ||
        !dluma_animation_refresh_no_reparse_components(
            root, relative, 0, lexical, DLUMA_ARRAY_COUNT(lexical)))
        return 0;
    file = CreateFileW(
        lexical, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    final_length = GetFinalPathNameByHandleW(
        file, final_path, DLUMA_ARRAY_COUNT(final_path), 0);
    if (!final_length || final_length >= DLUMA_ARRAY_COUNT(final_path) ||
        !dluma_final_path_equal_or_under(
            final_path, root_identity.final_path) ||
        !GetFileInformationByHandle(file, &before) ||
        before.nNumberOfLinks != 1U ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (ULONGLONG)size.QuadPart > maximum)
        goto done;
    output->size = (DWORD)size.QuadPart;
    output->data = (char*)dluma_alloc((SIZE_T)output->size + 1U);
    if (!output->data ||
        !ReadFile(file, output->data, output->size, &read, 0) ||
        read != output->size ||
        !GetFileInformationByHandle(file, &after) ||
        !dluma_animation_refresh_info_equal(&before, &after))
        goto done;
    output->data[output->size] = 0;
    dluma_sha256_bytes_hex(
        output->data, output->size, output->sha256);
    result = output->sha256[0] != 0;
done:
    CloseHandle(file);
    if (!result) {
        dluma_free(output->data);
        memset(output, 0, sizeof(*output));
    }
    return result;
}

static int dluma_animation_refresh_hash_owned(
    const WCHAR* root,
    const char* relative,
    ULONGLONG maximum,
    char sha256[DLUMA_SHA256_HEX_CAPACITY],
    ULONGLONG* size_out)
{
    WCHAR lexical[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity root_identity;
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size;
    DWORD final_length;
    int result = 0;
    if (!root || !maximum || !sha256 || !size_out) return 0;
    sha256[0] = 0;
    *size_out = 0;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (!dluma_open_directory_identity(root, &root_identity) ||
        !dluma_animation_refresh_no_reparse_components(
            root, relative, 0, lexical, DLUMA_ARRAY_COUNT(lexical)))
        return 0;
    file = CreateFileW(
        lexical, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    final_length = GetFinalPathNameByHandleW(
        file, final_path, DLUMA_ARRAY_COUNT(final_path), 0);
    if (!final_length || final_length >= DLUMA_ARRAY_COUNT(final_path) ||
        !dluma_final_path_equal_or_under(
            final_path, root_identity.final_path) ||
        !GetFileInformationByHandle(file, &before) ||
        before.nNumberOfLinks != 1U ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (ULONGLONG)size.QuadPart > maximum ||
        !dluma_sha256_handle_hex(file, maximum, sha256) ||
        !GetFileInformationByHandle(file, &after) ||
        !dluma_animation_refresh_info_equal(&before, &after))
        goto done;
    *size_out = (ULONGLONG)size.QuadPart;
    result = 1;
done:
    CloseHandle(file);
    if (!result) {
        sha256[0] = 0;
        *size_out = 0;
    }
    return result;
}

static ULONGLONG dluma_animation_refresh_artifact_maximum(
    DlumaAnimationRefreshArtifactRole role)
{
    switch (role) {
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT:
        return DLUMA_ANIMATION_REFRESH_ALIAS_MAX_BYTES;
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT:
        return DLUMA_ANIMATION_REFRESH_SCRIPT_MAX_BYTES;
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION:
        return DLUMA_ANIMATION_REFRESH_ANIMATION_MAX_BYTES;
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_FALLBACK_RPACK:
        return DLUMA_ANIMATION_REFRESH_RPACK_MAX_BYTES;
    default:
        return 0;
    }
}
static void dluma_animation_refresh_free_owned(
    DlumaAnimationRefreshOwnedBytes* bytes)
{
    if (!bytes) return;
    dluma_free(bytes->data);
    memset(bytes, 0, sizeof(*bytes));
}

static void dluma_animation_refresh_append_u64(
    char* output, unsigned int capacity, ULONGLONG value)
{
    char number[32];
    number[0] = 0;
    dluma_u64_to_text(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(output, capacity, number);
}

static void dluma_animation_refresh_json_text(
    char* output,
    unsigned int capacity,
    const char* key,
    const char* value,
    int comma)
{
    if (comma) dluma_str_append(output, capacity, ",");
    dluma_str_append(output, capacity, "\"");
    dluma_str_append(output, capacity, key);
    dluma_str_append(output, capacity, "\":\"");
    dluma_str_append(output, capacity, value ? value : "");
    dluma_str_append(output, capacity, "\"");
}

static const char* dluma_animation_refresh_host_kind_name(DlumaHostKind kind)
{
    switch (kind) {
    case DLUMA_HOST_CHROME_EDITOR: return "ChromeEditor";
    case DLUMA_HOST_PLAYER_STANDALONE: return "PlayerStandalone";
    case DLUMA_HOST_PLAYER_EDITOR_PLAYTEST: return "PlayerEditorPlaytest";
    default: return "Unknown";
    }
}
static int dluma_animation_refresh_write_result(
    const WCHAR* project_root,
    const char* request_id,
    const DlumaAnimationRefreshRequest* request,
    DlumaAnimationRefreshHost host,
    const DlumaAnimationRefreshManifest* manifest,
    const DlumaAnimationRefreshResult* result,
    int* published)
{
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR temporary_name[160];
    WCHAR final_name[128];
    char text[DLUMA_ANIMATION_REFRESH_RESULT_MAX_BYTES + 1U];
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE directory_lock = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    WCHAR request_wide[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    unsigned int resolved_included = 0U;
    unsigned int index;
    int resolved_truncated = 0;
    int exact_profile = runtime && runtime->initialized &&
        runtime->classification.exact_profile &&
        runtime->evidence.canonical_image_verified;
    if (published) *published = 0;
    if (!project_root || !request_id || !result ||
        !dluma_utf8_to_wide(
            request_id, request_wide, DLUMA_ARRAY_COUNT(request_wide)) ||
        !dluma_animation_refresh_safe_directory(
            project_root,
            ".dl-reanimated/animation-refresh/results",
            directory, DLUMA_ARRAY_COUNT(directory)))
        return 0;
    dluma_wstr_copy(final_name, DLUMA_ARRAY_COUNT(final_name), request_wide);
    dluma_wstr_append(final_name, DLUMA_ARRAY_COUNT(final_name), L".json");
    dluma_wstr_copy(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), request_wide);
    dluma_wstr_append(temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".tmp.");
    {
        char process[32];
        WCHAR process_wide[32];
        dluma_u64_to_text(GetCurrentProcessId(), process, DLUMA_ARRAY_COUNT(process));
        if (!dluma_utf8_to_wide(
                process, process_wide, DLUMA_ARRAY_COUNT(process_wide)))
            return 0;
        dluma_wstr_append(
            temporary_name, DLUMA_ARRAY_COUNT(temporary_name), process_wide);
    }
    if (!dluma_checked_path_join(
            final_path, DLUMA_ARRAY_COUNT(final_path), directory, final_name) ||
        !dluma_checked_path_join(
            temporary, DLUMA_ARRAY_COUNT(temporary), directory, temporary_name))
        return 0;
    text[0] = 0;
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "{");
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "format",
        DLUMA_ANIMATION_REFRESH_RESULT_FORMAT, 0);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",\"schemaVersion\":2");
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "requestId", request_id, 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "deploymentId",
        request ? request->deployment_id : "", 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "manifestSha256",
        request ? request->manifest_sha256 : "", 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "status",
        result->accepted ? "Completed" : "Refused", 1);
    if (result->refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_NONE)
        dluma_str_append(
            text, DLUMA_ARRAY_COUNT(text), ",\"failureStage\":null");
    else
        dluma_animation_refresh_json_text(
            text, DLUMA_ARRAY_COUNT(text), "failureStage",
            dluma_animation_refresh_refusal_name(result->refusal), 1);
    if (result->refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_NONE)
        dluma_str_append(
            text, DLUMA_ARRAY_COUNT(text), ",\"firstFailedStage\":null");
    else
        dluma_animation_refresh_json_text(
            text, DLUMA_ARRAY_COUNT(text), "firstFailedStage",
            dluma_animation_refresh_refusal_name(result->refusal), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "refusalReason",
        dluma_animation_refresh_refusal_name(result->refusal), 1);
    if (result->primary_refusal != DLUMA_ANIMATION_REFRESH_REFUSAL_NONE &&
        result->primary_refusal != result->refusal)
        dluma_animation_refresh_json_text(
            text, DLUMA_ARRAY_COUNT(text), "primaryFailureStage",
            dluma_animation_refresh_refusal_name(result->primary_refusal), 1);
    else
        dluma_str_append(
            text, DLUMA_ARRAY_COUNT(text), ",\"primaryFailureStage\":null");
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "hostIdentity",
        dluma_animation_refresh_host_name(host), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "hostKind",
        runtime ? dluma_animation_refresh_host_kind_name(
            runtime->classification.kind) : "Unknown", 1);
    dluma_str_append(
        text, DLUMA_ARRAY_COUNT(text),
        exact_profile ? ",\"exactProfile\":true" :
            ",\"exactProfile\":false");
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "hostImageSha256",
        exact_profile ? runtime->evidence.image.sha256 : "", 1);
    dluma_str_append(
        text, DLUMA_ARRAY_COUNT(text), ",\"hostImageTimestamp\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text),
        exact_profile ? runtime->evidence.image.timestamp : 0U);
    dluma_str_append(
        text, DLUMA_ARRAY_COUNT(text), ",\"hostImageSizeOfImage\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text),
        exact_profile ? runtime->evidence.image.size_of_image : 0U);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "route",
        request ? dluma_animation_refresh_route_name(request->route) : "Unknown",
        1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "sourceVisible",
        dluma_animation_refresh_stage_name(result->source_visible), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "scriptVisible",
        dluma_animation_refresh_stage_name(result->script_visible), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "animationFilesVisible",
        dluma_animation_refresh_stage_name(result->animation_files_visible), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "filesystemRefresh",
        dluma_animation_refresh_stage_name(result->filesystem_refresh), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "reload",
        dluma_animation_refresh_stage_name(result->reload), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "rebind",
        dluma_animation_refresh_stage_name(result->rebind), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "fallbackMount",
        dluma_animation_refresh_stage_name(result->fallback_mount), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "fallbackUnmount",
        dluma_animation_refresh_stage_name(result->fallback_unmount), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "selectedModelMatch",
        dluma_animation_refresh_stage_name(result->selected_model_match), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "packRegistration",
        dluma_animation_refresh_stage_name(result->pack_registration), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "scriptBankResolution",
        dluma_animation_refresh_stage_name(result->script_bank_resolution), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "bindCreation",
        dluma_animation_refresh_stage_name(result->bind_creation), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "clipResolution",
        dluma_animation_refresh_stage_name(result->clip_resolution), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "inspectorRebuild",
        dluma_animation_refresh_stage_name(result->inspector_rebuild), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "rollback",
        dluma_animation_refresh_stage_name(result->rollback), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "uiBridgeDiscovery",
        dluma_animation_refresh_stage_name(
            result->ui_bridge_discovery), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "commandInterception",
        dluma_animation_refresh_stage_name(
            result->command_interception), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "activeDocumentResolution",
        dluma_animation_refresh_stage_name(
            result->active_document_resolution), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "activeProjectResolution",
        dluma_animation_refresh_stage_name(
            result->active_project_resolution), 1);
    dluma_animation_refresh_json_text(
        text, DLUMA_ARRAY_COUNT(text), "commandSource",
        result->command_source[0] ? result->command_source : "unknown", 1);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",\"animationNameCount\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text), result->animation_name_count);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",\"animationCount\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text), result->animation_count);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",\"resolvedFiles\":[");
    if (manifest) {
        for (index = 0; index < manifest->artifact_count; ++index) {
            const char* path = manifest->artifacts[index].relative_path;
            size_t required = strlen(text) + strlen(path) + 4U + 512U;
            if (required >= DLUMA_ARRAY_COUNT(text)) {
                resolved_truncated = 1;
                break;
            }
            if (resolved_included)
                dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",");
            dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\"");
            dluma_str_append(text, DLUMA_ARRAY_COUNT(text), path);
            dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\"");
            ++resolved_included;
        }
    }
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "],\"resolvedFileCount\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text), manifest ? manifest->artifact_count : 0U);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), ",\"resolvedFilesIncluded\":");
    dluma_animation_refresh_append_u64(
        text, DLUMA_ARRAY_COUNT(text), resolved_included);
    dluma_str_append(
        text, DLUMA_ARRAY_COUNT(text),
        resolved_truncated ? ",\"resolvedFilesTruncated\":true" :
            ",\"resolvedFilesTruncated\":false");
    dluma_str_append(
        text, DLUMA_ARRAY_COUNT(text),
        ",\"offlineValidation\":true,\"livePlaybackProven\":false,"
        "\"note\":\"offline_validated_live_behavior_unproven\"}\r\n");
    if (strlen(text) >= DLUMA_ANIMATION_REFRESH_RESULT_MAX_BYTES)
        return 0;
    directory_lock = dluma_animation_refresh_lock_safe_directory(
        project_root, ".dl-reanimated/animation-refresh/results",
        directory, DLUMA_ARRAY_COUNT(directory));
    if (directory_lock == INVALID_HANDLE_VALUE) return 0;
    if (!dluma_checked_path_join(
            final_path, DLUMA_ARRAY_COUNT(final_path), directory, final_name) ||
        !dluma_checked_path_join(
            temporary, DLUMA_ARRAY_COUNT(temporary), directory, temporary_name)) {
        CloseHandle(directory_lock);
        return 0;
    }
    file = CreateFileW(
        temporary, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW,
        0x00000100UL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) {
        CloseHandle(directory_lock);
        return 0;
    }
    if (!dluma_project_regular_file_is_contained(project_root, temporary)) {
        CloseHandle(file);
        DeleteFileW(temporary);
        CloseHandle(directory_lock);
        return 0;
    }
    if (!WriteFile(
            file, text, (DWORD)strlen(text), &written, 0) ||
        written != strlen(text) || !FlushFileBuffers(file)) {
        CloseHandle(file);
        DeleteFileW(temporary);
        CloseHandle(directory_lock);
        return 0;
    }
    CloseHandle(file);
    if (!dluma_project_regular_file_is_contained(
            project_root, temporary) ||
        !MoveFileExW(temporary, final_path, MOVEFILE_WRITE_THROUGH)) {
        DWORD move_error = GetLastError();
        DeleteFileW(temporary);
        CloseHandle(directory_lock);
        return move_error == 183UL || move_error == 80UL;
    }
    CloseHandle(directory_lock);
    if (!dluma_project_regular_file_is_contained(
            project_root, final_path))
        return 0;
    if (published) *published = 1;
    return 1;
}

static DlumaAnimationRefreshHost dluma_animation_refresh_actual_host(
    int* exact_profile)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    if (exact_profile) *exact_profile = 0;
    if (!runtime || !runtime->initialized ||
        runtime->classification.forwarding_only ||
        !runtime->classification.exact_profile ||
        !runtime->evidence.canonical_image_verified)
        return DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED;
    if (exact_profile) *exact_profile = 1;
    if (runtime->classification.kind == DLUMA_HOST_CHROME_EDITOR)
        return DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    if (runtime->classification.kind == DLUMA_HOST_PLAYER_STANDALONE ||
        runtime->classification.kind == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST)
        return DLUMA_ANIMATION_REFRESH_HOST_PLAYER;
    if (exact_profile) *exact_profile = 0;
    return DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED;
}

static ULONGLONG dluma_animation_refresh_unix_seconds(void)
{
    FILETIME time;
    ULONGLONG ticks;
    GetSystemTimeAsFileTime(&time);
    ticks = ((ULONGLONG)time.dwHighDateTime << 32) |
        (ULONGLONG)time.dwLowDateTime;
    return ticks > 116444736000000000ULL
        ? (ticks - 116444736000000000ULL) / 10000000ULL : 0;
}

static DlumaAnimationRefreshRoute dluma_animation_refresh_configured_route(void)
{
    WCHAR value[64];
    value[0] = 0;
    GetPrivateProfileStringW(
        L"AnimationRefresh", L"Route", L"ProjectRPack", value,
        DLUMA_ARRAY_COUNT(value), g_dluma.config_path);
    if (dluma_wstricmp(value, L"RawLoose") == 0 ||
        dluma_wstricmp(value, L"LooseOnly") == 0)
        return DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    if (dluma_wstricmp(value, L"ProjectRPack") == 0 ||
        dluma_wstricmp(value, L"ProjectRPackFallback") == 0)
        return DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    return (DlumaAnimationRefreshRoute)0;
}

static int dluma_animation_refresh_configured_enabled(void)
{
    WCHAR value_wide[16];
    char value_utf8[16];
    value_wide[0] = 0;
    value_utf8[0] = 0;
    GetPrivateProfileStringW(
        L"AnimationRefresh", L"Enabled", L"0", value_wide,
        DLUMA_ARRAY_COUNT(value_wide), g_dluma.config_path);
    return dluma_wide_to_utf8(
               value_wide, value_utf8, DLUMA_ARRAY_COUNT(value_utf8)) &&
        dluma_parse_u32(value_utf8, 0U) ? 1 : 0;
}

static int dluma_animation_refresh_result_exists(
    const WCHAR* project_root, const char* request_id)
{
    char relative[256];
    WCHAR path[DLUMA_MAX_WPATH];
    relative[0] = 0;
    dluma_str_append(
        relative, DLUMA_ARRAY_COUNT(relative),
        ".dl-reanimated/animation-refresh/results/");
    dluma_str_append(relative, DLUMA_ARRAY_COUNT(relative), request_id);
    dluma_str_append(relative, DLUMA_ARRAY_COUNT(relative), ".json");
    return dluma_animation_refresh_join_relative(
               project_root, relative, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_project_regular_file_is_contained(project_root, path);
}

static int dluma_animation_refresh_request_name(
    const WCHAR* filename, char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY])
{
    WCHAR stem[33];
    unsigned int index;
    if (!filename || wcslen(filename) != 37U ||
        dluma_wstricmp(filename + 32U, L".json") != 0)
        return 0;
    for (index = 0; index < 32U; ++index) {
        WCHAR value = filename[index];
        if (!((value >= L'0' && value <= L'9') ||
              (value >= L'a' && value <= L'f')))
            return 0;
        stem[index] = value;
    }
    stem[32] = 0;
    return dluma_wide_to_utf8(
        stem, request_id, DLUMA_ANIMATION_REFRESH_ID_CAPACITY);
}

static int dluma_animation_refresh_coalesce_sha256(
    const DlumaAnimationRefreshRequest* request,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    char key[256];
    if (!request || !sha256) return 0;
    key[0] = 0;
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), request->deployment_id);
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), "|");
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), request->manifest_sha256);
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), "|");
    dluma_str_append(
        key, DLUMA_ARRAY_COUNT(key),
        dluma_animation_refresh_host_name(request->target_host));
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), "|");
    dluma_str_append(
        key, DLUMA_ARRAY_COUNT(key),
        dluma_animation_refresh_route_name(request->route));
    dluma_sha256_bytes_hex(key, strlen(key), sha256);
    return dluma_animation_refresh_sha256_is_canonical(sha256);
}

static int dluma_animation_refresh_coalesce_relative(
    const DlumaAnimationRefreshRequest* request,
    char* relative,
    unsigned int capacity)
{
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    if (!relative || !capacity ||
        !dluma_animation_refresh_coalesce_sha256(request, sha256))
        return 0;
    relative[0] = 0;
    dluma_str_append(
        relative, capacity,
        ".dl-reanimated/animation-refresh/results/coalesce-");
    dluma_str_append(relative, capacity, sha256);
    dluma_str_append(relative, capacity, ".processed");
    return dluma_animation_refresh_relative_path_is_canonical(relative);
}

/* 0 = no marker, 1 = same request replay, 2 = coalesced/invalid marker. */
static int dluma_animation_refresh_coalesce_state(
    const WCHAR* project_root,
    const DlumaAnimationRefreshRequest* request)
{
    char relative[256];
    WCHAR path[DLUMA_MAX_WPATH];
    DWORD attributes;
    DlumaAnimationRefreshOwnedBytes marker;
    int state = 2;
    memset(&marker, 0, sizeof(marker));
    if (!dluma_animation_refresh_coalesce_relative(
            request, relative, DLUMA_ARRAY_COUNT(relative)) ||
        !dluma_animation_refresh_join_relative(
            project_root, relative, path, DLUMA_ARRAY_COUNT(path)))
        return 2;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == 2UL || error == 3UL ? 0 : 2;
    }
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY |
                       FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !dluma_animation_refresh_read_owned(
            project_root, relative, 64U, &marker))
        return 2;
    if (marker.size == 32U &&
        dluma_stricmp(marker.data, request->request_id) == 0)
        state = 1;
    dluma_animation_refresh_free_owned(&marker);
    return state;
}

static int dluma_animation_refresh_write_coalesce_marker(
    const WCHAR* project_root,
    const DlumaAnimationRefreshRequest* request)
{
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    char process_text[32];
    char tick_text[32];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR temporary_path[DLUMA_MAX_WPATH];
    WCHAR final_name[128];
    WCHAR temporary_name[192];
    WCHAR sha_wide[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR process_wide[32];
    WCHAR tick_wide[32];
    HANDLE directory_lock = INVALID_HANDLE_VALUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    int result = 0;
    if (!project_root || !request ||
        !dluma_animation_refresh_coalesce_sha256(request, sha256) ||
        !dluma_utf8_to_wide(sha256, sha_wide, DLUMA_ARRAY_COUNT(sha_wide)))
        return 0;
    temporary_path[0] = 0;
    process_text[0] = 0;
    tick_text[0] = 0;
    dluma_u64_to_text(
        GetCurrentProcessId(), process_text, DLUMA_ARRAY_COUNT(process_text));
    dluma_u64_to_text(
        GetTickCount64(), tick_text, DLUMA_ARRAY_COUNT(tick_text));
    if (!dluma_utf8_to_wide(
            process_text, process_wide, DLUMA_ARRAY_COUNT(process_wide)) ||
        !dluma_utf8_to_wide(
            tick_text, tick_wide, DLUMA_ARRAY_COUNT(tick_wide)))
        return 0;
    final_name[0] = 0;
    dluma_wstr_append(final_name, DLUMA_ARRAY_COUNT(final_name), L"coalesce-");
    dluma_wstr_append(final_name, DLUMA_ARRAY_COUNT(final_name), sha_wide);
    dluma_wstr_append(final_name, DLUMA_ARRAY_COUNT(final_name), L".processed");
    temporary_name[0] = 0;
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L"coalesce-");
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), sha_wide);
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".tmp.");
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), process_wide);
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), L".");
    dluma_wstr_append(
        temporary_name, DLUMA_ARRAY_COUNT(temporary_name), tick_wide);
    directory_lock = dluma_animation_refresh_lock_safe_directory(
        project_root, ".dl-reanimated/animation-refresh/results",
        directory, DLUMA_ARRAY_COUNT(directory));
    if (directory_lock == INVALID_HANDLE_VALUE) return 0;
    if (!dluma_checked_path_join(
            final_path, DLUMA_ARRAY_COUNT(final_path), directory, final_name) ||
        !dluma_checked_path_join(
            temporary_path, DLUMA_ARRAY_COUNT(temporary_path), directory,
            temporary_name))
        goto done;
    file = CreateFileW(
        temporary_path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_NEW,
        0x00000100UL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (!dluma_project_regular_file_is_contained(
            project_root, temporary_path) ||
        !WriteFile(
            file, request->request_id, 32U, &written, 0) ||
        written != 32U || !FlushFileBuffers(file))
        goto done;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(
            temporary_path, final_path, MOVEFILE_WRITE_THROUGH)) {
        DWORD move_error = GetLastError();
        if (move_error != 183UL && move_error != 80UL) goto done;
    }
    result = dluma_project_regular_file_is_contained(
        project_root, final_path);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!result && temporary_path[0]) DeleteFileW(temporary_path);
    CloseHandle(directory_lock);
    return result;
}

static int dluma_animation_refresh_should_write_coalesce_marker(
    int resolved_manifest_valid,
    const DlumaAnimationRefreshResult* result)
{
    return resolved_manifest_valid && result && result->accepted != 0U;
}

static void dluma_animation_refresh_process_request(
    const WCHAR* project_root,
    const char* request_id,
    const char* request_relative,
    DlumaAnimationRefreshHost host,
    int exact_profile,
    const char* command_source)
{
    DlumaAnimationRefreshOwnedBytes request_bytes;
    DlumaAnimationRefreshOwnedBytes manifest_bytes;
    char artifact_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest = 0;
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    DlumaAnimationRefreshRefusal manifest_refusal;
    DlumaAnimationRefreshRuntimeOperationsContext operations_context;
    ULONGLONG artifact_size = 0;
    ULONGLONG artifact_maximum;
    ULONGLONG aggregate_size = 0;
    ULONGLONG remaining;
    unsigned int index;
    int request_valid = 0;
    int resolved_manifest_valid = 0;
    int coalesce_state = 0;
    int result_written = 0;
    int result_published = 0;
    memset(&request_bytes, 0, sizeof(request_bytes));
    memset(&manifest_bytes, 0, sizeof(manifest_bytes));
    memset(artifact_sha256, 0, sizeof(artifact_sha256));
    memset(&request, 0, sizeof(request));
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    memset(&operations_context, 0, sizeof(operations_context));
    memset(&result, 0, sizeof(result));
    result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_INVALID;
    if (!dluma_animation_refresh_read_owned(
            project_root, request_relative,
            DLUMA_ANIMATION_REFRESH_MAX_REQUEST_BYTES, &request_bytes) ||
        !dluma_animation_refresh_parse_request(
            request_bytes.data, request_bytes.size, &request) ||
        dluma_stricmp(request.request_id, request_id) != 0)
        goto receipt;
    request_valid = 1;
    admission.enabled =
        dluma_animation_refresh_configured_enabled() ? 1U : 0U;
    admission.exact_profile = exact_profile ? 1U : 0U;
    admission.active_project_owned = 1U;
    admission.now_unix_seconds = dluma_animation_refresh_unix_seconds();
    admission.actual_host = host;
    admission.configured_route = dluma_animation_refresh_configured_route();
    if (!admission.enabled) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_DISABLED;
        goto receipt;
    }
    if (host != DLUMA_ANIMATION_REFRESH_HOST_EDITOR &&
        host != DLUMA_ANIMATION_REFRESH_HOST_PLAYER) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_UNSUPPORTED_HOST;
        goto receipt;
    }
    if (!admission.exact_profile) {
        result.refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_EXACT_PROFILE_UNAVAILABLE;
        goto receipt;
    }
    if (request.target_host != host) {
        result.refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_HOST_MISMATCH;
        goto receipt;
    }
    if (request.route != admission.configured_route) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_ROUTE_NOT_ALLOWED;
        goto receipt;
    }
    if (request.route == DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE &&
        host == DLUMA_ANIMATION_REFRESH_HOST_PLAYER) {
        result.refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_RAW_LOOSE_PLAYER_UNSUPPORTED;
        goto receipt;
    }
    if (admission.now_unix_seconds < request.created_unix_seconds ||
        admission.now_unix_seconds > request.expires_unix_seconds) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_EXPIRED;
        goto receipt;
    }
    coalesce_state = dluma_animation_refresh_coalesce_state(
        project_root, &request);
    if (coalesce_state == 1) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_REPLAY;
        goto receipt;
    }
    if (coalesce_state == 2) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_COALESCED;
        goto receipt;
    }
    manifest = (DlumaAnimationRefreshManifest*)dluma_alloc(sizeof(*manifest));
    if (!manifest) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID;
        goto receipt;
    }
    if (!dluma_animation_refresh_read_owned(
            project_root, request.manifest_relative_path,
            DLUMA_ANIMATION_REFRESH_MAX_MANIFEST_BYTES, &manifest_bytes)) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_NOT_OWNED;
        goto receipt;
    }
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches =
        dluma_stricmp(
            request.manifest_sha256, manifest_bytes.sha256) == 0;
    if (!admission.manifest_hash_matches) {
        result.refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_HASH_MISMATCH;
        goto receipt;
    }
    if (!dluma_animation_refresh_parse_manifest(
            manifest_bytes.data, manifest_bytes.size, manifest)) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID;
        goto receipt;
    }
    if (dluma_stricmp(request.deployment_id, manifest->deployment_id) != 0) {
        result.refusal =
            DLUMA_ANIMATION_REFRESH_REFUSAL_DEPLOYMENT_MISMATCH;
        goto receipt;
    }
    if (manifest->created_unix_seconds > request.created_unix_seconds) {
        result.refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID;
        goto receipt;
    }
    manifest_refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID;
    if (!dluma_animation_refresh_manifest_validate(
            manifest, request.route, &manifest_refusal)) {
        result.refusal = manifest_refusal;
        goto receipt;
    }
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    for (index = 0; index < manifest->artifact_count; ++index) {
        artifact_maximum = dluma_animation_refresh_artifact_maximum(
            manifest->artifacts[index].role);
        if (aggregate_size >=
            DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES) {
            admission.artifacts_owned = 0U;
            admission.artifact_hashes_match = 0U;
            break;
        }
        remaining = DLUMA_ANIMATION_REFRESH_TOTAL_ARTIFACT_MAX_BYTES -
            aggregate_size;
        if (artifact_maximum > remaining) artifact_maximum = remaining;
        if (!artifact_maximum ||
            !dluma_animation_refresh_hash_owned(
                project_root, manifest->artifacts[index].relative_path,
                artifact_maximum, artifact_sha256, &artifact_size)) {
            admission.artifacts_owned = 0U;
            admission.artifact_hashes_match = 0U;
            break;
        }
        aggregate_size += artifact_size;
        if (dluma_stricmp(
                artifact_sha256,
                manifest->artifacts[index].sha256) != 0) {
            admission.artifact_hashes_match = 0U;
            break;
        }
    }
    if (admission.artifacts_owned && admission.artifact_hashes_match)
        resolved_manifest_valid = 1;
    operations_context.manifest = manifest;
    dluma_wstr_copy(
        operations_context.project_root,
        DLUMA_ARRAY_COUNT(operations_context.project_root),
        project_root);
    dluma_str_copy(
        operations_context.request_id,
        DLUMA_ARRAY_COUNT(operations_context.request_id),
        request_id);
    operations.context = &operations_context;
    if (host == DLUMA_ANIMATION_REFRESH_HOST_EDITOR) {
        operations.selected_model_reload_supported = 1;
        operations.reload_selected_model =
            dluma_animation_refresh_editor_selected_model;
    } else if (host == DLUMA_ANIMATION_REFRESH_HOST_PLAYER) {
        operations.selected_model_reload_supported = 1;
        operations.reload_selected_model =
            dluma_animation_refresh_player_runtime_pack;
    }
    (void)dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result);
receipt:
    if (host == DLUMA_ANIMATION_REFRESH_HOST_EDITOR) {
        result.active_document_resolution =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
        result.active_project_resolution =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    }
    if (command_source &&
        dluma_stricmp(command_source, "request-watcher") != 0)
        result.command_interception =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    dluma_str_copy(
        result.command_source,
        DLUMA_ARRAY_COUNT(result.command_source),
        command_source ? command_source : "unknown");
    result_written = dluma_animation_refresh_write_result(
        project_root, request_id,
        request_valid ? &request : 0, host,
        resolved_manifest_valid ? manifest : 0, &result,
        &result_published);
    if (!result_written) {
        dluma_log_pair(
            "WARN", "animation_refresh_result",
            "result_write_failed");
    } else {
        dluma_log_pair(
            "INFO", "animation_refresh_result",
            dluma_animation_refresh_refusal_name(result.refusal));
    }
    if (result_written && result_published &&
        dluma_animation_refresh_should_write_coalesce_marker(
            resolved_manifest_valid, &result) &&
        !dluma_animation_refresh_write_coalesce_marker(
            project_root, &request))
        dluma_log_pair(
            "WARN", "animation_refresh_coalesce_marker",
            "write_failed");
    dluma_animation_refresh_free_owned(&manifest_bytes);
    dluma_animation_refresh_free_owned(&request_bytes);
    dluma_free(manifest);
}
static unsigned int dluma_animation_refresh_runtime_poll_project_internal(
    const WCHAR* project_root,
    int force,
    const char* command_source)
{
    WCHAR request_directory[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    ULONGLONG now = GetTickCount64();
    DlumaAnimationRefreshHost host;
    unsigned int processed = 0;
    int exact_profile = 0;
    if (!project_root || !project_root[0] ||
        (!force && now - g_dluma_animation_refresh_last_poll_ms <
            DLUMA_ANIMATION_REFRESH_POLL_INTERVAL_MS) ||
        dluma_atomic_compare_exchange(
            &g_dluma_animation_refresh_poll_lock, 1, 0) != 0)
        return 0;
    g_dluma_animation_refresh_last_poll_ms = now;
    host = dluma_animation_refresh_actual_host(&exact_profile);
    if (!dluma_animation_refresh_safe_directory(
            project_root,
            ".dl-reanimated/animation-refresh/requests",
            request_directory, DLUMA_ARRAY_COUNT(request_directory)) ||
        !dluma_checked_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern),
            request_directory, L"*.json"))
        goto done;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) goto done;
    do {
        char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
        char request_relative[256];
        if (processed >= DLUMA_ANIMATION_REFRESH_MAX_REQUESTS_PER_POLL)
            break;
        if (data.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (!dluma_animation_refresh_request_name(
                data.cFileName, request_id) ||
            dluma_animation_refresh_result_exists(
                project_root, request_id))
            continue;
        request_relative[0] = 0;
        dluma_str_append(
            request_relative, DLUMA_ARRAY_COUNT(request_relative),
            ".dl-reanimated/animation-refresh/requests/");
        dluma_str_append(
            request_relative, DLUMA_ARRAY_COUNT(request_relative),
            request_id);
        dluma_str_append(
            request_relative, DLUMA_ARRAY_COUNT(request_relative),
            ".json");
        dluma_animation_refresh_process_request(
            project_root, request_id, request_relative,
            host, exact_profile, command_source);
        ++processed;
    } while (FindNextFileW(find, &data));
done:
    if (find != INVALID_HANDLE_VALUE) FindClose(find);
    dluma_atomic_exchange(&g_dluma_animation_refresh_poll_lock, 0);
    return processed;
}

void dluma_animation_refresh_runtime_poll_project(
    const WCHAR* project_root)
{
    (void)dluma_animation_refresh_runtime_poll_project_internal(
        project_root, 0, "request-watcher");
}

int dluma_animation_refresh_runtime_poll_project_now(
    const WCHAR* project_root,
    const char* command_source)
{
    return dluma_animation_refresh_runtime_poll_project_internal(
        project_root, 1, command_source) != 0U;
}
