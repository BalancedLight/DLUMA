#include "dlw_presence_core.h"

#include <string.h>

#define DLUMA_PRESENCE_RAW_CAPACITY \
    (DLUMA_PRESENCE_TEXT_CAPACITY * 3U + 16U)

static unsigned int dluma_presence_bounded_length(
    const char* value, unsigned int capacity)
{
    unsigned int length = 0;
    if (!value) return capacity;
    while (length < capacity && value[length]) ++length;
    return length;
}

static int dluma_presence_ascii_equal_ci(char left, char right)
{
    if (left >= 'A' && left <= 'Z') left = (char)(left + ('a' - 'A'));
    if (right >= 'A' && right <= 'Z') right = (char)(right + ('a' - 'A'));
    return left == right;
}

static int dluma_presence_contains_ci(
    const char* value, unsigned int length, const char* needle)
{
    unsigned int needle_length = 0;
    unsigned int i;
    unsigned int j;
    while (needle[needle_length]) ++needle_length;
    if (!needle_length || needle_length > length) return 0;
    for (i = 0; i + needle_length <= length; ++i) {
        for (j = 0; j < needle_length; ++j)
            if (!dluma_presence_ascii_equal_ci(value[i + j], needle[j]))
                break;
        if (j == needle_length) return 1;
    }
    return 0;
}

static int dluma_presence_token_looks_like_domain(
    const char* value, unsigned int length)
{
    unsigned int i;
    if (!value || length < 4U) return 0;
    for (i = 1U; i + 2U < length; ++i) {
        unsigned int suffix = i + 1U;
        unsigned int letters = 0;
        unsigned char previous;
        if (value[i] != '.') continue;
        previous = (unsigned char)value[i - 1U];
        if (!((previous >= 'A' && previous <= 'Z') ||
              (previous >= 'a' && previous <= 'z') ||
              (previous >= '0' && previous <= '9')))
            continue;
        while (suffix < length) {
            unsigned char byte = (unsigned char)value[suffix];
            if ((byte >= 'A' && byte <= 'Z') ||
                (byte >= 'a' && byte <= 'z')) {
                ++letters;
                ++suffix;
                continue;
            }
            if (byte == ')' || byte == ']' || byte == '}' ||
                byte == ',' || byte == ';' || byte == ':' ||
                byte == '!' || byte == '?' || byte == '\'' ||
                byte == '"')
                break;
            letters = 0;
            break;
        }
        if (letters >= 2U && letters <= 24U) return 1;
    }
    return 0;
}

static int dluma_presence_token_is_private(
    const char* value, unsigned int length)
{
    unsigned int i;
    unsigned int digit_start = 0;
    unsigned int digit_end = length;
    unsigned int digits = 0;
    int all_digits;
    if (!value || !length) return 0;
    if (dluma_presence_contains_ci(value, length, "://") ||
        dluma_presence_contains_ci(value, length, "www.") ||
        dluma_presence_contains_ci(value, length, "steamcommunity.com") ||
        dluma_presence_contains_ci(value, length, "steamusercontent") ||
        dluma_presence_token_looks_like_domain(value, length))
        return 1;
    while (digit_start < digit_end &&
           (value[digit_start] == '(' || value[digit_start] == '[' ||
            value[digit_start] == '{' || value[digit_start] == '\'' ||
            value[digit_start] == '"'))
        ++digit_start;
    while (digit_end > digit_start &&
           (value[digit_end - 1U] == ')' ||
            value[digit_end - 1U] == ']' ||
            value[digit_end - 1U] == '}' ||
            value[digit_end - 1U] == '\'' ||
            value[digit_end - 1U] == '"' ||
            value[digit_end - 1U] == ',' ||
            value[digit_end - 1U] == '.' ||
            value[digit_end - 1U] == ';' ||
            value[digit_end - 1U] == ':' ||
            value[digit_end - 1U] == '!' ||
            value[digit_end - 1U] == '?'))
        --digit_end;
    all_digits = digit_end > digit_start;
    for (i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)value[i];
        if (byte == '/' || byte == '\\') return 1;
    }
    for (i = digit_start; i < digit_end; ++i) {
        unsigned char byte = (unsigned char)value[i];
        if (byte >= '0' && byte <= '9') ++digits;
        else all_digits = 0;
    }
    if (length >= 3U &&
        ((value[0] >= 'A' && value[0] <= 'Z') ||
         (value[0] >= 'a' && value[0] <= 'z')) &&
        value[1] == ':')
        return 1;
    return all_digits && digits >= 8U;
}

