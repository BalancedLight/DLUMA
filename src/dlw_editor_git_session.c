#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "dlw_editor_git_session.h"
#include "dlw_editor_companion_runtime.h"
#include "core/dlw_json_reader.h"

extern int dluma_sha256_file_hex(const WCHAR *, ULONGLONG, char *);
extern int dluma_project_regular_file_is_contained(const WCHAR *, const WCHAR *);
extern void dluma_log_pair(const char *, const char *, const char *);

#define GIT_PATH_CAP 32768U
#define GIT_FRAME_CAP 65536U
#define GIT_POLL_MS 10U
#define GIT_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct GitSession {
    volatile LONG references, detached;
    SRWLOCK lock;
    HWND owner;
    DWORD owner_thread, child_pid;
    DlumaEditorPanelRef panel;
    uint64_t serial, request_serial;
    HANDLE pipe, child, job, worker, stop, reply;
    DlumaEditorGitRequestHandler handler; void *context;
    WCHAR *project_root; uint64_t project_generation;
    char *pending_type, *pending_payload, *pending_output; size_t pending_size, pending_used;
    int pending_result; char *notification_type, *notification_payload;
    void *runtime_lease;
    WCHAR *theme_config;
    HWND pending_child;
    uint64_t pending_serial, reply_generation;
    int pending, reply_bound;
    char token[65], child_hash[65], editor_hash[65];
    WCHAR pipe_short[128], pipe_full[160];
} GitSession;

static GitSession *g_session;
static volatile LONG g_sessions;
static volatile LONG64 g_serial;
static UINT g_dispatch_message;

