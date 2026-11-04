#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "dlw_editor_wiki_session.h"
#include "dlw_editor_companion_runtime.h"
#include "core/dlw_json_reader.h"

extern int dluma_sha256_file_hex(const WCHAR *, ULONGLONG, char *);
extern int dluma_project_regular_file_is_contained(const WCHAR *, const WCHAR *);
extern void dluma_log_pair(const char *, const char *, const char *);

#define WIKI_PATH_CAP 32768U
#define WIKI_FRAME_CAP 8192U
#define WIKI_POLL_MS 10U
#define WIKI_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct WikiSession {
    volatile LONG references;
    SRWLOCK lock;
    HWND owner;
    DWORD owner_thread, child_pid;
    DlumaEditorPanelRef panel;
    uint64_t serial, request_serial;
    HANDLE pipe, child, worker, stop, reply;
    void *runtime_lease;
    WCHAR *theme_config;
    HWND pending_child;
    uint64_t pending_serial, reply_generation;
    int pending, reply_bound;
    char token[65], child_hash[65], editor_hash[65];
    WCHAR pipe_short[128], pipe_full[160];
} WikiSession;

static WikiSession *g_session;
static volatile LONG g_sessions;
static volatile LONG64 g_serial;
static UINT g_dispatch_message;

static int wiki_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static unsigned char wiki_ascii_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int wiki_equal_ascii_fold(const char *a, const char *b)
{
    while (*a && wiki_ascii_fold((unsigned char)*a) == wiki_ascii_fold((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}

static int wiki_append(char *out, size_t capacity, const char *text)
{
    size_t used = 0, i = 0;
    while (used < capacity && out[used]) ++used;
    if (used == capacity) return 0;
    while (text[i]) {
        if (used + 1U >= capacity) return 0;
        out[used++] = text[i++];
    }
    out[used] = 0;
    return 1;
}

static int wiki_append_u64(char *out, size_t capacity, uint64_t value)
{
    char digits[21]; size_t count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (count) {
        char next[2]; next[0] = digits[--count]; next[1] = 0;
        if (!wiki_append(out, capacity, next)) return 0;
    }
    return 1;
}

static int wiki_wappend(WCHAR *out, size_t capacity, const WCHAR *text)
{
    size_t used = 0, i = 0;
    while (used < capacity && out[used]) ++used;
    if (used == capacity) return 0;
    while (text[i]) {
        if (used + 1U >= capacity) return 0;
        out[used++] = text[i++];
    }
    out[used] = 0;
    return 1;
}

static int wiki_wappend_ascii(WCHAR *out, size_t capacity, const char *text)
{
    size_t used = 0, i = 0;
    while (used < capacity && out[used]) ++used;
    if (used == capacity) return 0;
    while (text[i]) {
        if ((unsigned char)text[i] > 0x7f || used + 1U >= capacity) return 0;
        out[used++] = (WCHAR)(unsigned char)text[i++];
    }
    out[used] = 0;
    return 1;
}

static int wiki_wappend_u64(WCHAR *out, size_t capacity, uint64_t value)
{
    char digits[21] = {0};
    return wiki_append_u64(digits, sizeof(digits), value) && wiki_wappend_ascii(out, capacity, digits);
}

static void wiki_release(WikiSession *s)
{
    if (InterlockedDecrement(&s->references)) return;
    if (s->pipe && s->pipe != INVALID_HANDLE_VALUE) CloseHandle(s->pipe);
    if (s->child) CloseHandle(s->child);
    if (s->worker) CloseHandle(s->worker);
    if (s->stop) CloseHandle(s->stop);
    if (s->reply) CloseHandle(s->reply);
    if (s->theme_config) HeapFree(GetProcessHeap(), 0, s->theme_config);
    if (s->runtime_lease) dluma_editor_companion_runtime_release(s->runtime_lease);
    SecureZeroMemory(s->token, sizeof(s->token));
    HeapFree(GetProcessHeap(), 0, s);
    InterlockedDecrement(&g_sessions);
}

static int wiki_random(WikiSession *s)
{
    BYTE bytes[32]; unsigned int i; static const char hex[] = "0123456789abcdef";
    if (BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
    for (i = 0; i < sizeof(bytes); ++i) {
        s->token[i * 2U] = hex[bytes[i] >> 4];
        s->token[i * 2U + 1U] = hex[bytes[i] & 15U];
    }
    s->token[64] = 0;
    SecureZeroMemory(bytes, sizeof(bytes));
    return 1;
}

static HANDLE wiki_pipe(WikiSession *s)
{
    HANDLE process_token = NULL, pipe = INVALID_HANDLE_VALUE;
    BYTE user_buffer[1024], acl_buffer[1024];
    DWORD needed = 0, acl_size; TOKEN_USER *user; PACL acl = (PACL)acl_buffer;
    SECURITY_DESCRIPTOR descriptor; SECURITY_ATTRIBUTES attributes;
    WCHAR prefix[17]; unsigned int i;
    for (i = 0; i < 16U; ++i) prefix[i] = (WCHAR)s->token[i];
    prefix[16] = 0;
    if (!wiki_wappend(s->pipe_short, WIKI_COUNT(s->pipe_short), L"DLUMA.EditorWiki.") ||
        !wiki_wappend_u64(s->pipe_short, WIKI_COUNT(s->pipe_short), GetCurrentProcessId()) ||
        !wiki_wappend(s->pipe_short, WIKI_COUNT(s->pipe_short), L".") ||
        !wiki_wappend(s->pipe_short, WIKI_COUNT(s->pipe_short), prefix) ||
        !wiki_wappend(s->pipe_full, WIKI_COUNT(s->pipe_full), L"\\\\.\\pipe\\") ||
        !wiki_wappend(s->pipe_full, WIKI_COUNT(s->pipe_full), s->pipe_short))
        return INVALID_HANDLE_VALUE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token) ||
        !GetTokenInformation(process_token, TokenUser, user_buffer, sizeof(user_buffer), &needed)) goto done;
    user = (TOKEN_USER *)user_buffer;
    acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(user->User.Sid);
    if (acl_size > sizeof(acl_buffer) || !InitializeAcl(acl, acl_size, ACL_REVISION) ||
        !AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, user->User.Sid) ||
        !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)) goto done;
    attributes.nLength = sizeof(attributes); attributes.lpSecurityDescriptor = &descriptor; attributes.bInheritHandle = FALSE;
    pipe = CreateNamedPipeW(s->pipe_full, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 16384, 16384, 0, &attributes);
done:
    if (process_token) CloseHandle(process_token);
    return pipe;
}

