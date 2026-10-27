#include <stdio.h>
#include <string.h>

#include "../src/dlw_native_mod_config.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
            return 0;                                                          \
        }                                                                      \
    } while (0)

void dluma_str_copy(
    char* destination, unsigned int capacity, const char* source)
{
    unsigned int index = 0U;
    if (!destination || !capacity) return;
    if (!source) {
        destination[0] = '\0';
        return;
    }
    while (index + 1U < capacity && source[index]) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

int dluma_lua_config_identifier_valid(const char* value)
{
    unsigned int index;
    if (!value || !value[0]) return 0;
    for (index = 0U; value[index] && index < 64U; ++index) {
        char current = value[index];
        if (!((current >= 'a' && current <= 'z') ||
              (current >= 'A' && current <= 'Z') ||
              (current >= '0' && current <= '9') ||
              current == '_' || current == '-' || current == '.'))
            return 0;
    }
    return index != 0U && index <= 64U && !value[index] &&
           !(index == 1U && value[0] == '.') &&
           !(index == 2U && value[0] == '.' && value[1] == '.') &&
           value[index - 1U] != '.';
}

int dluma_lua_config_utf8_valid(
    const char* value,
    unsigned int maximum_bytes,
    unsigned int maximum_characters,
    int allow_empty)
{
    const unsigned char* cursor = (const unsigned char*)value;
    unsigned int bytes;
    unsigned int index = 0U;
    unsigned int characters = 0U;
    if (!value) return 0;
    bytes = (unsigned int)strlen(value);
    if ((!allow_empty && !bytes) || bytes > maximum_bytes)
        return 0;
    while (index < bytes) {
        unsigned char first = cursor[index];
        unsigned int width;
        if (first <= 0x7fU) {
            if (first < 0x20U || first == 0x7fU) return 0;
            width = 1U;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            width = 2U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xc0U) != 0x80U)
                return 0;
        } else if (first >= 0xe0U && first <= 0xefU) {
            width = 3U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xc0U) != 0x80U ||
                (cursor[index + 2U] & 0xc0U) != 0x80U ||
                (first == 0xe0U && cursor[index + 1U] < 0xa0U) ||
                (first == 0xedU && cursor[index + 1U] >= 0xa0U))
                return 0;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            width = 4U;
            if (index + width > bytes ||
                (cursor[index + 1U] & 0xc0U) != 0x80U ||
                (cursor[index + 2U] & 0xc0U) != 0x80U ||
                (cursor[index + 3U] & 0xc0U) != 0x80U ||
                (first == 0xf0U && cursor[index + 1U] < 0x90U) ||
                (first == 0xf4U && cursor[index + 1U] >= 0x90U))
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

void dluma_lua_config_compute_fingerprint(DlumaLuaConfigSchema* schema)
{
    unsigned int index;
    if (!schema) return;
    for (index = 0U; index < 64U; ++index)
        schema->fingerprint[index] = 'A';
    schema->fingerprint[64] = '\0';
}

static int parse_schema(
    const char* json,
    DlumaLuaConfigSchema* schema,
    char* diagnostic,
    unsigned int diagnostic_capacity)
{
    return dluma_native_mod_config_parse_json(
        json, (unsigned int)strlen(json), schema,
        diagnostic, diagnostic_capacity);
}

