#include "../src/core/dlw_dual_host.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

static const char g_editor_sha[] =
    "cacd9c96ba5d8c35a23170dc8be8029db9aa5e6223f5e6cd5de3e61d8e72da28";
static const char g_player_sha[] =
    "61d9dfb20cfa179c6d097537da46ba9dbe3d58797938762ef4eacff5dfcc4b3a";
static const char g_loader_sha[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char g_command_sha[] =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char g_inventory_sha[] =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

static DlumaPlaytestHandoff make_handoff(void)
{
    DlumaPlaytestHandoff value;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.schema_version = 1U;
    value.parent_pid = 1234U;
    value.parent_start_time = 987654321ULL;
    value.created_ms = 1000ULL;
    value.expires_ms = 121000ULL;
    value.generation = 7ULL;
    strcpy_s(value.command_line_sha256, sizeof(value.command_line_sha256),
             g_command_sha);
    strcpy_s(value.loader_sha256, sizeof(value.loader_sha256), g_loader_sha);
    strcpy_s(value.inventory_sha256, sizeof(value.inventory_sha256),
             g_inventory_sha);
    strcpy_s(value.project_root_utf8, sizeof(value.project_root_utf8),
             "X:/DevTools/workshop/SampleProject");
    strcpy_s(value.project_name, sizeof(value.project_name), "SampleProject");
    strcpy_s(value.map_name, sizeof(value.map_name), "nowhere");
    value.approval_count = 2U;
    value.sidecar_count = 1U;
    return value;
}

static DlumaHostEvidence make_player_evidence(void)
{
    DlumaHostEvidence value;
    memset(&value, 0, sizeof(value));
    value.struct_size = sizeof(value);
    value.canonical_image_verified = 1U;
    strcpy_s(value.canonical_image_utf8, sizeof(value.canonical_image_utf8),
             "X:/FixtureLibrary/steamapps/common/Dying Light/DevTools/DyingLightPlayer.exe");
    value.image.timestamp = 0x623397B8U;
    value.image.size_of_image = 0x00103000U;
    strcpy_s(value.image.sha256, sizeof(value.image.sha256), g_player_sha);
    strcpy_s(value.command_line_sha256, sizeof(value.command_line_sha256),
             g_command_sha);
    strcpy_s(value.loader_sha256, sizeof(value.loader_sha256), g_loader_sha);
    value.now_ms = 2000ULL;
    return value;
}

static int test_classification(void)
{
    DlumaHostEvidence evidence;
    DlumaHostClassification result;
    DlumaPlaytestHandoff handoff = make_handoff();
    memset(&evidence, 0, sizeof(evidence));
    evidence.struct_size = sizeof(evidence);
    evidence.canonical_image_verified = 1U;
    strcpy_s(evidence.canonical_image_utf8, sizeof(evidence.canonical_image_utf8),
             "Y:/FixtureLibrary/steamapps/common/Dying Light Developer Tools/DyingLightEditor.exe");
    evidence.image.timestamp = 0x623396E1U;
    evidence.image.size_of_image = 0x01E4A000U;
    strcpy_s(evidence.image.sha256, sizeof(evidence.image.sha256), g_editor_sha);
    strcpy_s(evidence.command_line_sha256,
             sizeof(evidence.command_line_sha256), g_command_sha);
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_CHROME_EDITOR);
    CHECK(result.exact_profile && !result.forwarding_only);

    evidence = make_player_evidence();
    evidence.selected_loader = 1U;
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_PLAYER_STANDALONE);
    evidence.has_editor_debugconf = 1U;
    evidence.selected_loader = 1U;
    evidence.parent_alive = 1U;
    evidence.parent_editor_profile_verified = 1U;
    evidence.parent_pid = handoff.parent_pid;
    evidence.parent_start_time = handoff.parent_start_time;
    evidence.handoff = &handoff;
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST);
    CHECK(result.handoff_valid && !result.forwarding_only);
    evidence.parent_editor_profile_verified = 0U;
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_UNKNOWN && result.forwarding_only);
    evidence.parent_editor_profile_verified = 1U;
    handoff.expires_ms = 1999ULL;
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_UNKNOWN && result.forwarding_only);
    evidence.image.sha256[0] = '0';
    CHECK(dluma_dual_host_classify(&evidence, &result));
    CHECK(result.kind == DLUMA_HOST_UNKNOWN && !result.exact_profile);
    return 1;
}

static int test_handoff_store_and_validation(void)
{
    DlumaPlaytestHandoffStore store;
    DlumaPlaytestHandoff records[33];
    DlumaPlaytestHandoff found;
    DlumaHostEvidence child = make_player_evidence();
    unsigned int index;
    dluma_playtest_handoff_store_initialize(&store);
    child.has_editor_debugconf = 1U;
    child.selected_loader = 1U;
    child.parent_alive = 1U;
    child.parent_editor_profile_verified = 1U;
    child.parent_pid = 1234U;
    child.parent_start_time = 987654321ULL;
    for (index = 0; index < 33U; ++index) {
        records[index] = make_handoff();
        records[index].generation = index + 1U;
        CHECK(dluma_playtest_handoff_publish(&store, &records[index]));
    }
    CHECK(store.next_slot == 33U);
    CHECK(dluma_playtest_handoff_find(&store, &child, &found));
    CHECK(found.generation == 33U);
    child.parent_start_time++;
    CHECK(!dluma_playtest_handoff_find(&store, &child, &found));
    child.parent_start_time--;
    child.loader_sha256[0] = 'D';
    CHECK(!dluma_playtest_handoff_find(&store, &child, &found));
    return 1;
}