static int wiki_env_key(const WCHAR *entry, const WCHAR *key)
{
    size_t i = 0;
    if (entry[0] == L'=') return 0;
    while (key[i] && entry[i]) {
        WCHAR c = entry[i], k = key[i];
        if (c >= L'A' && c <= L'Z') c = (WCHAR)(c + (L'a' - L'A'));
        if (k >= L'A' && k <= L'Z') k = (WCHAR)(k + (L'a' - L'A'));
        if (c != k) return 0;
        ++i;
    }
    return key[i] == 0 && entry[i] == L'=';
}

static WCHAR *wiki_environment(const WCHAR *root)
{
    LPWCH inherited = GetEnvironmentStringsW(); const WCHAR *cursor; const WCHAR **entries = NULL;
    WCHAR a[WIKI_PATH_CAP], b[WIKI_PATH_CAP]; WCHAR c[] = L"DOTNET_MULTILEVEL_LOOKUP=0";
    WCHAR *result = NULL, *out; size_t count = 0, used = 0, i, total = 1;
    if (!inherited) return NULL;
    a[0] = b[0] = 0;
    if (!wiki_wappend(a, WIKI_COUNT(a), L"DOTNET_ROOT=") || !wiki_wappend(a, WIKI_COUNT(a), root) ||
        !wiki_wappend(b, WIKI_COUNT(b), L"DOTNET_ROOT_X64=") || !wiki_wappend(b, WIKI_COUNT(b), root)) goto done;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1) ++count;
    entries = HeapAlloc(GetProcessHeap(), 0, (count + 3U) * sizeof(*entries));
    if (!entries) goto done;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1)
        if (!wiki_env_key(cursor, L"DOTNET_ROOT") && !wiki_env_key(cursor, L"DOTNET_ROOT_X64") &&
            !wiki_env_key(cursor, L"DOTNET_MULTILEVEL_LOOKUP")) entries[used++] = cursor;
    entries[used++] = a; entries[used++] = b; entries[used++] = c;
    for (i = 1; i < used; ++i) {
        const WCHAR *value = entries[i]; size_t j = i;
        while (j && CompareStringOrdinal(entries[j - 1], -1, value, -1, TRUE) == CSTR_GREATER_THAN) {
            entries[j] = entries[j - 1]; --j;
        }
        entries[j] = value;
    }
    for (i = 0; i < used; ++i) {
        size_t n = wcslen(entries[i]) + 1;
        if (n > 32767U || total > 32767U - n) goto done;
        total += n;
    }
    result = HeapAlloc(GetProcessHeap(), 0, total * sizeof(WCHAR));
    if (!result) goto done;
    out = result;
    for (i = 0; i < used; ++i) { size_t n = wcslen(entries[i]) + 1; memcpy(out, entries[i], n * sizeof(WCHAR)); out += n; }
    *out = 0;
