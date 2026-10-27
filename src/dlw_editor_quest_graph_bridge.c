#include "dlw_editor_quest_graph_bridge.h"

#include "dlw_dual_host_runtime.h"
#include "dlw_editor_launch.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "core/dlw_runtime_tree.h"
#include "core/dlw_json_reader.h"
#include "core/dlw_quest_context_core.h"
#include "core/dlw_quest_graph_edits.h"
#include "generated/dluma_api_catalog.generated.h"

#include <string.h>

static int dluma_qg_compare(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right ? 0 : left ? 1 : -1;
    while (left[index] && left[index] == right[index]) ++index;
    return (unsigned char)left[index] - (unsigned char)right[index];
}

static int dluma_qg_compare_n(
    const char* left,
    const char* right,
    unsigned int count)
{
    unsigned int index;
    if (!left || !right) return left == right ? 0 : left ? 1 : -1;
    for (index = 0; index < count; ++index) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];
        if (left_byte != right_byte) return left_byte - right_byte;
        if (!left_byte) return 0;
    }
    return 0;
}

#define strcmp dluma_qg_compare
#define strncmp(left, right, count) \
    dluma_qg_compare_n((left), (right), (unsigned int)(count))

/* Exact Windows QuestEditor identity assessed as implementation-ready on
 * 2026-08-26. The PXSL dialog signature, save handler, and MFC FromHandle
 * IAT entry below are admitted only after this complete PE identity
 * succeeds. */
#define DLUMA_QG_QUESTEDITOR_TIMESTAMP 0x623395EBU
#define DLUMA_QG_QUESTEDITOR_IMAGE_SIZE 0x0011F000U
#define DLUMA_QG_STOCK_SAVE_RVA 0x0000D4C0U
#define DLUMA_QG_CWND_FROM_HANDLE_IAT_RVA 0x000B2BF0U
#define DLUMA_QG_COMPILE_COMMAND 0x0E10U
#define DLUMA_QG_SCINTILLA_CONTROL 3581
#define DLUMA_QG_CONTEXT_CONTROL 3602
#define DLUMA_QG_OUTPUT_CONTROL 3603
#define DLUMA_QG_STATUS_CONTROL 3608
#define DLUMA_QG_PATCH_DIFF_CONTROL 3601
#define DLUMA_QG_BUTTON_CONTROL 0x7D41U
#define DLUMA_QG_MAIN_DISPATCH (WM_APP + 0x351U)
#define DLUMA_QG_MAIN_DISCOVER (WM_APP + 0x352U)
#define DLUMA_QG_MAIN_TEARDOWN (WM_APP + 0x353U)
#define DLUMA_QG_SOURCE_TIMER_BASE 0x4D10U
#define DLUMA_QG_SOURCE_DEBOUNCE_MS 350U
#define DLUMA_QG_MAX_DIALOGS 16U
#define DLUMA_QG_MAX_EDITS 4096U
#define DLUMA_QG_MAX_SOURCE_BYTES (8U * 1024U * 1024U)
#define DLUMA_QG_MAX_FRAME_BYTES (16U * 1024U * 1024U)
#define DLUMA_QG_MAX_COMPANION_BYTES (512ULL * 1024ULL * 1024ULL)
#define DLUMA_QG_MAX_PROJECT_ENTRIES 50000U
#define DLUMA_QG_MAX_PROJECT_DEPTH 32U
#define DLUMA_QG_MAX_WORKSHOP_PROJECTS 4096U
#define DLUMA_QG_IO_TIMEOUT_MS 5000ULL
#define DLUMA_QG_PIPE_POLL_MS 2U
#define DLUMA_QG_PRIVATE_RUNTIME_VERSION "10.0.13"

#define SCI_SETSAVEPOINT 2014U
#define SCI_SETTEXT 2181U
#define SCI_GETMODIFY 2159U
#define SCI_GETTEXT 2182U
#define SCI_GETTEXTLENGTH 2183U
#define SCI_GETCODEPAGE 2137U
#define SCI_GOTOPOS 2025U
#define SCI_BEGINUNDOACTION 2078U
#define SCI_ENDUNDOACTION 2079U
#define SCI_UNDO 2176U
#define SCI_SETTARGETSTART 2190U
#define SCI_SETTARGETEND 2192U
#define SCI_REPLACETARGET 2194U
#define SCI_GETCURRENTPOS 2008U
#define SCI_AUTOCSHOW 2100U
#define SCI_AUTOCSETSEPARATOR 2106U
#define DLUMA_QG_AUTOCOMPLETE_BYTES 65536U
#define SCN_MODIFIED 2008U

#define WS_CHILD 0x40000000UL
#define WS_VISIBLE 0x10000000UL
#define WS_TABSTOP 0x00010000UL
#define BS_PUSHBUTTON 0x00000000UL
#define SWP_NOACTIVATE 0x0010U
#define SWP_SHOWWINDOW 0x0040U
#define HWND_TOP ((HWND)0)
#define WM_GETFONT 0x0031U
#define WM_SETFONT 0x0030U
#define WM_NOTIFY 0x004EU
#define WM_TIMER 0x0113U

#define PIPE_ACCESS_DUPLEX 0x00000003UL
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000UL
#define PIPE_TYPE_BYTE 0x00000000UL
#define PIPE_READMODE_BYTE 0x00000000UL
#define PIPE_NOWAIT 0x00000001UL
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008UL
#define ERROR_NO_DATA 232UL
#define ERROR_PIPE_CONNECTED 535UL
#define ERROR_PIPE_LISTENING 536UL
#define ERROR_BROKEN_PIPE 109UL

#define TokenUser 1
#define TOKEN_QUERY 0x0008UL
#define ACL_REVISION 2UL
#define SECURITY_DESCRIPTOR_REVISION 1UL
#define GENERIC_ALL 0x10000000UL
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002UL
#define CREATE_UNICODE_ENVIRONMENT 0x00000400UL

#define DLUMA_SMTO_BLOCK 0x0001U
#define DLUMA_SMTO_ABORTIFHUNG 0x0002U
#define DLUMA_SMTO_ERRORONEXIT 0x0020U

typedef HANDLE* PHANDLE;
typedef DWORD* PDWORD;
typedef void* PSID;
typedef int TOKEN_INFORMATION_CLASS;

typedef struct DlumaQgAcl {
    BYTE revision;
    BYTE reserved1;
    WORD size;
    WORD ace_count;
    WORD reserved2;
} DlumaQgAcl;

typedef struct DlumaQgAceHeader {
    BYTE type;
    BYTE flags;
    WORD size;
} DlumaQgAceHeader;

typedef struct DlumaQgAccessAllowedAce {
    DlumaQgAceHeader header;
    DWORD mask;
    DWORD sid_start;
} DlumaQgAccessAllowedAce;

typedef struct DlumaQgSidAndAttributes {
    PSID sid;
    DWORD attributes;
} DlumaQgSidAndAttributes;

typedef struct DlumaQgTokenUser {
    DlumaQgSidAndAttributes user;
} DlumaQgTokenUser;

typedef struct DlumaQgSecurityDescriptor {
    BYTE revision;
    BYTE reserved1;
    WORD control;
    PVOID owner;
    PVOID group;
    DlumaQgAcl* sacl;
    DlumaQgAcl* dacl;
} DlumaQgSecurityDescriptor;

typedef struct DlumaQgSecurityAttributes {
    DWORD length;
    LPVOID descriptor;
    BOOL inherit;
} DlumaQgSecurityAttributes;

typedef struct DlumaQgStartupInfo {
    DWORD size;
    LPWSTR reserved;
    LPWSTR desktop;
    LPWSTR title;
    DWORD x;
    DWORD y;
    DWORD width;
    DWORD height;
    DWORD x_chars;
    DWORD y_chars;
    DWORD fill_attribute;
    DWORD flags;
    WORD show_window;
    WORD reserved2_size;
    BYTE* reserved2;
    HANDLE input;
    HANDLE output;
    HANDLE error;
} DlumaQgStartupInfo;

typedef struct DlumaQgNmHdr {
    HWND window_from;
    UINT_PTR id_from;
    UINT code;
} DlumaQgNmHdr;

typedef struct DlumaQgProcessInformation {
    HANDLE process;
    HANDLE thread;
    DWORD process_id;
    DWORD thread_id;
} DlumaQgProcessInformation;

typedef BOOL (WINAPI *DlumaQgOpenProcessTokenFn)(
    HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *DlumaQgGetTokenInformationFn)(
    HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
typedef BOOL (WINAPI *DlumaQgInitializeAclFn)(
    DlumaQgAcl*, DWORD, DWORD);
typedef BOOL (WINAPI *DlumaQgAddAccessAllowedAceFn)(
    DlumaQgAcl*, DWORD, DWORD, PSID);
typedef BOOL (WINAPI *DlumaQgInitializeSecurityDescriptorFn)(
    DlumaQgSecurityDescriptor*, DWORD);
typedef BOOL (WINAPI *DlumaQgSetSecurityDescriptorDaclFn)(
    DlumaQgSecurityDescriptor*, BOOL, DlumaQgAcl*, BOOL);
typedef DWORD (WINAPI *DlumaQgGetLengthSidFn)(PSID);
typedef LONG (WINAPI *DlumaQgBCryptGenRandomFn)(
    PVOID, BYTE*, ULONG, ULONG);
typedef void* (__fastcall *DlumaQgCWndFromHandleFn)(HWND);
typedef __int64 (__fastcall *DlumaQgStockSaveFn)(void*);

__declspec(dllimport) HWND WINAPI CreateWindowExW(
    DWORD extended_style, LPCWSTR class_name, LPCWSTR window_name,
    DWORD style, int x, int y, int width, int height, HWND parent,
    HMENU menu, HINSTANCE instance, LPVOID parameter);
__declspec(dllimport) BOOL WINAPI DestroyWindow(HWND window);
__declspec(dllimport) BOOL WINAPI SetWindowPos(
    HWND window, HWND insert_after, int x, int y, int width, int height,
    UINT flags);
__declspec(dllimport) HWND WINAPI GetDlgItem(HWND window, int identifier);
__declspec(dllimport) HWND WINAPI GetParent(HWND window);
__declspec(dllimport) BOOL WINAPI EnumChildWindows(
    HWND parent, WNDENUMPROC callback, LPARAM parameter);
__declspec(dllimport) BOOL WINAPI PostMessageW(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);
__declspec(dllimport) HANDLE WINAPI CreateNamedPipeW(
    LPCWSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
    DWORD output_buffer_size, DWORD input_buffer_size,
    DWORD default_timeout, DlumaQgSecurityAttributes* security_attributes);
__declspec(dllimport) BOOL WINAPI ConnectNamedPipe(
    HANDLE pipe, LPVOID overlapped);
__declspec(dllimport) BOOL WINAPI DisconnectNamedPipe(HANDLE pipe);
__declspec(dllimport) BOOL WINAPI GetNamedPipeClientProcessId(
    HANDLE pipe, ULONG* client_process_id);
__declspec(dllimport) BOOL WINAPI CreateProcessW(
    LPCWSTR application_name, LPWSTR command_line,
    LPVOID process_attributes, LPVOID thread_attributes,
    BOOL inherit_handles, DWORD creation_flags, LPVOID environment,
    LPCWSTR current_directory, DlumaQgStartupInfo* startup,
    DlumaQgProcessInformation* information);
__declspec(dllimport) LPWSTR WINAPI GetEnvironmentStringsW(void);
__declspec(dllimport) BOOL WINAPI FreeEnvironmentStringsW(LPWSTR environment);
__declspec(dllimport) HWND WINAPI SetFocus(HWND window);
__declspec(dllimport) UINT WINAPI GetACP(void);

typedef struct DlumaQgDialog {
    HWND dialog;
    HWND scintilla;
    HWND button;
    WNDPROC predecessor;
    WNDPROC scintilla_predecessor;
    char autocomplete_separator;
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR source_path[DLUMA_MAX_WPATH];
    char relative_path[DLUMA_MAX_WPATH * 4U];
    char document_id[DLUMA_SHA256_HEX_CAPACITY];
    char source_sha256[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG revision;
    ULONGLONG saved_revision;
    char saved_sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int suppress_source_notification;
} DlumaQgDialog;

typedef enum DlumaQgUiRequestKind {
    DLUMA_QG_UI_NONE = 0,
    DLUMA_QG_UI_SAVE = 1,
    DLUMA_QG_UI_COMPILE = 2,
    DLUMA_QG_UI_APPLY_SOURCE = 3,
    DLUMA_QG_UI_ACTIVATE = 4,
    DLUMA_QG_UI_RESTORE_SOURCE = 5
} DlumaQgUiRequestKind;

typedef DlumaQuestGraphTextEdit DlumaQgTextEdit;

typedef struct DlumaQgUiRequest {
    volatile LONG state;
    DlumaQgUiRequestKind kind;
    ULONGLONG expected_revision;
    ULONGLONG response_revision;
    char document_id[DLUMA_SHA256_HEX_CAPACITY];
    char expected_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char expected_result_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char source_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaQgTextEdit* edits;
    DWORD edit_count;
    char* restore_source;
    DWORD restore_source_size;
    unsigned int restore_was_modified;
    DWORD byte_offset;
    unsigned int has_byte_offset;
    char disk_path[DLUMA_MAX_WPATH * 4U];
    char error[96];
} DlumaQgUiRequest;

typedef struct DlumaQgSearch {
    const WCHAR* project_root;
    const WCHAR* preferred_leaf;
    const char* source_sha256;
    WCHAR match[DLUMA_MAX_WPATH];
    unsigned int match_count;
    unsigned int entries;
    unsigned int refused;
    unsigned int name_only;
} DlumaQgSearch;

typedef struct DlumaQgWindowCandidate {
    DWORD process_id;
    HWND window;
    ULONGLONG area;
} DlumaQgWindowCandidate;

typedef struct DlumaQgBridge {
    volatile LONG start_state;
    volatile LONG stopping;
    volatile LONG attach_lock;
    volatile LONG profile_state;
    volatile LONG callbacks_in_flight;
    volatile LONG output_lock;
    volatile LONG context_lock;
    volatile LONG connected;
    volatile LONG authenticated;
    volatile LONG worker_stopped;
    HWND main_window;
    WNDPROC main_predecessor;
    DWORD owner_thread_id;
    HMODULE quest_editor;
    DlumaQgDialog dialogs[DLUMA_QG_MAX_DIALOGS];
    DlumaQgUiRequest ui_request;
    HANDLE stop_event;
    HANDLE worker_thread;
    HANDLE pipe;
    WCHAR pipe_name[160];
    WCHAR pipe_short_name[128];
    char token[65];
    char* pending_frame;
    DWORD pending_frame_size;
    DlumaQuestContextCore context;
    char context_project_identity[DLUMA_MAX_WPATH * 4U];
    char context_map_identity[DLUMA_MAX_MAP_NAME];
    ULONGLONG pending_generation;
    ULONGLONG sent_generation;
    ULONGLONG last_launch_tick;
    HANDLE companion_process;
    DWORD companion_process_id;
    char companion_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaRuntimeTreeLease runtime_lease;
} DlumaQgBridge;

static DlumaQgBridge g_dluma_qg;

static const char g_dluma_qg_quest_editor_sha256[] =
    "56554E8EC993B1580E51E2E4F37B934B0E1B705D8282BA0309C1EF309CF4A3EB";

static int dluma_qg_ascii_hex(char value)
{
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

static int dluma_qg_hash_valid(const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 64U; ++index)
        if (!dluma_qg_ascii_hex(value[index])) return 0;
    return value[64] == 0;
}

static int dluma_qg_request_id_valid(const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 32U; ++index)
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f')))
            return 0;
    return value[32] == 0;
}

static int dluma_qg_token_equal(const char* value);

static int dluma_qg_message_type_supported(const char* value)
{
    return value &&
        (strcmp(value, "hello") == 0 ||
         strcmp(value, "applyEdits") == 0 ||
         strcmp(value, "restoreDocument") == 0 ||
         strcmp(value, "saveDocument") == 0 ||
         strcmp(value, "compileDocument") == 0 ||
         strcmp(value, "getContext") == 0 ||
         strcmp(value, "activateDocument") == 0 ||
         strcmp(value, "closeDocument") == 0);
}

static int dluma_qg_parse_envelope(
    char* json, DWORD size,
    char* type, unsigned int type_capacity,
    char* request_id, unsigned int request_id_capacity,
    char* token, unsigned int token_capacity,
    char** payload, DWORD* payload_size)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    int first = 1;
    const unsigned char* payload_begin = 0;
    const unsigned char* payload_end = 0;
    if (!json || !size || !type || !type_capacity ||
        !request_id || !request_id_capacity ||
        !token || !token_capacity || !payload || !payload_size)
        return 0;
    *payload = 0;
    *payload_size = 0;
    dluma_json_reader_initialize(&reader, json, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        char key[32];
        unsigned int bit;
        int next = dluma_json_object_next(&reader, &first);
        if (next <= 0) {
            if (next < 0) return 0;
            break;
        }
        if (!dluma_json_string(
                &reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(&reader, ':'))
            return 0;
        if (strcmp(key, "schemaVersion") == 0) {
            uint64_t value = 0;
            bit = 1U << 0;
            if ((fields & bit) ||
                !dluma_json_u64(&reader, &value) || value != 2U)
                return 0;
        } else if (strcmp(key, "type") == 0) {
            bit = 1U << 1;
            if ((fields & bit) ||
                !dluma_json_string(&reader, type, type_capacity))
                return 0;
        } else if (strcmp(key, "requestId") == 0) {
            bit = 1U << 2;
            if ((fields & bit) ||
                !dluma_json_string(
                    &reader, request_id, request_id_capacity))
                return 0;
        } else if (strcmp(key, "sessionToken") == 0) {
            bit = 1U << 3;
            if ((fields & bit) ||
                !dluma_json_string(&reader, token, token_capacity))
                return 0;
        } else if (strcmp(key, "payload") == 0) {
            bit = 1U << 4;
            dluma_json_space(&reader);
            if ((fields & bit) ||
                reader.cursor >= reader.end ||
                *reader.cursor != '{')
                return 0;
            payload_begin = reader.cursor;
            if (!dluma_json_skip_value(&reader)) return 0;
            payload_end = reader.cursor;
        } else {
            return 0;
        }
        fields |= bit;
    }
    if (!dluma_json_object_end(&reader) ||
        !dluma_json_finished(&reader) ||
        fields != 0x1FU ||
        !payload_begin || !payload_end ||
        payload_end <= payload_begin ||
        payload_end >= (const unsigned char*)json + size ||
        !dluma_qg_message_type_supported(type) ||
        !dluma_qg_request_id_valid(request_id) ||
        !dluma_qg_token_equal(token))
        return 0;
    *payload = (char*)payload_begin;
    *payload_size = (DWORD)(payload_end - payload_begin);
    *(char*)payload_end = 0;
    return 1;
}

static int dluma_qg_payload_mark(
    unsigned int* fields, unsigned int bit)
{
    if (!fields || (*fields & bit)) return 0;
    *fields |= bit;
    return 1;
}

static int dluma_qg_json_nullable_u64(DlumaJsonReader* reader)
{
    uint64_t value = 0;
    if (!reader) return 0;
    dluma_json_space(reader);
    if (reader->cursor < reader->end &&
        *reader->cursor == 'n')
        return dluma_json_skip_value(reader);
    return dluma_json_u64(reader, &value);
}

static int dluma_qg_validate_edits_payload(DlumaJsonReader* reader)
{
    unsigned int count = 0;
    int first = 1;
    if (!dluma_json_array_begin(reader)) return 0;
    for (;;) {
        unsigned int fields = 0;
        int object_first = 1;
        int next = dluma_json_array_next(reader, &first);
        if (next <= 0) {
            if (next < 0) return 0;
            break;
        }
        if (++count > DLUMA_QG_MAX_EDITS ||
            !dluma_json_object_begin(reader))
            return 0;
        for (;;) {
            char key[32];
            int object_next =
                dluma_json_object_next(reader, &object_first);
            if (object_next <= 0) {
                if (object_next < 0) return 0;
                break;
            }
            if (!dluma_json_string(
                    reader, key, (unsigned int)sizeof(key)) ||
                !dluma_json_take(reader, ':'))
                return 0;
            if (strcmp(key, "byteOffset") == 0) {
                uint64_t value = 0;
                if (!dluma_qg_payload_mark(&fields, 1U << 0) ||
                    !dluma_json_u64(reader, &value))
                    return 0;
            } else if (strcmp(key, "removedLength") == 0) {
                uint64_t value = 0;
                if (!dluma_qg_payload_mark(&fields, 1U << 1) ||
                    !dluma_json_u64(reader, &value))
                    return 0;
            } else if (strcmp(key, "replacementBytes") == 0) {
                if (!dluma_qg_payload_mark(&fields, 1U << 2) ||
                    !dluma_json_string(reader, 0, 0))
                    return 0;
            } else {
                return 0;
            }
        }
        if (!dluma_json_object_end(reader) || fields != 0x07U)
            return 0;
    }
    return count > 0U && dluma_json_array_end(reader);
}

static int dluma_qg_validate_payload(
    const char* type, const char* payload, DWORD payload_size)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    unsigned int expected_fields = 0;
    int first = 1;
    if (!type || !payload || !payload_size) return 0;
    if (strcmp(type, "hello") == 0)
        expected_fields = 0x0FU;
    else if (strcmp(type, "getContext") == 0 ||
             strcmp(type, "closeDocument") == 0)
        expected_fields = 0x01U;
    else if (strcmp(type, "activateDocument") == 0)
        expected_fields = 0x03U;
    else if (strcmp(type, "saveDocument") == 0 ||
             strcmp(type, "compileDocument") == 0)
        expected_fields = 0x0FU;
    else if (strcmp(type, "applyEdits") == 0)
        expected_fields = 0x1FU;
    else if (strcmp(type, "restoreDocument") == 0)
        expected_fields = 0x0FU;
    else
        return 0;
    dluma_json_reader_initialize(&reader, payload, payload_size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        char key[48];
        int next = dluma_json_object_next(&reader, &first);
        if (next <= 0) {
            if (next < 0) return 0;
            break;
        }
        if (!dluma_json_string(
                &reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(&reader, ':'))
            return 0;
        if (strcmp(type, "hello") == 0 &&
            strcmp(key, "protocolVersion") == 0) {
            uint64_t value = 0;
            if (!dluma_qg_payload_mark(&fields, 1U << 0) ||
                !dluma_json_u64(&reader, &value))
                return 0;
        } else if (strcmp(type, "hello") == 0 &&
                   strcmp(key, "clientProcessId") == 0) {
            uint64_t value = 0;
            if (!dluma_qg_payload_mark(&fields, 1U << 1) ||
                !dluma_json_u64(&reader, &value))
                return 0;
        } else if (strcmp(type, "hello") == 0 &&
                   strcmp(key, "editorProcessId") == 0) {
            uint64_t value = 0;
            if (!dluma_qg_payload_mark(&fields, 1U << 2) ||
                !dluma_json_u64(&reader, &value))
                return 0;
        } else if (strcmp(type, "hello") == 0 &&
                   strcmp(key, "executableSha256") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 3) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "getContext") == 0 &&
                   strcmp(key, "expectedGeneration") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 0) ||
                !dluma_qg_json_nullable_u64(&reader))
                return 0;
        } else if (strcmp(key, "documentId") == 0 &&
                   strcmp(type, "getContext") != 0 &&
                   strcmp(type, "hello") != 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 0) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "activateDocument") == 0 &&
                   strcmp(key, "byteOffset") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 1) ||
                !dluma_qg_json_nullable_u64(&reader))
                return 0;
        } else if (strcmp(type, "restoreDocument") == 0 &&
                   strcmp(key, "baselineBytes") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 1) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "restoreDocument") == 0 &&
                   strcmp(key, "baselineSha256") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 2) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "restoreDocument") == 0 &&
                   strcmp(key, "wasModified") == 0) {
            int value = 0;
            if (!dluma_qg_payload_mark(&fields, 1U << 3) ||
                !dluma_json_boolean(&reader, &value))
                return 0;
        } else if ((strcmp(type, "saveDocument") == 0 ||
                    strcmp(type, "compileDocument") == 0 ||
                    strcmp(type, "applyEdits") == 0) &&
                   strcmp(key, "expectedRevision") == 0) {
            uint64_t value = 0;
            if (!dluma_qg_payload_mark(&fields, 1U << 1) ||
                !dluma_json_u64(&reader, &value))
                return 0;
        } else if ((strcmp(type, "saveDocument") == 0 ||
                    strcmp(type, "compileDocument") == 0 ||
                    strcmp(type, "applyEdits") == 0) &&
                   strcmp(key, "expectedSha256") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 2) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if ((strcmp(type, "saveDocument") == 0 ||
                    strcmp(type, "compileDocument") == 0) &&
                   strcmp(key, "sourceSha256") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 3) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "applyEdits") == 0 &&
                   strcmp(key, "expectedResultSha256") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 3) ||
                !dluma_json_string(&reader, 0, 0))
                return 0;
        } else if (strcmp(type, "applyEdits") == 0 &&
                   strcmp(key, "edits") == 0) {
            if (!dluma_qg_payload_mark(&fields, 1U << 4) ||
                !dluma_qg_validate_edits_payload(&reader))
                return 0;
        } else {
            return 0;
        }
    }
    return dluma_json_object_end(&reader) &&
        dluma_json_finished(&reader) &&
        fields == expected_fields;
}

