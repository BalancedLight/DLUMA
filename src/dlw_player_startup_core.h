#ifndef DLUMA_PLAYER_STARTUP_CORE_H
#define DLUMA_PLAYER_STARTUP_CORE_H

#include <stddef.h>
#include <stdint.h>

#define DLUMA_PLAYER_STARTUP_STATUS_CAPACITY 128U
#define DLUMA_PLAYER_STARTUP_REASON_CAPACITY 96U
#define DLUMA_PLAYER_STARTUP_TTL_COPY_LIMIT \
    (DLUMA_PLAYER_STARTUP_STATUS_CAPACITY - 1U)

typedef enum DlumaPlayerStartupRevealMode {
    DLUMA_PLAYER_STARTUP_REVEAL_NONE = 0,
    DLUMA_PLAYER_STARTUP_REVEAL_FOREGROUND = 1,
    DLUMA_PLAYER_STARTUP_REVEAL_BACKGROUND = 2,
    DLUMA_PLAYER_STARTUP_REVEAL_INITIALIZATION_FAILED = 3,
    DLUMA_PLAYER_STARTUP_REVEAL_FAIL_OPEN = 4
} DlumaPlayerStartupRevealMode;

typedef enum DlumaPlayerStartupMilestone {
    DLUMA_PLAYER_STARTUP_MILESTONE_WORKBENCH = 0,
    DLUMA_PLAYER_STARTUP_MILESTONE_DISCOVERY = 1,
    DLUMA_PLAYER_STARTUP_MILESTONE_CONTENT = 2,
    DLUMA_PLAYER_STARTUP_MILESTONE_SCRIPTS = 3,
    DLUMA_PLAYER_STARTUP_MILESTONE_HOSTS = 4,
    DLUMA_PLAYER_STARTUP_MILESTONE_GAME = 5,
    DLUMA_PLAYER_STARTUP_MILESTONE_COUNT = 6
} DlumaPlayerStartupMilestone;

typedef struct DlumaPlayerStartupSnapshot {
    unsigned int profile_matched;
    unsigned int splash_bound;
    unsigned int splash_restored;
    unsigned int splash_repaint_count;
    unsigned int player_hooks_installed;
    unsigned int player_hooks_owned;
    unsigned int player_hooks_restored;
    unsigned int engine_hooks_installed;
    unsigned int engine_hooks_owned;
    unsigned int engine_hooks_restored;
    unsigned int ownership_conflict;
    unsigned int status_update_count;
    unsigned int native_status_count;
    unsigned int deferred_hide_count;
    unsigned int deferred_show_count;
    unsigned int deferred_focus_count;
    unsigned int initialization_result_valid;
    int initialization_result;
    DlumaPlayerStartupRevealMode reveal_mode;
    unsigned int active_call_count;
    char current_status[DLUMA_PLAYER_STARTUP_STATUS_CAPACITY];
    char fail_open_reason[DLUMA_PLAYER_STARTUP_REASON_CAPACITY];
} DlumaPlayerStartupSnapshot;

typedef struct DlumaPlayerStartupHook {
    void **slot;
    void *original;
    void *replacement;
    unsigned int installed;
    unsigned int restored;
    unsigned int ownership_conflict;
} DlumaPlayerStartupHook;

typedef int (*DlumaPlayerStartupCompareExchangeFn)(
    void *context,
    void **slot,
    void *expected,
    void *replacement,
    void **observed_out);

void dluma_player_startup_core_initialize(
    DlumaPlayerStartupSnapshot *snapshot);
int dluma_player_startup_core_profile_matches(
    int player_host,
    int exact_profile,
    uint32_t timestamp,
    uint32_t image_size,
    const char *sha256);
size_t dluma_player_startup_core_copy_bounded(
    char *destination,
    size_t destination_capacity,
    const char *source,
    size_t source_limit);
void *dluma_player_startup_core_select_reporter(
    void *caller_reporter,
    void *borrowed_reporter);
int dluma_player_startup_core_window_matches(
    uint32_t process_id,
    uint32_t window_process_id,
    const char *class_name);
DlumaPlayerStartupRevealMode dluma_player_startup_core_reveal_mode(
    int initialization_result,
    uint32_t process_id,
    uint32_t foreground_process_id);
int dluma_player_startup_core_install_hooks(
    DlumaPlayerStartupHook *hooks,
    size_t hook_count,
    DlumaPlayerStartupCompareExchangeFn compare_exchange,
    void *context);
int dluma_player_startup_core_restore_hooks(
    DlumaPlayerStartupHook *hooks,
    size_t hook_count,
    DlumaPlayerStartupCompareExchangeFn compare_exchange,
    void *context);
void dluma_player_startup_core_bind_splash(
    DlumaPlayerStartupSnapshot *snapshot);
void dluma_player_startup_core_restore_splash(
    DlumaPlayerStartupSnapshot *snapshot);
void dluma_player_startup_core_status(
    DlumaPlayerStartupSnapshot *snapshot,
    const char *status,
    int native_status,
    int repainted);
void dluma_player_startup_core_finish(
    DlumaPlayerStartupSnapshot *snapshot,
    int initialization_result,
    uint32_t process_id,
    uint32_t foreground_process_id);
void dluma_player_startup_core_fail_open(
    DlumaPlayerStartupSnapshot *snapshot,
    const char *reason);
const char *dluma_player_startup_core_reveal_mode_name(
    DlumaPlayerStartupRevealMode mode);
const char *dluma_player_startup_core_milestone(
    DlumaPlayerStartupMilestone milestone);

#endif
