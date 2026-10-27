#include "dlw_json_reader.h"

#include <limits.h>

#define DLUMA_JSON_MAX_DEPTH 32U

static void dluma_json_fail(DlumaJsonReader* reader, DlumaStatus status)
{
    if (!reader || reader->status != DLUMA_STATUS_OK) return;
    reader->status = status;
    reader->error_offset = reader->begin && reader->cursor
        ? (unsigned int)(reader->cursor - reader->begin)
        : 0U;
}

static int dluma_json_is_space(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int dluma_json_is_digit(unsigned char value)
{
    return value >= '0' && value <= '9';
}

static int dluma_json_hex(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    return -1;
}

static int dluma_json_literal(DlumaJsonReader* reader, const char* text)
{
    const unsigned char* start;
    const char* cursor;
    if (!reader || !text || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    start = reader->cursor;
    cursor = text;
    while (*cursor) {
        if (reader->cursor >= reader->end ||
            *reader->cursor != (unsigned char)*cursor) {
            reader->cursor = start;
            return 0;
        }
        ++reader->cursor;
        ++cursor;
    }
    return 1;
}

static int dluma_json_append_byte(
    DlumaJsonReader* reader,
    char* output,
    unsigned int capacity,
    unsigned int* length,
    unsigned char value)
{
    if (!length || !value) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    if (output) {
        if (*length + 1U >= capacity) {
            dluma_json_fail(reader, DLUMA_STATUS_INSUFFICIENT_BUFFER);
            return 0;
        }
        output[*length] = (char)value;
    }
    ++*length;
    return 1;
}

static int dluma_json_append_codepoint(
    DlumaJsonReader* reader,
    char* output,
    unsigned int capacity,
    unsigned int* length,
    unsigned int codepoint)
{
    if (!codepoint || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    if (codepoint <= 0x7FU)
        return dluma_json_append_byte(
            reader, output, capacity, length, (unsigned char)codepoint);
    if (codepoint <= 0x7FFU)
        return dluma_json_append_byte(
                   reader, output, capacity, length,
                   (unsigned char)(0xC0U | (codepoint >> 6U))) &&
               dluma_json_append_byte(
                   reader, output, capacity, length,
                   (unsigned char)(0x80U | (codepoint & 0x3FU)));
    if (codepoint <= 0xFFFFU)
        return dluma_json_append_byte(
                   reader, output, capacity, length,
                   (unsigned char)(0xE0U | (codepoint >> 12U))) &&
               dluma_json_append_byte(
                   reader, output, capacity, length,
                   (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU))) &&
               dluma_json_append_byte(
                   reader, output, capacity, length,
                   (unsigned char)(0x80U | (codepoint & 0x3FU)));
    return dluma_json_append_byte(
               reader, output, capacity, length,
               (unsigned char)(0xF0U | (codepoint >> 18U))) &&
           dluma_json_append_byte(
               reader, output, capacity, length,
               (unsigned char)(0x80U | ((codepoint >> 12U) & 0x3FU))) &&
           dluma_json_append_byte(
               reader, output, capacity, length,
               (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3FU))) &&
           dluma_json_append_byte(
               reader, output, capacity, length,
               (unsigned char)(0x80U | (codepoint & 0x3FU)));
}

static int dluma_json_read_u16_escape(
    DlumaJsonReader* reader,
    unsigned int* output)
{
    unsigned int index;
    unsigned int value = 0;
    if (!reader || !output) return 0;
    for (index = 0; index < 4U; ++index) {
        int digit;
        if (reader->cursor >= reader->end) {
            dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
            return 0;
        }
        digit = dluma_json_hex(*reader->cursor++);
        if (digit < 0) {
            dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
            return 0;
        }
        value = (value << 4U) | (unsigned int)digit;
    }
    *output = value;
    return 1;
}

static int dluma_json_copy_raw_utf8(
    DlumaJsonReader* reader,
    char* output,
    unsigned int capacity,
    unsigned int* length)
{
    unsigned char first;
    unsigned int count;
    unsigned int codepoint;
    unsigned int minimum;
    unsigned int index;
    if (!reader || reader->cursor >= reader->end) return 0;
    first = *reader->cursor;
    if (first < 0x80U) {
        if (first < 0x20U || !first) {
            dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
            return 0;
        }
        ++reader->cursor;
        return dluma_json_append_byte(
            reader, output, capacity, length, first);
    }
    if ((first & 0xE0U) == 0xC0U) {
        count = 2U;
        codepoint = first & 0x1FU;
        minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
        count = 3U;
        codepoint = first & 0x0FU;
        minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
        count = 4U;
        codepoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    if ((unsigned int)(reader->end - reader->cursor) < count) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    for (index = 1U; index < count; ++index) {
        unsigned char next = reader->cursor[index];
        if ((next & 0xC0U) != 0x80U) {
            dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
            return 0;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    for (index = 0; index < count; ++index)
        if (!dluma_json_append_byte(
                reader, output, capacity, length, reader->cursor[index]))
            return 0;
    reader->cursor += count;
    return 1;
}

void dluma_json_reader_initialize(
    DlumaJsonReader* reader,
    const void* bytes,
    unsigned int size)
{
    if (!reader) return;
    reader->begin = (const unsigned char*)bytes;
    reader->cursor = (const unsigned char*)bytes;
    reader->end = reader->cursor ? reader->cursor + size : 0;
    reader->depth = 0;
    reader->error_offset = 0;
    reader->status =
        bytes || !size ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
}

void dluma_json_space(DlumaJsonReader* reader)
{
    if (!reader || reader->status != DLUMA_STATUS_OK) return;
    while (reader->cursor < reader->end &&
           dluma_json_is_space(*reader->cursor))
        ++reader->cursor;
}

int dluma_json_take(DlumaJsonReader* reader, unsigned char expected)
{
    if (!reader || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    if (reader->cursor >= reader->end || *reader->cursor != expected) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    ++reader->cursor;
    return 1;
}

int dluma_json_object_begin(DlumaJsonReader* reader)
{
    if (!reader || reader->depth >= DLUMA_JSON_MAX_DEPTH) {
        dluma_json_fail(reader, DLUMA_STATUS_LIMIT_EXCEEDED);
        return 0;
    }
    if (!dluma_json_take(reader, '{')) return 0;
    ++reader->depth;
    return 1;
}

int dluma_json_object_next(DlumaJsonReader* reader, int* first)
{
    if (!reader || !first || reader->status != DLUMA_STATUS_OK) return -1;
    dluma_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == '}') return 0;
    if (!*first && !dluma_json_take(reader, ',')) return -1;
    *first = 0;
    return 1;
}

int dluma_json_object_end(DlumaJsonReader* reader)
{
    if (!reader || !reader->depth || !dluma_json_take(reader, '}')) return 0;
    --reader->depth;
    return 1;
}

int dluma_json_array_begin(DlumaJsonReader* reader)
{
    if (!reader || reader->depth >= DLUMA_JSON_MAX_DEPTH) {
        dluma_json_fail(reader, DLUMA_STATUS_LIMIT_EXCEEDED);
        return 0;
    }
    if (!dluma_json_take(reader, '[')) return 0;
    ++reader->depth;
    return 1;
}

int dluma_json_array_next(DlumaJsonReader* reader, int* first)
{
    if (!reader || !first || reader->status != DLUMA_STATUS_OK) return -1;
    dluma_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == ']') return 0;
    if (!*first && !dluma_json_take(reader, ',')) return -1;
    *first = 0;
    return 1;
}

int dluma_json_array_end(DlumaJsonReader* reader)
{
    if (!reader || !reader->depth || !dluma_json_take(reader, ']')) return 0;
    --reader->depth;
    return 1;
}

int dluma_json_string(
    DlumaJsonReader* reader,
    char* output,
    unsigned int capacity)
{
    unsigned int length = 0;
    if (!reader || reader->status != DLUMA_STATUS_OK ||
        (output && !capacity))
        return 0;
    if (!dluma_json_take(reader, '"')) return 0;
    while (reader->cursor < reader->end) {
        unsigned char value = *reader->cursor;
        if (value == '"') {
            ++reader->cursor;
            if (output) output[length] = 0;
            return 1;
        }
        if (value == '\\') {
            ++reader->cursor;
            if (reader->cursor >= reader->end) break;
            value = *reader->cursor++;
            if (value == '"' || value == '\\' || value == '/') {
                if (!dluma_json_append_byte(
                        reader, output, capacity, &length, value))
                    return 0;
            } else if (value == 'b' || value == 'f' ||
                       value == 'n' || value == 'r' || value == 't') {
                unsigned char decoded =
                    value == 'b' ? '\b' :
                    value == 'f' ? '\f' :
                    value == 'n' ? '\n' :
                    value == 'r' ? '\r' : '\t';
                if (!dluma_json_append_byte(
                        reader, output, capacity, &length, decoded))
                    return 0;
            } else if (value == 'u') {
                unsigned int high;
                unsigned int codepoint;
                if (!dluma_json_read_u16_escape(reader, &high)) return 0;
                codepoint = high;
                if (high >= 0xD800U && high <= 0xDBFFU) {
                    unsigned int low;
                    if (reader->end - reader->cursor < 6 ||
                        reader->cursor[0] != '\\' ||
                        reader->cursor[1] != 'u') {
                        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
                        return 0;
                    }
                    reader->cursor += 2;
                    if (!dluma_json_read_u16_escape(reader, &low) ||
                        low < 0xDC00U || low > 0xDFFFU) {
                        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
                        return 0;
                    }
                    codepoint = 0x10000U +
                        ((high - 0xD800U) << 10U) +
                        (low - 0xDC00U);
                }
                if (!dluma_json_append_codepoint(
                        reader, output, capacity, &length, codepoint))
                    return 0;
            } else {
                dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
                return 0;
            }
        } else if (!dluma_json_copy_raw_utf8(
                       reader, output, capacity, &length)) {
            return 0;
        }
    }
    dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
    return 0;
}

int dluma_json_boolean(DlumaJsonReader* reader, int* output)
{
    if (!reader || !output) return 0;
    if (dluma_json_literal(reader, "true")) {
        *output = 1;
        return 1;
    }
    if (dluma_json_literal(reader, "false")) {
        *output = 0;
        return 1;
    }
    dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
    return 0;
}

int dluma_json_i32(DlumaJsonReader* reader, int* output)
{
    int negative = 0;
    uint64_t value = 0;
    if (!reader || !output || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    if (reader->cursor < reader->end && *reader->cursor == '-') {
        negative = 1;
        ++reader->cursor;
    }
    if (reader->cursor >= reader->end ||
        !dluma_json_is_digit(*reader->cursor) ||
        (*reader->cursor == '0' && reader->cursor + 1 < reader->end &&
         dluma_json_is_digit(reader->cursor[1]))) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    while (reader->cursor < reader->end &&
           dluma_json_is_digit(*reader->cursor)) {
        unsigned int digit = (unsigned int)(*reader->cursor++ - '0');
        if (value > (0x80000000ULL - digit) / 10ULL) {
            dluma_json_fail(reader, DLUMA_STATUS_LIMIT_EXCEEDED);
            return 0;
        }
        value = value * 10ULL + digit;
    }
    if ((!negative && value > 0x7FFFFFFFULL) ||
        (negative && value > 0x80000000ULL)) {
        dluma_json_fail(reader, DLUMA_STATUS_LIMIT_EXCEEDED);
        return 0;
    }
    *output = negative
        ? (value == 0x80000000ULL ? INT_MIN : -(int)value)
        : (int)value;
    return 1;
}

int dluma_json_u64(DlumaJsonReader* reader, uint64_t* output)
{
    uint64_t value = 0;
    if (!reader || !output || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    if (reader->cursor >= reader->end ||
        !dluma_json_is_digit(*reader->cursor) ||
        (*reader->cursor == '0' && reader->cursor + 1 < reader->end &&
         dluma_json_is_digit(reader->cursor[1]))) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    while (reader->cursor < reader->end &&
           dluma_json_is_digit(*reader->cursor)) {
        unsigned int digit = (unsigned int)(*reader->cursor++ - '0');
        if (value > (UINT64_MAX - digit) / 10ULL) {
            dluma_json_fail(reader, DLUMA_STATUS_LIMIT_EXCEEDED);
            return 0;
        }
        value = value * 10ULL + digit;
    }
    *output = value;
    return 1;
}

static int dluma_json_skip_number(DlumaJsonReader* reader)
{
    const unsigned char* cursor;
    if (!reader || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    cursor = reader->cursor;
    if (cursor < reader->end && *cursor == '-') ++cursor;
    if (cursor >= reader->end || !dluma_json_is_digit(*cursor))
        goto invalid;
    if (*cursor == '0') {
        ++cursor;
        if (cursor < reader->end && dluma_json_is_digit(*cursor))
            goto invalid;
    } else {
        while (cursor < reader->end && dluma_json_is_digit(*cursor))
            ++cursor;
    }
    if (cursor < reader->end && *cursor == '.') {
        ++cursor;
        if (cursor >= reader->end || !dluma_json_is_digit(*cursor))
            goto invalid;
        while (cursor < reader->end && dluma_json_is_digit(*cursor))
            ++cursor;
    }
    if (cursor < reader->end && (*cursor == 'e' || *cursor == 'E')) {
        ++cursor;
        if (cursor < reader->end && (*cursor == '+' || *cursor == '-'))
            ++cursor;
        if (cursor >= reader->end || !dluma_json_is_digit(*cursor))
            goto invalid;
        while (cursor < reader->end && dluma_json_is_digit(*cursor))
            ++cursor;
    }
    reader->cursor = cursor;
    return 1;
invalid:
    dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
    return 0;
}

int dluma_json_skip_value(DlumaJsonReader* reader)
{
    int first = 1;
    if (!reader || reader->status != DLUMA_STATUS_OK) return 0;
    dluma_json_space(reader);
    if (reader->cursor >= reader->end) return 0;
    if (*reader->cursor == '"')
        return dluma_json_string(reader, 0, 0);
    if (*reader->cursor == '{') {
        if (!dluma_json_object_begin(reader)) return 0;
        for (;;) {
            int next = dluma_json_object_next(reader, &first);
            if (next < 0) return 0;
            if (!next) break;
            if (!dluma_json_string(reader, 0, 0) ||
                !dluma_json_take(reader, ':') ||
                !dluma_json_skip_value(reader))
                return 0;
        }
        return dluma_json_object_end(reader);
    }
    if (*reader->cursor == '[') {
        if (!dluma_json_array_begin(reader)) return 0;
        for (;;) {
            int next = dluma_json_array_next(reader, &first);
            if (next < 0) return 0;
            if (!next) break;
            if (!dluma_json_skip_value(reader)) return 0;
        }
        return dluma_json_array_end(reader);
    }
    if (dluma_json_literal(reader, "true") ||
        dluma_json_literal(reader, "false") ||
        dluma_json_literal(reader, "null"))
        return 1;
    return dluma_json_skip_number(reader);
}

int dluma_json_finished(DlumaJsonReader* reader)
{
    if (!reader || reader->status != DLUMA_STATUS_OK || reader->depth) return 0;
    dluma_json_space(reader);
    if (reader->cursor != reader->end) {
        dluma_json_fail(reader, DLUMA_STATUS_INVALID_ARGUMENT);
        return 0;
    }
    return 1;
}

unsigned int dluma_json_error_offset(const DlumaJsonReader* reader)
{
    return reader ? reader->error_offset : 0U;
}

DlumaStatus dluma_json_status(const DlumaJsonReader* reader)
{
    return reader ? reader->status : DLUMA_STATUS_INVALID_ARGUMENT;
}
