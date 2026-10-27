#include "dlw_common.h"
#include "dlw_editor_animation_bridge.h"
#include "dlw_editor_exit_observer.h"
#include "dlw_crash_core.h"
#include "dlw_loader_symbols.h"
#include "dlw_log.h"
#include "dlw_native_mods.h"
#include "dlw_path_security.h"
#include "dlw_player_inventory_recorder.h"
#include "dlw_playerdi_teardown.h"
#include "hosts/dlw_managed_host.h"

typedef int (__cdecl *DlumaDumpFunction)(DWORD, EXCEPTION_POINTERS*);
typedef DlumaDumpFunction (__cdecl *DlumaGetDumpFunction)(void);
typedef void (__cdecl *DlumaSetDumpFunction)(DlumaDumpFunction);
typedef LONG (WINAPI *DlumaTopLevelExceptionFilter)(EXCEPTION_POINTERS*);

#if defined(_MSC_VER)
void* __cdecl _exception_info(void);
#pragma intrinsic(_exception_info)
#endif

#define DLUMA_DUMP_RESULT_FATAL 2
#define DLUMA_EXCEPTION_CONTINUE_SEARCH 0L
#define DLUMA_EXCEPTION_BREAKPOINT 0x80000003UL
#define DLUMA_EXCEPTION_SINGLE_STEP 0x80000004UL
#define DLUMA_EXCEPTION_DEBUG_PRINT 0x40010006UL
#define DLUMA_EXCEPTION_DEBUG_PRINT_WIDE 0x4001000AUL
#define DLUMA_EXCEPTION_THREAD_NAME 0x406D1388UL
#define DLUMA_EXCEPTION_ACCESS_VIOLATION 0xC0000005UL
#define DLUMA_EXCEPTION_IN_PAGE_ERROR 0xC0000006UL
#define DLUMA_EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001DUL
#define DLUMA_EXCEPTION_STACK_OVERFLOW 0xC00000FDUL
#define DLUMA_EXCEPTION_HEAP_CORRUPTION 0xC0000374UL
#define DLUMA_EXCEPTION_FAIL_FAST 0xC0000409UL
#define DLUMA_EXCEPTION_CPP 0xE06D7363UL
#define DLUMA_CRASH_TITLE_COUNT 16U
#define DLUMA_CRASH_TITLE_CAPACITY 128U
#define DLUMA_CRASH_BODY_CAPACITY 384U
#define DLUMA_CRASH_LINE_CAPACITY 4096U
#define DLUMA_CRASH_COMPONENT_CAPACITY (32U * 1024U)
#define DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY 8192U
#define DLUMA_CRASH_HOST_CONTEXT_CAPACITY 2048U
#define DLUMA_CRASH_MAX_REPORT_BYTES (2UL * 1024UL * 1024UL)
#define DLUMA_CRASH_REPORT_RESERVE_BYTES 512UL
#define DLUMA_CRASH_MAX_MODULES 256U
#define DLUMA_CRASH_MAX_FRAMES 64U
#define DLUMA_CRASH_MAX_STACK_WORDS 64U
#define DLUMA_CRASH_MAX_EXCEPTION_RECORDS 8U
#define DLUMA_CRASH_KEEP_REPORTS 10U
#define DLUMA_CRASH_STOP_TIMEOUT_MS 2000ULL
#define DLUMA_CRASH_REPORTER_STACK_BYTES (1024U * 1024U)
#define DLUMA_CRASH_REPORT_PREFIX L"dluma_crash_session_"
#define DLUMA_CRASH_MAX_MODULE_FILE_BYTES (512ULL * 1024ULL * 1024ULL)

static const DlumaCrashCoreGameModuleProfile
g_dluma_crash_game_module_profiles[] = {
    {
        L"gamedll_x64_rwdi.dll",
        0x623397AAUL,
        0x02082000UL,
        "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95",
        "dying_light_1_49_gamedll_x64_rwdi"
    },
    {
        L"engine_x64_rwdi.dll",
        0x623395D0UL,
        0x00E8D000UL,
        "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C",
        "dying_light_1_49_engine_x64_rwdi"
    },
    {
        L"filesystem_x64_rwdi.dll",
        0x623394CDUL,
        0x00060000UL,
        "CB6FF4B0C9AA62C8A35742D463AE3C34FE803767E8E2AC9B33E4604886B9846D",
        "dying_light_1_49_filesystem_x64_rwdi"
    },
    {
        L"imagelib_x64_rwdi.dll",
        0x623394DCUL,
        0x00068000UL,
        "33FB6905C7F2CA587CC13F1A7CF16D17D245CB1FE739CC029D54529DF4A2DEE2",
        "dying_light_1_49_imagelib_x64_rwdi"
    }
};

typedef struct DlumaM128A {
    ULONGLONG low;
    LONGLONG high;
} DlumaM128A;

typedef struct DlumaXmmSaveArea32 {
    WORD control_word;
    WORD status_word;
    BYTE tag_word;
    BYTE reserved1;
    WORD error_opcode;
    DWORD error_offset;
    WORD error_selector;
    WORD reserved2;
    DWORD data_offset;
    WORD data_selector;
    WORD reserved3;
    DWORD mx_csr;
    DWORD mx_csr_mask;
    DlumaM128A float_registers[8];
    DlumaM128A xmm_registers[16];
    BYTE reserved4[96];
} DlumaXmmSaveArea32;

typedef struct __declspec(align(16)) DlumaX64Context {
    ULONGLONG p1_home;
    ULONGLONG p2_home;
    ULONGLONG p3_home;
    ULONGLONG p4_home;
    ULONGLONG p5_home;
    ULONGLONG p6_home;
    DWORD context_flags;
    DWORD mx_csr;
    WORD seg_cs;
    WORD seg_ds;
    WORD seg_es;
    WORD seg_fs;
    WORD seg_gs;
    WORD seg_ss;
    DWORD eflags;
    ULONGLONG dr0;
    ULONGLONG dr1;
    ULONGLONG dr2;
    ULONGLONG dr3;
    ULONGLONG dr6;
    ULONGLONG dr7;
    ULONGLONG rax;
    ULONGLONG rcx;
    ULONGLONG rdx;
    ULONGLONG rbx;
    ULONGLONG rsp;
    ULONGLONG rbp;
    ULONGLONG rsi;
    ULONGLONG rdi;
    ULONGLONG r8;
    ULONGLONG r9;
    ULONGLONG r10;
    ULONGLONG r11;
    ULONGLONG r12;
    ULONGLONG r13;
    ULONGLONG r14;
    ULONGLONG r15;
    ULONGLONG rip;
    DlumaXmmSaveArea32 flt_save;
    DlumaM128A vector_register[26];
    ULONGLONG vector_control;
    ULONGLONG debug_control;
    ULONGLONG last_branch_to_rip;
    ULONGLONG last_branch_from_rip;
    ULONGLONG last_exception_to_rip;
    ULONGLONG last_exception_from_rip;
} DlumaX64Context;

typedef struct DlumaRuntimeFunction {
    DWORD begin_address;
    DWORD end_address;
    DWORD unwind_data;
} DlumaRuntimeFunction;

typedef DlumaRuntimeFunction* (WINAPI *DlumaRtlLookupFunctionEntry)(
    ULONGLONG control_pc, ULONGLONG* image_base, void* history_table);
typedef void* (WINAPI *DlumaRtlVirtualUnwind)(
    DWORD handler_type, ULONGLONG image_base, ULONGLONG control_pc,
    DlumaRuntimeFunction* function_entry, DlumaX64Context* context,
    void** handler_data, ULONGLONG* establisher_frame,
    void* context_pointers);

typedef struct DlumaOsVersionInfo {
    DWORD size;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platform;
    WCHAR service_pack[128];
    WORD service_pack_major;
    WORD service_pack_minor;
    WORD suite_mask;
    BYTE product_type;
    BYTE reserved;
} DlumaOsVersionInfo;
typedef LONG (WINAPI *DlumaRtlGetVersion)(DlumaOsVersionInfo*);

typedef struct DlumaSystemInfo {
    union {
        DWORD oem_id;
        struct {
            WORD processor_architecture;
            WORD reserved;
        } architecture;
    } processor;
    DWORD page_size;
    void* minimum_application_address;
    void* maximum_application_address;
    ULONG_PTR active_processor_mask;
    DWORD processor_count;
    DWORD processor_type;
    DWORD allocation_granularity;
    WORD processor_level;
    WORD processor_revision;
} DlumaSystemInfo;

typedef struct DlumaMemoryStatus {
    DWORD length;
    DWORD memory_load;
    ULONGLONG total_physical;
    ULONGLONG available_physical;
    ULONGLONG total_page_file;
    ULONGLONG available_page_file;
    ULONGLONG total_virtual;
    ULONGLONG available_virtual;
    ULONGLONG available_extended_virtual;
} DlumaMemoryStatus;

__declspec(dllimport) void WINAPI GetNativeSystemInfo(
    DlumaSystemInfo* system_info);
__declspec(dllimport) BOOL WINAPI GlobalMemoryStatusEx(
    DlumaMemoryStatus* memory_status);
__declspec(dllimport) BOOL WINAPI K32EnumProcessModules(
    HANDLE process, HMODULE* modules, DWORD bytes, DWORD* bytes_needed);
__declspec(dllimport) DWORD WINAPI GetSystemDirectoryW(
    WCHAR* buffer, DWORD capacity);
__declspec(dllimport) DlumaTopLevelExceptionFilter WINAPI
SetUnhandledExceptionFilter(DlumaTopLevelExceptionFilter filter);

typedef struct DlumaCrashText {
    WCHAR neutral_title[DLUMA_CRASH_TITLE_CAPACITY];
    WCHAR editor_neutral_title[DLUMA_CRASH_TITLE_CAPACITY];
    WCHAR editor_context[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR editor_note[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR body_loader[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR body_game[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR body_windows[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR body_external[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR body_unknown[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR exception_label[64];
    WCHAR module_label[64];
    WCHAR address_label[64];
    WCHAR dump_label[96];
    WCHAR report_label[96];
    WCHAR dump_note[DLUMA_CRASH_BODY_CAPACITY];
    WCHAR titles[DLUMA_CRASH_TITLE_COUNT][DLUMA_CRASH_TITLE_CAPACITY];
} DlumaCrashText;

typedef struct DlumaCrashModule {
    HMODULE module;
    ULONGLONG base;
    DWORD image_size;
    DWORD pe_timestamp;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned char sha256_available;
    DlumaCrashCoreModuleClassification classification;
    WCHAR path[DLUMA_MAX_WPATH];
} DlumaCrashModule;

typedef struct DlumaCrashModuleSet {
    unsigned int count;
    unsigned int total;
    unsigned int truncated;
    ULONGLONG captured_ms;
    DlumaCrashModule items[DLUMA_CRASH_MAX_MODULES];
} DlumaCrashModuleSet;

typedef struct DlumaCrashIncident {
    DWORD exception_code;
    DWORD fault_thread_id;
    DWORD reporter_thread_id;
    DWORD context_copy_status;
    ULONGLONG captured_ms;
    ULONGLONG fault_address;
    ULONGLONG instruction_pointer;
    ULONGLONG stack_pointer;
    int stock_result;
    DWORD stock_result_available;
    DWORD stock_callback_exception_code;
    ULONGLONG stock_callback_exception_address;
    DWORD reporter_exception_code;
    ULONGLONG reporter_exception_address;
    EXCEPTION_RECORD exception_record;
    DlumaX64Context context_record;
    EXCEPTION_POINTERS exception_pointers;
} DlumaCrashIncident;

typedef struct DlumaCrashState {
    HMODULE filesystem_module;
    DlumaGetDumpFunction get_dump_function;
    DlumaSetDumpFunction set_dump_function;
    DlumaDumpFunction original_dump_function;
    DlumaTopLevelExceptionFilter original_exception_filter;
    DlumaRtlLookupFunctionEntry rtl_lookup_function_entry;
    DlumaRtlVirtualUnwind rtl_virtual_unwind;
    DlumaRtlGetVersion rtl_get_version;
    volatile LONG install_state;
    volatile LONG unavailable_reason;
    volatile LONG editor_filter_state;
    volatile LONG popup_shown;
    volatile LONG dialog_suppressed_editor_shutdown;
    volatile LONG report_state;
    volatile LONG active_callbacks;
    volatile LONG callback_thread_id;
    volatile LONG retained_for_process_exit;
    volatile LONG reporter_stop_requested;
    volatile LONG module_snapshot_active;
    volatile LONG runtime_context_active;
    volatile LONG runtime_context_sequence;
    volatile LONG host_context_active;
    volatile LONG host_context_sequence;
    volatile LONG editor_host;
    HANDLE report_handle;
    HANDLE reporter_thread;
    HANDLE reporter_work_event;
    HANDLE reporter_stock_event;
    HANDLE reporter_complete_event;
    DWORD report_bytes;
    DWORD report_write_failures;
    unsigned char report_truncated;
    unsigned char report_truncation_written;
    unsigned char report_file_valid;
    unsigned char reserved;
    WCHAR game_root[DLUMA_MAX_WPATH];
    WCHAR system_directory[DLUMA_MAX_WPATH];
    WCHAR dump_dir[DLUMA_MAX_WPATH];
    WCHAR crash_dir[DLUMA_MAX_WPATH];
    WCHAR crash_dir_final[DLUMA_MAX_WPATH];
    WCHAR report_path[DLUMA_MAX_WPATH];
    WCHAR executable_path[DLUMA_MAX_WPATH];
    WCHAR loader_path[DLUMA_MAX_WPATH];
    DlumaSystemInfo system_info;
    DlumaOsVersionInfo os_version;
    DlumaCrashModuleSet module_sets[2];
    HMODULE module_scratch[DLUMA_CRASH_MAX_MODULES];
    char runtime_contexts[2][DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY];
    char host_contexts[2][DLUMA_CRASH_HOST_CONTEXT_CAPACITY];
    char line[DLUMA_CRASH_LINE_CAPACITY];
    char component[DLUMA_CRASH_COMPONENT_CAPACITY];
    WCHAR dialog_body[4096];
    DlumaCrashText text;
    DlumaCrashIncident incident;
} DlumaCrashState;

enum {
    DLUMA_CRASH_UNAVAILABLE_NONE = 0,
    DLUMA_CRASH_UNAVAILABLE_FILESYSTEM_MODULE = 1,
    DLUMA_CRASH_UNAVAILABLE_GET_EXPORT = 2,
    DLUMA_CRASH_UNAVAILABLE_SET_EXPORT = 3,
    DLUMA_CRASH_UNAVAILABLE_CALLBACK_PENDING = 4,
    DLUMA_CRASH_UNAVAILABLE_INSTALL_REJECTED = 5,
    DLUMA_CRASH_UNAVAILABLE_REPORTER_WORKER = 6
};

static DlumaCrashState g_dluma_crash;

static const WCHAR *const g_dluma_crash_english_titles[DLUMA_CRASH_TITLE_COUNT] = {
    L"\"Oh, fuck this right in the EAR.\"",
    L"\"Shit, the goddamn switch broke down!\"",
    L"\"YOU'VE GOT TO BE FUCKING KIDDING ME!!\"",
    L"\"What's this fucking thing made of, popsicle sticks?!\"",
    L"\"Great, fantastic.\"",
    L"\"Oh, motherFUCKER.\"",
    L"\"Oh, fuck you, asshole.\"",
    L"\"Oh, shit... This is not good...\"",
    L"\"No... Damn it... Not now...\"",
    L"\"Well, that almost worked...\"",
    L"\"Any guess what happened?\"",
    L"\"Yeah, I think I should watch this from a distance.\"",
    L"\"No good. It's not working.\"",
    L"\"Sonofabitch, not now!\"",
    L"\"No-no-no-no-no...! Damn it!\"",
    L"\"Fuck. Spike, the whole goddamn district went down!\""
};

static int __cdecl dluma_dump_function_wrapper(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers);
static LONG WINAPI dluma_editor_unhandled_exception_filter(
    EXCEPTION_POINTERS* exception_pointers);

static void dluma_crash_defaults(DlumaCrashText* text_state)
{
    unsigned int i;
    memset(text_state, 0, sizeof(*text_state));
    dluma_wstr_copy(text_state->neutral_title,
                  DLUMA_ARRAY_COUNT(text_state->neutral_title),
                  L"Dying Light closed unexpectedly");
    dluma_wstr_copy(text_state->editor_neutral_title,
                  DLUMA_ARRAY_COUNT(text_state->editor_neutral_title),
                  L"Dying Light Editor closed unexpectedly");
    dluma_wstr_copy(text_state->editor_context,
                  DLUMA_ARRAY_COUNT(text_state->editor_context),
                  L"The Dying Light Editor closed unexpectedly. Editor MODs can corrupt editor state even when the fault appears elsewhere. Please include the loader log, crash info log, and Chrome's stock crash dump when reporting it.");
    dluma_wstr_copy(text_state->editor_note,
                  DLUMA_ARRAY_COUNT(text_state->editor_note),
                  L"Chrome's stock crash dump has been preserved. If this continues, disable Editor MODs before reopening the project and report the crash to the DLUMA maintainers. The Editor will now close.");
    dluma_wstr_copy(text_state->body_loader,
                  DLUMA_ARRAY_COUNT(text_state->body_loader),
                  L"The fault occurred inside DLUMA. Please include the loader log, crash info log, and crash dump when reporting it.");
    dluma_wstr_copy(text_state->body_game,
                  DLUMA_ARRAY_COUNT(text_state->body_game),
                  L"The faulting instruction was in a Dying Light game module. This does not by itself prove where earlier memory corruption originated.");
    dluma_wstr_copy(text_state->body_windows,
                  DLUMA_ARRAY_COUNT(text_state->body_windows),
                  L"The faulting instruction was in a Windows system module. This does not by itself prove where earlier memory corruption originated.");
    dluma_wstr_copy(text_state->body_external,
                  DLUMA_ARRAY_COUNT(text_state->body_external),
                  L"The fault occurred inside an external module. This can include drivers, overlays, injectors, or other software loaded into the game.");
    dluma_wstr_copy(text_state->body_unknown,
                  DLUMA_ARRAY_COUNT(text_state->body_unknown),
                  L"Windows could not map the fault address to a loaded module. The crash dump contains the most useful diagnostic context.");
    dluma_wstr_copy(text_state->exception_label,
                  DLUMA_ARRAY_COUNT(text_state->exception_label), L"Exception");
    dluma_wstr_copy(text_state->module_label,
                  DLUMA_ARRAY_COUNT(text_state->module_label), L"Fault module");
    dluma_wstr_copy(text_state->address_label,
                  DLUMA_ARRAY_COUNT(text_state->address_label), L"Fault address");
    dluma_wstr_copy(text_state->dump_label,
                  DLUMA_ARRAY_COUNT(text_state->dump_label),
                  L"Expected crash-dump folder");
    dluma_wstr_copy(text_state->report_label,
                  DLUMA_ARRAY_COUNT(text_state->report_label),
                  L"Crash info log");
    dluma_wstr_copy(text_state->dump_note,
                  DLUMA_ARRAY_COUNT(text_state->dump_note),
                  L"If this crash continues to occur, please disable your mods in the main menu and report a bug on the Steam forum for our mod loader. Dying Light will now close.");
    for (i = 0; i < DLUMA_CRASH_TITLE_COUNT; ++i)
        dluma_wstr_copy(text_state->titles[i],
                      DLUMA_ARRAY_COUNT(text_state->titles[i]),
                      g_dluma_crash_english_titles[i]);
}

void dluma_crash_localization_refresh(void)
{
    unsigned int i;
    dluma_crash_defaults(&g_dluma_crash.text);
    dluma_text_get_wide("DLUMA_Crash_NeutralTitle", L"Dying Light closed unexpectedly",
                      g_dluma_crash.text.neutral_title,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.neutral_title));
    dluma_text_get_wide(
        "DLUMA_Crash_Editor_NeutralTitle",
        g_dluma_crash.text.editor_neutral_title,
        g_dluma_crash.text.editor_neutral_title,
        DLUMA_ARRAY_COUNT(g_dluma_crash.text.editor_neutral_title));
    dluma_text_get_wide(
        "DLUMA_Crash_Editor_Context",
        g_dluma_crash.text.editor_context,
        g_dluma_crash.text.editor_context,
        DLUMA_ARRAY_COUNT(g_dluma_crash.text.editor_context));
    dluma_text_get_wide(
        "DLUMA_Crash_Editor_Note",
        g_dluma_crash.text.editor_note,
        g_dluma_crash.text.editor_note,
        DLUMA_ARRAY_COUNT(g_dluma_crash.text.editor_note));
    dluma_text_get_wide("DLUMA_Crash_Body_Loader", g_dluma_crash.text.body_loader,
                      g_dluma_crash.text.body_loader,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.body_loader));
    dluma_text_get_wide("DLUMA_Crash_Body_Game", g_dluma_crash.text.body_game,
                      g_dluma_crash.text.body_game,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.body_game));
    dluma_text_get_wide("DLUMA_Crash_Body_Windows", g_dluma_crash.text.body_windows,
                      g_dluma_crash.text.body_windows,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.body_windows));
    dluma_text_get_wide("DLUMA_Crash_Body_External", g_dluma_crash.text.body_external,
                      g_dluma_crash.text.body_external,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.body_external));
    dluma_text_get_wide("DLUMA_Crash_Body_Unknown", g_dluma_crash.text.body_unknown,
                      g_dluma_crash.text.body_unknown,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.body_unknown));
    dluma_text_get_wide("DLUMA_Crash_Label_Exception", g_dluma_crash.text.exception_label,
                      g_dluma_crash.text.exception_label,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.exception_label));
    dluma_text_get_wide("DLUMA_Crash_Label_Module", g_dluma_crash.text.module_label,
                      g_dluma_crash.text.module_label,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.module_label));
    dluma_text_get_wide("DLUMA_Crash_Label_Address", g_dluma_crash.text.address_label,
                      g_dluma_crash.text.address_label,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.address_label));
    dluma_text_get_wide("DLUMA_Crash_Label_DumpFolder", g_dluma_crash.text.dump_label,
                      g_dluma_crash.text.dump_label,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.dump_label));
    dluma_text_get_wide("DLUMA_Crash_Label_InfoLog", g_dluma_crash.text.report_label,
                      g_dluma_crash.text.report_label,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.report_label));
    dluma_text_get_wide("DLUMA_Crash_Note", g_dluma_crash.text.dump_note,
                      g_dluma_crash.text.dump_note,
                      DLUMA_ARRAY_COUNT(g_dluma_crash.text.dump_note));
    for (i = 0; i < DLUMA_CRASH_TITLE_COUNT; ++i) {
        static const char prefix[] = "DLUMA_Crash_Title_";
        const unsigned int prefix_length =
            (unsigned int)sizeof(prefix) - 1U;
        char key[32];
        dluma_str_copy(key, DLUMA_ARRAY_COUNT(key), prefix);
        key[prefix_length] = (char)('0' + ((i + 1) / 10));
        key[prefix_length + 1U] = (char)('0' + ((i + 1) % 10));
        key[prefix_length + 2U] = 0;
        dluma_text_get_wide(key, g_dluma_crash_english_titles[i],
                          g_dluma_crash.text.titles[i],
                          DLUMA_ARRAY_COUNT(g_dluma_crash.text.titles[i]));
    }
}

