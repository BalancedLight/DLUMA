#include "dlw_editor_quest_graph_bridge.h"

#include "dlw_dual_host_runtime.h"
#include "dlw_editor_launch.h"
#include "dlw_editor_host.h"
#include "dlw_editor_presence.h"
#include "dlw_editor_ui.h"
#include "dlw_editor_panel_host.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_quest_editor.h"
#include "core/dlw_runtime_tree.h"
#include "core/dlw_json_reader.h"
#include "core/dlw_quest_context_core.h"
#include "core/dlw_quest_graph_edits.h"
#include "generated/dluma_api_catalog.generated.h"

#include <string.h>

__declspec(dllimport) BOOL WINAPI FileTimeToSystemTime(
    const FILETIME* file_time, SYSTEMTIME* system_time);

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
#define DLUMA_QG_DOCUMENT_VTABLE_RVA 0x000BD128U
#define DLUMA_QG_CWND_FROM_HANDLE_IAT_RVA 0x000B2BF0U
#define DLUMA_QG_FULL_PATH_IAT_RVA 0x000B1E18U
#define DLUMA_QG_NATIVE_FREE_IAT_RVA 0x000B16A0U
#define DLUMA_QG_FILESYSTEM_TIMESTAMP 0x623394CDU
#define DLUMA_QG_FILESYSTEM_IMAGE_SIZE 0x00060000U
#define DLUMA_QG_FULL_PATH_RVA 0x00014D80U
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
#define DLUMA_QG_SAVE_TIMER_BASE 0x4E10U
#define DLUMA_QG_SOURCE_DEBOUNCE_MS 350U
#define DLUMA_QG_MAX_DIALOGS 16U
#define DLUMA_QG_MAX_PENDING_DOCUMENTS (2U * DLUMA_QG_MAX_DIALOGS)
#define DLUMA_QG_MAX_EDITS 4096U
#define DLUMA_QG_MAX_SOURCE_BYTES (8U * 1024U * 1024U)
#define DLUMA_QG_MAX_FRAME_BYTES (16U * 1024U * 1024U)
#define DLUMA_QG_MAX_COMPANION_BYTES (512ULL * 1024ULL * 1024ULL)
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
#define SCN_SAVEPOINTREACHED 2002U

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

typedef struct DlumaQgNativeString {
    char* data;
    unsigned int size, capacity;
} DlumaQgNativeString;
typedef DlumaQgNativeString* (__fastcall *DlumaQgFullPathFn)(
    DlumaQgNativeString*, const char*);
typedef void (__cdecl *DlumaQgNativeFreeFn)(void*);

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
__declspec(dllimport) BOOL WINAPI IsWindowEnabled(HWND window);
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
__declspec(dllimport) UINT WINAPI GetOEMCP(void);
__declspec(dllimport) BOOL WINAPI AreFileApisANSI(void);

typedef struct DlumaQgDialog {
    HWND dialog;
    HWND scintilla;
    HWND button;
    DlumaEditorPanelRef panel;
    WNDPROC predecessor;
    WNDPROC scintilla_predecessor;
    unsigned int clip_children_added;
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
    void* native_document;
    char native_path[DLUMA_MAX_WPATH * 4U];
    char* save_source;
    DWORD save_source_size;
    ULONGLONG save_source_revision;
    char save_source_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char save_source_encoding[64];
} DlumaQgDialog;

typedef enum DlumaQgUiRequestKind {
    DLUMA_QG_UI_NONE = 0,
    DLUMA_QG_UI_SAVE = 1,
    DLUMA_QG_UI_COMPILE = 2,
    DLUMA_QG_UI_APPLY_SOURCE = 3,
    DLUMA_QG_UI_ACTIVATE = 4,
    DLUMA_QG_UI_RESTORE_SOURCE = 5,
    DLUMA_QG_UI_PANEL_READY = 6
} DlumaQgUiRequestKind;

typedef DlumaQuestGraphTextEdit DlumaQgTextEdit;

typedef struct DlumaQgUiRequest {
    volatile LONG state;
    DlumaQgUiRequestKind kind;
    ULONGLONG expected_revision;
    ULONGLONG response_revision;
    HWND panel_parent;
    HWND panel_child;
    ULONGLONG panel_token;
    ULONGLONG panel_generation;
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
    DlumaQuestEditorCompileReceipt compile_receipt;
    char error[96];
} DlumaQgUiRequest;

typedef struct DlumaQgWindowCandidate {
    DWORD process_id;
    HWND window;
    ULONGLONG area;
} DlumaQgWindowCandidate;

typedef struct DlumaQgPendingDocument {
    ULONGLONG token;
    unsigned int admitted;
    unsigned int retired;
    char* open_frame;
    DWORD open_size;
    char* update_frame;
    DWORD update_size;
    char* saved_frame;
    DWORD saved_size;
} DlumaQgPendingDocument;

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
    unsigned int main_subscribed;
    unsigned int ui_teardown_complete;
    DWORD owner_thread_id;
    HMODULE quest_editor;
    HMODULE filesystem;
    int filesystem_profile_checked;
    DlumaQgFullPathFn native_full_path;
    DlumaQgNativeFreeFn native_string_free;
    DlumaQgDialog dialogs[DLUMA_QG_MAX_DIALOGS];
    DlumaQgUiRequest ui_request;
    HANDLE stop_event;
    HANDLE worker_thread;
    HANDLE pipe;
    WCHAR pipe_name[160];
    WCHAR pipe_short_name[128];
    char token[65];
    DlumaQgPendingDocument pending_documents[DLUMA_QG_MAX_PENDING_DOCUMENTS];
    DWORD pending_document_bytes;
    unsigned int pending_document_cursor;
    ULONGLONG companion_epoch;
    char* pending_panel_frame;
    DWORD pending_panel_frame_size;
    DlumaQgDialog* panel_dialog;
    DlumaQuestContextCore context;
    char context_project_identity[DLUMA_MAX_WPATH * 4U];
    char context_map_identity[DLUMA_MAX_MAP_NAME];
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
         strcmp(value, "panelReady") == 0 ||
         strcmp(value, "applyEdits") == 0 ||
         strcmp(value, "restoreDocument") == 0 ||
         strcmp(value, "completeClose") == 0 ||
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

static int dluma_qg_close_payload_valid(const char* payload, DWORD payload_size);

