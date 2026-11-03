/* Included by the quest graph bridge after its source snapshot helpers. */
#include "dlw_editor_theme_profile.h"

#ifndef DLUMA_QG_MAIN_CLOSE_REPLY
#define DLUMA_QG_MAIN_CLOSE_REPLY (WM_APP + 0x354U)
#endif
#ifndef DLUMA_QG_MAIN_CLOSE_ABORT
#define DLUMA_QG_MAIN_CLOSE_ABORT (WM_APP + 0x355U)
#endif
#define DLUMA_QG_CLOSE_MAX_WINDOWS 4096U
#define DLUMA_QG_CLOSE_CONTROL_CAPACITY 64U
#define DLUMA_QG_CLOSE_FRAME_CAPACITY 16384U

typedef struct DlumaQgCloseVector {
    void** data;
    uint32_t count;
    uint32_t capacity;
    uint32_t allocator_tag;
    uint32_t padding;
} DlumaQgCloseVector;
typedef unsigned char (__fastcall* DlumaQgCloseWindowFn)(void*, void*);
typedef unsigned char (__fastcall* DlumaQgCloseBatchFn)(void*, const DlumaQgCloseVector*);
typedef unsigned char (__fastcall* DlumaQgCloseDocumentFn)(void*, void*, void*);
typedef void* (__fastcall* DlumaQgCloseGetDocumentFn)(void*);

typedef struct DlumaQgCloseDocumentSnapshot {
    char document_id[DLUMA_SHA256_HEX_CAPACITY];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG revision;
} DlumaQgCloseDocumentSnapshot;
typedef struct DlumaQgCloseReply {
    ULONGLONG close_token;
    unsigned int allow_close;
    unsigned int count;
    DlumaQgCloseDocumentSnapshot documents[DLUMA_QG_MAX_DIALOGS];
} DlumaQgCloseReply;
typedef struct DlumaQgCloseTarget {
    HWND dialog;
    HWND scintilla;
    void* native_document;
    ULONGLONG panel_token;
    ULONGLONG binding_generation;
    char native_path[DLUMA_MAX_WPATH * 4U];
    DlumaQgCloseDocumentSnapshot snapshot;
} DlumaQgCloseTarget;
typedef struct DlumaQgCloseWindowIdentity {
    void* window;
    void* document;
    void* vtable;
} DlumaQgCloseWindowIdentity;
enum {
    DLUMA_QG_CLOSE_MAIN = 1,
    DLUMA_QG_CLOSE_WINDOW = 2,
    DLUMA_QG_CLOSE_BATCH = 3,
    DLUMA_QG_CLOSE_DOCUMENT = 4
};
typedef struct DlumaQgCloseOperation {
    unsigned int kind;
    unsigned int target_count;
    unsigned int window_count;
    unsigned int cancelled;
    unsigned int closed_mask;
    HWND main_window;
    void* manager;
    void* document;
    void* close_context;
    ULONGLONG token;
    ULONGLONG epoch;
    DWORD companion_process_id;
    char* finish_frame;
    DlumaQgCloseTarget targets[DLUMA_QG_MAX_DIALOGS];
    DlumaQgCloseWindowIdentity windows[DLUMA_QG_CLOSE_MAX_WINDOWS];
} DlumaQgCloseOperation;
typedef struct DlumaQgCloseControl {
    char* frame;
    DWORD size;
    ULONGLONG epoch;
} DlumaQgCloseControl;
typedef struct DlumaQgCloseHook {
    void* volatile* slot;
    void* original;
    void* replacement;
    unsigned int installed;
    unsigned int protection_pending;
    DWORD original_protection;
} DlumaQgCloseHook;

static struct {
    volatile LONG callbacks;
    volatile LONG stopping;
    volatile LONG active; /* 1: awaiting graph, 2: stock replay, 3: result queued next. */
    volatile LONG abort_requested;
    unsigned int installed;
    unsigned int main_replay_permit;
    unsigned int processing;
    ULONGLONG next_token;
    DlumaQgCloseHook hooks[3];
    DlumaQgCloseOperation* operation; /* Owner UI thread; mailbox reads hold output_lock. */
    DlumaQgCloseReply reply;
    unsigned int reply_ready;
    DlumaQgCloseControl control[DLUMA_QG_CLOSE_CONTROL_CAPACITY];
    unsigned int control_head;
    unsigned int control_count;
} g_dluma_qg_close;

static int dluma_qg_write_frame(HANDLE pipe, const char* json, DWORD size);
static void dluma_qg_close_abort(void);
static void dluma_qg_close_process_reply(void);
static unsigned char __fastcall dluma_qg_close_window_hook(void* manager, void* window);
static unsigned char __fastcall dluma_qg_close_batch_hook(void* manager, const DlumaQgCloseVector* windows);
static unsigned char __fastcall dluma_qg_close_document_hook(void* manager, void* document, void* context);

static int dluma_qg_close_owner(void)
{
    return g_dluma_qg.owner_thread_id &&
        GetCurrentThreadId() == g_dluma_qg.owner_thread_id;
}

