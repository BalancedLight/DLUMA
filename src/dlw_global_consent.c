#include "dlw_common.h"

#define DLUMA_GLOBAL_STATE_MAX_BYTES (512UL * 1024UL)
#define DLUMA_GLOBAL_MAX_MUTATIONS (DLUMA_MAX_LUA_FILES + 16U)
#define DLUMA_MOVEFILE_REPLACE_EXISTING 0x00000001UL
#define DLUMA_MOVEFILE_WRITE_THROUGH 0x00000008UL

typedef BOOL (WINAPI *DlumaMoveFileExWFn)(LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI *DlumaDeleteFileWFn)(LPCWSTR);

typedef struct DlumaIniMutation {
    const char* section;
    const char* key;
    const char* value;
    unsigned char written;
} DlumaIniMutation;

static void dluma_global_persistence_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma.lua_discovery.global_persistence_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_global_persistence_unlock(void)
{
    dluma_atomic_exchange(&g_dluma.lua_discovery.global_persistence_lock, 0);
}

static void dluma_global_runtime_queue_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma.lua_discovery.global_runtime_queue_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_global_runtime_queue_unlock(void)
{
    dluma_atomic_exchange(&g_dluma.lua_discovery.global_runtime_queue_lock, 0);
}

static int dluma_global_ascii_token_char(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '.' ||
           value == '_' || value == '-';
}

static void dluma_global_normalize_text(const char* source, char* out,
                                      unsigned int capacity)
{
    unsigned int i = 0;
    if (!out || !capacity) return;
    out[0] = 0;
    if (!source) return;
    while (*source && i + 1U < capacity) {
        char value = *source++;
        if (value == '\\') value = '/';
        out[i++] = dluma_ascii_lower(value);
    }
    out[i] = 0;
    dluma_trim_in_place(out);
}

