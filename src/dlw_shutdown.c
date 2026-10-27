#include "dlw_common.h"
#include "dlw_native_gameplay.h"
#include "dlw_frontend_error.h"

typedef void (__fastcall *DlumaExitGameFn)(void* game_interface);

#define DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_PE64_MAGIC 0x020BU
#define DLUMA_IMPORT_DIRECTORY_OFFSET 0x78UL
#define DLUMA_CONTENT_FINALIZE_MESSAGE (0x8000U + 0x04D1U)
#define DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE (0x8000U + 0x04D2U)
#define DLUMA_CONTENT_FINALIZE_TIMEOUT_MS 4000UL
#define DLUMA_ORDERLY_CLOSE_TOKEN_MAX_AGE_MS 5000ULL
#define DLUMA_ORDERLY_CLOSE_TAG_MAGIC ((LPARAM)0x444C5743UL)

__declspec(dllimport) BOOL WINAPI PostMessageW(
    HWND hWnd, unsigned int Msg, WPARAM wParam, LPARAM lParam);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(
    HWND hWnd, DWORD* process_id);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);

#define DLUMA_CONTENT_FINALIZE_IDLE 0L
#define DLUMA_CONTENT_FINALIZE_REQUESTED 1L
#define DLUMA_CONTENT_FINALIZE_OWNER_DRAINING 2L
#define DLUMA_CONTENT_FINALIZE_COMPLETED 3L
#define DLUMA_CONTENT_FINALIZE_FAILED 4L
#define DLUMA_ORDERLY_CLOSE_IDLE 0L
#define DLUMA_ORDERLY_CLOSE_CLAIMED 1L
#define DLUMA_ORDERLY_CLOSE_DELIVERED 2L
#define DLUMA_ORDERLY_CLOSE_FAILED 3L

static const char g_dluma_exit_game_export[] =
    "?ExitGame@IGame@@QEAAXXZ";
static void** g_dluma_exit_iat_slot;
static DlumaExitGameFn g_dluma_exit_original;
static volatile LONG g_dluma_exit_active_calls;
static volatile LONG g_dluma_exit_owner_thread_id;
static volatile LONG g_dluma_exit_owner_conflict;
static volatile LONGLONG g_dluma_exit_owner_generation;
static volatile LONGLONG g_dluma_exit_owner_active_token;
static unsigned char g_dluma_exit_detached;
static HWND g_dluma_game_window;
static WNDPROC g_dluma_window_original;
static volatile LONG g_dluma_window_active_calls;
static unsigned char g_dluma_window_detached;
static HANDLE g_dluma_content_finalize_event;
static volatile LONG g_dluma_content_finalize_state;
static volatile LONG g_dluma_content_finalize_result;
static volatile LONG g_dluma_content_finalize_exit_owner_calls;
static volatile LONGLONG g_dluma_content_finalize_exit_owner_token;
static volatile LONG g_dluma_content_finalize_requester_thread;
static volatile LONG g_dluma_orderly_close_state;
static volatile LONG g_dluma_orderly_close_requester_thread;
static volatile LONGLONG g_dluma_orderly_close_generation;
static volatile LONG g_dluma_orderly_close_token_lock;
static DlumaOrderlyCloseToken g_dluma_orderly_close_token;

static void __fastcall dluma_exit_game_hook(void* game_interface);

static void dluma_orderly_close_token_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_orderly_close_token_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_orderly_close_token_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_orderly_close_token_lock, 0);
}

static WORD dluma_shutdown_read_u16(const void* address)
{
    const BYTE* p = (const BYTE*)address;
    return (WORD)((WORD)p[0] | ((WORD)p[1] << 8));
}