static int dluma_qg_close_manager(void* manager)
{
    void** expected = (void**)dluma_editor_theme_profile_address(
        DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x1B70220U, sizeof(void*));
    void* vtable = dluma_editor_theme_profile_address(
        DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x1589418U, 0x140U);
    return expected && manager && *expected == manager &&
        dluma_is_readable(manager, 24U) && *(void**)manager == vtable;
}

/* A registered document anchors borrowed native object identities while close
 * operations are deferred. Never retain the manager's reallocating array. */
static int dluma_qg_close_registered(void* manager, void* document)
{
    void*** records;
    unsigned int count, i;
    if (!document || !dluma_qg_close_manager(manager)) return 0;
    records = *(void****)((BYTE*)manager + 8U);
    count = *(const unsigned int*)((BYTE*)manager + 16U);
    if (count > DLUMA_QG_CLOSE_MAX_WINDOWS ||
        (count && (!records || !dluma_is_readable(records, (SIZE_T)count * sizeof(void*))))) return 0;
    for (i = 0; i < count; ++i)
        if (records[i] && dluma_is_readable(records[i], sizeof(void*)) &&
            records[i][0] == document) return 1;
    return 0;
}

static int dluma_qg_close_window_identity(void* manager, void* window,
    DlumaQgCloseWindowIdentity* result)
{
    void** vtable;
    void* document;
    DlumaQgCloseGetDocumentFn getter;
    if (!window || !dluma_is_readable(window, sizeof(void*))) return 0;
    vtable = *(void***)window;
    if (!vtable || !dluma_is_readable(vtable, sizeof(void*)) ||
        !dluma_qg_executable_pointer(vtable[0])) return 0;
    getter = (DlumaQgCloseGetDocumentFn)vtable[0];
    document = getter(window);
    if (!dluma_qg_close_registered(manager, document)) return 0;
    result->window = window;
    result->document = document;
    result->vtable = vtable;
    return 1;
}

static int dluma_qg_close_queue(char* frame, DWORD size, ULONGLONG epoch)
{
    unsigned int index;
    int accepted = 0;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg_close.control_count < DLUMA_QG_CLOSE_CONTROL_CAPACITY &&
        epoch == g_dluma_qg.companion_epoch) {
        index = (g_dluma_qg_close.control_head + g_dluma_qg_close.control_count) %
            DLUMA_QG_CLOSE_CONTROL_CAPACITY;
        g_dluma_qg_close.control[index].frame = frame;
        g_dluma_qg_close.control[index].size = size;
        g_dluma_qg_close.control[index].epoch = epoch;
        ++g_dluma_qg_close.control_count;
        accepted = 1;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    return accepted;
}

static int dluma_qg_close_number(char* frame, DWORD* used, ULONGLONG value)
{
    char text[32];
    return dluma_u64_to_text(value, text, DLUMA_ARRAY_COUNT(text)) &&
        dluma_qg_json_append(frame, DLUMA_QG_CLOSE_FRAME_CAPACITY, used, text);
}

static char* dluma_qg_close_frame(DlumaQgCloseOperation* operation, int finished, DWORD* size)
{
    char* frame = finished ? operation->finish_frame :
        (char*)dluma_alloc(DLUMA_QG_CLOSE_FRAME_CAPACITY);
    DWORD used = 0;
    char request_id[33];
    unsigned int i, emitted = 0;
    if (!frame) return 0;
    memcpy(request_id, g_dluma_qg.token, 32U);
    request_id[32] = 0;
    frame[0] = 0;
#define QGC_APPEND(value) dluma_qg_json_append(frame, DLUMA_QG_CLOSE_FRAME_CAPACITY, &used, (value))
    if (!dluma_qg_response_begin(frame, DLUMA_QG_CLOSE_FRAME_CAPACITY, &used,
            request_id, finished ? "closeFinished" : "closePreflight") ||
        !QGC_APPEND("{\"closeToken\":") ||
        !dluma_qg_close_number(frame, &used, operation->token)) goto failed;
    if (finished) {
        if (!QGC_APPEND(operation->cancelled ? ",\"cancelled\":true,\"closedDocumentIds\":[" :
                ",\"cancelled\":false,\"closedDocumentIds\":[")) goto failed;
    } else if (!QGC_APPEND(operation->kind == DLUMA_QG_CLOSE_MAIN ?
            ",\"closeAll\":true,\"documents\":[" : ",\"closeAll\":false,\"documents\":[")) goto failed;
    for (i = 0; i < operation->target_count; ++i) {
        const DlumaQgCloseDocumentSnapshot* snapshot = &operation->targets[i].snapshot;
        if (finished && !(operation->closed_mask & (1U << i))) continue;
        if (emitted++ && !QGC_APPEND(",")) goto failed;
        if (finished) {
            if (!QGC_APPEND("\"") || !QGC_APPEND(snapshot->document_id) || !QGC_APPEND("\"")) goto failed;
        } else if (!QGC_APPEND("{\"documentId\":\"") || !QGC_APPEND(snapshot->document_id) ||
            !QGC_APPEND("\",\"revision\":") || !dluma_qg_close_number(frame, &used, snapshot->revision) ||
            !QGC_APPEND(",\"sha256\":\"") || !QGC_APPEND(snapshot->sha256) || !QGC_APPEND("\"}")) goto failed;
    }
    if (!QGC_APPEND("]}}")) goto failed;
    *size = used;
#undef QGC_APPEND
    return frame;
failed:
#undef QGC_APPEND
    if (!finished) dluma_free(frame);
    return 0;
}