static int dluma_qg_identifier_valid(
    const char* value, unsigned int max_length)
{
    unsigned int index;
    if (!value || !value[0]) return 0;
    for (index = 0; value[index] && index < max_length; ++index) {
        char ch = value[index];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_' || ch == '.'))
            return 0;
    }
    return index > 0 && index < max_length && value[index] == 0;
}

static void dluma_qg_lock(volatile LONG* lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_qg_unlock(volatile LONG* lock)
{
    dluma_atomic_exchange(lock, 0);
}

static WCHAR dluma_qg_fold_wide(WCHAR value)
{
    if (value >= L'A' && value <= L'Z')
        return value + (L'a' - L'A');
    return value;
}

static int dluma_qg_wide_starts_icase(
    const WCHAR* value, const WCHAR* expected)
{
    unsigned int index = 0;
    if (!value || !expected) return 0;
    while (expected[index]) {
        if (!value[index] ||
            dluma_qg_fold_wide(value[index]) !=
                dluma_qg_fold_wide(expected[index]))
            return 0;
        ++index;
    }
    return 1;
}

static int dluma_qg_wide_contains_icase(
    const WCHAR* value, const WCHAR* expected)
{
    unsigned int index;
    if (!value || !expected || !expected[0]) return 0;
    for (index = 0; value[index]; ++index)
        if (dluma_qg_wide_starts_icase(value + index, expected))
            return 1;
    return 0;
}

static int dluma_qg_wide_ends_icase(
    const WCHAR* value, const WCHAR* expected)
{
    SIZE_T value_length;
    SIZE_T expected_length;
    if (!value || !expected) return 0;
    value_length = wcslen(value);
    expected_length = wcslen(expected);
    return value_length >= expected_length &&
        dluma_qg_wide_starts_icase(
            value + value_length - expected_length, expected);
}

static int dluma_qg_module_identity(HMODULE module)
{
    BYTE* base = (BYTE*)module;
    WCHAR module_path[DLUMA_MAX_WPATH];
    DWORD nt;
    DWORD timestamp;
    DWORD image_size;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    if (!base || !dluma_is_readable(base, 0x100U) ||
        *(WORD*)base != 0x5A4DU)
        return 0;
    nt = *(DWORD*)(base + 0x3CU);
    if (nt > 0x10000000U ||
        !dluma_is_readable(base + nt, 0x60U) ||
        *(DWORD*)(base + nt) != 0x00004550U ||
        *(WORD*)(base + nt + 24U) != 0x020BU)
        return 0;
    timestamp = *(DWORD*)(base + nt + 8U);
    image_size = *(DWORD*)(base + nt + 0x50U);
    sha256[0] = 0;
    return timestamp == DLUMA_QG_QUESTEDITOR_TIMESTAMP &&
        image_size == DLUMA_QG_QUESTEDITOR_IMAGE_SIZE &&
        dluma_get_module_path_checked(
            module, module_path,
            DLUMA_ARRAY_COUNT(module_path)) &&
        dluma_sha256_file_hex(
            module_path, 32ULL * 1024ULL * 1024ULL, sha256) &&
        dluma_stricmp(sha256, g_dluma_qg_quest_editor_sha256) == 0;
}

static int dluma_qg_profile_ready(void)
{
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_qg.profile_state, 0, 0);
    HMODULE module;
    if (state == 2) return 1;
    if (state < 0 || !dluma_dual_host_runtime_is_editor()) return 0;
    module = GetModuleHandleW(L"QuestEditor.dll");
    if (!module) return 0;
    if (!dluma_qg_module_identity(module)) {
        dluma_atomic_exchange(&g_dluma_qg.profile_state, -1);
        dluma_log_pair(
            "WARN", "editor_quest_graph_profile",
            "quest_editor_identity_mismatch");
        return 0;
    }
    g_dluma_qg.quest_editor = module;
    dluma_atomic_exchange(&g_dluma_qg.profile_state, 2);
    dluma_log_pair(
        "INFO", "editor_quest_graph_profile",
        "exact_quest_editor_identity_accepted");
    return 1;
}

static int dluma_qg_executable_pointer(const void* address)
{
    MEMORY_BASIC_INFORMATION information;
    DWORD protection;
    if (!address ||
        VirtualQuery(address, &information, sizeof(information)) !=
            sizeof(information) ||
        information.State != MEM_COMMIT ||
        (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    protection = information.Protect & 0xFFU;
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

static int dluma_qg_dispatch_stock_save(DlumaQgDialog* slot)
{
    BYTE* base;
    DlumaQgCWndFromHandleFn from_handle;
    DlumaQgStockSaveFn save;
    void* dialog_object;
    if (!slot || !slot->dialog ||
        GetCurrentThreadId() != g_dluma_qg.owner_thread_id ||
        !dluma_qg_profile_ready())
        return 0;
    base = (BYTE*)g_dluma_qg.quest_editor;
    if (!base ||
        !dluma_is_readable(
            base + DLUMA_QG_CWND_FROM_HANDLE_IAT_RVA,
            sizeof(from_handle)))
        return 0;
    from_handle = *(DlumaQgCWndFromHandleFn*)(
        base + DLUMA_QG_CWND_FROM_HANDLE_IAT_RVA);
    save = (DlumaQgStockSaveFn)(base + DLUMA_QG_STOCK_SAVE_RVA);
    if (!dluma_qg_executable_pointer((const void*)from_handle) ||
        !dluma_qg_executable_pointer((const void*)save))
        return 0;
    dialog_object = from_handle(slot->dialog);
    if (!dialog_object ||
        !dluma_is_readable(dialog_object, sizeof(void*)))
        return 0;
    (void)save(dialog_object);
    return 1;
}

static int dluma_qg_scintilla_class(HWND window)
{
    WCHAR class_name[128];
    class_name[0] = 0;
    return window && IsWindow(window) &&
        GetClassNameW(window, class_name,
                      (int)DLUMA_ARRAY_COUNT(class_name)) > 0 &&
        dluma_qg_wide_contains_icase(class_name, L"Scintilla");
}

static int dluma_qg_dialog_signature(HWND window, HWND* scintilla)
{
    HWND code;
    DWORD process_id = 0;
    if (!window || !IsWindow(window) ||
        GetWindowThreadProcessId(window, &process_id) == 0 ||
        process_id != GetCurrentProcessId())
        return 0;
    code = GetDlgItem(window, DLUMA_QG_SCINTILLA_CONTROL);
    if (!dluma_qg_scintilla_class(code) ||
        !GetDlgItem(window, DLUMA_QG_CONTEXT_CONTROL) ||
        !GetDlgItem(window, DLUMA_QG_OUTPUT_CONTROL) ||
        !GetDlgItem(window, DLUMA_QG_STATUS_CONTROL))
        return 0;
    if (scintilla) *scintilla = code;
    return 1;
}

static DlumaQgDialog* dluma_qg_dialog_find(HWND dialog)
{
    unsigned int index;
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index)
        if (g_dluma_qg.dialogs[index].dialog == dialog)
            return &g_dluma_qg.dialogs[index];
    return 0;
}

static DlumaQgDialog* dluma_qg_scintilla_find(HWND scintilla)
{
    unsigned int index;
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index)
        if (g_dluma_qg.dialogs[index].dialog &&
            g_dluma_qg.dialogs[index].scintilla == scintilla)
            return &g_dluma_qg.dialogs[index];
    return 0;
}

static DlumaQgDialog* dluma_qg_document_find(const char* document_id)
{
    unsigned int index;
    if (!document_id) return 0;
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index)
        if (g_dluma_qg.dialogs[index].dialog &&
            dluma_stricmp(
                g_dluma_qg.dialogs[index].document_id,
                document_id) == 0)
            return &g_dluma_qg.dialogs[index];
    return 0;
}

