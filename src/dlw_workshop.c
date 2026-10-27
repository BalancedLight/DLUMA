#include "dlw_common.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"

static int dluma_is_dot_dir(const WCHAR* name)
{
    return !name || (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2])));
}

static void dluma_log_wide_pair(const char* label, const WCHAR* value)
{
    char utf8[DLUMA_MAX_WPATH * 2];
    if (!dluma_wide_to_utf8(value, utf8, DLUMA_ARRAY_COUNT(utf8))) dluma_str_copy(utf8, DLUMA_ARRAY_COUNT(utf8), "<unicode conversion failed>");
    dluma_log_pair("INFO", label, utf8);
}

typedef struct DlumaProjectClaimScan {
    DlumaDirectoryIdentity first_identity;
    WCHAR first_path[DLUMA_MAX_WPATH];
    unsigned int unique_count;
    unsigned char complete;
} DlumaProjectClaimScan;

typedef void (*DlumaProjectClaimVisitor)(
    const WCHAR* candidate,
    const DlumaDirectoryIdentity* candidate_identity,
    void* context);

static void dluma_add_root(const WCHAR* root)
{
    WCHAR clean[DLUMA_MAX_WPATH];
    SIZE_T n;
    unsigned int i;
    if (!root || !root[0] || g_dluma.workshop_root_count >= DLUMA_MAX_ROOTS) return;
    dluma_wstr_copy(clean, DLUMA_ARRAY_COUNT(clean), root);
    n = wcslen(clean);
    while (n > 3 && (clean[n - 1] == L'\\' || clean[n - 1] == L'/')) clean[--n] = 0;
    if (!dluma_dir_exists(clean)) return;
    for (i = 0; i < g_dluma.workshop_root_count; ++i) {
        if (dluma_wstricmp(g_dluma.workshop_roots[i], clean) == 0) return;
    }
    dluma_wstr_copy(g_dluma.workshop_roots[g_dluma.workshop_root_count++], DLUMA_MAX_WPATH, clean);
}

static void dluma_add_env_roots(void)
{
    WCHAR env[DLUMA_MAX_WPATH * 2];
    DWORD n = GetEnvironmentVariableW(L"DLUMA_WORKSHOP_ROOTS", env, DLUMA_ARRAY_COUNT(env));
    unsigned int start = 0;
    unsigned int i;
    if (!n || n >= DLUMA_ARRAY_COUNT(env)) return;
    for (i = 0;; ++i) {
        if (env[i] == L';' || env[i] == 0) {
            WCHAR one[DLUMA_MAX_WPATH];
            unsigned int len = i - start;
            unsigned int j;
            if (len >= DLUMA_ARRAY_COUNT(one)) len = DLUMA_ARRAY_COUNT(one) - 1;
            for (j = 0; j < len; ++j) one[j] = env[start + j];
            one[len] = 0;
            dluma_add_root(one);
            if (!env[i]) break;
            start = i + 1;
        }
    }
}

static void dluma_add_derived_roots_from_path(const WCHAR* source_path)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR candidate[DLUMA_MAX_WPATH];
    WCHAR parent[DLUMA_MAX_WPATH];
    unsigned int i;
    if (!source_path || !source_path[0]) return;

    dluma_wstr_copy(path, DLUMA_ARRAY_COUNT(path), source_path);
    if (!dluma_dir_exists(path)) dluma_path_dirname(path);

    /* A loader copied to <project>\mods can discover that project directly. */
    dluma_wstr_copy(parent, DLUMA_ARRAY_COUNT(parent), path);
    if (dluma_wends_with_icase(parent, L"\\mods") || dluma_wends_with_icase(parent, L"/mods")) {
        dluma_path_parent(parent);
        dluma_add_root(parent);
    }

    /* Walk likely game/Steam ancestors instead of assuming where the EXE lives. */
    dluma_wstr_copy(parent, DLUMA_ARRAY_COUNT(parent), path);
    for (i = 0; i < 12 && parent[0]; ++i) {
        const WCHAR* last = parent;
        WCHAR above[DLUMA_MAX_WPATH];
        const WCHAR* above_last;
        unsigned int j;

        /* Every ancestor is a possible Dying Light install root. */
        dluma_path_join(candidate, DLUMA_ARRAY_COUNT(candidate), parent, L"DevTools\\workshop");
        dluma_add_root(candidate);

        for (j = 0; parent[j]; ++j) if (parent[j] == L'\\' || parent[j] == L'/') last = parent + j + 1;
        if (dluma_wstricmp(last, L"239140") == 0) dluma_add_root(parent);

        if (dluma_wstricmp(last, L"workshop") == 0) {
            dluma_wstr_copy(above, DLUMA_ARRAY_COUNT(above), parent);
            dluma_path_parent(above);
            above_last = above;
            for (j = 0; above[j]; ++j) if (above[j] == L'\\' || above[j] == L'/') above_last = above + j + 1;
            if (dluma_wstricmp(above_last, L"DevTools") == 0) dluma_add_root(parent);
        }

        if (dluma_wstricmp(last, L"common") == 0) {
            dluma_wstr_copy(above, DLUMA_ARRAY_COUNT(above), parent);
            dluma_path_parent(above); /* steamapps */
            dluma_path_join(candidate, DLUMA_ARRAY_COUNT(candidate), above, L"workshop\\content\\239140");
            dluma_add_root(candidate);
        }
        dluma_path_parent(parent);
    }
}

