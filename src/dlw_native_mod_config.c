#include "dlw_native_mod_config.h"

typedef struct DlumaNativeConfigJson {
    const unsigned char *cursor;
    const unsigned char *end;
} DlumaNativeConfigJson;

static void dluma_native_config_diagnostic(
    char *out, unsigned int capacity, const char *value)
{
    if (!out || !capacity || out[0]) return;
    dluma_str_copy(
        out, capacity, value ? value : "dynamic_or_malformed");
}

static int dluma_native_config_text_equal(
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

static unsigned char dluma_native_config_ascii_lower(
    unsigned char value)
{
    if (value >= 'A' && value <= 'Z')
        return (unsigned char)(value + ('a' - 'A'));
    return value;
}

static int dluma_native_config_segment_equals(
    const char *value, unsigned int length, const char *expected)
{
    unsigned int index = 0;
    if (!value || !expected) return 0;
    while (expected[index]) {
        if (index >= length ||
            dluma_native_config_ascii_lower(
                (unsigned char)value[index]) !=
            dluma_native_config_ascii_lower(
                (unsigned char)expected[index]))
            return 0;
        ++index;
    }
    return index == length;
}

static int dluma_native_config_segment_valid(
    const char *value, unsigned int length)
{
    unsigned int base_length = 0;
    if (!value || !length ||
        (length == 1U && value[0] == '.') ||
        (length == 2U && value[0] == '.' && value[1] == '.') ||
        value[length - 1U] == '.' || value[length - 1U] == ' ')
        return 0;
    while (base_length < length && value[base_length] != '.')
        ++base_length;
    if (dluma_native_config_segment_equals(value, base_length, "CON") ||
        dluma_native_config_segment_equals(value, base_length, "PRN") ||
        dluma_native_config_segment_equals(value, base_length, "AUX") ||
        dluma_native_config_segment_equals(value, base_length, "NUL") ||
        dluma_native_config_segment_equals(value, base_length, "CLOCK$") ||
        dluma_native_config_segment_equals(value, base_length, "CONIN$") ||
        dluma_native_config_segment_equals(value, base_length, "CONOUT$"))
        return 0;
    if (base_length == 4U &&
        value[3] >= '1' && value[3] <= '9' &&
        ((dluma_native_config_ascii_lower((unsigned char)value[0]) == 'c' &&
          dluma_native_config_ascii_lower((unsigned char)value[1]) == 'o' &&
          dluma_native_config_ascii_lower((unsigned char)value[2]) == 'm') ||
         (dluma_native_config_ascii_lower((unsigned char)value[0]) == 'l' &&
          dluma_native_config_ascii_lower((unsigned char)value[1]) == 'p' &&
          dluma_native_config_ascii_lower((unsigned char)value[2]) == 't')))
        return 0;
    return 1;
}

int dluma_native_mod_config_path_valid(const char *path)
{
    static const char prefix[] = "data/";
    static const char suffix[] = ".json";
    unsigned int length;
    unsigned int index;
    unsigned int segment_start = 0;
    if (!path || !path[0]) return 0;
    length = (unsigned int)strlen(path);
    if (length >= DLUMA_NATIVE_MOD_CONFIG_PATH_CAPACITY ||
        length <= sizeof(prefix) - 1U + sizeof(suffix) - 1U ||
        memcmp(path, prefix, sizeof(prefix) - 1U) != 0 ||
        memcmp(
            path + length - (sizeof(suffix) - 1U),
            suffix, sizeof(suffix) - 1U) != 0)
        return 0;
    for (index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)path[index];
        if (value < 0x21U || value > 0x7eU ||
            value == '\\' || value == ':')
            return 0;
        if (value == '/') {
            if (!dluma_native_config_segment_valid(
                    path + segment_start, index - segment_start))
                return 0;
            segment_start = index + 1U;
        }
    }
    return dluma_native_config_segment_valid(
        path + segment_start, length - segment_start);
}

static void dluma_native_config_space(DlumaNativeConfigJson *json)
{
    while (json && json->cursor < json->end &&
           (*json->cursor == ' ' || *json->cursor == '\t' ||
            *json->cursor == '\r' || *json->cursor == '\n'))
        ++json->cursor;
}

