#ifndef DLUMA_LOGS_MENU_CORE_H
#define DLUMA_LOGS_MENU_CORE_H

#include "dlw_log_history_core.h"

#define DLUMA_LOGS_QUERY_CAPACITY 129U
#define DLUMA_LOGS_MAX_RESULTS 256U
#define DLUMA_LOGS_ROW_MESSAGE_LIMIT 68U

typedef enum DlumaLogsCategory
{
    DLUMA_LOGS_CATEGORY_ALL = 0,
    DLUMA_LOGS_CATEGORY_INFO = 1,
    DLUMA_LOGS_CATEGORY_WARNINGS = 2,
    DLUMA_LOGS_CATEGORY_ERRORS = 3,
    DLUMA_LOGS_CATEGORY_DEBUG = 4,
    DLUMA_LOGS_CATEGORY_COUNT = 5
} DlumaLogsCategory;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_logs_category_matches(
    DlumaLogsCategory category,
    DlumaLogHistorySeverity severity);
unsigned int dluma_logs_filter_indices(
    const DlumaLogHistoryEntry *entries,
    unsigned int entry_count,
    DlumaLogsCategory category,
    const char *query,
    unsigned int *indices_out,
    unsigned int capacity,
    unsigned int *total_matches_out);
int dluma_logs_choose_selection(
    const DlumaLogHistoryEntry *entries,
    const unsigned int *indices,
    unsigned int row_count,
    ULONGLONG previous_sequence,
    int previous_selection,
    int follow_newest);
void dluma_logs_format_row(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity);
void dluma_logs_format_row_header(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity);
void dluma_logs_format_row_message(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
