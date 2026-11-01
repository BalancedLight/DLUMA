#include "../src/core/dlw_launch_level_identity.h"

int main(void)
{
    /* A new stock or cancelled attempt invalidates an older selected root.
     * A nested attempt also prevents its caller from publishing stale data. */
    if (!dluma_launch_record_is_current(1, 1)) return 7;
    if (dluma_launch_record_is_current(1, 2)) return 8;
    if (dluma_launch_record_is_current(0, 2)) return 9;
    if (!dluma_launch_record_is_current(2, 2)) return 10;
    /* Two project launches may have the same map name but distinct levels. */
    if (!dluma_launch_matches_new_level(1, 0, 1, 2, 1, 10, 11)) return 1;
    if (dluma_launch_matches_new_level(1, 1, 1, 2, 1, 10, 11)) return 2;
    if (dluma_launch_matches_new_level(2, 1, 2, 2, 1, 10, 11)) return 3;
    if (!dluma_launch_matches_new_level(2, 1, 2, 3, 1, 10, 11)) return 4;
    if (dluma_launch_matches_new_level(2, 1, 2, 3, 0, 10, 11)) return 5;
    if (dluma_launch_matches_new_level(2, 1, 0, 3, 1, 10, 11)) return 6;
    if (dluma_launch_matches_new_level(2, 1, 2, 3, 1, 10, 9)) return 11;
    return 0;
}