/* A Save As keeps its graph eligible for a close choice. Publishing its current
 * text does not change the saved path identity or grant a save to that path. */
static int dluma_qg_close_flush_source(DlumaQgDialog* slot)
{
    char* source = 0;
    char* frame = 0;
    DWORD source_size = 0, frame_size = 0;
    char sha[DLUMA_SHA256_HEX_CAPACITY];
    int result = 1;
    dluma_qg_publish_saved_source(slot);
    dluma_qg_publish_source_changed(slot);
    if (!dluma_qg_scintilla_read(slot->scintilla, &source, &source_size)) return 0;
    dluma_sha256_bytes_hex(source, source_size, sha);
    if (dluma_stricmp(sha, slot->source_sha256)) {
        if (slot->revision == UINT64_MAX) { result = 0; goto done; }
        ++slot->revision;
        dluma_str_copy(slot->source_sha256, DLUMA_ARRAY_COUNT(slot->source_sha256), sha);
        result = dluma_qg_build_source_changed_frame(slot, source, source_size, &frame, &frame_size) &&
            dluma_qg_queue_document(frame, frame_size, slot->panel.token, 0);
        if (result) frame = 0;
    }
done:
    if (frame) dluma_free(frame);
    dluma_free(source);
    return result;
}

static int dluma_qg_close_relevant(const DlumaQgCloseOperation* operation,
    void* document, void* window)
{
    unsigned int i;
    if (operation->kind == DLUMA_QG_CLOSE_MAIN) return 1;
    if (operation->kind == DLUMA_QG_CLOSE_DOCUMENT) return document == operation->document;
    for (i = 0; i < operation->window_count; ++i)
        if (operation->windows[i].window == window && operation->windows[i].document == document) return 1;
    return 0;
}

static int dluma_qg_close_collect(DlumaQgCloseOperation* operation)
{
    unsigned int i;
    for (i = 0; i < DLUMA_QG_MAX_DIALOGS; ++i) {
        DlumaQgDialog* slot = &g_dluma_qg.dialogs[i];
        DlumaQgCloseTarget* target;
        void* object = 0;
        void* document = 0;
        char path[DLUMA_MAX_WPATH * 4U];
        ULONGLONG generation;
        if (!slot->dialog || !slot->scintilla || !IsWindow(slot->dialog) ||
            !dluma_qg_document_admitted(slot)) continue;
        if (!dluma_qg_native_document(slot, &object, &document, path, DLUMA_ARRAY_COUNT(path))) return 0;
        if (!dluma_qg_close_relevant(operation, document, (BYTE*)object + 560U)) continue;
        generation = dluma_editor_panel_host_binding_generation(&slot->panel);
        if (!generation || !dluma_qg_close_flush_source(slot)) return 0;
        target = &operation->targets[operation->target_count++];
        target->dialog = slot->dialog;
        target->scintilla = slot->scintilla;
        target->native_document = document;
        target->panel_token = slot->panel.token;
        target->binding_generation = generation;
        dluma_str_copy(target->native_path, DLUMA_ARRAY_COUNT(target->native_path), path);
        dluma_str_copy(target->snapshot.document_id, DLUMA_ARRAY_COUNT(target->snapshot.document_id), slot->document_id);
        dluma_str_copy(target->snapshot.sha256, DLUMA_ARRAY_COUNT(target->snapshot.sha256), slot->source_sha256);
        target->snapshot.revision = slot->revision;
    }
    return 1;
}

