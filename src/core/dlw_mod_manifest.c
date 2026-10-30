#include "dlw_mod_manifest.h"
#include "dlw_json_reader.h"
#include "../generated/dluma_api_catalog.generated.h"

#include <string.h>

#define DLUMA_MANIFEST_REQUIRED_MASK 0x00000FFFUL
#define DLUMA_API_IMMEDIATELY_PREVIOUS_CATALOG_SHA256 \
    "D49EDB1807E5E3B0109AF54D049D95E07072F499F480DD878F32AEE810DA220E"
#define DLUMA_API_COMPATIBLE_SCOPE_CATALOG_SHA256 \
    "84202AF55B7177081551F21DC0C1F17899000B6FD8D6A63B94BC38A6839701F0"

static int dluma_manifest_compare(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right ? 0 : left ? 1 : -1;
    while (left[index] && left[index] == right[index]) ++index;
    return (unsigned char)left[index] - (unsigned char)right[index];
}

static const char* dluma_manifest_find_char(const char* value, char wanted)
{
    if (!value) return 0;
    while (*value) {
        if (*value == wanted) return value;
        ++value;
    }
    return wanted ? 0 : value;
}

static const char* dluma_manifest_find_last_char(
    const char* value,
    char wanted)
{
    const char* found = 0;
    if (!value) return 0;
    do {
        if (*value == wanted) found = value;
    } while (*value++);
    return found;
}

#define strcmp dluma_manifest_compare
#define strchr dluma_manifest_find_char
#define strrchr dluma_manifest_find_last_char

static void dluma_manifest_copy(
    char* output,
    unsigned int capacity,
    const char* value)
{
    unsigned int index = 0;
    if (!output || !capacity) return;
    while (value && value[index] && index + 1U < capacity) {
        output[index] = value[index];
        ++index;
    }
    output[index] = 0;
}

static void dluma_manifest_diagnostic(
    DlumaModManifestDiagnostic* diagnostic,
    DlumaStatus status,
    unsigned int offset,
    const char* message)
{
    if (!diagnostic) return;
    diagnostic->status = status;
    diagnostic->byte_offset = offset;
    dluma_manifest_copy(
        diagnostic->message,
        (unsigned int)sizeof(diagnostic->message),
        message);
}

static int dluma_manifest_key_is(const char* value, const char* expected)
{
    return value && expected && strcmp(value, expected) == 0;
}

static int dluma_manifest_mark(
    uint64_t* fields,
    unsigned int bit,
    DlumaJsonReader* reader)
{
    uint64_t mask = 1ULL << bit;
    if (!fields || *fields & mask) {
        if (reader && reader->status == DLUMA_STATUS_OK)
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
        return 0;
    }
    *fields |= mask;
    return 1;
}

static int dluma_manifest_lower_id(
    const char* value,
    unsigned int minimum,
    unsigned int maximum)
{
    unsigned int index = 0;
    if (!value) return 0;
    while (value[index]) {
        char c = value[index];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return 0;
        ++index;
    }
    return index >= minimum && index <= maximum &&
        value[0] != '.' && value[0] != '_' && value[0] != '-' &&
        value[index - 1U] != '.' && value[index - 1U] != '_' &&
        value[index - 1U] != '-';
}

