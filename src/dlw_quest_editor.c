#include "dlw_quest_editor.h"
#include "dlw_editor_host.h"
#include "dlw_editor_launch.h"
#include "dlw_build_identity.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"
#include "dlw_native_mods.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_quest_authoring.h"
#include "core/dlw_mod_manifest.h"

#define DLUMA_QUEST_EDITOR_TIMESTAMP 0x623395EBUL
#define DLUMA_QUEST_EDITOR_IMAGE_SIZE 0x0011F000UL
#define DLUMA_QUEST_EDITOR_SHA256 \
    "56554E8EC993B1580E51E2E4F37B934B0E1B705D8282BA0309C1EF309CF4A3EB"
#define DLUMA_QUEST_EDITOR_INIT_RVA 0x00098310UL
#define DLUMA_QUEST_EDITOR_COMPILE_RVA 0x000984A0UL
#define DLUMA_QUEST_EDITOR_DEFINITION_RVA 0x000A0230UL
#define DLUMA_QUEST_EDITOR_JUMP_SIZE 14U
#define DLUMA_QUEST_EDITOR_INIT_STOLEN 14U
#define DLUMA_QUEST_EDITOR_COMPILE_STOLEN 15U
#define DLUMA_QUEST_EDITOR_MAX_STOCK_COMMANDS 512U
#define DLUMA_QUEST_EDITOR_MAX_ALIASES \
    ((DLUMA_MAX_NATIVE_MODS + DLUMA_MAX_LUA_FILES) * \
     DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS)
#define DLUMA_QUEST_EDITOR_MAX_EDF_BYTES (4UL * 1024UL * 1024UL)
#define DLUMA_QUEST_EDITOR_MAX_STOCK_EDF_BYTES (2UL * 1024UL * 1024UL)
#define DLUMA_EDITOR_TIMESTAMP 0x623396E1UL
#define DLUMA_EDITOR_IMAGE_SIZE 0x01E4A000UL
#define DLUMA_EDITOR_LOAD_LIBRARY_IAT_RVA 0x011457D0UL
#define DLUMA_PE64_MAGIC 0x020BU
#define DLUMA_IMPORT_DIRECTORY_OFFSET 0x78UL
#define DLUMA_QUEST_EDITOR_MAX_DEFINITIONS 4096U

typedef __int64 (__fastcall* DlumaQuestEditorInitializeFn)(__int64 context);
typedef void (__fastcall* DlumaQuestEditorCompileFn)(
    __int64 context,
    const char** source_path,
    const char** output_path);
typedef __int64 (__fastcall* DlumaQuestEditorDefinitionFn)(
    __int64* compiler,
    const char** definition_path);
typedef HMODULE (WINAPI* DlumaQuestEditorLoadLibraryAFn)(LPCSTR file_name);

typedef struct DlumaQuestEditorDetour {
    BYTE* target;
    BYTE* trampoline;
    SIZE_T trampoline_size;
    unsigned int stolen_size;
    BYTE original[32];
    BYTE patch[32];
    volatile LONG installed;
} DlumaQuestEditorDetour;

typedef struct DlumaQuestEditorState {
    HMODULE module;
    void** load_library_iat_slot;
    DlumaQuestEditorLoadLibraryAFn original_load_library;
    DlumaQuestEditorDetour initialize_detour;
    DlumaQuestEditorDetour compile_detour;
    DlumaQuestEditorDefinitionFn definition_loader;
    volatile LONG start_state;
    volatile LONG load_library_hook_installed;
    volatile LONG stopping;
    volatile LONG callbacks_in_flight;
    volatile LONG edf_lock;
    volatile LONG edf_ready;
    __int64* loaded_compiler;
    WCHAR edf_path[DLUMA_MAX_WPATH];
    char edf_path_utf8[DLUMA_MAX_WPATH * 2U];
    char edf_sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaQuestPhaseAlias* accepted_aliases;
    unsigned int accepted_alias_count;
} DlumaQuestEditorState;

static DlumaQuestEditorState g_dluma_quest_editor;

static const BYTE g_dluma_quest_editor_init_prefix[DLUMA_QUEST_EDITOR_INIT_STOLEN] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x83, 0x79, 0x08, 0x00
};

static const BYTE g_dluma_quest_editor_compile_prefix[
    DLUMA_QUEST_EDITOR_COMPILE_STOLEN] = {
    0x48, 0x89, 0x5C, 0x24, 0x08,
    0x48, 0x89, 0x6C, 0x24, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x18
};

static const BYTE g_dluma_quest_editor_definition_prefix[16] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7,
    0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48
};

static __int64 __fastcall dluma_quest_editor_initialize_hook(__int64 context);
static void __fastcall dluma_quest_editor_compile_hook(
    __int64 context,
    const char** source_path,
    const char** output_path);
static HMODULE WINAPI dluma_quest_editor_load_library_hook(
    LPCSTR file_name);

static WORD dluma_quest_editor_read_u16(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    return (WORD)((WORD)value[0] | ((WORD)value[1] << 8));
}

static DWORD dluma_quest_editor_read_u32(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    return (DWORD)value[0] | ((DWORD)value[1] << 8) |
        ((DWORD)value[2] << 16) | ((DWORD)value[3] << 24);
}

static ULONGLONG dluma_quest_editor_read_u64(const void* address)
{
    const BYTE* value = (const BYTE*)address;
    ULONGLONG result = 0;
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        result |= (ULONGLONG)value[index] << (index * 8U);
    return result;
}

static int dluma_quest_editor_rva_valid(
    DWORD rva,
    SIZE_T bytes,
    DWORD image_size)
{
    return rva && rva < image_size && bytes <= image_size - rva;
}

static int dluma_quest_editor_section_is_rdata(
    BYTE* base,
    DWORD nt,
    DWORD image_size,
    DWORD rva)
{
    WORD section_count;
    WORD optional_size;
    BYTE* section;
    unsigned int index;
    if (!base || !dluma_is_readable(base + nt + 4U, 20U)) return 0;
    section_count = dluma_quest_editor_read_u16(base + nt + 6U);
    optional_size = dluma_quest_editor_read_u16(base + nt + 20U);
    if (!section_count || section_count > 96U) return 0;
    section = base + nt + 24U + optional_size;
    if (!dluma_is_readable(section, (SIZE_T)section_count * 40U)) return 0;
    for (index = 0; index < section_count; ++index, section += 40U) {
        DWORD virtual_size = dluma_quest_editor_read_u32(section + 8U);
        DWORD virtual_address = dluma_quest_editor_read_u32(section + 12U);
        DWORD raw_size = dluma_quest_editor_read_u32(section + 16U);
        DWORD span = virtual_size > raw_size ? virtual_size : raw_size;
        DWORD characteristics = dluma_quest_editor_read_u32(section + 36U);
        if (rva < virtual_address || rva - virtual_address >= span) continue;
        return section[0] == '.' && section[1] == 'r' &&
            section[2] == 'd' && section[3] == 'a' &&
            section[4] == 't' && section[5] == 'a' && !section[6] &&
            (characteristics & 0x40000000UL) != 0 &&
            (characteristics & (0x80000000UL | 0x20000000UL)) == 0 &&
            dluma_quest_editor_rva_valid(
                rva, sizeof(void*), image_size);
    }
    return 0;
}

