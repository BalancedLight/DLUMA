#include <float.h>
#include <stdio.h>
#include <string.h>

#include "../src/bindings/dlw_player_variables_binding.c"

#define CHECK(value) do { if (!(value)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

static float __fastcall test_get_fvalue(void* variables, int id)
{
    void** table = *(void***)variables;
    return ((float*)table[id])[2];
}

static int test_record_at_readable_boundary(void)
{
    SYSTEM_INFO system;
    unsigned char* pages;
    unsigned char* module;
    unsigned char state[DLUMA_PLAYER_STATE_VARIABLES_OFFSET + sizeof(void*)];
    void* table[1042] = {0};
    void** variables = table;
    float* record;
    DWORD previous;
    DlumaPlayerVariableSnapshot snapshot;
    const DlumaPlayerVariableDefinition* definition =
        dluma_player_variables_binding_definition_at(0);
    GetSystemInfo(&system);
    pages = (unsigned char*)VirtualAlloc(0, 2U * system.dwPageSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    module = (unsigned char*)VirtualAlloc(0,
        DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA + sizeof(void*),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(pages && module);
    CHECK(VirtualProtect(pages + system.dwPageSize, system.dwPageSize,
        PAGE_NOACCESS, &previous));
    /* Exact Windows FloatPlayerVariable allocation is 24 bytes. */
    record = (float*)(pages + system.dwPageSize - 24U);
    *(uintptr_t*)record = (uintptr_t)module + DLUMA_PLAYER_FLOAT_VARIABLE_VTABLE_RVA;
    record[2] = record[3] = 10.0f;
    record[4] = record[5] = 0.0f;
    memset(state, 0, sizeof(state));
    *(uintptr_t*)state = (uintptr_t)module + DLUMA_PLAYER_STATE_VTABLE_RVA;
    *(void**)(state + DLUMA_PLAYER_STATE_VARIABLES_OFFSET) = &variables;
    *(void**)(module + DLUMA_PLAYER_LOCAL_STATE_GLOBAL_RVA) = state;
    table[definition->engine_id] = record;
    g_dluma_player_static_validation = 1;
    g_dluma_player_gamedll = (HMODULE)module;
    g_dluma_player_get_fvalue = test_get_fvalue;
    memset(&snapshot, 0, sizeof(snapshot));
    CHECK(dluma_player_variables_binding_read(definition, &snapshot));
    CHECK(snapshot.base == 10.0f && snapshot.value == 10.0f);
    g_dluma_player_static_validation = 0;
    g_dluma_player_gamedll = 0;
    g_dluma_player_get_fvalue = 0;
    CHECK(VirtualFree(pages, 0, MEM_RELEASE));
    CHECK(VirtualFree(module, 0, MEM_RELEASE));
    return 0;
}

int main(void)
{
    DlumaPlayerVariableSnapshot snapshot;
    float record[6];

    memset(&snapshot, 0, sizeof(snapshot));
    memset(record, 0, sizeof(record));
    snapshot.variable_record = record;

    record[2] = 7.0f;
    record[3] = 3.0f;
    record[4] = 2.0f;
    record[5] = 0.5f;
    CHECK(dluma_player_variables_binding_write_base(&snapshot, 4.0f));
    CHECK(record[3] == 4.0f);
    CHECK(record[2] == 8.0f);

    record[2] = 17.0f;
    record[3] = 19.0f;
    record[4] = 0.0f;
    record[5] = FLT_MAX;
    CHECK(!dluma_player_variables_binding_write_base(&snapshot, 2.0f));
    CHECK(record[2] == 17.0f);
    CHECK(record[3] == 19.0f);

    record[4] = FLT_MAX;
    record[5] = 0.0f;
    CHECK(!dluma_player_variables_binding_write_base(&snapshot, FLT_MAX));
    CHECK(record[2] == 17.0f);
    CHECK(record[3] == 19.0f);

    CHECK(test_record_at_readable_boundary() == 0);
    puts("player variables transactional write: ok");
    return 0;
}