static DWORD dluma_shutdown_read_u32(const void* address)
{
    const BYTE* p = (const BYTE*)address;
    return (DWORD)p[0] | ((DWORD)p[1] << 8) |
           ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static ULONGLONG dluma_shutdown_read_u64(const void* address)
{
    const BYTE* p = (const BYTE*)address;
    ULONGLONG value = 0;
    unsigned int i;
    for (i = 0; i < 8; ++i) value |= ((ULONGLONG)p[i]) << (i * 8);
    return value;
}

static int dluma_shutdown_pe_identity(
    HMODULE module, DWORD* timestamp, DWORD* image_size, DWORD* nt_out)
{
    BYTE* base = (BYTE*)module;
    DWORD nt;
    if (!base || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_shutdown_read_u32(base + 0x3C);
    if (nt > 0x100000 || !dluma_is_readable(base + nt, 0x108) ||
        base[nt] != 'P' || base[nt + 1] != 'E' ||
        dluma_shutdown_read_u16(base + nt + 24) != DLUMA_PE64_MAGIC)
        return 0;
    if (timestamp) *timestamp = dluma_shutdown_read_u32(base + nt + 8);
    if (image_size) *image_size = dluma_shutdown_read_u32(base + nt + 0x50);
    if (nt_out) *nt_out = nt;
    return 1;
}

static int dluma_shutdown_rva_valid(DWORD rva, SIZE_T bytes, DWORD image_size)
{
    return rva && rva < image_size && bytes <= image_size - rva;
}

static void** dluma_shutdown_find_exit_iat_slot(HMODULE gamedll)
{
    BYTE* base = (BYTE*)gamedll;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    DWORD nt = 0;
    DWORD import_rva;
    DWORD import_size;
    BYTE* descriptor;
    FARPROC expected;

    if (!dluma_shutdown_pe_identity(
            gamedll, &timestamp, &image_size, &nt) ||
        timestamp != DLUMA_DEVTOOLS_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_DEVTOOLS_GAMEDLL_IMAGE_SIZE)
        return 0;

    import_rva = dluma_shutdown_read_u32(
        base + nt + 24 + DLUMA_IMPORT_DIRECTORY_OFFSET);
    import_size = dluma_shutdown_read_u32(
        base + nt + 24 + DLUMA_IMPORT_DIRECTORY_OFFSET + 4);
    if (!dluma_shutdown_rva_valid(import_rva, 20, image_size) ||
        import_size < 20 || import_size > image_size - import_rva)
        return 0;

    expected = g_dluma.engine.engine_module
        ? GetProcAddress(g_dluma.engine.engine_module, g_dluma_exit_game_export)
        : 0;
    if (!expected) {
        HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
        expected = engine ? GetProcAddress(engine, g_dluma_exit_game_export) : 0;
    }
    if (!expected) return 0;

    descriptor = base + import_rva;
    while ((SIZE_T)(descriptor - (base + import_rva)) + 20 <= import_size) {
        DWORD original_thunk = dluma_shutdown_read_u32(descriptor);
        DWORD first_thunk = dluma_shutdown_read_u32(descriptor + 16);
        unsigned int index;
        if (!original_thunk && !first_thunk) break;
        if (!dluma_shutdown_rva_valid(original_thunk, 8, image_size) ||
            !dluma_shutdown_rva_valid(first_thunk, 8, image_size)) {
            descriptor += 20;
            continue;
        }
        for (index = 0; ; ++index) {
            DWORD oft_rva = original_thunk + index * 8;
            DWORD iat_rva = first_thunk + index * 8;
            ULONGLONG name_thunk;
            DWORD name_rva;
            const char* name;
            void** slot;
            if (!dluma_shutdown_rva_valid(oft_rva, 8, image_size) ||
                !dluma_shutdown_rva_valid(iat_rva, 8, image_size))
                break;
            name_thunk = dluma_shutdown_read_u64(base + oft_rva);
            if (!name_thunk) break;
            if (name_thunk & 0x8000000000000000ULL) continue;
            if (name_thunk > 0xFFFFFFFFULL) continue;
            name_rva = (DWORD)name_thunk;
            if (!dluma_shutdown_rva_valid(name_rva, 3, image_size)) continue;
            name = (const char*)(base + name_rva + 2);
            if (!dluma_is_readable(name, strlen(g_dluma_exit_game_export) + 1) ||
                dluma_stricmp(name, g_dluma_exit_game_export) != 0)
                continue;
            slot = (void**)(base + iat_rva);
            if (dluma_is_readable(slot, sizeof(void*)) && *slot == (void*)expected)
                return slot;
            return 0;
        }
        descriptor += 20;
    }
    return 0;
}

static int dluma_shutdown_restore_exit_hook(void)
{
    DWORD old_protect;
    DWORD restored_protect;
    if (!g_dluma_exit_iat_slot) {
        g_dluma_exit_detached = 1;
        return 1;
    }
    if (!dluma_is_readable(g_dluma_exit_iat_slot, sizeof(void*))) return 0;
    if (*g_dluma_exit_iat_slot != (void*)dluma_exit_game_hook &&
        *g_dluma_exit_iat_slot != (void*)g_dluma_exit_original) {
        dluma_log_line("WARN", "IGame::ExitGame IAT slot changed ownership; no restore write was performed.");
        return 0;
    }
    if (*g_dluma_exit_iat_slot == (void*)g_dluma_exit_original) {
        g_dluma_exit_detached = 1;
        return 1;
    }
    if (!VirtualProtect(g_dluma_exit_iat_slot, sizeof(void*),
                        PAGE_READWRITE, &old_protect))
        return 0;
    *g_dluma_exit_iat_slot = (void*)g_dluma_exit_original;
    VirtualProtect(g_dluma_exit_iat_slot, sizeof(void*),
                   old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), g_dluma_exit_iat_slot,
                          sizeof(void*));
    g_dluma_exit_detached = 1;
    return 1;
}

static void dluma_shutdown_restore_window_hook(void);

static void dluma_shutdown_publish_finalize_failure(const char* reason)
{
    dluma_log_pair("WARN", "content_report_finalization_failed",
                 reason ? reason : "unknown");
}

static int dluma_shutdown_run_content_finalizer(
    unsigned int observed_exit_owner_calls)
{
    return observed_exit_owner_calls
        ? dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner(
              observed_exit_owner_calls)
        : dluma_custom_map_asset_trace_game_thread_finalize();
}

int dluma_shutdown_snapshot_natural_exit_owner(
    unsigned int expected_exit_owner_calls,
    DlumaNaturalShutdownOwnerProof* proof_out)
{
    DlumaNaturalShutdownOwnerProof proof;
    memset(&proof, 0, sizeof(proof));
    if (!proof_out)
        return 0;
    proof.expected_exit_owner_calls =
        expected_exit_owner_calls;
    proof.observed_exit_owner_calls =
        (unsigned int)dluma_atomic_compare_exchange(
            &g_dluma_exit_active_calls, 0, 0);
    proof.expected_thread_id =
        dluma_atomic_compare_exchange(
            &g_dluma_exit_owner_thread_id, 0, 0);
    proof.observed_thread_id = (LONG)GetCurrentThreadId();
    proof.expected_token =
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_content_finalize_exit_owner_token, 0, 0);
    proof.observed_token =
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_exit_owner_active_token, 0, 0);
    proof.owner_conflict =
        dluma_atomic_compare_exchange(
            &g_dluma_exit_owner_conflict, 0, 0) != 0;
    proof.hook_owned =
        g_dluma_exit_iat_slot &&
        dluma_is_readable(g_dluma_exit_iat_slot, sizeof(void*)) &&
        *g_dluma_exit_iat_slot == (void*)dluma_exit_game_hook;
    proof.exact =
        proof.expected_exit_owner_calls == 1U &&
        proof.observed_exit_owner_calls == 1U &&
        proof.expected_thread_id == proof.observed_thread_id &&
        proof.expected_thread_id != 0 &&
        proof.expected_token &&
        proof.expected_token == proof.observed_token &&
        !proof.owner_conflict &&
        proof.hook_owned;
    *proof_out = proof;
    return proof.exact != 0;
}