static int git_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static unsigned char git_ascii_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int git_equal_ascii_fold(const char *a, const char *b)
{
    while (*a && git_ascii_fold((unsigned char)*a) == git_ascii_fold((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}

static int git_append(char *out, size_t capacity, const char *text)
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

static int git_append_u64(char *out, size_t capacity, uint64_t value)
{
    char digits[21]; size_t count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (count) {
        char next[2]; next[0] = digits[--count]; next[1] = 0;
        if (!git_append(out, capacity, next)) return 0;
    }
    return 1;
}

static int git_wappend(WCHAR *out, size_t capacity, const WCHAR *text)
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

static int git_wappend_ascii(WCHAR *out, size_t capacity, const char *text)
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

static int git_wappend_u64(WCHAR *out, size_t capacity, uint64_t value)
{
    char digits[21] = {0};
    return git_append_u64(digits, sizeof(digits), value) && git_wappend_ascii(out, capacity, digits);
}

static void git_release(GitSession *s)
{
    if (InterlockedDecrement(&s->references)) return;
    if (s->pipe && s->pipe != INVALID_HANDLE_VALUE) CloseHandle(s->pipe);
    if (s->child) CloseHandle(s->child);
    if (s->job) CloseHandle(s->job);
    if (s->project_root) HeapFree(GetProcessHeap(), 0, s->project_root);
    if (s->pending_type) HeapFree(GetProcessHeap(), 0, s->pending_type);
    if (s->pending_payload) HeapFree(GetProcessHeap(), 0, s->pending_payload);
    if (s->pending_output) HeapFree(GetProcessHeap(), 0, s->pending_output);
    if (s->notification_type) HeapFree(GetProcessHeap(), 0, s->notification_type);
    if (s->notification_payload) HeapFree(GetProcessHeap(), 0, s->notification_payload);
    if (s->worker) CloseHandle(s->worker);
    if (s->stop) CloseHandle(s->stop);
    if (s->reply) CloseHandle(s->reply);
    if (s->theme_config) HeapFree(GetProcessHeap(), 0, s->theme_config);
    if (s->runtime_lease) dluma_editor_companion_runtime_release(s->runtime_lease);
    SecureZeroMemory(s->token, sizeof(s->token));
    HeapFree(GetProcessHeap(), 0, s);
    InterlockedDecrement(&g_sessions);
}

static int git_random(GitSession *s)
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

static HANDLE git_pipe(GitSession *s)
{
    HANDLE process_token = NULL, pipe = INVALID_HANDLE_VALUE;
    BYTE user_buffer[1024], acl_buffer[1024];
    DWORD needed = 0, acl_size; TOKEN_USER *user; PACL acl = (PACL)acl_buffer;
    SECURITY_DESCRIPTOR descriptor; SECURITY_ATTRIBUTES attributes;
    WCHAR prefix[17]; unsigned int i;
    for (i = 0; i < 16U; ++i) prefix[i] = (WCHAR)s->token[i];
    prefix[16] = 0;
    if (!git_wappend(s->pipe_short, GIT_COUNT(s->pipe_short), L"DLUMA.EditorGit.") ||
        !git_wappend_u64(s->pipe_short, GIT_COUNT(s->pipe_short), GetCurrentProcessId()) ||
        !git_wappend(s->pipe_short, GIT_COUNT(s->pipe_short), L".") ||
        !git_wappend(s->pipe_short, GIT_COUNT(s->pipe_short), prefix) ||
        !git_wappend(s->pipe_full, GIT_COUNT(s->pipe_full), L"\\\\.\\pipe\\") ||
        !git_wappend(s->pipe_full, GIT_COUNT(s->pipe_full), s->pipe_short))
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

static int git_env_key(const WCHAR *entry, const WCHAR *key)
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

static WCHAR *git_environment(const WCHAR *root)
{
    LPWCH inherited = GetEnvironmentStringsW(); const WCHAR *cursor; const WCHAR **entries = NULL;
    WCHAR a[GIT_PATH_CAP], b[GIT_PATH_CAP]; WCHAR c[] = L"DOTNET_MULTILEVEL_LOOKUP=0";
    WCHAR *result = NULL, *out; size_t count = 0, used = 0, i, total = 1;
    if (!inherited) return NULL;
    a[0] = b[0] = 0;
    if (!git_wappend(a, GIT_COUNT(a), L"DOTNET_ROOT=") || !git_wappend(a, GIT_COUNT(a), root) ||
        !git_wappend(b, GIT_COUNT(b), L"DOTNET_ROOT_X64=") || !git_wappend(b, GIT_COUNT(b), root)) goto done;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1) ++count;
    entries = HeapAlloc(GetProcessHeap(), 0, (count + 3U) * sizeof(*entries));
    if (!entries) goto done;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1)
        if (!git_env_key(cursor, L"DOTNET_ROOT") && !git_env_key(cursor, L"DOTNET_ROOT_X64") &&
            !git_env_key(cursor, L"DOTNET_MULTILEVEL_LOOKUP")) entries[used++] = cursor;
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

static int git_safe_arg(const WCHAR *value)
{
    const WCHAR *p;
    if (!value || !*value) return 0;
    for (p = value; *p && (size_t)(p - value) < GIT_PATH_CAP; ++p)
        if (*p == L'"' || *p < 32) return 0;
    if ((size_t)(p - value) == GIT_PATH_CAP) return 0;
    return 1;
}

static int git_quote_arg(WCHAR *out, size_t capacity, const WCHAR *value)
{
    size_t n = wcslen(value), trailing = 0;
    while (trailing < n && value[n - trailing - 1U] == L'\\') ++trailing;
    if (!git_wappend(out, capacity, L"\"") || !git_wappend(out, capacity, value)) return 0;
    while (trailing--) if (!git_wappend(out, capacity, L"\\")) return 0;
    return git_wappend(out, capacity, L"\"");
}

static int git_command(WCHAR *command, size_t capacity, const WCHAR *executable,
                        const WCHAR *pipe, const char *token, DWORD editor_pid,
                        const char *editor_hash, uint64_t start, uint64_t parent,
                        uint64_t panel_token, const WCHAR *theme, const WCHAR *project, uint64_t generation)
{
    if (!capacity) return 0;
    command[0] = 0;
    return git_quote_arg(command, capacity, executable) &&
        git_wappend(command, capacity, L" --pipe \"") && git_wappend(command, capacity, pipe) &&
        git_wappend(command, capacity, L"\" --token \"") && git_wappend_ascii(command, capacity, token) &&
        git_wappend(command, capacity, L"\" --editor-pid ") && git_wappend_u64(command, capacity, editor_pid) &&
        git_wappend(command, capacity, L" --editor-sha256 ") && git_wappend_ascii(command, capacity, editor_hash) &&
        git_wappend(command, capacity, L" --editor-start-time ") && git_wappend_u64(command, capacity, start) &&
        git_wappend(command, capacity, L" --panel-parent ") && git_wappend_u64(command, capacity, parent) &&
        git_wappend(command, capacity, L" --panel-token ") && git_wappend_u64(command, capacity, panel_token) &&
        git_wappend(command, capacity, L" --theme-config ") && git_quote_arg(command, capacity, theme) &&
        git_wappend(command, capacity, L" --project-root ") && git_quote_arg(command, capacity, project) &&
        git_wappend(command, capacity, L" --project-generation ") && git_wappend_u64(command, capacity, generation);
}

static int git_launch(GitSession *s, const WCHAR *theme)
{
    HMODULE module = NULL; WCHAR *module_path = NULL, *runtime = NULL, *dotnet = NULL, *directory = NULL;
    WCHAR *executable = NULL, *command = NULL, *environment = NULL, *slash;
    WCHAR editor_path[GIT_PATH_CAP]; FILETIME created, exited, kernel, user; ULARGE_INTEGER start;
    STARTUPINFOW startup; PROCESS_INFORMATION process; DWORD n; int ok = 0;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    if (!git_safe_arg(theme) || !git_safe_arg(s->project_root) || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(ULONG_PTR)git_launch, &module)) return 0;
    module_path = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    runtime = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    dotnet = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    directory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    executable = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    command = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_PATH_CAP * sizeof(WCHAR));
    if (!module_path || !runtime || !dotnet || !directory || !executable || !command) goto done;
    n = GetModuleFileNameW(module, module_path, GIT_PATH_CAP);
    if (!n || n >= GIT_PATH_CAP || !(slash = wcsrchr(module_path, L'\\'))) goto done;
    *slash = 0;
    if (!git_wappend(runtime, GIT_PATH_CAP, module_path) || !git_wappend(runtime, GIT_PATH_CAP, L"\\dluma_runtime") ||
        !git_wappend(dotnet, GIT_PATH_CAP, runtime) || !git_wappend(dotnet, GIT_PATH_CAP, L"\\dotnet") ||
        !git_wappend(directory, GIT_PATH_CAP, runtime) || !git_wappend(directory, GIT_PATH_CAP, L"\\editor-git") ||
        !git_wappend(executable, GIT_PATH_CAP, directory) || !git_wappend(executable, GIT_PATH_CAP, L"\\Dluma.EditorGit.exe") ||
        !(s->runtime_lease = dluma_editor_companion_runtime_acquire(runtime, "10.0.13")) ||
        !git_safe_arg(executable) || !dluma_project_regular_file_is_contained(module_path, executable) ||
        !dluma_sha256_file_hex(executable, 64ULL * 1024ULL * 1024ULL, s->child_hash)) goto done;
    n = GetModuleFileNameW(NULL, editor_path, GIT_COUNT(editor_path));
    if (!n || n >= GIT_COUNT(editor_path) || !dluma_sha256_file_hex(editor_path, 128ULL * 1024ULL * 1024ULL, s->editor_hash) ||
        !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) goto done;
    start.LowPart = created.dwLowDateTime; start.HighPart = created.dwHighDateTime;
    if (!git_command(command, GIT_PATH_CAP, executable, s->pipe_short, s->token,
            GetCurrentProcessId(), s->editor_hash, start.QuadPart,
            (uint64_t)(ULONG_PTR)s->panel.content_window, s->panel.token, theme, s->project_root, s->project_generation)) goto done;
    environment = git_environment(dotnet);
    if (!environment || WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT) goto done;
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup); ZeroMemory(&process, sizeof(process));
    s->job = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&limits, sizeof(limits)); limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!s->job || !SetInformationJobObject(s->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto done;
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        environment, directory, &startup, &process)) goto done;
    if (!AssignProcessToJobObject(s->job, process.hProcess)) {
        TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); goto done;
    }
    s->child = process.hProcess; s->child_pid = process.dwProcessId;
    if (ResumeThread(process.hThread) == (DWORD)-1) { CloseHandle(process.hThread); goto done; }
    CloseHandle(process.hThread); ok = 1;
