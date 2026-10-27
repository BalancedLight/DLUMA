#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/dlw_discord_presence_core.h"

#include "../src/dlw_editor_presence_core.h"
static void test_menu_mapping(void)
{
    assert(dluma_discord_menu_from_visibility(0) ==
           DLUMA_DISCORD_MENU_MAIN);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP) ==
           DLUMA_DISCORD_MENU_SELECTING_MAP);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_SETTINGS) ==
           DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_MODS) ==
           DLUMA_DISCORD_MENU_ADJUSTING_MODS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_CREDITS) ==
           DLUMA_DISCORD_MENU_READING_CREDITS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_STATISTICS) ==
           DLUMA_DISCORD_MENU_VIEWING_STATISTICS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_SETTINGS |
               DLUMA_DISCORD_MENU_VISIBLE_MODS) ==
           DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_MODS |
               DLUMA_DISCORD_MENU_VISIBLE_CREDITS) ==
           DLUMA_DISCORD_MENU_ADJUSTING_MODS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_CREDITS |
               DLUMA_DISCORD_MENU_VISIBLE_STATISTICS) ==
           DLUMA_DISCORD_MENU_READING_CREDITS);
    assert(dluma_discord_menu_from_visibility(
               DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP |
               DLUMA_DISCORD_MENU_VISIBLE_SETTINGS |
               DLUMA_DISCORD_MENU_VISIBLE_MODS |
               DLUMA_DISCORD_MENU_VISIBLE_CREDITS |
               DLUMA_DISCORD_MENU_VISIBLE_STATISTICS) ==
           DLUMA_DISCORD_MENU_SELECTING_MAP);
}

static void test_level_source_priority(void)
{
    assert(dluma_discord_choose_level_source(0, 0) ==
           DLUMA_DISCORD_LEVEL_SOURCE_NONE);
    assert(dluma_discord_choose_level_source(1, 0) ==
           DLUMA_DISCORD_LEVEL_SOURCE_ACTIVE);
    assert(dluma_discord_choose_level_source(0, 1) ==
           DLUMA_DISCORD_LEVEL_SOURCE_SESSION);
    /*
     * The retained active snapshot may still name the frontend while the
     * current direct query is null. A validated live SessionDI observation
     * is the newer positive state and must win.
     */
    assert(dluma_discord_choose_level_source(1, 1) ==
           DLUMA_DISCORD_LEVEL_SOURCE_SESSION);
}

