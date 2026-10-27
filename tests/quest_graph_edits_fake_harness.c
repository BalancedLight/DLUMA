#include "core/dlw_quest_graph_edits.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do {                                            \
    if (!(expression)) {                                                  \
        fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                __FILE__, __LINE__, #expression);                         \
        return 0;                                                         \
    }                                                                     \
} while (0)

static int case_empty_replacement(void)
{
    char value[16];
    uint32_t size = 99U;
    CHECK(dluma_quest_graph_json_string(
        "{\"replacementBytes\":\"\"}", "replacementBytes",
        value, sizeof(value), 1));
    CHECK(!dluma_quest_graph_json_string(
        "{\"documentId\":\"\"}", "documentId",
        value, sizeof(value), 0));
    CHECK(dluma_quest_graph_base64_decode_in_place(
        value, 1024U, &size));
    CHECK(size == 0U);
    return 1;
}

static int case_base64_validation(void)
{
    char valid[16] = "UVVFU1Q=";
    char malformed_length[16] = "QQ=";
    char malformed_alphabet[16] = "Q?==";
    char malformed_padding[16] = "QQ==AAAA";
    uint32_t size = 0;
    CHECK(dluma_quest_graph_base64_decode_in_place(
        valid, 1024U, &size));
    CHECK(size == 5U && memcmp(valid, "QUEST", 5U) == 0);
    CHECK(!dluma_quest_graph_base64_decode_in_place(
        malformed_length, 1024U, &size));
    CHECK(!dluma_quest_graph_base64_decode_in_place(
        malformed_alphabet, 1024U, &size));
    CHECK(!dluma_quest_graph_base64_decode_in_place(
        malformed_padding, 1024U, &size));
    return 1;
}

static int case_edit_order_and_result(void)
{
    char first[] = "A";
    DlumaQuestGraphTextEdit edits[] = {
        {8U, 2U, 1U, first},
        {2U, 3U, 0U, 0}
    };
    uint32_t result = 0;
    uint32_t index = 0;
    CHECK(dluma_quest_graph_edits_validate(
        edits, 2U, 16U, 12U, 32U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_OK);
    CHECK(result == 8U && index == 2U);
    edits[1].byte_offset = 7U;
    CHECK(dluma_quest_graph_edits_validate(
        edits, 2U, 16U, 12U, 32U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_ORDERING_INVALID);
    CHECK(index == 1U);
    return 1;
}

static int case_bounds_and_caps(void)
{
    char replacement[8] = "1234567";
    DlumaQuestGraphTextEdit edit = {9U, 2U, 0U, 0};
    uint32_t result = 0;
    uint32_t index = 0;
    CHECK(dluma_quest_graph_edits_validate(
        &edit, 1U, 16U, 10U, 16U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_RANGE_INVALID);
    edit.byte_offset = 9U;
    edit.removed_length = 1U;
    edit.replacement = replacement;
    edit.replacement_size = 7U;
    CHECK(dluma_quest_graph_edits_validate(
        &edit, 1U, 16U, 10U, 15U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_RESULT_TOO_LARGE);
    edit.replacement = 0;
    CHECK(dluma_quest_graph_edits_validate(
        &edit, 1U, 16U, 10U, 32U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID);
    CHECK(dluma_quest_graph_edits_validate(
        &edit, 0U, 16U, 10U, 32U, &result, &index) ==
        DLUMA_QUEST_GRAPH_EDIT_PAYLOAD_INVALID);
    return 1;
}

int main(void)
{
    unsigned int passed = 0;
    if (!case_empty_replacement()) return 1;
    puts("[PASS] empty replacement is field-scoped"); ++passed;
    if (!case_base64_validation()) return 1;
    puts("[PASS] malformed Base64 is rejected"); ++passed;
    if (!case_edit_order_and_result()) return 1;
    puts("[PASS] edit ordering and result size"); ++passed;
    if (!case_bounds_and_caps()) return 1;
    puts("[PASS] edit bounds and caps"); ++passed;
    printf("quest graph edit cases passed: %u\n", passed);
    return 0;
}
