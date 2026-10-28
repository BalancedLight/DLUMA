#include "dlw_native_mods.h"
#include "dlw_atomic_ini.h"
#include "core/dlw_api_runtime.h"
#include "core/dlw_mod_package_runtime.h"
#include "game/dlw_game_backend.h"
#include "game/dlw_quest_phase_service.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_host.h"
#include "dlw_lua_config.h"
#include "dlw_native_companion_source_owner.h"
#include "dlw_native_gameplay.h"
#include "dlw_native_mod_config.h"
#include "dlw_path_security.h"
#include "dlw_sha256_internal.h"

#define DLUMA_NATIVE_MOD_MAX_EVENTS 256U

/* This file intentionally never loads a candidate while discovering it.
 * LoadLibrary executes DllMain, so PE/export inspection works only on bounded
 * raw bytes read through a regular file handle. */

typedef const DlumaNativeModApiV2*
    (__cdecl *DlumaNativeGetApiV2Fn)(void);

typedef struct DlumaEditorValidatorRegistration {
    unsigned int active;
    char handler_id[128];
    DlumaEditorQuestValidatorV1 callback;
    void* user;
} DlumaEditorValidatorRegistration;

typedef enum DlumaNativeLifecyclePhase {
    DLUMA_NATIVE_PHASE_IDLE = 0,
    DLUMA_NATIVE_PHASE_LOADING,
    DLUMA_NATIVE_PHASE_READY,
    DLUMA_NATIVE_PHASE_POSTING,
    DLUMA_NATIVE_PHASE_SHUTTING_DOWN,
    DLUMA_NATIVE_PHASE_FAILED,
    DLUMA_NATIVE_PHASE_RETAINED
} DlumaNativeLifecyclePhase;

typedef struct DlumaNativeModEntry {
    DlumaNativeModSnapshot snapshot;
    HMODULE module;
    DlumaNativeGetApiV2Fn get_api_v2;
    DlumaNativeModApiV2 callbacks;
    DlumaEditorModGetApiV1Fn get_editor_api_v1;
    DlumaEditorHostApiV1 editor_host_api;
    DlumaEditorModApiV1 editor_callbacks;
    DlumaEditorValidatorRegistration editor_validators[
        DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS];
    volatile LONG editor_callbacks_in_flight;
    unsigned int editor_loaded;
    DlumaModOwner api_owner;
    DlumaCapabilityRegistry api_capabilities;
    DlumaApiBackendV1 api_backend;
    DlumaApiRuntime api_runtime;
    DlumaGameHostContext api_host;
    volatile LONG quest_phase_callbacks_in_flight;
    DlumaModPackageSnapshot package;
    WCHAR project_root[DLUMA_MAX_WPATH];
    char active_project[128];
    char active_map[128];
    unsigned int lifecycle_phase;
    unsigned int scope_active;
    unsigned int cleanup_pending;
    unsigned int scope_exit_revoked;
    unsigned int global_map_exit_suspended;
    DWORD lifecycle_epoch;
    unsigned int registry_index;
    DlumaCompanionArchive* companion_pack;
} DlumaNativeModEntry;

typedef struct DlumaNativeQueuedEvent {
    uint64_t owner_id;
    uint64_t callback_id;
    uint64_t token;
    uint32_t kind;
    uint32_t map_generation;
    int32_t state;
    double number;
    char name[96];
} DlumaNativeQueuedEvent;

typedef struct DlumaNativeRegistry {
    DlumaNativeModEntry entries[DLUMA_MAX_NATIVE_MODS];
    DlumaNativeQueuedEvent events[DLUMA_NATIVE_MOD_MAX_EVENTS];
    unsigned int count;
    unsigned int total_found;
    DWORD next_lifecycle_epoch;
    ULONGLONG revision;
    volatile LONG lock;
    volatile LONG event_lock;
    volatile LONG pump_active;
    unsigned int event_head;
    unsigned int event_count;
    WCHAR ini_path[DLUMA_MAX_WPATH];
} DlumaNativeRegistry;

typedef struct DlumaNativeFileIdentity {
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
} DlumaNativeFileIdentity;

typedef struct DlumaNativeLockedArtifact {
    HANDLE module_file;
    HANDLE sidecar_file;
    BY_HANDLE_FILE_INFORMATION module_info;
    BY_HANDLE_FILE_INFORMATION sidecar_info;
    WCHAR module_final_path[DLUMA_MAX_WPATH];
    WCHAR sidecar_final_path[DLUMA_MAX_WPATH];
    DlumaCompanionArchive* companion_pack;
} DlumaNativeLockedArtifact;

typedef struct DlumaNativePeExports {
    unsigned char valid_pe;
    unsigned char machine_x64;
    unsigned char is_dll;
    unsigned char has_get_api_v2;
    unsigned char has_editor_api_v1;
    unsigned char has_legacy_v1_query;
    unsigned char has_legacy_v1_pre;
    unsigned char has_legacy_v1_post;
    unsigned char has_legacy_v1_shutdown;
    unsigned char has_legacy_initialize;
    unsigned char has_legacy_shutdown;
} DlumaNativePeExports;

static DlumaNativeRegistry g_dluma_native_mods;
static HMODULE g_dluma_native_original_gamedll;
static volatile LONG g_dluma_native_crash_context_sequence;
static volatile LONG g_dluma_native_project_exit_gate;
static volatile LONG g_dluma_native_map_exit_frozen;
static unsigned int g_dluma_native_map_exit_generation;
static char g_dluma_native_crash_context[32U * 1024U];

static void dluma_native_api_event_sink(
    void* user,
    uint32_t kind,
    uint64_t callback_id,
    uint64_t token,
    double number,
    const char* name);
static int dluma_native_quest_phase_event_sink(
    void* user,
    uint32_t event_kind,
    uint64_t callback_id,
    uint64_t registration_token,
    const DlumaQuestPhaseEvent* event_value);

#define DLUMA_IMAGE_FILE_MACHINE_AMD64 0x8664U
#define DLUMA_IMAGE_FILE_DLL 0x2000U
#define DLUMA_IMAGE_DOS_SIGNATURE 0x5A4DU
#define DLUMA_IMAGE_NT_SIGNATURE 0x00004550UL
#define DLUMA_IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20BU
/* Discovery runs on the game's startup thread. Keep this bounded list off
 * that thread's typically 1 MiB stack: 16 roots * 32 projects * 1024 WCHARs
 * already consumes exactly 1 MiB before the function's other locals. */
#define DLUMA_NATIVE_MAX_DISCOVERY_PROJECTS (DLUMA_MAX_ROOTS * 32U)
#define DLUMA_NATIVE_MAX_DISCOVERY_DIRECTORIES 4096U
#define DLUMA_NATIVE_MAX_DISCOVERY_DEPTH 16U

static void dluma_native_lock(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_native_mods.lock, 1, 0) != 0)
        Sleep(1);
}

static void dluma_native_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_native_mods.lock, 0);
}

static DWORD dluma_native_next_lifecycle_epoch_locked(void)
{
    ++g_dluma_native_mods.next_lifecycle_epoch;
    if (!g_dluma_native_mods.next_lifecycle_epoch)
        ++g_dluma_native_mods.next_lifecycle_epoch;
    return g_dluma_native_mods.next_lifecycle_epoch;
}

static void dluma_native_publish_crash_context_locked(void)
{
    unsigned int i;
    dluma_atomic_increment(&g_dluma_native_crash_context_sequence);
    g_dluma_native_crash_context[0] = 0;
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        const DlumaNativeModSnapshot* item =
            &g_dluma_native_mods.entries[i].snapshot;
        if (!item->loaded) continue;
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context),
            "[CRASH] [INFO] native_loaded id=");
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), item->id);
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), " source=");
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context),
            item->source_identity);
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), " path=");
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context),
            item->canonical_relative_path);
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), " sha256=");
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context),
            item->module_sha256);
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), " abi=");
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context),
            dluma_native_mods_abi_text(item->abi_kind));
        dluma_str_append(
            g_dluma_native_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_native_crash_context), "\r\n");
    }
    dluma_atomic_increment(&g_dluma_native_crash_context_sequence);
}

static int dluma_native_is_dot_dir(const WCHAR* name)
{
    return !name || (name[0] == L'.' &&
           (!name[1] || (name[1] == L'.' && !name[2])));
}

static int dluma_native_same_identity(const DlumaNativeFileIdentity* a,
                                    const DlumaNativeFileIdentity* b)
{
    return a && b && a->volume_serial == b->volume_serial &&
           a->file_index_high == b->file_index_high &&
           a->file_index_low == b->file_index_low;
}

static int dluma_native_get_file_identity(const WCHAR* path,
                                        DlumaNativeFileIdentity* out)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    if (!path || !path[0] || !out) return 0;
    memset(out, 0, sizeof(*out));
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ,
                       0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(file);
        return 0;
    }
    out->volume_serial = info.dwVolumeSerialNumber;
    out->file_index_high = info.nFileIndexHigh;
    out->file_index_low = info.nFileIndexLow;
    CloseHandle(file);
    return 1;
}

static int dluma_native_open_regular_final(const WCHAR* candidate, HANDLE* file_out,
                                         WCHAR* final_path,
                                         unsigned int final_capacity,
                                         BY_HANDLE_FILE_INFORMATION* info_out)
{
    DWORD attributes;
    HANDLE file;
    if (file_out) *file_out = INVALID_HANDLE_VALUE;
    if (!candidate || !candidate[0] || !file_out || !final_path ||
        !final_capacity || !info_out) return 0;
    attributes = GetFileAttributesW(candidate);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return 0;
    file = CreateFileW(candidate, GENERIC_READ,
                       FILE_SHARE_READ,
                       0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(info_out, 0, sizeof(*info_out));
    if (!dluma_get_final_path_checked(file, final_path, final_capacity) ||
        !GetFileInformationByHandle(file, info_out) ||
        (info_out->dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        CloseHandle(file);
        return 0;
    }
    *file_out = file;
    return 1;
}

static int dluma_native_path_under(const WCHAR* path, const WCHAR* root)
{
    SIZE_T n;
    if (!path || !root || !root[0] || !dluma_wstarts_with_icase(path, root))
        return 0;
    n = wcslen(root);
    return path[n] == L'\\' || path[n] == L'/' || path[n] == 0;
}

static int dluma_native_path_join_checked(
    WCHAR* out, unsigned int capacity,
    const WCHAR* left, const WCHAR* right)
{
    SIZE_T left_length;
    SIZE_T right_length;
    SIZE_T separator_length;
    const WCHAR* clean_right = right;
    if (!out || !capacity || !left || !right) return 0;
    left_length = wcslen(left);
    while (*clean_right == L'\\' || *clean_right == L'/') ++clean_right;
    right_length = wcslen(clean_right);
    separator_length =
        left_length && left[left_length - 1U] != L'\\' &&
        left[left_length - 1U] != L'/' ? 1U : 0U;
    if (left_length >= capacity ||
        separator_length > (SIZE_T)capacity - left_length - 1U ||
        right_length >
            (SIZE_T)capacity - left_length - separator_length - 1U) {
        out[0] = 0;
        return 0;
    }
    dluma_path_join(out, capacity, left, clean_right);
    return 1;
}

static DWORD dluma_native_u16(const BYTE* p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8);
}

static DWORD dluma_native_u32(const BYTE* p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

static int dluma_native_range(SIZE_T size, SIZE_T offset, SIZE_T wanted)
{
    return offset <= size && wanted <= size - offset;
}

static int dluma_native_rva_to_offset(const BYTE* bytes, SIZE_T size,
                                    SIZE_T section_offset,
                                    unsigned int section_count, DWORD rva,
                                    SIZE_T* offset_out)
{
    unsigned int i;
    for (i = 0; i < section_count; ++i) {
        SIZE_T entry = section_offset + (SIZE_T)i * 40U;
        DWORD virtual_size;
        DWORD virtual_address;
        DWORD raw_size;
        DWORD raw_offset;
        DWORD span;
        if (!dluma_native_range(size, entry, 40U)) return 0;
        virtual_size = dluma_native_u32(bytes + entry + 8U);
        virtual_address = dluma_native_u32(bytes + entry + 12U);
        raw_size = dluma_native_u32(bytes + entry + 16U);
        raw_offset = dluma_native_u32(bytes + entry + 20U);
        span = virtual_size > raw_size ? virtual_size : raw_size;
        if (rva >= virtual_address && rva - virtual_address < span) {
            SIZE_T result = (SIZE_T)raw_offset + (SIZE_T)(rva - virtual_address);
            if (!dluma_native_range(size, result, 1U)) return 0;
            *offset_out = result;
            return 1;
        }
    }
    return 0;
}

static int dluma_native_pe_has_name(const BYTE* bytes, SIZE_T size,
                                  SIZE_T section_offset,
                                  unsigned int section_count,
                                  DWORD export_rva, const char* wanted)
{
    SIZE_T directory;
    SIZE_T names;
    unsigned int i;
    DWORD name_count;
    DWORD names_rva;
    if (!wanted || !dluma_native_rva_to_offset(bytes, size, section_offset,
                                             section_count, export_rva,
                                             &directory) ||
        !dluma_native_range(size, directory, 40U)) return 0;
    name_count = dluma_native_u32(bytes + directory + 24U);
    names_rva = dluma_native_u32(bytes + directory + 32U);
    if (name_count > 4096U ||
        !dluma_native_rva_to_offset(bytes, size, section_offset, section_count,
                                  names_rva, &names)) return 0;
    for (i = 0; i < name_count; ++i) {
        DWORD name_rva;
        SIZE_T name_offset;
        unsigned int j;
        if (!dluma_native_range(size, names + (SIZE_T)i * 4U, 4U)) return 0;
        name_rva = dluma_native_u32(bytes + names + (SIZE_T)i * 4U);
        if (!dluma_native_rva_to_offset(bytes, size, section_offset,
                                      section_count, name_rva, &name_offset))
            continue;
        for (j = 0; wanted[j] ||
                    (dluma_native_range(size, name_offset + j, 1U) &&
                     bytes[name_offset + j]); ++j) {
            if (!dluma_native_range(size, name_offset + j, 1U) ||
                bytes[name_offset + j] != (BYTE)wanted[j]) break;
        }
        if (!wanted[j] && dluma_native_range(size, name_offset + j, 1U) &&
            bytes[name_offset + j] == 0) return 1;
    }
    return 0;
}

static int dluma_native_inspect_pe(const WCHAR* path, DlumaNativeModSnapshot* item)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER file_size;
    BYTE* bytes = 0;
    DWORD read = 0;
    SIZE_T nt;
    SIZE_T optional;
    SIZE_T sections;
    DWORD exports_rva;
    WORD machine;
    WORD characteristics;
    WORD optional_size;
    WORD section_count;
    DlumaNativePeExports found;
    int ok = 0;
    if (!path || !item) return 0;
    memset(&found, 0, sizeof(found));
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 ||
        (ULONGLONG)file_size.QuadPart > DLUMA_NATIVE_MOD_FILE_MAX_BYTES)
        goto done;
    bytes = (BYTE*)dluma_alloc((SIZE_T)file_size.QuadPart);
    if (!bytes || !ReadFile(file, bytes, (DWORD)file_size.QuadPart, &read, 0) ||
        read != (DWORD)file_size.QuadPart) goto done;
    if (!dluma_native_range((SIZE_T)file_size.QuadPart, 0, 0x40U) ||
        dluma_native_u16(bytes) != DLUMA_IMAGE_DOS_SIGNATURE) goto done;
    nt = dluma_native_u32(bytes + 0x3CU);
    if (!dluma_native_range((SIZE_T)file_size.QuadPart, nt, 24U) ||
        dluma_native_u32(bytes + nt) != DLUMA_IMAGE_NT_SIGNATURE) goto done;
    machine = (WORD)dluma_native_u16(bytes + nt + 4U);
    section_count = (WORD)dluma_native_u16(bytes + nt + 6U);
    optional_size = (WORD)dluma_native_u16(bytes + nt + 20U);
    characteristics = (WORD)dluma_native_u16(bytes + nt + 22U);
    optional = nt + 24U;
    if (!section_count || section_count > 96U || optional_size < 120U ||
        !dluma_native_range((SIZE_T)file_size.QuadPart, optional, optional_size) ||
        dluma_native_u16(bytes + optional) != DLUMA_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        goto done;
    sections = optional + optional_size;
    if (!dluma_native_range((SIZE_T)file_size.QuadPart, sections,
                          (SIZE_T)section_count * 40U)) goto done;
    found.valid_pe = 1;
    found.machine_x64 = (unsigned char)(machine == DLUMA_IMAGE_FILE_MACHINE_AMD64);
    found.is_dll = (unsigned char)((characteristics & DLUMA_IMAGE_FILE_DLL) != 0);
    exports_rva = dluma_native_u32(bytes + optional + 112U);
    if (exports_rva) {
        found.has_get_api_v2 = (unsigned char)dluma_native_pe_has_name(
            bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
            exports_rva, DLUMA_NATIVE_MOD_GET_API_V2_EXPORT);
        found.has_editor_api_v1 = (unsigned char)dluma_native_pe_has_name(
            bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
            exports_rva, DLUMA_EDITOR_MOD_GET_API_V1_EXPORT);
        found.has_legacy_v1_query =
            (unsigned char)dluma_native_pe_has_name(
                bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
                exports_rva, "DLUMANativeMod_QueryV1");
        found.has_legacy_v1_pre =
            (unsigned char)dluma_native_pe_has_name(
                bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
                exports_rva, "DLUMANativeMod_PreGameInitV1");
        found.has_legacy_v1_post =
            (unsigned char)dluma_native_pe_has_name(
                bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
                exports_rva, "DLUMANativeMod_PostGameInitV1");
        found.has_legacy_v1_shutdown =
            (unsigned char)dluma_native_pe_has_name(
                bytes, (SIZE_T)file_size.QuadPart, sections, section_count,
                exports_rva, "DLUMANativeMod_ShutdownV1");
        found.has_legacy_initialize = (unsigned char)dluma_native_pe_has_name(bytes,
            (SIZE_T)file_size.QuadPart, sections, section_count, exports_rva,
            "InitializeGameScriptDLL");
        found.has_legacy_shutdown = (unsigned char)dluma_native_pe_has_name(bytes,
            (SIZE_T)file_size.QuadPart, sections, section_count, exports_rva,
            "ShutdownGameScriptDLL");
    }
    item->pe_machine_valid = found.machine_x64;
    item->pe_dll_valid = found.valid_pe && found.is_dll;
    item->editor_abi_available = found.has_editor_api_v1;
    if (found.valid_pe && found.machine_x64 && found.is_dll &&
        found.has_get_api_v2)
        item->abi_kind = DLUMA_NATIVE_ABI_V2;
    else if (found.valid_pe && found.machine_x64 && found.is_dll &&
             found.has_editor_api_v1)
        item->abi_kind = DLUMA_NATIVE_ABI_EDITOR_V1;
    else if (found.valid_pe && found.machine_x64 && found.is_dll &&
             found.has_legacy_v1_query && found.has_legacy_v1_pre &&
             found.has_legacy_v1_post && found.has_legacy_v1_shutdown)
        item->abi_kind = DLUMA_NATIVE_ABI_LEGACY_V1;
    else if (found.valid_pe && found.machine_x64 && found.is_dll &&
             found.has_legacy_initialize && found.has_legacy_shutdown)
        item->abi_kind = DLUMA_NATIVE_ABI_LEGACY_GAME_SCRIPT;
    else
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
    ok = found.valid_pe;
done:
    if (bytes) dluma_free(bytes);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}

static int dluma_native_sidecar_path(const WCHAR* module, WCHAR* out,
                                   unsigned int capacity)
{
    SIZE_T n;
    static const WCHAR suffix[] = L".dlumamod.ini";
    if (!module || !out || !capacity) return 0;
    n = wcslen(module);
    if (n >= 4U && dluma_wstricmp(module + n - 4U, L".mod") == 0)
        n -= 4U;
    if (n >= capacity ||
        DLUMA_ARRAY_COUNT(suffix) > (SIZE_T)capacity - n) {
        out[0] = 0;
        return 0;
    }
    dluma_wstr_copy(out, capacity, module);
    out[n] = 0;
    dluma_wstr_append(out, capacity, L".dlumamod.ini");
    return 1;
}

static void dluma_native_make_default_id(const WCHAR* final_path, char* out,
                                       unsigned int capacity)
{
    const WCHAR* name = final_path;
    char utf8[256];
    unsigned int i;
    unsigned int n;
    for (i = 0; final_path && final_path[i]; ++i)
        if (final_path[i] == L'\\' || final_path[i] == L'/') name = final_path + i + 1;
    if (!dluma_wide_to_utf8(name, utf8, DLUMA_ARRAY_COUNT(utf8))) utf8[0] = 0;
    n = (unsigned int)strlen(utf8);
    if (n >= 4U && dluma_stricmp(utf8 + n - 4U, ".mod") == 0) utf8[n - 4U] = 0;
    for (i = 0; utf8[i]; ++i) {
        char c = utf8[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            utf8[i] = '-';
    }
    out[0] = 0;
    dluma_str_append(out, capacity, "local.");
    dluma_str_append(out, capacity, utf8[0] ? utf8 : "unnamed");
}

static int dluma_native_valid_id(const char* value)
{
    unsigned int i;
    if (!value || !value[0] || strlen(value) >= 128U) return 0;
    for (i = 0; value[i]; ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static int dluma_native_sidecar_value(
    const WCHAR* sidecar, const WCHAR* key, const WCHAR* fallback,
    WCHAR* value, unsigned int value_capacity)
{
    DWORD count;
    if (!sidecar || !key || !fallback || !value || value_capacity < 2U)
        return 0;
    value[0] = 0;
    count = GetPrivateProfileStringW(
        L"mod", key, fallback, value, value_capacity, sidecar);
    return count < value_capacity - 1U;
}

static int dluma_native_sidecar_identity_unchanged(
    HANDLE file, const BY_HANDLE_FILE_INFORMATION* before)
{
    BY_HANDLE_FILE_INFORMATION after;
    if (!file || file == INVALID_HANDLE_VALUE || !before ||
        !GetFileInformationByHandle(file, &after))
        return 0;
    return after.dwVolumeSerialNumber == before->dwVolumeSerialNumber &&
           after.nFileIndexHigh == before->nFileIndexHigh &&
           after.nFileIndexLow == before->nFileIndexLow &&
           after.nFileSizeHigh == before->nFileSizeHigh &&
           after.nFileSizeLow == before->nFileSizeLow &&
           after.ftLastWriteTime.dwHighDateTime ==
               before->ftLastWriteTime.dwHighDateTime &&
           after.ftLastWriteTime.dwLowDateTime ==
               before->ftLastWriteTime.dwLowDateTime;
}

static void dluma_native_read_sidecar(DlumaNativeModSnapshot* item)
{
    WCHAR sidecar[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR value[256];
    char utf8[256];
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    int parse_valid = 1;
    item->sidecar_sha256[0] = 0;
    if (!dluma_native_sidecar_path(
            item->final_path, sidecar, DLUMA_ARRAY_COUNT(sidecar))) {
        dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                     "sidecar path exceeds the native discovery limit");
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        return;
    }
    if (!dluma_path_exists(sidecar)) return;
    item->sidecar_present = 1;
    memset(&info, 0, sizeof(info));
    if (!dluma_native_open_regular_final(
            sidecar, &file, final_path, DLUMA_ARRAY_COUNT(final_path), &info) ||
        dluma_wstricmp(final_path, sidecar) != 0 ||
        info.nFileSizeHigh != 0 ||
        (ULONGLONG)info.nFileSizeLow > DLUMA_NATIVE_MOD_SIDECAR_MAX_BYTES ||
        !dluma_sha256_handle_hex(
            file, DLUMA_NATIVE_MOD_SIDECAR_MAX_BYTES,
            item->sidecar_sha256)) {
        dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                     "sidecar is not a locked regular file");
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return;
    }

    if (!dluma_native_sidecar_value(
            sidecar, L"id", L"", value, DLUMA_ARRAY_COUNT(value)))
        parse_valid = 0;
    if (parse_valid && value[0] &&
        dluma_wide_to_utf8(value, utf8, DLUMA_ARRAY_COUNT(utf8)) &&
        dluma_native_valid_id(utf8)) {
        dluma_str_copy(item->id, DLUMA_ARRAY_COUNT(item->id), utf8);
        item->declared_id = 1;
    }
    if (!dluma_native_sidecar_value(
            sidecar, L"name", L"", value, DLUMA_ARRAY_COUNT(value)))
        parse_valid = 0;
    if (parse_valid && value[0] &&
        dluma_wide_to_utf8(value, utf8, DLUMA_ARRAY_COUNT(utf8)))
        dluma_str_copy(item->name, DLUMA_ARRAY_COUNT(item->name), utf8);
    if (!dluma_native_sidecar_value(
            sidecar, L"version", L"", value, DLUMA_ARRAY_COUNT(value)))
        parse_valid = 0;
    if (parse_valid && value[0] &&
        dluma_wide_to_utf8(value, utf8, DLUMA_ARRAY_COUNT(utf8)))
        dluma_str_copy(item->version, DLUMA_ARRAY_COUNT(item->version), utf8);
    if (!dluma_native_sidecar_value(
            sidecar, L"load_priority", L"1000", value,
            DLUMA_ARRAY_COUNT(value)))
        parse_valid = 0;
    if (parse_valid &&
        dluma_wide_to_utf8(value, utf8, DLUMA_ARRAY_COUNT(utf8))) {
        unsigned int priority = dluma_parse_u32(utf8, 1000U);
        item->load_priority = priority <= 1000000U ? (int)priority : 1000;
    }
    if (!parse_valid) {
        dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                     "sidecar field is truncated or malformed");
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
    }
    if (!dluma_native_sidecar_identity_unchanged(file, &info)) {
        dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                     "sidecar identity changed while parsing");
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
    }
    CloseHandle(file);
}

static void dluma_native_ini_path(void)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    if (g_dluma_native_mods.ini_path[0]) return;
    dluma_wstr_copy(runtime, DLUMA_ARRAY_COUNT(runtime), g_dluma.marker_root);
    dluma_path_parent(runtime);
    dluma_path_join(g_dluma_native_mods.ini_path,
                  DLUMA_ARRAY_COUNT(g_dluma_native_mods.ini_path), runtime,
                  L"native_mods.ini");
}