static int dluma_manifest_semver(const char* value)
{
    unsigned int part;
    const char* cursor = value;
    if (!cursor || !*cursor) return 0;
    for (part = 0; part < 3U; ++part) {
        if (*cursor < '0' || *cursor > '9') return 0;
        if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9')
            return 0;
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        if (part < 2U) {
            if (*cursor++ != '.') return 0;
        }
    }
    if (*cursor == '-' || *cursor == '+') {
        ++cursor;
        if (!*cursor) return 0;
        while (*cursor) {
            char c = *cursor++;
            if (!((c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '.' || c == '-'))
                return 0;
        }
    }
    return *cursor == 0;
}

static int dluma_manifest_sha256(const char* value)
{
    unsigned int index;
    if (!value) return 0;
    for (index = 0; index < 64U; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return value[64] == 0;
}

static int dluma_manifest_ascii_equal_fold(
    const char* value,
    unsigned int length,
    const char* expected)
{
    unsigned int index;
    for (index = 0; index < length && expected[index]; ++index) {
        char left = value[index];
        char right = expected[index];
        if (left >= 'a' && left <= 'z') left = (char)(left - 32);
        if (right >= 'a' && right <= 'z') right = (char)(right - 32);
        if (left != right) return 0;
    }
    return index == length && expected[index] == 0;
}

static int dluma_manifest_device_name(
    const char* segment,
    unsigned int length)
{
    unsigned int stem = 0;
    while (stem < length && segment[stem] != '.') ++stem;
    if (dluma_manifest_ascii_equal_fold(segment, stem, "CON") ||
        dluma_manifest_ascii_equal_fold(segment, stem, "PRN") ||
        dluma_manifest_ascii_equal_fold(segment, stem, "AUX") ||
        dluma_manifest_ascii_equal_fold(segment, stem, "NUL"))
        return 1;
    if (stem == 4U &&
        (dluma_manifest_ascii_equal_fold(segment, 3U, "COM") ||
         dluma_manifest_ascii_equal_fold(segment, 3U, "LPT")) &&
        segment[3] >= '1' && segment[3] <= '9')
        return 1;
    return 0;
}

int dluma_mod_relative_path_validate_and_normalize(
    const char* value,
    char* output,
    unsigned int output_capacity)
{
    unsigned int source = 0;
    unsigned int target = 0;
    unsigned int segment_start = 0;
    if (!value || !value[0] || !output || output_capacity < 2U)
        return 0;
    if (value[0] == '/' || value[0] == '\\' ||
        ((value[0] >= 'A' && value[0] <= 'Z') ||
         (value[0] >= 'a' && value[0] <= 'z')) &&
            value[1] == ':')
        return 0;
    for (;;) {
        char c = value[source++];
        int separator = c == '/' || c == '\\' || c == 0;
        if (separator) {
            unsigned int length = target - segment_start;
            if (!length ||
                (length == 1U && output[segment_start] == '.') ||
                (length == 2U && output[segment_start] == '.' &&
                 output[segment_start + 1U] == '.') ||
                output[target - 1U] == '.' ||
                output[target - 1U] == ' ' ||
                dluma_manifest_device_name(
                    output + segment_start, length))
                return 0;
            if (!c) break;
            if (target + 1U >= output_capacity) return 0;
            output[target++] = '/';
            segment_start = target;
            continue;
        }
        if ((unsigned char)c < 0x20U ||
            c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '|' || c == '?' || c == '*')
            return 0;
        if (target + 1U >= output_capacity) return 0;
        output[target++] = c;
    }
    output[target] = 0;
    return target <= 511U;
}

static int dluma_manifest_map_name(const char* value)
{
    char normalized[128];
    unsigned int length;
    if (!value || !value[0] ||
        !dluma_mod_relative_path_validate_and_normalize(
            value, normalized, (unsigned int)sizeof(normalized)) ||
        strchr(normalized, '/'))
        return 0;
    length = (unsigned int)strlen(normalized);
    return length && length <= 127U;
}

static unsigned int dluma_manifest_access_id(const char* name)
{
    unsigned int index;
    for (index = 0; index < DLUMA_GENERATED_ACCESS_NAME_COUNT; ++index)
        if (strcmp(g_dluma_generated_access_names[index].name, name) == 0)
            return g_dluma_generated_access_names[index].id;
    return 0;
}

int dluma_mod_manifest_has_access(
    const DlumaModManifest* manifest,
    unsigned int access_id)
{
    unsigned int index;
    if (!manifest || !access_id) return 0;
    for (index = 0; index < manifest->access_count; ++index)
        if (manifest->access_ids[index] == access_id) return 1;
    return 0;
}

int dluma_mod_manifest_declares_quest_phase_handler(
    const DlumaModManifest* manifest,
    const char* handler_id)
{
    unsigned int index;
    if (!manifest || !handler_id || !handler_id[0]) return 0;
    for (index = 0; index < manifest->quest_phase_handler_count; ++index)
        if (strcmp(
                manifest->quest_phase_handlers[index].id,
                handler_id) == 0)
            return 1;
    return 0;
}

static int dluma_manifest_parse_access(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    int first = 1;
    if (!dluma_json_array_begin(reader)) return 0;
    for (;;) {
        char name[96];
        unsigned int id;
        unsigned int index;
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (output->access_count >= DLUMA_MOD_MANIFEST_MAX_ACCESS) {
            reader->status = DLUMA_STATUS_LIMIT_EXCEEDED;
            return 0;
        }
        if (!dluma_json_string(reader, name, (unsigned int)sizeof(name)))
            return 0;
        id = dluma_manifest_access_id(name);
        if (!id) {
            reader->status = DLUMA_STATUS_ACCESS_DENIED;
            return 0;
        }
        for (index = 0; index < output->access_count; ++index)
            if (output->access_ids[index] == id) {
                reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
                return 0;
            }
        output->access_ids[output->access_count++] = id;
    }
    return dluma_json_array_end(reader);
}

static int dluma_manifest_parse_hosts(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    int first = 1;
    unsigned int count = 0;
    if (!dluma_json_array_begin(reader)) return 0;
    output->host_flags = 0U;
    for (;;) {
        char name[16];
        unsigned int flag;
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (count >= 2U ||
            !dluma_json_string(reader, name, (unsigned int)sizeof(name)))
            return 0;
        flag = strcmp(name, "player") == 0 ? DLUMA_MOD_HOST_PLAYER :
               strcmp(name, "editor") == 0 ? DLUMA_MOD_HOST_EDITOR : 0U;
        if (!flag || (output->host_flags & flag)) {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
        output->host_flags |= flag;
        ++count;
    }
    return dluma_json_array_end(reader) && count != 0U;
}

static int dluma_manifest_ascii_fold_equal(
    const char* left,
    const char* right)
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

static int dluma_manifest_command_alias(const char* value)
{
    unsigned int index = 0;
    if (!value || !value[0]) return 0;
    while (value[index]) {
        char c = value[index];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
        ++index;
    }
    return index <= 63U;
}

static int dluma_manifest_parse_quest_phase_handler(
    DlumaJsonReader* reader,
    DlumaModQuestPhaseHandlerDeclaration* output)
{
    uint64_t fields = 0;
    int first = 1;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_manifest_key_is(key, "id")) {
            if (!dluma_manifest_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader, output->id,
                    (unsigned int)sizeof(output->id)) ||
                !dluma_manifest_lower_id(output->id, 1U, 127U))
                return 0;
        } else if (dluma_manifest_key_is(key, "command_alias")) {
            if (!dluma_manifest_mark(&fields, 1U, reader) ||
                !dluma_json_string(
                    reader, output->command_alias,
                    (unsigned int)sizeof(output->command_alias)) ||
                !dluma_manifest_command_alias(output->command_alias))
                return 0;
        } else if (dluma_manifest_key_is(key, "description")) {
            if (!dluma_manifest_mark(&fields, 2U, reader) ||
                !dluma_json_string(
                    reader, output->description,
                    (unsigned int)sizeof(output->description)))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) &&
        (fields & 5ULL) == 5ULL;
}

static int dluma_manifest_parse_quest_phase_handlers(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    int first = 1;
    if (!dluma_json_array_begin(reader)) return 0;
    for (;;) {
        unsigned int index;
        DlumaModQuestPhaseHandlerDeclaration* item;
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (output->quest_phase_handler_count >=
                DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS) {
            reader->status = DLUMA_STATUS_LIMIT_EXCEEDED;
            return 0;
        }
        item = &output->quest_phase_handlers[
            output->quest_phase_handler_count];
        if (!dluma_manifest_parse_quest_phase_handler(reader, item))
            return 0;
        for (index = 0; index < output->quest_phase_handler_count; ++index) {
            const DlumaModQuestPhaseHandlerDeclaration* prior =
                &output->quest_phase_handlers[index];
            if (strcmp(prior->id, item->id) == 0 ||
                (prior->command_alias[0] && item->command_alias[0] &&
                 dluma_manifest_ascii_fold_equal(
                     prior->command_alias, item->command_alias))) {
                reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
                return 0;
            }
        }
        ++output->quest_phase_handler_count;
    }
    return dluma_json_array_end(reader);
}

static int dluma_manifest_parse_configuration(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    uint64_t fields = 0;
    int first = 1;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        char key[32];
        char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':') ||
            !dluma_json_string(reader, path, (unsigned int)sizeof(path)))
            return 0;
        if (dluma_manifest_key_is(key, "schema")) {
            if (!dluma_manifest_mark(&fields, 0U, reader) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    path, output->configuration_schema,
                    (unsigned int)sizeof(output->configuration_schema)))
                return 0;
        } else if (dluma_manifest_key_is(key, "defaults")) {
            if (!dluma_manifest_mark(&fields, 1U, reader) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    path, output->configuration_defaults,
                    (unsigned int)sizeof(output->configuration_defaults)))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) && (fields & 1ULL);
}

