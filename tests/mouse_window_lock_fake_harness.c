#include "../src/dlw_mouse_lock_core.h"

#include <stdio.h>
#include <string.h>

#define GAME_WINDOW ((void *)0x1010)
#define OTHER_WINDOW ((void *)0x2020)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            printf("FAIL line %d: %s\n", __LINE__, #condition);            \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef struct FakeMouseLockPlatform
{
    void *foreground_window;
    DlumaMouseLockRect client_rect;
    DlumaMouseLockRect current_clip;
    DlumaMouseLockRect last_applied;
    long screen_offset_x;
    long screen_offset_y;
    int iconic;
    int get_client_rect_ok;
    int client_to_screen_ok;
    int get_clip_cursor_ok;
    int clip_cursor_ok;
    unsigned int get_client_rect_calls;
    unsigned int client_to_screen_calls;
    unsigned int get_clip_cursor_calls;
    unsigned int apply_calls;
    unsigned int release_calls;
} FakeMouseLockPlatform;

static void *fake_get_foreground_window(void *context)
{
    return ((FakeMouseLockPlatform *)context)->foreground_window;
}

static int fake_is_iconic(void *context, void *window)
{
    FakeMouseLockPlatform *platform =
        (FakeMouseLockPlatform *)context;
    (void)window;
    return platform->iconic;
}

static int fake_get_client_rect(
    void *context, void *window, DlumaMouseLockRect *rect_out)
{
    FakeMouseLockPlatform *platform =
        (FakeMouseLockPlatform *)context;
    (void)window;
    ++platform->get_client_rect_calls;
    if (!platform->get_client_rect_ok)
        return 0;
    *rect_out = platform->client_rect;
    return 1;
}

static int fake_client_to_screen(
    void *context, void *window, DlumaMouseLockPoint *point)
{
    FakeMouseLockPlatform *platform =
        (FakeMouseLockPlatform *)context;
    (void)window;
    ++platform->client_to_screen_calls;
    if (!platform->client_to_screen_ok)
        return 0;
    point->x += platform->screen_offset_x;
    point->y += platform->screen_offset_y;
    return 1;
}

static int fake_get_clip_cursor(
    void *context, DlumaMouseLockRect *rect_out)
{
    FakeMouseLockPlatform *platform =
        (FakeMouseLockPlatform *)context;
    ++platform->get_clip_cursor_calls;
    if (!platform->get_clip_cursor_ok)
        return 0;
    *rect_out = platform->current_clip;
    return 1;
}

static int fake_clip_cursor(
    void *context, const DlumaMouseLockRect *rect)
{
    FakeMouseLockPlatform *platform =
        (FakeMouseLockPlatform *)context;
    if (!platform->clip_cursor_ok)
        return 0;
    if (!rect)
    {
        ++platform->release_calls;
        platform->current_clip.left = -4000;
        platform->current_clip.top = -2000;
        platform->current_clip.right = 4000;
        platform->current_clip.bottom = 2000;
    }
    else
    {
        ++platform->apply_calls;
        platform->current_clip = *rect;
        platform->last_applied = *rect;
    }
    return 1;
}

static DlumaMouseLockPlatform fake_operations(
    FakeMouseLockPlatform *platform)
{
    DlumaMouseLockPlatform operations;
    operations.context = platform;
    operations.get_foreground_window =
        fake_get_foreground_window;
    operations.is_iconic = fake_is_iconic;
    operations.get_client_rect = fake_get_client_rect;
    operations.client_to_screen = fake_client_to_screen;
    operations.get_clip_cursor = fake_get_clip_cursor;
    operations.clip_cursor = fake_clip_cursor;
    return operations;
}

