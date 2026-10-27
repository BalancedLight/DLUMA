#include "dlw_quest_phase_player.h"
#include "dlw_build_identity.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_quest_phase_runtime.h"

#define DLUMA_QPP_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_QPP_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_QPP_GAMEDLL_SHA256 \
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
#define DLUMA_QPP_ON_ENTER_RVA 0x00D800C0UL
#define DLUMA_QPP_PHASE_CTOR_RVA 0x00D7F280UL
#define DLUMA_QPP_QUEST_CTOR_RVA 0x00D864B0UL
#define DLUMA_QPP_QUEST_VTABLE_RVA 0x01813D48UL
#define DLUMA_QPP_QUEST_VTABLE_FIRST_RVA 0x0079F220UL
#define DLUMA_QPP_STOLEN_SIZE 18U
#define DLUMA_QPP_JUMP_SIZE 14U
#define DLUMA_QPP_DRAIN_TIMEOUT_MS 2000ULL
#define DLUMA_QPP_MAX_EVENTS_PER_PUMP 256U
#define DLUMA_QPP_CALLBACK_THREAD_CAPACITY 64U

typedef void (__fastcall* DlumaQuestPhaseOnEnterFn)(void* phase);

typedef struct DlumaQuestPhasePlayerDetour {
    BYTE* target;
    BYTE* trampoline;
    SIZE_T trampoline_size;
    BYTE original[DLUMA_QPP_STOLEN_SIZE];
    BYTE patch[DLUMA_QPP_STOLEN_SIZE];
    volatile LONG installed;
} DlumaQuestPhasePlayerDetour;

typedef struct DlumaQuestPhaseObservation {
    DlumaQuestPhaseEvent event_value;
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
    ULONGLONG phase_vtable_rva;
    ULONGLONG quest_vtable_rva;
} DlumaQuestPhaseObservation;

typedef struct DlumaQuestPhaseCallbackThread {
    DWORD thread_id;
    unsigned int depth;
} DlumaQuestPhaseCallbackThread;

typedef struct DlumaQuestPhasePlayerState {
    HMODULE module;
    DWORD image_size;
    DlumaQuestPhasePlayerDetour detour;
    DlumaQuestPhaseOnEnterFn original_on_enter;
    volatile LONG start_state;
    volatile LONG stopping;
    volatile LONG callbacks_in_flight;
    volatile LONG callback_thread_lock;
    volatile LONG untracked_callbacks_in_flight;
    volatile LONG queue_lock;
    volatile LONGLONG next_sequence;
    volatile LONGLONG dropped_observations;
    ULONGLONG reported_drops;
    unsigned int map_generation;
    char map_name[128];
    DlumaQuestPhaseBindingSet bindings;
    DlumaQuestPhaseObservation queue[DLUMA_QUEST_PHASE_QUEUE_CAPACITY];
    unsigned int queue_head;
    unsigned int queue_count;
    DlumaQuestPhaseCallbackThread callback_threads[
        DLUMA_QPP_CALLBACK_THREAD_CAPACITY];
    DlumaQuestPhaseDispatcher dispatcher;
} DlumaQuestPhasePlayerState;

static DlumaQuestPhasePlayerState g_dluma_qpp;

static const BYTE g_dluma_qpp_on_enter_prefix[DLUMA_QPP_STOLEN_SIZE] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00
};

static const BYTE g_dluma_qpp_phase_layout_prefix[16] = {
    0x48, 0x89, 0x77, 0x50, 0x48, 0x89, 0x77, 0x58,
    0x48, 0x8B, 0x03, 0x48, 0x85, 0xC0, 0x48, 0x0F
};

static const BYTE g_dluma_qpp_phase_name_store[4] = {
    0x48, 0x89, 0x47, 0x60
};

static const BYTE g_dluma_qpp_quest_ctor_prefix[32] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
    0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8D, 0x05, 0x85,
    0xD8, 0xA8, 0x00, 0x48, 0x8B, 0xD9, 0xC7, 0x41,
    0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x01
};

static void __fastcall dluma_qpp_on_enter_hook(void* phase);

static const WCHAR* dluma_qpp_skip_extended(const WCHAR* value)
{
    if (value && value[0] == L'\\' && value[1] == L'\\' &&
        value[2] == L'?' && value[3] == L'\\')
        return value + 4;
    return value;
}

