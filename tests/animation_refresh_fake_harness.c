#include "../src/core/dlw_animation_refresh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); \
    return 0; } } while (0)

static const char request_json[] =
    "{\"format\":\"dl-reanimated-animation-refresh-request\","
    "\"schemaVersion\":2,\"requestId\":\"00112233445566778899aabbccddeeff\","
    "\"createdUtc\":\"2026-08-13T20:00:00Z\","
    "\"expiresUtc\":\"2026-08-13T20:10:00Z\","
    "\"deploymentId\":\"00112233445566778899aabb\","
    "\"manifestRelativePath\":\".dl-reanimated/animation-refresh/manifests/00112233445566778899aabb.json\","
    "\"manifestSha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
    "\"targetHost\":\"Editor\",\"route\":\"RawLoose\"}";

static const char manifest_json[] =
    "{\"format\":\"dl-reanimated-animation-content-manifest\","
    "\"schemaVersion\":2,\"deploymentId\":\"00112233445566778899aabb\","
    "\"characterId\":\"generic_character\","
    "\"modelResourceName\":\"generic_model\","
    "\"animationLibraryName\":\"generic_library\","
    "\"createdUtc\":\"2026-08-13T20:00:00Z\",\"artifacts\":["
    "{\"role\":\"AliasScript\",\"relativePath\":\"data/characters/generic_character/generic_model.ascr\","
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
    "\"resourceIdentity\":\"generic_model\"},"
    "{\"role\":\"AnimationScript\",\"relativePath\":\"data/characters/animations/animscripts/generic_library.scr\","
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\","
    "\"resourceIdentity\":\"generic_library\"},"
    "{\"role\":\"Animation\",\"relativePath\":\"data/characters/animations/generic_idle.anm2\","
    "\"sha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\","
    "\"resourceIdentity\":\"generic_idle\"},"
    "{\"role\":\"AnimationRuntimePack\","
    "\"relativePath\":\".dl-reanimated/animation-refresh/packages/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee.rpack\","
    "\"sha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\","
    "\"resourceIdentity\":\"generic_library\"}]}";

typedef struct FakeOps {
    unsigned int calls;
    unsigned int mounted;
    unsigned int unmounted;
} FakeOps;
static int op_refresh(void* context)
{
    FakeOps* ops = (FakeOps*)context;
    if (ops->calls != 0U) return 0;
    ops->calls = 1U;
    return 1;
}
static int op_refresh_fail(void* context)
{
    FakeOps* ops = (FakeOps*)context;
    if (ops->calls != 0U) return 0;
    ops->calls = 1U;
    return 0;
}
static int op_visibility(
    void* context,
    uint32_t* source_visible,
    uint32_t* script_visible,
    uint32_t* animations_visible)
{
    FakeOps* ops = (FakeOps*)context;
    if (ops->calls != 1U || !source_visible || !script_visible ||
        !animations_visible) return 0;
    *source_visible = 1U;
    *script_visible = 1U;
    *animations_visible = 1U;
    ops->calls = 2U;
    return 1;
}
static int op_reload(void* context)
{
    FakeOps* ops = (FakeOps*)context;
    if (ops->calls != 2U) return 0;
    ops->calls = 3U;
    return 1;
}
static int op_rebind(void* context)
{
    FakeOps* ops = (FakeOps*)context;
    if (ops->calls != 3U) return 0;
    ops->calls = 4U;
    return 1;
}
static int op_selected_model_reload(
    void* context,
    DlumaAnimationRefreshRoute route,
    DlumaAnimationRefreshResult* result)
{
    FakeOps* ops = (FakeOps*)context;
    if (!ops || ops->calls != 0U ||
        route != DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK || !result)
        return 0;
    ops->calls = 1U;
    result->selected_model_match = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->animation_name_count = 2U;
    return 1;
}
static int op_mount(void* context, unsigned int artifact_index)
{
    FakeOps* ops = (FakeOps*)context;
    if (artifact_index != 3U || ops->mounted) return 0;
    ops->mounted = 1U;
    return 1;
}
static int op_unmount(void* context, unsigned int artifact_index)
{
    FakeOps* ops = (FakeOps*)context;
    if (artifact_index != 3U || !ops->mounted || ops->unmounted) return 0;
    ops->unmounted = 1U;
    return 1;
}
static int op_unmount_fail(void* context, unsigned int artifact_index)
{
    (void)context;
    (void)artifact_index;
    return 0;
}