static void dluma_native_section(const DlumaNativeModSnapshot* item, WCHAR* out,
                               unsigned int capacity)
{
    char text[96];
    text[0] = 0;
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "native:");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), item->registry_key);
    dluma_utf8_to_wide(text, out, capacity);
}

static int dluma_native_host_abi_compatible(
    const DlumaNativeModSnapshot* item)
{
    if (!item || !item->pe_machine_valid || !item->pe_dll_valid ||
        !item->declared_host_flags)
        return 0;
    if ((item->declared_host_flags & DLUMA_NATIVE_HOST_PLAYER) &&
        item->abi_kind != DLUMA_NATIVE_ABI_V2)
        return 0;
    if ((item->declared_host_flags & DLUMA_NATIVE_HOST_EDITOR) &&
        !item->editor_abi_available)
        return 0;
    return 1;
}

static void dluma_native_apply_persistence(DlumaNativeModSnapshot* item)
{
    WCHAR section[128];
    WCHAR player_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR editor_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR host_state[16];
    WCHAR legacy_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR legacy_enabled[16];
    char player_fingerprint_utf8[DLUMA_SHA256_HEX_CAPACITY];
    char editor_fingerprint_utf8[DLUMA_SHA256_HEX_CAPACITY];
    char host_state_utf8[16];
    char legacy_fingerprint_utf8[DLUMA_SHA256_HEX_CAPACITY];
    char legacy_enabled_utf8[16];
    unsigned int requested;
    int stale_approval;
    int companion_invalid;
    dluma_native_ini_path();
    dluma_native_section(item, section, DLUMA_ARRAY_COUNT(section));
    player_fingerprint[0] = 0;
    editor_fingerprint[0] = 0;
    host_state[0] = 0;
    legacy_fingerprint[0] = 0;
    legacy_enabled[0] = 0;
    GetPrivateProfileStringW(
        section, L"player_approval_fingerprint", L"", player_fingerprint,
        DLUMA_ARRAY_COUNT(player_fingerprint), g_dluma_native_mods.ini_path);
    GetPrivateProfileStringW(
        section, L"editor_approval_fingerprint", L"", editor_fingerprint,
        DLUMA_ARRAY_COUNT(editor_fingerprint), g_dluma_native_mods.ini_path);
    GetPrivateProfileStringW(
        section, L"host_state_next_launch", L"", host_state,
        DLUMA_ARRAY_COUNT(host_state), g_dluma_native_mods.ini_path);
    dluma_wide_to_utf8(
        player_fingerprint, player_fingerprint_utf8,
        DLUMA_ARRAY_COUNT(player_fingerprint_utf8));
    dluma_wide_to_utf8(
        editor_fingerprint, editor_fingerprint_utf8,
        DLUMA_ARRAY_COUNT(editor_fingerprint_utf8));
    dluma_wide_to_utf8(
        host_state, host_state_utf8, DLUMA_ARRAY_COUNT(host_state_utf8));
    if (!host_state_utf8[0]) {
        WCHAR migrated_player[DLUMA_SHA256_HEX_CAPACITY];
        GetPrivateProfileStringW(
            section, L"approval_fingerprint", L"", legacy_fingerprint,
            DLUMA_ARRAY_COUNT(legacy_fingerprint), g_dluma_native_mods.ini_path);
        GetPrivateProfileStringW(
            section, L"enabled_next_launch", L"", legacy_enabled,
            DLUMA_ARRAY_COUNT(legacy_enabled), g_dluma_native_mods.ini_path);
        dluma_wide_to_utf8(
            legacy_fingerprint, legacy_fingerprint_utf8,
            DLUMA_ARRAY_COUNT(legacy_fingerprint_utf8));
        dluma_wide_to_utf8(
            legacy_enabled, legacy_enabled_utf8,
            DLUMA_ARRAY_COUNT(legacy_enabled_utf8));
        /* Absence of legacy state is a new mod, not a disabled choice. */
        if (!legacy_fingerprint_utf8[0] && !legacy_enabled_utf8[0])
            goto read_host_state;
        /* Preserve the old enabled intent until the startup policy can
         * validate and approve the changed identity after source selection. */
        if (g_dluma.config.auto_enable_mods &&
            dluma_parse_u32(legacy_enabled_utf8, 0) == 1U &&
            dluma_stricmp(legacy_fingerprint_utf8,
                          item->approval_fingerprint) != 0)
            goto read_host_state;
        requested = legacy_fingerprint_utf8[0] &&
            dluma_stricmp(
                legacy_fingerprint_utf8, item->approval_fingerprint) == 0 &&
            dluma_parse_u32(legacy_enabled_utf8, 0) == 1U
                ? DLUMA_NATIVE_HOST_PLAYER : DLUMA_NATIVE_HOST_OFF;
        if (!dluma_utf8_to_wide(
                item->player_approval_fingerprint, migrated_player,
                DLUMA_ARRAY_COUNT(migrated_player)) ||
            (requested == DLUMA_NATIVE_HOST_PLAYER &&
             !WritePrivateProfileStringW(
                 section, L"player_approval_fingerprint", migrated_player,
                 g_dluma_native_mods.ini_path)) ||
            !WritePrivateProfileStringW(
                section, L"host_state_next_launch",
                requested == DLUMA_NATIVE_HOST_PLAYER ? L"1" : L"0",
                g_dluma_native_mods.ini_path) ||
            !WritePrivateProfileStringW(
                section, L"host_approval_migration_v1", L"1",
                g_dluma_native_mods.ini_path)) {
            requested = DLUMA_NATIVE_HOST_OFF;
            player_fingerprint_utf8[0] = 0;
            dluma_log_pair(
                "WARN", "native_mod_host_approval_migration",
                "write_failed_disabled");
        } else {
            dluma_str_copy(
                player_fingerprint_utf8,
                DLUMA_ARRAY_COUNT(player_fingerprint_utf8),
                requested == DLUMA_NATIVE_HOST_PLAYER
                    ? item->player_approval_fingerprint : "");
            dluma_str_copy(
                host_state_utf8, DLUMA_ARRAY_COUNT(host_state_utf8),
                requested == DLUMA_NATIVE_HOST_PLAYER ? "1" : "0");
            dluma_log_pair(
                "INFO", "native_mod_host_approval_migration",
                requested == DLUMA_NATIVE_HOST_PLAYER
                    ? "legacy_player_only" : "legacy_disabled");
        }
    }
read_host_state:
    requested = dluma_parse_u32(host_state_utf8, 0);
    if (requested > DLUMA_NATIVE_HOST_BOTH) requested = DLUMA_NATIVE_HOST_OFF;
    companion_invalid = item->companion_pack.declared &&
        !item->companion_pack.valid;
    item->player_approval_valid = !companion_invalid &&
        player_fingerprint_utf8[0] &&
        dluma_stricmp(
            player_fingerprint_utf8,
            item->player_approval_fingerprint) == 0;
    item->editor_approval_valid = !companion_invalid &&
        editor_fingerprint_utf8[0] &&
        dluma_stricmp(
            editor_fingerprint_utf8,
            item->editor_approval_fingerprint) == 0;
    requested &= item->declared_host_flags;
    if (!item->player_approval_valid)
        requested &= ~DLUMA_NATIVE_HOST_PLAYER;
    if (!item->editor_approval_valid)
        requested &= ~DLUMA_NATIVE_HOST_EDITOR;
    item->requested_host_flags = (unsigned char)requested;
    item->approval_valid = item->player_approval_valid;
    item->enabled_requested =
        (requested & DLUMA_NATIVE_HOST_PLAYER) ? 1U : 0U;
    stale_approval =
        (player_fingerprint_utf8[0] && !item->player_approval_valid) ||
        (editor_fingerprint_utf8[0] && !item->editor_approval_valid);
    if (item->duplicate_id) item->state = DLUMA_NATIVE_STATE_CONFLICT;
    else if (!dluma_native_host_abi_compatible(item))
        item->state = DLUMA_NATIVE_STATE_INCOMPATIBLE;
    else if (companion_invalid)
        item->state =
            (player_fingerprint_utf8[0] || editor_fingerprint_utf8[0])
            ? DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED
            : DLUMA_NATIVE_STATE_INCOMPATIBLE;
    else if (stale_approval)
        item->state = DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED;
    else if (item->requested_host_flags)
        item->state = DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH;
    else
        item->state = DLUMA_NATIVE_STATE_DISCOVERED_DISABLED;
}

static void dluma_native_fingerprint(DlumaNativeModSnapshot* item)
{
    const char* fields[13];
    static const char invalid_pack[] = "<invalid companion pack>";
    char physical_identity[96];
    char companion_identity[96];
    char companion_size[64];
    char number[32];
    physical_identity[0] = 0;
    dluma_u64_to_text(item->volume_serial, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(physical_identity, DLUMA_ARRAY_COUNT(physical_identity), number);
    dluma_str_append(physical_identity, DLUMA_ARRAY_COUNT(physical_identity), ":");
    dluma_u64_to_text(item->file_index_high, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(physical_identity, DLUMA_ARRAY_COUNT(physical_identity), number);
    dluma_str_append(physical_identity, DLUMA_ARRAY_COUNT(physical_identity), ":");
    dluma_u64_to_text(item->file_index_low, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(physical_identity, DLUMA_ARRAY_COUNT(physical_identity), number);
    fields[0] = item->id;
    fields[1] = item->registry_key;
    fields[2] = item->module_sha256;
    fields[3] = item->sidecar_present ? item->sidecar_sha256 : "<no sidecar>";
    fields[4] = item->source_identity;
    fields[5] = item->canonical_relative_path;
    fields[6] = dluma_native_mods_abi_text(item->abi_kind);
    fields[7] = physical_identity;
    if (item->manifest_present) {
        fields[8] = item->manifest_sha256;
        fields[9] = item->lock_sha256;
        fields[10] = item->api_version;
        fields[11] = item->config_schema_fingerprint[0]
            ? item->config_schema_fingerprint : "<no configuration>";
        fields[12] = item->companion_pack.declared
            ? item->companion_pack.sha256 : "<no companion content>";
        dluma_sha256_fields_hex(
            "DLW_NATIVE_MOD_APPROVAL_V3", fields, 0,
            DLUMA_ARRAY_COUNT(fields), item->approval_fingerprint);
    } else if (!item->companion_pack.declared) {
        dluma_sha256_fields_hex(
            "DLW_NATIVE_MOD_APPROVAL_V1", fields, 0, 8U,
            item->approval_fingerprint);
    } else {
        dluma_str_copy(
            companion_identity, DLUMA_ARRAY_COUNT(companion_identity),
            invalid_pack);
        dluma_str_copy(
            companion_size, DLUMA_ARRAY_COUNT(companion_size), invalid_pack);
        if (item->companion_pack.valid) {
            companion_identity[0] = 0;
            dluma_u64_to_text(
                item->companion_pack.volume_serial, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                companion_identity, DLUMA_ARRAY_COUNT(companion_identity), number);
            dluma_str_append(
                companion_identity, DLUMA_ARRAY_COUNT(companion_identity), ":");
            dluma_u64_to_text(
                item->companion_pack.file_index_high, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                companion_identity, DLUMA_ARRAY_COUNT(companion_identity), number);
            dluma_str_append(
                companion_identity, DLUMA_ARRAY_COUNT(companion_identity), ":");
            dluma_u64_to_text(
                item->companion_pack.file_index_low, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                companion_identity, DLUMA_ARRAY_COUNT(companion_identity), number);
            companion_size[0] = 0;
            dluma_u64_to_text(
                item->companion_pack.file_size_high, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                companion_size, DLUMA_ARRAY_COUNT(companion_size), number);
            dluma_str_append(
                companion_size, DLUMA_ARRAY_COUNT(companion_size), ":");
            dluma_u64_to_text(
                item->companion_pack.file_size_low, number,
                DLUMA_ARRAY_COUNT(number));
            dluma_str_append(
                companion_size, DLUMA_ARRAY_COUNT(companion_size), number);
        }
        fields[8] = item->companion_pack.valid
                        ? item->companion_pack.filename : invalid_pack;
        fields[9] = item->companion_pack.valid
                        ? item->companion_pack.manifest_path : invalid_pack;
        fields[10] = item->companion_pack.valid
                         ? item->companion_pack.sha256 : invalid_pack;
        fields[11] = companion_identity;
        fields[12] = companion_size;
        dluma_sha256_fields_hex(
            "DLW_NATIVE_MOD_APPROVAL_V2", fields, 0,
            DLUMA_ARRAY_COUNT(fields), item->approval_fingerprint);
    }
    {
        const char* host_fields[2];
        host_fields[0] = item->approval_fingerprint;
        host_fields[1] = "player";
        dluma_sha256_fields_hex(
            "DLW_NATIVE_MOD_HOST_APPROVAL_V1", host_fields, 0, 2U,
            item->player_approval_fingerprint);
        host_fields[1] = "editor";
        dluma_sha256_fields_hex(
            "DLW_NATIVE_MOD_HOST_APPROVAL_V1", host_fields, 0, 2U,
            item->editor_approval_fingerprint);
    }
}

static int dluma_native_compare_entry(const DlumaNativeModEntry* a,
                                    const DlumaNativeModEntry* b)
{
    int compare;
    if (a->snapshot.load_priority != b->snapshot.load_priority)
        return a->snapshot.load_priority < b->snapshot.load_priority ? -1 : 1;
    compare = dluma_stricmp(a->snapshot.source_identity, b->snapshot.source_identity);
    if (compare) return compare;
    compare = dluma_stricmp(a->snapshot.canonical_relative_path,
                          b->snapshot.canonical_relative_path);
    if (compare) return compare;
    return dluma_stricmp(a->snapshot.registry_key, b->snapshot.registry_key);
}

static void dluma_native_sort_entries(void)
{
    unsigned int i;
    for (i = 1; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModEntry value = g_dluma_native_mods.entries[i];
        unsigned int j = i;
        while (j && dluma_native_compare_entry(&g_dluma_native_mods.entries[j - 1],
                                             &value) > 0) {
            g_dluma_native_mods.entries[j] = g_dluma_native_mods.entries[j - 1];
            --j;
        }
        g_dluma_native_mods.entries[j] = value;
    }
    for (i = 0; i < g_dluma_native_mods.count; ++i)
        g_dluma_native_mods.entries[i].snapshot.resolved_load_index = i;
}

static void dluma_native_mark_conflicts(void)
{
    unsigned int i;
    unsigned int j;
    unsigned char processed[DLUMA_MAX_NATIVE_MODS];
    memset(processed, 0, sizeof(processed));
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* a = &g_dluma_native_mods.entries[i].snapshot;
        if (!a->declared_id) continue;
        for (j = i + 1; j < g_dluma_native_mods.count; ++j) {
            DlumaNativeModSnapshot* b = &g_dluma_native_mods.entries[j].snapshot;
            if (b->declared_id && dluma_stricmp(a->id, b->id) == 0 &&
                dluma_stricmp(a->source_identity,
                            b->source_identity) == 0 &&
                (a->volume_serial != b->volume_serial ||
                 a->file_index_high != b->file_index_high ||
                 a->file_index_low != b->file_index_low)) {
                a->duplicate_id = 1;
                b->duplicate_id = 1;
            }
        }
    }
    for (i = 0; i < g_dluma_native_mods.count; ++i)
        dluma_native_apply_persistence(&g_dluma_native_mods.entries[i].snapshot);
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* first =
            &g_dluma_native_mods.entries[i].snapshot;
        char selected_source[512];
        int explicit_selection = 0;
        int selected_index = -1;
        int local_index = -1;
        int any_index = -1;
        int any_local_index = -1;
        int explicit_failed_index = -1;
        if (processed[i]) continue;
        selected_source[0] = 0;
        dluma_mod_source_selection_read(
            "dll", first->id, selected_source,
            DLUMA_ARRAY_COUNT(selected_source), &explicit_selection);
        /* A copy that reported an error is preferred last but stays
         * resolvable, so a declared ID whose copies all failed still offers a
         * source the player can choose. Loading remains gated on approval and
         * ABI compatibility. */
        for (j = i; j < g_dluma_native_mods.count; ++j) {
            DlumaNativeModSnapshot* item =
                &g_dluma_native_mods.entries[j].snapshot;
            if (dluma_stricmp(first->id, item->id) != 0) continue;
            processed[j] = 1;
            if (item->duplicate_id) continue;
            if (explicit_selection &&
                dluma_stricmp(item->source_identity,
                            selected_source) == 0) {
                if (!item->runtime_error[0])
                    selected_index = (int)j;
                else if (explicit_failed_index < 0)
                    explicit_failed_index = (int)j;
            }
            if (!item->source_is_subscribed &&
                (any_local_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_native_mods.entries[any_local_index]
                         .snapshot.project_root) < 0))
                any_local_index = (int)j;
            if (any_index < 0 ||
                dluma_wstricmp(
                    item->project_root,
                    g_dluma_native_mods.entries[any_index]
                        .snapshot.project_root) < 0)
                any_index = (int)j;
            if (item->runtime_error[0]) continue;
            if (!item->source_is_subscribed &&
                (local_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_native_mods.entries[local_index]
                         .snapshot.project_root) < 0))
                local_index = (int)j;
            if (!explicit_selection &&
                (selected_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_native_mods.entries[selected_index]
                         .snapshot.project_root) < 0))
                selected_index = (int)j;
        }
        if (!explicit_selection && local_index >= 0)
            selected_index = local_index;
        if (explicit_selection) {
            if (selected_index < 0)
                selected_index = explicit_failed_index;
        } else if (selected_index < 0) {
            selected_index =
                any_local_index >= 0 ? any_local_index : any_index;
        }
        if (!explicit_selection && selected_index >= 0)
            dluma_mod_source_selection_write(
                "dll", first->id,
                g_dluma_native_mods.entries[selected_index]
                    .snapshot.source_identity);
        for (j = i; j < g_dluma_native_mods.count; ++j) {
            DlumaNativeModSnapshot* item =
                &g_dluma_native_mods.entries[j].snapshot;
            if (dluma_stricmp(first->id, item->id) != 0) continue;
            item->source_selected = (unsigned char)
                ((int)j == selected_index ? 1 : 0);
            item->source_selection_missing = (unsigned char)
                (explicit_selection && selected_index < 0 ? 1 : 0);
        }
    }
}

