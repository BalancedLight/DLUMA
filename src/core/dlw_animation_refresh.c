#include "dlw_animation_refresh.h"
#include "dlw_json_reader.h"
#include <string.h>

static int eq(const char* a, const char* b)
{
    unsigned int index = 0;
    if (!a || !b) return 0;
    while (a[index] && b[index]) {
        if (a[index] != b[index]) return 0;
        ++index;
    }
    return a[index] == b[index];
}
static char fold(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int reserved(const char* value, unsigned int length)
{
    char base[16];
    unsigned int i = 0;
    while (i < length && i + 1U < sizeof(base) && value[i] != '.') {
        base[i] = fold(value[i]);
        ++i;
    }
    base[i] = 0;
    if (eq(base, "con") || eq(base, "prn") || eq(base, "aux") ||
        eq(base, "nul") || eq(base, "clock$")) return 1;
    return i == 4U &&
        ((!memcmp(base, "com", 3U)) || (!memcmp(base, "lpt", 3U))) &&
        base[3] >= '1' && base[3] <= '9';
}

int dluma_animation_refresh_identifier_is_valid(const char* value)
{
    unsigned int n = 0;
    if (!value || !value[0]) return 0;
    while (value[n]) {
        unsigned char c = (unsigned char)value[n];
        if (n + 1U >= DLUMA_ANIMATION_REFRESH_ID_CAPACITY ||
            c < 0x21U || c > 0x7eU || c == '/' || c == '\\' ||
            c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') return 0;
        ++n;
    }
    return value[n - 1U] != '.' && value[n - 1U] != ' ' &&
        !reserved(value, n);
}

static int lower_hex_id(const char* value, unsigned int expected)
{
    unsigned int i;
    if (!value) return 0;
    for (i = 0; i < expected; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return 0;
    return value[expected] == 0;
}

int dluma_animation_refresh_sha256_is_canonical(const char* value)
{
    return lower_hex_id(value, 64U);
}

int dluma_animation_refresh_relative_path_is_canonical(const char* value)
{
    unsigned int i = 0;
    unsigned int begin = 0;
    if (!value || !value[0] || value[0] == '/' || value[0] == '\\')
        return 0;
    while (value[i]) {
        unsigned char c = (unsigned char)value[i];
        if (i + 1U >= DLUMA_ANIMATION_REFRESH_PATH_CAPACITY ||
            c < 0x20U || c > 0x7eU || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') return 0;
        if (c == '/') {
            unsigned int length = i - begin;
            if (!length ||
                (length == 1U && value[begin] == '.') ||
                (length == 2U && value[begin] == '.' &&
                 value[begin + 1U] == '.') ||
                value[i - 1U] == '.' || value[i - 1U] == ' ' ||
                reserved(value + begin, length)) return 0;
            begin = i + 1U;
        }
        ++i;
    }
    return begin != i && value[i - 1U] != '.' && value[i - 1U] != ' ' &&
        !reserved(value + begin, i - begin);
}

static int ends(const char* value, const char* suffix)
{
    size_t a;
    size_t b;
    if (!value || !suffix) return 0;
    a = strlen(value);
    b = strlen(suffix);
    return b <= a && eq(value + a - b, suffix);
}

static int starts(const char* value, const char* prefix)
{
    unsigned int index = 0;
    if (!value || !prefix) return 0;
    while (prefix[index]) {
        if (value[index] != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static int canonical_time(const char* v, uint64_t* unix_seconds)
{
    static const unsigned short prior[12] = {
        0U,31U,59U,90U,120U,151U,181U,212U,243U,273U,304U,334U};
    unsigned int y, m, d, h, minute, s, i, max_day;
    uint64_t year_before, days;
    int leap;
    if (!v || !unix_seconds || strlen(v) != 20U || v[4] != '-' ||
        v[7] != '-' || v[10] != 'T' || v[13] != ':' ||
        v[16] != ':' || v[19] != 'Z') return 0;
    for (i = 0; i < 20U; ++i)
        if (i != 4U && i != 7U && i != 10U && i != 13U &&
            i != 16U && i != 19U && (v[i] < '0' || v[i] > '9'))
            return 0;
#define TWO(at) (((unsigned int)v[(at)]-'0')*10U+((unsigned int)v[(at)+1U]-'0'))
    y = ((unsigned int)v[0]-'0')*1000U + ((unsigned int)v[1]-'0')*100U +
        ((unsigned int)v[2]-'0')*10U + ((unsigned int)v[3]-'0');
    m = TWO(5U); d = TWO(8U); h = TWO(11U); minute = TWO(14U); s = TWO(17U);
#undef TWO
    if (y < 1970U || y > 9999U || m < 1U || m > 12U ||
        h > 23U || minute > 59U || s > 59U) return 0;
    leap = (y % 4U == 0U && y % 100U != 0U) || y % 400U == 0U;
    max_day = m == 2U ? (leap ? 29U : 28U) :
        ((m == 4U || m == 6U || m == 9U || m == 11U) ? 30U : 31U);
    if (d < 1U || d > max_day) return 0;
    year_before = (uint64_t)y - 1ULL;
    days = year_before * 365ULL + year_before / 4ULL -
        year_before / 100ULL + year_before / 400ULL -
        (1969ULL * 365ULL + 1969ULL / 4ULL -
         1969ULL / 100ULL + 1969ULL / 400ULL);
    days += prior[m - 1U] + d - 1U;
    if (leap && m > 2U) ++days;
    *unix_seconds = days * 86400ULL + h * 3600ULL + minute * 60ULL + s;
    return 1;
}

static int key(DlumaJsonReader* r, char* out, unsigned int cap)
{
    return dluma_json_string(r, out, cap) && dluma_json_take(r, ':');
}

static int parse_host(const char* value, DlumaAnimationRefreshHost* out)
{
    if (eq(value, "Editor")) *out = DLUMA_ANIMATION_REFRESH_HOST_EDITOR;
    else if (eq(value, "Player")) *out = DLUMA_ANIMATION_REFRESH_HOST_PLAYER;
    else return 0;
    return 1;
}

static int parse_route(const char* value, DlumaAnimationRefreshRoute* out)
{
    if (eq(value, "RawLoose") || eq(value, "LooseOnly"))
        *out = DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    else if (eq(value, "ProjectRPack") ||
             eq(value, "ProjectRPackFallback"))
        *out = DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    else return 0;
    return 1;
}

static int parse_role(const char* value, DlumaAnimationRefreshArtifactRole* out)
{
    if (eq(value, "AliasScript"))
        *out = DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT;
    else if (eq(value, "AnimationScript"))
        *out = DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT;
    else if (eq(value, "Animation"))
        *out = DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION;
    else if (eq(value, "AnimationRuntimePack") ||
             eq(value, "FallbackRPack"))
        *out = DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK;
    else return 0;
    return 1;
}

int dluma_animation_refresh_manifest_path_matches_deployment(
    const char* path, const char* deployment)
{
    static const char prefix[] =
        ".dl-reanimated/animation-refresh/manifests/";
    size_t p = sizeof(prefix) - 1U;
    size_t d;
    if (!path || !deployment || !starts(path, prefix)) return 0;
    d = strlen(deployment);
    return d && !memcmp(path + p, deployment, d) &&
        eq(path + p + d, ".json");
}

int dluma_animation_refresh_parse_request(
    const void* bytes, unsigned int size, DlumaAnimationRefreshRequest* out)
{
    DlumaJsonReader r;
    char name[64];
    char value[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    unsigned int fields = 0U;
    int first = 1;
    int next;
    uint64_t schema;
    if (!bytes || !out || !size ||
        size > DLUMA_ANIMATION_REFRESH_MAX_REQUEST_BYTES) return 0;
    memset(out, 0, sizeof(*out));
    dluma_json_reader_initialize(&r, bytes, size);
    if (!dluma_json_object_begin(&r)) return 0;
    while ((next = dluma_json_object_next(&r, &first)) > 0) {
        if (!key(&r, name, sizeof(name))) return 0;
#define STR_FIELD(bit,target) do { if (fields & (bit)) return 0; fields |= (bit); \
    if (!dluma_json_string(&r,(target),sizeof(target))) return 0; } while (0)
        if (eq(name, "format")) {
            STR_FIELD(1U, value);
            if (!eq(value, DLUMA_ANIMATION_REFRESH_REQUEST_FORMAT)) return 0;
        } else if (eq(name, "schemaVersion")) {
            if (fields & 2U) return 0;
            fields |= 2U; schema = 0;
            if (!dluma_json_u64(&r, &schema) ||
                (schema != DLUMA_ANIMATION_REFRESH_SCHEMA_VERSION &&
                 schema != DLUMA_ANIMATION_REFRESH_LEGACY_SCHEMA_VERSION))
                return 0;
            out->schema_version = (uint32_t)schema;
        } else if (eq(name, "requestId")) {
            STR_FIELD(4U, out->request_id);
        } else if (eq(name, "createdUtc")) {
            STR_FIELD(8U, out->created_utc);
        } else if (eq(name, "expiresUtc")) {
            STR_FIELD(16U, out->expires_utc);
        } else if (eq(name, "deploymentId")) {
            STR_FIELD(32U, out->deployment_id);
        } else if (eq(name, "manifestRelativePath")) {
            STR_FIELD(64U, out->manifest_relative_path);
        } else if (eq(name, "manifestSha256")) {
            STR_FIELD(128U, out->manifest_sha256);
        } else if (eq(name, "targetHost")) {
            STR_FIELD(256U, value);
            if (!parse_host(value, &out->target_host)) return 0;
        } else if (eq(name, "route")) {
            STR_FIELD(512U, value);
            if (!parse_route(value, &out->route)) return 0;
        } else return 0;
#undef STR_FIELD
    }
    if (next < 0 || !dluma_json_object_end(&r) || !dluma_json_finished(&r) ||
        fields != 1023U || !lower_hex_id(out->request_id, 32U) ||
        !lower_hex_id(out->deployment_id, 24U) ||
        !dluma_animation_refresh_relative_path_is_canonical(
            out->manifest_relative_path) ||
        !dluma_animation_refresh_sha256_is_canonical(out->manifest_sha256) ||
        !canonical_time(out->created_utc, &out->created_unix_seconds) ||
        !canonical_time(out->expires_utc, &out->expires_unix_seconds) ||
        out->expires_unix_seconds <= out->created_unix_seconds ||
        out->expires_unix_seconds - out->created_unix_seconds > 3600ULL)
        return 0;
    return dluma_animation_refresh_manifest_path_matches_deployment(
        out->manifest_relative_path, out->deployment_id);
}

static int role_path_valid(const DlumaAnimationRefreshArtifact* a)
{
    if (!a) return 0;
    switch (a->role) {
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT:
        return starts(a->relative_path, "data/characters/") &&
            ends(a->relative_path, ".ascr");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT:
        return starts(a->relative_path,
            "data/characters/animations/animscripts/") &&
            ends(a->relative_path, ".scr");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION:
        return starts(a->relative_path, "data/characters/animations/") &&
            !starts(a->relative_path,
                "data/characters/animations/animscripts/") &&
            ends(a->relative_path, ".anm2");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_FALLBACK_RPACK:
        return starts(a->relative_path,
            ".dl-reanimated/animation-refresh/packages/") &&
            ends(a->relative_path, ".rpack");
    default:
        return 0;
    }
}

static int parse_artifact(DlumaJsonReader* r, DlumaAnimationRefreshArtifact* out)
{
    char name[64];
    char value[32];
    unsigned int fields = 0U;
    int first = 1;
    int next;
    memset(out, 0, sizeof(*out));
    if (!dluma_json_object_begin(r)) return 0;
    while ((next = dluma_json_object_next(r, &first)) > 0) {
        if (!key(r, name, sizeof(name))) return 0;
#define ART_STR(bit,target) do { if (fields & (bit)) return 0; fields |= (bit); \
    if (!dluma_json_string(r,(target),sizeof(target))) return 0; } while (0)
        if (eq(name, "role")) {
            ART_STR(1U, value);
            if (!parse_role(value, &out->role)) return 0;
        } else if (eq(name, "relativePath")) {
            ART_STR(2U, out->relative_path);
        } else if (eq(name, "sha256")) {
            ART_STR(4U, out->sha256);
        } else if (eq(name, "resourceIdentity")) {
            ART_STR(8U, out->resource_identity);
        } else return 0;
#undef ART_STR
    }
    return next == 0 && dluma_json_object_end(r) && fields == 15U &&
        dluma_animation_refresh_relative_path_is_canonical(out->relative_path) &&
        dluma_animation_refresh_sha256_is_canonical(out->sha256) &&
        dluma_animation_refresh_identifier_is_valid(out->resource_identity) &&
        role_path_valid(out);
}

int dluma_animation_refresh_parse_manifest(
    const void* bytes, unsigned int size, DlumaAnimationRefreshManifest* out)
{
    DlumaJsonReader r;
    char name[64];
    char value[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    unsigned int fields = 0U;
    int first = 1;
    int next;
    uint64_t schema;
    if (!bytes || !out || !size ||
        size > DLUMA_ANIMATION_REFRESH_MAX_MANIFEST_BYTES) return 0;
    memset(out, 0, sizeof(*out));
    dluma_json_reader_initialize(&r, bytes, size);
    if (!dluma_json_object_begin(&r)) return 0;
    while ((next = dluma_json_object_next(&r, &first)) > 0) {
        if (!key(&r, name, sizeof(name))) return 0;
#define MAN_STR(bit,target) do { if (fields & (bit)) return 0; fields |= (bit); \
    if (!dluma_json_string(&r,(target),sizeof(target))) return 0; } while (0)
        if (eq(name, "format")) {
            MAN_STR(1U, value);
            if (!eq(value, DLUMA_ANIMATION_REFRESH_MANIFEST_FORMAT)) return 0;
        } else if (eq(name, "schemaVersion")) {
            if (fields & 2U) return 0;
            fields |= 2U; schema = 0;
            if (!dluma_json_u64(&r, &schema) ||
                (schema != DLUMA_ANIMATION_REFRESH_SCHEMA_VERSION &&
                 schema != DLUMA_ANIMATION_REFRESH_LEGACY_SCHEMA_VERSION))
                return 0;
            out->schema_version = (uint32_t)schema;
        } else if (eq(name, "deploymentId")) {
            MAN_STR(4U, out->deployment_id);
        } else if (eq(name, "characterId")) {
            MAN_STR(8U, out->character_id);
        } else if (eq(name, "modelResourceName")) {
            MAN_STR(16U, out->model_resource_name);
        } else if (eq(name, "animationLibraryName")) {
            MAN_STR(32U, out->animation_library_name);
        } else if (eq(name, "createdUtc")) {
            MAN_STR(64U, out->created_utc);
        } else if (eq(name, "artifacts")) {
            int array_first = 1;
            int array_next;
            if (fields & 128U) return 0;
            fields |= 128U;
            if (!dluma_json_array_begin(&r)) return 0;
            while ((array_next = dluma_json_array_next(&r, &array_first)) > 0) {
                if (out->artifact_count >= DLUMA_ANIMATION_REFRESH_MAX_ARTIFACTS ||
                    !parse_artifact(&r, &out->artifacts[out->artifact_count]))
                    return 0;
                ++out->artifact_count;
            }
            if (array_next < 0 || !dluma_json_array_end(&r)) return 0;
        } else return 0;
#undef MAN_STR
    }
    return next == 0 && dluma_json_object_end(&r) &&
        dluma_json_finished(&r) && fields == 255U &&
        lower_hex_id(out->deployment_id, 24U) &&
        dluma_animation_refresh_identifier_is_valid(out->character_id) &&
        dluma_animation_refresh_identifier_is_valid(out->model_resource_name) &&
        dluma_animation_refresh_identifier_is_valid(
            out->animation_library_name) &&
        canonical_time(out->created_utc, &out->created_unix_seconds) &&
        out->artifact_count != 0U;
}

static int same_i(const char* a, const char* b)
{
    unsigned int i = 0;
    while (a[i] && b[i]) {
        if (fold(a[i]) != fold(b[i])) return 0;
        ++i;
    }
    return a[i] == b[i];
}

static int equals_five_parts(
    const char* value,
    const char* first,
    const char* second,
    const char* third,
    const char* fourth,
    const char* fifth)
{
    const char* parts[5];
    unsigned int i;
    size_t offset = 0U;
    size_t length;
    if (!value || !first || !second || !third || !fourth || !fifth)
        return 0;
    parts[0] = first;
    parts[1] = second;
    parts[2] = third;
    parts[3] = fourth;
    parts[4] = fifth;
    for (i = 0; i < 5U; ++i) {
        length = strlen(parts[i]);
        size_t part_index;
        for (part_index = 0U; part_index < length; ++part_index)
            if (value[offset + part_index] != parts[i][part_index]) return 0;
        offset += length;
    }
    return value[offset] == 0;
}

static int artifact_contract_valid(
    const DlumaAnimationRefreshManifest* manifest,
    const DlumaAnimationRefreshArtifact* artifact)
{
    if (!manifest || !artifact) return 0;
    switch (artifact->role) {
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT:
        return eq(artifact->resource_identity, manifest->model_resource_name) &&
            equals_five_parts(
                artifact->relative_path,
                "data/characters/", manifest->character_id, "/",
                manifest->model_resource_name, ".ascr");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT:
        return eq(
                artifact->resource_identity,
                manifest->animation_library_name) &&
            equals_five_parts(
                artifact->relative_path,
                "data/characters/animations/animscripts/",
                manifest->animation_library_name, ".scr", "", "");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION:
        return equals_five_parts(
            artifact->relative_path,
            "data/characters/animations/", artifact->resource_identity,
            ".anm2", "", "");
    case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK:
        return eq(
                artifact->resource_identity,
                manifest->animation_library_name) &&
            equals_five_parts(
                artifact->relative_path,
                ".dl-reanimated/animation-refresh/packages/",
                artifact->sha256, ".rpack", "", "");
    default:
        return 0;
    }
}

int dluma_animation_refresh_manifest_validate(
    const DlumaAnimationRefreshManifest* manifest,
    DlumaAnimationRefreshRoute route,
    DlumaAnimationRefreshRefusal* refusal)
{
    unsigned int alias = 0, script = 0, animation = 0, fallback = 0;
    unsigned int i, j;
    if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID;
    if (!manifest ||
        (route != DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE &&
         route != DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK) ||
        !manifest->artifact_count ||
        manifest->artifact_count > DLUMA_ANIMATION_REFRESH_MAX_ARTIFACTS)
        return 0;
    for (i = 0; i < manifest->artifact_count; ++i) {
        const DlumaAnimationRefreshArtifact* a = &manifest->artifacts[i];
        for (j = 0; j < i; ++j) {
            const DlumaAnimationRefreshArtifact* b = &manifest->artifacts[j];
            if (same_i(a->relative_path, b->relative_path)) {
                if (refusal) *refusal =
                    DLUMA_ANIMATION_REFRESH_REFUSAL_DUPLICATE_PATH;
                return 0;
            }
            if (a->role == b->role &&
                same_i(a->resource_identity, b->resource_identity)) {
                if (refusal) *refusal =
                    DLUMA_ANIMATION_REFRESH_REFUSAL_DUPLICATE_IDENTITY;
                return 0;
            }
        }
        if (!artifact_contract_valid(manifest, a)) {
            if (refusal) *refusal =
                DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_INVALID;
            return 0;
        }
        switch (a->role) {
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT: ++alias; break;
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT: ++script; break;
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION: ++animation; break;
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK:
            ++fallback;
            break;
        default:
            if (refusal) *refusal =
                DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_INVALID;
            return 0;
        }
    }
    if (alias != 1U) {
        if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_ALIAS_MISSING;
        return 0;
    }
    if (script != 1U) {
        if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_MISSING;
        return 0;
    }
    if (!animation) {
        if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_ANIMATION_MISSING;
        return 0;
    }
    if (fallback != 1U) {
        if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MISSING;
        return 0;
    }
    if (refusal) *refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
    return 1;
}

static void init_result(DlumaAnimationRefreshResult* result)
{
    memset(result, 0, sizeof(*result));
    result->fallback_artifact_index = UINT32_MAX;
}

int dluma_animation_refresh_execute(
    const DlumaAnimationRefreshRequest* request,
    const DlumaAnimationRefreshManifest* manifest,
    const DlumaAnimationRefreshAdmission* admission,
    const DlumaAnimationRefreshHostOperations* operations,
    DlumaAnimationRefreshResult* result)
{
    DlumaAnimationRefreshRefusal why;
    unsigned int i;
    int fallback_mounted = 0;
    uint32_t source_visible = 0U;
    uint32_t script_visible = 0U;
    uint32_t animations_visible = 0U;
    if (!result) return 0;
    init_result(result);
#define REFUSE(code) do { \
    DlumaAnimationRefreshRefusal final_code = (code); \
    result->primary_refusal = (code); \
    if (fallback_mounted) { \
        if (!operations->fallback_unmount_supported || \
            !operations->unmount_fallback_rpack) { \
            result->fallback_unmount = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED; \
            final_code = DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_UNSUPPORTED; \
        } else if (!operations->unmount_fallback_rpack( \
                       operations->context, \
                       result->fallback_artifact_index)) { \
            result->fallback_unmount = DLUMA_ANIMATION_REFRESH_STAGE_FAILED; \
            final_code = DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_FAILED; \
        } else { \
            result->fallback_unmount = DLUMA_ANIMATION_REFRESH_STAGE_PASSED; \
        } \
        fallback_mounted = 0; \
    } \
    result->refusal = final_code; \
    return 0; \
} while (0)
    if (!request || !manifest || !admission || !operations)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_INVALID);
    if (!admission->enabled) REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_DISABLED);
    if (admission->actual_host != DLUMA_ANIMATION_REFRESH_HOST_EDITOR &&
        admission->actual_host != DLUMA_ANIMATION_REFRESH_HOST_PLAYER)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_UNSUPPORTED_HOST);
    if (!admission->exact_profile)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_EXACT_PROFILE_UNAVAILABLE);
    if (!admission->active_project_owned)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_NO_ACTIVE_PROJECT);
    if (request->target_host != admission->actual_host)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_HOST_MISMATCH);
    if (request->route != admission->configured_route)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_ROUTE_NOT_ALLOWED);
    if (request->route == DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE &&
        admission->actual_host == DLUMA_ANIMATION_REFRESH_HOST_PLAYER)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_RAW_LOOSE_PLAYER_UNSUPPORTED);
    if (admission->now_unix_seconds < request->created_unix_seconds ||
        admission->now_unix_seconds > request->expires_unix_seconds)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_EXPIRED);
    if (admission->request_replayed)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REPLAY);
    if (!eq(request->deployment_id, manifest->deployment_id))
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_DEPLOYMENT_MISMATCH);
    if (manifest->created_unix_seconds > request->created_unix_seconds)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_INVALID);
    if (!admission->manifest_owned)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_NOT_OWNED);
    if (!admission->manifest_hash_matches)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_MANIFEST_HASH_MISMATCH);
    if (!dluma_animation_refresh_manifest_validate(manifest, request->route, &why))
        REFUSE(why);
    if (!admission->artifacts_owned)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_NOT_OWNED);
    if (!admission->artifact_hashes_match)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_ARTIFACT_HASH_MISMATCH);
    for (i = 0; i < manifest->artifact_count; ++i) {
        if (manifest->artifacts[i].role ==
            DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION) ++result->animation_count;
        if (manifest->artifacts[i].role ==
            DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK)
            result->fallback_artifact_index = i;
    }
    if (operations->selected_model_reload_supported &&
        operations->reload_selected_model) {
        if (!operations->reload_selected_model(
                operations->context, request->route, result)) {
            DlumaAnimationRefreshRefusal selected_refusal = result->refusal;
            if (selected_refusal == DLUMA_ANIMATION_REFRESH_REFUSAL_NONE)
                selected_refusal =
                    DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
            result->primary_refusal = selected_refusal;
            result->refusal = selected_refusal;
            return 0;
        }
        result->accepted = 1U;
        result->refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
        return 1;
    }
    if (request->route ==
        DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK) {
        if (!operations->fallback_mount_supported ||
            !operations->mount_fallback_rpack) {
            result->fallback_mount = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
            result->pack_registration = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
            REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MOUNT_UNSUPPORTED);
        }
        if (!operations->fallback_unmount_supported ||
            !operations->unmount_fallback_rpack) {
            result->fallback_unmount = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
            REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_UNMOUNT_UNSUPPORTED);
        }
        if (!operations->mount_fallback_rpack(
            operations->context, result->fallback_artifact_index)) {
            result->fallback_mount = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            result->pack_registration = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_FALLBACK_MOUNT_FAILED);
        }
        fallback_mounted = 1;
        result->fallback_mount = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
        result->pack_registration = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    }
    if (!operations->filesystem_refresh_supported ||
        !operations->filesystem_refresh) {
        result->filesystem_refresh = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_UNSUPPORTED);
    }
    if (!operations->filesystem_refresh(operations->context)) {
        result->filesystem_refresh = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED);
    }
    result->filesystem_refresh = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    if (!operations->visibility_check_supported ||
        !operations->check_visibility) {
        result->source_visible = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        result->script_visible = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        result->animation_files_visible = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_VISIBILITY_CHECK_UNSUPPORTED);
    }
    if (!operations->check_visibility(
            operations->context, &source_visible, &script_visible,
            &animations_visible)) {
        result->source_visible = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result->script_visible = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result->animation_files_visible = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_VISIBILITY_CHECK_FAILED);
    }
    result->source_visible = source_visible ?
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED :
        DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    result->script_visible = script_visible ?
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED :
        DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    result->animation_files_visible = animations_visible ?
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED :
        DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    if (!source_visible)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_SOURCE_NOT_VISIBLE);
    if (!script_visible)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_NOT_VISIBLE);
    if (!animations_visible)
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_ANIMATION_NOT_VISIBLE);
    if (!operations->reload_supported || !operations->reload_animations) {
        result->reload = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_RELOAD_UNSUPPORTED);
    }
    if (!operations->reload_animations(operations->context)) {
        result->reload = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_RELOAD_FAILED);
    }
    result->reload = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    if (!operations->rebind_supported || !operations->rebind_animator) {
        result->rebind = DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REBIND_UNSUPPORTED);
    }
    if (!operations->rebind_animator(operations->context)) {
        result->rebind = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        REFUSE(DLUMA_ANIMATION_REFRESH_REFUSAL_REBIND_FAILED);
    }
    result->rebind = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->selected_model_match = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->script_bank_resolution = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->bind_creation = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->clip_resolution = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->inspector_rebuild = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->animation_name_count = result->animation_count;
    result->accepted = 1U;
    result->refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
