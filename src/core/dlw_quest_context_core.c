#include "dlw_quest_context_core.h"

#include <stddef.h>

typedef struct DlumaQuestContextToken {
    const char* start;
    uint32_t length;
} DlumaQuestContextToken;

static uint32_t dluma_context_length_bounded(
    const char* value, uint32_t maximum)
{
    uint32_t length = 0;
    if (!value) return 0;
    while (length < maximum && value[length]) ++length;
    return length;
}

static int dluma_context_equal_icase_n(
    const char* left, uint32_t left_length, const char* right)
{
    uint32_t index = 0;
    if (!left || !right) return 0;
    while (index < left_length && right[index]) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        ++index;
    }
    return index == left_length && right[index] == 0;
}

static int dluma_context_equal_icase(const char* left, const char* right)
{
    uint32_t left_length = dluma_context_length_bounded(left, UINT32_MAX);
    return dluma_context_equal_icase_n(left, left_length, right);
}

static int dluma_context_copy(
    char* output, uint32_t capacity,
    const char* value, uint32_t value_length)
{
    uint32_t index;
    if (!output || capacity < 2U || !value ||
        !value_length || value_length >= capacity)
        return 0;
    for (index = 0; index < value_length; ++index) {
        unsigned char byte = (unsigned char)value[index];
        if (byte < 0x20U || byte == 0x7FU) return 0;
        output[index] = value[index];
    }
    output[value_length] = 0;
    return 1;
}

static void dluma_context_trim(
    const char** value, uint32_t* length)
{
    if (!value || !*value || !length) return;
    while (*length &&
           ((*value)[0] == ' ' || (*value)[0] == '\t')) {
        ++*value;
        --*length;
    }
    while (*length &&
           ((*value)[*length - 1U] == ' ' ||
            (*value)[*length - 1U] == '\t'))
        --*length;
}

static DlumaQuestContextToken dluma_context_unwrap(
    DlumaQuestContextToken token)
{
    if (token.length >= 4U && token.start[0] == '<' &&
        token.start[1] == '<' &&
        token.start[token.length - 2U] == '>' &&
        token.start[token.length - 1U] == '>') {
        token.start += 2;
        token.length -= 4U;
    } else if (token.length >= 2U && token.start[0] == '"' &&
               token.start[token.length - 1U] == '"') {
        ++token.start;
        token.length -= 2U;
    }
    return token;
}

static uint32_t dluma_context_tokenize_prefix(
    const char* source, uint32_t start, uint32_t end,
    DlumaQuestContextToken* tokens, uint32_t capacity)
{
    uint32_t index = start;
    uint32_t count = 0;
    while (index < end) {
        uint32_t token_start;
        unsigned int quoted = 0;
        unsigned int angle_depth = 0;
        while (index < end &&
               (source[index] == ' ' || source[index] == '\t'))
            ++index;
        if (index >= end) break;
        token_start = index;
        while (index < end) {
            char value = source[index];
            if (value == '"' && !angle_depth) quoted = !quoted;
            else if (!quoted && index + 1U < end &&
                     value == '<' && source[index + 1U] == '<') {
                ++angle_depth;
                index += 2U;
                continue;
            } else if (!quoted && angle_depth && index + 1U < end &&
                       value == '>' && source[index + 1U] == '>') {
                --angle_depth;
                index += 2U;
                continue;
            } else if (!quoted && !angle_depth &&
                       (value == ' ' || value == '\t')) {
                break;
            }
            ++index;
        }
        if (count < capacity) {
            tokens[count].start = source + token_start;
            tokens[count].length = index - token_start;
            ++count;
        }
    }
    return count;
}

void dluma_quest_context_initialize(DlumaQuestContextCore* context)
{
    unsigned char* bytes;
    uint32_t index;
    if (!context) return;
    bytes = (unsigned char*)context;
    for (index = 0; index < (uint32_t)sizeof(*context); ++index)
        bytes[index] = 0;
}

int dluma_quest_context_add_object(
    DlumaQuestContextCore* context,
    const char* name,
    const char* class_name,
    const char* role)
{
    uint32_t name_length;
    uint32_t class_length;
    uint32_t role_length;
    uint32_t index;
    DlumaQuestContextObjectCore* object = 0;
    int changed = 0;
    if (!context || !name || !class_name || !role) return 0;
    name_length = dluma_context_length_bounded(
        name, DLUMA_QUEST_CONTEXT_NAME_CAPACITY);
    class_length = dluma_context_length_bounded(
        class_name, DLUMA_QUEST_CONTEXT_CLASS_CAPACITY);
    role_length = dluma_context_length_bounded(
        role, DLUMA_QUEST_CONTEXT_ROLE_CAPACITY);
    if (!name_length || name_length >= DLUMA_QUEST_CONTEXT_NAME_CAPACITY ||
        class_length >= DLUMA_QUEST_CONTEXT_CLASS_CAPACITY ||
        !role_length || role_length >= DLUMA_QUEST_CONTEXT_ROLE_CAPACITY)
        return 0;
    for (index = 0; index < context->object_count; ++index) {
        if (dluma_context_equal_icase(
                context->objects[index].name, name) &&
            dluma_context_equal_icase(
                context->objects[index].class_name, class_name)) {
            object = &context->objects[index];
            break;
        }
    }
    if (!object) {
        if (context->object_count >= DLUMA_QUEST_CONTEXT_MAX_OBJECTS)
            return 0;
        object = &context->objects[context->object_count++];
        if (!dluma_context_copy(
                object->name, DLUMA_QUEST_CONTEXT_NAME_CAPACITY,
                name, name_length) ||
            (class_length && !dluma_context_copy(
                object->class_name, DLUMA_QUEST_CONTEXT_CLASS_CAPACITY,
                class_name, class_length))) {
            --context->object_count;
            return 0;
        }
        changed = 1;
    }
    for (index = 0; index < object->role_count; ++index)
        if (dluma_context_equal_icase(object->roles[index], role))
            return changed;
    if (object->role_count < DLUMA_QUEST_CONTEXT_MAX_ROLES &&
        dluma_context_copy(
            object->roles[object->role_count],
            DLUMA_QUEST_CONTEXT_ROLE_CAPACITY,
            role, role_length)) {
        ++object->role_count;
        changed = 1;
    }
    if (changed) ++context->generation;
    return changed;
}