done:
    if (environment) HeapFree(GetProcessHeap(), 0, environment);
    if (command) { SecureZeroMemory(command, GIT_PATH_CAP * sizeof(WCHAR)); HeapFree(GetProcessHeap(), 0, command); }
    if (executable) HeapFree(GetProcessHeap(), 0, executable);
    if (directory) HeapFree(GetProcessHeap(), 0, directory);
    if (dotnet) HeapFree(GetProcessHeap(), 0, dotnet);
    if (runtime) HeapFree(GetProcessHeap(), 0, runtime);
    if (module_path) HeapFree(GetProcessHeap(), 0, module_path);
    return ok;
}

typedef struct GitRequest {
    char type[24], request[33], token[65], hash[65];
    uint64_t version, client, editor, parent, child, panel_token;
    char *payload; size_t payload_size;
} GitRequest;

static int git_parse_payload(DlumaJsonReader *reader, GitRequest *request)
{
    unsigned int fields = 0; int first = 1, next;
    if (!dluma_json_object_begin(reader)) return 0;
    while ((next = dluma_json_object_next(reader, &first)) > 0) {
        char key[32]; unsigned int bit = 0; uint64_t *number = NULL;
        if (!dluma_json_string(reader, key, sizeof(key)) || !dluma_json_take(reader, ':')) return 0;
        if (git_equal(request->type, "hello")) {
            if (git_equal(key, "protocolVersion")) { bit = 1; number = &request->version; }
            else if (git_equal(key, "clientProcessId")) { bit = 2; number = &request->client; }
            else if (git_equal(key, "editorProcessId")) { bit = 4; number = &request->editor; }
            else if (git_equal(key, "executableSha256")) bit = 8;
        } else if (git_equal(request->type, "panelReady")) {
            if (git_equal(key, "parentWindow")) { bit = 1; number = &request->parent; }
            else if (git_equal(key, "childWindow")) { bit = 2; number = &request->child; }
            else if (git_equal(key, "panelToken")) { bit = 4; number = &request->panel_token; }
        }
        if (!bit || (fields & bit)) return 0;
        fields |= bit;
        if (number) { if (!dluma_json_u64(reader, number)) return 0; }
        else if (!dluma_json_string(reader, request->hash, sizeof(request->hash))) return 0;
    }
    return next == 0 && dluma_json_object_end(reader) &&
        fields == (git_equal(request->type, "hello") ? 15U : 7U);
}