static int dluma_qpp_text_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static int dluma_qpp_section(
    BYTE* module,
    DWORD image_size,
    DWORD rva,
    SIZE_T size,
    DWORD required,
    DWORD forbidden,
    const char* name)
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
        unsigned int name_index;
        if (rva < virtual_address || rva - virtual_address >= span ||
            size > span - (rva - virtual_address))
            continue;
        if ((characteristics & required) != required ||
            (characteristics & forbidden) != 0)
            return 0;
        if (!name) return 1;
        for (name_index = 0; name[name_index]; ++name_index)
            if (name_index >= 8U ||
                section[name_index] != (BYTE)name[name_index])
                return 0;
        return name_index == 8U || section[name_index] == 0;
    }
    return 0;
}

static int dluma_qpp_pointer_section(
    const void* pointer,
    SIZE_T size,
    DWORD required,
    DWORD forbidden,
    const char* name)
{
    ULONG_PTR base = (ULONG_PTR)g_dluma_qpp.module;
    ULONG_PTR value = (ULONG_PTR)pointer;
    if (!base || value < base || value - base > g_dluma_qpp.image_size ||
        size > g_dluma_qpp.image_size - (value - base))
        return 0;
    return dluma_qpp_section(
        (BYTE*)base, g_dluma_qpp.image_size,
        (DWORD)(value - base), size, required, forbidden, name);
}