/* Returns one when stock must not execute the intercepted operation. */
static int dluma_qg_close_begin(unsigned int kind, HWND main_window, void* manager,
    void* argument, const DlumaQgCloseVector* windows, void* context)
{
    DlumaQgCloseOperation* operation;
    char* frame;
    DWORD frame_size = 0;
    unsigned int i;
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0)) return 1;
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.stopping, 0, 0) ||
        !g_dluma_qg_close.installed ||
        !dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0)) return 0;
    if (!dluma_qg_close_owner() || !dluma_qg_close_manager(manager)) return 1;
    operation = (DlumaQgCloseOperation*)dluma_alloc(sizeof(*operation));
    if (!operation) return 1;
    memset(operation, 0, sizeof(*operation));
    operation->finish_frame = (char*)dluma_alloc(DLUMA_QG_CLOSE_FRAME_CAPACITY);
    if (!operation->finish_frame) goto refused;
    operation->kind = kind;
    operation->main_window = main_window;
    operation->manager = manager;
    operation->document = kind == DLUMA_QG_CLOSE_DOCUMENT ? argument : 0;
    operation->close_context = context;
    if (kind == DLUMA_QG_CLOSE_WINDOW) {
        operation->window_count = 1;
        if (!dluma_qg_close_window_identity(manager, argument, &operation->windows[0])) goto refused;
    } else if (kind == DLUMA_QG_CLOSE_BATCH) {
        if (!windows || !dluma_is_readable(windows, 16U) ||
            windows->count > DLUMA_QG_CLOSE_MAX_WINDOWS || windows->count > windows->capacity ||
            (windows->count && (!windows->data ||
             !dluma_is_readable(windows->data, (SIZE_T)windows->count * sizeof(void*))))) goto refused;
        operation->window_count = windows->count;
        for (i = 0; i < windows->count; ++i)
            if (!dluma_qg_close_window_identity(manager, windows->data[i], &operation->windows[i])) goto refused;
    } else if (kind == DLUMA_QG_CLOSE_DOCUMENT && !dluma_qg_close_registered(manager, argument)) goto refused;
    if (!dluma_qg_close_collect(operation)) goto refused;
    if (!operation->target_count && kind != DLUMA_QG_CLOSE_MAIN) {
        dluma_free(operation->finish_frame);
        dluma_free(operation);
        return 0;
    }
    /* Non-null borrowed progress contexts are never carried across a yield for
     * another document class. The qualified PXSL cleanup ignores this argument. */
    if (kind == DLUMA_QG_CLOSE_DOCUMENT && context) {
        void* expected = dluma_editor_theme_profile_address(
            DLUMA_EDITOR_THEME_PROFILE_QUEST, 0xBD128U, 0xD0U);
        void* cleanup = dluma_editor_theme_profile_address(
            DLUMA_EDITOR_THEME_PROFILE_QUEST, 0x1C3B0U, 3U);
        if (!expected || !cleanup || *(void**)argument != expected ||
            ((void**)expected)[13] != cleanup || memcmp(cleanup, "\xC2\x00\x00", 3U)) goto refused;
        operation->close_context = 0;
    }
    if (g_dluma_qg_close.next_token == UINT64_MAX) goto refused;
    operation->token = ++g_dluma_qg_close.next_token;
    operation->epoch = g_dluma_qg.companion_epoch;
    operation->companion_process_id = g_dluma_qg.companion_process_id;
    frame = dluma_qg_close_frame(operation, 0, &frame_size);
    if (!frame) goto refused;
    /* Publish transaction before its notification can reach the pipe worker. */
    dluma_qg_lock(&g_dluma_qg.output_lock);
    /* Reserve the result's FIFO position before yielding or destroying an HWND. */
    if (g_dluma_qg_close.control_count > DLUMA_QG_CLOSE_CONTROL_CAPACITY - 2U) {
        dluma_qg_unlock(&g_dluma_qg.output_lock);
        dluma_free(frame);
        goto refused;
    }
    g_dluma_qg_close.operation = operation;
    g_dluma_qg_close.reply_ready = 0;
    dluma_atomic_exchange(&g_dluma_qg_close.abort_requested, 0);
    dluma_atomic_exchange(&g_dluma_qg_close.active, 1);
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (!dluma_qg_close_queue(frame, frame_size, operation->epoch)) {
        dluma_free(frame);
        dluma_qg_lock(&g_dluma_qg.output_lock);
        g_dluma_qg_close.operation = 0;
        dluma_atomic_exchange(&g_dluma_qg_close.active, 0);
        dluma_qg_unlock(&g_dluma_qg.output_lock);
        goto refused;
    }
    return 1;
refused:
    if (operation->finish_frame) dluma_free(operation->finish_frame);
    dluma_free(operation);
    return 1;
}