static void** dluma_quest_editor_find_load_library_iat(void)
{
    BYTE* base = (BYTE*)GetModuleHandleW(0);
    DWORD nt;
    DWORD timestamp;
    DWORD image_size;
    DWORD import_rva;
    DWORD import_size;
    BYTE* descriptor;
    HMODULE kernel;
    FARPROC expected;
    if (!base || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_quest_editor_read_u32(base + 0x3CU);
    if (nt > 0x100000UL || !dluma_is_readable(base + nt, 0x108U) ||
        base[nt] != 'P' || base[nt + 1U] != 'E' ||
        dluma_quest_editor_read_u16(base + nt + 24U) != DLUMA_PE64_MAGIC)
        return 0;
    timestamp = dluma_quest_editor_read_u32(base + nt + 8U);
    image_size = dluma_quest_editor_read_u32(base + nt + 0x50U);
    if (timestamp != DLUMA_EDITOR_TIMESTAMP ||
        image_size != DLUMA_EDITOR_IMAGE_SIZE ||
        !dluma_quest_editor_section_is_rdata(
            base, nt, image_size, DLUMA_EDITOR_LOAD_LIBRARY_IAT_RVA))
        return 0;
    import_rva = dluma_quest_editor_read_u32(
        base + nt + 24U + DLUMA_IMPORT_DIRECTORY_OFFSET);
    import_size = dluma_quest_editor_read_u32(
        base + nt + 24U + DLUMA_IMPORT_DIRECTORY_OFFSET + 4U);
    if (!dluma_quest_editor_rva_valid(import_rva, 20U, image_size) ||
        import_size < 20U || import_size > image_size - import_rva)
        return 0;
    kernel = GetModuleHandleW(L"kernel32.dll");
    expected = kernel ? GetProcAddress(kernel, "LoadLibraryA") : 0;
    if (!expected) return 0;
    descriptor = base + import_rva;
    while ((SIZE_T)(descriptor - (base + import_rva)) + 20U <=
           import_size) {
        DWORD original_thunk = dluma_quest_editor_read_u32(descriptor);
        DWORD name_rva = dluma_quest_editor_read_u32(descriptor + 12U);
        DWORD first_thunk = dluma_quest_editor_read_u32(descriptor + 16U);
        const char* library;
        unsigned int index;
        if (!original_thunk && !first_thunk) break;
        if (!dluma_quest_editor_rva_valid(name_rva, 13U, image_size) ||
            !dluma_quest_editor_rva_valid(original_thunk, 8U, image_size) ||
            !dluma_quest_editor_rva_valid(first_thunk, 8U, image_size)) {
            descriptor += 20U;
            continue;
        }
        library = (const char*)(base + name_rva);
        if (!dluma_is_readable(library, 13U) ||
            dluma_stricmp(library, "KERNEL32.dll") != 0) {
            descriptor += 20U;
            continue;
        }
        for (index = 0; ; ++index) {
            DWORD oft_rva = original_thunk + index * 8U;
            DWORD iat_rva = first_thunk + index * 8U;
            ULONGLONG thunk;
            DWORD import_name_rva;
            const char* import_name;
            void** slot;
            if (!dluma_quest_editor_rva_valid(oft_rva, 8U, image_size) ||
                !dluma_quest_editor_rva_valid(iat_rva, 8U, image_size))
                break;
            thunk = dluma_quest_editor_read_u64(base + oft_rva);
            if (!thunk) break;
            if ((thunk & 0x8000000000000000ULL) ||
                thunk > 0xFFFFFFFFULL)
                continue;
            import_name_rva = (DWORD)thunk;
            if (!dluma_quest_editor_rva_valid(
                    import_name_rva, 15U, image_size))
                continue;
            import_name = (const char*)(base + import_name_rva + 2U);
            if (!dluma_is_readable(import_name, 13U) ||
                dluma_stricmp(import_name, "LoadLibraryA") != 0)
                continue;
            if (iat_rva != DLUMA_EDITOR_LOAD_LIBRARY_IAT_RVA) return 0;
            slot = (void**)(base + iat_rva);
            return dluma_is_readable(slot, sizeof(void*)) &&
                *slot == (void*)expected ? slot : 0;
        }
        descriptor += 20U;
    }
    return 0;
}

static void dluma_quest_editor_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_quest_editor.edf_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_quest_editor_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_quest_editor.edf_lock, 0);
}

static void dluma_quest_editor_absolute_jump(BYTE* output, const void* target)
{
    ULONG_PTR destination = (ULONG_PTR)target;
    output[0] = 0xFF;
    output[1] = 0x25;
    output[2] = 0;
    output[3] = 0;
    output[4] = 0;
    output[5] = 0;
    memcpy(output + 6U, &destination, sizeof(destination));
}

static int dluma_quest_editor_target_section(
    BYTE* module,
    DWORD image_size,
    DWORD rva,
    SIZE_T size)
{
    DWORD nt;
    WORD section_count;
    WORD optional_size;
    BYTE* section;
    unsigned int index;
    if (!module || !rva || rva >= image_size || size > image_size - rva ||
        !dluma_is_readable(module, 0x100U) || module[0] != 'M' ||
        module[1] != 'Z')
        return 0;
    nt = *(const DWORD*)(module + 0x3CU);
    if (nt > 0x100000UL || !dluma_is_readable(module + nt, 0x108U) ||
        module[nt] != 'P' || module[nt + 1U] != 'E')
        return 0;
    section_count = *(const WORD*)(module + nt + 6U);
    optional_size = *(const WORD*)(module + nt + 20U);
    if (!section_count || section_count > 96U) return 0;
    section = module + nt + 24U + optional_size;
    if (!dluma_is_readable(section, (SIZE_T)section_count * 40U)) return 0;
    for (index = 0; index < section_count; ++index, section += 40U) {
        DWORD virtual_size = *(const DWORD*)(section + 8U);
        DWORD virtual_address = *(const DWORD*)(section + 12U);
        DWORD raw_size = *(const DWORD*)(section + 16U);
        DWORD span = virtual_size > raw_size ? virtual_size : raw_size;
        DWORD characteristics = *(const DWORD*)(section + 36U);
        if (rva < virtual_address || rva - virtual_address >= span) continue;
        return section[0] == '.' && section[1] == 't' &&
            section[2] == 'e' && section[3] == 'x' && section[4] == 't' &&
            !section[5] && (characteristics & 0x60000000UL) == 0x60000000UL &&
            (characteristics & 0x80000000UL) == 0;
    }
    return 0;
}

static int dluma_quest_editor_validate_module(HMODULE module)
{
    BYTE* base = (BYTE*)module;
    BYTE disk_header[4096];
    DWORD loaded_timestamp = 0;
    DWORD loaded_image_size = 0;
    DWORD disk_timestamp = 0;
    DWORD disk_image_size = 0;
    DWORD read = 0;
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR editor_path[DLUMA_MAX_WPATH];
    WCHAR expected_path[DLUMA_MAX_WPATH];
    WCHAR expected_final[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    BY_HANDLE_FILE_INFORMATION expected_info;
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE expected_file = INVALID_HANDLE_VALUE;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    LARGE_INTEGER zero;
    int result = 0;
    memset(&info, 0, sizeof(info));
    memset(&expected_info, 0, sizeof(expected_info));
    zero.QuadPart = 0;
    if (!module || !dluma_build_identity_parse_pe_header(
            base, 4096U, &loaded_timestamp, &loaded_image_size) ||
        loaded_timestamp != DLUMA_QUEST_EDITOR_TIMESTAMP ||
        loaded_image_size != DLUMA_QUEST_EDITOR_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            module, module_path, DLUMA_ARRAY_COUNT(module_path))) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "loaded module identity or path");
        return 0;
    }
    file = CreateFileW(
        module_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "module file open");
        return 0;
    }
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path))) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "module file identity");
        goto done;
    }
    if (!dluma_get_module_path_checked(
            GetModuleHandleW(0), editor_path,
            DLUMA_ARRAY_COUNT(editor_path))) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "Editor executable path");
        goto done;
    }
    dluma_path_parent(editor_path);
    if (!editor_path[0] || !dluma_checked_path_join(
            expected_path, DLUMA_ARRAY_COUNT(expected_path), editor_path,
            L"Plugins_x64_rwdi\\QuestEditor.dll")) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "expected plugin path construction");
        goto done;
    }
    expected_file = CreateFileW(
        expected_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (expected_file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(expected_file, &expected_info) ||
        (expected_info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        expected_info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            expected_file, expected_final,
            DLUMA_ARRAY_COUNT(expected_final)) ||
        dluma_wstricmp(final_path, expected_final) != 0 ||
        info.dwVolumeSerialNumber != expected_info.dwVolumeSerialNumber ||
        info.nFileIndexHigh != expected_info.nFileIndexHigh ||
        info.nFileIndexLow != expected_info.nFileIndexLow) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "loaded module provenance");
        goto done;
    }
    if (!dluma_sha256_handle_hex(
            file, 64ULL * 1024ULL * 1024ULL, sha256) ||
        dluma_stricmp(sha256, DLUMA_QUEST_EDITOR_SHA256) != 0) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "module file hash");
        goto done;
    }
    if (!SetFilePointerEx(file, zero, 0, FILE_BEGIN) ||
        !ReadFile(file, disk_header, sizeof(disk_header), &read, 0) ||
        !dluma_build_identity_parse_pe_header(
            disk_header, read, &disk_timestamp, &disk_image_size) ||
        disk_timestamp != loaded_timestamp ||
        disk_image_size != loaded_image_size) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "disk and loaded PE identity");
        goto done;
    }
    if (!dluma_quest_editor_target_section(
            base, loaded_image_size, DLUMA_QUEST_EDITOR_INIT_RVA,
            sizeof(g_dluma_quest_editor_init_prefix)) ||
        !dluma_quest_editor_target_section(
            base, loaded_image_size, DLUMA_QUEST_EDITOR_COMPILE_RVA,
            sizeof(g_dluma_quest_editor_compile_prefix)) ||
        !dluma_quest_editor_target_section(
            base, loaded_image_size, DLUMA_QUEST_EDITOR_DEFINITION_RVA,
            sizeof(g_dluma_quest_editor_definition_prefix))) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "target section ownership");
        goto done;
    }
    if (memcmp(base + DLUMA_QUEST_EDITOR_INIT_RVA,
               g_dluma_quest_editor_init_prefix,
               sizeof(g_dluma_quest_editor_init_prefix)) != 0 ||
        memcmp(base + DLUMA_QUEST_EDITOR_COMPILE_RVA,
               g_dluma_quest_editor_compile_prefix,
               sizeof(g_dluma_quest_editor_compile_prefix)) != 0 ||
        memcmp(base + DLUMA_QUEST_EDITOR_DEFINITION_RVA,
               g_dluma_quest_editor_definition_prefix,
               sizeof(g_dluma_quest_editor_definition_prefix)) != 0) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "target instruction prefixes");
        goto done;
    }
    result = 1;
