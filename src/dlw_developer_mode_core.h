#ifndef DLUMA_DEVELOPER_MODE_CORE_H
#define DLUMA_DEVELOPER_MODE_CORE_H

#include <stddef.h>
#include <stdint.h>

#define DLUMA_DEVELOPER_MODE_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_DEVELOPER_MODE_GAMEDLL_IMAGE_SIZE 0x02082000UL

#define DLUMA_DEVELOPER_MODE_CHEATS_FLAG_RVA 0x01D07FC3UL
#define DLUMA_DEVELOPER_MODE_NO_LOGOS_FLAG_RVA 0x01D07FC4UL
#define DLUMA_DEVELOPER_MODE_SKIP_WAIT_FLAG_RVA 0x01D07208UL

#define DLUMA_DEVELOPER_MODE_CHEATS_REFERENCE_RVA 0x010CCE2FUL
#define DLUMA_DEVELOPER_MODE_NO_LOGOS_REFERENCE_RVA 0x010CD07BUL
#define DLUMA_DEVELOPER_MODE_SKIP_WAIT_REFERENCE_RVA 0x003C9A0AUL

typedef int (*DlumaDeveloperModeCoreReadFn)(
    void* user, uint32_t rva, void* out, size_t size);
typedef int (*DlumaDeveloperModeCoreWriteFn)(
    void* user, uint32_t rva, const void* bytes, size_t size);

typedef enum DlumaDeveloperModeCoreStatus {
    DLUMA_DEVELOPER_MODE_CORE_DISABLED = 0,
    DLUMA_DEVELOPER_MODE_CORE_APPLIED = 1,
    DLUMA_DEVELOPER_MODE_CORE_UNSUPPORTED = 2,
    DLUMA_DEVELOPER_MODE_CORE_VALIDATION_FAILED = 3,
    DLUMA_DEVELOPER_MODE_CORE_WRITE_FAILED = 4
} DlumaDeveloperModeCoreStatus;

typedef struct DlumaDeveloperModeCoreInput {
    int enabled;
    uint32_t timestamp;
    uint32_t image_size;
    DlumaDeveloperModeCoreReadFn read;
    DlumaDeveloperModeCoreWriteFn write;
    void* user;
} DlumaDeveloperModeCoreInput;

typedef struct DlumaDeveloperModeCoreResult {
    DlumaDeveloperModeCoreStatus status;
    const char* failure_stage;
    unsigned int changed_count;
    unsigned char original_values[3];
} DlumaDeveloperModeCoreResult;

int dluma_developer_mode_core_apply(
    const DlumaDeveloperModeCoreInput* input,
    DlumaDeveloperModeCoreResult* result);

#endif
