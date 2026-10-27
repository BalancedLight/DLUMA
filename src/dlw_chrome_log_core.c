#include "dlw_chrome_log_core.h"

#include <string.h>

static const unsigned char g_dluma_chrome_log_prefix[] = {
    0x4C, 0x8B, 0xDC, 0x48, 0x83, 0xEC, 0x48
};
static const unsigned char g_dluma_chrome_level_prefix[] = {
    0x81, 0xF9, 0x52, 0x52, 0x52, 0x45, 0x74, 0x39,
    0x81, 0xF9
};

static void dluma_chrome_log_core_init_result(
    DlumaChromeLogCoreResult* result)
{
    if (!result) return;
    result->status = DLUMA_CHROME_LOG_CORE_INVALID_INPUT;
    result->level = 0;
    result->filter_action = 0;
    result->line_mode = 0;
}

static int dluma_chrome_log_core_profile_supported(
    uint32_t timestamp, uint32_t image_size)
{
    return (timestamp == DLUMA_CHROME_SHIPPING_TIMESTAMP &&
            image_size == DLUMA_CHROME_SHIPPING_IMAGE_SIZE) ||
           (timestamp == DLUMA_CHROME_DEVTOOLS_TIMESTAMP &&
            image_size == DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE);
}

int dluma_chrome_log_core_emit(
    const DlumaChromeLogCoreInput* input,
    DlumaChromeLogCoreResult* result)
{
    unsigned int level;
    dluma_chrome_log_core_init_result(result);
    if (!input || !result || !input->path_verified ||
        !input->log || !input->level_from_id ||
        !input->log_prefix || !input->level_prefix)
        return 0;
    if (!dluma_chrome_log_core_profile_supported(
            input->timestamp, input->image_size)) {
        result->status = DLUMA_CHROME_LOG_CORE_UNSUPPORTED_PROFILE;
        return 0;
    }
    if (input->log_prefix_size < sizeof(g_dluma_chrome_log_prefix) ||
        input->level_prefix_size < sizeof(g_dluma_chrome_level_prefix) ||
        memcmp(input->log_prefix, g_dluma_chrome_log_prefix,
               sizeof(g_dluma_chrome_log_prefix)) != 0 ||
        memcmp(input->level_prefix, g_dluma_chrome_level_prefix,
               sizeof(g_dluma_chrome_level_prefix)) != 0) {
        result->status = DLUMA_CHROME_LOG_CORE_PREFIX_MISMATCH;
        return 0;
    }

    level = input->level_from_id(DLUMA_CHROME_WARN_ID);
    /* CLFilterAction bit 2 is Chrome's crash-log output.  Keep this
     * disclaimer in the native crash log even when WARN console filtering is
     * configured off. */
    result->level = level;
    result->filter_action = 2;
    result->line_mode = 1;
    input->log(level, DLUMA_CHROME_LOG_CATEGORY, 0, 0,
               result->filter_action, result->line_mode,
               "%s\n", DLUMA_CHROME_LOG_MESSAGE);
    result->status = DLUMA_CHROME_LOG_CORE_EMITTED;
    return 1;
}