static void dluma_global_normalize_permissions(const char* source,
                                             char* out,
                                             unsigned int capacity)
{
    char tokens[64][64];
    unsigned int count = 0;
    unsigned int cursor = 0;
    unsigned int i, j;
    memset(tokens, 0, sizeof(tokens));
    if (!out || !capacity) return;
    out[0] = 0;
    while (source && source[cursor] && count < DLUMA_ARRAY_COUNT(tokens)) {
        char token[64];
        unsigned int length = 0;
        int duplicate = 0;
        while (source[cursor] == ',' || dluma_is_space(source[cursor])) ++cursor;
        while (source[cursor] && source[cursor] != ',') {
            char value = dluma_ascii_lower(source[cursor++]);
            if (dluma_global_ascii_token_char(value) &&
                length + 1U < DLUMA_ARRAY_COUNT(token))
                token[length++] = value;
        }
        if (source[cursor] == ',') ++cursor;
        token[length] = 0;
        if (!length) continue;
        for (i = 0; i < count; ++i) {
            if (dluma_stricmp(tokens[i], token) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
            dluma_str_copy(tokens[count++], DLUMA_ARRAY_COUNT(tokens[0]), token);
    }
    for (i = 1; i < count; ++i) {
        char token[64];
        dluma_str_copy(token, DLUMA_ARRAY_COUNT(token), tokens[i]);
        j = i;
        while (j > 0 && dluma_stricmp(tokens[j - 1], token) > 0) {
            dluma_str_copy(tokens[j], DLUMA_ARRAY_COUNT(tokens[j]), tokens[j - 1]);
            --j;
        }
        dluma_str_copy(tokens[j], DLUMA_ARRAY_COUNT(tokens[j]), token);
    }
    for (i = 0; i < count; ++i) {
        if (i) dluma_str_append(out, capacity, ",");
        dluma_str_append(out, capacity, tokens[i]);
    }
}

static int dluma_global_path_prefix(const WCHAR* path, const WCHAR* prefix,
                                  unsigned int* consumed)
{
    unsigned int i = 0;
    if (!path || !prefix) return 0;
    while (prefix[i]) {
        WCHAR a = path[i];
        WCHAR b = prefix[i];
        if (a == L'/') a = L'\\';
        if (b == L'/') b = L'\\';
        if (dluma_wascii_lower(a) != dluma_wascii_lower(b)) return 0;
        ++i;
    }
    if (path[i] == L'\\' || path[i] == L'/') ++i;
    if (consumed) *consumed = i;
    return 1;
}

static void dluma_global_make_relative_path(const DlumaLuaMetadata* item,
                                          char* out,
                                          unsigned int capacity)
{
    unsigned int consumed = 0;
    char utf8[DLUMA_MAX_WPATH * 2];
    const WCHAR* relative = item->file_path;
    if (dluma_global_path_prefix(item->file_path, item->project_root, &consumed))
        relative = item->file_path + consumed;
    utf8[0] = 0;
    if (!dluma_wide_to_utf8(relative, utf8, DLUMA_ARRAY_COUNT(utf8))) {
        out[0] = 0;
        return;
    }
    dluma_global_normalize_text(utf8, out, capacity);
}

static void dluma_global_make_source_identity(DlumaLuaMetadata* item)
{
    DlumaProjectSourceDescription description;
    if (!item || !dluma_classify_project_source(item->project_root,
                                               &description)) {
        if (item) item->source_identity[0] = 0;
        return;
    }
    item->source_is_subscribed = description.is_subscribed_workshop;
    item->source_is_workshop = description.is_subscribed_workshop;
    dluma_str_copy(item->source_identity,
                 DLUMA_ARRAY_COUNT(item->source_identity),
                 description.source_identity);
}

static void dluma_global_component_hash(const char* schema, const char* value,
                                      char out[DLUMA_SHA256_HEX_CAPACITY])
{
    const char* fields[1];
    fields[0] = value ? value : "";
    dluma_sha256_fields_hex(schema, fields, 0, 1, out);
}

static ULONGLONG dluma_global_hex_prefix_u64(const char* text)
{
    ULONGLONG value = 0;
    unsigned int i;
    for (i = 0; text && text[i] && i < 16U; ++i) {
        char c = dluma_ascii_lower(text[i]);
        unsigned int digit;
        if (c >= '0' && c <= '9') digit = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = 10U + (unsigned int)(c - 'a');
        else break;
        value = (value << 4) | digit;
    }
    return value;
}

int dluma_lua_prepare_global_identity(DlumaLuaMetadata* item)
{
    char normalized_id[128];
    const char* source_type;
    const char* fields[12];
    const char* key_fields[2];
    if (!item || !item->id[0] ||
        item->scope != DLUMA_LUA_SCOPE_GLOBAL) return 0;
    dluma_global_normalize_text(item->id, normalized_id,
                              DLUMA_ARRAY_COUNT(normalized_id));
    dluma_global_normalize_permissions(
        item->permissions, item->normalized_permissions,
        DLUMA_ARRAY_COUNT(item->normalized_permissions));
    dluma_global_make_relative_path(
        item, item->canonical_relative_path,
        DLUMA_ARRAY_COUNT(item->canonical_relative_path));
    dluma_global_make_source_identity(item);
    if (!item->canonical_relative_path[0] || !item->source_identity[0] ||
        strlen(item->content_sha256) != 64U) {
        item->valid = 0;
        item->review_state = DLUMA_GLOBAL_REVIEW_INVALID;
        return 0;
    }
    fields[0] = item->normalized_permissions;
    dluma_sha256_fields_hex(
        item->manifest_present
            ? "DLW_API_1_GLOBAL_PERMISSION_FP_V2"
            : "DLW_GLOBAL_PERMISSION_FP_V1",
                          fields, 0, 1, item->permission_fingerprint);
    source_type = item->source_is_subscribed ? "subscribed" : "local";
    fields[0] = normalized_id;
    fields[1] = "global";
    fields[2] = item->api_version;
    fields[3] = item->version;
    fields[4] = item->normalized_permissions;
    fields[5] = item->content_sha256;
    fields[6] = item->manifest_sha256;
    fields[7] = item->lock_sha256;
    fields[8] = source_type;
    fields[9] = item->workshop_id;
    fields[10] = item->source_identity;
    fields[11] = item->canonical_relative_path;
    dluma_sha256_fields_hex(
        item->manifest_present
            ? "DLW_API_1_GLOBAL_METADATA_FP_V2"
            : "DLW_GLOBAL_METADATA_FP_V1",
        fields, 0, item->manifest_present ? 12U : 9U,
        item->metadata_fingerprint);
    item->fingerprint =
        dluma_global_hex_prefix_u64(item->metadata_fingerprint);
    key_fields[0] = normalized_id;
    key_fields[1] = item->source_identity;
    dluma_sha256_fields_hex(
        item->manifest_present
            ? "DLW_API_1_GLOBAL_REGISTRY_KEY_V2"
            : "DLW_GLOBAL_REGISTRY_KEY_V1",
                          key_fields, 0, 2, item->registry_key);
    dluma_str_copy(item->storage_key, DLUMA_ARRAY_COUNT(item->storage_key),
                 item->registry_key);
    return 1;
}

int dluma_lua_prepare_local_identity(DlumaLuaMetadata* item)
{
    char normalized_id[128];
    const char* scope;
    const char* fields[7];
    if (!item || !item->id[0] ||
        (item->scope != DLUMA_LUA_SCOPE_PROJECT &&
         item->scope != DLUMA_LUA_SCOPE_MAP))
        return 0;
    dluma_global_normalize_text(item->id, normalized_id,
                              DLUMA_ARRAY_COUNT(normalized_id));
    dluma_global_normalize_permissions(
        item->permissions, item->normalized_permissions,
        DLUMA_ARRAY_COUNT(item->normalized_permissions));
    dluma_global_make_relative_path(
        item, item->canonical_relative_path,
        DLUMA_ARRAY_COUNT(item->canonical_relative_path));
    dluma_global_make_source_identity(item);
    if (!item->canonical_relative_path[0] || !item->source_identity[0] ||
        strlen(item->content_sha256) != 64U)
        return 0;
    scope = item->scope == DLUMA_LUA_SCOPE_PROJECT ? "project" : "map";
    fields[0] = scope;
    fields[1] = item->source_identity;
    fields[2] = item->canonical_relative_path;
    fields[3] = normalized_id;
    fields[4] = item->api_version;
    fields[5] = item->manifest_sha256;
    fields[6] = item->lock_sha256;
    dluma_sha256_fields_hex(
        item->manifest_present
            ? "DLW_API_1_LOCAL_STORAGE_KEY_V2"
            : "DLW_LOCAL_STORAGE_KEY_V1",
        fields, 0, item->manifest_present ? 7U : 4U,
        item->storage_key);
    if (item->manifest_present) {
        dluma_str_copy(
            item->registry_key,
            DLUMA_ARRAY_COUNT(item->registry_key),
            item->storage_key);
        fields[0] = normalized_id;
        fields[1] = scope;
        fields[2] = item->version;
        fields[3] = item->normalized_permissions;
        fields[4] = item->content_sha256;
        fields[5] = item->manifest_sha256;
        fields[6] = item->lock_sha256;
        dluma_sha256_fields_hex(
            "DLW_API_1_LOCAL_METADATA_FP_V2",
            fields, 0, 7U, item->metadata_fingerprint);
        fields[0] = item->normalized_permissions;
        dluma_sha256_fields_hex(
            "DLW_API_1_LOCAL_PERMISSION_FP_V2",
            fields, 0, 1U, item->permission_fingerprint);
    }
    item->fingerprint =
        dluma_global_hex_prefix_u64(item->content_sha256);
    return 1;
}

static int dluma_global_ini_read(const char* section, const char* key,
                               char* out, unsigned int capacity)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR section_w[96];
    WCHAR key_w[192];
    WCHAR value_w[1024];
    DWORD count;
    if (!section || !key || !out || !capacity) return 0;
    out[0] = 0;
    if (!dluma_utf8_to_wide(section, section_w, DLUMA_ARRAY_COUNT(section_w)) ||
        !dluma_utf8_to_wide(key, key_w, DLUMA_ARRAY_COUNT(key_w))) return 0;
    dluma_lua_get_global_registry_ini_path(path, DLUMA_ARRAY_COUNT(path));
    value_w[0] = 0;
    count = GetPrivateProfileStringW(section_w, key_w, L"", value_w,
                                     DLUMA_ARRAY_COUNT(value_w), path);
    if (!count || !value_w[0]) return 0;
    if (!dluma_wide_to_utf8(value_w, out, capacity)) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

static int dluma_global_parse_enabled(const char* value)
{
    return value && value[0] && dluma_stricmp(value, "0") != 0 &&
           dluma_stricmp(value, "false") != 0 &&
           dluma_stricmp(value, "off") != 0 &&
           dluma_stricmp(value, "disabled") != 0;
}

static int dluma_global_permission_contains(const char* set,
                                          const char* permission)
{
    unsigned int cursor = 0;
    unsigned int wanted = (unsigned int)strlen(permission);
    while (set && set[cursor]) {
        unsigned int start = cursor;
        while (set[cursor] && set[cursor] != ',') ++cursor;
        if (cursor - start == wanted &&
            memcmp(set + start, permission, wanted) == 0) return 1;
        if (set[cursor] == ',') ++cursor;
    }
    return 0;
}

static int dluma_global_permissions_expanded(const char* old_set,
                                           const char* new_set)
{
    unsigned int cursor = 0;
    char token[64];
    if (!new_set || !new_set[0]) return 0;
    while (new_set[cursor]) {
        unsigned int length = 0;
        while (new_set[cursor] && new_set[cursor] != ',' &&
               length + 1U < DLUMA_ARRAY_COUNT(token))
            token[length++] = new_set[cursor++];
        while (new_set[cursor] && new_set[cursor] != ',') ++cursor;
        if (new_set[cursor] == ',') ++cursor;
        token[length] = 0;
        if (length && !dluma_global_permission_contains(old_set, token)) return 1;
    }
    return 0;
}

static void dluma_global_append_change_reason(DlumaLuaMetadata* item,
                                            unsigned int flag,
                                            const char* text)
{
    item->change_reason_flags |= flag;
    if (item->change_reason[0])
        dluma_str_append(item->change_reason,
                       DLUMA_ARRAY_COUNT(item->change_reason), ", ");
    dluma_str_append(item->change_reason,
                   DLUMA_ARRAY_COUNT(item->change_reason), text);
}

static void dluma_global_classify_changes(DlumaLuaMetadata* item)
{
    char old_content[DLUMA_SHA256_HEX_CAPACITY];
    char old_version[DLUMA_SHA256_HEX_CAPACITY];
    char old_source[DLUMA_SHA256_HEX_CAPACITY];
    char old_path[DLUMA_SHA256_HEX_CAPACITY];
    char old_permissions[256];
    char current_version[DLUMA_SHA256_HEX_CAPACITY];
    char current_source[DLUMA_SHA256_HEX_CAPACITY];
    char current_path[DLUMA_SHA256_HEX_CAPACITY];
    int had_component = 0;
    item->change_reason_flags = DLUMA_GLOBAL_CHANGE_NONE;
    item->change_reason[0] = 0;
    dluma_global_component_hash("DLW_GLOBAL_VERSION_COMPONENT_V1",
                              item->version, current_version);
    dluma_global_component_hash("DLW_GLOBAL_SOURCE_COMPONENT_V1",
                              item->source_identity, current_source);
    dluma_global_component_hash("DLW_GLOBAL_PATH_COMPONENT_V1",
                              item->canonical_relative_path, current_path);
    old_content[0] = old_version[0] = old_source[0] = old_path[0] = 0;
    old_permissions[0] = 0;
    if (dluma_global_ini_read("global_mod_content", item->registry_key,
                            old_content, DLUMA_ARRAY_COUNT(old_content))) {
        had_component = 1;
        if (dluma_stricmp(old_content, item->content_sha256) != 0)
            dluma_global_append_change_reason(
                item, DLUMA_GLOBAL_CHANGE_CONTENT, "content changed");
    }
    if (dluma_global_ini_read("global_mod_version", item->registry_key,
                            old_version, DLUMA_ARRAY_COUNT(old_version))) {
        had_component = 1;
        if (dluma_stricmp(old_version, current_version) != 0)
            dluma_global_append_change_reason(
                item, DLUMA_GLOBAL_CHANGE_VERSION, "version changed");
    }
    if (item->approved_fingerprint[0] &&
        dluma_global_ini_read("global_mod_permissions", item->registry_key,
                            old_content, DLUMA_ARRAY_COUNT(old_content)) &&
        dluma_stricmp(old_content, item->permission_fingerprint) != 0) {
        dluma_global_append_change_reason(
            item, DLUMA_GLOBAL_CHANGE_PERMISSIONS, "permissions changed");
        if (dluma_global_ini_read("global_mod_permission_sets", item->registry_key,
                                old_permissions,
                                DLUMA_ARRAY_COUNT(old_permissions)) &&
            dluma_global_permissions_expanded(
                old_permissions, item->normalized_permissions))
            dluma_global_append_change_reason(
                item, DLUMA_GLOBAL_CHANGE_PERMISSION_EXPANSION,
                "permission set expanded");
    }
    if (dluma_global_ini_read("global_mod_source", item->registry_key,
                            old_source, DLUMA_ARRAY_COUNT(old_source))) {
        had_component = 1;
        if (dluma_stricmp(old_source, current_source) != 0)
            dluma_global_append_change_reason(
                item, DLUMA_GLOBAL_CHANGE_SOURCE, "source identity changed");
    }
    if (dluma_global_ini_read("global_mod_path", item->registry_key,
                            old_path, DLUMA_ARRAY_COUNT(old_path))) {
        had_component = 1;
        if (dluma_stricmp(old_path, current_path) != 0)
            dluma_global_append_change_reason(
                item, DLUMA_GLOBAL_CHANGE_PATH, "canonical path changed");
    }
    if (!item->change_reason[0] &&
        (item->seen_fingerprint[0] || item->approved_fingerprint[0]) &&
        !had_component)
        dluma_str_copy(item->change_reason,
                     DLUMA_ARRAY_COUNT(item->change_reason),
                     "content, version, permissions, source, or path changed");
}

void dluma_lua_classify_global_review(DlumaLuaMetadata* item,
                                    const DlumaLuaMetadata* previous)
{
    char enabled_text[32];
    char legacy_enabled[32];
    char persisted_permission[DLUMA_SHA256_HEX_CAPACITY];
    char discovered_error[256];
    if (!item) return;
    dluma_str_copy(
        discovered_error, DLUMA_ARRAY_COUNT(discovered_error),
        item->runtime_error);
    if (!g_dluma.lua_discovery.last_prompted_batch_hash[0])
        dluma_global_ini_read(
            "global_mod_ui", "last_prompted_batch",
            g_dluma.lua_discovery.last_prompted_batch_hash,
            DLUMA_ARRAY_COUNT(
                g_dluma.lua_discovery.last_prompted_batch_hash));
    item->enabled = 0;
    item->enabled_requested = 0;
    item->has_enabled_override = 0;
    item->loaded = previous ? previous->loaded : 0;
    item->runtime_transition = previous ? previous->runtime_transition :
        DLUMA_GLOBAL_TRANSITION_IDLE;
    item->runtime_error[0] = 0;
    if (previous && previous->runtime_error[0])
        dluma_str_copy(item->runtime_error,
                     DLUMA_ARRAY_COUNT(item->runtime_error),
                     previous->runtime_error);
    else if (discovered_error[0])
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            discovered_error);
    if (!item->valid || !item->metadata_fingerprint[0]) {
        item->review_state = DLUMA_GLOBAL_REVIEW_INVALID;
        item->pending_review = 1;
        return;
    }
    item->seen_fingerprint[0] = 0;
    item->approved_fingerprint[0] = 0;
    item->prompted_fingerprint[0] = 0;
    persisted_permission[0] = 0;
    dluma_global_ini_read("global_mod_seen", item->registry_key,
                        item->seen_fingerprint,
                        DLUMA_ARRAY_COUNT(item->seen_fingerprint));
    dluma_global_ini_read("global_mod_approved", item->registry_key,
                        item->approved_fingerprint,
                        DLUMA_ARRAY_COUNT(item->approved_fingerprint));
    dluma_global_ini_read("global_mod_prompted", item->registry_key,
                        item->prompted_fingerprint,
                        DLUMA_ARRAY_COUNT(item->prompted_fingerprint));
    dluma_global_ini_read("global_mod_permissions", item->registry_key,
                        persisted_permission,
                        DLUMA_ARRAY_COUNT(persisted_permission));
    enabled_text[0] = legacy_enabled[0] = 0;
    if (dluma_global_ini_read("global_mods", item->registry_key,
                            enabled_text, DLUMA_ARRAY_COUNT(enabled_text))) {
        item->has_enabled_override = 1;
        item->enabled_requested =
            (unsigned char)dluma_global_parse_enabled(enabled_text);
    } else if (dluma_global_ini_read("global_mods", item->id,
                                   legacy_enabled,
                                   DLUMA_ARRAY_COUNT(legacy_enabled))) {
        /* ID-only values migrate only as an explicit disabled/enabled intent.
         * They never create approval for a fingerprint. */
        item->has_enabled_override = 1;
        item->enabled_requested =
            (unsigned char)dluma_global_parse_enabled(legacy_enabled);
    }
    item->is_new = (unsigned char)(
        dluma_stricmp(item->seen_fingerprint,
                    item->metadata_fingerprint) != 0);
    item->approval_valid = (unsigned char)(
        item->approved_fingerprint[0] &&
        dluma_stricmp(item->approved_fingerprint,
                    item->metadata_fingerprint) == 0 &&
        persisted_permission[0] &&
        dluma_stricmp(persisted_permission,
                    item->permission_fingerprint) == 0);
    item->pending_review = (unsigned char)(item->approval_valid ? 0 : 1);
    if (!item->approval_valid) {
        if (item->seen_fingerprint[0] &&
            dluma_stricmp(item->seen_fingerprint,
                        item->metadata_fingerprint) == 0) {
            item->review_state = DLUMA_GLOBAL_REVIEW_SEEN;
        } else if (item->seen_fingerprint[0] ||
                   item->approved_fingerprint[0]) {
            item->review_state = DLUMA_GLOBAL_REVIEW_CHANGED_REVIEW_REQUIRED;
            dluma_global_classify_changes(item);
            if (previous && previous->loaded) {
                item->runtime_transition = DLUMA_GLOBAL_TRANSITION_DISABLING;
                dluma_log_pair("WARN", "global_approval_invalidated", item->id);
            }
        } else {
            item->review_state = item->is_new
                ? DLUMA_GLOBAL_REVIEW_NEW : DLUMA_GLOBAL_REVIEW_SEEN;
        }
        /* Author preference is display-only until exact player approval. */
        item->enabled = 0;
        item->enabled_requested = 0;
    } else if (item->enabled_requested) {
        item->review_state = DLUMA_GLOBAL_REVIEW_APPROVED_ENABLED;
        item->enabled = 1;
    } else {
        item->review_state = DLUMA_GLOBAL_REVIEW_APPROVED_DISABLED;
        item->enabled = 0;
    }
    if (dluma_startup_recovery_suppresses_mods()) {
        item->enabled = 0;
        item->runtime_transition = DLUMA_GLOBAL_TRANSITION_IDLE;
        if (item->enabled_requested)
            dluma_str_copy(item->runtime_error,
                         DLUMA_ARRAY_COUNT(item->runtime_error),
                         "suppressed for this startup-recovery session");
    }
}

static int dluma_ini_buffer_append(char* out, unsigned int capacity,
                                 unsigned int* used,
                                 const char* text, unsigned int length)
{
    if (!out || !used || !text || *used + length + 1U > capacity) return 0;
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = 0;
    return 1;
}

static int dluma_ini_name_equals(const char* line, unsigned int length,
                               const char* expected)
{
    char local[192];
    unsigned int copy = length;
    if (copy >= DLUMA_ARRAY_COUNT(local)) copy = DLUMA_ARRAY_COUNT(local) - 1U;
    memcpy(local, line, copy);
    local[copy] = 0;
    dluma_trim_in_place(local);
    return dluma_stricmp(local, expected) == 0;
}

static int dluma_ini_emit_mutations_for_section(
    char* out, unsigned int capacity, unsigned int* used,
    DlumaIniMutation* mutations, unsigned int mutation_count,
    const char* section)
{
    unsigned int i;
    for (i = 0; i < mutation_count; ++i) {
        if (mutations[i].written ||
            dluma_stricmp(mutations[i].section, section) != 0) continue;
        if (!dluma_ini_buffer_append(out, capacity, used,
                                   mutations[i].key,
                                   (unsigned int)strlen(mutations[i].key)) ||
            !dluma_ini_buffer_append(out, capacity, used, "=", 1) ||
            !dluma_ini_buffer_append(out, capacity, used,
                                   mutations[i].value,
                                   (unsigned int)strlen(mutations[i].value)) ||
            !dluma_ini_buffer_append(out, capacity, used, "\r\n", 2))
            return 0;
        mutations[i].written = 1;
    }
    return 1;
}

static int dluma_ini_rewrite_document(const char* old_text,
                                    unsigned int old_size,
                                    DlumaIniMutation* mutations,
                                    unsigned int mutation_count,
                                    char** output,
                                    DWORD* output_size)
{
    unsigned int capacity;
    unsigned int used = 0;
    unsigned int cursor = 0;
    char current_section[96];
    char* rewritten;
    unsigned int i;
    if (!output || !output_size || mutation_count > DLUMA_GLOBAL_MAX_MUTATIONS)
        return 0;
    capacity = old_size + mutation_count * 1024U + 4096U;
    if (capacity > DLUMA_GLOBAL_STATE_MAX_BYTES) return 0;
    rewritten = (char*)dluma_alloc(capacity);
    if (!rewritten) return 0;
    rewritten[0] = 0;
    current_section[0] = 0;
    while (cursor < old_size) {
        unsigned int line_start = cursor;
        unsigned int content_end;
        unsigned int line_end;
        unsigned int trim_start;
        unsigned int trim_end;
        int is_section = 0;
        int replacement = -1;
        while (cursor < old_size && old_text[cursor] != '\r' &&
               old_text[cursor] != '\n') ++cursor;
        content_end = cursor;
        if (cursor < old_size && old_text[cursor] == '\r') ++cursor;
        if (cursor < old_size && old_text[cursor] == '\n') ++cursor;
        line_end = cursor;
        trim_start = line_start;
        if (trim_start == 0 && content_end - trim_start >= 3U &&
            (unsigned char)old_text[0] == 0xEFU &&
            (unsigned char)old_text[1] == 0xBBU &&
            (unsigned char)old_text[2] == 0xBFU)
            trim_start += 3U;
        while (trim_start < content_end &&
               dluma_is_space(old_text[trim_start])) ++trim_start;
        trim_end = content_end;
        while (trim_end > trim_start &&
               dluma_is_space(old_text[trim_end - 1U])) --trim_end;
        if (trim_end > trim_start + 2U && old_text[trim_start] == '[' &&
            old_text[trim_end - 1U] == ']') {
            unsigned int length = trim_end - trim_start - 2U;
            if (!dluma_ini_emit_mutations_for_section(
                    rewritten, capacity, &used, mutations,
                    mutation_count, current_section)) goto fail;
            if (length >= DLUMA_ARRAY_COUNT(current_section))
                length = DLUMA_ARRAY_COUNT(current_section) - 1U;
            memcpy(current_section, old_text + trim_start + 1U, length);
            current_section[length] = 0;
            dluma_trim_in_place(current_section);
            is_section = 1;
        } else if (current_section[0] && trim_end > trim_start &&
                   old_text[trim_start] != ';' &&
                   old_text[trim_start] != '#') {
            unsigned int equals = trim_start;
            while (equals < trim_end && old_text[equals] != '=') ++equals;
            if (equals < trim_end) {
                unsigned int key_end = equals;
                while (key_end > trim_start &&
                       dluma_is_space(old_text[key_end - 1U])) --key_end;
                for (i = 0; i < mutation_count; ++i) {
                    if (dluma_stricmp(mutations[i].section,
                                    current_section) == 0 &&
                        dluma_ini_name_equals(old_text + trim_start,
                                            key_end - trim_start,
                                            mutations[i].key)) {
                        replacement = (int)i;
                        break;
                    }
                }
            }
        }
        if (replacement >= 0) {
            DlumaIniMutation* mutation = &mutations[(unsigned int)replacement];
            if (!mutation->written) {
                if (!dluma_ini_buffer_append(rewritten, capacity, &used,
                                           mutation->key,
                                           (unsigned int)strlen(mutation->key)) ||
                    !dluma_ini_buffer_append(rewritten, capacity, &used, "=", 1) ||
                    !dluma_ini_buffer_append(rewritten, capacity, &used,
                                           mutation->value,
                                           (unsigned int)strlen(mutation->value)) ||
                    !dluma_ini_buffer_append(rewritten, capacity, &used,
                                           "\r\n", 2)) goto fail;
                mutation->written = 1;
            }
        } else if (!dluma_ini_buffer_append(rewritten, capacity, &used,
                                          old_text + line_start,
                                          line_end - line_start)) goto fail;
        (void)is_section;
    }
    if (!dluma_ini_emit_mutations_for_section(
            rewritten, capacity, &used, mutations,
            mutation_count, current_section)) goto fail;
    for (i = 0; i < mutation_count; ++i) {
        unsigned int j;
        int section_emitted = 0;
        if (mutations[i].written) continue;
        if (used && rewritten[used - 1U] != '\n' &&
            !dluma_ini_buffer_append(rewritten, capacity, &used, "\r\n", 2))
            goto fail;
        for (j = 0; j < i; ++j) {
            if (dluma_stricmp(mutations[j].section,
                            mutations[i].section) == 0 &&
                mutations[j].written) {
                section_emitted = 1;
                break;
            }
        }
        if (!section_emitted) {
            if (used && !dluma_ini_buffer_append(
                    rewritten, capacity, &used, "\r\n", 2)) goto fail;
            if (!dluma_ini_buffer_append(rewritten, capacity, &used, "[", 1) ||
                !dluma_ini_buffer_append(rewritten, capacity, &used,
                                       mutations[i].section,
                                       (unsigned int)strlen(mutations[i].section)) ||
                !dluma_ini_buffer_append(rewritten, capacity, &used, "]\r\n", 3))
                goto fail;
        }
        if (!dluma_ini_emit_mutations_for_section(
                rewritten, capacity, &used, mutations,
                mutation_count, mutations[i].section)) goto fail;
    }
    *output = rewritten;
    *output_size = (DWORD)used;
    return 1;
fail:
    dluma_free(rewritten);
    return 0;
}

static int dluma_global_atomic_apply_mutations(DlumaIniMutation* mutations,
                                             unsigned int mutation_count)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR temp_path[DLUMA_MAX_WPATH];
    char* old_text = 0;
    char* rewritten = 0;
    DWORD old_size = 0;
    DWORD rewritten_size = 0;
    DWORD written = 0;
    HANDLE file;
    HMODULE kernel;
    DlumaMoveFileExWFn move_file;
    DlumaDeleteFileWFn delete_file;
    unsigned int i;
    if (!mutations || !mutation_count ||
        mutation_count > DLUMA_GLOBAL_MAX_MUTATIONS) return 0;
    for (i = 0; i < mutation_count; ++i) {
        if (!mutations[i].section || !mutations[i].section[0] ||
            !mutations[i].key || !mutations[i].key[0] ||
            !mutations[i].value ||
            strlen(mutations[i].section) > 64U ||
            strlen(mutations[i].key) > 128U ||
            strlen(mutations[i].value) > 768U)
            return 0;
        mutations[i].written = 0;
    }
    dluma_lua_get_global_registry_ini_path(path, DLUMA_ARRAY_COUNT(path));
    if (dluma_path_exists(path)) {
        if (!dluma_read_file_utf8(path, &old_text, &old_size)) return 0;
    } else {
        old_text = (char*)dluma_alloc(1);
        if (!old_text) return 0;
        old_text[0] = 0;
    }
    if (!dluma_ini_rewrite_document(old_text, old_size, mutations,
                                  mutation_count, &rewritten,
                                  &rewritten_size)) {
        dluma_free(old_text);
        return 0;
    }
    dluma_free(old_text);
    dluma_wstr_copy(temp_path, DLUMA_ARRAY_COUNT(temp_path), path);
    dluma_wstr_append(temp_path, DLUMA_ARRAY_COUNT(temp_path), L".tmp");
    file = CreateFileW(temp_path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        dluma_free(rewritten);
        return 0;
    }
    if ((rewritten_size &&
         (!WriteFile(file, rewritten, rewritten_size, &written, 0) ||
          written != rewritten_size)) || !FlushFileBuffers(file)) {
        CloseHandle(file);
        dluma_free(rewritten);
        return 0;
    }
    CloseHandle(file);
    dluma_free(rewritten);
    kernel = GetModuleHandleW(L"kernel32.dll");
    move_file = kernel ? (DlumaMoveFileExWFn)GetProcAddress(
        kernel, "MoveFileExW") : 0;
    delete_file = kernel ? (DlumaDeleteFileWFn)GetProcAddress(
        kernel, "DeleteFileW") : 0;
    if (!move_file || !move_file(
            temp_path, path,
            DLUMA_MOVEFILE_REPLACE_EXISTING | DLUMA_MOVEFILE_WRITE_THROUGH)) {
        if (delete_file) delete_file(temp_path);
        return 0;
    }
    return 1;
}

