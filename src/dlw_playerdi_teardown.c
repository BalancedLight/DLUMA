#include "dlw_common.h"
#include "dlw_playerdi_teardown.h"

static volatile LONG g_playerdi_observer_enabled;
static volatile LONG g_playerdi_diagnostic_opt_in;
static volatile LONG g_playerdi_supported_profile_validated;
static volatile LONG g_playerdi_original_enter_count;
static volatile LONG g_playerdi_original_return_count;
static volatile LONG g_playerdi_active_depth;
static volatile LONG g_playerdi_nested_pass_through_count;
static PVOID volatile g_playerdi_last_player;
static volatile LONG g_playerdi_last_thread;
static volatile LONGLONG g_playerdi_last_sequence;
static PVOID volatile g_playerdi_active_player;
static PVOID volatile g_playerdi_active_inventory_owner;
static volatile LONG g_playerdi_active_thread;
static volatile LONG g_playerdi_active_scope_depth;
static volatile LONG g_playerdi_active_original_entered;
static volatile LONG g_playerdi_active_original_returned;
static volatile LONGLONG g_playerdi_active_sequence;

static const unsigned char g_playerdi_ondestroy_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,
    0x48,0x89,0x6C,0x24,0x10,
    0x48,0x89,0x74,0x24,0x18,
    0x57,
    0x41,0x54,
    0x41,0x55,
    0x41,0x56,
    0x41,0x57
};

static const unsigned char g_playerdi_vector_copy_signature[] = {
    0x48,0x2B,0xD1,
    0x0F,0x1F,0x40,0x00,
    0x48,0x85,0xC9,
    0x74,0x07,
    0x48,0x8B,0x04,0x0A,
    0x48,0x89,0x01,
    0xFF,0xC3,
    0x48,0x83,0xC1,0x08,
    0x3B,0x5F,0x08,
    0x72,0xE9
};

static uintptr_t dluma_playerdi_atomic_read_pointer(
    PVOID volatile* value)
{
    return (uintptr_t)dluma_atomic_pointer_compare_exchange(
        value, 0, 0);
}

static uint64_t dluma_playerdi_atomic_read_u64(
    volatile LONGLONG* value)
{
    return (uint64_t)_InterlockedCompareExchange64(value, 0, 0);
}

void dluma_playerdi_teardown_configure(
    int observer_enabled,
    int diagnostic_opt_in,
    int supported_profile_validated)
{
    dluma_atomic_exchange(
        &g_playerdi_diagnostic_opt_in,
        diagnostic_opt_in ? 1 : 0);
    dluma_atomic_exchange(
        &g_playerdi_observer_enabled,
        observer_enabled && diagnostic_opt_in ? 1 : 0);
    dluma_atomic_exchange(
        &g_playerdi_supported_profile_validated,
        supported_profile_validated ? 1 : 0);
}

void dluma_playerdi_teardown_reset_diagnostics(void)
{
    dluma_atomic_exchange(&g_playerdi_original_enter_count, 0);
    dluma_atomic_exchange(&g_playerdi_original_return_count, 0);
    dluma_atomic_exchange(&g_playerdi_active_depth, 0);
    dluma_atomic_exchange(&g_playerdi_nested_pass_through_count, 0);
    dluma_atomic_pointer_exchange(&g_playerdi_last_player, 0);
    dluma_atomic_exchange(&g_playerdi_last_thread, 0);
    _InterlockedExchange64(&g_playerdi_last_sequence, 0);
    dluma_atomic_pointer_exchange(&g_playerdi_active_player, 0);
    dluma_atomic_pointer_exchange(
        &g_playerdi_active_inventory_owner, 0);
    dluma_atomic_exchange(&g_playerdi_active_thread, 0);
    dluma_atomic_exchange(&g_playerdi_active_scope_depth, 0);
    dluma_atomic_exchange(&g_playerdi_active_original_entered, 0);
    dluma_atomic_exchange(&g_playerdi_active_original_returned, 0);
    _InterlockedExchange64(&g_playerdi_active_sequence, 0);
}

