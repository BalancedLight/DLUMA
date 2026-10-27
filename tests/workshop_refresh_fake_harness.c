#include "../src/dlw_workshop_refresh_core.h"

#include <assert.h>
#include <stdio.h>

static int choose(
    const unsigned char *selectable,
    const unsigned char *matches,
    unsigned int count,
    int preferred)
{
    return dluma_workshop_refresh_choose_index(
        selectable, matches, count, preferred);
}

int main(void)
{
    static const unsigned char all[] = {1, 1, 1, 1, 1, 1};
    static const unsigned char unchanged[] = {0, 0, 1, 0, 0, 0};
    static const unsigned char reordered[] = {0, 0, 0, 0, 1, 0};
    static const unsigned char inserted[] = {0, 0, 0, 1, 0, 0};
    static const unsigned char removed[] = {0, 0, 0, 0, 0, 0};
    static const unsigned char with_divider[] = {1, 1, 0, 1, 1, 1};
    static const unsigned char duplicate_title_unique_root[] =
        {0, 0, 0, 1, 0, 0};
    static const unsigned char published_id_match[] =
        {0, 1, 0, 0, 0, 0};
    static const unsigned char none[] = {0, 0, 0};

    assert(choose(all, unchanged, 6, 2) == 2);
    assert(choose(all, reordered, 6, 2) == 4);
    assert(choose(all, inserted, 6, 2) == 3);
    assert(choose(with_divider, removed, 6, 2) == 3);
    assert(choose(all, duplicate_title_unique_root, 6, 1) == 3);
    assert(choose(all, published_id_match, 6, 5) == 1);
    assert(choose(with_divider, 0, 6, 2) == 3);
    assert(choose(with_divider, 0, 3, 2) == 1);
    assert(choose(with_divider, 0, 6, 99) == 5);
    assert(choose(none, 0, 3, 1) == -1);
    assert(choose(0, 0, 0, 0) == -1);

    /* Observed runtime shape: selection remained zero while focus and the
     * visible window had advanced to row fourteen. */
    assert(dluma_workshop_refresh_clamp_first_visible(10, 20) == 10);
    assert(dluma_workshop_refresh_clamp_first_visible(10, 8) == 7);
    assert(dluma_workshop_refresh_clamp_first_visible(0, 20) == 0);
    assert(dluma_workshop_refresh_clamp_first_visible(-1, 20) == -1);
    assert(dluma_workshop_refresh_clamp_first_visible(10, 0) == -1);

    puts("workshop_refresh_fake_harness: 16/16 ok");
    return 0;
}
