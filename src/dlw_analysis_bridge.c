#include "dlw_analysis_bridge.h"

#include "dlw_build_identity.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_player_startup.h"

/* The loader intentionally uses a small no-CRT Win32 surface instead of
 * windows.h. Keep the bridge's additional named-pipe/security declarations
 * local until another runtime consumer needs them. */
typedef HANDLE* PHANDLE;
typedef DWORD* PDWORD;
typedef void* PSID;
typedef int TOKEN_INFORMATION_CLASS;

typedef struct _ACL {
    BYTE AclRevision;
    BYTE Sbz1;
    WORD AclSize;
    WORD AceCount;
    WORD Sbz2;
} ACL, *PACL;

typedef struct _ACE_HEADER {
    BYTE AceType;
    BYTE AceFlags;
    WORD AceSize;
} ACE_HEADER;

typedef struct _ACCESS_ALLOWED_ACE {
    ACE_HEADER Header;
    DWORD Mask;
    DWORD SidStart;
} ACCESS_ALLOWED_ACE;

typedef struct _SID_AND_ATTRIBUTES {
    PSID Sid;
    DWORD Attributes;
} SID_AND_ATTRIBUTES;

typedef struct _TOKEN_USER {
    SID_AND_ATTRIBUTES User;
} TOKEN_USER;

typedef struct _SECURITY_DESCRIPTOR {
    BYTE Revision;
    BYTE Sbz1;
    WORD Control;
    PVOID Owner;
    PVOID Group;
    PACL Sacl;
    PACL Dacl;
} SECURITY_DESCRIPTOR, *PSECURITY_DESCRIPTOR;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _RTL_CRITICAL_SECTION {
    PVOID DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION;

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field) ((LONG)(LONG_PTR)&(((type*)0)->field))
#endif

#define TokenUser 1
#define TOKEN_QUERY 0x0008UL
#define ACL_REVISION 2UL
#define SECURITY_DESCRIPTOR_REVISION 1UL
#define GENERIC_ALL 0x10000000UL
#define PIPE_ACCESS_DUPLEX 0x00000003UL
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000UL
#define PIPE_TYPE_BYTE 0x00000000UL
#define PIPE_READMODE_BYTE 0x00000000UL
#define PIPE_NOWAIT 0x00000001UL
#define ERROR_NO_DATA 232UL
#define ERROR_PIPE_CONNECTED 535UL
#define ERROR_PIPE_LISTENING 536UL

__declspec(dllimport) void WINAPI InitializeCriticalSection(
    CRITICAL_SECTION* critical_section);
__declspec(dllimport) void WINAPI DeleteCriticalSection(
    CRITICAL_SECTION* critical_section);
__declspec(dllimport) void WINAPI EnterCriticalSection(
    CRITICAL_SECTION* critical_section);
__declspec(dllimport) void WINAPI LeaveCriticalSection(
    CRITICAL_SECTION* critical_section);
__declspec(dllimport) DWORD WINAPI WaitForMultipleObjects(
    DWORD count, const HANDLE* handles, BOOL wait_all, DWORD milliseconds);
__declspec(dllimport) HANDLE WINAPI CreateNamedPipeW(
    LPCWSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
    DWORD out_buffer_size, DWORD in_buffer_size, DWORD default_timeout,
    SECURITY_ATTRIBUTES* security_attributes);
__declspec(dllimport) BOOL WINAPI ConnectNamedPipe(
    HANDLE pipe, LPVOID overlapped);
__declspec(dllimport) BOOL WINAPI DisconnectNamedPipe(HANDLE pipe);

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008UL
#endif

#define DLUMA_ANALYSIS_BRIDGE_MAGIC 0x424C5744UL
#define DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY 8U
#define DLUMA_ANALYSIS_BRIDGE_REPLAY_CAPACITY 4096U
#define DLUMA_ANALYSIS_BRIDGE_IO_DEADLINE_MS 3000ULL
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_PROTOCOL 1U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_BUILD 2U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_LIFECYCLE 3U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_CAPABILITIES 4U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_CATALOG 5U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_PLAYER_STARTUP 6U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_PING 100U
#define DLUMA_ANALYSIS_BRIDGE_COMMAND_MARKER 101U

#pragma pack(push, 1)
typedef struct DlumaAnalysisBridgeHeader {
    DWORD magic;
    WORD major;
    WORD minor;
    ULONGLONG request_id;
    DWORD command_id;
    DWORD payload_size;
    BYTE authentication[32];
} DlumaAnalysisBridgeHeader;
#pragma pack(pop)

typedef struct DlumaAnalysisBridgeSlot {
    volatile LONG state;
    DlumaAnalysisBridgeHeader header;
    BYTE payload[DLUMA_ANALYSIS_BRIDGE_MAX_FRAME - sizeof(DlumaAnalysisBridgeHeader)];
    char response[2048];
    DWORD response_size;
    HANDLE completed_event;
} DlumaAnalysisBridgeSlot;

typedef struct DlumaAnalysisBridgeState {
    volatile LONG started;
    volatile LONG quiescing;
    volatile LONG stop_succeeded;
    volatile LONG dispatcher_available;
    HANDLE stop_event;
    HANDLE worker_thread;
    HANDLE pipe;
    CRITICAL_SECTION queue_lock;
    unsigned char queue_lock_initialized;
    BYTE key[32];
    DlumaAnalysisBridgeSlot queue[DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY];
    ULONGLONG replay[DLUMA_ANALYSIS_BRIDGE_REPLAY_CAPACITY];
    unsigned int replay_count;
    DWORD marker;
    DWORD game_thread_id;
    WCHAR pipe_name[128];
} DlumaAnalysisBridgeState;