done:
    if (expected_file != INVALID_HANDLE_VALUE) CloseHandle(expected_file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static int dluma_quest_editor_detour_install(
    DlumaQuestEditorDetour* state,
    BYTE* target,
    const BYTE* prefix,
    unsigned int stolen_size,
    const void* hook)
{
    BYTE tail[DLUMA_QUEST_EDITOR_JUMP_SIZE];
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    DWORD trampoline_old = 0;
    if (!state || !target || !prefix || !hook ||
        stolen_size < DLUMA_QUEST_EDITOR_JUMP_SIZE ||
        stolen_size > sizeof(state->original) ||
        memcmp(target, prefix, stolen_size) != 0)
        return 0;
    memset(state, 0, sizeof(*state));
    state->trampoline_size = stolen_size + sizeof(tail);
    state->trampoline = (BYTE*)VirtualAlloc(
        0, state->trampoline_size, MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!state->trampoline) return 0;
    memcpy(state->original, prefix, stolen_size);
    memcpy(state->trampoline, prefix, stolen_size);
    dluma_quest_editor_absolute_jump(tail, target + stolen_size);
    memcpy(state->trampoline + stolen_size, tail, sizeof(tail));
    if (!VirtualProtect(
            state->trampoline, state->trampoline_size,
            PAGE_EXECUTE_READ, &trampoline_old))
        goto failed;
    FlushInstructionCache(
        GetCurrentProcess(), state->trampoline,
        state->trampoline_size);
    memset(state->patch, 0x90, stolen_size);
    dluma_quest_editor_absolute_jump(state->patch, hook);
    if (!VirtualProtect(
            target, stolen_size, PAGE_EXECUTE_READWRITE,
            &old_protect) ||
        memcmp(target, prefix, stolen_size) != 0)
        goto failed;
    memcpy(target, state->patch, stolen_size);
    if (!VirtualProtect(
            target, stolen_size, old_protect,
            &restored_protect)) {
        memcpy(target, state->original, stolen_size);
        (void)VirtualProtect(
            target, stolen_size, old_protect,
            &restored_protect);
        goto failed;
    }
    FlushInstructionCache(GetCurrentProcess(), target, stolen_size);
    state->target = target;
    state->stolen_size = stolen_size;
    dluma_atomic_exchange(&state->installed, 1);
    return 1;
failed:
    if (state->trampoline) {
        (void)VirtualFree(state->trampoline, 0, MEM_RELEASE);
        state->trampoline = 0;
    }
    return 0;
}

static int dluma_quest_editor_detour_restore(
    DlumaQuestEditorDetour* state)
{
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    if (!state || !dluma_atomic_compare_exchange(
            &state->installed, 0, 0))
        return 1;
    if (!state->target ||
        memcmp(state->target, state->patch, state->stolen_size) != 0 ||
        !VirtualProtect(
            state->target, state->stolen_size,
            PAGE_EXECUTE_READWRITE, &old_protect))
        return 0;
    memcpy(state->target, state->original, state->stolen_size);
    if (!VirtualProtect(
            state->target, state->stolen_size,
            old_protect, &restored_protect))
        return 0;
    FlushInstructionCache(
        GetCurrentProcess(), state->target, state->stolen_size);
    dluma_atomic_exchange(&state->installed, 0);
    return 1;
}

static unsigned int dluma_quest_editor_raw_alias_snapshot(
    DlumaQuestPhaseAlias* output,
    unsigned int capacity)
{
    DlumaNativeModSnapshot* mods;
    unsigned int mod_count;
    unsigned int mod_index;
    unsigned int count = 0;
    if (!output || !capacity) return 0;
    mods = (DlumaNativeModSnapshot*)dluma_alloc(
        DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
    if (!mods) return 0;
    memset(mods, 0, DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
    mod_count = dluma_native_mods_snapshot(mods, DLUMA_MAX_NATIVE_MODS, 0);
    for (mod_index = 0; mod_index < mod_count; ++mod_index) {
        const DlumaNativeModSnapshot* mod = &mods[mod_index];
        unsigned int handler_index;
        if (mod->kind != DLUMA_MOD_KIND_NATIVE_DLL ||
            mod->duplicate_id || !mod->manifest_present ||
            !(mod->declared_host_flags & DLUMA_NATIVE_HOST_PLAYER) ||
            !mod->id[0])
            continue;
        for (handler_index = 0;
             handler_index < mod->quest_phase_handler_count;
             ++handler_index) {
            const DlumaNativeQuestPhaseHandlerSnapshot* handler =
                &mod->quest_phase_handlers[handler_index];
            if (!handler->command_alias[0]) continue;
            if (count >= capacity) {
                dluma_free(mods);
                return 0;
            }
            memset(&output[count], 0, sizeof(output[count]));
            dluma_str_copy(output[count].command,
                         DLUMA_ARRAY_COUNT(output[count].command),
                         handler->command_alias);
            dluma_str_copy(output[count].provider_mod,
                         DLUMA_ARRAY_COUNT(output[count].provider_mod),
                         mod->id);
            dluma_str_copy(output[count].handler_id,
                         DLUMA_ARRAY_COUNT(output[count].handler_id),
                         handler->id);
            output[count].runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
            output[count].load_priority = mod->load_priority;
            ++count;
        }
    }
    dluma_free(mods);
    {
        DlumaLuaQuestPhaseProviderMetadata* providers =
            (DlumaLuaQuestPhaseProviderMetadata*)dluma_alloc(
                DLUMA_MAX_LUA_FILES * sizeof(*providers));
        unsigned int provider_count;
        unsigned int provider_index;
        if (!providers) return count;
        memset(providers, 0,
               DLUMA_MAX_LUA_FILES * sizeof(*providers));
        provider_count = dluma_lua_snapshot_editor_quest_phase_providers(
            providers, DLUMA_MAX_LUA_FILES);
        if (provider_count > DLUMA_MAX_LUA_FILES) {
            dluma_free(providers);
            return count;
        }
        for (provider_index = 0;
             provider_index < provider_count;
             ++provider_index) {
            const DlumaLuaQuestPhaseProviderMetadata* provider =
                &providers[provider_index];
            unsigned int handler_index;
            if (provider->duplicate_id ||
                !(provider->declared_host_flags & DLUMA_MOD_HOST_PLAYER))
                continue;
            for (handler_index = 0;
                 handler_index < provider->quest_phase_handler_count;
                 ++handler_index) {
                const DlumaLuaQuestPhaseHandlerMetadata* handler =
                    &provider->quest_phase_handlers[handler_index];
                if (!handler->command_alias[0]) continue;
                if (count >= capacity) {
                    dluma_free(providers);
                    return count;
                }
                memset(&output[count], 0, sizeof(output[count]));
                dluma_str_copy(output[count].command,
                             DLUMA_ARRAY_COUNT(output[count].command),
                             handler->command_alias);
                dluma_str_copy(output[count].provider_mod,
                             DLUMA_ARRAY_COUNT(output[count].provider_mod),
                             provider->id);
                dluma_str_copy(output[count].handler_id,
                             DLUMA_ARRAY_COUNT(output[count].handler_id),
                             handler->id);
                output[count].runtime_kind =
                    DLUMA_QUEST_PHASE_RUNTIME_LUA;
                output[count].load_priority = provider->load_priority;
                ++count;
            }
        }
        dluma_free(providers);
    }
    return count;
}

unsigned int dluma_quest_editor_alias_snapshot(
    DlumaQuestPhaseAlias* output,
    unsigned int capacity)
{
    unsigned int count;
    if (!output || !capacity) return 0;
    dluma_quest_editor_lock();
    count = g_dluma_quest_editor.accepted_alias_count;
    if (count > capacity || (count && !g_dluma_quest_editor.accepted_aliases)) {
        dluma_quest_editor_unlock();
        return 0;
    }
    if (count)
        memcpy(output, g_dluma_quest_editor.accepted_aliases,
               count * sizeof(*output));
    dluma_quest_editor_unlock();
    return count;
}

int dluma_quest_editor_handler_declared(
    const char* provider_mod,
    const char* handler_id,
    unsigned int runtime_kind)
{
    DlumaNativeModSnapshot* mods;
    unsigned int mod_count;
    unsigned int mod_index;
    int declared = 0;
    if (!provider_mod || !handler_id ||
        (runtime_kind != DLUMA_QUEST_PHASE_RUNTIME_NATIVE &&
         runtime_kind != DLUMA_QUEST_PHASE_RUNTIME_LUA))
        return 0;
    if (runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA) {
        DlumaLuaQuestPhaseProviderMetadata* providers =
            (DlumaLuaQuestPhaseProviderMetadata*)dluma_alloc(
                DLUMA_MAX_LUA_FILES * sizeof(*providers));
        unsigned int provider_count;
        unsigned int provider_index;
        if (!providers) return 0;
        memset(providers, 0,
               DLUMA_MAX_LUA_FILES * sizeof(*providers));
        provider_count = dluma_lua_snapshot_editor_quest_phase_providers(
            providers, DLUMA_MAX_LUA_FILES);
        if (provider_count > DLUMA_MAX_LUA_FILES)
            provider_count = 0;
        for (provider_index = 0;
             provider_index < provider_count && !declared;
             ++provider_index) {
            const DlumaLuaQuestPhaseProviderMetadata* provider =
                &providers[provider_index];
            unsigned int handler_index;
            if (provider->duplicate_id ||
                dluma_stricmp(provider->id, provider_mod) != 0 ||
                !(provider->declared_host_flags & DLUMA_MOD_HOST_PLAYER))
                continue;
            for (handler_index = 0;
                 handler_index < provider->quest_phase_handler_count;
                 ++handler_index)
                if (dluma_stricmp(
                        provider->quest_phase_handlers[handler_index].id,
                        handler_id) == 0) {
                    declared = 1;
                    break;
                }
        }
        dluma_free(providers);
        return declared;
    }
    mods = (DlumaNativeModSnapshot*)dluma_alloc(
        DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
    if (!mods) return 0;
    memset(mods, 0, DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
    mod_count = dluma_native_mods_snapshot(mods, DLUMA_MAX_NATIVE_MODS, 0);
    for (mod_index = 0; mod_index < mod_count && !declared; ++mod_index) {
        const DlumaNativeModSnapshot* mod = &mods[mod_index];
        unsigned int handler_index;
        if (mod->kind != DLUMA_MOD_KIND_NATIVE_DLL ||
            mod->duplicate_id || !mod->manifest_present ||
            dluma_stricmp(mod->id, provider_mod) != 0 ||
            !(mod->declared_host_flags & DLUMA_NATIVE_HOST_PLAYER))
            continue;
        for (handler_index = 0;
             handler_index < mod->quest_phase_handler_count;
             ++handler_index)
            if (dluma_stricmp(
                    mod->quest_phase_handlers[handler_index].id,
                    handler_id) == 0) {
                declared = 1;
                break;
            }
    }
    dluma_free(mods);
    return declared;
}

int dluma_quest_editor_provider_diagnostic(
    const char* provider_mod,
    unsigned int runtime_kind,
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY],
    char artifact_sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    if (!provider_mod || !provider_mod[0] || !manifest_sha256 ||
        !artifact_sha256)
        return 0;
    manifest_sha256[0] = 0;
    artifact_sha256[0] = 0;
    if (runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA) {
        DlumaLuaQuestPhaseProviderMetadata* providers =
            (DlumaLuaQuestPhaseProviderMetadata*)dluma_alloc(
                DLUMA_MAX_LUA_FILES * sizeof(*providers));
        unsigned int count;
        unsigned int index;
        int found = 0;
        if (!providers) return 0;
        memset(providers, 0, DLUMA_MAX_LUA_FILES * sizeof(*providers));
        count = dluma_lua_snapshot_editor_quest_phase_providers(
            providers, DLUMA_MAX_LUA_FILES);
        if (count > DLUMA_MAX_LUA_FILES) count = 0;
        for (index = 0; index < count; ++index) {
            const DlumaLuaQuestPhaseProviderMetadata* provider =
                &providers[index];
            if (provider->duplicate_id ||
                dluma_stricmp(provider->id, provider_mod) != 0 ||
                !(provider->declared_host_flags & DLUMA_MOD_HOST_PLAYER) ||
                !provider->manifest_sha256[0] ||
                !provider->content_sha256[0])
                continue;
            dluma_str_copy(
                manifest_sha256, DLUMA_SHA256_HEX_CAPACITY,
                provider->manifest_sha256);
            dluma_str_copy(
                artifact_sha256, DLUMA_SHA256_HEX_CAPACITY,
                provider->content_sha256);
            found = 1;
            break;
        }
        dluma_free(providers);
        return found;
    }
    if (runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_NATIVE) {
        DlumaNativeModSnapshot* mods =
            (DlumaNativeModSnapshot*)dluma_alloc(
                DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
        unsigned int count;
        unsigned int index;
        int found = 0;
        if (!mods) return 0;
        memset(mods, 0, DLUMA_MAX_NATIVE_MODS * sizeof(*mods));
        count = dluma_native_mods_snapshot(mods, DLUMA_MAX_NATIVE_MODS, 0);
        if (count > DLUMA_MAX_NATIVE_MODS) count = 0;
        for (index = 0; index < count; ++index) {
            const DlumaNativeModSnapshot* mod = &mods[index];
            if (mod->kind != DLUMA_MOD_KIND_NATIVE_DLL ||
                mod->duplicate_id ||
                dluma_stricmp(mod->id, provider_mod) != 0 ||
                !mod->manifest_present ||
                !(mod->declared_host_flags & DLUMA_NATIVE_HOST_PLAYER) ||
                !mod->manifest_sha256[0] || !mod->module_sha256[0])
                continue;
            dluma_str_copy(
                manifest_sha256, DLUMA_SHA256_HEX_CAPACITY,
                mod->manifest_sha256);
            dluma_str_copy(
                artifact_sha256, DLUMA_SHA256_HEX_CAPACITY,
                mod->module_sha256);
            found = 1;
            break;
        }
        dluma_free(mods);
        return found;
    }
    return 0;
}

static int dluma_quest_editor_stock_underlay(
    WCHAR* output,
    unsigned int capacity,
    DlumaDirectoryIdentity* identity)
{
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR editor_root[DLUMA_MAX_WPATH];
    if (!output || !capacity || !identity ||
        !g_dluma_quest_editor.module ||
        !dluma_get_module_path_checked(
            g_dluma_quest_editor.module, module_path,
            DLUMA_ARRAY_COUNT(module_path)))
        return 0;
    dluma_wstr_copy(
        editor_root, DLUMA_ARRAY_COUNT(editor_root), module_path);
    dluma_path_parent(editor_root); /* Plugins_x64_rwdi */
    dluma_path_parent(editor_root); /* Editor installation */
    if (!editor_root[0] ||
        !dluma_checked_path_join(
            output, capacity, editor_root,
            L"DW\\Data\\Quests\\_underlay") ||
        !dluma_open_directory_identity(output, identity))
        return 0;
    dluma_wstr_copy(output, capacity, identity->final_path);
    return output[0] != 0;
}

static int dluma_quest_editor_resolve_stock_path(
    const char* configured,
    const WCHAR* underlay,
    WCHAR* output,
    unsigned int capacity)
{
    WCHAR leaf[128];
    unsigned int index;
    if (!configured || !configured[0] || !underlay || !output ||
        (dluma_stricmp(configured, "quest-shortcuts.edf") != 0 &&
         dluma_stricmp(configured, "quest-macros-public.edf") != 0 &&
         dluma_stricmp(configured, "quest-macros-private.edf") != 0))
        return 0;
    for (index = 0; configured[index]; ++index)
        if (configured[index] == '/' || configured[index] == '\\' ||
            configured[index] == ':')
            return 0;
    return dluma_utf8_to_wide(
               configured, leaf, DLUMA_ARRAY_COUNT(leaf)) &&
        dluma_checked_path_join(
            output, capacity, underlay, leaf);
}

static int dluma_quest_editor_read_stock_commands(
    const WCHAR* path,
    const DlumaDirectoryIdentity* underlay,
    char (*commands)[64],
    unsigned int* count)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION information;
    LARGE_INTEGER size;
    WCHAR final_path[DLUMA_MAX_WPATH];
    BYTE* bytes = 0;
    DWORD read = 0;
    unsigned int cursor = 0;
    int result = 0;
    memset(&information, 0, sizeof(information));
    if (!path || !path[0] || !underlay || !commands || !count)
        return 0;
    file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, 0,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_final_path_equal_or_under(
            final_path, underlay->final_path) ||
        !GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 ||
        size.QuadPart > DLUMA_QUEST_EDITOR_MAX_STOCK_EDF_BYTES)
        goto done;
    bytes = (BYTE*)dluma_alloc((SIZE_T)size.QuadPart + 1U);
    if (!bytes || (size.QuadPart &&
        (!ReadFile(file, bytes, (DWORD)size.QuadPart, &read, 0) ||
         read != (DWORD)size.QuadPart)))
        goto done;
    bytes[(DWORD)size.QuadPart] = 0;
    while (cursor < (unsigned int)size.QuadPart) {
        unsigned int line_end = cursor;
        unsigned int token;
        unsigned int token_size = 0;
        while (line_end < (unsigned int)size.QuadPart &&
               bytes[line_end] != '\r' && bytes[line_end] != '\n')
            ++line_end;
        token = cursor;
        while (token < line_end &&
               (bytes[token] == ' ' || bytes[token] == '\t')) ++token;
        if (token < line_end && bytes[token] == ',') {
            unsigned int existing;
            ++token;
            while (token + token_size < line_end) {
                BYTE current = bytes[token + token_size];
                if (!((current >= 'A' && current <= 'Z') ||
                      (current >= 'a' && current <= 'z') ||
                      (current >= '0' && current <= '9') ||
                      current == '_' || current == '-'))
                    break;
                if (token_size + 1U >= 64U) goto done;
                ++token_size;
            }
            if (token_size && token + token_size < line_end &&
                (bytes[token + token_size] == ' ' ||
                 bytes[token + token_size] == '\t')) {
                char command[64];
                for (existing = 0; existing < token_size; ++existing) {
                    char value = (char)bytes[token + existing];
                    command[existing] = value >= 'A' && value <= 'Z'
                        ? (char)(value + ('a' - 'A')) : value;
                }
                command[token_size] = 0;
                for (existing = 0; existing < *count; ++existing)
                    if (dluma_stricmp(commands[existing], command) == 0)
                        break;
                if (existing == *count) {
                    if (*count >= DLUMA_QUEST_EDITOR_MAX_STOCK_COMMANDS)
                        goto done;
                    dluma_str_copy(commands[*count], 64U, command);
                    ++*count;
                }
            }
        }
        cursor = line_end;
        while (cursor < (unsigned int)size.QuadPart &&
               (bytes[cursor] == '\r' || bytes[cursor] == '\n')) ++cursor;
    }
    result = 1;
done:
    if (bytes) dluma_free(bytes);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static int dluma_quest_editor_write_edf(
    const char* const stock_paths[3])
{
    char (*stock_commands)[64] = 0;
    const char** stock_command_ptrs = 0;
    DlumaQuestPhaseAlias* aliases = 0;
    unsigned char* accepted = 0;
    char* edf = 0;
    unsigned int stock_count = 0;
    unsigned int alias_count;
    unsigned int edf_size = 0;
    unsigned int index;
    WCHAR runtime[DLUMA_MAX_WPATH];
    WCHAR editor_directory[DLUMA_MAX_WPATH];
    WCHAR stock_underlay[DLUMA_MAX_WPATH];
    WCHAR stock_path[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity stock_underlay_identity;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    DlumaQuestPhaseDiagnostic diagnostic;
    int result = 0;
    memset(&stock_underlay_identity, 0, sizeof(stock_underlay_identity));
    temporary[0] = 0;
    stock_commands = (char (*)[64])dluma_alloc(
        DLUMA_QUEST_EDITOR_MAX_STOCK_COMMANDS * 64U);
    stock_command_ptrs = (const char**)dluma_alloc(
        DLUMA_QUEST_EDITOR_MAX_STOCK_COMMANDS * sizeof(*stock_command_ptrs));
    aliases = (DlumaQuestPhaseAlias*)dluma_alloc(
        DLUMA_QUEST_EDITOR_MAX_ALIASES * sizeof(*aliases));
    accepted = (unsigned char*)dluma_alloc(DLUMA_QUEST_EDITOR_MAX_ALIASES);
    edf = (char*)dluma_alloc(DLUMA_QUEST_EDITOR_MAX_EDF_BYTES);
    if (!stock_commands || !stock_command_ptrs || !aliases || !accepted ||
        !edf)
        goto done;
    memset(stock_commands, 0,
           DLUMA_QUEST_EDITOR_MAX_STOCK_COMMANDS * 64U);
    memset(aliases, 0,
           DLUMA_QUEST_EDITOR_MAX_ALIASES * sizeof(*aliases));
    memset(accepted, 0, DLUMA_QUEST_EDITOR_MAX_ALIASES);
    if (!dluma_quest_editor_stock_underlay(
            stock_underlay, DLUMA_ARRAY_COUNT(stock_underlay),
            &stock_underlay_identity))
        goto done;
    for (index = 0; index < 3U; ++index)
        if (!dluma_quest_editor_resolve_stock_path(
                stock_paths[index], stock_underlay,
                stock_path, DLUMA_ARRAY_COUNT(stock_path)) ||
            !dluma_quest_editor_read_stock_commands(
                stock_path, &stock_underlay_identity,
                stock_commands, &stock_count))
            goto done;
    for (index = 0; index < stock_count; ++index)
        stock_command_ptrs[index] = stock_commands[index];
    alias_count = dluma_quest_editor_raw_alias_snapshot(
        aliases, DLUMA_QUEST_EDITOR_MAX_ALIASES);
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!dluma_quest_phase_aliases_validate(
            aliases, alias_count, stock_command_ptrs,
            stock_count, accepted, &diagnostic) ||
        !dluma_quest_phase_build_edf(
            aliases, accepted, alias_count, edf,
            DLUMA_QUEST_EDITOR_MAX_EDF_BYTES, &edf_size))
        goto done;
    {
        unsigned int accepted_count = 0;
        DlumaQuestPhaseAlias* accepted_aliases = 0;
        for (index = 0; index < alias_count; ++index)
            if (accepted[index]) ++accepted_count;
        if (accepted_count) {
            unsigned int write_index = 0;
            accepted_aliases = (DlumaQuestPhaseAlias*)dluma_alloc(
                accepted_count * sizeof(*accepted_aliases));
            if (!accepted_aliases) goto done;
            for (index = 0; index < alias_count; ++index)
                if (accepted[index])
                    accepted_aliases[write_index++] = aliases[index];
        }
        if (g_dluma_quest_editor.accepted_aliases)
            dluma_free(g_dluma_quest_editor.accepted_aliases);
        g_dluma_quest_editor.accepted_aliases = accepted_aliases;
        g_dluma_quest_editor.accepted_alias_count = accepted_count;
    }
    dluma_path_join(runtime, DLUMA_ARRAY_COUNT(runtime),
                  g_dluma.module_dir, L"dluma_runtime");
    if (!dluma_mkdirs(runtime)) goto done;
    dluma_path_join(editor_directory, DLUMA_ARRAY_COUNT(editor_directory),
                  runtime, L"editor");
    if (!dluma_mkdirs(editor_directory)) goto done;
    dluma_path_join(g_dluma_quest_editor.edf_path,
                  DLUMA_ARRAY_COUNT(g_dluma_quest_editor.edf_path),
                  editor_directory, L"quest_extensions.edf");
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary),
                  g_dluma_quest_editor.edf_path);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp");
    file = CreateFileW(
        temporary, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        (edf_size && (!WriteFile(file, edf, edf_size, &written, 0) ||
                      written != edf_size)) ||
        !FlushFileBuffers(file))
        goto done;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(
            temporary, g_dluma_quest_editor.edf_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ||
        !dluma_wide_to_utf8(
            g_dluma_quest_editor.edf_path,
            g_dluma_quest_editor.edf_path_utf8,
            DLUMA_ARRAY_COUNT(g_dluma_quest_editor.edf_path_utf8)))
        goto done;
    dluma_sha256_bytes_hex(edf, edf_size,
                         g_dluma_quest_editor.edf_sha256);
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!result && temporary[0]) DeleteFileW(temporary);
    if (stock_commands) dluma_free(stock_commands);
    if (stock_command_ptrs) dluma_free(stock_command_ptrs);
    if (aliases) dluma_free(aliases);
    if (accepted) dluma_free(accepted);
    if (edf) dluma_free(edf);
    return result;
}

static const char* dluma_quest_editor_bounded_path(const char* value);

static const char* dluma_quest_editor_context_path(
    __int64 context,
    unsigned int offset)
{
    const char* value;
    if (!context || !dluma_is_readable(
            (const void*)(context + offset), sizeof(void*)))
        return 0;
    value = *(const char**)(context + offset);
    return dluma_quest_editor_bounded_path(value);
}

static const char* dluma_quest_editor_bounded_path(const char* value)
{
    unsigned int size = 0;
    if (!value) return 0;
    while (size < DLUMA_MAX_WPATH * 2U &&
           dluma_is_readable(value + size, 1U) && value[size])
        ++size;
    return size && size < DLUMA_MAX_WPATH * 2U ? value : 0;
}

static int dluma_quest_editor_resolve_compile_path(
    const char* configured,
    const WCHAR* project_root,
    char* output,
    unsigned int output_capacity)
{
    char normalized[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    WCHAR wide[DLUMA_MAX_WPATH];
    WCHAR relative[DLUMA_MAX_WPATH];
    unsigned int index;
    int absolute;
    if (!configured || !configured[0] || !project_root ||
        !project_root[0] || !output || !output_capacity ||
        !dluma_utf8_to_wide(
            configured, wide, DLUMA_ARRAY_COUNT(wide)))
        return 0;
    for (index = 0; wide[index]; ++index)
        if (wide[index] == L'/') wide[index] = L'\\';
    absolute = (wide[0] && wide[1] == L':') ||
        (wide[0] == L'\\' && wide[1] == L'\\');
    if (!absolute) {
        if (!dluma_mod_relative_path_validate_and_normalize(
                configured, normalized, DLUMA_ARRAY_COUNT(normalized)) ||
            !dluma_utf8_to_wide(
                normalized, relative, DLUMA_ARRAY_COUNT(relative)) ||
            !dluma_checked_path_join(
                wide, DLUMA_ARRAY_COUNT(wide), project_root, relative))
            return 0;
        for (index = 0; wide[index]; ++index)
            if (wide[index] == L'/') wide[index] = L'\\';
    }
    return dluma_wide_to_utf8(wide, output, output_capacity);
}

static int dluma_quest_editor_resolve_compile_paths(
    const char* source_path,
    const char* output_path,
    WCHAR* project_root,
    unsigned int project_capacity,
    char* source_absolute,
    unsigned int source_capacity,
    char* output_absolute,
    unsigned int output_capacity)
{
    char map_name[DLUMA_MAX_MAP_NAME];
    if (!source_path || !output_path || !project_root ||
        !source_absolute || !output_absolute ||
        !dluma_editor_launch_resolve_current_project(
            project_root, project_capacity,
            map_name, DLUMA_ARRAY_COUNT(map_name)) ||
        !dluma_quest_editor_resolve_compile_path(
            source_path, project_root,
            source_absolute, source_capacity) ||
        !dluma_quest_editor_resolve_compile_path(
            output_path, project_root,
            output_absolute, output_capacity))
        return 0;
    return 1;
}

static int dluma_quest_editor_definition_snapshot(
    __int64* compiler,
    void*** definitions_out,
    unsigned int* count_out)
{
    BYTE* compiler_bytes = (BYTE*)compiler;
    void** definitions;
    unsigned int count;
    if (!compiler || !definitions_out || !count_out ||
        !dluma_is_readable(compiler_bytes + 72U, 12U))
        return 0;
    definitions = *(void***)(compiler_bytes + 72U);
    count = *(const unsigned int*)(compiler_bytes + 80U);
    if (count > DLUMA_QUEST_EDITOR_MAX_DEFINITIONS ||
        (count && (!definitions || !dluma_is_readable(
            definitions, (SIZE_T)count * sizeof(void*)))))
        return 0;
    *definitions_out = definitions;
    *count_out = count;
    return 1;
}

static int dluma_quest_editor_validate_loaded_definition(
    __int64* compiler,
    unsigned int count_before,
    unsigned int expected_macro_count)
{
    void** definitions = 0;
    unsigned int count_after = 0;
    BYTE* definition;
    BYTE* parse_state;
    BYTE* macro_entries;
    unsigned int macro_count;
    unsigned int published_count;
    unsigned int index;
    if (!dluma_quest_editor_definition_snapshot(
            compiler, &definitions, &count_after) ||
        count_after != count_before + 1U || !definitions)
        return 0;
    definition = (BYTE*)definitions[count_after - 1U];
    if (!definition || !dluma_is_readable(definition, 116U)) return 0;
    parse_state = *(BYTE**)(definition + 56U);
    if (!parse_state || !dluma_is_readable(parse_state + 152U, 12U))
        return 0;
    macro_entries = *(BYTE**)(parse_state + 152U);
    macro_count = *(const unsigned int*)(parse_state + 160U);
    published_count = *(const unsigned int*)(definition + 112U);
    dluma_log_u64("INFO", "quest_editor_definition_count_before",
                count_before);
    dluma_log_u64("INFO", "quest_editor_definition_count_after",
                count_after);
    dluma_log_u64("INFO", "quest_editor_definition_macro_count",
                macro_count);
    dluma_log_u64("INFO", "quest_editor_definition_published_count",
                published_count);
    if (macro_count != expected_macro_count ||
        published_count != expected_macro_count ||
        !macro_entries || !dluma_is_readable(
            macro_entries, (SIZE_T)macro_count * 48U))
        return 0;
    for (index = 0; index < macro_count; ++index) {
        const BYTE* entry = macro_entries + (SIZE_T)index * 48U;
        const char* name = *(const char* const*)entry;
        unsigned int name_size = *(const unsigned int*)(entry + 8U);
        unsigned int alias_index;
        int expected = 0;
        char logged_name[64];
        if (!name || !name_size || name_size >= sizeof(logged_name) ||
            !dluma_is_readable(name, name_size + 1U) || name[name_size])
            return 0;
        memcpy(logged_name, name, name_size);
        logged_name[name_size] = 0;
        dluma_log_pair(
            "INFO", "quest_editor_definition_macro_name", logged_name);
        dluma_log_u64(
            "INFO", "quest_editor_definition_macro_name_bytes", name_size);
        if (dluma_stricmp(logged_name, "call-lua") == 0 ||
            dluma_stricmp(logged_name, "call-native") == 0)
            expected = 1;
        for (alias_index = 0;
             !expected && alias_index <
                 g_dluma_quest_editor.accepted_alias_count;
             ++alias_index)
            if (g_dluma_quest_editor.accepted_aliases &&
                dluma_stricmp(
                    logged_name,
                    g_dluma_quest_editor.accepted_aliases[
                        alias_index].command) == 0)
                expected = 1;
        if (!expected) return 0;
    }
    return 1;
}

static int dluma_quest_editor_ensure_edf(__int64 context)
{
    __int64* compiler;
    void** definitions = 0;
    unsigned int definition_count = 0;
    unsigned int expected_macro_count;
    const char* stock_paths[3];
    const char* edf_path;
    WCHAR project_root[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    int result = 0;
    if (!context || !g_dluma_quest_editor.definition_loader ||
        dluma_atomic_compare_exchange(&g_dluma_quest_editor.stopping, 0, 0))
        return 0;
    if (!dluma_is_readable((const void*)(context + 120), sizeof(void*)))
        return 0;
    compiler = *(__int64**)(context + 120);
    if (!compiler || !dluma_is_readable(compiler, sizeof(void*)) ||
        !*compiler || !dluma_is_readable((const void*)*compiler, 128U))
        return 0;
    compiler = (__int64*)*compiler;
    if (!dluma_editor_launch_resolve_current_project(
            project_root, DLUMA_ARRAY_COUNT(project_root),
            map_name, DLUMA_ARRAY_COUNT(map_name)))
        return 0;
    dluma_quest_editor_lock();
    if (g_dluma_quest_editor.loaded_compiler == compiler) {
        result = dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.edf_ready, 0, 0) == 1;
        goto done;
    }
    stock_paths[0] = dluma_quest_editor_context_path(context, 72U);
    stock_paths[1] = dluma_quest_editor_context_path(context, 88U);
    stock_paths[2] = dluma_quest_editor_context_path(context, 104U);
    if (!stock_paths[0] || !stock_paths[1] || !stock_paths[2] ||
        !dluma_quest_editor_write_edf(stock_paths) ||
        !dluma_quest_editor_definition_snapshot(
            compiler, &definitions, &definition_count))
        goto done;
    edf_path = g_dluma_quest_editor.edf_path_utf8;
    expected_macro_count = 2U +
        g_dluma_quest_editor.accepted_alias_count;
    __try {
        (void)g_dluma_quest_editor.definition_loader(compiler, &edf_path);
        result = dluma_quest_editor_validate_loaded_definition(
            compiler, definition_count, expected_macro_count);
    } __except(1) {
        result = 0;
    }
    g_dluma_quest_editor.loaded_compiler = compiler;
    dluma_atomic_exchange(
        &g_dluma_quest_editor.edf_ready, result ? 1 : -1);
    if (result) {
        dluma_log_pair("INFO", "quest_editor_edf_sha256",
                     g_dluma_quest_editor.edf_sha256);
        dluma_log_u64(
            "INFO", "quest_editor_alias_count",
            g_dluma_quest_editor.accepted_alias_count);
    } else
        dluma_log_pair(
            "ERROR", "quest_editor_validation_failure",
            "generated definition structure or macro count");
done:
    dluma_quest_editor_unlock();
    return result;
}

static __int64 __fastcall dluma_quest_editor_initialize_hook(__int64 context)
{
    DlumaQuestEditorInitializeFn original =
        (DlumaQuestEditorInitializeFn)g_dluma_quest_editor.initialize_detour.trampoline;
    __int64 result;
    int tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&g_dluma_quest_editor.callbacks_in_flight);
    result = original ? original(context) : 0;
    /* Managed QuestEditor startup can replace the process top-level filter.
     * Reassert the Editor-only fallback after InitializeMe while retaining
     * any replacement as the filter we forward to exactly once. */
    (void)dluma_crash_reporting_start_editor_filter();
    if (result && !dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.stopping, 0, 0) &&
        !dluma_quest_editor_ensure_edf(context))
        dluma_log_line(
            "ERROR",
            "QuestEditor initialized but the loader EDF could not be validated and loaded.");
    dluma_atomic_decrement(&g_dluma_quest_editor.callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
    return result;
}

static void __fastcall dluma_quest_editor_compile_hook(
    __int64 context,
    const char** source_path,
    const char** output_path)
{
    DlumaQuestEditorCompileFn original =
        (DlumaQuestEditorCompileFn)g_dluma_quest_editor.compile_detour.trampoline;
    DlumaQuestAuthoringTransaction* transaction = 0;
    DlumaQuestAuthoringPrepareResult prepare = DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    WCHAR project_root[DLUMA_MAX_WPATH];
    char source_absolute[DLUMA_MAX_WPATH * 2U];
    char output_absolute[DLUMA_MAX_WPATH * 2U];
    const char* source_value = 0;
    const char* output_value = 0;
    const char* compiler_source = 0;
    int definitions_ready;
    int authoring_ready = 0;
    int tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&g_dluma_quest_editor.callbacks_in_flight);
    definitions_ready = !dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.stopping, 0, 0) &&
        dluma_quest_editor_ensure_edf(context);
    if (!definitions_ready)
        dluma_log_line(
            "ERROR",
            "Quest compile is continuing without loader definitions; custom directives remain fail-closed.");
    if (source_path && dluma_is_readable(source_path, sizeof(*source_path)))
        source_value = dluma_quest_editor_bounded_path(*source_path);
    if (output_path && dluma_is_readable(output_path, sizeof(*output_path)))
        output_value = dluma_quest_editor_bounded_path(*output_path);
    if (source_value)
        dluma_log_pair("INFO", "quest_compile_source_raw", source_value);
    if (output_value)
        dluma_log_pair("INFO", "quest_compile_output_raw", output_value);
    if (definitions_ready && source_value && output_value &&
        dluma_quest_editor_resolve_compile_paths(
            source_value, output_value,
            project_root, DLUMA_ARRAY_COUNT(project_root),
            source_absolute, DLUMA_ARRAY_COUNT(source_absolute),
            output_absolute, DLUMA_ARRAY_COUNT(output_absolute))) {
        dluma_log_pair(
            "INFO", "quest_compile_source_resolved", source_absolute);
        dluma_log_pair(
            "INFO", "quest_compile_output_resolved", output_absolute);
        prepare = dluma_quest_authoring_prepare_for_project(
            source_absolute, output_absolute,
            project_root, &transaction);
        authoring_ready = prepare == DLUMA_QUEST_AUTHORING_STOCK ||
            prepare == DLUMA_QUEST_AUTHORING_CUSTOM;
        if (prepare == DLUMA_QUEST_AUTHORING_CUSTOM) {
            compiler_source =
                dluma_quest_authoring_compiler_source(transaction);
            if (!compiler_source) authoring_ready = 0;
            else {
                const char* compiler_sha256 =
                    dluma_quest_authoring_compiler_source_sha256(transaction);
                dluma_log_pair(
                    "INFO", "quest_compile_lowered_source", compiler_source);
                if (compiler_sha256)
                    dluma_log_pair(
                        "INFO", "quest_compile_lowered_sha256",
                        compiler_sha256);
            }
        }
    }
    if (!definitions_ready || authoring_ready) {
        if (original) {
            if (prepare == DLUMA_QUEST_AUTHORING_CUSTOM)
                original(context, &compiler_source, output_path);
            else
                original(context, source_path, output_path);
        }
    } else if (prepare == DLUMA_QUEST_AUTHORING_UNAVAILABLE) {
        dluma_log_pair(
            "ERROR", "quest_compile_rejected",
            "authoritative project source or authoring transaction unavailable");
    }
    if (transaction && authoring_ready &&
        !dluma_quest_authoring_finish(transaction))
        dluma_log_pair("ERROR", "quest_compile_sidecar_failure",
                     dluma_quest_authoring_diagnostic(transaction));
    else if (transaction && prepare == DLUMA_QUEST_AUTHORING_REJECT)
        dluma_log_pair("ERROR", "quest_compile_rejected",
                     dluma_quest_authoring_diagnostic(transaction));
    dluma_quest_authoring_release(transaction);
    dluma_atomic_decrement(&g_dluma_quest_editor.callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
}