static ULONGLONG dluma_roots_hash(void)
{
    ULONGLONG hash = 1469598103934665603ULL;
    unsigned int i, j;
    for (i = 0; i < g_dluma.workshop_root_count; ++i) {
        for (j = 0; g_dluma.workshop_roots[i][j]; ++j) {
            hash ^= (ULONGLONG)dluma_wascii_lower(g_dluma.workshop_roots[i][j]);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xFFu;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void dluma_discover_workshop_roots(void)
{
    WCHAR exe_path[DLUMA_MAX_WPATH];
    WCHAR cwd[DLUMA_MAX_WPATH];
    unsigned int i;
    ULONGLONG old_hash = dluma_roots_hash();
    ULONGLONG new_hash;
    g_dluma.workshop_root_count = 0;

    for (i = 0; i < g_dluma.config.configured_root_count; ++i) dluma_add_root(g_dluma.config.configured_roots[i]);
    dluma_add_env_roots();

    exe_path[0] = 0;
    if (!dluma_get_module_path_checked(
            0, exe_path, DLUMA_ARRAY_COUNT(exe_path)))
        exe_path[0] = 0;
    dluma_add_derived_roots_from_path(exe_path);
    dluma_add_derived_roots_from_path(g_dluma.module_dir);

    cwd[0] = 0;
    if (GetCurrentDirectoryW(DLUMA_ARRAY_COUNT(cwd), cwd)) dluma_add_derived_roots_from_path(cwd);

    new_hash = dluma_roots_hash();
    if (new_hash != old_hash) {
        dluma_log_u64("INFO", "workshop_root_count", g_dluma.workshop_root_count);
        for (i = 0; i < g_dluma.workshop_root_count; ++i) dluma_log_wide_pair("workshop_root", g_dluma.workshop_roots[i]);
    }
    if (!g_dluma.workshop_root_count) {
        dluma_log_line("WARN", "No workshop roots were found. Set DLUMA_WORKSHOP_ROOTS or edit dluma.ini.");
    }
}

static int dluma_manifest_matches_map(const WCHAR* project, const char* map_name)
{
    WCHAR manifest[DLUMA_MAX_WPATH];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    WCHAR configured[1024];
    unsigned int start = 0;
    unsigned int i;
    if (!dluma_valid_map_segment(map_name) ||
        !dluma_utf8_to_wide(
            map_name, map_wide, DLUMA_ARRAY_COUNT(map_wide)) ||
        !dluma_checked_path_join(
            manifest, DLUMA_ARRAY_COUNT(manifest), project,
            L"data\\dlscripts\\dluma_project.ini"))
        return 0;
    if (!dluma_path_exists(manifest) ||
        !dluma_project_regular_file_is_contained(
            project, manifest))
        return 0;
    configured[0] = 0;
    GetPrivateProfileStringW(L"project", L"maps", L"", configured, DLUMA_ARRAY_COUNT(configured), manifest);
    if (!configured[0]) GetPrivateProfileStringW(L"project", L"map", L"", configured, DLUMA_ARRAY_COUNT(configured), manifest);
    for (i = 0;; ++i) {
        if (configured[i] == L';' || configured[i] == L',' || configured[i] == 0) {
            WCHAR item[DLUMA_MAX_MAP_NAME];
            unsigned int len = i - start;
            unsigned int j, begin = 0;
            if (len >= DLUMA_ARRAY_COUNT(item)) len = DLUMA_ARRAY_COUNT(item) - 1;
            for (j = 0; j < len; ++j) item[j] = configured[start + j];
            item[len] = 0;
            while (item[begin] == L' ' || item[begin] == L'\t') ++begin;
            len = (unsigned int)wcslen(item + begin);
            while (len && (item[begin + len - 1] == L' ' || item[begin + len - 1] == L'\t')) item[begin + --len] = 0;
            if (item[begin] && dluma_wstricmp(item + begin, map_wide) == 0) return 1;
            if (!configured[i]) break;
            start = i + 1;
        }
    }
    return 0;
}

static int dluma_candidate_matches_map(const WCHAR* candidate, const char* map_name)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    if (!candidate || !dluma_valid_map_segment(map_name)) return 0;
    if (!dluma_utf8_to_wide(map_name, map_wide, DLUMA_ARRAY_COUNT(map_wide))) return 0;
    if (!dluma_checked_path_join(
            path, DLUMA_ARRAY_COUNT(path), candidate,
            L"data\\maps") ||
        !dluma_checked_path_join(
            path, DLUMA_ARRAY_COUNT(path), path, map_wide))
        return 0;
    if (dluma_dir_exists(path)) return 1;
    return dluma_manifest_matches_map(candidate, map_name);
}

static int dluma_candidate_map_directory_state(
    const WCHAR* candidate, const char* map_name)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    if (!candidate || !dluma_valid_map_segment(map_name) ||
        !dluma_utf8_to_wide(
            map_name, map_wide,
            DLUMA_ARRAY_COUNT(map_wide)) ||
        !dluma_checked_path_join(
            path, DLUMA_ARRAY_COUNT(path),
            candidate, L"data\\maps") ||
        !dluma_checked_path_join(
            path, DLUMA_ARRAY_COUNT(path),
            path, map_wide))
        return -1;
    return dluma_dir_exists(path) ? 1 : 0;
}

static int dluma_read_desc_field(const WCHAR* project, const char* field,
                               char* value_out, unsigned int value_capacity)
{
    WCHAR path[DLUMA_MAX_WPATH];
    char* text = 0;
    DWORD size = 0;
    unsigned int i;
    unsigned int field_length;
    if (!project || !field || !field[0] || !value_out || value_capacity < 2)
        return 0;
    value_out[0] = 0;
    field_length = (unsigned int)strlen(field);
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), project, L"desc.scr");
    if (!dluma_read_file_utf8(path, &text, &size) || !text) return 0;
    for (i = 0; i + field_length + 2 < size; ++i) {
        unsigned int cursor;
        unsigned int count = 0;
        unsigned int j;
        for (j = 0; j < field_length; ++j) {
            if (dluma_ascii_lower(text[i + j]) != dluma_ascii_lower(field[j]))
                break;
        }
        if (j != field_length) continue;
        cursor = i + field_length;
        while (cursor < size && dluma_is_space(text[cursor])) ++cursor;
        if (cursor >= size || text[cursor++] != '(') continue;
        while (cursor < size && dluma_is_space(text[cursor])) ++cursor;
        if (cursor >= size || text[cursor++] != '"') continue;
        while (cursor < size && text[cursor] != '"' &&
               count + 1 < value_capacity) {
            value_out[count++] = text[cursor++];
        }
        value_out[count] = 0;
        if (count && cursor < size && text[cursor] == '"') {
            dluma_free(text);
            return 1;
        }
        value_out[0] = 0;
    }
    dluma_free(text);
    return 0;
}