typedef BOOL (WINAPI *DlumaOpenProcessTokenFn)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *DlumaGetTokenInformationFn)(
    HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
typedef BOOL (WINAPI *DlumaInitializeAclFn)(PACL, DWORD, DWORD);
typedef BOOL (WINAPI *DlumaAddAccessAllowedAceFn)(PACL, DWORD, DWORD, PSID);
typedef BOOL (WINAPI *DlumaInitializeSecurityDescriptorFn)(PSECURITY_DESCRIPTOR, DWORD);
typedef BOOL (WINAPI *DlumaSetSecurityDescriptorDaclFn)(
    PSECURITY_DESCRIPTOR, BOOL, PACL, BOOL);
typedef DWORD (WINAPI *DlumaGetLengthSidFn)(PSID);

static DlumaAnalysisBridgeState g_dluma_analysis_bridge;

static void dluma_analysis_bridge_zero(void* data, SIZE_T size)
{
    BYTE* cursor = (BYTE*)data;
    while (size--) *cursor++ = 0;
}

static int dluma_analysis_bridge_bytes_equal(
    const BYTE* left, const BYTE* right, SIZE_T size)
{
    BYTE difference = 0;
    while (size--) difference |= *left++ ^ *right++;
    return difference == 0;
}

static void dluma_analysis_bridge_hmac(
    const DlumaAnalysisBridgeHeader* header,
    const BYTE* payload,
    BYTE output[32])
{
    BYTE inner_pad[64];
    BYTE outer_pad[64];
    BYTE inner_hash[32];
    DlumaSha256Context context;
    unsigned int index;
    for (index = 0; index < 64U; ++index) {
        BYTE key = index < 32U ? g_dluma_analysis_bridge.key[index] : 0;
        inner_pad[index] = (BYTE)(key ^ 0x36U);
        outer_pad[index] = (BYTE)(key ^ 0x5CU);
    }
    dluma_sha256_init(&context);
    dluma_sha256_update(&context, inner_pad, sizeof(inner_pad));
    dluma_sha256_update(
        &context, header, FIELD_OFFSET(DlumaAnalysisBridgeHeader, authentication));
    if (header->payload_size)
        dluma_sha256_update(&context, payload, header->payload_size);
    dluma_sha256_final(&context, inner_hash);
    dluma_sha256_init(&context);
    dluma_sha256_update(&context, outer_pad, sizeof(outer_pad));
    dluma_sha256_update(&context, inner_hash, sizeof(inner_hash));
    dluma_sha256_final(&context, output);
    dluma_analysis_bridge_zero(inner_pad, sizeof(inner_pad));
    dluma_analysis_bridge_zero(outer_pad, sizeof(outer_pad));
    dluma_analysis_bridge_zero(inner_hash, sizeof(inner_hash));
}

static int dluma_analysis_bridge_replay_accept(ULONGLONG request_id)
{
    unsigned int index;
    if (!request_id) return 0;
    for (index = 0; index < g_dluma_analysis_bridge.replay_count; ++index)
        if (g_dluma_analysis_bridge.replay[index] == request_id) return 0;
    if (g_dluma_analysis_bridge.replay_count >=
            DLUMA_ANALYSIS_BRIDGE_REPLAY_CAPACITY)
        return 0;
    g_dluma_analysis_bridge.replay[
        g_dluma_analysis_bridge.replay_count++] = request_id;
    return 1;
}

static int dluma_analysis_bridge_append(
    char* output, DWORD capacity, DWORD* size, const char* text)
{
    DWORD cursor;
    if (!output || !capacity || !size || !text) return 0;
    cursor = *size;
    while (*text) {
        if (cursor + 1U >= capacity) return 0;
        output[cursor++] = *text++;
    }
    output[cursor] = 0;
    *size = cursor;
    return 1;
}

static int dluma_analysis_bridge_append_u64(
    char* output, DWORD capacity, DWORD* size, ULONGLONG value)
{
    char text[32];
    if (!dluma_u64_to_text(value, text, DLUMA_ARRAY_COUNT(text))) return 0;
    return dluma_analysis_bridge_append(output, capacity, size, text);
}

static int dluma_analysis_bridge_append_json_string(
    char* output, DWORD capacity, DWORD* size, const char* text)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int count = 0;
    if (!dluma_analysis_bridge_append(output, capacity, size, "\"")) return 0;
    if (!text) text = "";
    while (*text && count < DLUMA_PLAYER_STARTUP_STATUS_CAPACITY) {
        unsigned char value = (unsigned char)*text++;
        char escaped[7];
        ++count;
        if (value == '"' || value == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)value;
            escaped[2] = 0;
        } else if (value >= 0x20U && value <= 0x7EU) {
            escaped[0] = (char)value;
            escaped[1] = 0;
        } else {
            escaped[0] = '\\';
            escaped[1] = 'u';
            escaped[2] = '0';
            escaped[3] = '0';
            escaped[4] = hex[(value >> 4) & 0x0FU];
            escaped[5] = hex[value & 0x0FU];
            escaped[6] = 0;
        }
        if (!dluma_analysis_bridge_append(
                output, capacity, size, escaped))
            return 0;
    }
    return dluma_analysis_bridge_append(output, capacity, size, "\"");
}

static int dluma_analysis_bridge_append_bool(
    char* output, DWORD capacity, DWORD* size, unsigned int value)
{
    return dluma_analysis_bridge_append(
        output, capacity, size, value ? "true" : "false");
}

