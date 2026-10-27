#ifndef DLUMA_STEAM_PRESENCE_CORE_H
#define DLUMA_STEAM_PRESENCE_CORE_H

#include "dlw_presence_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_STEAM_PLAYER_API_SHA256 \
    "0ee64af072052ab95438fb8e054a68e4d392de941ef867a0124992aafccc7938"
#define DLUMA_STEAM_PLAYER_API_TIMESTAMP 0x56995865UL
#define DLUMA_STEAM_PLAYER_API_IMAGE_SIZE 0x0003D000UL
#define DLUMA_STEAM_RETRY_INITIAL_MS 1000ULL
#define DLUMA_STEAM_RETRY_MAX_MS 30000ULL

typedef enum DlumaSteamPresenceHostProfile {
    DLUMA_STEAM_HOST_UNSUPPORTED = 0,
    DLUMA_STEAM_HOST_PLAYER = 1,
    DLUMA_STEAM_HOST_RETAIL_UNSUPPORTED = 2
} DlumaSteamPresenceHostProfile;

typedef enum DlumaSteamPresenceModuleAssessment {
    DLUMA_STEAM_MODULE_RETRY = 0,
    DLUMA_STEAM_MODULE_READY = 1,
    DLUMA_STEAM_MODULE_UNSUPPORTED = 2,
    DLUMA_STEAM_MODULE_REJECTED = 3
} DlumaSteamPresenceModuleAssessment;

typedef struct DlumaSteamPresenceModuleEvidence {
    unsigned int host_profile;
    unsigned int module_present;
    unsigned int canonical_colocation;
    unsigned int hash_match;
    unsigned int pe_identity_match;
    unsigned int accessor_export_present;
    unsigned int setter_export_present;
    unsigned int export_bytes_match;
    unsigned int export_pointer_ownership;
    unsigned int accessor_succeeded;
} DlumaSteamPresenceModuleEvidence;

DlumaSteamPresenceModuleAssessment dluma_steam_presence_assess_module(
    const DlumaSteamPresenceModuleEvidence* evidence);

typedef enum DlumaSteamPresenceActionKind {
    DLUMA_STEAM_ACTION_NONE = 0,
    DLUMA_STEAM_ACTION_SET_STATUS = 1,
    DLUMA_STEAM_ACTION_REMOVE_STATUS = 2
} DlumaSteamPresenceActionKind;

typedef struct DlumaSteamPresenceAction {
    unsigned int kind;
    char value[DLUMA_STEAM_STATUS_CAPACITY];
} DlumaSteamPresenceAction;

typedef struct DlumaSteamPresenceCore {
    unsigned int enabled;
    unsigned int observed_valid;
    unsigned int published_valid;
    unsigned int owns_status;
    unsigned long long next_retry_ms;
    unsigned long long retry_delay_ms;
    char observed_status[DLUMA_STEAM_STATUS_CAPACITY];
    char published_status[DLUMA_STEAM_STATUS_CAPACITY];
} DlumaSteamPresenceCore;

void dluma_steam_presence_core_initialize(
    DlumaSteamPresenceCore* state, int enabled);
void dluma_steam_presence_core_set_enabled(
    DlumaSteamPresenceCore* state, int enabled);
void dluma_steam_presence_core_observe(
    DlumaSteamPresenceCore* state,
    const DlumaPresenceSnapshot* snapshot);
DlumaSteamPresenceAction dluma_steam_presence_core_next_action(
    const DlumaSteamPresenceCore* state,
    unsigned long long now_ms);
void dluma_steam_presence_core_complete(
    DlumaSteamPresenceCore* state,
    const DlumaSteamPresenceAction* action,
    int succeeded,
    unsigned long long now_ms);

#ifdef __cplusplus
}
#endif

#endif
