#include "dlw_lua_config.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"

#ifndef CREATE_NEW
#define CREATE_NEW 1UL
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183UL
#endif

#define DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE 20U
#define DLUMA_LUA_CONFIG_RECORD_MAX_SIZE \
    (DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE + \
     DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY + \
     DLUMA_LUA_CONFIG_VALUE_CAPACITY)

typedef struct DlumaLuaConfigRegistrySlot {
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    DlumaLuaConfigSchema *schema;
    unsigned char occupied;
    unsigned char owner_kind;
    unsigned char reserved[2];
} DlumaLuaConfigRegistrySlot;

static DlumaLuaConfigRegistrySlot
    g_dluma_lua_config_registry[DLUMA_MOD_CONFIG_MAX_REGISTRY];
static volatile LONG g_dluma_lua_config_registry_lock;
static volatile LONG g_dluma_lua_config_storage_lock;

typedef struct DlumaLuaConfigDirectoryGuard {
    HANDLE anchor;
    HANDLE root;
    HANDLE directory;
    WCHAR final_anchor[DLUMA_MAX_WPATH];
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR final_directory[DLUMA_MAX_WPATH];
} DlumaLuaConfigDirectoryGuard;

static void dluma_lua_config_lock(volatile LONG *lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_lua_config_unlock(volatile LONG *lock)
{
    dluma_atomic_exchange(lock, 0);
}

static void dluma_lua_config_error(
    char *out, unsigned int capacity, const char *value)
{
    if (!out || !capacity) return;
    dluma_str_copy(out, capacity, value ? value : "config_failed");
}

static int dluma_lua_config_text_equal(
    const char *left, const char *right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_lua_config_hex_key_valid(const char *value)
{
    unsigned int i;
    if (!value) return 0;
    for (i = 0; i < 64U; ++i) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return value[64] == 0;
}

static int dluma_lua_config_device_name(const char *value)
{
    char base[16];
    unsigned int i = 0;
    if (!value) return 0;
    while (value[i] && value[i] != '.' && i + 1U < DLUMA_ARRAY_COUNT(base)) {
        base[i] = value[i];
        ++i;
    }
    base[i] = 0;
    if (dluma_stricmp(base, "CON") == 0 ||
        dluma_stricmp(base, "PRN") == 0 ||
        dluma_stricmp(base, "AUX") == 0 ||
        dluma_stricmp(base, "NUL") == 0)
        return 1;
    if (i == 4U &&
        ((dluma_ascii_lower(base[0]) == 'c' &&
          dluma_ascii_lower(base[1]) == 'o' &&
          dluma_ascii_lower(base[2]) == 'm') ||
         (dluma_ascii_lower(base[0]) == 'l' &&
          dluma_ascii_lower(base[1]) == 'p' &&
          dluma_ascii_lower(base[2]) == 't')) &&
        base[3] >= '1' && base[3] <= '9')
        return 1;
    return 0;
}

int dluma_lua_config_identifier_valid(const char *value)
{
    unsigned int i;
    if (!value || !value[0]) return 0;
    for (i = 0; value[i] && i < 64U; ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.'))
            return 0;
    }
    if (!i || i > 64U || value[i] ||
        (i == 1U && value[0] == '.') ||
        (i == 2U && value[0] == '.' && value[1] == '.') ||
        value[i - 1U] == '.' ||
        dluma_lua_config_device_name(value))
        return 0;
    return 1;
}

int dluma_lua_config_utf8_valid(
    const char *value,
    unsigned int maximum_bytes,
    unsigned int maximum_characters,
    int allow_empty)
{
    const unsigned char *cursor = (const unsigned char *)value;
    unsigned int bytes;
    unsigned int index = 0;
    unsigned int characters = 0;
    if (!value) return 0;
    bytes = (unsigned int)strlen(value);
    if ((!allow_empty && !bytes) || bytes > maximum_bytes)
        return 0;
    while (index < bytes) {
        unsigned char first = cursor[index];
        unsigned int width;
        if (first <= 0x7FU) {
            if (first < 0x20U || first == 0x7FU) return 0;
            width = 1U;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            width = 2U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xC0U) != 0x80U)
                return 0;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            width = 3U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xC0U) != 0x80U ||
                (cursor[index + 2U] & 0xC0U) != 0x80U ||
                (first == 0xE0U && cursor[index + 1U] < 0xA0U) ||
                (first == 0xEDU && cursor[index + 1U] >= 0xA0U))
                return 0;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            width = 4U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xC0U) != 0x80U ||
                (cursor[index + 2U] & 0xC0U) != 0x80U ||
                (cursor[index + 3U] & 0xC0U) != 0x80U ||
                (first == 0xF0U && cursor[index + 1U] < 0x90U) ||
                (first == 0xF4U && cursor[index + 1U] >= 0x90U))
                return 0;
        } else {
            return 0;
        }
        index += width;
        ++characters;
        if (characters > maximum_characters) return 0;
    }
    return 1;
}

static void dluma_lua_config_hash_u32(
    DlumaSha256Context *context, unsigned int value)
{
    BYTE bytes[4];
    bytes[0] = (BYTE)(value & 0xFFU);
    bytes[1] = (BYTE)((value >> 8) & 0xFFU);
    bytes[2] = (BYTE)((value >> 16) & 0xFFU);
    bytes[3] = (BYTE)((value >> 24) & 0xFFU);
    dluma_sha256_update(context, bytes, sizeof(bytes));
}

static void dluma_lua_config_hash_i32(
    DlumaSha256Context *context, int value)
{
    dluma_lua_config_hash_u32(context, (unsigned int)value);
}

static void dluma_lua_config_hash_text(
    DlumaSha256Context *context, const char *value)
{
    unsigned int length = value ? (unsigned int)strlen(value) : 0U;
    dluma_lua_config_hash_u32(context, length);
    if (length) dluma_sha256_update(context, value, length);
}

