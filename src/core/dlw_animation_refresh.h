#ifndef DLUMA_ANIMATION_REFRESH_H
#define DLUMA_ANIMATION_REFRESH_H

#include <stdint.h>

#define DLUMA_ANIMATION_REFRESH_SCHEMA_VERSION 2U
#define DLUMA_ANIMATION_REFRESH_LEGACY_SCHEMA_VERSION 1U
#define DLUMA_ANIMATION_REFRESH_MAX_REQUEST_BYTES (16U * 1024U)
#define DLUMA_ANIMATION_REFRESH_MAX_MANIFEST_BYTES (256U * 1024U)
#define DLUMA_ANIMATION_REFRESH_MAX_ARTIFACTS 128U
#define DLUMA_ANIMATION_REFRESH_ID_CAPACITY 97U
#define DLUMA_ANIMATION_REFRESH_PATH_CAPACITY 768U
#define DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY 193U
#define DLUMA_ANIMATION_REFRESH_TIME_CAPACITY 32U
#define DLUMA_ANIMATION_REFRESH_SHA256_CAPACITY 65U

#define DLUMA_ANIMATION_REFRESH_REQUEST_FORMAT \
    "dl-reanimated-animation-refresh-request"
#define DLUMA_ANIMATION_REFRESH_MANIFEST_FORMAT \
    "dl-reanimated-animation-content-manifest"
#define DLUMA_ANIMATION_REFRESH_RESULT_FORMAT \
    "dluma-animation-refresh-result"

typedef enum DlumaAnimationRefreshHost {
    DLUMA_ANIMATION_REFRESH_HOST_UNSUPPORTED = 0,
    DLUMA_ANIMATION_REFRESH_HOST_EDITOR = 1,
    DLUMA_ANIMATION_REFRESH_HOST_PLAYER = 2
} DlumaAnimationRefreshHost;

typedef enum DlumaAnimationRefreshRoute {
    DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE = 1,
    DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK = 2,
    /* Source aliases retained while schema-1 requests remain readable. */
    DLUMA_ANIMATION_REFRESH_ROUTE_LOOSE_ONLY =
        DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE,
    DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK_FALLBACK =
        DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK
} DlumaAnimationRefreshRoute;

typedef enum DlumaAnimationRefreshArtifactRole {
    DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT = 1,
    DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT = 2,
    DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION = 3,
    DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK = 4,
    DLUMA_ANIMATION_REFRESH_ARTIFACT_FALLBACK_RPACK =
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK
} DlumaAnimationRefreshArtifactRole;

typedef enum DlumaAnimationRefreshStage {
    DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED = 0,
    DLUMA_ANIMATION_REFRESH_STAGE_PASSED = 1,
    DLUMA_ANIMATION_REFRESH_STAGE_FAILED = 2,
    DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED = 3
} DlumaAnimationRefreshStage;

typedef enum DlumaAnimationRefreshRefusal {
    DLUMA_ANIMATION_REFRESH_REFUSAL_NONE = 0,
    DLUMA_ANIMATION_REFRESH_REFUSAL_DISABLED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_UNSUPPORTED_HOST,
    DLUMA_ANIMATION_REFRESH_REFUSAL_EXACT_PROFILE_UNAVAILABLE,
    DLUMA_ANIMATION_REFRESH_REFUSAL_NO_ACTIVE_PROJECT,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_INVALID,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_EXPIRED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_HOST_MISMATCH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ROUTE_NOT_ALLOWED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_PATH_INVALID,
    DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_NOT_OWNED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_HASH_MISMATCH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID,
    DLUMA_ANIMATION_REFRESH_REFUSAL_DEPLOYMENT_MISMATCH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_INVALID,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_NOT_OWNED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_HASH_MISMATCH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_DUPLICATE_PATH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_DUPLICATE_IDENTITY,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ALIAS_MISSING,
    DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_MISSING,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ANIMATION_MISSING,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MISSING,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REPLAY,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_COALESCED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_VISIBILITY_CHECK_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_VISIBILITY_CHECK_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_SOURCE_NOT_VISIBLE,
    DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_NOT_VISIBLE,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ANIMATION_NOT_VISIBLE,
    DLUMA_ANIMATION_REFRESH_REFUSAL_RELOAD_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_RELOAD_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REBIND_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_REBIND_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MOUNT_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MOUNT_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_SELECTED_MODEL_MISMATCH,
    DLUMA_ANIMATION_REFRESH_REFUSAL_BANK_STILL_REFERENCED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_BANK_RESOLUTION_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_BIND_CREATION_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_CLIP_RESOLUTION_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_INSPECTOR_REBUILD_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_RAW_LOOSE_PLAYER_UNSUPPORTED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED,
    DLUMA_ANIMATION_REFRESH_REFUSAL_RESULT_WRITE_FAILED
} DlumaAnimationRefreshRefusal;