static int dluma_quest_editor_requested_module(const char* file_name)
{
    const char* value = dluma_quest_editor_bounded_path(file_name);
    const char* leaf;
    const char* cursor;
    if (!value) return 0;
    leaf = value;
    for (cursor = value; *cursor; ++cursor)
        if (*cursor == '\\' || *cursor == '/') leaf = cursor + 1;
    return dluma_stricmp(leaf, "QuestEditor.dll") == 0;
}

static int dluma_quest_editor_install_module(HMODULE module)
{
    BYTE* base;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_quest_editor.start_state, 3, 1);
    if (state == 2)
        return g_dluma_quest_editor.module == module;
    if (state != 1 || !module) return 0;
    g_dluma_quest_editor.module = module;
    if (!dluma_quest_editor_validate_module(module)) goto failed;
    base = (BYTE*)module;
    g_dluma_quest_editor.definition_loader =
        (DlumaQuestEditorDefinitionFn)(
            base + DLUMA_QUEST_EDITOR_DEFINITION_RVA);
    if (!dluma_quest_editor_detour_install(
            &g_dluma_quest_editor.initialize_detour,
            base + DLUMA_QUEST_EDITOR_INIT_RVA,
            g_dluma_quest_editor_init_prefix,
            DLUMA_QUEST_EDITOR_INIT_STOLEN,
            (const void*)dluma_quest_editor_initialize_hook)) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "compiler initialization detour install");
        goto failed;
    }
    if (!dluma_quest_editor_detour_install(
            &g_dluma_quest_editor.compile_detour,
            base + DLUMA_QUEST_EDITOR_COMPILE_RVA,
            g_dluma_quest_editor_compile_prefix,
            DLUMA_QUEST_EDITOR_COMPILE_STOLEN,
            (const void*)dluma_quest_editor_compile_hook)) {
        dluma_log_pair("WARN", "quest_editor_validation_failure",
                     "compile detour install");
        (void)dluma_quest_editor_detour_restore(
            &g_dluma_quest_editor.initialize_detour);
        goto failed;
    }
    dluma_atomic_exchange(&g_dluma_quest_editor.stopping, 0);
    dluma_atomic_exchange(&g_dluma_quest_editor.start_state, 2);
    dluma_log_line(
        "INFO",
        "Exact QuestEditor hooks installed before plugin InitializeMe.");
    return 1;
