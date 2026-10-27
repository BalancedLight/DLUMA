#include "dlw_player_startup_core.h"

#define DLUMA_PLAYER_STARTUP_EXE_TIMESTAMP 0x623397B8UL
#define DLUMA_PLAYER_STARTUP_EXE_IMAGE_SIZE 0x00103000UL
#define DLUMA_PLAYER_STARTUP_EXE_SHA256 \
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"

static const char *g_dluma_player_startup_milestones[
    DLUMA_PLAYER_STARTUP_MILESTONE_COUNT] = {
    "Initializing DLUMA...",
    "Discovering mods...",
    "Preparing content...",
    "Initializing game scripts...",
    "Starting mod hosts...",
    "Initializing game..."
};

static int dluma_player_startup_core_fold_equal(
    const char *left,
    const char *right)
{
    unsigned char a;
    unsigned char b;
    if (!left || !right) return 0;
    while (*left && *right) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    return *left == *right;
}

void dluma_player_startup_core_initialize(
    DlumaPlayerStartupSnapshot *snapshot)
{
    size_t index;
    unsigned char *bytes;
    if (!snapshot) return;
    bytes = (unsigned char *)snapshot;
    for (index = 0; index < sizeof(*snapshot); ++index) bytes[index] = 0;
}

int dluma_player_startup_core_profile_matches(
    int player_host,
    int exact_profile,
    uint32_t timestamp,
    uint32_t image_size,
    const char *sha256)
{
    return player_host && exact_profile &&
           timestamp == DLUMA_PLAYER_STARTUP_EXE_TIMESTAMP &&
           image_size == DLUMA_PLAYER_STARTUP_EXE_IMAGE_SIZE &&
           dluma_player_startup_core_fold_equal(
               sha256, DLUMA_PLAYER_STARTUP_EXE_SHA256);
}

size_t dluma_player_startup_core_copy_bounded(
    char *destination,
    size_t destination_capacity,
    const char *source,
    size_t source_limit)
{
    size_t index = 0;
    size_t limit;
    if (!destination || !destination_capacity) return 0;
    destination[0] = 0;
    if (!source || !source_limit) return 0;
    limit = destination_capacity - 1U;
    if (source_limit < limit) limit = source_limit;
    while (index < limit && source[index]) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = 0;
    return index;
}

void *dluma_player_startup_core_select_reporter(
    void *caller_reporter,
    void *borrowed_reporter)
{
    return caller_reporter ? caller_reporter : borrowed_reporter;
}

int dluma_player_startup_core_window_matches(
    uint32_t process_id,
    uint32_t window_process_id,
    const char *class_name)
{
    return process_id != 0U && process_id == window_process_id &&
           class_name &&
           dluma_player_startup_core_fold_equal(
               class_name, "techland_game_class");
}

DlumaPlayerStartupRevealMode dluma_player_startup_core_reveal_mode(
    int initialization_result,
    uint32_t process_id,
    uint32_t foreground_process_id)
{
    if (initialization_result != 0)
        return DLUMA_PLAYER_STARTUP_REVEAL_INITIALIZATION_FAILED;
    if (process_id != 0U && process_id == foreground_process_id)
        return DLUMA_PLAYER_STARTUP_REVEAL_FOREGROUND;
    return DLUMA_PLAYER_STARTUP_REVEAL_BACKGROUND;
}

int dluma_player_startup_core_restore_hooks(
    DlumaPlayerStartupHook *hooks,
    size_t hook_count,
    DlumaPlayerStartupCompareExchangeFn compare_exchange,
    void *context)
{
    size_t index = hook_count;
    int result = 1;
    if ((!hooks && hook_count) || !compare_exchange) return 0;
    while (index) {
        DlumaPlayerStartupHook *hook = &hooks[--index];
        void *observed = 0;
        if (!hook->installed || hook->restored) continue;
        if (compare_exchange(
                context, hook->slot, hook->replacement,
                hook->original, &observed) || observed == hook->original) {
            hook->restored = 1U;
            continue;
        }
        hook->ownership_conflict = 1U;
        result = 0;
    }
    return result;
}