static int dluma_manifest_parse_companion_item(
    DlumaJsonReader* reader,
    DlumaModCompanionDeclaration* output)
{
    uint64_t fields = 0;
    int first = 1;
    output->required = 1U;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_manifest_key_is(key, "id")) {
            if (!dluma_manifest_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader, output->id,
                    (unsigned int)sizeof(output->id)) ||
                !dluma_manifest_lower_id(output->id, 1U, 63U))
                return 0;
        } else if (dluma_manifest_key_is(key, "path")) {
            char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
            if (!dluma_manifest_mark(&fields, 1U, reader) ||
                !dluma_json_string(
                    reader, path, (unsigned int)sizeof(path)) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    path, output->path,
                    (unsigned int)sizeof(output->path)))
                return 0;
        } else if (dluma_manifest_key_is(key, "required")) {
            int required;
            if (!dluma_manifest_mark(&fields, 2U, reader) ||
                !dluma_json_boolean(reader, &required))
                return 0;
            output->required = required ? 1U : 0U;
        } else if (dluma_manifest_key_is(key, "audio_manifest")) {
            char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
            if (!dluma_manifest_mark(&fields, 3U, reader) ||
                !dluma_json_string(
                    reader, path, (unsigned int)sizeof(path)) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    path, output->audio_manifest,
                    (unsigned int)sizeof(output->audio_manifest)))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) && (fields & 3ULL) == 3ULL;
}