static const WCHAR* dluma_crash_basename(const WCHAR* path)
{
    const WCHAR* name = path;
    unsigned int i;
    if (!path) return L"";
    for (i = 0; path[i]; ++i)
        if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    return name;
}

static int dluma_crash_is_known_third_party_basename(
    const WCHAR* basename)
{
    static const WCHAR* const known[] = {
        L"gameoverlayrenderer64.dll",
        L"steam_api64.dll",
        L"discordhook64.dll",
        L"nvspcap64.dll",
        L"rtsshooks64.dll",
        L"reshade64.dll"
    };
    unsigned int index;
    if (!basename || !basename[0]) return 0;
    for (index = 0; index < DLUMA_ARRAY_COUNT(known); ++index)
        if (dluma_wstricmp(basename, known[index]) == 0)
            return 1;
    return 0;
}

static int dluma_crash_path_is_under(
    const WCHAR* path, const WCHAR* root)
{
    SIZE_T root_length;
    if (!path || !root || !root[0] || !dluma_wstarts_with_icase(path, root))
        return 0;
    root_length = wcslen(root);
    return !path[root_length] || path[root_length] == L'\\' ||
           path[root_length] == L'/';
}

static void dluma_crash_classify_module(DlumaCrashModule* module)
{
    DlumaCrashCoreModuleIdentity identity;
    const WCHAR* basename;
    if (!module) return;
    basename = dluma_crash_basename(module->path);
    memset(&identity, 0, sizeof(identity));
    identity.mapped = 1;
    identity.process_executable =
        module->module == GetModuleHandleW(0);
    identity.loader_module =
        module->module == g_dluma.self_module;
    identity.windows_system_module =
        dluma_crash_path_is_under(
            module->path, g_dluma_crash.system_directory);
    identity.known_third_party_module =
        dluma_crash_is_known_third_party_basename(basename);
    identity.basename = basename;
    identity.pe_timestamp = module->pe_timestamp;
    identity.image_size = module->image_size;
    identity.sha256 = module->sha256;
    identity.sha256_available = module->sha256_available;
    module->classification = dluma_crash_core_classify_module(
        &identity, g_dluma_crash_game_module_profiles,
        DLUMA_ARRAY_COUNT(g_dluma_crash_game_module_profiles));
}

static int dluma_crash_join_checked(
    WCHAR* out, unsigned int capacity,
    const WCHAR* left, const WCHAR* right)
{
    SIZE_T left_length;
    SIZE_T right_length;
    if (!out || !capacity || !left || !right) return 0;
    left_length = wcslen(left);
    while (*right == L'\\' || *right == L'/') ++right;
    right_length = wcslen(right);
    if (!left_length || left_length + right_length + 2U > capacity) {
        out[0] = 0;
        return 0;
    }
    dluma_path_join(out, capacity, left, right);
    return out[0] && wcslen(out) == left_length + right_length + 1U;
}

static void dluma_crash_wappend_decimal(
    WCHAR* out, unsigned int capacity,
    ULONGLONG value, unsigned int minimum_digits)
{
    WCHAR reverse[32];
    WCHAR number[32];
    unsigned int count = 0;
    unsigned int index;
    do {
        reverse[count++] = (WCHAR)(L'0' + (value % 10ULL));
        value /= 10ULL;
    } while (value && count < DLUMA_ARRAY_COUNT(reverse));
    while (count < minimum_digits && count < DLUMA_ARRAY_COUNT(reverse))
        reverse[count++] = L'0';
    for (index = 0; index < count; ++index)
        number[index] = reverse[count - index - 1U];
    number[count] = 0;
    dluma_wstr_append(out, capacity, number);
}

static int dluma_crash_filetime_older(
    const FILETIME* first, const FILETIME* second)
{
    if (first->dwHighDateTime != second->dwHighDateTime)
        return first->dwHighDateTime < second->dwHighDateTime;
    return first->dwLowDateTime < second->dwLowDateTime;
}

static void dluma_crash_prune_reports(void)
{
    unsigned int pass;
    for (pass = 0; pass < 1024U; ++pass) {
        WIN32_FIND_DATAW data;
        HANDLE find;
        WCHAR pattern[DLUMA_MAX_WPATH];
        WCHAR oldest_name[260];
        FILETIME oldest_time;
        unsigned int count = 0;
        int have_oldest = 0;
        if (!dluma_crash_join_checked(
                pattern, DLUMA_ARRAY_COUNT(pattern),
                g_dluma_crash.crash_dir, L"dluma_crash_session_*.log"))
            return;
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            WCHAR full[DLUMA_MAX_WPATH];
            ULONGLONG size;
            if (!dluma_crash_core_report_name_matches(data.cFileName) ||
                (data.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                continue;
            if (!dluma_crash_join_checked(
                    full, DLUMA_ARRAY_COUNT(full),
                    g_dluma_crash.crash_dir, data.cFileName))
                continue;
            size = ((ULONGLONG)data.nFileSizeHigh << 32) |
                   data.nFileSizeLow;
            if (!size) {
                (void)DeleteFileW(full);
                continue;
            }
            ++count;
            if (!have_oldest ||
                dluma_crash_filetime_older(
                    &data.ftLastWriteTime, &oldest_time)) {
                dluma_wstr_copy(
                    oldest_name, DLUMA_ARRAY_COUNT(oldest_name),
                    data.cFileName);
                oldest_time = data.ftLastWriteTime;
                have_oldest = 1;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
        if (count < DLUMA_CRASH_KEEP_REPORTS || !have_oldest) return;
        {
            WCHAR full[DLUMA_MAX_WPATH];
            if (!dluma_crash_join_checked(
                    full, DLUMA_ARRAY_COUNT(full),
                    g_dluma_crash.crash_dir, oldest_name) ||
                !DeleteFileW(full))
                return;
        }
    }
}

static int dluma_crash_prepare_paths(void)
{
    HANDLE module_dir_handle = INVALID_HANDLE_VALUE;
    HANDLE crash_dir_handle = INVALID_HANDLE_VALUE;
    WCHAR module_dir_final[DLUMA_MAX_WPATH];
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    g_dluma_crash.executable_path[0] = 0;
    g_dluma_crash.loader_path[0] = 0;
    if (!dluma_get_module_path_checked(
            0, g_dluma_crash.executable_path,
            DLUMA_ARRAY_COUNT(g_dluma_crash.executable_path)) ||
        !dluma_get_module_path_checked(
            g_dluma.self_module, g_dluma_crash.loader_path,
            DLUMA_ARRAY_COUNT(g_dluma_crash.loader_path)))
        return 0;
    dluma_wstr_copy(
        g_dluma_crash.game_root,
        DLUMA_ARRAY_COUNT(g_dluma_crash.game_root),
        g_dluma_crash.executable_path);
    dluma_path_dirname(g_dluma_crash.game_root);
    if (dluma_wstricmp(
            dluma_crash_basename(g_dluma_crash.game_root),
            L"DevTools") == 0)
        dluma_path_parent(g_dluma_crash.game_root);
    if (!dluma_crash_join_checked(
            g_dluma_crash.dump_dir,
            DLUMA_ARRAY_COUNT(g_dluma_crash.dump_dir),
            g_dluma_crash.game_root, L"dumps") ||
        !dluma_crash_join_checked(
            runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir),
            g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_crash_join_checked(
            g_dluma_crash.crash_dir,
            DLUMA_ARRAY_COUNT(g_dluma_crash.crash_dir),
            runtime_dir, L"crashes") ||
        !dluma_mkdirs(g_dluma_crash.crash_dir))
        return 0;
    module_dir_handle = CreateFileW(
        g_dluma.module_dir, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    crash_dir_handle = CreateFileW(
        g_dluma_crash.crash_dir, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (module_dir_handle == INVALID_HANDLE_VALUE ||
        crash_dir_handle == INVALID_HANDLE_VALUE ||
        !dluma_get_final_path_checked(
            module_dir_handle, module_dir_final,
            DLUMA_ARRAY_COUNT(module_dir_final)) ||
        !dluma_get_final_path_checked(
            crash_dir_handle, g_dluma_crash.crash_dir_final,
            DLUMA_ARRAY_COUNT(g_dluma_crash.crash_dir_final)) ||
        !dluma_crash_path_is_under(
            g_dluma_crash.crash_dir_final, module_dir_final)) {
        if (module_dir_handle != INVALID_HANDLE_VALUE)
            CloseHandle(module_dir_handle);
        if (crash_dir_handle != INVALID_HANDLE_VALUE)
            CloseHandle(crash_dir_handle);
        return 0;
    }
    CloseHandle(module_dir_handle);
    CloseHandle(crash_dir_handle);
    dluma_crash_prune_reports();
    return 1;
}

static int dluma_crash_open_report_file(void)
{
    SYSTEMTIME utc;
    WCHAR name[160];
    WCHAR final_path[DLUMA_MAX_WPATH];
    unsigned int attempt;
    HANDLE file;
    GetSystemTime(&utc);
    for (attempt = 0; attempt < 10U; ++attempt) {
        name[0] = 0;
        dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), DLUMA_CRASH_REPORT_PREFIX);
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wYear, 4);
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wMonth, 2);
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wDay, 2);
        dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), L"T");
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wHour, 2);
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wMinute, 2);
        dluma_crash_wappend_decimal(name, DLUMA_ARRAY_COUNT(name), utc.wSecond, 2);
        dluma_crash_wappend_decimal(
            name, DLUMA_ARRAY_COUNT(name), utc.wMilliseconds, 3);
        dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), L"Z_pid-");
        dluma_crash_wappend_decimal(
            name, DLUMA_ARRAY_COUNT(name), GetCurrentProcessId(), 1);
        if (attempt) {
            dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), L"-");
            dluma_crash_wappend_decimal(
                name, DLUMA_ARRAY_COUNT(name), attempt, 1);
        }
        dluma_wstr_append(name, DLUMA_ARRAY_COUNT(name), L".log");
        if (!dluma_crash_join_checked(
                g_dluma_crash.report_path,
                DLUMA_ARRAY_COUNT(g_dluma_crash.report_path),
                g_dluma_crash.crash_dir, name))
            return 0;
        file = CreateFileW(
            g_dluma_crash.report_path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) continue;
        if (!dluma_get_final_path_checked(
                file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
            !dluma_crash_path_is_under(
                final_path, g_dluma_crash.crash_dir_final)) {
            CloseHandle(file);
            (void)DeleteFileW(g_dluma_crash.report_path);
            g_dluma_crash.report_path[0] = 0;
            return 0;
        }
        g_dluma_crash.report_handle = file;
        g_dluma_crash.report_file_valid = 1;
        return 1;
    }
    g_dluma_crash.report_path[0] = 0;
    return 0;
}