static int dluma_qpp_validate_module(HMODULE module)
{
    BYTE* base = (BYTE*)module;
    BYTE disk_header[4096];
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER zero;
    DWORD loaded_timestamp = 0;
    DWORD loaded_image_size = 0;
    DWORD disk_timestamp = 0;
    DWORD disk_image_size = 0;
    DWORD read = 0;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    HANDLE file = INVALID_HANDLE_VALUE;
    ULONG_PTR first_quest_method;
    int result = 0;
    memset(&info, 0, sizeof(info));
    zero.QuadPart = 0;
    if (!module || GetModuleHandleW(L"gamedll_x64_rwdi.dll") != module ||
        !dluma_build_identity_parse_pe_header(
            base, 4096U, &loaded_timestamp, &loaded_image_size) ||
        loaded_timestamp != DLUMA_QPP_GAMEDLL_TIMESTAMP ||
        loaded_image_size != DLUMA_QPP_GAMEDLL_IMAGE_SIZE ||
        !dluma_get_module_path_checked(
            module, module_path, DLUMA_ARRAY_COUNT(module_path)))
        return 0;
    file = CreateFileW(
        module_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        dluma_wstricmp(dluma_qpp_skip_extended(module_path),
                     dluma_qpp_skip_extended(final_path)) != 0 ||
        !dluma_sha256_handle_hex(
            file, 1024ULL * 1024ULL * 1024ULL, sha256) ||
        dluma_stricmp(sha256, DLUMA_QPP_GAMEDLL_SHA256) != 0 ||
        !SetFilePointerEx(file, zero, 0, FILE_BEGIN) ||
        !ReadFile(file, disk_header, sizeof(disk_header), &read, 0) ||
        !dluma_build_identity_parse_pe_header(
            disk_header, read, &disk_timestamp, &disk_image_size) ||
        disk_timestamp != loaded_timestamp ||
        disk_image_size != loaded_image_size ||
        !dluma_qpp_section(
            base, loaded_image_size, DLUMA_QPP_ON_ENTER_RVA,
            sizeof(g_dluma_qpp_on_enter_prefix),
            0x60000000UL, 0x80000000UL, ".text") ||
        !dluma_qpp_section(
            base, loaded_image_size, DLUMA_QPP_PHASE_CTOR_RVA,
            0x90U, 0x60000000UL, 0x80000000UL, ".text") ||
        !dluma_qpp_section(
            base, loaded_image_size, DLUMA_QPP_QUEST_CTOR_RVA,
            sizeof(g_dluma_qpp_quest_ctor_prefix),
            0x60000000UL, 0x80000000UL, ".text") ||
        !dluma_qpp_section(
            base, loaded_image_size, DLUMA_QPP_QUEST_VTABLE_RVA,
            sizeof(ULONG_PTR), 0x40000000UL,
            0xA0000000UL, ".rdata") ||
        memcmp(base + DLUMA_QPP_ON_ENTER_RVA,
               g_dluma_qpp_on_enter_prefix,
               sizeof(g_dluma_qpp_on_enter_prefix)) != 0 ||
        memcmp(base + DLUMA_QPP_PHASE_CTOR_RVA + 0x66U,
               g_dluma_qpp_phase_layout_prefix,
               sizeof(g_dluma_qpp_phase_layout_prefix)) != 0 ||
        memcmp(base + DLUMA_QPP_PHASE_CTOR_RVA + 0x82U,
               g_dluma_qpp_phase_name_store,
               sizeof(g_dluma_qpp_phase_name_store)) != 0 ||
        memcmp(base + DLUMA_QPP_QUEST_CTOR_RVA,
               g_dluma_qpp_quest_ctor_prefix,
               sizeof(g_dluma_qpp_quest_ctor_prefix)) != 0)
        goto done;
    first_quest_method = *(const ULONG_PTR*)(
        base + DLUMA_QPP_QUEST_VTABLE_RVA);
    if (first_quest_method !=
            (ULONG_PTR)(base + DLUMA_QPP_QUEST_VTABLE_FIRST_RVA) ||
        !dluma_qpp_section(
            base, loaded_image_size, DLUMA_QPP_QUEST_VTABLE_FIRST_RVA,
            1U, 0x60000000UL, 0x80000000UL, ".text"))
        goto done;
    g_dluma_qpp.image_size = loaded_image_size;
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static void dluma_qpp_absolute_jump(BYTE* output, ULONG_PTR destination)
{
    output[0] = 0xFF;
    output[1] = 0x25;
    output[2] = 0;
    output[3] = 0;
    output[4] = 0;
    output[5] = 0;
    memcpy(output + 6U, &destination, sizeof(destination));
}

static int dluma_qpp_detour_install(
    DlumaQuestPhasePlayerDetour* detour,
    BYTE* target,
    DlumaQuestPhaseOnEnterFn hook)
{
    BYTE tail[DLUMA_QPP_JUMP_SIZE];
    DWORD old_protect = 0;
    DWORD restored = 0;
    DWORD trampoline_old = 0;
    ULONG_PTR hook_address = 0;
    if (!detour || !target || !hook ||
        memcmp(target, g_dluma_qpp_on_enter_prefix,
               DLUMA_QPP_STOLEN_SIZE) != 0)
        return 0;
    memset(detour, 0, sizeof(*detour));
    detour->trampoline_size = DLUMA_QPP_STOLEN_SIZE + sizeof(tail);
    detour->trampoline = (BYTE*)VirtualAlloc(
        0, detour->trampoline_size, MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!detour->trampoline) return 0;
    memcpy(detour->original, g_dluma_qpp_on_enter_prefix,
           DLUMA_QPP_STOLEN_SIZE);
    memcpy(detour->trampoline, detour->original, DLUMA_QPP_STOLEN_SIZE);
    dluma_qpp_absolute_jump(
        tail, (ULONG_PTR)(target + DLUMA_QPP_STOLEN_SIZE));
    memcpy(detour->trampoline + DLUMA_QPP_STOLEN_SIZE,
           tail, sizeof(tail));
    if (!VirtualProtect(
            detour->trampoline, detour->trampoline_size,
            PAGE_EXECUTE_READ, &trampoline_old))
        goto failed;
    FlushInstructionCache(
        GetCurrentProcess(), detour->trampoline,
        detour->trampoline_size);
    memset(detour->patch, 0x90, sizeof(detour->patch));
    memcpy(&hook_address, &hook, sizeof(hook_address));
    dluma_qpp_absolute_jump(detour->patch, hook_address);
    if (!VirtualProtect(
            target, DLUMA_QPP_STOLEN_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect) ||
        memcmp(target, detour->original, DLUMA_QPP_STOLEN_SIZE) != 0)
        goto failed;
    memcpy(target, detour->patch, DLUMA_QPP_STOLEN_SIZE);
    if (!VirtualProtect(
            target, DLUMA_QPP_STOLEN_SIZE,
            old_protect, &restored)) {
        memcpy(target, detour->original, DLUMA_QPP_STOLEN_SIZE);
        (void)VirtualProtect(
            target, DLUMA_QPP_STOLEN_SIZE,
            old_protect, &restored);
        goto failed;
    }
    FlushInstructionCache(
        GetCurrentProcess(), target, DLUMA_QPP_STOLEN_SIZE);
    detour->target = target;
    dluma_atomic_exchange(&detour->installed, 1);
    return 1;
failed:
    if (detour->trampoline) {
        (void)VirtualFree(detour->trampoline, 0, MEM_RELEASE);
        detour->trampoline = 0;
    }
    return 0;
}

static int dluma_qpp_detour_restore(DlumaQuestPhasePlayerDetour* detour)
{
    DWORD old_protect = 0;
    DWORD restored = 0;
    if (!detour || !dluma_atomic_compare_exchange(
            &detour->installed, 0, 0))
        return 1;
    if (!detour->target ||
        memcmp(detour->target, detour->patch,
               DLUMA_QPP_STOLEN_SIZE) != 0 ||
        !VirtualProtect(
            detour->target, DLUMA_QPP_STOLEN_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect))
        return 0;
    memcpy(detour->target, detour->original, DLUMA_QPP_STOLEN_SIZE);
    if (!VirtualProtect(
            detour->target, DLUMA_QPP_STOLEN_SIZE,
            old_protect, &restored))
        return 0;
    FlushInstructionCache(
        GetCurrentProcess(), detour->target, DLUMA_QPP_STOLEN_SIZE);
    dluma_atomic_exchange(&detour->installed, 0);
    return 1;
}

static int dluma_qpp_utf8_valid(const char* value, unsigned int size)
{
    unsigned int cursor = 0;
    if (!value || !size) return 0;
    while (cursor < size) {
        unsigned char first = (unsigned char)value[cursor++];
        unsigned int count;
        unsigned int codepoint;
        unsigned int minimum;
        unsigned int index;
        if (first < 0x80U) {
            if (first < 0x20U) return 0;
            continue;
        }
        if ((first & 0xE0U) == 0xC0U) {
            count = 2U;
            codepoint = first & 0x1FU;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            count = 3U;
            codepoint = first & 0x0FU;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            count = 4U;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        } else return 0;
        if (cursor + count - 1U > size) return 0;
        for (index = 1U; index < count; ++index) {
            unsigned char next = (unsigned char)value[cursor++];
            if ((next & 0xC0U) != 0x80U) return 0;
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
            return 0;
    }
    return 1;
}

static int dluma_qpp_copy_phase_name(
    const char* source,
    char* output,
    unsigned int capacity)
{
    unsigned int index;
    if (!source || !output || capacity < 2U) return 0;
    for (index = 0; index + 1U < capacity; ++index) {
        char current;
        if (!dluma_is_readable(source + index, 1U)) return 0;
        current = source[index];
        if (!current) {
            output[index] = 0;
            return index && dluma_qpp_utf8_valid(output, index);
        }
        output[index] = current;
    }
    output[0] = 0;
    return 0;
}

static int dluma_qpp_copy_quest_name(
    const BYTE* quest,
    char* output,
    unsigned int capacity)
{
    const char* source;
    DWORD size;
    DWORD allocated;
    if (!quest || !output || capacity < 2U ||
        !dluma_is_readable(quest, 40U))
        return 0;
    source = *(const char* const*)(quest + 24U);
    size = *(const DWORD*)(quest + 32U);
    allocated = *(const DWORD*)(quest + 36U);
    if (!source || !size || size + 1U > capacity ||
        allocated < size || allocated > 1024U ||
        !dluma_is_readable(source, (SIZE_T)size + 1U) || source[size] != 0 ||
        !dluma_qpp_utf8_valid(source, size))
        return 0;
    memcpy(output, source, size);
    output[size] = 0;
    return 1;
}

static int dluma_qpp_validate_phase_object(
    const BYTE* phase,
    char quest_id[128],
    char phase_id[128],
    ULONGLONG* phase_vtable_rva,
    ULONGLONG* quest_vtable_rva)
{
    const ULONG_PTR* primary;
    const ULONG_PTR* phase_interface;
    const ULONG_PTR* visualization_interface;
    const BYTE* quest;
    ULONG_PTR quest_vtable;
    ULONG_PTR base = (ULONG_PTR)g_dluma_qpp.module;
    if (!phase || !quest_id || !phase_id || !phase_vtable_rva ||
        !quest_vtable_rva || !dluma_is_readable(phase, 120U))
        return 0;
    primary = *(const ULONG_PTR* const*)(phase + 0U);
    phase_interface = *(const ULONG_PTR* const*)(phase + 8U);
    visualization_interface = *(const ULONG_PTR* const*)(phase + 16U);
    if (!primary || !phase_interface || !visualization_interface ||
        !dluma_qpp_pointer_section(
            primary, 172U * sizeof(ULONG_PTR),
            0x40000000UL, 0xA0000000UL, ".rdata") ||
        !dluma_qpp_pointer_section(
            phase_interface, 6U * sizeof(ULONG_PTR),
            0x40000000UL, 0xA0000000UL, ".rdata") ||
        !dluma_qpp_pointer_section(
            visualization_interface, sizeof(ULONG_PTR),
            0x40000000UL, 0xA0000000UL, ".rdata") ||
        !dluma_qpp_pointer_section(
            (const void*)primary[171], 1U,
            0x60000000UL, 0x80000000UL, ".text") ||
        !dluma_qpp_pointer_section(
            (const void*)phase_interface[5], 1U,
            0x60000000UL, 0x80000000UL, ".text"))
        return 0;
    quest = *(const BYTE* const*)(phase + 80U);
    if (!quest || !dluma_is_readable(quest, 40U)) return 0;
    quest_vtable = *(const ULONG_PTR*)quest;
    if (quest_vtable != base + DLUMA_QPP_QUEST_VTABLE_RVA ||
        !dluma_qpp_copy_quest_name(quest, quest_id, 128U) ||
        !dluma_qpp_copy_phase_name(
            *(const char* const*)(phase + 96U), phase_id, 128U))
        return 0;
    *phase_vtable_rva = (ULONGLONG)((ULONG_PTR)primary - base);
    *quest_vtable_rva = (ULONGLONG)(quest_vtable - base);
    return 1;
}

static int dluma_qpp_try_lock(void)
{
    return dluma_atomic_compare_exchange(
        &g_dluma_qpp.queue_lock, 1, 0) == 0;
}

static void dluma_qpp_lock(void)
{
    while (!dluma_qpp_try_lock()) Sleep(0);
}

static void dluma_qpp_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_qpp.queue_lock, 0);
}