failed:
    dluma_atomic_exchange(&g_dluma_quest_editor.start_state, -1);
    dluma_log_line(
        "WARN",
        "QuestEditor exact profile or hook ownership validation failed; quest extensions are disabled.");
    return 0;
}

static HMODULE WINAPI dluma_quest_editor_load_library_hook(
    LPCSTR file_name)
{
    DlumaQuestEditorLoadLibraryAFn original;
    HMODULE module;
    int tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(&g_dluma_quest_editor.callbacks_in_flight);
    original = (DlumaQuestEditorLoadLibraryAFn)
        dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)&g_dluma_quest_editor.original_load_library,
            0, 0);
    module = original ? original(file_name) : 0;
    if (module && !dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.stopping, 0, 0) &&
        dluma_quest_editor_requested_module(file_name)) {
        if (dluma_quest_editor_install_module(module))
            dluma_log_line(
                "INFO",
                "Validated QuestEditor LoadLibraryA boundary intercepted before InitializeMe.");
        else
            dluma_log_pair(
                "WARN", "quest_editor_validation_failure",
                "early LoadLibraryA module integration");
    }
    dluma_atomic_decrement(&g_dluma_quest_editor.callbacks_in_flight);
    dluma_editor_host_callback_leave(tracked);
    return module;
}

