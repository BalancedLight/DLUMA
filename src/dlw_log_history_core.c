#include "dlw_log_history_core.h"

static LONGLONG dluma_log_history_read_i64(const volatile LONGLONG *value)
{
    return _InterlockedCompareExchange64((volatile LONGLONG *)value, 0, 0);
}

void dluma_log_history_store_reset(DlumaLogHistoryStore *store)
{
    if (!store)
        return;
    memset(store, 0, sizeof(*store));
}

void dluma_log_history_store_start_session(
    DlumaLogHistoryStore *store,
    ULONGLONG tick_ms)
{
    if (!store)
        return;
    dluma_log_history_store_reset(store);
    store->session_start_tick_ms = tick_ms;
}

DlumaLogHistorySeverity dluma_log_history_severity_for_level(
    const char *level)
{
    if (level && dluma_stricmp(level, "WARN") == 0)
        return DLUMA_LOG_HISTORY_WARNING;
    if (level && dluma_stricmp(level, "ERROR") == 0)
        return DLUMA_LOG_HISTORY_ERROR;
    if (level && (dluma_stricmp(level, "DEBUG") == 0 ||
                  dluma_stricmp(level, "TRACE") == 0))
        return DLUMA_LOG_HISTORY_DEBUG;
    return DLUMA_LOG_HISTORY_INFO;
}

void dluma_log_history_store_publish(
    DlumaLogHistoryStore *store,
    ULONGLONG tick_ms,
    const char *level,
    const char *message)
{
    ULONGLONG sequence;
    DlumaLogHistorySlot *slot;
    LONG version;
    if (!store)
        return;

    sequence = (ULONGLONG)dluma_log_history_read_i64(
                   &store->published_total) + 1ULL;
    slot = &store->slots[(unsigned int)(sequence - 1ULL) &
                         (DLUMA_LOG_HISTORY_CAPACITY - 1U)];
    version = dluma_atomic_increment(&slot->version);
    if (!(version & 1))
        version = dluma_atomic_increment(&slot->version);

    if (sequence == 1ULL && !store->session_start_tick_ms)
        store->session_start_tick_ms = tick_ms;
    slot->entry.sequence = sequence;
    slot->entry.tick_ms = tick_ms >= store->session_start_tick_ms
                              ? tick_ms - store->session_start_tick_ms
                              : 0ULL;
    slot->entry.severity = dluma_log_history_severity_for_level(level);
    dluma_str_copy(
        slot->entry.level, DLUMA_ARRAY_COUNT(slot->entry.level),
        level && level[0] ? level : "INFO");
    dluma_str_copy(
        slot->entry.message, DLUMA_ARRAY_COUNT(slot->entry.message),
        message ? message : "");

    dluma_atomic_exchange(&slot->version, version + 1);
    _InterlockedExchange64(
        &store->published_total, (LONGLONG)sequence);
    _InterlockedExchange64(&store->revision, (LONGLONG)sequence);
}

ULONGLONG dluma_log_history_store_revision(
    const DlumaLogHistoryStore *store)
{
    if (!store)
        return 0ULL;
    return (ULONGLONG)dluma_log_history_read_i64(&store->revision);
}

unsigned int dluma_log_history_store_snapshot(
    const DlumaLogHistoryStore *store,
    DlumaLogHistoryEntry *out,
    unsigned int capacity,
    ULONGLONG *revision_out,
    ULONGLONG *published_total_out)
{
    ULONGLONG total;
    ULONGLONG first;
    unsigned int available;
    unsigned int copied = 0;
    unsigned int index;

    if (revision_out)
        *revision_out = 0ULL;
    if (published_total_out)
        *published_total_out = 0ULL;
    if (!store || !out || !capacity)
        return 0;

    total = (ULONGLONG)dluma_log_history_read_i64(
        &store->published_total);
    if (published_total_out)
        *published_total_out = total;
    if (revision_out)
        *revision_out = dluma_log_history_store_revision(store);
    available = total > DLUMA_LOG_HISTORY_CAPACITY
                    ? DLUMA_LOG_HISTORY_CAPACITY
                    : (unsigned int)total;
    if (available > capacity)
        available = capacity;
    first = total - (ULONGLONG)available + 1ULL;

    for (index = 0; index < available; ++index)
    {
        ULONGLONG expected = first + (ULONGLONG)index;
        const DlumaLogHistorySlot *slot =
            &store->slots[(unsigned int)(expected - 1ULL) &
                          (DLUMA_LOG_HISTORY_CAPACITY - 1U)];
        unsigned int attempt;
        for (attempt = 0; attempt < 4U; ++attempt)
        {
            LONG before = dluma_atomic_compare_exchange(
                (volatile LONG *)&slot->version, 0, 0);
            DlumaLogHistoryEntry candidate;
            LONG after;
            if (before & 1)
                continue;
            memcpy(&candidate, &slot->entry, sizeof(candidate));
            after = dluma_atomic_compare_exchange(
                (volatile LONG *)&slot->version, 0, 0);
            if (before == after && !(after & 1) &&
                candidate.sequence == expected)
            {
                out[copied++] = candidate;
                break;
            }
        }
    }
    return copied;
}

int dluma_log_history_entry_matches(
    const DlumaLogHistoryEntry *entry,
    const char *query)
{
    if (!entry)
        return 0;
    if (!query || !query[0])
        return 1;
    return dluma_contains_icase(entry->level, query) ||
           dluma_contains_icase(entry->message, query);
}