done:
    if (entries) HeapFree(GetProcessHeap(), 0, entries);
    FreeEnvironmentStringsW(inherited);
    return result;
}

static int wiki_safe_arg(const WCHAR *value)
{
    const WCHAR *p;
    if (!value || !*value) return 0;
    for (p = value; *p && (size_t)(p - value) < WIKI_PATH_CAP; ++p)
        if (*p == L'"' || *p < 32) return 0;
    if ((size_t)(p - value) == WIKI_PATH_CAP) return 0;
    return p[-1] != L'\\';
}

static int wiki_command(WCHAR *command, size_t capacity, const WCHAR *executable,
                        const WCHAR *pipe, const char *token, DWORD editor_pid,
                        const char *editor_hash, uint64_t start, uint64_t parent,
                        uint64_t panel_token, const WCHAR *theme)
{
    if (!capacity) return 0;
    command[0] = 0;
    return wiki_wappend(command, capacity, L"\"") && wiki_wappend(command, capacity, executable) &&
        wiki_wappend(command, capacity, L"\" --pipe \"") && wiki_wappend(command, capacity, pipe) &&
        wiki_wappend(command, capacity, L"\" --token \"") && wiki_wappend_ascii(command, capacity, token) &&
        wiki_wappend(command, capacity, L"\" --editor-pid ") && wiki_wappend_u64(command, capacity, editor_pid) &&
        wiki_wappend(command, capacity, L" --editor-sha256 ") && wiki_wappend_ascii(command, capacity, editor_hash) &&
        wiki_wappend(command, capacity, L" --editor-start-time ") && wiki_wappend_u64(command, capacity, start) &&
        wiki_wappend(command, capacity, L" --panel-parent ") && wiki_wappend_u64(command, capacity, parent) &&
        wiki_wappend(command, capacity, L" --panel-token ") && wiki_wappend_u64(command, capacity, panel_token) &&
        wiki_wappend(command, capacity, L" --theme-config \"") && wiki_wappend(command, capacity, theme) &&
        wiki_wappend(command, capacity, L"\"");
}