static int dluma_manifest_parse_companion(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    int first = 1;
    if (!dluma_json_array_begin(reader)) return 0;
    for (;;) {
        unsigned int index;
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (output->companion_count >=
                DLUMA_MOD_MANIFEST_MAX_COMPANION) {
            reader->status = DLUMA_STATUS_LIMIT_EXCEEDED;
            return 0;
        }
        if (!dluma_manifest_parse_companion_item(
                reader,
                &output->companion[output->companion_count]))
            return 0;
        for (index = 0; index < output->companion_count; ++index)
            if (strcmp(
                    output->companion[index].id,
                    output->companion[output->companion_count].id) == 0) {
                reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
                return 0;
            }
        ++output->companion_count;
    }
    return dluma_json_array_end(reader);
}

static int dluma_manifest_parse_migration(
    DlumaJsonReader* reader,
    DlumaModManifest* output)
{
    uint64_t fields = 0;
    int first = 1;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_manifest_key_is(key, "legacy_id")) {
            if (!dluma_manifest_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader, output->legacy_storage_id,
                    (unsigned int)sizeof(output->legacy_storage_id)) ||
                !dluma_manifest_lower_id(
                    output->legacy_storage_id, 3U, 127U))
                return 0;
        } else if (dluma_manifest_key_is(key, "legacy_fingerprint")) {
            if (!dluma_manifest_mark(&fields, 1U, reader) ||
                !dluma_json_string(
                    reader, output->legacy_storage_fingerprint,
                    (unsigned int)sizeof(
                        output->legacy_storage_fingerprint)) ||
                !dluma_manifest_sha256(
                    output->legacy_storage_fingerprint))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) && (fields & 1ULL);
}

static int dluma_manifest_entry_extension(const DlumaModManifest* manifest)
{
    const char* dot;
    if (!manifest) return 0;
    dot = strrchr(manifest->entry, '.');
    if (!dot) return 0;
    if (manifest->language == DLUMA_MOD_LANGUAGE_LUA)
        return strcmp(dot, ".lua") == 0;
    if (manifest->language == DLUMA_MOD_LANGUAGE_CSHARP)
        return strcmp(dot, ".dll") == 0;
    if (manifest->language == DLUMA_MOD_LANGUAGE_NATIVE)
        return strcmp(dot, ".mod") == 0 || strcmp(dot, ".dll") == 0;
    return 0;
}