void dluma_playerdi_teardown_snapshot(
    DlumaPlayerDiTeardownDiagnostics* output)
{
    if (!output) return;
    memset(output, 0, sizeof(*output));
    output->observer_enabled = (uint32_t)dluma_atomic_compare_exchange(
        &g_playerdi_observer_enabled, 0, 0);
    output->diagnostic_opt_in = (uint32_t)dluma_atomic_compare_exchange(
        &g_playerdi_diagnostic_opt_in, 0, 0);
    output->supported_profile_validated =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_supported_profile_validated, 0, 0);
    output->original_enter_count =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_original_enter_count, 0, 0);
    output->original_return_count =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_original_return_count, 0, 0);
    output->active_depth = (uint32_t)dluma_atomic_compare_exchange(
        &g_playerdi_active_depth, 0, 0);
    output->nested_pass_through_count =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_nested_pass_through_count, 0, 0);
    output->last_player =
        dluma_playerdi_atomic_read_pointer(&g_playerdi_last_player);
    output->last_thread = (uint32_t)dluma_atomic_compare_exchange(
        &g_playerdi_last_thread, 0, 0);
    output->last_sequence =
        dluma_playerdi_atomic_read_u64(&g_playerdi_last_sequence);
    output->active_player =
        dluma_playerdi_atomic_read_pointer(&g_playerdi_active_player);
    output->active_inventory_owner =
        dluma_playerdi_atomic_read_pointer(
            &g_playerdi_active_inventory_owner);
    output->active_thread = (uint32_t)dluma_atomic_compare_exchange(
        &g_playerdi_active_thread, 0, 0);
    output->active_scope_depth =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_active_scope_depth, 0, 0);
    output->active_original_entered =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_active_original_entered, 0, 0);
    output->active_original_returned =
        (uint32_t)dluma_atomic_compare_exchange(
            &g_playerdi_active_original_returned, 0, 0);
    output->active_sequence =
        dluma_playerdi_atomic_read_u64(&g_playerdi_active_sequence);
}

void dluma_playerdi_ondestroy_observe_call(
    DlumaPlayerDiOnDestroyOriginal original,
    void* player_di,
    float destroy_time)
{
    LONG depth;
    LONGLONG sequence;
    DWORD thread_id;
    void* previous_player;
    void* previous_inventory_owner;
    LONG previous_thread;
    LONG previous_scope_depth;
    LONG previous_original_entered;
    LONG previous_original_returned;
    LONGLONG previous_sequence;

    if (!original) return;

    thread_id = GetCurrentThreadId();
    sequence = _InterlockedIncrement64(&g_playerdi_last_sequence);
    depth = dluma_atomic_increment(&g_playerdi_active_depth);
    dluma_atomic_increment(&g_playerdi_original_enter_count);
    dluma_atomic_pointer_exchange(&g_playerdi_last_player, player_di);
    dluma_atomic_exchange(&g_playerdi_last_thread, (LONG)thread_id);

    previous_player = dluma_atomic_pointer_compare_exchange(
        &g_playerdi_active_player, 0, 0);
    previous_inventory_owner = dluma_atomic_pointer_compare_exchange(
        &g_playerdi_active_inventory_owner, 0, 0);
    previous_thread = dluma_atomic_compare_exchange(
        &g_playerdi_active_thread, 0, 0);
    previous_scope_depth = dluma_atomic_compare_exchange(
        &g_playerdi_active_scope_depth, 0, 0);
    previous_original_entered = dluma_atomic_compare_exchange(
        &g_playerdi_active_original_entered, 0, 0);
    previous_original_returned = dluma_atomic_compare_exchange(
        &g_playerdi_active_original_returned, 0, 0);
    previous_sequence = _InterlockedCompareExchange64(
        &g_playerdi_active_sequence, 0, 0);

    if (depth > 1)
        dluma_atomic_increment(&g_playerdi_nested_pass_through_count);
    dluma_atomic_pointer_exchange(
        &g_playerdi_active_player, player_di);
    dluma_atomic_pointer_exchange(
        &g_playerdi_active_inventory_owner,
        player_di
            ? (void*)((uintptr_t)player_di +
                      DLUMA_PLAYERDI_INVENTORY_OWNER_OFFSET)
            : 0);
    dluma_atomic_exchange(&g_playerdi_active_thread, (LONG)thread_id);
    dluma_atomic_exchange(&g_playerdi_active_scope_depth, depth);
    dluma_atomic_exchange(&g_playerdi_active_original_entered, 1);
    dluma_atomic_exchange(&g_playerdi_active_original_returned, 0);
    _InterlockedExchange64(&g_playerdi_active_sequence, sequence);

    original(player_di, destroy_time);

    dluma_atomic_increment(&g_playerdi_original_return_count);
    dluma_atomic_exchange(&g_playerdi_active_original_returned, 1);
    if (depth > 1) {
        dluma_atomic_pointer_exchange(
            &g_playerdi_active_player, previous_player);
        dluma_atomic_pointer_exchange(
            &g_playerdi_active_inventory_owner,
            previous_inventory_owner);
        dluma_atomic_exchange(
            &g_playerdi_active_thread, previous_thread);
        dluma_atomic_exchange(
            &g_playerdi_active_scope_depth, previous_scope_depth);
        dluma_atomic_exchange(
            &g_playerdi_active_original_entered,
            previous_original_entered);
        dluma_atomic_exchange(
            &g_playerdi_active_original_returned,
            previous_original_returned);
        _InterlockedExchange64(
            &g_playerdi_active_sequence, previous_sequence);
    } else {
        dluma_atomic_pointer_exchange(&g_playerdi_active_player, 0);
        dluma_atomic_pointer_exchange(
            &g_playerdi_active_inventory_owner, 0);
        dluma_atomic_exchange(&g_playerdi_active_thread, 0);
        dluma_atomic_exchange(&g_playerdi_active_scope_depth, 0);
        dluma_atomic_exchange(&g_playerdi_active_original_entered, 0);
        dluma_atomic_exchange(&g_playerdi_active_original_returned, 0);
        _InterlockedExchange64(&g_playerdi_active_sequence, sequence);
    }
    dluma_atomic_decrement(&g_playerdi_active_depth);
}