static void dluma_native_config_identity(
    const DlumaNativeModSnapshot* item,
    DlumaModConfigIdentity* out);

static void dluma_native_prune_config_registry(void)
{
    DlumaModConfigIdentity* identities;
    unsigned int index;
    identities = (DlumaModConfigIdentity*)dluma_alloc(
        (SIZE_T)g_dluma_native_mods.count * sizeof(*identities));
    if (!identities && g_dluma_native_mods.count)
        return;
    for (index = 0; index < g_dluma_native_mods.count; ++index)
        dluma_native_config_identity(
            &g_dluma_native_mods.entries[index].snapshot,
            &identities[index]);
    dluma_mod_config_prune_owner(
        DLUMA_MOD_CONFIG_OWNER_NATIVE,
        identities, g_dluma_native_mods.count);
    dluma_free(identities);
}

static int dluma_native_identity_is_loaded_module(const DlumaNativeFileIdentity* id,
                                                HMODULE module)
{
    WCHAR path[DLUMA_MAX_WPATH];
    DlumaNativeFileIdentity module_id;
    if (!id || !module) return 0;
    return dluma_get_module_path_checked(
               module, path, DLUMA_ARRAY_COUNT(path)) &&
           dluma_native_get_file_identity(path, &module_id) &&
           dluma_native_same_identity(id, &module_id);
}

static void dluma_native_config_identity(
    const DlumaNativeModSnapshot* item,
    DlumaModConfigIdentity* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!item) return;
    dluma_str_copy(
        out->registry_key, DLUMA_ARRAY_COUNT(out->registry_key),
        item->registry_key);
    dluma_str_copy(
        out->storage_key, DLUMA_ARRAY_COUNT(out->storage_key),
        item->registry_key);
    dluma_str_copy(
        out->metadata_fingerprint,
        DLUMA_ARRAY_COUNT(out->metadata_fingerprint),
        item->approval_fingerprint);
    dluma_str_copy(
        out->config_schema_fingerprint,
        DLUMA_ARRAY_COUNT(out->config_schema_fingerprint),
        item->config_schema_fingerprint);
    out->config_available = item->config_available;
    out->owner_kind = DLUMA_MOD_CONFIG_OWNER_NATIVE;
}

static void dluma_native_add_candidate(const WCHAR* project_root,
                                     const WCHAR* mods_dir,
                                     const WCHAR* candidate)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR final_mods[DLUMA_MAX_WPATH];
    WCHAR final_project[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    BY_HANDLE_FILE_INFORMATION mods_info;
    BY_HANDLE_FILE_INFORMATION project_info;
    DlumaNativeFileIdentity identity;
    DlumaNativeModEntry* entry;
    DlumaNativeModSnapshot* item;
    char relative_utf8[512];
    DlumaProjectSourceDescription source;
    const char* key_fields[2];
    unsigned int i;
    HANDLE directory = INVALID_HANDLE_VALUE;
    if (g_dluma_native_mods.count >= DLUMA_MAX_NATIVE_MODS) {
        dluma_log_line("WARN", "native_mod_registry_truncated at bounded capacity");
        return;
    }
    if (!dluma_native_open_regular_final(candidate, &file, final_path,
                                       DLUMA_ARRAY_COUNT(final_path), &info))
        return;
    /* A directory's final path is needed for containment.  It is opened with
     * backup semantics, while the candidate itself was opened as a regular
     * file above. */
    directory = CreateFileW(mods_dir, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            0, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                            0);
    if (directory == INVALID_HANDLE_VALUE ||
        !dluma_get_final_path_checked(
            directory, final_mods, DLUMA_ARRAY_COUNT(final_mods)) ||
        !GetFileInformationByHandle(directory, &mods_info) ||
        (mods_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        CloseHandle(file);
        return;
    }
    CloseHandle(directory);
    directory = CreateFileW(project_root, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            0, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                            0);
    if (directory == INVALID_HANDLE_VALUE ||
        !dluma_get_final_path_checked(
            directory, final_project, DLUMA_ARRAY_COUNT(final_project)) ||
        !GetFileInformationByHandle(directory, &project_info) ||
        (project_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_native_path_under(final_mods, final_project) ||
        !dluma_native_path_under(final_path, final_mods)) {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        CloseHandle(file);
        return;
    }
    CloseHandle(directory);
    {
        const WCHAR* relative = final_path + wcslen(final_mods);
        SIZE_T relative_length;
        while (*relative == L'\\' || *relative == L'/') ++relative;
        if (!relative[0] ||
            !dluma_wide_to_utf8(
                relative, relative_utf8, DLUMA_ARRAY_COUNT(relative_utf8))) {
            CloseHandle(file);
            return;
        }
        relative_length = strlen(relative_utf8);
        if (relative_length >
            DLUMA_ARRAY_COUNT(relative_utf8) - sizeof("mods\\")) {
            CloseHandle(file);
            return;
        }
    }
    identity.volume_serial = info.dwVolumeSerialNumber;
    identity.file_index_high = info.nFileIndexHigh;
    identity.file_index_low = info.nFileIndexLow;
    if (dluma_native_identity_is_loaded_module(&identity, g_dluma.self_module) ||
        dluma_native_identity_is_loaded_module(&identity,
                                              g_dluma_native_original_gamedll)) {
        dluma_log_line("INFO", "native_mod_candidate_excluded_selected_loader_identity");
        CloseHandle(file);
        return;
    }
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* existing = &g_dluma_native_mods.entries[i].snapshot;
        if (existing->volume_serial == identity.volume_serial &&
            existing->file_index_high == identity.file_index_high &&
            existing->file_index_low == identity.file_index_low) {
            dluma_log_line("INFO", "native_mod_candidate_deduplicated_physical_file");
            CloseHandle(file);
            return;
        }
    }
    entry = &g_dluma_native_mods.entries[g_dluma_native_mods.count];
    memset(entry, 0, sizeof(*entry));
    item = &entry->snapshot;
    item->kind = DLUMA_MOD_KIND_NATIVE_DLL;
    item->declared_host_flags = DLUMA_NATIVE_HOST_PLAYER;
    item->volume_serial = identity.volume_serial;
    item->file_index_high = identity.file_index_high;
    item->file_index_low = identity.file_index_low;
    item->file_size = info.nFileSizeLow;
    item->load_priority = 1000;
    dluma_wstr_copy(item->final_path, DLUMA_ARRAY_COUNT(item->final_path), final_path);
    dluma_wstr_copy(item->project_root, DLUMA_ARRAY_COUNT(item->project_root),
                  final_project);
    dluma_native_make_default_id(final_path, item->id, DLUMA_ARRAY_COUNT(item->id));
    dluma_str_copy(item->name, DLUMA_ARRAY_COUNT(item->name), item->id + 6);
    dluma_str_copy(item->version, DLUMA_ARRAY_COUNT(item->version), "unknown");
    if (!dluma_classify_project_source(project_root, &source)) {
        CloseHandle(file);
        return;
    }
    dluma_str_copy(item->source_name, DLUMA_ARRAY_COUNT(item->source_name),
                 source.project_name);
    item->source_is_workshop = source.is_subscribed_workshop;
    item->source_is_subscribed = source.is_subscribed_workshop;
    dluma_str_copy(item->workshop_id, DLUMA_ARRAY_COUNT(item->workshop_id),
                 source.published_id);
    dluma_str_copy(item->source_identity,
                 DLUMA_ARRAY_COUNT(item->source_identity),
                 source.source_identity);
    item->canonical_relative_path[0] = 0;
    dluma_str_append(item->canonical_relative_path,
                   DLUMA_ARRAY_COUNT(item->canonical_relative_path), "mods\\");
    dluma_str_append(item->canonical_relative_path,
                   DLUMA_ARRAY_COUNT(item->canonical_relative_path),
                   relative_utf8);
    key_fields[0] = item->source_identity;
    key_fields[1] = item->canonical_relative_path;
    dluma_sha256_fields_hex("DLW_NATIVE_MOD_REGISTRY_KEY_V1", key_fields, 0, 2,
                          item->registry_key);
    if (!dluma_sha256_file_hex(final_path, DLUMA_NATIVE_MOD_FILE_MAX_BYTES,
                             item->module_sha256) ||
        !dluma_native_inspect_pe(final_path, item)) {
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                     "malformed, unreadable, oversized, or non-PE module");
    }
    dluma_native_read_sidecar(item);
    dluma_native_fingerprint(item);
    ++g_dluma_native_mods.count;
    CloseHandle(file);
}

static void dluma_native_add_legacy_candidate(
    const WCHAR* project_root,
    const WCHAR* mods_dir,
    const WCHAR* candidate)
{
    unsigned int before = g_dluma_native_mods.count;
    dluma_native_add_candidate(project_root, mods_dir, candidate);
    if (g_dluma_native_mods.count > before) {
        DlumaNativeModSnapshot* item =
            &g_dluma_native_mods.entries[before].snapshot;
        if (item->abi_kind == DLUMA_NATIVE_ABI_V2) {
            item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
            dluma_str_copy(
                item->runtime_error,
                DLUMA_ARRAY_COUNT(item->runtime_error),
                "DLUMA API 1.0 native mods require mod.dluma.json and "
                "mod.dluma.lock.json");
            dluma_native_fingerprint(item);
        }
    }
}

static void dluma_native_add_manifest_candidate(
    const WCHAR* project_root,
    const WCHAR* mods_dir,
    const WCHAR* manifest_path)
{
    DlumaModPackageSnapshot package;
    DlumaModArtifactSnapshot config_artifact;
    DlumaLuaConfigSchema* config_schema = 0;
    DlumaModConfigIdentity config_identity;
    WCHAR package_root[DLUMA_MAX_WPATH];
    WCHAR companion_path[DLUMA_MAX_WPATH];
    WCHAR companion_relative[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    DlumaNativeModEntry* entry;
    DlumaNativeModSnapshot* item;
    const char* key_fields[2];
    unsigned int companion_index;
    unsigned int before = g_dluma_native_mods.count;
    memset(&package, 0, sizeof(package));
    memset(&config_artifact, 0, sizeof(config_artifact));
    config_artifact.struct_size =
        (unsigned int)sizeof(config_artifact);
    package.struct_size = (unsigned int)sizeof(package);
    dluma_wstr_copy(
        package_root, DLUMA_ARRAY_COUNT(package_root), manifest_path);
    dluma_path_parent(package_root);
    if (dluma_mod_package_discover(
            package_root, &package) != DLUMA_STATUS_OK ||
        !package.valid ||
        package.manifest.language != DLUMA_MOD_LANGUAGE_NATIVE) {
        dluma_log_line(
            "WARN", "native_mod_manifest_package_rejected");
        return;
    }
    dluma_native_add_candidate(
        project_root, mods_dir, package.entry_path);
    if (g_dluma_native_mods.count <= before) return;
    entry = &g_dluma_native_mods.entries[before];
    item = &entry->snapshot;
    entry->package = package;
    dluma_wstr_copy(
        entry->project_root, DLUMA_ARRAY_COUNT(entry->project_root),
        project_root);
    dluma_str_copy(item->id, DLUMA_ARRAY_COUNT(item->id),
                 package.manifest.id);
    dluma_str_copy(item->name, DLUMA_ARRAY_COUNT(item->name),
                 package.manifest.name);
    dluma_str_copy(item->description, DLUMA_ARRAY_COUNT(item->description),
                 package.manifest.description);
    dluma_str_copy(item->version, DLUMA_ARRAY_COUNT(item->version),
                 package.manifest.version);
    item->load_priority = package.manifest.load_priority;
    item->declared_id = 1;
    item->manifest_present = 1;
    item->scope = (unsigned char)package.manifest.scope;
    item->access_count = package.manifest.access_count;
    item->declared_host_flags =
        (unsigned char)package.manifest.host_flags;
    item->quest_phase_handler_count =
        package.manifest.quest_phase_handler_count;
    for (companion_index = 0;
         companion_index < item->quest_phase_handler_count;
         ++companion_index) {
        dluma_str_copy(
            item->quest_phase_handlers[companion_index].id,
            DLUMA_ARRAY_COUNT(item->quest_phase_handlers[companion_index].id),
            package.manifest.quest_phase_handlers[companion_index].id);
        dluma_str_copy(
            item->quest_phase_handlers[companion_index].command_alias,
            DLUMA_ARRAY_COUNT(
                item->quest_phase_handlers[companion_index].command_alias),
            package.manifest.quest_phase_handlers[companion_index].command_alias);
        dluma_str_copy(
            item->quest_phase_handlers[companion_index].description,
            DLUMA_ARRAY_COUNT(
                item->quest_phase_handlers[companion_index].description),
            package.manifest.quest_phase_handlers[companion_index].description);
    }
    if (!dluma_native_host_abi_compatible(item)) {
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        dluma_str_copy(
            item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
            "native module exports do not satisfy its declared Player/Editor hosts");
    }
    dluma_str_copy(
        item->api_version, DLUMA_ARRAY_COUNT(item->api_version),
        DLUMA_API_VERSION);
    dluma_str_copy(
        item->manifest_sha256,
        DLUMA_ARRAY_COUNT(item->manifest_sha256),
        package.manifest_sha256);
    dluma_str_copy(
        item->lock_sha256,
        DLUMA_ARRAY_COUNT(item->lock_sha256),
        package.lock_sha256);
    if (dluma_stricmp(
            item->module_sha256,
            package.entry_sha256) != 0) {
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            "manifest entry hash differs from the locked native module");
    }
    if (item->sidecar_present) {
        memset(&item->companion_pack, 0, sizeof(item->companion_pack));
        item->sidecar_present = 0;
        item->sidecar_sha256[0] = 0;
        item->config_declared = 0;
        item->config_available = 0;
        item->config_schema_fingerprint[0] = 0;
        item->abi_kind = DLUMA_NATIVE_ABI_INVALID;
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            "legacy .dlumamod.ini sidecars are unsupported in DLUMA API 1.0 "
            "packages");
    }
    if (package.manifest.configuration_schema[0]) {
        item->config_declared = 1;
        item->config_available = 0;
        config_schema = (DlumaLuaConfigSchema*)dluma_alloc(
            sizeof(*config_schema));
        if (!config_schema) {
            dluma_str_copy(
                item->config_diagnostic,
                DLUMA_ARRAY_COUNT(item->config_diagnostic),
                "config_schema_allocation_failed");
        } else {
            memset(config_schema, 0, sizeof(*config_schema));
            if (dluma_mod_package_read_artifact(
                    &package,
                    package.manifest.configuration_schema,
                    DLUMA_MOD_ARTIFACT_CONFIGURATION,
                    DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES,
                    &config_artifact) != DLUMA_STATUS_OK) {
                dluma_str_copy(
                    item->config_diagnostic,
                    DLUMA_ARRAY_COUNT(item->config_diagnostic),
                    config_artifact.diagnostic[0]
                        ? config_artifact.diagnostic
                        : "locked_config_schema_unavailable");
            } else if (dluma_native_mod_config_parse_json(
                    (const char*)config_artifact.bytes,
                    config_artifact.size, config_schema,
                    item->config_diagnostic,
                    DLUMA_ARRAY_COUNT(item->config_diagnostic))) {
                item->config_available = 1;
                item->config_category_count =
                    (unsigned short)config_schema->category_count;
                item->config_setting_count =
                    (unsigned short)config_schema->setting_count;
                dluma_str_copy(
                    item->config_schema_fingerprint,
                    DLUMA_ARRAY_COUNT(
                        item->config_schema_fingerprint),
                    config_schema->fingerprint);
            }
            dluma_mod_artifact_release(&config_artifact);
        }
    }
    for (companion_index = 0;
         companion_index < package.manifest.companion_count;
         ++companion_index) {
        const DlumaModCompanionDeclaration* declaration =
            &package.manifest.companion[companion_index];
        if (!declaration->audio_manifest[0])
            continue;
        if (item->companion_pack.declared) {
            item->companion_pack.valid = 0;
            dluma_str_copy(
                item->companion_pack.error,
                DLUMA_ARRAY_COUNT(item->companion_pack.error),
                "multiple audio companion declarations require host-time opening");
            break;
        }
        if (!dluma_utf8_to_wide(
                declaration->path, companion_relative,
                DLUMA_ARRAY_COUNT(companion_relative))) {
            item->companion_pack.declared = 1;
            item->companion_pack.valid = 0;
            dluma_str_copy(
                item->companion_pack.error,
                DLUMA_ARRAY_COUNT(item->companion_pack.error),
                "manifest companion path is invalid");
            break;
        }
        {
            unsigned int path_index;
            for (path_index = 0;
                 companion_relative[path_index]; ++path_index)
                if (companion_relative[path_index] == L'/')
                    companion_relative[path_index] = L'\\';
        }
        dluma_path_join(
            companion_path, DLUMA_ARRAY_COUNT(companion_path),
            package.package_root, companion_relative);
        if (!companion_path[0] ||
            !dluma_companion_archive_discover_file(
                companion_path,
                declaration->audio_manifest,
                &item->companion_pack)) {
            item->companion_pack.declared = 1;
            item->companion_pack.valid = 0;
            if (!item->companion_pack.error[0])
                dluma_str_copy(
                    item->companion_pack.error,
                    DLUMA_ARRAY_COUNT(item->companion_pack.error),
                    "manifest companion path is invalid");
        }
    }
    key_fields[0] = item->source_identity;
    key_fields[1] = item->canonical_relative_path;
    dluma_sha256_fields_hex(
        "DLW_NATIVE_MOD_REGISTRY_KEY_V2", key_fields, 0, 2U,
        item->registry_key);
    dluma_native_fingerprint(item);
    if (item->config_available) {
        dluma_native_config_identity(item, &config_identity);
        if (!config_schema ||
            !dluma_mod_config_publish(
                &config_identity, config_schema)) {
            item->config_available = 0;
            dluma_str_copy(
                item->config_diagnostic,
                DLUMA_ARRAY_COUNT(item->config_diagnostic),
                config_schema
                    ? "config_registry_publish_failed"
                    : "config_schema_allocation_failed");
        }
    }
    dluma_free(config_schema);
    /*
     * dluma_native_add_candidate() initially applies legacy-path persistence
     * before the API 1.0 manifest has supplied the stable ID, V2 registry key,
     * locked companion identity, and final approval fingerprint.  Reapply it
     * only after those values are final so menu approval survives restart and
     * companion failures are reflected in the visible state.
     */
    dluma_native_apply_persistence(item);
}

void dluma_native_mods_set_original_gamedll(HMODULE original_gamedll)
{
    g_dluma_native_original_gamedll = original_gamedll;
}

