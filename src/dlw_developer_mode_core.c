#include "dlw_developer_mode_core.h"

#include <string.h>

typedef struct DlumaDeveloperModeFlagProfile {
    uint32_t flag_rva;
    uint32_t reference_rva;
    const unsigned char* reference;
    size_t reference_size;
} DlumaDeveloperModeFlagProfile;

static const unsigned char g_dluma_developer_cheats_reference[] = {
    0xC6, 0x05, 0x8D, 0xB1, 0xC3, 0x00, 0x01,
    0xE9, 0xB5, 0x05, 0x00, 0x00
};
static const unsigned char g_dluma_developer_no_logos_reference[] = {
    0xC6, 0x05, 0x42, 0xAF, 0xC3, 0x00, 0x01,
    0xE9, 0x69, 0x03, 0x00, 0x00
};
static const unsigned char g_dluma_developer_skip_wait_reference[] = {
    0xC6, 0x05, 0xF7, 0xD7, 0x93, 0x01, 0x01,
    0xB0, 0x01, 0xE9, 0x20, 0x23, 0x00, 0x00
};
static const DlumaDeveloperModeFlagProfile g_dluma_developer_flags[] = {
    {
        DLUMA_DEVELOPER_MODE_CHEATS_FLAG_RVA,
        DLUMA_DEVELOPER_MODE_CHEATS_REFERENCE_RVA,
        g_dluma_developer_cheats_reference,
        sizeof(g_dluma_developer_cheats_reference)
    },
    {
        DLUMA_DEVELOPER_MODE_NO_LOGOS_FLAG_RVA,
        DLUMA_DEVELOPER_MODE_NO_LOGOS_REFERENCE_RVA,
        g_dluma_developer_no_logos_reference,
        sizeof(g_dluma_developer_no_logos_reference)
    },
    {
        DLUMA_DEVELOPER_MODE_SKIP_WAIT_FLAG_RVA,
        DLUMA_DEVELOPER_MODE_SKIP_WAIT_REFERENCE_RVA,
        g_dluma_developer_skip_wait_reference,
        sizeof(g_dluma_developer_skip_wait_reference)
    }
};

static void dluma_developer_mode_core_result_init(
    DlumaDeveloperModeCoreResult* result)
{
    unsigned int index;
    if (!result) return;
    result->status = DLUMA_DEVELOPER_MODE_CORE_VALIDATION_FAILED;
    result->failure_stage = "invalid_input";
    result->changed_count = 0;
    for (index = 0; index < 3; ++index)
        result->original_values[index] = 0;
}

static int dluma_developer_mode_core_restore(
    const DlumaDeveloperModeCoreInput* input,
    const DlumaDeveloperModeCoreResult* result)
{
    unsigned int index;
    int restored = 1;
    for (index = 0; index < 3; ++index)
    {
        unsigned char observed = 0xFF;
        if (!input->write(
                input->user, g_dluma_developer_flags[index].flag_rva,
                &result->original_values[index], 1) ||
            !input->read(
                input->user, g_dluma_developer_flags[index].flag_rva,
                &observed, 1) ||
            observed != result->original_values[index])
            restored = 0;
    }
    return restored;
}

int dluma_developer_mode_core_apply(
    const DlumaDeveloperModeCoreInput* input,
    DlumaDeveloperModeCoreResult* result)
{
    unsigned int index;
    unsigned char enabled_value = 1;
    dluma_developer_mode_core_result_init(result);
    if (!input || !result)
        return 0;
    if (!input->enabled)
    {
        result->status = DLUMA_DEVELOPER_MODE_CORE_DISABLED;
        result->failure_stage = 0;
        return 1;
    }
    if (input->timestamp != DLUMA_DEVELOPER_MODE_GAMEDLL_TIMESTAMP ||
        input->image_size != DLUMA_DEVELOPER_MODE_GAMEDLL_IMAGE_SIZE)
    {
        result->status = DLUMA_DEVELOPER_MODE_CORE_UNSUPPORTED;
        result->failure_stage = "unsupported_pe_identity";
        return 0;
    }
    if (!input->read || !input->write)
    {
        result->failure_stage = "memory_callbacks_missing";
        return 0;
    }

    /* Validate every code reference and every current flag before the first
     * write. A partial or guessed profile must leave the process unchanged. */
    for (index = 0; index < 3; ++index)
    {
        unsigned char reference[16];
        const DlumaDeveloperModeFlagProfile* flag =
            &g_dluma_developer_flags[index];
        if (flag->reference_size > sizeof(reference) ||
            !input->read(
                input->user, flag->reference_rva,
                reference, flag->reference_size) ||
            memcmp(reference, flag->reference, flag->reference_size) != 0)
        {
            result->failure_stage = "instruction_reference_mismatch";
            return 0;
        }
        if (!input->read(
                input->user, flag->flag_rva,
                &result->original_values[index], 1) ||
            result->original_values[index] > 1)
        {
            result->failure_stage = "flag_value_invalid";
            return 0;
        }
    }

    for (index = 0; index < 3; ++index)
    {
        unsigned char observed = 0;
        if (result->original_values[index] == 1)
            continue;
        if (!input->write(
                input->user, g_dluma_developer_flags[index].flag_rva,
                &enabled_value, 1) ||
            !input->read(
                input->user, g_dluma_developer_flags[index].flag_rva,
                &observed, 1) ||
            observed != enabled_value)
        {
            result->status = DLUMA_DEVELOPER_MODE_CORE_WRITE_FAILED;
            result->failure_stage = dluma_developer_mode_core_restore(input, result)
                                        ? "write_or_readback_failed"
                                        : "rollback_failed";
            result->changed_count = 0;
            return 0;
        }
        ++result->changed_count;
    }

    result->status = DLUMA_DEVELOPER_MODE_CORE_APPLIED;
    result->failure_stage = 0;
    return 1;
}