static int dluma_native_config_take(
    DlumaNativeConfigJson *json, unsigned char expected)
{
    dluma_native_config_space(json);
    if (!json || json->cursor >= json->end ||
        *json->cursor != expected)
        return 0;
    ++json->cursor;
    return 1;
}

static int dluma_native_config_hex(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int dluma_native_config_u16_escape(
    DlumaNativeConfigJson *json, unsigned int *value_out)
{
    unsigned int value = 0;
    unsigned int index;
    if (!json || !value_out ||
        (unsigned int)(json->end - json->cursor) < 4U)
        return 0;
    for (index = 0; index < 4U; ++index) {
        int digit = dluma_native_config_hex(json->cursor[index]);
        if (digit < 0) return 0;
        value = (value << 4) | (unsigned int)digit;
    }
    json->cursor += 4;
    *value_out = value;
    return 1;
}

static int dluma_native_config_append_codepoint(
    char *out, unsigned int capacity,
    unsigned int *used, unsigned int codepoint)
{
    unsigned char encoded[4];
    unsigned int count;
    if (!out || !capacity || !used || !codepoint ||
        codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU))
        return 0;
    if (codepoint <= 0x7fU) {
        encoded[0] = (unsigned char)codepoint;
        count = 1U;
    } else if (codepoint <= 0x7ffU) {
        encoded[0] = (unsigned char)(0xc0U | (codepoint >> 6));
        encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 2U;
    } else if (codepoint <= 0xffffU) {
        encoded[0] = (unsigned char)(0xe0U | (codepoint >> 12));
        encoded[1] =
            (unsigned char)(0x80U | ((codepoint >> 6) & 0x3fU));
        encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 3U;
    } else {
        encoded[0] = (unsigned char)(0xf0U | (codepoint >> 18));
        encoded[1] =
            (unsigned char)(0x80U | ((codepoint >> 12) & 0x3fU));
        encoded[2] =
            (unsigned char)(0x80U | ((codepoint >> 6) & 0x3fU));
        encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 4U;
    }
    if (*used + count + 1U > capacity) return 0;
    memcpy(out + *used, encoded, count);
    *used += count;
    out[*used] = 0;
    return 1;
}

static int dluma_native_config_string(
    DlumaNativeConfigJson *json, char *out, unsigned int capacity)
{
    unsigned int used = 0;
    if (!json || !out || capacity < 1U ||
        !dluma_native_config_take(json, '"'))
        return 0;
    out[0] = 0;
    while (json->cursor < json->end) {
        unsigned char value = *json->cursor++;
        if (value == '"') {
            out[used] = 0;
            return 1;
        }
        if (value < 0x20U || value == 0x7fU) return 0;
        if (value == '\\') {
            unsigned int codepoint;
            if (json->cursor >= json->end) return 0;
            value = *json->cursor++;
            if (value == '"' || value == '\\' || value == '/') {
                codepoint = value;
            } else if (value == 'b') {
                codepoint = 8U;
            } else if (value == 'f') {
                codepoint = 12U;
            } else if (value == 'n') {
                codepoint = 10U;
            } else if (value == 'r') {
                codepoint = 13U;
            } else if (value == 't') {
                codepoint = 9U;
            } else if (value == 'u') {
                unsigned int high;
                if (!dluma_native_config_u16_escape(json, &high) ||
                    high == 0U)
                    return 0;
                if (high >= 0xd800U && high <= 0xdbffU) {
                    unsigned int low;
                    if ((unsigned int)(json->end - json->cursor) < 6U ||
                        json->cursor[0] != '\\' ||
                        json->cursor[1] != 'u')
                        return 0;
                    json->cursor += 2;
                    if (!dluma_native_config_u16_escape(json, &low) ||
                        low < 0xdc00U || low > 0xdfffU)
                        return 0;
                    codepoint =
                        0x10000U + ((high - 0xd800U) << 10) +
                        (low - 0xdc00U);
                } else {
                    if (high >= 0xdc00U && high <= 0xdfffU)
                        return 0;
                    codepoint = high;
                }
            } else {
                return 0;
            }
            if (codepoint < 0x20U || codepoint == 0x7fU ||
                !dluma_native_config_append_codepoint(
                    out, capacity, &used, codepoint))
                return 0;
            continue;
        }
        if (used + 2U > capacity) return 0;
        out[used++] = (char)value;
        out[used] = 0;
    }
    return 0;
}

