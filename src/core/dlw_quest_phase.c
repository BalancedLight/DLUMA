#include "dlw_quest_phase.h"

typedef struct DlumaQuestJsonKeyRef {
    unsigned int offset;
    unsigned int size;
} DlumaQuestJsonKeyRef;

typedef struct DlumaQuestJsonWriter {
    const unsigned char* begin;
    const unsigned char* cursor;
    const unsigned char* end;
    char* output;
    unsigned int output_capacity;
    unsigned int output_size;
    unsigned int depth;
    unsigned int nodes;
    unsigned int key_count;
    DlumaQuestJsonKeyRef keys[DLUMA_QUEST_PHASE_MAX_JSON_NODES];
    DlumaQuestPhaseDiagnostic* diagnostic;
} DlumaQuestJsonWriter;

static void dluma_qp_zero(void* memory, unsigned int size)
{
    unsigned char* cursor = (unsigned char*)memory;
    while (cursor && size--) *cursor++ = 0;
}

static unsigned int dluma_qp_length(const char* value)
{
    unsigned int size = 0;
    if (!value) return 0;
    while (value[size]) ++size;
    return size;
}

static int dluma_qp_copy(char* output, unsigned int capacity, const char* value)
{
    unsigned int size = dluma_qp_length(value);
    unsigned int index;
    if (!output || !capacity || !value || size + 1U > capacity) return 0;
    for (index = 0; index < size; ++index) output[index] = value[index];
    output[size] = 0;
    return 1;
}

static int dluma_qp_copy_n(
    char* output,
    unsigned int capacity,
    const unsigned char* value,
    unsigned int size)
{
    unsigned int index;
    if (!output || !capacity || (!value && size) || size + 1U > capacity)
        return 0;
    for (index = 0; index < size; ++index) output[index] = (char)value[index];
    output[size] = 0;
    return 1;
}

static int dluma_qp_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static int dluma_qp_fold_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    for (;;) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
        if (!a) return 1;
        ++index;
    }
}

static int dluma_qp_bytes_equal(
    const char* left,
    const char* right,
    unsigned int size)
{
    unsigned int index;
    if ((!left || !right) && size) return 0;
    for (index = 0; index < size; ++index)
        if (left[index] != right[index]) return 0;
    return 1;
}

static void dluma_qp_diagnostic(
    DlumaQuestPhaseDiagnostic* diagnostic,
    unsigned int offset,
    const char* code,
    const char* message)
{
    if (!diagnostic) return;
    dluma_qp_zero(diagnostic, (unsigned int)sizeof(*diagnostic));
    diagnostic->byte_offset = offset;
    (void)dluma_qp_copy(
        diagnostic->code, (unsigned int)sizeof(diagnostic->code), code);
    (void)dluma_qp_copy(
        diagnostic->message, (unsigned int)sizeof(diagnostic->message),
        message);
}

static void dluma_qj_fail(
    DlumaQuestJsonWriter* writer,
    const char* code,
    const char* message)
{
    unsigned int offset = 0;
    if (!writer || !writer->diagnostic || writer->diagnostic->code[0]) return;
    if (writer->begin && writer->cursor && writer->cursor >= writer->begin)
        offset = (unsigned int)(writer->cursor - writer->begin);
    dluma_qp_diagnostic(writer->diagnostic, offset, code, message);
}

static int dluma_qj_put(DlumaQuestJsonWriter* writer, unsigned char value)
{
    if (!writer || !writer->output ||
        writer->output_size + 1U >= writer->output_capacity) {
        dluma_qj_fail(writer, "json_output_limit", "canonical JSON output is too large");
        return 0;
    }
    writer->output[writer->output_size++] = (char)value;
    return 1;
}