typedef struct DlumaAnimationRefreshRequest {
    uint32_t schema_version;
    char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char created_utc[DLUMA_ANIMATION_REFRESH_TIME_CAPACITY];
    char expires_utc[DLUMA_ANIMATION_REFRESH_TIME_CAPACITY];
    uint64_t created_unix_seconds;
    uint64_t expires_unix_seconds;
    char deployment_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char manifest_relative_path[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    char manifest_sha256[DLUMA_ANIMATION_REFRESH_SHA256_CAPACITY];
    DlumaAnimationRefreshRoute route;
    DlumaAnimationRefreshHost target_host;
} DlumaAnimationRefreshRequest;

typedef struct DlumaAnimationRefreshArtifact {
    DlumaAnimationRefreshArtifactRole role;
    char relative_path[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    char sha256[DLUMA_ANIMATION_REFRESH_SHA256_CAPACITY];
    char resource_identity[DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY];
} DlumaAnimationRefreshArtifact;

typedef struct DlumaAnimationRefreshManifest {
    uint32_t schema_version;
    char deployment_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char character_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char model_resource_name[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char animation_library_name[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char created_utc[DLUMA_ANIMATION_REFRESH_TIME_CAPACITY];
    uint64_t created_unix_seconds;
    uint32_t artifact_count;
    DlumaAnimationRefreshArtifact artifacts[
        DLUMA_ANIMATION_REFRESH_MAX_ARTIFACTS];
} DlumaAnimationRefreshManifest;

typedef struct DlumaAnimationRefreshAdmission {
    uint32_t enabled;
    uint32_t exact_profile;
    uint32_t active_project_owned;
    uint32_t manifest_owned;
    uint32_t manifest_hash_matches;
    uint32_t artifacts_owned;
    uint32_t artifact_hashes_match;
    uint32_t request_replayed;
    uint64_t now_unix_seconds;
    DlumaAnimationRefreshHost actual_host;
    DlumaAnimationRefreshRoute configured_route;
} DlumaAnimationRefreshAdmission;

struct DlumaAnimationRefreshResult;

typedef struct DlumaAnimationRefreshHostOperations {
    void* context;
    int filesystem_refresh_supported;
    int visibility_check_supported;
    int reload_supported;
    int rebind_supported;
    int selected_model_reload_supported;
    int fallback_mount_supported;
    int fallback_unmount_supported;
    int (*filesystem_refresh)(void* context);
    int (*check_visibility)(
        void* context,
        uint32_t* source_visible,
        uint32_t* script_visible,
        uint32_t* animations_visible);
    int (*reload_animations)(void* context);
    int (*rebind_animator)(void* context);
    int (*reload_selected_model)(
        void* context,
        DlumaAnimationRefreshRoute route,
        struct DlumaAnimationRefreshResult* result);
    int (*mount_fallback_rpack)(void* context, unsigned int artifact_index);
    int (*unmount_fallback_rpack)(
        void* context,
        unsigned int artifact_index);
} DlumaAnimationRefreshHostOperations;

typedef struct DlumaAnimationRefreshResult {
    DlumaAnimationRefreshRefusal refusal;
    DlumaAnimationRefreshRefusal primary_refusal;
    DlumaAnimationRefreshStage source_visible;
    DlumaAnimationRefreshStage script_visible;
    DlumaAnimationRefreshStage animation_files_visible;
    DlumaAnimationRefreshStage fallback_mount;
    DlumaAnimationRefreshStage fallback_unmount;
    DlumaAnimationRefreshStage filesystem_refresh;
    DlumaAnimationRefreshStage reload;
    DlumaAnimationRefreshStage rebind;
    DlumaAnimationRefreshStage selected_model_match;
    DlumaAnimationRefreshStage pack_registration;
    DlumaAnimationRefreshStage script_bank_resolution;
    DlumaAnimationRefreshStage bind_creation;
    DlumaAnimationRefreshStage clip_resolution;
    DlumaAnimationRefreshStage inspector_rebuild;
    DlumaAnimationRefreshStage rollback;
    DlumaAnimationRefreshStage ui_bridge_discovery;
    DlumaAnimationRefreshStage command_interception;
    DlumaAnimationRefreshStage active_document_resolution;
    DlumaAnimationRefreshStage active_project_resolution;
    char command_source[32];
    uint32_t animation_count;
    uint32_t animation_name_count;
    uint32_t fallback_artifact_index;
    uint32_t accepted;
} DlumaAnimationRefreshResult;

int dluma_animation_refresh_parse_request(
    const void* bytes,
    unsigned int size,
    DlumaAnimationRefreshRequest* output);
int dluma_animation_refresh_parse_manifest(
    const void* bytes,
    unsigned int size,
    DlumaAnimationRefreshManifest* output);
int dluma_animation_refresh_relative_path_is_canonical(
    const char* relative_path);
int dluma_animation_refresh_sha256_is_canonical(const char* sha256);
int dluma_animation_refresh_identifier_is_valid(const char* identifier);
int dluma_animation_refresh_manifest_path_matches_deployment(
    const char* relative_path,
    const char* deployment_id);
int dluma_animation_refresh_manifest_validate(
    const DlumaAnimationRefreshManifest* manifest,
    DlumaAnimationRefreshRoute route,
    DlumaAnimationRefreshRefusal* refusal);
int dluma_animation_refresh_execute(
    const DlumaAnimationRefreshRequest* request,
    const DlumaAnimationRefreshManifest* manifest,
    const DlumaAnimationRefreshAdmission* admission,
    const DlumaAnimationRefreshHostOperations* operations,
    DlumaAnimationRefreshResult* result);
const char* dluma_animation_refresh_host_name(DlumaAnimationRefreshHost host);
const char* dluma_animation_refresh_route_name(DlumaAnimationRefreshRoute route);
const char* dluma_animation_refresh_stage_name(DlumaAnimationRefreshStage stage);
const char* dluma_animation_refresh_refusal_name(
    DlumaAnimationRefreshRefusal refusal);

#endif