static int git_equal_token(const char *a, const char *b)
{
    unsigned int i, mismatch = 0;
    if (strlen(a) != 64 || strlen(b) != 64) return 0;
    for (i = 0; i < 64; ++i) mismatch |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return mismatch == 0;
}

static int git_parse(char *json, DWORD size, GitSession *session, GitRequest *request)
{
    DlumaJsonReader reader, payload_reader; unsigned int fields = 0, i; int first = 1, next;
    const unsigned char *payload = NULL, *end = NULL;
    ZeroMemory(request, sizeof(*request)); dluma_json_reader_initialize(&reader, json, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    while ((next = dluma_json_object_next(&reader, &first)) > 0) {
        char key[32]; unsigned int bit;
        if (!dluma_json_string(&reader, key, sizeof(key)) || !dluma_json_take(&reader, ':')) return 0;
        if (git_equal(key, "schemaVersion")) {
            uint64_t version = 0; bit = 1;
            if ((fields & bit) || !dluma_json_u64(&reader, &version) || version != 2) return 0;
        } else if (git_equal(key, "type")) {
            bit = 2; if ((fields & bit) || !dluma_json_string(&reader, request->type, sizeof(request->type))) return 0;
        } else if (git_equal(key, "requestId")) {
            bit = 4; if ((fields & bit) || !dluma_json_string(&reader, request->request, sizeof(request->request))) return 0;
        } else if (git_equal(key, "sessionToken")) {
            bit = 8; if ((fields & bit) || !dluma_json_string(&reader, request->token, sizeof(request->token))) return 0;
        } else if (git_equal(key, "payload")) {
            bit = 16; dluma_json_space(&reader); payload = reader.cursor;
            if ((fields & bit) || !dluma_json_skip_value(&reader)) return 0;
            end = reader.cursor;
        } else return 0;
        fields |= bit;
    }
    if (next != 0 || !dluma_json_object_end(&reader) || !dluma_json_finished(&reader) || fields != 31U ||
        !payload || !end || end <= payload || *payload != '{' || !git_equal_token(request->token, session->token) ||
        strlen(request->request) != 32 || !request->type[0]) return 0;
    for (i = 0; i < 32; ++i)
        if (!((request->request[i] >= '0' && request->request[i] <= '9') ||
              (request->request[i] >= 'a' && request->request[i] <= 'f'))) return 0;
    request->payload = (char *)payload; request->payload_size = (size_t)(end - payload);
    if (!git_equal(request->type, "hello") && !git_equal(request->type, "panelReady")) return 1;
    dluma_json_reader_initialize(&payload_reader, payload, (unsigned int)(end - payload));
    return git_parse_payload(&payload_reader, request) && dluma_json_finished(&payload_reader);
}

static int git_io(GitSession *s, void *buffer, DWORD length, int writing, DWORD timeout)
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
        (void)WaitForSingleObject(s->stop, GIT_POLL_MS);
    }
    return offset == length;
}

static int git_response(char *frame, size_t capacity, const char *type,
                         const char *request, const char *token, const char *payload)
{
    if (!capacity) return 0;
    frame[0] = 0;
    return git_append(frame, capacity, "{\"schemaVersion\":2,\"type\":\"") &&
        git_append(frame, capacity, type) && git_append(frame, capacity, "\",\"requestId\":\"") &&
        git_append(frame, capacity, request) && git_append(frame, capacity, "\",\"sessionToken\":\"") &&
        git_append(frame, capacity, token) && git_append(frame, capacity, "\",\"payload\":") &&
        git_append(frame, capacity, payload) && git_append(frame, capacity, "}");
}