static int dluma_qj_put_utf8(
    DlumaQuestJsonWriter* writer,
    unsigned int codepoint,
    unsigned int* decoded_size)
{
    unsigned char bytes[4];
    unsigned int count;
    unsigned int index;
    if (!codepoint || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        dluma_qj_fail(writer, "json_utf8", "JSON contains an invalid Unicode scalar");
        return 0;
    }
    if (codepoint <= 0x7FU) {
        bytes[0] = (unsigned char)codepoint;
        count = 1U;
    } else if (codepoint <= 0x7FFU) {
        bytes[0] = (unsigned char)(0xC0U | (codepoint >> 6U));
        bytes[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 2U;
    } else if (codepoint <= 0xFFFFU) {
        bytes[0] = (unsigned char)(0xE0U | (codepoint >> 12U));
        bytes[1] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 3U;
    } else {
        bytes[0] = (unsigned char)(0xF0U | (codepoint >> 18U));
        bytes[1] = (unsigned char)(0x80U | ((codepoint >> 12U) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        bytes[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        count = 4U;
    }
    if (*decoded_size > DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES - count) {
        dluma_qj_fail(writer, "json_string_limit", "decoded JSON string exceeds 1024 bytes");
        return 0;
    }
    *decoded_size += count;
    for (index = 0; index < count; ++index)
        if (!dluma_qj_put(writer, bytes[index])) return 0;
    return 1;
}

static int dluma_qj_hex(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static int dluma_qj_u16(
    DlumaQuestJsonWriter* writer,
    unsigned int* output)
{
    unsigned int index;
    unsigned int value = 0;
    for (index = 0; index < 4U; ++index) {
        int digit;
        if (writer->cursor >= writer->end ||
            (digit = dluma_qj_hex(*writer->cursor++)) < 0) {
            dluma_qj_fail(writer, "json_escape", "invalid JSON Unicode escape");
            return 0;
        }
        value = (value << 4U) | (unsigned int)digit;
    }
    *output = value;
    return 1;
}

static int dluma_qj_raw_utf8(
    DlumaQuestJsonWriter* writer,
    unsigned int* decoded_size)
{
    unsigned char first;
    unsigned int count;
    unsigned int minimum;
    unsigned int codepoint;
    unsigned int index;
    if (writer->cursor >= writer->end) return 0;
    first = *writer->cursor;
    if (first < 0x80U) return 0;
    if ((first & 0xE0U) == 0xC0U) {
        count = 2U; minimum = 0x80U; codepoint = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
        count = 3U; minimum = 0x800U; codepoint = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
        count = 4U; minimum = 0x10000U; codepoint = first & 0x07U;
    } else {
        dluma_qj_fail(writer, "json_utf8", "invalid UTF-8 in JSON string");
        return 0;
    }
    if ((unsigned int)(writer->end - writer->cursor) < count) {
        dluma_qj_fail(writer, "json_utf8", "truncated UTF-8 in JSON string");
        return 0;
    }
    for (index = 1U; index < count; ++index) {
        unsigned char next = writer->cursor[index];
        if ((next & 0xC0U) != 0x80U) {
            dluma_qj_fail(writer, "json_utf8", "invalid UTF-8 continuation byte");
            return 0;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        dluma_qj_fail(writer, "json_utf8", "non-canonical UTF-8 in JSON string");
        return 0;
    }
    writer->cursor += count;
    return dluma_qj_put_utf8(writer, codepoint, decoded_size);
}

static int dluma_qj_string(
    DlumaQuestJsonWriter* writer,
    unsigned int* content_offset,
    unsigned int* content_size)
{
    unsigned int decoded_size = 0;
    if (writer->cursor >= writer->end || *writer->cursor++ != '"') {
        dluma_qj_fail(writer, "json_string", "expected a JSON string");
        return 0;
    }
    if (!dluma_qj_put(writer, '"')) return 0;
    if (content_offset) *content_offset = writer->output_size;
    while (writer->cursor < writer->end) {
        unsigned char value = *writer->cursor++;
        if (value == '"') {
            if (content_size && content_offset)
                *content_size = writer->output_size - *content_offset;
            return dluma_qj_put(writer, '"');
        }
        if (value == '\\') {
            unsigned char escape;
            unsigned int codepoint;
            if (writer->cursor >= writer->end) {
                dluma_qj_fail(writer, "json_escape", "truncated JSON escape");
                return 0;
            }
            escape = *writer->cursor++;
            if (escape == 'u') {
                unsigned int first;
                if (!dluma_qj_u16(writer, &first)) return 0;
                codepoint = first;
                if (first >= 0xD800U && first <= 0xDBFFU) {
                    unsigned int second;
                    if ((unsigned int)(writer->end - writer->cursor) < 6U ||
                        writer->cursor[0] != '\\' || writer->cursor[1] != 'u') {
                        dluma_qj_fail(writer, "json_escape", "unpaired high surrogate");
                        return 0;
                    }
                    writer->cursor += 2;
                    if (!dluma_qj_u16(writer, &second) ||
                        second < 0xDC00U || second > 0xDFFFU) {
                        dluma_qj_fail(writer, "json_escape", "invalid surrogate pair");
                        return 0;
                    }
                    codepoint = 0x10000U +
                        ((first - 0xD800U) << 10U) + (second - 0xDC00U);
                } else if (first >= 0xDC00U && first <= 0xDFFFU) {
                    dluma_qj_fail(writer, "json_escape", "unpaired low surrogate");
                    return 0;
                }
                if (codepoint == 0U) {
                    if (decoded_size >= DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES) {
                        dluma_qj_fail(writer, "json_string_limit", "decoded JSON string exceeds 1024 bytes");
                        return 0;
                    }
                    ++decoded_size;
                    if (!dluma_qj_put(writer, '\\') || !dluma_qj_put(writer, 'u') ||
                        !dluma_qj_put(writer, '0') || !dluma_qj_put(writer, '0') ||
                        !dluma_qj_put(writer, '0') || !dluma_qj_put(writer, '0'))
                        return 0;
                } else if (codepoint == '"' || codepoint == '\\') {
                    if (decoded_size >= DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES) return 0;
                    ++decoded_size;
                    if (!dluma_qj_put(writer, '\\') ||
                        !dluma_qj_put(writer, (unsigned char)codepoint)) return 0;
                } else if (codepoint < 0x20U) {
                    static const char hex[] = "0123456789ABCDEF";
                    if (decoded_size >= DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES) return 0;
                    ++decoded_size;
                    if (!dluma_qj_put(writer, '\\') || !dluma_qj_put(writer, 'u') ||
                        !dluma_qj_put(writer, '0') || !dluma_qj_put(writer, '0') ||
                        !dluma_qj_put(writer, (unsigned char)hex[(codepoint >> 4U) & 15U]) ||
                        !dluma_qj_put(writer, (unsigned char)hex[codepoint & 15U]))
                        return 0;
                } else if (!dluma_qj_put_utf8(writer, codepoint, &decoded_size)) {
                    return 0;
                }
            } else {
                unsigned char decoded;
                if (escape == '"' || escape == '\\' || escape == '/') decoded = escape;
                else if (escape == 'b') decoded = '\b';
                else if (escape == 'f') decoded = '\f';
                else if (escape == 'n') decoded = '\n';
                else if (escape == 'r') decoded = '\r';
                else if (escape == 't') decoded = '\t';
                else {
                    dluma_qj_fail(writer, "json_escape", "unknown JSON escape");
                    return 0;
                }
                if (decoded_size >= DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES) {
                    dluma_qj_fail(writer, "json_string_limit", "decoded JSON string exceeds 1024 bytes");
                    return 0;
                }
                ++decoded_size;
                if (decoded == '"' || decoded == '\\') {
                    if (!dluma_qj_put(writer, '\\') || !dluma_qj_put(writer, decoded)) return 0;
                } else if (decoded == '/') {
                    if (!dluma_qj_put(writer, decoded)) return 0;
                } else {
                    if (!dluma_qj_put(writer, '\\') || !dluma_qj_put(writer, escape)) return 0;
                }
            }
        } else if (value < 0x20U) {
            dluma_qj_fail(writer, "json_control", "unescaped control byte in JSON string");
            return 0;
        } else if (value < 0x80U) {
            if (decoded_size >= DLUMA_QUEST_PHASE_MAX_JSON_STRING_BYTES) {
                dluma_qj_fail(writer, "json_string_limit", "decoded JSON string exceeds 1024 bytes");
                return 0;
            }
            ++decoded_size;
            if (!dluma_qj_put(writer, value)) return 0;
        } else {
            --writer->cursor;
            if (!dluma_qj_raw_utf8(writer, &decoded_size)) return 0;
        }
    }
    dluma_qj_fail(writer, "json_string", "unterminated JSON string");
    return 0;
}

static void dluma_qj_space(DlumaQuestJsonWriter* writer)
{
    while (writer->cursor < writer->end) {
        unsigned char value = *writer->cursor;
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
        ++writer->cursor;
    }
}

static int dluma_qj_value(DlumaQuestJsonWriter* writer);

static int dluma_qj_node(DlumaQuestJsonWriter* writer)
{
    if (writer->nodes >= DLUMA_QUEST_PHASE_MAX_JSON_NODES) {
        dluma_qj_fail(writer, "json_node_limit", "JSON exceeds 256 nodes");
        return 0;
    }
    ++writer->nodes;
    return 1;
}

static int dluma_qj_object(DlumaQuestJsonWriter* writer)
{
    unsigned int key_base = writer->key_count;
    int first = 1;
    if (writer->depth >= DLUMA_QUEST_PHASE_MAX_JSON_DEPTH) {
        dluma_qj_fail(writer, "json_depth_limit", "JSON exceeds depth 8");
        return 0;
    }
    ++writer->depth;
    ++writer->cursor;
    if (!dluma_qj_put(writer, '{')) return 0;
    dluma_qj_space(writer);
    while (writer->cursor < writer->end && *writer->cursor != '}') {
        unsigned int key_offset = 0;
        unsigned int key_size = 0;
        unsigned int index;
        if (!first && !dluma_qj_put(writer, ',')) return 0;
        if (!dluma_qj_string(writer, &key_offset, &key_size)) return 0;
        for (index = key_base; index < writer->key_count; ++index)
            if (writer->keys[index].size == key_size &&
                dluma_qp_bytes_equal(
                    writer->output + writer->keys[index].offset,
                    writer->output + key_offset, key_size)) {
                dluma_qj_fail(writer, "json_duplicate_key", "JSON object contains a duplicate key");
                return 0;
            }
        if (writer->key_count >= DLUMA_QUEST_PHASE_MAX_JSON_NODES) return 0;
        writer->keys[writer->key_count].offset = key_offset;
        writer->keys[writer->key_count].size = key_size;
        ++writer->key_count;
        dluma_qj_space(writer);
        if (writer->cursor >= writer->end || *writer->cursor++ != ':') {
            dluma_qj_fail(writer, "json_colon", "JSON object key is missing a colon");
            return 0;
        }
        if (!dluma_qj_put(writer, ':') || !dluma_qj_value(writer)) return 0;
        first = 0;
        dluma_qj_space(writer);
        if (writer->cursor < writer->end && *writer->cursor == ',') {
            ++writer->cursor;
            dluma_qj_space(writer);
            if (writer->cursor >= writer->end || *writer->cursor == '}') {
                dluma_qj_fail(writer, "json_trailing_comma", "JSON object has a trailing comma");
                return 0;
            }
        } else break;
    }
    if (writer->cursor >= writer->end || *writer->cursor++ != '}') {
        dluma_qj_fail(writer, "json_object", "unterminated JSON object");
        return 0;
    }
    writer->key_count = key_base;
    --writer->depth;
    return dluma_qj_put(writer, '}');
}

static int dluma_qj_array(DlumaQuestJsonWriter* writer)
{
    int first = 1;
    if (writer->depth >= DLUMA_QUEST_PHASE_MAX_JSON_DEPTH) {
        dluma_qj_fail(writer, "json_depth_limit", "JSON exceeds depth 8");
        return 0;
    }
    ++writer->depth;
    ++writer->cursor;
    if (!dluma_qj_put(writer, '[')) return 0;
    dluma_qj_space(writer);
    while (writer->cursor < writer->end && *writer->cursor != ']') {
        if (!first && !dluma_qj_put(writer, ',')) return 0;
        if (!dluma_qj_value(writer)) return 0;
        first = 0;
        dluma_qj_space(writer);
        if (writer->cursor < writer->end && *writer->cursor == ',') {
            ++writer->cursor;
            dluma_qj_space(writer);
            if (writer->cursor >= writer->end || *writer->cursor == ']') {
                dluma_qj_fail(writer, "json_trailing_comma", "JSON array has a trailing comma");
                return 0;
            }
        } else break;
    }
    if (writer->cursor >= writer->end || *writer->cursor++ != ']') {
        dluma_qj_fail(writer, "json_array", "unterminated JSON array");
        return 0;
    }
    --writer->depth;
    return dluma_qj_put(writer, ']');
}

static int dluma_qj_literal(DlumaQuestJsonWriter* writer, const char* text)
{
    const char* cursor = text;
    while (*cursor) {
        if (writer->cursor >= writer->end ||
            *writer->cursor++ != (unsigned char)*cursor) {
            dluma_qj_fail(writer, "json_literal", "invalid JSON literal");
            return 0;
        }
        if (!dluma_qj_put(writer, (unsigned char)*cursor++)) return 0;
    }
    return 1;
}

static int dluma_qj_number(DlumaQuestJsonWriter* writer)
{
    const unsigned char* start = writer->cursor;
    const unsigned char* cursor = start;
    unsigned int index;
    if (cursor < writer->end && *cursor == '-') ++cursor;
    if (cursor >= writer->end) goto invalid;
    if (*cursor == '0') {
        ++cursor;
        if (cursor < writer->end && *cursor >= '0' && *cursor <= '9') goto invalid;
    } else if (*cursor >= '1' && *cursor <= '9') {
        do { ++cursor; } while (cursor < writer->end && *cursor >= '0' && *cursor <= '9');
    } else goto invalid;
    if (cursor < writer->end && *cursor == '.') {
        ++cursor;
        if (cursor >= writer->end || *cursor < '0' || *cursor > '9') goto invalid;
        do { ++cursor; } while (cursor < writer->end && *cursor >= '0' && *cursor <= '9');
    }
    if (cursor < writer->end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        if (cursor < writer->end && (*cursor == '+' || *cursor == '-')) ++cursor;
        if (cursor >= writer->end || *cursor < '0' || *cursor > '9') goto invalid;
        do { ++cursor; } while (cursor < writer->end && *cursor >= '0' && *cursor <= '9');
    }
    writer->cursor = cursor;
    for (index = 0; index < (unsigned int)(cursor - start); ++index)
        if (!dluma_qj_put(writer, start[index])) return 0;
    return 1;
invalid:
    writer->cursor = cursor;
    dluma_qj_fail(writer, "json_number", "invalid JSON number");
    return 0;
}

static int dluma_qj_value(DlumaQuestJsonWriter* writer)
{
    dluma_qj_space(writer);
    if (!dluma_qj_node(writer) || writer->cursor >= writer->end) {
        dluma_qj_fail(writer, "json_value", "expected a JSON value");
        return 0;
    }
    if (*writer->cursor == '{') return dluma_qj_object(writer);
    if (*writer->cursor == '[') return dluma_qj_array(writer);
    if (*writer->cursor == '"') return dluma_qj_string(writer, 0, 0);
    if (*writer->cursor == 't') return dluma_qj_literal(writer, "true");
    if (*writer->cursor == 'f') return dluma_qj_literal(writer, "false");
    if (*writer->cursor == 'n') return dluma_qj_literal(writer, "null");
    if (*writer->cursor == '-' ||
        (*writer->cursor >= '0' && *writer->cursor <= '9'))
        return dluma_qj_number(writer);
    dluma_qj_fail(writer, "json_value", "invalid JSON value");
    return 0;
}

int dluma_quest_phase_json_canonicalize_object(
    const void* bytes,
    unsigned int size,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size,
    DlumaQuestPhaseDiagnostic* diagnostic)
{
    DlumaQuestJsonWriter writer;
    if (diagnostic) dluma_qp_zero(diagnostic, (unsigned int)sizeof(*diagnostic));
    if (output_size) *output_size = 0;
    if (!bytes || !size || size > DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES ||
        !output || output_capacity < 3U || !output_size) {
        dluma_qp_diagnostic(diagnostic, 0U, "json_size", "JSON argument size is invalid");
        return 0;
    }
    dluma_qp_zero(&writer, (unsigned int)sizeof(writer));
    writer.begin = (const unsigned char*)bytes;
    writer.cursor = writer.begin;
    writer.end = writer.begin + size;
    writer.output = output;
    writer.output_capacity = output_capacity;
    writer.diagnostic = diagnostic;
    dluma_qj_space(&writer);
    if (writer.cursor >= writer.end || *writer.cursor != '{') {
        dluma_qp_diagnostic(diagnostic, 0U, "json_root", "arguments must have a JSON object root");
        return 0;
    }
    if (!dluma_qj_value(&writer)) return 0;
    dluma_qj_space(&writer);
    if (writer.cursor != writer.end) {
        dluma_qj_fail(&writer, "json_trailing", "trailing bytes follow the JSON object");
        return 0;
    }
    writer.output[writer.output_size] = 0;
    *output_size = writer.output_size;
    dluma_qp_diagnostic(diagnostic, 0U, "ok", "JSON arguments are valid");
    return 1;
}

static int dluma_qp_identifier(const char* value, unsigned int maximum)
{
    unsigned int index = 0;
    if (!value || !value[0]) return 0;
    while (value[index]) {
        char c = value[index];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return 0;
        ++index;
    }
    return index <= maximum;
}

static void dluma_qp_skip_space(
    const unsigned char** cursor,
    const unsigned char* end)
{
    while (*cursor < end && (**cursor == ' ' || **cursor == '\t')) ++*cursor;
}

static int dluma_qp_quoted(
    const unsigned char** cursor,
    const unsigned char* end,
    const unsigned char** value,
    unsigned int* value_size)
{
    const unsigned char* start;
    if ((unsigned int)(end - *cursor) < 4U ||
        (*cursor)[0] != '<' || (*cursor)[1] != '<') return 0;
    *cursor += 2;
    start = *cursor;
    while ((unsigned int)(end - *cursor) >= 2U) {
        if ((*cursor)[0] == '>' && (*cursor)[1] == '>') {
            *value = start;
            *value_size = (unsigned int)(*cursor - start);
            *cursor += 2;
            return 1;
        }
        ++*cursor;
    }
    return 0;
}

static int dluma_qp_expect(
    const unsigned char** cursor,
    const unsigned char* end,
    const char* text)
{
    while (*text) {
        if (*cursor >= end || **cursor != (unsigned char)*text) return 0;
        ++*cursor;
        ++text;
    }
    return 1;
}

static int dluma_qp_wait(
    const unsigned char* bytes,
    unsigned int size,
    double* output)
{
    unsigned int index = 0;
    double value = 0.0;
    double scale = 0.1;
    int fractional = 0;
    int digits = 0;
    if (!bytes || !size || !output) return 0;
    while (index < size) {
        unsigned char c = bytes[index++];
        if (c == '.' && !fractional) {
            fractional = 1;
            continue;
        }
        if (c < '0' || c > '9') return 0;
        digits = 1;
        if (!fractional) {
            value = value * 10.0 + (double)(c - '0');
        } else {
            value += (double)(c - '0') * scale;
            scale *= 0.1;
        }
        if (value > 86400.0) return 0;
    }
    if (!digits || bytes[size - 1U] == '.' || value < 0.0 || value > 86400.0)
        return 0;
    *output = value;
    return 1;
}

int dluma_quest_phase_parse_wait_seconds(
    const void* bytes,
    unsigned int size,
    double* output)
{
    return dluma_qp_wait((const unsigned char*)bytes, size, output);
}

static const DlumaQuestPhaseAlias* dluma_qp_find_alias(
    const char* command,
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count)
{
    unsigned int index;
    for (index = 0; index < alias_count; ++index)
        if (dluma_qp_fold_equal(command, aliases[index].command))
            return &aliases[index];
    return 0;
}

int dluma_quest_phase_parse_directive(
    const void* line_bytes,
    unsigned int line_size,
    unsigned int source_line,
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count,
    DlumaQuestPhaseDirective* output,
    DlumaQuestPhaseDiagnostic* diagnostic)
{
    const unsigned char* begin = (const unsigned char*)line_bytes;
    const unsigned char* cursor = begin;
    const unsigned char* end = begin ? begin + line_size : 0;
    const unsigned char* value;
    unsigned int value_size;
    const DlumaQuestPhaseAlias* alias = 0;
    char command[64];
    unsigned int command_size = 0;
    const unsigned char* wait_start;
    if (diagnostic) dluma_qp_zero(diagnostic, (unsigned int)sizeof(*diagnostic));
    if (!begin || !line_size || !output ||
        output->struct_size < sizeof(*output) ||
        (alias_count && !aliases)) {
        dluma_qp_diagnostic(diagnostic, 0U, "directive_argument", "invalid directive parser input");
        return 0;
    }
    dluma_qp_zero(output, (unsigned int)sizeof(*output));
    output->struct_size = (unsigned int)sizeof(*output);
    output->source_line = source_line;
    dluma_qp_skip_space(&cursor, end);
    if (cursor >= end || *cursor++ != ',') goto syntax;
    while (cursor < end && *cursor != ' ' && *cursor != '\t') {
        if (command_size + 1U >= sizeof(command)) goto syntax;
        command[command_size++] = (char)*cursor++;
    }
    command[command_size] = 0;
    if (dluma_qp_equal(command, "call-lua"))
        output->runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_LUA;
    else if (dluma_qp_equal(command, "call-native"))
        output->runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
    else {
        alias = dluma_qp_find_alias(command, aliases, alias_count);
        if (!alias) {
            dluma_qp_diagnostic(diagnostic, 0U, "not_custom", "line is not a registered custom quest directive");
            return 0;
        }
        output->runtime_kind = alias->runtime_kind;
        if (!dluma_qp_copy(output->provider_mod, sizeof(output->provider_mod),
                         alias->provider_mod) ||
            !dluma_qp_copy(output->handler_id, sizeof(output->handler_id),
                         alias->handler_id)) goto syntax;
    }
    dluma_qp_skip_space(&cursor, end);
    if (!dluma_qp_quoted(&cursor, end, &value, &value_size) || !value_size ||
        !dluma_qp_copy_n(output->phase_id, sizeof(output->phase_id), value, value_size))
        goto syntax;
    if (!alias) {
        dluma_qp_skip_space(&cursor, end);
        if (!dluma_qp_expect(&cursor, end, "-mod=" ) ||
            !dluma_qp_quoted(&cursor, end, &value, &value_size) ||
            !dluma_qp_copy_n(output->provider_mod, sizeof(output->provider_mod), value, value_size) ||
            !dluma_qp_identifier(output->provider_mod, 127U)) goto syntax;
        dluma_qp_skip_space(&cursor, end);
        if (!dluma_qp_expect(&cursor, end, "-handler=") ||
            !dluma_qp_quoted(&cursor, end, &value, &value_size) ||
            !dluma_qp_copy_n(output->handler_id, sizeof(output->handler_id), value, value_size) ||
            !dluma_qp_identifier(output->handler_id, 127U)) goto syntax;
    }
    dluma_qp_skip_space(&cursor, end);
    if (!dluma_qp_expect(&cursor, end, "-arguments=") ||
        !dluma_qp_quoted(&cursor, end, &value, &value_size) ||
        !dluma_quest_phase_json_canonicalize_object(
            value, value_size, output->arguments,
            (unsigned int)sizeof(output->arguments),
            &output->arguments_size, diagnostic))
        return 0;
    dluma_qp_skip_space(&cursor, end);
    if (!dluma_qp_expect(&cursor, end, "-wait=")) goto syntax;
    wait_start = cursor;
    while (cursor < end && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '\r' && *cursor != '\n') ++cursor;
    if (!dluma_qp_wait(wait_start, (unsigned int)(cursor - wait_start),
                     &output->wait_seconds)) goto syntax;
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                            *cursor == '\r' || *cursor == '\n')) ++cursor;
    if (cursor != end) goto syntax;
    if (!dluma_qp_copy(output->contract_id, sizeof(output->contract_id),
                     output->provider_mod)) goto syntax;
    value_size = dluma_qp_length(output->contract_id);
    if (value_size + 1U + dluma_qp_length(output->handler_id) + 1U >
        sizeof(output->contract_id)) goto syntax;
    output->contract_id[value_size++] = ':';
    {
        unsigned int index = 0;
        while (output->handler_id[index])
            output->contract_id[value_size++] = output->handler_id[index++];
        output->contract_id[value_size] = 0;
    }
    output->completion = output->wait_seconds == 0.0
        ? DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET
        : DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT;
    dluma_qp_diagnostic(diagnostic, 0U, "ok", "custom quest directive is valid");
    return 1;
syntax:
    dluma_qp_diagnostic(
        diagnostic,
        begin && cursor && cursor >= begin ? (unsigned int)(cursor - begin) : 0U,
        "directive_syntax",
        "custom quest directive is malformed or contains an out-of-range field");
    return 0;
}

static int dluma_qp_alias_command(const char* value)
{
    unsigned int index = 0;
    if (!value || !value[0]) return 0;
    while (value[index]) {
        char c = value[index];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) return 0;
        ++index;
    }
    return index <= 63U;
}

int dluma_quest_phase_aliases_validate(
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count,
    const char* const* stock_commands,
    unsigned int stock_command_count,
    unsigned char* accepted,
    DlumaQuestPhaseDiagnostic* diagnostic)
{
    unsigned int index;
    if (diagnostic) dluma_qp_zero(diagnostic, (unsigned int)sizeof(*diagnostic));
    if ((alias_count && (!aliases || !accepted)) ||
        (stock_command_count && !stock_commands)) return 0;
    for (index = 0; index < alias_count; ++index) {
        unsigned int stock;
        int valid = dluma_qp_alias_command(aliases[index].command) &&
            dluma_qp_identifier(aliases[index].provider_mod, 127U) &&
            dluma_qp_identifier(aliases[index].handler_id, 127U) &&
            (aliases[index].runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA ||
             aliases[index].runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_NATIVE);
        if (valid && (dluma_qp_fold_equal(aliases[index].command, "call-lua") ||
                      dluma_qp_fold_equal(aliases[index].command, "call-native")))
            valid = 0;
        for (stock = 0; valid && stock < stock_command_count; ++stock)
            if (dluma_qp_fold_equal(aliases[index].command, stock_commands[stock]))
                valid = 0;
        accepted[index] = valid ? 1U : 0U;
    }
    /* A collision disables every declaration that owns the folded command.
     * Selection must never depend on discovery order or load priority. */
    for (index = 0; index < alias_count; ++index) {
        unsigned int other;
        for (other = index + 1U; other < alias_count; ++other)
            if (dluma_qp_fold_equal(
                    aliases[index].command, aliases[other].command)) {
                accepted[index] = 0U;
                accepted[other] = 0U;
            }
    }
    dluma_qp_diagnostic(diagnostic, 0U, "ok", "quest phase aliases were filtered safely");
    return 1;
}

static int dluma_qp_append(
    char* output,
    unsigned int capacity,
    unsigned int* size,
    const char* text)
{
    unsigned int index = 0;
    if (!output || !size || !text) return 0;
    while (text[index]) {
        if (*size + 1U >= capacity) return 0;
        output[(*size)++] = text[index++];
    }
    output[*size] = 0;
    return 1;
}

static int dluma_qp_append_macro(
    char* output,
    unsigned int capacity,
    unsigned int* size,
    const char* command,
    int builtin)
{
    return dluma_qp_append(output, capacity, size, ",") &&
        dluma_qp_append(output, capacity, size, command) &&
        dluma_qp_append(
            output, capacity, size,
            builtin
                ? " name mod handler arguments wait =\r\n"
                : " name arguments wait =\r\n") &&
        dluma_qp_append(
            output, capacity, size,
            "  Phase -type=<<wait guard>> -name=<(,name)>"
            " -time=<(,wait)> -time2p=<(,wait)>"
            " -time3p=<(,wait)> -time4p=<(,wait)>\r\n\r\n");
}

int dluma_quest_phase_build_edf(
    const DlumaQuestPhaseAlias* aliases,
    const unsigned char* accepted,
    unsigned int alias_count,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size)
{
    unsigned int index;
    unsigned int size = 0;
    if (!output || !output_capacity || !output_size ||
        (alias_count && (!aliases || !accepted))) return 0;
    output[0] = 0;
    if (!dluma_qp_append(output, output_capacity, &size,
            "# Generated by DLUMA. Do not edit.\r\n\r\n") ||
        !dluma_qp_append_macro(output, output_capacity, &size, "call-lua", 1) ||
        !dluma_qp_append_macro(output, output_capacity, &size, "call-native", 1))
        return 0;
    for (index = 0; index < alias_count; ++index)
        if (accepted[index] &&
            !dluma_qp_append_macro(output, output_capacity, &size,
                                 aliases[index].command, 0))
            return 0;
    *output_size = size;
    return 1;
}

int dluma_quest_phase_build_compiler_line(
    const char* command,
    const DlumaQuestPhaseDirective* directive,
    const char* wait_token,
    char* output,
    unsigned int output_capacity,
    unsigned int* output_size)
{
    unsigned int command_size = 0;
    unsigned int size = 0;
    double parsed_wait = 0.0;
    int builtin_lua;
    int builtin_native;
    if (!command || !directive || !wait_token || !output ||
        !output_capacity || !output_size ||
        directive->struct_size < sizeof(*directive) ||
        !directive->phase_id[0] || !directive->provider_mod[0] ||
        !directive->handler_id[0])
        return 0;
    while (command[command_size]) {
        char value = command[command_size];
        if (command_size + 1U >= 64U ||
            !((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '-'))
            return 0;
        ++command_size;
    }
    if (!command_size ||
        !dluma_quest_phase_parse_wait_seconds(
            wait_token, dluma_qp_length(wait_token), &parsed_wait) ||
        parsed_wait != directive->wait_seconds)
        return 0;
    builtin_lua = dluma_qp_equal(command, "call-lua");
    builtin_native = dluma_qp_equal(command, "call-native");
    if ((builtin_lua &&
         directive->runtime_kind != DLUMA_QUEST_PHASE_RUNTIME_LUA) ||
        (builtin_native &&
         directive->runtime_kind != DLUMA_QUEST_PHASE_RUNTIME_NATIVE))
        return 0;
    output[0] = 0;
    if (!dluma_qp_append(output, output_capacity, &size, ",") ||
        !dluma_qp_append(output, output_capacity, &size, command) ||
        !dluma_qp_append(output, output_capacity, &size, " <<") ||
        !dluma_qp_append(output, output_capacity, &size, directive->phase_id) ||
        !dluma_qp_append(output, output_capacity, &size, ">> "))
        return 0;
    if (builtin_lua || builtin_native) {
        if (!dluma_qp_append(output, output_capacity, &size, "<<") ||
            !dluma_qp_append(
                output, output_capacity, &size, directive->provider_mod) ||
            !dluma_qp_append(output, output_capacity, &size, ">> <<") ||
            !dluma_qp_append(
                output, output_capacity, &size, directive->handler_id) ||
            !dluma_qp_append(output, output_capacity, &size, ">> "))
            return 0;
    }
    if (!dluma_qp_append(output, output_capacity, &size, "<<{}>> ") ||
        !dluma_qp_append(output, output_capacity, &size, wait_token))
        return 0;
    *output_size = size;
    return 1;
}

static int dluma_qp_registration_key(
    char* output,
    unsigned int capacity,
    const char* owner,
    const char* handler)
{
    unsigned int owner_size = dluma_qp_length(owner);
    unsigned int handler_size = dluma_qp_length(handler);
    unsigned int index;
    if (!output || !capacity || !dluma_qp_identifier(owner, 127U) ||
        !dluma_qp_identifier(handler, 127U) ||
        owner_size + 1U + handler_size + 1U > capacity) return 0;
    for (index = 0; index < owner_size; ++index) output[index] = owner[index];
    output[owner_size] = ':';
    for (index = 0; index < handler_size; ++index)
        output[owner_size + 1U + index] = handler[index];
    output[owner_size + 1U + handler_size] = 0;
    return 1;
}

void dluma_quest_phase_dispatcher_initialize(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t generation)
{
    if (!dispatcher) return;
    dluma_qp_zero(dispatcher, (unsigned int)sizeof(*dispatcher));
    dispatcher->generation = generation ? generation : 1U;
    dispatcher->next_token = 1U;
    dispatcher->next_sequence = 1U;
}

static int dluma_qp_register(
    DlumaQuestPhaseDispatcher* dispatcher,
    DlumaQuestPhaseRegistration* slots,
    unsigned int capacity,
    const char* owner_mod,
    const char* key,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* out_token,
    int allow_shared_key)
{
    unsigned int index;
    unsigned int free_index = capacity;
    uint64_t token;
    if (!dispatcher || dispatcher->stopping || !owner_mod || !key ||
        !callback || !out_token || !dluma_qp_identifier(owner_mod, 127U) ||
        load_priority < -1000 || load_priority > 1000) return 0;
    for (index = 0; index < capacity; ++index) {
        if (slots[index].active && dluma_qp_equal(slots[index].key, key) &&
            (!allow_shared_key ||
             dluma_qp_equal(slots[index].owner_mod, owner_mod))) return 0;
        if (!slots[index].active && free_index == capacity) free_index = index;
    }
    if (free_index == capacity) return 0;
    token = dispatcher->next_token++;
    if (!token) token = dispatcher->next_token++;
    dluma_qp_zero(&slots[free_index], (unsigned int)sizeof(slots[free_index]));
    slots[free_index].token = token;
    slots[free_index].epoch = ++dispatcher->registration_epoch;
    slots[free_index].generation = dispatcher->generation;
    slots[free_index].active = 1U;
    slots[free_index].load_priority = load_priority;
    slots[free_index].callback = callback;
    slots[free_index].user = user;
    if (!dluma_qp_copy(slots[free_index].owner_mod,
                     sizeof(slots[free_index].owner_mod), owner_mod) ||
        !dluma_qp_copy(slots[free_index].key,
                     sizeof(slots[free_index].key), key)) {
        dluma_qp_zero(&slots[free_index], (unsigned int)sizeof(slots[free_index]));
        return 0;
    }
    *out_token = token;
    return 1;
}

int dluma_quest_phase_register_provider(
    DlumaQuestPhaseDispatcher* dispatcher,
    const char* owner_mod,
    const char* handler_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* out_token)
{
    char contract[257];
    if (!dluma_qp_registration_key(
            contract, (unsigned int)sizeof(contract), owner_mod, handler_id))
        return 0;
    return dluma_qp_register(
        dispatcher, dispatcher ? dispatcher->providers : 0,
        DLUMA_QUEST_PHASE_MAX_PROVIDERS, owner_mod, contract,
        load_priority, callback, user, out_token, 0);
}

int dluma_quest_phase_register_observer(
    DlumaQuestPhaseDispatcher* dispatcher,
    const char* owner_mod,
    const char* contract_id,
    int load_priority,
    DlumaQuestPhaseDispatchCallback callback,
    void* user,
    uint64_t* out_token)
{
    const char* colon = contract_id;
    char provider[128];
    char handler[128];
    unsigned int provider_size = 0;
    unsigned int handler_size;
    if (!contract_id) return 0;
    while (*colon && *colon != ':') ++colon;
    if (*colon != ':') return 0;
    provider_size = (unsigned int)(colon - contract_id);
    handler_size = dluma_qp_length(colon + 1);
    if (!dluma_qp_copy_n(provider, sizeof(provider),
                       (const unsigned char*)contract_id, provider_size) ||
        !dluma_qp_copy(handler, sizeof(handler), colon + 1) ||
        !dluma_qp_identifier(provider, 127U) ||
        !dluma_qp_identifier(handler, 127U) || !handler_size)
        return 0;
    return dluma_qp_register(
        dispatcher, dispatcher ? dispatcher->observers : 0,
        DLUMA_QUEST_PHASE_MAX_OBSERVERS, owner_mod, contract_id,
        load_priority, callback, user, out_token, 1);
}

int dluma_quest_phase_unregister(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t token)
{
    unsigned int index;
    if (!dispatcher || !token) return 0;
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_PROVIDERS; ++index)
        if (dispatcher->providers[index].active &&
            dispatcher->providers[index].token == token) {
            dispatcher->providers[index].active = 0U;
            dispatcher->providers[index].callback = 0;
            dispatcher->providers[index].user = 0;
            return 1;
        }
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_OBSERVERS; ++index)
        if (dispatcher->observers[index].active &&
            dispatcher->observers[index].token == token) {
            dispatcher->observers[index].active = 0U;
            dispatcher->observers[index].callback = 0;
            dispatcher->observers[index].user = 0;
            return 1;
        }
    return 0;
}

int dluma_quest_phase_enqueue(
    DlumaQuestPhaseDispatcher* dispatcher,
    const DlumaQuestPhaseEvent* event_value)
{
    unsigned int index;
    DlumaQuestPhaseQueuedEvent* queued;
    if (!dispatcher || dispatcher->stopping || !event_value ||
        event_value->struct_size < sizeof(*event_value) ||
        event_value->arguments_size > DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES ||
        (event_value->arguments_size && !event_value->arguments_json))
        return 0;
    if (dispatcher->queue_count >= DLUMA_QUEST_PHASE_QUEUE_CAPACITY) {
        ++dispatcher->overflow_count;
        return 0;
    }
    index = (dispatcher->queue_head + dispatcher->queue_count) %
        DLUMA_QUEST_PHASE_QUEUE_CAPACITY;
    queued = &dispatcher->queue[index];
    dluma_qp_zero(queued, (unsigned int)sizeof(*queued));
    queued->event_value = *event_value;
    if (!queued->event_value.sequence)
        queued->event_value.sequence = dispatcher->next_sequence++;
    else if (queued->event_value.sequence >= dispatcher->next_sequence)
        dispatcher->next_sequence = queued->event_value.sequence + 1U;
    queued->event_value.local_peer = 1U;
    for (index = 0; index < event_value->arguments_size; ++index)
        queued->arguments[index] = event_value->arguments_json[index];
    queued->arguments[event_value->arguments_size] = 0;
    queued->event_value.arguments_json = queued->arguments;
    ++dispatcher->queue_count;
    return 1;
}

static int dluma_qp_target_before(
    const DlumaQuestPhaseDispatchTarget* left,
    const DlumaQuestPhaseDispatchTarget* right)
{
    unsigned int index = 0;
    if (left->load_priority != right->load_priority)
        return left->load_priority < right->load_priority;
    while (left->owner_mod[index] &&
           left->owner_mod[index] == right->owner_mod[index]) ++index;
    if (left->owner_mod[index] != right->owner_mod[index])
        return (unsigned char)left->owner_mod[index] <
               (unsigned char)right->owner_mod[index];
    return left->token < right->token;
}

static DlumaQuestPhaseRegistration* dluma_qp_find_token(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t token)
{
    unsigned int index;
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_PROVIDERS; ++index)
        if (dispatcher->providers[index].active &&
            dispatcher->providers[index].token == token)
            return &dispatcher->providers[index];
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_OBSERVERS; ++index)
        if (dispatcher->observers[index].active &&
            dispatcher->observers[index].token == token)
            return &dispatcher->observers[index];
    return 0;
}