static int dluma_qg_validate_payload(
    const char* type, const char* payload, DWORD payload_size)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    unsigned int expected_fields = 0;
    int first = 1;
    if (!type || !payload || !payload_size) return 0;
    if (strcmp(type, "completeClose") == 0)
        return dluma_qg_close_payload_valid(payload, payload_size);
    if (strcmp(type, "hello") == 0)
        expected_fields = 0x0FU;
    else if (strcmp(type, "panelReady") == 0)
        expected_fields = 0x07U;
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
        if (strcmp(type, "panelReady") == 0) {
            uint64_t value = 0;
            unsigned int bit = strcmp(key, "parentWindow") == 0 ? 1U :
                strcmp(key, "childWindow") == 0 ? 2U :
                strcmp(key, "panelToken") == 0 ? 4U : 0U;
            if (!bit || !dluma_qg_payload_mark(&fields, bit) ||
                !dluma_json_u64(&reader, &value) || !value ||
                (bit != 4U && value > 0x7FFFFFFFFFFFFFFFULL))
                return 0;
        } else if (strcmp(type, "hello") == 0 &&
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

/* fs::full_path and its QuestEditor caller are qualified independently for
 * this Windows filesystem build. The caller releases the returned TTL buffer
 * with the same CRT free import used below. */
static int dluma_qg_filesystem_ready(void)
{
    static const BYTE prefix[] = {
        0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x48,0x89,0x7c,0x24,0x18,0x4c,0x89,0x64,0x24,0x20,
        0x55,0x48,0x8d,0xac,0x24,0x30,0xff,0xff,0xff,
        0x48,0x81,0xec,0xd0,0x01,0x00,0x00
    };
    HMODULE module = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");
    BYTE* base = (BYTE*)module;
    BYTE* quest = (BYTE*)g_dluma_qg.quest_editor;
    WCHAR path[DLUMA_MAX_WPATH];
    char sha[DLUMA_SHA256_HEX_CAPACITY];
    DWORD nt;
    DlumaQgFullPathFn resolve;
    DlumaQgNativeFreeFn release;
    if (!module || !crt || !quest) return 0;
    if (g_dluma_qg.filesystem_profile_checked &&
        g_dluma_qg.filesystem == module)
        return g_dluma_qg.native_full_path && g_dluma_qg.native_string_free;
    g_dluma_qg.filesystem = module;
    g_dluma_qg.filesystem_profile_checked = 1;
    g_dluma_qg.native_full_path = 0;
    g_dluma_qg.native_string_free = 0;
    if (!dluma_is_readable(base, 0x100U) || *(WORD*)base != 0x5a4dU)
        return 0;
    nt = *(DWORD*)(base + 0x3cU);
    if (nt > 0x10000000U || !dluma_is_readable(base + nt, 0x60U) ||
        *(DWORD*)(base + nt) != 0x00004550U ||
        *(WORD*)(base + nt + 24U) != 0x020bU ||
        *(DWORD*)(base + nt + 8U) != DLUMA_QG_FILESYSTEM_TIMESTAMP ||
        *(DWORD*)(base + nt + 0x50U) != DLUMA_QG_FILESYSTEM_IMAGE_SIZE ||
        !dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(path, 16ULL * 1024ULL * 1024ULL, sha) ||
        dluma_stricmp(sha,
            "C833EBD635572EC9D8D773479FA0DD6E4FA80D9F958A0876C8F07E5F1189B65A"))
        return 0;
    resolve = (DlumaQgFullPathFn)GetProcAddress(module,
        "?full_path@fs@@YA?BV?$string_base@D@ttl@@PEBD@Z");
    release = (DlumaQgNativeFreeFn)GetProcAddress(crt, "free");
    if ((BYTE*)resolve != base + DLUMA_QG_FULL_PATH_RVA ||
        !dluma_is_readable((const void*)resolve, sizeof(prefix)) ||
        memcmp((const void*)resolve, prefix, sizeof(prefix)) ||
        !dluma_is_readable(quest + DLUMA_QG_FULL_PATH_IAT_RVA, sizeof(resolve)) ||
        *(DlumaQgFullPathFn*)(quest + DLUMA_QG_FULL_PATH_IAT_RVA) != resolve ||
        !dluma_is_readable(quest + DLUMA_QG_NATIVE_FREE_IAT_RVA, sizeof(release)) ||
        *(DlumaQgNativeFreeFn*)(quest + DLUMA_QG_NATIVE_FREE_IAT_RVA) != release ||
        !dluma_qg_executable_pointer((const void*)release))
        return 0;
    g_dluma_qg.native_full_path = resolve;
    g_dluma_qg.native_string_free = release;
    return 1;
}

static int dluma_qg_absolute_path(const WCHAR* path)
{
    if (!path || !path[0]) return 0;
    if (path[0] == L'\\' && path[1] == L'\\') return 1;
    return ((path[0] >= L'A' && path[0] <= L'Z') ||
            (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/');
}

static int dluma_qg_regular_file_path(const WCHAR* path,
    WCHAR* canonical, unsigned int capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    int valid;
    if (!path || !canonical || !capacity || !dluma_qg_absolute_path(path))
        return 0;
    canonical[0] = 0;
    file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    valid = GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        info.nNumberOfLinks == 1U &&
        dluma_get_final_path_checked(file, canonical, capacity);
    CloseHandle(file);
    if (!valid) canonical[0] = 0;
    return valid;
}

static int dluma_qg_native_filename_to_wide(const char* path,
    WCHAR* output, unsigned int capacity)
{
    UINT code_page = AreFileApisANSI() ? GetACP() : GetOEMCP();
    if (!path || !path[0] || !output || !capacity || capacity > 0x7fffffffU)
        return 0;
    output[0] = 0;
    /* Stock Source opens the byte filename with CreateFileA. Decode using the
     * same file-API codepage; invalid multibyte sequences cannot identify it. */
    return MultiByteToWideChar(code_page, 0x00000008U /* MB_ERR_INVALID_CHARS */,
        path, -1, output, (int)capacity) > 0;
}

static int dluma_qg_native_relative_path(WCHAR* path)
{
    WCHAR* segment = path;
    WCHAR* cursor;
    if (!path || !path[0] || path[0] == L'/' || path[0] == L'\\')
        return 0;
    /* Validate decoded characters: a DBCS trail byte must never be treated as
     * an ASCII separator. The canonical regular-file check rejects devices. */
    for (cursor = path; ; ++cursor) {
        WCHAR c = *cursor;
        if (!c || c == L'/' || c == L'\\') {
            SIZE_T count = (SIZE_T)(cursor - segment);
            if (!count || (count == 1U && segment[0] == L'.') ||
                (count == 2U && segment[0] == L'.' && segment[1] == L'.') ||
                cursor[-1] == L'.' || cursor[-1] == L' ')
                return 0;
            if (!c) return 1;
            *cursor = L'\\';
            segment = cursor + 1;
        } else if (c < 0x20U || c == L'<' || c == L'>' || c == L':' ||
                   c == L'"' || c == L'|' || c == L'?' || c == L'*')
            return 0;
    }
}

static int dluma_qg_mounted_source_path_core(const char* native_path,
    WCHAR* canonical, unsigned int capacity)
{
    DlumaQgNativeString result;
    /* The admitted native resolver constructs its path in a MAX_PATH buffer. */
    char resolved[260];
    WCHAR wide[260];
    int valid = 0;
    if (!native_path || !native_path[0] || !canonical || !capacity ||
        strlen(native_path) >= sizeof(resolved) ||
        !dluma_qg_filesystem_ready()) return 0;
    canonical[0] = 0;
    memset(&result, 0, sizeof(result));
    __try {
        if (g_dluma_qg.native_full_path(&result, native_path) == &result &&
            result.data && result.size && result.size <= result.capacity &&
            result.size < sizeof(resolved) &&
            dluma_is_readable(result.data, (SIZE_T)result.size + 1U) &&
            !result.data[result.size] && !memchr(result.data, 0, result.size)) {
            memcpy(resolved, result.data, (SIZE_T)result.size + 1U);
            valid = 1;
        }
    } __except(1) { valid = 0; }
    if (result.data) {
        __try { g_dluma_qg.native_string_free(result.data); }
        __except(1) { valid = 0; }
    }
    return valid && dluma_qg_native_filename_to_wide(resolved, wide, DLUMA_ARRAY_COUNT(wide)) &&
        dluma_qg_wide_ends_icase(wide, L".pxsl") &&
        dluma_qg_regular_file_path(wide, canonical, capacity);
}

static int dluma_qg_mounted_source_path(const char* native_path,
    WCHAR* canonical, unsigned int capacity)
{
    return GetCurrentThreadId() == g_dluma_qg.owner_thread_id &&
        dluma_qg_mounted_source_path_core(native_path, canonical, capacity);
}

static int dluma_qg_mounted_identity_matches(DlumaQgDialog* slot,
    const char* native_path)
{
    WCHAR canonical[DLUMA_MAX_WPATH];
    return slot && slot->source_path[0] &&
        dluma_qg_mounted_source_path(native_path, canonical, DLUMA_ARRAY_COUNT(canonical)) &&
        !dluma_wstricmp(canonical, slot->source_path) &&
        dluma_project_regular_file_is_contained(slot->project_root, canonical);
}

static int dluma_qg_native_document(
    DlumaQgDialog* slot, void** object_out, void** document_out,
    char* path, unsigned int path_capacity)
{
    BYTE* base;
    DlumaQgCWndFromHandleFn from_handle;
    BYTE* dialog_object;
    BYTE* document;
    const char* native_path;
    unsigned int length, index;
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
    if (!dluma_qg_executable_pointer((const void*)from_handle))
        return 0;
    dialog_object = (BYTE*)from_handle(slot->dialog);
    if (!dialog_object ||
        !dluma_is_readable(dialog_object, 712U) ||
        *(HWND*)(dialog_object + 704U) != slot->scintilla)
        return 0;
    document = *(BYTE**)(dialog_object + 632U);
    if (!document || !dluma_is_readable(document, 88U) ||
        *(void**)document != (void*)(base + DLUMA_QG_DOCUMENT_VTABLE_RVA) ||
        *(void**)(document + 64U) != dialog_object)
        return 0;
    native_path = *(const char**)(document + 72U);
    length = *(const unsigned int*)(document + 80U);
    if (!path || !length || length >= path_capacity || !native_path ||
        !dluma_is_readable(native_path, length))
        return 0;
    for (index = 0; index < length; ++index) if (!native_path[index]) return 0;
    memcpy(path, native_path, length);
    path[length] = 0;
    /* A Save As remains closable through its new native identity. For the
     * bound document, a namespace switch must not redirect a later stock save
     * or close replay to another project with the same relative filename. */
    if (slot->native_document == document && slot->source_path[0] &&
        !dluma_stricmp(path, slot->native_path) &&
        !dluma_qg_mounted_identity_matches(slot, path))
        return 0;
    if (object_out) *object_out = dialog_object;
    if (document_out) *document_out = document;
    return 1;
}

static int dluma_qg_native_identity_matches(DlumaQgDialog* slot)
{
    void* document = 0;
    char path[DLUMA_MAX_WPATH * 4U];
    return slot && slot->native_document &&
        dluma_qg_native_document(slot, 0, &document, path, DLUMA_ARRAY_COUNT(path)) &&
        document == slot->native_document &&
        dluma_stricmp(path, slot->native_path) == 0;
}

static int dluma_qg_dispatch_stock_save(DlumaQgDialog* slot)
{
    void* dialog_object = 0;
    void* document = 0;
    char path[DLUMA_MAX_WPATH * 4U];
    DlumaQgStockSaveFn save;
    if (!dluma_qg_native_identity_matches(slot) ||
        !dluma_qg_native_document(slot, &dialog_object, &document,
            path, DLUMA_ARRAY_COUNT(path)))
        return 0;
    save = (DlumaQgStockSaveFn)((BYTE*)g_dluma_qg.quest_editor + DLUMA_QG_STOCK_SAVE_RVA);
    if (!dluma_qg_executable_pointer((const void*)save))
        return 0;
    (void)save(dialog_object);
    return dluma_qg_native_identity_matches(slot);
}

static HWND dluma_qg_stock_compile_control(DlumaQgDialog* slot)
{
    HWND button;
    DWORD process_id = 0;
    WCHAR class_name[16];
    if (!slot || !slot->dialog ||
        GetWindowThreadProcessId(slot->dialog, &process_id) != GetCurrentThreadId() ||
        process_id != GetCurrentProcessId() ||
        !dluma_qg_native_identity_matches(slot) ||
        !IsWindowEnabled(slot->dialog))
        return 0;
    button = GetDlgItem(slot->dialog, DLUMA_QG_COMPILE_COMMAND);
    if (!button || GetParent(button) != slot->dialog ||
        GetWindowThreadProcessId(button, &process_id) != GetCurrentThreadId() ||
        process_id != GetCurrentProcessId() || !IsWindowEnabled(button) ||
        GetClassNameW(button, class_name, DLUMA_ARRAY_COUNT(class_name)) != 6 ||
        memcmp(class_name, L"Button", sizeof(L"Button")))
        return 0;
    return button;
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
    RECT current_rectangle;
    POINT dialog_origin;
    HWND patch_diff;
    int available_width;
    int width = 156;
    int height = 26;
    int x;
    int y;
    if (!slot || !slot->dialog || !slot->button || slot->panel.token ||
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
    if (IsWindowVisible(slot->button) &&
        GetWindowRect(slot->button, &current_rectangle) &&
        current_rectangle.left == dialog_origin.x + x &&
        current_rectangle.top == dialog_origin.y + y &&
        current_rectangle.right - current_rectangle.left == width &&
        current_rectangle.bottom - current_rectangle.top == height)
        return;
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

static LRESULT CALLBACK dluma_qg_scintilla_proc_impl(
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

static LRESULT CALLBACK dluma_qg_scintilla_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try { result = dluma_qg_scintilla_proc_impl(window, message, wparam, lparam); }
    __finally { dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight); }
    return result;
}

static int dluma_qg_project_from_native_source(const char* native_path,
    const WCHAR* source_path, WCHAR* project_root, unsigned int project_capacity)
{
    struct ProjectPaths {
        WCHAR workshop[DLUMA_MAX_WPATH], candidate[DLUMA_MAX_WPATH];
        WCHAR quests[DLUMA_MAX_WPATH], joined[DLUMA_MAX_WPATH];
        WCHAR canonical[DLUMA_MAX_WPATH];
        DlumaDirectoryIdentity project, quest_directory;
    } *paths;
    WCHAR component[260], relative[260];
    int valid = 0;
    const WCHAR* cursor;
    SIZE_T count = 0;
    if (!native_path || !source_path || !project_root || !project_capacity) return 0;
    paths = (struct ProjectPaths*)dluma_alloc(sizeof(*paths));
    if (!paths) return 0;
    if (!dluma_editor_launch_resolve_workshop_root(paths->workshop, DLUMA_ARRAY_COUNT(paths->workshop)) ||
        !dluma_final_path_equal_or_under(source_path, paths->workshop)) goto done;
    cursor = source_path + wcslen(paths->workshop);
    while (*cursor == L'\\' || *cursor == L'/') ++cursor;
    while (cursor[count] && cursor[count] != L'\\' && cursor[count] != L'/') {
        if (count + 1U >= DLUMA_ARRAY_COUNT(component)) goto done;
        component[count] = cursor[count]; ++count;
    }
    if (!count || !cursor[count]) goto done;
    component[count] = 0;
    if (!dluma_checked_path_join(paths->candidate, DLUMA_ARRAY_COUNT(paths->candidate), paths->workshop, component) ||
        !dluma_project_candidate_root_is_contained(paths->workshop, paths->candidate) ||
        !dluma_open_directory_identity(paths->candidate, &paths->project) ||
        !dluma_checked_path_join(paths->quests, DLUMA_ARRAY_COUNT(paths->quests), paths->project.final_path, L"data\\quests") ||
        !dluma_open_directory_identity(paths->quests, &paths->quest_directory) ||
        !dluma_final_path_equal_or_under(paths->quest_directory.final_path, paths->project.final_path) ||
        !dluma_final_path_equal_or_under(source_path, paths->quest_directory.final_path) ||
        !dluma_project_regular_file_is_contained(paths->project.final_path, source_path))
        goto done;
    if (!dluma_qg_native_filename_to_wide(native_path, relative, DLUMA_ARRAY_COUNT(relative))) goto done;
    if (!dluma_qg_absolute_path(relative)) {
        if (!dluma_qg_native_relative_path(relative) ||
            !dluma_checked_path_join(paths->joined, DLUMA_ARRAY_COUNT(paths->joined), paths->project.final_path, relative) ||
            !dluma_qg_regular_file_path(paths->joined, paths->canonical, DLUMA_ARRAY_COUNT(paths->canonical)) ||
            dluma_wstricmp(paths->canonical, source_path)) goto done;
    }
    if (wcslen(paths->project.final_path) + 1U > project_capacity) goto done;
    dluma_wstr_copy(project_root, project_capacity, paths->project.final_path);
    valid = 1;
done:
    dluma_free(paths);
    return valid;
}

int dluma_editor_quest_resolve_compile_paths(
    const char* native_source, const char* native_output,
    int editor_callback_tracked,
    WCHAR* project_root, unsigned int project_capacity,
    char* source_absolute, unsigned int source_capacity,
    char* output_absolute, unsigned int output_capacity)
{
    struct CompilePaths {
        WCHAR project[DLUMA_MAX_WPATH], source[DLUMA_MAX_WPATH];
        WCHAR output[DLUMA_MAX_WPATH], joined[DLUMA_MAX_WPATH];
        WCHAR parent[DLUMA_MAX_WPATH];
        DlumaDirectoryIdentity parent_identity;
        char source_utf8[DLUMA_MAX_WPATH * 4U];
        char output_utf8[DLUMA_MAX_WPATH * 4U];
    } *paths;
    const char* failure = "native_source_unavailable";
    unsigned int index;
    int valid = 0;
    if (!native_source || !native_output || !project_root || !project_capacity ||
        !source_absolute || !source_capacity || !output_absolute || !output_capacity)
        return 0;
    project_root[0] = 0; source_absolute[0] = 0; output_absolute[0] = 0;
    if (!editor_callback_tracked || !dluma_editor_host_current_thread_in_callback() ||
        !dluma_dual_host_runtime_is_editor() || !dluma_qg_profile_ready()) {
        dluma_log_pair("ERROR", "quest_compile_resolution", "native_editor_callback_unavailable");
        return 0;
    }
    paths = (struct CompilePaths*)dluma_alloc(sizeof(*paths));
    if (!paths) return 0;
    if (!dluma_qg_mounted_source_path_core(native_source, paths->source, DLUMA_ARRAY_COUNT(paths->source)))
        goto done;
    failure = "source_project_unavailable";
    if (!dluma_qg_project_from_native_source(native_source, paths->source,
            paths->project, DLUMA_ARRAY_COUNT(paths->project))) goto done;
    failure = "native_output_path_invalid";
    if (!dluma_qg_native_filename_to_wide(native_output, paths->output, DLUMA_ARRAY_COUNT(paths->output)))
        goto done;
    if (!dluma_qg_absolute_path(paths->output)) {
        if (!dluma_qg_native_relative_path(paths->output) ||
            !dluma_checked_path_join(paths->joined, DLUMA_ARRAY_COUNT(paths->joined),
                paths->project, paths->output)) goto done;
        dluma_wstr_copy(paths->output, DLUMA_ARRAY_COUNT(paths->output), paths->joined);
    }
    for (index = 0; paths->output[index]; ++index)
        if (paths->output[index] == L'/') paths->output[index] = L'\\';
    dluma_wstr_copy(paths->parent, DLUMA_ARRAY_COUNT(paths->parent), paths->output);
    dluma_path_parent(paths->parent);
    failure = "output_parent_outside_source_project";
    if (!dluma_open_directory_identity(paths->parent, &paths->parent_identity) ||
        !dluma_final_path_equal_or_under(paths->parent_identity.final_path, paths->project))
        goto done;
    failure = "compile_path_encoding_or_capacity";
    if (!dluma_wide_to_utf8(paths->source, paths->source_utf8, DLUMA_ARRAY_COUNT(paths->source_utf8)) ||
        !dluma_wide_to_utf8(paths->output, paths->output_utf8, DLUMA_ARRAY_COUNT(paths->output_utf8)) ||
        wcslen(paths->project) + 1U > project_capacity ||
        strlen(paths->source_utf8) + 1U > source_capacity ||
        strlen(paths->output_utf8) + 1U > output_capacity) goto done;
    dluma_wstr_copy(project_root, project_capacity, paths->project);
    dluma_str_copy(source_absolute, source_capacity, paths->source_utf8);
    dluma_str_copy(output_absolute, output_capacity, paths->output_utf8);
    valid = 1;
done:
    if (!valid) dluma_log_pair("ERROR", "quest_compile_resolution", failure);
    dluma_free(paths);
    return valid;
}

int dluma_editor_quest_native_filename_from_utf8(
    const char* path, char* output, unsigned int capacity)
{
    WCHAR wide[DLUMA_MAX_WPATH];
    UINT code_page = AreFileApisANSI() ? GetACP() : GetOEMCP();
    BOOL used_default = FALSE;
    int count;
    if (!path || !output || !capacity || capacity > 0x7fffffffU) return 0;
    output[0] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0x00000008U /* MB_ERR_INVALID_CHARS */,
            path, -1, wide, DLUMA_ARRAY_COUNT(wide))) return 0;
    count = WideCharToMultiByte(code_page,
        code_page == CP_UTF8 ? 0 : 0x00000400U /* WC_NO_BEST_FIT_CHARS */,
        wide, -1, output, (int)capacity, 0,
        code_page == CP_UTF8 ? 0 : &used_default);
    if (!count || used_default) { output[0] = 0; return 0; }
    return 1;
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
    struct DocumentPaths {
        WCHAR project_root[DLUMA_MAX_WPATH], source_path[DLUMA_MAX_WPATH];
        char relative_path[DLUMA_MAX_WPATH * 4U];
        char identity[DLUMA_MAX_WPATH * 4U + 32U];
    } *paths;
    int valid = 0;
    char native_path[260];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    char document_id[DLUMA_SHA256_HEX_CAPACITY];
    char lifetime[32];
    void* native_document = 0;
    if (!slot || !text || !slot->panel.token) return 0;
    paths = (struct DocumentPaths*)dluma_alloc(sizeof(*paths));
    if (!paths) return 0;
    if (!dluma_qg_native_document(slot, 0, &native_document,
            native_path, DLUMA_ARRAY_COUNT(native_path))) {
        dluma_log_pair("WARN", "editor_quest_graph_source_binding", "native_document_unavailable");
        goto done;
    }
    if (!dluma_qg_mounted_source_path(native_path, paths->source_path, DLUMA_ARRAY_COUNT(paths->source_path))) {
        dluma_log_pair("WARN", "editor_quest_graph_source_binding", "mounted_source_unavailable");
        goto done;
    }
    if (!dluma_qg_project_from_native_source(native_path, paths->source_path,
            paths->project_root, DLUMA_ARRAY_COUNT(paths->project_root))) {
        dluma_log_pair("WARN", "editor_quest_graph_source_binding", "source_project_unavailable");
        goto done;
    }
    if (!dluma_qg_make_relative(paths->project_root, paths->source_path,
            paths->relative_path, DLUMA_ARRAY_COUNT(paths->relative_path)) ||
        !dluma_wide_to_utf8(paths->source_path, paths->identity, DLUMA_ARRAY_COUNT(paths->identity)) ||
        !dluma_u64_to_text(slot->panel.token, lifetime, DLUMA_ARRAY_COUNT(lifetime)))
        goto done;
    dluma_str_append(paths->identity, DLUMA_ARRAY_COUNT(paths->identity), "|");
    dluma_str_append(paths->identity, DLUMA_ARRAY_COUNT(paths->identity), lifetime);
    dluma_sha256_bytes_hex(paths->identity, strlen(paths->identity), document_id);
    dluma_sha256_bytes_hex(text, text_size, digest);
    /* Publish the binding only after all native/file/project checks pass. */
    slot->native_document = native_document;
    dluma_str_copy(slot->native_path, DLUMA_ARRAY_COUNT(slot->native_path), native_path);
    dluma_wstr_copy(slot->project_root, DLUMA_ARRAY_COUNT(slot->project_root), paths->project_root);
    dluma_wstr_copy(slot->source_path, DLUMA_ARRAY_COUNT(slot->source_path), paths->source_path);
    dluma_str_copy(slot->relative_path, DLUMA_ARRAY_COUNT(slot->relative_path), paths->relative_path);
    dluma_str_copy(slot->document_id, DLUMA_ARRAY_COUNT(slot->document_id), document_id);
    dluma_str_copy(slot->source_sha256, DLUMA_ARRAY_COUNT(slot->source_sha256), digest);
    if (!slot->revision) slot->revision = 1ULL;
    valid = 1;
done:
    dluma_free(paths);
    return valid;
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

static int dluma_qg_build_source_frame(
    DlumaQgDialog* slot, const char* source, DWORD source_size,
    const char* type, ULONGLONG source_revision, const char* source_sha,
    const char* source_encoding, const WCHAR* disk_path,
    char** output, DWORD* output_size)
{
    char revision[32];
    char request_id[33];
    char encoding_name[32];
    char disk_utf8[DLUMA_MAX_WPATH * 4U];
    char* json;
    DWORD capacity;
    DWORD used = 0;
    if (!slot || !source || !output || !output_size ||
        !dluma_u64_to_text(
            source_revision, revision, DLUMA_ARRAY_COUNT(revision)))
        return 0;
    memcpy(request_id, g_dluma_qg.token, 32U);
    request_id[32] = 0;
    if (source_encoding) {
        dluma_str_copy(encoding_name, DLUMA_ARRAY_COUNT(encoding_name), source_encoding);
        if (strcmp(encoding_name, source_encoding)) return 0;
    } else if (!dluma_qg_scintilla_encoding(
            slot->scintilla, encoding_name,
            DLUMA_ARRAY_COUNT(encoding_name)))
        return 0;
    if (disk_path && !dluma_wide_to_utf8(disk_path, disk_utf8, DLUMA_ARRAY_COUNT(disk_utf8)))
        return 0;
    capacity = source_size + ((source_size + 2U) / 3U) + 32768U;
    if (capacity > DLUMA_QG_MAX_FRAME_BYTES) return 0;
    json = (char*)dluma_alloc((SIZE_T)capacity + 1U);
    if (!json) return 0;
    json[0] = 0;
    if (!dluma_qg_json_append(json, capacity, &used,
            "{\"schemaVersion\":2,\"type\":\"") ||
        !dluma_qg_json_append(json, capacity, &used, type) ||
        !dluma_qg_json_append(json, capacity, &used, "\",\"requestId\":\"") ||
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
            json, capacity, &used, source_sha) ||
        !dluma_qg_json_append(json, capacity, &used, "\"")) {
        dluma_free(json);
        return 0;
    }
    if (disk_path &&
        (!dluma_qg_json_append(json, capacity, &used, ",\"diskPath\":\"") ||
         !dluma_qg_json_escape(json, capacity, &used, disk_utf8) ||
         !dluma_qg_json_append(json, capacity, &used, "\""))) {
        dluma_free(json);
        return 0;
    }
    if (!dluma_qg_json_append(json, capacity, &used, "}}")) {
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
    if (!slot) return 0;
    return dluma_qg_build_source_frame(slot, source, source_size, "sourceChanged",
        slot->revision, slot->source_sha256, 0, 0, output, output_size);
}

/* Document queue entries are bounded by the native dialog count and use the
 * output lock. A full open cannot be replaced by an incremental update. */
static DlumaQgPendingDocument* dluma_qg_pending_document(ULONGLONG token, int create)
{
    unsigned int i;
    DlumaQgPendingDocument* free_slot = 0;
    for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
        DlumaQgPendingDocument* entry = &g_dluma_qg.pending_documents[i];
        if (entry->token == token) return entry;
        if (!entry->token && !free_slot) free_slot = entry;
    }
    if (create && free_slot) { free_slot->token = token; return free_slot; }
    return 0;
}

static void dluma_qg_clear_pending_documents(void)
{
    unsigned int i;
    for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
        DlumaQgPendingDocument* entry = &g_dluma_qg.pending_documents[i];
        if (entry->open_frame) dluma_free(entry->open_frame);
        if (entry->update_frame) dluma_free(entry->update_frame);
        if (entry->saved_frame) dluma_free(entry->saved_frame);
        memset(entry, 0, sizeof(*entry));
    }
    g_dluma_qg.pending_document_bytes = 0;
    g_dluma_qg.pending_document_cursor = 0;
}

static void dluma_qg_retire_document(ULONGLONG token)
{
    DlumaQgPendingDocument* entry;
    if (!token) return;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    entry = dluma_qg_pending_document(token, 0);
    if (entry) {
        g_dluma_qg.pending_document_bytes -= entry->open_size + entry->update_size;
        if (entry->open_frame) dluma_free(entry->open_frame);
        if (entry->update_frame) dluma_free(entry->update_frame);
        entry->open_frame = entry->update_frame = 0;
        entry->open_size = entry->update_size = 0;
        entry->retired = 1U;
        if (!entry->saved_frame) memset(entry, 0, sizeof(*entry));
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
}

static int dluma_qg_queue_document(char* frame, DWORD size, ULONGLONG token, int mode)
{
    DlumaQgPendingDocument* entry;
    DWORD retired;
    unsigned int i;
    int accepted = 0;
    if (!frame || !size || size > DLUMA_QG_MAX_FRAME_BYTES || !token) return 0;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    entry = dluma_qg_pending_document(token, 1);
    if (!entry || entry->retired || (mode != 1 && !entry->admitted)) goto done;
    /* Only the latest explicit graph activation needs an unsent open. Known
     * documents retain their incremental queues and are resynced on activation. */
    if (mode == 1) for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
        DlumaQgPendingDocument* other = &g_dluma_qg.pending_documents[i];
        if (other != entry && other->open_frame) {
            g_dluma_qg.pending_document_bytes -= other->open_size;
            dluma_free(other->open_frame); other->open_frame = 0; other->open_size = 0;
        }
    }
    retired = mode == 1 ? entry->open_size + entry->update_size :
        mode == 2 ? entry->saved_size : entry->update_size;
    if (g_dluma_qg.pending_document_bytes - retired + size > 3U * DLUMA_QG_MAX_FRAME_BYTES)
        goto done;
    if (mode == 1) {
        if (entry->open_frame) dluma_free(entry->open_frame);
        if (entry->update_frame) dluma_free(entry->update_frame);
        entry->open_frame = frame; entry->open_size = size;
        entry->update_frame = 0; entry->update_size = 0;
    } else if (mode == 2) {
        if (entry->saved_frame) dluma_free(entry->saved_frame);
        entry->saved_frame = frame; entry->saved_size = size;
    } else {
        if (entry->update_frame) dluma_free(entry->update_frame);
        entry->update_frame = frame; entry->update_size = size;
    }
    g_dluma_qg.pending_document_bytes = g_dluma_qg.pending_document_bytes - retired + size;
    accepted = 1;
done:
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    return accepted;
}

static void dluma_qg_publish_source_changed(DlumaQgDialog* slot)
{
    char* source = 0;
    DWORD source_size = 0;
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    char* frame = 0;
    DWORD frame_size = 0;
    int admitted;
    if (!slot || !slot->document_id[0] || !dluma_qg_native_identity_matches(slot) ||
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
    if (!slot->panel.token) { dluma_free(source); return; }
    dluma_qg_lock(&g_dluma_qg.output_lock);
    {
        DlumaQgPendingDocument* entry = dluma_qg_pending_document(slot->panel.token, 0);
        admitted = entry && entry->admitted;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (!admitted && slot != g_dluma_qg.panel_dialog) { dluma_free(source); return; }
    if (admitted && dluma_qg_build_source_changed_frame(
            slot, source, source_size, &frame, &frame_size)) {
        if (dluma_qg_queue_document(frame, frame_size, slot->panel.token, 0)) {
            dluma_free(source); return;
        }
        dluma_free(frame); frame = 0;
    }
    /* A new or reconnected companion first receives the complete latest source. */
    if (dluma_qg_build_open_frame(slot, source, source_size, &frame, &frame_size) &&
        !dluma_qg_queue_document(frame, frame_size, slot->panel.token, 1)) dluma_free(frame);
    dluma_free(source);
}

static void dluma_qg_saved_source_clear(DlumaQgDialog* slot)
{
    if (slot->save_source) dluma_free(slot->save_source);
    slot->save_source = 0;
    slot->save_source_size = 0;
    slot->save_source_revision = 0;
    slot->save_source_sha256[0] = 0;
    slot->save_source_encoding[0] = 0;
}

static int dluma_qg_document_admitted(DlumaQgDialog* slot)
{
    DlumaQgPendingDocument* entry;
    int admitted;
    if (!slot || !slot->panel.token) return 0;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    entry = dluma_qg_pending_document(slot->panel.token, 0);
    admitted = entry && entry->admitted;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    return admitted;
}

static void dluma_qg_capture_saved_source(DlumaQgDialog* slot)
{
    char* source = 0;
    DWORD size = 0;
    char sha[DLUMA_SHA256_HEX_CAPACITY];
    char encoding[32];
    if (!dluma_qg_document_admitted(slot) || !dluma_qg_native_identity_matches(slot) ||
        !dluma_qg_scintilla_read(slot->scintilla, &source, &size)) return;
    if (!dluma_qg_scintilla_encoding(slot->scintilla, encoding, DLUMA_ARRAY_COUNT(encoding))) {
        dluma_free(source); return;
    }
    dluma_sha256_bytes_hex(source, size, sha);
    if (dluma_stricmp(sha, slot->source_sha256)) {
        ++slot->revision;
        dluma_str_copy(slot->source_sha256, DLUMA_ARRAY_COUNT(slot->source_sha256), sha);
    }
    dluma_qg_saved_source_clear(slot);
    slot->save_source = source;
    slot->save_source_size = size;
    slot->save_source_revision = slot->revision;
    dluma_str_copy(slot->save_source_sha256, DLUMA_ARRAY_COUNT(slot->save_source_sha256), sha);
    dluma_str_copy(slot->save_source_encoding, DLUMA_ARRAY_COUNT(slot->save_source_encoding), encoding);
    /* Leave the stock save handler before checking its disk publication. */
    if (!SetTimer(slot->dialog, (UINT_PTR)DLUMA_QG_SAVE_TIMER_BASE +
            (UINT_PTR)(slot - g_dluma_qg.dialogs), 10U, 0))
        dluma_qg_saved_source_clear(slot);
}

static void dluma_qg_publish_saved_source(DlumaQgDialog* slot)
{
    char disk_sha[DLUMA_SHA256_HEX_CAPACITY];
    char* frame = 0;
    DWORD frame_size = 0;
    if (!slot || !slot->save_source) return;
    if (!dluma_qg_document_admitted(slot) || !dluma_qg_native_identity_matches(slot) ||
        !dluma_project_regular_file_is_contained(slot->project_root, slot->source_path) ||
        !dluma_sha256_file_hex(slot->source_path, DLUMA_QG_MAX_SOURCE_BYTES, disk_sha) ||
        dluma_stricmp(disk_sha, slot->save_source_sha256)) {
        dluma_qg_saved_source_clear(slot); return;
    }
    slot->saved_revision = slot->save_source_revision;
    dluma_str_copy(slot->saved_sha256, DLUMA_ARRAY_COUNT(slot->saved_sha256), disk_sha);
    if (dluma_qg_build_source_frame(slot, slot->save_source, slot->save_source_size,
            "sourceSaved", slot->save_source_revision, slot->save_source_sha256,
            slot->save_source_encoding, slot->source_path, &frame, &frame_size)) {
        if (!dluma_qg_queue_document(frame, frame_size, slot->panel.token, 2)) {
            dluma_free(frame);
            if (SetTimer(slot->dialog, (UINT_PTR)DLUMA_QG_SAVE_TIMER_BASE +
                    (UINT_PTR)(slot - g_dluma_qg.dialogs), 100U, 0)) return;
        }
    }
    dluma_qg_saved_source_clear(slot);
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

static int dluma_qg_generated_catalog_root(WCHAR* output, unsigned int capacity)
{
    WCHAR module_path[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity identity;
    if (!dluma_get_module_path_checked(g_dluma.self_module,
            module_path, DLUMA_ARRAY_COUNT(module_path))) return 0;
    dluma_path_parent(module_path);
    if (!dluma_checked_path_join(output, capacity, module_path, L"dluma_runtime\\editor") ||
        !dluma_open_directory_identity(output, &identity)) return 0;
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
    WCHAR generated_root[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    char project_utf8[DLUMA_MAX_WPATH * 4U];
    char installed_utf8[DLUMA_MAX_WPATH * 4U];
    char generated_utf8[DLUMA_MAX_WPATH * 4U];
    char context_generation[32];
    int have_project;
    int have_installed;
    int have_generated;
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
    have_generated = dluma_qg_generated_catalog_root(generated_root, DLUMA_ARRAY_COUNT(generated_root)) &&
        dluma_wide_to_utf8(generated_root, generated_utf8, DLUMA_ARRAY_COUNT(generated_utf8));
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
        (have_generated &&
            ((have_installed && !dluma_qg_json_append(output, capacity, &used, ",")) ||
             !dluma_qg_json_append(output, capacity, &used, "\"") ||
             !dluma_qg_json_escape(output, capacity, &used, generated_utf8) ||
             !dluma_qg_json_append(output, capacity, &used, "\""))) ||
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

static void dluma_qg_compile_time_json(const FILETIME* time, char output[32])
{
    SYSTEMTIME utc;
    unsigned int values[7];
    static const unsigned int offsets[7] = {1U, 6U, 9U, 12U, 15U, 18U, 21U};
    static const unsigned int widths[7] = {4U, 2U, 2U, 2U, 2U, 2U, 3U};
    unsigned int index;
    dluma_str_copy(output, 32U, "null");
    if ((!time->dwHighDateTime && !time->dwLowDateTime) ||
        !FileTimeToSystemTime(time, &utc) || utc.wYear > 9999U)
        return;
    dluma_str_copy(output, 32U, "\"0000-00-00T00:00:00.000Z\"");
    values[0] = utc.wYear; values[1] = utc.wMonth; values[2] = utc.wDay;
    values[3] = utc.wHour; values[4] = utc.wMinute; values[5] = utc.wSecond;
    values[6] = utc.wMilliseconds;
    for (index = 0; index < 7U; ++index) {
        unsigned int remaining = widths[index];
        while (remaining) {
            --remaining;
            output[offsets[index] + remaining] = (char)('0' + values[index] % 10U);
            values[index] /= 10U;
        }
    }
}

static DWORD dluma_qg_compile_response(
    char* output, DWORD capacity, const char* request_id,
    const DlumaQuestEditorCompileReceipt* receipt)
{
    DWORD used = 0;
    char last_write[32];
    const char* message = receipt->output_verified && receipt->compiler_returned
        ? "Quest output updated."
        : receipt->matched_callbacks == 1U && receipt->verification_failed
        ? "Compilation failed."
        : !receipt->callback_count
        ? "The Editor did not confirm compilation."
        : "Compilation could not be confirmed.";
    dluma_str_copy(last_write, DLUMA_ARRAY_COUNT(last_write), "null");
    if (receipt->output_verified)
        dluma_qg_compile_time_json(&receipt->output_last_write, last_write);
    if (!dluma_qg_response_begin(
            output, capacity, &used, request_id, "result") ||
        !dluma_qg_json_append(
            output, capacity, &used,
            "{\"receipt\":{\"saveAttempted\":true,"
            "\"saveSucceeded\":true,\"stockCommandDispatched\":true,"
            "\"stockCommandCompleted\":") ||
        !dluma_qg_json_append(output, capacity, &used,
            receipt->compiler_returned ? "true," : "false,") ||
        !dluma_qg_json_append(output, capacity, &used,
            "\"diagnosticsAvailable\":false,\"diagnostics\":[],"
            "\"outputSha256\":") ||
        (receipt->output_verified &&
            !dluma_qg_json_append(output, capacity, &used, "\"")) ||
        !dluma_qg_json_append(output, capacity, &used,
            receipt->output_verified ? receipt->output_sha256 : "null") ||
        (receipt->output_verified &&
            !dluma_qg_json_append(output, capacity, &used, "\"")) ||
        !dluma_qg_json_append(output, capacity, &used, ",\"outputLastWriteUtc\":") ||
        !dluma_qg_json_append(output, capacity, &used, last_write) ||
        !dluma_qg_json_append(output, capacity, &used, ",\"customSidecarPublished\":") ||
        !dluma_qg_json_append(output, capacity, &used,
            receipt->custom_sidecar_published ? "true," : "false,") ||
        !dluma_qg_json_append(output, capacity, &used, "\"message\":\"") ||
        !dluma_qg_json_append(output, capacity, &used, message) ||
        !dluma_qg_json_append(output, capacity, &used, "\"}}}"))
        return 0;
    return used;
}

static void dluma_qg_pipe_diagnostic(const char* stage, DWORD error, int failed)
{
    DWORD saved_error = GetLastError();
    const char* level = failed ? "WARN" : "INFO";
    dluma_log_pair(level, "editor_quest_graph_pipe_stage", stage);
    if (error) dluma_log_hex(level, "editor_quest_graph_pipe_error", error);
    SetLastError(saved_error);
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
        {
            DWORD error = GetLastError();
            if (error != ERROR_NO_DATA) {
                dluma_qg_pipe_diagnostic("write_file_failed", error, 1);
                return 0;
            }
        }
        Sleep(DLUMA_QG_PIPE_POLL_MS);
    }
    if (total != size) dluma_qg_pipe_diagnostic("write_timeout", 0, 1);
    return total == size;
}

static int dluma_qg_write_frame(
    HANDLE pipe, const char* json, DWORD size)
{
    BYTE prefix[4];
    if (!json || !size || size > DLUMA_QG_MAX_FRAME_BYTES) {
        dluma_qg_pipe_diagnostic("write_frame_invalid", 0, 1);
        return 0;
    }
    prefix[0] = (BYTE)(size & 0xFFU);
    prefix[1] = (BYTE)((size >> 8) & 0xFFU);
    prefix[2] = (BYTE)((size >> 16) & 0xFFU);
    prefix[3] = (BYTE)((size >> 24) & 0xFFU);
    return dluma_qg_write_exact(pipe, prefix, sizeof(prefix)) &&
        dluma_qg_write_exact(pipe, json, size);
}

static int dluma_qg_dispatch_panel_ready(HWND parent, HWND child,
    ULONGLONG token, ULONGLONG* generation, char* error, unsigned int capacity)
{
    DlumaQgUiRequest* request = &g_dluma_qg.ui_request;
    ULONG_PTR result = 0;
    int delivered;
    if (dluma_atomic_compare_exchange(&request->state, -1, 2) == 2) {
        dluma_qg_ui_payload_free(request);
        memset((void*)request, 0, sizeof(*request));
    }
    if (dluma_atomic_compare_exchange(&request->state, -1, 0) != 0) {
        dluma_str_copy(error, capacity, "ui_dispatch_busy");
        return 0;
    }
    memset((void*)request, 0, sizeof(*request));
    request->state = -1;
    request->kind = DLUMA_QG_UI_PANEL_READY;
    request->panel_parent = parent;
    request->panel_child = child;
    request->panel_token = token;
    dluma_atomic_exchange(&request->state, 1);
    delivered = SendMessageTimeoutW(g_dluma_qg.main_window,
        DLUMA_QG_MAIN_DISPATCH, 0, 0,
        DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG | DLUMA_SMTO_ERRORONEXIT,
        30000U, &result) != 0;
    if (dluma_atomic_compare_exchange(&request->state, 0, 0) != 2) {
        dluma_str_copy(error, capacity, "ui_dispatch_failed");
        return 0;
    }
    *generation = request->panel_generation;
    dluma_str_copy(error, capacity, request->error);
    dluma_atomic_exchange(&request->state, 0);
    return delivered && result != 0;
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
    DlumaQuestEditorCompileReceipt* compile_receipt,
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
        (kind == DLUMA_QG_UI_COMPILE && !compile_receipt) ||
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
    if (compile_receipt) *compile_receipt = request->compile_receipt;
    dluma_qg_ui_payload_free(request);
    dluma_atomic_exchange(&request->state, 0);
    return delivered && result != 0;
invalid:
    dluma_qg_edits_free(edits, edit_count);
    if (restore_source) dluma_free(restore_source);
    return 0;
}

#include "dlw_editor_quest_graph_close.inc"

static int dluma_qg_close_payload_valid(const char* payload, DWORD payload_size)
{
    DlumaQgCloseReply reply;
    return dluma_qg_close_parse_reply(payload, payload_size, &reply);
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
    DlumaQuestEditorCompileReceipt compile_receipt;
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
    if (!json || !size || size > DLUMA_QG_MAX_FRAME_BYTES) {
        dluma_qg_pipe_diagnostic("request_frame_invalid", 0, 1);
        return 0;
    }
    json[size] = 0;
    type[0] = request_id[0] = token[0] = disk_path[0] = 0;
    if (!dluma_qg_parse_envelope(
            json, size,
            type, (unsigned int)sizeof(type),
            request_id, (unsigned int)sizeof(request_id),
            token, (unsigned int)sizeof(token),
            &payload, &payload_size)) {
        dluma_qg_pipe_diagnostic("envelope_invalid", 0, 1);
        return 0;
    }
    if (!dluma_qg_validate_payload(type, payload, payload_size)) {
        dluma_qg_pipe_diagnostic(
            strcmp(type, "hello") == 0 ? "hello_payload_invalid" :
            strcmp(type, "panelReady") == 0 ? "panel_ready_payload_invalid" :
            strcmp(type, "getContext") == 0 ? "get_context_payload_invalid" :
                "other_payload_invalid", 0, 1);
        return 0;
    }
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
            dluma_qg_pipe_diagnostic("hello_identity_rejected", 0, 1);
            response_size = dluma_qg_error_response(
                response, sizeof(response), request_id,
                "hello_identity_mismatch", 0);
            ok = response_size && dluma_qg_write_frame(pipe, response, response_size);
            if (!ok) dluma_qg_pipe_diagnostic(response_size ?
                "hello_rejection_write_failed" : "hello_rejection_build_failed", 0, 1);
            return ok;
        }
        dluma_atomic_exchange(&g_dluma_qg.authenticated, 1);
        dluma_qg_pipe_diagnostic("hello_identity_accepted", 0, 0);
        response_size = dluma_qg_hello_response(
            response, sizeof(response), request_id);
        ok = response_size && dluma_qg_write_frame(pipe, response, response_size);
        if (!ok) dluma_qg_pipe_diagnostic(response_size ?
            "hello_response_write_failed" : "hello_response_build_failed", 0, 1);
        return ok;
    }
    if (!dluma_atomic_compare_exchange(
            &g_dluma_qg.authenticated, 0, 0)) {
        dluma_qg_pipe_diagnostic("request_before_authentication", 0, 1);
        return 0;
    }
    if (strcmp(type, "panelReady") == 0) {
        ULONGLONG parent = 0, child = 0, panel_token = 0, generation = 0;
        DWORD used = 0;
        char generation_text[32];
        error[0] = 0;
        ok = dluma_qg_json_u64(json, "parentWindow", &parent) &&
            dluma_qg_json_u64(json, "childWindow", &child) &&
            dluma_qg_json_u64(json, "panelToken", &panel_token) &&
            dluma_qg_dispatch_panel_ready((HWND)(ULONG_PTR)parent,
                (HWND)(ULONG_PTR)child, panel_token, &generation,
                error, DLUMA_ARRAY_COUNT(error));
        if (!ok) dluma_qg_pipe_diagnostic("panel_ready_refused", 0, 1);
        if (!dluma_u64_to_text(generation, generation_text,
                DLUMA_ARRAY_COUNT(generation_text)) ||
            !dluma_qg_response_begin(response, sizeof(response), &used, request_id, "result") ||
            !dluma_qg_json_append(response, sizeof(response), &used,
                ok ? "{\"bound\":true,\"bindingGeneration\":" :
                     "{\"bound\":false,\"bindingGeneration\":") ||
            !dluma_qg_json_append(response, sizeof(response), &used, generation_text) ||
            !dluma_qg_json_append(response, sizeof(response), &used, ",\"message\":\"") ||
            !dluma_qg_json_escape(response, sizeof(response), &used,
                ok ? "Graph panel connected." : error[0] ? error : "Graph panel binding failed.") ||
            !dluma_qg_json_append(response, sizeof(response), &used, "\"}}")) {
            dluma_qg_pipe_diagnostic("panel_ready_response_build_failed", 0, 1);
            return 0;
        }
        ok = dluma_qg_write_frame(pipe, response, used);
        if (!ok) dluma_qg_pipe_diagnostic("panel_ready_response_write_failed", 0, 1);
        return ok;
    }
    if (strcmp(type, "getContext") == 0) {
        response_size = dluma_qg_context_response(
            response, sizeof(response), request_id);
        ok = response_size && dluma_qg_write_frame(pipe, response, response_size);
        if (!ok) dluma_qg_pipe_diagnostic(response_size ?
            "get_context_response_write_failed" : "get_context_response_build_failed", 0, 1);
        return ok;
    }
    if (strcmp(type, "completeClose") == 0) {
        DlumaQgCloseReply reply;
        ok = dluma_qg_close_parse_reply(json, payload_size, &reply) &&
            dluma_qg_close_accept_reply(&reply);
        response_size = ok
            ? dluma_qg_bridge_result_response(response, sizeof(response), request_id, "Close decision accepted.")
            : dluma_qg_error_response(response, sizeof(response), request_id, "close_request_not_pending", 0);
        return response_size && dluma_qg_write_frame(pipe, response, response_size);
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
                0,
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
            0, 0, 0, error, sizeof(error));
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
            0,
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
        strcmp(type, "compileDocument") == 0 ? &compile_receipt : 0,
        error, sizeof(error));
    if (ok) {
        response_size = dluma_stricmp(type, "compileDocument") == 0
            ? dluma_qg_compile_response(
                response, sizeof(response), request_id, &compile_receipt)
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
    {
        DWORD error = GetLastError();
        if (error == ERROR_NO_DATA) return 1;
        dluma_qg_pipe_diagnostic("read_file_failed", error, 1);
    }
    return 0;
}

static int dluma_qg_send_pending(HANDLE pipe)
{
    char* panel_frame = 0;
    char* open_frame = 0;
    char* update_frame = 0;
    char* saved_frame = 0;
    DWORD panel_size = 0, open_size = 0, update_size = 0, saved_size = 0;
    ULONGLONG token = 0, epoch;
    unsigned int i;
    int result = 1;
    if (!dluma_atomic_compare_exchange(&g_dluma_qg.authenticated, 0, 0)) return 1;
    dluma_qg_lock(&g_dluma_qg.output_lock);
    epoch = g_dluma_qg.companion_epoch;
    panel_frame = g_dluma_qg.pending_panel_frame;
    panel_size = g_dluma_qg.pending_panel_frame_size;
    g_dluma_qg.pending_panel_frame = 0;
    g_dluma_qg.pending_panel_frame_size = 0;
    for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
        unsigned int index = (g_dluma_qg.pending_document_cursor + i) % DLUMA_QG_MAX_PENDING_DOCUMENTS;
        DlumaQgPendingDocument* entry = &g_dluma_qg.pending_documents[index];
        if (!entry->open_frame && !entry->update_frame && !entry->saved_frame) continue;
        token = entry->token;
        open_frame = entry->open_frame; open_size = entry->open_size;
        update_frame = entry->update_frame; update_size = entry->update_size;
        saved_frame = entry->saved_frame; saved_size = entry->saved_size;
        entry->open_frame = entry->update_frame = entry->saved_frame = 0;
        entry->open_size = entry->update_size = entry->saved_size = 0;
        g_dluma_qg.pending_document_bytes -= open_size + update_size + saved_size;
        if (entry->retired) memset(entry, 0, sizeof(*entry));
        g_dluma_qg.pending_document_cursor = (index + 1U) % DLUMA_QG_MAX_PENDING_DOCUMENTS;
        break;
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    if (panel_frame) { result = dluma_qg_write_frame(pipe, panel_frame, panel_size); dluma_free(panel_frame); }
    if (open_frame) {
        if (result) result = dluma_qg_write_frame(pipe, open_frame, open_size);
        dluma_free(open_frame);
        if (result) {
            DlumaQgPendingDocument* entry;
            dluma_qg_lock(&g_dluma_qg.output_lock);
            entry = dluma_qg_pending_document(token, 0);
            if (entry && epoch == g_dluma_qg.companion_epoch) entry->admitted = 1U;
            dluma_qg_unlock(&g_dluma_qg.output_lock);
        }
    }
    if (saved_frame) {
        if (result) result = dluma_qg_write_frame(pipe, saved_frame, saved_size);
        dluma_free(saved_frame);
    }
    if (update_frame) {
        if (result) result = dluma_qg_write_frame(pipe, update_frame, update_size);
        dluma_free(update_frame);
    }
    return result;
}

static int dluma_qg_connected_loop(HANDLE pipe)
{
    const char* exit_stage = "stop_requested";
    BYTE prefix[4];
    DWORD prefix_used = 0;
    char* frame = 0;
    DWORD frame_size = 0;
    DWORD frame_used = 0;
    dluma_atomic_exchange(&g_dluma_qg.connected, 1);
    dluma_atomic_exchange(&g_dluma_qg.authenticated, 0);
    dluma_qg_lock(&g_dluma_qg.output_lock);
    {
        unsigned int i;
        for (i = 0; i < DLUMA_QG_MAX_PENDING_DOCUMENTS; ++i) {
            DlumaQgPendingDocument* entry = &g_dluma_qg.pending_documents[i];
            entry->admitted = 0;
            if (entry->update_frame) {
                g_dluma_qg.pending_document_bytes -= entry->update_size;
                dluma_free(entry->update_frame); entry->update_frame = 0; entry->update_size = 0;
            }
            if (entry->saved_frame) {
                g_dluma_qg.pending_document_bytes -= entry->saved_size;
                dluma_free(entry->saved_frame); entry->saved_frame = 0; entry->saved_size = 0;
            }
            if (entry->retired) memset(entry, 0, sizeof(*entry));
        }
    }
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    for (;;) {
        if (WaitForSingleObject(g_dluma_qg.stop_event, 0) == WAIT_OBJECT_0)
            break;
        if (!dluma_qg_send_pending(pipe)) {
            exit_stage = "pending_send_failed"; break;
        }
        if (!dluma_qg_send_control_pending(pipe)) {
            exit_stage = "control_send_failed"; break;
        }
        if (prefix_used < sizeof(prefix)) {
            if (!dluma_qg_read_available(
                    pipe, prefix, sizeof(prefix), &prefix_used)) {
                exit_stage = "prefix_read_failed";
                break;
            }
            if (prefix_used < sizeof(prefix)) {
                Sleep(DLUMA_QG_PIPE_POLL_MS);
                continue;
            }
            frame_size = (DWORD)prefix[0] |
                ((DWORD)prefix[1] << 8) |
                ((DWORD)prefix[2] << 16) |
                ((DWORD)prefix[3] << 24);
            if (!frame_size || frame_size > DLUMA_QG_MAX_FRAME_BYTES) {
                exit_stage = "frame_size_invalid";
                break;
            }
            frame = (char*)dluma_alloc((SIZE_T)frame_size + 1U);
            if (!frame) { exit_stage = "frame_allocation_failed"; break; }
            frame_used = 0;
        }
        if (!dluma_qg_read_available(
                pipe, (BYTE*)frame, frame_size, &frame_used)) {
            exit_stage = "body_read_failed";
            break;
        }
        if (frame_used == frame_size) {
            if (!dluma_qg_handle_json(pipe, frame, frame_size)) {
                exit_stage = "request_handler_failed"; break;
            }
            dluma_free(frame);
            frame = 0;
            frame_size = frame_used = prefix_used = 0;
            continue;
        }
        Sleep(DLUMA_QG_PIPE_POLL_MS);
    }
    dluma_qg_pipe_diagnostic(exit_stage, 0, strcmp(exit_stage, "stop_requested") != 0);
    if (frame) dluma_free(frame);
    dluma_atomic_exchange(&g_dluma_qg.authenticated, 0);
    dluma_atomic_exchange(&g_dluma_qg.connected, 0);
    dluma_qg_close_reset();
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

static int dluma_qg_queue_panel_attach(const DlumaEditorPanelRef* panel)
{
    char* json = (char*)dluma_alloc(768U);
    char parent[32], token[32], request_id[33];
    DWORD used = 0;
    if (!json || !panel || !panel->token || !panel->content_window ||
        !dluma_u64_to_text((ULONGLONG)(ULONG_PTR)panel->content_window, parent, sizeof(parent)) ||
        !dluma_u64_to_text(panel->token, token, sizeof(token))) {
        if (json) dluma_free(json);
        return 0;
    }
    memcpy(request_id, g_dluma_qg.token, 32U); request_id[32] = 0;
    json[0] = 0;
    if (!dluma_qg_response_begin(json, 768U, &used, request_id, "attachPanel") ||
        !dluma_qg_json_append(json, 768U, &used, "{\"parentWindow\":") ||
        !dluma_qg_json_append(json, 768U, &used, parent) ||
        !dluma_qg_json_append(json, 768U, &used, ",\"panelToken\":") ||
        !dluma_qg_json_append(json, 768U, &used, token) ||
        !dluma_qg_json_append(json, 768U, &used, "}}")) {
        dluma_free(json); return 0;
    }
    dluma_qg_lock(&g_dluma_qg.output_lock);
    if (g_dluma_qg.pending_panel_frame) dluma_free(g_dluma_qg.pending_panel_frame);
    g_dluma_qg.pending_panel_frame = json;
    g_dluma_qg.pending_panel_frame_size = used;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
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
    char panel_parent[32], panel_token[32];
    WCHAR panel_value[32];
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
    if (!g_dluma_qg.panel_dialog || !g_dluma_qg.panel_dialog->panel.token ||
        !dluma_u64_to_text((ULONGLONG)(ULONG_PTR)g_dluma_qg.panel_dialog->panel.content_window,
            panel_parent, sizeof(panel_parent)) ||
        !dluma_u64_to_text(g_dluma_qg.panel_dialog->panel.token, panel_token, sizeof(panel_token)))
        return 0;
    dluma_wstr_append(command, DLUMA_ARRAY_COUNT(command), L" --panel-parent ");
    if (!dluma_utf8_to_wide(panel_parent, panel_value, DLUMA_ARRAY_COUNT(panel_value)) ||
        !dluma_qg_append_quoted(command, DLUMA_ARRAY_COUNT(command), panel_value)) return 0;
    dluma_wstr_append(command, DLUMA_ARRAY_COUNT(command), L" --panel-token ");
    if (!dluma_utf8_to_wide(panel_token, panel_value, DLUMA_ARRAY_COUNT(panel_value)) ||
        !dluma_qg_append_quoted(command, DLUMA_ARRAY_COUNT(command), panel_value)) return 0;
    if (g_dluma.config_path[0]) {
        dluma_wstr_append(command, DLUMA_ARRAY_COUNT(command), L" --theme-config ");
        if (!dluma_qg_append_quoted(command, DLUMA_ARRAY_COUNT(command), g_dluma.config_path))
            return 0;
    }
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
    dluma_qg_lock(&g_dluma_qg.output_lock);
    ++g_dluma_qg.companion_epoch;
    dluma_qg_clear_pending_documents();
    if (g_dluma_qg.pending_panel_frame) dluma_free(g_dluma_qg.pending_panel_frame);
    g_dluma_qg.pending_panel_frame = 0; g_dluma_qg.pending_panel_frame_size = 0;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
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
    (void)KillTimer(slot->dialog, (UINT_PTR)DLUMA_QG_SAVE_TIMER_BASE +
        (UINT_PTR)(slot - g_dluma_qg.dialogs));
    dluma_qg_publish_saved_source(slot);
    dluma_qg_saved_source_clear(slot);
    if (!slot->panel.token || !IsWindow(slot->panel.content_window)) {
        MessageBoxW(slot->dialog, L"The graph panel could not be created.",
            L"Visual Quest Editor", 0x30U);
        return;
    }
    if (!dluma_qg_scintilla_read(
            slot->scintilla, &source, &source_size) ||
        !dluma_qg_resolve_document(slot, source, source_size)) {
        if (source) dluma_free(source);
        (void)dluma_editor_panel_host_show(&slot->panel, 0);
        MessageBoxW(
            slot->dialog,
            L"The native quest source could not be resolved to an editable file in its project. Close and reopen the source, then try again.",
            L"Visual Quest Editor", 0x30U);
        return;
    }
    if (!dluma_qg_close_install()) {
        dluma_free(source);
        (void)dluma_editor_panel_host_show(&slot->panel, 0);
        MessageBoxW(slot->dialog, L"The graph could not be opened safely. Try again or restart Developer Tools.",
            L"Visual Quest Editor", 0x30U);
        return;
    }
    if (g_dluma_qg.panel_dialog && g_dluma_qg.panel_dialog != slot) {
        (void)dluma_editor_panel_host_unbind(&g_dluma_qg.panel_dialog->panel);
        (void)dluma_editor_panel_host_show(&g_dluma_qg.panel_dialog->panel, 0);
    }
    g_dluma_qg.panel_dialog = slot;
    if (!dluma_qg_session_start() ||
        !dluma_qg_queue_panel_attach(&slot->panel) ||
        !dluma_qg_build_open_frame(
            slot, source, source_size, &frame, &frame_size) ||
        !dluma_qg_queue_document(frame, frame_size, slot->panel.token, 1)) {
        if (frame) dluma_free(frame);
        dluma_free(source);
        (void)dluma_editor_panel_host_show(&slot->panel, 0);
        MessageBoxW(
            slot->dialog,
            L"The packaged visual quest editor or its authenticated local bridge is unavailable. Stock PXSL editing remains unchanged.",
            L"Visual Quest Editor", 0x10U);
        return;
    }
    dluma_free(source);
}

static void dluma_qg_panel_notify(const DlumaEditorPanelRef* panel,
    unsigned int event, unsigned int argument, void* context)
{
    DlumaQgDialog* slot = (DlumaQgDialog*)context;
    if (!slot || slot->panel.token != panel->token) return;
    if (event == DLUMA_EDITOR_PANEL_VISIBILITY && argument &&
        !dluma_atomic_compare_exchange(&g_dluma_qg.stopping, 0, 0))
        dluma_qg_open_dialog(slot);
    if (event == DLUMA_EDITOR_PANEL_CHILD_EXITED) {
        (void)dluma_editor_panel_host_show(&slot->panel, 0);
        dluma_log_line("WARN", "editor_quest_graph_companion_exited");
    }
    if (event == DLUMA_EDITOR_PANEL_CLOSING) {
        dluma_qg_retire_document(panel->token);
        if (g_dluma_qg.panel_dialog == slot) g_dluma_qg.panel_dialog = 0;
    }
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
    /* A nested native message pump must not execute the same request again. */
    if (dluma_atomic_compare_exchange(&request->state, 3, 1) != 1)
        return;
    request->error[0] = 0;
    if (request->kind == DLUMA_QG_UI_PANEL_READY) {
        slot = g_dluma_qg.panel_dialog;
        if (!slot || !slot->dialog || !IsWindow(slot->dialog) ||
            slot->panel.token != request->panel_token ||
            slot->panel.content_window != request->panel_parent ||
            !g_dluma_qg.companion_process || !g_dluma_qg.companion_process_id ||
            !dluma_editor_panel_host_bind(&slot->panel, request->panel_child,
                g_dluma_qg.companion_process_id, g_dluma_qg.companion_process)) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "panel_identity_unavailable");
            goto done;
        }
        request->panel_generation = dluma_editor_panel_host_binding_generation(&slot->panel);
        accepted = request->panel_generation != 0;
        if (accepted) dluma_log_line("INFO", "editor_quest_graph_panel_bound");
        if (!accepted) dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
            "panel_binding_generation_unavailable");
        goto done;
    }
    slot = dluma_qg_document_find(request->document_id);
    if (!slot || !slot->dialog || !slot->scintilla) {
        dluma_str_copy(request->error,
                     DLUMA_ARRAY_COUNT(request->error),
                     "document_not_active");
        goto done;
    }
    if (!dluma_qg_native_identity_matches(slot)) {
        dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
            "native_document_identity_changed");
        goto done;
    }
    if (request->kind == DLUMA_QG_UI_ACTIVATE) {
        if (slot->panel.token) (void)dluma_editor_panel_host_show(&slot->panel, 0);
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
        if (!request->restore_was_modified &&
            (!dluma_project_regular_file_is_contained(slot->project_root, slot->source_path) ||
             !dluma_sha256_file_hex(slot->source_path, DLUMA_QG_MAX_SOURCE_BYTES, disk_sha) ||
             dluma_stricmp(disk_sha, current_sha))) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "saved_baseline_changed");
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
        ULONGLONG observation;
        HWND compile_control;
        int dispatched = 0;
        int observed = 0;
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
        compile_control = dluma_qg_stock_compile_control(slot);
        if (!g_dluma_qg.main_subscribed || !g_dluma_qg.main_window ||
            !compile_control) {
            dluma_str_copy(request->error,
                         DLUMA_ARRAY_COUNT(request->error),
                         "stock_compile_route_unavailable");
            goto done;
        }
        observation = dluma_quest_editor_compile_observe_begin(
            slot->project_root, slot->source_path, current_sha);
        if (!observation) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "compile_observation_unavailable");
            goto done;
        }
        __try {
            if (dluma_qg_stock_compile_control(slot) == compile_control) {
                /* Deliver the stock button notification to its Source owner. */
                (void)SendMessageW(slot->dialog, WM_COMMAND,
                    (WPARAM)DLUMA_QG_COMPILE_COMMAND, (LPARAM)compile_control);
                dispatched = 1;
            }
        } __finally {
            observed = dluma_quest_editor_compile_observe_end(
                observation, &request->compile_receipt);
        }
        dluma_log_u64("INFO", "quest_graph_compile_observation", observation);
        dluma_log_u64("INFO", "quest_graph_compile_callbacks",
            request->compile_receipt.callback_count);
        dluma_log_u64("INFO", "quest_graph_compile_matches",
            request->compile_receipt.matched_callbacks);
        dluma_log_u64("INFO", "quest_graph_compile_nested_callbacks",
            request->compile_receipt.nested_callbacks);
        dluma_log_u64("INFO", "quest_graph_compile_returned",
            request->compile_receipt.compiler_returned);
        dluma_log_u64("INFO", "quest_graph_compile_output_verified",
            request->compile_receipt.output_verified);
        if (!dispatched) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "stock_compile_route_unavailable");
            goto done;
        }
        if (!observed || !dluma_qg_native_identity_matches(slot) ||
            request->expected_revision != slot->revision ||
            strcmp(request->document_id, slot->document_id) ||
            !dluma_qg_scintilla_read(slot->scintilla, &updated, &updated_size)) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "source_changed_during_compile");
            goto done;
        }
        dluma_sha256_bytes_hex(updated, updated_size, disk_sha);
        if (dluma_stricmp(current_sha, disk_sha)) {
            dluma_str_copy(request->error, DLUMA_ARRAY_COUNT(request->error),
                "source_changed_during_compile");
            goto done;
        }
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