static void dluma_qpp_callback_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_qpp.callback_thread_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_qpp_callback_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_qpp.callback_thread_lock, 0);
}

static int dluma_qpp_callback_enter(void)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    unsigned int free_index = DLUMA_QPP_CALLBACK_THREAD_CAPACITY;
    int tracked = 0;
    dluma_qpp_callback_lock();
    for (index = 0; index < DLUMA_QPP_CALLBACK_THREAD_CAPACITY; ++index) {
        DlumaQuestPhaseCallbackThread* slot =
            &g_dluma_qpp.callback_threads[index];
        if (slot->thread_id == thread_id && slot->depth) {
            if (slot->depth != 0xFFFFFFFFU) {
                ++slot->depth;
                tracked = 1;
            }
            break;
        }
        if (!slot->thread_id && free_index == DLUMA_QPP_CALLBACK_THREAD_CAPACITY)
            free_index = index;
    }
    if (!tracked && index == DLUMA_QPP_CALLBACK_THREAD_CAPACITY &&
        free_index < DLUMA_QPP_CALLBACK_THREAD_CAPACITY) {
        g_dluma_qpp.callback_threads[free_index].thread_id = thread_id;
        g_dluma_qpp.callback_threads[free_index].depth = 1U;
        tracked = 1;
    }
    if (!tracked)
        dluma_atomic_increment(&g_dluma_qpp.untracked_callbacks_in_flight);
    dluma_qpp_callback_unlock();
    return tracked;
}

