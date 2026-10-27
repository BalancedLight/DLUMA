#ifndef DLUMA_INTERNAL_HOOK_ATTESTATION_H
#define DLUMA_INTERNAL_HOOK_ATTESTATION_H

#define DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY 32U
#define DLUMA_INTERNAL_ATOMIC_DETOUR_SIZE 16U

typedef enum DlumaInternalWaveBankEntryState {
    DLUMA_INTERNAL_WAVE_BANK_ENTRY_INVALID = 0,
    DLUMA_INTERNAL_WAVE_BANK_ENTRY_ORIGINAL = 1,
    DLUMA_INTERNAL_WAVE_BANK_ENTRY_LOADER_OWNED_DETOUR = 2
} DlumaInternalWaveBankEntryState;

typedef struct DlumaInternalFunctionDetourEvidenceV1 {
    unsigned int size;
    const void* expected_target;
    const void* owner_target;
    const void* route_address;
    const void* route_original_target;
    const void* expected_hook_target;
    const void* route_hook_target;
    const void* route_relay;
    const void* bound_original;
    unsigned int expected_rva;
    unsigned int owner_rva;
    unsigned int expected_prefix_size;
    unsigned int owner_prefix_size;
    unsigned int route_byte_count;
    unsigned int route_stolen_size;
    unsigned int owner_route_count;
    unsigned int owned_route_count;
    unsigned char owner_validated;
    unsigned char owner_installed;
    unsigned char route_enabled;
    unsigned char route_installed;
    unsigned char route_kind_is_function;
    unsigned char current_routing_bytes_match;
    unsigned char untouched_tail_matches;
    unsigned char reserved;
    unsigned char expected_prefix[DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY];
    unsigned char owner_expected_prefix[
        DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY];
    unsigned char owner_actual_prefix[
        DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY];
    unsigned char route_original_bytes[
        DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY];
} DlumaInternalFunctionDetourEvidenceV1;

int dluma_internal_function_detour_evidence_matches(
    const DlumaInternalFunctionDetourEvidenceV1* evidence);

int dluma_internal_classify_wave_bank_entry(
    int original_prefix_matches,
    const DlumaInternalFunctionDetourEvidenceV1* detour_evidence);

int dluma_custom_map_asset_isolation_snapshot_audio_fmod_load_banks_detour(
    const void* expected_target,
    unsigned int expected_rva,
    const unsigned char* expected_prefix,
    unsigned int expected_prefix_size,
    DlumaInternalFunctionDetourEvidenceV1* evidence_out);

int dluma_custom_map_asset_isolation_snapshot_update_dlc_content_detour(
    const void* expected_target,
    unsigned int expected_rva,
    const unsigned char* expected_prefix,
    unsigned int expected_prefix_size,
    DlumaInternalFunctionDetourEvidenceV1* evidence_out);

int dluma_custom_map_asset_isolation_snapshot_text_from_file_detour(
    const void* expected_target,
    unsigned int expected_rva,
    const unsigned char* expected_prefix,
    unsigned int expected_prefix_size,
    DlumaInternalFunctionDetourEvidenceV1* evidence_out);

#endif