static void test_utf8(void)
{
    static const char controlled[] = {
        'A', '\r', '\n', '\t', 'B', '\0', 'C'
    };
    static const char invalid[] = "A\xFF" "B";
    static const char emoji_unit[] = "\xF0\x9F\x8C\x86";
    static const char unicode_lines[] =
        "one\xE2\x80\xA8two\xE2\x80\xA9three";
    char emoji[4 * 130 + 1];
    char output[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    unsigned int i;
    unsigned int written;

    written = dluma_discord_utf8_sanitize(
        controlled, sizeof(controlled), output, sizeof(output), 128);
    assert(written == 2);
    assert(strcmp(output, "AB") == 0);

    written = dluma_discord_utf8_sanitize(
        invalid, sizeof(invalid), output, sizeof(output), 128);
    assert(written == 5);
    assert((unsigned char)output[1] == 0xEF);
    assert((unsigned char)output[2] == 0xBF);
    assert((unsigned char)output[3] == 0xBD);

    for (i = 0; i < 130; ++i) {
        memcpy(emoji + i * 4, emoji_unit, 4);
    }
    emoji[sizeof(emoji) - 1] = 0;
    written = dluma_discord_utf8_sanitize(
        emoji, sizeof(emoji), output, sizeof(output), 128);
    assert(written == 512);
    assert(output[512] == 0);

    written = dluma_discord_utf8_sanitize(
        unicode_lines, sizeof(unicode_lines) - 1,
        output, sizeof(output), 128);
    assert(written == 11);
    assert(strcmp(output, "onetwothree") == 0);
}

static void test_urls(void)
{
    char longest[302];
    char workshop_url[DLUMA_DISCORD_ACTIVITY_BUTTON_URL_CAPACITY];
    unsigned int i;
    assert(dluma_discord_https_asset_url_valid(
        "https://steamusercontent.example/preview.jpg"));
    assert(!dluma_discord_https_asset_url_valid(
        "http://steamusercontent.example/preview.jpg"));
    assert(!dluma_discord_https_asset_url_valid(
        "https://steamusercontent.example/bad path.jpg"));
    assert(!dluma_discord_https_asset_url_valid(
        "https://steamusercontent.example\\preview.jpg"));
    assert(!dluma_discord_https_asset_url_valid(
        "https://steamusercontent.example/\xC3\xA9.jpg"));
    memcpy(longest, "https://", 8);
    for (i = 8; i < 300; ++i) longest[i] = 'a';
    longest[300] = 0;
    assert(dluma_discord_https_asset_url_valid(longest));
    longest[300] = 'a';
    longest[301] = 0;
    assert(!dluma_discord_https_asset_url_valid(longest));

    assert(dluma_discord_build_workshop_item_url(
        "3771245504", sizeof("3771245504"),
        workshop_url, sizeof(workshop_url)));
    assert(strcmp(
        workshop_url,
        "https://steamcommunity.com/sharedfiles/filedetails/"
        "?id=3771245504") == 0);
    assert(!dluma_discord_build_workshop_item_url(
        "", sizeof(""), workshop_url, sizeof(workshop_url)));
    assert(!dluma_discord_build_workshop_item_url(
        "0", sizeof("0"), workshop_url, sizeof(workshop_url)));
    assert(!dluma_discord_build_workshop_item_url(
        "12x", sizeof("12x"), workshop_url, sizeof(workshop_url)));
    assert(!dluma_discord_build_workshop_item_url(
        "18446744073709551616",
        sizeof("18446744073709551616"),
        workshop_url, sizeof(workshop_url)));
    assert(!dluma_discord_build_workshop_item_url(
        "3771245504", sizeof("3771245504"),
        workshop_url, 32));
}

static void test_localized_name_formats(void)
{
    char output[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    static const char japanese[] =
        "{name}\xE3\x82\x92\xE6\x8E\xA2\xE7\xB4\xA2\xE4\xB8\xAD";
    static const char expected[] =
        "\xE6\x9D\xB1\xE4\xBA\xAC"
        "\xE3\x82\x92\xE6\x8E\xA2\xE7\xB4\xA2\xE4\xB8\xAD";
    assert(dluma_discord_apply_name_format(
        "Map: {name}", sizeof("Map: {name}"),
        "SampleProject", sizeof("SampleProject"),
        output, sizeof(output), 128));
    assert(strcmp(output, "Map: SampleProject") == 0);
    assert(dluma_discord_apply_name_format(
        japanese, sizeof(japanese),
        "\xE6\x9D\xB1\xE4\xBA\xAC",
        sizeof("\xE6\x9D\xB1\xE4\xBA\xAC"),
        output, sizeof(output), 128));
    assert(strcmp(output, expected) == 0);
    assert(!dluma_discord_apply_name_format(
        "Map", sizeof("Map"), "value", sizeof("value"),
        output, sizeof(output), 128));
}

static void test_debounce(void)
{
    DlumaDiscordDebounceState state;
    int changed = 0;
    memset(&state, 0, sizeof(state));
    assert(dluma_discord_debounce_update(
               &state, 10, 1, 1000, 250, 1500, &changed) == 10);
    assert(changed);
    changed = 0;
    assert(dluma_discord_debounce_update(
               &state, 20, 1, 1100, 250, 1500, &changed) == 10);
    assert(!changed);
    assert(dluma_discord_debounce_update(
               &state, 20, 1, 1349, 250, 1500, &changed) == 10);
    assert(dluma_discord_debounce_update(
               &state, 20, 1, 1350, 250, 1500, &changed) == 20);
    assert(changed);
    changed = 0;
    assert(dluma_discord_debounce_update(
               &state, 0, 0, 2800, 250, 1500, &changed) == 20);
    assert(state.has_committed);
    assert(dluma_discord_debounce_update(
               &state, 0, 0, 2851, 250, 1500, &changed) == 0);
    assert(!state.has_committed);
    assert(changed);
}

static void test_timer_identity(void)
{
    DlumaDiscordTimerState timer;
    memset(&timer, 0, sizeof(timer));

    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_MENU,
               0, 0, 1000) == 1000);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_MENU,
               99, 0x9999, 1050) == 1000);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_GAMEPLAY,
               10, 0x1000, 1100) == 1100);
    /*
     * Pause, resume, quest, title, and image changes do not change the
     * gameplay identity, so they must retain the original map start.
     */
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_GAMEPLAY,
               10, 0x1000, 1200) == 1100);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_GAMEPLAY,
               11, 0x2000, 1300) == 1300);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_EDITOR,
               0, 0x3000, 1400) == 1400);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_EDITOR,
               0, 0x3000, 1450) == 1400);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_EDITOR,
               0, 0x4000, 1500) == 1500);
    assert(dluma_discord_timer_update(
               &timer, DLUMA_DISCORD_TIMER_NONE,
               0, 0, 0) == 0);
    assert(!timer.valid);
}

