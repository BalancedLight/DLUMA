#ifndef DLUMA_DUAL_HOST_H
#define DLUMA_DUAL_HOST_H

#include <stdint.h>

#define DLUMA_HANDOFF_MAX_RECORDS 32U
#define DLUMA_HANDOFF_MAX_SIDECARS 256U
#define DLUMA_HANDOFF_EXPIRY_MS 120000ULL

typedef enum DlumaHostKind {
    DLUMA_HOST_UNKNOWN = 0,
    DLUMA_HOST_CHROME_EDITOR = 1,
    DLUMA_HOST_PLAYER_STANDALONE = 2,
    DLUMA_HOST_PLAYER_EDITOR_PLAYTEST = 3
} DlumaHostKind;

typedef struct DlumaPeIdentity {
    uint32_t timestamp;
    uint32_t size_of_image;
    char sha256[65];
} DlumaPeIdentity;

typedef struct DlumaPlaytestHandoff {
    uint32_t struct_size;
    uint32_t schema_version;
    uint32_t parent_pid;
    uint32_t reserved;
    uint64_t parent_start_time;
    uint64_t created_ms;
    uint64_t expires_ms;
    uint64_t generation;
    char command_line_sha256[65];
    char loader_sha256[65];
    char project_root_utf8[1024];
    char project_name[128];
    char map_name[128];
    uint32_t approval_count;
    uint32_t sidecar_count;
    char inventory_sha256[65];
} DlumaPlaytestHandoff;

typedef struct DlumaHostEvidence {
    uint32_t struct_size;
    uint32_t canonical_image_verified;
    char canonical_image_utf8[1024];
    DlumaPeIdentity image;
    uint32_t has_editor_debugconf;
    uint32_t selected_loader;
    uint32_t parent_alive;
    uint32_t parent_editor_profile_verified;
    uint32_t parent_pid;
    uint64_t parent_start_time;
    uint64_t now_ms;
    char command_line_sha256[65];
    char loader_sha256[65];
    const DlumaPlaytestHandoff* handoff;
} DlumaHostEvidence;

typedef struct DlumaHostClassification {
    DlumaHostKind kind;
    uint32_t exact_profile;
    uint32_t forwarding_only;
    uint32_t handoff_valid;
    char reason[128];
} DlumaHostClassification;

int dluma_dual_host_classify(
    const DlumaHostEvidence* evidence,
    DlumaHostClassification* output);

int dluma_dual_host_is_editor_identity(
    const char* canonical_image_utf8,
    const DlumaPeIdentity* image);

int dluma_playtest_handoff_validate(
    const DlumaPlaytestHandoff* handoff,
    const DlumaHostEvidence* child_evidence);

typedef struct DlumaPlaytestHandoffStore {
    uint64_t revision;
    uint64_t next_slot;
    DlumaPlaytestHandoff records[DLUMA_HANDOFF_MAX_RECORDS];
} DlumaPlaytestHandoffStore;

void dluma_playtest_handoff_store_initialize(DlumaPlaytestHandoffStore* store);
int dluma_playtest_handoff_publish(
    DlumaPlaytestHandoffStore* store,
    const DlumaPlaytestHandoff* record);
int dluma_playtest_handoff_find(
    const DlumaPlaytestHandoffStore* store,
    const DlumaHostEvidence* child_evidence,
    DlumaPlaytestHandoff* output);

typedef enum DlumaHostLifecycleState {
    DLUMA_HOST_LIFECYCLE_NEW = 0,
    DLUMA_HOST_LIFECYCLE_IDLE = 1,
    DLUMA_HOST_LIFECYCLE_INITIALIZING = 2,
    DLUMA_HOST_LIFECYCLE_SESSION_ACTIVE = 3,
    DLUMA_HOST_LIFECYCLE_QUIESCING = 4,
    DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT = 5,
    DLUMA_HOST_LIFECYCLE_TERMINAL = 6
} DlumaHostLifecycleState;

typedef enum DlumaHostLifecycleAction {
    DLUMA_HOST_ACTION_REJECT = 0,
    DLUMA_HOST_ACTION_START_GENERATION = 1,
    DLUMA_HOST_ACTION_RETURN_ACTIVE_MANAGER = 2,
    DLUMA_HOST_ACTION_SHUTDOWN_GENERATION = 3,
    DLUMA_HOST_ACTION_ALREADY_STOPPED = 4
} DlumaHostLifecycleAction;

typedef struct DlumaDualHostLifecycle {
    DlumaHostKind host_kind;
    DlumaHostLifecycleState state;
    uint64_t generation;
    uint32_t initialize_exports;
    uint32_t shutdown_exports;
    uint32_t original_loads;
    uint32_t original_unloads;
    uint32_t original_initializes;
    uint32_t original_shutdowns;
    uint32_t module_pinned;
    uint32_t dependency_graph_retained;
} DlumaDualHostLifecycle;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_dual_host_lifecycle_initialize(
    DlumaDualHostLifecycle* lifecycle,
    DlumaHostKind host_kind);
DlumaHostLifecycleAction dluma_dual_host_lifecycle_begin_initialize(
    DlumaDualHostLifecycle* lifecycle);
int dluma_dual_host_lifecycle_original_loaded(
    DlumaDualHostLifecycle* lifecycle);
int dluma_dual_host_lifecycle_original_initialized(
    DlumaDualHostLifecycle* lifecycle,
    int succeeded);
DlumaHostLifecycleAction dluma_dual_host_lifecycle_begin_shutdown(
    DlumaDualHostLifecycle* lifecycle);
int dluma_dual_host_lifecycle_finish_shutdown(
    DlumaDualHostLifecycle* lifecycle,
    int callbacks_drained,
    int original_shutdown_called,
    int original_unloaded);

#ifdef __cplusplus
}
#endif

#endif