static unsigned char __fastcall dluma_qg_close_window_hook(void* manager, void* window)
{
    unsigned char result = 0;
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try {
        if (!dluma_qg_close_begin(DLUMA_QG_CLOSE_WINDOW, 0, manager, window, 0, 0))
            result = ((DlumaQgCloseWindowFn)g_dluma_qg_close.hooks[1].original)(manager, window);
    } __finally {
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
    return result;
}
static unsigned char __fastcall dluma_qg_close_batch_hook(void* manager, const DlumaQgCloseVector* windows)
{
    unsigned char result = 0;
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try {
        if (!dluma_qg_close_begin(DLUMA_QG_CLOSE_BATCH, 0, manager, 0, windows, 0))
            result = ((DlumaQgCloseBatchFn)g_dluma_qg_close.hooks[0].original)(manager, windows);
    } __finally {
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
    return result;
}
static unsigned char __fastcall dluma_qg_close_document_hook(void* manager, void* document, void* context)
{
    unsigned char result = 0;
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try {
        if (!dluma_qg_close_begin(DLUMA_QG_CLOSE_DOCUMENT, 0, manager, document, 0, context))
            result = ((DlumaQgCloseDocumentFn)g_dluma_qg_close.hooks[2].original)(manager, document, context);
    } __finally {
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
    return result;
}

static int dluma_qg_close_begin_main(HWND window)
{
    void** manager;
    if (dluma_qg_close_owner() && g_dluma_qg_close.main_replay_permit &&
        g_dluma_qg_close.operation && g_dluma_qg_close.operation->main_window == window) {
        g_dluma_qg_close.main_replay_permit = 0;
        return 0;
    }
    manager = (void**)dluma_editor_theme_profile_address(
        DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x1B70220U, sizeof(void*));
    return dluma_qg_close_begin(DLUMA_QG_CLOSE_MAIN, window, manager ? *manager : 0, 0, 0, 0);
}

static int dluma_qg_close_snapshot_parse(DlumaJsonReader* reader,
    DlumaQgCloseDocumentSnapshot* snapshot)
{
    char field[48];
    unsigned int seen = 0;
    int first = 1;
    uint64_t revision;
    if (!dluma_json_object_begin(reader)) return 0;
    while (dluma_json_object_next(reader, &first)) {
        if (!dluma_json_string(reader, field, sizeof(field)) || !dluma_json_take(reader, ':')) return 0;
        if (!strcmp(field, "documentId")) {
            if ((seen & 1U) || !dluma_json_string(reader, snapshot->document_id, sizeof(snapshot->document_id)) ||
                !dluma_qg_hash_valid(snapshot->document_id)) return 0;
            seen |= 1U;
        } else if (!strcmp(field, "revision")) {
            if ((seen & 2U) || !dluma_json_u64(reader, &revision) || revision > 0x7FFFFFFFFFFFFFFFULL) return 0;
            snapshot->revision = revision; seen |= 2U;
        } else if (!strcmp(field, "sha256")) {
            if ((seen & 4U) || !dluma_json_string(reader, snapshot->sha256, sizeof(snapshot->sha256)) ||
                !dluma_qg_hash_valid(snapshot->sha256)) return 0;
            seen |= 4U;
        } else return 0;
    }
    return seen == 7U && dluma_json_object_end(reader);
}

static int dluma_qg_close_parse_reply(const char* json, DWORD bytes, DlumaQgCloseReply* reply)
{
    DlumaJsonReader reader;
    char field[48];
    int first = 1, allow;
    unsigned int seen = 0, i;
    uint64_t token;
    if (!json || !reply || !bytes || bytes > DLUMA_QG_CLOSE_FRAME_CAPACITY) return 0;
    memset(reply, 0, sizeof(*reply));
    dluma_json_reader_initialize(&reader, json, bytes);
    if (!dluma_json_object_begin(&reader)) return 0;
    while (dluma_json_object_next(&reader, &first)) {
        if (!dluma_json_string(&reader, field, sizeof(field)) || !dluma_json_take(&reader, ':')) return 0;
        if (!strcmp(field, "closeToken")) {
            if ((seen & 1U) || !dluma_json_u64(&reader, &token) || !token) return 0;
            reply->close_token = token; seen |= 1U;
        } else if (!strcmp(field, "allowClose")) {
            if ((seen & 2U) || !dluma_json_boolean(&reader, &allow)) return 0;
            reply->allow_close = allow ? 1U : 0U; seen |= 2U;
        } else if (!strcmp(field, "documents")) {
            int array_first = 1;
            if ((seen & 4U) || !dluma_json_array_begin(&reader)) return 0;
            seen |= 4U;
            while (dluma_json_array_next(&reader, &array_first)) {
                if (reply->count == DLUMA_QG_MAX_DIALOGS ||
                    !dluma_qg_close_snapshot_parse(&reader, &reply->documents[reply->count])) return 0;
                for (i = 0; i < reply->count; ++i)
                    if (!dluma_stricmp(reply->documents[i].document_id,
                            reply->documents[reply->count].document_id)) return 0;
                ++reply->count;
            }
            if (!dluma_json_array_end(&reader)) return 0;
        } else return 0;
    }
    return seen == 7U && dluma_json_object_end(&reader) && dluma_json_finished(&reader);
}

/* Pipe thread only publishes a mailbox; it never waits for a UI dispatch. */
static int dluma_qg_close_accept_reply(const DlumaQgCloseReply* reply)
{
    int accepted = 0;
    HWND window = g_dluma_qg.main_window;
    if (!reply || !window) return 0;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg_close.operation &&
        dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) == 1 &&
        !g_dluma_qg_close.reply_ready &&
        g_dluma_qg_close.operation->token == reply->close_token &&
        g_dluma_qg_close.operation->epoch == g_dluma_qg.companion_epoch &&
        dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0)) {
        g_dluma_qg_close.reply = *reply;
        g_dluma_qg_close.reply_ready = 1;
        accepted = 1;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (accepted && !PostMessageW(window, DLUMA_QG_MAIN_CLOSE_REPLY, 0, 0)) {
        dluma_qg_lock(&g_dluma_qg.output_lock);
        if (g_dluma_qg_close.reply.close_token == reply->close_token)
            g_dluma_qg_close.reply_ready = 0;
        dluma_qg_unlock(&g_dluma_qg.output_lock);
        accepted = 0;
    }
    return accepted;
}

static DlumaQgDialog* dluma_qg_close_target_slot(const DlumaQgCloseTarget* target)
{
    unsigned int i;
    for (i = 0; i < DLUMA_QG_MAX_DIALOGS; ++i) {
        DlumaQgDialog* slot = &g_dluma_qg.dialogs[i];
        if (slot->dialog == target->dialog && slot->scintilla == target->scintilla &&
            slot->panel.token == target->panel_token &&
            !dluma_stricmp(slot->document_id, target->snapshot.document_id)) return slot;
    }
    return 0;
}

static int dluma_qg_close_revalidate(DlumaQgCloseOperation* operation, const DlumaQgCloseReply* reply)
{
    unsigned int i, j;
    if (!dluma_qg_close_manager(operation->manager) ||
        operation->epoch != g_dluma_qg.companion_epoch ||
        operation->companion_process_id != g_dluma_qg.companion_process_id ||
        !dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0) ||
        reply->count != operation->target_count) return 0;
    if (operation->kind == DLUMA_QG_CLOSE_MAIN &&
        (!IsWindow(operation->main_window) || operation->main_window != g_dluma_qg.main_window)) return 0;
    if (operation->document && !dluma_qg_close_registered(operation->manager, operation->document)) return 0;
    for (i = 0; i < operation->window_count; ++i) {
        DlumaQgCloseWindowIdentity current;
        if (!dluma_qg_close_registered(operation->manager, operation->windows[i].document) ||
            !dluma_qg_close_window_identity(operation->manager, operation->windows[i].window, &current) ||
            current.document != operation->windows[i].document || current.vtable != operation->windows[i].vtable) return 0;
    }
    for (i = 0; i < operation->target_count; ++i) {
        DlumaQgCloseTarget* target = &operation->targets[i];
        DlumaQgDialog* slot = dluma_qg_close_target_slot(target);
        void* document = 0;
        char path[DLUMA_MAX_WPATH * 4U];
        if (!slot || !dluma_qg_document_admitted(slot) ||
            dluma_editor_panel_host_binding_generation(&slot->panel) != target->binding_generation ||
            !dluma_qg_native_document(slot, 0, &document, path, DLUMA_ARRAY_COUNT(path)) ||
            document != target->native_document || dluma_stricmp(path, target->native_path) ||
            !dluma_qg_close_flush_source(slot)) return 0;
        for (j = 0; j < reply->count; ++j)
            if (!dluma_stricmp(reply->documents[j].document_id, target->snapshot.document_id)) break;
        if (j == reply->count || slot->revision != reply->documents[j].revision ||
            dluma_stricmp(slot->source_sha256, reply->documents[j].sha256)) return 0;
    }
    return 1;
}