static void dluma_qg_button_position(DlumaQgDialog* slot)
{
    RECT code_rectangle;
    RECT patch_rectangle;
    POINT dialog_origin;
    HWND patch_diff;
    int available_width;
    int width = 156;
    int height = 26;
    int x;
    int y;
    if (!slot || !slot->dialog || !slot->button ||
        !slot->scintilla ||
        !GetWindowRect(slot->scintilla, &code_rectangle))
        return;
    dialog_origin.x = 0;
    dialog_origin.y = 0;
    if (!ClientToScreen(slot->dialog, &dialog_origin))
        return;
    available_width =
        code_rectangle.right - code_rectangle.left;
    if (available_width < 112)
        return;
    if (width > available_width - 24)
        width = available_width - 24;
    patch_diff = GetDlgItem(
        slot->dialog, DLUMA_QG_PATCH_DIFF_CONTROL);
    if (patch_diff &&
        GetWindowRect(patch_diff, &patch_rectangle)) {
        x = patch_rectangle.left -
            dialog_origin.x - width - 8;
        y = patch_rectangle.top - dialog_origin.y;
    } else {
        x = code_rectangle.right -
            dialog_origin.x - width - 12;
        y = code_rectangle.top -
            dialog_origin.y - height - 2;
    }
    /* A sibling BUTTON inside the Scintilla rectangle reports visible but
     * is overpainted by the stock editor surface. Keep it in the toolbar
     * row immediately left of Patch Diff instead. */
    (void)SetWindowPos(
        slot->button, HWND_TOP, x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static int dluma_qg_scintilla_read(
    HWND scintilla, char** output, DWORD* output_size)
{
    LRESULT length;
    char* text;
    if (!output || !output_size || !dluma_qg_scintilla_class(scintilla))
        return 0;
    *output = 0;
    *output_size = 0;
    length = SendMessageA(scintilla, SCI_GETTEXTLENGTH, 0, 0);
    if (length < 0 || (ULONGLONG)length > DLUMA_QG_MAX_SOURCE_BYTES)
        return 0;
    text = (char*)dluma_alloc((SIZE_T)length + 1U);
    if (!text) return 0;
    text[0] = 0;
    if (SendMessageA(
            scintilla, SCI_GETTEXT, (WPARAM)((SIZE_T)length + 1U),
            (LPARAM)text) < 0) {
        dluma_free(text);
        return 0;
    }
    text[length] = 0;
    *output = text;
    *output_size = (DWORD)length;
    return 1;
}

static void dluma_qg_context_refresh_identity(void)
{
    WCHAR project_root[DLUMA_MAX_WPATH];
    char project_identity[DLUMA_MAX_WPATH * 4U];
    char map_identity[DLUMA_MAX_MAP_NAME];
    ULONGLONG generation;
    project_root[0] = 0;
    project_identity[0] = 0;
    map_identity[0] = 0;
    if (!dluma_editor_launch_resolve_current_project(
            project_root, DLUMA_ARRAY_COUNT(project_root),
            map_identity, DLUMA_ARRAY_COUNT(map_identity)) ||
        !dluma_wide_to_utf8(
            project_root, project_identity,
            DLUMA_ARRAY_COUNT(project_identity))) {
        dluma_str_copy(
            project_identity, DLUMA_ARRAY_COUNT(project_identity),
            "editor");
        map_identity[0] = 0;
    }
    dluma_qg_lock(&g_dluma_qg.context_lock);
    if (dluma_stricmp(
            g_dluma_qg.context_project_identity,
            project_identity) != 0 ||
        dluma_stricmp(
            g_dluma_qg.context_map_identity,
            map_identity) != 0) {
        generation = g_dluma_qg.context.generation;
        dluma_quest_context_initialize(&g_dluma_qg.context);
        g_dluma_qg.context.generation =
            generation == UINT64_MAX
                ? generation : generation + 1ULL;
        dluma_str_copy(
            g_dluma_qg.context_project_identity,
            DLUMA_ARRAY_COUNT(g_dluma_qg.context_project_identity),
            project_identity);
        dluma_str_copy(
            g_dluma_qg.context_map_identity,
            DLUMA_ARRAY_COUNT(g_dluma_qg.context_map_identity),
            map_identity);
    }
    dluma_qg_unlock(&g_dluma_qg.context_lock);
}

static char* dluma_qg_autocomplete_copy(const char* source)
{
    const BYTE* cursor = (const BYTE*)source;
    SIZE_T length = 0;
    char* copy;
    if (!source) return 0;
    while (length < DLUMA_QG_AUTOCOMPLETE_BYTES) {
        MEMORY_BASIC_INFORMATION information;
        const BYTE* region_end;
        SIZE_T available;
        SIZE_T index;
        DWORD protection;
        if (VirtualQuery(
                cursor, &information,
                sizeof(information)) != sizeof(information))
            return 0;
        protection = information.Protect & 0xFFUL;
        if (information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(protection == PAGE_READONLY ||
              protection == PAGE_READWRITE ||
              protection == PAGE_WRITECOPY ||
              protection == PAGE_EXECUTE_READ ||
              protection == PAGE_EXECUTE_READWRITE ||
              protection == PAGE_EXECUTE_WRITECOPY))
            return 0;
        region_end = (const BYTE*)information.BaseAddress +
            information.RegionSize;
        if (cursor >= region_end) return 0;
        available = (SIZE_T)(region_end - cursor);
        if (available > DLUMA_QG_AUTOCOMPLETE_BYTES - length)
            available = DLUMA_QG_AUTOCOMPLETE_BYTES - length;
        for (index = 0; index < available; ++index) {
            if (!cursor[index]) {
                length += index;
                copy = (char*)dluma_alloc(length + 1U);
                if (!copy) return 0;
                memcpy(copy, source, length);
                copy[length] = 0;
                return copy;
            }
        }
        cursor += available;
        length += available;
    }
    return 0;
}

static void dluma_qg_capture_autocomplete(
    DlumaQgDialog* slot, const char* candidates_pointer)
{
    char* source = 0;
    char* candidates = 0;
    DWORD source_size = 0;
    LRESULT caret;
    if (!slot || !slot->scintilla ||
        GetCurrentThreadId() != g_dluma_qg.owner_thread_id)
        return;
    candidates = dluma_qg_autocomplete_copy(candidates_pointer);
    if (!candidates ||
        !dluma_qg_scintilla_read(
            slot->scintilla, &source, &source_size))
        goto done;
    caret = SendMessageA(
        slot->scintilla, SCI_GETCURRENTPOS, 0, 0);
    if (caret < 0 || (ULONGLONG)caret > source_size)
        goto done;
    dluma_qg_context_refresh_identity();
    dluma_qg_lock(&g_dluma_qg.context_lock);
    (void)dluma_quest_context_capture_autocomplete(
        &g_dluma_qg.context, source, source_size,
        (DWORD)caret, candidates,
        slot->autocomplete_separator
            ? slot->autocomplete_separator : ' ');
    dluma_qg_unlock(&g_dluma_qg.context_lock);
done:
    if (source) dluma_free(source);
    if (candidates) dluma_free(candidates);
}

static LRESULT CALLBACK dluma_qg_scintilla_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DlumaQgDialog* slot = dluma_qg_scintilla_find(window);
    WNDPROC predecessor = slot
        ? slot->scintilla_predecessor : 0;
    LRESULT result;
    if (slot && message == SCI_AUTOCSETSEPARATOR &&
        wparam > 0U && wparam <= 0x7FU)
        slot->autocomplete_separator = (char)wparam;
    if (slot && message == SCI_AUTOCSHOW && lparam)
        dluma_qg_capture_autocomplete(
            slot, (const char*)lparam);
    result = predecessor
        ? CallWindowProcW(
            predecessor, window, message, wparam, lparam)
        : 0;
    if (slot && message == WM_NCDESTROY) {
        slot->scintilla = 0;
        slot->scintilla_predecessor = 0;
    }
    return result;
}

static int dluma_qg_file_leaf(const WCHAR* path, const WCHAR** leaf)
{
    const WCHAR* cursor;
    const WCHAR* value;
    if (!path || !path[0] || !leaf) return 0;
    value = path;
    for (cursor = path; *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/') value = cursor + 1;
    *leaf = value;
    return value[0] != 0;
}

static int dluma_qg_same_leaf(
    const WCHAR* path, const WCHAR* expected)
{
    const WCHAR* leaf;
    return expected && expected[0] &&
        dluma_qg_file_leaf(path, &leaf) &&
        dluma_wstricmp(leaf, expected) == 0;
}

static void dluma_qg_search_directory(
    DlumaQgSearch* search, const WCHAR* directory, unsigned int depth)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR child[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!search || search->refused || depth > DLUMA_QG_MAX_PROJECT_DEPTH ||
        !dluma_checked_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*")) {
        if (search) search->refused = 1;
        return;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (++search->entries > DLUMA_QG_MAX_PROJECT_ENTRIES) {
            search->refused = 1;
            break;
        }
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (!dluma_checked_path_join(
                child, DLUMA_ARRAY_COUNT(child),
                directory, data.cFileName)) {
            search->refused = 1;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                dluma_qg_search_directory(search, child, depth + 1U);
            if (search->refused) break;
            continue;
        }
        if (!dluma_qg_wide_ends_icase(data.cFileName, L".pxsl") ||
            (search->preferred_leaf && search->preferred_leaf[0] &&
             !dluma_qg_same_leaf(child, search->preferred_leaf)))
            continue;
        if (dluma_project_regular_file_is_contained(
                search->project_root, child)) {
            char digest[DLUMA_SHA256_HEX_CAPACITY];
            if (search->name_only ||
                (dluma_sha256_file_hex(
                    child, DLUMA_QG_MAX_SOURCE_BYTES, digest) &&
                 dluma_stricmp(digest, search->source_sha256) == 0)) {
                ++search->match_count;
                if (search->match_count == 1U)
                    dluma_wstr_copy(
                        search->match, DLUMA_ARRAY_COUNT(search->match),
                        child);
            }
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static int dluma_qg_search_project_document(
    const WCHAR* project_root,
    const WCHAR* preferred_leaf,
    const char* source_sha256,
    WCHAR* source_path,
    unsigned int source_capacity)
{
    DlumaQgSearch search;
    if (!project_root || !source_sha256 ||
        !source_path || !source_capacity)
        return -1;
    source_path[0] = 0;
    memset(&search, 0, sizeof(search));
    search.project_root = project_root;
    search.preferred_leaf = preferred_leaf;
    search.source_sha256 = source_sha256;
    dluma_qg_search_directory(&search, project_root, 0);
    if (search.refused) return -1;
    if (search.match_count != 1U && preferred_leaf &&
        preferred_leaf[0]) {
        memset(&search, 0, sizeof(search));
        search.project_root = project_root;
        search.source_sha256 = source_sha256;
        dluma_qg_search_directory(&search, project_root, 0);
        if (search.refused) return -1;
    }
    if (search.match_count != 1U)
        return 0;
    dluma_wstr_copy(
        source_path, source_capacity, search.match);
    return source_path[0] ? 1 : -1;
}

static int dluma_qg_search_project_document_by_leaf(
    const WCHAR* project_root,
    const WCHAR* preferred_leaf,
    WCHAR* source_path,
    unsigned int source_capacity)
{
    DlumaQgSearch search;
    if (!project_root || !preferred_leaf || !preferred_leaf[0] ||
        !source_path || !source_capacity)
        return -1;
    source_path[0] = 0;
    memset(&search, 0, sizeof(search));
    search.project_root = project_root;
    search.preferred_leaf = preferred_leaf;
    search.source_sha256 = "";
    search.name_only = 1U;
    dluma_qg_search_directory(&search, project_root, 0);
    if (search.refused) return -1;
    if (search.match_count != 1U) return 0;
    dluma_wstr_copy(source_path, source_capacity, search.match);
    return source_path[0] ? 1 : -1;
}

static int dluma_qg_search_workshop_pass(
    const WCHAR* workshop_root,
    const WCHAR* preferred_leaf,
    const char* source_sha256,
    WCHAR* project_root,
    unsigned int project_capacity,
    WCHAR* source_path,
    unsigned int source_capacity,
    int name_only)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR candidate[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find = INVALID_HANDLE_VALUE;
    unsigned int projects = 0;
    unsigned int entries = 0;
    unsigned int matches = 0;
    int result = -1;
    if (!workshop_root || !source_sha256 ||
        !project_root || !project_capacity ||
        !source_path || !source_capacity ||
        !dluma_checked_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern),
            workshop_root, L"*"))
        return -1;
    project_root[0] = 0;
    source_path[0] = 0;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return -1;
    do {
        DlumaDirectoryIdentity identity;
        DlumaQgSearch search;
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' &&
             data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (++projects > DLUMA_QG_MAX_WORKSHOP_PROJECTS)
            goto done;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (!dluma_checked_path_join(
                candidate, DLUMA_ARRAY_COUNT(candidate),
                workshop_root, data.cFileName) ||
            !dluma_project_candidate_root_is_contained(
                workshop_root, candidate) ||
            !dluma_open_directory_identity(candidate, &identity))
            goto done;
        memset(&search, 0, sizeof(search));
        search.project_root = identity.final_path;
        search.preferred_leaf = preferred_leaf;
        search.source_sha256 = source_sha256;
        search.name_only = name_only ? 1U : 0U;
        dluma_qg_search_directory(
            &search, identity.final_path, 0);
        if (search.refused ||
            search.entries >
                DLUMA_QG_MAX_PROJECT_ENTRIES - entries)
            goto done;
        entries += search.entries;
        if (!search.match_count) continue;
        if (search.match_count != 1U || ++matches != 1U)
            goto complete;
        dluma_wstr_copy(
            project_root, project_capacity,
            identity.final_path);
        dluma_wstr_copy(
            source_path, source_capacity, search.match);
    } while (FindNextFileW(find, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        goto done;
complete:
    result = matches == 1U &&
        project_root[0] && source_path[0] ? 1 : 0;
done:
    FindClose(find);
    return result;
}

static int dluma_qg_search_workshop_document(
    const WCHAR* workshop_root,
    const WCHAR* preferred_leaf,
    const char* source_sha256,
    WCHAR* project_root,
    unsigned int project_capacity,
    WCHAR* source_path,
    unsigned int source_capacity)
{
    int status = dluma_qg_search_workshop_pass(
        workshop_root, preferred_leaf, source_sha256,
        project_root, project_capacity,
        source_path, source_capacity, 0);
    if (status == 1) return 1;
    if (status < 0 || !preferred_leaf ||
        !preferred_leaf[0])
        return status;
    status = dluma_qg_search_workshop_pass(
        workshop_root, 0, source_sha256,
        project_root, project_capacity,
        source_path, source_capacity, 0);
    if (status == 1) return 1;
    return dluma_qg_search_workshop_pass(
        workshop_root, preferred_leaf, source_sha256,
        project_root, project_capacity,
        source_path, source_capacity, 1);
}

static int dluma_qg_title_leaf(HWND dialog, WCHAR* output,
                             unsigned int capacity)
{
    HWND window = dialog;
    unsigned int level;
    if (!output || capacity < 2U) return 0;
    output[0] = 0;
    for (level = 0; window && level < 4U; ++level) {
        WCHAR title[DLUMA_MAX_WPATH];
        SIZE_T length;
        title[0] = 0;
        if (GetWindowTextW(window, title,
                           (int)DLUMA_ARRAY_COUNT(title)) > 0) {
            length = wcslen(title);
            while (length && (title[length - 1U] == L' ' ||
                              title[length - 1U] == L'*'))
                title[--length] = 0;
            if (dluma_qg_wide_ends_icase(title, L".pxsl")) {
                const WCHAR* leaf;
                if (dluma_qg_file_leaf(title, &leaf) &&
                    wcslen(leaf) + 1U <= capacity) {
                    dluma_wstr_copy(output, capacity, leaf);
                    return 1;
                }
            }
        }
        window = GetParent(window);
    }
    return 0;
}

static int dluma_qg_make_relative(
    const WCHAR* root, const WCHAR* path,
    char* output, unsigned int capacity)
{
    SIZE_T root_length;
    const WCHAR* relative;
    if (!root || !path || !output || capacity < 2U) return 0;
    output[0] = 0;
    root_length = wcslen(root);
    if (!root_length || !dluma_qg_wide_starts_icase(path, root) ||
        (path[root_length] != L'\\' && path[root_length] != L'/'))
        return 0;
    relative = path + root_length + 1U;
    return relative[0] &&
        dluma_wide_to_utf8(relative, output, capacity) &&
        !dluma_qg_wide_contains_icase(relative, L"..\\") &&
        !dluma_qg_wide_contains_icase(relative, L"../");
}

static int dluma_qg_resolve_document(
    DlumaQgDialog* slot, const char* text, DWORD text_size)
{
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR workshop_root[DLUMA_MAX_WPATH];
    WCHAR preferred_leaf[DLUMA_MAX_WPATH];
    WCHAR source_path[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    int project_status = 0;
    if (!slot || !text) return 0;
    project_root[0] = 0;
    workshop_root[0] = 0;
    preferred_leaf[0] = 0;
    source_path[0] = 0;
    map_name[0] = 0;
    dluma_sha256_bytes_hex(text, text_size, digest);
    (void)dluma_qg_title_leaf(
        slot->dialog, preferred_leaf,
        DLUMA_ARRAY_COUNT(preferred_leaf));
    if (dluma_editor_launch_resolve_current_project(
            project_root, DLUMA_ARRAY_COUNT(project_root),
            map_name, DLUMA_ARRAY_COUNT(map_name))) {
        project_status = dluma_qg_search_project_document(
            project_root, preferred_leaf, digest,
            source_path, DLUMA_ARRAY_COUNT(source_path));
        if (project_status < 0)
            return 0;
        if (project_status == 0 && preferred_leaf[0]) {
            project_status = dluma_qg_search_project_document_by_leaf(
                project_root, preferred_leaf,
                source_path, DLUMA_ARRAY_COUNT(source_path));
            if (project_status < 0) return 0;
        }
    }
    if (project_status != 1) {
        if (!dluma_editor_launch_resolve_workshop_root(
                workshop_root,
                DLUMA_ARRAY_COUNT(workshop_root)) ||
            dluma_qg_search_workshop_document(
                workshop_root, preferred_leaf, digest,
                project_root, DLUMA_ARRAY_COUNT(project_root),
                source_path, DLUMA_ARRAY_COUNT(source_path)) != 1)
            return 0;
    }
    if (!dluma_project_regular_file_is_contained(
            project_root, source_path) ||
        !dluma_qg_make_relative(
            project_root, source_path,
            slot->relative_path,
            DLUMA_ARRAY_COUNT(slot->relative_path)))
        return 0;
    dluma_wstr_copy(
        slot->project_root, DLUMA_ARRAY_COUNT(slot->project_root),
        project_root);
    dluma_wstr_copy(
        slot->source_path, DLUMA_ARRAY_COUNT(slot->source_path),
        source_path);
    dluma_str_copy(
        slot->source_sha256,
        DLUMA_ARRAY_COUNT(slot->source_sha256), digest);
    dluma_sha256_bytes_hex(
        slot->relative_path, strlen(slot->relative_path),
        slot->document_id);
    if (!slot->revision) slot->revision = 1ULL;
    return 1;
}

static int dluma_qg_json_append(
    char* output, DWORD capacity, DWORD* used, const char* text)
{
    DWORD length;
    if (!output || !used || !text) return 0;
    length = (DWORD)strlen(text);
    if (*used > capacity || length > capacity - *used - 1U)
        return 0;
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = 0;
    return 1;
}

static int dluma_qg_json_escape(
    char* output, DWORD capacity, DWORD* used, const char* text)
{
    const unsigned char* cursor = (const unsigned char*)text;
    if (!output || !used || !text) return 0;
    while (*cursor) {
        char encoded[7];
        const char* value = encoded;
        encoded[0] = (char)*cursor;
        encoded[1] = 0;
        if (*cursor == '"') value = "\\\"";
        else if (*cursor == '\\') value = "\\\\";
        else if (*cursor == '\b') value = "\\b";
        else if (*cursor == '\f') value = "\\f";
        else if (*cursor == '\n') value = "\\n";
        else if (*cursor == '\r') value = "\\r";
        else if (*cursor == '\t') value = "\\t";
        else if (*cursor < 0x20U) {
            static const char hex[] = "0123456789abcdef";
            encoded[0] = '\\'; encoded[1] = 'u';
            encoded[2] = '0'; encoded[3] = '0';
            encoded[4] = hex[*cursor >> 4];
            encoded[5] = hex[*cursor & 15U];
            encoded[6] = 0;
        }
        if (!dluma_qg_json_append(output, capacity, used, value))
            return 0;
        ++cursor;
    }
    return 1;
}

static int dluma_qg_base64_append(
    char* output, DWORD capacity, DWORD* used,
    const BYTE* data, DWORD size)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    DWORD index;
    for (index = 0; index < size; index += 3U) {
        DWORD remaining = size - index;
        DWORD value = ((DWORD)data[index] << 16);
        char encoded[5];
        if (remaining > 1U) value |= ((DWORD)data[index + 1U] << 8);
        if (remaining > 2U) value |= data[index + 2U];
        encoded[0] = table[(value >> 18) & 63U];
        encoded[1] = table[(value >> 12) & 63U];
        encoded[2] = remaining > 1U ? table[(value >> 6) & 63U] : '=';
        encoded[3] = remaining > 2U ? table[value & 63U] : '=';
        encoded[4] = 0;
        if (!dluma_qg_json_append(output, capacity, used, encoded))
            return 0;
    }
    return 1;
}

static int dluma_qg_scintilla_encoding(
    HWND scintilla, char* output, unsigned int capacity)
{
    LRESULT code_page;
    if (!output || capacity < 13U ||
        !dluma_qg_scintilla_class(scintilla))
        return 0;
    code_page = SendMessageA(scintilla, SCI_GETCODEPAGE, 0, 0);
    if (code_page == 0)
        code_page = (LRESULT)GetACP();
    if (code_page == 65001) {
        dluma_str_copy(output, capacity, "utf-8");
        return 1;
    }
    if (code_page == 1252) {
        dluma_str_copy(output, capacity, "windows-1252");
        return 1;
    }
    output[0] = 0;
    return 0;
}

static int dluma_qg_build_open_frame(
    DlumaQgDialog* slot, const char* source, DWORD source_size,
    char** output, DWORD* output_size)
{
    char project_utf8[DLUMA_MAX_WPATH * 4U];
    char revision[32];
    char request_id[33];
    char encoding_name[32];
    char* json;
    DWORD capacity;
    DWORD used = 0;
    if (!slot || !source || !output || !output_size ||
        !dluma_wide_to_utf8(
            slot->project_root, project_utf8,
            DLUMA_ARRAY_COUNT(project_utf8)) ||
        !dluma_u64_to_text(
            slot->revision, revision, DLUMA_ARRAY_COUNT(revision)))
        return 0;
    memcpy(request_id, g_dluma_qg.token, 32U);
    request_id[32] = 0;
    if (!dluma_qg_scintilla_encoding(
            slot->scintilla, encoding_name,
            DLUMA_ARRAY_COUNT(encoding_name)))
        return 0;
    capacity = source_size + ((source_size + 2U) / 3U) +
        (DWORD)strlen(project_utf8) * 6U +
        (DWORD)strlen(slot->relative_path) * 6U + 2048U;
    if (capacity > DLUMA_QG_MAX_FRAME_BYTES) return 0;
    json = (char*)dluma_alloc((SIZE_T)capacity + 1U);
    if (!json) return 0;
    json[0] = 0;
    if (!dluma_qg_json_append(json, capacity, &used,
            "{\"schemaVersion\":2,\"type\":\"openDocument\",\"requestId\":\"") ||
        !dluma_qg_json_escape(json, capacity, &used, request_id) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"sessionToken\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, g_dluma_qg.token) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"payload\":{\"documentId\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, slot->document_id) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"projectRoot\":\"") ||
        !dluma_qg_json_escape(json, capacity, &used, project_utf8) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"relativePath\":\"") ||
        !dluma_qg_json_escape(
            json, capacity, &used, slot->relative_path) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"revision\":") ||
        !dluma_qg_json_append(json, capacity, &used, revision) ||
        !dluma_qg_json_append(json, capacity, &used,
            ",\"encodingName\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, encoding_name) ||
        !dluma_qg_json_append(json, capacity, &used, "\",\"bytes\":\"") ||
        !dluma_qg_base64_append(
            json, capacity, &used,
            (const BYTE*)source, source_size) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"sha256\":\"") ||
        !dluma_qg_json_append(
            json, capacity, &used, slot->source_sha256) ||
        !dluma_qg_json_append(
            json, capacity, &used,
            SendMessageA(slot->scintilla, SCI_GETMODIFY, 0, 0)
                ? "\",\"wasModified\":true}}"
                : "\",\"wasModified\":false}}")) {
        dluma_free(json);
        return 0;
    }
    *output = json;
    *output_size = used;
    return 1;
}

static int dluma_qg_build_source_changed_frame(
    DlumaQgDialog* slot, const char* source, DWORD source_size,
    char** output, DWORD* output_size)
{
    char revision[32];
    char request_id[33];
    char encoding_name[32];
    char* json;
    DWORD capacity;
    DWORD used = 0;
    if (!slot || !source || !output || !output_size ||
        !dluma_u64_to_text(
            slot->revision, revision, DLUMA_ARRAY_COUNT(revision)))
        return 0;
    memcpy(request_id, g_dluma_qg.token, 32U);
    request_id[32] = 0;
    if (!dluma_qg_scintilla_encoding(
            slot->scintilla, encoding_name,
            DLUMA_ARRAY_COUNT(encoding_name)))
        return 0;
    capacity = source_size + ((source_size + 2U) / 3U) + 2048U;
    if (capacity > DLUMA_QG_MAX_FRAME_BYTES) return 0;
    json = (char*)dluma_alloc((SIZE_T)capacity + 1U);
    if (!json) return 0;
    json[0] = 0;
    if (!dluma_qg_json_append(json, capacity, &used,
            "{\"schemaVersion\":2,\"type\":\"sourceChanged\",\"requestId\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, request_id) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"sessionToken\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, g_dluma_qg.token) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"payload\":{\"documentId\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, slot->document_id) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"revision\":") ||
        !dluma_qg_json_append(json, capacity, &used, revision) ||
        !dluma_qg_json_append(json, capacity, &used,
            ",\"encodingName\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, encoding_name) ||
        !dluma_qg_json_append(json, capacity, &used, "\",\"bytes\":\"") ||
        !dluma_qg_base64_append(
            json, capacity, &used,
            (const BYTE*)source, source_size) ||
        !dluma_qg_json_append(json, capacity, &used,
            "\",\"sha256\":\"") ||
        !dluma_qg_json_append(
            json, capacity, &used, slot->source_sha256) ||
        !dluma_qg_json_append(json, capacity, &used, "\"}}")) {
        dluma_free(json);
        return 0;
    }
    *output = json;
    *output_size = used;
    return 1;
}

static int dluma_qg_queue_frame(char* frame, DWORD frame_size)
{
    if (!frame || !frame_size || frame_size > DLUMA_QG_MAX_FRAME_BYTES)
        return 0;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg.pending_frame) dluma_free(g_dluma_qg.pending_frame);
    g_dluma_qg.pending_frame = frame;
    g_dluma_qg.pending_frame_size = frame_size;
    ++g_dluma_qg.pending_generation;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    return 1;
}