static int dluma_read_workshop_id(const WCHAR* project, char* id_out,
                                unsigned int id_capacity)
{
    return dluma_read_desc_field(project, "id", id_out, id_capacity);
}

static void dluma_source_normalize_utf8(const char* input, char* output,
                                      unsigned int capacity)
{
    unsigned int i = 0;
    if (!output || !capacity) return;
    while (input && *input && i + 1U < capacity) {
        char c = dluma_ascii_lower(*input++);
        output[i++] = c == '\\' ? '/' : c;
    }
    output[i] = 0;
}

static int dluma_source_is_subscribed_path(const WCHAR* canonical_root)
{
    return canonical_root &&
        (dluma_wcontains_icase(
             canonical_root, L"\\steamapps\\workshop\\content\\239140\\") ||
         dluma_wcontains_icase(
             canonical_root, L"/steamapps/workshop/content/239140/"));
}

int dluma_classify_project_source(
    const WCHAR* project_root, DlumaProjectSourceDescription* description)
{
    DlumaDirectoryIdentity identity;
    const WCHAR* leaf;
    char canonical_utf8[DLUMA_MAX_WPATH * 2];
    char normalized[DLUMA_MAX_WPATH * 2];
    char path_hash[DLUMA_SHA256_HEX_CAPACITY];
    const char* path_fields[1];
    unsigned int i;
    if (!project_root || !project_root[0] || !description) return 0;
    memset(description, 0, sizeof(*description));
    if (!dluma_open_directory_identity(project_root, &identity)) return 0;
    dluma_wstr_copy(description->canonical_root,
                  DLUMA_ARRAY_COUNT(description->canonical_root),
                  identity.final_path);
    dluma_read_desc_field(project_root, "title", description->project_name,
                        DLUMA_ARRAY_COUNT(description->project_name));
    if (!dluma_read_workshop_id(project_root, description->published_id,
                              DLUMA_ARRAY_COUNT(description->published_id)) ||
        dluma_stricmp(description->published_id, "0") == 0)
        description->published_id[0] = 0;
    description->is_subscribed_workshop = (unsigned char)
        (dluma_source_is_subscribed_path(identity.final_path) ? 1 : 0);
    leaf = identity.final_path;
    for (i = 0; identity.final_path[i]; ++i)
        if (identity.final_path[i] == L'\\' ||
            identity.final_path[i] == L'/')
            leaf = identity.final_path + i + 1;
    if (!description->project_name[0] &&
        !dluma_wide_to_utf8(leaf, description->project_name,
                          DLUMA_ARRAY_COUNT(description->project_name)))
        dluma_str_copy(description->project_name,
                     DLUMA_ARRAY_COUNT(description->project_name),
                     "Unnamed local project");
    if (!dluma_wide_to_utf8(identity.final_path, canonical_utf8,
                          DLUMA_ARRAY_COUNT(canonical_utf8)))
        return 0;
    dluma_source_normalize_utf8(canonical_utf8, normalized,
                              DLUMA_ARRAY_COUNT(normalized));
    path_fields[0] = normalized;
    dluma_sha256_fields_hex(
        "DLW_MOD_SOURCE_PATH_V1", path_fields, 0, 1U, path_hash);
    if (description->is_subscribed_workshop &&
        description->published_id[0]) {
        dluma_str_copy(description->source_identity,
                     DLUMA_ARRAY_COUNT(description->source_identity),
                     "subscribed-workshop:");
        dluma_str_append(description->source_identity,
                       DLUMA_ARRAY_COUNT(description->source_identity),
                       description->published_id);
        dluma_str_append(description->source_identity,
                       DLUMA_ARRAY_COUNT(description->source_identity), ":");
        dluma_str_append(description->source_identity,
                       DLUMA_ARRAY_COUNT(description->source_identity),
                       path_hash);
        return 1;
    }
    dluma_str_copy(description->source_identity,
                 DLUMA_ARRAY_COUNT(description->source_identity),
                 description->is_subscribed_workshop
                     ? "subscribed-workshop-path:"
                     : "local-project:");
    dluma_str_append(description->source_identity,
                   DLUMA_ARRAY_COUNT(description->source_identity), path_hash);
    return 1;
}