static void dluma_qg_close_finish(DlumaQgCloseOperation* operation)
{
    char* frame;
    DWORD size = 0;
    unsigned int i;
    /* A cancelled/failed stock prompt can still have saved another document. */
    for (i = 0; i < operation->target_count; ++i) {
        DlumaQgDialog* slot = dluma_qg_close_target_slot(&operation->targets[i]);
        if (slot) {
            dluma_qg_publish_saved_source(slot);
            dluma_qg_publish_source_changed(slot);
        }
    }
    if (operation->epoch == g_dluma_qg.companion_epoch &&
        dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0)) {
        frame = dluma_qg_close_frame(operation, 1, &size);
        if (!frame || !dluma_qg_close_queue(frame, size, operation->epoch)) {
            dluma_atomic_exchange(&g_dluma_qg_close.active, 3);
            return;
        }
        operation->finish_frame = 0;
    }
    dluma_qg_lock(&g_dluma_qg.output_lock);
    g_dluma_qg_close.operation = 0;
    g_dluma_qg_close.reply_ready = 0;
    dluma_atomic_exchange(&g_dluma_qg_close.active, 0);
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (operation->finish_frame) dluma_free(operation->finish_frame);
    dluma_free(operation);
}

static void dluma_qg_close_process_reply_impl(void)
{
    DlumaQgCloseOperation* operation;
    DlumaQgCloseReply reply;
    unsigned int ready, i;
    unsigned char stock_result = 1;
    if (!dluma_qg_close_owner()) return;
    operation = g_dluma_qg_close.operation;
    if (!operation) return;
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) == 3) {
        dluma_qg_close_finish(operation); return;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) != 1) return;
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.abort_requested, 0, 0)) {
        operation->cancelled = 1;
        operation->closed_mask = 0;
        dluma_qg_close_finish(operation);
        return;
    }
    dluma_qg_lock(&g_dluma_qg.output_lock);
    ready = g_dluma_qg_close.reply_ready;
    reply = g_dluma_qg_close.reply;
    g_dluma_qg_close.reply_ready = 0;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (!ready || reply.close_token != operation->token) return;
    /* Cancel never requires documents to survive or still match their snapshots. */
    if (!reply.allow_close || !dluma_qg_close_revalidate(operation, &reply) ||
        dluma_atomic_compare_exchange(&g_dluma_qg_close.abort_requested, 0, 0) ||
        dluma_atomic_compare_exchange(&g_dluma_qg_close.stopping, 0, 0)) {
        operation->cancelled = 1;
        dluma_qg_close_finish(operation);
        return;
    }
    dluma_atomic_exchange(&g_dluma_qg_close.active, 2);
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try {
        if (operation->kind == DLUMA_QG_CLOSE_MAIN) {
            g_dluma_qg_close.main_replay_permit = 1;
            (void)SendMessageW(operation->main_window, WM_CLOSE, 0, 0);
            stock_result = !IsWindow(operation->main_window) || !IsWindowVisible(operation->main_window);
        } else if (operation->kind == DLUMA_QG_CLOSE_WINDOW) {
            stock_result = ((DlumaQgCloseWindowFn)g_dluma_qg_close.hooks[1].original)(
                operation->manager, operation->windows[0].window);
        } else if (operation->kind == DLUMA_QG_CLOSE_BATCH) {
            void* windows[DLUMA_QG_CLOSE_MAX_WINDOWS];
            DlumaQgCloseVector vector;
            for (i = 0; i < operation->window_count; ++i) windows[i] = operation->windows[i].window;
            memset(&vector, 0, sizeof(vector));
            vector.data = windows;
            vector.count = vector.capacity = operation->window_count;
            vector.allocator_tag = 76U;
            stock_result = ((DlumaQgCloseBatchFn)g_dluma_qg_close.hooks[0].original)(operation->manager, &vector);
        } else {
            stock_result = ((DlumaQgCloseDocumentFn)g_dluma_qg_close.hooks[2].original)(
                operation->manager, operation->document, operation->close_context);
        }
    } __finally {
        g_dluma_qg_close.main_replay_permit = 0;
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
    operation->cancelled = stock_result ? 0U : 1U;
    operation->closed_mask = 0;
    for (i = 0; i < operation->target_count; ++i) {
        const DlumaQgCloseTarget* target = &operation->targets[i];
        unsigned int j;
        int closed = !IsWindow(target->dialog);
        if (!closed) for (j = 0; j < DLUMA_QG_MAX_DIALOGS; ++j) {
            DlumaQgDialog* slot = &g_dluma_qg.dialogs[j];
            void* document = 0;
            char path[DLUMA_MAX_WPATH * 4U];
            if (slot->dialog == target->dialog &&
                dluma_qg_native_document(slot, 0, &document, path, DLUMA_ARRAY_COUNT(path))) {
                closed = document != target->native_document;
                break;
            }
        }
        /* A panel rebind is not evidence that its native document closed. */
        if (closed) operation->closed_mask |= 1U << i;
        else operation->cancelled = 1U;
    }
    dluma_qg_close_finish(operation);
}