static void dluma_global_build_component_values(
    const DlumaLuaMetadata* item,
    char version_hash[DLUMA_SHA256_HEX_CAPACITY],
    char source_hash[DLUMA_SHA256_HEX_CAPACITY],
    char path_hash[DLUMA_SHA256_HEX_CAPACITY])
{
    dluma_global_component_hash("DLW_GLOBAL_VERSION_COMPONENT_V1",
                              item->version, version_hash);
    dluma_global_component_hash("DLW_GLOBAL_SOURCE_COMPONENT_V1",
                              item->source_identity, source_hash);
    dluma_global_component_hash("DLW_GLOBAL_PATH_COMPONENT_V1",
                              item->canonical_relative_path, path_hash);
}

static unsigned int dluma_global_fill_review_mutations(
    DlumaIniMutation* mutations, const DlumaLuaMetadata* item,
    int include_seen, int include_approval, int include_enabled,
    const char* enabled_value)
{
    static char version_hash[DLUMA_SHA256_HEX_CAPACITY];
    static char source_hash[DLUMA_SHA256_HEX_CAPACITY];
    static char path_hash[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int count = 0;
    dluma_global_build_component_values(item, version_hash, source_hash,
                                      path_hash);
#define DLUMA_ADD_MUTATION(section_, value_) do { \
    mutations[count].section = section_; \
    mutations[count].key = item->registry_key; \
    mutations[count].value = value_; \
    mutations[count].written = 0; \
    ++count; \
} while (0)
    if (include_seen)
        DLUMA_ADD_MUTATION("global_mod_seen", item->metadata_fingerprint);
    if (include_approval) {
        DLUMA_ADD_MUTATION("global_mod_approved", item->metadata_fingerprint);
        DLUMA_ADD_MUTATION("global_mod_permissions", item->permission_fingerprint);
    }
    if (include_enabled)
        DLUMA_ADD_MUTATION("global_mods", enabled_value);
    if (include_seen || include_approval) {
        DLUMA_ADD_MUTATION("global_mod_content", item->content_sha256);
        DLUMA_ADD_MUTATION("global_mod_version", version_hash);
        DLUMA_ADD_MUTATION("global_mod_source", source_hash);
        DLUMA_ADD_MUTATION("global_mod_path", path_hash);
        DLUMA_ADD_MUTATION("global_mod_permission_sets",
                         item->normalized_permissions);
    }
#undef DLUMA_ADD_MUTATION
    return count;
}

