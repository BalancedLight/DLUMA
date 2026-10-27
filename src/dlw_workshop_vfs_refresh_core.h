#ifndef DLUMA_WORKSHOP_VFS_REFRESH_CORE_H
#define DLUMA_WORKSHOP_VFS_REFRESH_CORE_H

typedef struct DlumaWorkshopVfsRefreshCore {
    volatile LONG published_generation;
    volatile LONG refreshed_generation;
    volatile LONG refresh_pending;
} DlumaWorkshopVfsRefreshCore;

static LONG dluma_workshop_vfs_refresh_note_published(
    DlumaWorkshopVfsRefreshCore *state)
{
    LONG generation;
    if (!state)
        return 0;
    generation = dluma_atomic_increment(&state->published_generation);
    if (!generation)
        generation = dluma_atomic_increment(&state->published_generation);
    dluma_atomic_exchange(&state->refresh_pending, 1);
    return generation;
}

static int dluma_workshop_vfs_refresh_begin(
    DlumaWorkshopVfsRefreshCore *state,
    LONG *generation_out)
{
    LONG published;
    LONG refreshed;
    if (generation_out)
        *generation_out = 0;
    if (!state)
        return 0;
    published = dluma_atomic_compare_exchange(
        &state->published_generation, 0, 0);
    refreshed = dluma_atomic_compare_exchange(
        &state->refreshed_generation, 0, 0);
    if (!dluma_atomic_compare_exchange(
            &state->refresh_pending, 0, 0) ||
        !published || published == refreshed)
        return 0;
    if (generation_out)
        *generation_out = published;
    return 1;
}

static void dluma_workshop_vfs_refresh_complete(
    DlumaWorkshopVfsRefreshCore *state,
    LONG generation)
{
    if (!state || !generation)
        return;
    dluma_atomic_exchange(&state->refreshed_generation, generation);
    if (dluma_atomic_compare_exchange(
            &state->published_generation, 0, 0) == generation)
    {
        dluma_atomic_exchange(&state->refresh_pending, 0);
        if (dluma_atomic_compare_exchange(
                &state->published_generation, 0, 0) != generation)
            dluma_atomic_exchange(&state->refresh_pending, 1);
    }
    else
    {
        dluma_atomic_exchange(&state->refresh_pending, 1);
    }
}

#endif