int dluma_quest_editor_start(void)
{
    void** slot;
    void* original;
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    LONG state;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    state = dluma_atomic_compare_exchange(
        &g_dluma_quest_editor.start_state, 1, 0);
    if (state != 0) return state == 1 || state == 2 || state == 3;
    dluma_atomic_exchange(&g_dluma_quest_editor.stopping, 0);
    if (dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.load_library_hook_installed,
            1, 0) != 0)
        goto failed;
    slot = dluma_quest_editor_find_load_library_iat();
    if (!slot || !VirtualProtect(
            slot, sizeof(void*), PAGE_READWRITE, &old_protect))
        goto failed;
    original = *slot;
    g_dluma_quest_editor.load_library_iat_slot = slot;
    g_dluma_quest_editor.original_load_library =
        (DlumaQuestEditorLoadLibraryAFn)original;
    if (dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)slot,
            (void*)dluma_quest_editor_load_library_hook,
            original) != original) {
        (void)VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect);
        goto failed;
    }
    if (!VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect)) {
        (void)dluma_atomic_pointer_compare_exchange(
            (PVOID volatile*)slot, original,
            (void*)dluma_quest_editor_load_library_hook);
        (void)VirtualProtect(
            slot, sizeof(void*), old_protect, &restored_protect);
        goto failed;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    dluma_atomic_exchange(
        &g_dluma_quest_editor.load_library_hook_installed, 2);
    dluma_log_line(
        "INFO",
        "Validated Chrome Editor LoadLibraryA QuestEditor boundary installed.");
    g_dluma_quest_editor.module = GetModuleHandleW(L"QuestEditor.dll");
    if (!g_dluma_quest_editor.module)
        return 1;
    dluma_log_line(
        "WARN",
        "QuestEditor was already loaded when the early boundary was installed; validating a fail-closed late attachment.");
    return dluma_quest_editor_install_module(g_dluma_quest_editor.module);
failed:
    dluma_atomic_exchange(&g_dluma_quest_editor.start_state, -1);
    dluma_atomic_exchange(
        &g_dluma_quest_editor.load_library_hook_installed, -1);
    dluma_log_line(
        "WARN",
        "Chrome Editor LoadLibraryA IAT ownership validation failed; quest extensions are disabled.");
    return 0;
}