/* GameDI::OnFrame migrates across serialized workers, so no arbitrary worker
 * can own shutdown. Prefer the active exact route when shutdown is invoked
 * inside it, accept the exact active IGame::ExitGame lifecycle owner, or
 * marshal to the independently validated timer/window owner before
 * publishing the global stop gate. */
static int dluma_shutdown_finalize_content_on_owner_thread(void)
{
    const LONG window_owner =
        dluma_engine_game_frame_window_owner_thread_id(
            &g_dluma.engine);
    const LONG current_thread = (LONG)GetCurrentThreadId();
    const unsigned int observed_exit_owner_calls =
        (unsigned int)dluma_atomic_compare_exchange(
            &g_dluma_content_finalize_exit_owner_calls, 0, 0);
    DWORD window_thread = 0;
    DWORD wait_result;
    LONG state;
    LONG result;
    DlumaNaturalShutdownOwnerProof natural_owner;
    DlumaContentFinalizationAuthorityEvidence authority_evidence;

    memset(&natural_owner, 0, sizeof(natural_owner));
    memset(&authority_evidence, 0, sizeof(authority_evidence));
    if (observed_exit_owner_calls)
        (void)dluma_shutdown_snapshot_natural_exit_owner(
            observed_exit_owner_calls, &natural_owner);
    if (dluma_engine_current_thread_can_finalize_content_ex(
            &g_dluma.engine, 0,
            observed_exit_owner_calls ? &natural_owner : 0,
            &authority_evidence))
        return dluma_shutdown_run_content_finalizer(
            observed_exit_owner_calls);
    if (!window_owner) {
        dluma_shutdown_publish_finalize_failure(
            "validated_window_owner_unavailable");
        return 0;
    }
    if (!g_dluma_game_window || !g_dluma_content_finalize_event) {
        dluma_shutdown_publish_finalize_failure("owner_window_unavailable");
        return 0;
    }
    window_thread = GetWindowThreadProcessId(g_dluma_game_window, 0);
    if (!window_thread ||
        (LONG)window_thread != window_owner) {
        dluma_shutdown_publish_finalize_failure(
            "validated_window_owner_mismatch");
        return 0;
    }
    if (current_thread == window_owner)
        return dluma_shutdown_run_content_finalizer(
            observed_exit_owner_calls);

    ResetEvent(g_dluma_content_finalize_event);
    dluma_atomic_exchange(
        &g_dluma_content_finalize_requester_thread, current_thread);
    dluma_atomic_exchange(&g_dluma_content_finalize_result, 0);
    state = dluma_atomic_compare_exchange(
        &g_dluma_content_finalize_state,
        DLUMA_CONTENT_FINALIZE_REQUESTED,
        DLUMA_CONTENT_FINALIZE_IDLE);
    if (state != DLUMA_CONTENT_FINALIZE_IDLE) {
        dluma_shutdown_publish_finalize_failure("owner_claim_failed");
        return 0;
    }
    if (!PostMessageW(
            g_dluma_game_window, DLUMA_CONTENT_FINALIZE_MESSAGE, 0, 0)) {
        dluma_atomic_exchange(
            &g_dluma_content_finalize_state, DLUMA_CONTENT_FINALIZE_FAILED);
        dluma_shutdown_publish_finalize_failure("owner_message_post_failed");
        return 0;
    }

    wait_result = WaitForSingleObject(
        g_dluma_content_finalize_event,
        DLUMA_CONTENT_FINALIZE_TIMEOUT_MS);
    if (wait_result != WAIT_OBJECT_0) {
        dluma_atomic_exchange(
            &g_dluma_content_finalize_state, DLUMA_CONTENT_FINALIZE_FAILED);
        dluma_shutdown_publish_finalize_failure("owner_pump_timeout");
        return 0;
    }
    result = dluma_atomic_compare_exchange(
        &g_dluma_content_finalize_result, 0, 0);
    state = dluma_atomic_compare_exchange(
        &g_dluma_content_finalize_state, 0, 0);
    dluma_atomic_exchange(
        &g_dluma_content_finalize_state, DLUMA_CONTENT_FINALIZE_IDLE);
    if (state != DLUMA_CONTENT_FINALIZE_COMPLETED || result != 1) {
        dluma_shutdown_publish_finalize_failure("owner_finalize_failed");
        return 0;
    }
    dluma_log_line(
        "INFO",
        "Content reports finalized through the independently validated window owner.");
    return 1;
}