static int parse_valid(
    DlumaAnimationRefreshRequest* request,
    DlumaAnimationRefreshManifest* manifest)
{
    return dluma_animation_refresh_parse_request(
               request_json, (unsigned int)strlen(request_json), request) &&
           dluma_animation_refresh_parse_manifest(
               manifest_json, (unsigned int)strlen(manifest_json), manifest);
}

static int test_strict_parsers(void)
{
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    CHECK(manifest && parse_valid(&request, manifest));
    CHECK(request.target_host == DLUMA_ANIMATION_REFRESH_HOST_EDITOR);
    CHECK(request.schema_version == DLUMA_ANIMATION_REFRESH_SCHEMA_VERSION);
    CHECK(request.route == DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE);
    CHECK(!strcmp(
        dluma_animation_refresh_route_name(request.route), "RawLoose"));
    CHECK(!strcmp(dluma_animation_refresh_route_name(
        DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK), "ProjectRPack"));
    CHECK(manifest->artifacts[3].role ==
        DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK);
    CHECK(manifest->schema_version == DLUMA_ANIMATION_REFRESH_SCHEMA_VERSION);
    CHECK(manifest->artifact_count == 4U);
    CHECK(!strcmp(manifest->character_id, "generic_character"));
    CHECK(!strcmp(dluma_animation_refresh_refusal_name(
        DLUMA_ANIMATION_REFRESH_REFUSAL_VISIBILITY_CHECK_UNSUPPORTED),
        "visibility_check_unsupported"));
    CHECK(!strcmp(dluma_animation_refresh_refusal_name(
        DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_COALESCED),
        "request_coalesced"));
    CHECK(!strcmp(dluma_animation_refresh_refusal_name(
        DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_FAILED),
        "fallback_unmount_failed"));
    free(manifest);
    return 1;
}

static int test_paths_and_hashes_fail_closed(void)
{
    CHECK(dluma_animation_refresh_relative_path_is_canonical(
        "data/characters/animations/idle.anm2"));
    CHECK(!dluma_animation_refresh_relative_path_is_canonical(
        "../idle.anm2"));
    CHECK(!dluma_animation_refresh_relative_path_is_canonical(
        "data\\idle.anm2"));
    CHECK(!dluma_animation_refresh_relative_path_is_canonical(
        "C:/idle.anm2"));
    CHECK(!dluma_animation_refresh_relative_path_is_canonical(
        "data/con/file.anm2"));
    CHECK(dluma_animation_refresh_sha256_is_canonical(
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"));
    CHECK(!dluma_animation_refresh_sha256_is_canonical(
        "ABCDEFabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"));
    return 1;
}

static int test_unknown_duplicate_and_stale_rejected(void)
{
    char unknown[sizeof(request_json) + 32U];
    char duplicate[sizeof(request_json) + 64U];
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    size_t length = strlen(request_json);
    memcpy(unknown, request_json, length - 1U);
    strcpy_s(unknown + length - 1U, sizeof(unknown) - length + 1U, ",\"unknown\":true}");
    CHECK(!dluma_animation_refresh_parse_request(
        unknown, (unsigned int)strlen(unknown), &request));
    memcpy(duplicate, request_json, length - 1U);
    strcpy_s(duplicate + length - 1U, sizeof(duplicate) - length + 1U,
        ",\"route\":\"RawLoose\"}");
    CHECK(!dluma_animation_refresh_parse_request(
        duplicate, (unsigned int)strlen(duplicate), &request));
    CHECK(manifest && parse_valid(&request, manifest));
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    admission.enabled = 1U;
    admission.exact_profile = 1U;
    admission.active_project_owned = 1U;
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches = 1U;
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    admission.configured_route = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    admission.now_unix_seconds = request.expires_unix_seconds + 1ULL;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_EXPIRED);
    admission.now_unix_seconds = request.created_unix_seconds + 1ULL;
    manifest->created_unix_seconds = request.created_unix_seconds + 1ULL;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID);
    free(manifest);
    return 1;
}

