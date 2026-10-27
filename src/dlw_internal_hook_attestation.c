#include "dlw_internal_hook_attestation.h"

static int dluma_internal_attestation_bytes_equal(
    const unsigned char* left,
    const unsigned char* right,
    unsigned int size)
{
    unsigned int index;
    if (!left || !right || !size) return 0;
    for (index = 0U; index < size; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

int dluma_internal_function_detour_evidence_matches(
    const DlumaInternalFunctionDetourEvidenceV1* evidence)
{
    unsigned int prefix_size;
    if (!evidence ||
        evidence->size < (unsigned int)sizeof(*evidence) ||
        !evidence->expected_target ||
        evidence->owner_target != evidence->expected_target ||
        evidence->route_address != evidence->expected_target ||
        evidence->route_original_target != evidence->expected_target ||
        !evidence->expected_hook_target ||
        evidence->route_hook_target != evidence->expected_hook_target ||
        !evidence->route_relay ||
        evidence->bound_original != evidence->route_relay ||
        evidence->owner_rva != evidence->expected_rva ||
        evidence->owner_route_count != 1U ||
        evidence->owned_route_count != 1U ||
        !evidence->owner_validated ||
        !evidence->owner_installed ||
        !evidence->route_enabled ||
        !evidence->route_installed ||
        !evidence->route_kind_is_function ||
        !evidence->current_routing_bytes_match ||
        !evidence->untouched_tail_matches ||
        evidence->route_byte_count != DLUMA_INTERNAL_ATOMIC_DETOUR_SIZE ||
        evidence->route_stolen_size < evidence->route_byte_count ||
        evidence->route_stolen_size >
            DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY)
        return 0;
    prefix_size = evidence->expected_prefix_size;
    if (!prefix_size ||
        prefix_size > DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY ||
        evidence->owner_prefix_size < prefix_size ||
        evidence->owner_prefix_size >
            DLUMA_INTERNAL_DETOUR_PREFIX_CAPACITY ||
        evidence->route_stolen_size < prefix_size)
        return 0;
    return dluma_internal_attestation_bytes_equal(
               evidence->expected_prefix,
               evidence->owner_expected_prefix,
               prefix_size) &&
           dluma_internal_attestation_bytes_equal(
               evidence->expected_prefix,
               evidence->owner_actual_prefix,
               prefix_size) &&
           dluma_internal_attestation_bytes_equal(
               evidence->expected_prefix,
               evidence->route_original_bytes,
               prefix_size);
}

int dluma_internal_classify_wave_bank_entry(
    int original_prefix_matches,
    const DlumaInternalFunctionDetourEvidenceV1* detour_evidence)
{
    if (original_prefix_matches)
        return DLUMA_INTERNAL_WAVE_BANK_ENTRY_ORIGINAL;
    return dluma_internal_function_detour_evidence_matches(
               detour_evidence)
        ? DLUMA_INTERNAL_WAVE_BANK_ENTRY_LOADER_OWNED_DETOUR
        : DLUMA_INTERNAL_WAVE_BANK_ENTRY_INVALID;
}
