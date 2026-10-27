#ifndef DLUMA_WORKSHOP_REFRESH_CORE_H
#define DLUMA_WORKSHOP_REFRESH_CORE_H

/* Pure selection policy shared by the exact-build FillMaps hook and its
 * native replay harness. identity_matches may be null when no stable identity
 * was captured. Divider and malformed rows are represented as non-selectable.
 */
static int dluma_workshop_refresh_choose_index(
    const unsigned char *selectable,
    const unsigned char *identity_matches,
    unsigned int count,
    int preferred_index)
{
    unsigned int i;
    unsigned int distance;
    int preferred;
    if (!selectable || !count)
        return -1;
    if (identity_matches)
        for (i = 0; i < count; ++i)
            if (selectable[i] && identity_matches[i])
                return (int)i;
    preferred = preferred_index;
    if (preferred < 0) preferred = 0;
    if ((unsigned int)preferred >= count)
        preferred = (int)count - 1;
    if (selectable[preferred])
        return preferred;
    for (distance = 1; distance < count; ++distance)
    {
        int lower = preferred - (int)distance;
        int upper = preferred + (int)distance;
        /* Divider repair follows the list's reading order: prefer the first
         * real row below the divider, then fall back above it. */
        if ((unsigned int)upper < count && selectable[upper])
            return upper;
        if (lower >= 0 && selectable[lower])
            return lower;
    }
    return -1;
}

/* Focus and selection are independent of the native viewport.  Mouse-wheel
 * browsing can leave focus above the visible window, so preserve the actual
 * first-visible index directly and clamp it only to the rebuilt list. */
static int dluma_workshop_refresh_clamp_first_visible(
    int previous_first_visible, unsigned int count)
{
    if (!count || previous_first_visible < 0)
        return -1;
    if ((unsigned int)previous_first_visible >= count)
        return (int)count - 1;
    return previous_first_visible;
}

#endif