static void dluma_request_quiesce_impl(
    const char* reason, unsigned int observed_exit_owner_calls)
{
    int content_finalized;
    if (dluma_atomic_compare_exchange(
            &g_dluma.lifecycle_state,
            DLUMA_LIFECYCLE_QUIESCING,
            DLUMA_LIFECYCLE_RUNNING) != DLUMA_LIFECYCLE_RUNNING)
        return;

    dluma_log_pair("INFO", "shutdown_quiesce_reason",
                 reason ? reason : "unspecified orderly exit");
    dluma_atomic_exchange(
        &g_dluma_content_finalize_exit_owner_calls,
        (LONG)observed_exit_owner_calls);
    _InterlockedExchange64(
        &g_dluma_content_finalize_exit_owner_token,
        observed_exit_owner_calls
            ? _InterlockedCompareExchange64(
                  &g_dluma_exit_owner_active_token, 0, 0)
            : 0);
    content_finalized = dluma_shutdown_finalize_content_on_owner_thread();
    if (!content_finalized)
        dluma_log_line(
            "WARN",
            "Owner-thread content report finalization did not complete; loader teardown remains fail-closed.");

    (void)dluma_frontend_error_stop();
    dluma_mouse_lock_quiesce();
    /* Publish the global stop gate only after the owner-thread drain request.
     * The timer/window pump must remain live while a non-owner ExitGame thread
     * waits for the immutable game-thread owner. */
    dluma_atomic_exchange(&g_dluma.stop_requested, 1);
    if (g_dluma.stop_event) SetEvent(g_dluma.stop_event);

    dluma_native_gameplay_quiesce();
    if (observed_exit_owner_calls)
        dluma_ui_quiesce_from_exit_game_owner(
            observed_exit_owner_calls);
    else
        dluma_ui_quiesce();
    dluma_engine_quiesce(&g_dluma.engine);
    dluma_shutdown_restore_window_hook();
    if (dluma_shutdown_restore_exit_hook())
        dluma_log_line("INFO", "IGame::ExitGame import hook restored.");
    dluma_atomic_exchange(&g_dluma.lifecycle_state, DLUMA_LIFECYCLE_DETACHED);
    dluma_log_line("INFO", "Loader callbacks quiesced before engine teardown.");
}

void dluma_request_quiesce(const char* reason)
{
    dluma_request_quiesce_impl(reason, 0);
}

