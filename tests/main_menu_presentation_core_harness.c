#include "dlw_main_menu_presentation_core.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,      \
                    #expression);                                             \
            return 0;                                                         \
        }                                                                     \
    } while (0)

static void make_digest(char *digest, char letter)
{
    unsigned int index;
    for (index = 0; index < DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH;
         ++index)
        digest[index] = letter;
    digest[DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH] = 0;
}

static int test_placeholder_expansion(void)
{
    static const DlumaMainMenuPresentationPlaceholder placeholders[] = {
        {"{version}", "0.3.1"}, {"{commit}", "abcdef012345"}};
    char text[64];
    CHECK(dluma_main_menu_presentation_expand(
        "DL Mod Loader {version}, commit: {commit}", placeholders,
        (unsigned int)(sizeof(placeholders) / sizeof(placeholders[0])),
        text, sizeof(text)));
    CHECK(strcmp(text, "DL Mod Loader 0.3.1, commit: abcdef012345") == 0);
    CHECK(!dluma_main_menu_presentation_expand(
        "DL Mod Loader {version}", placeholders, 2U, text, 8U));
    CHECK(text[0] == 0);
    CHECK(dluma_main_menu_presentation_expand_localized(
        "", 0, "DL Mod Loader {version}", placeholders, 2U,
        text, sizeof(text)));
    CHECK(strcmp(text, "DL Mod Loader 0.3.1") == 0);
    CHECK(dluma_main_menu_presentation_expand_localized(
        "Localized {version}", 1, "DL Mod Loader {version}",
        placeholders, 2U, text, sizeof(text)));
    CHECK(strcmp(text, "Localized 0.3.1") == 0);
    return 1;
}

static int test_sha_prefix(void)
{
    char prefix[DLUMA_MAIN_MENU_PRESENTATION_SHA256_PREFIX_LENGTH + 1U];
    char digest[DLUMA_MAIN_MENU_PRESENTATION_SHA256_HEX_LENGTH + 1U];
    make_digest(digest, 'a');
    memcpy(digest, "abcdef0123456789", 16U);
    CHECK(dluma_main_menu_presentation_sha256_prefix(digest, prefix));
    CHECK(strcmp(prefix, "abcdef0123456789") == 0);
    make_digest(digest, 'A');
    CHECK(dluma_main_menu_presentation_sha256_prefix(digest, prefix));
    CHECK(strcmp(prefix, "AAAAAAAAAAAAAAAA") == 0);
    digest[20] = 'g';
    CHECK(!dluma_main_menu_presentation_sha256_prefix(digest, prefix));
    digest[20] = 0;
    CHECK(!dluma_main_menu_presentation_sha256_prefix(digest, prefix));
    return 1;
}

static int test_commit(void)
{
    char commit[DLUMA_MAIN_MENU_PRESENTATION_GIT_COMMIT_HEX_LENGTH + 1U];
    CHECK(dluma_main_menu_presentation_git_commit(
        "abcdef0123456789abcdef0123456789abcdef01", commit));
    CHECK(strcmp(commit, "abcdef0123456789abcdef0123456789abcdef01") == 0);
    CHECK(!dluma_main_menu_presentation_git_commit("0123456789abcdef", commit));
    CHECK(!dluma_main_menu_presentation_git_commit(
        "abcdef0123456789abcdef0123456789abcdef0g", commit));
    return 1;
}

static int test_holiday_calendar(void)
{
    CHECK(dluma_main_menu_presentation_holiday(2025U, 12U, 30U) ==
          DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 12U, 31U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 1U, 1U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 1U, 2U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NEW_YEAR);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 1U, 3U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 2U, 12U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 2U, 13U) ==
          DLUMA_MAIN_MENU_HOLIDAY_VALENTINES);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 2U, 15U) ==
          DLUMA_MAIN_MENU_HOLIDAY_VALENTINES);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 2U, 16U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 3U, 15U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 3U, 16U) ==
          DLUMA_MAIN_MENU_HOLIDAY_SAINT_PATRICKS);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 3U, 18U) ==
          DLUMA_MAIN_MENU_HOLIDAY_SAINT_PATRICKS);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 3U, 19U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2024U, 3U, 28U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 3U, 29U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 4U, 1U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 4U, 2U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 4U, 18U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 4U, 21U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);
    CHECK(dluma_main_menu_presentation_holiday(2038U, 4U, 23U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);
    CHECK(dluma_main_menu_presentation_holiday(2038U, 4U, 26U) ==
          DLUMA_MAIN_MENU_HOLIDAY_EASTER);

    /* Mother's Day: second Sunday in May, Saturday through Monday. */
    CHECK(dluma_main_menu_presentation_holiday(2025U, 5U, 9U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 5U, 10U) ==
          DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 5U, 12U) ==
          DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 5U, 13U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 5U, 9U) ==
          DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 5U, 11U) ==
          DLUMA_MAIN_MENU_HOLIDAY_MOTHERS_DAY);

    /* Father's Day: third Sunday in June, Saturday through Monday. */
    CHECK(dluma_main_menu_presentation_holiday(2025U, 6U, 14U) ==
          DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 6U, 16U) ==
          DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 6U, 17U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 6U, 20U) ==
          DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2026U, 6U, 22U) ==
          DLUMA_MAIN_MENU_HOLIDAY_FATHERS_DAY);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 2U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 3U) ==
          DLUMA_MAIN_MENU_HOLIDAY_INDEPENDENCE_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 5U) ==
          DLUMA_MAIN_MENU_HOLIDAY_INDEPENDENCE_DAY);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 6U) ==
          DLUMA_MAIN_MENU_HOLIDAY_TANABATA);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 8U) ==
          DLUMA_MAIN_MENU_HOLIDAY_TANABATA);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 7U, 9U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 7U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 8U) ==
          DLUMA_MAIN_MENU_HOLIDAY_LEIF_ERIKSON);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 10U) ==
          DLUMA_MAIN_MENU_HOLIDAY_LEIF_ERIKSON);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 11U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 27U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 10U, 28U) ==
          DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 11U, 1U) ==
          DLUMA_MAIN_MENU_HOLIDAY_HALLOWEEN);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 11U, 2U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);

    CHECK(dluma_main_menu_presentation_holiday(2024U, 11U, 26U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 11U, 27U) ==
          DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 12U, 1U) ==
          DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 12U, 2U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 11U, 26U) ==
          DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 11U, 30U) ==
          DLUMA_MAIN_MENU_HOLIDAY_THANKSGIVING);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 12U, 19U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 12U, 20U) ==
          DLUMA_MAIN_MENU_HOLIDAY_HAPPY_HOLIDAYS);

    CHECK(dluma_main_menu_presentation_holiday(2025U, 2U, 29U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2024U, 2U, 29U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(1500U, 12U, 25U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    CHECK(dluma_main_menu_presentation_holiday(2025U, 13U, 1U) ==
          DLUMA_MAIN_MENU_HOLIDAY_NONE);
    return 1;
}

int main(void)
{
    if (!test_placeholder_expansion() || !test_sha_prefix() ||
        !test_commit() || !test_holiday_calendar())
        return 1;
    puts("main menu presentation core harness: PASS");
    return 0;
}
