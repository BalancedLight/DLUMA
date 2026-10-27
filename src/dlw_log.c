#include "dlw_log.h"

#define DLUMA_LOG_WRITER_BATCH_CAPACITY (64U * 1024U)
#define DLUMA_LOG_FLUSH_INTERVAL_MS 1000ULL
#define DLUMA_LOG_STOP_TIMEOUT_MS 3000UL

typedef struct DlumaLogSlot {
    volatile LONG sequence;
    ULONGLONG tick_ms;
    char level[DLUMA_LOG_HISTORY_LEVEL_CAPACITY];
    char message[DLUMA_LOG_RECORD_CAPACITY];
    char text[DLUMA_LOG_RECORD_CAPACITY];
} DlumaLogSlot;

typedef struct DlumaLogState {
    DlumaLogSlot slots[DLUMA_LOG_QUEUE_CAPACITY];
    volatile LONG enqueue_position;
    volatile LONG dequeue_position;
    volatile LONG active_producers;
    volatile LONG dropped_lines;
    volatile LONG started;
    volatile LONG stop_requested;
    volatile LONG retained_for_process_exit;
    HANDLE wake_event;
    HANDLE writer_thread;
    HANDLE append_handle;
    HANDLE emergency_handle;
} DlumaLogState;

static DlumaLogState g_dluma_log;
static DlumaLogHistoryStore g_dluma_log_history;

static void dluma_log_restore_dropped(LONG count)
{
    LONG current;
    LONG observed;
    if (count <= 0) return;
    current = g_dluma_log.dropped_lines;
    do {
        observed = dluma_atomic_compare_exchange(
            &g_dluma_log.dropped_lines, current + count, current);
        if (observed == current) return;
        current = observed;
    } while (1);
}

static int dluma_log_queue_empty(void)
{
    LONG position = g_dluma_log.dequeue_position;
    DlumaLogSlot* slot = &g_dluma_log.slots[(DWORD)position &
                                       (DLUMA_LOG_QUEUE_CAPACITY - 1U)];
    return slot->sequence != position + 1;
}

static int dluma_log_has_pending(void)
{
    return !dluma_log_queue_empty() ||
        g_dluma_log.active_producers > 0 ||
        g_dluma_log.dropped_lines > 0;
}

static int dluma_log_enqueue(
    const char* text,
    ULONGLONG tick_ms,
    const char* level,
    const char* message)
{
    LONG position;
    DlumaLogSlot* slot;
    LONG sequence;
    LONG difference;
    unsigned int spins = 0;
    if (!text || !text[0] || g_dluma_log.started != 2 ||
        g_dluma_log.stop_requested)
        return 0;
    dluma_atomic_increment(&g_dluma_log.active_producers);
    if (g_dluma_log.started != 2 || g_dluma_log.stop_requested) {
        dluma_atomic_decrement(&g_dluma_log.active_producers);
        return 0;
    }
    position = g_dluma_log.enqueue_position;
    for (;;) {
        slot = &g_dluma_log.slots[(DWORD)position &
                                (DLUMA_LOG_QUEUE_CAPACITY - 1U)];
        sequence = slot->sequence;
        difference = sequence - position;
        if (difference == 0) {
            if (dluma_atomic_compare_exchange(
                    &g_dluma_log.enqueue_position, position + 1,
                    position) == position)
                break;
            position = g_dluma_log.enqueue_position;
        } else if (difference < 0 || ++spins >= DLUMA_LOG_QUEUE_CAPACITY) {
            dluma_atomic_increment(&g_dluma_log.dropped_lines);
            dluma_atomic_decrement(&g_dluma_log.active_producers);
            return 0;
        } else {
            position = g_dluma_log.enqueue_position;
        }
    }
    slot->tick_ms = tick_ms;
    dluma_str_copy(
        slot->level, DLUMA_ARRAY_COUNT(slot->level),
        level && level[0] ? level : "INFO");
    dluma_str_copy(
        slot->message, DLUMA_ARRAY_COUNT(slot->message),
        message ? message : "");
    dluma_str_copy(slot->text, DLUMA_ARRAY_COUNT(slot->text), text);
    dluma_atomic_exchange(&slot->sequence, position + 1);
    if (g_dluma_log.wake_event) SetEvent(g_dluma_log.wake_event);
    dluma_atomic_decrement(&g_dluma_log.active_producers);
    return 1;
}

static unsigned int dluma_log_dequeue_batch(
    char* batch, unsigned int capacity)
{
    unsigned int used = 0;
    for (;;) {
        LONG position = g_dluma_log.dequeue_position;
        DlumaLogSlot* slot =
            &g_dluma_log.slots[(DWORD)position &
                             (DLUMA_LOG_QUEUE_CAPACITY - 1U)];
        LONG sequence = slot->sequence;
        unsigned int length;
        if (sequence != position + 1) break;
        length = (unsigned int)strlen(slot->text);
        if (length > capacity - used) {
            if (used) break;
            length = capacity;
        }
        if (length) memcpy(batch + used, slot->text, length);
        used += length;
        dluma_log_history_store_publish(
            &g_dluma_log_history,
            slot->tick_ms,
            slot->level,
            slot->message);
        slot->text[0] = 0;
        slot->level[0] = 0;
        slot->message[0] = 0;
        dluma_atomic_exchange(
            &slot->sequence, position + (LONG)DLUMA_LOG_QUEUE_CAPACITY);
        dluma_atomic_exchange(&g_dluma_log.dequeue_position, position + 1);
        if (used == capacity) break;
    }
    return used;
}

