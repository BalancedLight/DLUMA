#ifndef DLUMA_LOG_H
#define DLUMA_LOG_H

#include "dlw_common.h"
#define DLUMA_LOG_QUEUE_CAPACITY 256U
#define DLUMA_LOG_RECORD_CAPACITY 2048U
#include "dlw_log_history_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int dluma_log_start(const WCHAR* path);
int dluma_log_stop(void);
void dluma_log_emergency(const char* message);
ULONGLONG dluma_log_history_revision(void);
unsigned int dluma_log_history_snapshot(
    DlumaLogHistoryEntry *out,
    unsigned int capacity,
    ULONGLONG *revision_out,
    ULONGLONG *published_total_out);

#ifdef __cplusplus
}
#endif

#endif