static const char* dluma_analysis_bridge_host_name(void)
{
    switch (dluma_dual_host_runtime_kind()) {
    case DLUMA_HOST_CHROME_EDITOR: return "editor";
    case DLUMA_HOST_PLAYER_STANDALONE: return "player";
    case DLUMA_HOST_PLAYER_EDITOR_PLAYTEST: return "player_playtest";
    default: return "unknown";
    }
}

static int dluma_analysis_bridge_valid_state_id(const char* state_id)
{
    unsigned int index;
    if (!state_id) return 0;
    for (index = 0; index < 16U; ++index) {
        char value = state_id[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f')))
            return 0;
    }
    return state_id[16] == 0;
}

static int dluma_analysis_bridge_load_key(void)
{
    WCHAR local_app_data[DLUMA_MAX_WPATH];
    WCHAR root[DLUMA_MAX_WPATH];
    WCHAR environment_root[DLUMA_MAX_WPATH];
    WCHAR protected_root[DLUMA_MAX_WPATH];
    WCHAR key_path[DLUMA_MAX_WPATH];
    WCHAR state_id[DLUMA_ANALYSIS_BRIDGE_STATE_ID_CAPACITY];
    HANDLE file;
    DWORD read = 0;
    BYTE extra = 0;
    DWORD local_app_data_size;
    SIZE_T local_app_data_length;
    if (!dluma_analysis_bridge_valid_state_id(
            g_dluma.config.analysis_bridge_state_id) ||
        !dluma_utf8_to_wide(
            g_dluma.config.analysis_bridge_state_id, state_id,
            DLUMA_ARRAY_COUNT(state_id)))
        return 0;
    local_app_data[0] = 0;
    local_app_data_size = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data,
        DLUMA_ARRAY_COUNT(local_app_data));
    if (!local_app_data_size ||
        local_app_data_size >= DLUMA_ARRAY_COUNT(local_app_data))
        return 0;
    local_app_data_length = wcslen(local_app_data);
    /* Four separators plus every fixed component and the NUL must fit. */
    if (local_app_data_length >
            DLUMA_ARRAY_COUNT(local_app_data) - 63U)
        return 0;
    dluma_path_join(root, DLUMA_ARRAY_COUNT(root),
                  local_app_data, L"DLUMAAgentAnalysis");
    dluma_path_join(environment_root, DLUMA_ARRAY_COUNT(environment_root),
                  root, state_id);
    dluma_path_join(protected_root, DLUMA_ARRAY_COUNT(protected_root),
                  environment_root, L"protected");
    dluma_path_join(key_path, DLUMA_ARRAY_COUNT(key_path),
                  protected_root, L"bridge-key-v1.bin");
    file = CreateFileW(
        key_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(file, g_dluma_analysis_bridge.key,
                  sizeof(g_dluma_analysis_bridge.key), &read, 0) ||
        read != sizeof(g_dluma_analysis_bridge.key) ||
        !ReadFile(file, &extra, 1U, &read, 0) || read != 0) {
        CloseHandle(file);
        dluma_analysis_bridge_zero(
            g_dluma_analysis_bridge.key,
            sizeof(g_dluma_analysis_bridge.key));
        return 0;
    }
    CloseHandle(file);
    return 1;
}

static HANDLE dluma_analysis_bridge_create_pipe(void)
{
    HMODULE advapi = 0;
    DlumaOpenProcessTokenFn open_process_token;
    DlumaGetTokenInformationFn get_token_information;
    DlumaInitializeAclFn initialize_acl;
    DlumaAddAccessAllowedAceFn add_access_allowed_ace;
    DlumaInitializeSecurityDescriptorFn initialize_security_descriptor;
    DlumaSetSecurityDescriptorDaclFn set_security_descriptor_dacl;
    DlumaGetLengthSidFn get_length_sid;
    HANDLE token = 0;
    BYTE token_buffer[1024];
    BYTE acl_buffer[1024];
    SECURITY_DESCRIPTOR descriptor;
    SECURITY_ATTRIBUTES attributes;
    TOKEN_USER* token_user;
    DWORD token_size = 0;
    DWORD acl_size;
    PACL acl = (PACL)acl_buffer;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    char pid_text[32];
    WCHAR pid_wide[32];

    if (!dluma_u64_to_text(
            GetCurrentProcessId(), pid_text, DLUMA_ARRAY_COUNT(pid_text)) ||
        !dluma_utf8_to_wide(pid_text, pid_wide, DLUMA_ARRAY_COUNT(pid_wide)))
        return INVALID_HANDLE_VALUE;
    dluma_wstr_copy(g_dluma_analysis_bridge.pipe_name,
                  DLUMA_ARRAY_COUNT(g_dluma_analysis_bridge.pipe_name),
                  L"\\\\.\\pipe\\DLUMA.Analysis.v1.");
    dluma_wstr_append(g_dluma_analysis_bridge.pipe_name,
                    DLUMA_ARRAY_COUNT(g_dluma_analysis_bridge.pipe_name), pid_wide);

    advapi = LoadLibraryW(L"advapi32.dll");
    if (!advapi) return INVALID_HANDLE_VALUE;
    open_process_token = (DlumaOpenProcessTokenFn)(void*)
        GetProcAddress(advapi, "OpenProcessToken");
    get_token_information = (DlumaGetTokenInformationFn)(void*)
        GetProcAddress(advapi, "GetTokenInformation");
    initialize_acl = (DlumaInitializeAclFn)(void*)
        GetProcAddress(advapi, "InitializeAcl");
    add_access_allowed_ace = (DlumaAddAccessAllowedAceFn)(void*)
        GetProcAddress(advapi, "AddAccessAllowedAce");
    initialize_security_descriptor = (DlumaInitializeSecurityDescriptorFn)(void*)
        GetProcAddress(advapi, "InitializeSecurityDescriptor");
    set_security_descriptor_dacl = (DlumaSetSecurityDescriptorDaclFn)(void*)
        GetProcAddress(advapi, "SetSecurityDescriptorDacl");
    get_length_sid = (DlumaGetLengthSidFn)(void*)
        GetProcAddress(advapi, "GetLengthSid");
    if (!open_process_token || !get_token_information || !initialize_acl ||
        !add_access_allowed_ace || !initialize_security_descriptor ||
        !set_security_descriptor_dacl || !get_length_sid)
        goto cleanup;
    if (!open_process_token(GetCurrentProcess(), TOKEN_QUERY, &token) ||
        !get_token_information(
            token, TokenUser, token_buffer,
            sizeof(token_buffer), &token_size))
        goto cleanup;
    token_user = (TOKEN_USER*)token_buffer;
    acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) +
               get_length_sid(token_user->User.Sid);
    if (acl_size > sizeof(acl_buffer) ||
        !initialize_acl(acl, acl_size, ACL_REVISION) ||
        !add_access_allowed_ace(
            acl, ACL_REVISION, GENERIC_ALL, token_user->User.Sid) ||
        !initialize_security_descriptor(
            &descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !set_security_descriptor_dacl(&descriptor, TRUE, acl, FALSE))
        goto cleanup;
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = &descriptor;
    attributes.bInheritHandle = FALSE;
    pipe = CreateNamedPipeW(
        g_dluma_analysis_bridge.pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1U, DLUMA_ANALYSIS_BRIDGE_MAX_FRAME,
        DLUMA_ANALYSIS_BRIDGE_MAX_FRAME, 0, &attributes);
cleanup:
    if (token) CloseHandle(token);
    FreeLibrary(advapi);
    return pipe;
}