int dluma_quest_editor_stop(void)
{
    void** slot = g_dluma_quest_editor.load_library_iat_slot;
    void* original = (void*)g_dluma_quest_editor.original_load_library;
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    ULONGLONG started;
    dluma_atomic_exchange(&g_dluma_quest_editor.stopping, 1);
    if (slot) {
        if (!original || !dluma_is_readable(slot, sizeof(void*)) ||
            (*slot != (void*)dluma_quest_editor_load_library_hook &&
             *slot != original))
            return 0;
        if (*slot == (void*)dluma_quest_editor_load_library_hook) {
            if (!VirtualProtect(
                    slot, sizeof(void*), PAGE_READWRITE, &old_protect) ||
                dluma_atomic_pointer_compare_exchange(
                    (PVOID volatile*)slot, original,
                    (void*)dluma_quest_editor_load_library_hook) !=
                        (void*)dluma_quest_editor_load_library_hook ||
                !VirtualProtect(
                    slot, sizeof(void*), old_protect,
                    &restored_protect))
                return 0;
            FlushInstructionCache(
                GetCurrentProcess(), slot, sizeof(void*));
        }
    }
    if (!dluma_quest_editor_detour_restore(
            &g_dluma_quest_editor.compile_detour) ||
        !dluma_quest_editor_detour_restore(
            &g_dluma_quest_editor.initialize_detour))
        return 0;
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_quest_editor.callbacks_in_flight, 0, 0) != 0 &&
           GetTickCount64() - started < 2000ULL)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_quest_editor.callbacks_in_flight, 0, 0) != 0)
        return 0;
    if (g_dluma_quest_editor.compile_detour.trampoline &&
        !VirtualFree(
            g_dluma_quest_editor.compile_detour.trampoline,
            0, MEM_RELEASE))
        return 0;
    if (g_dluma_quest_editor.initialize_detour.trampoline &&
        !VirtualFree(
            g_dluma_quest_editor.initialize_detour.trampoline,
            0, MEM_RELEASE))
        return 0;
    memset(&g_dluma_quest_editor.compile_detour, 0,
           sizeof(g_dluma_quest_editor.compile_detour));
    memset(&g_dluma_quest_editor.initialize_detour, 0,
           sizeof(g_dluma_quest_editor.initialize_detour));
    g_dluma_quest_editor.definition_loader = 0;
    g_dluma_quest_editor.loaded_compiler = 0;
    g_dluma_quest_editor.module = 0;
    g_dluma_quest_editor.load_library_iat_slot = 0;
    g_dluma_quest_editor.original_load_library = 0;
    dluma_atomic_exchange(&g_dluma_quest_editor.edf_ready, 0);
    if (g_dluma_quest_editor.accepted_aliases) {
        dluma_free(g_dluma_quest_editor.accepted_aliases);
        g_dluma_quest_editor.accepted_aliases = 0;
    }
    g_dluma_quest_editor.accepted_alias_count = 0;
    dluma_atomic_exchange(
        &g_dluma_quest_editor.load_library_hook_installed, 0);
    dluma_atomic_exchange(&g_dluma_quest_editor.start_state, 0);
    return 1;
}

int dluma_quest_editor_callbacks_drained(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_quest_editor.callbacks_in_flight,
               0, 0) == 0;
}
