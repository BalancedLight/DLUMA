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

static int test_date_override(void)
{
    unsigned int month = 99U, day = 99U;
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -dluma_date_override=10/31",
        2026U, &month, &day) == 1);
    CHECK(month == 10U && day == 31U);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"\"player path.exe\" \"-DLUMA_DATE_OVERRIDE=12/25\"",
        2026U, &month, &day) == 1);
    CHECK(month == 12U && day == 25U);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -dluma_date_override=02/30",
        2026U, &month, &day) == -1);
    CHECK(month == 12U && day == 25U);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -dluma_date_override=02/29",
        2026U, &month, &day) == -1);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -dluma_date_override=02/29",
        2028U, &month, &day) == 1);
    CHECK(month == 2U && day == 29U);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -other=10/31",
        2026U, &month, &day) == 0);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe -dluma_date_override=1/01",
        2026U, &month, &day) == -1);
    CHECK(dluma_main_menu_presentation_date_override(
        (const unsigned short *)L"player.exe \"-dluma_date_override=10/31",
        2026U, &month, &day) == -1);
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

static int test_seasonal_appearances(void)
{
    static const DlumaMainMenuAppearance halloween_expected[] = {
        {"player_1_tpp.msh", "player_1_zombie"},
        {"player_12_tpp.msh", "player_12_zombie"},
        {"player_2_tpp.msh", "player_2_zombie"},
        {"player_4_tpp.msh", "phosphor_paint_a"}
    };
    static const DlumaMainMenuAppearance christmas_expected[] = {
        {"player_2_tpp.msh", "Christma_event"},
        {"player_9_tpp.msh", "Christmas_event"},
        {"player_14_tpp.msh", "Christmas_event"}
    };
    const DlumaMainMenuAppearanceSet *halloween =
        dluma_main_menu_presentation_appearances(2026U, 10U, 28U);
    const DlumaMainMenuAppearanceSet *christmas =
        dluma_main_menu_presentation_appearances(2026U, 12U, 20U);
    const DlumaMainMenuAppearanceSet *anniversary =
        dluma_main_menu_presentation_appearances(2026U, 1U, 26U);
    const DlumaMainMenuAppearanceSet *parents_day =
        dluma_main_menu_presentation_appearances(2026U, 5U, 9U);
    const DlumaMainMenuAppearanceSet *tanabata =
        dluma_main_menu_presentation_appearances(2026U, 7U, 6U);
    unsigned int i;

    CHECK(halloween && christmas && anniversary && parents_day && tanabata);
    CHECK(halloween->count == 4U && christmas->count == 3U);
    CHECK(anniversary->count == 1U && parents_day->count == 1U);
    CHECK(tanabata->count == 1U);
    for (i = 0; i < halloween->count; ++i) {
        CHECK(strcmp(halloween->appearances[i].model, halloween_expected[i].model) == 0);
        CHECK(strcmp(halloween->appearances[i].skin, halloween_expected[i].skin) == 0);
    }
    for (i = 0; i < christmas->count; ++i) {
        CHECK(strcmp(christmas->appearances[i].model, christmas_expected[i].model) == 0);
        CHECK(strcmp(christmas->appearances[i].skin, christmas_expected[i].skin) == 0);
    }
    CHECK(strcmp(anniversary->appearances[0].model, "player_11_tpp.msh") == 0);
    CHECK(strcmp(anniversary->appearances[0].skin, "TECHLAND_2") == 0);
    CHECK(strcmp(parents_day->appearances[0].model, "player_acolyte_tpp.msh") == 0);
    CHECK(strcmp(parents_day->appearances[0].skin, "5th_anniversary") == 0);
    CHECK(strcmp(tanabata->appearances[0].model, "player_11_tpp.msh") == 0);
    CHECK(strcmp(tanabata->appearances[0].skin, "JAP") == 0);
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 10U, 27U));
    CHECK(dluma_main_menu_presentation_appearances(2026U, 10U, 31U) == halloween);
    CHECK(dluma_main_menu_presentation_appearances(2026U, 11U, 1U) == halloween);
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 11U, 2U));
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 12U, 19U));
    CHECK(dluma_main_menu_presentation_appearances(2026U, 12U, 30U) == christmas);
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 12U, 31U));
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 1U, 25U));
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 1U, 27U));
    CHECK(dluma_main_menu_presentation_appearances(2027U, 1U, 26U) == anniversary);
    CHECK(dluma_main_menu_presentation_appearances(2026U, 5U, 11U) == parents_day);
    CHECK(dluma_main_menu_presentation_appearances(2026U, 6U, 20U) == parents_day);
    CHECK(dluma_main_menu_presentation_appearances(2026U, 6U, 22U) == parents_day);
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 6U, 23U));
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 7U, 5U));
    CHECK(dluma_main_menu_presentation_appearances(2026U, 7U, 7U) == tanabata);
    CHECK(dluma_main_menu_presentation_appearances(2026U, 7U, 8U) == tanabata);
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 7U, 9U));
    CHECK(!dluma_main_menu_presentation_appearances(1500U, 1U, 26U));
    CHECK(!dluma_main_menu_presentation_appearances(2026U, 10U, 32U));
    return 1;
}

