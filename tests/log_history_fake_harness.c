#include "dlw_log_history_core.h"
#include "dlw_logs_menu_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "CHECK failed at line %d: %s\n",             \
                    __LINE__, #condition);                                  \
            return 0;                                                       \
        }                                                                   \
    } while (0)

static DlumaLogHistoryStore g_store;
static DlumaLogHistoryEntry g_snapshot[DLUMA_LOG_HISTORY_CAPACITY];

char dluma_ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z'
               ? (char)(value + ('a' - 'A'))
               : value;
}

int dluma_stricmp(const char *first, const char *second)
{
    unsigned int index = 0;
    if (!first) first = "";
    if (!second) second = "";
    while (first[index] && second[index]) {
        char a = dluma_ascii_lower(first[index]);
        char b = dluma_ascii_lower(second[index]);
        if (a != b) return (unsigned char)a - (unsigned char)b;
        ++index;
    }
    return (unsigned char)first[index] - (unsigned char)second[index];
}

int dluma_contains_icase(const char *haystack, const char *needle)
{
    unsigned int start;
    unsigned int offset;
    if (!haystack || !needle) return 0;
    if (!needle[0]) return 1;
    for (start = 0; haystack[start]; ++start) {
        for (offset = 0; needle[offset]; ++offset) {
            if (!haystack[start + offset] ||
                dluma_ascii_lower(haystack[start + offset]) !=
                    dluma_ascii_lower(needle[offset]))
                break;
        }
        if (!needle[offset]) return 1;
    }
    return 0;
}

void dluma_str_copy(char *destination, unsigned int capacity,
                  const char *source)
{
    unsigned int index = 0;
    if (!destination || !capacity) return;
    if (source)
        while (source[index] && index + 1U < capacity) {
            destination[index] = source[index];
            ++index;
        }
    destination[index] = 0;
}

void dluma_str_append(char *destination, unsigned int capacity,
                    const char *source)
{
    unsigned int used;
    if (!destination || !capacity || !source) return;
    used = (unsigned int)strlen(destination);
    dluma_str_copy(destination + used, capacity - used, source);
}

unsigned int dluma_u64_to_text(ULONGLONG value, char *out,
                             unsigned int capacity)
{
    char reverse[32];
    unsigned int digits = 0;
    unsigned int index;
    if (!out || !capacity) return 0;
    do {
        reverse[digits++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value && digits < sizeof(reverse));
    if (digits + 1U > capacity) {
        out[0] = 0;
        return 0;
    }
    for (index = 0; index < digits; ++index)
        out[index] = reverse[digits - index - 1U];
    out[digits] = 0;
    return digits;
}

static void publish_number(ULONGLONG tick, const char *level,
                           unsigned int number)
{
    char message[64];
    sprintf_s(message, sizeof(message), "record-%u", number);
    dluma_log_history_store_publish(&g_store, tick, level, message);
}

static int case_classification_search_and_reset(void)
{
    DlumaLogHistoryEntry entry;
    ULONGLONG revision = 99;
    ULONGLONG total = 99;
    memset(&entry, 0, sizeof(entry));
    CHECK(dluma_log_history_severity_for_level("INFO") ==
          DLUMA_LOG_HISTORY_INFO);
    CHECK(dluma_log_history_severity_for_level("FIX") ==
          DLUMA_LOG_HISTORY_INFO);
    CHECK(dluma_log_history_severity_for_level("SCRIPT") ==
          DLUMA_LOG_HISTORY_INFO);
    CHECK(dluma_log_history_severity_for_level("CONTEXT") ==
          DLUMA_LOG_HISTORY_INFO);
    CHECK(dluma_log_history_severity_for_level("CUSTOM") ==
          DLUMA_LOG_HISTORY_INFO);
    CHECK(dluma_log_history_severity_for_level("warn") ==
          DLUMA_LOG_HISTORY_WARNING);
    CHECK(dluma_log_history_severity_for_level("ERROR") ==
          DLUMA_LOG_HISTORY_ERROR);
    CHECK(dluma_log_history_severity_for_level("DEBUG") ==
          DLUMA_LOG_HISTORY_DEBUG);
    CHECK(dluma_log_history_severity_for_level("trace") ==
          DLUMA_LOG_HISTORY_DEBUG);

    dluma_str_copy(entry.level, DLUMA_ARRAY_COUNT(entry.level), "CuStOm");
    dluma_str_copy(entry.message, DLUMA_ARRAY_COUNT(entry.message),
                 "Mixed Case Message");
    CHECK(dluma_log_history_entry_matches(&entry, "custom"));
    CHECK(dluma_log_history_entry_matches(&entry, "case message"));
    CHECK(!dluma_log_history_entry_matches(&entry, "missing"));

    dluma_log_history_store_start_session(&g_store, 1000ULL);
    publish_number(1250ULL, "INFO", 1U);
    CHECK(dluma_log_history_store_snapshot(
              &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot),
              &revision, &total) == 1U);
    CHECK(g_snapshot[0].tick_ms == 250ULL);
    CHECK(revision == 1ULL && total == 1ULL);
    dluma_log_history_store_reset(&g_store);
    CHECK(dluma_log_history_store_snapshot(
              &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot),
              &revision, &total) == 0U);
    CHECK(revision == 0ULL && total == 0ULL);
    return 1;
}