static int dluma_mod_source_key(const char* kind, const char* declared_id,
                              char* output, unsigned int capacity)
{
    char normalized_kind[32];
    char normalized_id[128];
    if (!kind || !kind[0] || !declared_id || !declared_id[0] ||
        !output || !capacity)
        return 0;
    dluma_source_normalize_utf8(kind, normalized_kind,
                              DLUMA_ARRAY_COUNT(normalized_kind));
    dluma_source_normalize_utf8(declared_id, normalized_id,
                              DLUMA_ARRAY_COUNT(normalized_id));
    if (strlen(normalized_kind) + strlen(normalized_id) + 2U > capacity)
        return 0;
    dluma_str_copy(output, capacity, normalized_kind);
    dluma_str_append(output, capacity, ":");
    dluma_str_append(output, capacity, normalized_id);
    return 1;
}

int dluma_mod_source_selection_read(
    const char* kind, const char* declared_id,
    char* source_identity_out, unsigned int capacity,
    int* explicit_selection_out)
{
    char key[192];
    WCHAR key_w[192];
    WCHAR value_w[1024];
    DWORD count;
    if (source_identity_out && capacity) source_identity_out[0] = 0;
    if (explicit_selection_out) *explicit_selection_out = 0;
    if (!source_identity_out || !capacity ||
        !dluma_mod_source_key(kind, declared_id, key, DLUMA_ARRAY_COUNT(key)) ||
        !dluma_utf8_to_wide(key, key_w, DLUMA_ARRAY_COUNT(key_w)))
        return 0;
    value_w[0] = 0;
    count = GetPrivateProfileStringW(L"mod_sources", key_w, L"", value_w,
                                     DLUMA_ARRAY_COUNT(value_w),
                                     g_dluma.config_path);
    if (!count || !value_w[0]) return 1;
    if (explicit_selection_out) *explicit_selection_out = 1;
    return dluma_wide_to_utf8(value_w, source_identity_out, capacity);
}