void dluma_lua_config_compute_fingerprint(DlumaLuaConfigSchema *schema)
{
    static const char domain[] = "DLW_LUA_CONFIG_SCHEMA_V1";
    DlumaSha256Context context;
    BYTE digest[32];
    unsigned int i;
    if (!schema) return;
    dluma_sha256_init(&context);
    dluma_lua_config_hash_text(&context, domain);
    dluma_lua_config_hash_u32(&context, schema->schema_version);
    dluma_lua_config_hash_u32(&context, schema->category_count);
    dluma_lua_config_hash_u32(&context, schema->setting_count);
    for (i = 0; i < schema->category_count; ++i) {
        const DlumaLuaConfigCategory *category = &schema->categories[i];
        dluma_lua_config_hash_text(&context, category->id);
        dluma_lua_config_hash_text(&context, category->name);
        dluma_lua_config_hash_u32(&context, category->first_setting);
        dluma_lua_config_hash_u32(&context, category->setting_count);
    }
    for (i = 0; i < schema->setting_count; ++i) {
        const DlumaLuaConfigSetting *setting = &schema->settings[i];
        unsigned int option_index;
        dluma_lua_config_hash_text(&context, setting->key);
        dluma_lua_config_hash_text(&context, setting->name);
        dluma_lua_config_hash_text(&context, setting->description);
        dluma_lua_config_hash_u32(&context, setting->kind);
        if (setting->editor != DLUMA_LUA_CONFIG_EDITOR_DEFAULT)
            dluma_lua_config_hash_u32(&context, setting->editor);
        dluma_lua_config_hash_i32(&context, setting->default_integer);
        dluma_lua_config_hash_i32(&context, setting->minimum);
        dluma_lua_config_hash_i32(&context, setting->maximum);
        dluma_lua_config_hash_u32(&context, setting->step);
        dluma_lua_config_hash_u32(&context, setting->max_length);
        dluma_lua_config_hash_text(&context, setting->default_text);
        dluma_lua_config_hash_u32(&context, setting->option_count);
        for (option_index = 0;
             option_index < setting->option_count;
             ++option_index) {
            dluma_lua_config_hash_text(
                &context, setting->options[option_index].value);
            dluma_lua_config_hash_text(
                &context, setting->options[option_index].name);
        }
    }
    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, schema->fingerprint);
}

void dluma_mod_config_identity_from_lua(
    const DlumaLuaMetadata *metadata,
    DlumaModConfigIdentity *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!metadata) return;
    dluma_str_copy(
        out->registry_key, DLUMA_ARRAY_COUNT(out->registry_key),
        metadata->registry_key);
    dluma_str_copy(
        out->storage_key, DLUMA_ARRAY_COUNT(out->storage_key),
        metadata->storage_key);
    dluma_str_copy(
        out->metadata_fingerprint,
        DLUMA_ARRAY_COUNT(out->metadata_fingerprint),
        metadata->metadata_fingerprint);
    dluma_str_copy(
        out->config_schema_fingerprint,
        DLUMA_ARRAY_COUNT(out->config_schema_fingerprint),
        metadata->config_schema_fingerprint);
    out->config_available = metadata->config_available;
    out->owner_kind = DLUMA_MOD_CONFIG_OWNER_LUA;
}

static int dluma_mod_config_identity_valid(
    const DlumaModConfigIdentity *identity)
{
    return identity &&
           (identity->owner_kind == DLUMA_MOD_CONFIG_OWNER_LUA ||
            identity->owner_kind == DLUMA_MOD_CONFIG_OWNER_NATIVE) &&
           identity->config_available &&
           dluma_lua_config_hex_key_valid(identity->registry_key) &&
           dluma_lua_config_hex_key_valid(identity->storage_key) &&
           dluma_lua_config_hex_key_valid(
               identity->metadata_fingerprint) &&
           dluma_lua_config_hex_key_valid(
               identity->config_schema_fingerprint);
}

int dluma_mod_config_publish(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema)
{
    DlumaLuaConfigSchema *copy;
    DlumaLuaConfigSchema *old = 0;
    unsigned int index;
    unsigned int free_index = DLUMA_MOD_CONFIG_MAX_REGISTRY;
    if (!dluma_mod_config_identity_valid(identity) || !schema ||
        dluma_stricmp(
            identity->config_schema_fingerprint,
            schema->fingerprint) != 0 ||
        !dluma_lua_config_hex_key_valid(schema->fingerprint))
        return 0;
    copy = (DlumaLuaConfigSchema *)dluma_alloc(sizeof(*copy));
    if (!copy) return 0;
    memcpy(copy, schema, sizeof(*copy));
    dluma_lua_config_lock(&g_dluma_lua_config_registry_lock);
    for (index = 0; index < DLUMA_MOD_CONFIG_MAX_REGISTRY; ++index) {
        DlumaLuaConfigRegistrySlot *slot =
            &g_dluma_lua_config_registry[index];
        if (!slot->occupied) {
            if (free_index == DLUMA_MOD_CONFIG_MAX_REGISTRY)
                free_index = index;
            continue;
        }
        if (slot->owner_kind == identity->owner_kind &&
            dluma_stricmp(
                slot->registry_key, identity->registry_key) == 0)
            break;
    }
    if (index == DLUMA_MOD_CONFIG_MAX_REGISTRY) index = free_index;
    if (index == DLUMA_MOD_CONFIG_MAX_REGISTRY) {
        dluma_lua_config_unlock(&g_dluma_lua_config_registry_lock);
        dluma_free(copy);
        return 0;
    }
    old = g_dluma_lua_config_registry[index].schema;
    memset(&g_dluma_lua_config_registry[index], 0,
           sizeof(g_dluma_lua_config_registry[index]));
    dluma_str_copy(
        g_dluma_lua_config_registry[index].registry_key,
        DLUMA_ARRAY_COUNT(g_dluma_lua_config_registry[index].registry_key),
        identity->registry_key);
    dluma_str_copy(
        g_dluma_lua_config_registry[index].metadata_fingerprint,
        DLUMA_ARRAY_COUNT(
            g_dluma_lua_config_registry[index].metadata_fingerprint),
        identity->metadata_fingerprint);
    dluma_str_copy(
        g_dluma_lua_config_registry[index].schema_fingerprint,
        DLUMA_ARRAY_COUNT(
            g_dluma_lua_config_registry[index].schema_fingerprint),
        schema->fingerprint);
    g_dluma_lua_config_registry[index].schema = copy;
    g_dluma_lua_config_registry[index].occupied = 1;
    g_dluma_lua_config_registry[index].owner_kind =
        identity->owner_kind;
    dluma_lua_config_unlock(&g_dluma_lua_config_registry_lock);
    dluma_free(old);
    return 1;
}

int dluma_lua_config_publish(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema)
{
    DlumaModConfigIdentity identity;
    dluma_mod_config_identity_from_lua(metadata, &identity);
    return dluma_mod_config_publish(&identity, schema);
}