static int git_send(GitSession *s, const char *type, const char *request, const char *payload)
{
    char *frame; int ok; DWORD length; BYTE prefix[4];
    frame = HeapAlloc(GetProcessHeap(), 0, GIT_FRAME_CAP + 1U);
    if (!frame) return 0;
    if (!git_response(frame, GIT_FRAME_CAP + 1U, type, request, s->token, payload)) { HeapFree(GetProcessHeap(), 0, frame); return 0; }
    length = (DWORD)strlen(frame); prefix[0] = (BYTE)length; prefix[1] = (BYTE)(length >> 8);
    prefix[2] = (BYTE)(length >> 16); prefix[3] = (BYTE)(length >> 24);
    ok = git_io(s, prefix, sizeof(prefix), 1, 5000) && git_io(s, frame, length, 1, 5000);
    HeapFree(GetProcessHeap(), 0, frame); return ok;
}

static int git_hello_payload(char *payload, size_t capacity, DWORD editor_pid, const char *hash)
{
    if (!capacity) return 0;
    payload[0] = 0;
    return git_append(payload, capacity, "{\"protocolVersion\":2,\"editorProcessId\":") &&
        git_append_u64(payload, capacity, editor_pid) &&
        git_append(payload, capacity, ",\"hostBuildId\":\"") && git_append(payload, capacity, hash) &&
        git_append(payload, capacity, "\",\"projectRoot\":null,\"installedCatalogRoots\":[],\"contextGeneration\":0}");
}

static int git_panel_payload(char *payload, size_t capacity, int bound, uint64_t generation)
{
    if (!capacity) return 0;
    payload[0] = 0;
    return git_append(payload, capacity, bound ? "{\"bound\":true,\"bindingGeneration\":" : "{\"bound\":false,\"bindingGeneration\":") &&
        git_append_u64(payload, capacity, generation) &&
        git_append(payload, capacity, bound ? ",\"message\":\"Git panel connected.\"}" :
                                              ",\"message\":\"Git panel could not be opened.\"}");
}

static int git_read_request(GitSession *s, GitRequest *request, char *frame)
{
    BYTE prefix[4]; DWORD length;
    if (!git_io(s, prefix, sizeof(prefix), 0, 15000)) return 0;
    length = (DWORD)prefix[0] | ((DWORD)prefix[1] << 8) | ((DWORD)prefix[2] << 16) | ((DWORD)prefix[3] << 24);
    return length && length <= GIT_FRAME_CAP && git_io(s, frame, length, 0, 15000) &&
        git_parse(frame, length, s, request);
}


static char *git_copy(const char *value, size_t length)
{
    char *copy = HeapAlloc(GetProcessHeap(), 0, length + 1U);
    if (copy) { memcpy(copy, value, length); copy[length] = 0; }
    return copy;
}

static int git_value(const char *value, size_t length)
{
    DlumaJsonReader reader;
    if (!value || !length || length > GIT_FRAME_CAP) return 0;
    while (length && (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')) { ++value; --length; }
    if (!length || *value != '{') return 0;
    dluma_json_reader_initialize(&reader, value, (unsigned int)length);
    return dluma_json_skip_value(&reader) && dluma_json_finished(&reader);
}

static int git_owner_request(GitSession *s, const GitRequest *request)
{
    int completed;
    char *type = git_copy(request->type, strlen(request->type));
    char *payload = git_copy(request->payload, request->payload_size);
    char *output = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GIT_FRAME_CAP + 1U);
    if (!type || !payload || !output) {
        if (type) HeapFree(GetProcessHeap(), 0, type);
        if (payload) HeapFree(GetProcessHeap(), 0, payload);
        if (output) HeapFree(GetProcessHeap(), 0, output);
        return 0;
    }
    AcquireSRWLockExclusive(&s->lock);
    s->pending_type = type; s->pending_payload = payload; s->pending_output = output;
    s->pending_size = request->payload_size; s->pending_used = 0; s->pending = 1;
    s->pending_child = (HWND)(ULONG_PTR)request->child;
    s->pending_serial = ++s->request_serial; ResetEvent(s->reply);
    ReleaseSRWLockExclusive(&s->lock);
    if (!PostMessageW(s->owner, g_dispatch_message, (WPARAM)s->pending_serial, (LPARAM)s->serial)) {
        AcquireSRWLockExclusive(&s->lock); s->pending = 0; ReleaseSRWLockExclusive(&s->lock);
    } else {
        HANDLE waits[2] = { s->reply, s->stop };
        for (;;) {
            DWORD available = 0, wait = WaitForMultipleObjects(2, waits, FALSE, 50);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_FAILED ||
                WaitForSingleObject(s->child, 0) != WAIT_TIMEOUT ||
                !PeekNamedPipe(s->pipe, NULL, 0, NULL, &available, NULL)) {
                SetEvent(s->stop);
                AcquireSRWLockExclusive(&s->lock);
                if (s->pending == 1) { s->pending = 0; SetEvent(s->reply); }
                ReleaseSRWLockExclusive(&s->lock);
                (void)WaitForSingleObject(s->reply, INFINITE);
                break;
            }
        }
    }
    AcquireSRWLockExclusive(&s->lock);
    completed = s->pending == 2;
    s->pending = 0;
    ReleaseSRWLockExclusive(&s->lock);
    return completed;
}

