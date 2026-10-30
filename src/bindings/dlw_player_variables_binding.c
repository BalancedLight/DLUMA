#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "dlw_player_variables_binding.h"

#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Windows Player 1.49 exact-build evidence. The named macOS decompile was
 * used only for semantics; every address below is independently gated by the
 * loaded Windows PE identity, vtables, executable protection, and prefix. */
#define DLUMA_PLAYER_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_PLAYER_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_PLAYER_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_PLAYER_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA 0x01D07698UL
#define DLUMA_PLAYER_STATE_VTABLE_RVA 0x017E2FF8UL
#define DLUMA_PLAYER_STATE_VARIABLES_OFFSET 0x00D0UL
#define DLUMA_PLAYER_FLOAT_VARIABLE_VTABLE_RVA 0x017EB5F0UL
#define DLUMA_PLAYER_BOOL_VARIABLE_VTABLE_RVA 0x017EB5A0UL
#define DLUMA_PLAYER_STATE_VARIABLES_DIRTY_OFFSET 0x0122UL
#define DLUMA_PLAYER_GET_FVALUE_RVA 0x00BD2100UL

#define DLUMA_PLAYER_STATE_VFUNC0_RVA 0x010BA850UL
#define DLUMA_PLAYER_STATE_VFUNC1_RVA 0x00BB62C0UL
#define DLUMA_PLAYER_STATE_VFUNC2_RVA 0x00BB5D20UL
#define DLUMA_PLAYER_STATE_VFUNC3_RVA 0x00BB5D30UL
#define DLUMA_PLAYER_FLOAT_VFUNC0_RVA 0x00BDBB00UL
#define DLUMA_PLAYER_FLOAT_VFUNC1_RVA 0x00937E80UL
#define DLUMA_PLAYER_FLOAT_VFUNC2_RVA 0x000BF2C0UL
#define DLUMA_PLAYER_FLOAT_VFUNC3_RVA 0x000BF2C0UL

typedef float (__fastcall *DlumaPlayerGetFValueFn)(void* variables, int id);

static const DlumaPlayerVariableDefinition g_dluma_player_definitions[] = {
    { "low_health_effect_threshold", 103u, 0.0f, 100000.0f },
    { "stamina_warning_level", 209u, 0.0f, 10000.0f },
    { "max_stamina", 496u, 0.0f, 10000.0f },
    { "move_backward_max_speed", 619u, 0.0f, 100.0f },
    { "move_backward_min_speed", 620u, 0.0f, 100.0f },
    { "move_forward_max_speed", 621u, 0.0f, 100.0f },
    { "move_forward_min_speed", 622u, 0.0f, 100.0f },
    { "move_strafe_max_speed", 623u, 0.0f, 100.0f },
    { "move_strafe_min_speed", 624u, 0.0f, 100.0f },
    { "move_sprint_speed", 625u, 0.1f, 100.0f },
    { "move_sprint_stamina_consumption", 689u, 0.0f, 10000.0f },
    { "max_health", 702u, 1.0f, 100000.0f },
    { "base_max_health", 706u, 1.0f, 100000.0f },
    { "health_critical_level", 707u, 0.0f, 100000.0f },
    { "health_regeneration_delay", 713u, 0.0f, 3600.0f },
    { "health_regeneration_max_level", 714u, 0.0f, 100000.0f },
    { "health_regeneration_time_to_max", 715u, 0.0f, 3600.0f },
    { "camera_default_fov", 779u, 20.0f, 170.0f },
    { "camera_car_fov", 780u, 20.0f, 170.0f },
    { "fov_sprint_modifier", 817u, -0.95f, 10.0f },
    { "fov_amortizer_modifier", 818u, -0.95f, 10.0f },
    { "kick_stamina_usage", 823u, 0.0f, 10000.0f },
    { "infinite_fitness", 969u, 0.0f, 1.0f },
    { "infinite_stamina", 970u, 0.0f, 1.0f }
};

static HMODULE g_dluma_player_gamedll;
static DlumaPlayerGetFValueFn g_dluma_player_get_fvalue;
static int g_dluma_player_static_validation;