static void dluma_qg_publish_source_changed(DlumaQgDialog* slot)
{
    char* source = 0;
    DWORD source_size = 0;
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    char* frame = 0;
    DWORD frame_size = 0;
    if (!slot || !slot->document_id[0] ||
        !dluma_qg_scintilla_read(
            slot->scintilla, &source, &source_size))
        return;
    dluma_sha256_bytes_hex(source, source_size, digest);
    if (dluma_stricmp(digest, slot->source_sha256) == 0) {
        dluma_free(source);
        return;
    }
    ++slot->revision;
    dluma_str_copy(
        slot->source_sha256,
        DLUMA_ARRAY_COUNT(slot->source_sha256), digest);
    if (dluma_qg_build_source_changed_frame(
            slot, source, source_size, &frame, &frame_size) &&
        !dluma_qg_queue_frame(frame, frame_size))
        dluma_free(frame);
    dluma_free(source);
}

static int dluma_qg_random_token(void)
{
    HMODULE bcrypt;
    DlumaQgBCryptGenRandomFn random;
    BYTE bytes[32];
    static const char hex[] = "0123456789abcdef";
    unsigned int index;
    bcrypt = LoadLibraryW(L"bcrypt.dll");
    if (!bcrypt) return 0;
    random = (DlumaQgBCryptGenRandomFn)(void*)
        GetProcAddress(bcrypt, "BCryptGenRandom");
    if (!random || random(0, bytes, sizeof(bytes),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        FreeLibrary(bcrypt);
        return 0;
    }
    FreeLibrary(bcrypt);
    for (index = 0; index < sizeof(bytes); ++index) {
        g_dluma_qg.token[index * 2U] = hex[bytes[index] >> 4];
        g_dluma_qg.token[index * 2U + 1U] = hex[bytes[index] & 15U];
        bytes[index] = 0;
    }
    g_dluma_qg.token[64] = 0;
    return 1;
}

static HANDLE dluma_qg_create_pipe(void)
{
    HMODULE advapi = 0;
    DlumaQgOpenProcessTokenFn open_process_token;
    DlumaQgGetTokenInformationFn get_token_information;
    DlumaQgInitializeAclFn initialize_acl;
    DlumaQgAddAccessAllowedAceFn add_access_allowed_ace;
    DlumaQgInitializeSecurityDescriptorFn initialize_descriptor;
    DlumaQgSetSecurityDescriptorDaclFn set_dacl;
    DlumaQgGetLengthSidFn get_sid_length;
    HANDLE token = 0;
    BYTE token_buffer[1024];
    BYTE acl_buffer[1024];
    DlumaQgSecurityDescriptor descriptor;
    DlumaQgSecurityAttributes attributes;
    DlumaQgTokenUser* token_user;
    DlumaQgAcl* acl = (DlumaQgAcl*)acl_buffer;
    DWORD token_size = 0;
    DWORD acl_size;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    char pid_text[32];
    WCHAR pid_wide[32];
    WCHAR token_prefix[17];
    unsigned int index;
    if (!dluma_u64_to_text(
            GetCurrentProcessId(), pid_text,
            DLUMA_ARRAY_COUNT(pid_text)) ||
        !dluma_utf8_to_wide(
            pid_text, pid_wide, DLUMA_ARRAY_COUNT(pid_wide)))
        return INVALID_HANDLE_VALUE;
    for (index = 0; index < 16U; ++index)
        token_prefix[index] = (WCHAR)g_dluma_qg.token[index];
    token_prefix[16] = 0;
    dluma_wstr_copy(
        g_dluma_qg.pipe_short_name,
        DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_short_name),
        L"DLUMA.QuestGraph.v1.");
    dluma_wstr_append(
        g_dluma_qg.pipe_short_name,
        DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_short_name), pid_wide);
    dluma_wstr_append(
        g_dluma_qg.pipe_short_name,
        DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_short_name), L".");
    dluma_wstr_append(
        g_dluma_qg.pipe_short_name,
        DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_short_name), token_prefix);
    dluma_wstr_copy(
        g_dluma_qg.pipe_name, DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_name),
        L"\\\\.\\pipe\\");
    dluma_wstr_append(
        g_dluma_qg.pipe_name, DLUMA_ARRAY_COUNT(g_dluma_qg.pipe_name),
        g_dluma_qg.pipe_short_name);
    advapi = LoadLibraryW(L"advapi32.dll");
    if (!advapi) return INVALID_HANDLE_VALUE;
    open_process_token = (DlumaQgOpenProcessTokenFn)(void*)
        GetProcAddress(advapi, "OpenProcessToken");
    get_token_information = (DlumaQgGetTokenInformationFn)(void*)
        GetProcAddress(advapi, "GetTokenInformation");
    initialize_acl = (DlumaQgInitializeAclFn)(void*)
        GetProcAddress(advapi, "InitializeAcl");
    add_access_allowed_ace = (DlumaQgAddAccessAllowedAceFn)(void*)
        GetProcAddress(advapi, "AddAccessAllowedAce");
    initialize_descriptor = (DlumaQgInitializeSecurityDescriptorFn)(void*)
        GetProcAddress(advapi, "InitializeSecurityDescriptor");
    set_dacl = (DlumaQgSetSecurityDescriptorDaclFn)(void*)
        GetProcAddress(advapi, "SetSecurityDescriptorDacl");
    get_sid_length = (DlumaQgGetLengthSidFn)(void*)
        GetProcAddress(advapi, "GetLengthSid");
    if (!open_process_token || !get_token_information ||
        !initialize_acl || !add_access_allowed_ace ||
        !initialize_descriptor || !set_dacl || !get_sid_length)
        goto cleanup;
    if (!open_process_token(GetCurrentProcess(), TOKEN_QUERY, &token) ||
        !get_token_information(
            token, TokenUser, token_buffer,
            sizeof(token_buffer), &token_size))
        goto cleanup;
    token_user = (DlumaQgTokenUser*)token_buffer;
    acl_size = sizeof(DlumaQgAcl) +
        sizeof(DlumaQgAccessAllowedAce) - sizeof(DWORD) +
        get_sid_length(token_user->user.sid);
    if (acl_size > sizeof(acl_buffer) ||
        !initialize_acl(acl, acl_size, ACL_REVISION) ||
        !add_access_allowed_ace(
            acl, ACL_REVISION, GENERIC_ALL,
            token_user->user.sid) ||
        !initialize_descriptor(
            &descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !set_dacl(&descriptor, TRUE, acl, FALSE))
        goto cleanup;
    attributes.length = sizeof(attributes);
    attributes.descriptor = &descriptor;
    attributes.inherit = FALSE;
    pipe = CreateNamedPipeW(
        g_dluma_qg.pipe_name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1U, 65536U, 65536U, 0, &attributes);
cleanup:
    if (token) CloseHandle(token);
    FreeLibrary(advapi);
    return pipe;
}

static int dluma_qg_json_string(
    const char* json, const char* field,
    char* output, unsigned int capacity)
{
    char needle[96];
    const char* start;
    unsigned int needle_length;
    unsigned int used = 0;
    if (!json || !field || !output || capacity < 2U ||
        strlen(field) + 5U >= sizeof(needle))
        return 0;
    needle[0] = '"';
    dluma_str_copy(needle + 1U,
                 (unsigned int)sizeof(needle) - 1U, field);
    needle_length = (unsigned int)strlen(needle);
    needle[needle_length++] = '"';
    needle[needle_length++] = ':';
    needle[needle_length++] = '"';
    needle[needle_length] = 0;
    start = strstr(json, needle);
    if (!start) return 0;
    start += needle_length;
    while (*start && *start != '"') {
        if (*start == '\\' || (unsigned char)*start < 0x20U ||
            used + 1U >= capacity)
            return 0;
        output[used++] = *start++;
    }
    if (*start != '"') return 0;
    output[used] = 0;
    return used != 0;
}

static int dluma_qg_json_u64(
    const char* json, const char* field, ULONGLONG* output)
{
    char needle[96];
    const char* start;
    unsigned int needle_length;
    ULONGLONG value = 0;
    unsigned int digits = 0;
    if (!json || !field || !output ||
        strlen(field) + 4U >= sizeof(needle))
        return 0;
    needle[0] = '"';
    dluma_str_copy(needle + 1U,
                 (unsigned int)sizeof(needle) - 1U, field);
    needle_length = (unsigned int)strlen(needle);
    needle[needle_length++] = '"';
    needle[needle_length++] = ':';
    needle[needle_length] = 0;
    start = strstr(json, needle);
    if (!start) return 0;
    start += needle_length;
    while (*start >= '0' && *start <= '9') {
        ULONGLONG digit = (ULONGLONG)(*start - '0');
        if (value > 1844674407370955161ULL ||
            (value == 1844674407370955161ULL && digit > 5ULL))
            return 0;
        value = value * 10ULL + digit;
        ++start;
        ++digits;
    }
    if (!digits) return 0;
    *output = value;
    return 1;
}

static void dluma_qg_edits_free(
    DlumaQgTextEdit* edits, DWORD edit_count)
{
    DWORD index;
    if (!edits) return;
    for (index = 0; index < edit_count; ++index)
        if (edits[index].replacement)
            dluma_free(edits[index].replacement);
    dluma_free(edits);
}

static void dluma_qg_ui_payload_free(DlumaQgUiRequest* request)
{
    if (!request) return;
    dluma_qg_edits_free(request->edits, request->edit_count);
    if (request->restore_source) dluma_free(request->restore_source);
    request->edits = 0;
    request->edit_count = 0;
    request->restore_source = 0;
    request->restore_source_size = 0;
}

static int dluma_qg_parse_edits(
    char* json, DlumaQgTextEdit** output, DWORD* output_count)
{
    char* cursor;
    DlumaQgTextEdit* edits;
    DWORD count = 0;
    if (!json || !output || !output_count) return 0;
    *output = 0;
    *output_count = 0;
    cursor = strstr(json, "\"edits\":");
    if (!cursor) return 0;
    cursor += 8U;
    while (*cursor == ' ' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n')
        ++cursor;
    if (*cursor++ != '[') return 0;
    edits = (DlumaQgTextEdit*)dluma_alloc(
        (SIZE_T)DLUMA_QG_MAX_EDITS * sizeof(*edits));
    if (!edits) return 0;
    memset(edits, 0, (SIZE_T)DLUMA_QG_MAX_EDITS * sizeof(*edits));
    for (;;) {
        char* end;
        char saved;
        char* encoded = 0;
        ULONGLONG byte_offset = 0;
        ULONGLONG removed_length = 0;
        uint32_t decoded_size = 0;
        SIZE_T encoded_capacity;
        while (*cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n' ||
               *cursor == ',')
            ++cursor;
        if (*cursor == ']') break;
        if (*cursor != '{' || count >= DLUMA_QG_MAX_EDITS) goto fail;
        end = strchr(cursor, '}');
        if (!end) goto fail;
        saved = *end;
        *end = 0;
        encoded_capacity = (SIZE_T)(end - cursor) + 1U;
        encoded = (char*)dluma_alloc(encoded_capacity);
        if (!encoded ||
            !dluma_qg_json_u64(
                cursor, "byteOffset", &byte_offset) ||
            !dluma_qg_json_u64(
                cursor, "removedLength", &removed_length) ||
            byte_offset > 0xFFFFFFFFULL ||
            removed_length > 0xFFFFFFFFULL ||
            !dluma_quest_graph_json_string(
                cursor, "replacementBytes", encoded,
                (unsigned int)encoded_capacity, 1) ||
            !dluma_quest_graph_base64_decode_in_place(
                encoded, DLUMA_QG_MAX_SOURCE_BYTES,
                &decoded_size)) {
            *end = saved;
            if (encoded) dluma_free(encoded);
            goto fail;
        }
        *end = saved;
        edits[count].byte_offset = (DWORD)byte_offset;
        edits[count].removed_length = (DWORD)removed_length;
        edits[count].replacement_size = decoded_size;
        edits[count].replacement = encoded;
        ++count;
        cursor = end + 1U;
    }
    if (!count) goto fail;
    *output = edits;
    *output_count = count;
    return 1;
fail:
    dluma_qg_edits_free(edits, count);
    return 0;
}

static int dluma_qg_parse_base64_field(
    const char* json, const char* field,
    char** output, DWORD* output_size)
{
    SIZE_T capacity;
    char* encoded;
    uint32_t decoded_size = 0;
    if (!json || !field || !output || !output_size) return 0;
    *output = 0;
    *output_size = 0;
    capacity = strlen(json) + 1U;
    if (capacity < 2U || capacity > DLUMA_QG_MAX_FRAME_BYTES)
        return 0;
    encoded = (char*)dluma_alloc(capacity);
    if (!encoded) return 0;
    if (!dluma_quest_graph_json_string(
            json, field, encoded, (DWORD)capacity, 1) ||
        !dluma_quest_graph_base64_decode_in_place(
            encoded, DLUMA_QG_MAX_SOURCE_BYTES, &decoded_size)) {
        dluma_free(encoded);
        return 0;
    }
    *output = encoded;
    *output_size = decoded_size;
    return 1;
}

static int dluma_qg_json_boolean(
    const char* json, const char* field, int* output)
{
    char needle[96];
    const char* start;
    unsigned int length;
    if (!json || !field || !output ||
        strlen(field) + 4U >= sizeof(needle))
        return 0;
    needle[0] = '"';
    dluma_str_copy(
        needle + 1U, (unsigned int)sizeof(needle) - 1U, field);
    length = (unsigned int)strlen(needle);
    needle[length++] = '"';
    needle[length++] = ':';
    needle[length] = 0;
    start = strstr(json, needle);
    if (!start) return 0;
    start += length;
    if (strncmp(start, "true", 4U) == 0) {
        *output = 1;
        return 1;
    }
    if (strncmp(start, "false", 5U) == 0) {
        *output = 0;
        return 1;
    }
    return 0;
}

static int dluma_qg_token_equal(const char* value)
{
    unsigned int index;
    unsigned int difference = 0;
    if (!value || strlen(value) != 64U) return 0;
    for (index = 0; index < 64U; ++index)
        difference |= (unsigned int)(
            (unsigned char)value[index] ^
            (unsigned char)g_dluma_qg.token[index]);
    return difference == 0;
}

static int dluma_qg_response_begin(
    char* output, DWORD capacity, DWORD* used,
    const char* request_id, const char* type)
{
    return dluma_qg_json_append(output, capacity, used,
            "{\"schemaVersion\":2,\"type\":\"") &&
        dluma_qg_json_append(output, capacity, used, type) &&
        dluma_qg_json_append(output, capacity, used,
            "\",\"requestId\":\"") &&
        dluma_qg_json_escape(output, capacity, used, request_id) &&
        dluma_qg_json_append(output, capacity, used,
            "\",\"sessionToken\":\"") &&
        dluma_qg_json_append(output, capacity, used, g_dluma_qg.token) &&
        dluma_qg_json_append(output, capacity, used, "\",\"payload\":");
}

static DWORD dluma_qg_error_response(
    char* output, DWORD capacity, const char* request_id,
    const char* code, int retryable)
{
    DWORD used = 0;
    if (!dluma_qg_response_begin(
            output, capacity, &used, request_id, "error") ||
        !dluma_qg_json_append(output, capacity, &used, "{\"code\":\"") ||
        !dluma_qg_json_escape(output, capacity, &used, code) ||
        !dluma_qg_json_append(output, capacity, &used, "\",\"message\":\"") ||
        !dluma_qg_json_escape(output, capacity, &used, code) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            retryable ? "\",\"retryable\":true}}" :
                        "\",\"retryable\":false}}"))
        return 0;
    return used;
}

