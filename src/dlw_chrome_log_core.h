#ifndef DLUMA_CHROME_LOG_CORE_H
#define DLUMA_CHROME_LOG_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifndef _WIN32
#ifndef __cdecl
#define __cdecl
#endif
#endif

#define DLUMA_CHROME_SHIPPING_TIMESTAMP 0x6A4A8BDEUL
#define DLUMA_CHROME_SHIPPING_IMAGE_SIZE 0x0005B000UL
#define DLUMA_CHROME_DEVTOOLS_TIMESTAMP 0x623394CDUL
#define DLUMA_CHROME_DEVTOOLS_IMAGE_SIZE 0x00060000UL

#define DLUMA_CHROME_WARN_ID 0x5741524EUL
#define DLUMA_CHROME_LOG_CATEGORY "DLUMA"
#define DLUMA_CHROME_LOG_MESSAGE \
    "Dying Light has been modded with unofficial code created by the community. " \
    "These logs should not represent the stability of the core game."

typedef void (__cdecl *DlumaChromeLogFn)(
    unsigned int level,
    const char* category,
    const char* source,
    int line,
    int filter_action,
    int line_mode,
    const char* format,
    ...);
typedef unsigned int (__cdecl *DlumaChromeLogLevelFromIdFn)(unsigned int id);

typedef enum DlumaChromeLogCoreStatus {
    DLUMA_CHROME_LOG_CORE_EMITTED = 0,
    DLUMA_CHROME_LOG_CORE_INVALID_INPUT = 1,
    DLUMA_CHROME_LOG_CORE_UNSUPPORTED_PROFILE = 2,
    DLUMA_CHROME_LOG_CORE_PREFIX_MISMATCH = 3
} DlumaChromeLogCoreStatus;

typedef struct DlumaChromeLogCoreInput {
    int path_verified;
    uint32_t timestamp;
    uint32_t image_size;
    const unsigned char* log_prefix;
    size_t log_prefix_size;
    const unsigned char* level_prefix;
    size_t level_prefix_size;
    DlumaChromeLogFn log;
    DlumaChromeLogLevelFromIdFn level_from_id;
} DlumaChromeLogCoreInput;

typedef struct DlumaChromeLogCoreResult {
    DlumaChromeLogCoreStatus status;
    unsigned int level;
    int filter_action;
    int line_mode;
} DlumaChromeLogCoreResult;

int dluma_chrome_log_core_emit(
    const DlumaChromeLogCoreInput* input,
    DlumaChromeLogCoreResult* result);

#endif
