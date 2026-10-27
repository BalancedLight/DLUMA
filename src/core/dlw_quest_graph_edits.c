#include "dlw_quest_graph_edits.h"

#include <stddef.h>

static uint32_t dluma_qg_text_length(const char* text)
{
    uint32_t length = 0;
    if (!text) return 0;
    while (text[length]) {
        if (length == UINT32_MAX) return 0;
        ++length;
    }
    return length;
}

static const char* dluma_qg_find(const char* text, const char* needle)
{
    const char* candidate;
    if (!text || !needle || !needle[0]) return 0;
    for (candidate = text; *candidate; ++candidate) {
        const char* left = candidate;
        const char* right = needle;
        while (*left && *right && *left == *right) {
            ++left;
            ++right;
        }
        if (!*right) return candidate;
    }
    return 0;
}

int dluma_quest_graph_json_string(
    const char* json,
    const char* field,
    char* output,
    uint32_t capacity,
    int allow_empty)
{
    char needle[96];
    uint32_t field_length;
    uint32_t needle_length;
    uint32_t used = 0;
    const char* start;
    if (!json || !field || !output || capacity < 2U)
        return 0;
    field_length = dluma_qg_text_length(field);
    if (!field_length || field_length + 5U >= sizeof(needle))
        return 0;
    needle[0] = '"';
    for (needle_length = 0; needle_length < field_length; ++needle_length)
        needle[needle_length + 1U] = field[needle_length];
    needle_length = field_length + 1U;
    needle[needle_length++] = '"';
    needle[needle_length++] = ':';
    needle[needle_length++] = '"';
    needle[needle_length] = 0;
    start = dluma_qg_find(json, needle);
    if (!start) return 0;
    start += needle_length;
    while (*start && *start != '"') {
        if (*start == '\\' || (unsigned char)*start < 0x20U ||
            used + 1U >= capacity)
            return 0;
        output[used++] = *start++;
    }
    if (*start != '"') return 0;
    output[used] = 0;
    return allow_empty || used != 0U;
}

static int dluma_qg_base64_value(char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

int dluma_quest_graph_base64_decode_in_place(
    char* encoded,
    uint32_t maximum_decoded_size,
    uint32_t* decoded_size)
{
    uint32_t input_size;
    uint32_t input_index;
    uint32_t output_index = 0;
    if (!encoded || !decoded_size) return 0;
    input_size = dluma_qg_text_length(encoded);
    if (!input_size) {
        *decoded_size = 0;
        return 1;
    }
    if ((input_size & 3U) != 0U) return 0;
    for (input_index = 0; input_index < input_size; input_index += 4U) {
        int first = dluma_qg_base64_value(encoded[input_index]);
        int second = dluma_qg_base64_value(encoded[input_index + 1U]);
        int third = encoded[input_index + 2U] == '='
            ? -2 : dluma_qg_base64_value(encoded[input_index + 2U]);
        int fourth = encoded[input_index + 3U] == '='
            ? -2 : dluma_qg_base64_value(encoded[input_index + 3U]);
        uint32_t value;
        if (first < 0 || second < 0 || third == -1 || fourth == -1 ||
            (third == -2 && fourth != -2) ||
            (third == -2 && (second & 15) != 0) ||
            (fourth == -2 && third >= 0 && (third & 3) != 0) ||
            ((third == -2 || fourth == -2) &&
             input_index + 4U != input_size))
            return 0;
        value = ((uint32_t)first << 18) | ((uint32_t)second << 12);
        if (third >= 0) value |= (uint32_t)third << 6;
        if (fourth >= 0) value |= (uint32_t)fourth;
        if (output_index >= maximum_decoded_size) return 0;
        encoded[output_index++] = (char)((value >> 16) & 0xFFU);
        if (third >= 0) {
            if (output_index >= maximum_decoded_size) return 0;
            encoded[output_index++] = (char)((value >> 8) & 0xFFU);
        }
        if (fourth >= 0) {
            if (output_index >= maximum_decoded_size) return 0;
            encoded[output_index++] = (char)(value & 0xFFU);
        }
    }
    for (input_index = 0; input_index < output_index; ++input_index)
        if (!encoded[input_index]) return 0;
    encoded[output_index] = 0;
    *decoded_size = output_index;
    return 1;
}

DlumaQuestGraphEditError dluma_quest_graph_edits_validate(
    const DlumaQuestGraphTextEdit* edits,
    uint32_t edit_count,
    uint32_t maximum_edit_count,
    uint32_t source_size,
    uint32_t maximum_source_size,
    uint32_t* result_size,
    uint32_t* error_index)
{
    uint64_t size = source_size;
    uint32_t previous_offset = source_size;
    uint32_t index;
    if (result_size) *result_size = 0;
    if (error_index) *error_index = 0;
    if (!edits || !edit_count || edit_count > maximum_edit_count ||
        source_size > maximum_source_size)
        return DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID;
    for (index = 0; index < edit_count; ++index) {
        const DlumaQuestGraphTextEdit* edit = &edits[index];
        uint64_t end = (uint64_t)edit->byte_offset + edit->removed_length;
        if (error_index) *error_index = index;
        if (edit->replacement_size && !edit->replacement)
            return DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID;
        if (edit->byte_offset > source_size || end > source_size ||
            size < edit->removed_length)
            return DLUMA_QUEST_GRAPH_EDIT_RANGE_INVALID;
        if (index && end > previous_offset)
            return DLUMA_QUEST_GRAPH_EDIT_ORDERING_INVALID;
        size = size - edit->removed_length + edit->replacement_size;
        if (size > maximum_source_size)
            return DLUMA_QUEST_GRAPH_EDIT_RESULT_TOO_LARGE;
        previous_offset = edit->byte_offset;
    }
    if (result_size) *result_size = (uint32_t)size;
    if (error_index) *error_index = edit_count;
    return DLUMA_QUEST_GRAPH_EDIT_OK;
}

const char* dluma_quest_graph_edit_error_code(
    DlumaQuestGraphEditError error)
{
    if (error == DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID)
        return "edit_payload_invalid";
    if (error == DLUMA_QUEST_GRAPH_EDIT_RANGE_INVALID)
        return "edit_range_invalid";
    if (error == DLUMA_QUEST_GRAPH_EDIT_ORDERING_INVALID)
        return "edit_ordering_invalid";
    if (error == DLUMA_QUEST_GRAPH_EDIT_RESULT_TOO_LARGE)
        return "edit_result_too_large";
    return error == DLUMA_QUEST_GRAPH_EDIT_OK
        ? "ok" : "edit_validation_failed";
}