static int dluma_qg_installed_catalog_root(
    WCHAR* output, unsigned int capacity)
{
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR editor_root[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity identity;
    if (!output || capacity < 2U || !g_dluma_qg.quest_editor ||
        !dluma_get_module_path_checked(
            g_dluma_qg.quest_editor, module_path,
            DLUMA_ARRAY_COUNT(module_path)))
        return 0;
    dluma_wstr_copy(
        editor_root, DLUMA_ARRAY_COUNT(editor_root), module_path);
    dluma_path_parent(editor_root);
    dluma_path_parent(editor_root);
    if (!editor_root[0] ||
        !dluma_checked_path_join(
            output, capacity, editor_root,
            L"DW\\Data\\Quests\\_underlay") ||
        !dluma_open_directory_identity(output, &identity))
        return 0;
    dluma_wstr_copy(output, capacity, identity.final_path);
    return output[0] != 0;
}

static DWORD dluma_qg_hello_response(
    char* output, DWORD capacity, const char* request_id)
{
    DWORD used = 0;
    char process_id[32];
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR installed_root[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    char project_utf8[DLUMA_MAX_WPATH * 4U];
    char installed_utf8[DLUMA_MAX_WPATH * 4U];
    char context_generation[32];
    int have_project;
    int have_installed;
    int have_context_generation;
    project_root[0] = installed_root[0] = 0;
    map_name[0] = project_utf8[0] = installed_utf8[0] = 0;
    have_project = dluma_editor_launch_resolve_current_project(
        project_root, DLUMA_ARRAY_COUNT(project_root),
        map_name, DLUMA_ARRAY_COUNT(map_name)) &&
        dluma_wide_to_utf8(
            project_root, project_utf8,
            DLUMA_ARRAY_COUNT(project_utf8));
    have_installed = dluma_qg_installed_catalog_root(
        installed_root, DLUMA_ARRAY_COUNT(installed_root)) &&
        dluma_wide_to_utf8(
            installed_root, installed_utf8,
            DLUMA_ARRAY_COUNT(installed_utf8));
    dluma_qg_context_refresh_identity();
    dluma_qg_lock(&g_dluma_qg.context_lock);
    have_context_generation = dluma_u64_to_text(
        g_dluma_qg.context.generation, context_generation,
        DLUMA_ARRAY_COUNT(context_generation));
    dluma_qg_unlock(&g_dluma_qg.context_lock);
    if (!have_context_generation ||
        !dluma_u64_to_text(
            GetCurrentProcessId(), process_id,
            DLUMA_ARRAY_COUNT(process_id)) ||
        !dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"protocolVersion\":2,\"editorProcessId\":") ||
        !dluma_qg_json_append(output, capacity, &used, process_id) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            ",\"hostBuildId\":\"QuestEditor-56554E8EC993B158\","
            "\"projectRoot\":") ||
        (have_project
            ? (!dluma_qg_json_append(output, capacity, &used, "\"") ||
               !dluma_qg_json_escape(
                   output, capacity, &used, project_utf8) ||
               !dluma_qg_json_append(output, capacity, &used, "\""))
            : !dluma_qg_json_append(
                  output, capacity, &used, "null")) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            ",\"installedCatalogRoots\":[") ||
        (have_installed
            ? (!dluma_qg_json_append(output, capacity, &used, "\"") ||
               !dluma_qg_json_escape(
                   output, capacity, &used, installed_utf8) ||
               !dluma_qg_json_append(output, capacity, &used, "\""))
            : 0) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "],\"contextGeneration\":") ||
        !dluma_qg_json_append(
            output, capacity, &used, context_generation) ||
        !dluma_qg_json_append(output, capacity, &used, "}}"))
        return 0;
    return used;
}

static DWORD dluma_qg_context_response(
    char* output, DWORD capacity, const char* request_id)
{
    DWORD used = 0;
    char generation[32];
    unsigned int index;
    unsigned int serialized = 0;
    int result = 0;
    dluma_qg_context_refresh_identity();
    dluma_qg_lock(&g_dluma_qg.context_lock);
    if (!dluma_u64_to_text(
            g_dluma_qg.context.generation, generation,
            DLUMA_ARRAY_COUNT(generation)) ||
        !dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"context\":{\"generation\":") ||
        !dluma_qg_json_append(output, capacity, &used, generation) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            ",\"projectIdentity\":\"") ||
        !dluma_qg_json_escape(
            output, capacity, &used,
            g_dluma_qg.context_project_identity[0]
                ? g_dluma_qg.context_project_identity : "editor") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "\",\"mapIdentity\":\"") ||
        !dluma_qg_json_escape(
            output, capacity, &used,
            g_dluma_qg.context_map_identity) ||
        !dluma_qg_json_append(
            output, capacity, &used, "\",\"objects\":["))
        goto done;
    for (index = 0; index < g_dluma_qg.context.object_count; ++index) {
        const DlumaQuestContextObjectCore* object =
            &g_dluma_qg.context.objects[index];
        DWORD checkpoint = used;
        unsigned int role_index;
        int exposed = 0;
        for (role_index = 0;
             role_index < object->role_count; ++role_index)
            if (dluma_stricmp(
                    object->roles[role_index], "class") == 0 ||
                dluma_stricmp(
                    object->roles[role_index], "scenario") == 0 ||
                dluma_stricmp(
                    object->roles[role_index], "object") == 0)
                exposed = 1;
        if (!exposed) continue;
        if (capacity - used < 256U) break;
        if ((serialized && !dluma_qg_json_append(
                output, capacity, &used, ",")) ||
            !dluma_qg_json_append(
                output, capacity, &used, "{\"name\":\"") ||
            !dluma_qg_json_escape(
                output, capacity, &used, object->name) ||
            !dluma_qg_json_append(
                output, capacity, &used, "\",\"className\":\"") ||
            !dluma_qg_json_escape(
                output, capacity, &used,
                object->class_name[0]
                    ? object->class_name : "Unknown") ||
            !dluma_qg_json_append(
                output, capacity, &used, "\",\"roles\":[")) {
            used = checkpoint;
            output[used] = 0;
            break;
        }
        for (role_index = 0;
             role_index < object->role_count; ++role_index) {
            if ((role_index && !dluma_qg_json_append(
                    output, capacity, &used, ",")) ||
                !dluma_qg_json_append(
                    output, capacity, &used, "\"") ||
                !dluma_qg_json_escape(
                    output, capacity, &used,
                    object->roles[role_index]) ||
                !dluma_qg_json_append(
                    output, capacity, &used, "\""))
                break;
        }
        if (role_index != object->role_count ||
            !dluma_qg_json_append(
                output, capacity, &used, "]}")) {
            used = checkpoint;
            output[used] = 0;
            break;
        }
        ++serialized;
    }
    if (!dluma_qg_json_append(
            output, capacity, &used,
            "],\"texts\":[],\"speakers\":[],"
            "\"dialogueGroups\":[],\"questReferences\":[]}}}"))
        goto done;
    result = 1;
done:
    dluma_qg_unlock(&g_dluma_qg.context_lock);
    return result ? used : 0;
}

static DWORD dluma_qg_bridge_result_response(
    char* output, DWORD capacity, const char* request_id,
    const char* message)
{
    DWORD used = 0;
    if (!dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"success\":true,\"message\":\"") ||
        !dluma_qg_json_escape(output, capacity, &used, message) ||
        !dluma_qg_json_append(output, capacity, &used, "\"}}"))
        return 0;
    return used;
}

static DWORD dluma_qg_save_response(
    char* output, DWORD capacity, const char* request_id,
    ULONGLONG revision, const char* sha256, const char* disk_path)
{
    DWORD used = 0;
    char revision_text[32];
    if (!disk_path || !disk_path[0] || !dluma_u64_to_text(
            revision, revision_text,
            DLUMA_ARRAY_COUNT(revision_text)) ||
        !dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"saveAttempted\":true,\"saveSucceeded\":true,"
            "\"cancelled\":false,\"revision\":") ||
        !dluma_qg_json_append(output, capacity, &used, revision_text) ||
        !dluma_qg_json_append(output, capacity, &used, ",\"sha256\":\"") ||
        !dluma_qg_json_append(output, capacity, &used, sha256) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "\",\"diskPath\":\"") ||
        !dluma_qg_json_escape(output, capacity, &used, disk_path) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "\",\"message\":\"PXSL synchronized with Developer Tools.\"}}"))
        return 0;
    return used;
}

static DWORD dluma_qg_apply_response(
    char* output, DWORD capacity, const char* request_id,
    ULONGLONG revision, const char* sha256)
{
    DWORD used = 0;
    char revision_text[32];
    if (!dluma_u64_to_text(
            revision, revision_text,
            DLUMA_ARRAY_COUNT(revision_text)) ||
        !dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"applied\":true,\"revision\":") ||
        !dluma_qg_json_append(output, capacity, &used, revision_text) ||
        !dluma_qg_json_append(output, capacity, &used, ",\"sha256\":\"") ||
        !dluma_qg_json_append(output, capacity, &used, sha256) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "\",\"message\":\"PXSL source admitted by Developer Tools.\"}}"))
        return 0;
    return used;
}

static DWORD dluma_qg_restore_response(
    char* output, DWORD capacity, const char* request_id,
    ULONGLONG revision, const char* sha256)
{
    DWORD used = 0;
    char revision_text[32];
    if (!dluma_u64_to_text(
            revision, revision_text,
            DLUMA_ARRAY_COUNT(revision_text)) ||
        !dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"restored\":true,\"revision\":") ||
        !dluma_qg_json_append(output, capacity, &used, revision_text) ||
        !dluma_qg_json_append(output, capacity, &used, ",\"sha256\":\"") ||
        !dluma_qg_json_append(output, capacity, &used, sha256) ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "\",\"message\":\"PXSL baseline restored in Developer Tools.\"}}"))
        return 0;
    return used;
}

static DWORD dluma_qg_compile_response(
    char* output, DWORD capacity, const char* request_id)
{
    DWORD used = 0;
    if (!dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"receipt\":{\"saveAttempted\":true,"
            "\"saveSucceeded\":true,\"stockCommandDispatched\":true,"
            "\"stockCommandCompleted\":false,"
            "\"diagnosticsAvailable\":false,\"diagnostics\":[],"
            "\"outputSha256\":null,\"outputLastWriteUtc\":null,"
            "\"customSidecarPublished\":false,"
            "\"message\":\"Stock QuestEditor compile command was dispatched; "
            "completion and diagnostics remain unobserved in Developer Tools.\"}}}"))
        return 0;
    return used;
}

static int dluma_qg_write_exact(
    HANDLE pipe, const void* data, DWORD size)
{
    const BYTE* cursor = (const BYTE*)data;
    DWORD total = 0;
    ULONGLONG started = GetTickCount64();
    while (total < size &&
           GetTickCount64() - started < DLUMA_QG_IO_TIMEOUT_MS) {
        DWORD written = 0;
        DWORD chunk = size - total;
        if (chunk > 65536U) chunk = 65536U;
        if (WriteFile(pipe, cursor + total, chunk, &written, 0)) {
            total += written;
            if (!written) Sleep(DLUMA_QG_PIPE_POLL_MS);
            continue;
        }
        if (GetLastError() != ERROR_NO_DATA) return 0;
        Sleep(DLUMA_QG_PIPE_POLL_MS);
    }
    return total == size;
}

static int dluma_qg_write_frame(
    HANDLE pipe, const char* json, DWORD size)
{
    BYTE prefix[4];
    if (!json || !size || size > DLUMA_QG_MAX_FRAME_BYTES) return 0;
    prefix[0] = (BYTE)(size & 0xFFU);
    prefix[1] = (BYTE)((size >> 8) & 0xFFU);
    prefix[2] = (BYTE)((size >> 16) & 0xFFU);
    prefix[3] = (BYTE)((size >> 24) & 0xFFU);
    return dluma_qg_write_exact(pipe, prefix, sizeof(prefix)) &&
        dluma_qg_write_exact(pipe, json, size);
}

static int dluma_qg_dispatch_ui(
    DlumaQgUiRequestKind kind, const char* document_id,
    ULONGLONG expected_revision, const char* expected_sha256,
    const char* expected_result_sha256,
    const char* source_sha256, DlumaQgTextEdit* edits,
    DWORD edit_count, char* restore_source,
    DWORD restore_source_size, int restore_was_modified,
    int has_byte_offset, DWORD byte_offset,
    ULONGLONG* revision, char* response_sha256,
    unsigned int response_sha256_capacity,
    char* response_path, unsigned int response_path_capacity,
    char* error, unsigned int error_capacity)
{
    ULONG_PTR result = 0;
    int delivered;
    DlumaQgUiRequest* request = &g_dluma_qg.ui_request;
    if (!g_dluma_qg.main_window || !IsWindow(g_dluma_qg.main_window) ||
        !document_id ||
        (kind != DLUMA_QG_UI_ACTIVATE &&
         kind != DLUMA_QG_UI_RESTORE_SOURCE &&
         !expected_sha256) ||
        (kind == DLUMA_QG_UI_APPLY_SOURCE &&
         !expected_result_sha256) ||
        ((kind == DLUMA_QG_UI_SAVE || kind == DLUMA_QG_UI_COMPILE) &&
         !source_sha256) ||
        (kind == DLUMA_QG_UI_RESTORE_SOURCE &&
         (!source_sha256 || !restore_source ||
          restore_source_size > DLUMA_QG_MAX_SOURCE_BYTES)) ||
        !revision || !response_sha256 ||
        response_sha256_capacity < DLUMA_SHA256_HEX_CAPACITY ||
        (kind == DLUMA_QG_UI_SAVE &&
         (!response_path || response_path_capacity < 2U)) ||
        !error || error_capacity < 2U)
        goto invalid;
    if (dluma_atomic_compare_exchange(&request->state, -1, 2) == 2) {
        dluma_qg_ui_payload_free(request);
        memset((void*)request, 0, sizeof(*request));
    }
    if (dluma_atomic_compare_exchange(&request->state, -1, 0) != 0) {
        dluma_str_copy(error, error_capacity, "ui_dispatch_busy");
        goto invalid;
    }
    memset((void*)request, 0, sizeof(*request));
    request->state = -1;
    request->kind = kind;
    request->expected_revision = expected_revision;
    dluma_str_copy(request->document_id,
                 DLUMA_ARRAY_COUNT(request->document_id), document_id);
    if (expected_sha256)
        dluma_str_copy(request->expected_sha256,
                     DLUMA_ARRAY_COUNT(request->expected_sha256),
                      expected_sha256);
    if (expected_result_sha256)
        dluma_str_copy(request->expected_result_sha256,
                     DLUMA_ARRAY_COUNT(request->expected_result_sha256),
                     expected_result_sha256);
    if (source_sha256)
        dluma_str_copy(request->source_sha256,
                     DLUMA_ARRAY_COUNT(request->source_sha256),
                     source_sha256);
    request->edits = edits;
    request->edit_count = edit_count;
    request->restore_source = restore_source;
    request->restore_source_size = restore_source_size;
    request->restore_was_modified = restore_was_modified ? 1U : 0U;
    request->has_byte_offset = has_byte_offset ? 1U : 0U;
    request->byte_offset = byte_offset;
    dluma_atomic_exchange(&request->state, 1);
    delivered = SendMessageTimeoutW(
            g_dluma_qg.main_window, DLUMA_QG_MAIN_DISPATCH, 0, 0,
            DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG |
                DLUMA_SMTO_ERRORONEXIT,
            30000U, &result) != 0;
    if (dluma_atomic_compare_exchange(&request->state, 0, 0) != 2) {
        dluma_str_copy(error, error_capacity, "ui_dispatch_failed");
        /* The UI may still consume this request after a timeout. Keep its
         * edit buffers owned by the request to prevent a use-after-free. */
        return 0;
    }
    *revision = request->response_revision;
    dluma_str_copy(
        response_sha256, response_sha256_capacity,
        request->source_sha256);
    if (response_path && response_path_capacity)
        dluma_str_copy(
            response_path, response_path_capacity,
            request->disk_path);
    dluma_str_copy(error, error_capacity, request->error);
    dluma_qg_ui_payload_free(request);
    dluma_atomic_exchange(&request->state, 0);
    return delivered && result != 0;
invalid:
    dluma_qg_edits_free(edits, edit_count);
    if (restore_source) dluma_free(restore_source);
    return 0;
}