static LRESULT CALLBACK dluma_qg_dialog_proc_impl(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DlumaQgDialog* slot = dluma_qg_dialog_find(window);
    WNDPROC predecessor = slot ? slot->predecessor : 0;
    UINT_PTR source_timer = slot
        ? (UINT_PTR)DLUMA_QG_SOURCE_TIMER_BASE +
            (UINT_PTR)(slot - g_dluma_qg.dialogs)
        : 0;
    UINT_PTR save_timer = slot
        ? (UINT_PTR)DLUMA_QG_SAVE_TIMER_BASE + (UINT_PTR)(slot - g_dluma_qg.dialogs)
        : 0;
    int savepoint_reached = 0;
    LRESULT result;
    if (slot && message == WM_COMMAND &&
        (UINT)(wparam & 0xFFFFU) == DLUMA_QG_BUTTON_CONTROL) {
        if (slot->panel.token) (void)dluma_editor_panel_host_show(&slot->panel, 1);
        return 0;
    }
    if (slot && message == WM_TIMER &&
        (UINT_PTR)wparam == source_timer) {
        (void)KillTimer(window, source_timer);
        dluma_qg_publish_source_changed(slot);
        return 0;
    }
    if (slot && message == WM_TIMER && (UINT_PTR)wparam == save_timer) {
        (void)KillTimer(window, save_timer);
        dluma_qg_publish_saved_source(slot);
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
        if (notification->window_from == slot->scintilla &&
            notification->code == SCN_SAVEPOINTREACHED)
            savepoint_reached = 1;
    }
    if (slot && message == WM_DESTROY) {
        dluma_qg_publish_saved_source(slot);
        if (slot->panel.token) (void)dluma_editor_panel_host_close(&slot->panel);
    }
    result = predecessor
        ? CallWindowProcW(predecessor, window, message, wparam, lparam)
        : 0;
    if (slot && savepoint_reached && !g_dluma_qg.stopping)
        dluma_qg_capture_saved_source(slot);
    if (slot && message == WM_SIZE) {
        dluma_qg_button_position(slot);
        if (slot->panel.token) (void)dluma_editor_panel_host_layout(&slot->panel);
    }
    if (slot && message == WM_NCDESTROY) {
        (void)KillTimer(window, source_timer);
        (void)KillTimer(window, save_timer);
        dluma_qg_saved_source_clear(slot);
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
        slot->clip_children_added = 0;
        slot->project_root[0] = 0;
        slot->source_path[0] = 0;
        slot->relative_path[0] = 0;
        slot->document_id[0] = 0;
        slot->source_sha256[0] = 0;
        slot->native_document = 0;
        slot->native_path[0] = 0;
        slot->revision = 0;
        slot->saved_revision = 0;
        slot->saved_sha256[0] = 0;
    }
    return result;
}