int dluma_mod_config_snapshot(
    unsigned int owner_kind,
    const char *registry_key,
    const char *metadata_fingerprint,
    const char *schema_fingerprint,
    DlumaLuaConfigSchema *out)
{
    unsigned int index;
    int found = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!registry_key || !metadata_fingerprint || !schema_fingerprint)
        return 0;
    if (owner_kind != DLUMA_MOD_CONFIG_OWNER_LUA &&
        owner_kind != DLUMA_MOD_CONFIG_OWNER_NATIVE)
        return 0;
    dluma_lua_config_lock(&g_dluma_lua_config_registry_lock);
    for (index = 0; index < DLUMA_MOD_CONFIG_MAX_REGISTRY; ++index) {
        const DlumaLuaConfigRegistrySlot *slot =
            &g_dluma_lua_config_registry[index];
        if (!slot->occupied || !slot->schema) continue;
        if (slot->owner_kind == owner_kind &&
            dluma_stricmp(slot->registry_key, registry_key) == 0 &&
            dluma_stricmp(
                slot->metadata_fingerprint, metadata_fingerprint) == 0 &&
            dluma_stricmp(
                slot->schema_fingerprint, schema_fingerprint) == 0) {
            memcpy(out, slot->schema, sizeof(*out));
            found = 1;
            break;
        }
    }
    dluma_lua_config_unlock(&g_dluma_lua_config_registry_lock);
    return found;
}

int dluma_lua_config_snapshot(
    const char *registry_key,
    const char *metadata_fingerprint,
    const char *schema_fingerprint,
    DlumaLuaConfigSchema *out)
{
    return dluma_mod_config_snapshot(
        DLUMA_MOD_CONFIG_OWNER_LUA,
        registry_key, metadata_fingerprint,
        schema_fingerprint, out);
}

static int dluma_lua_config_registry_matches_locked(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema)
{
    unsigned int index;
    if (!dluma_mod_config_identity_valid(identity) || !schema ||
        dluma_stricmp(
            identity->config_schema_fingerprint,
            schema->fingerprint) != 0)
        return 0;
    for (index = 0; index < DLUMA_MOD_CONFIG_MAX_REGISTRY; ++index) {
        const DlumaLuaConfigRegistrySlot *slot =
            &g_dluma_lua_config_registry[index];
        if (!slot->occupied || !slot->schema)
            continue;
        if (slot->owner_kind == identity->owner_kind &&
            dluma_stricmp(
                slot->registry_key,
                identity->registry_key) == 0 &&
            dluma_stricmp(
                slot->metadata_fingerprint,
                identity->metadata_fingerprint) == 0 &&
            dluma_stricmp(
                slot->schema_fingerprint,
                schema->fingerprint) == 0)
            return 1;
    }
    return 0;
}

static int dluma_lua_config_registry_matches(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema)
{
    int result;
    dluma_lua_config_lock(
        &g_dluma_lua_config_registry_lock);
    result = dluma_lua_config_registry_matches_locked(
        identity, schema);
    dluma_lua_config_unlock(
        &g_dluma_lua_config_registry_lock);
    return result;
}

void dluma_mod_config_prune_owner(
    unsigned int owner_kind,
    const DlumaModConfigIdentity *entries,
    unsigned int count)
{
    DlumaLuaConfigSchema *retired[DLUMA_MOD_CONFIG_MAX_REGISTRY];
    unsigned int retired_count = 0;
    unsigned int index;
    dluma_lua_config_lock(&g_dluma_lua_config_registry_lock);
    for (index = 0; index < DLUMA_MOD_CONFIG_MAX_REGISTRY; ++index) {
        DlumaLuaConfigRegistrySlot *slot =
            &g_dluma_lua_config_registry[index];
        unsigned int entry_index;
        int keep = 0;
        if (!slot->occupied || slot->owner_kind != owner_kind) continue;
        for (entry_index = 0; entries && entry_index < count; ++entry_index) {
            if (entries[entry_index].config_available &&
                entries[entry_index].owner_kind == owner_kind &&
                dluma_stricmp(entries[entry_index].registry_key,
                            slot->registry_key) == 0 &&
                dluma_stricmp(entries[entry_index].metadata_fingerprint,
                            slot->metadata_fingerprint) == 0 &&
                dluma_stricmp(entries[entry_index].config_schema_fingerprint,
                            slot->schema_fingerprint) == 0) {
                keep = 1;
                break;
            }
        }
        if (!keep) {
            retired[retired_count++] = slot->schema;
            memset(slot, 0, sizeof(*slot));
        }
    }
    dluma_lua_config_unlock(&g_dluma_lua_config_registry_lock);
    for (index = 0; index < retired_count; ++index)
        dluma_free(retired[index]);
}

void dluma_lua_config_prune(
    const DlumaLuaMetadata *entries,
    unsigned int count)
{
    DlumaModConfigIdentity *identities = 0;
    unsigned int index;
    if (entries && count) {
        identities = (DlumaModConfigIdentity*)dluma_alloc(
            (SIZE_T)count * sizeof(*identities));
        if (!identities) return;
        for (index = 0; index < count; ++index)
            dluma_mod_config_identity_from_lua(
                &entries[index], &identities[index]);
    }
    dluma_mod_config_prune_owner(
        DLUMA_MOD_CONFIG_OWNER_LUA, identities, count);
    dluma_free(identities);
}

void dluma_lua_config_shutdown(void)
{
    dluma_mod_config_prune_owner(
        DLUMA_MOD_CONFIG_OWNER_LUA, 0, 0);
    dluma_mod_config_prune_owner(
        DLUMA_MOD_CONFIG_OWNER_NATIVE, 0, 0);
}

int dluma_lua_config_find_setting(
    const DlumaLuaConfigSchema *schema,
    const char *key,
    unsigned int *index_out)
{
    unsigned int index;
    if (!schema || !key) return 0;
    for (index = 0; index < schema->setting_count; ++index) {
        if (dluma_lua_config_text_equal(
                schema->settings[index].key, key)) {
            if (index_out) *index_out = index;
            return 1;
        }
    }
    return 0;
}

static int dluma_lua_config_i32_to_text(
    int value, char *out, unsigned int capacity)
{
    char reverse[16];
    unsigned int count = 0;
    unsigned int position = 0;
    ULONGLONG magnitude;
    if (!out || capacity < 2U) return 0;
    if (value < 0) {
        magnitude = (ULONGLONG)(-(LONGLONG)value);
        out[position++] = '-';
    } else {
        magnitude = (ULONGLONG)value;
    }
    do {
        reverse[count++] = (char)('0' + (magnitude % 10ULL));
        magnitude /= 10ULL;
    } while (magnitude && count < DLUMA_ARRAY_COUNT(reverse));
    if (position + count + 1U > capacity) {
        out[0] = 0;
        return 0;
    }
    while (count) out[position++] = reverse[--count];
    out[position] = 0;
    return 1;
}