static int test_editor_generations(void)
{
    DlumaDualHostLifecycle lifecycle;
    unsigned int generation;
    dluma_dual_host_lifecycle_initialize(&lifecycle, DLUMA_HOST_CHROME_EDITOR);
    for (generation = 1U; generation <= 3U; ++generation) {
        CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
              DLUMA_HOST_ACTION_START_GENERATION);
        CHECK(lifecycle.generation == generation);
        CHECK(dluma_dual_host_lifecycle_original_loaded(&lifecycle));
        CHECK(dluma_dual_host_lifecycle_original_initialized(&lifecycle, 1));
        CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
              DLUMA_HOST_ACTION_RETURN_ACTIVE_MANAGER);
        CHECK(dluma_dual_host_lifecycle_begin_shutdown(&lifecycle) ==
              DLUMA_HOST_ACTION_SHUTDOWN_GENERATION);
        CHECK(dluma_dual_host_lifecycle_finish_shutdown(
            &lifecycle, 1, 1, 1));
        CHECK(lifecycle.state == DLUMA_HOST_LIFECYCLE_IDLE);
    }
    CHECK(lifecycle.original_loads == 3U);
    CHECK(lifecycle.original_unloads == 3U);
    CHECK(lifecycle.original_initializes == 3U);
    CHECK(lifecycle.original_shutdowns == 3U);
    return 1;
}

static int test_drain_failure_retains_graph(void)
{
    DlumaDualHostLifecycle lifecycle;
    dluma_dual_host_lifecycle_initialize(&lifecycle, DLUMA_HOST_CHROME_EDITOR);
    CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
          DLUMA_HOST_ACTION_START_GENERATION);
    CHECK(dluma_dual_host_lifecycle_original_loaded(&lifecycle));
    CHECK(dluma_dual_host_lifecycle_original_initialized(&lifecycle, 1));
    CHECK(dluma_dual_host_lifecycle_begin_shutdown(&lifecycle) ==
          DLUMA_HOST_ACTION_SHUTDOWN_GENERATION);
    CHECK(!dluma_dual_host_lifecycle_finish_shutdown(&lifecycle, 0, 1, 0));
    CHECK(lifecycle.state == DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT);
    CHECK(lifecycle.dependency_graph_retained && lifecycle.module_pinned);
    CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
          DLUMA_HOST_ACTION_REJECT);
    return 1;
}

static int test_shutdown_from_callback_retains_without_teardown(void)
{
    DlumaDualHostLifecycle lifecycle;
    dluma_dual_host_lifecycle_initialize(&lifecycle, DLUMA_HOST_CHROME_EDITOR);
    CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
          DLUMA_HOST_ACTION_START_GENERATION);
    CHECK(dluma_dual_host_lifecycle_original_loaded(&lifecycle));
    CHECK(dluma_dual_host_lifecycle_original_initialized(&lifecycle, 1));
    CHECK(dluma_dual_host_lifecycle_begin_shutdown(&lifecycle) ==
          DLUMA_HOST_ACTION_SHUTDOWN_GENERATION);
    CHECK(dluma_dual_host_lifecycle_begin_shutdown(&lifecycle) ==
          DLUMA_HOST_ACTION_REJECT);
    CHECK(!dluma_dual_host_lifecycle_finish_shutdown(
        &lifecycle, 0, 0, 0));
    CHECK(lifecycle.state == DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT);
    CHECK(lifecycle.original_shutdowns == 0U);
    CHECK(lifecycle.original_unloads == 0U);
    CHECK(lifecycle.dependency_graph_retained && lifecycle.module_pinned);
    return 1;
}

static int test_player_is_one_shot(void)
{
    DlumaDualHostLifecycle lifecycle;
    dluma_dual_host_lifecycle_initialize(
        &lifecycle, DLUMA_HOST_PLAYER_EDITOR_PLAYTEST);
    CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
          DLUMA_HOST_ACTION_START_GENERATION);
    CHECK(dluma_dual_host_lifecycle_original_loaded(&lifecycle));
    CHECK(dluma_dual_host_lifecycle_original_initialized(&lifecycle, 1));
    CHECK(dluma_dual_host_lifecycle_begin_shutdown(&lifecycle) ==
          DLUMA_HOST_ACTION_SHUTDOWN_GENERATION);
    CHECK(dluma_dual_host_lifecycle_finish_shutdown(&lifecycle, 1, 1, 0));
    CHECK(lifecycle.state == DLUMA_HOST_LIFECYCLE_TERMINAL);
    CHECK(dluma_dual_host_lifecycle_begin_initialize(&lifecycle) ==
          DLUMA_HOST_ACTION_REJECT);
    return 1;
}

int main(void)
{
    int passed = 0;
    if (!test_classification()) return 1;
    puts("[PASS] exact dual-host classification");
    ++passed;
    if (!test_handoff_store_and_validation()) return 1;
    puts("[PASS] bounded validated playtest handoffs");
    ++passed;
    if (!test_editor_generations()) return 1;
    puts("[PASS] three clean Editor generations");
    ++passed;
    if (!test_drain_failure_retains_graph()) return 1;
    puts("[PASS] failed drain retains complete graph");
    ++passed;
    if (!test_shutdown_from_callback_retains_without_teardown()) return 1;
    puts("[PASS] shutdown from callback retains without nested teardown");
    ++passed;
    if (!test_player_is_one_shot()) return 1;
    puts("[PASS] Player remains one-shot");
    ++passed;
    printf("Dual-host fake cases passed: %d\n", passed);
    return 0;
}