int dluma_player_startup_core_install_hooks(
    DlumaPlayerStartupHook *hooks,
    size_t hook_count,
    DlumaPlayerStartupCompareExchangeFn compare_exchange,
    void *context)
{
    size_t index;
    if ((!hooks && hook_count) || !compare_exchange) return 0;
    for (index = 0; index < hook_count; ++index) {
        DlumaPlayerStartupHook *hook = &hooks[index];
        void *observed = 0;
        hook->installed = 0U;
        hook->restored = 0U;
        hook->ownership_conflict = 0U;
        if (!hook->slot || !hook->original || !hook->replacement ||
            !compare_exchange(
                context, hook->slot, hook->original,
                hook->replacement, &observed)) {
            (void)dluma_player_startup_core_restore_hooks(
                hooks, index, compare_exchange, context);
            return 0;
        }
        hook->installed = 1U;
    }
    return 1;
}

void dluma_player_startup_core_bind_splash(
    DlumaPlayerStartupSnapshot *snapshot)
{
    if (!snapshot) return;
    snapshot->splash_bound = 1U;
    snapshot->splash_restored = 0U;
}

void dluma_player_startup_core_restore_splash(
    DlumaPlayerStartupSnapshot *snapshot)
{
    if (!snapshot || !snapshot->splash_bound) return;
    snapshot->splash_restored = 1U;
}

void dluma_player_startup_core_status(
    DlumaPlayerStartupSnapshot *snapshot,
    const char *status,
    int native_status,
    int repainted)
{
    if (!snapshot || !status || !status[0]) return;
    (void)dluma_player_startup_core_copy_bounded(
        snapshot->current_status,
        sizeof(snapshot->current_status),
        status,
        DLUMA_PLAYER_STARTUP_TTL_COPY_LIMIT);
    ++snapshot->status_update_count;
    if (native_status) ++snapshot->native_status_count;
    if (repainted) ++snapshot->splash_repaint_count;
}

void dluma_player_startup_core_finish(
    DlumaPlayerStartupSnapshot *snapshot,
    int initialization_result,
    uint32_t process_id,
    uint32_t foreground_process_id)
{
    if (!snapshot) return;
    snapshot->initialization_result_valid = 1U;
    snapshot->initialization_result = initialization_result;
    snapshot->reveal_mode = dluma_player_startup_core_reveal_mode(
        initialization_result, process_id, foreground_process_id);
}

void dluma_player_startup_core_fail_open(
    DlumaPlayerStartupSnapshot *snapshot,
    const char *reason)
{
    if (!snapshot) return;
    snapshot->reveal_mode = DLUMA_PLAYER_STARTUP_REVEAL_FAIL_OPEN;
    (void)dluma_player_startup_core_copy_bounded(
        snapshot->fail_open_reason,
        sizeof(snapshot->fail_open_reason),
        reason ? reason : "unspecified",
        DLUMA_PLAYER_STARTUP_REASON_CAPACITY - 1U);
}

const char *dluma_player_startup_core_reveal_mode_name(
    DlumaPlayerStartupRevealMode mode)
{
    switch (mode) {
        case DLUMA_PLAYER_STARTUP_REVEAL_FOREGROUND:
            return "foreground";
        case DLUMA_PLAYER_STARTUP_REVEAL_BACKGROUND:
            return "background_no_activate";
        case DLUMA_PLAYER_STARTUP_REVEAL_INITIALIZATION_FAILED:
            return "initialization_failed_hidden";
        case DLUMA_PLAYER_STARTUP_REVEAL_FAIL_OPEN:
            return "fail_open_stock";
        default:
            return "pending";
    }
}

const char *dluma_player_startup_core_milestone(
    DlumaPlayerStartupMilestone milestone)
{
    if ((unsigned int)milestone >= DLUMA_PLAYER_STARTUP_MILESTONE_COUNT)
        return "";
    return g_dluma_player_startup_milestones[milestone];
}