static int dluma_global_find_exact_locked(
    const DlumaGlobalMutationRequest* request, unsigned int* index_out)
{
    unsigned int i;
    if (!request || !request->registry_key[0] ||
        !request->metadata_fingerprint[0] ||
        request->registry_revision !=
            g_dluma.lua_discovery.global_registry_revision)
        return 0;
    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (dluma_stricmp(item->registry_key, request->registry_key) == 0 &&
            dluma_stricmp(item->metadata_fingerprint,
                        request->metadata_fingerprint) == 0) {
            if (index_out) *index_out = i;
            return 1;
        }
    }
    return 0;
}

static int dluma_global_queue_command(const DlumaLuaMetadata* item,
                                    unsigned char type,
                                    ULONGLONG revision)
{
    unsigned int next;
    DlumaGlobalRuntimeCommand* command;
    if (!item || type == DLUMA_GLOBAL_RUNTIME_NONE) return 0;
    dluma_global_runtime_queue_lock();
    next = (g_dluma.lua_discovery.global_runtime_tail + 1U) %
        DLUMA_MAX_GLOBAL_RUNTIME_COMMANDS;
    if (next == g_dluma.lua_discovery.global_runtime_head) {
        dluma_global_runtime_queue_unlock();
        dluma_log_line("ERROR", "Global Lua runtime command queue is full; request remains persisted but no unsafe inline state operation was attempted.");
        return 0;
    }
    command = &g_dluma.lua_discovery.global_runtime_commands[
        g_dluma.lua_discovery.global_runtime_tail];
    memset(command, 0, sizeof(*command));
    dluma_str_copy(command->registry_key,
                 DLUMA_ARRAY_COUNT(command->registry_key), item->registry_key);
    dluma_str_copy(command->metadata_fingerprint,
                 DLUMA_ARRAY_COUNT(command->metadata_fingerprint),
                 item->metadata_fingerprint);
    command->registry_revision = revision;
    command->type = type;
    g_dluma.lua_discovery.global_runtime_tail = next;
    dluma_global_runtime_queue_unlock();
    dluma_log_pair("INFO", type == DLUMA_GLOBAL_RUNTIME_LOAD
        ? "global_state_load_queued" : "global_state_unload_queued", item->id);
    return 1;
}