int dluma_playerdi_validate_ondestroy_relay(
    const unsigned char* bytes,
    size_t byte_count,
    DlumaPlayerDiRelayValidation* output)
{
    static const uint32_t boundaries[] = {5U,10U,15U,16U,18U,20U,22U};
    unsigned int index;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    output->stolen_size = DLUMA_PLAYERDI_ONDESTROY_STOLEN_SIZE;
    output->instruction_count = DLUMA_ARRAY_COUNT(boundaries);
    for (index = 0; index < DLUMA_ARRAY_COUNT(boundaries); ++index)
        output->instruction_boundaries[index] = boundaries[index];
    output->resume_instruction_offset =
        DLUMA_PLAYERDI_ONDESTROY_STOLEN_SIZE;
    if (!bytes || byte_count < sizeof(g_playerdi_ondestroy_prefix))
        return 0;
    output->exact_prefix =
        memcmp(
            bytes, g_playerdi_ondestroy_prefix,
            sizeof(g_playerdi_ondestroy_prefix)) == 0;
    output->has_relative_branch = 0;
    output->has_rip_relative_operand = 0;
    output->ends_on_instruction_boundary =
        boundaries[5] == DLUMA_PLAYERDI_ONDESTROY_STOLEN_SIZE;
    return output->exact_prefix &&
        output->ends_on_instruction_boundary;
}

int dluma_playerdi_vector_copy_signature_matches(
    const unsigned char* bytes,
    size_t byte_count)
{
    return bytes &&
        byte_count >= sizeof(g_playerdi_vector_copy_signature) &&
        memcmp(
            bytes, g_playerdi_vector_copy_signature,
            sizeof(g_playerdi_vector_copy_signature)) == 0;
}

static int dluma_playerdi_add_offset(
    uintptr_t base, uint64_t offset, uintptr_t* output)
{
    uintptr_t maximum = ~(uintptr_t)0;
    if (!output || offset > (uint64_t)maximum ||
        base > maximum - (uintptr_t)offset)
        return 0;
    *output = base + (uintptr_t)offset;
    return 1;
}

static void dluma_playerdi_query_region(
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    uintptr_t address,
    DlumaPlayerDiMemoryRegion* output)
{
    if (!output) return;
    memset(output, 0, sizeof(*output));
    if (query_memory &&
        query_memory(callback_user, address, output)) {
        uintptr_t region_end;
        output->query_succeeded = 1;
        if (output->base <= address &&
            output->size <= ~(uintptr_t)0 - output->base) {
            region_end = output->base + output->size;
            output->address_in_region =
                address < region_end ? 1U : 0U;
        }
    }
}

static int dluma_playerdi_read_header(
    DlumaPlayerDiReadMemory read_memory,
    void* callback_user,
    uintptr_t address,
    DlumaPlayerDiVectorHeader* output)
{
    unsigned char bytes[20];
    if (!read_memory || !output ||
        !read_memory(callback_user, address, bytes, sizeof(bytes)))
        return 0;
    memset(output, 0, sizeof(*output));
    memcpy(&output->data, bytes, sizeof(output->data));
    memcpy(&output->count, bytes + 8, sizeof(output->count));
    memcpy(&output->capacity, bytes + 12, sizeof(output->capacity));
    memcpy(&output->tag_or_field, bytes + 16, sizeof(output->tag_or_field));
    return 1;
}

static uint64_t dluma_playerdi_bytes_to_region_end(
    uintptr_t address,
    const DlumaPlayerDiMemoryRegion* region)
{
    uintptr_t region_end;
    uint32_t basic_protection;
    if (!region || !region->query_succeeded ||
        !region->address_in_region ||
        region->size > ~(uintptr_t)0 - region->base)
        return 0;
    basic_protection = region->protection & 0xFFU;
    if (region->state != 0x1000U ||
        (region->protection & 0x100U) != 0U ||
        (basic_protection != 0x02U &&
         basic_protection != 0x04U &&
         basic_protection != 0x08U &&
         basic_protection != 0x20U &&
         basic_protection != 0x40U &&
         basic_protection != 0x80U))
        return 0;
    region_end = region->base + region->size;
    return (uint64_t)(region_end - address);
}

