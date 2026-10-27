#include "../src/core/dlw_editor_exit_intent.h"

#include <stdio.h>

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        return 0; \
    } \
} while (0)

static int test_cancelled_close(void)
{
    DlumaEditorExitIntentSnapshot state;
    unsigned long long generation;
    dluma_editor_exit_intent_initialize(&state, 1U);
    generation = dluma_editor_exit_intent_begin(
        &state, DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE, 100ULL);
    CHECK(generation == 1ULL);
    CHECK(dluma_editor_exit_intent_should_suppress(&state));
    dluma_editor_exit_intent_resolve(&state, generation, 0);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_NONE);
    CHECK(!dluma_editor_exit_intent_should_suppress(&state));
    return 1;
}

static int test_nested_alt_f4_accepted(void)
{
    DlumaEditorExitIntentSnapshot state;
    unsigned long long outer;
    unsigned long long nested;
    dluma_editor_exit_intent_initialize(&state, 1U);
    outer = dluma_editor_exit_intent_begin(
        &state, DLUMA_EDITOR_EXIT_REASON_ALT_F4, 200ULL);
    nested = dluma_editor_exit_intent_begin(
        &state, DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE, 201ULL);
    CHECK(outer == nested);
    CHECK(state.reason == DLUMA_EDITOR_EXIT_REASON_ALT_F4);
    dluma_editor_exit_intent_resolve(&state, nested, 1);
    dluma_editor_exit_intent_resolve(&state, outer, 0);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED);
    CHECK(dluma_editor_exit_intent_should_suppress(&state));
    return 1;
}

static int test_session_end_and_fail_open(void)
{
    DlumaEditorExitIntentSnapshot state;
    dluma_editor_exit_intent_initialize(&state, 0U);
    dluma_editor_exit_intent_commit(
        &state, DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END, 300ULL);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_NONE);
    CHECK(!dluma_editor_exit_intent_should_suppress(&state));
    state.observer_active = 1U;
    dluma_editor_exit_intent_commit(
        &state, DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END, 301ULL);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED);
    CHECK(state.reason == DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END);
    CHECK(dluma_editor_exit_intent_should_suppress(&state));
    return 1;
}

static int test_destroy_commits_only_existing_intent(void)
{
    DlumaEditorExitIntentSnapshot state;
    dluma_editor_exit_intent_initialize(&state, 1U);
    dluma_editor_exit_intent_commit_if_tentative(&state);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_NONE);
    (void)dluma_editor_exit_intent_begin(
        &state, DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE, 400ULL);
    dluma_editor_exit_intent_commit_if_tentative(&state);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED);
    return 1;
}

static int test_destroy_without_close_message(void)
{
    DlumaEditorExitIntentSnapshot state;
    dluma_editor_exit_intent_initialize(&state, 1U);
    /* File/Exit can destroy the main frame without sending WM_CLOSE. */
    dluma_editor_exit_intent_commit(
        &state, DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED, 500ULL);
    CHECK(state.state == DLUMA_EDITOR_EXIT_INTENT_COMMITTED);
    CHECK(dluma_editor_exit_intent_should_suppress(&state));
    dluma_editor_exit_intent_resolve(&state, state.generation, 0);
    CHECK(dluma_editor_exit_intent_should_suppress(&state));
    return 1;
}

int main(void)
{
    int passed = 0;
    passed += test_cancelled_close();
    passed += test_nested_alt_f4_accepted();
    passed += test_session_end_and_fail_open();
    passed += test_destroy_commits_only_existing_intent();
    passed += test_destroy_without_close_message();
    if (passed != 5) return 1;
    printf("Editor exit-intent fake cases passed: %d\n", passed);
    return 0;
}
