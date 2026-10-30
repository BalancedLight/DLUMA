#ifndef DLUMA_LUA_CONFIG_H
#define DLUMA_LUA_CONFIG_H

#include "dlw_common.h"

#define DLUMA_LUA_CONFIG_SCHEMA_VERSION 1U
#define DLUMA_LUA_CONFIG_MAX_CATEGORIES 8U
#define DLUMA_LUA_CONFIG_MAX_SETTINGS_PER_CATEGORY 9U
#define DLUMA_LUA_CONFIG_MAX_SETTINGS 48U
#define DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS 16U
#define DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY 65U
#define DLUMA_LUA_CONFIG_NAME_CAPACITY 97U
#define DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY 257U
#define DLUMA_LUA_CONFIG_VALUE_CAPACITY 4097U
#define DLUMA_LUA_CONFIG_MAX_TEXT_CHARACTERS 1024U
#define DLUMA_LUA_CONFIG_MAX_TEXT_BYTES 4096U
#define DLUMA_LUA_CONFIG_TOTAL_STORAGE_LIMIT (64U * 1024U)
#define DLUMA_MOD_CONFIG_MAX_REGISTRY (DLUMA_MAX_LUA_FILES + 256U)

typedef enum DlumaModConfigOwnerKind {
    DLUMA_MOD_CONFIG_OWNER_INVALID = 0,
    DLUMA_MOD_CONFIG_OWNER_LUA = 1,
    DLUMA_MOD_CONFIG_OWNER_NATIVE = 2,
    DLUMA_MOD_CONFIG_OWNER_CSHARP = 3
} DlumaModConfigOwnerKind;

typedef struct DlumaModConfigIdentity {
    char registry_key[DLUMA_SHA256_HEX_CAPACITY];
    char storage_key[DLUMA_SHA256_HEX_CAPACITY];
    char metadata_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    char config_schema_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    unsigned char config_available;
    unsigned char owner_kind;
    unsigned char reserved[2];
} DlumaModConfigIdentity;

typedef enum DlumaLuaConfigKind {
    DLUMA_LUA_CONFIG_INVALID = 0,
    DLUMA_LUA_CONFIG_BOOLEAN,
    DLUMA_LUA_CONFIG_INTEGER,
    DLUMA_LUA_CONFIG_ENUM,
    DLUMA_LUA_CONFIG_TEXT
} DlumaLuaConfigKind;

typedef enum DlumaLuaConfigEditor {
    DLUMA_LUA_CONFIG_EDITOR_DEFAULT = 0,
    DLUMA_LUA_CONFIG_EDITOR_KEYBIND = 1
} DlumaLuaConfigEditor;

typedef struct DlumaLuaConfigEnumOption {
    char value[DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY];
    char name[DLUMA_LUA_CONFIG_NAME_CAPACITY];
} DlumaLuaConfigEnumOption;

typedef struct DlumaLuaConfigSetting {
    char key[DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY];
    char name[DLUMA_LUA_CONFIG_NAME_CAPACITY];
    char description[DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY];
    char default_text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    int default_integer;
    int minimum;
    int maximum;
    unsigned int step;
    unsigned int max_length;
    unsigned int option_count;
    unsigned char kind;
    unsigned char editor;
    unsigned char reserved[2];
    DlumaLuaConfigEnumOption options[DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS];
} DlumaLuaConfigSetting;

typedef struct DlumaLuaConfigCategory {
    char id[DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY];
    char name[DLUMA_LUA_CONFIG_NAME_CAPACITY];
    unsigned int first_setting;
    unsigned int setting_count;
} DlumaLuaConfigCategory;

typedef struct DlumaLuaConfigSchema {
    unsigned int schema_version;
    unsigned int category_count;
    unsigned int setting_count;
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    DlumaLuaConfigCategory categories[DLUMA_LUA_CONFIG_MAX_CATEGORIES];
    DlumaLuaConfigSetting settings[DLUMA_LUA_CONFIG_MAX_SETTINGS];
} DlumaLuaConfigSchema;

typedef struct DlumaLuaConfigValue {
    char text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    int integer;
    unsigned char kind;
    unsigned char present;
    unsigned char reserved[2];
} DlumaLuaConfigValue;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_lua_config_identifier_valid(const char *value);
int dluma_lua_config_utf8_valid(
    const char *value,
    unsigned int maximum_bytes,
    unsigned int maximum_characters,
    int allow_empty);
void dluma_lua_config_compute_fingerprint(DlumaLuaConfigSchema *schema);
void dluma_mod_config_identity_from_lua(
    const DlumaLuaMetadata *metadata,
    DlumaModConfigIdentity *out);
int dluma_mod_config_publish(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema);
int dluma_mod_config_snapshot(
    unsigned int owner_kind,
    const char *registry_key,
    const char *metadata_fingerprint,
    const char *schema_fingerprint,
    DlumaLuaConfigSchema *out);
void dluma_mod_config_prune_owner(
    unsigned int owner_kind,
    const DlumaModConfigIdentity *entries,
    unsigned int count);
int dluma_lua_config_publish(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema);
int dluma_lua_config_snapshot(
    const char *registry_key,
    const char *metadata_fingerprint,
    const char *schema_fingerprint,
    DlumaLuaConfigSchema *out);
void dluma_lua_config_prune(
    const DlumaLuaMetadata *entries,
    unsigned int count);
void dluma_lua_config_shutdown(void);

int dluma_lua_config_find_setting(
    const DlumaLuaConfigSchema *schema,
    const char *key,
    unsigned int *index_out);
void dluma_lua_config_default_value(
    const DlumaLuaConfigSetting *setting,
    DlumaLuaConfigValue *out);
int dluma_lua_config_value_valid(
    const DlumaLuaConfigSetting *setting,
    const DlumaLuaConfigValue *value);
int dluma_lua_config_value_equal(
    const DlumaLuaConfigValue *left,
    const DlumaLuaConfigValue *right);
int dluma_lua_config_value_to_text(
    const DlumaLuaConfigValue *value,
    char *out,
    unsigned int capacity);
int dluma_lua_config_value_from_text(
    const DlumaLuaConfigSetting *setting,
    const char *text,
    DlumaLuaConfigValue *out);

int dluma_mod_config_read(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *out,
    int *has_override,
    char *error,
    unsigned int error_capacity);
int dluma_mod_config_write(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    const DlumaLuaConfigValue *value,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    int *changed,
    char *error,
    unsigned int error_capacity);
int dluma_mod_config_reset(
    const DlumaModConfigIdentity *identity,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    DlumaLuaConfigValue *new_value,
    int *changed,
    char *error,
    unsigned int error_capacity);

int dluma_lua_config_read(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *out,
    int *has_override,
    char *error,
    unsigned int error_capacity);
int dluma_lua_config_write(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    const DlumaLuaConfigValue *value,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    int *changed,
    char *error,
    unsigned int error_capacity);
int dluma_lua_config_reset(
    const DlumaLuaMetadata *metadata,
    const DlumaLuaConfigSchema *schema,
    unsigned int setting_index,
    DlumaLuaConfigValue *old_value,
    int *old_value_known,
    DlumaLuaConfigValue *new_value,
    int *changed,
    char *error,
    unsigned int error_capacity);

#ifdef __cplusplus
}
#endif

#endif