static int test_profile_route_hash_replay_refusals(void)
{
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    CHECK(manifest && parse_valid(&request, manifest));
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    admission.enabled = 1U;
    admission.active_project_owned = 1U;
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches = 1U;
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    admission.configured_route = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    admission.now_unix_seconds = request.created_unix_seconds + 1ULL;
    CHECK(!dluma_animation_refresh_execute(&request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_EXACT_PROFILE_UNAVAILABLE);
    admission.exact_profile = 1U;
    admission.manifest_hash_matches = 0U;
    CHECK(!dluma_animation_refresh_execute(&request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_HASH_MISMATCH);
    admission.manifest_hash_matches = 1U;
    admission.request_replayed = 1U;
    CHECK(!dluma_animation_refresh_execute(&request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_REPLAY);
    admission.request_replayed = 0U;
    admission.configured_route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    CHECK(!dluma_animation_refresh_execute(&request, manifest, &admission, &operations, &result));
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_ROUTE_NOT_ALLOWED);
    free(manifest);
    return 1;
}

static int test_order_and_unsupported_rebind(void)
{
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    FakeOps ops;
    CHECK(manifest && parse_valid(&request, manifest));
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    memset(&ops, 0, sizeof(ops));
    admission.enabled = 1U;
    admission.exact_profile = 1U;
    admission.active_project_owned = 1U;
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches = 1U;
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    admission.configured_route = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    admission.now_unix_seconds = request.created_unix_seconds + 1ULL;
    operations.context = &ops;
    operations.filesystem_refresh_supported = 1;
    operations.visibility_check_supported = 1;
    operations.reload_supported = 1;
    operations.filesystem_refresh = op_refresh;
    operations.check_visibility = op_visibility;
    operations.reload_animations = op_reload;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(ops.calls == 3U);
    CHECK(result.refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_REBIND_UNSUPPORTED);
    CHECK(result.filesystem_refresh == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.source_visible == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.script_visible == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.animation_files_visible == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.reload == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.rebind == DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED);
    operations.rebind_supported = 1;
    operations.rebind_animator = op_rebind;
    ops.calls = 0U;
    CHECK(dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(ops.calls == 4U && result.accepted);
    free(manifest);
    return 1;
}

static int test_project_pack_cleanup(void)
{
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    FakeOps ops;
    CHECK(manifest && parse_valid(&request, manifest));
    request.target_host = DLUMA_ANIMATION_REFRESH_HOST_PLAYER;
    request.route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    memset(&ops, 0, sizeof(ops));
    admission.enabled = 1U;
    admission.exact_profile = 1U;
    admission.active_project_owned = 1U;
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches = 1U;
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_PLAYER;
    admission.configured_route =
        DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    admission.now_unix_seconds = request.created_unix_seconds + 1ULL;
    operations.context = &ops;
    operations.fallback_mount_supported = 1;
    operations.fallback_unmount_supported = 1;
    operations.filesystem_refresh_supported = 1;
    operations.mount_fallback_rpack = op_mount;
    operations.unmount_fallback_rpack = op_unmount;
    operations.filesystem_refresh = op_refresh_fail;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);
    CHECK(ops.mounted == 1U && ops.unmounted == 1U);
    CHECK(result.fallback_unmount == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.primary_refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);

    memset(&ops, 0, sizeof(ops));
    operations.context = &ops;
    operations.unmount_fallback_rpack = op_unmount_fail;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_FAILED);
    CHECK(ops.mounted == 1U && ops.unmounted == 0U);
    CHECK(result.fallback_unmount == DLUMA_ANIMATION_REFRESH_STAGE_FAILED);
    CHECK(result.primary_refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);

    memset(&ops, 0, sizeof(ops));
    request.target_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    operations.context = &ops;
    operations.unmount_fallback_rpack = op_unmount;
    CHECK(!dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(result.refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);
    CHECK(ops.mounted == 1U && ops.unmounted == 1U);
    CHECK(result.fallback_unmount == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.primary_refusal ==
        DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);
    free(manifest);
    return 1;
}
static int test_duplicate_identity_and_fallback_policy(void)
{
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshRefusal refusal;
    CHECK(manifest && dluma_animation_refresh_parse_manifest(
        manifest_json, (unsigned int)strlen(manifest_json), manifest));
    strcpy_s(manifest->artifacts[2].resource_identity, sizeof(manifest->artifacts[2].resource_identity),
           manifest->artifacts[1].resource_identity);
    manifest->artifacts[2].role = manifest->artifacts[1].role;
    CHECK(!dluma_animation_refresh_manifest_validate(
        manifest, DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE, &refusal));
    CHECK(refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_DUPLICATE_IDENTITY);
    CHECK(dluma_animation_refresh_parse_manifest(
        manifest_json, (unsigned int)strlen(manifest_json), manifest));
    manifest->artifact_count = 3U;
    CHECK(!dluma_animation_refresh_manifest_validate(
        manifest, DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE, &refusal));
    CHECK(refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MISSING);
    CHECK(!dluma_animation_refresh_manifest_validate(
        manifest, DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK,
        &refusal));
    CHECK(refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MISSING);
    CHECK(dluma_animation_refresh_parse_manifest(
        manifest_json, (unsigned int)strlen(manifest_json), manifest));
    strcpy_s(
        manifest->artifacts[0].relative_path,
        sizeof(manifest->artifacts[0].relative_path),
        "data/characters/other_character/generic_model.ascr");
    CHECK(!dluma_animation_refresh_manifest_validate(
        manifest, DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE, &refusal));
    CHECK(refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_INVALID);
    CHECK(dluma_animation_refresh_parse_manifest(
        manifest_json, (unsigned int)strlen(manifest_json), manifest));
    strcpy_s(
        manifest->artifacts[3].relative_path,
        sizeof(manifest->artifacts[3].relative_path),
        ".dl-reanimated/animation-refresh/packages/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff.rpack");
    CHECK(!dluma_animation_refresh_manifest_validate(
        manifest, DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE, &refusal));
    CHECK(refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_INVALID);
    free(manifest);
    return 1;
}

static int test_selected_model_bridge_has_no_global_reload_fallthrough(void)
{
    DlumaAnimationRefreshRequest request;
    DlumaAnimationRefreshManifest* manifest =
        (DlumaAnimationRefreshManifest*)malloc(sizeof(*manifest));
    DlumaAnimationRefreshAdmission admission;
    DlumaAnimationRefreshHostOperations operations;
    DlumaAnimationRefreshResult result;
    FakeOps ops;
    CHECK(manifest && parse_valid(&request, manifest));
    request.route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    memset(&admission, 0, sizeof(admission));
    memset(&operations, 0, sizeof(operations));
    memset(&ops, 0, sizeof(ops));
    admission.enabled = 1U;
    admission.exact_profile = 1U;
    admission.active_project_owned = 1U;
    admission.manifest_owned = 1U;
    admission.manifest_hash_matches = 1U;
    admission.artifacts_owned = 1U;
    admission.artifact_hashes_match = 1U;
    admission.actual_host = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    admission.configured_route = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    admission.now_unix_seconds = request.created_unix_seconds + 1ULL;
    operations.context = &ops;
    operations.selected_model_reload_supported = 1;
    operations.reload_selected_model = op_selected_model_reload;
    CHECK(dluma_animation_refresh_execute(
        &request, manifest, &admission, &operations, &result));
    CHECK(ops.calls == 1U && result.accepted);
    CHECK(result.selected_model_match == DLUMA_ANIMATION_REFRESH_STAGE_PASSED);
    CHECK(result.fallback_mount == DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED);
    CHECK(result.filesystem_refresh == DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED);
    CHECK(result.reload == DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED);
    CHECK(result.rebind == DLUMA_ANIMATION_REFRESH_STAGE_NOT_ATTEMPTED);
    free(manifest);
    return 1;
}

int main(void)
{
    int passed = 0;
    if (!test_strict_parsers()) return 1; ++passed; puts("[PASS] strict request and manifest parsers");
    if (!test_paths_and_hashes_fail_closed()) return 1; ++passed; puts("[PASS] canonical paths and hashes");
    if (!test_unknown_duplicate_and_stale_rejected()) return 1; ++passed; puts("[PASS] unknown duplicate and stale rejected");
    if (!test_profile_route_hash_replay_refusals()) return 1; ++passed; puts("[PASS] profile route hash and replay refusals");
    if (!test_order_and_unsupported_rebind()) return 1; ++passed; puts("[PASS] ordered stages and unsupported rebind");
    if (!test_selected_model_bridge_has_no_global_reload_fallthrough()) return 1; ++passed; puts("[PASS] selected-model bridge has no global reload fallthrough");
    if (!test_project_pack_cleanup()) return 1; ++passed; puts("[PASS] project pack cleanup across supported hosts");
    if (!test_duplicate_identity_and_fallback_policy()) return 1; ++passed; puts("[PASS] duplicate identity and fallback policy");
    printf("Animation refresh fake cases passed: %d\n", passed);
    return 0;
}