static int dluma_qg_handle_json(
    HANDLE pipe, char* json, DWORD size)
{
    char* payload = 0;
    DWORD payload_size = 0;
    char type[48];
    char request_id[96];
    char token[80];
    char document_id[DLUMA_SHA256_HEX_CAPACITY];
    char expected_sha[DLUMA_SHA256_HEX_CAPACITY];
    char expected_result_sha[DLUMA_SHA256_HEX_CAPACITY];
    char source_sha[DLUMA_SHA256_HEX_CAPACITY];
    char executable_sha[DLUMA_SHA256_HEX_CAPACITY];
    char response_sha[DLUMA_SHA256_HEX_CAPACITY];
    char disk_path[DLUMA_MAX_WPATH * 4U];
    char error[96];
    char response[DLUMA_MAX_WPATH * 8U + 4096U];
    DlumaQgTextEdit* edits = 0;
    DWORD edit_count = 0;
    char* restore_source = 0;
    DWORD restore_source_size = 0;
    int restore_was_modified = 0;
    ULONGLONG protocol_version = 0;
    ULONGLONG client_process_id = 0;
    ULONGLONG editor_process_id = 0;
    ULONGLONG expected_revision = 0;
    ULONGLONG revision = 0;
    ULONGLONG byte_offset = 0;
    DWORD response_size;
    int ok = 0;
    int retryable = 0;
    int has_byte_offset = 0;
    if (!json || !size || size > DLUMA_QG_MAX_FRAME_BYTES) return 0;
    json[size] = 0;
    type[0] = request_id[0] = token[0] = disk_path[0] = 0;
    if (!dluma_qg_parse_envelope(
            json, size,
            type, (unsigned int)sizeof(type),
            request_id, (unsigned int)sizeof(request_id),
            token, (unsigned int)sizeof(token),
            &payload, &payload_size) ||
        !dluma_qg_validate_payload(
            type, payload, payload_size))
        return 0;
    json = payload;
    if (strcmp(type, "hello") == 0) {
        executable_sha[0] = 0;
        if (!dluma_qg_json_u64(
                json, "protocolVersion", &protocol_version) ||
            !dluma_qg_json_u64(
                json, "clientProcessId", &client_process_id) ||
            !dluma_qg_json_u64(
                json, "editorProcessId", &editor_process_id) ||
            !dluma_qg_json_string(
                json, "executableSha256", executable_sha,
                sizeof(executable_sha)) ||
            protocol_version != 2ULL ||
            client_process_id !=
                (ULONGLONG)g_dluma_qg.companion_process_id ||
            editor_process_id !=
                (ULONGLONG)GetCurrentProcessId() ||
            !dluma_qg_hash_valid(executable_sha) ||
            !dluma_qg_hash_valid(g_dluma_qg.companion_sha256) ||
            dluma_stricmp(
                executable_sha,
                g_dluma_qg.companion_sha256) != 0) {
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "hello_identity_mismatch", 0);
            return response_size &&
                dluma_qg_write_frame(pipe, response, response_size);
        }
        dluma_atomic_exchange(&g_dluma_qg.authenticated, 1);
        response_size = dluma_qg_hello_response(
            response, sizeof(response), request_id);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (!dluma_atomic_compare_exchange(
            &g_dluma_qg.authenticated, 0, 0))
        return 0;
    if (strcmp(type, "getContext") == 0) {
        response_size = dluma_qg_context_response(
            response, sizeof(response), request_id);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (strcmp(type, "closeDocument") == 0) {
        document_id[0] = 0;
        if (!dluma_qg_json_string(
                json, "documentId", document_id,
                sizeof(document_id)) ||
            !dluma_qg_hash_valid(document_id)) {
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "invalid_request", 0);
        } else {
            response_size = dluma_qg_bridge_result_response(
                response, sizeof(response), request_id,
                "Visual document closed.");
        }
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (strcmp(type, "activateDocument") == 0) {
        document_id[0] = error[0] = response_sha[0] = 0;
        has_byte_offset = dluma_qg_json_u64(
            json, "byteOffset", &byte_offset);
        if (!dluma_qg_json_string(
                json, "documentId", document_id,
                sizeof(document_id)) ||
            !dluma_qg_hash_valid(document_id) ||
            (has_byte_offset && byte_offset > 0xFFFFFFFFULL)) {
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "invalid_request", 0);
        } else {
            ok = dluma_qg_dispatch_ui(
                DLUMA_QG_UI_ACTIVATE, document_id,
                0, 0, 0, 0, 0, 0,
                0, 0, 0,
                has_byte_offset,
                (DWORD)byte_offset, &revision,
                response_sha, sizeof(response_sha),
                0, 0,
                error, sizeof(error));
            response_size = ok
                ? dluma_qg_bridge_result_response(
                    response, sizeof(response), request_id,
                    "Developer Tools source activated.")
                : dluma_qg_error_response(
                    response, sizeof(response), request_id,
                    error[0] ? error : "request_refused", 1);
        }
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (strcmp(type, "restoreDocument") == 0) {
        document_id[0] = source_sha[0] = response_sha[0] = error[0] = 0;
        if (!dluma_qg_json_string(
                json, "documentId", document_id,
                sizeof(document_id)) ||
            !dluma_qg_hash_valid(document_id) ||
            !dluma_qg_json_string(
                json, "baselineSha256", source_sha,
                sizeof(source_sha)) ||
            !dluma_qg_hash_valid(source_sha) ||
            !dluma_qg_json_boolean(
                json, "wasModified", &restore_was_modified) ||
            !dluma_qg_parse_base64_field(
                json, "baselineBytes", &restore_source,
                &restore_source_size)) {
            if (restore_source) dluma_free(restore_source);
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "restore_payload_invalid", 0);
            return response_size &&
                dluma_qg_write_frame(pipe, response, response_size);
        }
        dluma_sha256_bytes_hex(
            restore_source, restore_source_size, expected_sha);
        if (dluma_stricmp(expected_sha, source_sha) != 0) {
            dluma_free(restore_source);
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "restore_hash_invalid", 0);
            return response_size &&
                dluma_qg_write_frame(pipe, response, response_size);
        }
        ok = dluma_qg_dispatch_ui(
            DLUMA_QG_UI_RESTORE_SOURCE, document_id,
            0, 0, 0, source_sha, 0, 0,
            restore_source, restore_source_size,
            restore_was_modified, 0, 0,
            &revision, response_sha, sizeof(response_sha),
            0, 0, error, sizeof(error));
        restore_source = 0;
        response_size = ok
            ? dluma_qg_restore_response(
                response, sizeof(response), request_id,
                revision, response_sha)
            : dluma_qg_error_response(
                response, sizeof(response), request_id,
                error[0] ? error : "restore_failed", 1);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    document_id[0] = expected_sha[0] = expected_result_sha[0] = source_sha[0] =
        response_sha[0] = error[0] = 0;
    if ((strcmp(type, "saveDocument") != 0 &&
         strcmp(type, "compileDocument") != 0 &&
         strcmp(type, "applyEdits") != 0) ||
        !dluma_qg_json_string(
            json, "documentId", document_id,
            sizeof(document_id)) ||
        !dluma_qg_hash_valid(document_id) ||
        !dluma_qg_json_u64(
            json, "expectedRevision", &expected_revision) ||
        !dluma_qg_json_string(
            json, "expectedSha256", expected_sha,
            sizeof(expected_sha)) ||
        !dluma_qg_hash_valid(expected_sha)) {
        response_size = dluma_qg_error_response(
            response, sizeof(response), request_id,
            "invalid_request", 0);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (strcmp(type, "applyEdits") == 0) {
        if (!dluma_qg_json_string(
                json, "expectedResultSha256", expected_result_sha,
                sizeof(expected_result_sha)) ||
            !dluma_qg_hash_valid(expected_result_sha) ||
            !dluma_qg_parse_edits(json, &edits, &edit_count)) {
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "edit_payload_invalid", 0);
            return response_size &&
                dluma_qg_write_frame(pipe, response, response_size);
        }
        ok = dluma_qg_dispatch_ui(
            DLUMA_QG_UI_APPLY_SOURCE, document_id,
            expected_revision, expected_sha,
            expected_result_sha, 0, edits, edit_count,
            0, 0, 0, 0, 0,
            &revision, response_sha, sizeof(response_sha),
            0, 0,
            error, sizeof(error));
        edits = 0;
        edit_count = 0;
        response_size = ok
            ? dluma_qg_apply_response(
                response, sizeof(response), request_id,
                revision, response_sha)
            : dluma_qg_error_response(
                response, sizeof(response), request_id,
                error[0] ? error : "request_refused", 1);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    if (!dluma_qg_json_string(
            json, "sourceSha256", source_sha,
            sizeof(source_sha)) ||
        !dluma_qg_hash_valid(source_sha)) {
        response_size = dluma_qg_error_response(
            response, sizeof(response), request_id,
            "invalid_request", 0);
        return response_size &&
            dluma_qg_write_frame(pipe, response, response_size);
    }
    ok = dluma_qg_dispatch_ui(
        strcmp(type, "compileDocument") == 0
            ? DLUMA_QG_UI_COMPILE : DLUMA_QG_UI_SAVE,
        document_id, expected_revision, expected_sha,
        0, source_sha, 0, 0,
        0, 0, 0, 0, 0,
        &revision, response_sha, sizeof(response_sha),
        strcmp(type, "saveDocument") == 0 ? disk_path : 0,
        strcmp(type, "saveDocument") == 0
            ? (unsigned int)sizeof(disk_path) : 0U,
        error, sizeof(error));
    if (ok) {
        response_size = dluma_stricmp(type, "compileDocument") == 0
            ? dluma_qg_compile_response(
                response, sizeof(response), request_id)
            : dluma_qg_save_response(
                response, sizeof(response), request_id,
                revision, response_sha, disk_path);
    } else {
        retryable = strstr(error, "conflict") != 0 ||
            strstr(error, "busy") != 0 ||
            strstr(error, "failed") != 0;
        response_size = dluma_qg_error_response(
            response, sizeof(response), request_id,
            error[0] ? error : "request_refused", retryable);
    }
    return response_size &&
        dluma_qg_write_frame(pipe, response, response_size);
}

static int dluma_qg_read_available(
    HANDLE pipe, BYTE* output, DWORD capacity, DWORD* used)
{
    DWORD read = 0;
    if (*used >= capacity) return 1;
    if (ReadFile(pipe, output + *used, capacity - *used, &read, 0)) {
        *used += read;
        return 1;
    }
    if (GetLastError() == ERROR_NO_DATA) return 1;
    return 0;
}

static int dluma_qg_send_pending(HANDLE pipe)
{
    char* frame = 0;
    DWORD frame_size = 0;
    ULONGLONG generation = 0;
    int result = 1;
    if (!dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0))
        return 1;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg.pending_frame &&
        g_dluma_qg.pending_generation != g_dluma_qg.sent_generation) {
        frame = g_dluma_qg.pending_frame;
        frame_size = g_dluma_qg.pending_frame_size;
        generation = g_dluma_qg.pending_generation;
        g_dluma_qg.pending_frame = 0;
        g_dluma_qg.pending_frame_size = 0;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (frame) {
        result = dluma_qg_write_frame(pipe, frame, frame_size);
        dluma_free(frame);
        if (result) g_dluma_qg.sent_generation = generation;
    }
    return result;
}

static int dluma_qg_connected_loop(HANDLE pipe)
{
    BYTE prefix[4];
    DWORD prefix_used = 0;
    char* frame = 0;
    DWORD frame_size = 0;
    DWORD frame_used = 0;
    dluma_atomic_exchange(&g_dluma_qg.connected, 1);
    dluma_atomic_exchange(&g_dluma_qg.authenticated, 0);
    for (;;) {
        if (WaitForSingleObject(g_dluma_qg.stop_event, 0) == WAIT_OBJECT_0)
            break;
        if (!dluma_qg_send_pending(pipe)) break;
        if (prefix_used < sizeof(prefix)) {
            if (!dluma_qg_read_available(
                    pipe, prefix, sizeof(prefix), &prefix_used))
                break;
            if (prefix_used < sizeof(prefix)) {
                Sleep(DLUMA_QG_PIPE_POLL_MS);
                continue;
            }
            frame_size = (DWORD)prefix[0] |
                ((DWORD)prefix[1] << 8) |
                ((DWORD)prefix[2] << 16) |
                ((DWORD)prefix[3] << 24);
            if (!frame_size || frame_size > DLUMA_QG_MAX_FRAME_BYTES)
                break;
            frame = (char*)dluma_alloc((SIZE_T)frame_size + 1U);
            if (!frame) break;
            frame_used = 0;
        }
        if (!dluma_qg_read_available(
                pipe, (BYTE*)frame, frame_size, &frame_used))
            break;
        if (frame_used == frame_size) {
            if (!dluma_qg_handle_json(pipe, frame, frame_size)) break;
            dluma_free(frame);
            frame = 0;
            frame_size = frame_used = prefix_used = 0;
            continue;
        }
        Sleep(DLUMA_QG_PIPE_POLL_MS);
    }
    if (frame) dluma_free(frame);
    dluma_atomic_exchange(&g_dluma_qg.authenticated, 0);
    dluma_atomic_exchange(&g_dluma_qg.connected, 0);
    return 1;
}

static int dluma_qg_pipe_peer_is_companion(HANDLE pipe)
{
    ULONGLONG started = GetTickCount64();
    ULONG peer_process_id = 0;
    if (!GetNamedPipeClientProcessId(pipe, &peer_process_id) ||
        !peer_process_id)
        return 0;
    /* CreateProcess may return to the launch thread just after the child has
     * connected. Give that thread a short bounded window to publish the
     * kernel-issued PID and process handle, then bind this pipe instance to
     * both. A bearer-token claim alone never authenticates a peer. */
    while (GetTickCount64() - started < 2000ULL) {
        DWORD expected_process_id = g_dluma_qg.companion_process_id;
        HANDLE expected_process = g_dluma_qg.companion_process;
        if (expected_process_id && expected_process) {
            return peer_process_id == expected_process_id &&
                WaitForSingleObject(expected_process, 0) == WAIT_TIMEOUT;
        }
        if (WaitForSingleObject(g_dluma_qg.stop_event, 0) == WAIT_OBJECT_0)
            return 0;
        Sleep(1U);
    }
    return 0;
}

static DWORD WINAPI dluma_qg_worker(LPVOID context)
{
    HANDLE pipe = (HANDLE)context;
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    for (;;) {
        BOOL connected;
        DWORD error;
        if (WaitForSingleObject(g_dluma_qg.stop_event, 0) == WAIT_OBJECT_0)
            break;
        connected = ConnectNamedPipe(pipe, 0);
        if (!connected) {
            error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) connected = TRUE;
            else if (error == ERROR_PIPE_LISTENING || error == ERROR_NO_DATA) {
                if (error == ERROR_NO_DATA) DisconnectNamedPipe(pipe);
                Sleep(DLUMA_QG_PIPE_POLL_MS);
                continue;
            } else if (error == ERROR_BROKEN_PIPE) {
                DisconnectNamedPipe(pipe);
                continue;
            } else {
                dluma_log_hex(
                    "WARN", "Quest graph pipe connection failed", error);
                break;
            }
        }
        if (connected) {
            if (dluma_qg_pipe_peer_is_companion(pipe))
                (void)dluma_qg_connected_loop(pipe);
            else
                dluma_log_pair(
                    "WARN", "editor_quest_graph_pipe",
                    "client_process_identity_refused");
            DisconnectNamedPipe(pipe);
        }
    }
    dluma_atomic_exchange(&g_dluma_qg.worker_stopped, 1);
    dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
    return 0;
}

static int dluma_qg_module_directory(
    WCHAR* output, unsigned int capacity)
{
    WCHAR module_path[DLUMA_MAX_WPATH];
    SIZE_T length;
    if (!output || capacity < 2U ||
        !dluma_get_module_path_checked(
            g_dluma.self_module, module_path,
            DLUMA_ARRAY_COUNT(module_path)))
        return 0;
    length = wcslen(module_path);
    while (length && module_path[length - 1U] != L'\\' &&
           module_path[length - 1U] != L'/')
        --length;
    if (!length) return 0;
    module_path[length - 1U] = 0;
    dluma_wstr_copy(output, capacity, module_path);
    return output[0] != 0;
}

static int dluma_qg_runtime_acquire(void)
{
    WCHAR module_root[DLUMA_MAX_WPATH];
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    if (g_dluma_qg.runtime_lease.file_count)
        return 1;
    if (g_dluma_qg.runtime_lease.struct_size !=
        (unsigned int)sizeof(g_dluma_qg.runtime_lease)) {
        memset(&g_dluma_qg.runtime_lease, 0, sizeof(g_dluma_qg.runtime_lease));
        g_dluma_qg.runtime_lease.struct_size =
            (unsigned int)sizeof(g_dluma_qg.runtime_lease);
    }
    if (!dluma_qg_module_directory(
            module_root, DLUMA_ARRAY_COUNT(module_root)) ||
        !dluma_checked_path_join(
            runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
            module_root, L"dluma_runtime"))
        return 0;
    return dluma_runtime_tree_acquire(
        runtime_root,
        DLUMA_QG_PRIVATE_RUNTIME_VERSION,
        DLUMA_API_CATALOG_SHA256,
        &g_dluma_qg.runtime_lease);
}

static WCHAR dluma_qg_environment_fold(WCHAR value)
{
    if (value >= L'A' && value <= L'Z')
        return (WCHAR)(value + (L'a' - L'A'));
    return value;
}

static int dluma_qg_environment_has_key(
    const WCHAR* entry, const WCHAR* key)
{
    unsigned int index = 0;
    if (!entry || !key || entry[0] == L'=') return 0;
    while (key[index] && entry[index] &&
           dluma_qg_environment_fold(key[index]) ==
               dluma_qg_environment_fold(entry[index]))
        ++index;
    return key[index] == 0 && entry[index] == L'=';
}

static int dluma_qg_environment_is_private_runtime_key(
    const WCHAR* entry)
{
    return dluma_qg_environment_has_key(entry, L"DOTNET_ROOT") ||
        dluma_qg_environment_has_key(entry, L"DOTNET_ROOT_X64") ||
        dluma_qg_environment_has_key(
            entry, L"DOTNET_MULTILEVEL_LOOKUP");
}

static WCHAR* dluma_qg_child_environment(const WCHAR* dotnet_root)
{
    LPWSTR inherited = 0;
    const WCHAR* cursor;
    const WCHAR** entries = 0;
    WCHAR root_entry[DLUMA_MAX_WPATH + 32U];
    WCHAR root_x64_entry[DLUMA_MAX_WPATH + 32U];
    WCHAR multilevel_entry[] = L"DOTNET_MULTILEVEL_LOOKUP=0";
    WCHAR* output = 0;
    SIZE_T inherited_count = 0;
    SIZE_T entry_count = 0;
    SIZE_T character_count = 1U;
    SIZE_T index;
    if (!dotnet_root || !dotnet_root[0]) return 0;
    root_entry[0] = 0;
    root_x64_entry[0] = 0;
    dluma_wstr_copy(
        root_entry, DLUMA_ARRAY_COUNT(root_entry), L"DOTNET_ROOT=");
    dluma_wstr_append(
        root_entry, DLUMA_ARRAY_COUNT(root_entry), dotnet_root);
    dluma_wstr_copy(
        root_x64_entry, DLUMA_ARRAY_COUNT(root_x64_entry),
        L"DOTNET_ROOT_X64=");
    dluma_wstr_append(
        root_x64_entry, DLUMA_ARRAY_COUNT(root_x64_entry), dotnet_root);
    if (wcslen(root_entry) !=
            wcslen(dotnet_root) + wcslen(L"DOTNET_ROOT=") ||
        wcslen(root_x64_entry) !=
            wcslen(dotnet_root) + wcslen(L"DOTNET_ROOT_X64="))
        return 0;
    inherited = GetEnvironmentStringsW();
    if (!inherited) return 0;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1U)
        ++inherited_count;
    entries = (const WCHAR**)dluma_alloc(
        (inherited_count + 3U) * sizeof(*entries));
    if (!entries) goto done;
    for (cursor = inherited; *cursor; cursor += wcslen(cursor) + 1U)
        if (!dluma_qg_environment_is_private_runtime_key(cursor))
            entries[entry_count++] = cursor;
    entries[entry_count++] = root_entry;
    entries[entry_count++] = root_x64_entry;
    entries[entry_count++] = multilevel_entry;
    for (index = 1U; index < entry_count; ++index) {
        const WCHAR* value = entries[index];
        SIZE_T position = index;
        while (position > 0U &&
               dluma_wstricmp(entries[position - 1U], value) > 0) {
            entries[position] = entries[position - 1U];
            --position;
        }
        entries[position] = value;
    }
    for (index = 0; index < entry_count; ++index) {
        SIZE_T length = wcslen(entries[index]) + 1U;
        if (character_count > 32767U - length)
            goto done;
        character_count += length;
    }
    output = (WCHAR*)dluma_alloc(character_count * sizeof(WCHAR));
    if (output) {
        WCHAR* destination = output;
        for (index = 0; index < entry_count; ++index) {
            SIZE_T length = wcslen(entries[index]) + 1U;
            memcpy(destination, entries[index], length * sizeof(WCHAR));
            destination += length;
        }
        *destination = 0;
    }