static void dluma_crash_close_empty_report(void)
{
    int delete_empty = 0;
    if (g_dluma_crash.report_handle &&
        g_dluma_crash.report_handle != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        size.QuadPart = -1;
        if (GetFileSizeEx(g_dluma_crash.report_handle, &size) &&
            size.QuadPart == 0)
            delete_empty = 1;
        CloseHandle(g_dluma_crash.report_handle);
        g_dluma_crash.report_handle = INVALID_HANDLE_VALUE;
    }
    if (delete_empty && g_dluma_crash.report_path[0])
        (void)DeleteFileW(g_dluma_crash.report_path);
    if (delete_empty)
        g_dluma_crash.report_path[0] = 0;
    g_dluma_crash.report_file_valid = 0;
}

static void dluma_crash_capture_system(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    DWORD system_directory_length;
    memset(&g_dluma_crash.system_info, 0, sizeof(g_dluma_crash.system_info));
    memset(&g_dluma_crash.os_version, 0, sizeof(g_dluma_crash.os_version));
    g_dluma_crash.system_directory[0] = 0;
    system_directory_length = GetSystemDirectoryW(
        g_dluma_crash.system_directory,
        DLUMA_ARRAY_COUNT(g_dluma_crash.system_directory));
    if (!system_directory_length ||
        system_directory_length >=
            DLUMA_ARRAY_COUNT(g_dluma_crash.system_directory))
        g_dluma_crash.system_directory[0] = 0;
    GetNativeSystemInfo(&g_dluma_crash.system_info);
    if (ntdll) {
        g_dluma_crash.rtl_lookup_function_entry =
            (DlumaRtlLookupFunctionEntry)GetProcAddress(
                ntdll, "RtlLookupFunctionEntry");
        g_dluma_crash.rtl_virtual_unwind =
            (DlumaRtlVirtualUnwind)GetProcAddress(
                ntdll, "RtlVirtualUnwind");
        g_dluma_crash.rtl_get_version =
            (DlumaRtlGetVersion)GetProcAddress(
                ntdll, "RtlGetVersion");
    }
    g_dluma_crash.os_version.size = sizeof(g_dluma_crash.os_version);
    if (!g_dluma_crash.rtl_get_version ||
        g_dluma_crash.rtl_get_version(&g_dluma_crash.os_version) != 0)
        memset(&g_dluma_crash.os_version, 0, sizeof(g_dluma_crash.os_version));
}

static void dluma_crash_capture_modules(void)
{
    DWORD needed = 0;
    unsigned int index;
    unsigned int count;
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.module_snapshot_active, 0, 0);
    LONG next = active ? 0 : 1;
    DlumaCrashModuleSet* set = &g_dluma_crash.module_sets[next];
    if (!K32EnumProcessModules(
            GetCurrentProcess(), g_dluma_crash.module_scratch,
            sizeof(g_dluma_crash.module_scratch), &needed))
        return;
    memset(set, 0, sizeof(*set));
    set->total = needed / (DWORD)sizeof(HMODULE);
    count = set->total < DLUMA_CRASH_MAX_MODULES
        ? set->total : DLUMA_CRASH_MAX_MODULES;
    set->count = count;
    set->truncated = set->total > count;
    set->captured_ms = GetTickCount64();
    for (index = 0; index < count; ++index) {
        DlumaCrashModule* item = &set->items[index];
        HMODULE module = g_dluma_crash.module_scratch[index];
        const WCHAR* basename;
        item->module = module;
        item->base = (ULONGLONG)(ULONG_PTR)module;
        if (!dluma_get_module_path_checked(
                module, item->path, DLUMA_ARRAY_COUNT(item->path)))
            dluma_wstr_copy(
                item->path, DLUMA_ARRAY_COUNT(item->path),
                L"<path-unavailable>");
        if (module && dluma_is_readable(module, 4096U))
            (void)dluma_build_identity_parse_pe_header(
                (const BYTE*)module, 4096U,
                &item->pe_timestamp, &item->image_size);
        basename = dluma_crash_basename(item->path);
        if (dluma_crash_core_game_profile_basename_matches(
                basename, g_dluma_crash_game_module_profiles,
                DLUMA_ARRAY_COUNT(g_dluma_crash_game_module_profiles)) &&
            dluma_sha256_file_hex(
                item->path, DLUMA_CRASH_MAX_MODULE_FILE_BYTES,
                item->sha256))
            item->sha256_available = 1;
        dluma_crash_classify_module(item);
    }
    dluma_atomic_exchange(&g_dluma_crash.module_snapshot_active, next);
}

void dluma_crash_publish_runtime_context(
    const char* phase,
    const WCHAR* project_root,
    const char* map_name,
    unsigned int owner_epoch,
    ULONGLONG owner_generation)
{
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.runtime_context_active, 0, 0);
    LONG next = active ? 0 : 1;
    char* out = g_dluma_crash.runtime_contexts[next];
    char number[32];
    char project[DLUMA_MAX_WPATH * 3];
    dluma_atomic_increment(&g_dluma_crash.runtime_context_sequence);
    out[0] = 0;
    project[0] = 0;
    if (project_root && project_root[0])
        (void)dluma_wide_to_utf8(
            project_root, project, DLUMA_ARRAY_COUNT(project));
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        "[CRASH] [INFO] runtime phase=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        phase ? phase : "<unknown>");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " lifecycle=");
    dluma_u64_to_text(
        (ULONGLONG)(DWORD)g_dluma.lifecycle_state,
        number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, number);
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " started=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        g_dluma.started ? "1" : "0");
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " stopping=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        g_dluma.stop_requested ? "1" : "0");
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " engine_ready=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        g_dluma.engine.ready ? "1" : "0");
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " map=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        map_name && map_name[0] ? map_name : "<none>");
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " project=");
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY,
        project[0] ? project : "<none>");
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " owner_epoch=");
    dluma_u64_to_text(owner_epoch, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, number);
    dluma_str_append(
        out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, " owner_generation=");
    dluma_u64_to_text(owner_generation, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, number);
    dluma_str_append(out, DLUMA_CRASH_RUNTIME_CONTEXT_CAPACITY, "\r\n");
    dluma_atomic_exchange(&g_dluma_crash.runtime_context_active, next);
    dluma_atomic_increment(&g_dluma_crash.runtime_context_sequence);
}

void dluma_crash_publish_host_context(
    const char* host_kind,
    int exact_profile,
    const char* classification_reason,
    const char* session_phase,
    ULONGLONG current_generation,
    ULONGLONG last_generation)
{
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.host_context_active, 0, 0);
    LONG next = active ? 0 : 1;
    char* out = g_dluma_crash.host_contexts[next];
    char number[32];
    dluma_atomic_increment(&g_dluma_crash.host_context_sequence);
    out[0] = 0;
#define DLUMA_CRASH_HOST_TEXT(key, value) \
    do { \
        dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, \
                       "[CRASH] [INFO] " key "="); \
        dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, \
                       (value) && (value)[0] ? (value) : "<unavailable>"); \
        dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, "\r\n"); \
    } while (0)
    DLUMA_CRASH_HOST_TEXT("host_kind", host_kind);
    DLUMA_CRASH_HOST_TEXT(
        "host_exact_profile", exact_profile ? "1" : "0");
    DLUMA_CRASH_HOST_TEXT(
        "host_classification_reason", classification_reason);
    DLUMA_CRASH_HOST_TEXT("editor_session_phase", session_phase);
#undef DLUMA_CRASH_HOST_TEXT
    dluma_str_append(
        out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY,
        "[CRASH] [INFO] editor_current_generation=");
    dluma_u64_to_text(
        current_generation, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, number);
    dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, "\r\n");
    dluma_str_append(
        out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY,
        "[CRASH] [INFO] editor_last_generation=");
    dluma_u64_to_text(last_generation, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, number);
    dluma_str_append(out, DLUMA_CRASH_HOST_CONTEXT_CAPACITY, "\r\n");
    dluma_atomic_exchange(
        &g_dluma_crash.editor_host,
        dluma_crash_core_ascii_equal_icase(host_kind, "chrome_editor") ? 1 : 0);
    dluma_atomic_exchange(&g_dluma_crash.host_context_active, next);
    dluma_atomic_increment(&g_dluma_crash.host_context_sequence);
}

void dluma_crash_reporting_refresh(void)
{
    if (g_dluma_crash.install_state != 2) return;
    dluma_crash_capture_modules();
    dluma_crash_publish_runtime_context(
        "refresh", g_dluma.active_project_root, g_dluma.active_map,
        g_dluma.active_project_owner_epoch,
        g_dluma.active_project_owner_transition_generation);
}

static const DlumaCrashModule* dluma_crash_module_for_address(
    ULONGLONG address)
{
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.module_snapshot_active, 0, 0);
    const DlumaCrashModuleSet* set = &g_dluma_crash.module_sets[active ? 1 : 0];
    unsigned int index;
    for (index = 0; index < set->count; ++index) {
        const DlumaCrashModule* item = &set->items[index];
        if (dluma_crash_core_address_in_module(
                address, item->base, item->image_size))
            return item;
    }
    return 0;
}

static DlumaCrashCoreModuleClassification dluma_crash_describe_fault(
    ULONGLONG address, const DlumaCrashModule** module_out)
{
    const DlumaCrashModule* module = dluma_crash_module_for_address(address);
    if (module_out) *module_out = module;
    if (!module)
        return dluma_crash_core_classify_module(
            0, g_dluma_crash_game_module_profiles,
            DLUMA_ARRAY_COUNT(g_dluma_crash_game_module_profiles));
    return module->classification;
}

static void dluma_crash_append_hex_text(
    char* out, unsigned int capacity,
    ULONGLONG value, unsigned int digits)
{
    char number[19];
    if (digits > 16U) digits = 16U;
    if (dluma_crash_core_format_hex(
            value, digits, number, DLUMA_ARRAY_COUNT(number)))
        dluma_str_append(out, capacity, number);
}

static int dluma_crash_dedicated_write_part(
    void* context,
    const char* data,
    unsigned long length,
    unsigned long* written)
{
    DWORD count = 0;
    BOOL result = WriteFile(
        (HANDLE)context, data, (DWORD)length, &count, 0);
    if (written) *written = count;
    return result ? 1 : 0;
}

static void dluma_crash_write_dedicated(
    const char* text, DWORD length)
{
    unsigned long written;
    if (!text || !length ||
        !g_dluma_crash.report_file_valid ||
        !g_dluma_crash.report_handle ||
        g_dluma_crash.report_handle == INVALID_HANDLE_VALUE)
        return;
    written = dluma_crash_core_write_all(
        dluma_crash_dedicated_write_part,
        g_dluma_crash.report_handle, text, length, 4U);
    if (written != length) ++g_dluma_crash.report_write_failures;
}

static void dluma_crash_emit_raw(const char* text)
{
    DWORD length;
    if (!text || !text[0]) return;
    length = (DWORD)strlen(text);
    if (g_dluma_crash.report_truncated) return;
    if (!dluma_crash_core_report_can_append(
            g_dluma_crash.report_bytes, length,
            DLUMA_CRASH_MAX_REPORT_BYTES,
            DLUMA_CRASH_REPORT_RESERVE_BYTES)) {
        static const char truncated[] =
            "[CRASH] [WARN] report_truncated=1 limit_bytes=2097152\r\n";
        g_dluma_crash.report_truncated = 1;
        if (!g_dluma_crash.report_truncation_written) {
            DWORD truncated_length = (DWORD)strlen(truncated);
            g_dluma_crash.report_truncation_written = 1;
            dluma_log_emergency(truncated);
            dluma_crash_write_dedicated(
                truncated, truncated_length);
            g_dluma_crash.report_bytes += truncated_length;
        }
        return;
    }
    dluma_log_emergency(text);
    dluma_crash_write_dedicated(text, length);
    g_dluma_crash.report_bytes += length;
}

static void dluma_crash_emit_final_raw(const char* text)
{
    DWORD length;
    if (!text || !text[0]) return;
    length = (DWORD)strlen(text);
    if (length > DLUMA_CRASH_MAX_REPORT_BYTES -
                 g_dluma_crash.report_bytes)
        return;
    dluma_log_emergency(text);
    dluma_crash_write_dedicated(text, length);
    g_dluma_crash.report_bytes += length;
}

static void dluma_crash_emit_final_text(
    const char* key, const char* value)
{
    g_dluma_crash.line[0] = 0;
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "[CRASH] [INFO] ");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        key ? key : "value");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "=");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        dluma_crash_core_value_or_unavailable(value));
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
    dluma_crash_emit_final_raw(g_dluma_crash.line);
}

static void dluma_crash_emit_final_u64(
    const char* key, ULONGLONG value)
{
    char number[32];
    dluma_u64_to_text(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_crash_emit_final_text(key, number);
}

static void dluma_crash_emit_text(const char* key, const char* value)
{
    g_dluma_crash.line[0] = 0;
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "[CRASH] [INFO] ");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        key ? key : "value");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "=");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        dluma_crash_core_value_or_unavailable(value));
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
    dluma_crash_emit_raw(g_dluma_crash.line);
}

static void dluma_crash_emit_wide(const char* key, const WCHAR* value)
{
    char utf8[DLUMA_MAX_WPATH * 3];
    utf8[0] = 0;
    if (value && value[0])
        (void)dluma_wide_to_utf8(
            value, utf8, DLUMA_ARRAY_COUNT(utf8));
    dluma_crash_emit_text(key, utf8);
}

static void dluma_crash_emit_u64(const char* key, ULONGLONG value)
{
    char number[32];
    dluma_u64_to_text(value, number, DLUMA_ARRAY_COUNT(number));
    dluma_crash_emit_text(key, number);
}

static void dluma_crash_emit_hex(
    const char* key, ULONGLONG value, unsigned int digits)
{
    char number[32];
    number[0] = 0;
    dluma_crash_append_hex_text(
        number, DLUMA_ARRAY_COUNT(number), value, digits);
    dluma_crash_emit_text(key, number);
}

static ULONGLONG dluma_crash_filetime_u64(const FILETIME* time)
{
    return ((ULONGLONG)time->dwHighDateTime << 32) |
           time->dwLowDateTime;
}

static void dluma_crash_format_system_time(
    const SYSTEMTIME* time, char* out, unsigned int capacity,
    int utc)
{
    char number[32];
    out[0] = 0;
#define DLUMA_APPEND_TIME_VALUE(value, digits) \
    do { \
        unsigned int length; \
        dluma_u64_to_text((value), number, DLUMA_ARRAY_COUNT(number)); \
        length = (unsigned int)strlen(number); \
        while (length++ < (digits)) dluma_str_append(out, capacity, "0"); \
        dluma_str_append(out, capacity, number); \
    } while (0)
    DLUMA_APPEND_TIME_VALUE(time->wYear, 4U);
    dluma_str_append(out, capacity, "-");
    DLUMA_APPEND_TIME_VALUE(time->wMonth, 2U);
    dluma_str_append(out, capacity, "-");
    DLUMA_APPEND_TIME_VALUE(time->wDay, 2U);
    dluma_str_append(out, capacity, "T");
    DLUMA_APPEND_TIME_VALUE(time->wHour, 2U);
    dluma_str_append(out, capacity, ":");
    DLUMA_APPEND_TIME_VALUE(time->wMinute, 2U);
    dluma_str_append(out, capacity, ":");
    DLUMA_APPEND_TIME_VALUE(time->wSecond, 2U);
    dluma_str_append(out, capacity, ".");
    DLUMA_APPEND_TIME_VALUE(time->wMilliseconds, 3U);
    if (utc) dluma_str_append(out, capacity, "Z");
#undef DLUMA_APPEND_TIME_VALUE
}