static void test_coalescing(void)
{
    DlumaDiscordActivityV1 left;
    DlumaDiscordActivityV1 right;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    left.struct_size = right.struct_size = sizeof(left);
    left.generation = 1;
    right.generation = 99;
    strcpy_s(left.details, sizeof(left.details), "Selecting Map");
    strcpy_s(right.details, sizeof(right.details), "Selecting Map");
    strcpy_s(left.large_image, sizeof(left.large_image), "dl_fallback");
    strcpy_s(right.large_image, sizeof(right.large_image), "dl_fallback");
    assert(dluma_discord_activity_equal(&left, &right));
    right.flags = DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE;
    assert(!dluma_discord_activity_equal(&left, &right));
    right = left;
    left.flags = right.flags =
        DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP;
    left.start_timestamp_seconds = 1000;
    right.start_timestamp_seconds = 1001;
    assert(!dluma_discord_activity_equal(&left, &right));
    right = left;
    left.flags = right.flags =
        DLUMA_DISCORD_ACTIVITY_HAS_BUTTON;
    strcpy_s(
        left.button_label, sizeof(left.button_label),
        "View on Steam Workshop");
    strcpy_s(
        right.button_label, sizeof(right.button_label),
        "View on Steam Workshop");
    strcpy_s(
        left.button_url, sizeof(left.button_url),
        "https://steamcommunity.com/sharedfiles/filedetails/?id=1");
    strcpy_s(
        right.button_url, sizeof(right.button_url),
        "https://steamcommunity.com/sharedfiles/filedetails/?id=1");
    assert(dluma_discord_activity_equal(&left, &right));
    right.button_url[strlen(right.button_url) - 1] = '2';
    assert(!dluma_discord_activity_equal(&left, &right));
}