static int dluma_lua_config_parse_i32(const char *text, int *out)
{
    const char *cursor = text;
    ULONGLONG value = 0;
    ULONGLONG limit;
    int negative = 0;
    if (!cursor || !cursor[0] || !out) return 0;
    if (*cursor == '-') {
        negative = 1;
        ++cursor;
    } else if (*cursor == '+') {
        ++cursor;
    }
    if (!*cursor) return 0;
    limit = negative ? 2147483648ULL : 2147483647ULL;
    while (*cursor) {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') return 0;
        digit = (unsigned int)(*cursor - '0');
        if (value > (limit - digit) / 10ULL) return 0;
        value = value * 10ULL + digit;
        ++cursor;
    }
    if (negative) {
        if (value == 2147483648ULL)
            *out = (-2147483647 - 1);
        else
            *out = -(int)value;
    } else {
        *out = (int)value;
    }
    return 1;
}

void dluma_lua_config_default_value(
    const DlumaLuaConfigSetting *setting,
    DlumaLuaConfigValue *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!setting) return;
    out->kind = setting->kind;
    out->present = 1;
    if (setting->kind == DLUMA_LUA_CONFIG_BOOLEAN ||
        setting->kind == DLUMA_LUA_CONFIG_INTEGER)
        out->integer = setting->default_integer;
    else
        dluma_str_copy(out->text, DLUMA_ARRAY_COUNT(out->text),
                     setting->default_text);
}

int dluma_lua_config_value_valid(
    const DlumaLuaConfigSetting *setting,
    const DlumaLuaConfigValue *value)
{
    unsigned int index;
    if (!setting || !value || !value->present ||
        value->kind != setting->kind)
        return 0;
    switch ((DlumaLuaConfigKind)setting->kind) {
    case DLUMA_LUA_CONFIG_BOOLEAN:
        return value->integer == 0 || value->integer == 1;
    case DLUMA_LUA_CONFIG_INTEGER:
        if (value->integer < setting->minimum ||
            value->integer > setting->maximum ||
            !setting->step)
            return 0;
        return ((ULONGLONG)(
                    (LONGLONG)value->integer -
                    (LONGLONG)setting->minimum) %
                setting->step) == 0;
    case DLUMA_LUA_CONFIG_ENUM:
        for (index = 0; index < setting->option_count; ++index)
            if (dluma_lua_config_text_equal(
                    setting->options[index].value, value->text))
                return 1;
        return 0;
    case DLUMA_LUA_CONFIG_TEXT:
        return dluma_lua_config_utf8_valid(
            value->text,
            DLUMA_LUA_CONFIG_MAX_TEXT_BYTES,
            setting->max_length,
            1);
    default:
        return 0;
    }
}

int dluma_lua_config_value_equal(
    const DlumaLuaConfigValue *left,
    const DlumaLuaConfigValue *right)
{
    if (!left || !right || !left->present || !right->present ||
        left->kind != right->kind)
        return 0;
    if (left->kind == DLUMA_LUA_CONFIG_BOOLEAN ||
        left->kind == DLUMA_LUA_CONFIG_INTEGER)
        return left->integer == right->integer;
    return dluma_lua_config_text_equal(left->text, right->text);
}

int dluma_lua_config_value_to_text(
    const DlumaLuaConfigValue *value,
    char *out,
    unsigned int capacity)
{
    if (!value || !value->present || !out || !capacity) return 0;
    if (value->kind == DLUMA_LUA_CONFIG_BOOLEAN) {
        dluma_str_copy(out, capacity, value->integer ? "true" : "false");
        return 1;
    }
    if (value->kind == DLUMA_LUA_CONFIG_INTEGER)
        return dluma_lua_config_i32_to_text(value->integer, out, capacity);
    if (value->kind == DLUMA_LUA_CONFIG_ENUM ||
        value->kind == DLUMA_LUA_CONFIG_TEXT) {
        if (strlen(value->text) + 1U > capacity) return 0;
        dluma_str_copy(out, capacity, value->text);
        return 1;
    }
    return 0;
}

int dluma_lua_config_value_from_text(
    const DlumaLuaConfigSetting *setting,
    const char *text,
    DlumaLuaConfigValue *out)
{
    if (!setting || !text || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->kind = setting->kind;
    out->present = 1;
    if (setting->kind == DLUMA_LUA_CONFIG_BOOLEAN) {
        if (dluma_stricmp(text, "true") == 0 ||
            dluma_lua_config_text_equal(text, "1"))
            out->integer = 1;
        else if (dluma_stricmp(text, "false") == 0 ||
                 dluma_lua_config_text_equal(text, "0"))
            out->integer = 0;
        else
            return 0;
    } else if (setting->kind == DLUMA_LUA_CONFIG_INTEGER) {
        if (!dluma_lua_config_parse_i32(text, &out->integer)) return 0;
    } else {
        if (strlen(text) >= DLUMA_ARRAY_COUNT(out->text)) return 0;
        dluma_str_copy(out->text, DLUMA_ARRAY_COUNT(out->text), text);
    }
    return dluma_lua_config_value_valid(setting, out);
}

static int dluma_lua_config_checked_join(
    WCHAR *out, unsigned int capacity,
    const WCHAR *left, const WCHAR *right)
{
    SIZE_T left_length;
    SIZE_T right_length;
    if (!out || !capacity || !left || !right) return 0;
    left_length = wcslen(left);
    right_length = wcslen(right);
    if (left_length + right_length + 2U > capacity) {
        out[0] = 0;
        return 0;
    }
    dluma_path_join(out, capacity, left, right);
    return out[0] != 0 &&
           wcslen(out) == left_length + right_length +
                           (left_length && left[left_length - 1U] != L'\\'
                                ? 1U : 0U);
}

static HANDLE dluma_lua_config_open_directory(
    const WCHAR *path,
    WCHAR *final_path,
    unsigned int final_capacity)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION information;
    if (!path || !path[0] || !final_path ||
        !final_capacity)
        return INVALID_HANDLE_VALUE;
    final_path[0] = 0;
    directory = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (directory == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(
            directory, &information) ||
        !(information.dwFileAttributes &
          FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory, final_path, final_capacity)) {
        CloseHandle(directory);
        final_path[0] = 0;
        return INVALID_HANDLE_VALUE;
    }
    return directory;
}