int dluma_playerdi_decode_vector_copy_fault(
    const DlumaPlayerDiVectorCrashInput* input,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerDiVectorCrashEvidence* output)
{
    uint64_t byte_offset;
    uintptr_t source_value = 0;
    uintptr_t destination_value = 0;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    if (!input ||
        input->module_timestamp !=
            DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_TIMESTAMP ||
        input->module_image_size !=
            DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_IMAGE_SIZE ||
        input->fault_rva != DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA ||
        !input->supported_profile_validated ||
        !input->instruction_signature_validated ||
        !input->access_operation_is_read)
        return 0;

    output->accepted = 1;
    output->exact_supported_signature = 1;
    output->source_vector_address = input->rdi;
    output->destination_vector_address = input->rsi;
    output->element_index = (uint32_t)input->rbx;
    output->index_multiplication_valid =
        input->rbx <= UINT32_MAX &&
        input->rbx <= UINT64_MAX / sizeof(uintptr_t) ? 1U : 0U;
    if (!output->index_multiplication_valid) return 1;
    byte_offset = (uint64_t)input->rbx * sizeof(uintptr_t);
    output->element_byte_offset = byte_offset;
    if (input->access_target >= byte_offset)
        output->inferred_source_data =
            input->access_target - (uintptr_t)byte_offset;

    dluma_playerdi_query_region(
        query_memory, callback_user, input->rdi,
        &output->source_header_region);
    dluma_playerdi_query_region(
        query_memory, callback_user, input->rsi,
        &output->destination_header_region);
    output->source_header_readable = dluma_playerdi_read_header(
        read_memory, callback_user, input->rdi, &output->source);
    output->destination_header_readable = dluma_playerdi_read_header(
        read_memory, callback_user, input->rsi, &output->destination);

    if (output->source_header_readable) {
        output->index_less_than_count =
            input->rbx < output->source.count ? 1U : 0U;
        output->index_less_than_capacity =
            input->rbx < output->source.capacity ? 1U : 0U;
        output->count_not_greater_than_capacity =
            output->source.count <= output->source.capacity ? 1U : 0U;
        output->computed_source_element_valid =
            dluma_playerdi_add_offset(
                output->source.data, byte_offset,
                &output->computed_source_element);
        dluma_playerdi_query_region(
            query_memory, callback_user, output->source.data,
            &output->source_data_region);
        if (output->computed_source_element_valid) {
            dluma_playerdi_query_region(
                query_memory, callback_user,
                output->computed_source_element,
                &output->source_element_region);
            output->fault_matches_computed_source_element =
                output->computed_source_element ==
                    input->access_target ? 1U : 0U;
            output->source_element_readable =
                read_memory &&
                read_memory(
                    callback_user,
                    output->computed_source_element,
                    &source_value, sizeof(source_value));
            if (output->source_element_readable)
                output->source_element_value = source_value;
        }
        output->source_bytes_readable_to_region_end =
            dluma_playerdi_bytes_to_region_end(
                output->source.data, &output->source_data_region);
        output->source_qwords_readable_to_region_end =
            output->source_bytes_readable_to_region_end /
                sizeof(uintptr_t);
    }

    if (output->destination_header_readable) {
        output->destination_index_less_than_count =
            input->rbx < output->destination.count ? 1U : 0U;
        output->destination_index_less_than_capacity =
            input->rbx < output->destination.capacity ? 1U : 0U;
        output->destination_count_not_greater_than_capacity =
            output->destination.count <= output->destination.capacity
                ? 1U
                : 0U;
        output->computed_destination_element_valid =
            dluma_playerdi_add_offset(
                output->destination.data, byte_offset,
                &output->computed_destination_element);
        dluma_playerdi_query_region(
            query_memory, callback_user, output->destination.data,
            &output->destination_data_region);
        if (output->computed_destination_element_valid) {
            dluma_playerdi_query_region(
                query_memory, callback_user,
                output->computed_destination_element,
                &output->destination_element_region);
            output->destination_element_readable =
                read_memory &&
                read_memory(
                    callback_user,
                    output->computed_destination_element,
                    &destination_value,
                    sizeof(destination_value));
            if (output->destination_element_readable)
                output->destination_element_value =
                    destination_value;
        }
    }
    return 1;
}

int dluma_playerdi_inventory_header_layout_validated(void)
{
    /*
     * Runtime 7 proves the qword-vector header used by the failing copy, but
     * not the five PlayerDI container pointers or their owning layouts.
     * Keep safe lifecycle snapshots disabled until those offsets are
     * independently established against the supported Windows image.
     */
    return 0;
}