static int dluma_native_open_scan_directory(
    const WCHAR* directory, const WCHAR* final_mods)
{
    HANDLE handle;
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    DWORD attributes;
    if (!directory || !directory[0] || !final_mods || !final_mods[0])
        return 0;
    attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    handle = CreateFileW(
        directory, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    memset(&info, 0, sizeof(info));
    if (!dluma_get_final_path_checked(
            handle, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !GetFileInformationByHandle(handle, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_native_path_under(final_path, final_mods)) {
        CloseHandle(handle);
        return 0;
    }
    CloseHandle(handle);
    return 1;
}

static void dluma_native_scan_directory(
    const WCHAR* project_root, const WCHAR* mods,
    const WCHAR* final_mods, const WCHAR* directory,
    unsigned int depth, unsigned int* directories_seen,
    unsigned int* traversal_truncated)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    const WCHAR* relative_directory;
    int inside_native_root;
    if (!directories_seen || !traversal_truncated ||
        depth > DLUMA_NATIVE_MAX_DISCOVERY_DEPTH ||
        *directories_seen >= DLUMA_NATIVE_MAX_DISCOVERY_DIRECTORIES) {
        if (traversal_truncated) *traversal_truncated = 1;
        return;
    }
    if (!dluma_native_open_scan_directory(directory, final_mods)) return;
    relative_directory = directory + wcslen(mods);
    while (*relative_directory == L'\\' ||
           *relative_directory == L'/')
        ++relative_directory;
    inside_native_root =
        dluma_wstarts_with_icase(relative_directory, L"native") &&
        (!relative_directory[6] ||
         relative_directory[6] == L'\\' ||
         relative_directory[6] == L'/');
    ++*directories_seen;
    if (!dluma_native_path_join_checked(
            pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*")) {
        *traversal_truncated = 1;
        return;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        WCHAR candidate[DLUMA_MAX_WPATH];
        if (dluma_native_is_dot_dir(data.cFileName) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (!dluma_native_path_join_checked(
                candidate, DLUMA_ARRAY_COUNT(candidate),
                directory, data.cFileName)) {
            *traversal_truncated = 1;
            continue;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth >= DLUMA_NATIVE_MAX_DISCOVERY_DEPTH) {
                *traversal_truncated = 1;
                continue;
            }
            dluma_native_scan_directory(
                project_root, mods, final_mods, candidate, depth + 1U,
                directories_seen, traversal_truncated);
            continue;
        }
        if (dluma_wstricmp(data.cFileName, L"mod.dluma.json") == 0) {
            ++g_dluma_native_mods.total_found;
            dluma_native_add_manifest_candidate(
                project_root, mods, candidate);
            continue;
        }
        if (!dluma_wends_with_icase(data.cFileName, L".mod") ||
            inside_native_root)
            continue;
        ++g_dluma_native_mods.total_found;
        dluma_native_add_legacy_candidate(
            project_root, mods, candidate);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void dluma_native_scan_project(
    const WCHAR* project_root, unsigned int* directories_seen,
    unsigned int* traversal_truncated)
{
    WCHAR mods[DLUMA_MAX_WPATH];
    WCHAR final_mods[DLUMA_MAX_WPATH];
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION info;
    if (!project_root || !project_root[0] ||
        !directories_seen || !traversal_truncated)
        return;
    if (GetFileAttributesW(project_root) & FILE_ATTRIBUTE_REPARSE_POINT) return;
    if (!dluma_native_path_join_checked(
            mods, DLUMA_ARRAY_COUNT(mods), project_root, L"mods"))
        return;
    directory = CreateFileW(
        mods, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE) return;
    memset(&info, 0, sizeof(info));
    if (!dluma_get_final_path_checked(
            directory, final_mods, DLUMA_ARRAY_COUNT(final_mods)) ||
        !GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(directory);
        return;
    }
    CloseHandle(directory);
    dluma_native_scan_directory(
        project_root, mods, final_mods, mods, 0U,
        directories_seen, traversal_truncated);
}

static int dluma_native_project_seen(WCHAR projects[][DLUMA_MAX_WPATH],
                                   unsigned int count, const WCHAR* candidate)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        if (dluma_wstricmp(projects[i], candidate) == 0) return 1;
    return 0;
}

static void dluma_native_auto_enable_startup(void);
static int g_dluma_native_initial_discovery_complete;

void dluma_native_mods_discover(void)
{
    WCHAR (*projects)[DLUMA_MAX_WPATH] = 0;
    unsigned int project_count = 0;
    unsigned int r;
    unsigned int i;
    unsigned int logged_count;
    unsigned int logged_total;
    unsigned int directories_seen = 0;
    unsigned int traversal_truncated = 0;
    dluma_native_lock();
    /* Once code has been loaded, a rescan could replace its bookkeeping while
     * callback pointers remain live.  New files are therefore next-process
     * discovery; the current process stays immutable. */
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        if (g_dluma_native_mods.entries[i].module ||
            g_dluma_native_mods.entries[i].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_IDLE) {
            dluma_native_unlock();
            return;
        }
    }
    memset(g_dluma_native_mods.entries, 0, sizeof(g_dluma_native_mods.entries));
    g_dluma_native_mods.count = 0;
    g_dluma_native_mods.total_found = 0;
    projects = (WCHAR (*)[DLUMA_MAX_WPATH])dluma_alloc(
        (SIZE_T)DLUMA_NATIVE_MAX_DISCOVERY_PROJECTS * sizeof(*projects));
    if (!projects) {
        ++g_dluma_native_mods.revision;
        dluma_native_publish_crash_context_locked();
        dluma_native_unlock();
        dluma_log_line("ERROR", "native_mod_discovery_allocation_failed");
        return;
    }
    for (r = 0; r < g_dluma.workshop_root_count; ++r) {
        WCHAR pattern[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW data;
        HANDLE find;
        const WCHAR* root = g_dluma.workshop_roots[r];
        if (!root[0] || (GetFileAttributesW(root) & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (!dluma_native_project_seen(projects, project_count, root) &&
            project_count < DLUMA_NATIVE_MAX_DISCOVERY_PROJECTS)
            dluma_wstr_copy(projects[project_count++], DLUMA_MAX_WPATH, root);
        dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*");
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            WCHAR project[DLUMA_MAX_WPATH];
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                dluma_native_is_dot_dir(data.cFileName)) continue;
            dluma_path_join(project, DLUMA_ARRAY_COUNT(project), root, data.cFileName);
            if (!dluma_native_project_seen(projects, project_count, project) &&
                project_count < DLUMA_NATIVE_MAX_DISCOVERY_PROJECTS)
                dluma_wstr_copy(projects[project_count++], DLUMA_MAX_WPATH, project);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    for (i = 0; i < project_count; ++i)
        dluma_native_scan_project(
            projects[i], &directories_seen, &traversal_truncated);
    dluma_native_sort_entries();
    dluma_native_mark_conflicts();
    if (!g_dluma_native_initial_discovery_complete) {
        g_dluma_native_initial_discovery_complete = 1;
        dluma_native_auto_enable_startup();
    }
    dluma_native_prune_config_registry();
    ++g_dluma_native_mods.revision;
    dluma_native_publish_crash_context_locked();
    logged_count = g_dluma_native_mods.count;
    logged_total = g_dluma_native_mods.total_found;
    dluma_native_unlock();
    dluma_log_u64("INFO", "native_mod_roots_scanned", project_count);
    dluma_log_u64("INFO", "native_mod_directories_scanned", directories_seen);
    dluma_log_u64(
        traversal_truncated ? "WARN" : "INFO",
        "native_mod_directory_traversal_truncated", traversal_truncated);
    dluma_log_u64("INFO", "native_mod_candidates_found", logged_total);
    for (i = 0; i < logged_count; ++i) {
        DlumaNativeModSnapshot item_copy;
        char path[DLUMA_MAX_WPATH * 2];
        dluma_native_lock();
        if (i >= g_dluma_native_mods.count) {
            dluma_native_unlock();
            break;
        }
        memcpy(&item_copy, &g_dluma_native_mods.entries[i].snapshot,
               sizeof(item_copy));
        dluma_native_unlock();
        dluma_wide_to_utf8(
            item_copy.final_path, path, DLUMA_ARRAY_COUNT(path));
        dluma_log_pair("INFO", "native_mod_candidate_path", path);
        dluma_log_pair(
            "INFO", "native_mod_candidate_source",
            item_copy.source_identity);
        dluma_log_pair(
            "INFO", "native_mod_candidate_sha256",
            item_copy.module_sha256);
        dluma_log_pair(
            "INFO", "native_mod_candidate_registry_key",
            item_copy.registry_key);
        dluma_log_pair(
            "INFO", "native_mod_candidate_approval_fingerprint",
            item_copy.approval_fingerprint);
        dluma_log_pair(
            "INFO", "native_mod_candidate_player_approval_fingerprint",
            item_copy.player_approval_fingerprint);
        dluma_log_pair("INFO", "native_mod_candidate_abi",
                     dluma_native_mods_abi_text(item_copy.abi_kind));
        dluma_log_pair("INFO", "native_mod_candidate_default_state", "disabled");
        dluma_log_u64("INFO", "native_mod_resolved_load_order",
                    item_copy.resolved_load_index);
    }
    dluma_free(projects);
}

unsigned int dluma_native_mods_snapshot(DlumaNativeModSnapshot* out,
                                      unsigned int capacity,
                                      ULONGLONG* revision_out)
{
    unsigned int count;
    dluma_native_lock();
    count = g_dluma_native_mods.count;
    if (revision_out) *revision_out = g_dluma_native_mods.revision;
    if (out && capacity) {
        unsigned int copied = count < capacity ? count : capacity;
        unsigned int i;
        for (i = 0; i < copied; ++i)
            out[i] = g_dluma_native_mods.entries[i].snapshot;
    }
    dluma_native_unlock();
    return count;
}

void dluma_native_mods_make_mutation_request(DlumaNativeModMutationRequest* request,
                                           const DlumaNativeModSnapshot* item,
                                           ULONGLONG revision)
{
    if (!request) return;
    memset(request, 0, sizeof(*request));
    if (!item) return;
    dluma_str_copy(request->registry_key, DLUMA_ARRAY_COUNT(request->registry_key),
                 item->registry_key);
    dluma_str_copy(request->approval_fingerprint,
                 DLUMA_ARRAY_COUNT(request->approval_fingerprint),
                 item->approval_fingerprint);
    request->registry_revision = revision;
}

static int dluma_native_write_hosts(
    const DlumaNativeModSnapshot* item,
    unsigned int host_flags)
{
    WCHAR section[128];
    WCHAR player_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR editor_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR host_state[16];
    WCHAR temporary[DLUMA_MAX_WPATH];
    char host_state_utf8[16];
    if (!item || host_flags > DLUMA_NATIVE_HOST_BOTH ||
        (host_flags & ~item->declared_host_flags))
        return 0;
    dluma_native_ini_path();
    dluma_native_section(item, section, DLUMA_ARRAY_COUNT(section));
    if (!dluma_utf8_to_wide(
            item->player_approval_fingerprint, player_fingerprint,
            DLUMA_ARRAY_COUNT(player_fingerprint)) ||
        !dluma_utf8_to_wide(
            item->editor_approval_fingerprint, editor_fingerprint,
            DLUMA_ARRAY_COUNT(editor_fingerprint)) ||
        !dluma_u64_to_text(
            host_flags, host_state_utf8,
            DLUMA_ARRAY_COUNT(host_state_utf8)) ||
        !dluma_utf8_to_wide(
            host_state_utf8, host_state, DLUMA_ARRAY_COUNT(host_state)))
        return 0;
    if (!dluma_atomic_ini_begin(g_dluma_native_mods.ini_path,
                                temporary, DLUMA_ARRAY_COUNT(temporary)))
        return 0;
    if (!WritePrivateProfileStringW(
            section, L"player_approval_fingerprint",
            (host_flags & DLUMA_NATIVE_HOST_PLAYER)
                ? player_fingerprint : 0,
            temporary) ||
        !WritePrivateProfileStringW(
            section, L"editor_approval_fingerprint",
            (host_flags & DLUMA_NATIVE_HOST_EDITOR)
                ? editor_fingerprint : 0,
            temporary) ||
        !WritePrivateProfileStringW(
            section, L"host_state_next_launch", host_state, temporary)) {
        dluma_atomic_ini_abort(temporary);
        return 0;
    }
    return dluma_atomic_ini_commit(g_dluma_native_mods.ini_path, temporary);
}

/* Called under the registry lock after source selection on the first scan. */
static void dluma_native_auto_enable_startup(void)
{
    unsigned int i;
    if (!g_dluma.config.auto_enable_mods ||
        dluma_startup_recovery_suppresses_mods()) return;
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* item = &g_dluma_native_mods.entries[i].snapshot;
        WCHAR section[128];
        WCHAR saved[16];
        WCHAR legacy[16];
        unsigned int hosts;
        if (item->scope != DLUMA_SCOPE_GLOBAL || item->duplicate_id ||
            !item->source_selected || item->source_selection_missing ||
            item->runtime_error[0] || !dluma_native_host_abi_compatible(item) ||
            (item->companion_pack.declared && !item->companion_pack.valid))
            continue;
        dluma_native_section(item, section, DLUMA_ARRAY_COUNT(section));
        saved[0] = legacy[0] = 0;
        GetPrivateProfileStringW(section, L"host_state_next_launch", L"",
            saved, DLUMA_ARRAY_COUNT(saved), g_dluma_native_mods.ini_path);
        if (saved[0]) {
            char text[16];
            if (!dluma_wide_to_utf8(saved, text, DLUMA_ARRAY_COUNT(text)))
                continue;
            hosts = dluma_parse_u32(text, 0);
            if (!hosts || hosts > DLUMA_NATIVE_HOST_BOTH) continue;
            hosts &= item->declared_host_flags;
        } else {
            GetPrivateProfileStringW(section, L"enabled_next_launch", L"",
                legacy, DLUMA_ARRAY_COUNT(legacy), g_dluma_native_mods.ini_path);
            if (legacy[0]) {
                if (legacy[0] != L'1' || legacy[1] != L'\0') continue;
                hosts = DLUMA_NATIVE_HOST_PLAYER & item->declared_host_flags;
            } else {
                hosts = item->declared_host_flags;
            }
        }
        if (!hosts ||
            ((hosts & DLUMA_NATIVE_HOST_PLAYER) && !item->player_approval_fingerprint[0]) ||
            ((hosts & DLUMA_NATIVE_HOST_EDITOR) && !item->editor_approval_fingerprint[0]))
            continue;
        if (item->requested_host_flags == hosts &&
            (!(hosts & DLUMA_NATIVE_HOST_PLAYER) || item->player_approval_valid) &&
            (!(hosts & DLUMA_NATIVE_HOST_EDITOR) || item->editor_approval_valid))
            continue;
        if (!dluma_native_write_hosts(item, hosts)) {
            item->requested_host_flags = 0;
            item->enabled_requested = 0;
            item->approval_valid = 0;
            item->player_approval_valid = 0;
            item->editor_approval_valid = 0;
            dluma_log_pair("WARN", "native_mod_auto_enable_write_failed", item->id);
            continue;
        }
        item->requested_host_flags = (unsigned char)hosts;
        item->player_approval_valid = (hosts & DLUMA_NATIVE_HOST_PLAYER) != 0;
        item->editor_approval_valid = (hosts & DLUMA_NATIVE_HOST_EDITOR) != 0;
        item->approval_valid = item->player_approval_valid;
        item->enabled_requested = item->player_approval_valid;
        item->auto_approved = 1;
        item->state = DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH;
        dluma_log_pair("INFO", "native_mod_auto_enabled", item->id);
    }
}

static int dluma_native_write_enabled(
    const DlumaNativeModSnapshot* item,
    int enabled)
{
    unsigned int host_flags = item
        ? item->requested_host_flags : DLUMA_NATIVE_HOST_OFF;
    if (enabled) host_flags |= DLUMA_NATIVE_HOST_PLAYER;
    else host_flags &= ~DLUMA_NATIVE_HOST_PLAYER;
    return dluma_native_write_hosts(item, host_flags);
}

int dluma_native_mods_request_enabled(const DlumaNativeModMutationRequest* request,
                                    int enabled, int approve_current)
{
    unsigned int i;
    unsigned int requested = DLUMA_NATIVE_HOST_OFF;
    if (!request) return 0;
    dluma_native_lock();
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        const DlumaNativeModSnapshot* item =
            &g_dluma_native_mods.entries[i].snapshot;
        if (dluma_stricmp(item->registry_key, request->registry_key) == 0) {
            requested = item->requested_host_flags;
            break;
        }
    }
    dluma_native_unlock();
    if (enabled) requested |= DLUMA_NATIVE_HOST_PLAYER;
    else requested &= ~DLUMA_NATIVE_HOST_PLAYER;
    return dluma_native_mods_request_hosts(
        request, requested, approve_current, 0);
}

int dluma_native_mods_request_source(
    const char* declared_id, const char* source_identity,
    ULONGLONG registry_revision)
{
    unsigned int i;
    int found = 0;
    if (!declared_id || !declared_id[0] ||
        !source_identity || !source_identity[0])
        return 0;
    dluma_native_lock();
    if (registry_revision == g_dluma_native_mods.revision) {
        for (i = 0; i < g_dluma_native_mods.count; ++i) {
            DlumaNativeModSnapshot* item =
                &g_dluma_native_mods.entries[i].snapshot;
            /* Which copy provides the ID is independent of whether that copy
             * loaded. Enabling still requires approval and ABI compatibility. */
            if (dluma_stricmp(item->id, declared_id) == 0 &&
                dluma_stricmp(item->source_identity, source_identity) == 0 &&
                !item->duplicate_id) {
                found = 1;
                break;
            }
        }
    }
    dluma_native_unlock();
    if (!found || !dluma_mod_source_selection_write(
            "dll", declared_id, source_identity))
        return 0;
    dluma_native_lock();
    if (registry_revision != g_dluma_native_mods.revision) {
        dluma_native_unlock();
        return 0;
    }
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* item =
            &g_dluma_native_mods.entries[i].snapshot;
        if (dluma_stricmp(item->id, declared_id) != 0) continue;
        item->source_selected = (unsigned char)
            (dluma_stricmp(item->source_identity, source_identity) == 0
                ? 1 : 0);
        item->source_selection_missing = 0;
    }
    ++g_dluma_native_mods.revision;
    dluma_native_publish_crash_context_locked();
    dluma_native_unlock();
    return 1;
}

int dluma_native_mods_request_hosts(
    const DlumaNativeModMutationRequest* request,
    unsigned int host_flags,
    int approve_player,
    int approve_editor)
{
    unsigned int i;
    int persisted = 0;
    const char* rejection = "invalid_request";
    if (!request || !request->registry_key[0] ||
        !request->approval_fingerprint[0] ||
        host_flags > DLUMA_NATIVE_HOST_BOTH) {
        dluma_log_pair(
            "WARN", "native_mod_approval_rejected", rejection);
        return 0;
    }
    dluma_native_lock();
    if (request->registry_revision != g_dluma_native_mods.revision) {
        rejection = "stale_registry_revision";
        goto done;
    }
    rejection = "registry_key_not_found";
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* item = &g_dluma_native_mods.entries[i].snapshot;
        if (dluma_stricmp(item->registry_key, request->registry_key) != 0) continue;
        if (dluma_stricmp(item->approval_fingerprint,
                         request->approval_fingerprint) != 0) {
            rejection = "approval_fingerprint_changed";
            break;
        }
        if (item->duplicate_id) {
            rejection = "duplicate_mod_id";
            break;
        }
        if (!item->source_selected) {
            rejection = "mod_source_not_selected";
            break;
        }
        if (!dluma_native_host_abi_compatible(item)) {
            rejection = "incompatible_native_host_abi";
            break;
        }
        if (item->companion_pack.declared &&
            !item->companion_pack.valid) {
            rejection = "companion_pack_invalid";
            break;
        }
        if (host_flags & ~item->declared_host_flags) {
            rejection = "host_not_declared_by_manifest";
            break;
        }
        if ((host_flags & DLUMA_NATIVE_HOST_PLAYER) &&
            !item->player_approval_valid && !approve_player) {
            rejection = "exact_player_approval_required";
            break;
        }
        if ((host_flags & DLUMA_NATIVE_HOST_EDITOR) &&
            !item->editor_approval_valid && !approve_editor) {
            rejection = "separate_editor_full_trust_approval_required";
            break;
        }
        if (!dluma_native_write_hosts(item, host_flags)) {
            rejection = "native_mods_ini_write_failed";
            break;
        }
        item->player_approval_valid =
            (host_flags & DLUMA_NATIVE_HOST_PLAYER) ? 1U : 0U;
        item->editor_approval_valid =
            (host_flags & DLUMA_NATIVE_HOST_EDITOR) ? 1U : 0U;
        item->approval_valid = item->player_approval_valid;
        item->enabled_requested =
            (host_flags & DLUMA_NATIVE_HOST_PLAYER) ? 1U : 0U;
        item->requested_host_flags = (unsigned char)host_flags;
        item->state = host_flags ? DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH :
            (item->loaded ? DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH :
             DLUMA_NATIVE_STATE_DISCOVERED_DISABLED);
        persisted = 1;
        switch (host_flags) {
        case DLUMA_NATIVE_HOST_PLAYER:
            dluma_log_pair("INFO", "native_mod_approval_state",
                         "player_next_launch_restart_required");
            break;
        case DLUMA_NATIVE_HOST_EDITOR:
            dluma_log_pair("INFO", "native_mod_approval_state",
                         "editor_next_launch_restart_required");
            break;
        case DLUMA_NATIVE_HOST_BOTH:
            dluma_log_pair("INFO", "native_mod_approval_state",
                         "both_hosts_next_launch_restart_required");
            break;
        default:
            dluma_log_pair("INFO", "native_mod_approval_state",
                         "disabled_next_launch_restart_required");
            break;
        }
        break;
    }
done:
    dluma_native_unlock();
    if (!persisted)
        dluma_log_pair(
            "WARN", "native_mod_approval_rejected", rejection);
    return persisted;
}

int dluma_native_mods_disable_exact_next_launch(
    const char* registry_key, const char* approval_fingerprint)
{
    unsigned int i;
    int persisted = 0;
    if (!registry_key || !registry_key[0] || !approval_fingerprint ||
        !approval_fingerprint[0]) return 0;
    dluma_native_lock();
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* item = &g_dluma_native_mods.entries[i].snapshot;
        if (dluma_stricmp(item->registry_key, registry_key) != 0 ||
            dluma_stricmp(item->approval_fingerprint,
                         approval_fingerprint) != 0)
            continue;
        if (item->duplicate_id || !dluma_native_host_abi_compatible(item) ||
            !dluma_native_write_enabled(item, 0))
            break;
        item->enabled_requested = 0;
        item->player_approval_valid = 0;
        item->approval_valid = 0;
        item->requested_host_flags &= ~DLUMA_NATIVE_HOST_PLAYER;
        item->state = item->requested_host_flags
            ? DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH
            : DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH;
        persisted = 1;
        dluma_log_pair("WARN", "startup_recovery_native_disabled_next_launch",
                     item->id);
        break;
    }
    dluma_native_unlock();
    return persisted;
}

int dluma_native_mods_disable_all_next_launch(void)
{
    unsigned int i;
    unsigned int disabled = 0;
    int complete = 1;
    dluma_native_lock();
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModSnapshot* item = &g_dluma_native_mods.entries[i].snapshot;
        if (!item->enabled_requested) continue;
        if (item->duplicate_id || !dluma_native_host_abi_compatible(item) ||
            !dluma_native_write_enabled(item, 0)) {
            complete = 0;
            continue;
        }
        item->enabled_requested = 0;
        item->player_approval_valid = 0;
        item->approval_valid = 0;
        item->requested_host_flags &= ~DLUMA_NATIVE_HOST_PLAYER;
        item->state = item->requested_host_flags
            ? DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH
            : DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH;
        ++disabled;
    }
    dluma_native_unlock();
    dluma_log_u64("WARN", "startup_recovery_native_mods_disabled", disabled);
    return complete;
}

const char* dluma_native_mods_abi_text(unsigned char abi_kind)
{
    switch ((DlumaNativeModAbiKind)abi_kind) {
    case DLUMA_NATIVE_ABI_V2: return "DLUMA NATIVE ABI V2 / API 1.0";
    case DLUMA_NATIVE_ABI_EDITOR_V1: return "DLUMA EDITOR MOD ABI V1";
    case DLUMA_NATIVE_ABI_LEGACY_V1:
        return "LEGACY DLUMA NATIVE ABI V1 / UNSUPPORTED";
    case DLUMA_NATIVE_ABI_LEGACY_GAME_SCRIPT: return "LEGACY GAME-SCRIPT ABI";
    case DLUMA_NATIVE_ABI_INVALID: return "INVALID";
    default: return "UNKNOWN";
    }
}

const char* dluma_native_mods_state_text(const DlumaNativeModSnapshot* item)
{
    if (!item) return "UNAVAILABLE";
    switch ((DlumaNativeModRuntimeState)item->state) {
    case DLUMA_NATIVE_STATE_APPROVED_NEXT_LAUNCH: return "ENABLED NEXT LAUNCH / RESTART REQUIRED";
    case DLUMA_NATIVE_STATE_LOADED: return "LOADED";
    case DLUMA_NATIVE_STATE_DISABLE_NEXT_LAUNCH: return "DISABLE NEXT LAUNCH / RESTART REQUIRED";
    case DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED: return "CHANGED / DISABLED / REVIEW REQUIRED";
    case DLUMA_NATIVE_STATE_CONFLICT: return "CONFLICT";
    case DLUMA_NATIVE_STATE_INCOMPATIBLE: return "INCOMPATIBLE";
    case DLUMA_NATIVE_STATE_LOAD_ERROR: return "LOAD ERROR";
    case DLUMA_NATIVE_STATE_STARTUP_RECOVERY_SUPPRESSED:
        return "STARTUP RECOVERY SUPPRESSED (THIS SESSION)";
    default: return "NEW / DISABLED / REVIEW REQUIRED";
    }
}