static HANDLE dluma_lua_config_open_or_create_child_directory(
    const WCHAR *path,
    const WCHAR *final_parent,
    WCHAR *final_path,
    unsigned int final_capacity)
{
    DWORD attributes;
    HANDLE directory;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD path_error = GetLastError();
        if (path_error != ERROR_FILE_NOT_FOUND &&
            path_error != ERROR_PATH_NOT_FOUND)
            return INVALID_HANDLE_VALUE;
        if (!CreateDirectoryW(path, 0) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            return INVALID_HANDLE_VALUE;
    }
    directory = dluma_lua_config_open_directory(
        path, final_path, final_capacity);
    if (directory == INVALID_HANDLE_VALUE ||
        !dluma_final_path_equal_or_under(
            final_path, final_parent) ||
        dluma_wstricmp(final_path, final_parent) == 0) {
        if (directory != INVALID_HANDLE_VALUE)
            CloseHandle(directory);
        final_path[0] = 0;
        return INVALID_HANDLE_VALUE;
    }
    return directory;
}

static int dluma_lua_config_prepare_directories(
    const WCHAR *runtime,
    const WCHAR *root,
    const WCHAR *directory)
{
    WCHAR final_anchor[DLUMA_MAX_WPATH];
    WCHAR final_runtime[DLUMA_MAX_WPATH];
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR final_directory[DLUMA_MAX_WPATH];
    HANDLE anchor = INVALID_HANDLE_VALUE;
    HANDLE runtime_handle = INVALID_HANDLE_VALUE;
    HANDLE root_handle = INVALID_HANDLE_VALUE;
    HANDLE directory_handle = INVALID_HANDLE_VALUE;
    int result = 0;
    anchor = dluma_lua_config_open_directory(
        g_dluma.module_dir, final_anchor,
        DLUMA_ARRAY_COUNT(final_anchor));
    if (anchor == INVALID_HANDLE_VALUE) goto done;
    runtime_handle =
        dluma_lua_config_open_or_create_child_directory(
            runtime, final_anchor, final_runtime,
            DLUMA_ARRAY_COUNT(final_runtime));
    if (runtime_handle == INVALID_HANDLE_VALUE)
        goto done;
    root_handle =
        dluma_lua_config_open_or_create_child_directory(
            root, final_runtime, final_root,
            DLUMA_ARRAY_COUNT(final_root));
    if (root_handle == INVALID_HANDLE_VALUE)
        goto done;
    directory_handle =
        dluma_lua_config_open_or_create_child_directory(
            directory, final_root, final_directory,
            DLUMA_ARRAY_COUNT(final_directory));
    if (directory_handle == INVALID_HANDLE_VALUE)
        goto done;
    result = 1;
done:
    if (directory_handle != INVALID_HANDLE_VALUE)
        CloseHandle(directory_handle);
    if (root_handle != INVALID_HANDLE_VALUE)
        CloseHandle(root_handle);
    if (runtime_handle != INVALID_HANDLE_VALUE)
        CloseHandle(runtime_handle);
    if (anchor != INVALID_HANDLE_VALUE)
        CloseHandle(anchor);
    return result;
}

static void dluma_lua_config_close_directory_guard(
    DlumaLuaConfigDirectoryGuard *guard)
{
    if (!guard) return;
    if (guard->directory != INVALID_HANDLE_VALUE)
        CloseHandle(guard->directory);
    if (guard->root != INVALID_HANDLE_VALUE)
        CloseHandle(guard->root);
    if (guard->anchor != INVALID_HANDLE_VALUE)
        CloseHandle(guard->anchor);
    memset(guard, 0, sizeof(*guard));
    guard->anchor = INVALID_HANDLE_VALUE;
    guard->root = INVALID_HANDLE_VALUE;
    guard->directory = INVALID_HANDLE_VALUE;
}

static int dluma_lua_config_open_directory_guard(
    const WCHAR *directory,
    DlumaLuaConfigDirectoryGuard *guard)
{
    WCHAR root_path[DLUMA_MAX_WPATH];
    SIZE_T length;
    if (!directory || !directory[0] || !guard)
        return 0;
    memset(guard, 0, sizeof(*guard));
    guard->anchor = INVALID_HANDLE_VALUE;
    guard->root = INVALID_HANDLE_VALUE;
    guard->directory = INVALID_HANDLE_VALUE;
    dluma_wstr_copy(
        root_path, DLUMA_ARRAY_COUNT(root_path), directory);
    length = wcslen(root_path);
    while (length &&
           root_path[length - 1U] != L'\\' &&
           root_path[length - 1U] != L'/')
        --length;
    if (!length) return 0;
    while (length &&
           (root_path[length - 1U] == L'\\' ||
            root_path[length - 1U] == L'/'))
        --length;
    root_path[length] = 0;
    guard->anchor = dluma_lua_config_open_directory(
        g_dluma.module_dir, guard->final_anchor,
        DLUMA_ARRAY_COUNT(guard->final_anchor));
    if (guard->anchor == INVALID_HANDLE_VALUE)
        goto failed;
    guard->root = dluma_lua_config_open_directory(
        root_path, guard->final_root,
        DLUMA_ARRAY_COUNT(guard->final_root));
    if (guard->root == INVALID_HANDLE_VALUE ||
        !dluma_final_path_equal_or_under(
            guard->final_root, guard->final_anchor) ||
        dluma_wstricmp(
            guard->final_root,
            guard->final_anchor) == 0)
        goto failed;
    guard->directory = dluma_lua_config_open_directory(
        directory, guard->final_directory,
        DLUMA_ARRAY_COUNT(guard->final_directory));
    if (guard->directory == INVALID_HANDLE_VALUE ||
        !dluma_final_path_equal_or_under(
            guard->final_directory,
            guard->final_root) ||
        dluma_wstricmp(
            guard->final_directory,
            guard->final_root) == 0)
        goto failed;
    return 1;
failed:
    dluma_lua_config_close_directory_guard(guard);
    return 0;
}