static void git_clear_request(GitSession *s)
{
    HeapFree(GetProcessHeap(), 0, s->pending_type); s->pending_type = NULL;
    HeapFree(GetProcessHeap(), 0, s->pending_payload); s->pending_payload = NULL;
    HeapFree(GetProcessHeap(), 0, s->pending_output); s->pending_output = NULL;
}

static int git_active(GitSession *s, DWORD *count)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info;
    if (!s->job) { *count = 0; return 1; }
    ZeroMemory(&info, sizeof(info));
    if (!QueryInformationJobObject(s->job, JobObjectBasicAccountingInformation, &info, sizeof(info), NULL)) return 0;
    *count = info.ActiveProcesses; return 1;
}

static DWORD WINAPI git_worker(void *context)
{
    GitSession *s = (GitSession *)context; ULONG peer = 0; GitRequest request;
    char *frame = HeapAlloc(GetProcessHeap(), 0, GIT_FRAME_CAP); char payload[256];
    DWORD error, available, active; int authenticated = 0;
    if (!frame || !git_launch(s, s->theme_config)) goto done;
    while (WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT && WaitForSingleObject(s->child, 0) == WAIT_TIMEOUT) {
        if (ConnectNamedPipe(s->pipe, NULL)) break;
        error = GetLastError(); if (error == ERROR_PIPE_CONNECTED) break;
        if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA) goto done;
        if (error == ERROR_NO_DATA) (void)DisconnectNamedPipe(s->pipe);
        (void)WaitForSingleObject(s->stop, GIT_POLL_MS);
    }
    if (WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT || !GetNamedPipeClientProcessId(s->pipe, &peer) ||
        peer != s->child_pid || !git_read_request(s, &request, frame) || !git_equal(request.type, "hello") ||
        request.version != 2 || request.client != s->child_pid || request.editor != GetCurrentProcessId() ||
        !git_equal_ascii_fold(request.hash, s->child_hash) || WaitForSingleObject(s->child, 0) != WAIT_TIMEOUT ||
        !git_hello_payload(payload, sizeof(payload), GetCurrentProcessId(), s->editor_hash) ||
        !git_send(s, "result", request.request, payload)) goto done;
    authenticated = 1;
    while (WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT && WaitForSingleObject(s->child, 0) == WAIT_TIMEOUT) {
        char *nt = NULL, *np = NULL;
        AcquireSRWLockExclusive(&s->lock);
        nt = s->notification_type; np = s->notification_payload;
        s->notification_type = NULL; s->notification_payload = NULL;
        ReleaseSRWLockExclusive(&s->lock);
        if (nt) {
            char id[33]; memcpy(id, s->token, 32); id[32] = 0;
            error = git_send(s, nt, id, np) ? 0U : 1U;
            HeapFree(GetProcessHeap(), 0, nt); HeapFree(GetProcessHeap(), 0, np);
            if (error) break;
        }
        available = 0;
        if (!PeekNamedPipe(s->pipe, NULL, 0, NULL, &available, NULL)) break;
        if (!available) { (void)WaitForSingleObject(s->stop, 20); continue; }
        if (!git_read_request(s, &request, frame) ||
            (!git_equal(request.type, "panelReady") && !git_equal(request.type, "gitState") &&
             !git_equal(request.type, "gitProjectAcquire") && !git_equal(request.type, "gitProjectCancel") &&
             !git_equal(request.type, "gitWorktreeAcquire") &&
             !git_equal(request.type, "gitChildStarted") && !git_equal(request.type, "gitLeaseRelease"))) break;
        if (git_equal(request.type, "panelReady") &&
            (request.parent != (uint64_t)(ULONG_PTR)s->panel.content_window || request.panel_token != s->panel.token ||
             !request.child || request.child > INT64_MAX)) break;
        if (!git_owner_request(s, &request)) { git_clear_request(s); break; }
        if (s->pending_used <= GIT_FRAME_CAP) s->pending_output[s->pending_used] = 0;
        if (!git_value(s->pending_output, s->pending_used) ||
            !git_send(s, s->pending_result ? "result" : "error", request.request, s->pending_output)) {
            git_clear_request(s); break;
        }
        git_clear_request(s);
    }