static int case_wrap_order_and_limits(void)
{
    unsigned int index;
    unsigned int indices[DLUMA_LOGS_MAX_RESULTS];
    unsigned int matches = 0;
    ULONGLONG revision = 0;
    ULONGLONG total = 0;
    dluma_log_history_store_start_session(&g_store, 5000ULL);
    for (index = 1; index <= 1100U; ++index)
        publish_number(5000ULL + index, "INFO", index);
    CHECK(dluma_log_history_store_snapshot(
              &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot),
              &revision, &total) == DLUMA_LOG_HISTORY_CAPACITY);
    CHECK(total == 1100ULL && revision == 1100ULL);
    CHECK(g_snapshot[0].sequence == 77ULL);
    CHECK(g_snapshot[DLUMA_LOG_HISTORY_CAPACITY - 1U].sequence == 1100ULL);
    for (index = 1; index < DLUMA_LOG_HISTORY_CAPACITY; ++index)
        CHECK(g_snapshot[index].sequence ==
              g_snapshot[index - 1U].sequence + 1ULL);
    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_LOG_HISTORY_CAPACITY,
              DLUMA_LOGS_CATEGORY_ALL, "", indices,
              DLUMA_ARRAY_COUNT(indices), &matches) ==
          DLUMA_LOGS_MAX_RESULTS);
    CHECK(matches == DLUMA_LOG_HISTORY_CAPACITY);
    CHECK(indices[0] == DLUMA_LOG_HISTORY_CAPACITY - 1U);
    CHECK(indices[DLUMA_LOGS_MAX_RESULTS - 1U] == 768U);
    return 1;
}

static int case_categories_rows_and_selection(void)
{
    static const char *const levels[] = {
        "INFO", "FIX", "SCRIPT", "CONTEXT", "OTHER",
        "WARN", "ERROR", "DEBUG", "TRACE"
    };
    unsigned int indices[16];
    unsigned int matches = 0;
    unsigned int index;
    char row[256];
    char header[96];
    char message[DLUMA_LOGS_ROW_MESSAGE_LIMIT + 4U];
    dluma_log_history_store_start_session(&g_store, 100ULL);
    for (index = 0; index < DLUMA_ARRAY_COUNT(levels); ++index)
        dluma_log_history_store_publish(
            &g_store, 100ULL + index, levels[index],
            index == 6U ? "Needle failure\nwith detail" : "ordinary");
    CHECK(dluma_log_history_store_snapshot(
              &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot), 0, 0) ==
          DLUMA_ARRAY_COUNT(levels));
    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_ARRAY_COUNT(levels), DLUMA_LOGS_CATEGORY_INFO,
              "", indices, DLUMA_ARRAY_COUNT(indices), &matches) == 5U);
    CHECK(matches == 5U);
    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_ARRAY_COUNT(levels),
              DLUMA_LOGS_CATEGORY_WARNINGS, "", indices,
              DLUMA_ARRAY_COUNT(indices), &matches) == 1U);
    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_ARRAY_COUNT(levels), DLUMA_LOGS_CATEGORY_ERRORS,
              "needle", indices, DLUMA_ARRAY_COUNT(indices), &matches) == 1U);
    CHECK(indices[0] == 6U);
    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_ARRAY_COUNT(levels), DLUMA_LOGS_CATEGORY_DEBUG,
              "TR", indices, DLUMA_ARRAY_COUNT(indices), &matches) == 1U);
    CHECK(indices[0] == 8U);

    CHECK(dluma_logs_filter_indices(
              g_snapshot, DLUMA_ARRAY_COUNT(levels), DLUMA_LOGS_CATEGORY_ALL,
              "", indices, DLUMA_ARRAY_COUNT(indices), &matches) ==
          DLUMA_ARRAY_COUNT(levels));
    CHECK(dluma_logs_choose_selection(
              g_snapshot, indices, DLUMA_ARRAY_COUNT(levels),
              g_snapshot[4].sequence, 4, 0) == 4);
    CHECK(dluma_logs_choose_selection(
              g_snapshot, indices, DLUMA_ARRAY_COUNT(levels),
              g_snapshot[4].sequence, 4, 1) == 0);
    CHECK(dluma_logs_choose_selection(
              g_snapshot, indices, DLUMA_ARRAY_COUNT(levels),
              9999ULL, 3, 0) == 3);
    CHECK(dluma_logs_choose_selection(
              g_snapshot, indices, 0, 9999ULL, 3, 0) == -1);

    g_snapshot[6].tick_ms = 3723004ULL;
    dluma_logs_format_row_header(
        &g_snapshot[6], header, DLUMA_ARRAY_COUNT(header));
    dluma_logs_format_row_message(
        &g_snapshot[6], message, DLUMA_ARRAY_COUNT(message));
    CHECK(strcmp(header, "+01:02:03.004 [ERROR]") == 0);
    CHECK(strcmp(message, "Needle failure with detail") == 0);
    dluma_logs_format_row(&g_snapshot[6], row, DLUMA_ARRAY_COUNT(row));
    CHECK(strstr(row, "+01:02:03.004 [ERROR]") != 0);
    CHECK(strstr(row, "Needle failure with detail") != 0);
    CHECK(strchr(row, '\n') == 0);
    CHECK(strcmp(g_snapshot[6].message,
                 "Needle failure\nwith detail") == 0);

    memset(g_snapshot[6].message, 'x',
           DLUMA_LOGS_ROW_MESSAGE_LIMIT + 5U);
    g_snapshot[6].message[DLUMA_LOGS_ROW_MESSAGE_LIMIT + 5U] = 0;
    dluma_logs_format_row_message(
        &g_snapshot[6], message, DLUMA_ARRAY_COUNT(message));
    CHECK(strlen(message) == DLUMA_LOGS_ROW_MESSAGE_LIMIT + 3U);
    CHECK(strcmp(message + DLUMA_LOGS_ROW_MESSAGE_LIMIT, "...") == 0);
    return 1;
}