static void initialize_fixture(
    DlumaMouseLockCoreState *state,
    FakeMouseLockPlatform *platform,
    DlumaMouseLockPlatform *operations)
{
    memset(platform, 0, sizeof(*platform));
    platform->foreground_window = GAME_WINDOW;
    platform->client_rect.left = 0;
    platform->client_rect.top = 0;
    platform->client_rect.right = 1280;
    platform->client_rect.bottom = 720;
    platform->current_clip.left = -4000;
    platform->current_clip.top = -2000;
    platform->current_clip.right = 4000;
    platform->current_clip.bottom = 2000;
    platform->screen_offset_x = -1920;
    platform->screen_offset_y = 100;
    platform->get_client_rect_ok = 1;
    platform->client_to_screen_ok = 1;
    platform->get_clip_cursor_ok = 1;
    platform->clip_cursor_ok = 1;
    *operations = fake_operations(platform);
    dluma_mouse_lock_core_initialize(state, 1);
    dluma_mouse_lock_core_bind(state, GAME_WINDOW);
}

static int test_foreground_capture_and_redundant_suppression(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);

    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(state.owns_clip);
    CHECK(fake.apply_calls == 1U);
    CHECK(fake.last_applied.left == -1920);
    CHECK(fake.last_applied.top == 100);
    CHECK(fake.last_applied.right == -640);
    CHECK(fake.last_applied.bottom == 820);
    CHECK(fake.client_to_screen_calls == 2U);

    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_NONE);
    CHECK(fake.apply_calls == 1U);
    CHECK(fake.get_clip_cursor_calls == 2U);
    return 1;
}

static int test_matching_foreign_clip_is_explicitly_claimed(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    fake.current_clip.left = -1920;
    fake.current_clip.top = 100;
    fake.current_clip.right = -640;
    fake.current_clip.bottom = 820;

    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(fake.apply_calls == 1U);
    CHECK(state.owns_clip);
    return 1;
}

static int test_reasserts_after_release_and_geometry_change(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.current_clip.left = -4000;
    fake.current_clip.top = -2000;
    fake.current_clip.right = 4000;
    fake.current_clip.bottom = 2000;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(fake.apply_calls == 2U);
    CHECK(state.owns_clip);

    fake.screen_offset_x = 250;
    fake.screen_offset_y = -300;
    fake.client_rect.right = 1600;
    fake.client_rect.bottom = 900;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(fake.apply_calls == 3U);
    CHECK(fake.last_applied.left == 250);
    CHECK(fake.last_applied.top == -300);
    CHECK(fake.last_applied.right == 1850);
    CHECK(fake.last_applied.bottom == 600);
    return 1;
}

static int test_interactive_move_suspends_until_resume(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    CHECK(dluma_mouse_lock_core_suspend(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(state.suspended);
    CHECK(!state.owns_clip);
    CHECK(fake.release_calls == 1U);

    fake.screen_offset_x = 250;
    fake.screen_offset_y = -300;
    fake.client_rect.right = 1600;
    fake.client_rect.bottom = 900;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_NONE);
    CHECK(fake.apply_calls == 1U);

    dluma_mouse_lock_core_resume(&state);
    CHECK(!state.suspended);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(fake.apply_calls == 2U);
    CHECK(fake.last_applied.left == 250);
    CHECK(fake.last_applied.top == -300);
    CHECK(fake.last_applied.right == 1850);
    CHECK(fake.last_applied.bottom == 600);
    return 1;
}

static int test_focus_loss_and_minimize_release(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.foreground_window = OTHER_WINDOW;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(fake.release_calls == 1U);
    CHECK(!state.owns_clip);

    fake.foreground_window = GAME_WINDOW;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    fake.iconic = 1;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(fake.release_calls == 2U);
    CHECK(!state.owns_clip);
    return 1;
}

static int test_foreign_clip_is_not_released_asynchronously(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    DlumaMouseLockRect foreign_clip = {10, 20, 30, 40};
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.current_clip = foreign_clip;
    fake.foreground_window = OTHER_WINDOW;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_FOREIGN_CLIP_PRESERVED);
    CHECK(fake.release_calls == 0U);
    CHECK(!state.owns_clip);
    CHECK(dluma_mouse_lock_core_rect_equal(
        &fake.current_clip, &foreign_clip));
    return 1;
}