int dluma_mod_source_selection_write(
    const char* kind, const char* declared_id,
    const char* source_identity)
{
    char key[192];
    WCHAR key_w[192];
    WCHAR value_w[1024];
    if (!source_identity || !source_identity[0] ||
        !dluma_mod_source_key(kind, declared_id, key, DLUMA_ARRAY_COUNT(key)) ||
        !dluma_utf8_to_wide(key, key_w, DLUMA_ARRAY_COUNT(key_w)) ||
        !dluma_utf8_to_wide(source_identity, value_w,
                          DLUMA_ARRAY_COUNT(value_w)))
        return 0;
    if (!WritePrivateProfileStringW(L"mod_sources", key_w, value_w,
                                    g_dluma.config_path))
        return 0;
    return WritePrivateProfileStringW(0, 0, 0, g_dluma.config_path) ? 1 : 0;
}

void dluma_describe_project_source(const WCHAR* project_root,
                                 char* name_out, unsigned int name_capacity,
                                 char* id_out, unsigned int id_capacity,
                                 unsigned char* is_workshop_out)
{
    DlumaProjectSourceDescription description;
    if (name_out && name_capacity) name_out[0] = 0;
    if (id_out && id_capacity) id_out[0] = 0;
    if (is_workshop_out) *is_workshop_out = 0;
    if (!dluma_classify_project_source(project_root, &description)) return;
    if (name_out && name_capacity)
        dluma_str_copy(name_out, name_capacity, description.project_name);
    if (id_out && id_capacity)
        dluma_str_copy(id_out, id_capacity, description.published_id);
    if (is_workshop_out)
        *is_workshop_out = description.is_subscribed_workshop;
}

static void dluma_visit_project_claims(
    const char* map_name, DlumaProjectClaimVisitor visitor,
    void* context, unsigned char* complete_out)
{
    unsigned int root_index;
    unsigned char complete = 1;
    for (root_index = 0;
         root_index < g_dluma.workshop_root_count;
         ++root_index) {
        WCHAR pattern[DLUMA_MAX_WPATH];
        DlumaDirectoryIdentity discovery_identity;
        WIN32_FIND_DATAW data;
        HANDLE find;
        const WCHAR* const root =
            g_dluma.workshop_roots[root_index];
        if (!dluma_open_directory_identity(
                root, &discovery_identity)) {
            complete = 0;
            continue;
        }
        if (dluma_candidate_matches_map(root, map_name)) {
            DlumaDirectoryIdentity candidate_identity;
            if (dluma_validate_candidate_under_root(
                    root, &discovery_identity,
                    &candidate_identity)) {
                int map_directory_state =
                    dluma_candidate_map_directory_state(
                        root, map_name);
                if (map_directory_state < 0 ||
                    (map_directory_state > 0 &&
                     !dluma_validate_map_anchor_under_project(
                         root, map_name,
                         &candidate_identity)))
                    complete = 0;
                else
                    visitor(
                        root, &candidate_identity, context);
            } else {
                complete = 0;
            }
        }
        if (!dluma_checked_path_join(
                pattern, DLUMA_ARRAY_COUNT(pattern),
                root, L"*")) {
            complete = 0;
            continue;
        }
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND &&
                error != ERROR_NO_MORE_FILES)
                complete = 0;
            continue;
        }
        do {
            WCHAR candidate[DLUMA_MAX_WPATH];
            DlumaDirectoryIdentity candidate_identity;
            if (!(data.dwFileAttributes &
                  FILE_ATTRIBUTE_DIRECTORY) ||
                dluma_is_dot_dir(data.cFileName))
                continue;
            if (!dluma_checked_path_join(
                    candidate, DLUMA_ARRAY_COUNT(candidate),
                    root, data.cFileName)) {
                complete = 0;
                continue;
            }
            if (!dluma_candidate_matches_map(
                    candidate, map_name))
                continue;
            if (!dluma_validate_candidate_under_root(
                    candidate, &discovery_identity,
                    &candidate_identity)) {
                complete = 0;
                continue;
            }
            {
                int map_directory_state =
                    dluma_candidate_map_directory_state(
                        candidate, map_name);
                if (map_directory_state < 0 ||
                    (map_directory_state > 0 &&
                     !dluma_validate_map_anchor_under_project(
                         candidate, map_name,
                         &candidate_identity))) {
                    complete = 0;
                    continue;
                }
            }
            visitor(candidate, &candidate_identity, context);
        } while (FindNextFileW(find, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES)
            complete = 0;
        FindClose(find);
    }
    if (complete_out) *complete_out = complete;
}