static void dluma_crash_emit_register(
    const char* name, ULONGLONG value)
{
    g_dluma_crash.line[0] = 0;
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "[CRASH] [INFO] register name=");
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), name);
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), " value=");
    dluma_crash_append_hex_text(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), value, 16);
    dluma_str_append(
        g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
    dluma_crash_emit_raw(g_dluma_crash.line);
}

static void dluma_crash_emit_module_address(
    const char* kind, unsigned int index, ULONGLONG address)
{
    const DlumaCrashModule* module =
        dluma_crash_module_for_address(address);
    char path[DLUMA_MAX_WPATH * 3];
    path[0] = 0;
    if (module) {
        (void)dluma_wide_to_utf8(
            module->path, path, DLUMA_ARRAY_COUNT(path));
    }
    if (dluma_crash_core_format_frame_line(
            kind, index, address, path,
            module ? module->base : 0,
            g_dluma_crash.line,
            DLUMA_ARRAY_COUNT(g_dluma_crash.line)))
        dluma_crash_emit_raw(g_dluma_crash.line);
    else
        dluma_crash_emit_text("stack_frame", "format_truncated");
    if (module && module->module == g_dluma.self_module &&
        address >= module->base &&
        address - module->base <= 0xFFFFFFFFULL) {
        DlumaLoaderSymbolLookup symbol;
        if (dluma_loader_symbol_lookup(
                (uint32_t)(address - module->base), &symbol)) {
            char number[32];
            g_dluma_crash.line[0] = 0;
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                "[CRASH] [INFO] loader_symbol kind=");
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                kind ? kind : "frame");
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " index=");
            dluma_u64_to_text(
                index, number, DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                number);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " rva=");
            dluma_crash_append_hex_text(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                symbol.query_rva, 8);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " function_start=");
            dluma_crash_append_hex_text(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                symbol.start_rva, 8);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " function_end=");
            dluma_crash_append_hex_text(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                symbol.end_rva, 8);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " classification=");
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                dluma_loader_symbol_classification_name(
                    symbol.classification));
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " symbol=");
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                symbol.name);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " source=");
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                symbol.source_file);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " line=");
            dluma_u64_to_text(
                symbol.source_line, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                number);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                "\r\n");
            dluma_crash_emit_raw(g_dluma_crash.line);
        }
    }
}

static int dluma_crash_playerdi_safe_read(
    void* user, uintptr_t address, void* output, size_t size)
{
    (void)user;
    if (!address || !output || !size ||
        !dluma_is_readable((const void*)address, size))
        return 0;
#if defined(_MSC_VER)
    __try {
        memcpy(output, (const void*)address, size);
    }
    __except (1) {
        return 0;
    }
#else
    memcpy(output, (const void*)address, size);
#endif
    return 1;
}

static int dluma_crash_playerdi_query_memory(
    void* user,
    uintptr_t address,
    DlumaPlayerDiMemoryRegion* output)
{
    MEMORY_BASIC_INFORMATION region;
    (void)user;
    if (!address || !output) return 0;
    memset(output, 0, sizeof(*output));
    memset(&region, 0, sizeof(region));
    if (VirtualQuery(
            (const void*)address, &region,
            sizeof(region)) != sizeof(region))
        return 0;
    output->base = (uintptr_t)region.BaseAddress;
    output->size = region.RegionSize;
    output->state = region.State;
    output->protection = region.Protect;
    output->query_succeeded = 1;
    if (output->base <= address &&
        output->size <= ~(uintptr_t)0 - output->base &&
        address < output->base + output->size)
        output->address_in_region = 1;
    return 1;
}

static void dluma_crash_emit_playerdi_region(
    const char* prefix,
    const DlumaPlayerDiMemoryRegion* region)
{
    char key[96];
#define DLUMA_PLAYERDI_REGION_KEY(suffix) \
    key[0] = 0; \
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), prefix); \
    dluma_str_append(key, DLUMA_ARRAY_COUNT(key), suffix)
    if (!prefix || !region) return;
    DLUMA_PLAYERDI_REGION_KEY("_query_succeeded");
    dluma_crash_emit_u64(key, region->query_succeeded);
    DLUMA_PLAYERDI_REGION_KEY("_base");
    dluma_crash_emit_hex(key, region->base, 16);
    DLUMA_PLAYERDI_REGION_KEY("_size");
    dluma_crash_emit_u64(key, region->size);
    DLUMA_PLAYERDI_REGION_KEY("_state");
    dluma_crash_emit_hex(key, region->state, 8);
    DLUMA_PLAYERDI_REGION_KEY("_protection");
    dluma_crash_emit_hex(key, region->protection, 8);
    DLUMA_PLAYERDI_REGION_KEY("_address_in_region");
    dluma_crash_emit_u64(key, region->address_in_region);
#undef DLUMA_PLAYERDI_REGION_KEY
}

static void dluma_crash_emit_playerdi_teardown_scope(void)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    memset(&diagnostics, 0, sizeof(diagnostics));
    dluma_playerdi_teardown_snapshot(&diagnostics);
    dluma_crash_emit_u64(
        "loader_symbol_manifest_available",
        dluma_loader_symbol_manifest_available() ? 1U : 0U);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_observer_enabled",
        diagnostics.observer_enabled);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_observer_diagnostic_opt_in",
        diagnostics.diagnostic_opt_in);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_supported_profile_validated",
        diagnostics.supported_profile_validated);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_original_enter_count",
        diagnostics.original_enter_count);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_original_return_count",
        diagnostics.original_return_count);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_active_depth",
        diagnostics.active_depth);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_nested_pass_through_count",
        diagnostics.nested_pass_through_count);
    dluma_crash_emit_hex(
        "playerdi_ondestroy_last_player",
        diagnostics.last_player, 16);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_last_thread",
        diagnostics.last_thread);
    dluma_crash_emit_u64(
        "playerdi_ondestroy_last_sequence",
        diagnostics.last_sequence);
    dluma_crash_emit_hex(
        "playerdi_teardown_scope_player",
        diagnostics.active_player, 16);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_present",
        diagnostics.active_scope_depth && diagnostics.active_player
            ? 1U
            : 0U);
    dluma_crash_emit_hex(
        "playerdi_teardown_scope_inventory_owner",
        diagnostics.active_inventory_owner, 16);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_thread",
        diagnostics.active_thread);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_depth",
        diagnostics.active_scope_depth);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_original_entered",
        diagnostics.active_original_entered);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_original_returned",
        diagnostics.active_original_returned);
    dluma_crash_emit_u64(
        "playerdi_teardown_scope_sequence",
        diagnostics.active_sequence);
    dluma_crash_emit_u64(
        "playerdi_inventory_header_layout_validated",
        dluma_playerdi_inventory_header_layout_validated() ? 1U : 0U);
    if (!dluma_playerdi_inventory_header_layout_validated())
        dluma_crash_emit_text(
            "playerdi_inventory_snapshot_status",
            "inventory_header_layout_unvalidated");
}

static void dluma_crash_player_inventory_event_key(
    char* output, unsigned int capacity, unsigned int slot,
    const char* field)
{
    char ordinal[16];
    if (!output || !capacity) return;
    output[0] = 0;
    dluma_u64_to_text(slot, ordinal, DLUMA_ARRAY_COUNT(ordinal));
    dluma_str_append(
        output, capacity, "player_inventory_event_");
    dluma_str_append(output, capacity, ordinal);
    dluma_str_append(output, capacity, "_");
    dluma_str_append(output, capacity, field ? field : "field");
}

static void dluma_crash_emit_player_inventory_event(
    unsigned int slot, const DlumaPlayerInventoryEvent* event)
{
    char key[96];
    if (!event || !event->sequence) return;
#define DLUMA_INVENTORY_EVENT_U64(field, value)              \
    do {                                                    \
        dluma_crash_player_inventory_event_key(               \
            key, DLUMA_ARRAY_COUNT(key), slot, field);        \
        dluma_crash_emit_u64(key, (uint64_t)(value));         \
    } while (0)
#define DLUMA_INVENTORY_EVENT_HEX(field, value)               \
    do {                                                    \
        dluma_crash_player_inventory_event_key(               \
            key, DLUMA_ARRAY_COUNT(key), slot, field);        \
        dluma_crash_emit_hex(key, (uintptr_t)(value), 16);     \
    } while (0)
#define DLUMA_INVENTORY_EVENT_TEXT(field, value)              \
    do {                                                    \
        dluma_crash_player_inventory_event_key(               \
            key, DLUMA_ARRAY_COUNT(key), slot, field);        \
        dluma_crash_emit_text(key, value);                    \
    } while (0)
    DLUMA_INVENTORY_EVENT_U64("sequence", event->sequence);
    DLUMA_INVENTORY_EVENT_TEXT(
        "phase", dluma_player_inventory_phase_name(event->phase));
    DLUMA_INVENTORY_EVENT_U64("thread", event->thread_id);
    DLUMA_INVENTORY_EVENT_U64("clear_depth", event->clear_depth);
    DLUMA_INVENTORY_EVENT_U64(
        "observer_depth", event->observer_depth);
    DLUMA_INVENTORY_EVENT_U64(
        "outer_loop_index", event->outer_loop_index);
    DLUMA_INVENTORY_EVENT_U64("reason", event->reason);
    DLUMA_INVENTORY_EVENT_U64("group", event->group_index);
    DLUMA_INVENTORY_EVENT_U64("child", event->child_index);
    DLUMA_INVENTORY_EVENT_HEX("inventory", event->inventory);
    DLUMA_INVENTORY_EVENT_HEX("item", event->item);
    DLUMA_INVENTORY_EVENT_HEX(
        "header", event->vector_header_address);
    DLUMA_INVENTORY_EVENT_HEX(
        "previous_data", event->previous.data);
    DLUMA_INVENTORY_EVENT_U64(
        "previous_count", event->previous.count);
    DLUMA_INVENTORY_EVENT_U64(
        "previous_capacity", event->previous.capacity);
    DLUMA_INVENTORY_EVENT_TEXT(
        "previous_failure",
        dluma_player_inventory_failure_name(
            event->previous_failure));
    DLUMA_INVENTORY_EVENT_HEX(
        "current_data", event->current.data);
    DLUMA_INVENTORY_EVENT_U64(
        "current_count", event->current.count);
    DLUMA_INVENTORY_EVENT_U64(
        "current_capacity", event->current.capacity);
    DLUMA_INVENTORY_EVENT_TEXT(
        "current_failure",
        dluma_player_inventory_failure_name(
            event->current_failure));
    DLUMA_INVENTORY_EVENT_U64(
        "item_vector_contract_applicable",
        event->item_vector_contract_applicable);
    DLUMA_INVENTORY_EVENT_HEX(
        "transition_kind_mask", event->transition_kind_mask);
    DLUMA_INVENTORY_EVENT_U64(
        "transition_latched", event->transition_latched);
#undef DLUMA_INVENTORY_EVENT_TEXT
#undef DLUMA_INVENTORY_EVENT_HEX
#undef DLUMA_INVENTORY_EVENT_U64
}

static void dluma_crash_emit_player_inventory_integrity(void)
{
    static DlumaPlayerInventoryRuntimeSnapshot diagnostics;
    const DlumaPlayerInventoryTreeSnapshot* tree;
    const DlumaPlayerInventoryTreeSnapshot* last_complete;
    const DlumaPlayerInventoryTreeSnapshot* last_valid;
    const DlumaPlayerInventoryFirstTransition* first;
    const DlumaPlayerInventoryInvalidTransitionEvidence*
        invalid_evidence;
    const DlumaPlayerInventoryStateFiveCrashState* state;
    memset(&diagnostics, 0, sizeof(diagnostics));
    if (!dluma_player_inventory_runtime_snapshot(&diagnostics)) {
        dluma_crash_emit_text(
            "player_inventory_recorder_snapshot", "seqlock_busy");
        return;
    }
    tree = &diagnostics.last_tree;
    last_complete = &diagnostics.last_complete_tree;
    last_valid = &diagnostics.last_valid_item_vector_tree;
    first = &diagnostics.first_invalid_transition;
    invalid_evidence = &diagnostics.first_invalid_evidence;
    state = &diagnostics.state_five;
    dluma_crash_emit_text(
        "player_inventory_recorder_snapshot", "available");
    dluma_crash_emit_u64(
        "player_inventory_diagnostic_enabled",
        diagnostics.diagnostic_enabled);
    dluma_crash_emit_u64(
        "player_inventory_exact_profile_validated",
        diagnostics.exact_profile_validated);
    dluma_crash_emit_u64(
        "player_inventory_dropped_writes",
        diagnostics.dropped_writes);
    dluma_crash_emit_u64(
        "player_inventory_clear_depth", diagnostics.clear_depth);
    dluma_crash_emit_u64(
        "player_inventory_observer_depth",
        diagnostics.observer_depth);
    dluma_crash_emit_hex(
        "player_inventory_player_di",
        diagnostics.publication.player_di, 16);
    dluma_crash_emit_hex(
        "player_inventory_owner",
        diagnostics.publication.inventory_owner, 16);
    dluma_crash_emit_u64(
        "player_inventory_creation_generation",
        diagnostics.publication.creation_generation);
    dluma_crash_emit_u64(
        "player_inventory_content_epoch",
        diagnostics.publication.content_epoch);
    dluma_crash_emit_u64(
        "player_inventory_map_generation",
        diagnostics.publication.map_generation);
    dluma_crash_emit_u64(
        "player_inventory_publication_thread",
        diagnostics.publication.owner_thread);
    dluma_crash_emit_u64(
        "player_inventory_publication_sequence",
        diagnostics.publication.publication_sequence);
    dluma_crash_emit_u64(
        "player_inventory_last_capture_sequence",
        tree->capture_sequence);
    dluma_crash_emit_text(
        "player_inventory_last_capture_phase",
        dluma_player_inventory_phase_name(tree->phase));
    dluma_crash_emit_u64(
        "player_inventory_last_capture_thread", tree->thread_id);
    dluma_crash_emit_u64(
        "player_inventory_last_item_vector_rows",
        tree->item_vector_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_valid_item_vector_rows",
        tree->valid_item_vector_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_invalid_item_vector_rows",
        tree->invalid_item_vector_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_non_item_rows",
        tree->non_item_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_absent_rows",
        tree->absent_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_unknown_rows",
        tree->unknown_row_count);
    dluma_crash_emit_u64(
        "player_inventory_last_tree_profile_complete",
        tree->tree_profile_complete);
    dluma_crash_emit_u64(
        "player_inventory_last_item_vector_tree_valid",
        tree->item_vector_tree_valid);
    dluma_crash_emit_u64(
        "player_inventory_last_complete_capture_sequence",
        last_complete->capture_sequence);
    dluma_crash_emit_u64(
        "player_inventory_last_valid_item_vector_capture_sequence",
        last_valid->capture_sequence);
    dluma_crash_emit_text(
        "player_inventory_last_valid_item_vector_capture_phase",
        dluma_player_inventory_phase_name(last_valid->phase));
    dluma_crash_emit_u64(
        "player_inventory_first_invalid_transition_present",
        first->present);
    if (first->present) {
        const DlumaPlayerInventoryEvent* event = &first->event;
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_sequence",
            event->sequence);
        dluma_crash_emit_text(
            "player_inventory_first_invalid_transition_phase",
            dluma_player_inventory_phase_name(event->phase));
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_group",
            event->group_index);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_child",
            event->child_index);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_outer_loop_index",
            event->outer_loop_index);
        dluma_crash_emit_hex(
            "player_inventory_first_invalid_transition_header",
            event->vector_header_address, 16);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_before_count",
            event->previous.count);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_before_capacity",
            event->previous.capacity);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_after_count",
            event->current.count);
        dluma_crash_emit_u64(
            "player_inventory_first_invalid_transition_after_capacity",
            event->current.capacity);
        dluma_crash_emit_text(
            "player_inventory_first_invalid_transition_failure",
            dluma_player_inventory_failure_name(
                event->current_failure));
    }
    dluma_crash_emit_u64(
        "player_inventory_first_any_header_change_present",
        diagnostics.first_any_header_change.present);
    /* Runtime15 report compatibility: this is the optional first-any latch,
     * not the actionable first-invalid latch emitted above. */
    dluma_crash_emit_u64(
        "player_inventory_first_transition_present",
        diagnostics.first_transition.present);
    if (diagnostics.first_transition.present)
        dluma_crash_emit_text(
            "player_inventory_first_transition_phase",
            dluma_player_inventory_phase_name(
                diagnostics.first_transition.event.phase));
    dluma_crash_emit_u64(
        "player_inventory_first_underflow_present",
        diagnostics.first_count_underflow_transition.present);
    dluma_crash_emit_u64(
        "player_inventory_first_over_capacity_present",
        diagnostics.first_count_greater_than_capacity.present);
    dluma_crash_emit_u64(
        "player_inventory_first_unreadable_present",
        diagnostics.first_unreadable_transition.present);
    dluma_crash_emit_u64(
        "player_inventory_first_instability_present",
        diagnostics.first_header_instability.present);
    dluma_crash_emit_u64(
        "player_inventory_first_clear_reentrancy_present",
        diagnostics.first_clear_reentrancy_transition.present);
    dluma_crash_emit_u64(
        "player_inventory_first_invalid_evidence_present",
        invalid_evidence->present);
    dluma_crash_emit_u64(
        "player_inventory_first_invalid_preceding_count",
        invalid_evidence->preceding_count);
    dluma_crash_emit_u64(
        "player_inventory_first_invalid_following_count",
        invalid_evidence->following_count);
    dluma_crash_emit_u64(
        "player_inventory_event_count", diagnostics.event_count);
    dluma_crash_emit_u64(
        "player_inventory_event_overwrite_count",
        diagnostics.event_overwrite_count);
    {
        unsigned int event_slot;
        for (event_slot = 0;
             event_slot < DLUMA_PLAYER_INVENTORY_EVENT_CAPACITY;
             ++event_slot)
            dluma_crash_emit_player_inventory_event(
                event_slot, &diagnostics.events[event_slot]);
    }
    dluma_crash_emit_u64(
        "player_inventory_state_five_sequence",
        state->publication_sequence);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c08_token",
        state->c08_token);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c08_return_count",
        state->c08_return_count);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c10_entry_count",
        state->c10_entry_count);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c08_thread",
        state->c08_thread_id);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c10_thread",
        state->c10_thread_id);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c10_scope_result",
        state->c10_scope_result);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c10_scope_active",
        state->c10_scope_active);
    dluma_crash_emit_u64(
        "player_inventory_state_five_c10_scope_armed",
        state->c10_scope_armed);
    dluma_crash_emit_u64(
        "player_inventory_state_five_armed_epoch",
        state->armed_epoch);
    dluma_crash_emit_hex(
        "player_inventory_state_five_outgoing_session",
        state->outgoing_session, 16);
    dluma_crash_emit_u64(
        "player_inventory_session_on_destroy_entry_count",
        state->session_on_destroy_entry_count);
    dluma_crash_emit_u64(
        "player_inventory_session_binding_matched",
        state->session_binding_matched);
    dluma_crash_emit_hex(
        "player_inventory_session_on_destroy_session",
        state->session_on_destroy_session, 16);
    dluma_crash_emit_u64(
        "player_inventory_unload_level_active_depth",
        state->unload_level_active_depth);
    dluma_crash_emit_u64(
        "player_inventory_unload_level_thread",
        state->unload_level_thread_id);
    dluma_crash_emit_hex(
        "player_inventory_unload_level_session",
        state->unload_level_session, 16);
    dluma_crash_emit_u64(
        "player_inventory_periodic_report_finalized",
        state->periodic_report_finalized);
    dluma_crash_emit_u64(
        "player_inventory_periodic_report_stale",
        state->periodic_report_stale);
}