static int dluma_manifest_validate(DlumaModManifest* output)
{
    unsigned int phase_handle_access;
    if (!output ||
        !dluma_manifest_lower_id(output->id, 3U, 127U) ||
        !output->name[0] || !output->author[0] ||
        !dluma_manifest_semver(output->version) ||
        output->load_priority < -1000 ||
        output->load_priority > 1000 ||
        !dluma_manifest_entry_extension(output) || !output->host_flags)
        return 0;
    if ((output->host_flags & DLUMA_MOD_HOST_EDITOR) &&
        output->language != DLUMA_MOD_LANGUAGE_NATIVE)
        return 0;
    if (output->quest_phase_handler_count) {
        phase_handle_access =
            dluma_manifest_access_id("quest.phase.handle");
        if (output->language == DLUMA_MOD_LANGUAGE_CSHARP ||
            !(output->host_flags & DLUMA_MOD_HOST_PLAYER) ||
            !dluma_mod_manifest_has_access(output, phase_handle_access))
            return 0;
    }
    if (output->scope == DLUMA_SCOPE_MAP) {
        if (!dluma_manifest_map_name(output->map)) return 0;
    } else if (output->map[0]) {
        return 0;
    }
    if (output->language == DLUMA_MOD_LANGUAGE_CSHARP) {
        const char* cursor = output->entry_type;
        if (!*cursor ||
            !( (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= 'a' && *cursor <= 'z') ||
               *cursor == '_'))
            return 0;
        while (*++cursor)
            if (!( (*cursor >= 'A' && *cursor <= 'Z') ||
                   (*cursor >= 'a' && *cursor <= 'z') ||
                   (*cursor >= '0' && *cursor <= '9') ||
                   *cursor == '_' || *cursor == '.' ||
                   *cursor == '+' || *cursor == '`'))
                return 0;
    } else if (output->entry_type[0]) {
        return 0;
    }
    return 1;
}