static int wiki_launch(WikiSession *s, const WCHAR *theme)
{
    HMODULE module = NULL; WCHAR *module_path = NULL, *runtime = NULL, *dotnet = NULL, *directory = NULL;
    WCHAR *executable = NULL, *command = NULL, *environment = NULL, *slash;
    WCHAR editor_path[WIKI_PATH_CAP]; FILETIME created, exited, kernel, user; ULARGE_INTEGER start;
    STARTUPINFOW startup; PROCESS_INFORMATION process; DWORD n; int ok = 0;
    if (!wiki_safe_arg(theme) || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(ULONG_PTR)wiki_launch, &module)) return 0;
    module_path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    runtime = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    dotnet = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    directory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    executable = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, WIKI_PATH_CAP * sizeof(WCHAR));
    if (!module_path || !runtime || !dotnet || !directory || !executable || !command) goto done;
    n = GetModuleFileNameW(module, module_path, WIKI_PATH_CAP);
    if (!n || n >= WIKI_PATH_CAP || !(slash = wcsrchr(module_path, L'\\'))) goto done;
    *slash = 0;
    if (!wiki_wappend(runtime, WIKI_PATH_CAP, module_path) || !wiki_wappend(runtime, WIKI_PATH_CAP, L"\\dluma_runtime") ||
        !wiki_wappend(dotnet, WIKI_PATH_CAP, runtime) || !wiki_wappend(dotnet, WIKI_PATH_CAP, L"\\dotnet") ||
        !wiki_wappend(directory, WIKI_PATH_CAP, runtime) || !wiki_wappend(directory, WIKI_PATH_CAP, L"\\editor-wiki") ||
        !wiki_wappend(executable, WIKI_PATH_CAP, directory) || !wiki_wappend(executable, WIKI_PATH_CAP, L"\\Dluma.EditorWiki.exe") ||
        !(s->runtime_lease = dluma_editor_companion_runtime_acquire(runtime, "10.0.13")) ||
        !wiki_safe_arg(executable) || !dluma_project_regular_file_is_contained(module_path, executable) ||
        !dluma_sha256_file_hex(executable, 64ULL * 1024ULL * 1024ULL, s->child_hash)) goto done;
    n = GetModuleFileNameW(NULL, editor_path, WIKI_COUNT(editor_path));
    if (!n || n >= WIKI_COUNT(editor_path) || !dluma_sha256_file_hex(editor_path, 128ULL * 1024ULL * 1024ULL, s->editor_hash) ||
        !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) goto done;
    start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
    if (!wiki_command(command, WIKI_PATH_CAP, executable, s->pipe_short, s->token,
            GetCurrentProcessId(), s->editor_hash, start.QuadPart,
            (uint64_t)(ULONG_PTR)s->panel.content_window, s->panel.token, theme)) goto done;
    environment = wiki_environment(dotnet);
    if (!environment || WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT) goto done;
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup); ZeroMemory(&process, sizeof(process));
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                        environment, directory, &startup, &process)) goto done;
    CloseHandle(process.hThread); s->child = process.hProcess; s->child_pid = process.dwProcessId; ok = 1;
done:
    if (environment) HeapFree(GetProcessHeap(), 0, environment);
    if (command) { SecureZeroMemory(command, WIKI_PATH_CAP * sizeof(WCHAR)); HeapFree(GetProcessHeap(), 0, command); }
    if (executable) HeapFree(GetProcessHeap(), 0, executable);
    if (directory) HeapFree(GetProcessHeap(), 0, directory);
    if (dotnet) HeapFree(GetProcessHeap(), 0, dotnet);
    if (runtime) HeapFree(GetProcessHeap(), 0, runtime);
    if (module_path) HeapFree(GetProcessHeap(), 0, module_path);
    return ok;
}

typedef struct WikiRequest {
    char type[24], request[33], token[65], hash[65];
    uint64_t version, client, editor, parent, child, panel_token;
} WikiRequest;

static int wiki_parse_payload(DlumaJsonReader *reader, WikiRequest *request)
{
    unsigned int fields = 0; int first = 1, next;
    if (!dluma_json_object_begin(reader)) return 0;
    while ((next = dluma_json_object_next(reader, &first)) > 0) {
        char key[32]; unsigned int bit = 0; uint64_t *number = NULL;
        if (!dluma_json_string(reader, key, sizeof(key)) || !dluma_json_take(reader, ':')) return 0;
        if (wiki_equal(request->type, "hello")) {
            if (wiki_equal(key, "protocolVersion")) { bit = 1; number = &request->version; }
            else if (wiki_equal(key, "clientProcessId")) { bit = 2; number = &request->client; }
            else if (wiki_equal(key, "editorProcessId")) { bit = 4; number = &request->editor; }
            else if (wiki_equal(key, "executableSha256")) bit = 8;
        } else if (wiki_equal(request->type, "panelReady")) {
            if (wiki_equal(key, "parentWindow")) { bit = 1; number = &request->parent; }
            else if (wiki_equal(key, "childWindow")) { bit = 2; number = &request->child; }
            else if (wiki_equal(key, "panelToken")) { bit = 4; number = &request->panel_token; }
        }
        if (!bit || (fields & bit)) return 0;
        fields |= bit;
        if (number) { if (!dluma_json_u64(reader, number)) return 0; }
        else if (!dluma_json_string(reader, request->hash, sizeof(request->hash))) return 0;
    }
    return next == 0 && dluma_json_object_end(reader) &&
        fields == (wiki_equal(request->type, "hello") ? 15U : 7U);
}