void dluma_lua_recompute_unseen_batch_locked(void)
{
    char fingerprints[DLUMA_MAX_LUA_FILES][DLUMA_SHA256_HEX_CAPACITY];
    const char* fields[DLUMA_MAX_LUA_FILES];
    unsigned int count = 0;
    unsigned int i, j;
    int should_prompt = 0;
    memset(fingerprints, 0, sizeof(fingerprints));
    for (i = 0; i < g_dluma.lua_discovery.global_count &&
                count < DLUMA_MAX_LUA_FILES; ++i) {
        const DlumaLuaMetadata* item =
            &g_dluma.lua_discovery.global_files[i];
        if (!item->valid || item->duplicate_id || !item->is_new ||
            !item->metadata_fingerprint[0]) continue;
        dluma_str_copy(fingerprints[count],
                     DLUMA_ARRAY_COUNT(fingerprints[count]),
                     item->metadata_fingerprint);
        if (dluma_stricmp(item->prompted_fingerprint,
                        item->metadata_fingerprint) != 0)
            should_prompt = 1;
        ++count;
    }
    for (i = 1; i < count; ++i) {
        char value[DLUMA_SHA256_HEX_CAPACITY];
        dluma_str_copy(value, DLUMA_ARRAY_COUNT(value), fingerprints[i]);
        j = i;
        while (j > 0 && dluma_stricmp(fingerprints[j - 1], value) > 0) {
            dluma_str_copy(fingerprints[j],
                         DLUMA_ARRAY_COUNT(fingerprints[j]),
                         fingerprints[j - 1]);
            --j;
        }
        dluma_str_copy(fingerprints[j],
                     DLUMA_ARRAY_COUNT(fingerprints[j]), value);
    }
    for (i = 0; i < count; ++i) fields[i] = fingerprints[i];
    if (count)
        dluma_sha256_fields_hex("DLW_GLOBAL_UNSEEN_BATCH_V1",
                              fields, 0, count,
                              g_dluma.lua_discovery.current_unseen_batch_hash);
    else
        g_dluma.lua_discovery.current_unseen_batch_hash[0] = 0;
    g_dluma.lua_discovery.current_unseen_count = count;
    g_dluma.lua_discovery.current_batch_should_prompt =
        (unsigned char)(count && should_prompt &&
        dluma_stricmp(g_dluma.lua_discovery.current_unseen_batch_hash,
                    g_dluma.lua_discovery.last_prompted_batch_hash) != 0);
}