static int dluma_player_float_finite(float value)
{
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static int dluma_player_string_equal(
    const char* left,
    const char* right)
{
    if (!left || !right) return 0;
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int dluma_player_memory_readable(const void* address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t cursor;
    uintptr_t end;
    if (!address || !size) return 0;
    cursor = (uintptr_t)address;
    if (cursor > UINTPTR_MAX - size) return 0;
    end = cursor + size;
    while (cursor < end) {
        uintptr_t region_end;
        if (VirtualQuery((const void*)cursor, &info, sizeof(info)) !=
                sizeof(info) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return 0;
        region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (region_end <= cursor) return 0;
        cursor = region_end < end ? region_end : end;
    }
    return 1;
}

static int dluma_player_memory_writable(const void* address, SIZE_T size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t cursor;
    uintptr_t end;
    if (!address || !size) return 0;
    cursor = (uintptr_t)address;
    if (cursor > UINTPTR_MAX - size) return 0;
    end = cursor + size;
    while (cursor < end) {
        DWORD protection;
        uintptr_t region_end;
        if (VirtualQuery((const void*)cursor, &info, sizeof(info)) !=
                sizeof(info) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return 0;
        protection = info.Protect & 0xFFu;
        if (protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY)
            return 0;
        region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (region_end <= cursor) return 0;
        cursor = region_end < end ? region_end : end;
    }
    return 1;
}

static int dluma_player_validate_pe(
    HMODULE module,
    DWORD timestamp,
    DWORD image_size)
{
    const IMAGE_DOS_HEADER* dos;
    const IMAGE_NT_HEADERS64* nt;
    if (!module ||
        !dluma_player_memory_readable(module, sizeof(IMAGE_DOS_HEADER)))
        return 0;
    dos = (const IMAGE_DOS_HEADER*)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return 0;
    nt = (const IMAGE_NT_HEADERS64*)(
        (const unsigned char*)module + dos->e_lfanew);
    if (!dluma_player_memory_readable(nt, sizeof(*nt)) ||
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;
    return nt->FileHeader.TimeDateStamp == timestamp &&
           nt->OptionalHeader.SizeOfImage == image_size;
}

static int dluma_player_validate_vtable(
    HMODULE module,
    uintptr_t rva,
    const uintptr_t expected[4])
{
    const uintptr_t* table = (const uintptr_t*)(
        (const unsigned char*)module + rva);
    uint32_t index;
    if (!dluma_player_memory_readable(table, 4u * sizeof(uintptr_t)))
        return 0;
    for (index = 0; index < 4u; ++index) {
        if (table[index] != (uintptr_t)module + expected[index])
            return 0;
    }
    return 1;
}

static int dluma_player_validate_code_prefix(
    HMODULE module,
    uintptr_t rva,
    const unsigned char* expected,
    SIZE_T expected_size)
{
    const unsigned char* target;
    MEMORY_BASIC_INFORMATION info;
    DWORD protection;
    if (!module || !expected || !expected_size) return 0;
    target = (const unsigned char*)module + rva;
    if (!dluma_player_memory_readable(target, expected_size) ||
        VirtualQuery(target, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT)
        return 0;
    protection = info.Protect & 0xFFu;
    if (protection != PAGE_EXECUTE &&
        protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY)
        return 0;
    return memcmp(target, expected, expected_size) == 0;
}

static int dluma_player_validate_adapter(void)
{
    static const unsigned char get_fvalue_prefix[] = {
        0x48, 0x8B, 0x01, 0x4C, 0x63, 0xC2, 0x4E, 0x8B,
        0x0C, 0xC0, 0x8D, 0x82, 0x97, 0xFD, 0xFF, 0xFF,
        0x83, 0xF8, 0x29, 0x77, 0x63, 0x48, 0x8B, 0x05,
        0x6C, 0x5E, 0x13, 0x01, 0x48, 0x8B, 0x80, 0x08
    };
    static const uintptr_t state_functions[4] = {
        DLUMA_PLAYER_STATE_VFUNC0_RVA,
        DLUMA_PLAYER_STATE_VFUNC1_RVA,
        DLUMA_PLAYER_STATE_VFUNC2_RVA,
        DLUMA_PLAYER_STATE_VFUNC3_RVA
    };
    static const uintptr_t float_functions[4] = {
        DLUMA_PLAYER_FLOAT_VFUNC0_RVA,
        DLUMA_PLAYER_FLOAT_VFUNC1_RVA,
        DLUMA_PLAYER_FLOAT_VFUNC2_RVA,
        DLUMA_PLAYER_FLOAT_VFUNC3_RVA
    };
    HMODULE engine;
    HMODULE gamedll;
    if (g_dluma_player_static_validation != 0)
        return g_dluma_player_static_validation > 0;
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    gamedll = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    /* Module absence is transient during early loader/bootstrap validation.
     * Do not permanently poison the capability before the game DLLs exist. */
    if (!engine || !gamedll) return 0;
    if (!dluma_player_validate_pe(
            engine,
            DLUMA_PLAYER_ENGINE_TIMESTAMP,
            DLUMA_PLAYER_ENGINE_IMAGE_SIZE) ||
        !dluma_player_validate_pe(
            gamedll,
            DLUMA_PLAYER_GAMEDLL_TIMESTAMP,
            DLUMA_PLAYER_GAMEDLL_IMAGE_SIZE) ||
        !dluma_player_validate_vtable(
            gamedll,
            DLUMA_PLAYER_STATE_VTABLE_RVA,
            state_functions) ||
        !dluma_player_validate_vtable(
            gamedll,
            DLUMA_PLAYER_FLOAT_VARIABLE_VTABLE_RVA,
            float_functions) ||
        !dluma_player_validate_code_prefix(
            gamedll,
            DLUMA_PLAYER_GET_FVALUE_RVA,
            get_fvalue_prefix,
            sizeof(get_fvalue_prefix))) {
        g_dluma_player_static_validation = -1;
        return 0;
    }
    g_dluma_player_gamedll = gamedll;
    g_dluma_player_get_fvalue = (DlumaPlayerGetFValueFn)(
        (unsigned char*)gamedll + DLUMA_PLAYER_GET_FVALUE_RVA);
    g_dluma_player_static_validation = 1;
    return 1;
}

int dluma_player_variables_binding_configured(void)
{
    return 1;
}

int dluma_player_variables_binding_ready(void)
{
    return dluma_player_validate_adapter();
}

int dluma_player_variables_binding_local_state(void** out_player_state)
{
    void** global;
    void* player_state;
    if (out_player_state) *out_player_state = 0;
    if (!dluma_player_validate_adapter() || !g_dluma_player_gamedll)
        return 0;
    global = (void**)(
        (unsigned char*)g_dluma_player_gamedll +
        DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA);
    if (!dluma_player_memory_readable(global, sizeof(*global)))
        return 0;
    player_state = *global;
    if (!player_state ||
        !dluma_player_memory_readable(player_state, sizeof(void*)) ||
        *(uintptr_t*)player_state !=
            (uintptr_t)g_dluma_player_gamedll +
            DLUMA_PLAYER_STATE_VTABLE_RVA)
        return 0;
    if (out_player_state) *out_player_state = player_state;
    return 1;
}

uint32_t dluma_player_variables_binding_definition_count(void)
{
    return (uint32_t)(
        sizeof(g_dluma_player_definitions) /
        sizeof(g_dluma_player_definitions[0]));
}

const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_at(uint32_t index)
{
    if (index >= dluma_player_variables_binding_definition_count())
        return 0;
    return &g_dluma_player_definitions[index];
}

const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_named(const char* exact_name)
{
    uint32_t index;
    if (!exact_name) return 0;
    for (index = 0;
         index < dluma_player_variables_binding_definition_count();
         ++index) {
        if (dluma_player_string_equal(
                exact_name,
                g_dluma_player_definitions[index].name))
            return &g_dluma_player_definitions[index];
    }
    return 0;
}

const DlumaPlayerVariableDefinition*
dluma_player_variables_binding_definition_id(uint32_t engine_id)
{
    uint32_t index;
    for (index = 0;
         index < dluma_player_variables_binding_definition_count();
         ++index) {
        if (g_dluma_player_definitions[index].engine_id == engine_id)
            return &g_dluma_player_definitions[index];
    }
    return 0;
}

int dluma_player_variables_binding_read(
    const DlumaPlayerVariableDefinition* definition,
    DlumaPlayerVariableSnapshot* snapshot)
{
    void* player_state;
    void* variables;
    void** variable_table;
    void* variable_record;
    float* variable;
    if (!definition || !snapshot ||
        !dluma_player_variables_binding_local_state(&player_state) ||
        !dluma_player_memory_readable(
            (unsigned char*)player_state +
                DLUMA_PLAYER_STATE_VARIABLES_OFFSET,
            sizeof(void*)))
        return 0;
    variables = *(void**)(
        (unsigned char*)player_state +
        DLUMA_PLAYER_STATE_VARIABLES_OFFSET);
    if (!variables ||
        !dluma_player_memory_readable(variables, sizeof(void*)) ||
        !(variable_table = *(void***)variables) ||
        !dluma_player_memory_readable(
            variable_table + definition->engine_id,
            sizeof(*variable_table)))
        return 0;
    variable_record = variable_table[definition->engine_id];
    if (definition->engine_id == 969u || definition->engine_id == 970u) {
        unsigned char bits;
        if (!variable_record ||
            !dluma_player_memory_readable(variable_record, sizeof(void*) + 1u) ||
            *(uintptr_t*)variable_record !=
                (uintptr_t)g_dluma_player_gamedll + DLUMA_PLAYER_BOOL_VARIABLE_VTABLE_RVA)
            return 0;
        bits = *((unsigned char*)variable_record + 8);
        snapshot->player_state = player_state;
        snapshot->variables = variables;
        snapshot->variable_record = variable_record;
        snapshot->definition = definition;
        snapshot->value = (float)(bits & 1u);
        snapshot->base = (float)((bits >> 1) & 1u);
        snapshot->add = 0.0f;
        snapshot->multiplier = 0.0f;
        return 1;
    }
    if (!variable_record ||
        !dluma_player_memory_readable(
            variable_record,
            sizeof(void*) + 4u * sizeof(float)) ||
        *(uintptr_t*)variable_record !=
            (uintptr_t)g_dluma_player_gamedll +
            DLUMA_PLAYER_FLOAT_VARIABLE_VTABLE_RVA)
        return 0;
    variable = (float*)variable_record;
    snapshot->player_state = player_state;
    snapshot->variables = variables;
    snapshot->variable_record = variable_record;
    snapshot->definition = definition;
    snapshot->value = g_dluma_player_get_fvalue(
        variables, (int)definition->engine_id);
    snapshot->base = variable[3];
    snapshot->add = variable[4];
    snapshot->multiplier = variable[5];
    return dluma_player_float_finite(snapshot->value) &&
           dluma_player_float_finite(snapshot->base) &&
           dluma_player_float_finite(snapshot->add) &&
           dluma_player_float_finite(snapshot->multiplier);
}

int dluma_player_variables_binding_write_base(
    const DlumaPlayerVariableSnapshot* snapshot,
    float base)
{
    float add;
    float derived;
    float multiplier;
    float* variable;
    if (snapshot && snapshot->definition &&
        (snapshot->definition->engine_id == 969u || snapshot->definition->engine_id == 970u)) {
        unsigned char* bits;
        unsigned char* dirty;
        void* current_state = 0;
        if (!snapshot->variable_record || !snapshot->player_state ||
            (base != 0.0f && base != 1.0f) ||
            !dluma_player_variables_binding_local_state(&current_state) ||
            current_state != snapshot->player_state ||
            !dluma_player_memory_readable(snapshot->variable_record, sizeof(void*) + 1u) ||
            *(uintptr_t*)snapshot->variable_record !=
                (uintptr_t)g_dluma_player_gamedll + DLUMA_PLAYER_BOOL_VARIABLE_VTABLE_RVA)
            return 0;
        bits = (unsigned char*)snapshot->variable_record + 8;
        dirty = (unsigned char*)snapshot->player_state + DLUMA_PLAYER_STATE_VARIABLES_DIRTY_OFFSET;
        if (!dluma_player_memory_writable(bits, 1) || !dluma_player_memory_writable(dirty, 1))
            return 0;
        /* Leave the effective bit set when restoring false; the pending
         * recomputation decides whether another active buff still owns it. */
        *bits = (unsigned char)((*bits & 0xfdu) | (base == 1.0f ? 3u : 0u));
        /* PlayerState's next update reapplies current buffs to the base. */
        *dirty |= 1u;
        return 1;
    }
    if (!snapshot || !snapshot->variable_record ||
        !dluma_player_float_finite(base) ||
        !dluma_player_memory_writable(
            (unsigned char*)snapshot->variable_record + 8,
            4u * sizeof(float)))
        return 0;
    variable = (float*)snapshot->variable_record;
    add = variable[4];
    multiplier = variable[5];
    if (!dluma_player_float_finite(add) ||
        !dluma_player_float_finite(multiplier))
        return 0;
    derived = (multiplier + 1.0f) * base + add;
    if (!dluma_player_float_finite(derived))
        return 0;
    variable[3] = base;
    variable[2] = derived;
    return 1;
}