static LRESULT CALLBACK dluma_qg_dialog_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
    __try { result = dluma_qg_dialog_proc_impl(window, message, wparam, lparam); }
    __finally { dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight); }
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
    if (!dluma_editor_ui_request_enroll(dialog) ||
        !dluma_editor_ui_request_enroll(scintilla)) return 0;
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
    /* Stock diagnostic updates erase the dialog background while typing.
     * Clip child controls so that erase cannot flash the entire toolbar. */
    if (!(GetWindowLongPtrW(dialog, GWL_STYLE) & 0x02000000L)) {
        (void)SetWindowLongPtrW(dialog, GWL_STYLE,
            GetWindowLongPtrW(dialog, GWL_STYLE) | 0x02000000L);
        slot->clip_children_added = 1U;
    }
    slot->panel.size = sizeof(slot->panel);
    slot->panel.version = 1U;
    if (dluma_editor_panel_host_create(dialog, scintilla, L"Source", L"Graph",
            dluma_qg_panel_notify, slot, &slot->panel)) {
        (void)ShowWindow(slot->button, 0);
        dluma_log_line("INFO", "editor_quest_graph_panel_created");
    } else
        dluma_log_line("WARN", "editor_quest_graph_panel_unavailable");
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

static int dluma_qg_restore_window_proc(HWND window, WNDPROC installed, WNDPROC* predecessor)
{
    WNDPROC current, previous;
    if (!*predecessor || !window || !IsWindow(window)) { *predecessor = 0; return 1; }
    current = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    if (current == *predecessor) { *predecessor = 0; return 1; }
    if (current != installed) return 0;
    SetLastError(0);
    previous = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)*predecessor);
    if (!previous && GetLastError()) return 0;
    *predecessor = 0;
    return 1;
}

