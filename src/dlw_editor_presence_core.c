#include "dlw_editor_presence_core.h"

#include <string.h>

static unsigned int dluma_editor_presence_bounded_length(
    const char* value, unsigned int capacity)
{
    unsigned int length = 0;
    if (!value) return capacity;
    while (length < capacity && value[length]) ++length;
    return length;
}

static void dluma_editor_presence_copy(
    char* output, unsigned int capacity, const char* value)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    if (!value) {
        output[0] = 0;
        return;
    }
    while (index + 1U < capacity && value[index]) {
        output[index] = value[index];
        ++index;
    }
    output[index] = 0;
}

static char dluma_editor_presence_lower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? (char)(value + ('a' - 'A')) : value;
}

static int dluma_editor_presence_equal_ci(
    const char* value, unsigned int length, const char* expected)
{
    unsigned int index = 0;
    while (expected[index]) ++index;
    if (index != length) return 0;
    for (index = 0; index < length; ++index)
        if (dluma_editor_presence_lower(value[index]) !=
            dluma_editor_presence_lower(expected[index]))
            return 0;
    return 1;
}

static unsigned int dluma_editor_presence_find_product_marker(
    const char* value, unsigned int length)
{
    static const char marker[] = " - Dying Light Developer Tools";
    unsigned int marker_length = sizeof(marker) - 1U;
    unsigned int offset;
    unsigned int index;
    if (length < marker_length) return length;
    for (offset = 0; offset + marker_length <= length; ++offset) {
        for (index = 0; index < marker_length; ++index)
            if (dluma_editor_presence_lower(value[offset + index]) !=
                dluma_editor_presence_lower(marker[index]))
                break;
        if (index == marker_length) return offset;
    }
    return length;
}