static void dluma_qpp_callback_leave(int tracked)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    dluma_qpp_callback_lock();
    if (tracked) {
        for (index = 0; index < DLUMA_QPP_CALLBACK_THREAD_CAPACITY; ++index) {
            DlumaQuestPhaseCallbackThread* slot =
                &g_dluma_qpp.callback_threads[index];
            if (slot->thread_id != thread_id || !slot->depth) continue;
            --slot->depth;
            if (!slot->depth) slot->thread_id = 0;
            break;
        }
    } else if (dluma_atomic_compare_exchange(
                   &g_dluma_qpp.untracked_callbacks_in_flight, 0, 0) > 0) {
        dluma_atomic_decrement(&g_dluma_qpp.untracked_callbacks_in_flight);
    }
    dluma_qpp_callback_unlock();
}

static int dluma_qpp_current_thread_in_callback(void)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    int active = 0;
    dluma_qpp_callback_lock();
    if (dluma_atomic_compare_exchange(
            &g_dluma_qpp.untracked_callbacks_in_flight, 0, 0) > 0) {
        active = 1;
    } else {
        for (index = 0; index < DLUMA_QPP_CALLBACK_THREAD_CAPACITY; ++index)
            if (g_dluma_qpp.callback_threads[index].thread_id == thread_id &&
                g_dluma_qpp.callback_threads[index].depth) {
                active = 1;
                break;
            }
    }
    dluma_qpp_callback_unlock();
    return active;
}

static const DlumaQuestPhaseBindingSnapshot* dluma_qpp_find_binding_locked(
    const char* quest_id,
    const char* phase_id)
{
    unsigned int index;
    if (g_dluma_qpp.bindings.map_generation != g_dluma_qpp.map_generation)
        return 0;
    for (index = 0; index < g_dluma_qpp.bindings.binding_count; ++index) {
        const DlumaQuestPhaseBindingSnapshot* binding =
            &g_dluma_qpp.bindings.bindings[index];
        if (dluma_qpp_text_equal(binding->quest_id, quest_id) &&
            dluma_qpp_text_equal(binding->phase_id, phase_id))
            return binding;
    }
    return 0;
}