static void dluma_native_set_error(DlumaNativeModSnapshot* item, const char* stage)
{
    item->state = DLUMA_NATIVE_STATE_LOAD_ERROR;
    dluma_str_copy(item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error), stage);
}

static void dluma_native_locked_artifact_close(
    DlumaNativeLockedArtifact* artifact)
{
    int companion_mounted;
    if (!artifact) return;
    companion_mounted = artifact->companion_pack &&
        artifact->companion_pack->mounted;
    if (artifact->companion_pack) {
        dluma_companion_archive_close(artifact->companion_pack);
        if (!companion_mounted)
            dluma_free(artifact->companion_pack);
        artifact->companion_pack = 0;
    }
    if (artifact->sidecar_file != INVALID_HANDLE_VALUE)
        CloseHandle(artifact->sidecar_file);
    if (artifact->module_file != INVALID_HANDLE_VALUE)
        CloseHandle(artifact->module_file);
    artifact->sidecar_file = INVALID_HANDLE_VALUE;
    artifact->module_file = INVALID_HANDLE_VALUE;
}

static int dluma_native_revalidate_before_load(
    const DlumaNativeModSnapshot* item, DlumaNativeLockedArtifact* artifact)
{
    WCHAR sidecar[DLUMA_MAX_WPATH];
    char current[DLUMA_SHA256_HEX_CAPACITY];
    char sidecar_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD sidecar_attributes;
    BY_HANDLE_FILE_INFORMATION after_hash;
    if (!item || !artifact) return 0;
    memset(artifact, 0, sizeof(*artifact));
    artifact->module_file = INVALID_HANDLE_VALUE;
    artifact->sidecar_file = INVALID_HANDLE_VALUE;
    if (!dluma_native_open_regular_final(
            item->final_path, &artifact->module_file,
            artifact->module_final_path,
            DLUMA_ARRAY_COUNT(artifact->module_final_path),
            &artifact->module_info))
        return 0;
    if (artifact->module_info.dwVolumeSerialNumber != item->volume_serial ||
        artifact->module_info.nFileIndexHigh != item->file_index_high ||
        artifact->module_info.nFileIndexLow != item->file_index_low ||
        dluma_wstricmp(artifact->module_final_path, item->final_path) != 0)
        goto done;
    if (!dluma_sha256_handle_hex(
            artifact->module_file, DLUMA_NATIVE_MOD_FILE_MAX_BYTES, current) ||
        dluma_stricmp(current, item->module_sha256) != 0)
        goto done;
    memset(&after_hash, 0, sizeof(after_hash));
    if (!GetFileInformationByHandle(artifact->module_file, &after_hash) ||
        after_hash.dwVolumeSerialNumber !=
            artifact->module_info.dwVolumeSerialNumber ||
        after_hash.nFileIndexHigh != artifact->module_info.nFileIndexHigh ||
        after_hash.nFileIndexLow != artifact->module_info.nFileIndexLow ||
        after_hash.nFileSizeHigh != artifact->module_info.nFileSizeHigh ||
        after_hash.nFileSizeLow != artifact->module_info.nFileSizeLow ||
        after_hash.ftLastWriteTime.dwHighDateTime !=
            artifact->module_info.ftLastWriteTime.dwHighDateTime ||
        after_hash.ftLastWriteTime.dwLowDateTime !=
            artifact->module_info.ftLastWriteTime.dwLowDateTime)
        goto done;
    if (!dluma_native_sidecar_path(
            item->final_path, sidecar, DLUMA_ARRAY_COUNT(sidecar)))
        goto done;
    sidecar_attributes = GetFileAttributesW(sidecar);
    if (!item->sidecar_present) {
        if (sidecar_attributes != INVALID_FILE_ATTRIBUTES) goto done;
    } else {
        if (sidecar_attributes == INVALID_FILE_ATTRIBUTES ||
            (sidecar_attributes & (FILE_ATTRIBUTE_DIRECTORY |
                                   FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !dluma_native_open_regular_final(
                sidecar, &artifact->sidecar_file,
                artifact->sidecar_final_path,
                DLUMA_ARRAY_COUNT(artifact->sidecar_final_path),
                &artifact->sidecar_info) ||
            dluma_wstricmp(artifact->sidecar_final_path, sidecar) != 0 ||
            !dluma_sha256_handle_hex(
                artifact->sidecar_file, DLUMA_NATIVE_MOD_SIDECAR_MAX_BYTES,
                sidecar_sha256) ||
            dluma_stricmp(sidecar_sha256, item->sidecar_sha256) != 0)
            goto done;
        memset(&after_hash, 0, sizeof(after_hash));
        if (!GetFileInformationByHandle(
                artifact->sidecar_file, &after_hash) ||
            after_hash.dwVolumeSerialNumber !=
                artifact->sidecar_info.dwVolumeSerialNumber ||
            after_hash.nFileIndexHigh !=
                artifact->sidecar_info.nFileIndexHigh ||
            after_hash.nFileIndexLow !=
                artifact->sidecar_info.nFileIndexLow ||
            after_hash.nFileSizeHigh !=
                artifact->sidecar_info.nFileSizeHigh ||
            after_hash.nFileSizeLow !=
                artifact->sidecar_info.nFileSizeLow ||
            after_hash.ftLastWriteTime.dwHighDateTime !=
                artifact->sidecar_info.ftLastWriteTime.dwHighDateTime ||
            after_hash.ftLastWriteTime.dwLowDateTime !=
                artifact->sidecar_info.ftLastWriteTime.dwLowDateTime)
            goto done;
    }
    if (item->companion_pack.declared) {
        artifact->companion_pack = (DlumaCompanionArchive*)dluma_alloc(
            sizeof(*artifact->companion_pack));
        if (!artifact->companion_pack) goto done;
        memset(
            artifact->companion_pack, 0,
            sizeof(*artifact->companion_pack));
        artifact->companion_pack->file = INVALID_HANDLE_VALUE;
        if (!dluma_companion_archive_lock(
                &item->companion_pack, artifact->companion_pack))
            goto done;
    }
    return 1;

done:
    dluma_native_locked_artifact_close(artifact);
    return 0;
}

static int dluma_native_loaded_module_matches(
    HMODULE module, const DlumaNativeLockedArtifact* artifact)
{
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE file = INVALID_HANDLE_VALUE;
    int result = 0;
    if (!module || !artifact ||
        !dluma_get_module_path_checked(
            module, module_path, DLUMA_ARRAY_COUNT(module_path)) ||
        !dluma_native_open_regular_final(
            module_path, &file, final_path, DLUMA_ARRAY_COUNT(final_path),
            &info))
        return 0;
    if (dluma_wstricmp(final_path, artifact->module_final_path) == 0 &&
        info.dwVolumeSerialNumber ==
            artifact->module_info.dwVolumeSerialNumber &&
        info.nFileIndexHigh == artifact->module_info.nFileIndexHigh &&
        info.nFileIndexLow == artifact->module_info.nFileIndexLow)
        result = 1;
    CloseHandle(file);
    return result;
}

static DlumaStringView dluma_native_quest_phase_view(
    const char* value,
    uint64_t exact_size)
{
    DlumaStringView view;
    view.data = value;
    view.size = exact_size;
    return view;
}

static int dluma_native_quest_phase_event_sink(
    void* user,
    uint32_t event_kind,
    uint64_t callback_id,
    uint64_t registration_token,
    const DlumaQuestPhaseEvent* event_value)
{
    DlumaNativeModEntry* entry = (DlumaNativeModEntry*)user;
    DlumaModPackageSnapshot current;
    DlumaNativeModApiV2 callbacks;
    DlumaQuestPhaseNativeEventV1 native_event;
    const DlumaApiV1* api;
    unsigned int required_access;
    DlumaStatus callback_status = DLUMA_STATUS_INTERNAL_ERROR;
    if (!entry || !event_value ||
        (event_kind != DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
         event_kind != DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER))
        return 0;
    required_access = event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
        ? DLUMA_ACCESS_QUEST_PHASE_HANDLE
        : DLUMA_ACCESS_QUEST_PHASE_OBSERVE;
    memset(&current, 0, sizeof(current));
    current.struct_size = (unsigned int)sizeof(current);
    if (!entry->snapshot.player_approval_valid ||
        !entry->snapshot.loaded ||
        !entry->snapshot.pre_init_succeeded ||
        !entry->snapshot.post_init_succeeded ||
        !entry->scope_active || g_dluma_native_map_exit_frozen ||
        (g_dluma_native_project_exit_gate &&
         entry->package.manifest.scope != DLUMA_SCOPE_GLOBAL) ||
        entry->lifecycle_phase != DLUMA_NATIVE_PHASE_READY ||
        dluma_mod_package_revalidate(&entry->package, &current) !=
            DLUMA_STATUS_OK ||
        !dluma_mod_manifest_has_access(&current.manifest, required_access) ||
        (event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
         !dluma_mod_manifest_declares_quest_phase_handler(
             &current.manifest, event_value->handler_id)))
        return 0;
    callbacks = entry->callbacks;
    api = dluma_api_runtime_api(&entry->api_runtime);
    if (!api || !callbacks.quest_phase_event) return 0;
    memset(&native_event, 0, sizeof(native_event));
    native_event.struct_size = (uint32_t)sizeof(native_event);
    native_event.struct_version = 1U;
    native_event.kind = event_kind;
    native_event.map_generation = event_value->map_generation;
    native_event.callback_id = callback_id;
    native_event.token = registration_token;
    native_event.sequence = event_value->sequence;
    native_event.runtime_kind = event_value->runtime_kind;
    native_event.completion = event_value->completion;
    native_event.local_peer = 1U;
    native_event.wait_seconds = event_value->wait_seconds;
    native_event.quest_id = dluma_native_quest_phase_view(
        event_value->quest_id, strlen(event_value->quest_id));
    native_event.phase_id = dluma_native_quest_phase_view(
        event_value->phase_id, strlen(event_value->phase_id));
    native_event.contract_id = dluma_native_quest_phase_view(
        event_value->contract_id, strlen(event_value->contract_id));
    native_event.provider = dluma_native_quest_phase_view(
        event_value->provider_mod, strlen(event_value->provider_mod));
    native_event.handler = dluma_native_quest_phase_view(
        event_value->handler_id, strlen(event_value->handler_id));
    native_event.binding_hash = dluma_native_quest_phase_view(
        event_value->binding_hash, strlen(event_value->binding_hash));
    native_event.arguments_json = dluma_native_quest_phase_view(
        event_value->arguments_json, event_value->arguments_size);
    dluma_atomic_increment(&entry->quest_phase_callbacks_in_flight);
    if (g_dluma_native_map_exit_frozen || (g_dluma_native_project_exit_gate &&
        entry->package.manifest.scope != DLUMA_SCOPE_GLOBAL)) {
        dluma_atomic_decrement(&entry->quest_phase_callbacks_in_flight);
        return 0;
    }
    __try {
        callback_status = callbacks.quest_phase_event(api, &native_event);
    }
    __except(1) {
        callback_status = DLUMA_STATUS_INTERNAL_ERROR;
    }
    dluma_atomic_decrement(&entry->quest_phase_callbacks_in_flight);
    return callback_status == DLUMA_STATUS_OK;
}

static int dluma_native_api_initialize_entry(
    DlumaNativeModEntry* entry,
    DWORD lifecycle_epoch,
    const DlumaModConfigIdentity* config_identity,
    const DlumaNativeModApiV2* callbacks)
{
    DlumaModOwnerDefinition definition;
    DlumaGameHostContextDefinition host_definition;
    const DlumaModManifest* manifest;
    if (!entry || !callbacks || !entry->package.valid ||
        entry->package.manifest.language !=
            DLUMA_MOD_LANGUAGE_NATIVE)
        return 0;
    manifest = &entry->package.manifest;
    memset(&definition, 0, sizeof(definition));
    definition.struct_size = (uint32_t)sizeof(definition);
    definition.struct_version = 1U;
    definition.owner_id =
        ((uint64_t)lifecycle_epoch << 32U) |
        ((uint64_t)entry->registry_index + 1U);
    definition.language = DLUMA_MOD_LANGUAGE_NATIVE;
    definition.scope = manifest->scope;
    definition.map_generation = 1U;
    definition.game_thread_id = GetCurrentThreadId();
    definition.stable_id.data = entry->snapshot.id;
    definition.stable_id.size =
        (uint64_t)strlen(entry->snapshot.id);
    definition.access_ids = manifest->access_ids;
    definition.access_count = manifest->access_count;
    definition.max_handles = 256U;
    definition.max_subscriptions = 128U;
    definition.max_timers = 128U;
    definition.max_companion_artifacts = 64U;
    if (dluma_mod_owner_initialize(
            &entry->api_owner, &definition) != DLUMA_STATUS_OK)
        return 0;
    memset(&host_definition, 0, sizeof(host_definition));
    host_definition.struct_size =
        (uint32_t)sizeof(host_definition);
    host_definition.struct_version = 1U;
    host_definition.owner = &entry->api_owner;
    host_definition.package = &entry->package;
    host_definition.config_identity =
        config_identity;
    host_definition.storage_key =
        entry->snapshot.registry_key;
    host_definition.event_sink =
        dluma_native_api_event_sink;
    host_definition.event_user = entry;
    entry->api_host.struct_size =
        (uint32_t)sizeof(entry->api_host);
    if (dluma_game_host_context_initialize(
            &entry->api_host,
            &host_definition) != DLUMA_STATUS_OK) {
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        dluma_mod_owner_finish_revoke(
            &entry->api_owner);
        return 0;
    }
    dluma_game_backend_prepare_capabilities(&entry->api_capabilities);
    dluma_game_host_context_prepare_capabilities(
        &entry->api_host,
        &entry->api_capabilities);
    dluma_game_backend_initialize_for_host(
        &entry->api_backend,
        &entry->api_host);
    if (dluma_api_runtime_initialize(
            &entry->api_runtime,
            &entry->api_owner,
            &entry->api_capabilities,
            &entry->api_backend) != DLUMA_STATUS_OK) {
        dluma_game_host_context_revoke(
            &entry->api_host);
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        dluma_mod_owner_finish_revoke(
            &entry->api_owner);
        return 0;
    }
    entry->callbacks = *callbacks;
    if (callbacks->quest_phase_event &&
        !dluma_quest_phase_service_attach(
            &entry->api_owner, &entry->package,
            entry->snapshot.load_priority,
            dluma_native_quest_phase_event_sink, entry)) {
        dluma_game_host_context_revoke(&entry->api_host);
        dluma_mod_owner_begin_revoke(&entry->api_owner);
        dluma_mod_owner_finish_revoke(&entry->api_owner);
        return 0;
    }
    return 1;
}

static DlumaEditorStatusV1 __cdecl dluma_native_editor_attach_validator(
    void* owner_context,
    const char* handler_id,
    uint32_t handler_id_size,
    DlumaEditorQuestValidatorV1 validator,
    void* validator_user)
{
    DlumaNativeModEntry* requested = (DlumaNativeModEntry*)owner_context;
    DlumaEditorStatusV1 status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR;
    unsigned int entry_index;
    unsigned int handler_index;
    unsigned int slot;
    char handler[128];
    if (!requested || !handler_id || !handler_id_size ||
        handler_id_size >= sizeof(handler) || !validator)
        return DLUMA_EDITOR_STATUS_INVALID_ARGUMENT;
    for (slot = 0; slot < handler_id_size; ++slot) {
        char c = handler_id[slot];
        if (!c || !((c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-'))
            return DLUMA_EDITOR_STATUS_INVALID_ARGUMENT;
        handler[slot] = c;
    }
    handler[handler_id_size] = 0;
    dluma_native_lock();
    for (entry_index = 0; entry_index < g_dluma_native_mods.count;
         ++entry_index)
        if (&g_dluma_native_mods.entries[entry_index] == requested)
            break;
    if (entry_index >= g_dluma_native_mods.count ||
        !(requested->snapshot.declared_host_flags & DLUMA_NATIVE_HOST_EDITOR)) {
        status = DLUMA_EDITOR_STATUS_PERMISSION_DENIED;
        goto done;
    }
    if (!dluma_mod_manifest_has_access(
            &requested->package.manifest,
            DLUMA_ACCESS_EDITOR_QUEST_EXTEND)) {
        status = DLUMA_EDITOR_STATUS_PERMISSION_DENIED;
        goto done;
    }
    for (handler_index = 0;
         handler_index < requested->snapshot.quest_phase_handler_count;
         ++handler_index)
        if (dluma_stricmp(
                requested->snapshot.quest_phase_handlers[handler_index].id,
                handler) == 0)
            break;
    if (handler_index >= requested->snapshot.quest_phase_handler_count) {
        status = DLUMA_EDITOR_STATUS_NOT_DECLARED;
        goto done;
    }
    for (slot = 0; slot < DLUMA_ARRAY_COUNT(requested->editor_validators);
         ++slot) {
        DlumaEditorValidatorRegistration* registration =
            &requested->editor_validators[slot];
        if (registration->active &&
            dluma_stricmp(registration->handler_id, handler) == 0) {
            status = DLUMA_EDITOR_STATUS_ALREADY_REGISTERED;
            goto done;
        }
    }
    for (slot = 0; slot < DLUMA_ARRAY_COUNT(requested->editor_validators);
         ++slot) {
        DlumaEditorValidatorRegistration* registration =
            &requested->editor_validators[slot];
        if (registration->active) continue;
        memset(registration, 0, sizeof(*registration));
        dluma_str_copy(
            registration->handler_id,
            DLUMA_ARRAY_COUNT(registration->handler_id), handler);
        registration->callback = validator;
        registration->user = validator_user;
        registration->active = 1U;
        status = DLUMA_EDITOR_STATUS_OK;
        goto done;
    }
    status = DLUMA_EDITOR_STATUS_LIMIT_EXCEEDED;
done:
    dluma_native_unlock();
    return status;
}

static int dluma_native_editor_call_load(
    DlumaNativeModEntry* entry,
    const DlumaEditorLoadEventV1* event_value)
{
    DlumaEditorStatusV1 status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR;
    int tracked;
    if (!entry || !entry->editor_callbacks.editor_load || !event_value)
        return 0;
    tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&entry->editor_callbacks_in_flight);
    __try { status = entry->editor_callbacks.editor_load(event_value); }
    __except(1) { status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR; }
    dluma_atomic_decrement(&entry->editor_callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
    return status == DLUMA_EDITOR_STATUS_OK;
}

static int dluma_native_editor_call_context(
    DlumaNativeModEntry* entry,
    const DlumaEditorContextEventV1* event_value)
{
    DlumaEditorStatusV1 status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR;
    int tracked;
    if (!entry || !entry->editor_callbacks.context_changed || !event_value)
        return 0;
    tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&entry->editor_callbacks_in_flight);
    __try { status = entry->editor_callbacks.context_changed(event_value); }
    __except(1) { status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR; }
    dluma_atomic_decrement(&entry->editor_callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
    return status == DLUMA_EDITOR_STATUS_OK;
}

static int dluma_native_editor_call_session(
    DlumaNativeModEntry* entry,
    DlumaEditorSessionCallbackV1 callback,
    const DlumaEditorSessionEventV1* event_value)
{
    DlumaEditorStatusV1 status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR;
    int tracked;
    if (!entry || !callback || !event_value) return 0;
    tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&entry->editor_callbacks_in_flight);
    __try { status = callback(event_value); }
    __except(1) { status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR; }
    dluma_atomic_decrement(&entry->editor_callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
    return status == DLUMA_EDITOR_STATUS_OK;
}

int dluma_native_mods_editor_load(void)
{
    const DlumaDualHostRuntimeSnapshot* host =
        dluma_dual_host_runtime_snapshot();
    unsigned int i;
    int all_loaded = 1;
    if (!host || host->classification.kind != DLUMA_HOST_CHROME_EDITOR)
        return 0;
    for (i = 0; ; ++i) {
        DlumaNativeModSnapshot claimed;
        DlumaModPackageSnapshot approved_package;
        DlumaModPackageSnapshot current_package;
        DlumaNativeLockedArtifact artifact;
        DlumaEditorLoadEventV1 load_event;
        DlumaEditorModApiV1 callbacks;
        DlumaEditorModGetApiV1Fn get_editor_api = 0;
        HMODULE module = 0;
        DWORD epoch;
        int changed_before_load = 0;
        int loaded_ok = 0;
        int committed = 0;
        const char* error = 0;
        memset(&claimed, 0, sizeof(claimed));
        memset(&approved_package, 0, sizeof(approved_package));
        memset(&current_package, 0, sizeof(current_package));
        memset(&artifact, 0, sizeof(artifact));
        memset(&load_event, 0, sizeof(load_event));
        memset(&callbacks, 0, sizeof(callbacks));
        artifact.module_file = INVALID_HANDLE_VALUE;
        artifact.sidecar_file = INVALID_HANDLE_VALUE;
        dluma_native_lock();
        if (i >= g_dluma_native_mods.count) {
            dluma_native_unlock();
            break;
        }
        if (!g_dluma_native_mods.entries[i].snapshot.source_selected ||
            !(g_dluma_native_mods.entries[i].snapshot.requested_host_flags &
                  DLUMA_NATIVE_HOST_EDITOR) ||
            !g_dluma_native_mods.entries[i].snapshot.editor_approval_valid ||
            !g_dluma_native_mods.entries[i].snapshot.editor_abi_available ||
            g_dluma_native_mods.entries[i].snapshot.duplicate_id ||
            g_dluma_native_mods.entries[i].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_IDLE) {
            dluma_native_unlock();
            continue;
        }
        g_dluma_native_mods.entries[i].lifecycle_phase =
            DLUMA_NATIVE_PHASE_LOADING;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[i].lifecycle_epoch = epoch;
        memcpy(&claimed, &g_dluma_native_mods.entries[i].snapshot,
               sizeof(claimed));
        memcpy(&approved_package, &g_dluma_native_mods.entries[i].package,
               sizeof(approved_package));
        memset(&g_dluma_native_mods.entries[i].editor_host_api, 0,
               sizeof(g_dluma_native_mods.entries[i].editor_host_api));
        g_dluma_native_mods.entries[i].editor_host_api.struct_size =
            sizeof(DlumaEditorHostApiV1);
        g_dluma_native_mods.entries[i].editor_host_api.abi_version =
            DLUMA_EDITOR_MOD_ABI_V1;
        g_dluma_native_mods.entries[i].editor_host_api.quest.struct_size =
            sizeof(DlumaEditorQuestServiceV1);
        g_dluma_native_mods.entries[i].editor_host_api.quest.struct_version = 1U;
        g_dluma_native_mods.entries[i].editor_host_api.quest.owner_context =
            &g_dluma_native_mods.entries[i];
        g_dluma_native_mods.entries[i].editor_host_api.quest.attach_validator =
            dluma_native_editor_attach_validator;
        dluma_native_unlock();

        current_package.struct_size = sizeof(current_package);
        if (!claimed.manifest_present ||
            dluma_mod_package_revalidate(
                &approved_package, &current_package) != DLUMA_STATUS_OK) {
            changed_before_load = 1;
            error = "editor package identity changed before approved load";
            goto editor_commit;
        }
        if (!dluma_native_revalidate_before_load(&claimed, &artifact)) {
            changed_before_load = 1;
            error = "editor module or file identity changed before approved load";
            goto editor_commit;
        }
        module = LoadLibraryExW(
            artifact.module_final_path, 0,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module || !dluma_native_loaded_module_matches(module, &artifact)) {
            error = "editor module load path or identity validation failed";
            goto editor_commit;
        }
        get_editor_api = (DlumaEditorModGetApiV1Fn)GetProcAddress(
            module, DLUMA_EDITOR_MOD_GET_API_V1_EXPORT);
        if (!get_editor_api) {
            error = "approved DLUMAEditorMod_GetApiV1 export changed";
            goto editor_commit;
        }
        callbacks.struct_size = sizeof(callbacks);
        callbacks.abi_version = DLUMA_EDITOR_MOD_ABI_V1;
        __try {
            loaded_ok = get_editor_api(
                &g_dluma_native_mods.entries[i].editor_host_api,
                &callbacks) == DLUMA_EDITOR_STATUS_OK;
        }
        __except(1) { loaded_ok = 0; }
        if (!loaded_ok || callbacks.struct_size != sizeof(callbacks) ||
            callbacks.abi_version != DLUMA_EDITOR_MOD_ABI_V1 ||
            !callbacks.editor_load || !callbacks.context_changed ||
            !callbacks.session_begin || !callbacks.session_end) {
            error = "DLUMA Editor ABI V1 table validation failed";
            goto editor_commit;
        }
        load_event.struct_size = sizeof(load_event);
        load_event.struct_version = 1U;
        load_event.process_start_time = host->process_start_time;
        dluma_str_copy(load_event.host_release,
                     DLUMA_ARRAY_COUNT(load_event.host_release), DLUMA_VERSION);
        dluma_str_copy(load_event.editor_sha256,
                     DLUMA_ARRAY_COUNT(load_event.editor_sha256),
                     host->evidence.image.sha256);
        g_dluma_native_mods.entries[i].editor_callbacks = callbacks;
        if (!dluma_native_editor_call_load(
                &g_dluma_native_mods.entries[i], &load_event)) {
            error = "editor load callback failed or raised SEH";
            memset(g_dluma_native_mods.entries[i].editor_validators, 0,
                   sizeof(g_dluma_native_mods.entries[i].editor_validators));
            goto editor_commit;
        }
        loaded_ok = 1;

editor_commit:
        dluma_native_lock();
        if (i < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[i].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_LOADING &&
            g_dluma_native_mods.entries[i].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[i];
            entry->module = module;
            entry->get_editor_api_v1 = get_editor_api;
            if (changed_before_load) {
                entry->snapshot.editor_approval_valid = 0;
                entry->snapshot.requested_host_flags &=
                    ~DLUMA_NATIVE_HOST_EDITOR;
                entry->snapshot.state =
                    DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED;
            } else if (error) {
                dluma_native_set_error(&entry->snapshot, error);
            } else {
                entry->editor_loaded = 1U;
                entry->snapshot.loaded = 1U;
                entry->snapshot.state = DLUMA_NATIVE_STATE_LOADED;
                entry->snapshot.runtime_error[0] = 0;
            }
            if (error)
                dluma_str_copy(
                    entry->snapshot.runtime_error,
                    DLUMA_ARRAY_COUNT(entry->snapshot.runtime_error), error);
            entry->lifecycle_phase = error
                ? DLUMA_NATIVE_PHASE_RETAINED : DLUMA_NATIVE_PHASE_READY;
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
            committed = 1;
        }
        dluma_native_unlock();
        dluma_native_locked_artifact_close(&artifact);
        if (!committed || error) {
            all_loaded = 0;
            dluma_log_pair("ERROR", "editor_mod_load_error",
                         error ? error : "stale lifecycle epoch");
        } else {
            dluma_log_pair("INFO", "editor_mod_loaded", claimed.id);
        }
    }
    return all_loaded;
}

int dluma_native_mods_editor_context_changed(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    ULONGLONG context_generation)
{
    DlumaEditorContextEventV1 event_value;
    unsigned int index;
    int result = 1;
    memset(&event_value, 0, sizeof(event_value));
    event_value.struct_size = sizeof(event_value);
    event_value.struct_version = 1U;
    event_value.context_generation = context_generation;
    if ((project_root && project_root[0] &&
         !dluma_wide_to_utf8(
             project_root, event_value.project_root_utf8,
             DLUMA_ARRAY_COUNT(event_value.project_root_utf8))) ||
        (project_name && strlen(project_name) >=
             DLUMA_ARRAY_COUNT(event_value.project_name)) ||
        (map_name && strlen(map_name) >=
             DLUMA_ARRAY_COUNT(event_value.map_name)))
        return 0;
    dluma_str_copy(
        event_value.project_name,
        DLUMA_ARRAY_COUNT(event_value.project_name),
        project_name ? project_name : "");
    dluma_str_copy(
        event_value.map_name,
        DLUMA_ARRAY_COUNT(event_value.map_name),
        map_name ? map_name : "");
    for (index = 0; index < g_dluma_native_mods.count; ++index) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[index];
        if (entry->editor_loaded &&
            !dluma_native_editor_call_context(entry, &event_value)) {
            result = 0;
            dluma_log_pair("WARN", "editor_mod_context_callback_failed",
                         entry->snapshot.id);
        }
    }
    return result;
}

static int dluma_native_mods_editor_session(
    ULONGLONG session_generation,
    int begin)
{
    DlumaEditorSessionEventV1 event_value;
    unsigned int index;
    int result = 1;
    memset(&event_value, 0, sizeof(event_value));
    event_value.struct_size = sizeof(event_value);
    event_value.struct_version = 1U;
    event_value.session_generation = session_generation;
    for (index = 0; index < g_dluma_native_mods.count; ++index) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[index];
        DlumaEditorSessionCallbackV1 callback = begin
            ? entry->editor_callbacks.session_begin
            : entry->editor_callbacks.session_end;
        if (entry->editor_loaded &&
            !dluma_native_editor_call_session(entry, callback, &event_value)) {
            result = 0;
            dluma_log_pair(
                "WARN",
                begin ? "editor_mod_session_begin_failed"
                      : "editor_mod_session_end_failed",
                entry->snapshot.id);
        }
    }
    return result;
}

int dluma_native_mods_editor_session_begin(ULONGLONG session_generation)
{
    return dluma_native_mods_editor_session(session_generation, 1);
}

int dluma_native_mods_editor_session_end(ULONGLONG session_generation)
{
    return dluma_native_mods_editor_session(session_generation, 0);
}

int dluma_native_mods_editor_callbacks_drained(void)
{
    unsigned int index;
    for (index = 0; index < g_dluma_native_mods.count; ++index)
        if (dluma_atomic_compare_exchange(
                &g_dluma_native_mods.entries[index].editor_callbacks_in_flight,
                0, 0) != 0)
            return 0;
    return 1;
}

int dluma_native_mods_editor_validate_phase(
    const char* provider_mod,
    const char* handler_id,
    const char* phase_id,
    unsigned int source_line,
    const char* arguments_json,
    unsigned int arguments_size,
    char* diagnostic,
    unsigned int diagnostic_capacity)
{
    DlumaEditorQuestValidatorV1 callback = 0;
    void* callback_user = 0;
    DlumaNativeModEntry* callback_entry = 0;
    DlumaEditorQuestValidationRequestV1 request;
    DlumaEditorQuestValidationResultV1 result;
    unsigned int entry_index;
    unsigned int slot;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = 0;
    if (!provider_mod || !provider_mod[0] || !handler_id ||
        !handler_id[0] || !phase_id || !phase_id[0] || !source_line ||
        (!arguments_json && arguments_size) ||
        arguments_size > DLUMA_EDITOR_ARGUMENT_CAPACITY)
        return 0;
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.struct_size = sizeof(request);
    request.struct_version = 1U;
    request.source_line = source_line;
    request.arguments_size = arguments_size;
    if (strlen(handler_id) >= DLUMA_ARRAY_COUNT(request.handler_id) ||
        strlen(phase_id) >= DLUMA_ARRAY_COUNT(request.phase_id))
        return 0;
    dluma_str_copy(request.handler_id, DLUMA_ARRAY_COUNT(request.handler_id),
                 handler_id);
    dluma_str_copy(request.phase_id, DLUMA_ARRAY_COUNT(request.phase_id), phase_id);
    if (arguments_size)
        memcpy(request.arguments_json, arguments_json, arguments_size);
    request.arguments_json[arguments_size] = 0;
    result.struct_size = sizeof(result);
    result.struct_version = 1U;
    dluma_native_lock();
    for (entry_index = 0; entry_index < g_dluma_native_mods.count;
         ++entry_index) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[entry_index];
        if (!entry->editor_loaded ||
            dluma_stricmp(entry->snapshot.id, provider_mod) != 0)
            continue;
        callback_entry = entry;
        for (slot = 0; slot < DLUMA_ARRAY_COUNT(entry->editor_validators);
             ++slot) {
            DlumaEditorValidatorRegistration* registration =
                &entry->editor_validators[slot];
            if (registration->active &&
                dluma_stricmp(registration->handler_id, handler_id) == 0) {
                callback = registration->callback;
                callback_user = registration->user;
                dluma_atomic_increment(&entry->editor_callbacks_in_flight);
                break;
            }
        }
        break;
    }
    dluma_native_unlock();
    if (!callback_entry) return 1;
    if (!callback) return 1;
    {
        DlumaEditorStatusV1 status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR;
        int tracked = dluma_editor_host_callback_enter();
        __try { status = callback(callback_user, &request, &result); }
        __except(1) { status = DLUMA_EDITOR_STATUS_INTERNAL_ERROR; }
        dluma_atomic_decrement(&callback_entry->editor_callbacks_in_flight);
        dluma_editor_host_callback_leave(tracked);
        result.diagnostic[DLUMA_ARRAY_COUNT(result.diagnostic) - 1U] = 0;
        if (status != DLUMA_EDITOR_STATUS_OK ||
            result.struct_size < sizeof(result) ||
            result.struct_version != 1U || !result.accepted) {
            if (diagnostic && diagnostic_capacity)
                dluma_str_copy(
                    diagnostic, diagnostic_capacity,
                    result.diagnostic[0]
                        ? result.diagnostic
                        : "editor quest validator rejected the binding");
            return 0;
        }
    }
    return 1;
}

void dluma_native_mods_pre_game_init(HMODULE original_gamedll)
{
    unsigned int i;
    (void)original_gamedll;
    for (i = 0; ; ++i) {
        DlumaNativeModSnapshot claimed;
        DlumaModPackageSnapshot approved_package;
        DlumaModPackageSnapshot current_package;
        DlumaNativeLockedArtifact artifact;
        HMODULE module = 0;
        DlumaNativeGetApiV2Fn get_api_v2 = 0;
        const DlumaNativeModApiV2* callback_source = 0;
        DlumaNativeModApiV2 callbacks;
        const DlumaApiV1* api = 0;
        DlumaModConfigIdentity config_identity;
        int callback_ok = 0;
        int changed_before_load = 0;
        const char* error = 0;
        DWORD epoch;
        int committed = 0;
        int recovery_suppressed =
            dluma_startup_recovery_suppresses_mods();
        memset(&artifact, 0, sizeof(artifact));
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&approved_package, 0, sizeof(approved_package));
        memset(&current_package, 0, sizeof(current_package));
        artifact.module_file = INVALID_HANDLE_VALUE;
        artifact.sidecar_file = INVALID_HANDLE_VALUE;
        dluma_native_lock();
        if (i >= g_dluma_native_mods.count) {
            dluma_native_unlock();
            break;
        }
        if (!g_dluma_native_mods.entries[i].snapshot.source_selected ||
            !g_dluma_native_mods.entries[i].snapshot.enabled_requested ||
            !g_dluma_native_mods.entries[i].snapshot.approval_valid ||
            g_dluma_native_mods.entries[i].snapshot.duplicate_id ||
            g_dluma_native_mods.entries[i].snapshot.abi_kind !=
                DLUMA_NATIVE_ABI_V2 ||
            g_dluma_native_mods.entries[i].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_IDLE) {
            dluma_native_unlock();
            continue;
        }
        if (recovery_suppressed) {
            DlumaNativeModSnapshot* item =
                &g_dluma_native_mods.entries[i].snapshot;
            item->state = DLUMA_NATIVE_STATE_STARTUP_RECOVERY_SUPPRESSED;
            dluma_str_copy(
                item->runtime_error, DLUMA_ARRAY_COUNT(item->runtime_error),
                "suppressed for this startup-recovery session");
            dluma_str_copy(
                claimed.id, DLUMA_ARRAY_COUNT(claimed.id), item->id);
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
            dluma_native_unlock();
            dluma_log_pair("WARN", "native_mod_suppressed_startup_recovery",
                         claimed.id);
            continue;
        }
        g_dluma_native_mods.entries[i].lifecycle_phase =
            DLUMA_NATIVE_PHASE_LOADING;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[i].lifecycle_epoch = epoch;
        memcpy(&claimed, &g_dluma_native_mods.entries[i].snapshot,
               sizeof(claimed));
        memcpy(
            &approved_package,
            &g_dluma_native_mods.entries[i].package,
            sizeof(approved_package));
        dluma_native_config_identity(&claimed, &config_identity);
        dluma_native_unlock();

        dluma_log_pair("INFO", "native_mod_load_begin", claimed.id);
        dluma_startup_recovery_note_native_mod(
            claimed.id, claimed.registry_key, claimed.approval_fingerprint,
            "native_load_library");
        current_package.struct_size =
            (unsigned int)sizeof(current_package);
        if (!claimed.manifest_present ||
            dluma_mod_package_revalidate(
                &approved_package, &current_package) !=
                DLUMA_STATUS_OK) {
            changed_before_load = 1;
            error =
                "mod.dluma.json, mod.dluma.lock.json, package tree, or "
                "artifact identity changed before approved load";
            goto commit;
        }
        if (!dluma_native_revalidate_before_load(&claimed, &artifact)) {
            changed_before_load = 1;
            error =
                "module, sidecar, companion pack, path, or file identity "
                "changed before approved load";
            goto commit;
        }
        module = LoadLibraryExW(
            artifact.module_final_path, 0,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            error = "LoadLibraryExW failed";
            goto commit;
        }
        if (!dluma_native_loaded_module_matches(module, &artifact)) {
            error = "loaded module path or file identity did not match locked artifact";
            goto commit;
        }
        get_api_v2 = (DlumaNativeGetApiV2Fn)GetProcAddress(
            module, DLUMA_NATIVE_MOD_GET_API_V2_EXPORT);
        if (!get_api_v2) {
            error = "approved native ABI V2 export changed";
            goto commit;
        }
        dluma_startup_recovery_note_native_mod(
            claimed.id, claimed.registry_key, claimed.approval_fingerprint,
            "native_get_api_v2");
        __try { callback_source = get_api_v2(); }
        __except(1) { callback_source = 0; }
        if (!callback_source ||
            !dluma_is_readable(callback_source, sizeof(uint32_t) * 2U) ||
            callback_source->struct_size <
                offsetof(DlumaNativeModApiV2, quest_phase_event) ||
            callback_source->struct_size > 4096U ||
            callback_source->abi_version != DLUMA_NATIVE_MOD_ABI_V2 ||
            !dluma_is_readable(
                callback_source,
                callback_source->struct_size < sizeof(callbacks)
                    ? callback_source->struct_size : sizeof(callbacks))) {
            error = "DLUMA native ABI V2 table validation failed";
            goto commit;
        }
        memcpy(
            &callbacks, callback_source,
            callback_source->struct_size < sizeof(callbacks)
                ? callback_source->struct_size : sizeof(callbacks));
        if (!callbacks.pre_game_bootstrap ||
            !callbacks.post_game_ready ||
            !callbacks.scope_activated ||
            !callbacks.scope_deactivated ||
            !callbacks.shutdown ||
            !callbacks.event) {
            error = "DLUMA native ABI V2 mandatory callback is null";
            goto commit;
        }
        dluma_native_lock();
        if (i < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[i].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_LOADING &&
            g_dluma_native_mods.entries[i].lifecycle_epoch == epoch) {
            g_dluma_native_mods.entries[i].registry_index = i;
        }
        dluma_native_unlock();
        if (!dluma_native_api_initialize_entry(
                &g_dluma_native_mods.entries[i],
                epoch,
                &config_identity,
                &callbacks)) {
            error = "language-neutral API owner initialization failed";
            goto commit;
        }
        api = dluma_api_runtime_api(
            &g_dluma_native_mods.entries[i].api_runtime);
        if (!api) {
            error = "language-neutral API directory is unavailable";
            goto commit;
        }
        dluma_log_pair("INFO", "native_mod_pre_init_begin", claimed.id);
        dluma_startup_recovery_note_native_mod(
            claimed.id, claimed.registry_key, claimed.approval_fingerprint,
            "native_pre_game_init");
        __try {
            callback_ok =
                callbacks.pre_game_bootstrap(api) == DLUMA_STATUS_OK;
        }
        __except(1) { callback_ok = 0; }
        if (!callback_ok) {
            error = "pre-game callback failed or raised SEH";
            __try {
                callbacks.shutdown(api, DLUMA_STOP_RECOVERY);
            }
            __except(1) { error = "pre-game unwind raised SEH"; }
            dluma_mod_owner_begin_revoke(
                &g_dluma_native_mods.entries[i].api_owner);
            dluma_game_host_context_revoke(
                &g_dluma_native_mods.entries[i].api_host);
            dluma_mod_owner_finish_revoke(
                &g_dluma_native_mods.entries[i].api_owner);
        }

commit:
        dluma_native_lock();
        if (i < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[i].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_LOADING &&
            g_dluma_native_mods.entries[i].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[i];
            DlumaNativeModSnapshot* item = &entry->snapshot;
            entry->module = module;
            entry->get_api_v2 = get_api_v2;
            entry->callbacks = callbacks;
            if (changed_before_load) {
                item->approval_valid = 0;
                item->enabled_requested = 0;
                item->state = DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED;
                dluma_str_copy(
                    item->runtime_error,
                    DLUMA_ARRAY_COUNT(item->runtime_error), error);
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_FAILED;
            } else if (error) {
                dluma_native_set_error(item, error);
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_FAILED;
            } else {
                item->loaded = 1;
                item->pre_init_succeeded = 1;
                item->state = DLUMA_NATIVE_STATE_LOADED;
                item->runtime_error[0] = 0;
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_READY;
            }
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
            committed = 1;
        }
        dluma_native_unlock();
        dluma_native_locked_artifact_close(&artifact);
        if (!committed) {
            dluma_log_pair(
                "WARN", "native_mod_stale_epoch_commit_ignored", claimed.id);
            continue;
        }
        if (error) {
            dluma_log_pair("ERROR", "native_mod_load_error", error);
            dluma_startup_recovery_note_native_failure(
                claimed.id, claimed.registry_key,
                claimed.approval_fingerprint);
            continue;
        }
        dluma_startup_recovery_clear_native_mod();
        dluma_log_pair("INFO", "native_mod_pre_init_end", claimed.id);
        dluma_log_pair("INFO", "native_mod_load_end", claimed.id);
    }
}

void dluma_native_mods_post_game_init(HMODULE original_gamedll,
                                    void* game_script_manager)
{
    unsigned int i;
    (void)original_gamedll;
    if (!game_script_manager) return;
    for (i = 0; ; ++i) {
        DlumaNativeModApiV2 callbacks;
        const DlumaApiV1* api;
        DlumaScopeActivationV1 activation;
        DlumaModOwner* api_owner;
        DWORD map_generation;
        unsigned int scope;
        char id[128];
        char registry_key[DLUMA_SHA256_HEX_CAPACITY];
        char approval_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
        DWORD epoch;
        int callback_ok = 0;
        int unwind_failed = 0;
        int committed = 0;
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&activation, 0, sizeof(activation));
        dluma_native_lock();
        if (i >= g_dluma_native_mods.count) {
            dluma_native_unlock();
            break;
        }
        if (!g_dluma_native_mods.entries[i].module ||
            !g_dluma_native_mods.entries[i].snapshot.pre_init_succeeded ||
            !g_dluma_native_mods.entries[i].callbacks.post_game_ready ||
            !g_dluma_native_mods.entries[i].callbacks.scope_activated ||
            !g_dluma_native_mods.entries[i].callbacks.event ||
            !g_dluma_native_mods.entries[i].callbacks.shutdown ||
            g_dluma_native_mods.entries[i].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY) {
            dluma_native_unlock();
            continue;
        }
        g_dluma_native_mods.entries[i].lifecycle_phase =
            DLUMA_NATIVE_PHASE_POSTING;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[i].lifecycle_epoch = epoch;
        callbacks = g_dluma_native_mods.entries[i].callbacks;
        api = dluma_api_runtime_api(
            &g_dluma_native_mods.entries[i].api_runtime);
        api_owner = &g_dluma_native_mods.entries[i].api_owner;
        map_generation = api_owner->map_generation;
        scope =
            g_dluma_native_mods.entries[i].package.manifest.scope;
        dluma_str_copy(
            id, DLUMA_ARRAY_COUNT(id),
            g_dluma_native_mods.entries[i].snapshot.id);
        dluma_str_copy(
            registry_key, DLUMA_ARRAY_COUNT(registry_key),
            g_dluma_native_mods.entries[i].snapshot.registry_key);
        dluma_str_copy(
            approval_fingerprint, DLUMA_ARRAY_COUNT(approval_fingerprint),
            g_dluma_native_mods.entries[i].snapshot.approval_fingerprint);
        dluma_native_unlock();

        dluma_log_pair("INFO", "native_mod_post_init_begin", id);
        dluma_startup_recovery_note_native_mod(
            id, registry_key, approval_fingerprint,
            "native_post_game_init");
        activation.struct_size = (uint32_t)sizeof(activation);
        activation.scope = scope;
        activation.map_generation = map_generation;
        if (api) {
            __try {
                callback_ok =
                    callbacks.post_game_ready(api) == DLUMA_STATUS_OK;
                if (callback_ok && scope == DLUMA_SCOPE_GLOBAL)
                    callback_ok = callbacks.scope_activated(
                        api, &activation) == DLUMA_STATUS_OK;
            }
            __except(1) { callback_ok = 0; }
        }
        if (callback_ok && scope != DLUMA_SCOPE_GLOBAL) {
            dluma_game_host_context_advance_scope(
                &g_dluma_native_mods.entries[i].api_host);
            dluma_mod_owner_advance_scope(
                api_owner, map_generation);
        }
        if (!callback_ok) {
            if (api) {
                __try {
                    callbacks.shutdown(api, DLUMA_STOP_RECOVERY);
                }
                __except(1) { unwind_failed = 1; }
            } else {
                unwind_failed = 1;
            }
            dluma_mod_owner_begin_revoke(api_owner);
            dluma_game_host_context_revoke(
                &g_dluma_native_mods.entries[i].api_host);
            dluma_mod_owner_finish_revoke(api_owner);
        }
        dluma_native_lock();
        if (i < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[i].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_POSTING &&
            g_dluma_native_mods.entries[i].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[i];
            if (callback_ok) {
                entry->snapshot.post_init_succeeded = 1;
                entry->scope_active =
                    scope == DLUMA_SCOPE_GLOBAL ? 1U : 0U;
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_READY;
            } else {
                dluma_native_set_error(
                    &entry->snapshot,
                    unwind_failed ? "post-game unwind raised SEH"
                                  : "post-game callback failed or raised SEH");
                entry->snapshot.pre_init_succeeded = 0;
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_FAILED;
            }
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
            committed = 1;
        }
        dluma_native_unlock();
        if (!committed) {
            dluma_log_pair(
                "WARN", "native_mod_stale_epoch_commit_ignored", id);
            continue;
        }
        if (!callback_ok) {
            dluma_log_pair("ERROR", "native_mod_post_init_error", id);
            dluma_startup_recovery_note_native_failure(
                id, registry_key, approval_fingerprint);
            continue;
        }
        dluma_startup_recovery_clear_native_mod();
        dluma_log_pair("INFO", "native_mod_post_init_end", id);
    }
}