static unsigned long long dluma_editor_presence_hash(
    unsigned int kind, const char* value)
{
    unsigned long long hash = 1469598103934665603ULL;
    unsigned int index = 0;
    hash ^= kind;
    hash *= 1099511628211ULL;
    while (value && value[index]) {
        hash ^= (unsigned char)value[index++];
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1ULL;
}

static const char* dluma_editor_presence_details(unsigned int kind)
{
    if (kind == DLUMA_EDITOR_DOCUMENT_MAP) return "Editing Map";
    if (kind == DLUMA_EDITOR_DOCUMENT_QUEST) return "Editing Quest";
    if (kind == DLUMA_EDITOR_DOCUMENT_SCRIPT) return "Editing Script";
    return "Editing Document";
}

void dluma_editor_presence_build_browsing(
    DlumaEditorPresenceObservation* output)
{
    if (!output) return;
    memset(output, 0, sizeof(*output));
    output->snapshot.struct_size = sizeof(output->snapshot);
    output->snapshot.context = DLUMA_PRESENCE_CONTEXT_EDITOR;
    dluma_editor_presence_copy(
        output->snapshot.primary_text,
        sizeof(output->snapshot.primary_text),
        "Browsing Developer Tools");
    output->document_identity = dluma_editor_presence_hash(
        DLUMA_EDITOR_DOCUMENT_NONE, output->snapshot.primary_text);
}

int dluma_editor_presence_parse_title_utf8(
    const char* title,
    unsigned int title_capacity,
    DlumaEditorPresenceObservation* output)
{
    char raw_name[DLUMA_EDITOR_PRESENCE_TITLE_CAPACITY];
    unsigned int length;
    unsigned int leaf = 0;
    unsigned int dot;
    unsigned int end;
    unsigned int extension_length;
    unsigned int name_length;
    unsigned int index;
    unsigned int kind = DLUMA_EDITOR_DOCUMENT_GENERIC;
    if (!output) return 0;
    dluma_editor_presence_build_browsing(output);
    if (!title || !title_capacity ||
        title_capacity > DLUMA_EDITOR_PRESENCE_TITLE_CAPACITY)
        return 0;
    length = dluma_editor_presence_bounded_length(title, title_capacity);
    if (!length || length >= title_capacity) return 0;
    end = dluma_editor_presence_find_product_marker(title, length);
    while (end && (title[end - 1U] == ' ' ||
                   title[end - 1U] == '*'))
        --end;
    if (!end) return 0;
    for (index = 0; index < end; ++index)
        if (title[index] == '/' || title[index] == '\\') leaf = index + 1U;
    dot = end;
    while (dot > leaf && title[dot - 1U] != '.') --dot;
    if (dot <= leaf || title[dot - 1U] != '.') return 0;
    --dot;
    extension_length = end - dot;
    if (extension_length < 2U || extension_length > 16U) return 0;
    for (index = dot + 1U; index < end; ++index) {
        unsigned char value = (unsigned char)title[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_'))
            return 0;
    }
    if (dluma_editor_presence_equal_ci(
            title + dot, extension_length, ".map"))
        kind = DLUMA_EDITOR_DOCUMENT_MAP;
    else if (dluma_editor_presence_equal_ci(
                 title + dot, extension_length, ".pxsl"))
        kind = DLUMA_EDITOR_DOCUMENT_QUEST;
    else if (dluma_editor_presence_equal_ci(
                 title + dot, extension_length, ".scr"))
        kind = DLUMA_EDITOR_DOCUMENT_SCRIPT;
    name_length = dot - leaf;
    while (name_length && title[leaf + name_length - 1U] == ' ')
        --name_length;
    if (!name_length || name_length >= sizeof(raw_name)) return 0;
    memcpy(raw_name, title + leaf, name_length);
    raw_name[name_length] = 0;
    memset(output, 0, sizeof(*output));
    output->snapshot.struct_size = sizeof(output->snapshot);
    output->snapshot.context = DLUMA_PRESENCE_CONTEXT_EDITOR;
    dluma_editor_presence_copy(
        output->snapshot.primary_text,
        sizeof(output->snapshot.primary_text),
        dluma_editor_presence_details(kind));
    if (!dluma_presence_sanitize_text(
            raw_name, output->snapshot.secondary_text,
            sizeof(output->snapshot.secondary_text))) {
        dluma_editor_presence_build_browsing(output);
        return 0;
    }
    output->document_kind = kind;
    output->has_document = 1U;
    output->document_identity = dluma_editor_presence_hash(
        kind, output->snapshot.secondary_text);
    return 1;
}

int dluma_editor_presence_observation_equal(
    const DlumaEditorPresenceObservation* left,
    const DlumaEditorPresenceObservation* right)
{
    if (!left || !right) return left == right;
    return left->document_identity == right->document_identity &&
           left->document_kind == right->document_kind &&
           left->has_document == right->has_document &&
           dluma_presence_snapshot_equal(
               &left->snapshot, &right->snapshot);
}

int dluma_editor_options_model_admit(
    DlumaEditorOptionsModel* model,
    int exact_build,
    int exact_dialog,
    int exact_notifications,
    unsigned int stock_tab_count,
    unsigned int stock_page_count,
    int already_inserted,
    int enabled)
{
    if (!model) return 0;
    memset(model, 0, sizeof(*model));
    model->selected_index = -1;
    if (!exact_build || !exact_dialog || !exact_notifications ||
        stock_tab_count != 4U || stock_page_count != 4U ||
        already_inserted)
        return 0;
    model->admitted = 1U;
    model->inserted = 1U;
    model->enabled = enabled ? 1U : 0U;
    model->selected_index = 0;
    return 1;
}

DlumaEditorOptionsAction dluma_editor_options_model_select(
    DlumaEditorOptionsModel* model,
    unsigned int total_tab_count,
    int selected_index)
{
    if (!model || !model->admitted || !model->inserted ||
        total_tab_count != 5U || selected_index < 0 ||
        selected_index >= 5)
        return DLUMA_EDITOR_OPTIONS_REFUSE;
    model->selected_index = selected_index;
    return selected_index == 4
        ? DLUMA_EDITOR_OPTIONS_SHOW_CUSTOM
        : DLUMA_EDITOR_OPTIONS_FORWARD_STOCK;
}

DlumaEditorOptionsAction dluma_editor_options_model_toggle(
    DlumaEditorOptionsModel* model,
    int enabled)
{
    if (!model || !model->admitted || !model->inserted)
        return DLUMA_EDITOR_OPTIONS_REFUSE;
    model->enabled = enabled ? 1U : 0U;
    return DLUMA_EDITOR_OPTIONS_PERSIST_TOGGLE;
}

DlumaEditorOptionsAction dluma_editor_options_model_reset(
    DlumaEditorOptionsModel* model)
{
    if (!model || !model->admitted || !model->inserted)
        return DLUMA_EDITOR_OPTIONS_REFUSE;
    model->enabled = 1U;
    return DLUMA_EDITOR_OPTIONS_PERSIST_RESET;
}

DlumaEditorOptionsAction dluma_editor_options_model_teardown(
    DlumaEditorOptionsModel* model)
{
    if (!model || !model->admitted || !model->inserted)
        return DLUMA_EDITOR_OPTIONS_REFUSE;
    memset(model, 0, sizeof(*model));
    model->selected_index = -1;
    return DLUMA_EDITOR_OPTIONS_TEARDOWN;
}

int dluma_editor_playtest_ownership_begin(
    DlumaEditorPlaytestOwnershipState* state,
    const DlumaEditorPlaytestOwnershipEvidence* evidence,
    unsigned long long generation)
{
    if (!state || !evidence || !generation || state->active ||
        generation <= state->generation ||
        !evidence->pid_match || !evidence->start_time_match ||
        !evidence->executable_match || !evidence->loader_match ||
        !evidence->handoff_match || !evidence->unexpired)
        return 0;
    state->generation = generation;
    state->active = 1U;
    return 1;
}

int dluma_editor_playtest_ownership_finish(
    DlumaEditorPlaytestOwnershipState* state,
    unsigned long long generation)
{
    if (!state || !state->active ||
        generation != state->generation)
        return 0;
    state->active = 0U;
    return 1;
}