static void test_editor_documents_and_privacy(void)
{
    DlumaEditorPresenceObservation map;
    DlumaEditorPresenceObservation same_map;
    DlumaEditorPresenceObservation quest;
    DlumaEditorPresenceObservation script;
    DlumaEditorPresenceObservation generic;
    DlumaEditorPresenceObservation browsing;
    char long_title[700];
    unsigned int i;

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\nested\\Harbor.map - Dying Light Developer Tools",
        sizeof("folder\\nested\\Harbor.map - Dying Light Developer Tools"),
        &map));
    assert(map.snapshot.context == DLUMA_PRESENCE_CONTEXT_EDITOR);
    assert(strcmp(map.snapshot.primary_text, "Editing Map") == 0);
    assert(strcmp(map.snapshot.secondary_text, "Harbor") == 0);
    assert(strstr(map.snapshot.secondary_text, "folder") == 0);
    assert(strstr(map.snapshot.secondary_text, "nested") == 0);
    assert(map.has_document);

    assert(dluma_editor_presence_parse_title_utf8(
        "Harbor.map", sizeof("Harbor.map"), &same_map));
    assert(dluma_editor_presence_observation_equal(&map, &same_map));

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\quests\\First Light.pxsl",
        sizeof("folder\\quests\\First Light.pxsl"), &quest));
    assert(strcmp(quest.snapshot.primary_text, "Editing Quest") == 0);
    assert(strcmp(quest.snapshot.secondary_text, "First Light") == 0);
    assert(!dluma_editor_presence_observation_equal(&map, &quest));

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\scripts\\weather.scr", sizeof("folder\\scripts\\weather.scr"),
        &script));
    assert(strcmp(script.snapshot.primary_text, "Editing Script") == 0);
    assert(strcmp(script.snapshot.secondary_text, "weather") == 0);

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\layout.asset", sizeof("folder\\layout.asset"),
        &generic));
    assert(strcmp(generic.snapshot.primary_text, "Editing Document") == 0);
    assert(strcmp(generic.snapshot.secondary_text, "layout") == 0);

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\Control\tName.map",
        sizeof("folder\\Control\tName.map"), &generic));
    assert(strcmp(generic.snapshot.secondary_text, "Control Name") == 0);

    assert(dluma_editor_presence_parse_title_utf8(
        "folder\\\xE5\xA4\x9C\xE6\x98\x8E\xE3\x81\x91.map",
        sizeof("folder\\\xE5\xA4\x9C\xE6\x98\x8E\xE3\x81\x91.map"),
        &generic));
    assert(strcmp(
        generic.snapshot.secondary_text,
        "\xE5\xA4\x9C\xE6\x98\x8E\xE3\x81\x91") == 0);

    assert(!dluma_editor_presence_parse_title_utf8(
        "folder\\secret.example.map",
        sizeof("folder\\secret.example.map"), &generic));
    assert(strcmp(
        generic.snapshot.primary_text, "Browsing Developer Tools") == 0);
    assert(!dluma_editor_presence_parse_title_utf8(
        "folder\\12345678901234567890.map",
        sizeof("folder\\12345678901234567890.map"), &generic));

    for (i = 0; i < 600U; ++i) long_title[i] = 'A';
    memcpy(long_title + 600U, ".map", 5U);
    assert(dluma_editor_presence_parse_title_utf8(
        long_title, sizeof(long_title), &generic));
    assert(strlen(generic.snapshot.secondary_text) ==
           DLUMA_STEAM_STATUS_MAX_UTF8_BYTES);

    dluma_editor_presence_build_browsing(&browsing);
    assert(!browsing.has_document);
    assert(strcmp(
        browsing.snapshot.primary_text, "Browsing Developer Tools") == 0);
    assert(browsing.document_identity != map.document_identity);
}


static DlumaEditorPlaytestOwnershipEvidence exact_ownership_evidence(void)
{
    DlumaEditorPlaytestOwnershipEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.pid_match = 1U;
    evidence.start_time_match = 1U;
    evidence.executable_match = 1U;
    evidence.loader_match = 1U;
    evidence.handoff_match = 1U;
    evidence.unexpired = 1U;
    return evidence;
}