static void dluma_crash_emit_playerdi_vector_fault(
    const EXCEPTION_RECORD* record,
    const DlumaX64Context* context,
    const DlumaCrashModule* fault_module,
    ULONGLONG fault_address)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    DlumaPlayerDiVectorCrashInput input;
    DlumaPlayerDiVectorCrashEvidence evidence;
    unsigned char signature[DLUMA_PLAYERDI_VECTOR_COPY_SIGNATURE_SIZE];
    ULONGLONG fault_rva;
    uintptr_t signature_address;
    int signature_validated = 0;
    if (!record || !context || !fault_module ||
        fault_address < fault_module->base ||
        fault_module->classification.module_class !=
            DLUMA_CRASH_CORE_MODULE_GAME_MODULE ||
        !fault_module->classification.identity_validated ||
        dluma_wstricmp(
            dluma_crash_basename(fault_module->path),
            L"gamedll_x64_rwdi.dll") != 0)
        return;
    fault_rva = fault_address - fault_module->base;
    if (fault_rva != DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA)
        return;

    memset(&diagnostics, 0, sizeof(diagnostics));
    memset(&input, 0, sizeof(input));
    memset(&evidence, 0, sizeof(evidence));
    memset(signature, 0, sizeof(signature));
    dluma_playerdi_teardown_snapshot(&diagnostics);
    if (fault_module->base <=
            ~(uintptr_t)0 - DLUMA_PLAYERDI_VECTOR_COPY_SIGNATURE_RVA) {
        signature_address =
            (uintptr_t)fault_module->base +
            DLUMA_PLAYERDI_VECTOR_COPY_SIGNATURE_RVA;
        signature_validated = dluma_crash_playerdi_safe_read(
            0, signature_address, signature, sizeof(signature)) &&
            dluma_playerdi_vector_copy_signature_matches(
                signature, sizeof(signature));
    }
    input.module_timestamp = fault_module->pe_timestamp;
    input.module_image_size = fault_module->image_size;
    input.fault_rva = (uint32_t)fault_rva;
    input.supported_profile_validated =
        diagnostics.supported_profile_validated;
    input.instruction_signature_validated =
        signature_validated ? 1U : 0U;
    input.access_operation_is_read =
        record->NumberParameters >= 2U &&
        record->ExceptionInformation[0] == 0U;
    input.access_target =
        record->NumberParameters >= 2U
            ? (uintptr_t)record->ExceptionInformation[1]
            : 0;
    input.rbx = (uintptr_t)context->rbx;
    input.rdi = (uintptr_t)context->rdi;
    input.rsi = (uintptr_t)context->rsi;
    if (!dluma_playerdi_decode_vector_copy_fault(
            &input, dluma_crash_playerdi_safe_read,
            dluma_crash_playerdi_query_memory, 0, &evidence)) {
        dluma_crash_emit_text(
            "playerdi_vector_fault_decoder",
            "exact_supported_signature_rejected");
        return;
    }

    dluma_crash_emit_text(
        "playerdi_vector_fault_decoder",
        "exact_supported_signature_accepted");
    dluma_crash_emit_hex(
        "playerdi_vector_source_header",
        evidence.source_vector_address, 16);
    {
        uint32_t source_group = UINT32_MAX;
        uint32_t source_child = UINT32_MAX;
        const int source_mapped =
            dluma_player_inventory_runtime_find_header(
                evidence.source_vector_address,
                &source_group, &source_child);
        dluma_crash_emit_u64(
            "playerdi_vector_source_tree_mapped",
            source_mapped ? 1U : 0U);
        if (source_mapped) {
            dluma_crash_emit_u64(
                "playerdi_vector_source_group", source_group);
            dluma_crash_emit_u64(
                "playerdi_vector_source_child", source_child);
        }
    }
    dluma_crash_emit_hex(
        "playerdi_vector_destination_header",
        evidence.destination_vector_address, 16);
    dluma_crash_emit_u64(
        "playerdi_vector_source_header_readable",
        evidence.source_header_readable);
    dluma_crash_emit_hex(
        "playerdi_vector_source_data",
        evidence.source.data, 16);
    dluma_crash_emit_u64(
        "playerdi_vector_source_count",
        evidence.source.count);
    dluma_crash_emit_u64(
        "playerdi_vector_source_capacity",
        evidence.source.capacity);
    dluma_crash_emit_hex(
        "playerdi_vector_source_tag_or_field",
        evidence.source.tag_or_field, 8);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_header_readable",
        evidence.destination_header_readable);
    dluma_crash_emit_hex(
        "playerdi_vector_destination_data",
        evidence.destination.data, 16);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_count",
        evidence.destination.count);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_capacity",
        evidence.destination.capacity);
    dluma_crash_emit_hex(
        "playerdi_vector_destination_tag_or_field",
        evidence.destination.tag_or_field, 8);
    dluma_crash_emit_u64(
        "playerdi_vector_element_index",
        evidence.element_index);
    dluma_crash_emit_u64(
        "playerdi_vector_element_byte_offset",
        evidence.element_byte_offset);
    dluma_crash_emit_u64(
        "playerdi_vector_index_multiplication_valid",
        evidence.index_multiplication_valid);
    dluma_crash_emit_hex(
        "playerdi_vector_inferred_source_data",
        evidence.inferred_source_data, 16);
    dluma_crash_emit_hex(
        "playerdi_vector_computed_source_element",
        evidence.computed_source_element, 16);
    dluma_crash_emit_hex(
        "playerdi_vector_computed_destination_element",
        evidence.computed_destination_element, 16);
    dluma_crash_emit_u64(
        "playerdi_vector_computed_source_element_valid",
        evidence.computed_source_element_valid);
    dluma_crash_emit_u64(
        "playerdi_vector_computed_destination_element_valid",
        evidence.computed_destination_element_valid);
    dluma_crash_emit_u64(
        "playerdi_vector_source_element_readable",
        evidence.source_element_readable);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_element_readable",
        evidence.destination_element_readable);
    if (evidence.source_element_readable)
        dluma_crash_emit_hex(
            "playerdi_vector_source_element_value",
            evidence.source_element_value, 16);
    if (evidence.destination_element_readable)
        dluma_crash_emit_hex(
            "playerdi_vector_destination_element_value",
            evidence.destination_element_value, 16);
    dluma_crash_emit_u64(
        "playerdi_vector_index_less_than_count",
        evidence.index_less_than_count);
    dluma_crash_emit_u64(
        "playerdi_vector_index_less_than_capacity",
        evidence.index_less_than_capacity);
    dluma_crash_emit_u64(
        "playerdi_vector_count_not_greater_than_capacity",
        evidence.count_not_greater_than_capacity);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_index_less_than_count",
        evidence.destination_index_less_than_count);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_index_less_than_capacity",
        evidence.destination_index_less_than_capacity);
    dluma_crash_emit_u64(
        "playerdi_vector_destination_count_not_greater_than_capacity",
        evidence.destination_count_not_greater_than_capacity);
    dluma_crash_emit_u64(
        "playerdi_vector_fault_matches_computed_source_element",
        evidence.fault_matches_computed_source_element);
    dluma_crash_emit_u64(
        "playerdi_vector_source_bytes_to_region_end",
        evidence.source_bytes_readable_to_region_end);
    dluma_crash_emit_u64(
        "playerdi_vector_source_qwords_to_region_end",
        evidence.source_qwords_readable_to_region_end);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_source_header_region",
        &evidence.source_header_region);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_source_data_region",
        &evidence.source_data_region);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_source_element_region",
        &evidence.source_element_region);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_destination_header_region",
        &evidence.destination_header_region);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_destination_data_region",
        &evidence.destination_data_region);
    dluma_crash_emit_playerdi_region(
        "playerdi_vector_destination_element_region",
        &evidence.destination_element_region);
}

static void dluma_crash_emit_context(const DlumaX64Context* context)
{
    unsigned int index;
    unsigned int frame_count = 0;
    unsigned int stack_count = 0;
    const char* unwind_status = "complete";
    const char* stack_status = "complete";
    DlumaX64Context unwind;
    if (!context) {
        dluma_crash_emit_text("context", "unavailable");
        return;
    }
    dluma_crash_emit_hex("context_flags", context->context_flags, 8);
    dluma_crash_emit_hex("mxcsr", context->mx_csr, 8);
    dluma_crash_emit_hex("eflags", context->eflags, 8);
    dluma_crash_emit_hex("seg_cs", context->seg_cs, 4);
    dluma_crash_emit_hex("seg_ds", context->seg_ds, 4);
    dluma_crash_emit_hex("seg_es", context->seg_es, 4);
    dluma_crash_emit_hex("seg_fs", context->seg_fs, 4);
    dluma_crash_emit_hex("seg_gs", context->seg_gs, 4);
    dluma_crash_emit_hex("seg_ss", context->seg_ss, 4);
    dluma_crash_emit_register("RAX", context->rax);
    dluma_crash_emit_register("RBX", context->rbx);
    dluma_crash_emit_register("RCX", context->rcx);
    dluma_crash_emit_register("RDX", context->rdx);
    dluma_crash_emit_register("RSI", context->rsi);
    dluma_crash_emit_register("RDI", context->rdi);
    dluma_crash_emit_register("RBP", context->rbp);
    dluma_crash_emit_register("RSP", context->rsp);
    dluma_crash_emit_register("R8", context->r8);
    dluma_crash_emit_register("R9", context->r9);
    dluma_crash_emit_register("R10", context->r10);
    dluma_crash_emit_register("R11", context->r11);
    dluma_crash_emit_register("R12", context->r12);
    dluma_crash_emit_register("R13", context->r13);
    dluma_crash_emit_register("R14", context->r14);
    dluma_crash_emit_register("R15", context->r15);
    dluma_crash_emit_register("RIP", context->rip);
    dluma_crash_emit_register("DR0", context->dr0);
    dluma_crash_emit_register("DR1", context->dr1);
    dluma_crash_emit_register("DR2", context->dr2);
    dluma_crash_emit_register("DR3", context->dr3);
    dluma_crash_emit_register("DR6", context->dr6);
    dluma_crash_emit_register("DR7", context->dr7);
    unwind = *context;
    for (index = 0; index < DLUMA_CRASH_MAX_FRAMES && unwind.rip; ++index) {
        ULONGLONG old_rip = unwind.rip;
        ULONGLONG old_rsp = unwind.rsp;
        DlumaRuntimeFunction* function_entry = 0;
        ULONGLONG image_base = 0;
        dluma_crash_emit_module_address("stack_frame", index, unwind.rip);
        ++frame_count;
        if (g_dluma_crash.rtl_lookup_function_entry &&
            g_dluma_crash.rtl_virtual_unwind)
            function_entry = g_dluma_crash.rtl_lookup_function_entry(
                unwind.rip, &image_base, 0);
        if (function_entry) {
            void* handler_data = 0;
            ULONGLONG establisher = 0;
            (void)g_dluma_crash.rtl_virtual_unwind(
                0, image_base, unwind.rip, function_entry,
                &unwind, &handler_data, &establisher, 0);
        } else {
            ULONGLONG* stack = (ULONGLONG*)(ULONG_PTR)unwind.rsp;
            if (!dluma_is_readable(stack, sizeof(*stack))) {
                unwind_status = "leaf_stack_unreadable";
                break;
            }
            unwind.rip = *stack;
            unwind.rsp += sizeof(*stack);
        }
        switch (dluma_crash_core_unwind_step(
                    old_rip, old_rsp, unwind.rip, unwind.rsp)) {
        case DLUMA_CRASH_CORE_UNWIND_COMPLETE:
            break;
        case DLUMA_CRASH_CORE_UNWIND_NON_PROGRESS:
            unwind_status = "non_progress";
            break;
        case DLUMA_CRASH_CORE_UNWIND_STACK_REGRESSED:
            unwind_status = "stack_regressed";
            break;
        default:
            continue;
        }
        break;
    }
    if (frame_count == DLUMA_CRASH_MAX_FRAMES && unwind.rip)
        unwind_status = "truncated";
    dluma_crash_emit_u64("stack_frame_count", frame_count);
    dluma_crash_emit_text("stack_unwind_status", unwind_status);
    for (index = 0; index < DLUMA_CRASH_MAX_STACK_WORDS; ++index) {
        ULONGLONG address =
            context->rsp + (ULONGLONG)index * sizeof(ULONGLONG);
        const ULONGLONG* word = (const ULONGLONG*)(ULONG_PTR)address;
        char number[32];
        if (address < context->rsp ||
            !dluma_is_readable(word, sizeof(*word))) {
            stack_status = stack_count
                ? "bounded_by_unreadable_memory" : "unavailable";
            break;
        }
        g_dluma_crash.line[0] = 0;
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            "[CRASH] [INFO] stack_word index=");
        dluma_u64_to_text(index, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), number);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " address=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            address, 16);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " value=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            *word, 16);
        {
            const DlumaCrashModule* module =
                dluma_crash_module_for_address(*word);
            if (module) {
                char path[DLUMA_MAX_WPATH * 3];
                path[0] = 0;
                (void)dluma_wide_to_utf8(
                    module->path, path, DLUMA_ARRAY_COUNT(path));
                dluma_str_append(
                    g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                    " candidate_module=");
                dluma_str_append(
                    g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                    path);
                dluma_str_append(
                    g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                    " candidate_offset=");
                dluma_crash_append_hex_text(
                    g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                    *word - module->base, 16);
            }
        }
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
        dluma_crash_emit_raw(g_dluma_crash.line);
        ++stack_count;
    }
    if (stack_count == DLUMA_CRASH_MAX_STACK_WORDS)
        stack_status = "truncated";
    dluma_crash_emit_u64("stack_sample_count", stack_count);
    dluma_crash_emit_text("stack_sample_status", stack_status);
}