static int dluma_analysis_bridge_read_exact(
    HANDLE pipe, void* output, DWORD size)
{
    BYTE* cursor = (BYTE*)output;
    DWORD total = 0;
    ULONGLONG started = GetTickCount64();
    while (total < size &&
           GetTickCount64() - started < DLUMA_ANALYSIS_BRIDGE_IO_DEADLINE_MS) {
        DWORD read = 0;
        if (ReadFile(pipe, cursor + total, size - total, &read, 0)) {
            total += read;
            if (!read) Sleep(1);
            continue;
        }
        if (GetLastError() != ERROR_NO_DATA) return 0;
        if (WaitForSingleObject(g_dluma_analysis_bridge.stop_event, 1) ==
                WAIT_OBJECT_0)
            return 0;
    }
    return total == size;
}

static int dluma_analysis_bridge_write_frame(
    HANDLE pipe, const DlumaAnalysisBridgeHeader* request,
    const char* payload, DWORD payload_size)
{
    BYTE frame[DLUMA_ANALYSIS_BRIDGE_MAX_FRAME];
    DlumaAnalysisBridgeHeader* response = (DlumaAnalysisBridgeHeader*)frame;
    DWORD frame_size;
    DWORD written = 0;
    BYTE authentication[32];
    unsigned int index;
    if (!request || payload_size >
            DLUMA_ANALYSIS_BRIDGE_MAX_FRAME - sizeof(*response))
        return 0;
    *response = *request;
    response->payload_size = payload_size;
    dluma_analysis_bridge_zero(
        response->authentication, sizeof(response->authentication));
    for (index = 0; index < payload_size; ++index)
        frame[sizeof(*response) + index] = (BYTE)payload[index];
    dluma_analysis_bridge_hmac(
        response, frame + sizeof(*response), authentication);
    for (index = 0; index < 32U; ++index)
        response->authentication[index] = authentication[index];
    frame_size = sizeof(*response) + payload_size;
    if (!WriteFile(pipe, frame, frame_size, &written, 0) ||
        written != frame_size)
        return 0;
    return 1;
}

static DWORD dluma_analysis_bridge_error(
    char* output, DWORD capacity, const char* code)
{
    DWORD size = 0;
    if (!dluma_analysis_bridge_append(
            output, capacity, &size, "{\"ok\":false,\"error\":\"") ||
        !dluma_analysis_bridge_append(output, capacity, &size, code) ||
        !dluma_analysis_bridge_append(output, capacity, &size, "\"}"))
        return 0;
    return size;
}

