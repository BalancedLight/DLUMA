#ifndef DLUMA_LAUNCH_LEVEL_IDENTITY_H
#define DLUMA_LAUNCH_LEVEL_IDENTITY_H

#include <stdint.h>

static __inline int dluma_launch_record_is_current(uint64_t record, uint64_t attempt)
{
    return record && record == attempt;
}

/* A selected project is associated only after the outgoing level changes. */
static __inline int dluma_launch_matches_new_level(
    uint64_t launch, uint64_t consumed, uintptr_t previous_level,
    uintptr_t observed_level, int map_matches, uint64_t attempt_ms,
    uint64_t level_sample_ms)
{
    return launch && launch != consumed && previous_level && observed_level &&
        previous_level != observed_level && map_matches && attempt_ms &&
        level_sample_ms > attempt_ms;
}

#endif