static void dluma_qpp_capture_observation(void* phase_pointer)
{
    char quest_id[128];
    char phase_id[128];
    ULONGLONG phase_vtable_rva = 0;
    ULONGLONG quest_vtable_rva = 0;
    const DlumaQuestPhaseBindingSnapshot* binding;
    DlumaQuestPhaseObservation* observation;
    unsigned int index;
    LONGLONG sequence;
    memset(quest_id, 0, sizeof(quest_id));
    memset(phase_id, 0, sizeof(phase_id));
    if (!dluma_qpp_validate_phase_object(
            (const BYTE*)phase_pointer, quest_id, phase_id,
            &phase_vtable_rva, &quest_vtable_rva))
        return;
    if (!dluma_qpp_try_lock()) {
        (void)_InterlockedIncrement64(&g_dluma_qpp.dropped_observations);
        return;
    }
    binding = dluma_qpp_find_binding_locked(quest_id, phase_id);
    if (!binding) {
        dluma_qpp_unlock();
        return;
    }
    if (g_dluma_qpp.queue_count >= DLUMA_QUEST_PHASE_QUEUE_CAPACITY) {
        (void)_InterlockedIncrement64(&g_dluma_qpp.dropped_observations);
        dluma_qpp_unlock();
        return;
    }
    index = (g_dluma_qpp.queue_head + g_dluma_qpp.queue_count) %
        DLUMA_QUEST_PHASE_QUEUE_CAPACITY;
    observation = &g_dluma_qpp.queue[index];
    memset(observation, 0, sizeof(*observation));
    observation->event_value.struct_size =
        (unsigned int)sizeof(observation->event_value);
    observation->event_value.map_generation = g_dluma_qpp.map_generation;
    sequence = _InterlockedIncrement64(&g_dluma_qpp.next_sequence);
    if (!sequence) sequence =
        _InterlockedIncrement64(&g_dluma_qpp.next_sequence);
    observation->event_value.sequence = (uint64_t)sequence;
    observation->event_value.runtime_kind = binding->runtime_kind;
    observation->event_value.completion = binding->completion;
    observation->event_value.wait_seconds = binding->wait_seconds;
    dluma_str_copy(observation->event_value.quest_id,
                 DLUMA_ARRAY_COUNT(observation->event_value.quest_id),
                 binding->quest_id);
    dluma_str_copy(observation->event_value.phase_id,
                 DLUMA_ARRAY_COUNT(observation->event_value.phase_id),
                 binding->phase_id);
    dluma_str_copy(observation->event_value.contract_id,
                 DLUMA_ARRAY_COUNT(observation->event_value.contract_id),
                 binding->contract_id);
    dluma_str_copy(observation->event_value.provider_mod,
                 DLUMA_ARRAY_COUNT(observation->event_value.provider_mod),
                 binding->provider_mod);
    dluma_str_copy(observation->event_value.handler_id,
                 DLUMA_ARRAY_COUNT(observation->event_value.handler_id),
                 binding->handler_id);
    dluma_str_copy(observation->event_value.binding_hash,
                 DLUMA_ARRAY_COUNT(observation->event_value.binding_hash),
                 binding->binding_hash);
    memcpy(observation->arguments, binding->arguments,
           binding->arguments_size);
    observation->arguments[binding->arguments_size] = 0;
    observation->event_value.arguments_json = observation->arguments;
    observation->event_value.arguments_size = binding->arguments_size;
    observation->event_value.local_peer = 1U;
    observation->phase_vtable_rva = phase_vtable_rva;
    observation->quest_vtable_rva = quest_vtable_rva;
    ++g_dluma_qpp.queue_count;
    dluma_qpp_unlock();
}

static void __fastcall dluma_qpp_on_enter_hook(void* phase)
{
    DlumaQuestPhaseOnEnterFn original;
    int tracked = dluma_qpp_callback_enter();
    dluma_atomic_increment(&g_dluma_qpp.callbacks_in_flight);
    original = g_dluma_qpp.original_on_enter;
    if (!dluma_atomic_compare_exchange(&g_dluma_qpp.stopping, 0, 0))
        dluma_qpp_capture_observation(phase);
    if (original) original(phase);
    dluma_atomic_decrement(&g_dluma_qpp.callbacks_in_flight);
    dluma_qpp_callback_leave(tracked);
}