static DWORD dluma_analysis_bridge_immediate(
    DWORD command_id, char* output, DWORD capacity)
{
    DWORD size = 0;
    const DlumaBuildIdentity* identity = dluma_build_identity_snapshot();
    const DlumaDualHostRuntimeSnapshot* host = dluma_dual_host_runtime_snapshot();
    if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_PROTOCOL) {
        dluma_analysis_bridge_append(
            output, capacity, &size,
            "{\"ok\":true,\"protocol\":{\"major\":1,\"minor\":0,"
            "\"transport\":\"local_named_pipe\",\"max_frame\":16384}}"
        );
    } else if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_BUILD) {
        dluma_analysis_bridge_append(
            output, capacity, &size,
            "{\"ok\":true,\"loader_version\":\"" DLUMA_VERSION
            "\",\"git_head\":\"");
        dluma_analysis_bridge_append(
            output, capacity, &size, dluma_build_identity_git_head());
        dluma_analysis_bridge_append(
            output, capacity, &size, "\",\"sha256\":\"");
        dluma_analysis_bridge_append(
            output, capacity, &size,
            identity && identity->file_hash_valid ? identity->sha256 : "");
        dluma_analysis_bridge_append(
            output, capacity, &size, "\",\"complete\":");
        dluma_analysis_bridge_append(
            output, capacity, &size,
            identity && identity->complete ? "true}" : "false}");
    } else if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_LIFECYCLE) {
        dluma_analysis_bridge_append(
            output, capacity, &size, "{\"ok\":true,\"host\":\"");
        dluma_analysis_bridge_append(
            output, capacity, &size, dluma_analysis_bridge_host_name());
        dluma_analysis_bridge_append(
            output, capacity, &size, "\",\"exact_profile\":");
        dluma_analysis_bridge_append(
            output, capacity, &size,
            host && host->classification.exact_profile ? "true" : "false");
        dluma_analysis_bridge_append(
            output, capacity, &size, ",\"lifecycle_state\":");
        dluma_analysis_bridge_append_u64(
            output, capacity, &size,
            (ULONGLONG)dluma_atomic_compare_exchange(
                &g_dluma.lifecycle_state, 0, 0));
        dluma_analysis_bridge_append(output, capacity, &size, "}");
    } else if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_CAPABILITIES) {
        dluma_analysis_bridge_append(
            output, capacity, &size,
            "{\"ok\":true,\"dispatcher\":{\"available\":");
        dluma_analysis_bridge_append(
            output, capacity, &size,
            dluma_atomic_compare_exchange(
                &g_dluma_analysis_bridge.dispatcher_available, 0, 0)
                ? "true,\"reason\":\"player_game_thread_route\"}"
                : "false,\"reason\":\"unsupported_host_no_worker_fallback\"}");
        dluma_analysis_bridge_append(
            output, capacity, &size, ",\"actions\":{\"available\":");
        dluma_analysis_bridge_append(
            output, capacity, &size,
            g_dluma.config.analysis_bridge_allow_actions
                ? "true,\"reason\":\"explicitly_enabled\"}}"
                : "false,\"reason\":\"AllowActions_is_zero\"}}");
    } else if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_CATALOG) {
        dluma_analysis_bridge_append(
            output, capacity, &size,
            "{\"ok\":true,\"commands\":["
            "{\"id\":1,\"name\":\"protocol_identity\",\"effect\":false},"
            "{\"id\":2,\"name\":\"loader_identity\",\"effect\":false},"
            "{\"id\":3,\"name\":\"lifecycle_state\",\"effect\":false},"
            "{\"id\":4,\"name\":\"capabilities\",\"effect\":false},"
            "{\"id\":5,\"name\":\"command_catalog\",\"effect\":false},"
            "{\"id\":6,\"name\":\"player_startup\",\"effect\":false,"
            "\"host\":\"player\"},"
            "{\"id\":100,\"name\":\"dispatcher_ping\",\"effect\":false,"
            "\"host\":\"player\"},"
            "{\"id\":101,\"name\":\"game_thread_marker\",\"effect\":true,"
            "\"host\":\"player\",\"schema\":{\"value_max\":1000000}}]}"
        );
    } else if (command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_PLAYER_STARTUP) {
        DlumaPlayerStartupSnapshot snapshot;
        if (!dluma_player_startup_snapshot(&snapshot))
            return dluma_analysis_bridge_error(
                output, capacity, "player_startup_snapshot_unavailable");
        dluma_analysis_bridge_append(output, capacity, &size, "{\"ok\":true,\"profile_matched\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.profile_matched);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"splash_bound\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.splash_bound);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"splash_restored\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.splash_restored);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"splash_repaints\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.splash_repaint_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"player_hooks_installed\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.player_hooks_installed);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"player_hooks_owned\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.player_hooks_owned);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"player_hooks_restored\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.player_hooks_restored);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"engine_hooks_installed\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.engine_hooks_installed);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"engine_hooks_owned\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.engine_hooks_owned);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"engine_hooks_restored\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.engine_hooks_restored);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"ownership_conflict\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.ownership_conflict);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"status_updates\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.status_update_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"native_statuses\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.native_status_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"deferred_hide\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.deferred_hide_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"deferred_show\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.deferred_show_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"deferred_focus\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.deferred_focus_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"initialization_result_valid\":");
        dluma_analysis_bridge_append_bool(output, capacity, &size, snapshot.initialization_result_valid);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"initialization_result\":");
        if (snapshot.initialization_result < 0) {
            dluma_analysis_bridge_append(output, capacity, &size, "-");
            dluma_analysis_bridge_append_u64(
                output, capacity, &size,
                (ULONGLONG)(-(LONGLONG)snapshot.initialization_result));
        } else {
            dluma_analysis_bridge_append_u64(
                output, capacity, &size,
                (ULONGLONG)snapshot.initialization_result);
        }
        dluma_analysis_bridge_append(output, capacity, &size, ",\"reveal_mode\":");
        dluma_analysis_bridge_append_json_string(
            output, capacity, &size,
            dluma_player_startup_core_reveal_mode_name(snapshot.reveal_mode));
        dluma_analysis_bridge_append(output, capacity, &size, ",\"active_calls\":");
        dluma_analysis_bridge_append_u64(output, capacity, &size, snapshot.active_call_count);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"current_status\":");
        dluma_analysis_bridge_append_json_string(
            output, capacity, &size, snapshot.current_status);
        dluma_analysis_bridge_append(output, capacity, &size, ",\"fail_open_reason\":");
        dluma_analysis_bridge_append_json_string(
            output, capacity, &size, snapshot.fail_open_reason);
        dluma_analysis_bridge_append(output, capacity, &size, "}");
    } else {
        return dluma_analysis_bridge_error(
            output, capacity, "unknown_command");
    }
    return size;
}