static unsigned int dluma_log_append_drop_warning(
    char* batch, unsigned int used, unsigned int capacity)
{
    LONG dropped = dluma_atomic_exchange(&g_dluma_log.dropped_lines, 0);
    char number[32];
    char warning[256];
    unsigned int length;
    if (dropped <= 0) return used;
    warning[0] = 0;
    dluma_str_append(warning, DLUMA_ARRAY_COUNT(warning),
                   "[LOGGER] [WARN] dropped ");
    dluma_u64_to_text((ULONGLONG)(DWORD)dropped, number,
                    DLUMA_ARRAY_COUNT(number));
    dluma_str_append(warning, DLUMA_ARRAY_COUNT(warning), number);
    dluma_str_append(warning, DLUMA_ARRAY_COUNT(warning),
                   " log record(s) while the bounded queue was full\r\n");
    length = (unsigned int)strlen(warning);
    if (length <= capacity - used) {
        const char* history_message = warning +
            (sizeof("[LOGGER] [WARN] ") - 1U);
        memcpy(batch + used, warning, length);
        warning[length - 2U] = 0;
        dluma_log_history_store_publish(
            &g_dluma_log_history,
            GetTickCount64(),
            "WARN",
            history_message);
        return used + length;
    }
    dluma_log_restore_dropped(dropped);
    return used;
}

static DWORD WINAPI dluma_log_writer_thread(LPVOID parameter)
{
    char batch[DLUMA_LOG_WRITER_BATCH_CAPACITY + 1U];
    ULONGLONG last_flush_ms = GetTickCount64();
    int dirty = 0;
    (void)parameter;
    for (;;) {
        unsigned int used;
        DWORD written = 0;
        ULONGLONG now;
        if (g_dluma_log.wake_event)
            (void)WaitForSingleObject(g_dluma_log.wake_event, 1000);
        if (g_dluma_log.wake_event) ResetEvent(g_dluma_log.wake_event);
        do {
            used = dluma_log_dequeue_batch(
                batch, DLUMA_LOG_WRITER_BATCH_CAPACITY);
            if (used)
                used = dluma_log_append_drop_warning(
                    batch, used, DLUMA_LOG_WRITER_BATCH_CAPACITY);
            else if (dluma_log_queue_empty())
                used = dluma_log_append_drop_warning(
                    batch, 0, DLUMA_LOG_WRITER_BATCH_CAPACITY);
            if (used && g_dluma_log.append_handle != INVALID_HANDLE_VALUE) {
                batch[used] = 0;
                OutputDebugStringA(batch);
                if (WriteFile(g_dluma_log.append_handle, batch, used,
                              &written, 0) &&
                    written == used)
                    dirty = 1;
            }
        } while (dluma_log_has_pending());
        now = GetTickCount64();
        if (dirty && now - last_flush_ms >= DLUMA_LOG_FLUSH_INTERVAL_MS) {
            (void)FlushFileBuffers(g_dluma_log.append_handle);
            dirty = 0;
            last_flush_ms = now;
        }
        if (g_dluma_log.stop_requested && dluma_log_queue_empty() &&
            g_dluma_log.active_producers == 0) {
            if (dirty)
                (void)FlushFileBuffers(g_dluma_log.append_handle);
            return 0;
        }
    }
}