static void dluma_collect_project_claim(
    const WCHAR* candidate,
    const DlumaDirectoryIdentity* candidate_identity,
    void* context)
{
    DlumaProjectClaimScan* scan =
        (DlumaProjectClaimScan*)context;
    if (!scan || !candidate || !candidate_identity) return;
    if (!scan->unique_count) {
        scan->first_identity = *candidate_identity;
        dluma_wstr_copy(
            scan->first_path,
            DLUMA_ARRAY_COUNT(scan->first_path), candidate);
        scan->unique_count = 1;
        return;
    }
    if (!dluma_directory_identity_equal(
            &scan->first_identity, candidate_identity))
        scan->unique_count = 2;
}

typedef struct DlumaExactProofMatchScan {
    const char* map_name;
    const DlumaDirectoryIdentity* proof_identity;
    unsigned int unique_matches;
    unsigned char complete;
} DlumaExactProofMatchScan;

static void dluma_collect_exact_proof_match(
    const WCHAR* candidate,
    const DlumaDirectoryIdentity* candidate_identity,
    void* context)
{
    DlumaExactProofMatchScan* scan =
        (DlumaExactProofMatchScan*)context;
    if (!scan || !candidate || !candidate_identity) return;
    if (!dluma_validate_map_anchor_under_project(
            candidate, scan->map_name,
            candidate_identity)) {
        scan->complete = 0;
        return;
    }
    if (dluma_directory_identity_equal(
            candidate_identity, scan->proof_identity))
        scan->unique_matches = 1;
}

static const char* dluma_project_owner_reason_name(
    unsigned int reason)
{
    switch (reason) {
    case DLUMA_PROJECT_OWNER_REASON_NONE:
        return "none";
    case DLUMA_PROJECT_OWNER_REASON_TRACKING_UNAVAILABLE:
        return "tracking_unavailable";
    case DLUMA_PROJECT_OWNER_REASON_NO_EVIDENCE:
        return "no_evidence";
    case DLUMA_PROJECT_OWNER_REASON_MAP_MISMATCH:
        return "map_mismatch";
    case DLUMA_PROJECT_OWNER_REASON_TRANSITION_MISMATCH:
        return "transition_mismatch";
    case DLUMA_PROJECT_OWNER_REASON_CONSUMED:
        return "proof_consumed";
    case DLUMA_PROJECT_OWNER_REASON_AMBIGUOUS_EVIDENCE:
        return "ambiguous_evidence";
    case DLUMA_PROJECT_OWNER_REASON_NO_ACTIVE_EPOCH:
        return "no_active_epoch";
    case DLUMA_PROJECT_OWNER_REASON_OWNERSHIP_INCOMPLETE:
        return "ownership_incomplete";
    case DLUMA_PROJECT_OWNER_REASON_SOURCE_INVALID:
        return "source_invalid";
    case DLUMA_PROJECT_OWNER_REASON_INVALID_PATH:
        return "invalid_path";
    case DLUMA_PROJECT_OWNER_REASON_LEDGER_BUSY:
        return "ledger_busy";
    default:
        return "unknown";
    }
}