int dluma_lua_snapshot_unseen_batch(DlumaGlobalUnseenBatch* out)
{
    unsigned int i;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    dluma_lua_registry_lock();
    dluma_str_copy(out->batch_fingerprint,
                 DLUMA_ARRAY_COUNT(out->batch_fingerprint),
                 g_dluma.lua_discovery.current_unseen_batch_hash);
    out->unseen_count = g_dluma.lua_discovery.current_unseen_count;
    out->should_prompt = g_dluma.lua_discovery.current_batch_should_prompt;
    out->registry_revision = g_dluma.lua_discovery.global_registry_revision;
    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        const DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (item->valid && !item->duplicate_id && item->is_new) {
            dluma_str_copy(out->first_name, DLUMA_ARRAY_COUNT(out->first_name),
                         item->name);
            break;
        }
    }
    dluma_lua_registry_unlock();
    return out->unseen_count != 0;
}

int dluma_lua_mark_unseen_batch_prompted(const char* expected_batch,
                                       ULONGLONG expected_revision)
{
    DlumaIniMutation* mutations;
    DlumaLuaMetadata* snapshots;
    unsigned int count = 0;
    unsigned int i;
    int persisted;
    if (!expected_batch || !expected_batch[0]) return 0;
    mutations = (DlumaIniMutation*)dluma_alloc(
        sizeof(DlumaIniMutation) * DLUMA_GLOBAL_MAX_MUTATIONS);
    snapshots = (DlumaLuaMetadata*)dluma_alloc(
        sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
    if (!mutations || !snapshots) {
        if (mutations) dluma_free(mutations);
        if (snapshots) dluma_free(snapshots);
        return 0;
    }
    memset(mutations, 0, sizeof(DlumaIniMutation) * DLUMA_GLOBAL_MAX_MUTATIONS);
    memset(snapshots, 0, sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
    dluma_global_persistence_lock();
    dluma_lua_registry_lock();
    if (expected_revision != g_dluma.lua_discovery.global_registry_revision ||
        dluma_stricmp(expected_batch,
                    g_dluma.lua_discovery.current_unseen_batch_hash) != 0) {
        dluma_lua_registry_unlock();
        dluma_global_persistence_unlock();
        dluma_free(mutations);
        dluma_free(snapshots);
        return 0;
    }
    for (i = 0; i < g_dluma.lua_discovery.global_count &&
                count < DLUMA_MAX_LUA_FILES; ++i) {
        const DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (!item->valid || item->duplicate_id || !item->is_new) continue;
        snapshots[count] = *item;
        mutations[count].section = "global_mod_prompted";
        mutations[count].key = snapshots[count].registry_key;
        mutations[count].value = snapshots[count].metadata_fingerprint;
        ++count;
    }
    mutations[count].section = "global_mod_ui";
    mutations[count].key = "last_prompted_batch";
    mutations[count].value = expected_batch;
    ++count;
    dluma_lua_registry_unlock();
    persisted = dluma_global_atomic_apply_mutations(mutations, count);
    if (persisted) {
        dluma_lua_registry_lock();
        if (expected_revision == g_dluma.lua_discovery.global_registry_revision &&
            dluma_stricmp(expected_batch,
                        g_dluma.lua_discovery.current_unseen_batch_hash) == 0) {
            for (i = 0; i + 1U < count; ++i) {
                unsigned int j;
                for (j = 0; j < g_dluma.lua_discovery.global_count; ++j) {
                    DlumaLuaMetadata* item =
                        &g_dluma.lua_discovery.global_files[j];
                    if (dluma_stricmp(item->registry_key,
                                    snapshots[i].registry_key) == 0 &&
                        dluma_stricmp(item->metadata_fingerprint,
                                    snapshots[i].metadata_fingerprint) == 0)
                        dluma_str_copy(item->prompted_fingerprint,
                                     DLUMA_ARRAY_COUNT(item->prompted_fingerprint),
                                     item->metadata_fingerprint);
                }
            }
            dluma_str_copy(g_dluma.lua_discovery.last_prompted_batch_hash,
                         DLUMA_ARRAY_COUNT(g_dluma.lua_discovery.last_prompted_batch_hash),
                         expected_batch);
            ++g_dluma.lua_discovery.global_registry_revision;
            dluma_lua_recompute_unseen_batch_locked();
            dluma_lua_rehash_globals_locked();
        } else persisted = 0;
        dluma_lua_registry_unlock();
    }
    dluma_global_persistence_unlock();
    dluma_free(mutations);
    dluma_free(snapshots);
    return persisted;
}

int dluma_lua_mark_global_seen(const DlumaGlobalMutationRequest* request)
{
    DlumaLuaMetadata snapshot;
    DlumaIniMutation mutations[10];
    unsigned int index;
    unsigned int count;
    int persisted;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(mutations, 0, sizeof(mutations));
    dluma_global_persistence_lock();
    dluma_lua_registry_lock();
    if (!dluma_global_find_exact_locked(request, &index) ||
        g_dluma.lua_discovery.global_files[index].duplicate_id) {
        dluma_lua_registry_unlock();
        dluma_global_persistence_unlock();
        return 0;
    }
    snapshot = g_dluma.lua_discovery.global_files[index];
    dluma_lua_registry_unlock();
    count = dluma_global_fill_review_mutations(
        mutations, &snapshot, 1, 0, 0, "0");
    persisted = dluma_global_atomic_apply_mutations(mutations, count);
    if (persisted) {
        dluma_lua_registry_lock();
        if (dluma_global_find_exact_locked(request, &index)) {
            DlumaLuaMetadata* item =
                &g_dluma.lua_discovery.global_files[index];
            dluma_str_copy(item->seen_fingerprint,
                         DLUMA_ARRAY_COUNT(item->seen_fingerprint),
                         item->metadata_fingerprint);
            item->is_new = 0;
            if (!item->approval_valid)
                item->review_state = DLUMA_GLOBAL_REVIEW_SEEN;
            ++g_dluma.lua_discovery.global_registry_revision;
            dluma_lua_recompute_unseen_batch_locked();
            dluma_lua_rehash_globals_locked();
            dluma_log_pair("INFO", "new_global_details_marked_seen", item->id);
        } else persisted = 0;
        dluma_lua_registry_unlock();
    }
    dluma_global_persistence_unlock();
    return persisted;
}

int dluma_lua_permission_is_sensitive(const DlumaLuaMetadata* item)
{
    if (!item) return 0;
    return dluma_global_permission_contains(item->normalized_permissions,
                                          "world.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "world.transform") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "player.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "model.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "model.animate") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "map.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "camera.effect") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "ai.spawn") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "cinematic.control") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "inventory.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "quest.write") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "quest.phase.handle") ||
           dluma_global_permission_contains(item->normalized_permissions,
                                          "world.global_state");
}