static unsigned int dluma_presence_utf8_sequence_length(
    const unsigned char* value, unsigned int remaining,
    unsigned int* codepoint_out)
{
    unsigned int codepoint;
    unsigned int needed;
    unsigned int i;
    unsigned char first;
    if (codepoint_out) *codepoint_out = 0;
    if (!value || !remaining) return 0;
    first = value[0];
    if (first < 0x80U) {
        if (codepoint_out) *codepoint_out = first;
        return 1;
    }
    if (first >= 0xC2U && first <= 0xDFU) {
        needed = 2U;
        codepoint = first & 0x1FU;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        needed = 3U;
        codepoint = first & 0x0FU;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        needed = 4U;
        codepoint = first & 0x07U;
    } else {
        return 0;
    }
    if (needed > remaining) return 0;
    for (i = 1; i < needed; ++i) {
        if ((value[i] & 0xC0U) != 0x80U) return 0;
        codepoint = (codepoint << 6) | (value[i] & 0x3FU);
    }
    if ((needed == 3U && codepoint < 0x800U) ||
        (needed == 4U && codepoint < 0x10000U) ||
        codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        return 0;
    if (codepoint_out) *codepoint_out = codepoint;
    return needed;
}

static int dluma_presence_codepoint_is_space_or_control(
    unsigned int codepoint)
{
    return codepoint <= 0x20U || codepoint == 0x7FU ||
           (codepoint >= 0x80U && codepoint <= 0x9FU) ||
           codepoint == 0x2028U || codepoint == 0x2029U;
}

static void dluma_presence_append_raw(
    char* output, unsigned int capacity, const char* value)
{
    unsigned int offset = dluma_presence_bounded_length(output, capacity);
    unsigned int length = dluma_presence_bounded_length(
        value, DLUMA_PRESENCE_TEXT_CAPACITY);
    unsigned int i;
    if (!output || !capacity || !value ||
        offset >= capacity || length >= DLUMA_PRESENCE_TEXT_CAPACITY)
        return;
    for (i = 0; i < length && offset + 1U < capacity; ++i)
        output[offset++] = value[i];
    output[offset] = 0;
}

static unsigned int dluma_presence_copy_token(
    const char* token, unsigned int token_length,
    char* output, unsigned int output_capacity,
    unsigned int offset)
{
    unsigned int cursor = 0;
    if (offset && offset + 1U < output_capacity &&
        offset < DLUMA_STEAM_STATUS_MAX_UTF8_BYTES)
        output[offset++] = ' ';
    while (cursor < token_length) {
        unsigned int codepoint = 0;
        unsigned int sequence = dluma_presence_utf8_sequence_length(
            (const unsigned char*)token + cursor,
            token_length - cursor, &codepoint);
        if (!sequence) {
            ++cursor;
            continue;
        }
        if (!dluma_presence_codepoint_is_space_or_control(codepoint)) {
            if (offset + sequence >= output_capacity ||
                offset + sequence > DLUMA_STEAM_STATUS_MAX_UTF8_BYTES)
                break;
            memcpy(output + offset, token + cursor, sequence);
            offset += sequence;
        }
        cursor += sequence;
    }
    output[offset] = 0;
    return offset;
}

int dluma_presence_sanitize_text(
    const char* input, char* output, unsigned int output_capacity)
{
    unsigned int input_length;
    unsigned int cursor = 0;
    unsigned int output_length = 0;
    if (!output || output_capacity < 2U) return 0;
    output[0] = 0;
    input_length = dluma_presence_bounded_length(
        input, DLUMA_PRESENCE_RAW_CAPACITY);
    if (!input || input_length >= DLUMA_PRESENCE_RAW_CAPACITY) return 0;
    while (cursor < input_length) {
        unsigned int token_start;
        unsigned int token_end;
        unsigned int codepoint = 0;
        unsigned int sequence = dluma_presence_utf8_sequence_length(
            (const unsigned char*)input + cursor,
            input_length - cursor, &codepoint);
        if (!sequence || dluma_presence_codepoint_is_space_or_control(codepoint)) {
            cursor += sequence ? sequence : 1U;
            continue;
        }
        token_start = cursor;
        token_end = cursor;
        while (token_end < input_length) {
            sequence = dluma_presence_utf8_sequence_length(
                (const unsigned char*)input + token_end,
                input_length - token_end, &codepoint);
            if (!sequence ||
                dluma_presence_codepoint_is_space_or_control(codepoint))
                break;
            token_end += sequence;
        }
        if (!dluma_presence_token_is_private(
                input + token_start, token_end - token_start))
            output_length = dluma_presence_copy_token(
                input + token_start, token_end - token_start,
                output, output_capacity, output_length);
        cursor = token_end;
        if (output_length >= DLUMA_STEAM_STATUS_MAX_UTF8_BYTES ||
            output_length + 1U >= output_capacity)
            break;
    }
    while (output_length && output[output_length - 1U] == ' ')
        output[--output_length] = 0;
    return output_length != 0;
}

int dluma_presence_snapshot_equal(
    const DlumaPresenceSnapshot* left,
    const DlumaPresenceSnapshot* right)
{
    if (!left || !right) return left == right;
    return left->struct_size == right->struct_size &&
           left->context == right->context &&
           left->flags == right->flags &&
           memcmp(left->primary_text, right->primary_text,
                  sizeof(left->primary_text)) == 0 &&
           memcmp(left->secondary_text, right->secondary_text,
                  sizeof(left->secondary_text)) == 0 &&
           memcmp(left->pause_text, right->pause_text,
                  sizeof(left->pause_text)) == 0;
}

int dluma_presence_format_steam_status(
    const DlumaPresenceSnapshot* snapshot,
    char* output,
    unsigned int output_capacity)
{
    char raw[DLUMA_PRESENCE_RAW_CAPACITY];
    if (output && output_capacity) output[0] = 0;
    if (!snapshot || !output || output_capacity < 2U ||
        snapshot->struct_size != sizeof(*snapshot))
        return 0;
    raw[0] = 0;
    if (snapshot->context == DLUMA_PRESENCE_CONTEXT_FRONTEND) {
        dluma_presence_append_raw(
            raw, sizeof(raw), snapshot->primary_text);
    } else if (snapshot->context == DLUMA_PRESENCE_CONTEXT_GAMEPLAY) {
        if (snapshot->flags & DLUMA_PRESENCE_HAS_QUEST) {
            dluma_presence_append_raw(
                raw, sizeof(raw), snapshot->primary_text);
            if (raw[0] && snapshot->secondary_text[0])
                dluma_presence_append_raw(raw, sizeof(raw), " | ");
        }
        dluma_presence_append_raw(
            raw, sizeof(raw), snapshot->secondary_text);
        if ((snapshot->flags & DLUMA_PRESENCE_PAUSE_KNOWN) &&
            snapshot->pause_text[0]) {
            if (raw[0])
                dluma_presence_append_raw(raw, sizeof(raw), " | ");
            dluma_presence_append_raw(
                raw, sizeof(raw), snapshot->pause_text);
        }
    } else {
        return 0;
    }
    return dluma_presence_sanitize_text(
        raw, output, output_capacity);
}
