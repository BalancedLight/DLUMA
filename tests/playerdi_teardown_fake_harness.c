#include "dlw_playerdi_teardown.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                    #condition, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)

typedef struct FakeMemory {
    unsigned char source_header[20];
    unsigned char destination_header[20];
    uintptr_t source_header_address;
    uintptr_t destination_header_address;
    uintptr_t source_region_base;
    size_t source_region_size;
    uintptr_t destination_region_base;
    size_t destination_region_size;
    uintptr_t destination_element_address;
    uintptr_t destination_element_value;
    int source_header_readable;
} FakeMemory;

static int g_original_calls;
static int g_original_failures;
static int g_nested;
static void* g_expected_player;

extern int dluma_playerdi_abi_probe(
    DlumaPlayerDiOnDestroyOriginal original,
    void* player_di);

static void DLUMA_PLAYERDI_FASTCALL fake_original(
    void* player_di, float destroy_time)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    ++g_original_calls;
    if (player_di != g_expected_player &&
        player_di != (void*)(uintptr_t)0x22222000U)
        ++g_original_failures;
    if (destroy_time < 1.0f || destroy_time > 3.0f)
        ++g_original_failures;
    dluma_playerdi_teardown_snapshot(&diagnostics);
    if (diagnostics.active_player != (uintptr_t)player_di ||
        diagnostics.active_inventory_owner !=
            (uintptr_t)player_di +
                DLUMA_PLAYERDI_INVENTORY_OWNER_OFFSET ||
        diagnostics.active_thread == 0U ||
        diagnostics.active_scope_depth == 0U ||
        diagnostics.active_original_entered != 1U ||
        diagnostics.active_original_returned != 0U)
        ++g_original_failures;
    if (g_nested && g_original_calls == 1) {
        dluma_playerdi_ondestroy_observe_call(
            fake_original,
            (void*)(uintptr_t)0x22222000U,
            2.0f);
    }
}

static void write_header(
    unsigned char* output,
    uintptr_t data,
    uint32_t count,
    uint32_t capacity,
    uint32_t tag)
{
    memcpy(output, &data, sizeof(data));
    memcpy(output + 8, &count, sizeof(count));
    memcpy(output + 12, &capacity, sizeof(capacity));
    memcpy(output + 16, &tag, sizeof(tag));
}

static int fake_read(
    void* user, uintptr_t address, void* output, size_t size)
{
    FakeMemory* memory = (FakeMemory*)user;
    if (address == memory->source_header_address &&
        size == sizeof(memory->source_header) &&
        memory->source_header_readable) {
        memcpy(output, memory->source_header, size);
        return 1;
    }
    if (address == memory->destination_header_address &&
        size == sizeof(memory->destination_header)) {
        memcpy(output, memory->destination_header, size);
        return 1;
    }
    if (address == memory->destination_element_address &&
        size == sizeof(uintptr_t)) {
        memcpy(output, &memory->destination_element_value, size);
        return 1;
    }
    return 0;
}

static int fake_query(
    void* user,
    uintptr_t address,
    DlumaPlayerDiMemoryRegion* output)
{
    FakeMemory* memory = (FakeMemory*)user;
    memset(output, 0, sizeof(*output));
    output->state = 0x1000U;
    output->protection = 0x04U;
    output->query_succeeded = 1;
    if (address >= memory->source_region_base &&
        address < memory->source_region_base +
                    memory->source_region_size) {
        output->base = memory->source_region_base;
        output->size = memory->source_region_size;
        output->address_in_region = 1;
        return 1;
    }
    if (address >= memory->destination_region_base &&
        address < memory->destination_region_base +
                    memory->destination_region_size) {
        output->base = memory->destination_region_base;
        output->size = memory->destination_region_size;
        output->address_in_region = 1;
        return 1;
    }
    output->base = address & ~(uintptr_t)0xFFFU;
    output->size = 0x1000U;
    output->state = 0x10000U;
    output->protection = 0x01U;
    return 1;
}

static int test_relay_contract(void)
{
    static const unsigned char prefix[] = {
        0x48,0x89,0x5C,0x24,0x08,
        0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,
        0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
    };
    DlumaPlayerDiRelayValidation result;
    unsigned char corrupted[sizeof(prefix)];
    CHECK(dluma_playerdi_validate_ondestroy_relay(
        prefix, sizeof(prefix), &result));
    CHECK(result.exact_prefix);
    CHECK(result.stolen_size == 20U);
    CHECK(result.instruction_boundaries[5] == 20U);
    CHECK(result.resume_instruction_offset == 20U);
    CHECK(!result.has_relative_branch);
    CHECK(!result.has_rip_relative_operand);
    memcpy(corrupted, prefix, sizeof(prefix));
    corrupted[19] ^= 1U;
    CHECK(!dluma_playerdi_validate_ondestroy_relay(
        corrupted, sizeof(corrupted), &result));
    return 1;
}

