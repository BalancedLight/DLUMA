#ifndef DLUMA_DISCORD_PRESENCE_CORE_H
#define DLUMA_DISCORD_PRESENCE_CORE_H

#include "dlw_discord_presence_api.h"
#include "core/dlw_dual_host.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DlumaDiscordMenuActivity {
    DLUMA_DISCORD_MENU_MAIN = 0,
    DLUMA_DISCORD_MENU_SELECTING_MAP = 1,
    DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS = 2,
    DLUMA_DISCORD_MENU_ADJUSTING_MODS = 3,
    DLUMA_DISCORD_MENU_READING_CREDITS = 4,
    DLUMA_DISCORD_MENU_VIEWING_STATISTICS = 5
} DlumaDiscordMenuActivity;

typedef enum DlumaDiscordLevelSource {
    DLUMA_DISCORD_LEVEL_SOURCE_NONE = 0,
    DLUMA_DISCORD_LEVEL_SOURCE_ACTIVE = 1,
    DLUMA_DISCORD_LEVEL_SOURCE_SESSION = 2
} DlumaDiscordLevelSource;

#define DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP 0x01u
#define DLUMA_DISCORD_MENU_VISIBLE_SETTINGS 0x02u
#define DLUMA_DISCORD_MENU_VISIBLE_MODS 0x04u
#define DLUMA_DISCORD_MENU_VISIBLE_CREDITS 0x08u
#define DLUMA_DISCORD_MENU_VISIBLE_STATISTICS 0x10u

typedef struct DlumaDiscordDebounceState {
    unsigned int committed_kind;
    unsigned int candidate_kind;
    unsigned char has_committed;
    unsigned char has_candidate;
    unsigned short reserved;
    unsigned long long candidate_since_ms;
    unsigned long long last_positive_ms;
} DlumaDiscordDebounceState;

typedef enum DlumaDiscordTimerKind {
    DLUMA_DISCORD_TIMER_NONE = 0,
    DLUMA_DISCORD_TIMER_MENU = 1,
    DLUMA_DISCORD_TIMER_GAMEPLAY = 2,
    DLUMA_DISCORD_TIMER_EDITOR = 3
} DlumaDiscordTimerKind;

typedef struct DlumaDiscordTimerState {
    unsigned long long start_timestamp_seconds;
    unsigned long long level_identity;
    unsigned int map_context;
    unsigned char kind;
    unsigned char valid;
    unsigned short reserved;
} DlumaDiscordTimerState;

typedef struct DlumaDiscordHostRoute {
    unsigned long long application_id;
    unsigned int uses_editor_setting;
} DlumaDiscordHostRoute;

int dluma_discord_select_host_route(
    DlumaHostKind host_kind,
    int forwarding_only,
    unsigned long long dying_light_application_id,
    unsigned long long developer_tools_application_id,
    DlumaDiscordHostRoute* output);

unsigned int dluma_discord_utf8_sanitize(
    const char* input, unsigned int input_size,
    char* output, unsigned int output_capacity,
    unsigned int max_codepoints);
unsigned int dluma_discord_apply_name_format(
    const char* format, unsigned int format_capacity,
    const char* value, unsigned int value_capacity,
    char* output, unsigned int output_capacity,
    unsigned int max_codepoints);
int dluma_discord_https_asset_url_valid(const char* value);
int dluma_discord_build_workshop_item_url(
    const char* workshop_id, unsigned int workshop_id_capacity,
    char* output, unsigned int output_capacity);
int dluma_discord_activity_equal(
    const DlumaDiscordActivityV1* left,
    const DlumaDiscordActivityV1* right);
DlumaDiscordMenuActivity dluma_discord_menu_from_visibility(
    unsigned int visibility_mask);
DlumaDiscordLevelSource dluma_discord_choose_level_source(
    int active_level_positive,
    int session_level_positive);
unsigned long long dluma_discord_timer_update(
    DlumaDiscordTimerState* state,
    DlumaDiscordTimerKind kind,
    unsigned int map_context,
    unsigned long long level_identity,
    unsigned long long now_seconds);
unsigned int dluma_discord_debounce_update(
    DlumaDiscordDebounceState* state,
    unsigned int observed_kind,
    int positive_observation,
    unsigned long long now_ms,
    unsigned int debounce_ms,
    unsigned int unmatched_grace_ms,
    int* changed_out);
unsigned long long dluma_discord_next_backoff(
    unsigned long long current_ms,
    unsigned long long initial_ms,
    unsigned long long maximum_ms);

#ifdef __cplusplus
}
#endif

#endif