static int test_seasonal_props(void)
{
    const DlumaMainMenuPropSet *thanksgiving =
        dluma_main_menu_presentation_props(2025U, 11U, 26U);
    const DlumaMainMenuPropSet *christmas =
        dluma_main_menu_presentation_props(2025U, 12U, 20U);
    unsigned int i;

    CHECK(thanksgiving && christmas);
    CHECK(thanksgiving->count == 2U && christmas->count == 1U);
    CHECK(strcmp(thanksgiving->props[0].mesh, "dlc_stick_chicken_b.msh") == 0);
    CHECK(strcmp(thanksgiving->props[1].mesh, "dlc_stick_chicken.msh") == 0);
    CHECK(strcmp(christmas->props[0].mesh, "wn_christmascandy.msh") == 0);
    for (i = 0; i < thanksgiving->count; ++i) {
        CHECK(!thanksgiving->props[i].skin);
        CHECK(thanksgiving->props[i].grip == DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED);
        CHECK(strcmp(thanksgiving->props[i].idle_loop, "TPP_Stand_menu_A") == 0);
    }
    CHECK(!christmas->props[0].skin);
    CHECK(christmas->props[0].grip == DLUMA_MAIN_MENU_PROP_GRIP_ONE_HANDED);
    CHECK(strcmp(christmas->props[0].idle_loop, "TPP_Stand_menu_A") == 0);
    CHECK(!dluma_main_menu_presentation_props(2025U, 11U, 25U));
    CHECK(dluma_main_menu_presentation_props(2025U, 11U, 30U) == thanksgiving);
    CHECK(!dluma_main_menu_presentation_props(2025U, 12U, 1U));
    CHECK(!dluma_main_menu_presentation_props(2025U, 12U, 19U));
    CHECK(dluma_main_menu_presentation_props(2025U, 12U, 30U) == christmas);
    CHECK(!dluma_main_menu_presentation_props(2025U, 12U, 31U));
    CHECK(!dluma_main_menu_presentation_props(2026U, 10U, 31U));
    CHECK(!dluma_main_menu_presentation_props(1500U, 12U, 25U));
    return 1;
}

static int test_halloween_animation_chance(void)
{
    unsigned int roll;
    unsigned int selected = 0;
    for (roll = 0; roll < 10U; ++roll) {
        selected += dluma_main_menu_presentation_halloween_animation(
            2026U, 10U, 31U, roll) ? 1U : 0U;
        CHECK(!dluma_main_menu_presentation_halloween_animation(
            2026U, 12U, 25U, roll));
    }
    CHECK(selected == 1U);
    CHECK(dluma_main_menu_presentation_halloween_animation(2026U, 10U, 28U, 0U));
    CHECK(dluma_main_menu_presentation_halloween_animation(2026U, 11U, 1U, 0U));
    CHECK(!dluma_main_menu_presentation_halloween_animation(2026U, 10U, 27U, 0U));
    CHECK(!dluma_main_menu_presentation_halloween_animation(2026U, 11U, 2U, 0U));
    return 1;
}

int main(void)
{
    if (!test_placeholder_expansion() || !test_sha_prefix() ||
        !test_commit() || !test_date_override() || !test_holiday_calendar() ||
        !test_seasonal_appearances() || !test_seasonal_props() ||
        !test_halloween_animation_chance())
        return 1;
    puts("main menu presentation core harness: PASS");
    return 0;
}
