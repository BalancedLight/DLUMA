#include "../src/dlw_player_startup_core.h"

#include <stdio.h>
#include <string.h>

#define ORIGINAL_A ((void *)0x1010)
#define ORIGINAL_B ((void *)0x2020)
#define REPLACEMENT_A ((void *)0x3030)
#define REPLACEMENT_B ((void *)0x4040)
#define FOREIGN_OWNER ((void *)0x5050)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #condition);            \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct FakeHookPlatform {
    unsigned int calls;
    unsigned int fail_on_call;
} FakeHookPlatform;

static int fake_compare_exchange(
    void *context,
    void **slot,
    void *expected,
    void *replacement,
    void **observed_out)
{
    FakeHookPlatform *platform = (FakeHookPlatform *)context;
    ++platform->calls;
    *observed_out = *slot;
    if (platform->calls == platform->fail_on_call || *slot != expected)
        return 0;
    *slot = replacement;
    *observed_out = replacement;
    return 1;
}

static void initialize_hooks(
    DlumaPlayerStartupHook hooks[2],
    void **slot_a,
    void **slot_b)
{
    memset(hooks, 0, 2U * sizeof(*hooks));
    hooks[0].slot = slot_a;
    hooks[0].original = ORIGINAL_A;
    hooks[0].replacement = REPLACEMENT_A;
    hooks[1].slot = slot_b;
    hooks[1].original = ORIGINAL_B;
    hooks[1].replacement = REPLACEMENT_B;
}

static int test_exact_profile_refusal(void)
{
    static const char exact_hash[] =
        "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A";
    CHECK(dluma_player_startup_core_profile_matches(
        1, 1, 0x623397B8U, 0x00103000U, exact_hash));
    CHECK(!dluma_player_startup_core_profile_matches(
        0, 1, 0x623397B8U, 0x00103000U, exact_hash));
    CHECK(!dluma_player_startup_core_profile_matches(
        1, 0, 0x623397B8U, 0x00103000U, exact_hash));
    CHECK(!dluma_player_startup_core_profile_matches(
        1, 1, 0x623397B9U, 0x00103000U, exact_hash));
    CHECK(!dluma_player_startup_core_profile_matches(
        1, 1, 0x623397B8U, 0x00103001U, exact_hash));
    CHECK(!dluma_player_startup_core_profile_matches(
        1, 1, 0x623397B8U, 0x00103000U,
        "71D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"));
    return 1;
}

static int test_partial_install_rolls_back(void)
{
    DlumaPlayerStartupHook hooks[2];
    FakeHookPlatform platform = {0U, 2U};
    void *slot_a = ORIGINAL_A;
    void *slot_b = ORIGINAL_B;
    initialize_hooks(hooks, &slot_a, &slot_b);
    CHECK(!dluma_player_startup_core_install_hooks(
        hooks, 2U, fake_compare_exchange, &platform));
    CHECK(slot_a == ORIGINAL_A);
    CHECK(slot_b == ORIGINAL_B);
    CHECK(hooks[0].installed && hooks[0].restored);
    CHECK(!hooks[1].installed);
    CHECK(platform.calls == 3U);
    return 1;
}

static int test_foreign_hook_ownership_is_preserved(void)
{
    DlumaPlayerStartupHook hooks[2];
    FakeHookPlatform platform = {0U, 0U};
    void *slot_a = ORIGINAL_A;
    void *slot_b = ORIGINAL_B;
    initialize_hooks(hooks, &slot_a, &slot_b);
    CHECK(dluma_player_startup_core_install_hooks(
        hooks, 2U, fake_compare_exchange, &platform));
    slot_b = FOREIGN_OWNER;
    CHECK(!dluma_player_startup_core_restore_hooks(
        hooks, 2U, fake_compare_exchange, &platform));
    CHECK(slot_a == ORIGINAL_A);
    CHECK(slot_b == FOREIGN_OWNER);
    CHECK(hooks[0].restored);
    CHECK(!hooks[1].restored);
    CHECK(hooks[1].ownership_conflict);
    return 1;
}

static int test_reporter_selection_and_bounded_ttl_copy(void)
{
    char output[8];
    void *caller = (void *)0x7777;
    void *borrowed = (void *)0x8888;
    CHECK(dluma_player_startup_core_select_reporter(caller, borrowed) == caller);
    CHECK(dluma_player_startup_core_select_reporter(0, borrowed) == borrowed);
    CHECK(dluma_player_startup_core_copy_bounded(
        output, sizeof(output), "Loading resources...", 127U) == 7U);
    CHECK(strcmp(output, "Loading") == 0);
    CHECK(dluma_player_startup_core_copy_bounded(
        output, sizeof(output), "abcdef", 3U) == 3U);
    CHECK(strcmp(output, "abc") == 0);
    CHECK(dluma_player_startup_core_copy_bounded(
        output, sizeof(output), 0, 3U) == 0U);
    CHECK(output[0] == 0);
    return 1;
}

