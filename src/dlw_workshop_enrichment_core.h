#ifndef DLUMA_WORKSHOP_ENRICHMENT_CORE_H
#define DLUMA_WORKSHOP_ENRICHMENT_CORE_H

/* Pure Steam/cache decisions shared by the game-thread implementation and
 * the native replay harness. No function performs I/O or mutates state. */
static int dluma_workshop_enrichment_batch_contains(
    const ULONGLONG *ids, unsigned int count, ULONGLONG published_id)
{
    unsigned int i;
    if (!ids || !published_id)
        return 0;
    for (i = 0; i < count; ++i)
        if (ids[i] == published_id)
            return 1;
    return 0;
}

static int dluma_workshop_enrichment_cache_is_current(
    int preview_ready,
    unsigned int cache_updated_time,
    unsigned int remote_updated_time)
{
    return preview_ready && remote_updated_time != 0 &&
           cache_updated_time == remote_updated_time;
}

static int dluma_workshop_enrichment_should_start_http(
    int http_available,
    int requested,
    int details_ready,
    int cache_current,
    int download_started,
    int preview_url_available,
    int preview_failed,
    unsigned int attempts,
    unsigned int maximum_attempts,
    ULONGLONG now,
    ULONGLONG retry_after)
{
    return http_available && requested && details_ready && !cache_current &&
           !download_started && preview_url_available && !preview_failed &&
           attempts < maximum_attempts && now >= retry_after;
}

static int dluma_workshop_enrichment_should_retry_persona(
    int author_available,
    int details_ready,
    int owner_available,
    int author_ready,
    int author_failed,
    unsigned int attempts,
    unsigned int maximum_attempts,
    ULONGLONG started,
    ULONGLONG now,
    ULONGLONG retry_delay,
    ULONGLONG timeout)
{
    ULONGLONG elapsed;
    if (!author_available || !details_ready || !owner_available ||
        author_ready || author_failed || !started || now < started ||
        attempts >= maximum_attempts)
        return 0;
    elapsed = now - started;
    return elapsed >= retry_delay && elapsed < timeout;
}

static int dluma_workshop_enrichment_persona_timed_out(
    int author_ready,
    ULONGLONG started,
    ULONGLONG now,
    ULONGLONG timeout)
{
    return !author_ready && started && now >= started &&
           now - started >= timeout;
}

static int dluma_workshop_enrichment_cached_preview_survives_failure(
    int preview_ready)
{
    return preview_ready != 0;
}

#endif