static DlumaStringView dluma_native_scope_string(const char* value)
{
    DlumaStringView view;
    view.data = value;
    view.size = value ? (uint64_t)strlen(value) : 0U;
    return view;
}

void dluma_native_mods_deactivate_scope(
    unsigned int next_map_generation)
{
    int reverse_index;
    unsigned int count;
    unsigned int index;
    dluma_native_lock();
    count = g_dluma_native_mods.count;
    dluma_native_unlock();

    /* Scope exit is reverse language-local load order.  The caller invokes
     * this after Lua and managed scope exit, preserving global reverse
     * shutdown order across hosts. */
    for (reverse_index = (int)count - 1; reverse_index >= 0;
         --reverse_index) {
        DlumaNativeModApiV2 callbacks;
        DlumaScopeActivationV1 activation;
        const DlumaApiV1* api;
        DlumaModOwner* api_owner;
        char old_project[128];
        char old_map[128];
        DWORD epoch;
        int raised = 0;
        int drained;
        index = (unsigned int)reverse_index;
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&activation, 0, sizeof(activation));
        dluma_native_lock();
        if (index >= g_dluma_native_mods.count ||
            !g_dluma_native_mods.entries[index].scope_active ||
            g_dluma_native_mods.entries[index].package.manifest.scope ==
                DLUMA_SCOPE_GLOBAL ||
            g_dluma_native_mods.entries[index].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY) {
            dluma_native_unlock();
            continue;
        }
        g_dluma_native_mods.entries[index].lifecycle_phase =
            DLUMA_NATIVE_PHASE_POSTING;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[index].lifecycle_epoch = epoch;
        callbacks = g_dluma_native_mods.entries[index].callbacks;
        api = dluma_api_runtime_api(
            &g_dluma_native_mods.entries[index].api_runtime);
        api_owner = &g_dluma_native_mods.entries[index].api_owner;
        dluma_str_copy(
            old_project, DLUMA_ARRAY_COUNT(old_project),
            g_dluma_native_mods.entries[index].active_project);
        dluma_str_copy(
            old_map, DLUMA_ARRAY_COUNT(old_map),
            g_dluma_native_mods.entries[index].active_map);
        activation.struct_size = (uint32_t)sizeof(activation);
        activation.scope =
            g_dluma_native_mods.entries[index].package.manifest.scope;
        activation.map_generation = api_owner->map_generation;
        dluma_native_unlock();
        activation.project = dluma_native_scope_string(old_project);
        activation.map = dluma_native_scope_string(old_map);
        if (api) {
            __try {
                callbacks.scope_deactivated(api, &activation);
            }
            __except(1) { raised = 1; }
        } else {
            raised = 1;
        }
        dluma_game_host_context_advance_scope(
            &g_dluma_native_mods.entries[index].api_host);
        dluma_mod_owner_advance_scope(
            api_owner, next_map_generation);
        drained = 1;
        dluma_native_lock();
        if (index < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[index].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_POSTING &&
            g_dluma_native_mods.entries[index].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry =
                &g_dluma_native_mods.entries[index];
            entry->scope_active = 0;
            entry->active_project[0] = 0;
            entry->active_map[0] = 0;
            if (raised || !drained) {
                dluma_native_set_error(
                    &entry->snapshot,
                    raised
                        ? "scope-deactivation callback raised SEH"
                        : "scope resources did not fully drain");
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_FAILED;
            } else {
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_READY;
            }
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
        }
        dluma_native_unlock();
    }

    /* Global native mods stay active across maps. Their opaque game handles
     * expire, while timers and non-map resources remain owned and exact-name
     * subscriptions are rebound to the next generation. */
    for (index = 0; index < count; ++index) {
        DlumaNativeModEntry* entry =
            &g_dluma_native_mods.entries[index];
        if (!entry->scope_active ||
            entry->package.manifest.scope !=
                DLUMA_SCOPE_GLOBAL ||
            entry->lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY)
            continue;
        dluma_mod_owner_advance_scope(
            &entry->api_owner,
            next_map_generation);
        if (!entry->global_map_exit_suspended)
            dluma_game_host_context_rebind_map(&entry->api_host);
        dluma_native_lock();
        entry->active_project[0] = 0;
        entry->active_map[0] = 0;
        dluma_native_unlock();
    }

}