int dluma_log_start(const WCHAR* path)
{
    unsigned int i;
    HANDLE truncate_handle;
    DWORD thread_id = 0;
    if (!path || !path[0]) return 0;
    if (g_dluma_log.started == 2) return 1;
    if (dluma_atomic_compare_exchange(&g_dluma_log.started, 1, 0) != 0)
        return g_dluma_log.started == 2;
    dluma_log_history_store_start_session(
        &g_dluma_log_history, GetTickCount64());
    memset(&g_dluma_log.slots, 0, sizeof(g_dluma_log.slots));
    for (i = 0; i < DLUMA_LOG_QUEUE_CAPACITY; ++i)
        g_dluma_log.slots[i].sequence = (LONG)i;
    g_dluma_log.enqueue_position = 0;
    g_dluma_log.dequeue_position = 0;
    g_dluma_log.active_producers = 0;
    g_dluma_log.dropped_lines = 0;
    g_dluma_log.stop_requested = 0;
    g_dluma_log.retained_for_process_exit = 0;
    g_dluma_log.append_handle = INVALID_HANDLE_VALUE;
    g_dluma_log.emergency_handle = INVALID_HANDLE_VALUE;
    truncate_handle = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (truncate_handle == INVALID_HANDLE_VALUE) goto fail;
    CloseHandle(truncate_handle);
    g_dluma_log.append_handle = CreateFileW(
        path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (g_dluma_log.append_handle == INVALID_HANDLE_VALUE) goto fail;
    g_dluma_log.emergency_handle = CreateFileW(
        path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (g_dluma_log.emergency_handle == INVALID_HANDLE_VALUE) goto fail;
    g_dluma_log.wake_event = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_log.wake_event) goto fail;
    g_dluma_log.writer_thread = CreateThread(
        0, 256U * 1024U, dluma_log_writer_thread, 0, 0, &thread_id);
    if (!g_dluma_log.writer_thread) goto fail;
    dluma_atomic_exchange(&g_dluma_log.started, 2);
    return 1;

fail:
    if (g_dluma_log.writer_thread) CloseHandle(g_dluma_log.writer_thread);
    if (g_dluma_log.wake_event) CloseHandle(g_dluma_log.wake_event);
    if (g_dluma_log.emergency_handle != INVALID_HANDLE_VALUE)
        CloseHandle(g_dluma_log.emergency_handle);
    if (g_dluma_log.append_handle != INVALID_HANDLE_VALUE)
        CloseHandle(g_dluma_log.append_handle);
    memset(&g_dluma_log, 0, sizeof(g_dluma_log));
    dluma_log_history_store_reset(&g_dluma_log_history);
    return 0;
}

int dluma_log_stop(void)
{
    DWORD result;
    if (g_dluma_log.started != 2) return g_dluma_log.started == 0;
    dluma_atomic_exchange(&g_dluma_log.stop_requested, 1);
    if (g_dluma_log.wake_event) SetEvent(g_dluma_log.wake_event);
    result = WaitForSingleObject(
        g_dluma_log.writer_thread, DLUMA_LOG_STOP_TIMEOUT_MS);
    if (result != WAIT_OBJECT_0) {
        HMODULE pinned = 0;
        dluma_atomic_exchange(&g_dluma_log.retained_for_process_exit, 1);
        (void)GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(const void*)&dluma_log_stop, &pinned);
        dluma_log_emergency(
            "[LOGGER] [ERROR] writer drain timed out; logger and loader are retained until process exit\r\n");
        return 0;
    }
    CloseHandle(g_dluma_log.writer_thread);
    CloseHandle(g_dluma_log.wake_event);
    CloseHandle(g_dluma_log.append_handle);
    CloseHandle(g_dluma_log.emergency_handle);
    memset(&g_dluma_log, 0, sizeof(g_dluma_log));
    dluma_log_history_store_reset(&g_dluma_log_history);
    return 1;
}

void dluma_log_emergency(const char* message)
{
    DWORD length;
    DWORD offset = 0;
    unsigned int attempts = 0;
    if (!message || !message[0]) return;
    OutputDebugStringA(message);
    length = (DWORD)strlen(message);
    if (g_dluma_log.emergency_handle != INVALID_HANDLE_VALUE &&
        g_dluma_log.emergency_handle) {
        while (offset < length && attempts < 4U) {
            DWORD written = 0;
            if (!WriteFile(g_dluma_log.emergency_handle, message + offset,
                           length - offset, &written, 0) ||
                written == 0)
                break;
            offset += written;
            ++attempts;
        }
    }
}

void dluma_log_line(const char* level, const char* message)
{
    char line[2048];
    char number[32];
    ULONGLONG tick_ms = GetTickCount64();
    line[0] = 0;
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "[");
    dluma_u64_to_text(tick_ms, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), number);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " ms] [");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), level ? level : "INFO");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "] ");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), message ? message : "");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "\r\n");

    if (!dluma_log_enqueue(line, tick_ms, level, message))
        OutputDebugStringA(line);
    if (level && dluma_stricmp(level, "ERROR") == 0)
        dluma_ui_report_error(message ? message : "Unknown loader error");
}

ULONGLONG dluma_log_history_revision(void)
{
    return dluma_log_history_store_revision(&g_dluma_log_history);
}

unsigned int dluma_log_history_snapshot(
    DlumaLogHistoryEntry *out,
    unsigned int capacity,
    ULONGLONG *revision_out,
    ULONGLONG *published_total_out)
{
    return dluma_log_history_store_snapshot(
        &g_dluma_log_history,
        out,
        capacity,
        revision_out,
        published_total_out);
}

void dluma_log_pair(const char* level, const char* label, const char* value)
{
    char line[1200];
    line[0] = 0;
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), label ? label : "value");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " = ");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), value ? value : "");
    dluma_log_line(level, line);
}

void dluma_log_u64(const char* level, const char* label, ULONGLONG value)
{
    char number[32];
    dluma_u64_to_text(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_log_pair(level, label, number);
}

void dluma_log_hex(const char* level, const char* label, DWORD value)
{
    char number[16];
    dluma_u32_hex(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_log_pair(level, label, number);
}
