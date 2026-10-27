#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/dlw_presence_core.h"
#include "../src/dlw_steam_presence_core.h"

static void snapshot_initialize(
    DlumaPresenceSnapshot* snapshot,
    DlumaPresenceContextKind context)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->context = context;
}

static void test_status_mapping(void)
{
    DlumaPresenceSnapshot snapshot;
    char output[DLUMA_STEAM_STATUS_CAPACITY];

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_FRONTEND);
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "In the Main Menu");
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(output, "In the Main Menu") == 0);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    snapshot.flags = DLUMA_PRESENCE_HAS_QUEST |
                     DLUMA_PRESENCE_PAUSE_KNOWN |
                     DLUMA_PRESENCE_PAUSED;
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "Map: Old Town");
    strcpy_s(snapshot.secondary_text, sizeof(snapshot.secondary_text),
             "Quest: Find the cure");
    strcpy_s(snapshot.pause_text, sizeof(snapshot.pause_text),
             "Game is PAUSED");
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(
        output,
        "Map: Old Town | Quest: Find the cure | Game is PAUSED") == 0);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "Map: Old Town");
    strcpy_s(snapshot.secondary_text, sizeof(snapshot.secondary_text),
             "Exploring Old Town");
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(output, "Exploring Old Town") == 0);

    snapshot.flags = DLUMA_PRESENCE_PAUSE_KNOWN;
    strcpy_s(snapshot.pause_text, sizeof(snapshot.pause_text),
             "Game is RUNNING");
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(
        output, "Exploring Old Town | Game is RUNNING") == 0);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_EDITOR);
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "Editing Map");
    strcpy_s(snapshot.secondary_text, sizeof(snapshot.secondary_text),
             "Harbor");
    assert(!dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(output[0] == 0);
    snapshot.context = DLUMA_PRESENCE_CONTEXT_NONE;
    assert(!dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(output[0] == 0);
}

static void test_status_safety_and_utf8_boundaries(void)
{
    DlumaPresenceSnapshot snapshot;
    char output[DLUMA_STEAM_STATUS_CAPACITY];
    unsigned int i;
    unsigned int length;
    static const char emoji[] = "\xF0\x9F\x8C\x86";

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    strcpy_s(
        snapshot.secondary_text, sizeof(snapshot.secondary_text),
        "Exploring X:\\Profiles\\Player https://example.invalid/map "
        "workshop.example 3771245504 (3771245505), Safe Place");
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(output, "Exploring Safe Place") == 0);
    assert(strstr(output, "Profiles") == 0);
    assert(strstr(output, "http") == 0);
    assert(strstr(output, "3771245504") == 0);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    for (i = 0; i < 255U; ++i)
        snapshot.secondary_text[i] = 'a';
    snapshot.secondary_text[255] = 0;
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strlen(output) == 255U);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    for (i = 0; i < 100U; ++i)
        memcpy(snapshot.secondary_text + i * 4U, emoji, 4U);
    snapshot.secondary_text[400] = 0;
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    length = (unsigned int)strlen(output);
    assert(length <= DLUMA_STEAM_STATUS_MAX_UTF8_BYTES);
    assert(length % 4U == 0U);
    assert(length == 252U);

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_GAMEPLAY);
    memcpy(
        snapshot.secondary_text,
        "Exploring A\r\nB\xFF" "C\tD",
        sizeof("Exploring A\r\nB\xFF" "C\tD"));
    assert(dluma_presence_format_steam_status(
        &snapshot, output, sizeof(output)));
    assert(strcmp(output, "Exploring A B C D") == 0);
    assert(strchr(output, '\r') == 0);
    assert(strchr(output, '\n') == 0);
    assert(strchr(output, '\t') == 0);
}

static DlumaSteamPresenceModuleEvidence accepted_module(void)
{
    DlumaSteamPresenceModuleEvidence evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.host_profile = DLUMA_STEAM_HOST_PLAYER;
    evidence.module_present = 1;
    evidence.canonical_colocation = 1;
    evidence.hash_match = 1;
    evidence.pe_identity_match = 1;
    evidence.accessor_export_present = 1;
    evidence.setter_export_present = 1;
    evidence.export_bytes_match = 1;
    evidence.export_pointer_ownership = 1;
    evidence.accessor_succeeded = 1;
    return evidence;
}

static void test_fake_module_assessment(void)
{
    DlumaSteamPresenceModuleEvidence evidence = accepted_module();
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_READY);

    evidence.module_present = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_RETRY);
    evidence = accepted_module();
    evidence.host_profile = DLUMA_STEAM_HOST_UNSUPPORTED;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_UNSUPPORTED);
    evidence.host_profile = DLUMA_STEAM_HOST_RETAIL_UNSUPPORTED;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_UNSUPPORTED);

    evidence = accepted_module();
    evidence.hash_match = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_REJECTED);
    evidence = accepted_module();
    evidence.accessor_export_present = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_REJECTED);
    evidence = accepted_module();
    evidence.setter_export_present = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_REJECTED);
    evidence = accepted_module();
    evidence.export_bytes_match = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_REJECTED);
    evidence = accepted_module();
    evidence.export_pointer_ownership = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_REJECTED);
    evidence = accepted_module();
    evidence.accessor_succeeded = 0;
    assert(dluma_steam_presence_assess_module(&evidence) ==
           DLUMA_STEAM_MODULE_RETRY);
}