static void dluma_qg_close_process_reply(void)
{
    if (!dluma_qg_close_owner() || g_dluma_qg_close.processing) return;
    g_dluma_qg_close.processing = 1;
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try { dluma_qg_close_process_reply_impl(); }
    __finally {
        g_dluma_qg_close.processing = 0;
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
}

static void dluma_qg_close_abort(void)
{
    DlumaQgCloseOperation* operation;
    dluma_atomic_exchange(&g_dluma_qg_close.abort_requested, 1);
    if (!dluma_qg_close_owner()) {
        if (g_dluma_qg.main_window) (void)PostMessageW(g_dluma_qg.main_window, DLUMA_QG_MAIN_CLOSE_ABORT, 0, 0);
        return;
    }
    if (g_dluma_qg_close.processing) return;
    operation = g_dluma_qg_close.operation;
    if (!operation || dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) == 2) return;
    g_dluma_qg_close.processing = 1;
    dluma_atomic_increment(&g_dluma_qg_close.callbacks);
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try {
        if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) != 3) {
            operation->cancelled = 1;
            operation->closed_mask = 0;
        }
        dluma_qg_close_finish(operation);
    } __finally {
        g_dluma_qg_close.processing = 0;
        dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        dluma_atomic_decrement(&g_dluma_qg_close.callbacks);
    }
}

/* Called by the single pipe sender after draining its document queues. */
static int dluma_qg_send_control_pending(HANDLE pipe)
{
    DlumaQgCloseControl control;
    unsigned int i;
    int ready = 1;
    memset(&control, 0, sizeof(control));
    if (!dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0)) return 1;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
        DlumaQgPendingDocument* entry = &g_dluma_qg.pending_documents[i];
        if (entry->open_frame || entry->update_frame || entry->saved_frame) { ready = 0; break; }
    }
    if (ready && g_dluma_qg_close.control_count) {
        control = g_dluma_qg_close.control[g_dluma_qg_close.control_head];
        memset(&g_dluma_qg_close.control[g_dluma_qg_close.control_head], 0, sizeof(control));
        g_dluma_qg_close.control_head = (g_dluma_qg_close.control_head + 1U) % DLUMA_QG_CLOSE_CONTROL_CAPACITY;
        --g_dluma_qg_close.control_count;
        if (control.epoch != g_dluma_qg.companion_epoch) ready = 0;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (control.frame) {
        int result = !ready || dluma_qg_write_frame(pipe, control.frame, control.size);
        dluma_free(control.frame);
        if (!result) return 0;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0) == 3 && g_dluma_qg.main_window)
        (void)PostMessageW(g_dluma_qg.main_window, DLUMA_QG_MAIN_CLOSE_REPLY, 0, 0);
    return 1;
}

/* Disconnect resets the transport mailbox. The UI owns deferred operations. */
static void dluma_qg_close_reset(void)
{
    unsigned int i;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    for (i = 0; i < DLUMA_QG_CLOSE_CONTROL_CAPACITY; ++i) {
        if (g_dluma_qg_close.control[i].frame) dluma_free(g_dluma_qg_close.control[i].frame);
        memset(&g_dluma_qg_close.control[i], 0, sizeof(g_dluma_qg_close.control[i]));
    }
    g_dluma_qg_close.control_head = g_dluma_qg_close.control_count = 0;
    g_dluma_qg_close.reply_ready = 0;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    dluma_qg_close_abort();
}