static void dluma_qp_dispatch_one(
    DlumaQuestPhaseDispatcher* dispatcher,
    const DlumaQuestPhaseDispatchTarget* target,
    const DlumaQuestPhaseEvent* event_value)
{
    DlumaQuestPhaseRegistration* current =
        dluma_qp_find_token(dispatcher, target->token);
    if (!current || current->generation != dispatcher->generation ||
        current->callback != target->callback || current->user != target->user)
        return;
    target->callback(target->user, event_value);
}

unsigned int dluma_quest_phase_pump(
    DlumaQuestPhaseDispatcher* dispatcher,
    unsigned int maximum_events)
{
    unsigned int processed = 0;
    DlumaQuestPhaseQueuedEvent current_event;
    if (!dispatcher || dispatcher->stopping || dispatcher->dispatch_depth ||
        !maximum_events) return 0;
    dispatcher->dispatch_depth = 1U;
    while (processed < maximum_events && dispatcher->queue_count &&
           !dispatcher->stopping) {
        unsigned int index;
        unsigned int target_count = 0;
        unsigned int observer_base = 0;
        uint64_t epoch = dispatcher->registration_epoch;
        DlumaQuestPhaseDispatchTarget* targets = dispatcher->dispatch_targets;
        current_event = dispatcher->queue[dispatcher->queue_head];
        current_event.event_value.arguments_json = current_event.arguments;
        dispatcher->queue_head = (dispatcher->queue_head + 1U) %
            DLUMA_QUEST_PHASE_QUEUE_CAPACITY;
        --dispatcher->queue_count;
        for (index = 0; index < DLUMA_QUEST_PHASE_MAX_PROVIDERS; ++index) {
            DlumaQuestPhaseRegistration* slot = &dispatcher->providers[index];
            if (!slot->active || slot->epoch > epoch ||
                !dluma_qp_equal(slot->key, current_event.event_value.contract_id))
                continue;
            targets[target_count].token = slot->token;
            targets[target_count].load_priority = slot->load_priority;
            (void)dluma_qp_copy(targets[target_count].owner_mod,
                              sizeof(targets[target_count].owner_mod),
                              slot->owner_mod);
            targets[target_count].callback = slot->callback;
            targets[target_count].user = slot->user;
            ++target_count;
            break;
        }
        observer_base = target_count;
        for (index = 0; index < DLUMA_QUEST_PHASE_MAX_OBSERVERS; ++index) {
            DlumaQuestPhaseRegistration* slot = &dispatcher->observers[index];
            unsigned int insert;
            DlumaQuestPhaseDispatchTarget value;
            if (!slot->active || slot->epoch > epoch ||
                !dluma_qp_equal(slot->key, current_event.event_value.contract_id))
                continue;
            dluma_qp_zero(&value, (unsigned int)sizeof(value));
            value.token = slot->token;
            value.load_priority = slot->load_priority;
            (void)dluma_qp_copy(value.owner_mod, sizeof(value.owner_mod), slot->owner_mod);
            value.callback = slot->callback;
            value.user = slot->user;
            insert = target_count;
            while (insert > observer_base &&
                   dluma_qp_target_before(&value, &targets[insert - 1U])) {
                targets[insert] = targets[insert - 1U];
                --insert;
            }
            targets[insert] = value;
            ++target_count;
        }
        for (index = 0; index < target_count && !dispatcher->stopping; ++index)
            dluma_qp_dispatch_one(
                dispatcher, &targets[index], &current_event.event_value);
        ++processed;
    }
    dispatcher->dispatch_depth = 0U;
    return processed;
}