static HANDLE dluma_lua_config_open_regular_file(
    const WCHAR *path,
    const WCHAR *final_directory,
    DWORD access,
    DWORD sharing)
{
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION information;
    HANDLE file;
    file = CreateFileW(
        path, access, sharing, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY |
          FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_path,
            DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_final_path_equal_or_under(
            final_path, final_directory) ||
        dluma_wstricmp(
            final_path, final_directory) == 0) {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static int dluma_lua_config_build_paths(
    const DlumaModConfigIdentity *identity,
    const char *setting_key,
    int create,
    WCHAR *directory,
    WCHAR *path,
    WCHAR *temporary)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    WCHAR root[DLUMA_MAX_WPATH];
    WCHAR storage[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR filename[96];
    char setting_hash[DLUMA_SHA256_HEX_CAPACITY];
    char filename_utf8[80];
    if (!identity || !setting_key || !directory || !path ||
        !dluma_lua_config_hex_key_valid(identity->storage_key) ||
        !dluma_lua_config_identifier_valid(setting_key) ||
        !g_dluma.module_dir[0])
        return 0;
    if (!dluma_lua_config_checked_join(
            runtime, DLUMA_ARRAY_COUNT(runtime),
            g_dluma.module_dir, L"dluma_runtime") ||
        !dluma_lua_config_checked_join(
            root, DLUMA_ARRAY_COUNT(root), runtime, L"mod_config") ||
        !dluma_utf8_to_wide(identity->storage_key, storage,
                          DLUMA_ARRAY_COUNT(storage)) ||
        !dluma_lua_config_checked_join(
            directory, DLUMA_MAX_WPATH, root, storage))
        return 0;
    if (create) {
        if (!dluma_lua_config_prepare_directories(
                runtime, root, directory))
            return 0;
    } else {
        DWORD root_attributes = GetFileAttributesW(root);
        DWORD directory_attributes = GetFileAttributesW(directory);
        if ((root_attributes != INVALID_FILE_ATTRIBUTES &&
             (root_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) ||
            (directory_attributes != INVALID_FILE_ATTRIBUTES &&
             (directory_attributes & FILE_ATTRIBUTE_REPARSE_POINT)))
            return 0;
    }
    dluma_sha256_bytes_hex(
        setting_key, strlen(setting_key), setting_hash);
    dluma_str_copy(filename_utf8, DLUMA_ARRAY_COUNT(filename_utf8), setting_hash);
    dluma_str_append(filename_utf8, DLUMA_ARRAY_COUNT(filename_utf8), ".dlumac");
    if (!dluma_utf8_to_wide(
            filename_utf8, filename, DLUMA_ARRAY_COUNT(filename)) ||
        !dluma_lua_config_checked_join(
            path, DLUMA_MAX_WPATH, directory, filename))
        return 0;
    if (temporary) {
        dluma_wstr_copy(temporary, DLUMA_MAX_WPATH, path);
        if (wcslen(temporary) + 5U >= DLUMA_MAX_WPATH) return 0;
        dluma_wstr_append(temporary, DLUMA_MAX_WPATH, L".tmp");
    }
    return 1;
}

static DWORD dluma_lua_config_read_u32(const BYTE *bytes)
{
    return (DWORD)bytes[0] |
           ((DWORD)bytes[1] << 8) |
           ((DWORD)bytes[2] << 16) |
           ((DWORD)bytes[3] << 24);
}

static void dluma_lua_config_write_u32(BYTE *bytes, DWORD value)
{
    bytes[0] = (BYTE)(value & 0xFFU);
    bytes[1] = (BYTE)((value >> 8) & 0xFFU);
    bytes[2] = (BYTE)((value >> 16) & 0xFFU);
    bytes[3] = (BYTE)((value >> 24) & 0xFFU);
}

static int dluma_lua_config_file_size(
    const WCHAR *path,
    const WCHAR *final_directory,
    ULONGLONG *size_out)
{
    HANDLE file;
    LARGE_INTEGER size;
    DWORD attributes;
    if (size_out) *size_out = 0;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD path_error = GetLastError();
        return path_error == ERROR_FILE_NOT_FOUND ||
               path_error == ERROR_PATH_NOT_FOUND;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return 0;
    file = dluma_lua_config_open_regular_file(
        path, final_directory, GENERIC_READ,
        FILE_SHARE_READ);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    if (size_out) *size_out = (ULONGLONG)size.QuadPart;
    return 1;
}

static ULONGLONG dluma_lua_config_storage_usage(const WCHAR *directory)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    ULONGLONG total = 0;
    if (!dluma_lua_config_checked_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), directory, L"*.dlumac"))
        return ~0ULL;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ||
               error == ERROR_PATH_NOT_FOUND ? 0 : ~0ULL;
    }
    do {
        ULONGLONG size;
        if (data.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            FindClose(find);
            return ~0ULL;
        }
        size = ((ULONGLONG)data.nFileSizeHigh << 32) |
               data.nFileSizeLow;
        if (total > ~0ULL - size) {
            FindClose(find);
            return ~0ULL;
        }
        total += size;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return total;
}

static int dluma_lua_config_read_unlocked(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *out,
    int *has_override,
    char *error,
    unsigned int error_capacity)
{
    static const BYTE magic[8] =
        {'D', 'L', 'W', 'C', 'F', 'G', '1', '\n'};
    const DlumaLuaConfigSetting *setting;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    DlumaLuaConfigDirectoryGuard guard;
    BYTE data[DLUMA_LUA_CONFIG_RECORD_MAX_SIZE];
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0;
    DWORD attributes;
    DWORD key_length;
    DWORD value_length;
    char value_text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    if (has_override) *has_override = 0;
    if (error && error_capacity) error[0] = 0;
    if (!identity || !schema || !out ||
        setting_index >= schema->setting_count ||
        !identity->config_available ||
        dluma_stricmp(identity->config_schema_fingerprint,
                    schema->fingerprint) != 0) {
        dluma_lua_config_error(error, error_capacity,
                             "config_schema_stale");
        return 0;
    }
    setting = &schema->settings[setting_index];
    dluma_lua_config_default_value(setting, out);
    if (!dluma_lua_config_build_paths(
            identity, setting->key, 0, directory, path, 0)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_path_invalid");
        return 0;
    }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD path_error = GetLastError();
        if (path_error == ERROR_FILE_NOT_FOUND ||
            path_error == ERROR_PATH_NOT_FOUND)
            return 1;
        dluma_lua_config_error(error, error_capacity,
                             "config_read_failed");
        return 0;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_record_unsafe");
        return 0;
    }
    if (has_override) *has_override = 1;
    if (!dluma_lua_config_open_directory_guard(
            directory, &guard)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_record_unsafe");
        return 0;
    }
    file = dluma_lua_config_open_regular_file(
        path, guard.final_directory, GENERIC_READ,
        FILE_SHARE_READ);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileSizeEx(file, &size) ||
        size.QuadPart < DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE ||
        size.QuadPart > (LONGLONG)sizeof(data) ||
        !ReadFile(file, data, (DWORD)size.QuadPart, &read, 0) ||
        read != (DWORD)size.QuadPart) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_read_failed");
        return 0;
    }
    CloseHandle(file);
    dluma_lua_config_close_directory_guard(&guard);
    key_length = dluma_lua_config_read_u32(data + 12);
    value_length = dluma_lua_config_read_u32(data + 16);
    if (memcmp(data, magic, sizeof(magic)) != 0 ||
        data[8] != setting->kind ||
        data[9] || data[10] || data[11] ||
        key_length != strlen(setting->key) ||
        value_length >= sizeof(value_text) ||
        DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE + key_length + value_length !=
            read ||
        memcmp(data + DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE,
               setting->key, key_length) != 0) {
        dluma_lua_config_error(error, error_capacity,
                             "config_record_malformed");
        return 0;
    }
    memcpy(
        value_text,
        data + DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE + key_length,
        value_length);
    value_text[value_length] = 0;
    if (strlen(value_text) != value_length) {
        dluma_lua_config_error(error, error_capacity,
                             "config_record_malformed");
        return 0;
    }
    if (!dluma_lua_config_value_from_text(setting, value_text, out)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_value_invalid");
        return 0;
    }
    return 1;
}