int dluma_lua_permission_is_unsupported(const DlumaLuaMetadata* item)
{
    static const char* supported[] = {
        "game.read", "player.read", "world.query", "events.sensor",
        "events.interact", "events.enemy_killed", "storage.mod",
        "ui.debug", "ui.overlay", "audio.play", "world.write",
        "quest.write", "quest.phase.handle", "quest.phase.observe"
    };
    unsigned int cursor = 0;
    if (!item) return 1;
    while (item->normalized_permissions[cursor]) {
        char token[64];
        unsigned int length = 0;
        unsigned int i;
        int known = 0;
        while (item->normalized_permissions[cursor] &&
               item->normalized_permissions[cursor] != ',' &&
               length + 1U < DLUMA_ARRAY_COUNT(token))
            token[length++] = item->normalized_permissions[cursor++];
        while (item->normalized_permissions[cursor] &&
               item->normalized_permissions[cursor] != ',') ++cursor;
        if (item->normalized_permissions[cursor] == ',') ++cursor;
        token[length] = 0;
        for (i = 0; i < DLUMA_ARRAY_COUNT(supported); ++i) {
            if (dluma_stricmp(token, supported[i]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) return 1;
    }
    return 0;
}

int dluma_lua_request_global_enabled(const DlumaGlobalMutationRequest* request,
                                   int enabled, int approve_current)
{
    DlumaLuaMetadata snapshot;
    DlumaIniMutation mutations[12];
    unsigned int index;
    unsigned int count;
    int persisted;
    ULONGLONG new_revision = 0;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(mutations, 0, sizeof(mutations));
    dluma_global_persistence_lock();
    dluma_lua_registry_lock();
    if (!dluma_global_find_exact_locked(request, &index) ||
        g_dluma.lua_discovery.global_files[index].duplicate_id ||
        !g_dluma.lua_discovery.global_files[index].source_selected ||
        !g_dluma.lua_discovery.global_files[index].valid) {
        dluma_lua_registry_unlock();
        dluma_global_persistence_unlock();
        return 0;
    }
    snapshot = g_dluma.lua_discovery.global_files[index];
    if (enabled && !snapshot.approval_valid && !approve_current) {
        dluma_lua_registry_unlock();
        dluma_global_persistence_unlock();
        return 0;
    }
    if (enabled && dluma_lua_permission_is_unsupported(&snapshot)) {
        dluma_lua_registry_unlock();
        dluma_global_persistence_unlock();
        return 0;
    }
    dluma_lua_registry_unlock();
    count = dluma_global_fill_review_mutations(
        mutations, &snapshot,
        approve_current ? 1 : 0,
        approve_current ? 1 : 0,
        1, enabled ? "1" : "0");
    persisted = dluma_global_atomic_apply_mutations(mutations, count);
    if (persisted) {
        dluma_lua_registry_lock();
        if (dluma_global_find_exact_locked(request, &index)) {
            DlumaLuaMetadata* item =
                &g_dluma.lua_discovery.global_files[index];
            if (approve_current) {
                dluma_str_copy(item->approved_fingerprint,
                             DLUMA_ARRAY_COUNT(item->approved_fingerprint),
                             item->metadata_fingerprint);
                dluma_str_copy(item->seen_fingerprint,
                             DLUMA_ARRAY_COUNT(item->seen_fingerprint),
                             item->metadata_fingerprint);
                item->approval_valid = 1;
                item->pending_review = 0;
                item->is_new = 0;
            }
            item->enabled_requested = (unsigned char)(enabled ? 1 : 0);
            item->enabled = (unsigned char)(enabled && item->approval_valid);
            item->has_enabled_override = 1;
            item->runtime_error[0] = 0;
            item->runtime_transition = enabled
                ? DLUMA_GLOBAL_TRANSITION_ENABLING
                : DLUMA_GLOBAL_TRANSITION_DISABLING;
            item->review_state = enabled
                ? DLUMA_GLOBAL_REVIEW_APPROVED_ENABLED
                : DLUMA_GLOBAL_REVIEW_APPROVED_DISABLED;
            ++g_dluma.lua_discovery.global_registry_revision;
            new_revision = g_dluma.lua_discovery.global_registry_revision;
            dluma_lua_recompute_unseen_batch_locked();
            dluma_lua_rehash_globals_locked();
            snapshot = *item;
        } else persisted = 0;
        dluma_lua_registry_unlock();
    }
    dluma_global_persistence_unlock();
    if (persisted)
        dluma_global_queue_command(
            &snapshot, enabled ? DLUMA_GLOBAL_RUNTIME_LOAD
                               : DLUMA_GLOBAL_RUNTIME_UNLOAD,
            new_revision);
    return persisted;
}

int dluma_lua_request_global_source(
    const char* declared_id, const char* source_identity,
    ULONGLONG registry_revision)
{
    DlumaLuaMetadata* snapshot;
    unsigned int count;
    unsigned int index;
    int target = -1;
    ULONGLONG revision;
    if (!declared_id || !declared_id[0] ||
        !source_identity || !source_identity[0])
        return 0;
    dluma_lua_registry_lock();
    if (registry_revision != g_dluma.lua_discovery.global_registry_revision) {
        dluma_lua_registry_unlock();
        return 0;
    }
    for (index = 0; index < g_dluma.lua_discovery.global_count; ++index) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[index];
        /* Resolving which copy provides a declared ID is independent of that
         * copy's validity. Enabling, approval, and load still require
         * `valid`, so a selection can never promote an invalid script. */
        if (dluma_stricmp(item->id, declared_id) == 0 &&
            dluma_stricmp(item->source_identity, source_identity) == 0 &&
            !item->duplicate_id) {
            target = (int)index;
            break;
        }
    }
    dluma_lua_registry_unlock();
    if (target < 0 ||
        !dluma_mod_source_selection_write("lua", declared_id,
                                        source_identity))
        return 0;
    dluma_lua_registry_lock();
    if (registry_revision != g_dluma.lua_discovery.global_registry_revision ||
        (unsigned int)target >= g_dluma.lua_discovery.global_count ||
        dluma_stricmp(g_dluma.lua_discovery.global_files[target].id,
                    declared_id) != 0 ||
        dluma_stricmp(g_dluma.lua_discovery.global_files[target].source_identity,
                    source_identity) != 0) {
        dluma_lua_registry_unlock();
        return 0;
    }
    for (index = 0; index < g_dluma.lua_discovery.global_count; ++index) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[index];
        if (dluma_stricmp(item->id, declared_id) != 0) continue;
        item->source_selected = (unsigned char)
            ((int)index == target ? 1 : 0);
        item->source_selection_missing = 0;
        item->enabled = (unsigned char)
            (item->source_selected && item->enabled_requested &&
             item->approval_valid && !item->duplicate_id ? 1 : 0);
    }
    ++g_dluma.lua_discovery.global_registry_revision;
    revision = g_dluma.lua_discovery.global_registry_revision;
    dluma_lua_rehash_globals_locked();
    count = g_dluma.lua_discovery.global_count;
    snapshot = (DlumaLuaMetadata*)dluma_alloc(
        sizeof(DlumaLuaMetadata) * (count ? count : 1U));
    if (snapshot)
        for (index = 0; index < count; ++index)
            snapshot[index] = g_dluma.lua_discovery.global_files[index];
    dluma_lua_registry_unlock();
    if (!snapshot) return 0;
    dluma_lua_runtime_publish_globals(snapshot, count, revision);
    dluma_free(snapshot);
    dluma_log_pair("INFO", "global_mod_source_selected", source_identity);
    return 1;
}