DlumaStatus dluma_mod_manifest_parse(
    const void* bytes,
    unsigned int size,
    DlumaModManifest* output,
    DlumaModManifestDiagnostic* diagnostic)
{
    DlumaJsonReader reader;
    uint64_t fields = 0;
    int first = 1;
    if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
    if (!bytes || !size || size > DLUMA_MOD_MANIFEST_MAX_BYTES ||
        !output || output->struct_size < sizeof(*output)) {
        dluma_manifest_diagnostic(
            diagnostic, size > DLUMA_MOD_MANIFEST_MAX_BYTES
                ? DLUMA_STATUS_LIMIT_EXCEEDED
                : DLUMA_STATUS_INVALID_ARGUMENT,
            0U, "invalid manifest buffer or output record");
        return diagnostic ? diagnostic->status : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    output->struct_size = (unsigned int)sizeof(*output);
    output->struct_version = DLUMA_MOD_MANIFEST_VERSION;
    output->host_flags = DLUMA_MOD_HOST_PLAYER;
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader)) goto invalid;
    for (;;) {
        char key[64];
        int next = dluma_json_object_next(&reader, &first);
        if (next < 0) goto invalid;
        if (!next) break;
        if (!dluma_json_string(&reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(&reader, ':'))
            goto invalid;
        if (dluma_manifest_key_is(key, "$schema")) {
            char ignored[512];
            if (!dluma_manifest_mark(&fields, 31U, &reader) ||
                !dluma_json_string(
                    &reader, ignored, (unsigned int)sizeof(ignored)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "schema")) {
            char value[32];
            if (!dluma_manifest_mark(&fields, 0U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                strcmp(value, "dluma.mod.v1") != 0)
                goto invalid;
        } else if (dluma_manifest_key_is(key, "api_version")) {
            char value[16];
            if (!dluma_manifest_mark(&fields, 1U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                strcmp(value, DLUMA_API_PUBLIC_VERSION) != 0)
                goto invalid;
        } else if (dluma_manifest_key_is(key, "id")) {
            if (!dluma_manifest_mark(&fields, 2U, &reader) ||
                !dluma_json_string(
                    &reader, output->id,
                    (unsigned int)sizeof(output->id)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "name")) {
            if (!dluma_manifest_mark(&fields, 3U, &reader) ||
                !dluma_json_string(
                    &reader, output->name,
                    (unsigned int)sizeof(output->name)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "version")) {
            if (!dluma_manifest_mark(&fields, 4U, &reader) ||
                !dluma_json_string(
                    &reader, output->version,
                    (unsigned int)sizeof(output->version)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "description")) {
            if (!dluma_manifest_mark(&fields, 5U, &reader) ||
                !dluma_json_string(
                    &reader, output->description,
                    (unsigned int)sizeof(output->description)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "author")) {
            if (!dluma_manifest_mark(&fields, 6U, &reader) ||
                !dluma_json_string(
                    &reader, output->author,
                    (unsigned int)sizeof(output->author)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "language")) {
            char value[16];
            if (!dluma_manifest_mark(&fields, 7U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)))
                goto invalid;
            output->language =
                strcmp(value, "lua") == 0 ? DLUMA_MOD_LANGUAGE_LUA :
                strcmp(value, "csharp") == 0 ? DLUMA_MOD_LANGUAGE_CSHARP :
                strcmp(value, "native") == 0 ? DLUMA_MOD_LANGUAGE_NATIVE : 0U;
            if (!output->language) goto invalid;
        } else if (dluma_manifest_key_is(key, "scope")) {
            char value[16];
            if (!dluma_manifest_mark(&fields, 8U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)))
                goto invalid;
            output->scope =
                strcmp(value, "global") == 0 ? DLUMA_SCOPE_GLOBAL :
                strcmp(value, "project") == 0 ? DLUMA_SCOPE_PROJECT :
                strcmp(value, "map") == 0 ? DLUMA_SCOPE_MAP : 0U;
            if (!output->scope) goto invalid;
        } else if (dluma_manifest_key_is(key, "map")) {
            if (!dluma_manifest_mark(&fields, 12U, &reader) ||
                !dluma_json_string(
                    &reader, output->map,
                    (unsigned int)sizeof(output->map)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "entry")) {
            char value[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
            if (!dluma_manifest_mark(&fields, 9U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    value, output->entry,
                    (unsigned int)sizeof(output->entry)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "entry_type")) {
            if (!dluma_manifest_mark(&fields, 13U, &reader) ||
                !dluma_json_string(
                    &reader, output->entry_type,
                    (unsigned int)sizeof(output->entry_type)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "load_priority")) {
            if (!dluma_manifest_mark(&fields, 10U, &reader) ||
                !dluma_json_i32(&reader, &output->load_priority))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "access")) {
            if (!dluma_manifest_mark(&fields, 11U, &reader) ||
                !dluma_manifest_parse_access(&reader, output))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "hosts")) {
            if (!dluma_manifest_mark(&fields, 17U, &reader) ||
                !dluma_manifest_parse_hosts(&reader, output))
                goto invalid;
            output->hosts_explicit = 1U;
        } else if (dluma_manifest_key_is(
                       key, "quest_phase_handlers")) {
            if (!dluma_manifest_mark(&fields, 18U, &reader) ||
                !dluma_manifest_parse_quest_phase_handlers(
                    &reader, output))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "configuration")) {
            if (!dluma_manifest_mark(&fields, 14U, &reader) ||
                !dluma_manifest_parse_configuration(&reader, output))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "companion_content")) {
            if (!dluma_manifest_mark(&fields, 15U, &reader) ||
                !dluma_manifest_parse_companion(&reader, output))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "storage_migration")) {
            if (!dluma_manifest_mark(&fields, 16U, &reader) ||
                !dluma_manifest_parse_migration(&reader, output))
                goto invalid;
        } else {
            reader.status = DLUMA_STATUS_INVALID_ARGUMENT;
            goto invalid;
        }
    }
    if (!dluma_json_object_end(&reader) ||
        !dluma_json_finished(&reader) ||
        (fields & DLUMA_MANIFEST_REQUIRED_MASK) !=
            DLUMA_MANIFEST_REQUIRED_MASK ||
        !dluma_manifest_validate(output))
        goto invalid;
    dluma_manifest_diagnostic(
        diagnostic, DLUMA_STATUS_OK, 0U, "manifest is valid");
    return DLUMA_STATUS_OK;
invalid:
    if (reader.status == DLUMA_STATUS_OK)
        reader.status = DLUMA_STATUS_INVALID_ARGUMENT;
    dluma_manifest_diagnostic(
        diagnostic, reader.status,
        dluma_json_error_offset(&reader),
        "manifest is malformed, incomplete, or violates DLUMA API 1.0");
    return reader.status;
}

static unsigned int dluma_lock_kind(const char* value)
{
    if (strcmp(value, "manifest") == 0) return DLUMA_MOD_ARTIFACT_MANIFEST;
    if (strcmp(value, "entry") == 0) return DLUMA_MOD_ARTIFACT_ENTRY;
    if (strcmp(value, "script_module") == 0)
        return DLUMA_MOD_ARTIFACT_SCRIPT_MODULE;
    if (strcmp(value, "assembly") == 0) return DLUMA_MOD_ARTIFACT_ASSEMBLY;
    if (strcmp(value, "dependency") == 0) return DLUMA_MOD_ARTIFACT_DEPENDENCY;
    if (strcmp(value, "configuration") == 0)
        return DLUMA_MOD_ARTIFACT_CONFIGURATION;
    if (strcmp(value, "companion") == 0) return DLUMA_MOD_ARTIFACT_COMPANION;
    if (strcmp(value, "license") == 0) return DLUMA_MOD_ARTIFACT_LICENSE;
    return 0U;
}

static int dluma_lock_parse_artifact(
    DlumaJsonReader* reader,
    DlumaModLockedArtifact* output)
{
    uint64_t fields = 0;
    int first = 1;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_manifest_key_is(key, "path")) {
            char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
            if (!dluma_manifest_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader, path, (unsigned int)sizeof(path)) ||
                !dluma_mod_relative_path_validate_and_normalize(
                    path, output->path,
                    (unsigned int)sizeof(output->path)))
                return 0;
        } else if (dluma_manifest_key_is(key, "kind")) {
            char kind[32];
            if (!dluma_manifest_mark(&fields, 1U, reader) ||
                !dluma_json_string(
                    reader, kind, (unsigned int)sizeof(kind)))
                return 0;
            output->kind = dluma_lock_kind(kind);
            if (!output->kind) return 0;
        } else if (dluma_manifest_key_is(key, "size")) {
            if (!dluma_manifest_mark(&fields, 2U, reader) ||
                !dluma_json_u64(reader, &output->size) ||
                output->size > 1073741824ULL)
                return 0;
        } else if (dluma_manifest_key_is(key, "sha256")) {
            if (!dluma_manifest_mark(&fields, 3U, reader) ||
                !dluma_json_string(
                    reader, output->sha256,
                    (unsigned int)sizeof(output->sha256)) ||
                !dluma_manifest_sha256(output->sha256))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) && (fields & 15ULL) == 15ULL;
}

static int dluma_lock_parse_artifacts(
    DlumaJsonReader* reader,
    DlumaModLock* output)
{
    int first = 1;
    if (!dluma_json_array_begin(reader)) return 0;
    for (;;) {
        unsigned int index;
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (output->artifact_count >= output->artifact_capacity ||
            output->artifact_count >= DLUMA_MOD_LOCK_MAX_ARTIFACTS) {
            reader->status = DLUMA_STATUS_INSUFFICIENT_BUFFER;
            return 0;
        }
        if (!dluma_lock_parse_artifact(
                reader, &output->artifacts[output->artifact_count]))
            return 0;
        for (index = 0; index < output->artifact_count; ++index)
            if (strcmp(
                    output->artifacts[index].path,
                    output->artifacts[output->artifact_count].path) == 0) {
                reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
                return 0;
            }
        ++output->artifact_count;
    }
    return dluma_json_array_end(reader) && output->artifact_count;
}

DlumaStatus dluma_mod_lock_parse(
    const void* bytes,
    unsigned int size,
    DlumaModLock* output,
    DlumaModManifestDiagnostic* diagnostic)
{
    DlumaJsonReader reader;
    DlumaModLockedArtifact* artifacts;
    unsigned int capacity;
    uint64_t fields = 0;
    int first = 1;
    if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
    if (!bytes || !size || size > DLUMA_MOD_LOCK_MAX_BYTES ||
        !output || output->struct_size < sizeof(*output) ||
        !output->artifacts || !output->artifact_capacity) {
        dluma_manifest_diagnostic(
            diagnostic, size > DLUMA_MOD_LOCK_MAX_BYTES
                ? DLUMA_STATUS_LIMIT_EXCEEDED
                : DLUMA_STATUS_INVALID_ARGUMENT,
            0U, "invalid lock buffer or output record");
        return diagnostic ? diagnostic->status : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    artifacts = output->artifacts;
    capacity = output->artifact_capacity;
    memset(output, 0, sizeof(*output));
    output->struct_size = (unsigned int)sizeof(*output);
    output->struct_version = DLUMA_MOD_MANIFEST_VERSION;
    output->artifacts = artifacts;
    output->artifact_capacity = capacity;
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader)) goto invalid;
    for (;;) {
        char key[64];
        int next = dluma_json_object_next(&reader, &first);
        if (next < 0) goto invalid;
        if (!next) break;
        if (!dluma_json_string(&reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(&reader, ':'))
            goto invalid;
        if (dluma_manifest_key_is(key, "$schema")) {
            char ignored[512];
            if (!dluma_manifest_mark(&fields, 31U, &reader) ||
                !dluma_json_string(
                    &reader, ignored, (unsigned int)sizeof(ignored)))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "schema")) {
            char value[32];
            if (!dluma_manifest_mark(&fields, 0U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                strcmp(value, "dluma.mod.lock.v1") != 0)
                goto invalid;
        } else if (dluma_manifest_key_is(key, "api_version")) {
            char value[16];
            if (!dluma_manifest_mark(&fields, 1U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                strcmp(value, DLUMA_API_PUBLIC_VERSION) != 0)
                goto invalid;
        } else if (dluma_manifest_key_is(key, "mod_id")) {
            if (!dluma_manifest_mark(&fields, 2U, &reader) ||
                !dluma_json_string(
                    &reader, output->mod_id,
                    (unsigned int)sizeof(output->mod_id)) ||
                !dluma_manifest_lower_id(output->mod_id, 3U, 127U))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "manifest_sha256")) {
            if (!dluma_manifest_mark(&fields, 3U, &reader) ||
                !dluma_json_string(
                    &reader, output->manifest_sha256,
                    (unsigned int)sizeof(output->manifest_sha256)) ||
                !dluma_manifest_sha256(output->manifest_sha256))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "catalog_sha256")) {
            if (!dluma_manifest_mark(&fields, 4U, &reader) ||
                !dluma_json_string(
                    &reader, output->catalog_sha256,
                    (unsigned int)sizeof(output->catalog_sha256)) ||
                !dluma_manifest_sha256(output->catalog_sha256))
                goto invalid;
        } else if (dluma_manifest_key_is(key, "artifacts")) {
            if (!dluma_manifest_mark(&fields, 5U, &reader) ||
                !dluma_lock_parse_artifacts(&reader, output))
                goto invalid;
        } else {
            reader.status = DLUMA_STATUS_INVALID_ARGUMENT;
            goto invalid;
        }
    }
    if (!dluma_json_object_end(&reader) ||
        !dluma_json_finished(&reader) ||
        (fields & 0x3FULL) != 0x3FULL ||
        (strcmp(output->catalog_sha256, DLUMA_API_CATALOG_SHA256) != 0 &&
         strcmp(output->catalog_sha256,
                DLUMA_API_IMMEDIATELY_PREVIOUS_CATALOG_SHA256) != 0 &&
         strcmp(output->catalog_sha256,
                DLUMA_API_COMPATIBLE_SCOPE_CATALOG_SHA256) != 0))
        goto invalid;
    output->uses_previous_catalog =
        strcmp(output->catalog_sha256, DLUMA_API_CATALOG_SHA256) != 0;
    dluma_manifest_diagnostic(
        diagnostic, DLUMA_STATUS_OK, 0U, "lock is valid");
    return DLUMA_STATUS_OK;
invalid:
    if (reader.status == DLUMA_STATUS_OK)
        reader.status = DLUMA_STATUS_INVALID_ARGUMENT;
    dluma_manifest_diagnostic(
        diagnostic, reader.status,
        dluma_json_error_offset(&reader),
        "lock is malformed, incomplete, or uses another API catalog");
    return reader.status;
}

int dluma_mod_manifest_expected_suffix(
    const DlumaModManifest* manifest,
    const char* suffix)
{
    const char* dot;
    if (!manifest || !suffix) return 0;
    dot = strrchr(manifest->entry, '.');
    return dot && strcmp(dot, suffix) == 0;
}

int dluma_mod_lock_catalog_compatible(
    const DlumaModLock* lock,
    const DlumaModManifest* manifest)
{
    if (!lock || !manifest) return 0;
    /* Admitted prior catalogs preserve the public ABI. */
    return 1;
}