done:
    if (entries) dluma_free((void*)entries);
    (void)FreeEnvironmentStringsW(inherited);
    return output;
}

static int dluma_qg_append_quoted(
    WCHAR* output, unsigned int capacity, const WCHAR* value)
{
    const WCHAR* cursor;
    SIZE_T used = wcslen(output);
    if (!value || used + 3U > capacity) return 0;
    output[used++] = L'"';
    for (cursor = value; *cursor; ++cursor) {
        if (*cursor == L'"' || *cursor < 0x20U ||
            used + 2U >= capacity)
            return 0;
        output[used++] = *cursor;
    }
    output[used++] = L'"';
    output[used] = 0;
    return 1;
}

static int dluma_qg_launch_companion(void)
{
    WCHAR module_root[DLUMA_MAX_WPATH];
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    WCHAR dotnet_root[DLUMA_MAX_WPATH];
    WCHAR companion_dir[DLUMA_MAX_WPATH];
    WCHAR executable[DLUMA_MAX_WPATH];
    WCHAR token_wide[80];
    WCHAR pid_wide[32];
    char pid[32];
    WCHAR command[DLUMA_MAX_WPATH * 3U];
    DlumaQgStartupInfo startup;
    DlumaQgProcessInformation process;
    WCHAR* environment = 0;
    ULONGLONG now = GetTickCount64();
    if (dluma_atomic_compare_exchange(&g_dluma_qg.connected, 0, 0))
        return 1;
    if (g_dluma_qg.companion_process) {
        DWORD child_state = WaitForSingleObject(
            g_dluma_qg.companion_process, 0);
        if (child_state == WAIT_TIMEOUT)
            return 1;
        CloseHandle(g_dluma_qg.companion_process);
        g_dluma_qg.companion_process = 0;
        g_dluma_qg.companion_process_id = 0;
        g_dluma_qg.companion_sha256[0] = 0;
    }
    if (g_dluma_qg.last_launch_tick &&
        now - g_dluma_qg.last_launch_tick < 5000ULL)
        return 1;
    if (!dluma_qg_module_directory(
            module_root, DLUMA_ARRAY_COUNT(module_root)) ||
        !dluma_checked_path_join(
            runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
            module_root, L"dluma_runtime") ||
        !dluma_checked_path_join(
            dotnet_root, DLUMA_ARRAY_COUNT(dotnet_root),
            runtime_root, L"dotnet") ||
        !dluma_checked_path_join(
            companion_dir, DLUMA_ARRAY_COUNT(companion_dir),
            runtime_root, L"quest-graph") ||
        !dluma_checked_path_join(
            executable, DLUMA_ARRAY_COUNT(executable),
            companion_dir, L"Dluma.QuestGraph.exe") ||
        !dluma_project_regular_file_is_contained(
            module_root, executable) ||
        !dluma_sha256_file_hex(
            executable, DLUMA_QG_MAX_COMPANION_BYTES,
            g_dluma_qg.companion_sha256) ||
        !dluma_utf8_to_wide(
            g_dluma_qg.token, token_wide,
            DLUMA_ARRAY_COUNT(token_wide)) ||
        !dluma_u64_to_text(
            GetCurrentProcessId(), pid, DLUMA_ARRAY_COUNT(pid)) ||
        !dluma_utf8_to_wide(pid, pid_wide, DLUMA_ARRAY_COUNT(pid_wide))) {
        dluma_log_pair(
            "WARN", "editor_quest_graph_companion",
            "packaged_executable_unavailable");
        return 0;
    }
    command[0] = 0;
    if (!dluma_qg_append_quoted(
            command, DLUMA_ARRAY_COUNT(command), executable))
        return 0;
    dluma_wstr_append(command, DLUMA_ARRAY_COUNT(command), L" --pipe ");
    if (!dluma_qg_append_quoted(
            command, DLUMA_ARRAY_COUNT(command),
            g_dluma_qg.pipe_short_name))
        return 0;
    dluma_wstr_append(
        command, DLUMA_ARRAY_COUNT(command), L" --session-token ");
    if (!dluma_qg_append_quoted(
            command, DLUMA_ARRAY_COUNT(command), token_wide))
        return 0;
    dluma_wstr_append(command, DLUMA_ARRAY_COUNT(command), L" --editor-pid ");
    if (!dluma_qg_append_quoted(
            command, DLUMA_ARRAY_COUNT(command), pid_wide))
        return 0;
    environment = dluma_qg_child_environment(dotnet_root);
    if (!environment) {
        dluma_log_pair(
            "WARN", "editor_quest_graph_companion",
            "private_runtime_environment_unavailable");
        return 0;
    }
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.size = sizeof(startup);
    if (!CreateProcessW(
            executable, command, 0, 0, FALSE,
            CREATE_UNICODE_ENVIRONMENT, environment, companion_dir,
            &startup, &process)) {
        dluma_free(environment);
        dluma_log_hex(
            "WARN", "Quest graph companion launch failed",
            GetLastError());
        return 0;
    }
    dluma_free(environment);
    CloseHandle(process.thread);
    g_dluma_qg.companion_process = process.process;
    g_dluma_qg.companion_process_id = process.process_id;
    g_dluma_qg.last_launch_tick = now;
    dluma_log_pair(
        "INFO", "editor_quest_graph_companion",
        "launched_authenticated_session");
    return 1;
}

static int dluma_qg_session_start(void)
{
    DWORD thread_id = 0;
    if (g_dluma_qg.worker_thread && g_dluma_qg.pipe &&
        g_dluma_qg.pipe != INVALID_HANDLE_VALUE)
        return dluma_qg_launch_companion();
    if (!dluma_qg_random_token()) return 0;
    g_dluma_qg.pipe = dluma_qg_create_pipe();
    if (!g_dluma_qg.pipe || g_dluma_qg.pipe == INVALID_HANDLE_VALUE)
        return 0;
    g_dluma_qg.stop_event = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_qg.stop_event) {
        CloseHandle(g_dluma_qg.pipe);
        g_dluma_qg.pipe = INVALID_HANDLE_VALUE;
        return 0;
    }
    dluma_atomic_exchange(&g_dluma_qg.worker_stopped, 0);
    g_dluma_qg.worker_thread = CreateThread(
        0, 0, dluma_qg_worker, g_dluma_qg.pipe, 0, &thread_id);
    if (!g_dluma_qg.worker_thread) {
        CloseHandle(g_dluma_qg.stop_event);
        CloseHandle(g_dluma_qg.pipe);
        g_dluma_qg.stop_event = 0;
        g_dluma_qg.pipe = INVALID_HANDLE_VALUE;
        return 0;
    }
    return dluma_qg_launch_companion();
}

static void dluma_qg_open_dialog(DlumaQgDialog* slot)
{
    char* source = 0;
    DWORD source_size = 0;
    char* frame = 0;
    DWORD frame_size = 0;
    if (!slot || !slot->scintilla) return;
    if (!dluma_qg_scintilla_read(
            slot->scintilla, &source, &source_size) ||
        !dluma_qg_resolve_document(slot, source, source_size)) {
        if (source) dluma_free(source);
        MessageBoxW(
            slot->dialog,
            L"The active PXSL file could not be resolved uniquely inside the current project. Save it, close duplicate identical copies, and try again.",
            L"Visual Quest Editor", 0x30U);
        return;
    }
    if (!dluma_qg_session_start() ||
        !dluma_qg_build_open_frame(
            slot, source, source_size, &frame, &frame_size) ||
        !dluma_qg_queue_frame(frame, frame_size)) {
        if (frame) dluma_free(frame);
        dluma_free(source);
        MessageBoxW(
            slot->dialog,
            L"The packaged visual quest editor or its authenticated local bridge is unavailable. Stock PXSL editing remains unchanged.",
            L"Visual Quest Editor", 0x10U);
        return;
    }
    dluma_free(source);
}

static void dluma_qg_ui_request_execute(void)
{
    DlumaQgUiRequest* request = &g_dluma_qg.ui_request;
    DlumaQgDialog* slot;
    char* current = 0;
    char* updated = 0;
    DWORD current_size = 0;
    DWORD updated_size = 0;
    char current_sha[DLUMA_SHA256_HEX_CAPACITY];
    char disk_sha[DLUMA_SHA256_HEX_CAPACITY];
    DWORD index;
    int accepted = 0;
    if (dluma_atomic_compare_exchange(&request->state, 0, 0) != 1)
        return;
    request->error[0] = 0;
    slot = dluma_qg_document_find(request->document_id);
    if (!slot || !slot->dialog || !slot->scintilla) {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "document_not_active");
        goto done;
    }
    if (request->kind == DLUMA_QG_UI_ACTIVATE) {
        LRESULT source_length = SendMessageA(
            slot->scintilla, SCI_GETTEXTLENGTH, 0, 0);
        if (source_length < 0 ||
            (request->has_byte_offset &&
             (ULONGLONG)request->byte_offset >
                (ULONGLONG)source_length)) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "source_offset_invalid");
            goto done;
        }
        (void)SetFocus(slot->scintilla);
        if (request->has_byte_offset)
            (void)SendMessageA(
                slot->scintilla, SCI_GOTOPOS,
                (WPARAM)request->byte_offset, 0);
        request->response_revision = slot->revision;
        dluma_str_copy(
            request->source_sha256,
            DLUMA_ARRAY_COUNT(request->source_sha256),
            slot->source_sha256);
        accepted = 1;
        goto done;
    }
    if (request->kind == DLUMA_QG_UI_RESTORE_SOURCE) {
        LRESULT source_length = SendMessageA(
            slot->scintilla, SCI_GETTEXTLENGTH, 0, 0);
        LRESULT replaced;
        if (source_length < 0 || !request->restore_source ||
            request->restore_source_size > DLUMA_QG_MAX_SOURCE_BYTES) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "restore_payload_invalid");
            goto done;
        }
        dluma_sha256_bytes_hex(
            request->restore_source,
            request->restore_source_size, current_sha);
        if (dluma_stricmp(current_sha, request->source_sha256) != 0) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "restore_hash_invalid");
            goto done;
        }
        slot->suppress_source_notification = 1U;
        (void)SendMessageA(
            slot->scintilla, SCI_BEGINUNDOACTION, 0, 0);
        (void)SendMessageA(
            slot->scintilla, SCI_SETTARGETSTART, 0, 0);
        (void)SendMessageA(
            slot->scintilla, SCI_SETTARGETEND,
            (WPARAM)source_length, 0);
        replaced = SendMessageA(
            slot->scintilla, SCI_REPLACETARGET,
            (WPARAM)request->restore_source_size,
            (LPARAM)request->restore_source);
        (void)SendMessageA(
            slot->scintilla, SCI_ENDUNDOACTION, 0, 0);
        if (replaced != (LRESULT)request->restore_source_size ||
            !dluma_qg_scintilla_read(
                slot->scintilla, &updated, &updated_size) ||
            updated_size != request->restore_source_size) {
            (void)SendMessageA(slot->scintilla, SCI_UNDO, 0, 0);
            slot->suppress_source_notification = 0U;
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "restore_result_verification_failed");
            goto done;
        }
        dluma_sha256_bytes_hex(updated, updated_size, current_sha);
        if (dluma_stricmp(current_sha, request->source_sha256) != 0) {
            (void)SendMessageA(slot->scintilla, SCI_UNDO, 0, 0);
            slot->suppress_source_notification = 0U;
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "restore_result_hash_mismatch");
            goto done;
        }
        ++slot->revision;
        dluma_str_copy(
            slot->source_sha256,
            DLUMA_ARRAY_COUNT(slot->source_sha256), current_sha);
        if (!request->restore_was_modified) {
            (void)SendMessageA(
                slot->scintilla, SCI_SETSAVEPOINT, 0, 0);
            slot->saved_revision = slot->revision;
            dluma_str_copy(
                slot->saved_sha256,
                DLUMA_ARRAY_COUNT(slot->saved_sha256), current_sha);
        } else {
            slot->saved_revision = 0;
            slot->saved_sha256[0] = 0;
        }
        slot->suppress_source_notification = 0U;
        request->response_revision = slot->revision;
        dluma_str_copy(
            request->source_sha256,
            DLUMA_ARRAY_COUNT(request->source_sha256), current_sha);
        accepted = 1;
        goto done;
    }
    if (request->expected_revision != slot->revision ||
        !dluma_qg_scintilla_read(
            slot->scintilla, &current, &current_size)) {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "revision_conflict");
        goto done;
    }
    dluma_sha256_bytes_hex(current, current_size, current_sha);
    if (dluma_stricmp(current_sha, request->expected_sha256) != 0) {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "source_conflict");
        goto done;
    }
    if (request->kind == DLUMA_QG_UI_APPLY_SOURCE) {
        uint32_t result_size = 0;
        uint32_t error_index = 0;
        DlumaQuestGraphEditError edit_error =
            dluma_quest_graph_edits_validate(
                request->edits, request->edit_count,
                DLUMA_QG_MAX_EDITS, current_size,
                DLUMA_QG_MAX_SOURCE_BYTES,
                &result_size, &error_index);
        (void)error_index;
        if (edit_error != DLUMA_QUEST_GRAPH_EDIT_OK) {
            dluma_str_copy(
                request->error,
                DLUMA_ARRAY_COUNT(request->error),
                dluma_quest_graph_edit_error_code(edit_error));
            goto done;
        }
        slot->suppress_source_notification = 1U;
        (void)SendMessageA(
            slot->scintilla, SCI_BEGINUNDOACTION, 0, 0);
        for (index = 0; index < request->edit_count; ++index) {
            const DlumaQgTextEdit* edit = &request->edits[index];
            LRESULT replaced;
            (void)SendMessageA(
                slot->scintilla, SCI_SETTARGETSTART,
                (WPARAM)edit->byte_offset, 0);
            (void)SendMessageA(
                slot->scintilla, SCI_SETTARGETEND,
                (WPARAM)(edit->byte_offset +
                    edit->removed_length), 0);
            replaced = SendMessageA(
                slot->scintilla, SCI_REPLACETARGET,
                (WPARAM)edit->replacement_size,
                (LPARAM)edit->replacement);
            if (replaced != (LRESULT)edit->replacement_size)
                break;
        }
        (void)SendMessageA(
            slot->scintilla, SCI_ENDUNDOACTION, 0, 0);
        if (index != request->edit_count) {
            (void)SendMessageA(slot->scintilla, SCI_UNDO, 0, 0);
            slot->suppress_source_notification = 0U;
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "scintilla_edit_failed");
            goto done;
        }
        if (!dluma_qg_scintilla_read(
                slot->scintilla, &updated, &updated_size) ||
            updated_size != result_size) {
            (void)SendMessageA(slot->scintilla, SCI_UNDO, 0, 0);
            slot->suppress_source_notification = 0U;
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "scintilla_edit_verification_failed");
            goto done;
        }
        slot->suppress_source_notification = 0U;
        dluma_sha256_bytes_hex(updated, updated_size, current_sha);
        if (dluma_stricmp(
                current_sha, request->expected_result_sha256) != 0) {
            (void)SendMessageA(slot->scintilla, SCI_UNDO, 0, 0);
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "scintilla_edit_hash_mismatch");
            goto done;
        }
        ++slot->revision;
        dluma_str_copy(
            slot->source_sha256,
            DLUMA_ARRAY_COUNT(slot->source_sha256), current_sha);
        dluma_str_copy(
            request->source_sha256,
            DLUMA_ARRAY_COUNT(request->source_sha256), current_sha);
        request->response_revision = slot->revision;
        accepted = 1;
        goto done;
    }
    if (!dluma_project_regular_file_is_contained(
            slot->project_root, slot->source_path) ||
        dluma_stricmp(current_sha, request->source_sha256) != 0) {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "source_identity_mismatch");
        goto done;
    }
    if (request->kind == DLUMA_QG_UI_SAVE) {
        if (!dluma_qg_dispatch_stock_save(slot)) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "stock_save_route_unavailable");
            goto done;
        }
        if (!dluma_project_regular_file_is_contained(
                slot->project_root, slot->source_path) ||
            !dluma_sha256_file_hex(
                slot->source_path, DLUMA_QG_MAX_SOURCE_BYTES, disk_sha) ||
            dluma_stricmp(disk_sha, current_sha) != 0) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "stock_save_verification_failed");
            goto done;
        }
        (void)SendMessageA(slot->scintilla, SCI_SETSAVEPOINT, 0, 0);
        slot->saved_revision = slot->revision;
        dluma_str_copy(
            slot->saved_sha256,
            DLUMA_ARRAY_COUNT(slot->saved_sha256), current_sha);
        if (!dluma_wide_to_utf8(
                slot->source_path, request->disk_path,
                DLUMA_ARRAY_COUNT(request->disk_path))) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "saved_path_encoding_failed");
            goto done;
        }
    } else if (request->kind == DLUMA_QG_UI_COMPILE) {
        if (slot->saved_revision != slot->revision ||
            dluma_stricmp(slot->saved_sha256, current_sha) != 0) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "verified_stock_save_required");
            goto done;
        }
        if (!dluma_project_regular_file_is_contained(
                slot->project_root, slot->source_path) ||
            !dluma_sha256_file_hex(
                slot->source_path, DLUMA_QG_MAX_SOURCE_BYTES, disk_sha) ||
            dluma_stricmp(disk_sha, current_sha) != 0) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "saved_source_identity_changed");
            goto done;
        }
        if (!g_dluma_qg.main_predecessor) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "stock_compile_route_unavailable");
            goto done;
        }
        (void)CallWindowProcW(
            g_dluma_qg.main_predecessor, g_dluma_qg.main_window,
            WM_COMMAND, (WPARAM)DLUMA_QG_COMPILE_COMMAND, 0);
    } else {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "unsupported_ui_request");
        goto done;
    }
    dluma_str_copy(
        slot->source_sha256,
        DLUMA_ARRAY_COUNT(slot->source_sha256), current_sha);
    dluma_str_copy(
        request->source_sha256,
        DLUMA_ARRAY_COUNT(request->source_sha256), current_sha);
    request->response_revision = slot->revision;
    accepted = 1;