int dluma_native_mods_revoke_project_for_exit(
    unsigned int expected_generation, DlumaProjectHostExitDrain* out)
{
    return dluma_native_mods_revoke_project_for_exit_ex(expected_generation,0,out);
}

int dluma_native_mods_revoke_project_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority* authority,
    DlumaProjectHostExitDrain* out)
{
    DlumaProjectHostExitDrain result;
    unsigned int indices[DLUMA_MAX_NATIVE_MODS];
    uint64_t owners[DLUMA_MAX_NATIVE_MODS];
    unsigned int selected = 0;
    unsigned int i;
    unsigned int current_thread = GetCurrentThreadId();
    memset(&result, 0, sizeof(result));
    result.expected_generation = expected_generation;
    result.sdk_contract_only = 1U;
    if (!out) return 0;
    if (!dluma_project_exit_validate_authority(expected_generation,authority)) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_owner_not_authoritative");
        *out = result; return 0;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_native_mods.pump_active, 1, 0)) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_pump_active");
        *out = result; return 0;
    }
    dluma_atomic_exchange(&g_dluma_native_project_exit_gate, 1);
    if (dluma_atomic_compare_exchange(&g_dluma_native_mods.lock, 1, 0)) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_registry_busy");
        goto done;
    }
    for (i = 0; i < g_dluma_native_mods.count; ++i) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[i];
        unsigned int active;
        if (entry->package.manifest.scope == DLUMA_SCOPE_GLOBAL) {
            if (entry->scope_active) ++result.global_owners_preserved;
            continue;
        }
        if (entry->scope_exit_revoked == 2U) {
            ++result.remaining_owners;
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_previous_deactivation_failed");
            dluma_str_copy(result.first_owner, DLUMA_ARRAY_COUNT(result.first_owner), entry->snapshot.id);
            break;
        }
        if (!entry->scope_active && !entry->cleanup_pending) {
            if (entry->scope_exit_revoked && entry->api_owner.map_generation == expected_generation) {
                indices[selected] = i;
                owners[selected++] = entry->api_owner.owner_id;
            }
            continue;
        }
        ++result.project_owners;
        active = (unsigned int)dluma_atomic_compare_exchange(&entry->quest_phase_callbacks_in_flight,0,0);
        if (entry->lifecycle_phase == DLUMA_NATIVE_PHASE_POSTING) ++active;
        result.active_callbacks += active;
        if (entry->snapshot.abi_kind != DLUMA_NATIVE_ABI_V2 || !entry->callbacks.scope_deactivated) {
            ++result.unsupported_owners;
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_lifecycle_contract_unavailable");
        } else if (entry->api_owner.map_generation != expected_generation) {
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_owner_generation_mismatch");
        } else if (active || entry->lifecycle_phase != DLUMA_NATIVE_PHASE_READY || entry->cleanup_pending) {
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_callback_or_cleanup_pending");
        }
        if (result.blocker[0]) {
            dluma_str_copy(result.first_owner, DLUMA_ARRAY_COUNT(result.first_owner), entry->snapshot.id);
            break;
        }
        indices[selected] = i;
        owners[selected++] = entry->api_owner.owner_id;
    }
    if (result.blocker[0]) { dluma_native_unlock(); goto done; }
    result.accepted = result.owner_thread_confirmed = 1U;
    /* Claim every local participant before releasing the registry. New local
     * event/phase dispatch is gated, and global owners are not changed. */
    for (i = 0; i < selected; ++i)
        if (!g_dluma_native_mods.entries[indices[i]].scope_exit_revoked) {
            g_dluma_native_mods.entries[indices[i]].lifecycle_phase = DLUMA_NATIVE_PHASE_POSTING;
            g_dluma_native_mods.entries[indices[i]].api_owner.game_thread_id=current_thread;
        }
    dluma_native_unlock();
    for (i = selected; i > 0; --i) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[indices[i-1U]];
        DlumaScopeActivationV1 activation;
        const DlumaApiV1* api = dluma_api_runtime_api(&entry->api_runtime);
        int raised = 0;
        if (entry->scope_exit_revoked) continue;
        memset(&activation, 0, sizeof(activation));
        activation.struct_size = sizeof(activation);
        activation.scope = entry->package.manifest.scope;
        activation.map_generation = expected_generation;
        activation.project = dluma_native_scope_string(entry->active_project);
        activation.map = dluma_native_scope_string(entry->active_map);
        if (api) {
            __try { entry->callbacks.scope_deactivated(api, &activation); }
            __except(1) { raised = 1; }
        } else raised = 1;
        dluma_mod_owner_begin_revoke(&entry->api_owner);
        dluma_game_host_context_advance_scope(&entry->api_host);
        dluma_mod_owner_finish_revoke(&entry->api_owner);
        entry->scope_active = 0U;
        entry->active_project[0] = entry->active_map[0] = 0;
        entry->scope_exit_revoked = raised ? 2U : 1U;
        entry->lifecycle_phase = raised ? DLUMA_NATIVE_PHASE_FAILED : DLUMA_NATIVE_PHASE_READY;
        if (raised) {
            ++result.remaining_owners;
            dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_deactivation_failed");
            dluma_str_copy(result.first_owner, DLUMA_ARRAY_COUNT(result.first_owner), entry->snapshot.id);
            dluma_native_set_error(&entry->snapshot, "project exit scope callback failed or raised SEH");
        }
        if (dluma_atomic_compare_exchange(&entry->quest_phase_callbacks_in_flight,0,0) ||
            entry->api_owner.state != DLUMA_MOD_OWNER_REVOKED || entry->cleanup_pending)
            ++result.remaining_owners;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_native_mods.event_lock,1,0)) {
        dluma_str_copy(result.blocker, DLUMA_ARRAY_COUNT(result.blocker), "native_scope_event_queue_busy");
    } else {
        unsigned int write = 0;
        for (i = 0; i < g_dluma_native_mods.event_count; ++i) {
            DlumaNativeQueuedEvent* event = &g_dluma_native_mods.events[
                (g_dluma_native_mods.event_head+i) % DLUMA_NATIVE_MOD_MAX_EVENTS];
            unsigned int owner;
            int retired = 0;
            for (owner=0;owner<selected;++owner) if (event->owner_id==owners[owner]) retired=1;
            if (!retired) g_dluma_native_mods.events[
                (g_dluma_native_mods.event_head+write++) % DLUMA_NATIVE_MOD_MAX_EVENTS] = *event;
        }
        g_dluma_native_mods.event_count = write;
        dluma_atomic_exchange(&g_dluma_native_mods.event_lock,0);
    }
    if (!dluma_atomic_compare_exchange(&g_dluma_native_mods.lock,1,0)) {
        ++g_dluma_native_mods.revision;
        dluma_native_unlock();
    } else {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"native_scope_publication_busy");
    }
    result.complete = !result.blocker[0] && !result.remaining_owners;
done:
    dluma_atomic_exchange(&g_dluma_native_mods.pump_active,0);
    *out=result;
    return result.complete != 0;
}

int dluma_native_mods_freeze_map_exit(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority)
{
    unsigned int i;
    int ready=1;
    if(!dluma_project_exit_validate_authority(generation,authority))return 0;
    dluma_atomic_exchange(&g_dluma_native_map_exit_frozen,1);
    dluma_atomic_exchange(&g_dluma_native_project_exit_gate,1);
    g_dluma_native_map_exit_generation=generation;
    if(dluma_atomic_compare_exchange(&g_dluma_native_mods.pump_active,0,0) ||
       dluma_atomic_compare_exchange(&g_dluma_native_mods.lock,1,0))return 0;
    for(i=0;i<g_dluma_native_mods.count;++i){
        DlumaNativeModEntry* e=&g_dluma_native_mods.entries[i];
        if(!e->scope_active)continue;
        if(e->lifecycle_phase!=DLUMA_NATIVE_PHASE_READY || e->cleanup_pending ||
           dluma_atomic_compare_exchange(&e->quest_phase_callbacks_in_flight,0,0) ||
           (e->active_map[0] && e->api_owner.map_generation!=generation))ready=0;
    }
    dluma_native_unlock();return ready;
}

int dluma_native_mods_suspend_global_map(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority,unsigned int* count)
{
    unsigned int i;int complete=1;
    if(count)*count=0;
    if(!g_dluma_native_map_exit_frozen || generation!=g_dluma_native_map_exit_generation ||
       !dluma_project_exit_validate_authority(generation,authority) ||
       dluma_atomic_compare_exchange(&g_dluma_native_mods.pump_active,0,0))return 0;
    for(i=0;i<g_dluma_native_mods.count;++i){
        DlumaNativeModEntry* e=&g_dluma_native_mods.entries[i];
        if(e->package.manifest.scope!=DLUMA_SCOPE_GLOBAL || !e->scope_active)continue;
        if(e->lifecycle_phase!=DLUMA_NATIVE_PHASE_READY ||
           dluma_atomic_compare_exchange(&e->quest_phase_callbacks_in_flight,0,0))return 0;
        e->api_owner.game_thread_id=GetCurrentThreadId();
        if(!dluma_game_host_context_suspend_map_for_exit(&e->api_host))complete=0;
        e->global_map_exit_suspended=1U;
        if(count)++*count;
    }
    return complete;
}

int dluma_native_mods_bind_frontend_after_exit(unsigned int old_generation,
    unsigned int new_generation,const DlumaProjectExitDrainAuthority* authority)
{
    unsigned int i;
    if(!g_dluma_native_map_exit_frozen || old_generation!=g_dluma_native_map_exit_generation ||
       !dluma_project_exit_validate_authority(old_generation,authority))return 0;
    for(i=0;i<g_dluma_native_mods.count;++i){
        DlumaNativeModEntry* e=&g_dluma_native_mods.entries[i];
        if(!e->global_map_exit_suspended || e->package.manifest.scope!=DLUMA_SCOPE_GLOBAL)continue;
        e->api_owner.game_thread_id=GetCurrentThreadId();
        e->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        if(e->api_owner.map_generation!=new_generation)
            dluma_mod_owner_advance_scope(&e->api_owner,new_generation);
        e->api_owner.state=DLUMA_MOD_OWNER_REVOKING;
        dluma_game_host_context_bind_clean_frontend(&e->api_host);
        e->active_project[0]=e->active_map[0]=0;
    }
    if(dluma_atomic_compare_exchange(&g_dluma_native_mods.event_lock,1,0))return 0;
    {
        unsigned int write=0;
        for(i=0;i<g_dluma_native_mods.event_count;++i){
            DlumaNativeQueuedEvent event=g_dluma_native_mods.events[(g_dluma_native_mods.event_head+i)%DLUMA_NATIVE_MOD_MAX_EVENTS];
            unsigned int owner;int keep=0;
            if(event.kind==DLUMA_MOD_EVENT_CONFIGURATION_CHANGED)
                for(owner=0;owner<g_dluma_native_mods.count;++owner)
                    if(g_dluma_native_mods.entries[owner].package.manifest.scope==DLUMA_SCOPE_GLOBAL &&
                       g_dluma_native_mods.entries[owner].api_owner.owner_id==event.owner_id)keep=1;
            if(keep){event.map_generation=new_generation;g_dluma_native_mods.events[(g_dluma_native_mods.event_head+write++)%DLUMA_NATIVE_MOD_MAX_EVENTS]=event;}
        }
        g_dluma_native_mods.event_count=write;
    }
    dluma_atomic_exchange(&g_dluma_native_mods.event_lock,0);
    return 1;
}

void dluma_native_mods_resume_frontend_after_exit(void)
{
    unsigned int i;
    for(i=0;i<g_dluma_native_mods.count;++i){
        DlumaNativeModEntry* e=&g_dluma_native_mods.entries[i];
        if(e->global_map_exit_suspended && e->package.manifest.scope==DLUMA_SCOPE_GLOBAL &&
           e->api_owner.state==DLUMA_MOD_OWNER_REVOKING)e->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
    }
    dluma_atomic_exchange(&g_dluma_native_project_exit_gate,0);
    dluma_atomic_exchange(&g_dluma_native_map_exit_frozen,0);
}

void dluma_native_mods_activate_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation)
{
    unsigned int count;
    unsigned int index;
    if(g_dluma_native_map_exit_frozen)return;
    dluma_native_lock();
    count = g_dluma_native_mods.count;
    dluma_native_unlock();
    if (!project_root || !project_root[0] ||
        !map_name || !map_name[0]) {
        dluma_atomic_exchange(&g_dluma_native_project_exit_gate, 0);
        return;
    }
    if (g_dluma_native_project_exit_gate) return;

    for (index = 0; index < count; ++index) {
        DlumaNativeModEntry* entry =
            &g_dluma_native_mods.entries[index];
        if (!entry->scope_active ||
            entry->package.manifest.scope !=
                DLUMA_SCOPE_GLOBAL ||
            entry->lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY)
            continue;
        if (entry->api_owner.map_generation != map_generation || entry->global_map_exit_suspended) {
            if(entry->api_owner.map_generation!=map_generation)
                dluma_mod_owner_advance_scope(&entry->api_owner,map_generation);
            dluma_game_host_context_rebind_map(
                &entry->api_host);
            entry->global_map_exit_suspended=0U;
        }
        dluma_native_lock();
        dluma_str_copy(
            entry->active_project,
            DLUMA_ARRAY_COUNT(entry->active_project),
            project_name ? project_name : "");
        dluma_str_copy(
            entry->active_map,
            DLUMA_ARRAY_COUNT(entry->active_map),
            map_name);
        dluma_native_unlock();
    }

    /* Scope entry follows native load order and happens before managed/Lua
     * activation in the caller. */
    for (index = 0; index < count; ++index) {
        DlumaNativeModApiV2 callbacks;
        DlumaScopeActivationV1 activation;
        const DlumaApiV1* api;
        DlumaModOwner* api_owner;
        char next_project[128];
        char next_map[128];
        unsigned int scope;
        DWORD epoch;
        int callback_ok = 0;
        int unwind_raised = 0;
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&activation, 0, sizeof(activation));
        dluma_native_lock();
        if (index >= g_dluma_native_mods.count ||
            g_dluma_native_mods.entries[index].scope_active ||
            !g_dluma_native_mods.entries[index]
                 .snapshot.post_init_succeeded ||
            g_dluma_native_mods.entries[index].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY ||
            dluma_wstricmp(
                g_dluma_native_mods.entries[index].project_root,
                project_root) != 0) {
            dluma_native_unlock();
            continue;
        }
        scope =
            g_dluma_native_mods.entries[index].package.manifest.scope;
        if (scope == DLUMA_SCOPE_GLOBAL ||
            (scope == DLUMA_SCOPE_MAP &&
             dluma_stricmp(
                 g_dluma_native_mods.entries[index]
                     .package.manifest.map,
                 map_name) != 0)) {
            dluma_native_unlock();
            continue;
        }
        g_dluma_native_mods.entries[index].lifecycle_phase =
            DLUMA_NATIVE_PHASE_POSTING;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[index].lifecycle_epoch = epoch;
        callbacks = g_dluma_native_mods.entries[index].callbacks;
        api = dluma_api_runtime_api(
            &g_dluma_native_mods.entries[index].api_runtime);
        api_owner = &g_dluma_native_mods.entries[index].api_owner;
        dluma_str_copy(
            next_project, DLUMA_ARRAY_COUNT(next_project),
            project_name ? project_name : "");
        dluma_str_copy(
            next_map, DLUMA_ARRAY_COUNT(next_map), map_name);
        dluma_game_host_context_advance_scope(
            &g_dluma_native_mods.entries[index].api_host);
        if (g_dluma_native_mods.entries[index].scope_exit_revoked &&
            api_owner->state == DLUMA_MOD_OWNER_REVOKED) {
            api_owner->state = DLUMA_MOD_OWNER_ACTIVE;
            api_owner->game_thread_id = GetCurrentThreadId();
            g_dluma_native_mods.entries[index].scope_exit_revoked = 0;
        }
        dluma_mod_owner_advance_scope(api_owner, map_generation);
        dluma_native_unlock();
        activation.struct_size = (uint32_t)sizeof(activation);
        activation.scope = scope;
        activation.map_generation = map_generation;
        activation.project =
            dluma_native_scope_string(next_project);
        activation.map = dluma_native_scope_string(next_map);
        if (api) {
            __try {
                callback_ok =
                    callbacks.scope_activated(
                        api, &activation) == DLUMA_STATUS_OK;
            }
            __except(1) { callback_ok = 0; }
        }
        if (!callback_ok) {
            if (api) {
                __try {
                    callbacks.scope_deactivated(api, &activation);
                }
                __except(1) { unwind_raised = 1; }
            }
            dluma_game_host_context_advance_scope(
                &g_dluma_native_mods.entries[index].api_host);
            dluma_mod_owner_advance_scope(api_owner, map_generation);
        }
        dluma_native_lock();
        if (index < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[index].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_POSTING &&
            g_dluma_native_mods.entries[index].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry =
                &g_dluma_native_mods.entries[index];
            if (callback_ok) {
                entry->scope_active = 1U;
                dluma_str_copy(
                    entry->active_project,
                    DLUMA_ARRAY_COUNT(entry->active_project),
                    next_project);
                dluma_str_copy(
                    entry->active_map,
                    DLUMA_ARRAY_COUNT(entry->active_map),
                    next_map);
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_READY;
            } else {
                dluma_native_set_error(
                    &entry->snapshot,
                    unwind_raised
                        ? "scope-activation unwind raised SEH"
                        : "scope-activation callback failed or raised SEH");
                entry->lifecycle_phase = DLUMA_NATIVE_PHASE_FAILED;
            }
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
        }
        dluma_native_unlock();
    }
}