int dluma_mod_config_read(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *out,
    int *has_override,
    char *error,
    unsigned int error_capacity)
{
    int result;
    dluma_lua_config_lock(&g_dluma_lua_config_storage_lock);
    result = dluma_lua_config_read_unlocked(
        identity, schema, setting_index, out, has_override,
        error, error_capacity);
    dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
    return result;
}

static int dluma_lua_config_write_record(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    const WCHAR *directory,
    const WCHAR *path,
    const WCHAR *temporary,
    const DlumaLuaConfigSetting *setting,
    const DlumaLuaConfigValue *value,
    char *error,
    unsigned int error_capacity)
{
    static const BYTE magic[8] =
        {'D', 'L', 'W', 'C', 'F', 'G', '1', '\n'};
    BYTE data[DLUMA_LUA_CONFIG_RECORD_MAX_SIZE];
    char value_text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    DWORD size;
    DWORD written = 0;
    DWORD key_length = (DWORD)strlen(setting->key);
    DWORD value_length;
    HANDLE file;
    DlumaLuaConfigDirectoryGuard guard;
    BY_HANDLE_FILE_INFORMATION temporary_information;
    WCHAR final_temporary[DLUMA_MAX_WPATH];
    ULONGLONG usage;
    ULONGLONG old_size = 0;
    if (!dluma_lua_config_value_to_text(
            value, value_text, DLUMA_ARRAY_COUNT(value_text))) {
        dluma_lua_config_error(error, error_capacity,
                             "config_value_invalid");
        return 0;
    }
    value_length = (DWORD)strlen(value_text);
    size = DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE +
           key_length + value_length;
    if (size > sizeof(data)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_record_unsafe");
        return 0;
    }
    if (!dluma_lua_config_open_directory_guard(
            directory, &guard) ||
        !dluma_lua_config_file_size(
            path, guard.final_directory, &old_size)) {
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_record_unsafe");
        return 0;
    }
    usage = dluma_lua_config_storage_usage(directory);
    if (usage == ~0ULL || usage < old_size ||
        usage - old_size >
            DLUMA_LUA_CONFIG_TOTAL_STORAGE_LIMIT ||
        size > DLUMA_LUA_CONFIG_TOTAL_STORAGE_LIMIT -
                   (usage - old_size)) {
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_storage_quota");
        return 0;
    }
    memset(data, 0, size);
    memcpy(data, magic, sizeof(magic));
    data[8] = setting->kind;
    dluma_lua_config_write_u32(data + 12, key_length);
    dluma_lua_config_write_u32(data + 16, value_length);
    memcpy(data + DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE,
           setting->key, key_length);
    memcpy(data + DLUMA_LUA_CONFIG_RECORD_HEADER_SIZE + key_length,
           value_text, value_length);
    {
        DWORD temporary_attributes =
            GetFileAttributesW(temporary);
        if (temporary_attributes != INVALID_FILE_ATTRIBUTES) {
            if ((temporary_attributes &
                 (FILE_ATTRIBUTE_DIRECTORY |
                  FILE_ATTRIBUTE_REPARSE_POINT)) ||
                !DeleteFileW(temporary)) {
                dluma_lua_config_error(
                    error, error_capacity,
                    "config_record_unsafe");
                dluma_lua_config_close_directory_guard(
                    &guard);
                return 0;
            }
        } else {
            DWORD temporary_error = GetLastError();
            if (temporary_error != ERROR_FILE_NOT_FOUND &&
                temporary_error != ERROR_PATH_NOT_FOUND) {
                dluma_lua_config_error(
                    error, error_capacity,
                    "config_record_unsafe");
                dluma_lua_config_close_directory_guard(
                    &guard);
                return 0;
            }
        }
    }
    file = CreateFileW(
        temporary, GENERIC_WRITE, 0, 0, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    memset(
        &temporary_information, 0,
        sizeof(temporary_information));
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(
            file, &temporary_information) ||
        (temporary_information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY |
          FILE_ATTRIBUTE_REPARSE_POINT)) ||
        temporary_information.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_temporary,
            DLUMA_ARRAY_COUNT(final_temporary)) ||
        !dluma_final_path_equal_or_under(
            final_temporary, guard.final_directory) ||
        dluma_wstricmp(
            final_temporary,
            guard.final_directory) == 0 ||
        !WriteFile(file, data, size, &written, 0) ||
        written != size ||
        !FlushFileBuffers(file)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        (void)DeleteFileW(temporary);
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_write_failed");
        return 0;
    }
    CloseHandle(file);
    dluma_lua_config_lock(
        &g_dluma_lua_config_registry_lock);
    if (!dluma_lua_config_registry_matches_locked(
            identity, schema)) {
        dluma_lua_config_unlock(
            &g_dluma_lua_config_registry_lock);
        (void)DeleteFileW(temporary);
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_schema_stale");
        return 0;
    }
    if (!MoveFileExW(
            temporary, path,
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        dluma_lua_config_unlock(
            &g_dluma_lua_config_registry_lock);
        (void)DeleteFileW(temporary);
        dluma_lua_config_close_directory_guard(&guard);
        dluma_lua_config_error(error, error_capacity,
                             "config_write_failed");
        return 0;
    }
    dluma_lua_config_unlock(
        &g_dluma_lua_config_registry_lock);
    dluma_lua_config_close_directory_guard(&guard);
    if (error && error_capacity) error[0] = 0;
    return 1;
}