static int test_observer_calls_original_once_and_preserves_abi(void)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    g_original_calls = 0;
    g_original_failures = 0;
    g_nested = 0;
    g_expected_player = (void*)(uintptr_t)0x11111000U;
    dluma_playerdi_teardown_reset_diagnostics();
    dluma_playerdi_teardown_configure(1, 1, 1);
    CHECK(dluma_playerdi_abi_probe(
        fake_original, g_expected_player));
    dluma_playerdi_teardown_snapshot(&diagnostics);
    CHECK(g_original_calls == 1);
    CHECK(g_original_failures == 0);
    CHECK(diagnostics.original_enter_count == 1U);
    CHECK(diagnostics.original_return_count == 1U);
    CHECK(diagnostics.active_depth == 0U);
    CHECK(diagnostics.active_player == 0U);
    CHECK(diagnostics.active_inventory_owner == 0U);
    CHECK(diagnostics.active_original_entered == 0U);
    CHECK(diagnostics.active_original_returned == 0U);
    CHECK(diagnostics.last_player ==
          (uintptr_t)g_expected_player);
    return 1;
}

static int test_disabled_configuration_has_no_active_observer(void)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    dluma_playerdi_teardown_reset_diagnostics();
    dluma_playerdi_teardown_configure(0, 0, 1);
    dluma_playerdi_teardown_snapshot(&diagnostics);
    CHECK(!diagnostics.observer_enabled);
    CHECK(!diagnostics.diagnostic_opt_in);
    CHECK(diagnostics.supported_profile_validated);
    CHECK(diagnostics.original_enter_count == 0U);
    CHECK(diagnostics.original_return_count == 0U);
    CHECK(diagnostics.active_depth == 0U);
    return 1;
}

static int test_nested_observer_is_pass_through_once_per_entry(void)
{
    DlumaPlayerDiTeardownDiagnostics diagnostics;
    g_original_calls = 0;
    g_original_failures = 0;
    g_nested = 1;
    g_expected_player = (void*)(uintptr_t)0x11111000U;
    dluma_playerdi_teardown_reset_diagnostics();
    dluma_playerdi_ondestroy_observe_call(
        fake_original, g_expected_player, 1.0f);
    dluma_playerdi_teardown_snapshot(&diagnostics);
    CHECK(g_original_calls == 2);
    CHECK(g_original_failures == 0);
    CHECK(diagnostics.original_enter_count == 2U);
    CHECK(diagnostics.original_return_count == 2U);
    CHECK(diagnostics.nested_pass_through_count == 1U);
    CHECK(diagnostics.active_depth == 0U);
    CHECK(diagnostics.active_player == 0U);
    return 1;
}