static int test_all_setting_types_and_exact_fields(void)
{
    static const char valid[] =
        "{"
        "\"schemaVersion\":1,"
        "\"categories\":[{"
        "\"id\":\"general\","
        "\"name\":\"General\","
        "\"settings\":["
        "{\"key\":\"enabled\",\"name\":\"Enabled\","
        "\"description\":\"Boolean value.\","
        "\"type\":\"boolean\",\"default\":true},"
        "{\"key\":\"amount\",\"name\":\"Amount\","
        "\"description\":\"Integer value.\","
        "\"type\":\"integer\",\"default\":0,"
        "\"min\":-20,\"max\":20,\"step\":10},"
        "{\"key\":\"mode\",\"name\":\"Mode\","
        "\"description\":\"Enum value.\","
        "\"type\":\"enum\",\"default\":\"normal\","
        "\"options\":["
        "{\"value\":\"normal\",\"name\":\"Normal\"},"
        "{\"value\":\"dynamic\",\"name\":\"Dynamic\"}]},"
        "{\"key\":\"label\",\"name\":\"Label\","
        "\"description\":\"Text value.\","
        "\"type\":\"text\",\"default\":\"road\","
        "\"maxLength\":8},"
        "{\"key\":\"toggle_view\",\"name\":\"Toggle view\","
        "\"description\":\"Change the camera view.\","
        "\"type\":\"keybind\",\"default\":84}"
        "]}]}";
    DlumaLuaConfigSchema schema;
    char diagnostic[96];
    memset(&schema, 0, sizeof(schema));
    diagnostic[0] = '\0';
    CHECK(parse_schema(
        valid, &schema, diagnostic,
        (unsigned int)sizeof(diagnostic)));
    CHECK(diagnostic[0] == '\0');
    CHECK(schema.schema_version == 1U);
    CHECK(schema.category_count == 1U);
    CHECK(schema.setting_count == 5U);
    CHECK(schema.settings[0].kind == DLUMA_LUA_CONFIG_BOOLEAN);
    CHECK(schema.settings[0].default_integer == 1);
    CHECK(schema.settings[1].kind == DLUMA_LUA_CONFIG_INTEGER);
    CHECK(schema.settings[1].minimum == -20);
    CHECK(schema.settings[1].maximum == 20);
    CHECK(schema.settings[1].step == 10U);
    CHECK(schema.settings[2].kind == DLUMA_LUA_CONFIG_ENUM);
    CHECK(schema.settings[2].option_count == 2U);
    CHECK(strcmp(schema.settings[2].default_text, "normal") == 0);
    CHECK(schema.settings[3].kind == DLUMA_LUA_CONFIG_TEXT);
    CHECK(schema.settings[3].max_length == 8U);
    CHECK(strcmp(schema.settings[3].default_text, "road") == 0);
    CHECK(schema.settings[4].kind == DLUMA_LUA_CONFIG_INTEGER);
    CHECK(schema.settings[4].editor == DLUMA_LUA_CONFIG_EDITOR_KEYBIND);
    CHECK(schema.settings[4].default_integer == 84);
    CHECK(schema.settings[4].minimum == 1);
    CHECK(schema.settings[4].maximum == 254);
    CHECK(schema.settings[4].step == 1U);
    CHECK(strlen(schema.fingerprint) == 64U);
    return 1;
}