static int dluma_analysis_bridge_parse_marker(
    const BYTE* payload, DWORD payload_size, DWORD* value)
{
    static const char prefix[] = "{\"value\":";
    DWORD cursor = 0;
    ULONGLONG parsed = 0;
    unsigned int index;
    if (!payload || !value || payload_size < sizeof(prefix)) return 0;
    for (index = 0; index + 1U < sizeof(prefix); ++index)
        if (payload[cursor++] != (BYTE)prefix[index]) return 0;
    if (cursor >= payload_size || payload[cursor] < '0' ||
        payload[cursor] > '9')
        return 0;
    while (cursor < payload_size && payload[cursor] >= '0' &&
           payload[cursor] <= '9') {
        ULONGLONG digit = (ULONGLONG)(payload[cursor++] - '0');
        if (parsed > (1000000ULL - digit) / 10ULL) return 0;
        parsed = parsed * 10ULL + digit;
    }
    if (cursor + 1U != payload_size || payload[cursor] != '}') return 0;
    *value = (DWORD)parsed;
    return 1;
}

static DlumaAnalysisBridgeSlot* dluma_analysis_bridge_enqueue(
    const DlumaAnalysisBridgeHeader* header, const BYTE* payload)
{
    unsigned int index;
    DlumaAnalysisBridgeSlot* selected = 0;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        DlumaAnalysisBridgeSlot* slot = &g_dluma_analysis_bridge.queue[index];
        if (slot->state == 0) {
            DWORD cursor;
            slot->header = *header;
            for (cursor = 0; cursor < header->payload_size; ++cursor)
                slot->payload[cursor] = payload[cursor];
            slot->response_size = 0;
            slot->response[0] = 0;
            ResetEvent(slot->completed_event);
            dluma_atomic_exchange(&slot->state, 1);
            selected = slot;
            break;
        }
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    return selected;
}

void dluma_analysis_bridge_game_thread_pump(void)
{
    unsigned int index;
    DlumaAnalysisBridgeSlot* selected = 0;
    DWORD size = 0;
    if (!dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.started, 0, 0) ||
        !g_dluma_analysis_bridge.queue_lock_initialized ||
        !dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.dispatcher_available, 0, 0) ||
        dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.quiescing, 0, 0))
        return;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        if (g_dluma_analysis_bridge.queue[index].state == 1) {
            selected = &g_dluma_analysis_bridge.queue[index];
            dluma_atomic_exchange(&selected->state, 2);
            break;
        }
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    if (!selected) return;
    g_dluma_analysis_bridge.game_thread_id = GetCurrentThreadId();
    if (selected->header.command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_PING) {
        dluma_analysis_bridge_append(
            selected->response, sizeof(selected->response), &size,
            "{\"ok\":true,\"game_thread_id\":");
        dluma_analysis_bridge_append_u64(
            selected->response, sizeof(selected->response), &size,
            g_dluma_analysis_bridge.game_thread_id);
        dluma_analysis_bridge_append(
            selected->response, sizeof(selected->response), &size, "}");
    } else if (selected->header.command_id ==
                   DLUMA_ANALYSIS_BRIDGE_COMMAND_MARKER) {
        DWORD marker;
        if (!dluma_analysis_bridge_parse_marker(
                selected->payload, selected->header.payload_size, &marker))
            size = dluma_analysis_bridge_error(
                selected->response, sizeof(selected->response),
                "invalid_marker_payload");
        else {
            g_dluma_analysis_bridge.marker = marker;
            dluma_analysis_bridge_append(
                selected->response, sizeof(selected->response), &size,
                "{\"ok\":true,\"marker\":");
            dluma_analysis_bridge_append_u64(
                selected->response, sizeof(selected->response), &size, marker);
            dluma_analysis_bridge_append(
                selected->response, sizeof(selected->response), &size, "}");
        }
    } else {
        size = dluma_analysis_bridge_error(
            selected->response, sizeof(selected->response),
            "dispatcher_command_unavailable");
    }
    selected->response_size = size;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    if (selected->state == 4) {
        selected->response_size = 0;
        selected->response[0] = 0;
        dluma_atomic_exchange(&selected->state, 0);
    } else {
        dluma_atomic_exchange(&selected->state, 3);
        SetEvent(selected->completed_event);
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
}

static void dluma_analysis_bridge_release_slot(DlumaAnalysisBridgeSlot* slot)
{
    if (!slot) return;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    slot->response_size = 0;
    slot->response[0] = 0;
    dluma_atomic_exchange(&slot->state, 0);
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
}

