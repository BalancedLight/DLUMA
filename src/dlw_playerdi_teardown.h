#ifndef DLUMA_PLAYERDI_TEARDOWN_H
#define DLUMA_PLAYERDI_TEARDOWN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_PLAYERDI_SUPPORTED_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_PLAYERDI_ONDESTROY_RVA 0x00B06420UL
#define DLUMA_PLAYERDI_ONDESTROY_STOLEN_SIZE 20U
#define DLUMA_PLAYERDI_VECTOR_COPY_FAULT_RVA 0x00BD1E45UL
#define DLUMA_PLAYERDI_VECTOR_COPY_SIGNATURE_RVA 0x00BD1E39UL
#define DLUMA_PLAYERDI_VECTOR_COPY_SIGNATURE_SIZE 30U
#define DLUMA_PLAYERDI_INVENTORY_OWNER_OFFSET 0x0928U

#if defined(_MSC_VER)
#define DLUMA_PLAYERDI_FASTCALL __fastcall
#else
#define DLUMA_PLAYERDI_FASTCALL
#endif

typedef void (DLUMA_PLAYERDI_FASTCALL *DlumaPlayerDiOnDestroyOriginal)(
    void* player_di, float destroy_time);

typedef struct DlumaPlayerDiTeardownDiagnostics {
    uint32_t observer_enabled;
    uint32_t diagnostic_opt_in;
    uint32_t supported_profile_validated;
    uint32_t original_enter_count;
    uint32_t original_return_count;
    uint32_t active_depth;
    uint32_t nested_pass_through_count;
    uintptr_t last_player;
    uint32_t last_thread;
    uint32_t reserved0;
    uint64_t last_sequence;
    uintptr_t active_player;
    uintptr_t active_inventory_owner;
    uint32_t active_thread;
    uint32_t active_scope_depth;
    uint32_t active_original_entered;
    uint32_t active_original_returned;
    uint64_t active_sequence;
} DlumaPlayerDiTeardownDiagnostics;

typedef struct DlumaPlayerDiRelayValidation {
    uint32_t exact_prefix;
    uint32_t stolen_size;
    uint32_t instruction_count;
    uint32_t instruction_boundaries[7];
    uint32_t has_relative_branch;
    uint32_t has_rip_relative_operand;
    uint32_t ends_on_instruction_boundary;
    uint32_t resume_instruction_offset;
} DlumaPlayerDiRelayValidation;

typedef struct DlumaPlayerDiVectorHeader {
    uintptr_t data;
    uint32_t count;
    uint32_t capacity;
    uint32_t tag_or_field;
} DlumaPlayerDiVectorHeader;

typedef struct DlumaPlayerDiMemoryRegion {
    uintptr_t base;
    size_t size;
    uint32_t state;
    uint32_t protection;
    uint32_t query_succeeded;
    uint32_t address_in_region;
} DlumaPlayerDiMemoryRegion;

typedef int (*DlumaPlayerDiReadMemory)(
    void* user, uintptr_t address, void* output, size_t size);
typedef int (*DlumaPlayerDiQueryMemory)(
    void* user, uintptr_t address, DlumaPlayerDiMemoryRegion* output);

typedef struct DlumaPlayerDiVectorCrashInput {
    uint32_t module_timestamp;
    uint32_t module_image_size;
    uint32_t fault_rva;
    uint32_t supported_profile_validated;
    uint32_t instruction_signature_validated;
    uint32_t access_operation_is_read;
    uintptr_t access_target;
    uintptr_t rbx;
    uintptr_t rdi;
    uintptr_t rsi;
} DlumaPlayerDiVectorCrashInput;

typedef struct DlumaPlayerDiVectorCrashEvidence {
    uint32_t accepted;
    uint32_t exact_supported_signature;
    uintptr_t source_vector_address;
    uintptr_t destination_vector_address;
    DlumaPlayerDiVectorHeader source;
    DlumaPlayerDiVectorHeader destination;
    uint32_t source_header_readable;
    uint32_t destination_header_readable;
    uint32_t element_index;
    uint32_t index_multiplication_valid;
    uint64_t element_byte_offset;
    uintptr_t inferred_source_data;
    uintptr_t computed_source_element;
    uintptr_t computed_destination_element;
    uint32_t computed_source_element_valid;
    uint32_t computed_destination_element_valid;
    uint32_t source_element_readable;
    uint32_t destination_element_readable;
    uintptr_t source_element_value;
    uintptr_t destination_element_value;
    uint32_t index_less_than_count;
    uint32_t index_less_than_capacity;
    uint32_t count_not_greater_than_capacity;
    uint32_t destination_index_less_than_count;
    uint32_t destination_index_less_than_capacity;
    uint32_t destination_count_not_greater_than_capacity;
    uint32_t fault_matches_computed_source_element;
    uint64_t source_bytes_readable_to_region_end;
    uint64_t source_qwords_readable_to_region_end;
    DlumaPlayerDiMemoryRegion source_header_region;
    DlumaPlayerDiMemoryRegion source_data_region;
    DlumaPlayerDiMemoryRegion source_element_region;
    DlumaPlayerDiMemoryRegion destination_header_region;
    DlumaPlayerDiMemoryRegion destination_data_region;
    DlumaPlayerDiMemoryRegion destination_element_region;
} DlumaPlayerDiVectorCrashEvidence;

void dluma_playerdi_teardown_configure(
    int observer_enabled,
    int diagnostic_opt_in,
    int supported_profile_validated);

void dluma_playerdi_teardown_reset_diagnostics(void);

void dluma_playerdi_teardown_snapshot(
    DlumaPlayerDiTeardownDiagnostics* output);

void dluma_playerdi_ondestroy_observe_call(
    DlumaPlayerDiOnDestroyOriginal original,
    void* player_di,
    float destroy_time);

int dluma_playerdi_validate_ondestroy_relay(
    const unsigned char* bytes,
    size_t byte_count,
    DlumaPlayerDiRelayValidation* output);

int dluma_playerdi_vector_copy_signature_matches(
    const unsigned char* bytes,
    size_t byte_count);

int dluma_playerdi_decode_vector_copy_fault(
    const DlumaPlayerDiVectorCrashInput* input,
    DlumaPlayerDiReadMemory read_memory,
    DlumaPlayerDiQueryMemory query_memory,
    void* callback_user,
    DlumaPlayerDiVectorCrashEvidence* output);

int dluma_playerdi_inventory_header_layout_validated(void);

#ifdef __cplusplus
}
#endif

#endif