static int test_synchronous_release_and_quiesce(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    DlumaMouseLockRect foreign_clip = {10, 20, 30, 40};
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.current_clip = foreign_clip;
    CHECK(dluma_mouse_lock_core_release(
              &state, &operations, 1) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(fake.release_calls == 1U);
    CHECK(!state.owns_clip);

    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(dluma_mouse_lock_core_quiesce(
              &state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(state.quiesced);
    CHECK(!state.owns_clip);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_NONE);
    return 1;
}

static int test_geometry_and_platform_failures_fail_open(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.get_client_rect_ok = 0;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE);
    CHECK(fake.release_calls == 1U);
    CHECK(!state.owns_clip);

    fake.get_client_rect_ok = 1;
    fake.client_to_screen_ok = 0;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE);
    CHECK(fake.apply_calls == 1U);

    fake.client_to_screen_ok = 1;
    fake.clip_cursor_ok = 0;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLY_FAILED);
    CHECK(!state.owns_clip);

    operations.get_client_rect = 0;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE);
    return 1;
}

static int test_query_failure_reasserts_and_release_failure_retains(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.get_clip_cursor_ok = 0;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(fake.apply_calls == 2U);
    CHECK(state.owns_clip);

    fake.clip_cursor_ok = 0;
    CHECK(dluma_mouse_lock_core_release(
              &state, &operations, 1) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASE_FAILED);
    CHECK(state.owns_clip);
    return 1;
}

static int test_query_failure_preserves_clip_during_async_loss(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);

    fake.get_clip_cursor_ok = 0;
    fake.foreground_window = OTHER_WINDOW;
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_FOREIGN_CLIP_PRESERVED);
    CHECK(fake.release_calls == 0U);
    CHECK(!state.owns_clip);
    return 1;
}

static int test_disabled_unbind_and_reinitialize(void)
{
    DlumaMouseLockCoreState state;
    FakeMouseLockPlatform fake;
    DlumaMouseLockPlatform operations;
    initialize_fixture(&state, &fake, &operations);

    dluma_mouse_lock_core_initialize(&state, 0);
    dluma_mouse_lock_core_bind(&state, GAME_WINDOW);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_NONE);
    CHECK(fake.apply_calls == 0U);

    dluma_mouse_lock_core_initialize(&state, 1);
    dluma_mouse_lock_core_bind(&state, GAME_WINDOW);
    CHECK(dluma_mouse_lock_core_maintain(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_APPLIED);
    CHECK(dluma_mouse_lock_core_unbind(&state, &operations) ==
          DLUMA_MOUSE_LOCK_CORE_RELEASED);
    CHECK(!state.bound);
    CHECK(state.window == 0);
    return 1;
}

static int test_rect_contract(void)
{
    DlumaMouseLockRect valid = {-10, -20, 1, 1};
    DlumaMouseLockRect same = {-10, -20, 1, 1};
    DlumaMouseLockRect empty_width = {2, 0, 2, 10};
    DlumaMouseLockRect empty_height = {0, 7, 10, 7};
    CHECK(dluma_mouse_lock_core_rect_valid(&valid));
    CHECK(dluma_mouse_lock_core_rect_equal(&valid, &same));
    CHECK(!dluma_mouse_lock_core_rect_valid(&empty_width));
    CHECK(!dluma_mouse_lock_core_rect_valid(&empty_height));
    same.bottom = 2;
    CHECK(!dluma_mouse_lock_core_rect_equal(&valid, &same));
    return 1;
}

int main(void)
{
    if (!test_foreground_capture_and_redundant_suppression() ||
        !test_matching_foreign_clip_is_explicitly_claimed() ||
        !test_reasserts_after_release_and_geometry_change() ||
        !test_interactive_move_suspends_until_resume() ||
        !test_focus_loss_and_minimize_release() ||
        !test_foreign_clip_is_not_released_asynchronously() ||
        !test_synchronous_release_and_quiesce() ||
        !test_geometry_and_platform_failures_fail_open() ||
        !test_query_failure_reasserts_and_release_failure_retains() ||
        !test_query_failure_preserves_clip_during_async_loss() ||
        !test_disabled_unbind_and_reinitialize() ||
        !test_rect_contract())
        return 1;
    puts("mouse window lock fake harness: PASS");
    return 0;
}