static void dluma_log_project_owner_proof_state(
    const char* label, const char* map_name,
    const char* reason)
{
    static char last_record[DLUMA_MAX_MAP_NAME + 128];
    char record[DLUMA_MAX_MAP_NAME + 128];
    record[0] = 0;
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), "state=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), label);
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), " map=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), map_name);
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), " reason=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), reason);
    if (dluma_stricmp(last_record, record) == 0) return;
    dluma_str_copy(
        last_record, DLUMA_ARRAY_COUNT(last_record), record);
    dluma_log_pair("WARN", label, record);
}

int dluma_find_project_for_map(
    const char* map_name, WCHAR* project_out,
    unsigned int project_cap,
    DlumaProjectOwnerProofSnapshot* proof_out,
    unsigned int active_owner_epoch,
    ULONGLONG active_owner_transition_generation)
{
    static char last_ambiguous_map[DLUMA_MAX_MAP_NAME];
    static char last_retained_pending_map[DLUMA_MAX_MAP_NAME];
    DlumaProjectClaimScan claims;
    DlumaProjectOwnerProofSnapshot proof;
    DlumaDirectoryIdentity proof_identity;
    DlumaExactProofMatchScan matches;
    unsigned char visit_complete = 1;
    if (!project_out || !project_cap ||
        !dluma_valid_map_segment(map_name))
        return 0;
    project_out[0] = 0;
    if (proof_out) {
        memset(proof_out, 0, sizeof(*proof_out));
        proof_out->status =
            DLUMA_PROJECT_OWNER_PROOF_UNAVAILABLE;
    }
    memset(&claims, 0, sizeof(claims));
    claims.complete = 1;
    dluma_visit_project_claims(
        map_name, dluma_collect_project_claim,
        &claims, &visit_complete);
    claims.complete = visit_complete;
    if (!claims.complete) {
        dluma_log_project_owner_proof_state(
            "project_owner_proof_rejected", map_name,
            "candidate_scan_incomplete");
        return 0;
    }
    if (claims.unique_count == 1) {
        last_ambiguous_map[0] = 0;
        last_retained_pending_map[0] = 0;
        dluma_wstr_copy(
            project_out, project_cap, claims.first_path);
        return 1;
    }
    if (claims.unique_count < 2) return 0;

    memset(&proof, 0, sizeof(proof));
    dluma_custom_map_exact_project_owner_snapshot(
        map_name, &proof);
    if (proof_out) *proof_out = proof;
    if (proof.status != DLUMA_PROJECT_OWNER_PROOF_READY) {
        const char* const label =
            proof.status == DLUMA_PROJECT_OWNER_PROOF_REJECTED
                ? "project_owner_proof_rejected"
                : "project_owner_proof_pending";
        dluma_log_project_owner_proof_state(
            label, map_name,
            dluma_project_owner_reason_name(proof.reason));
        if (dluma_project_owner_pending_proof_retains_context(
                active_owner_epoch,
                active_owner_transition_generation,
                &proof)) {
            if (dluma_stricmp(
                    last_retained_pending_map,
                    map_name) != 0) {
                dluma_str_copy(
                    last_retained_pending_map,
                    DLUMA_ARRAY_COUNT(
                        last_retained_pending_map),
                    map_name);
                dluma_log_pair(
                    "INFO",
                    "project_owner_context_retained_pending_exact_proof",
                    map_name);
            }
            return 0;
        }
        goto ambiguous;
    }
    if (dluma_stricmp(proof.map_name, map_name) != 0 ||
        !dluma_open_directory_identity(
            proof.project_root, &proof_identity) ||
        !dluma_validate_map_anchor_under_project(
            proof.project_root, map_name,
            &proof_identity)) {
        dluma_log_project_owner_proof_state(
            "project_owner_proof_rejected", map_name,
            "proof_root_validation_failed");
        goto ambiguous;
    }
    memset(&matches, 0, sizeof(matches));
    matches.map_name = map_name;
    matches.proof_identity = &proof_identity;
    matches.complete = 1;
    visit_complete = 1;
    dluma_visit_project_claims(
        map_name, dluma_collect_exact_proof_match,
        &matches, &visit_complete);
    if (!visit_complete || !matches.complete ||
        matches.unique_matches != 1) {
        dluma_log_project_owner_proof_state(
            "project_owner_proof_rejected", map_name,
            !visit_complete || !matches.complete
                ? "candidate_anchor_validation_failed"
                : "no_matching_provider_identity");
        goto ambiguous;
    }
    if (!dluma_custom_map_exact_project_owner_consume(
            &proof)) {
        dluma_log_project_owner_proof_state(
            "project_owner_proof_pending", map_name,
            "proof_consume_failed");
        goto ambiguous;
    }
    last_ambiguous_map[0] = 0;
    last_retained_pending_map[0] = 0;
    dluma_wstr_copy(
        project_out, project_cap, proof.project_root);
    if (proof_out) *proof_out = proof;
    dluma_log_pair(
        "INFO",
        "ambiguous_map_resolved_by_exact_source",
        map_name);
    dluma_log_wide_pair(
        "exact_project_owner_root", proof.project_root);
    return 1;

ambiguous:
    last_retained_pending_map[0] = 0;
    if (dluma_stricmp(last_ambiguous_map, map_name) != 0) {
        dluma_str_copy(
            last_ambiguous_map,
            DLUMA_ARRAY_COUNT(last_ambiguous_map), map_name);
        dluma_log_line(
            "ERROR",
            "Multiple workshop projects claim the active map name; no project context was selected.");
        dluma_log_pair("ERROR", "ambiguous_map", map_name);
    }
    return 0;
}