int dluma_quest_phase_player_start(HMODULE gamedll)
{
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_qpp.start_state, 1, 0);
    if (state != 0)
        return state == 2;
    g_dluma_qpp.module = gamedll;
    if (!dluma_qpp_validate_module(gamedll) ||
        !dluma_qpp_detour_install(
            &g_dluma_qpp.detour,
            (BYTE*)gamedll + DLUMA_QPP_ON_ENTER_RVA,
            dluma_qpp_on_enter_hook)) {
        g_dluma_qpp.module = 0;
        g_dluma_qpp.image_size = 0;
        dluma_atomic_exchange(&g_dluma_qpp.start_state, -1);
        dluma_log_pair(
            "WARN", "quest_phase_on_enter_hook",
            "exact_profile_validation_failed");
        return 0;
    }
    memcpy(&g_dluma_qpp.original_on_enter,
           &g_dluma_qpp.detour.trampoline,
           sizeof(g_dluma_qpp.original_on_enter));
    dluma_quest_phase_dispatcher_initialize(&g_dluma_qpp.dispatcher, 1U);
    dluma_atomic_exchange(&g_dluma_qpp.start_state, 2);
    dluma_log_pair(
        "INFO", "quest_phase_on_enter_hook",
        "exact_devtools_profile_installed");
    return 1;
}

int dluma_quest_phase_player_on_map_generation_changed(
    const WCHAR* project_root,
    const char* map_name,
    unsigned int map_generation)
{
    DlumaQuestPhaseBindingSet next;
    DlumaQuestPhaseBindingSet previous;
    int loaded = 1;
    if (!map_generation ||
        dluma_atomic_compare_exchange(&g_dluma_qpp.start_state, 0, 0) != 2 ||
        dluma_atomic_compare_exchange(&g_dluma_qpp.stopping, 0, 0))
        return 0;
    memset(&next, 0, sizeof(next));
    memset(&previous, 0, sizeof(previous));
    next.map_generation = map_generation;
    if (project_root && project_root[0] && map_name && map_name[0])
        loaded = dluma_quest_phase_load_project_bindings(
            project_root, map_generation, &next);
    if (!loaded) {
        /* Publish an empty generation before reporting failure so a malformed
         * or changed sidecar can never leave the previous map's bindings
         * executable. */
        memset(&next, 0, sizeof(next));
        next.map_generation = map_generation;
    }
    dluma_qpp_lock();
    previous = g_dluma_qpp.bindings;
    g_dluma_qpp.bindings = next;
    g_dluma_qpp.map_generation = map_generation;
    dluma_str_copy(g_dluma_qpp.map_name,
                 DLUMA_ARRAY_COUNT(g_dluma_qpp.map_name),
                 map_name ? map_name : "");
    memset(g_dluma_qpp.queue, 0, sizeof(g_dluma_qpp.queue));
    g_dluma_qpp.queue_head = 0U;
    g_dluma_qpp.queue_count = 0U;
    dluma_qpp_unlock();
    dluma_quest_phase_release_bindings(&previous);
    if (!dluma_quest_phase_dispatcher_advance_generation(
            &g_dluma_qpp.dispatcher, map_generation))
        loaded = 0;
    if (loaded) {
        dluma_log_u64("INFO", "quest_phase_binding_generation",
                    map_generation);
        dluma_log_u64("INFO", "quest_phase_binding_count",
                    next.binding_count);
        dluma_log_u64("INFO", "quest_phase_sidecar_count",
                    next.sidecar_count);
    } else {
        dluma_log_pair(
            "ERROR", "quest_phase_binding_generation",
            "rejected_fail_closed");
    }
    return loaded;
}

void dluma_quest_phase_player_game_thread_pump(void)
{
    unsigned int copied = 0;
    ULONGLONG dropped;
    if (dluma_atomic_compare_exchange(&g_dluma_qpp.start_state, 0, 0) != 2 ||
        dluma_atomic_compare_exchange(&g_dluma_qpp.stopping, 0, 0))
        return;
    while (copied < DLUMA_QPP_MAX_EVENTS_PER_PUMP) {
        DlumaQuestPhaseObservation observation;
        int have_event = 0;
        memset(&observation, 0, sizeof(observation));
        dluma_qpp_lock();
        if (g_dluma_qpp.queue_count) {
            observation = g_dluma_qpp.queue[g_dluma_qpp.queue_head];
            memset(&g_dluma_qpp.queue[g_dluma_qpp.queue_head], 0,
                   sizeof(g_dluma_qpp.queue[g_dluma_qpp.queue_head]));
            g_dluma_qpp.queue_head = (g_dluma_qpp.queue_head + 1U) %
                DLUMA_QUEST_PHASE_QUEUE_CAPACITY;
            --g_dluma_qpp.queue_count;
            have_event = 1;
        }
        dluma_qpp_unlock();
        if (!have_event) break;
        observation.event_value.arguments_json = observation.arguments;
        if (observation.event_value.map_generation ==
                g_dluma_qpp.map_generation &&
            !dluma_quest_phase_enqueue(
                &g_dluma_qpp.dispatcher, &observation.event_value))
            (void)_InterlockedIncrement64(
                &g_dluma_qpp.dropped_observations);
        ++copied;
    }
    (void)dluma_quest_phase_pump(
        &g_dluma_qpp.dispatcher, DLUMA_QPP_MAX_EVENTS_PER_PUMP);
    dropped = (ULONGLONG)_InterlockedCompareExchange64(
        &g_dluma_qpp.dropped_observations, 0, 0);
    if (dropped != g_dluma_qpp.reported_drops) {
        g_dluma_qpp.reported_drops = dropped;
        dluma_log_u64("WARN", "quest_phase_observation_drops", dropped);
    }
}