static int dluma_native_config_literal(
    DlumaNativeConfigJson *json, const char *literal)
{
    unsigned int length;
    dluma_native_config_space(json);
    if (!json || !literal) return 0;
    length = (unsigned int)strlen(literal);
    if ((unsigned int)(json->end - json->cursor) < length ||
        memcmp(json->cursor, literal, length) != 0)
        return 0;
    json->cursor += length;
    return 1;
}

static int dluma_native_config_boolean(
    DlumaNativeConfigJson *json, int *out)
{
    if (!out) return 0;
    if (dluma_native_config_literal(json, "true")) {
        *out = 1;
        return 1;
    }
    if (dluma_native_config_literal(json, "false")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int dluma_native_config_i32(
    DlumaNativeConfigJson *json, int *out)
{
    const unsigned char *cursor;
    ULONGLONG value = 0;
    ULONGLONG limit;
    int negative = 0;
    dluma_native_config_space(json);
    if (!json || !out || json->cursor >= json->end) return 0;
    cursor = json->cursor;
    if (*cursor == '-') {
        negative = 1;
        ++cursor;
    }
    if (cursor >= json->end || *cursor < '0' || *cursor > '9')
        return 0;
    if (*cursor == '0' && cursor + 1 < json->end &&
        cursor[1] >= '0' && cursor[1] <= '9')
        return 0;
    limit = negative ? 2147483648ULL : 2147483647ULL;
    while (cursor < json->end && *cursor >= '0' && *cursor <= '9') {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (limit - digit) / 10ULL) return 0;
        value = value * 10ULL + digit;
        ++cursor;
    }
    json->cursor = cursor;
    if (negative)
        *out = value == 2147483648ULL
            ? (-2147483647 - 1) : -(int)value;
    else
        *out = (int)value;
    return 1;
}

static int dluma_native_config_u32(
    DlumaNativeConfigJson *json, unsigned int *out)
{
    int value;
    if (!out || !dluma_native_config_i32(json, &value) || value < 0)
        return 0;
    *out = (unsigned int)value;
    return 1;
}

static int dluma_native_config_object_next(
    DlumaNativeConfigJson *json, int *first)
{
    if (!json || !first) return 0;
    dluma_native_config_space(json);
    if (json->cursor < json->end && *json->cursor == '}')
        return 0;
    if (!*first && !dluma_native_config_take(json, ','))
        return -1;
    *first = 0;
    return 1;
}

static int dluma_native_config_array_next(
    DlumaNativeConfigJson *json, int *first)
{
    if (!json || !first) return 0;
    dluma_native_config_space(json);
    if (json->cursor < json->end && *json->cursor == ']')
        return 0;
    if (!*first && !dluma_native_config_take(json, ','))
        return -1;
    *first = 0;
    return 1;
}

static int dluma_native_config_parse_option(
    DlumaNativeConfigJson *json,
    DlumaLuaConfigEnumOption *option)
{
    unsigned int fields = 0;
    int first = 1;
    if (!json || !option ||
        !dluma_native_config_take(json, '{'))
        return 0;
    memset(option, 0, sizeof(*option));
    for (;;) {
        char key[32];
        int next = dluma_native_config_object_next(json, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_native_config_string(
                json, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_native_config_take(json, ':'))
            return 0;
        if (dluma_native_config_text_equal(key, "value")) {
            if ((fields & 1U) ||
                !dluma_native_config_string(
                    json, option->value,
                    DLUMA_ARRAY_COUNT(option->value)))
                return 0;
            fields |= 1U;
        } else if (dluma_native_config_text_equal(key, "name")) {
            if ((fields & 2U) ||
                !dluma_native_config_string(
                    json, option->name,
                    DLUMA_ARRAY_COUNT(option->name)))
                return 0;
            fields |= 2U;
        } else {
            return 0;
        }
    }
    if (!dluma_native_config_take(json, '}') || fields != 3U ||
        !dluma_lua_config_identifier_valid(option->value) ||
        !dluma_lua_config_utf8_valid(
            option->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0))
        return 0;
    return 1;
}

static int dluma_native_config_parse_options(
    DlumaNativeConfigJson *json,
    DlumaLuaConfigSetting *setting)
{
    int first = 1;
    if (!json || !setting ||
        !dluma_native_config_take(json, '['))
        return 0;
    setting->option_count = 0;
    for (;;) {
        unsigned int earlier;
        int next = dluma_native_config_array_next(json, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (setting->option_count >=
                DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS ||
            !dluma_native_config_parse_option(
                json,
                &setting->options[setting->option_count]))
            return 0;
        for (earlier = 0;
             earlier < setting->option_count;
             ++earlier)
            if (dluma_native_config_text_equal(
                    setting->options[earlier].value,
                    setting->options[
                        setting->option_count].value))
                return 0;
        ++setting->option_count;
    }
    return dluma_native_config_take(json, ']');
}

typedef enum DlumaNativeConfigDefaultKind {
    DLUMA_NATIVE_CONFIG_DEFAULT_NONE = 0,
    DLUMA_NATIVE_CONFIG_DEFAULT_BOOLEAN = 1,
    DLUMA_NATIVE_CONFIG_DEFAULT_INTEGER = 2,
    DLUMA_NATIVE_CONFIG_DEFAULT_STRING = 3
} DlumaNativeConfigDefaultKind;

static int dluma_native_config_parse_setting(
    DlumaNativeConfigJson *json,
    DlumaLuaConfigSetting *setting,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    char type[24];
    char default_text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    int default_integer = 0;
    unsigned int default_kind = DLUMA_NATIVE_CONFIG_DEFAULT_NONE;
    unsigned int fields = 0;
    int first = 1;
    unsigned int option_index;
    if (!json || !setting ||
        !dluma_native_config_take(json, '{'))
        return 0;
    memset(setting, 0, sizeof(*setting));
    type[0] = 0;
    default_text[0] = 0;
    for (;;) {
        char key[32];
        int next = dluma_native_config_object_next(json, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_native_config_string(
                json, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_native_config_take(json, ':'))
            return 0;
        if (dluma_native_config_text_equal(key, "key")) {
            if ((fields & 0x001U) ||
                !dluma_native_config_string(
                    json, setting->key,
                    DLUMA_ARRAY_COUNT(setting->key)))
                return 0;
            fields |= 0x001U;
        } else if (dluma_native_config_text_equal(key, "name")) {
            if ((fields & 0x002U) ||
                !dluma_native_config_string(
                    json, setting->name,
                    DLUMA_ARRAY_COUNT(setting->name)))
                return 0;
            fields |= 0x002U;
        } else if (dluma_native_config_text_equal(
                       key, "description")) {
            if ((fields & 0x004U) ||
                !dluma_native_config_string(
                    json, setting->description,
                    DLUMA_ARRAY_COUNT(setting->description)))
                return 0;
            fields |= 0x004U;
        } else if (dluma_native_config_text_equal(key, "type")) {
            if ((fields & 0x008U) ||
                !dluma_native_config_string(
                    json, type, DLUMA_ARRAY_COUNT(type)))
                return 0;
            fields |= 0x008U;
        } else if (dluma_native_config_text_equal(key, "default")) {
            const unsigned char *saved;
            if (fields & 0x010U) return 0;
            dluma_native_config_space(json);
            saved = json->cursor;
            if (dluma_native_config_boolean(json, &default_integer))
                default_kind = DLUMA_NATIVE_CONFIG_DEFAULT_BOOLEAN;
            else {
                json->cursor = saved;
                if (dluma_native_config_i32(json, &default_integer))
                    default_kind = DLUMA_NATIVE_CONFIG_DEFAULT_INTEGER;
                else {
                    json->cursor = saved;
                    if (!dluma_native_config_string(
                            json, default_text,
                            DLUMA_ARRAY_COUNT(default_text)))
                        return 0;
                    default_kind = DLUMA_NATIVE_CONFIG_DEFAULT_STRING;
                }
            }
            fields |= 0x010U;
        } else if (dluma_native_config_text_equal(key, "min")) {
            if ((fields & 0x020U) ||
                !dluma_native_config_i32(json, &setting->minimum))
                return 0;
            fields |= 0x020U;
        } else if (dluma_native_config_text_equal(key, "max")) {
            if ((fields & 0x040U) ||
                !dluma_native_config_i32(json, &setting->maximum))
                return 0;
            fields |= 0x040U;
        } else if (dluma_native_config_text_equal(key, "step")) {
            if ((fields & 0x080U) ||
                !dluma_native_config_u32(json, &setting->step))
                return 0;
            fields |= 0x080U;
        } else if (dluma_native_config_text_equal(
                       key, "maxLength")) {
            if ((fields & 0x100U) ||
                !dluma_native_config_u32(
                    json, &setting->max_length))
                return 0;
            fields |= 0x100U;
        } else if (dluma_native_config_text_equal(key, "options")) {
            if ((fields & 0x200U) ||
                !dluma_native_config_parse_options(json, setting))
                return 0;
            fields |= 0x200U;
        } else {
            dluma_native_config_diagnostic(
                diagnostic, diagnostic_capacity,
                "invalid_setting");
            return 0;
        }
    }
    if (!dluma_native_config_take(json, '}') ||
        (fields & 0x01fU) != 0x01fU ||
        !dluma_lua_config_identifier_valid(setting->key) ||
        !dluma_lua_config_utf8_valid(
            setting->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0) ||
        !dluma_lua_config_utf8_valid(
            setting->description,
            DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY - 1U, 0)) {
        dluma_native_config_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid_setting");
        return 0;
    }
    if (dluma_native_config_text_equal(type, "boolean")) {
        if (default_kind != DLUMA_NATIVE_CONFIG_DEFAULT_BOOLEAN ||
            (fields & (0x020U | 0x040U | 0x080U |
                       0x100U | 0x200U)))
            goto invalid_default;
        setting->kind = DLUMA_LUA_CONFIG_BOOLEAN;
        setting->default_integer = default_integer;
    } else if (dluma_native_config_text_equal(type, "integer")) {
        LONGLONG span;
        LONGLONG default_offset;
        if (default_kind != DLUMA_NATIVE_CONFIG_DEFAULT_INTEGER ||
            (fields & (0x020U | 0x040U | 0x080U)) !=
                (0x020U | 0x040U | 0x080U) ||
            (fields & (0x100U | 0x200U)) ||
            !setting->step ||
            setting->minimum > setting->maximum ||
            default_integer < setting->minimum ||
            default_integer > setting->maximum)
            goto invalid_default;
        span = (LONGLONG)setting->maximum -
               (LONGLONG)setting->minimum;
        default_offset = (LONGLONG)default_integer -
                         (LONGLONG)setting->minimum;
        if ((ULONGLONG)span % setting->step ||
            (ULONGLONG)default_offset % setting->step)
            goto invalid_default;
        setting->kind = DLUMA_LUA_CONFIG_INTEGER;
        setting->default_integer = default_integer;
    } else if (dluma_native_config_text_equal(type, "keybind")) {
        if (default_kind != DLUMA_NATIVE_CONFIG_DEFAULT_INTEGER ||
            default_integer < 1 || default_integer > 254 ||
            ((fields & 0x020U) && setting->minimum != 1) ||
            ((fields & 0x040U) && setting->maximum != 254) ||
            ((fields & 0x080U) && setting->step != 1U) ||
            (fields & (0x100U | 0x200U)))
            goto invalid_default;
        setting->kind = DLUMA_LUA_CONFIG_INTEGER;
        setting->editor = DLUMA_LUA_CONFIG_EDITOR_KEYBIND;
        setting->default_integer = default_integer;
        setting->minimum = 1;
        setting->maximum = 254;
        setting->step = 1U;
    } else if (dluma_native_config_text_equal(type, "enum")) {
        int default_found = 0;
        if (default_kind != DLUMA_NATIVE_CONFIG_DEFAULT_STRING ||
            (fields & 0x200U) == 0U ||
            (fields & (0x020U | 0x040U | 0x080U | 0x100U)) ||
            setting->option_count < 2U)
            goto invalid_default;
        for (option_index = 0;
             option_index < setting->option_count;
             ++option_index)
            if (dluma_native_config_text_equal(
                    setting->options[option_index].value,
                    default_text))
                default_found = 1;
        if (!default_found) goto invalid_default;
        setting->kind = DLUMA_LUA_CONFIG_ENUM;
        dluma_str_copy(
            setting->default_text,
            DLUMA_ARRAY_COUNT(setting->default_text),
            default_text);
    } else if (dluma_native_config_text_equal(type, "text")) {
        if (default_kind != DLUMA_NATIVE_CONFIG_DEFAULT_STRING ||
            (fields & 0x100U) == 0U ||
            (fields & (0x020U | 0x040U | 0x080U | 0x200U)) ||
            !setting->max_length ||
            setting->max_length >
                DLUMA_LUA_CONFIG_MAX_TEXT_CHARACTERS ||
            !dluma_lua_config_utf8_valid(
                default_text, DLUMA_LUA_CONFIG_MAX_TEXT_BYTES,
                setting->max_length, 1))
            goto invalid_default;
        setting->kind = DLUMA_LUA_CONFIG_TEXT;
        dluma_str_copy(
            setting->default_text,
            DLUMA_ARRAY_COUNT(setting->default_text),
            default_text);
    } else {
        dluma_native_config_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid_setting");
        return 0;
    }
    return 1;

invalid_default:
    dluma_native_config_diagnostic(
        diagnostic, diagnostic_capacity,
        "invalid_default");
    return 0;
}

static int dluma_native_config_parse_category(
    DlumaNativeConfigJson *json,
    DlumaLuaConfigSchema *schema,
    DlumaLuaConfigCategory *category,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    unsigned int fields = 0;
    int first = 1;
    if (!json || !schema || !category ||
        !dluma_native_config_take(json, '{'))
        return 0;
    memset(category, 0, sizeof(*category));
    for (;;) {
        char key[32];
        int next = dluma_native_config_object_next(json, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_native_config_string(
                json, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_native_config_take(json, ':'))
            return 0;
        if (dluma_native_config_text_equal(key, "id")) {
            if ((fields & 1U) ||
                !dluma_native_config_string(
                    json, category->id,
                    DLUMA_ARRAY_COUNT(category->id)))
                return 0;
            fields |= 1U;
        } else if (dluma_native_config_text_equal(key, "name")) {
            if ((fields & 2U) ||
                !dluma_native_config_string(
                    json, category->name,
                    DLUMA_ARRAY_COUNT(category->name)))
                return 0;
            fields |= 2U;
        } else if (dluma_native_config_text_equal(
                       key, "settings")) {
            int setting_first = 1;
            if ((fields & 4U) ||
                !dluma_native_config_take(json, '['))
                return 0;
            fields |= 4U;
            category->first_setting = schema->setting_count;
            for (;;) {
                unsigned int earlier;
                DlumaLuaConfigSetting *setting;
                int setting_next =
                    dluma_native_config_array_next(
                        json, &setting_first);
                if (setting_next < 0) return 0;
                if (!setting_next) break;
                if (category->setting_count >=
                        DLUMA_LUA_CONFIG_MAX_SETTINGS_PER_CATEGORY ||
                    schema->setting_count >=
                        DLUMA_LUA_CONFIG_MAX_SETTINGS) {
                    dluma_native_config_diagnostic(
                        diagnostic, diagnostic_capacity,
                        "schema_limits");
                    return 0;
                }
                setting =
                    &schema->settings[schema->setting_count];
                if (!dluma_native_config_parse_setting(
                        json, setting, diagnostic,
                        diagnostic_capacity))
                    return 0;
                for (earlier = 0;
                     earlier < schema->setting_count;
                     ++earlier)
                    if (dluma_native_config_text_equal(
                            schema->settings[earlier].key,
                            setting->key)) {
                        dluma_native_config_diagnostic(
                            diagnostic, diagnostic_capacity,
                            "duplicate_identifier");
                        return 0;
                    }
                ++schema->setting_count;
                ++category->setting_count;
            }
            if (!dluma_native_config_take(json, ']')) return 0;
        } else {
            dluma_native_config_diagnostic(
                diagnostic, diagnostic_capacity,
                "invalid_category");
            return 0;
        }
    }
    if (!dluma_native_config_take(json, '}') ||
        fields != 7U || !category->setting_count ||
        !dluma_lua_config_identifier_valid(category->id) ||
        !dluma_lua_config_utf8_valid(
            category->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0)) {
        dluma_native_config_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid_category");
        return 0;
    }
    return 1;
}

int dluma_mod_config_parse_json(
    const char *bytes,
    unsigned int size,
    DlumaLuaConfigSchema *schema,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    DlumaNativeConfigJson json;
    unsigned int fields = 0;
    int first = 1;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = 0;
    if (!schema) return 0;
    memset(schema, 0, sizeof(*schema));
    if (!bytes || !size ||
        size > DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES) {
        dluma_native_config_diagnostic(
            diagnostic, diagnostic_capacity,
            "schema_limits");
        return 0;
    }
    json.cursor = (const unsigned char*)bytes;
    json.end = json.cursor + size;
    if (!dluma_native_config_take(&json, '{')) goto malformed;
    for (;;) {
        char key[32];
        int next = dluma_native_config_object_next(&json, &first);
        if (next < 0) goto malformed;
        if (!next) break;
        if (!dluma_native_config_string(
                &json, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_native_config_take(&json, ':'))
            goto malformed;
        if (dluma_native_config_text_equal(
                key, "schemaVersion")) {
            if ((fields & 1U) ||
                !dluma_native_config_u32(
                    &json, &schema->schema_version))
                goto malformed;
            fields |= 1U;
        } else if (dluma_native_config_text_equal(
                       key, "categories")) {
            int category_first = 1;
            if ((fields & 2U) ||
                !dluma_native_config_take(&json, '['))
                goto malformed;
            fields |= 2U;
            for (;;) {
                unsigned int earlier;
                DlumaLuaConfigCategory *category;
                int category_next =
                    dluma_native_config_array_next(
                        &json, &category_first);
                if (category_next < 0) goto malformed;
                if (!category_next) break;
                if (schema->category_count >=
                    DLUMA_LUA_CONFIG_MAX_CATEGORIES) {
                    dluma_native_config_diagnostic(
                        diagnostic, diagnostic_capacity,
                        "schema_limits");
                    return 0;
                }
                category =
                    &schema->categories[schema->category_count];
                if (!dluma_native_config_parse_category(
                        &json, schema, category,
                        diagnostic, diagnostic_capacity)) {
                    dluma_native_config_diagnostic(
                        diagnostic, diagnostic_capacity,
                        "dynamic_or_malformed");
                    return 0;
                }
                for (earlier = 0;
                     earlier < schema->category_count;
                     ++earlier)
                    if (dluma_native_config_text_equal(
                            schema->categories[earlier].id,
                            category->id)) {
                        dluma_native_config_diagnostic(
                            diagnostic, diagnostic_capacity,
                            "duplicate_identifier");
                        return 0;
                    }
                ++schema->category_count;
            }
            if (!dluma_native_config_take(&json, ']'))
                goto malformed;
        } else {
            goto malformed;
        }
    }
    if (!dluma_native_config_take(&json, '}')) goto malformed;
    dluma_native_config_space(&json);
    if (json.cursor != json.end || fields != 3U ||
        !schema->category_count || !schema->setting_count)
        goto malformed;
    if (schema->schema_version !=
        DLUMA_LUA_CONFIG_SCHEMA_VERSION) {
        dluma_native_config_diagnostic(
            diagnostic, diagnostic_capacity,
            "unsupported_schema_version");
        return 0;
    }
    dluma_lua_config_compute_fingerprint(schema);
    if (!schema->fingerprint[0]) goto malformed;
    return 1;

malformed:
    dluma_native_config_diagnostic(
        diagnostic, diagnostic_capacity,
        "dynamic_or_malformed");
    return 0;
}

int dluma_native_mod_config_parse_json(
    const char *bytes,
    unsigned int size,
    DlumaLuaConfigSchema *schema,
    char *diagnostic,
    unsigned int diagnostic_capacity)
{
    return dluma_mod_config_parse_json(
        bytes, size, schema, diagnostic, diagnostic_capacity);
}