static int wiki_equal_token(const char *a, const char *b)
{
    unsigned int i, mismatch = 0;
    if (strlen(a) != 64 || strlen(b) != 64) return 0;
    for (i = 0; i < 64; ++i) mismatch |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return mismatch == 0;
}

static int wiki_parse(char *json, DWORD size, WikiSession *session, WikiRequest *request)
{
    DlumaJsonReader reader, payload_reader; unsigned int fields = 0, i; int first = 1, next;
    const unsigned char *payload = NULL, *end = NULL;
    ZeroMemory(request, sizeof(*request)); dluma_json_reader_initialize(&reader, json, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    while ((next = dluma_json_object_next(&reader, &first)) > 0) {
        char key[32]; unsigned int bit;
        if (!dluma_json_string(&reader, key, sizeof(key)) || !dluma_json_take(&reader, ':')) return 0;
        if (wiki_equal(key, "schemaVersion")) {
            uint64_t version = 0; bit = 1;
            if ((fields & bit) || !dluma_json_u64(&reader, &version) || version != 2) return 0;
        } else if (wiki_equal(key, "type")) {
            bit = 2; if ((fields & bit) || !dluma_json_string(&reader, request->type, sizeof(request->type))) return 0;
        } else if (wiki_equal(key, "requestId")) {
            bit = 4; if ((fields & bit) || !dluma_json_string(&reader, request->request, sizeof(request->request))) return 0;
        } else if (wiki_equal(key, "sessionToken")) {
            bit = 8; if ((fields & bit) || !dluma_json_string(&reader, request->token, sizeof(request->token))) return 0;
        } else if (wiki_equal(key, "payload")) {
            bit = 16; dluma_json_space(&reader); payload = reader.cursor;
            if ((fields & bit) || !dluma_json_skip_value(&reader)) return 0;
            end = reader.cursor;
        } else return 0;
        fields |= bit;
    }
    if (next != 0 || !dluma_json_object_end(&reader) || !dluma_json_finished(&reader) || fields != 31U ||
        !payload || !end || end <= payload || !wiki_equal_token(request->token, session->token) ||
        strlen(request->request) != 32 || (!wiki_equal(request->type, "hello") && !wiki_equal(request->type, "panelReady"))) return 0;
    for (i = 0; i < 32; ++i)
        if (!((request->request[i] >= '0' && request->request[i] <= '9') ||
              (request->request[i] >= 'a' && request->request[i] <= 'f'))) return 0;
    dluma_json_reader_initialize(&payload_reader, payload, (unsigned int)(end - payload));
    return wiki_parse_payload(&payload_reader, request) && dluma_json_finished(&payload_reader);
}

static int wiki_io(WikiSession *s, void *buffer, DWORD length, int writing, DWORD timeout)
{
    BYTE *bytes = (BYTE *)buffer; DWORD offset = 0, n; ULONGLONG start = GetTickCount64();
    while (offset < length && GetTickCount64() - start < timeout) {
        BOOL success;
        if (WaitForSingleObject(s->stop, 0) == WAIT_OBJECT_0 ||
            WaitForSingleObject(s->child, 0) != WAIT_TIMEOUT) return 0;
        n = 0;
        success = writing ? WriteFile(s->pipe, bytes + offset, length - offset, &n, NULL) :
                            ReadFile(s->pipe, bytes + offset, length - offset, &n, NULL);
        if (success && n) { offset += n; continue; }
        if (!success && GetLastError() != ERROR_NO_DATA && GetLastError() != ERROR_PIPE_LISTENING) return 0;
        (void)WaitForSingleObject(s->stop, WIKI_POLL_MS);
    }
    return offset == length;
}

static int wiki_response(char *frame, size_t capacity, const char *type,
                         const char *request, const char *token, const char *payload)
{
    if (!capacity) return 0;
    frame[0] = 0;
    return wiki_append(frame, capacity, "{\"schemaVersion\":2,\"type\":\"") &&
        wiki_append(frame, capacity, type) && wiki_append(frame, capacity, "\",\"requestId\":\"") &&
        wiki_append(frame, capacity, request) && wiki_append(frame, capacity, "\",\"sessionToken\":\"") &&
        wiki_append(frame, capacity, token) && wiki_append(frame, capacity, "\",\"payload\":") &&
        wiki_append(frame, capacity, payload) && wiki_append(frame, capacity, "}");
}

static int wiki_send(WikiSession *s, const char *type, const char *request, const char *payload)
{
    char frame[2048]; DWORD length; BYTE prefix[4];
    if (!wiki_response(frame, sizeof(frame), type, request, s->token, payload)) return 0;
    length = (DWORD)strlen(frame); prefix[0] = (BYTE)length; prefix[1] = (BYTE)(length >> 8);
    prefix[2] = (BYTE)(length >> 16); prefix[3] = (BYTE)(length >> 24);
    return wiki_io(s, prefix, sizeof(prefix), 1, 5000) && wiki_io(s, frame, length, 1, 5000);
}

static int wiki_hello_payload(char *payload, size_t capacity, DWORD editor_pid, const char *hash)
{
    if (!capacity) return 0;
    payload[0] = 0;
    return wiki_append(payload, capacity, "{\"protocolVersion\":2,\"editorProcessId\":") &&
        wiki_append_u64(payload, capacity, editor_pid) &&
        wiki_append(payload, capacity, ",\"hostBuildId\":\"") && wiki_append(payload, capacity, hash) &&
        wiki_append(payload, capacity, "\",\"projectRoot\":null,\"installedCatalogRoots\":[],\"contextGeneration\":0}");
}

static int wiki_panel_payload(char *payload, size_t capacity, int bound, uint64_t generation)
{
    if (!capacity) return 0;
    payload[0] = 0;
    return wiki_append(payload, capacity, bound ? "{\"bound\":true,\"bindingGeneration\":" : "{\"bound\":false,\"bindingGeneration\":") &&
        wiki_append_u64(payload, capacity, generation) &&
        wiki_append(payload, capacity, bound ? ",\"message\":\"Wiki panel connected.\"}" :
                                              ",\"message\":\"Wiki panel could not be opened.\"}");
}

static int wiki_read_request(WikiSession *s, WikiRequest *request)
{
    BYTE prefix[4]; DWORD length; char frame[WIKI_FRAME_CAP];
    if (!wiki_io(s, prefix, sizeof(prefix), 0, 15000)) return 0;
    length = (DWORD)prefix[0] | ((DWORD)prefix[1] << 8) | ((DWORD)prefix[2] << 16) | ((DWORD)prefix[3] << 24);
    return length && length <= sizeof(frame) && wiki_io(s, frame, length, 0, 15000) &&
        wiki_parse(frame, length, s, request);
}

static int wiki_bind_request(WikiSession *s, const WikiRequest *request, uint64_t *generation)
{
    HANDLE waits[2]; DWORD result; uint64_t serial;
    if (request->parent != (uint64_t)(ULONG_PTR)s->panel.content_window ||
        request->panel_token != s->panel.token || !request->child || request->child > INT64_MAX) return 0;
    AcquireSRWLockExclusive(&s->lock);
    serial = ++s->request_serial;
    if (!serial) serial = ++s->request_serial;
    s->pending_child = (HWND)(ULONG_PTR)request->child; s->pending_serial = serial;
    s->pending = 1; s->reply_bound = 0; s->reply_generation = 0; ResetEvent(s->reply);
    ReleaseSRWLockExclusive(&s->lock);
    if (!PostMessageW(s->owner, g_dispatch_message, (WPARAM)serial, (LPARAM)s->serial)) return 0;
    waits[0] = s->stop; waits[1] = s->reply;
    result = WaitForMultipleObjects(2, waits, FALSE, 5000);
    AcquireSRWLockExclusive(&s->lock);
    if (result != WAIT_OBJECT_0 + 1 || s->pending_serial != serial || s->pending != 2) {
        if (s->pending_serial == serial) s->pending = 0;
        ReleaseSRWLockExclusive(&s->lock); return 0;
    }
    *generation = s->reply_generation;
    result = s->reply_bound ? 1U : 0U; s->pending = 0;
    ReleaseSRWLockExclusive(&s->lock);
    return result != 0;
}

static DWORD WINAPI wiki_worker(void *context)
{
    WikiSession *s = (WikiSession *)context; ULONG peer = 0; WikiRequest request;
    DWORD error; uint64_t generation = 0; char payload[256]; int bound = 0; const char *stage = "launch_failed";
    if (WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT || !wiki_launch(s, s->theme_config)) goto done;
    stage = "pipe_connection_failed";
    while (WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT && WaitForSingleObject(s->child, 0) == WAIT_TIMEOUT) {
        if (ConnectNamedPipe(s->pipe, NULL)) break;
        error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) break;
        if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA) goto done;
        if (error == ERROR_NO_DATA) (void)DisconnectNamedPipe(s->pipe);
        (void)WaitForSingleObject(s->stop, WIKI_POLL_MS);
    }
    stage = "authentication_failed";
    if (WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT ||
        !GetNamedPipeClientProcessId(s->pipe, &peer) || peer != s->child_pid ||
        WaitForSingleObject(s->child, 0) != WAIT_TIMEOUT || !wiki_read_request(s, &request)) goto done;
    if (!wiki_equal(request.type, "hello") || request.version != 2 || request.client != s->child_pid ||
        request.editor != GetCurrentProcessId() || !wiki_equal_ascii_fold(request.hash, s->child_hash)) {
        (void)wiki_send(s, "error", request.request,
            "{\"code\":\"hello_identity_mismatch\",\"message\":\"Editor session authentication failed.\",\"retryable\":false}");
        goto done;
    }
    if (!wiki_hello_payload(payload, sizeof(payload), GetCurrentProcessId(), s->editor_hash) ||
        !wiki_send(s, "result", request.request, payload)) goto done;
    stage = "panel_binding_failed";
    if (!wiki_read_request(s, &request) || !wiki_equal(request.type, "panelReady")) goto done;
    bound = wiki_bind_request(s, &request, &generation);
    if (!wiki_panel_payload(payload, sizeof(payload), bound, generation) ||
        !wiki_send(s, "result", request.request, payload) || !bound) goto done;
    while (WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT && WaitForSingleObject(s->child, 0) == WAIT_TIMEOUT) {
        DWORD available = 0;
        if (!PeekNamedPipe(s->pipe, NULL, 0, NULL, &available, NULL) || available) break;
        (void)WaitForSingleObject(s->stop, 50);
    }
