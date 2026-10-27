#include <assert.h>
#include <stdio.h>

#include "../src/dlw_common.h"
#include "../src/dlw_workshop_vfs_refresh_core.h"

typedef struct FakePreviewSource {
    DlumaWorkshopVfsRefreshCore refresh;
    int native_dds_count;
    int catalog_dds_count;
    int refresh_calls;
    int backing_probe_calls;
} FakePreviewSource;

static LONG fake_publish_dds(FakePreviewSource *source)
{
    ++source->native_dds_count;
    return dluma_workshop_vfs_refresh_note_published(
        &source->refresh);
}

static int fake_refresh_source(FakePreviewSource *source)
{
    LONG generation = 0;
    if (!dluma_workshop_vfs_refresh_begin(
            &source->refresh, &generation))
        return 0;
    ++source->refresh_calls;
    source->catalog_dds_count = source->native_dds_count;
    dluma_workshop_vfs_refresh_complete(
        &source->refresh, generation);
    return 1;
}

static int fake_exists(FakePreviewSource *source, int ordinal)
{
    return ordinal >= 0 && ordinal < source->catalog_dds_count;
}

static int fake_ensure_visible_and_probe(
    FakePreviewSource *source, int ordinal)
{
    if (!fake_exists(source, ordinal))
    {
        if (!fake_refresh_source(source) ||
            !fake_exists(source, ordinal))
            return 0;
    }
    ++source->backing_probe_calls;
    return 1;
}

int main(void)
{
    FakePreviewSource source = {0};
    LONG first_generation;
    LONG batch_generation;
    LONG race_generation;

    assert(!fake_exists(&source, 0));
    first_generation = fake_publish_dds(&source);
    assert(first_generation == 1);
    assert(!fake_exists(&source, 0));
    assert(fake_refresh_source(&source));
    assert(source.refresh_calls == 1);
    assert(fake_exists(&source, 0));
    assert(!fake_refresh_source(&source));

    batch_generation = fake_publish_dds(&source);
    assert(batch_generation == 2);
    assert(fake_publish_dds(&source) == 3);
    assert(fake_publish_dds(&source) == 4);
    assert(fake_refresh_source(&source));
    assert(source.refresh_calls == 2);
    assert(source.catalog_dds_count == 4);
    assert(!fake_refresh_source(&source));

    source.native_dds_count = source.catalog_dds_count + 1;
    assert(!fake_ensure_visible_and_probe(
        &source, source.catalog_dds_count));
    assert(source.backing_probe_calls == 0);

    assert(fake_publish_dds(&source) == 5);
    assert(fake_ensure_visible_and_probe(&source, 5));
    assert(source.backing_probe_calls == 1);

    assert(dluma_workshop_vfs_refresh_begin(
        &source.refresh, &race_generation) == 0);
    assert(fake_publish_dds(&source) == 6);
    assert(dluma_workshop_vfs_refresh_begin(
        &source.refresh, &race_generation));
    assert(fake_publish_dds(&source) == 7);
    source.catalog_dds_count = source.native_dds_count;
    dluma_workshop_vfs_refresh_complete(
        &source.refresh, race_generation);
    assert(fake_refresh_source(&source));
    assert(source.refresh_calls == 4);

    puts("workshop_vfs_refresh_fake_harness: 26/26 ok");
    return 0;
}