static void __fastcall dluma_exit_game_hook(void* game_interface)
{
    DlumaExitGameFn original;
    LONG observed_exit_owner_calls;
    LONG current_thread = (LONG)GetCurrentThreadId();
    LONGLONG owner_token = 0;
    observed_exit_owner_calls =
        dluma_atomic_increment(&g_dluma_exit_active_calls);
    if (observed_exit_owner_calls == 1) {
        owner_token =
            _InterlockedIncrement64(
                &g_dluma_exit_owner_generation);
        if (!owner_token)
            owner_token =
                _InterlockedIncrement64(
                    &g_dluma_exit_owner_generation);
        dluma_atomic_exchange(
            &g_dluma_exit_owner_thread_id, current_thread);
        _InterlockedExchange64(
            &g_dluma_exit_owner_active_token, owner_token);
    } else {
        dluma_atomic_exchange(
            &g_dluma_exit_owner_conflict, 1);
    }
    original = g_dluma_exit_original;
    dluma_request_quiesce_impl(
        "IGame::ExitGame (confirmed base-menu quit)",
        observed_exit_owner_calls > 0
            ? (unsigned int)observed_exit_owner_calls : 0U);
    if (original) original(game_interface);
    if (dluma_atomic_decrement(
            &g_dluma_exit_active_calls) == 0) {
        _InterlockedExchange64(
            &g_dluma_exit_owner_active_token, 0);
        dluma_atomic_exchange(
            &g_dluma_exit_owner_thread_id, 0);
    }
}