static int dluma_qg_close_data_slot(void* volatile* slot)
{
    MEMORY_BASIC_INFORMATION memory;
    DWORD protection;
    if (!slot || ((ULONG_PTR)slot & 7U) ||
        VirtualQuery((void*)slot, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (BYTE*)slot + sizeof(void*) > (BYTE*)memory.BaseAddress + memory.RegionSize) return 0;
    protection = memory.Protect & 0xFFU;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY;
}

static int dluma_qg_close_restore_hook(DlumaQgCloseHook* hook)
{
    DWORD protection, restored;
    void* observed;
    if (!hook->installed && !hook->protection_pending) return 1;
    if (!dluma_qg_close_data_slot(hook->slot)) return 0;
    observed = dluma_atomic_pointer_load(hook->slot);
    if (observed == hook->replacement) {
        if (!VirtualProtect((void*)hook->slot, sizeof(void*), PAGE_READWRITE, &protection)) return 0;
        if (!hook->protection_pending) hook->original_protection = protection;
        hook->protection_pending = 1;
        observed = dluma_atomic_pointer_compare_exchange(hook->slot, hook->original, hook->replacement);
        if (observed != hook->replacement) return 0;
    } else if (observed != hook->original || !hook->protection_pending) return 0;
    if (!VirtualProtect((void*)hook->slot, sizeof(void*), hook->original_protection, &restored)) return 0;
    hook->protection_pending = 0;
    hook->installed = 0;
    return 1;
}

static int dluma_qg_close_install(void)
{
    static const DWORD slot_rvas[3] = { 0x15894E8U, 0x15894F0U, 0x1589528U };
    static const DWORD method_rvas[3] = { 0x321D30U, 0x321C10U, 0x3206D0U };
    static const BYTE prefixes[3][16] = {
        {0x48,0x8B,0xC4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8B,0xEC,0x48},
        {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48},
        {0x40,0x57,0x48,0x83,0xEC,0x60,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,0xFF,0x48}
    };
    void* replacements[3];
    unsigned int i;
    HMODULE pinned;
    if (g_dluma_qg_close.installed) return 1;
    if (g_dluma_qg_close.callbacks || g_dluma_qg_close.active || !dluma_qg_close_owner() ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) return 0;
    replacements[0] = (void*)(ULONG_PTR)dluma_qg_close_batch_hook;
    replacements[1] = (void*)(ULONG_PTR)dluma_qg_close_window_hook;
    replacements[2] = (void*)(ULONG_PTR)dluma_qg_close_document_hook;
    for (i = 0; i < 3U; ++i) {
        DlumaQgCloseHook* hook = &g_dluma_qg_close.hooks[i];
        void* expected = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, method_rvas[i], 16U);
        if (hook->installed || hook->protection_pending) return 0;
        hook->slot = (void* volatile*)dluma_editor_theme_profile_address(
            DLUMA_EDITOR_THEME_PROFILE_EDITOR, slot_rvas[i], sizeof(void*));
        if (!expected || !dluma_qg_executable_pointer(expected) ||
            memcmp(expected, prefixes[i], sizeof(prefixes[i])) ||
            !dluma_qg_close_data_slot(hook->slot) || dluma_atomic_pointer_load(hook->slot) != expected) return 0;
        hook->original = expected;
        hook->replacement = replacements[i];
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(ULONG_PTR)dluma_qg_close_window_hook, &pinned)) return 0;
    dluma_atomic_exchange(&g_dluma_qg_close.stopping, 0);
    for (i = 0; i < 3U; ++i) {
        DlumaQgCloseHook* hook = &g_dluma_qg_close.hooks[i];
        DWORD protection, restored;
        void* observed;
        if (!VirtualProtect((void*)hook->slot, sizeof(void*), PAGE_READWRITE, &protection)) break;
        hook->original_protection = protection;
        hook->protection_pending = 1;
        observed = dluma_atomic_pointer_compare_exchange(hook->slot, hook->replacement, hook->original);
        if (observed == hook->original) hook->installed = 1;
        if (!VirtualProtect((void*)hook->slot, sizeof(void*), protection, &restored)) break;
        hook->protection_pending = 0;
        if (!hook->installed) break;
    }
    if (i != 3U) {
        unsigned int index = 3U;
        dluma_atomic_exchange(&g_dluma_qg_close.stopping, 1);
        while (index) (void)dluma_qg_close_restore_hook(&g_dluma_qg_close.hooks[--index]);
        return 0;
    }
    g_dluma_qg_close.installed = 1;
    return 1;
}

static int dluma_qg_close_stop(void)
{
    unsigned int i;
    int complete = 1;
    dluma_atomic_exchange(&g_dluma_qg_close.stopping, 1);
    dluma_qg_close_abort();
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.active, 0, 0)) return 0;
    for (i = 3U; i; --i)
        if (!dluma_qg_close_restore_hook(&g_dluma_qg_close.hooks[i - 1U])) complete = 0;
    if (dluma_atomic_compare_exchange(&g_dluma_qg_close.callbacks, 0, 0)) complete = 0;
    if (complete) g_dluma_qg_close.installed = 0;
    return complete;
}