#undef REFUSE
    return 1;
}
const char* dluma_animation_refresh_host_name(DlumaAnimationRefreshHost host)
{
    return host == DLUMA_ANIMATION_REFRESH_HOST_EDITOR ? "Editor" :
        host == DLUMA_ANIMATION_REFRESH_HOST_PLAYER ? "Player" : "Unsupported";
}
const char* dluma_animation_refresh_route_name(DlumaAnimationRefreshRoute route)
{
    return route == DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE ? "RawLoose" :
        route == DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK ?
        "ProjectRPack" : "Unknown";
}
const char* dluma_animation_refresh_stage_name(DlumaAnimationRefreshStage stage)
{
    return stage == DLUMA_ANIMATION_REFRESH_STAGE_PASSED ? "Passed" :
        stage == DLUMA_ANIMATION_REFRESH_STAGE_FAILED ? "Failed" :
        stage == DLUMA_ANIMATION_REFRESH_STAGE_UNSUPPORTED ? "Unsupported" :
        "NotAttempted";
}
const char* dluma_animation_refresh_refusal_name(DlumaAnimationRefreshRefusal value)
{
    static const char* names[] = {
        "none","disabled","unsupported_host","exact_profile_unavailable",
        "no_active_project","request_invalid","request_expired",
        "request_host_mismatch","route_not_allowed","manifest_path_invalid",
        "manifest_not_owned","manifest_hash_mismatch","manifest_invalid",
        "deployment_mismatch","artifact_invalid","artifact_not_owned",
        "artifact_hash_mismatch","duplicate_path","duplicate_identity",
        "alias_missing","script_missing","animation_missing",
        "fallback_missing","replay","request_coalesced",
        "filesystem_refresh_unsupported",
        "filesystem_refresh_failed","visibility_check_unsupported",
        "visibility_check_failed","source_not_visible","script_not_visible",
        "animation_not_visible","reload_unsupported","reload_failed",
        "rebind_unsupported","rebind_failed","fallback_mount_unsupported",
        "fallback_mount_failed","fallback_unmount_unsupported",
        "fallback_unmount_failed","selected_model_mismatch",
        "bank_still_referenced","pack_registration_failed",
        "script_bank_resolution_failed","bind_creation_failed",
        "clip_resolution_failed","inspector_rebuild_failed",
        "raw_loose_player_unsupported","owner_thread_dispatch_failed",
        "rollback_failed","result_write_failed"};
    return (unsigned int)value < sizeof(names) / sizeof(names[0]) ?
        names[(unsigned int)value] : "unknown";
}
