#include "dlw_mouse_lock_core.h"

static void dluma_mouse_lock_core_clear_rect(
    DlumaMouseLockRect *rect)
{
    if (!rect)
        return;
    rect->left = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
}

static void dluma_mouse_lock_core_copy_rect(
    DlumaMouseLockRect *destination,
    const DlumaMouseLockRect *source)
{
    if (!destination || !source)
        return;
    destination->left = source->left;
    destination->top = source->top;
    destination->right = source->right;
    destination->bottom = source->bottom;
}

static void dluma_mouse_lock_core_drop_ownership(
    DlumaMouseLockCoreState *state)
{
    if (!state)
        return;
    state->owns_clip = 0;
    dluma_mouse_lock_core_clear_rect(&state->last_owned_rect);
}

static int dluma_mouse_lock_core_platform_can_maintain(
    const DlumaMouseLockPlatform *platform)
{
    return platform &&
           platform->get_foreground_window &&
           platform->is_iconic &&
           platform->get_client_rect &&
           platform->client_to_screen &&
           platform->get_clip_cursor &&
           platform->clip_cursor;
}

void dluma_mouse_lock_core_initialize(
    DlumaMouseLockCoreState *state, int enabled)
{
    if (!state)
        return;
    state->window = 0;
    dluma_mouse_lock_core_clear_rect(&state->last_owned_rect);
    state->enabled = enabled ? 1U : 0U;
    state->bound = 0;
    state->owns_clip = 0;
    state->suspended = 0;
    state->quiesced = 0;
}

void dluma_mouse_lock_core_bind(
    DlumaMouseLockCoreState *state, void *window)
{
    if (!state)
        return;
    state->window = window;
    state->bound = window ? 1U : 0U;
    state->suspended = 0;
}

int dluma_mouse_lock_core_rect_equal(
    const DlumaMouseLockRect *left, const DlumaMouseLockRect *right)
{
    return left && right &&
           left->left == right->left &&
           left->top == right->top &&
           left->right == right->right &&
           left->bottom == right->bottom;
}

int dluma_mouse_lock_core_rect_valid(
    const DlumaMouseLockRect *rect)
{
    return rect &&
           rect->right > rect->left &&
           rect->bottom > rect->top;
}

DlumaMouseLockCoreResult dluma_mouse_lock_core_release(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform,
    int synchronous)
{
    DlumaMouseLockRect observed;
    if (!state || !platform || !platform->clip_cursor)
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;
    if (!state->owns_clip)
        return DLUMA_MOUSE_LOCK_CORE_NONE;

    if (!synchronous)
    {
        if (!platform->get_clip_cursor ||
            !platform->get_clip_cursor(
                platform->context, &observed) ||
            !dluma_mouse_lock_core_rect_equal(
                &observed, &state->last_owned_rect))
        {
            dluma_mouse_lock_core_drop_ownership(state);
            return DLUMA_MOUSE_LOCK_CORE_FOREIGN_CLIP_PRESERVED;
        }
    }

    if (!platform->clip_cursor(platform->context, 0))
        return DLUMA_MOUSE_LOCK_CORE_RELEASE_FAILED;
    dluma_mouse_lock_core_drop_ownership(state);
    return DLUMA_MOUSE_LOCK_CORE_RELEASED;
}

DlumaMouseLockCoreResult dluma_mouse_lock_core_suspend(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform)
{
    DlumaMouseLockCoreResult result;
    if (!state)
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;
    result = dluma_mouse_lock_core_release(state, platform, 1);
    state->suspended = 1;
    return result;
}

void dluma_mouse_lock_core_resume(DlumaMouseLockCoreState *state)
{
    if (!state)
        return;
    state->suspended = 0;
}

DlumaMouseLockCoreResult dluma_mouse_lock_core_maintain(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform)
{
    DlumaMouseLockRect client;
    DlumaMouseLockRect target;
    DlumaMouseLockRect observed;
    DlumaMouseLockPoint upper_left;
    DlumaMouseLockPoint lower_right;
    int observed_available;

    if (!state || !platform)
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;
    if (!state->enabled || state->quiesced || state->suspended ||
        !state->bound || !state->window)
        return DLUMA_MOUSE_LOCK_CORE_NONE;
    if (!dluma_mouse_lock_core_platform_can_maintain(platform))
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;

    if (platform->get_foreground_window(platform->context) !=
            state->window ||
        platform->is_iconic(platform->context, state->window))
        return dluma_mouse_lock_core_release(state, platform, 0);

    if (!platform->get_client_rect(
            platform->context, state->window, &client) ||
        !dluma_mouse_lock_core_rect_valid(&client))
    {
        (void)dluma_mouse_lock_core_release(state, platform, 0);
        return DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE;
    }

    upper_left.x = client.left;
    upper_left.y = client.top;
    lower_right.x = client.right;
    lower_right.y = client.bottom;
    if (!platform->client_to_screen(
            platform->context, state->window, &upper_left) ||
        !platform->client_to_screen(
            platform->context, state->window, &lower_right))
    {
        (void)dluma_mouse_lock_core_release(state, platform, 0);
        return DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE;
    }
    target.left = upper_left.x;
    target.top = upper_left.y;
    target.right = lower_right.x;
    target.bottom = lower_right.y;
    if (!dluma_mouse_lock_core_rect_valid(&target))
    {
        (void)dluma_mouse_lock_core_release(state, platform, 0);
        return DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE;
    }

    observed_available = platform->get_clip_cursor(
        platform->context, &observed);
    if (state->owns_clip && observed_available &&
        dluma_mouse_lock_core_rect_equal(
            &observed, &state->last_owned_rect) &&
        dluma_mouse_lock_core_rect_equal(
            &observed, &target))
        return DLUMA_MOUSE_LOCK_CORE_NONE;

    if (state->owns_clip && observed_available &&
        !dluma_mouse_lock_core_rect_equal(
            &observed, &state->last_owned_rect))
        dluma_mouse_lock_core_drop_ownership(state);

    if (!platform->clip_cursor(platform->context, &target))
        return DLUMA_MOUSE_LOCK_CORE_APPLY_FAILED;
    dluma_mouse_lock_core_copy_rect(
        &state->last_owned_rect, &target);
    state->owns_clip = 1;
    return DLUMA_MOUSE_LOCK_CORE_APPLIED;
}

DlumaMouseLockCoreResult dluma_mouse_lock_core_quiesce(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform)
{
    DlumaMouseLockCoreResult result;
    if (!state)
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;
    result = dluma_mouse_lock_core_release(state, platform, 1);
    state->quiesced = 1;
    return result;
}

DlumaMouseLockCoreResult dluma_mouse_lock_core_unbind(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform)
{
    DlumaMouseLockCoreResult result;
    if (!state)
        return DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE;
    result = dluma_mouse_lock_core_release(state, platform, 1);
    state->window = 0;
    state->bound = 0;
    return result;
}