static void dluma_crash_emit_exception_chain(EXCEPTION_RECORD* record)
{
    unsigned int record_index;
    EXCEPTION_RECORD* seen[DLUMA_CRASH_MAX_EXCEPTION_RECORDS];
    memset(seen, 0, sizeof(seen));
    for (record_index = 0;
         record && record_index < DLUMA_CRASH_MAX_EXCEPTION_RECORDS;
         ++record_index) {
        unsigned int seen_index;
        unsigned int parameter_index;
        unsigned int parameter_count;
        char number[32];
        for (seen_index = 0; seen_index < record_index; ++seen_index)
            if (seen[seen_index] == record) {
                dluma_crash_emit_text(
                    "exception_chain_status", "cyclic");
                return;
            }
        if (!dluma_is_readable(record, sizeof(*record))) {
            dluma_crash_emit_text(
                "exception_chain_status", "unreadable");
            return;
        }
        seen[record_index] = record;
        g_dluma_crash.line[0] = 0;
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            "[CRASH] [INFO] exception_record index=");
        dluma_u64_to_text(
            record_index, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), number);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " code=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            record->ExceptionCode, 8);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " flags=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            record->ExceptionFlags, 8);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " address=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            (ULONGLONG)(ULONG_PTR)record->ExceptionAddress, 16);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " parameters=");
        dluma_u64_to_text(
            record->NumberParameters, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), number);
        if (record->NumberParameters > 15U)
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " malformed=1");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
        dluma_crash_emit_raw(g_dluma_crash.line);
        parameter_count =
            record->NumberParameters < 15U
                ? record->NumberParameters : 15U;
        for (parameter_index = 0;
             parameter_index < parameter_count; ++parameter_index) {
            g_dluma_crash.line[0] = 0;
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                "[CRASH] [INFO] exception_parameter record=");
            dluma_u64_to_text(
                record_index, number, DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                number);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " index=");
            dluma_u64_to_text(
                parameter_index, number, DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                number);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                " value=");
            dluma_crash_append_hex_text(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                record->ExceptionInformation[parameter_index], 16);
            dluma_str_append(
                g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
                "\r\n");
            dluma_crash_emit_raw(g_dluma_crash.line);
        }
        record = record->ExceptionRecord;
    }
    if (record)
        dluma_crash_emit_text("exception_chain_status", "truncated");
}

static void dluma_crash_emit_module_snapshot(void)
{
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.module_snapshot_active, 0, 0);
    const DlumaCrashModuleSet* set =
        &g_dluma_crash.module_sets[active ? 1 : 0];
    unsigned int index;
    dluma_crash_emit_u64("module_snapshot_count", set->count);
    dluma_crash_emit_u64("module_snapshot_total", set->total);
    dluma_crash_emit_u64("module_snapshot_truncated", set->truncated);
    dluma_crash_emit_u64(
        "module_snapshot_captured_ms", set->captured_ms);
    for (index = 0; index < set->count; ++index) {
        const DlumaCrashModule* item = &set->items[index];
        char path[DLUMA_MAX_WPATH * 3];
        char number[32];
        path[0] = 0;
        (void)dluma_wide_to_utf8(
            item->path, path, DLUMA_ARRAY_COUNT(path));
        g_dluma_crash.line[0] = 0;
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            "[CRASH] [INFO] module index=");
        dluma_u64_to_text(index, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), number);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " base=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->base, 16);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " size=");
        dluma_u64_to_text(
            item->image_size, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), number);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " pe_timestamp=");
        dluma_crash_append_hex_text(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->pe_timestamp, 8);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " sha256=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->sha256_available
                ? item->sha256 : "<unavailable>");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " class=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            dluma_crash_core_module_class_text(
                item->classification.module_class));
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " first_party=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->classification.first_party ? "1" : "0");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " identity_validated=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->classification.identity_validated ? "1" : "0");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " profile=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            item->classification.profile
                ? item->classification.profile : "<none>");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
            " path=");
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), path);
        dluma_str_append(
            g_dluma_crash.line, DLUMA_ARRAY_COUNT(g_dluma_crash.line), "\r\n");
        dluma_crash_emit_raw(g_dluma_crash.line);
    }
}

static void dluma_crash_emit_component_contexts(void)
{
    unsigned int attempt;
    int host_copied = 0;
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_crash.runtime_context_active, 0, 0);
    const char* runtime =
        g_dluma_crash.runtime_contexts[active ? 1 : 0];
    if (runtime[0]) dluma_crash_emit_raw(runtime);
    else dluma_crash_emit_text("runtime_snapshot", "unavailable");
    for (attempt = 0; attempt < 2U; ++attempt) {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_crash.host_context_sequence, 0, 0);
        LONG host_active;
        LONG after;
        if (before & 1) continue;
        host_active = dluma_atomic_compare_exchange(
            &g_dluma_crash.host_context_active, 0, 0);
        dluma_str_copy(
            g_dluma_crash.component,
            DLUMA_ARRAY_COUNT(g_dluma_crash.component),
            g_dluma_crash.host_contexts[host_active ? 1 : 0]);
        after = dluma_atomic_compare_exchange(
            &g_dluma_crash.host_context_sequence, 0, 0);
        if (before == after && !(after & 1)) {
            host_copied = g_dluma_crash.component[0] != 0;
            break;
        }
    }
    if (host_copied) dluma_crash_emit_raw(g_dluma_crash.component);
    else dluma_crash_emit_text("host_snapshot", "unavailable");
    g_dluma_crash.component[0] = 0;
    if (dluma_editor_animation_bridge_copy_crash_context(
            g_dluma_crash.component,
            DLUMA_ARRAY_COUNT(g_dluma_crash.component)))
        dluma_crash_emit_raw(g_dluma_crash.component);
    else
        dluma_crash_emit_text(
            "animation_reload_snapshot", "unavailable");
    g_dluma_crash.component[0] = 0;
    if (dluma_native_mods_copy_crash_context(
            g_dluma_crash.component,
            DLUMA_ARRAY_COUNT(g_dluma_crash.component)))
        dluma_crash_emit_raw(g_dluma_crash.component);
    else
        dluma_crash_emit_text("native_mod_snapshot", "unavailable");
    g_dluma_crash.component[0] = 0;
    if (dluma_managed_mods_copy_crash_context(
            g_dluma_crash.component,
            DLUMA_ARRAY_COUNT(g_dluma_crash.component)))
        dluma_crash_emit_raw(g_dluma_crash.component);
    else
        dluma_crash_emit_text("managed_mod_snapshot", "unavailable");
    g_dluma_crash.component[0] = 0;
    if (dluma_lua_runtime_copy_crash_context(
            g_dluma_crash.component,
            DLUMA_ARRAY_COUNT(g_dluma_crash.component)))
        dluma_crash_emit_raw(g_dluma_crash.component);
    else
        dluma_crash_emit_text("lua_mod_snapshot", "unavailable");
}

