#include "dlw_logs_menu_core.h"

int dluma_logs_category_matches(
    DlumaLogsCategory category,
    DlumaLogHistorySeverity severity)
{
    if (category == DLUMA_LOGS_CATEGORY_ALL)
        return 1;
    if (category == DLUMA_LOGS_CATEGORY_INFO)
        return severity == DLUMA_LOG_HISTORY_INFO;
    if (category == DLUMA_LOGS_CATEGORY_WARNINGS)
        return severity == DLUMA_LOG_HISTORY_WARNING;
    if (category == DLUMA_LOGS_CATEGORY_ERRORS)
        return severity == DLUMA_LOG_HISTORY_ERROR;
    if (category == DLUMA_LOGS_CATEGORY_DEBUG)
        return severity == DLUMA_LOG_HISTORY_DEBUG;
    return 0;
}

unsigned int dluma_logs_filter_indices(
    const DlumaLogHistoryEntry *entries,
    unsigned int entry_count,
    DlumaLogsCategory category,
    const char *query,
    unsigned int *indices_out,
    unsigned int capacity,
    unsigned int *total_matches_out)
{
    unsigned int index;
    unsigned int copied = 0;
    unsigned int total = 0;
    if (total_matches_out)
        *total_matches_out = 0;
    if (!entries || !entry_count)
        return 0;
    for (index = entry_count; index > 0; --index)
    {
        const DlumaLogHistoryEntry *entry = &entries[index - 1U];
        if (!dluma_logs_category_matches(category, entry->severity) ||
            !dluma_log_history_entry_matches(entry, query))
            continue;
        ++total;
        if (indices_out && copied < capacity)
            indices_out[copied++] = index - 1U;
    }
    if (total_matches_out)
        *total_matches_out = total;
    return copied;
}

int dluma_logs_choose_selection(
    const DlumaLogHistoryEntry *entries,
    const unsigned int *indices,
    unsigned int row_count,
    ULONGLONG previous_sequence,
    int previous_selection,
    int follow_newest)
{
    unsigned int row;
    if (!entries || !indices || !row_count)
        return -1;
    if (follow_newest || !previous_sequence)
        return 0;
    for (row = 0; row < row_count; ++row)
    {
        if (entries[indices[row]].sequence == previous_sequence)
            return (int)row;
    }
    if (previous_selection >= 0 &&
        previous_selection < (int)row_count)
        return previous_selection;
    return 0;
}

static void dluma_logs_append_two_digits(
    char *out, unsigned int capacity, unsigned int value)
{
    char digits[3];
    digits[0] = (char)('0' + ((value / 10U) % 10U));
    digits[1] = (char)('0' + (value % 10U));
    digits[2] = 0;
    dluma_str_append(out, capacity, digits);
}

static void dluma_logs_append_three_digits(
    char *out, unsigned int capacity, unsigned int value)
{
    char digits[4];
    digits[0] = (char)('0' + ((value / 100U) % 10U));
    digits[1] = (char)('0' + ((value / 10U) % 10U));
    digits[2] = (char)('0' + (value % 10U));
    digits[3] = 0;
    dluma_str_append(out, capacity, digits);
}

void dluma_logs_format_row_header(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity)
{
    ULONGLONG seconds;
    unsigned int hours;
    unsigned int minutes;
    unsigned int secs;
    unsigned int millis;
    char hour_text[32];
    if (!out || !capacity)
        return;
    out[0] = 0;
    if (!entry)
        return;

    seconds = entry->tick_ms / 1000ULL;
    hours = (unsigned int)(seconds / 3600ULL);
    minutes = (unsigned int)((seconds / 60ULL) % 60ULL);
    secs = (unsigned int)(seconds % 60ULL);
    millis = (unsigned int)(entry->tick_ms % 1000ULL);
    dluma_str_append(out, capacity, "+");
    dluma_u64_to_text(hours, hour_text, DLUMA_ARRAY_COUNT(hour_text));
    if (hours < 10U)
        dluma_str_append(out, capacity, "0");
    dluma_str_append(out, capacity, hour_text);
    dluma_str_append(out, capacity, ":");
    dluma_logs_append_two_digits(out, capacity, minutes);
    dluma_str_append(out, capacity, ":");
    dluma_logs_append_two_digits(out, capacity, secs);
    dluma_str_append(out, capacity, ".");
    dluma_logs_append_three_digits(out, capacity, millis);
    dluma_str_append(out, capacity, " [");
    dluma_str_append(out, capacity, entry->level);
    dluma_str_append(out, capacity, "]");
}

void dluma_logs_format_row_message(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity)
{
    char compact_message[DLUMA_LOGS_ROW_MESSAGE_LIMIT + 4U];
    unsigned int source_index = 0;
    unsigned int compact_index = 0;
    if (!out || !capacity)
        return;
    out[0] = 0;
    if (!entry)
        return;
    while (entry->message[source_index] &&
           compact_index < DLUMA_LOGS_ROW_MESSAGE_LIMIT)
    {
        char ch = entry->message[source_index++];
        if (ch == '\r' || ch == '\n' || ch == '\t')
            ch = ' ';
        compact_message[compact_index++] = ch;
    }
    if (entry->message[source_index])
    {
        compact_message[compact_index++] = '.';
        compact_message[compact_index++] = '.';
        compact_message[compact_index++] = '.';
    }
    compact_message[compact_index] = 0;
    dluma_str_append(out, capacity, compact_message);
}

void dluma_logs_format_row(
    const DlumaLogHistoryEntry *entry,
    char *out,
    unsigned int capacity)
{
    char message[DLUMA_LOGS_ROW_MESSAGE_LIMIT + 4U];
    if (!out || !capacity)
        return;
    dluma_logs_format_row_header(entry, out, capacity);
    dluma_logs_format_row_message(
        entry, message, DLUMA_ARRAY_COUNT(message));
    if (out && out[0] && message[0])
        dluma_str_append(out, capacity, "  ");
    dluma_str_append(out, capacity, message);
}