int dluma_quest_phase_dispatcher_quiesce(
    DlumaQuestPhaseDispatcher* dispatcher)
{
    unsigned int index;
    if (!dispatcher) return 0;
    dispatcher->stopping = 1U;
    if (dispatcher->dispatch_depth) return 0;
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_PROVIDERS; ++index)
        dluma_qp_zero(&dispatcher->providers[index],
                    (unsigned int)sizeof(dispatcher->providers[index]));
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_OBSERVERS; ++index)
        dluma_qp_zero(&dispatcher->observers[index],
                    (unsigned int)sizeof(dispatcher->observers[index]));
    dispatcher->queue_head = 0U;
    dispatcher->queue_count = 0U;
    ++dispatcher->generation;
    return 1;
}

int dluma_quest_phase_dispatcher_advance_generation(
    DlumaQuestPhaseDispatcher* dispatcher,
    uint64_t generation)
{
    unsigned int index;
    if (!dispatcher || dispatcher->stopping || dispatcher->dispatch_depth ||
        !generation)
        return 0;
    dispatcher->generation = generation;
    dispatcher->queue_head = 0U;
    dispatcher->queue_count = 0U;
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_PROVIDERS; ++index)
        if (dispatcher->providers[index].active)
            dispatcher->providers[index].generation = generation;
    for (index = 0; index < DLUMA_QUEST_PHASE_MAX_OBSERVERS; ++index)
        if (dispatcher->observers[index].active)
            dispatcher->observers[index].generation = generation;
    return 1;
}