static int dluma_qg_teardown_dialogs(void)
{
    unsigned int index;
    int complete = 1;
    if (!dluma_qg_close_stop()) return 0;
    for (index = 0; index < DLUMA_QG_MAX_DIALOGS; ++index) {
        DlumaQgDialog* slot = &g_dluma_qg.dialogs[index];
        WNDPROC current;
        if (!slot->dialog) continue;
        if (slot->scintilla_predecessor && IsWindow(slot->scintilla)) {
            current = (WNDPROC)GetWindowLongPtrW(slot->scintilla, GWLP_WNDPROC);
            if (current != dluma_qg_scintilla_proc && current != slot->scintilla_predecessor) {
                complete = 0; continue;
            }
        }
        if (slot->predecessor && IsWindow(slot->dialog)) {
            current = (WNDPROC)GetWindowLongPtrW(slot->dialog, GWLP_WNDPROC);
            if (current != dluma_qg_dialog_proc && current != slot->predecessor) {
                complete = 0; continue;
            }
        }
        if (slot->panel.token && IsWindow(slot->panel.panel_window) &&
            !dluma_editor_panel_host_close(&slot->panel)) { complete = 0; continue; }
        if (!dluma_qg_restore_window_proc(slot->scintilla, dluma_qg_scintilla_proc,
                &slot->scintilla_predecessor) ||
            !dluma_qg_restore_window_proc(slot->dialog, dluma_qg_dialog_proc,
                &slot->predecessor)) { complete = 0; continue; }
        if (IsWindow(slot->dialog)) {
            (void)KillTimer(slot->dialog, (UINT_PTR)DLUMA_QG_SOURCE_TIMER_BASE + index);
            (void)KillTimer(slot->dialog, (UINT_PTR)DLUMA_QG_SAVE_TIMER_BASE + index);
        }
        dluma_qg_saved_source_clear(slot);
        if (slot->clip_children_added && IsWindow(slot->dialog))
            (void)SetWindowLongPtrW(slot->dialog, GWL_STYLE,
                GetWindowLongPtrW(slot->dialog, GWL_STYLE) & ~0x02000000L);
        if (slot->button && IsWindow(slot->button)) (void)DestroyWindow(slot->button);
        if (g_dluma_qg.panel_dialog == slot) g_dluma_qg.panel_dialog = 0;
        memset(slot, 0, sizeof(*slot));
    }
    return complete && dluma_editor_panel_host_callbacks_drained();
}