static void test_publish_deduplicate_retry_and_transitions(void)
{
    DlumaSteamPresenceCore state;
    DlumaPresenceSnapshot snapshot;
    DlumaSteamPresenceAction action;

    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_FRONTEND);
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "In the Main Menu");
    dluma_steam_presence_core_initialize(&state, 1);
    dluma_steam_presence_core_observe(&state, &snapshot);
    action = dluma_steam_presence_core_next_action(&state, 1000);
    assert(action.kind == DLUMA_STEAM_ACTION_SET_STATUS);
    assert(strcmp(action.value, "In the Main Menu") == 0);

    dluma_steam_presence_core_complete(&state, &action, 0, 1000);
    assert(state.next_retry_ms == 2000);
    assert(dluma_steam_presence_core_next_action(&state, 1999).kind ==
           DLUMA_STEAM_ACTION_NONE);
    action = dluma_steam_presence_core_next_action(&state, 2000);
    assert(action.kind == DLUMA_STEAM_ACTION_SET_STATUS);
    dluma_steam_presence_core_complete(&state, &action, 1, 2000);
    assert(state.owns_status);
    assert(dluma_steam_presence_core_next_action(&state, 2001).kind ==
           DLUMA_STEAM_ACTION_NONE);

    dluma_steam_presence_core_set_enabled(&state, 0);
    action = dluma_steam_presence_core_next_action(&state, 2002);
    assert(action.kind == DLUMA_STEAM_ACTION_REMOVE_STATUS);
    dluma_steam_presence_core_complete(&state, &action, 0, 2002);
    assert(state.owns_status);
    assert(dluma_steam_presence_core_next_action(&state, 3001).kind ==
           DLUMA_STEAM_ACTION_NONE);
    action = dluma_steam_presence_core_next_action(&state, 3002);
    assert(action.kind == DLUMA_STEAM_ACTION_REMOVE_STATUS);
    dluma_steam_presence_core_complete(&state, &action, 1, 3002);
    assert(!state.owns_status);

    dluma_steam_presence_core_set_enabled(&state, 1);
    action = dluma_steam_presence_core_next_action(&state, 3003);
    assert(action.kind == DLUMA_STEAM_ACTION_SET_STATUS);
    dluma_steam_presence_core_complete(&state, &action, 1, 3003);
    dluma_steam_presence_core_observe(&state, 0);
    action = dluma_steam_presence_core_next_action(&state, 3004);
    assert(action.kind == DLUMA_STEAM_ACTION_REMOVE_STATUS);
    dluma_steam_presence_core_complete(&state, &action, 1, 3004);
    assert(!state.owns_status);

    dluma_steam_presence_core_initialize(&state, 0);
    dluma_steam_presence_core_observe(&state, &snapshot);
    assert(dluma_steam_presence_core_next_action(&state, 1).kind ==
           DLUMA_STEAM_ACTION_NONE);
}

static void test_retry_cap_and_pending_value(void)
{
    DlumaSteamPresenceCore state;
    DlumaPresenceSnapshot snapshot;
    DlumaSteamPresenceAction action;
    unsigned long long now = 0;
    unsigned int i;
    snapshot_initialize(&snapshot, DLUMA_PRESENCE_CONTEXT_FRONTEND);
    strcpy_s(snapshot.primary_text, sizeof(snapshot.primary_text),
             "Selecting Map");
    dluma_steam_presence_core_initialize(&state, 1);
    dluma_steam_presence_core_observe(&state, &snapshot);
    for (i = 0; i < 8U; ++i) {
        action = dluma_steam_presence_core_next_action(&state, now);
        assert(action.kind == DLUMA_STEAM_ACTION_SET_STATUS);
        assert(strcmp(action.value, "Selecting Map") == 0);
        dluma_steam_presence_core_complete(&state, &action, 0, now);
        assert(state.retry_delay_ms <= DLUMA_STEAM_RETRY_MAX_MS);
        now = state.next_retry_ms;
    }
    assert(state.retry_delay_ms == DLUMA_STEAM_RETRY_MAX_MS);
}

int main(void)
{
    test_status_mapping();
    test_status_safety_and_utf8_boundaries();
    test_fake_module_assessment();
    test_publish_deduplicate_retry_and_transitions();
    test_retry_cap_and_pending_value();
    puts("steam_presence_fake_harness: ok");
    return 0;
}