done:
    if (frame) HeapFree(GetProcessHeap(), 0, frame);
    if (s->pipe != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(s->pipe); CloseHandle(s->pipe); s->pipe = INVALID_HANDLE_VALUE; }
    SetEvent(s->stop);
    (void)PostMessageW(s->owner, g_dispatch_message, 0, (LPARAM)s->serial);
    if (authenticated || s->child) {
        if (s->child) (void)WaitForSingleObject(s->child, 1000);
        if (!git_active(s, &active) || active) {
            if (s->job) (void)TerminateJobObject(s->job, 1);
        }
        /* A failed job query or failed termination keeps all ownership alive. */
        for (;;) {
            if (git_active(s, &active) && !active) break;
            Sleep(250);
        }
    }
    SetEvent(s->stop);
    (void)PostMessageW(s->owner, g_dispatch_message, 0, (LPARAM)s->serial);
    git_release(s); return 0;
}

int dluma_editor_git_session_open(const DlumaEditorGitSessionSpecV1 *spec)
{
    GitSession *s; DWORD pid = 0; size_t n;
    if (g_session || !spec || spec->size != sizeof(*spec) || spec->version != 1 || !spec->handle_request ||
        !spec->project_generation || !git_safe_arg(spec->project_root) || !git_safe_arg(spec->theme_config) ||
        spec->panel.size != sizeof(spec->panel) || spec->panel.version != 1 || !spec->panel.token ||
        !IsWindow(spec->owner) || GetWindowThreadProcessId(spec->owner, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !IsWindow(spec->panel.content_window) || !IsWindow(spec->panel.panel_window) ||
        GetParent(spec->panel.panel_window) != spec->owner || GetParent(spec->panel.content_window) != spec->panel.panel_window ||
        GetWindowThreadProcessId(spec->panel.content_window, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !(GetWindowLongPtrW(spec->panel.content_window, GWL_STYLE) & WS_CHILD)) return 0;
    if (!g_dispatch_message) g_dispatch_message = RegisterWindowMessageW(L"DLUMA.EditorGit.Dispatch.1");
    if (!g_dispatch_message) return 0;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s)); if (!s) return 0;
    s->references = 1; InterlockedIncrement(&g_sessions); InitializeSRWLock(&s->lock);
    s->owner = spec->owner; s->owner_thread = GetCurrentThreadId(); s->panel = spec->panel;
    s->handler = spec->handle_request; s->context = spec->context; s->project_generation = spec->project_generation;
    s->serial = (uint64_t)InterlockedIncrement64(&g_serial); s->pipe = INVALID_HANDLE_VALUE;
    s->stop = CreateEventW(NULL, TRUE, FALSE, NULL); s->reply = CreateEventW(NULL, TRUE, FALSE, NULL);
    n = (wcslen(spec->theme_config) + 1U) * sizeof(WCHAR); s->theme_config = HeapAlloc(GetProcessHeap(), 0, n);
    if (s->theme_config) memcpy(s->theme_config, spec->theme_config, n);
    n = (wcslen(spec->project_root) + 1U) * sizeof(WCHAR); s->project_root = HeapAlloc(GetProcessHeap(), 0, n);
    if (s->project_root) memcpy(s->project_root, spec->project_root, n);
    if (!s->serial || !s->stop || !s->reply || !s->theme_config || !s->project_root || !git_random(s) ||
        (s->pipe = git_pipe(s)) == INVALID_HANDLE_VALUE) { git_release(s); return 0; }
    g_session = s; InterlockedIncrement(&s->references);
    s->worker = CreateThread(NULL, 0, git_worker, s, 0, NULL);
    if (!s->worker) { g_session = NULL; git_release(s); git_release(s); return 0; }
    return 1;
}

