#include <assert.h>
#include <stdio.h>

typedef unsigned long long ULONGLONG;
#include "../src/dlw_workshop_enrichment_core.h"

int main(void)
{
    static const ULONGLONG batch[] = {11ULL, 22ULL, 33ULL};

    assert(dluma_workshop_enrichment_batch_contains(batch, 3, 22ULL));
    assert(!dluma_workshop_enrichment_batch_contains(batch, 3, 44ULL));
    assert(!dluma_workshop_enrichment_batch_contains(batch, 3, 0ULL));

    assert(dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 0, 0, 1, 0, 0, 2, 100, 0));
    assert(!dluma_workshop_enrichment_should_start_http(
        0, 1, 1, 0, 0, 1, 0, 0, 2, 100, 0));
    assert(dluma_workshop_enrichment_cache_is_current(1, 50, 50));
    assert(!dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 1, 0, 1, 0, 0, 2, 100, 0));
    assert(!dluma_workshop_enrichment_cache_is_current(1, 49, 50));
    assert(dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 0, 0, 1, 0, 1, 2, 100, 0));
    assert(!dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 0, 1, 1, 0, 0, 2, 100, 0));
    assert(!dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 0, 0, 1, 1, 0, 2, 100, 0));
    assert(!dluma_workshop_enrichment_should_start_http(
        1, 1, 1, 0, 0, 1, 0, 0, 2, 99, 100));

    assert(!dluma_workshop_enrichment_should_retry_persona(
        1, 1, 1, 0, 0, 1, 2, 100, 104, 5, 15));
    assert(dluma_workshop_enrichment_should_retry_persona(
        1, 1, 1, 0, 0, 1, 2, 100, 105, 5, 15));
    assert(!dluma_workshop_enrichment_should_retry_persona(
        1, 1, 1, 0, 0, 2, 2, 100, 105, 5, 15));
    assert(dluma_workshop_enrichment_persona_timed_out(
        0, 100, 115, 15));

    assert(dluma_workshop_enrichment_cached_preview_survives_failure(1));
    assert(!dluma_workshop_enrichment_cached_preview_survives_failure(0));

    puts("workshop_enrichment_fake_harness: 18/18 ok");
    return 0;
}