int dluma_mod_config_write(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    const DlumaLuaConfigValue *value,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    int *changed,
    char *error,
    unsigned int error_capacity)
{
    const DlumaLuaConfigSetting *setting;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    DlumaLuaConfigValue previous;
    int has_override = 0;
    int previous_known;
    int result;
    if (old_value) memset(old_value, 0, sizeof(*old_value));
    if (old_value_known) *old_value_known = 0;
    if (changed) *changed = 0;
    if (error && error_capacity) error[0] = 0;
    if (!identity || !schema || !value ||
        setting_index >= schema->setting_count ||
        !dluma_lua_config_registry_matches(
            identity, schema) ||
        !dluma_lua_config_value_valid(
            &schema->settings[setting_index], value)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_value_invalid");
        return 0;
    }
    setting = &schema->settings[setting_index];
    dluma_lua_config_lock(&g_dluma_lua_config_storage_lock);
    previous_known = dluma_lua_config_read_unlocked(
        identity, schema, setting_index, &previous, &has_override,
        error, error_capacity);
    if (previous_known &&
        dluma_lua_config_value_equal(&previous, value)) {
        if (!dluma_lua_config_registry_matches(
                identity, schema)) {
            dluma_lua_config_error(
                error, error_capacity,
                "config_schema_stale");
            dluma_lua_config_unlock(
                &g_dluma_lua_config_storage_lock);
            return 0;
        }
        if (old_value) *old_value = previous;
        if (old_value_known) *old_value_known = 1;
        if (error && error_capacity) error[0] = 0;
        dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
        return 1;
    }
    if (!dluma_lua_config_build_paths(
            identity, setting->key, 1,
            directory, path, temporary)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_path_invalid");
        dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
        return 0;
    }
    result = dluma_lua_config_write_record(
        identity, schema, directory, path, temporary,
        setting, value,
        error, error_capacity);
    if (result) {
        if (old_value && previous_known) *old_value = previous;
        if (old_value_known) *old_value_known = previous_known;
        if (changed) *changed = 1;
    }
    dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
    return result;
}

int dluma_mod_config_reset(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    DlumaLuaConfigValue *new_value,
    int *changed,
    char *error,
    unsigned int error_capacity)
{
    const DlumaLuaConfigSetting *setting;
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    DlumaLuaConfigValue previous;
    DlumaLuaConfigValue fallback;
    DlumaLuaConfigDirectoryGuard guard;
    int has_override = 0;
    int previous_known;
    DWORD attributes;
    if (old_value) memset(old_value, 0, sizeof(*old_value));
    if (old_value_known) *old_value_known = 0;
    if (changed) *changed = 0;
    if (error && error_capacity) error[0] = 0;
    if (!identity || !schema ||
        setting_index >= schema->setting_count ||
        !dluma_lua_config_registry_matches(
            identity, schema)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_schema_stale");
        return 0;
    }
    setting = &schema->settings[setting_index];
    dluma_lua_config_default_value(setting, &fallback);
    if (new_value) *new_value = fallback;
    dluma_lua_config_lock(&g_dluma_lua_config_storage_lock);
    previous_known = dluma_lua_config_read_unlocked(
        identity, schema, setting_index, &previous, &has_override,
        error, error_capacity);
    if (!dluma_lua_config_build_paths(
            identity, setting->key, 0, directory, path, 0)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_path_invalid");
        dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
        return 0;
    }
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        HANDLE record = INVALID_HANDLE_VALUE;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !dluma_lua_config_open_directory_guard(
                directory, &guard)) {
            dluma_lua_config_error(error, error_capacity,
                                 "config_reset_failed");
            dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
            return 0;
        }
        record = dluma_lua_config_open_regular_file(
            path, guard.final_directory, GENERIC_READ,
            FILE_SHARE_READ);
        if (record == INVALID_HANDLE_VALUE) {
            dluma_lua_config_close_directory_guard(&guard);
            dluma_lua_config_error(error, error_capacity,
                                 "config_reset_failed");
            dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
            return 0;
        }
        CloseHandle(record);
        dluma_lua_config_lock(
            &g_dluma_lua_config_registry_lock);
        if (!dluma_lua_config_registry_matches_locked(
                identity, schema) ||
            !DeleteFileW(path)) {
            dluma_lua_config_unlock(
                &g_dluma_lua_config_registry_lock);
            dluma_lua_config_close_directory_guard(&guard);
            dluma_lua_config_error(
                error, error_capacity,
                dluma_lua_config_registry_matches(
                    identity, schema)
                    ? "config_reset_failed"
                    : "config_schema_stale");
            dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
            return 0;
        }
        dluma_lua_config_unlock(
            &g_dluma_lua_config_registry_lock);
        dluma_lua_config_close_directory_guard(&guard);
        has_override = 1;
    } else {
        DWORD path_error = GetLastError();
        if (path_error != ERROR_FILE_NOT_FOUND &&
            path_error != ERROR_PATH_NOT_FOUND) {
            dluma_lua_config_error(error, error_capacity,
                                 "config_reset_failed");
            dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
            return 0;
        }
    }
    if (!dluma_lua_config_registry_matches(
            identity, schema)) {
        dluma_lua_config_error(error, error_capacity,
                             "config_schema_stale");
        dluma_lua_config_unlock(
            &g_dluma_lua_config_storage_lock);
        return 0;
    }
    if (old_value && previous_known) *old_value = previous;
    if (old_value_known) *old_value_known = previous_known;
    if (changed)
        *changed = has_override &&
            (!previous_known ||
             !dluma_lua_config_value_equal(&previous, &fallback));
    if (error && error_capacity) error[0] = 0;
    dluma_lua_config_unlock(&g_dluma_lua_config_storage_lock);
    return 1;
}

int dluma_lua_config_read(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *out,
    int *has_override,
    char *error,
    unsigned int error_capacity)
{
    DlumaModConfigIdentity identity;
    dluma_mod_config_identity_from_lua(metadata, &identity);
    return dluma_mod_config_read(
        &identity, schema, setting_index, out, has_override,
        error, error_capacity);
}

int dluma_lua_config_write(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    const DlumaLuaConfigValue *value,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    int *changed,
    char *error,
    unsigned int error_capacity)
{
    DlumaModConfigIdentity identity;
    dluma_mod_config_identity_from_lua(metadata, &identity);
    return dluma_mod_config_write(
        &identity, schema, setting_index, value,
        old_value, old_value_known, changed,
        error, error_capacity);
}

int dluma_lua_config_reset(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    DlumaLuaConfigValue *new_value,
    int *changed,
    char *error,
    unsigned int error_capacity)
{
    DlumaModConfigIdentity identity;
    dluma_mod_config_identity_from_lua(metadata, &identity);
    return dluma_mod_config_reset(
        &identity, schema, setting_index,
        old_value, old_value_known, new_value, changed,
        error, error_capacity);
}