int dluma_editor_git_session_dispatch(HWND owner, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    GitSession *s = g_session; int status = 0; size_t used = 0;
    if (!s || message != g_dispatch_message || owner != s->owner || GetCurrentThreadId() != s->owner_thread ||
        (uint64_t)lparam != s->serial) return 0;
    if (result) *result = 0;
    if (!wparam) { if (result) *result = -1; return 1; }
    if (WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT) {
        AcquireSRWLockExclusive(&s->lock);
        if (s->pending == 1 && s->pending_serial == (uint64_t)wparam) {
            s->pending = 0; SetEvent(s->reply);
        }
        ReleaseSRWLockExclusive(&s->lock);
        return 1;
    }
    AcquireSRWLockExclusive(&s->lock);
    if (s->pending != 1 || s->pending_serial != (uint64_t)wparam) { ReleaseSRWLockExclusive(&s->lock); return 1; }
    s->pending = 3; ReleaseSRWLockExclusive(&s->lock);
    if (git_equal(s->pending_type, "panelReady")) {
        uint64_t generation = 0;
        int bound = dluma_editor_panel_host_bind(&s->panel, s->pending_child, s->child_pid, s->child);
        if (bound) generation = dluma_editor_panel_host_binding_generation(&s->panel);
        if (bound && !generation) { (void)dluma_editor_panel_host_unbind(&s->panel); bound = 0; }
        status = git_panel_payload(s->pending_output, GIT_FRAME_CAP, bound, generation);
        used = strlen(s->pending_output);
    } else {
        status = s->handler(s->pending_type, s->pending_payload, s->pending_size, s->pending_output,
                            GIT_FRAME_CAP, &used, s->context) == DLUMA_EDITOR_GIT_REQUEST_OK;
    }
    AcquireSRWLockExclusive(&s->lock);
    s->pending_used = used; s->pending_result = status; s->pending = 2; SetEvent(s->reply);
    ReleaseSRWLockExclusive(&s->lock); return 1;
}

int dluma_editor_git_session_notify(const char *type, const char *json_payload)
{
    GitSession *s = g_session; char *t, *p; size_t n;
    if (!s || GetCurrentThreadId() != s->owner_thread || !type ||
        (!git_equal(type, "gitContext") && !git_equal(type, "gitCommand")) || !json_payload ||
        WaitForSingleObject(s->stop, 0) != WAIT_TIMEOUT) return 0;
    n = strlen(json_payload); if (!git_value(json_payload, n)) return 0;
    t = git_copy(type, strlen(type)); p = git_copy(json_payload, n);
    if (!t || !p) { if (t) HeapFree(GetProcessHeap(), 0, t); if (p) HeapFree(GetProcessHeap(), 0, p); return 0; }
    AcquireSRWLockExclusive(&s->lock);
    if (s->notification_type) { ReleaseSRWLockExclusive(&s->lock); HeapFree(GetProcessHeap(), 0, t); HeapFree(GetProcessHeap(), 0, p); return 0; }
    s->notification_type = t; s->notification_payload = p; ReleaseSRWLockExclusive(&s->lock); return 1;
}

int dluma_editor_git_session_detach(void)
{
    GitSession *s = g_session;
    if (!s) return 1;
    if (GetCurrentThreadId() != s->owner_thread) return 0;
    if (InterlockedCompareExchange(&s->detached, 1, 0) == 0) {
        SetEvent(s->stop); (void)dluma_editor_panel_host_unbind(&s->panel);
        InterlockedExchange(&s->detached, 2);
    }
    return InterlockedCompareExchange(&s->detached, 0, 0) == 2;
}

int dluma_editor_git_session_abort(void)
{
    GitSession *s = g_session;
    if (!s) return 1;
    if (GetCurrentThreadId() != s->owner_thread) return 0;
    SetEvent(s->stop);
    if (!s->job) return 0;
    return TerminateJobObject(s->job, 1) != 0;
}

int dluma_editor_git_session_close(void)
{
    GitSession *s = g_session; DWORD count;
    if (!s) return 1;
    if (GetCurrentThreadId() == s->owner_thread) {
        if (!dluma_editor_git_session_detach()) return 0;
    } else if (InterlockedCompareExchange(&s->detached, 0, 0) != 2) return 0;
    if (WaitForSingleObject(s->worker, 0) != WAIT_OBJECT_0 || !git_active(s, &count) || count) return 0;
    g_session = NULL; git_release(s); return 1;
}

int dluma_editor_git_session_drained(void) { return InterlockedCompareExchange(&g_sessions, 0, 0) == 0; }
int dluma_editor_git_session_job_snapshot(DlumaEditorGitJobSnapshotV1 *out)
{
    GitSession *s = g_session; DWORD count;
    if (!s || !out || out->size != sizeof(*out) || out->version != 1 || GetCurrentThreadId() != s->owner_thread ||
        !s->job || !s->child || !s->child_pid || !git_active(s, &count)) return 0;
    out->job = s->job; out->companion_process = s->child; out->companion_pid = s->child_pid; out->active_processes = count; return 1;
}