done:
    if (current) dluma_free(current);
    if (updated) dluma_free(updated);
    dluma_atomic_exchange(&request->state, 2);
    request->error[DLUMA_ARRAY_COUNT(request->error) - 1U] = 0;
    /* SendMessageTimeout receives this value from the main-frame procedure. */
    request->error[0] = accepted ? 0 : request->error[0];
}

static LRESULT CALLBACK dluma_qg_dialog_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DlumaQgDialog* slot = dluma_qg_dialog_find(window);
    WNDPROC predecessor = slot ? slot->predecessor : 0;
    UINT_PTR source_timer = slot
        ? (UINT_PTR)DLUMA_QG_SOURCE_TIMER_BASE +
            (UINT_PTR)(slot - g_dluma_qg.dialogs)
        : 0;
    LRESULT result;
    if (slot && message == WM_COMMAND &&
        (UINT)(wparam & 0xFFFFU) == DLUMA_QG_BUTTON_CONTROL) {
        dluma_qg_open_dialog(slot);
        return 0;
    }
    if (slot && message == WM_TIMER &&
        (UINT_PTR)wparam == source_timer) {
        (void)KillTimer(window, source_timer);
        dluma_qg_publish_source_changed(slot);
        return 0;
    }
    if (slot && message == WM_NOTIFY && lparam &&
        !slot->suppress_source_notification) {
        const DlumaQgNmHdr* notification =
            (const DlumaQgNmHdr*)lparam;
        if (notification->window_from == slot->scintilla &&
            notification->code == SCN_MODIFIED)
            (void)SetTimer(
                window, source_timer,
                DLUMA_QG_SOURCE_DEBOUNCE_MS, 0);
    }
    result = predecessor
        ? CallWindowProcW(predecessor, window, message, wparam, lparam)
        : 0;
    if (slot && message == WM_SIZE) dluma_qg_button_position(slot);
    if (slot && message == WM_NCDESTROY) {
        (void)KillTimer(window, source_timer);
        if (slot->scintilla &&
            IsWindow(slot->scintilla) &&
            slot->scintilla_predecessor &&
            (WNDPROC)GetWindowLongPtrW(
                slot->scintilla, GWLP_WNDPROC) ==
                    dluma_qg_scintilla_proc)
            (void)SetWindowLongPtrW(
                slot->scintilla, GWLP_WNDPROC,
                (LONG_PTR)slot->scintilla_predecessor);
        slot->dialog = 0;
        slot->scintilla = 0;
        slot->button = 0;
        slot->predecessor = 0;
        slot->scintilla_predecessor = 0;
        slot->project_root[0] = 0;
        slot->source_path[0] = 0;
        slot->relative_path[0] = 0;
        slot->document_id[0] = 0;
        slot->source_sha256[0] = 0;
        slot->revision = 0;
        slot->saved_revision = 0;
        slot->saved_sha256[0] = 0;
    }
    return result;
}

static int dluma_qg_dialog_attach(HWND dialog, HWND scintilla)
{
    unsigned int index;
    DlumaQgDialog* slot = dluma_qg_dialog_find(dialog);
    WNDPROC observed;
    WNDPROC installed;
    WNDPROC observed_scintilla;
    WNDPROC installed_scintilla;
    LRESULT font;
    if (slot) {
        dluma_qg_button_position(slot);
        return 1;
    }
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index)
        if (!g_dluma_qg.dialogs[index].dialog) break;
    if (index == DLUMA_QG_MAX_DIALOGS ||
        GetDlgItem(dialog, DLUMA_QG_BUTTON_CONTROL))
        return 0;
    slot = &g_dluma_qg.dialogs[index];
    memset(slot, 0, sizeof(*slot));
    observed = (WNDPROC)GetWindowLongPtrW(dialog, GWLP_WNDPROC);
    observed_scintilla = (WNDPROC)GetWindowLongPtrW(
        scintilla, GWLP_WNDPROC);
    if (!observed || observed == dluma_qg_dialog_proc ||
        !observed_scintilla ||
        observed_scintilla == dluma_qg_scintilla_proc)
        return 0;
    slot->dialog = dialog;
    slot->scintilla = scintilla;
    slot->predecessor = observed;
    slot->scintilla_predecessor = observed_scintilla;
    slot->autocomplete_separator = ' ';
    slot->button = CreateWindowExW(
        0, L"BUTTON", L"Visual Quest Editor",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 176, 26, dialog,
        (HMENU)(UINT_PTR)DLUMA_QG_BUTTON_CONTROL,
        g_dluma.self_module, 0);
    if (!slot->button) {
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    font = SendMessageA(dialog, WM_GETFONT, 0, 0);
    if (font) (void)SendMessageA(
        slot->button, WM_SETFONT, (WPARAM)font, TRUE);
    SetLastError(0);
    installed = (WNDPROC)SetWindowLongPtrW(
        dialog, GWLP_WNDPROC, (LONG_PTR)dluma_qg_dialog_proc);
    if (!installed && GetLastError() != 0) {
        DestroyWindow(slot->button);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    if (installed && installed != dluma_qg_dialog_proc)
        slot->predecessor = installed;
    SetLastError(0);
    installed_scintilla = (WNDPROC)SetWindowLongPtrW(
        scintilla, GWLP_WNDPROC,
        (LONG_PTR)dluma_qg_scintilla_proc);
    if (!installed_scintilla && GetLastError() != 0) {
        if ((WNDPROC)GetWindowLongPtrW(
                dialog, GWLP_WNDPROC) == dluma_qg_dialog_proc)
            (void)SetWindowLongPtrW(
                dialog, GWLP_WNDPROC,
                (LONG_PTR)slot->predecessor);
        DestroyWindow(slot->button);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }
    if (installed_scintilla &&
        installed_scintilla != dluma_qg_scintilla_proc)
        slot->scintilla_predecessor = installed_scintilla;
    dluma_qg_button_position(slot);
    dluma_log_pair(
        "INFO", "editor_quest_graph_button",
        "attached_to_exact_pxsl_dialog_signature");
    return 1;
}

static BOOL CALLBACK dluma_qg_discover_dialog(
    HWND window, LPARAM parameter)
{
    HWND scintilla = 0;
    (void)parameter;
    if (dluma_qg_dialog_signature(window, &scintilla))
        (void)dluma_qg_dialog_attach(window, scintilla);
    return TRUE;
}

static void dluma_qg_discover_dialogs(void)
{
    if (dluma_qg_profile_ready() && g_dluma_qg.main_window)
        (void)EnumChildWindows(
            g_dluma_qg.main_window, dluma_qg_discover_dialog, 0);
}

static void dluma_qg_teardown_dialogs(void)
{
    unsigned int index;
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index) {
        DlumaQgDialog* slot = &g_dluma_qg.dialogs[index];
        if (!slot->dialog) continue;
        if (slot->scintilla && IsWindow(slot->scintilla) &&
            slot->scintilla_predecessor &&
            (WNDPROC)GetWindowLongPtrW(
                slot->scintilla, GWLP_WNDPROC) ==
                    dluma_qg_scintilla_proc)
            (void)SetWindowLongPtrW(
                slot->scintilla, GWLP_WNDPROC,
                (LONG_PTR)slot->scintilla_predecessor);
        if (IsWindow(slot->dialog) && slot->predecessor &&
            (WNDPROC)GetWindowLongPtrW(
                slot->dialog, GWLP_WNDPROC) == dluma_qg_dialog_proc)
            (void)SetWindowLongPtrW(
                slot->dialog, GWLP_WNDPROC,
                (LONG_PTR)slot->predecessor);
        if (slot->button && IsWindow(slot->button))
            (void)DestroyWindow(slot->button);
        memset(slot, 0, sizeof(*slot));
    }
}

static LRESULT CALLBACK dluma_qg_main_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    WNDPROC predecessor = g_dluma_qg.main_predecessor;
    LRESULT result;
    if (message == DLUMA_QG_MAIN_DISCOVER) {
        dluma_qg_discover_dialogs();
        return 1;
    }
    if (message == DLUMA_QG_MAIN_DISPATCH) {
        dluma_qg_ui_request_execute();
        return g_dluma_qg.ui_request.error[0] ? 0 : 1;
    }
    if (message == DLUMA_QG_MAIN_TEARDOWN) {
        dluma_qg_teardown_dialogs();
        return 1;
    }
    result = predecessor
        ? CallWindowProcW(predecessor, window, message, wparam, lparam)
        : 0;
    if (message == WM_NCDESTROY && g_dluma_qg.main_window == window) {
        g_dluma_qg.main_window = 0;
        g_dluma_qg.main_predecessor = 0;
        g_dluma_qg.owner_thread_id = 0;
    }
    return result;
}

static int dluma_qg_main_window_matches(
    HWND window, DWORD expected_process, DWORD expected_thread)
{
    DWORD process_id = 0;
    DWORD thread_id;
    LONG_PTR style;
    LONG_PTR extended_style;
    WCHAR class_name[128];
    WCHAR title[2048];
    if (!window || !IsWindow(window) || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER))
        return 0;
    thread_id = GetWindowThreadProcessId(window, &process_id);
    if (!thread_id || process_id != expected_process ||
        (expected_thread && thread_id != expected_thread))
        return 0;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    class_name[0] = title[0] = 0;
    if ((style & 0x00C00000L) != 0x00C00000L ||
        !(style & 0x00080000L) || !(style & 0x00040000L) ||
        (extended_style & 0x00000080L) ||
        GetClassNameW(
            window, class_name,
            (int)DLUMA_ARRAY_COUNT(class_name)) <= 0 ||
        !dluma_qg_wide_starts_icase(class_name, L"Afx:") ||
        GetWindowTextW(
            window, title, (int)DLUMA_ARRAY_COUNT(title)) <= 0)
        return 0;
    return dluma_qg_wide_contains_icase(
        title, L"Dying Light Developer Tools");
}

static BOOL CALLBACK dluma_qg_find_main(
    HWND window, LPARAM parameter)
{
    DlumaQgWindowCandidate* candidate =
        (DlumaQgWindowCandidate*)parameter;
    RECT rectangle;
    ULONGLONG area;
    if (!candidate || !dluma_qg_main_window_matches(
            window, candidate->process_id, 0) ||
        !GetClientRect(window, &rectangle) ||
        rectangle.right <= rectangle.left ||
        rectangle.bottom <= rectangle.top)
        return TRUE;
    area = (ULONGLONG)(rectangle.right - rectangle.left) *
           (ULONGLONG)(rectangle.bottom - rectangle.top);
    if (!candidate->window || area > candidate->area) {
        candidate->window = window;
        candidate->area = area;
    }
    return TRUE;
}

static int dluma_qg_attach_main(void)
{
    DlumaQgWindowCandidate candidate;
    WNDPROC observed;
    WNDPROC installed;
    DWORD process_id = 0;
    DWORD thread_id;
    int accepted = 0;
    if (g_dluma_qg.main_window && g_dluma_qg.main_predecessor &&
        dluma_qg_main_window_matches(
            g_dluma_qg.main_window, GetCurrentProcessId(),
            g_dluma_qg.owner_thread_id) &&
        (WNDPROC)GetWindowLongPtrW(
            g_dluma_qg.main_window, GWLP_WNDPROC) == dluma_qg_main_proc) {
        (void)PostMessageW(
            g_dluma_qg.main_window, DLUMA_QG_MAIN_DISCOVER, 0, 0);
        return 1;
    }
    if (g_dluma_qg.main_window && IsWindow(g_dluma_qg.main_window))
        return 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_qg.attach_lock, 1, 0) != 0)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.process_id = GetCurrentProcessId();
    if (!EnumWindows(dluma_qg_find_main, (LPARAM)&candidate) ||
        !candidate.window)
        goto done;
    observed = (WNDPROC)GetWindowLongPtrW(
        candidate.window, GWLP_WNDPROC);
    thread_id = GetWindowThreadProcessId(
        candidate.window, &process_id);
    if (!thread_id || process_id != candidate.process_id ||
        !observed || observed == dluma_qg_main_proc)
        goto done;
    g_dluma_qg.main_window = candidate.window;
    g_dluma_qg.main_predecessor = observed;
    g_dluma_qg.owner_thread_id = thread_id;
    SetLastError(0);
    installed = (WNDPROC)SetWindowLongPtrW(
        candidate.window, GWLP_WNDPROC, (LONG_PTR)dluma_qg_main_proc);
    if (!installed && GetLastError() != 0) {
        g_dluma_qg.main_window = 0;
        g_dluma_qg.main_predecessor = 0;
        g_dluma_qg.owner_thread_id = 0;
        goto done;
    }
    if (installed && installed != dluma_qg_main_proc)
        g_dluma_qg.main_predecessor = installed;
    accepted = g_dluma_qg.main_predecessor != 0;
done:
    dluma_atomic_exchange(&g_dluma_qg.attach_lock, 0);
    if (accepted) {
        dluma_log_pair(
            "INFO", "editor_quest_graph_ui_bridge",
            "owner_thread_dispatcher_installed");
        (void)PostMessageW(
            g_dluma_qg.main_window, DLUMA_QG_MAIN_DISCOVER, 0, 0);
    }
    return accepted;
}

int dluma_editor_quest_graph_bridge_start(void)
{
    LONG state;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    /* UI admission is independent from optional native context providers.
     * The exact dialog controls are proven for this QuestEditor build, while
     * object/text/role context continues through the bounded persisted-data
     * provider until a separate native ABI profile is admitted. */
    if (!dluma_qg_profile_ready()) return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_qg.start_state, 1, 0);
    if (state < 0) return 0;
    if (state == 0) {
        g_dluma_qg.pipe = INVALID_HANDLE_VALUE;
        dluma_qg_lock(&g_dluma_qg.context_lock);
        dluma_quest_context_initialize(&g_dluma_qg.context);
        dluma_str_copy(
            g_dluma_qg.context_project_identity,
            DLUMA_ARRAY_COUNT(g_dluma_qg.context_project_identity),
            "editor");
        g_dluma_qg.context_map_identity[0] = 0;
        dluma_qg_unlock(&g_dluma_qg.context_lock);
        dluma_atomic_exchange(&g_dluma_qg.stopping, 0);
        if (!dluma_qg_runtime_acquire()) {
            dluma_log_pair(
                "WARN", "editor_quest_graph_private_runtime",
                g_dluma_qg.runtime_lease.diagnostic[0]
                    ? g_dluma_qg.runtime_lease.diagnostic
                    : "manifest_bound_runtime_unavailable");
            dluma_atomic_exchange(&g_dluma_qg.start_state, -1);
            return 0;
        }
        dluma_atomic_exchange(&g_dluma_qg.start_state, 2);
    }
    (void)dluma_qg_attach_main();
    /* The main frame and QuestEditor dialog may be created after loader
     * startup. The existing Editor watcher calls this function repeatedly. */
    return dluma_atomic_compare_exchange(
               &g_dluma_qg.start_state, 0, 0) == 2;
}

int dluma_editor_quest_graph_bridge_stop(void)
{
    DWORD wait_result = WAIT_OBJECT_0;
    ULONG_PTR ignored = 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_qg.start_state, 0, 0) != 2)
        return 1;
    dluma_atomic_exchange(&g_dluma_qg.stopping, 1);
    if (g_dluma_qg.main_window && IsWindow(g_dluma_qg.main_window))
        (void)SendMessageTimeoutW(
            g_dluma_qg.main_window, DLUMA_QG_MAIN_TEARDOWN, 0, 0,
            DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG |
                DLUMA_SMTO_ERRORONEXIT,
            2000U, &ignored);
    if (g_dluma_qg.stop_event) (void)SetEvent(g_dluma_qg.stop_event);
    if (g_dluma_qg.worker_thread)
        wait_result = WaitForSingleObject(
            g_dluma_qg.worker_thread, 2000U);
    if (wait_result != WAIT_OBJECT_0) return 0;
    if (g_dluma_qg.worker_thread) CloseHandle(g_dluma_qg.worker_thread);
    if (g_dluma_qg.stop_event) CloseHandle(g_dluma_qg.stop_event);
    if (g_dluma_qg.companion_process)
        CloseHandle(g_dluma_qg.companion_process);
    if (g_dluma_qg.pipe && g_dluma_qg.pipe != INVALID_HANDLE_VALUE)
        CloseHandle(g_dluma_qg.pipe);
    dluma_qg_ui_payload_free(&g_dluma_qg.ui_request);
    memset((void*)&g_dluma_qg.ui_request, 0,
           sizeof(g_dluma_qg.ui_request));
    g_dluma_qg.worker_thread = 0;
    g_dluma_qg.stop_event = 0;
    g_dluma_qg.pipe = INVALID_HANDLE_VALUE;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg.pending_frame) dluma_free(g_dluma_qg.pending_frame);
    g_dluma_qg.pending_frame = 0;
    g_dluma_qg.pending_frame_size = 0;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (g_dluma_qg.main_window && g_dluma_qg.main_predecessor &&
        (WNDPROC)GetWindowLongPtrW(
            g_dluma_qg.main_window, GWLP_WNDPROC) == dluma_qg_main_proc)
        (void)SetWindowLongPtrW(
            g_dluma_qg.main_window, GWLP_WNDPROC,
            (LONG_PTR)g_dluma_qg.main_predecessor);
    g_dluma_qg.main_window = 0;
    g_dluma_qg.main_predecessor = 0;
    g_dluma_qg.owner_thread_id = 0;
    g_dluma_qg.token[0] = 0;
    g_dluma_qg.pipe_name[0] = 0;
    g_dluma_qg.pipe_short_name[0] = 0;
    dluma_qg_lock(&g_dluma_qg.context_lock);
    dluma_quest_context_initialize(&g_dluma_qg.context);
    dluma_str_copy(
        g_dluma_qg.context_project_identity,
        DLUMA_ARRAY_COUNT(g_dluma_qg.context_project_identity),
        "editor");
    g_dluma_qg.context_map_identity[0] = 0;
    dluma_qg_unlock(&g_dluma_qg.context_lock);
    g_dluma_qg.pending_generation = 0;
    g_dluma_qg.sent_generation = 0;
    g_dluma_qg.last_launch_tick = 0;
    g_dluma_qg.companion_process = 0;
    g_dluma_qg.companion_process_id = 0;
    g_dluma_qg.companion_sha256[0] = 0;
    dluma_runtime_tree_release(&g_dluma_qg.runtime_lease);
    dluma_atomic_exchange(&g_dluma_qg.connected, 0);
    dluma_atomic_exchange(&g_dluma_qg.authenticated, 0);
    dluma_atomic_exchange(&g_dluma_qg.stopping, 0);
    dluma_atomic_exchange(&g_dluma_qg.start_state, 0);
    return 1;
}

int dluma_editor_quest_graph_bridge_callbacks_drained(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_qg.callbacks_in_flight, 0, 0) == 0;
}