done:
    if (s->pipe != INVALID_HANDLE_VALUE) {
        (void)DisconnectNamedPipe(s->pipe);
        CloseHandle(s->pipe); s->pipe = INVALID_HANDLE_VALUE;
    }
    if (!bound && WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT) {
        dluma_log_pair("WARN", "editor_wiki_session", stage);
        if (!PostMessageW(s->owner, g_dispatch_message, 0, (LPARAM)s->serial))
            dluma_log_pair("WARN", "editor_wiki_session", "failure_notification_unavailable");
    }
    /* Keep the runtime lease until the exact launched process has exited. */
    if (s->child) (void)WaitForSingleObject(s->child, INFINITE);
    wiki_release(s);
    return 0;
}

int dluma_editor_wiki_session_open(HWND owner, const DlumaEditorPanelRef *panel, const WCHAR *theme_config)
{
    WikiSession *s; DWORD pid = 0;
    if (g_session || !panel || panel->size != sizeof(*panel) || panel->version != 1 || !panel->token ||
        !IsWindow(owner) || GetWindowThreadProcessId(owner, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !IsWindow(panel->content_window) || !IsWindow(panel->panel_window) ||
        GetParent(panel->panel_window) != owner || GetParent(panel->content_window) != panel->panel_window ||
        GetWindowThreadProcessId(panel->content_window, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !(GetWindowLongPtrW(panel->content_window, GWL_STYLE) & WS_CHILD)) return 0;
    if (!g_dispatch_message) g_dispatch_message = RegisterWindowMessageW(L"DLUMA.EditorWiki.PanelReady.1");
    if (!g_dispatch_message) return 0;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return 0;
    s->references = 1; InterlockedIncrement(&g_sessions); InitializeSRWLock(&s->lock);
    s->owner = owner; s->owner_thread = GetCurrentThreadId(); s->panel = *panel;
    s->serial = (uint64_t)InterlockedIncrement64(&g_serial); s->pipe = INVALID_HANDLE_VALUE;
    s->stop = CreateEventW(NULL, TRUE, FALSE, NULL); s->reply = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (wiki_safe_arg(theme_config)) {
        size_t bytes = (wcslen(theme_config) + 1U) * sizeof(WCHAR);
        s->theme_config = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (s->theme_config) memcpy(s->theme_config, theme_config, bytes);
    }
    if (!s->serial || !s->stop || !s->reply || !s->theme_config || !wiki_random(s) ||
        (s->pipe = wiki_pipe(s)) == INVALID_HANDLE_VALUE) { wiki_release(s); return 0; }
    g_session = s; InterlockedIncrement(&s->references);
    s->worker = CreateThread(NULL, 0, wiki_worker, s, 0, NULL);
    if (!s->worker) { g_session = NULL; SetEvent(s->stop); wiki_release(s); wiki_release(s); return 0; }
    return 1;
}

int dluma_editor_wiki_session_dispatch(HWND owner, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    WikiSession *s = g_session; HWND child; uint64_t serial, generation = 0; int bound;
    if (!s || message != g_dispatch_message || owner != s->owner || GetCurrentThreadId() != s->owner_thread ||
        (uint64_t)lparam != s->serial) return 0;
    if (result) *result = 0;
    if (!wparam) { if (result) *result = -1; return 1; }
    InterlockedIncrement(&s->references); serial = (uint64_t)wparam;
    AcquireSRWLockExclusive(&s->lock);
    if (s->pending != 1 || s->pending_serial != serial || WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT) {
        ReleaseSRWLockExclusive(&s->lock); wiki_release(s); return 1;
    }
    child = s->pending_child; s->pending = 3;
    ReleaseSRWLockExclusive(&s->lock);
    bound = dluma_editor_panel_host_bind(&s->panel, child, s->child_pid, s->child);
    if (bound) generation = dluma_editor_panel_host_binding_generation(&s->panel);
    if (bound && !generation) { (void)dluma_editor_panel_host_unbind(&s->panel); bound = 0; }
    AcquireSRWLockExclusive(&s->lock);
    if (s->pending == 3 && s->pending_serial == serial && WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT) {
        s->reply_bound = bound && generation != 0; s->reply_generation = generation; s->pending = 2; SetEvent(s->reply);
    } else if (bound) {
        ReleaseSRWLockExclusive(&s->lock); (void)dluma_editor_panel_host_unbind(&s->panel); wiki_release(s); return 1;
    }
    ReleaseSRWLockExclusive(&s->lock); wiki_release(s); return 1;
}

void dluma_editor_wiki_session_close(void)
{
    WikiSession *s = g_session;
    if (!s || GetCurrentThreadId() != s->owner_thread) return;
    g_session = NULL; SetEvent(s->stop);
    (void)dluma_editor_panel_host_unbind(&s->panel);
    wiki_release(s);
}

int dluma_editor_wiki_session_callbacks_drained(void)
{
    return InterlockedCompareExchange(&g_sessions, 0, 0) == 0;
}