static int test_exact_fault_decoder(void)
{
    const uintptr_t source_data =
        (uintptr_t)0x00000001F37C37D0ULL;
    const uintptr_t access_target =
        (uintptr_t)0x00000001F37EF000ULL;
    const uintptr_t destination_data =
        (uintptr_t)0x00000002037E7050ULL;
    DlumaPlayerDiVectorCrashInput input;
    DlumaPlayerDiVectorCrashEvidence evidence;
    FakeMemory memory;
    memset(&input, 0, sizeof(input));
    memset(&memory, 0, sizeof(memory));
    memory.source_header_address =
        (uintptr_t)0x0000000156F8A6F0ULL;
    memory.destination_header_address =
        (uintptr_t)0x000000000EDADA08ULL;
    memory.source_region_base =
        (uintptr_t)0x00000001F37C3000ULL;
    memory.source_region_size = 0x2C000U;
    memory.destination_region_base =
        (uintptr_t)0x00000002037E7000ULL;
    memory.destination_region_size = 0x40000U;
    memory.destination_element_address =
        destination_data + 0x2B830U;
    memory.destination_element_value =
        (uintptr_t)0x123456789ABCDEF0ULL;
    memory.source_header_readable = 1;
    write_header(
        memory.source_header, source_data,
        22279U, 22279U, 59U);
    write_header(
        memory.destination_header, destination_data,
        22279U, 22279U, 59U);

    input.module_timestamp =
        DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_TIMESTAMP;
    input.module_image_size =
        DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_IMAGE_SIZE;
    input.fault_rva = DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA;
    input.supported_profile_validated = 1;
    input.instruction_signature_validated = 1;
    input.access_operation_is_read = 1;
    input.access_target = access_target;
    input.rbx = 0x5706U;
    input.rdi = memory.source_header_address;
    input.rsi = memory.destination_header_address;
    CHECK(dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    CHECK(evidence.accepted);
    CHECK(evidence.element_index == 0x5706U);
    CHECK(evidence.element_byte_offset == 0x2B830U);
    CHECK(evidence.source.data == source_data);
    CHECK(evidence.computed_source_element == access_target);
    CHECK(evidence.inferred_source_data == source_data);
    CHECK(evidence.fault_matches_computed_source_element);
    CHECK(evidence.index_less_than_count);
    CHECK(evidence.index_less_than_capacity);
    CHECK(evidence.count_not_greater_than_capacity);
    CHECK(evidence.destination_index_less_than_count);
    CHECK(evidence.destination_index_less_than_capacity);
    CHECK(evidence.destination_count_not_greater_than_capacity);
    CHECK(evidence.source_bytes_readable_to_region_end ==
          0x2B830U);
    CHECK(evidence.source_qwords_readable_to_region_end ==
          0x5706U);
    CHECK(!evidence.source_element_readable);
    CHECK(evidence.destination_element_readable);
    CHECK(evidence.source_data_region.state == 0x1000U);
    CHECK(evidence.source_data_region.protection == 0x04U);
    CHECK(evidence.source_element_region.state == 0x10000U);
    CHECK(evidence.source_element_region.protection == 0x01U);

    write_header(
        memory.source_header, source_data,
        22280U, 22279U, 59U);
    CHECK(dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    CHECK(!evidence.count_not_greater_than_capacity);

    input.fault_rva += 1U;
    CHECK(!dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    input.fault_rva = DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA;
    input.module_timestamp ^= 1U;
    CHECK(!dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    input.module_timestamp =
        DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_TIMESTAMP;
    input.instruction_signature_validated = 0;
    CHECK(!dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    input.instruction_signature_validated = 1;
    input.access_operation_is_read = 0;
    CHECK(!dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    return 1;
}

static int test_unreadable_header_is_bounded(void)
{
    DlumaPlayerDiVectorCrashInput input;
    DlumaPlayerDiVectorCrashEvidence evidence;
    FakeMemory memory;
    memset(&input, 0, sizeof(input));
    memset(&memory, 0, sizeof(memory));
    memory.source_header_address = (uintptr_t)0x11110000U;
    memory.destination_header_address = (uintptr_t)0x22220000U;
    memory.source_header_readable = 0;
    write_header(
        memory.destination_header,
        (uintptr_t)0x33330000U, 4U, 4U, 59U);
    input.module_timestamp =
        DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_TIMESTAMP;
    input.module_image_size =
        DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_IMAGE_SIZE;
    input.fault_rva = DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA;
    input.supported_profile_validated = 1;
    input.instruction_signature_validated = 1;
    input.access_operation_is_read = 1;
    input.access_target = (uintptr_t)0x44440000U;
    input.rbx = 3U;
    input.rdi = memory.source_header_address;
    input.rsi = memory.destination_header_address;
    CHECK(dluma_playerdi_decode_vector_copy_fault(
        &input, fake_read, fake_query, &memory, &evidence));
    CHECK(evidence.accepted);
    CHECK(!evidence.source_header_readable);
    CHECK(!evidence.computed_source_element_valid);
    CHECK(evidence.inferred_source_data ==
          input.access_target - 24U);
    return 1;
}

static int test_inventory_layout_remains_fail_closed(void)
{
    CHECK(!dluma_playerdi_inventory_header_layout_validated());
    return 1;
}

int main(void)
{
    CHECK(test_relay_contract());
    CHECK(test_disabled_configuration_has_no_active_observer());
    CHECK(test_observer_calls_original_once_and_preserves_abi());
    CHECK(test_nested_observer_is_pass_through_once_per_entry());
    CHECK(test_exact_fault_decoder());
    CHECK(test_unreadable_header_is_bounded());
    CHECK(test_inventory_layout_remains_fail_closed());
    puts("playerdi teardown fake harness: PASS");
    return 0;
}