static void dluma_native_dispatch_queued_event(
    DlumaNativeModEntry* entry,
    const DlumaNativeQueuedEvent* queued)
{
    DlumaNativeModApiV2 callbacks;
    DlumaModEventV1 event_value;
    const DlumaApiV1* api;
    char project[128];
    char map[128];
    DWORD epoch;
    int callback_ok = 0;
    if (!entry || !queued) return;
    memset(&callbacks, 0, sizeof(callbacks));
    memset(&event_value, 0, sizeof(event_value));
    dluma_native_lock();
    /* Map reconciliation runs before this queue is drained. Global owners
     * survive that transition, but observations from their old map do not. */
    if (entry->lifecycle_phase !=
            DLUMA_NATIVE_PHASE_READY ||
        g_dluma_native_map_exit_frozen ||
        (g_dluma_native_project_exit_gate &&
         entry->package.manifest.scope != DLUMA_SCOPE_GLOBAL) ||
        !entry->scope_active ||
        entry->api_owner.state != DLUMA_MOD_OWNER_ACTIVE ||
        entry->api_owner.owner_id != queued->owner_id ||
        entry->api_owner.map_generation != queued->map_generation ||
        !entry->callbacks.event) {
        dluma_native_unlock();
        return;
    }
    entry->lifecycle_phase =
        DLUMA_NATIVE_PHASE_POSTING;
    epoch = dluma_native_next_lifecycle_epoch_locked();
    entry->lifecycle_epoch = epoch;
    callbacks = entry->callbacks;
    api = dluma_api_runtime_api(&entry->api_runtime);
    dluma_str_copy(
        project, DLUMA_ARRAY_COUNT(project),
        entry->active_project);
    dluma_str_copy(
        map, DLUMA_ARRAY_COUNT(map),
        entry->active_map);
    dluma_native_unlock();

    event_value.struct_size =
        (uint32_t)sizeof(event_value);
    event_value.struct_version = 1U;
    event_value.kind = queued->kind;
    event_value.map_generation =
        queued->map_generation;
    event_value.callback_id =
        queued->callback_id;
    event_value.token = queued->token;
    event_value.number = queued->number;
    event_value.state = queued->state;
    event_value.name =
        dluma_native_scope_string(queued->name);
    event_value.project =
        dluma_native_scope_string(project);
    event_value.map =
        dluma_native_scope_string(map);
    if (api) {
        __try {
            callback_ok = callbacks.event(
                api, &event_value) == DLUMA_STATUS_OK;
        }
        __except(1) { callback_ok = 0; }
    }

    dluma_native_lock();
    if (entry->lifecycle_phase ==
            DLUMA_NATIVE_PHASE_POSTING &&
        entry->lifecycle_epoch == epoch) {
        if (callback_ok) {
            entry->lifecycle_phase =
                DLUMA_NATIVE_PHASE_READY;
        } else {
            dluma_native_set_error(
                &entry->snapshot,
                "native event callback failed or raised SEH");
            entry->snapshot.post_init_succeeded = 0U;
            entry->scope_active = 0U;
            entry->cleanup_pending = 1U;
            entry->lifecycle_phase =
                DLUMA_NATIVE_PHASE_FAILED;
            dluma_mod_owner_begin_revoke(
                &entry->api_owner);
            /* Registry revisions describe snapshot-visible state.  Successful
             * event delivery only returns the private lifecycle phase to READY;
             * publishing a revision for every timer/game event makes open Mods
             * screens clear and rebuild their list every frame. */
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
        }
    }
    dluma_native_unlock();
}

static void dluma_native_api_event_sink(
    void* user,
    uint32_t kind,
    uint64_t callback_id,
    uint64_t token,
    double number,
    const char* name)
{
    DlumaNativeQueuedEvent queued;
    DlumaNativeModEntry* entry =
        (DlumaNativeModEntry*)user;
    if (!entry) return;
    memset(&queued, 0, sizeof(queued));
    queued.owner_id = entry->api_owner.owner_id;
    queued.callback_id = callback_id;
    queued.token = token;
    queued.kind = kind;
    queued.map_generation =
        entry->api_owner.map_generation;
    queued.number = number;
    dluma_str_copy(
        queued.name, DLUMA_ARRAY_COUNT(queued.name),
        name ? name : "");
    dluma_native_dispatch_queued_event(
        entry, &queued);
}

int dluma_native_mods_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number)
{
    DlumaNativeEventTarget targets[
        DLUMA_NATIVE_MOD_MAX_EVENTS];
    unsigned int target_count;
    unsigned int index;
    int queued_any = 0;
    if (!exact_name || !exact_name[0] ||
        strlen(exact_name) >= 96U)
        return 0;
    target_count =
        dluma_native_gameplay_snapshot_event_targets(
            kind,
            map_generation,
            native_index_epoch,
            native_subscription_key_revision,
            exact_name,
            targets,
            DLUMA_ARRAY_COUNT(targets));
    while (dluma_atomic_compare_exchange(
               &g_dluma_native_mods.event_lock,
               1, 0) != 0)
        Sleep(1);
    for (index = 0; index < target_count; ++index) {
        unsigned int entry_index;
        unsigned int queue_index;
        DlumaNativeQueuedEvent* event_value;
        int native_owner = 0;
        for (entry_index = 0;
             entry_index < g_dluma_native_mods.count;
             ++entry_index) {
            if (g_dluma_native_mods.entries[entry_index]
                    .api_owner.owner_id ==
                targets[index].owner_id) {
                native_owner = 1;
                break;
            }
        }
        if (!native_owner) continue;
        if (g_dluma_native_mods.event_count >=
            DLUMA_NATIVE_MOD_MAX_EVENTS)
            break;
        queue_index =
            (g_dluma_native_mods.event_head +
             g_dluma_native_mods.event_count) %
            DLUMA_NATIVE_MOD_MAX_EVENTS;
        event_value =
            &g_dluma_native_mods.events[queue_index];
        memset(event_value, 0, sizeof(*event_value));
        event_value->owner_id =
            targets[index].owner_id;
        event_value->callback_id =
            targets[index].callback_id;
        event_value->token =
            targets[index].registration_token;
        event_value->kind = kind;
        event_value->map_generation = map_generation;
        event_value->state = state ? 1 : 0;
        event_value->number = number;
        dluma_str_copy(
            event_value->name,
            DLUMA_ARRAY_COUNT(event_value->name),
            exact_name);
        ++g_dluma_native_mods.event_count;
        queued_any = 1;
    }
    dluma_atomic_exchange(
        &g_dluma_native_mods.event_lock, 0);
    if (target_count && !queued_any)
        dluma_log_line(
            "ERROR",
            "native MOD event queue overflow");
    return queued_any;
}

static void dluma_native_finish_pending_cleanup(
    DlumaNativeModEntry* entry)
{
    DlumaNativeModApiV2 callbacks;
    const DlumaApiV1* api;
    int raised = 0;
    int drained;
    if (!entry || !entry->cleanup_pending)
        return;
    callbacks = entry->callbacks;
    api = dluma_api_runtime_api(&entry->api_runtime);
    if (api && callbacks.shutdown) {
        __try {
            callbacks.shutdown(api, DLUMA_STOP_RECOVERY);
        }
        __except(1) { raised = 1; }
    } else {
        raised = 1;
    }
    dluma_game_host_context_revoke(
        &entry->api_host);
    dluma_mod_owner_finish_revoke(
        &entry->api_owner);
    drained = 1;
    dluma_native_lock();
    entry->cleanup_pending = 0U;
    entry->lifecycle_phase = drained && !raised
        ? DLUMA_NATIVE_PHASE_FAILED
        : DLUMA_NATIVE_PHASE_RETAINED;
    if (!drained || raised)
        dluma_native_set_error(
            &entry->snapshot,
            raised
                ? "native event recovery callback raised SEH"
                : "native event recovery retained undrained resources");
    ++g_dluma_native_mods.revision;
    dluma_native_publish_crash_context_locked();
    dluma_native_unlock();
}

void dluma_native_mods_prepare_game_frame(void)
{
    if(g_dluma_native_map_exit_frozen)return;
    unsigned int index;
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) ||
        dluma_engine_game_frame_authority_status(&g_dluma.engine, GetTickCount64()) !=
            DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE)
        return;
    dluma_native_lock();
    for (index = 0; index < g_dluma_native_mods.count; ++index) {
        DlumaNativeModEntry* entry = &g_dluma_native_mods.entries[index];
        if (entry->api_owner.state != DLUMA_MOD_OWNER_ACTIVE ||
            entry->lifecycle_phase != DLUMA_NATIVE_PHASE_READY)
            continue;
        /* Scope callbacks precede event dispatch. Rebind before either can
         * call the API, and refresh adapters that became ready after load. */
        entry->api_owner.game_thread_id = GetCurrentThreadId();
        dluma_game_backend_prepare_capabilities(&entry->api_capabilities);
        dluma_game_host_context_prepare_capabilities(
            &entry->api_host, &entry->api_capabilities);
    }
    dluma_native_unlock();
}

void dluma_native_mods_game_thread_pump(
    double delta_seconds)
{
    if(g_dluma_native_map_exit_frozen)return;
    unsigned int index;
    if (dluma_atomic_compare_exchange(
            &g_dluma_native_mods.pump_active,
            1, 0) != 0)
        return;
    for (;;) {
        DlumaNativeQueuedEvent queued;
        DlumaNativeModEntry* target = 0;
        int have_event = 0;
        memset(&queued, 0, sizeof(queued));
        while (dluma_atomic_compare_exchange(
                   &g_dluma_native_mods.event_lock,
                   1, 0) != 0)
            Sleep(1);
        if (g_dluma_native_mods.event_count) {
            queued = g_dluma_native_mods.events[
                g_dluma_native_mods.event_head];
            memset(
                &g_dluma_native_mods.events[
                    g_dluma_native_mods.event_head],
                0,
                sizeof(g_dluma_native_mods.events[0]));
            g_dluma_native_mods.event_head =
                (g_dluma_native_mods.event_head + 1U) %
                DLUMA_NATIVE_MOD_MAX_EVENTS;
            --g_dluma_native_mods.event_count;
            have_event = 1;
        }
        dluma_atomic_exchange(
            &g_dluma_native_mods.event_lock, 0);
        if (!have_event) break;
        for (index = 0;
             index < g_dluma_native_mods.count;
             ++index)
            if (g_dluma_native_mods.entries[index]
                    .api_owner.owner_id == queued.owner_id) {
                target =
                    &g_dluma_native_mods.entries[index];
                break;
            }
        if (target) {
            if (queued.kind == 10U &&
                !queued.callback_id)
                dluma_game_host_context_notify_config_changed(
                    &target->api_host,
                    queued.name);
            else
                dluma_native_dispatch_queued_event(
                    target, &queued);
            dluma_native_finish_pending_cleanup(target);
        }
    }
    for (index = 0;
         index < g_dluma_native_mods.count;
         ++index) {
        DlumaNativeModEntry* entry =
            &g_dluma_native_mods.entries[index];
        if (entry->scope_active &&
            entry->lifecycle_phase ==
                DLUMA_NATIVE_PHASE_READY)
            dluma_game_host_context_pump(
                &entry->api_host,
                delta_seconds);
        dluma_native_finish_pending_cleanup(entry);
    }
    dluma_atomic_exchange(
        &g_dluma_native_mods.pump_active, 0);
}

int dluma_native_mods_config_changed(
    const char* registry_key,
    const char* approval_fingerprint,
    const char* setting_key)
{
    unsigned int index;
    int notified = 0;
    if (!registry_key || !approval_fingerprint ||
        !setting_key || !setting_key[0])
        return 0;
    for (index = 0;
         index < g_dluma_native_mods.count;
         ++index) {
        DlumaNativeModEntry* entry =
            &g_dluma_native_mods.entries[index];
        if (!entry->scope_active ||
            entry->lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY ||
            dluma_stricmp(
                entry->snapshot.registry_key,
                registry_key) != 0 ||
            dluma_stricmp(
                entry->snapshot.approval_fingerprint,
                approval_fingerprint) != 0)
            continue;
        while (dluma_atomic_compare_exchange(
                   &g_dluma_native_mods.event_lock,
                   1, 0) != 0)
            Sleep(1);
        if (g_dluma_native_mods.event_count <
            DLUMA_NATIVE_MOD_MAX_EVENTS) {
            unsigned int queue_index =
                (g_dluma_native_mods.event_head +
                 g_dluma_native_mods.event_count) %
                DLUMA_NATIVE_MOD_MAX_EVENTS;
            DlumaNativeQueuedEvent* queued =
                &g_dluma_native_mods.events[queue_index];
            memset(queued, 0, sizeof(*queued));
            queued->owner_id =
                entry->api_owner.owner_id;
            queued->kind = 10U;
            queued->map_generation =
                entry->api_owner.map_generation;
            dluma_str_copy(
                queued->name,
                DLUMA_ARRAY_COUNT(queued->name),
                setting_key);
            ++g_dluma_native_mods.event_count;
            notified = 1;
        }
        dluma_atomic_exchange(
            &g_dluma_native_mods.event_lock, 0);
    }
    return notified;
}

void dluma_native_mods_transition_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation)
{
    dluma_native_mods_deactivate_scope(map_generation);
    dluma_native_mods_activate_scope(
        project_root,
        project_name,
        map_name,
        map_generation);
}

void dluma_native_mods_shutdown_chain(HMODULE original_gamedll,
                                    int loader_callbacks_drained)
{
    int i;
    unsigned int claimed_count;
    (void)original_gamedll;
    dluma_native_lock();
    claimed_count = g_dluma_native_mods.count;
    dluma_native_unlock();
    for (i = (int)claimed_count - 1; i >= 0; --i) {
        DlumaNativeModApiV2 callbacks;
        const DlumaApiV1* api;
        DlumaScopeActivationV1 activation;
        DlumaModOwner* api_owner;
        DWORD map_generation;
        int post_init_succeeded;
        int scope_active;
        unsigned int scope;
        char active_project[128];
        char active_map[128];
        int owner_drained = 0;
        char id[128];
        DWORD epoch;
        int raised = 0;
        unsigned int index = (unsigned int)i;
        memset(&callbacks, 0, sizeof(callbacks));
        memset(&activation, 0, sizeof(activation));
        dluma_native_lock();
        if (index >= g_dluma_native_mods.count ||
            !g_dluma_native_mods.entries[index].module ||
            !g_dluma_native_mods.entries[index].snapshot.pre_init_succeeded ||
            !g_dluma_native_mods.entries[index].callbacks.shutdown ||
            g_dluma_native_mods.entries[index].lifecycle_phase !=
                DLUMA_NATIVE_PHASE_READY) {
            dluma_native_unlock();
            continue;
        }
        g_dluma_native_mods.entries[index].lifecycle_phase =
            DLUMA_NATIVE_PHASE_SHUTTING_DOWN;
        epoch = dluma_native_next_lifecycle_epoch_locked();
        g_dluma_native_mods.entries[index].lifecycle_epoch = epoch;
        callbacks = g_dluma_native_mods.entries[index].callbacks;
        api = dluma_api_runtime_api(
            &g_dluma_native_mods.entries[index].api_runtime);
        api_owner =
            &g_dluma_native_mods.entries[index].api_owner;
        map_generation = api_owner->map_generation;
        post_init_succeeded =
            g_dluma_native_mods.entries[index]
                .snapshot.post_init_succeeded;
        scope_active =
            g_dluma_native_mods.entries[index].scope_active != 0;
        scope =
            g_dluma_native_mods.entries[index]
                .package.manifest.scope;
        dluma_str_copy(
            active_project, DLUMA_ARRAY_COUNT(active_project),
            g_dluma_native_mods.entries[index].active_project);
        dluma_str_copy(
            active_map, DLUMA_ARRAY_COUNT(active_map),
            g_dluma_native_mods.entries[index].active_map);
        dluma_str_copy(
            id, DLUMA_ARRAY_COUNT(id),
            g_dluma_native_mods.entries[index].snapshot.id);
        dluma_native_unlock();

        dluma_log_u64("INFO", "native_mod_shutdown_reverse_index", (ULONGLONG)i);
        activation.struct_size = (uint32_t)sizeof(activation);
        activation.scope = scope;
        activation.map_generation = map_generation;
        activation.project =
            dluma_native_scope_string(active_project);
        activation.map =
            dluma_native_scope_string(active_map);
        if (loader_callbacks_drained && api) {
            __try {
                if (post_init_succeeded && scope_active)
                    callbacks.scope_deactivated(api, &activation);
                callbacks.shutdown(api, DLUMA_STOP_SHUTDOWN);
            }
            __except(1) { raised = 1; }
            dluma_mod_owner_begin_revoke(api_owner);
            dluma_game_host_context_revoke(
                &g_dluma_native_mods.entries[index].api_host);
            dluma_mod_owner_finish_revoke(api_owner);
            owner_drained = 1;
        }
        dluma_native_lock();
        if (index < g_dluma_native_mods.count &&
            g_dluma_native_mods.entries[index].lifecycle_phase ==
                DLUMA_NATIVE_PHASE_SHUTTING_DOWN &&
            g_dluma_native_mods.entries[index].lifecycle_epoch == epoch) {
            DlumaNativeModEntry* entry =
                &g_dluma_native_mods.entries[index];
            if (raised)
                dluma_native_set_error(
                    &entry->snapshot, "shutdown callback raised SEH");
            else if (!owner_drained)
                dluma_native_set_error(
                    &entry->snapshot,
                    "shutdown retained because callbacks did not fully drain");
            entry->lifecycle_phase = DLUMA_NATIVE_PHASE_RETAINED;
            ++g_dluma_native_mods.revision;
            dluma_native_publish_crash_context_locked();
        }
        dluma_native_unlock();
        /* An arbitrary native MOD can retain callbacks, threads, or hooks that
         * the loader cannot prove drained. Keep its handle pinned until process
         * exit instead of risking an unsafe FreeLibrary. */
        dluma_log_pair("WARN", "native_mod_retained_until_process_exit", id);
    }
}

DlumaNativeCompanionOwnerValidationResult
dluma_native_mods_validate_companion_source_owner_ex(
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaNativeCompanionOwnerValidationDiagnostic* diagnostic)
{
    DlumaNativeCompanionOwnerValidationResult result =
        DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING;
    if (diagnostic)
        memset(diagnostic, 0, sizeof(*diagnostic));
    if (!record || record->owner_kind !=
            DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK)
        return result;
    if (diagnostic)
        diagnostic->registered = record->native_admission;
    /* The generic companion-archive service publishes
     * DLUMA_SOURCE_OWNER_MOD_COMPANION_CONTENT records, which are validated by
     * the process-lifetime source classifier.  It deliberately carries no
     * native-registry admission snapshot.  A legacy
     * DLUMA_SOURCE_OWNER_NATIVE_COMPANION_PACK record therefore cannot be
     * joined to the current registry and must fail closed instead of being
     * reconstructed from mutable discovery state. */
    dluma_native_lock();
    if (diagnostic)
        diagnostic->current_native_registry_revision =
            g_dluma_native_mods.revision;
    result = record->native_admission.stable_candidate_id[0]
        ? DLUMA_COMPANION_ARCHIVE_OWNER_NATIVE_REGISTRY_ITEM_NOT_PUBLISHED
        : DLUMA_COMPANION_ARCHIVE_OWNER_RECORD_MISSING;
    if (diagnostic)
        diagnostic->result = result;
    dluma_native_unlock();
    return result;
}

int dluma_native_mods_validate_companion_source_owner(
    const DlumaProcessLifetimeSourceRecord* record)
{
    return dluma_native_mods_validate_companion_source_owner_ex(
               record, 0) ==
           DLUMA_COMPANION_ARCHIVE_OWNER_VALID;
}

int dluma_native_mods_copy_crash_context(char* out, unsigned int capacity)
{
    unsigned int attempt;
    if (!out || !capacity) return 0;
    out[0] = 0;
    for (attempt = 0; attempt < 2U; ++attempt) {
        LONG before = g_dluma_native_crash_context_sequence;
        LONG after;
        if (before & 1) continue;
        dluma_str_copy(out, capacity, g_dluma_native_crash_context);
        after = g_dluma_native_crash_context_sequence;
        if (before == after && !(after & 1))
            return out[0] != 0;
        out[0] = 0;
    }
    return 0;
}