static void test_editor_options_and_playtest_ownership(void)
{
    DlumaEditorOptionsModel options;
    DlumaEditorPlaytestOwnershipState ownership;
    DlumaEditorPlaytestOwnershipEvidence evidence;

    assert(dluma_editor_options_model_admit(
        &options, 1, 1, 1, 4U, 4U, 0, 1));
    assert(options.inserted && options.enabled);
    assert(dluma_editor_options_model_select(&options, 5U, 4) ==
           DLUMA_EDITOR_OPTIONS_SHOW_CUSTOM);
    assert(dluma_editor_options_model_select(&options, 5U, 2) ==
           DLUMA_EDITOR_OPTIONS_FORWARD_STOCK);
    assert(dluma_editor_options_model_select(&options, 4U, 4) ==
           DLUMA_EDITOR_OPTIONS_REFUSE);
    assert(dluma_editor_options_model_toggle(&options, 0) ==
           DLUMA_EDITOR_OPTIONS_PERSIST_TOGGLE);
    assert(!options.enabled);
    assert(dluma_editor_options_model_reset(&options) ==
           DLUMA_EDITOR_OPTIONS_PERSIST_RESET);
    assert(options.enabled);
    assert(dluma_editor_options_model_teardown(&options) ==
           DLUMA_EDITOR_OPTIONS_TEARDOWN);
    assert(!options.admitted && !options.inserted);
    assert(dluma_editor_options_model_select(&options, 5U, 0) ==
           DLUMA_EDITOR_OPTIONS_REFUSE);

    assert(!dluma_editor_options_model_admit(
        &options, 0, 1, 1, 4U, 4U, 0, 1));
    assert(!dluma_editor_options_model_admit(
        &options, 1, 0, 1, 4U, 4U, 0, 1));
    assert(!dluma_editor_options_model_admit(
        &options, 1, 1, 0, 4U, 4U, 0, 1));
    assert(!dluma_editor_options_model_admit(
        &options, 1, 1, 1, 3U, 4U, 0, 1));
    assert(!dluma_editor_options_model_admit(
        &options, 1, 1, 1, 4U, 3U, 0, 1));
    assert(!dluma_editor_options_model_admit(
        &options, 1, 1, 1, 4U, 4U, 1, 1));

    memset(&ownership, 0, sizeof(ownership));
    evidence = exact_ownership_evidence();
    assert(dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 2ULL));
    assert(!dluma_editor_playtest_ownership_finish(&ownership, 2ULL));
    assert(dluma_editor_playtest_ownership_finish(&ownership, 1ULL));
    assert(dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 2ULL));
    assert(dluma_editor_playtest_ownership_finish(&ownership, 2ULL));
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));

    memset(&ownership, 0, sizeof(ownership));
    evidence = exact_ownership_evidence();
    evidence.pid_match = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    evidence = exact_ownership_evidence();
    evidence.start_time_match = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    evidence = exact_ownership_evidence();
    evidence.executable_match = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    evidence = exact_ownership_evidence();
    evidence.loader_match = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    evidence = exact_ownership_evidence();
    evidence.handoff_match = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
    evidence = exact_ownership_evidence();
    evidence.unexpired = 0U;
    assert(!dluma_editor_playtest_ownership_begin(
        &ownership, &evidence, 1ULL));
}

static void test_host_routing(void)
{
    const unsigned long long player_application = 11ULL;
    const unsigned long long tools_application = 22ULL;
    DlumaDiscordHostRoute route;

    assert(dluma_discord_select_host_route(
        DLUMA_HOST_PLAYER_STANDALONE, 0,
        player_application, tools_application, &route));
    assert(route.application_id == player_application);
    assert(!route.uses_editor_setting);

    assert(dluma_discord_select_host_route(
        DLUMA_HOST_CHROME_EDITOR, 0,
        player_application, tools_application, &route));
    assert(route.application_id == tools_application);
    assert(route.uses_editor_setting);

    assert(dluma_discord_select_host_route(
        DLUMA_HOST_PLAYER_EDITOR_PLAYTEST, 0,
        player_application, tools_application, &route));
    assert(route.application_id == tools_application);
    assert(!route.uses_editor_setting);

    assert(!dluma_discord_select_host_route(
        DLUMA_HOST_UNKNOWN, 0,
        player_application, tools_application, &route));
    assert(!dluma_discord_select_host_route(
        DLUMA_HOST_CHROME_EDITOR, 1,
        player_application, tools_application, &route));
    assert(!dluma_discord_select_host_route(
        DLUMA_HOST_CHROME_EDITOR, 0,
        player_application, 0, &route));
    assert(!dluma_discord_select_host_route(
        DLUMA_HOST_PLAYER_STANDALONE, 0,
        0, tools_application, &route));
}
int main(void)
{
    test_menu_mapping();
    test_level_source_priority();
    test_utf8();
    test_urls();
    test_localized_name_formats();
    test_debounce();
    test_timer_identity();
    test_coalescing();
    test_editor_documents_and_privacy();
    test_editor_options_and_playtest_ownership();
    test_host_routing();
    assert(dluma_discord_next_backoff(0, 1000, 30000) == 1000);
    assert(dluma_discord_next_backoff(1000, 1000, 30000) == 2000);
    assert(dluma_discord_next_backoff(16000, 1000, 30000) == 30000);
    assert(dluma_discord_next_backoff(30000, 1000, 30000) == 30000);
    puts("discord_presence_fake_harness: ok");
    return 0;
}