static int test_malformed_duplicate_unknown_and_limits_fail_closed(void)
{
    static const char duplicate_root[] =
        "{\"schemaVersion\":1,\"schemaVersion\":1,\"categories\":[]}";
    static const char unknown_setting[] =
        "{\"schemaVersion\":1,\"categories\":[{"
        "\"id\":\"one\",\"name\":\"One\",\"settings\":[{"
        "\"key\":\"enabled\",\"name\":\"Enabled\","
        "\"description\":\"Value.\",\"type\":\"boolean\","
        "\"default\":true,\"unexpected\":false}]}]}";
    static const char duplicate_setting_id[] =
        "{\"schemaVersion\":1,\"categories\":[{"
        "\"id\":\"one\",\"name\":\"One\",\"settings\":["
        "{\"key\":\"same\",\"name\":\"First\","
        "\"description\":\"Value.\",\"type\":\"boolean\","
        "\"default\":true},"
        "{\"key\":\"same\",\"name\":\"Second\","
        "\"description\":\"Value.\",\"type\":\"boolean\","
        "\"default\":false}]}]}";
    static const char invalid_integer[] =
        "{\"schemaVersion\":1,\"categories\":[{"
        "\"id\":\"one\",\"name\":\"One\",\"settings\":[{"
        "\"key\":\"amount\",\"name\":\"Amount\","
        "\"description\":\"Value.\",\"type\":\"integer\","
        "\"default\":5,\"min\":0,\"max\":10,\"step\":4}]}]}";
    static const char duplicate_enum_value[] =
        "{\"schemaVersion\":1,\"categories\":[{"
        "\"id\":\"one\",\"name\":\"One\",\"settings\":[{"
        "\"key\":\"mode\",\"name\":\"Mode\","
        "\"description\":\"Value.\",\"type\":\"enum\","
        "\"default\":\"same\",\"options\":["
        "{\"value\":\"same\",\"name\":\"First\"},"
        "{\"value\":\"same\",\"name\":\"Second\"}]}]}]}";
    static const char unsupported_version[] =
        "{\"schemaVersion\":2,\"categories\":[{"
        "\"id\":\"one\",\"name\":\"One\",\"settings\":[{"
        "\"key\":\"enabled\",\"name\":\"Enabled\","
        "\"description\":\"Value.\",\"type\":\"boolean\","
        "\"default\":true}]}]}";
    static const char invalid_keybind[] =
        "{\"schemaVersion\":1,\"categories\":[{"
        "\"id\":\"controls\",\"name\":\"Controls\",\"settings\":[{"
        "\"key\":\"toggle_view\",\"name\":\"Toggle view\","
        "\"description\":\"Change the camera view.\",\"type\":\"keybind\","
        "\"default\":255}]}]}";
    const char* invalid[] = {
        duplicate_root,
        unknown_setting,
        duplicate_setting_id,
        invalid_integer,
        duplicate_enum_value,
        unsupported_version,
        invalid_keybind
    };
    unsigned int index;
    for (index = 0U;
         index < (unsigned int)(sizeof(invalid) / sizeof(invalid[0]));
         ++index) {
        DlumaLuaConfigSchema schema;
        char diagnostic[96];
        memset(&schema, 0, sizeof(schema));
        diagnostic[0] = '\0';
        CHECK(!parse_schema(
            invalid[index], &schema, diagnostic,
            (unsigned int)sizeof(diagnostic)));
        CHECK(diagnostic[0] != '\0');
        CHECK(schema.fingerprint[0] == '\0');
    }
    {
        DlumaLuaConfigSchema schema;
        char diagnostic[96];
        CHECK(!dluma_native_mod_config_parse_json(
            "{}", DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES + 1U,
            &schema, diagnostic,
            (unsigned int)sizeof(diagnostic)));
        CHECK(strcmp(diagnostic, "schema_limits") == 0);
    }
    return 1;
}

static int test_config_manifest_paths_are_canonical(void)
{
    static const char* invalid[] = {
        "",
        "config.json",
        "/data/config.json",
        "data/../config.json",
        "data\\config.json",
        "data/C:/config.json",
        "data/NUL.json",
        "data/trailing./config.json",
        "data/config.json ",
        "data/config.JSON"
    };
    unsigned int index;
    CHECK(dluma_native_mod_config_path_valid(
        "data/example/config.json"));
    CHECK(dluma_native_mod_config_path_valid(
        "data/another/category/config.json"));
    for (index = 0U;
         index < (unsigned int)(sizeof(invalid) / sizeof(invalid[0]));
         ++index)
        CHECK(!dluma_native_mod_config_path_valid(invalid[index]));
    return 1;
}

int main(void)
{
    unsigned int passed = 0U;
    if (!test_all_setting_types_and_exact_fields()) return 1;
    ++passed;
    puts("[PASS] all native config setting types");
    if (!test_malformed_duplicate_unknown_and_limits_fail_closed())
        return 1;
    ++passed;
    puts("[PASS] malformed native config fails closed");
    if (!test_config_manifest_paths_are_canonical()) return 1;
    ++passed;
    puts("[PASS] native config paths are canonical");
    printf("native mod config fake cases passed: %u\n", passed);
    return 0;
}
