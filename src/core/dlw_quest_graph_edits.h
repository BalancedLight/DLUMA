#ifndef DLUMA_QUEST_GRAPH_EDITS_H
#define DLUMA_QUEST_GRAPH_EDITS_H

#include <stdint.h>

typedef struct DlumaQuestGraphTextEdit {
    uint32_t byte_offset;
    uint32_t removed_length;
    uint32_t replacement_size;
    char* replacement;
} DlumaQuestGraphTextEdit;

typedef enum DlumaQuestGraphEditError {
    DLUMA_QUEST_GRAPH_EDIT_OK = 0,
    DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID = 1,
    DLUMA_QUEST_GRAPH_EDIT_RANGE_INVALID = 2,
    DLUMA_QUEST_GRAPH_EDIT_ORDERING_INVALID = 3,
    DLUMA_QUEST_GRAPH_EDIT_RESULT_TOO_LARGE = 4
} DlumaQuestGraphEditError;

int dluma_quest_graph_json_string(
    const char* json,
    const char* field,
    char* output,
    uint32_t capacity,
    int allow_empty);

int dluma_quest_graph_base64_decode_in_place(
    char* encoded,
    uint32_t maximum_decoded_size,
    uint32_t* decoded_size);

DlumaQuestGraphEditError dluma_quest_graph_edits_validate(
    const DlumaQuestGraphTextEdit* edits,
    uint32_t edit_count,
    uint32_t maximum_edit_count,
    uint32_t source_size,
    uint32_t maximum_source_size,
    uint32_t* result_size,
    uint32_t* error_index);

const char* dluma_quest_graph_edit_error_code(
    DlumaQuestGraphEditError error);

#endif