static void dluma_analysis_bridge_abandon_slot(DlumaAnalysisBridgeSlot* slot)
{
    LONG state;
    if (!slot) return;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    state = slot->state;
    if (state == 2) {
        /* The game thread owns the immutable request. Preserve the slot until
         * it notices this abandoned generation and clears it itself. */
        dluma_atomic_exchange(&slot->state, 4);
    } else {
        slot->response_size = 0;
        slot->response[0] = 0;
        dluma_atomic_exchange(&slot->state, 0);
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
}

static int dluma_analysis_bridge_handle_client(HANDLE pipe)
{
    DlumaAnalysisBridgeHeader header;
    BYTE payload[DLUMA_ANALYSIS_BRIDGE_MAX_FRAME - sizeof(header)];
    BYTE expected[32];
    char response[2048];
    DWORD response_size = 0;
    DlumaAnalysisBridgeSlot* slot;
    HANDLE waits[2];
    DWORD wait_result;
    if (!dluma_analysis_bridge_read_exact(pipe, &header, sizeof(header)) ||
        header.magic != DLUMA_ANALYSIS_BRIDGE_MAGIC ||
        header.major != DLUMA_ANALYSIS_BRIDGE_PROTOCOL_MAJOR ||
        header.minor > DLUMA_ANALYSIS_BRIDGE_PROTOCOL_MINOR ||
        header.payload_size > sizeof(payload))
        return 0;
    if (header.payload_size &&
        !dluma_analysis_bridge_read_exact(
            pipe, payload, header.payload_size))
        return 0;
    dluma_analysis_bridge_hmac(&header, payload, expected);
    if (!dluma_analysis_bridge_bytes_equal(
            header.authentication, expected, sizeof(expected)) ||
        !dluma_analysis_bridge_replay_accept(header.request_id))
        return 0;
    if (header.command_id >= DLUMA_ANALYSIS_BRIDGE_COMMAND_PROTOCOL &&
        header.command_id <= DLUMA_ANALYSIS_BRIDGE_COMMAND_PLAYER_STARTUP) {
        if (header.payload_size != 2U || payload[0] != '{' || payload[1] != '}')
            response_size = dluma_analysis_bridge_error(
                response, sizeof(response), "invalid_empty_schema");
        else
            response_size = dluma_analysis_bridge_immediate(
                header.command_id, response, sizeof(response));
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    if (header.command_id != DLUMA_ANALYSIS_BRIDGE_COMMAND_PING &&
        header.command_id != DLUMA_ANALYSIS_BRIDGE_COMMAND_MARKER) {
        response_size = dluma_analysis_bridge_error(
            response, sizeof(response), "unknown_command");
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    if (!dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.dispatcher_available, 0, 0)) {
        response_size = dluma_analysis_bridge_error(
            response, sizeof(response), "unsupported_host_no_worker_fallback");
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    if (header.command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_PING &&
        (header.payload_size != 2U || payload[0] != '{' || payload[1] != '}')) {
        response_size = dluma_analysis_bridge_error(
            response, sizeof(response), "invalid_empty_schema");
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    if (header.command_id == DLUMA_ANALYSIS_BRIDGE_COMMAND_MARKER &&
        !g_dluma.config.analysis_bridge_allow_actions) {
        response_size = dluma_analysis_bridge_error(
            response, sizeof(response), "actions_disabled");
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    slot = dluma_analysis_bridge_enqueue(&header, payload);
    if (!slot) {
        response_size = dluma_analysis_bridge_error(
            response, sizeof(response), "dispatcher_queue_saturated");
        return dluma_analysis_bridge_write_frame(
            pipe, &header, response, response_size);
    }
    waits[0] = slot->completed_event;
    waits[1] = g_dluma_analysis_bridge.stop_event;
    wait_result = WaitForMultipleObjects(2U, waits, FALSE, 5000U);
    if (wait_result == WAIT_OBJECT_0) {
        int written = dluma_analysis_bridge_write_frame(
            pipe, &header, slot->response, slot->response_size);
        dluma_analysis_bridge_release_slot(slot);
        return written;
    }
    dluma_analysis_bridge_abandon_slot(slot);
    return 0;
}

static DWORD WINAPI dluma_analysis_bridge_worker(LPVOID context)
{
    HANDLE pipe = (HANDLE)context;
    for (;;) {
        BOOL connected;
        DWORD error;
        if (WaitForSingleObject(
                g_dluma_analysis_bridge.stop_event, 0) == WAIT_OBJECT_0)
            break;
        connected = ConnectNamedPipe(pipe, 0);
        if (!connected) {
            error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) connected = TRUE;
            else if (error == ERROR_PIPE_LISTENING || error == ERROR_NO_DATA) {
                if (error == ERROR_NO_DATA) DisconnectNamedPipe(pipe);
                Sleep(2);
                continue;
            } else {
                dluma_log_hex(
                    "ERROR", "Analysis bridge ConnectNamedPipe failed", error);
                break;
            }
        }
        if (connected) {
            (void)dluma_analysis_bridge_handle_client(pipe);
            DisconnectNamedPipe(pipe);
        }
    }
    dluma_atomic_exchange(&g_dluma_analysis_bridge.stop_succeeded, 1);
    return 0;
}

static void dluma_analysis_bridge_cancel_queue(void)
{
    unsigned int index;
    if (!g_dluma_analysis_bridge.queue_lock_initialized) return;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        DlumaAnalysisBridgeSlot* slot = &g_dluma_analysis_bridge.queue[index];
        if (slot->state == 1) {
            slot->response_size = dluma_analysis_bridge_error(
                slot->response, sizeof(slot->response), "bridge_quiescing");
            dluma_atomic_exchange(&slot->state, 3);
            SetEvent(slot->completed_event);
        }
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
}

static int dluma_analysis_bridge_game_requests_drained(void)
{
    unsigned int index;
    int drained = 1;
    if (!g_dluma_analysis_bridge.queue_lock_initialized) return 1;
    EnterCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        LONG state = g_dluma_analysis_bridge.queue[index].state;
        if (state == 2 || state == 4) {
            drained = 0;
            break;
        }
    }
    LeaveCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    return drained;
}

static void dluma_analysis_bridge_cleanup(void)
{
    unsigned int index;
    if (g_dluma_analysis_bridge.worker_thread) {
        CloseHandle(g_dluma_analysis_bridge.worker_thread);
        g_dluma_analysis_bridge.worker_thread = 0;
    }
    if (g_dluma_analysis_bridge.pipe &&
        g_dluma_analysis_bridge.pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_dluma_analysis_bridge.pipe);
        g_dluma_analysis_bridge.pipe = INVALID_HANDLE_VALUE;
    }
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        if (g_dluma_analysis_bridge.queue[index].completed_event) {
            CloseHandle(g_dluma_analysis_bridge.queue[index].completed_event);
            g_dluma_analysis_bridge.queue[index].completed_event = 0;
        }
        g_dluma_analysis_bridge.queue[index].state = 0;
    }
    if (g_dluma_analysis_bridge.stop_event) {
        CloseHandle(g_dluma_analysis_bridge.stop_event);
        g_dluma_analysis_bridge.stop_event = 0;
    }
    if (g_dluma_analysis_bridge.queue_lock_initialized) {
        DeleteCriticalSection(&g_dluma_analysis_bridge.queue_lock);
        g_dluma_analysis_bridge.queue_lock_initialized = 0;
    }
    dluma_analysis_bridge_zero(
        g_dluma_analysis_bridge.key, sizeof(g_dluma_analysis_bridge.key));
}

int dluma_analysis_bridge_start(int game_thread_dispatcher_available)
{
    unsigned int index;
    DWORD thread_id = 0;
    if (!g_dluma.config.analysis_bridge_enabled) return 1;
    if (dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.started, 1, 0) != 0)
        return 1;
    g_dluma_analysis_bridge.pipe = INVALID_HANDLE_VALUE;
    g_dluma_analysis_bridge.quiescing = 0;
    g_dluma_analysis_bridge.stop_succeeded = 0;
    g_dluma_analysis_bridge.dispatcher_available =
        game_thread_dispatcher_available ? 1 : 0;
    dluma_analysis_bridge_zero(
        g_dluma_analysis_bridge.replay,
        sizeof(g_dluma_analysis_bridge.replay));
    g_dluma_analysis_bridge.replay_count = 0;
    g_dluma_analysis_bridge.marker = 0;
    g_dluma_analysis_bridge.game_thread_id = 0;
    InitializeCriticalSection(&g_dluma_analysis_bridge.queue_lock);
    g_dluma_analysis_bridge.queue_lock_initialized = 1;
    g_dluma_analysis_bridge.stop_event = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_analysis_bridge.stop_event) goto failed;
    for (index = 0; index < DLUMA_ANALYSIS_BRIDGE_QUEUE_CAPACITY; ++index) {
        g_dluma_analysis_bridge.queue[index].completed_event =
            CreateEventW(0, TRUE, FALSE, 0);
        if (!g_dluma_analysis_bridge.queue[index].completed_event) goto failed;
    }
    if (!dluma_analysis_bridge_load_key()) {
        dluma_log_line(
            "ERROR", "Analysis bridge key or StateId is unavailable; bridge refused.");
        goto failed;
    }
    g_dluma_analysis_bridge.pipe = dluma_analysis_bridge_create_pipe();
    if (g_dluma_analysis_bridge.pipe == INVALID_HANDLE_VALUE) {
        dluma_log_hex(
            "ERROR", "Analysis bridge private pipe creation failed", GetLastError());
        goto failed;
    }
    g_dluma_analysis_bridge.worker_thread = CreateThread(
        0, 0, dluma_analysis_bridge_worker,
        g_dluma_analysis_bridge.pipe, 0, &thread_id);
    if (!g_dluma_analysis_bridge.worker_thread) {
        dluma_log_hex(
            "ERROR", "Analysis bridge worker creation failed", GetLastError());
        goto failed;
    }
    dluma_log_pair(
        "INFO", "analysis_bridge_dispatcher",
        game_thread_dispatcher_available ? "player_game_thread" : "unavailable");
    return 1;