static void dluma_crash_write_report(const DlumaCrashIncident* incident)
{
    SYSTEMTIME utc;
    SYSTEMTIME local;
    char time_text[64];
    EXCEPTION_RECORD* record = 0;
    const DlumaX64Context* context = 0;
    const DlumaCrashModule* fault_module = 0;
    ULONGLONG address = 0;
    DlumaCrashCoreModuleClassification classification;
    DlumaMemoryStatus memory_status;
    FILETIME process_created;
    FILETIME process_exit;
    FILETIME process_kernel;
    FILETIME process_user;
    DWORD exception_code = incident ? incident->exception_code : 0;
    EXCEPTION_POINTERS* exception_pointers =
        incident && incident->context_copy_status
            ? (EXCEPTION_POINTERS*)&incident->exception_pointers : 0;
    DlumaEditorExitIntentSnapshot editor_exit;
    int editor_exit_available = 0;
    GetSystemTime(&utc);
    GetLocalTime(&local);
    if (exception_pointers &&
        dluma_is_readable(exception_pointers, sizeof(*exception_pointers))) {
        if (exception_pointers->ExceptionRecord &&
            dluma_is_readable(
                exception_pointers->ExceptionRecord,
                sizeof(*exception_pointers->ExceptionRecord))) {
            record = exception_pointers->ExceptionRecord;
            if (record->ExceptionCode)
                exception_code = record->ExceptionCode;
            address = (ULONGLONG)(ULONG_PTR)record->ExceptionAddress;
        }
        if (exception_pointers->ContextRecord &&
            dluma_is_readable(
                exception_pointers->ContextRecord,
                sizeof(DlumaX64Context)))
            context =
                (const DlumaX64Context*)exception_pointers->ContextRecord;
    }
    classification =
        dluma_crash_describe_fault(address, &fault_module);
    dluma_crash_emit_text("report_begin", "DLW_CRASH_REPORT_V1");
    dluma_crash_emit_text("report_mode", "reserved_worker");
    dluma_crash_format_system_time(
        &utc, time_text, DLUMA_ARRAY_COUNT(time_text), 1);
    dluma_crash_emit_text("time_utc", time_text);
    dluma_crash_format_system_time(
        &local, time_text, DLUMA_ARRAY_COUNT(time_text), 0);
    dluma_crash_emit_text("time_local", time_text);
    dluma_crash_emit_u64("process_id", GetCurrentProcessId());
    dluma_crash_emit_u64(
        "thread_id", incident ? incident->fault_thread_id : 0U);
    dluma_crash_emit_u64(
        "fault_thread_id", incident ? incident->fault_thread_id : 0U);
    dluma_crash_emit_u64(
        "reporter_thread_id", incident ? incident->reporter_thread_id : 0U);
    dluma_crash_emit_u64(
        "incident_captured_ms", incident ? incident->captured_ms : 0ULL);
    dluma_crash_emit_u64(
        "context_copy_status", incident ? incident->context_copy_status : 0U);
    dluma_crash_emit_hex(
        "captured_instruction_pointer",
        incident ? incident->instruction_pointer : 0ULL, 16);
    dluma_crash_emit_hex(
        "captured_stack_pointer",
        incident ? incident->stack_pointer : 0ULL, 16);
    dluma_crash_emit_u64("uptime_ms", GetTickCount64());
    dluma_crash_emit_u64("debugger_present", IsDebuggerPresent() ? 1U : 0U);
    dluma_crash_emit_text("loader_version", DLUMA_VERSION);
    dluma_crash_emit_text(
        "loader_build_identity", dluma_build_identity_text());
    dluma_crash_emit_text(
        "loader_git_head", dluma_build_identity_git_head());
    dluma_crash_emit_u64(
        "loader_worktree_dirty", dluma_build_worktree_dirty() ? 1U : 0U);
    dluma_crash_emit_wide(
        "executable_path", g_dluma_crash.executable_path);
    dluma_crash_emit_wide("loader_path", g_dluma_crash.loader_path);
    dluma_crash_emit_wide("loader_log_path", g_dluma.log_path);
    dluma_crash_emit_wide("crash_info_path", g_dluma_crash.report_path);
    dluma_crash_emit_wide("dump_directory", g_dluma_crash.dump_dir);
    dluma_crash_emit_text("dump_path", "<stock-managed-unavailable>");
    if (g_dluma_crash.editor_host) {
        editor_exit_available =
            dluma_editor_exit_observer_copy_snapshot(&editor_exit);
        if (!editor_exit_available)
            dluma_editor_exit_intent_initialize(&editor_exit, 0U);
        dluma_crash_emit_text(
            "editor_exit_intent",
            dluma_editor_exit_intent_state_text(editor_exit.state));
        dluma_crash_emit_text(
            "editor_exit_reason",
            dluma_editor_exit_intent_reason_text(editor_exit.reason));
        dluma_crash_emit_u64(
            "editor_exit_generation", editor_exit.generation);
        dluma_crash_emit_u64(
            "editor_exit_observed_ms", editor_exit.observed_ms);
        dluma_crash_emit_u64(
            "editor_exit_elapsed_ms",
            editor_exit.observed_ms && GetTickCount64() >= editor_exit.observed_ms
                ? GetTickCount64() - editor_exit.observed_ms : 0ULL);
        if (editor_exit_available &&
            dluma_editor_exit_intent_should_suppress(&editor_exit)) {
            dluma_atomic_exchange(
                &g_dluma_crash.dialog_suppressed_editor_shutdown, 1);
            dluma_crash_emit_text(
                "crash_dialog_suppressed", "editor_shutdown");
        } else {
            dluma_crash_emit_text("crash_dialog_suppressed", "none");
        }
    }
    if (g_dluma_crash.os_version.major) {
        dluma_crash_emit_u64(
            "os_major", g_dluma_crash.os_version.major);
        dluma_crash_emit_u64(
            "os_minor", g_dluma_crash.os_version.minor);
        dluma_crash_emit_u64(
            "os_build", g_dluma_crash.os_version.build);
    } else {
        dluma_crash_emit_text("os_version", "unavailable");
    }
    dluma_crash_emit_u64(
        "processor_architecture",
        g_dluma_crash.system_info.processor.architecture.
            processor_architecture);
    dluma_crash_emit_u64(
        "processor_count", g_dluma_crash.system_info.processor_count);
    dluma_crash_emit_u64(
        "page_size", g_dluma_crash.system_info.page_size);
    memset(&memory_status, 0, sizeof(memory_status));
    memory_status.length = sizeof(memory_status);
    if (GlobalMemoryStatusEx(&memory_status)) {
        dluma_crash_emit_u64(
            "memory_load_percent", memory_status.memory_load);
        dluma_crash_emit_u64(
            "memory_total_physical", memory_status.total_physical);
        dluma_crash_emit_u64(
            "memory_available_physical",
            memory_status.available_physical);
        dluma_crash_emit_u64(
            "memory_total_virtual", memory_status.total_virtual);
        dluma_crash_emit_u64(
            "memory_available_virtual",
            memory_status.available_virtual);
    } else {
        dluma_crash_emit_text("memory_status", "unavailable");
    }
    if (GetProcessTimes(
            GetCurrentProcess(), &process_created, &process_exit,
            &process_kernel, &process_user)) {
        dluma_crash_emit_u64(
            "process_created_filetime",
            dluma_crash_filetime_u64(&process_created));
        dluma_crash_emit_u64(
            "process_kernel_100ns",
            dluma_crash_filetime_u64(&process_kernel));
        dluma_crash_emit_u64(
            "process_user_100ns",
            dluma_crash_filetime_u64(&process_user));
    } else {
        dluma_crash_emit_text("process_times", "unavailable");
    }
    dluma_crash_emit_text(
        "thread_created_filetime", "unavailable_off_fault_thread");
    dluma_crash_emit_text(
        "thread_kernel_100ns", "unavailable_off_fault_thread");
    dluma_crash_emit_text(
        "thread_user_100ns", "unavailable_off_fault_thread");
    dluma_crash_emit_text(
        "exception_name",
        dluma_crash_core_exception_name(exception_code));
    dluma_crash_emit_hex("exception_code", exception_code, 8);
    dluma_crash_emit_hex("fault_address", address, 16);
    dluma_crash_emit_text(
        "fault_classification",
        dluma_crash_core_module_class_text(
            classification.module_class));
    dluma_crash_emit_text(
        "fault_module_class",
        dluma_crash_core_module_class_text(
            classification.module_class));
    dluma_crash_emit_u64(
        "fault_module_first_party",
        classification.first_party ? 1U : 0U);
    dluma_crash_emit_u64(
        "fault_module_identity_validated",
        classification.identity_validated ? 1U : 0U);
    dluma_crash_emit_text(
        "fault_module_profile",
        classification.profile ? classification.profile : "<none>");
    dluma_crash_emit_u64(
        "fault_location_is_not_root_cause_proof", 1U);
    if (fault_module) {
        dluma_crash_emit_wide("fault_module_path", fault_module->path);
        dluma_crash_emit_hex(
            "fault_module_base", fault_module->base, 16);
        dluma_crash_emit_hex(
            "fault_module_offset",
            address - fault_module->base, 16);
    } else {
        dluma_crash_emit_text("fault_module_path", "unmapped");
    }
    if (record &&
        (record->ExceptionCode == DLUMA_EXCEPTION_ACCESS_VIOLATION ||
         record->ExceptionCode == DLUMA_EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters >= 2U) {
        const char* operation =
            dluma_crash_core_access_operation(
                record->ExceptionInformation[0]);
        dluma_crash_emit_text("access_operation", operation);
        dluma_crash_emit_hex(
            "access_target",
            record->ExceptionInformation[1], 16);
        if (record->ExceptionCode == DLUMA_EXCEPTION_IN_PAGE_ERROR &&
            record->NumberParameters >= 3U)
            dluma_crash_emit_hex(
                "in_page_status",
                record->ExceptionInformation[2], 8);
    }
    dluma_crash_emit_playerdi_teardown_scope();
    dluma_crash_emit_player_inventory_integrity();
    dluma_crash_emit_playerdi_vector_fault(
        record, context, fault_module, address);
    if (record) dluma_crash_emit_exception_chain(record);
    else dluma_crash_emit_text("exception_record", "unavailable");
    dluma_crash_emit_context(context);
    dluma_crash_emit_module_snapshot();
    dluma_crash_emit_component_contexts();
}

static void dluma_crash_finish_report(const DlumaCrashIncident* incident)
{
    dluma_crash_emit_final_u64(
        "stock_dump_result",
        (ULONGLONG)(DWORD)(incident ? incident->stock_result : 0));
    dluma_crash_emit_final_u64(
        "stock_dump_result_available",
        incident ? incident->stock_result_available : 0U);
    dluma_crash_emit_final_u64(
        "stock_callback_exception_code",
        incident ? incident->stock_callback_exception_code : 0U);
    dluma_crash_emit_final_u64(
        "stock_callback_exception_address",
        incident ? incident->stock_callback_exception_address : 0ULL);
    dluma_crash_emit_final_u64(
        "reporter_exception_code",
        incident ? incident->reporter_exception_code : 0U);
    dluma_crash_emit_final_u64(
        "reporter_exception_address",
        incident ? incident->reporter_exception_address : 0ULL);
    dluma_crash_emit_final_u64(
        "dedicated_write_failures",
        g_dluma_crash.report_write_failures);
    dluma_crash_emit_final_u64(
        "report_truncated", g_dluma_crash.report_truncated ? 1U : 0U);
    dluma_crash_emit_final_text("report_end", "DLW_CRASH_REPORT_V1");
    if (g_dluma_crash.report_file_valid &&
        g_dluma_crash.report_handle &&
        g_dluma_crash.report_handle != INVALID_HANDLE_VALUE)
        (void)FlushFileBuffers(g_dluma_crash.report_handle);
    dluma_atomic_exchange(&g_dluma_crash.report_state, 2);
}

static void dluma_crash_append_hex_wide(
    WCHAR* out, unsigned int capacity,
    ULONGLONG value, unsigned int digits)
{
    static const WCHAR hex[] = L"0123456789ABCDEF";
    WCHAR number[19];
    unsigned int i;
    if (digits > 16U) digits = 16U;
    number[0] = L'0';
    number[1] = L'x';
    for (i = 0; i < digits; ++i)
        number[2U + i] =
            hex[(value >> ((digits - i - 1U) * 4U)) & 0xFU];
    number[2U + digits] = 0;
    dluma_wstr_append(out, capacity, number);
}

static const WCHAR* dluma_crash_pick_title(
    DWORD exception_code, const void* address)
{
    unsigned int available[DLUMA_CRASH_TITLE_COUNT];
    unsigned int count = 0;
    unsigned int i;
    ULONGLONG seed;
    if (!g_dluma.config.crash_random_titles) {
        if (g_dluma_crash.editor_host)
            return g_dluma_crash.text.editor_neutral_title;
        return g_dluma_crash.text.neutral_title;
    }
    for (i = 0; i < DLUMA_CRASH_TITLE_COUNT; ++i)
        if (g_dluma_crash.text.titles[i][0]) available[count++] = i;
    if (!count) {
        if (g_dluma_crash.editor_host)
            return g_dluma_crash.text.editor_neutral_title;
        return g_dluma_crash.text.neutral_title;
    }
    seed = GetTickCount64() ^ (ULONGLONG)(ULONG_PTR)address ^ exception_code;
    return g_dluma_crash.text.titles[available[(unsigned int)(seed % count)]];
}

static void dluma_crash_show_dialog(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
    WCHAR module_name[DLUMA_MAX_WPATH];
    const WCHAR* classification_text;
    ULONGLONG address = 0;
    ULONGLONG module_offset = 0;
    const DlumaCrashModule* module = 0;
    DlumaCrashCoreModuleClassification classification;
    WCHAR* body = g_dluma_crash.dialog_body;
    if (exception_pointers &&
        dluma_is_readable(exception_pointers, sizeof(*exception_pointers)) &&
        exception_pointers->ExceptionRecord &&
        dluma_is_readable(
            exception_pointers->ExceptionRecord,
            sizeof(*exception_pointers->ExceptionRecord))) {
        EXCEPTION_RECORD* record = exception_pointers->ExceptionRecord;
        if (record->ExceptionCode) exception_code = record->ExceptionCode;
        address = (ULONGLONG)(ULONG_PTR)record->ExceptionAddress;
    }
    classification =
        dluma_crash_describe_fault(address, &module);
    if (module) {
        dluma_wstr_copy(
            module_name, DLUMA_ARRAY_COUNT(module_name),
            dluma_crash_basename(module->path));
        module_offset = address - module->base;
    } else {
        dluma_wstr_copy(
            module_name, DLUMA_ARRAY_COUNT(module_name), L"<unmapped>");
    }
    if (classification.module_class ==
        DLUMA_CRASH_CORE_MODULE_LOADER_MODULE)
        classification_text = g_dluma_crash.text.body_loader;
    else if (
        classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_GAME_EXECUTABLE ||
        classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_GAME_MODULE)
        classification_text = g_dluma_crash.text.body_game;
    else if (
        classification.module_class ==
        DLUMA_CRASH_CORE_MODULE_WINDOWS_SYSTEM_MODULE)
        classification_text = g_dluma_crash.text.body_windows;
    else if (
        classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_KNOWN_THIRD_PARTY_MODULE ||
        classification.module_class ==
            DLUMA_CRASH_CORE_MODULE_UNKNOWN_LOADED_MODULE)
        classification_text = g_dluma_crash.text.body_external;
    else
        classification_text = g_dluma_crash.text.body_unknown;
    body[0] = 0;
    if (g_dluma_crash.editor_host) {
        dluma_wstr_append(body, 4096U, g_dluma_crash.text.editor_context);
        dluma_wstr_append(body, 4096U, L"\r\n\r\n");
    }
    dluma_wstr_append(body, 4096U, classification_text);
    dluma_wstr_append(body, 4096U, L"\r\n\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.text.exception_label);
    dluma_wstr_append(body, 4096U, L": ");
    dluma_crash_append_hex_wide(body, 4096U, exception_code, 8);
    dluma_wstr_append(body, 4096U, L"\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.text.module_label);
    dluma_wstr_append(body, 4096U, L": ");
    dluma_wstr_append(body, 4096U, module_name);
    dluma_wstr_append(body, 4096U, L" + ");
    dluma_crash_append_hex_wide(body, 4096U, module_offset, 16);
    dluma_wstr_append(body, 4096U, L"\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.text.address_label);
    dluma_wstr_append(body, 4096U, L": ");
    dluma_crash_append_hex_wide(body, 4096U, address, 16);
    dluma_wstr_append(body, 4096U, L"\r\n\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.text.report_label);
    dluma_wstr_append(body, 4096U, L":\r\n");
    dluma_wstr_append(
        body, 4096U,
        g_dluma_crash.report_path[0]
            ? g_dluma_crash.report_path : L"<unavailable>");
    dluma_wstr_append(body, 4096U, L"\r\n\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.text.dump_label);
    dluma_wstr_append(body, 4096U, L":\r\n");
    dluma_wstr_append(body, 4096U, g_dluma_crash.dump_dir);
    dluma_wstr_append(body, 4096U, L"\r\n\r\n");
    dluma_wstr_append(
        body, 4096U,
        g_dluma_crash.editor_host
            ? g_dluma_crash.text.editor_note
            : g_dluma_crash.text.dump_note);
    MessageBoxW(
        0, body, dluma_crash_pick_title(
            exception_code, (const void*)(ULONG_PTR)address),
        MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
}

static DWORD dluma_crash_exception_code(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
#if defined(_MSC_VER)
    __try {
#endif
        if (exception_pointers && exception_pointers->ExceptionRecord &&
            exception_pointers->ExceptionRecord->ExceptionCode)
            exception_code =
                exception_pointers->ExceptionRecord->ExceptionCode;
#if defined(_MSC_VER)
    }
    __except (1) {
    }
#endif
    return exception_code;
}

static int dluma_crash_has_reportable_exception(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
    return dluma_crash_core_exception_is_reportable(
        dluma_crash_exception_code(exception_code, exception_pointers));
}

static void dluma_crash_capture_incident(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
    DlumaCrashIncident* incident = &g_dluma_crash.incident;
    memset(incident, 0, sizeof(*incident));
    incident->exception_code = exception_code;
    incident->fault_thread_id = GetCurrentThreadId();
    incident->captured_ms = GetTickCount64();
#if defined(_MSC_VER)
    __try {
#endif
        if (exception_pointers && exception_pointers->ExceptionRecord) {
            memcpy(
                &incident->exception_record,
                exception_pointers->ExceptionRecord,
                sizeof(incident->exception_record));
            incident->exception_record.ExceptionRecord = 0;
            incident->exception_code =
                incident->exception_record.ExceptionCode
                    ? incident->exception_record.ExceptionCode
                    : exception_code;
            incident->fault_address =
                (ULONGLONG)(ULONG_PTR)
                    incident->exception_record.ExceptionAddress;
            incident->context_copy_status |= 1U;
        }
        if (exception_pointers && exception_pointers->ContextRecord) {
            memcpy(
                &incident->context_record,
                exception_pointers->ContextRecord,
                sizeof(incident->context_record));
            incident->instruction_pointer = incident->context_record.rip;
            incident->stack_pointer = incident->context_record.rsp;
            incident->context_copy_status |= 2U;
        }
#if defined(_MSC_VER)
    }
    __except (1) {
        incident->context_copy_status |= 4U;
    }
#endif
    incident->exception_pointers.ExceptionRecord =
        (incident->context_copy_status & 1U)
            ? &incident->exception_record : 0;
    incident->exception_pointers.ContextRecord =
        (incident->context_copy_status & 2U)
            ? (PVOID)&incident->context_record : 0;
}

static int dluma_crash_capture_secondary_exception(
    EXCEPTION_POINTERS* exception_pointers,
    DWORD* exception_code, ULONGLONG* exception_address)
{
    if (exception_code) *exception_code = 0;
    if (exception_address) *exception_address = 0;
    if (exception_pointers && exception_pointers->ExceptionRecord) {
        if (exception_code)
            *exception_code =
                exception_pointers->ExceptionRecord->ExceptionCode;
        if (exception_address)
            *exception_address =
                (ULONGLONG)(ULONG_PTR)
                    exception_pointers->ExceptionRecord->ExceptionAddress;
    }
    return 1;
}

static DWORD dluma_crash_emergency_append_text(
    char* output, DWORD used, DWORD capacity, const char* text)
{
    while (text && *text && used < capacity)
        output[used++] = *text++;
    return used;
}

static DWORD dluma_crash_emergency_append_hex(
    char* output, DWORD used, DWORD capacity,
    ULONGLONG value, unsigned int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int index;
    if (digits > 16U) digits = 16U;
    used = dluma_crash_emergency_append_text(
        output, used, capacity, "0x");
    for (index = 0; index < digits && used < capacity; ++index)
        output[used++] =
            hex[(value >> ((digits - index - 1U) * 4U)) & 0xFU];
    return used;
}

static void dluma_crash_write_reporter_emergency(void)
{
    DWORD used = 0;
    used = dluma_crash_emergency_append_text(
        g_dluma_crash.line, used, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "[CRASH] [INFO] report_begin=DLUMA_CRASH_REPORT_V1\r\n"
        "[CRASH] [ERROR] report_mode=reserved_worker_emergency\r\n"
        "[CRASH] [ERROR] reporter_exception_code=");
    used = dluma_crash_emergency_append_hex(
        g_dluma_crash.line, used, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        g_dluma_crash.incident.reporter_exception_code, 8U);
    used = dluma_crash_emergency_append_text(
        g_dluma_crash.line, used, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "\r\n[CRASH] [ERROR] reporter_exception_address=");
    used = dluma_crash_emergency_append_hex(
        g_dluma_crash.line, used, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        g_dluma_crash.incident.reporter_exception_address, 16U);
    used = dluma_crash_emergency_append_text(
        g_dluma_crash.line, used, DLUMA_ARRAY_COUNT(g_dluma_crash.line),
        "\r\n[CRASH] [INFO] report_end=DLUMA_CRASH_REPORT_V1\r\n");
    dluma_log_emergency(g_dluma_crash.line);
    dluma_crash_write_dedicated(g_dluma_crash.line, used);
    if (g_dluma_crash.report_file_valid &&
        g_dluma_crash.report_handle &&
        g_dluma_crash.report_handle != INVALID_HANDLE_VALUE)
        (void)FlushFileBuffers(g_dluma_crash.report_handle);
    dluma_atomic_exchange(&g_dluma_crash.report_state, 2);
}

static DWORD WINAPI dluma_crash_reporter_thread(void* unused)
{
    (void)unused;
    for (;;) {
        if (WaitForSingleObject(
                g_dluma_crash.reporter_work_event, INFINITE) !=
            WAIT_OBJECT_0)
            return 1U;
        if (g_dluma_crash.reporter_stop_requested) return 0U;
        g_dluma_crash.incident.reporter_thread_id = GetCurrentThreadId();
#if defined(_MSC_VER)
        __try {
#endif
            dluma_crash_write_report(&g_dluma_crash.incident);
            if (WaitForSingleObject(
                    g_dluma_crash.reporter_stock_event, INFINITE) !=
                WAIT_OBJECT_0)
                g_dluma_crash.incident.stock_result_available = 0U;
            dluma_crash_finish_report(&g_dluma_crash.incident);
            if (g_dluma.config.crash_dialog_enabled &&
                !g_dluma_crash.dialog_suppressed_editor_shutdown &&
                dluma_atomic_compare_exchange(
                    &g_dluma_crash.popup_shown, 1, 0) == 0)
                dluma_crash_show_dialog(
                    g_dluma_crash.incident.exception_code,
                    &g_dluma_crash.incident.exception_pointers);
#if defined(_MSC_VER)
        }
        __except (dluma_crash_capture_secondary_exception(
            (EXCEPTION_POINTERS*)_exception_info(),
            &g_dluma_crash.incident.reporter_exception_code,
            &g_dluma_crash.incident.reporter_exception_address)) {
            dluma_crash_write_reporter_emergency();
        }
#endif
        (void)SetEvent(g_dluma_crash.reporter_complete_event);
    }
}

static void dluma_crash_close_reporter_handles(void)
{
    if (g_dluma_crash.reporter_thread)
        CloseHandle(g_dluma_crash.reporter_thread);
    if (g_dluma_crash.reporter_complete_event)
        CloseHandle(g_dluma_crash.reporter_complete_event);
    if (g_dluma_crash.reporter_stock_event)
        CloseHandle(g_dluma_crash.reporter_stock_event);
    if (g_dluma_crash.reporter_work_event)
        CloseHandle(g_dluma_crash.reporter_work_event);
    g_dluma_crash.reporter_thread = 0;
    g_dluma_crash.reporter_complete_event = 0;
    g_dluma_crash.reporter_stock_event = 0;
    g_dluma_crash.reporter_work_event = 0;
}

static int dluma_crash_reporter_start(void)
{
    g_dluma_crash.reporter_stop_requested = 0;
    g_dluma_crash.reporter_work_event =
        CreateEventW(0, FALSE, FALSE, 0);
    g_dluma_crash.reporter_stock_event =
        CreateEventW(0, TRUE, FALSE, 0);
    g_dluma_crash.reporter_complete_event =
        CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_crash.reporter_work_event ||
        !g_dluma_crash.reporter_stock_event ||
        !g_dluma_crash.reporter_complete_event) {
        dluma_crash_close_reporter_handles();
        return 0;
    }
    g_dluma_crash.reporter_thread = CreateThread(
        0, DLUMA_CRASH_REPORTER_STACK_BYTES,
        dluma_crash_reporter_thread, 0, 0, 0);
    if (!g_dluma_crash.reporter_thread) {
        dluma_crash_close_reporter_handles();
        return 0;
    }
    return 1;
}

static int dluma_crash_reporter_stop(void)
{
    DWORD result;
    if (!g_dluma_crash.reporter_thread) {
        dluma_crash_close_reporter_handles();
        return 1;
    }
    dluma_atomic_exchange(&g_dluma_crash.reporter_stop_requested, 1);
    (void)SetEvent(g_dluma_crash.reporter_work_event);
    result = WaitForSingleObject(
        g_dluma_crash.reporter_thread,
        (DWORD)DLUMA_CRASH_STOP_TIMEOUT_MS);
    if (result != WAIT_OBJECT_0) return 0;
    dluma_crash_close_reporter_handles();
    return 1;
}

static void dluma_crash_signal_incident(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
    dluma_crash_capture_incident(exception_code, exception_pointers);
    (void)SetEvent(g_dluma_crash.reporter_work_event);
}

static void dluma_crash_publish_stock_result(
    int result, int available,
    DWORD secondary_code, ULONGLONG secondary_address)
{
    g_dluma_crash.incident.stock_result = result;
    g_dluma_crash.incident.stock_result_available = available ? 1U : 0U;
    g_dluma_crash.incident.stock_callback_exception_code = secondary_code;
    g_dluma_crash.incident.stock_callback_exception_address =
        secondary_address;
    (void)SetEvent(g_dluma_crash.reporter_stock_event);
}

static int __cdecl dluma_dump_function_wrapper(
    DWORD exception_code, EXCEPTION_POINTERS* exception_pointers)
{
    DlumaDumpFunction original;
    DWORD thread_id = GetCurrentThreadId();
    DWORD secondary_code = 0;
    ULONGLONG secondary_address = 0;
    int reportable;
    int claimed = 0;
    int stock_available = 0;
    int result = 0;
    dluma_atomic_increment(&g_dluma_crash.active_callbacks);
    (void)dluma_atomic_compare_exchange(
        &g_dluma_crash.callback_thread_id, (LONG)thread_id, 0);
    original = g_dluma_crash.original_dump_function;
    reportable = dluma_crash_has_reportable_exception(
        exception_code, exception_pointers);
    if (reportable &&
        dluma_atomic_compare_exchange(
            &g_dluma_crash.report_state, 1, 0) == 0) {
        claimed = 1;
        dluma_crash_signal_incident(exception_code, exception_pointers);
    }
    if (original) {
#if defined(_MSC_VER)
        __try {
#endif
            result = original(exception_code, exception_pointers);
            stock_available = 1;
#if defined(_MSC_VER)
        }
        __except (dluma_crash_capture_secondary_exception(
            (EXCEPTION_POINTERS*)_exception_info(),
            &secondary_code, &secondary_address)) {
            result = DLUMA_DUMP_RESULT_FATAL;
        }
#endif
    }
    if (!claimed &&
        (result == DLUMA_DUMP_RESULT_FATAL || secondary_code) &&
        dluma_atomic_compare_exchange(
            &g_dluma_crash.report_state, 1, 0) == 0) {
        claimed = 1;
        dluma_crash_signal_incident(exception_code, exception_pointers);
    }
    if (claimed) {
        dluma_crash_publish_stock_result(
            result, stock_available,
            secondary_code, secondary_address);
        (void)WaitForSingleObject(
            g_dluma_crash.reporter_complete_event, INFINITE);
    }
    if (dluma_atomic_decrement(&g_dluma_crash.active_callbacks) == 0)
        dluma_atomic_exchange(&g_dluma_crash.callback_thread_id, 0);
    return result;
}

static LONG WINAPI dluma_editor_unhandled_exception_filter(
    EXCEPTION_POINTERS* exception_pointers)
{
    DlumaTopLevelExceptionFilter original;
    DWORD exception_code =
        dluma_crash_exception_code(0, exception_pointers);
    DWORD secondary_code = 0;
    ULONGLONG secondary_address = 0;
    DWORD thread_id = GetCurrentThreadId();
    int reportable;
    int claimed = 0;
    int stock_available = 0;
    LONG result = DLUMA_EXCEPTION_CONTINUE_SEARCH;
    dluma_atomic_increment(&g_dluma_crash.active_callbacks);
    (void)dluma_atomic_compare_exchange(
        &g_dluma_crash.callback_thread_id, (LONG)thread_id, 0);
    reportable = dluma_crash_has_reportable_exception(
        exception_code, exception_pointers);
    if (reportable &&
        dluma_atomic_compare_exchange(
            &g_dluma_crash.report_state, 1, 0) == 0) {
        claimed = 1;
        dluma_crash_signal_incident(exception_code, exception_pointers);
    }
    original = g_dluma_crash.original_exception_filter;
    if (original &&
        original != dluma_editor_unhandled_exception_filter) {
#if defined(_MSC_VER)
        __try {
#endif
            result = original(exception_pointers);
            stock_available = 1;
#if defined(_MSC_VER)
        }
        __except (dluma_crash_capture_secondary_exception(
            (EXCEPTION_POINTERS*)_exception_info(),
            &secondary_code, &secondary_address)) {
            result = DLUMA_EXCEPTION_CONTINUE_SEARCH;
        }
#endif
    }
    if (!claimed && secondary_code &&
        dluma_atomic_compare_exchange(
            &g_dluma_crash.report_state, 1, 0) == 0) {
        claimed = 1;
        dluma_crash_signal_incident(exception_code, exception_pointers);
    }
    if (claimed) {
        dluma_crash_publish_stock_result(
            (int)result, stock_available,
            secondary_code, secondary_address);
        (void)WaitForSingleObject(
            g_dluma_crash.reporter_complete_event, INFINITE);
    }
    if (dluma_atomic_decrement(&g_dluma_crash.active_callbacks) == 0)
        dluma_atomic_exchange(&g_dluma_crash.callback_thread_id, 0);
    return result;
}

static void dluma_crash_retain(const char* reason)
{
    HMODULE pinned = 0;
    dluma_atomic_exchange(&g_dluma_crash.retained_for_process_exit, 1);
    (void)GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_PIN |
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        (LPCWSTR)(const void*)&dluma_dump_function_wrapper, &pinned);
    dluma_log_emergency(reason);
}

int dluma_crash_reporting_start(void)
{
    DlumaDumpFunction current;
    LONG state;
    LONG unavailable_reason = DLUMA_CRASH_UNAVAILABLE_NONE;
    const char* unavailable_message = 0;
    for (;;) {
        state = dluma_atomic_compare_exchange(
            &g_dluma_crash.install_state, 0, 0);
        if (state == 2) return 1;
        if (state != 0 && state != 3) return 0;
        if (dluma_atomic_compare_exchange(
                &g_dluma_crash.install_state, 1, state) == state)
            break;
    }
    g_dluma_crash.filesystem_module =
        GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!g_dluma_crash.filesystem_module) {
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_FILESYSTEM_MODULE;
        unavailable_message =
            "Chrome filesystem module is not loaded yet; loader crash diagnostics will retry.";
        goto unavailable;
    }
    g_dluma_crash.get_dump_function =
        (DlumaGetDumpFunction)GetProcAddress(
            g_dluma_crash.filesystem_module,
            "?GetDumpFunction@@YAP6A?AW4TYPE@EDumpResult@@KPEAU_EXCEPTION_POINTERS@@@ZXZ");
    if (!g_dluma_crash.get_dump_function) {
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_GET_EXPORT;
        unavailable_message =
            "Chrome GetDumpFunction export is unavailable; loader crash diagnostics will retry.";
        goto unavailable;
    }
    g_dluma_crash.set_dump_function =
        (DlumaSetDumpFunction)GetProcAddress(
            g_dluma_crash.filesystem_module,
            "?SetDumpFunction@@YAXP6A?AW4TYPE@EDumpResult@@KPEAU_EXCEPTION_POINTERS@@@Z@Z");
    if (!g_dluma_crash.set_dump_function) {
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_SET_EXPORT;
        unavailable_message =
            "Chrome SetDumpFunction export is unavailable; loader crash diagnostics will retry.";
        goto unavailable;
    }
    current = g_dluma_crash.get_dump_function();
    if (!current && !g_dluma_crash.editor_host) {
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_CALLBACK_PENDING;
        unavailable_message =
            "Chrome dump callback is unavailable; loader crash diagnostics were not installed.";
        goto unavailable;
    }
    if (current == dluma_dump_function_wrapper) {
        if (g_dluma_crash.original_dump_function) {
            dluma_atomic_exchange(&g_dluma_crash.install_state, 2);
            return 1;
        }
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_INSTALL_REJECTED;
        unavailable_message =
            "Chrome dump callback already points at the loader without retained stock ownership; retry was rejected.";
        goto unavailable;
    }
    g_dluma_crash.report_handle = INVALID_HANDLE_VALUE;
    g_dluma_crash.report_bytes = 0;
    g_dluma_crash.report_write_failures = 0;
    g_dluma_crash.report_truncated = 0;
    g_dluma_crash.report_truncation_written = 0;
    g_dluma_crash.report_file_valid = 0;
    g_dluma_crash.report_state = 0;
    g_dluma_crash.popup_shown = 0;
    g_dluma_crash.dialog_suppressed_editor_shutdown = 0;
    g_dluma_crash.active_callbacks = 0;
    g_dluma_crash.callback_thread_id = 0;
    g_dluma_crash.retained_for_process_exit = 0;
    g_dluma_crash.reporter_stop_requested = 0;
    memset(&g_dluma_crash.incident, 0, sizeof(g_dluma_crash.incident));
    dluma_crash_localization_refresh();
    if (dluma_crash_prepare_paths())
        (void)dluma_crash_open_report_file();
    else {
        g_dluma_crash.crash_dir[0] = 0;
        g_dluma_crash.crash_dir_final[0] = 0;
        g_dluma_crash.report_path[0] = 0;
    }
    dluma_crash_capture_system();
    dluma_crash_capture_modules();
    dluma_crash_publish_runtime_context(
        "install", g_dluma.active_project_root, g_dluma.active_map,
        g_dluma.active_project_owner_epoch,
        g_dluma.active_project_owner_transition_generation);
    if (!dluma_crash_reporter_start()) {
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_REPORTER_WORKER;
        unavailable_message =
            "Reserved crash reporter worker is unavailable; Chrome callback ownership was left unchanged and loader crash diagnostics will retry.";
        goto unavailable;
    }
    g_dluma_crash.original_dump_function = current;
    g_dluma_crash.set_dump_function(dluma_dump_function_wrapper);
    if (g_dluma_crash.get_dump_function() !=
        dluma_dump_function_wrapper) {
        g_dluma_crash.original_dump_function = 0;
        unavailable_reason = DLUMA_CRASH_UNAVAILABLE_INSTALL_REJECTED;
        unavailable_message =
            "Chrome rejected the loader dump callback wrapper; loader crash diagnostics will retry.";
        goto unavailable;
    }
    dluma_atomic_exchange(
        &g_dluma_crash.unavailable_reason,
        DLUMA_CRASH_UNAVAILABLE_NONE);
    dluma_atomic_exchange(&g_dluma_crash.install_state, 2);
    if (!current && g_dluma_crash.editor_host)
        dluma_log_line(
            "INFO",
            "Chrome Editor's empty dump callback slot is now owned by loader crash diagnostics; Windows Error Reporting remains unchanged.");
    else
        dluma_log_line(
            "INFO",
            "Chrome dump callback wrapped; crash diagnostics are active independently of the popup setting and the stock result remains unchanged.");
    if (g_dluma_crash.report_path[0]) {
        char path[DLUMA_MAX_WPATH * 3];
        path[0] = 0;
        if (dluma_wide_to_utf8(
                g_dluma_crash.report_path, path,
                DLUMA_ARRAY_COUNT(path)))
            dluma_log_pair(
                "INFO", "crash_info_log_path", path);
    } else {
        dluma_log_line(
            "WARN",
            "Dedicated crash info log could not be pre-opened; emergency diagnostics will still use dluma.log and OutputDebugString.");
    }
    if (!g_dluma.config.crash_dialog_enabled)
        dluma_log_line(
            "INFO",
            "Crash dialog popup is disabled; crash logging remains active.");
    return 1;

unavailable:
    if (!dluma_crash_reporter_stop()) {
        dluma_crash_retain(
            "[CRASH] [ERROR] reserved reporter worker did not stop during failed installation; crash state retained until process exit\r\n");
        dluma_atomic_exchange(&g_dluma_crash.install_state, 3);
        return 0;
    }
    dluma_crash_close_empty_report();
    dluma_atomic_exchange(&g_dluma_crash.install_state, 3);
    if (dluma_atomic_exchange(
            &g_dluma_crash.unavailable_reason,
            unavailable_reason) != unavailable_reason)
        dluma_log_line(
            "WARN",
            unavailable_message
                ? unavailable_message
                : "Chrome dump callback is unavailable; loader crash diagnostics will retry.");
    return 0;
}

int dluma_crash_reporting_start_editor_filter(void)
{
    DlumaTopLevelExceptionFilter previous;
    LONG state;
    if (!g_dluma_crash.editor_host ||
        g_dluma_crash.install_state != 2)
        return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_crash.editor_filter_state, 1, 0);
    if (state == 1) return 0;
    if (state == 2) {
        previous = SetUnhandledExceptionFilter(
            dluma_editor_unhandled_exception_filter);
        if (previous != dluma_editor_unhandled_exception_filter) {
            g_dluma_crash.original_exception_filter = previous;
            dluma_log_line(
                "INFO",
                "Editor unhandled-exception fallback was refreshed after an Editor lifecycle transition.");
        }
        return 1;
    }
    previous = SetUnhandledExceptionFilter(
        dluma_editor_unhandled_exception_filter);
    g_dluma_crash.original_exception_filter =
        previous != dluma_editor_unhandled_exception_filter
            ? previous : 0;
    dluma_atomic_exchange(&g_dluma_crash.editor_filter_state, 2);
    dluma_log_line(
        "INFO",
        "Editor unhandled-exception fallback installed; the prior Editor filter is called exactly once and its result is returned unchanged.");
    return 1;
}

int dluma_crash_reporting_stop(void)
{
    ULONGLONG started;
    if (g_dluma_crash.install_state == 0 ||
        g_dluma_crash.install_state == 3) {
        if (!dluma_crash_reporter_stop()) {
            dluma_crash_retain(
                "[CRASH] [ERROR] reserved reporter worker did not stop from an inactive state; crash state retained until process exit\r\n");
            return 0;
        }
        dluma_crash_close_empty_report();
        dluma_atomic_exchange(&g_dluma_crash.install_state, 0);
        return 1;
    }
    if (g_dluma_crash.install_state != 2) return 0;
    if (g_dluma_crash.callback_thread_id ==
            (LONG)GetCurrentThreadId() &&
        g_dluma_crash.active_callbacks > 0) {
        dluma_crash_retain(
            "[CRASH] [ERROR] shutdown entered from the crash callback; crash state retained until process exit\r\n");
        return 0;
    }
    dluma_atomic_exchange(&g_dluma_crash.install_state, 4);
    if (!g_dluma_crash.get_dump_function ||
        !g_dluma_crash.set_dump_function ||
        g_dluma_crash.get_dump_function() !=
            dluma_dump_function_wrapper) {
        dluma_crash_retain(
            "[CRASH] [ERROR] dump callback ownership was lost; crash state retained until process exit\r\n");
        return 0;
    }
    g_dluma_crash.set_dump_function(
        g_dluma_crash.original_dump_function);
    if (g_dluma_crash.get_dump_function() ==
        dluma_dump_function_wrapper) {
        dluma_crash_retain(
            "[CRASH] [ERROR] stock dump callback restoration did not take effect; crash state retained until process exit\r\n");
        return 0;
    }
    started = GetTickCount64();
    while (g_dluma_crash.active_callbacks > 0 &&
           GetTickCount64() - started <
               DLUMA_CRASH_STOP_TIMEOUT_MS)
        Sleep(1);
    if (g_dluma_crash.active_callbacks > 0) {
        dluma_crash_retain(
            "[CRASH] [ERROR] crash callback drain timed out; crash state retained until process exit\r\n");
        return 0;
    }
    if (!dluma_crash_reporter_stop()) {
        dluma_crash_retain(
            "[CRASH] [ERROR] reserved reporter worker did not stop; crash state retained until process exit\r\n");
        return 0;
    }
    dluma_crash_close_empty_report();
    g_dluma_crash.original_dump_function = 0;
    g_dluma_crash.get_dump_function = 0;
    g_dluma_crash.set_dump_function = 0;
    g_dluma_crash.filesystem_module = 0;
    dluma_atomic_exchange(&g_dluma_crash.install_state, 0);
    dluma_log_line(
        "INFO", "Chrome dump callback restored after crash diagnostics drained.");
    return 1;
}