void dluma_lua_process_global_runtime_queue(void)
{
    DlumaGlobalRuntimeCommand command;
    int have_command = 0;
    unsigned int i;
    memset(&command, 0, sizeof(command));
    dluma_global_runtime_queue_lock();
    if (g_dluma.lua_discovery.global_runtime_head !=
        g_dluma.lua_discovery.global_runtime_tail) {
        command = g_dluma.lua_discovery.global_runtime_commands[
            g_dluma.lua_discovery.global_runtime_head];
        g_dluma.lua_discovery.global_runtime_head =
            (g_dluma.lua_discovery.global_runtime_head + 1U) %
                DLUMA_MAX_GLOBAL_RUNTIME_COMMANDS;
        have_command = 1;
    }
    dluma_global_runtime_queue_unlock();
    if (!have_command) return;
    dluma_lua_registry_lock();
    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (dluma_stricmp(item->registry_key, command.registry_key) != 0 ||
            dluma_stricmp(item->metadata_fingerprint,
                        command.metadata_fingerprint) != 0)
            continue;
        if (command.type == DLUMA_GLOBAL_RUNTIME_UNLOAD) {
            /* Keep DISABLING visible until the game-thread runtime confirms
             * that the matching state no longer exists. */
            item->runtime_transition = DLUMA_GLOBAL_TRANSITION_DISABLING;
            item->runtime_error[0] = 0;
        } else if (item->source_selected && item->approval_valid &&
                   item->enabled_requested &&
                   !item->duplicate_id) {
            /* The UI only persists intent and queues this immutable identity.
             * The Lua game-thread pump consumes the registry snapshot and
             * performs state creation outside the button callback. */
            item->loaded = 0;
            item->runtime_transition = DLUMA_GLOBAL_TRANSITION_ENABLING;
            item->runtime_error[0] = 0;
        }
        ++g_dluma.lua_discovery.global_registry_revision;
        dluma_lua_rehash_globals_locked();
        break;
    }
    dluma_lua_registry_unlock();
    {
        DlumaLuaMetadata* snapshot = (DlumaLuaMetadata*)dluma_alloc(
            sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
        if (snapshot) {
            ULONGLONG revision = 0;
            unsigned int count = dluma_lua_snapshot_globals_with_revision(
                snapshot, DLUMA_MAX_LUA_FILES, 0, &revision);
            if (count > DLUMA_MAX_LUA_FILES) count = DLUMA_MAX_LUA_FILES;
            dluma_lua_runtime_publish_globals(snapshot, count, revision);
            dluma_free(snapshot);
        }
    }
    if (command.type == DLUMA_GLOBAL_RUNTIME_LOAD &&
        !dluma_lua_runtime_is_available())
        dluma_lua_update_global_runtime_status(
            command.registry_key,
            command.metadata_fingerprint,
            0,
            "Private Lua runtime unavailable; the enabled request is saved but no state was created.");
}

void dluma_lua_update_global_runtime_status(
    const char* registry_key,
    const char* metadata_fingerprint,
    int loaded,
    const char* error_text)
{
    unsigned int i;
    if (!registry_key || !metadata_fingerprint) return;
    dluma_lua_registry_lock();
    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (dluma_stricmp(item->registry_key, registry_key) != 0 ||
            dluma_stricmp(item->metadata_fingerprint, metadata_fingerprint) != 0)
            continue;
        item->loaded = (unsigned char)(loaded ? 1 : 0);
        item->runtime_transition = error_text && error_text[0]
            ? DLUMA_GLOBAL_TRANSITION_ERROR : DLUMA_GLOBAL_TRANSITION_IDLE;
        dluma_str_copy(item->runtime_error,
                     DLUMA_ARRAY_COUNT(item->runtime_error),
                     error_text ? error_text : "");
        ++g_dluma.lua_discovery.global_registry_revision;
        dluma_lua_rehash_globals_locked();
        break;
    }
    dluma_lua_registry_unlock();
}