failed:
    dluma_analysis_bridge_cleanup();
    dluma_atomic_exchange(&g_dluma_analysis_bridge.started, 0);
    return 0;
}

void dluma_analysis_bridge_quiesce(void)
{
    if (!dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.started, 0, 0))
        return;
    dluma_atomic_exchange(&g_dluma_analysis_bridge.quiescing, 1);
    if (g_dluma_analysis_bridge.stop_event)
        SetEvent(g_dluma_analysis_bridge.stop_event);
    dluma_analysis_bridge_cancel_queue();
}

int dluma_analysis_bridge_stop(void)
{
    DWORD result;
    ULONGLONG started;
    if (!dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.started, 0, 0))
        return 1;
    dluma_analysis_bridge_quiesce();
    if (!g_dluma_analysis_bridge.worker_thread) return 0;
    result = WaitForSingleObject(g_dluma_analysis_bridge.worker_thread, 5000U);
    if (result != WAIT_OBJECT_0 ||
        !dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.stop_succeeded, 0, 0))
        return 0;
    started = GetTickCount64();
    while (!dluma_analysis_bridge_game_requests_drained() &&
           GetTickCount64() - started < 5000ULL)
        Sleep(1);
    if (!dluma_analysis_bridge_game_requests_drained()) {
        dluma_atomic_exchange(&g_dluma_analysis_bridge.stop_succeeded, 0);
        return 0;
    }
    dluma_analysis_bridge_cleanup();
    dluma_atomic_exchange(&g_dluma_analysis_bridge.dispatcher_available, 0);
    dluma_atomic_exchange(&g_dluma_analysis_bridge.started, 0);
    return 1;
}

int dluma_analysis_bridge_stop_succeeded(void)
{
    if (!dluma_atomic_compare_exchange(
            &g_dluma_analysis_bridge.started, 0, 0))
        return 1;
    return dluma_atomic_compare_exchange(
        &g_dluma_analysis_bridge.stop_succeeded, 0, 0) != 0;
}