static int test_splash_repaint_and_cleanup_are_idempotent(void)
{
    DlumaPlayerStartupSnapshot snapshot;
    dluma_player_startup_core_initialize(&snapshot);
    dluma_player_startup_core_bind_splash(&snapshot);
    dluma_player_startup_core_status(
        &snapshot, "Preparing content...", 0, 1);
    dluma_player_startup_core_status(
        &snapshot, "Loading resources...", 1, 1);
    CHECK(snapshot.splash_bound);
    CHECK(snapshot.status_update_count == 2U);
    CHECK(snapshot.native_status_count == 1U);
    CHECK(snapshot.splash_repaint_count == 2U);
    CHECK(strcmp(snapshot.current_status, "Loading resources...") == 0);
    dluma_player_startup_core_restore_splash(&snapshot);
    dluma_player_startup_core_restore_splash(&snapshot);
    CHECK(snapshot.splash_restored);
    return 1;
}

static int test_hwnd_filtering_and_reveal_policy(void)
{
    CHECK(dluma_player_startup_core_window_matches(
        42U, 42U, "techland_game_class"));
    CHECK(!dluma_player_startup_core_window_matches(
        42U, 24U, "techland_game_class"));
    CHECK(!dluma_player_startup_core_window_matches(
        42U, 42U, "another_window_class"));
    CHECK(dluma_player_startup_core_reveal_mode(0, 42U, 42U) ==
          DLUMA_PLAYER_STARTUP_REVEAL_FOREGROUND);
    CHECK(dluma_player_startup_core_reveal_mode(0, 42U, 24U) ==
          DLUMA_PLAYER_STARTUP_REVEAL_BACKGROUND);
    CHECK(dluma_player_startup_core_reveal_mode(1, 42U, 42U) ==
          DLUMA_PLAYER_STARTUP_REVEAL_INITIALIZATION_FAILED);
    return 1;
}

static int test_failure_and_fail_open_snapshot(void)
{
    DlumaPlayerStartupSnapshot snapshot;
    dluma_player_startup_core_initialize(&snapshot);
    dluma_player_startup_core_finish(&snapshot, -7, 42U, 42U);
    CHECK(snapshot.initialization_result_valid);
    CHECK(snapshot.initialization_result == -7);
    CHECK(snapshot.reveal_mode ==
          DLUMA_PLAYER_STARTUP_REVEAL_INITIALIZATION_FAILED);
    dluma_player_startup_core_fail_open(
        &snapshot, "exact_import_validation_failed");
    CHECK(snapshot.reveal_mode == DLUMA_PLAYER_STARTUP_REVEAL_FAIL_OPEN);
    CHECK(strcmp(
        snapshot.fail_open_reason,
        "exact_import_validation_failed") == 0);
    CHECK(strcmp(
        dluma_player_startup_core_reveal_mode_name(snapshot.reveal_mode),
        "fail_open_stock") == 0);
    return 1;
}

static int test_fixed_milestone_order(void)
{
    static const char *expected[DLUMA_PLAYER_STARTUP_MILESTONE_COUNT] = {
        "Initializing Mod Workbench...",
        "Discovering mods...",
        "Preparing content...",
        "Initializing game scripts...",
        "Starting mod hosts...",
        "Initializing game..."
    };
    unsigned int index;
    for (index = 0; index < DLUMA_PLAYER_STARTUP_MILESTONE_COUNT; ++index)
        CHECK(strcmp(
            dluma_player_startup_core_milestone(
                (DlumaPlayerStartupMilestone)index),
            expected[index]) == 0);
    CHECK(dluma_player_startup_core_milestone(
        DLUMA_PLAYER_STARTUP_MILESTONE_COUNT)[0] == 0);
    return 1;
}

int main(void)
{
    if (!test_exact_profile_refusal() ||
        !test_partial_install_rolls_back() ||
        !test_foreign_hook_ownership_is_preserved() ||
        !test_reporter_selection_and_bounded_ttl_copy() ||
        !test_splash_repaint_and_cleanup_are_idempotent() ||
        !test_hwnd_filtering_and_reveal_policy() ||
        !test_failure_and_fail_open_snapshot() ||
        !test_fixed_milestone_order())
        return 1;
    puts("player startup fake harness: PASS");
    return 0;
}