typedef struct ConcurrentWriter
{
    volatile LONG complete;
    unsigned int count;
} ConcurrentWriter;

static DWORD WINAPI concurrent_writer(LPVOID parameter)
{
    ConcurrentWriter *writer = (ConcurrentWriter *)parameter;
    unsigned int index;
    for (index = 1; index <= writer->count; ++index)
        publish_number(10000ULL + index, "DEBUG", index);
    dluma_atomic_exchange(&writer->complete, 1);
    return 0;
}

static int validate_snapshot_records(unsigned int count)
{
    unsigned int index;
    ULONGLONG prior = 0;
    for (index = 0; index < count; ++index) {
        unsigned int message_number = 0;
        CHECK(sscanf_s(g_snapshot[index].message,
                       "record-%u", &message_number) == 1);
        CHECK(message_number == (unsigned int)g_snapshot[index].sequence);
        CHECK(!prior || g_snapshot[index].sequence > prior);
        CHECK(g_snapshot[index].severity == DLUMA_LOG_HISTORY_DEBUG);
        prior = g_snapshot[index].sequence;
    }
    return 1;
}

static int case_concurrent_untorn_snapshots(void)
{
    ConcurrentWriter writer;
    HANDLE thread;
    DWORD wait_result;
    unsigned int snapshots = 0;
    memset(&writer, 0, sizeof(writer));
    writer.count = 6000U;
    dluma_log_history_store_start_session(&g_store, 10000ULL);
    thread = CreateThread(0, 0, concurrent_writer, &writer, 0, 0);
    CHECK(thread != 0);
    while (!dluma_atomic_compare_exchange(&writer.complete, 0, 0)) {
        unsigned int count = dluma_log_history_store_snapshot(
            &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot), 0, 0);
        CHECK(validate_snapshot_records(count));
        ++snapshots;
    }
    wait_result = WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    CHECK(wait_result == WAIT_OBJECT_0);
    CHECK(snapshots > 0U);
    CHECK(validate_snapshot_records(dluma_log_history_store_snapshot(
        &g_store, g_snapshot, DLUMA_ARRAY_COUNT(g_snapshot), 0, 0)));
    CHECK(g_snapshot[DLUMA_LOG_HISTORY_CAPACITY - 1U].sequence == 6000ULL);
    return 1;
}

int main(void)
{
    CHECK(case_classification_search_and_reset());
    CHECK(case_wrap_order_and_limits());
    CHECK(case_categories_rows_and_selection());
    CHECK(case_concurrent_untorn_snapshots());
    puts("log_history_fake_harness: PASS");
    return 0;
}
