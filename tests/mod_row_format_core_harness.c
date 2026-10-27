#include "dlw_mod_row_format_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,     \
                    #expression);                                             \
            return 0;                                                         \
        }                                                                     \
    } while (0)

static int test_compact_titles(void)
{
    char compact[64];
    CHECK(dluma_mod_row_format_utf8_ellipsis(
        "Dying-Light-Universal-Mod-Loader--0.3.0-", 24U,
        compact, sizeof(compact)));
    CHECK(strcmp(compact, "Dying-Light-Universal-Mo...") == 0);
    CHECK(dluma_mod_row_format_utf8_ellipsis(
        "\xE3\x83\xA2\xE3\x83\x89\xE5\x90\x8D\xE3\x81\x8C\xE9\x95\xB7\xE3\x81\x84", 8U,
        compact, sizeof(compact)));
    CHECK(strcmp(compact, "\xE3\x83\xA2\xE3\x83\x89...") == 0);
    CHECK(dluma_mod_row_format_utf8_ellipsis("", 24U, compact, sizeof(compact)));
    CHECK(strcmp(compact, "") == 0);
    return 1;
}

static int test_version_and_associations(void)
{
    DlumaModRowAssociationPlan plan;
    void *cached_item = (void *)1;
    CHECK(strcmp(dluma_mod_row_format_version(0), "UNKNOWN") == 0);
    CHECK(strcmp(dluma_mod_row_format_version(""), "UNKNOWN") == 0);
    CHECK(strcmp(dluma_mod_row_format_version("0.3.0"), "0.3.0") == 0);
    dluma_mod_row_format_association_plan(&plan);
    CHECK(plan.primary_association == 0);
    CHECK(plan.secondary_count == 2U);
    CHECK(plan.secondary_associations[0] == 1);
    CHECK(plan.secondary_associations[1] == 2);
    CHECK(dluma_mod_row_format_accept_live_item(4, 3, (void *)1));
    CHECK(!dluma_mod_row_format_accept_live_item(4, 4, (void *)1));
    CHECK(!dluma_mod_row_format_accept_live_item(4, 3, 0));
    CHECK(dluma_mod_row_format_rebind_live_item(
        4, 3, (void *)2, &cached_item));
    CHECK(cached_item == (void *)2);
    CHECK(!dluma_mod_row_format_rebind_live_item(
        4, 4, (void *)3, &cached_item));
    CHECK(cached_item == (void *)2);
    return 1;
}

int main(void)
{
    if (!test_compact_titles() || !test_version_and_associations())
        return 1;
    puts("mod row format core harness: PASS");
    return 0;
}