static int dluma_qg_main_listener(
    HWND window, unsigned int stage, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT *result, void *context)
{
    (void)wparam; (void)lparam; (void)context;
    if (window != g_dluma_qg.main_window) return 0;
    if (stage == DLUMA_EDITOR_UI_DESTROY) {
        g_dluma_qg.main_window = 0;
        g_dluma_qg.owner_thread_id = 0;
        return 0;
    }
    if (stage != DLUMA_EDITOR_UI_BEFORE) return 0;
    if (message == WM_CLOSE && dluma_qg_close_begin_main(window)) {
        *result = 0;
        return 1;
    }
    if (message == DLUMA_QG_MAIN_CLOSE_REPLY) {
        dluma_qg_close_process_reply();
        *result = 1;
        return 1;
    }
    if (message == DLUMA_QG_MAIN_CLOSE_ABORT) {
        dluma_qg_close_abort();
        *result = 1;
        return 1;
    }
    if (message == DLUMA_QG_MAIN_DISCOVER) {
        if (!g_dluma_qg.stopping) dluma_qg_discover_dialogs();
        *result = 1;
        return 1;
    }
    if (message == DLUMA_QG_MAIN_DISPATCH) {
        if (!g_dluma_qg.stopping) {
            dluma_atomic_increment(&g_dluma_qg.callbacks_in_flight);
            dluma_qg_ui_request_execute();
            *result = g_dluma_qg.ui_request.error[0] ? 0 : 1;
            dluma_atomic_decrement(&g_dluma_qg.callbacks_in_flight);
        } else *result = 0;
        return 1;
    }
    if (message == DLUMA_QG_MAIN_TEARDOWN) {
        g_dluma_qg.ui_teardown_complete = dluma_qg_teardown_dialogs() ? 1U : 0U;
        *result = 1;
        return 1;
    }
    return 0;
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
    DWORD process_id = 0;
    DWORD thread_id;
    int accepted = 0;
    if (g_dluma_qg.main_window && g_dluma_qg.main_subscribed &&
        dluma_qg_main_window_matches(
            g_dluma_qg.main_window, GetCurrentProcessId(),
            g_dluma_qg.owner_thread_id)) {
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
    thread_id = GetWindowThreadProcessId(
        candidate.window, &process_id);
    if (!thread_id || process_id != candidate.process_id)
        goto done;
    g_dluma_qg.main_window = candidate.window;
    g_dluma_qg.owner_thread_id = thread_id;
    if (!dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_GRAPH,
                                    dluma_qg_main_listener, 0) ||
        !dluma_editor_ui_request_enroll(candidate.window)) {
        g_dluma_qg.main_window = 0;
        g_dluma_qg.owner_thread_id = 0;
        goto done;
    }
    g_dluma_qg.main_subscribed = 1U;
    accepted = 1;
done:
    dluma_atomic_exchange(&g_dluma_qg.attach_lock, 0);
    if (accepted) {
        dluma_log_pair(
            "INFO", "editor_quest_graph_ui_bridge",
            "owner_thread_dispatcher_requested");
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
    if (g_dluma_qg.stop_event) (void)SetEvent(g_dluma_qg.stop_event);
    if (g_dluma_qg.main_window && IsWindow(g_dluma_qg.main_window))
        (void)SendMessageTimeoutW(
            g_dluma_qg.main_window, DLUMA_QG_MAIN_TEARDOWN, 0, 0,
            DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG |
                DLUMA_SMTO_ERRORONEXIT,
            2000U, &ignored);
    if (g_dluma_qg.main_window && IsWindow(g_dluma_qg.main_window) &&
        !g_dluma_qg.ui_teardown_complete) return 0;
    if (g_dluma_qg.worker_thread)
        wait_result = WaitForSingleObject(g_dluma_qg.worker_thread, 2000U);
    if (wait_result != WAIT_OBJECT_0) return 0;
    if (!dluma_qg_close_stop()) return 0;
    if (!dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_GRAPH) ||
        dluma_atomic_compare_exchange(&g_dluma_qg.callbacks_in_flight, 0, 0))
        return 0;
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
    dluma_qg_close_reset();
    dluma_qg_lock(&g_dluma_qg.output_lock);
    dluma_qg_clear_pending_documents();
    g_dluma_qg.companion_epoch = 0;
    if (g_dluma_qg.pending_panel_frame) dluma_free(g_dluma_qg.pending_panel_frame);
    g_dluma_qg.pending_panel_frame = 0;
    g_dluma_qg.pending_panel_frame_size = 0;
    g_dluma_qg.panel_dialog = 0;
    dluma_qg_unlock(&g_dluma_qg.output_lock);
    g_dluma_qg.main_window = 0;
    g_dluma_qg.main_subscribed = 0;
    g_dluma_qg.ui_teardown_complete = 0;
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
