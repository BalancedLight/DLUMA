#ifndef DLUMA_LOG_HISTORY_CORE_H
#define DLUMA_LOG_HISTORY_CORE_H

#include "dlw_common.h"

#ifndef DLUMA_LOG_RECORD_CAPACITY
#define DLUMA_LOG_RECORD_CAPACITY 2048U
#endif
#define DLUMA_LOG_HISTORY_CAPACITY 1024U
#define DLUMA_LOG_HISTORY_LEVEL_CAPACITY 16U

typedef enum DlumaLogHistorySeverity
{
    DLUMA_LOG_HISTORY_INFO = 0,
    DLUMA_LOG_HISTORY_WARNING = 1,
    DLUMA_LOG_HISTORY_ERROR = 2,
    DLUMA_LOG_HISTORY_DEBUG = 3
} DlumaLogHistorySeverity;

typedef struct DlumaLogHistoryEntry
{
    ULONGLONG sequence;
    ULONGLONG tick_ms;
    DlumaLogHistorySeverity severity;
    char level[DLUMA_LOG_HISTORY_LEVEL_CAPACITY];
    char message[DLUMA_LOG_RECORD_CAPACITY];
} DlumaLogHistoryEntry;

typedef struct DlumaLogHistorySlot
{
    volatile LONG version;
    DlumaLogHistoryEntry entry;
} DlumaLogHistorySlot;

typedef struct DlumaLogHistoryStore
{
    DlumaLogHistorySlot slots[DLUMA_LOG_HISTORY_CAPACITY];
    volatile LONGLONG published_total;
    volatile LONGLONG revision;
    ULONGLONG session_start_tick_ms;
} DlumaLogHistoryStore;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_log_history_store_reset(DlumaLogHistoryStore *store);
void dluma_log_history_store_start_session(
    DlumaLogHistoryStore *store,
    ULONGLONG tick_ms);
void dluma_log_history_store_publish(
    DlumaLogHistoryStore *store,
    ULONGLONG tick_ms,
    const char *level,
    const char *message);
ULONGLONG dluma_log_history_store_revision(
    const DlumaLogHistoryStore *store);
unsigned int dluma_log_history_store_snapshot(
    const DlumaLogHistoryStore *store,
    DlumaLogHistoryEntry *out,
    unsigned int capacity,
    ULONGLONG *revision_out,
    ULONGLONG *published_total_out);
DlumaLogHistorySeverity dluma_log_history_severity_for_level(
    const char *level);
int dluma_log_history_entry_matches(
    const DlumaLogHistoryEntry *entry,
    const char *query);

#ifdef __cplusplus
}
#endif

#endif