static LRESULT CALLBACK dluma_game_window_proc(
    HWND window, unsigned int message, WPARAM wparam, LPARAM lparam)
{
    WNDPROC original;
    LRESULT result;
    dluma_atomic_increment(&g_dluma_window_active_calls);
    original = g_dluma_window_original;
    if (message == DLUMA_FRONTEND_ERROR_PRESENT_MESSAGE) {
        dluma_frontend_error_present_on_owner(window);
        dluma_atomic_decrement(&g_dluma_window_active_calls);
        return 0;
    }
    if (message == DLUMA_FRONTEND_ERROR_QUIT_MESSAGE) {
        if (dluma_frontend_error_consume_user_quit(window)) {
            dluma_request_quiesce("user_frontend_error_quit");
            /* This is the ordinary window-close route, explicitly requested
             * by the user. Never enter the blocked native main-menu path. */
            result = original
                ? CallWindowProcW(original, window, WM_CLOSE, 0, 0) : 0;
        } else {
            result = 0;
        }
        dluma_atomic_decrement(&g_dluma_window_active_calls);
        return result;
    }
    dluma_mouse_lock_window_message_pre(
        window, message, wparam, lparam);

    if (message == DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE) {
        const LONG current_thread = (LONG)GetCurrentThreadId();
        DWORD process_id = 0;
        const DWORD owner_thread =
            GetWindowThreadProcessId(window, &process_id);
        const ULONGLONG generation = (ULONGLONG)wparam;
        const ULONGLONG now = GetTickCount64();
        const int exact_owner =
            window == g_dluma_game_window &&
            owner_thread != 0 &&
            current_thread == (LONG)owner_thread &&
            process_id == GetCurrentProcessId();
        int token_delivered = 0;
        if (exact_owner && !g_dluma.stop_requested) {
            dluma_orderly_close_token_lock();
            token_delivered =
                dluma_orderly_close_token_mark_delivered(
                &g_dluma_orderly_close_token,
                generation, (unsigned long)current_thread,
                now, DLUMA_ORDERLY_CLOSE_TOKEN_MAX_AGE_MS);
            dluma_orderly_close_token_unlock();
        }
        const int delivered =
            token_delivered &&
            PostMessageW(
                window, WM_CLOSE, (WPARAM)generation,
                DLUMA_ORDERLY_CLOSE_TAG_MAGIC);
        dluma_atomic_exchange(
            &g_dluma_orderly_close_state,
            delivered ? DLUMA_ORDERLY_CLOSE_DELIVERED
                      : DLUMA_ORDERLY_CLOSE_FAILED);
        dluma_atomic_decrement(&g_dluma_window_active_calls);
        return 0;
    }

    if (message == DLUMA_CONTENT_FINALIZE_MESSAGE) {
        const LONG current_thread = (LONG)GetCurrentThreadId();
        const DWORD actual_window_thread =
            GetWindowThreadProcessId(window, 0);
        LONG state = dluma_atomic_compare_exchange(
            &g_dluma_content_finalize_state,
            DLUMA_CONTENT_FINALIZE_OWNER_DRAINING,
            DLUMA_CONTENT_FINALIZE_REQUESTED);
        int finalized = 0;
        if (state != DLUMA_CONTENT_FINALIZE_REQUESTED) {
            dluma_shutdown_publish_finalize_failure("owner_claim_failed");
        } else if (!actual_window_thread ||
                   current_thread !=
                       (LONG)actual_window_thread ||
                   !dluma_engine_current_thread_can_finalize_content(
                       &g_dluma.engine,
                       (LONG)actual_window_thread)) {
            dluma_shutdown_publish_finalize_failure(
                "validated_window_owner_mismatch");
        } else {
            const LONG exit_owner_calls = dluma_atomic_compare_exchange(
                &g_dluma_content_finalize_exit_owner_calls, 0, 0);
            finalized = exit_owner_calls > 0
                ? dluma_custom_map_asset_trace_game_thread_finalize_from_exit_game_owner(
                      (unsigned int)exit_owner_calls)
                : dluma_custom_map_asset_trace_game_thread_finalize();
        }
        dluma_atomic_exchange(
            &g_dluma_content_finalize_result, finalized ? 1 : -1);
        dluma_atomic_exchange(
            &g_dluma_content_finalize_state,
            finalized ? DLUMA_CONTENT_FINALIZE_COMPLETED
                      : DLUMA_CONTENT_FINALIZE_FAILED);
        if (g_dluma_content_finalize_event)
            SetEvent(g_dluma_content_finalize_event);
        dluma_atomic_decrement(&g_dluma_window_active_calls);
        return 0;
    }

    /* WM_QUERYENDSESSION can still be cancelled after the application votes
     * yes. Keep the loader live until WM_ENDSESSION confirms termination. */
    if (message == WM_CLOSE) {
        const LONG current_thread = (LONG)GetCurrentThreadId();
        const ULONGLONG generation = (ULONGLONG)wparam;
        DlumaOrderlyCloseSnapshot internal = {0};
        int internal_consumed = 0;
        if (lparam == DLUMA_ORDERLY_CLOSE_TAG_MAGIC &&
            window == g_dluma_game_window) {
            dluma_orderly_close_token_lock();
            internal_consumed =
                dluma_orderly_close_token_consume(
                    &g_dluma_orderly_close_token,
                    generation, (unsigned long)current_thread,
                    GetTickCount64(),
                    DLUMA_ORDERLY_CLOSE_TOKEN_MAX_AGE_MS);
            if (internal_consumed)
                (void)dluma_orderly_close_token_snapshot(
                    &g_dluma_orderly_close_token, &internal);
            dluma_orderly_close_token_unlock();
        }
        if (internal_consumed) {
            char close_reason[DLUMA_ORDERLY_CLOSE_REASON_CAPACITY + 48U];
            dluma_str_copy(
                close_reason, DLUMA_ARRAY_COUNT(close_reason),
                dluma_orderly_close_origin_name(internal.origin));
            dluma_str_append(
                close_reason, DLUMA_ARRAY_COUNT(close_reason), ":");
            dluma_str_append(
                close_reason, DLUMA_ARRAY_COUNT(close_reason),
                internal.reason);
            dluma_request_quiesce(close_reason);
        } else {
            /* A raw WM_CLOSE has no keyboard provenance. Alt+F4 is
             * classified at its preceding SC_CLOSE instead. */
            dluma_request_quiesce("window_close_unknown");
        }
    } else if (message == WM_SYSCOMMAND &&
               ((DWORD)wparam & 0xFFF0UL) == SC_CLOSE) {
        const int alt_f4 =
            (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 &&
            (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        dluma_request_quiesce(
            alt_f4 ? "user_alt_f4" : "system_menu_close");
    } else if (message == WM_ENDSESSION && wparam) {
        dluma_request_quiesce("system_session_end");
    }

    result = original
        ? CallWindowProcW(original, window, message, wparam, lparam) : 0;
    dluma_mouse_lock_window_message_post(
        window, message, wparam, lparam);
    dluma_atomic_decrement(&g_dluma_window_active_calls);
    return result;
}

static void dluma_shutdown_restore_window_hook(void)
{
    WNDPROC observed;
    if (!g_dluma_game_window || !g_dluma_window_original) {
        g_dluma_window_detached = 1;
        return;
    }
    observed = (WNDPROC)GetWindowLongPtrW(
        g_dluma_game_window, GWLP_WNDPROC);
    if (observed == g_dluma_window_original) {
        g_dluma_window_detached = 1;
    } else if (observed == dluma_game_window_proc) {
        if ((WNDPROC)SetWindowLongPtrW(
                g_dluma_game_window, GWLP_WNDPROC,
                (LONG_PTR)g_dluma_window_original) == dluma_game_window_proc) {
            g_dluma_window_detached = 1;
            dluma_log_line("INFO", "Dying Light window procedure restored.");
        } else {
            dluma_log_line("WARN", "Dying Light window procedure restore failed; loader callback state is retained.");
        }
    } else {
        dluma_log_line("WARN", "Dying Light window procedure changed ownership; loader callback state is retained in pass-through mode.");
    }
}

int dluma_shutdown_hooks_start(void)
{
    HMODULE gamedll;
    void** slot;
    DWORD old_protect;
    DWORD restored_protect;

    if (g_dluma_exit_iat_slot && g_dluma_exit_original) return 1;
    dluma_mouse_lock_start(
        g_dluma.config.mouse_window_lock_enabled);
    if (!g_dluma_content_finalize_event) {
        g_dluma_content_finalize_event = CreateEventW(0, FALSE, FALSE, 0);
        if (!g_dluma_content_finalize_event) {
            dluma_log_hex(
                "WARN", "Content finalization event creation failed",
                GetLastError());
            return 0;
        }
    }
    g_dluma_content_finalize_state = 0;
    dluma_atomic_exchange(&g_dluma_content_finalize_result, 0);
    dluma_atomic_exchange(&g_dluma_content_finalize_exit_owner_calls, 0);
    _InterlockedExchange64(
        &g_dluma_content_finalize_exit_owner_token, 0);
    dluma_atomic_exchange(&g_dluma_content_finalize_requester_thread, 0);
    dluma_atomic_exchange(
        &g_dluma_orderly_close_state, DLUMA_ORDERLY_CLOSE_IDLE);
    dluma_atomic_exchange(&g_dluma_orderly_close_requester_thread, 0);
    _InterlockedExchange64(&g_dluma_orderly_close_generation, 0);
    dluma_atomic_exchange(&g_dluma_orderly_close_token_lock, 0);
    dluma_orderly_close_token_initialize(
        &g_dluma_orderly_close_token);

    gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    slot = gamedll ? dluma_shutdown_find_exit_iat_slot(gamedll) : 0;
    if (!slot) {
        dluma_log_line("WARN", "Verified IGame::ExitGame import was not found; menu-quit interception is disabled for this build.");
        return 0;
    }
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        dluma_log_hex("WARN", "VirtualProtect failed for IGame::ExitGame IAT slot",
                    GetLastError());
        return 0;
    }
    g_dluma_exit_original = (DlumaExitGameFn)*slot;
    g_dluma_exit_iat_slot = slot;
    g_dluma_exit_active_calls = 0;
    g_dluma_exit_owner_thread_id = 0;
    g_dluma_exit_owner_conflict = 0;
    _InterlockedExchange64(
        &g_dluma_exit_owner_active_token, 0);
    g_dluma_exit_detached = 0;
    *slot = (void*)dluma_exit_game_hook;
    VirtualProtect(slot, sizeof(void*), old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    dluma_log_line("INFO", "Verified IGame::ExitGame import hook installed for orderly menu quits.");
    return 1;
}

void dluma_shutdown_try_bind_window(void)
{
    HWND window;
    WNDPROC observed;
    if (g_dluma_game_window || g_dluma.stop_requested ||
        g_dluma.lifecycle_state != DLUMA_LIFECYCLE_RUNNING)
        return;
    window = dluma_engine_get_window(&g_dluma.engine);
    if (!window) return;
    observed = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (!observed || observed == dluma_game_window_proc) return;
    g_dluma_window_original = observed;
    g_dluma_game_window = window;
    g_dluma_window_active_calls = 0;
    g_dluma_window_detached = 0;
    if ((WNDPROC)SetWindowLongPtrW(
            window, GWLP_WNDPROC, (LONG_PTR)dluma_game_window_proc) != observed) {
        g_dluma_game_window = 0;
        g_dluma_window_original = 0;
        dluma_log_line("WARN", "Dying Light window subclass installation failed; Alt+F4 uses the menu-exit fallback only.");
        return;
    }
    dluma_mouse_lock_bind_window(window);
    dluma_frontend_error_bind_owner_window(window);
    dluma_log_line("INFO", "Dying Light window subclass installed for Alt+F4 and session close.");
}

int dluma_shutdown_schedule_orderly_close_ex(
    DlumaOrderlyCloseOrigin origin,
    const char* reason,
    unsigned long long epoch)
{
    DWORD process_id = 0;
    WNDPROC observed;
    ULONGLONG generation;
    const LONG requester_thread = (LONG)GetCurrentThreadId();
    LONG prior = dluma_atomic_compare_exchange(
        &g_dluma_orderly_close_state,
        DLUMA_ORDERLY_CLOSE_CLAIMED,
        DLUMA_ORDERLY_CLOSE_IDLE);
    if (prior != DLUMA_ORDERLY_CLOSE_IDLE)
        return prior == DLUMA_ORDERLY_CLOSE_CLAIMED ||
            prior == DLUMA_ORDERLY_CLOSE_DELIVERED;
    dluma_atomic_exchange(
        &g_dluma_orderly_close_requester_thread,
        requester_thread);
    if (g_dluma.stop_requested ||
        g_dluma.lifecycle_state != DLUMA_LIFECYCLE_RUNNING ||
        !g_dluma_game_window || !g_dluma_window_original ||
        g_dluma_window_detached) {
        dluma_atomic_exchange(
            &g_dluma_orderly_close_state,
            DLUMA_ORDERLY_CLOSE_FAILED);
        return 0;
    }
    observed = (WNDPROC)GetWindowLongPtrW(
        g_dluma_game_window, GWLP_WNDPROC);
    if (observed != dluma_game_window_proc ||
        !GetWindowThreadProcessId(g_dluma_game_window, &process_id) ||
        process_id != GetCurrentProcessId()) {
        dluma_atomic_exchange(
            &g_dluma_orderly_close_state,
            DLUMA_ORDERLY_CLOSE_FAILED);
        return 0;
    }
    generation = (ULONGLONG)_InterlockedIncrement64(
        &g_dluma_orderly_close_generation);
    if (!generation)
        generation = (ULONGLONG)_InterlockedIncrement64(
            &g_dluma_orderly_close_generation);
    dluma_orderly_close_token_lock();
    prior = dluma_orderly_close_token_publish(
        &g_dluma_orderly_close_token,
        origin, generation, reason,
        (unsigned long)requester_thread,
        epoch, GetTickCount64());
    dluma_orderly_close_token_unlock();
    if (!prior || !PostMessageW(
            g_dluma_game_window,
            DLUMA_ORDERLY_CLOSE_DELIVERY_MESSAGE,
            (WPARAM)generation, 0)) {
        dluma_atomic_exchange(
            &g_dluma_orderly_close_state,
            DLUMA_ORDERLY_CLOSE_FAILED);
        return 0;
    }
    return 1;
}

int dluma_shutdown_schedule_orderly_close(const char* reason)
{
    return dluma_shutdown_schedule_orderly_close_ex(
        DLUMA_CLOSE_ORIGIN_ASSET_ISOLATION_FAIL_CLOSED,
        reason, 0);
}

int dluma_shutdown_snapshot_orderly_close(
    DlumaOrderlyCloseSnapshot* snapshot_out)
{
    int result;
    dluma_orderly_close_token_lock();
    result = dluma_orderly_close_token_snapshot(
        &g_dluma_orderly_close_token, snapshot_out);
    dluma_orderly_close_token_unlock();
    return result;
}

int dluma_shutdown_hooks_stop(void)
{
    ULONGLONG started;
    int error_panel_drained = dluma_frontend_error_stop();
    dluma_shutdown_restore_window_hook();
    (void)dluma_shutdown_restore_exit_hook();
    started = GetTickCount64();
    while ((dluma_atomic_compare_exchange(
                &g_dluma_exit_active_calls, 0, 0) != 0 ||
            dluma_atomic_compare_exchange(
                &g_dluma_window_active_calls, 0, 0) != 0) &&
           GetTickCount64() - started < 2000)
        Sleep(1);
    if (dluma_atomic_compare_exchange(&g_dluma_exit_active_calls, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(&g_dluma_window_active_calls, 0, 0) != 0 ||
        !g_dluma_exit_detached || !g_dluma_window_detached ||
        (!error_panel_drained && !dluma_frontend_error_stop())) {
        dluma_log_line("WARN", "Shutdown callbacks did not fully detach and drain; callback state is retained.");
        return 0;
    }
    dluma_mouse_lock_reset();
    if (g_dluma_exit_detached &&
        dluma_atomic_compare_exchange(&g_dluma_exit_active_calls, 0, 0) == 0)
        g_dluma_exit_original = 0;
    if (g_dluma_window_detached &&
        dluma_atomic_compare_exchange(&g_dluma_window_active_calls, 0, 0) == 0)
        g_dluma_window_original = 0;
    if (g_dluma_exit_detached) g_dluma_exit_iat_slot = 0;
    if (g_dluma_window_detached) g_dluma_game_window = 0;
    if (g_dluma_content_finalize_event) {
        CloseHandle(g_dluma_content_finalize_event);
        g_dluma_content_finalize_event = 0;
    }
    return 1;
}