int dluma_quest_context_capture_autocomplete(
    DlumaQuestContextCore* context,
    const char* source,
    uint32_t source_size,
    uint32_t caret_offset,
    const char* candidates,
    char separator)
{
    DlumaQuestContextToken tokens[24];
    DlumaQuestContextToken macro;
    DlumaQuestContextToken class_token = { 0, 0 };
    uint32_t line_start;
    uint32_t token_count;
    uint32_t argument_index;
    uint32_t candidate_start = 0;
    uint32_t candidate_size;
    char class_name[DLUMA_QUEST_CONTEXT_CLASS_CAPACITY];
    char macro_name[64];
    char role[DLUMA_QUEST_CONTEXT_ROLE_CAPACITY];
    char positional_role[DLUMA_QUEST_CONTEXT_ROLE_CAPACITY];
    const char* category = "completion";
    int changed = 0;
    if (!context || !source || caret_offset > source_size ||
        !candidates || !candidates[0])
        return 0;
    line_start = caret_offset;
    while (line_start && source[line_start - 1U] != '\n' &&
           source[line_start - 1U] != '\r')
        --line_start;
    token_count = dluma_context_tokenize_prefix(
        source, line_start, caret_offset,
        tokens, (uint32_t)(sizeof(tokens) / sizeof(tokens[0])));
    if (!token_count || !tokens[0].length || tokens[0].start[0] != ',')
        return 0;
    macro = tokens[0];
    ++macro.start;
    --macro.length;
    if (!dluma_context_copy(
            macro_name, (uint32_t)sizeof(macro_name),
            macro.start, macro.length))
        return 0;
    argument_index = token_count - 1U;
    if (caret_offset && source[caret_offset - 1U] != ' ' &&
        source[caret_offset - 1U] != '\t' && argument_index)
        --argument_index;
    if (token_count > 2U) class_token = dluma_context_unwrap(tokens[2]);
    class_name[0] = 0;
    if (class_token.length)
        (void)dluma_context_copy(
            class_name, (uint32_t)sizeof(class_name),
            class_token.start, class_token.length);
    if (dluma_context_equal_icase(macro_name, "interactive-movie")) {
        if (argument_index == 1U) category = "class";
        else if (argument_index == 2U) category = "scenario";
    } else if (dluma_context_equal_icase(macro_name, "take")) {
        if (argument_index == 1U) category = "class";
        else if (argument_index == 2U) category = "object";
    }
    if (dluma_context_equal_icase(category, "completion"))
        return 0;
    {
        const char prefix[] = "pxsl:";
        uint32_t used = 0;
        uint32_t index;
        for (index = 0; index < sizeof(prefix) - 1U; ++index)
            positional_role[used++] = prefix[index];
        for (index = 0; macro_name[index] &&
             used + 1U < sizeof(positional_role); ++index)
            positional_role[used++] = macro_name[index];
        if (used + 3U >= sizeof(positional_role)) return 0;
        positional_role[used++] = ':';
        if (argument_index >= 10U)
            positional_role[used++] = (char)('0' +
                ((argument_index / 10U) % 10U));
        positional_role[used++] = (char)('0' + (argument_index % 10U));
        positional_role[used] = 0;
    }
    {
        uint32_t category_length = dluma_context_length_bounded(category, 32U);
        uint32_t index;
        for (index = 0; index < category_length; ++index)
            role[index] = category[index];
        role[category_length] = 0;
    }
    candidate_size = dluma_context_length_bounded(candidates, 65536U);
    if (!separator) separator = ' ';
    while (candidate_start < candidate_size) {
        uint32_t candidate_end = candidate_start;
        const char* value;
        uint32_t length;
        char candidate[DLUMA_QUEST_CONTEXT_NAME_CAPACITY];
        const char* object_class = class_name;
        while (candidate_end < candidate_size &&
               candidates[candidate_end] != separator &&
               candidates[candidate_end] != '\r' &&
               candidates[candidate_end] != '\n')
            ++candidate_end;
        value = candidates + candidate_start;
        length = candidate_end - candidate_start;
        dluma_context_trim(&value, &length);
        if (length > 2U && value[length - 2U] == '?' &&
            value[length - 1U] >= '0' && value[length - 1U] <= '9')
            length -= 2U;
        if (length && dluma_context_copy(
                candidate, (uint32_t)sizeof(candidate), value, length)) {
            if (dluma_context_equal_icase(category, "class"))
                object_class = candidate;
            if (dluma_quest_context_add_object(
                    context, candidate, object_class, role))
                changed = 1;
            if (dluma_quest_context_add_object(
                    context, candidate, object_class, positional_role))
                changed = 1;
        }
        candidate_start = candidate_end + 1U;
    }
    return changed;
}