int dluma_quest_phase_player_register_provider(
    const char* owner_mod,
    const char* handler_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* token)
{
    if (dluma_atomic_compare_exchange(&g_dluma_qpp.start_state, 0, 0) != 2 ||
        dluma_atomic_compare_exchange(&g_dluma_qpp.stopping, 0, 0))
        return 0;
    return dluma_quest_phase_register_provider(
        &g_dluma_qpp.dispatcher, owner_mod, handler_id, load_priority,
        callback, user, token);
}

int dluma_quest_phase_player_register_observer(
    const char* owner_mod,
    const char* contract_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* token)
{
    if (dluma_atomic_compare_exchange(&g_dluma_qpp.start_state, 0, 0) != 2 ||
        dluma_atomic_compare_exchange(&g_dluma_qpp.stopping, 0, 0))
        return 0;
    return dluma_quest_phase_register_observer(
        &g_dluma_qpp.dispatcher, owner_mod, contract_id, load_priority,
        callback, user, token);
}

int dluma_quest_phase_player_unregister(uint64_t token)
{
    if (dluma_atomic_compare_exchange(&g_dluma_qpp.start_state, 0, 0) != 2)
        return 0;
    return dluma_quest_phase_unregister(&g_dluma_qpp.dispatcher, token);
}

int dluma_quest_phase_player_stop(void)
{
    ULONGLONG started;
    DlumaQuestPhaseBindingSet bindings;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_qpp.start_state, 0, 0);
    if (state == 3) return 1;
    if (state != 2) return state == 0 || state == -1;
    dluma_atomic_exchange(&g_dluma_qpp.stopping, 1);
    if (!dluma_qpp_detour_restore(&g_dluma_qpp.detour)) {
        dluma_log_pair(
            "ERROR", "quest_phase_on_enter_hook",
            "restore_ownership_or_protection_failed_retained");
        return 0;
    }
    if (dluma_qpp_current_thread_in_callback()) {
        dluma_log_pair(
            "ERROR", "quest_phase_callback_graph",
            "shutdown_from_hook_callback_retained_without_wait");
        return 0;
    }
    started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_qpp.callbacks_in_flight, 0, 0) != 0 &&
           GetTickCount64() - started < DLUMA_QPP_DRAIN_TIMEOUT_MS)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_qpp.callbacks_in_flight, 0, 0) != 0 ||
        !dluma_quest_phase_dispatcher_quiesce(&g_dluma_qpp.dispatcher)) {
        dluma_log_pair(
            "ERROR", "quest_phase_callback_graph",
            "drain_failed_retained");
        return 0;
    }
    memset(&bindings, 0, sizeof(bindings));
    dluma_qpp_lock();
    bindings = g_dluma_qpp.bindings;
    memset(&g_dluma_qpp.bindings, 0, sizeof(g_dluma_qpp.bindings));
    memset(g_dluma_qpp.queue, 0, sizeof(g_dluma_qpp.queue));
    g_dluma_qpp.queue_head = 0U;
    g_dluma_qpp.queue_count = 0U;
    dluma_qpp_unlock();
    dluma_quest_phase_release_bindings(&bindings);
    g_dluma_qpp.original_on_enter = 0;
    if (g_dluma_qpp.detour.trampoline &&
        !VirtualFree(g_dluma_qpp.detour.trampoline, 0, MEM_RELEASE)) {
        dluma_log_pair(
            "ERROR", "quest_phase_trampoline",
            "release_failed_retained");
        return 0;
    }
    g_dluma_qpp.detour.trampoline = 0;
    g_dluma_qpp.module = 0;
    g_dluma_qpp.image_size = 0;
    dluma_atomic_exchange(&g_dluma_qpp.start_state, 3);
    return 1;
}

int dluma_quest_phase_player_started(void)
{
    return dluma_atomic_compare_exchange(
        &g_dluma_qpp.start_state, 0, 0) == 2;
}