static int dluma_path_already_collected(WCHAR paths[][DLUMA_MAX_WPATH], unsigned int count, const WCHAR* candidate)
{
    unsigned int i;
    for (i = 0; i < count; ++i) if (dluma_wstricmp(paths[i], candidate) == 0) return 1;
    return 0;
}

static unsigned int dluma_collect_from_dir(const WCHAR* dir, WCHAR paths[][DLUMA_MAX_WPATH], unsigned int count, unsigned int max_paths)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!dluma_dir_exists(dir) || count >= max_paths) return count;
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), dir, L"*.dlscript");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return count;
    do {
        WCHAR full[DLUMA_MAX_WPATH];
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        dluma_path_join(full, DLUMA_ARRAY_COUNT(full), dir, data.cFileName);
        if (!dluma_path_already_collected(paths, count, full) && count < max_paths) {
            dluma_wstr_copy(paths[count++], DLUMA_MAX_WPATH, full);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

static void dluma_sort_paths(WCHAR paths[][DLUMA_MAX_WPATH], unsigned int count)
{
    unsigned int i, j;
    for (i = 1; i < count; ++i) {
        WCHAR temp[DLUMA_MAX_WPATH];
        dluma_wstr_copy(temp, DLUMA_ARRAY_COUNT(temp), paths[i]);
        j = i;
        while (j > 0 && dluma_wstricmp(paths[j - 1], temp) > 0) {
            dluma_wstr_copy(paths[j], DLUMA_MAX_WPATH, paths[j - 1]);
            --j;
        }
        dluma_wstr_copy(paths[j], DLUMA_MAX_WPATH, temp);
    }
}

unsigned int dluma_collect_script_paths(const WCHAR* project_root, const char* map_name, WCHAR paths[][DLUMA_MAX_WPATH], unsigned int max_paths)
{
    WCHAR dir[DLUMA_MAX_WPATH];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];
    unsigned int count = 0;
    if (!project_root || !project_root[0] || !paths || !max_paths) return 0;
    dluma_utf8_to_wide(map_name ? map_name : "", map_wide, DLUMA_ARRAY_COUNT(map_wide));

    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\dlscripts");
    count = dluma_collect_from_dir(dir, paths, count, max_paths);
    /* Transitional compatibility for the location used by the 0.1.3
     * Mesmerized test.  Only *.dlscript is considered, so Techland *.scr
     * files in data\\scripts are untouched. */
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts\\dluma");
    count = dluma_collect_from_dir(dir, paths, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts");
    count = dluma_collect_from_dir(dir, paths, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"dlscripts");
    count = dluma_collect_from_dir(dir, paths, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"mods\\dlscripts");
    count = dluma_collect_from_dir(dir, paths, count, max_paths);
    if (map_wide[0]) {
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\maps");
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, map_wide);
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, L"dlscripts");
        count = dluma_collect_from_dir(dir, paths, count, max_paths);
    }
    dluma_sort_paths(paths, count);
    return count;
}
