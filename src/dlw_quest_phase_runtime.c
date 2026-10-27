#include "dlw_quest_phase_runtime.h"
#include "core/dlw_json_reader.h"
#include "core/dlw_quest_phase.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"

#define DLUMA_QUEST_INVENTORY_MAX_DEPTH 16U
#define DLUMA_QUEST_INVENTORY_MAX_ENTRIES 8192U
#define DLUMA_QUEST_XML_MAX_BYTES (64ULL * 1024ULL * 1024ULL)

typedef struct DlumaQuestSidecarHeader {
    char schema[64];
    char generated_by[64];
    char source_path[DLUMA_HANDOFF_SIDECAR_PATH_CAPACITY];
    char source_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char output_path[DLUMA_HANDOFF_SIDECAR_PATH_CAPACITY];
    char output_sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int bindings_present;
} DlumaQuestSidecarHeader;

static int dluma_quest_sha256_text(const char* value)
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

static int dluma_quest_text_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static const WCHAR* dluma_quest_skip_extended(const WCHAR* value)
{
    if (value && value[0] == L'\\' && value[1] == L'\\' &&
        value[2] == L'?' && value[3] == L'\\')
        return value + 4;
    return value;
}

static int dluma_quest_wends_icase(const WCHAR* value, const WCHAR* suffix)
{
    SIZE_T value_size;
    SIZE_T suffix_size;
    if (!value || !suffix) return 0;
    value_size = wcslen(value);
    suffix_size = wcslen(suffix);
    if (suffix_size > value_size) return 0;
    return dluma_wstricmp(value + value_size - suffix_size, suffix) == 0;
}

static int dluma_quest_parse_file_identity(
    DlumaJsonReader* reader,
    char* path,
    unsigned int path_capacity,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    int first = 1;
    unsigned int fields = 0;
    if (!reader || !path || !path_capacity || !sha256 ||
        !dluma_json_object_begin(reader))
        return 0;
    for (;;) {
        char key[64];
        int next = dluma_json_object_next(reader, &first);
        unsigned int bit;
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(reader, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_stricmp(key, "path") == 0) bit = 1U;
        else if (dluma_stricmp(key, "sha256") == 0) bit = 2U;
        else return 0;
        if (fields & bit) return 0;
        fields |= bit;
        if (bit == 1U) {
            if (!dluma_json_string(
                    reader, path, path_capacity))
                return 0;
        } else if (!dluma_json_string(
                       reader, sha256,
                       DLUMA_SHA256_HEX_CAPACITY))
            return 0;
    }
    return dluma_json_object_end(reader) && fields == 3U &&
        path[0] && dluma_quest_sha256_text(sha256);
}

static int dluma_quest_parse_sidecar_header(
    const BYTE* bytes,
    unsigned int size,
    DlumaQuestSidecarHeader* header)
{
    DlumaJsonReader reader;
    int first = 1;
    unsigned int fields = 0;
    if (!bytes || !size || !header) return 0;
    memset(header, 0, sizeof(*header));
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        char key[64];
        int next = dluma_json_object_next(&reader, &first);
        unsigned int bit;
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(&reader, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_json_take(&reader, ':'))
            return 0;
        if (dluma_stricmp(key, "schema") == 0) bit = 1U;
        else if (dluma_stricmp(key, "generated_by") == 0) bit = 2U;
        else if (dluma_stricmp(key, "source") == 0) bit = 4U;
        else if (dluma_stricmp(key, "output") == 0) bit = 8U;
        else if (dluma_stricmp(key, "bindings") == 0) bit = 16U;
        else if (dluma_stricmp(key, "provider_hashes_diagnostic") == 0)
            bit = 32U;
        else return 0;
        if (fields & bit) return 0;
        fields |= bit;
        if (bit == 1U) {
            if (!dluma_json_string(
                    &reader, header->schema,
                    DLUMA_ARRAY_COUNT(header->schema)))
                return 0;
        } else if (bit == 2U) {
            if (!dluma_json_string(
                    &reader, header->generated_by,
                    DLUMA_ARRAY_COUNT(header->generated_by)))
                return 0;
        } else if (bit == 4U) {
            if (!dluma_quest_parse_file_identity(
                    &reader, header->source_path,
                    DLUMA_ARRAY_COUNT(header->source_path),
                    header->source_sha256))
                return 0;
        } else if (bit == 8U) {
            if (!dluma_quest_parse_file_identity(
                    &reader, header->output_path,
                    DLUMA_ARRAY_COUNT(header->output_path),
                    header->output_sha256))
                return 0;
        } else {
            if (bit == 16U) header->bindings_present = 1U;
            if (!dluma_json_skip_value(&reader)) return 0;
        }
    }
    return dluma_json_object_end(&reader) && dluma_json_finished(&reader) &&
        (fields & 31U) == 31U && header->bindings_present &&
        dluma_stricmp(header->schema, "dluma.quest_phase_bindings.v1") == 0 &&
        dluma_stricmp(header->generated_by, "dluma") == 0;
}

static int dluma_quest_read_locked(
    const WCHAR* path,
    const WCHAR* final_root,
    ULONGLONG maximum,
    BYTE** bytes_out,
    unsigned int* size_out,
    WCHAR* final_path,
    unsigned int final_capacity,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    LARGE_INTEGER size;
    BYTE* bytes = 0;
    DWORD read = 0;
    int result = 0;
    if (!path || !final_root || !bytes_out || !size_out || !final_path ||
        !final_capacity || !sha256)
        return 0;
    *bytes_out = 0;
    *size_out = 0;
    final_path[0] = 0;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(file, &before) ||
        (before.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        before.nNumberOfLinks != 1U ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > maximum ||
        (ULONGLONG)size.QuadPart > 0xFFFFFFFFULL ||
        !dluma_get_final_path_checked(file, final_path, final_capacity) ||
        !dluma_final_path_equal_or_under(final_path, final_root))
        goto done;
    bytes = (BYTE*)dluma_alloc((SIZE_T)size.QuadPart + 1U);
    if (!bytes) goto done;
    if (size.QuadPart &&
        (!ReadFile(file, bytes, (DWORD)size.QuadPart, &read, 0) ||
         read != (DWORD)size.QuadPart))
        goto done;
    bytes[(DWORD)size.QuadPart] = 0;
    if (!GetFileInformationByHandle(file, &after) ||
        before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh ||
        before.nFileIndexLow != after.nFileIndexLow ||
        before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow)
        goto done;
    dluma_sha256_bytes_hex(bytes, (SIZE_T)size.QuadPart, sha256);
    *bytes_out = bytes;
    *size_out = (unsigned int)size.QuadPart;
    bytes = 0;
    result = 1;
done:
    if (bytes) dluma_free(bytes);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return result;
}

static int dluma_quest_lower_identifier(
    const char* value,
    unsigned int maximum)
{
    unsigned int index = 0;
    if (!value || !value[0]) return 0;
    while (value[index]) {
        char current = value[index];
        if (!((current >= 'a' && current <= 'z') ||
              (current >= '0' && current <= '9') ||
              current == '.' || current == '_' || current == '-'))
            return 0;
        ++index;
    }
    return index <= maximum;
}

static int dluma_quest_contract_matches(
    const DlumaQuestPhaseBindingSnapshot* binding)
{
    char expected[257];
    SIZE_T provider_size;
    SIZE_T handler_size;
    if (!binding) return 0;
    provider_size = strlen(binding->provider_mod);
    handler_size = strlen(binding->handler_id);
    if (provider_size + 1U + handler_size + 1U > sizeof(expected)) return 0;
    memcpy(expected, binding->provider_mod, provider_size);
    expected[provider_size] = ':';
    memcpy(expected + provider_size + 1U,
           binding->handler_id, handler_size + 1U);
    return dluma_quest_text_equal(expected, binding->contract_id);
}

static int dluma_quest_parse_wait(
    DlumaJsonReader* reader,
    double* output)
{
    const unsigned char* start;
    const unsigned char* cursor;
    if (!reader || !output) return 0;
    dluma_json_space(reader);
    start = reader->cursor;
    cursor = start;
    while (cursor < reader->end &&
           ((*cursor >= '0' && *cursor <= '9') || *cursor == '.'))
        ++cursor;
    if (cursor == start ||
        !dluma_quest_phase_parse_wait_seconds(
            start, (unsigned int)(cursor - start), output))
        return 0;
    reader->cursor = cursor;
    return 1;
}

static int dluma_quest_parse_arguments(
    DlumaJsonReader* reader,
    DlumaQuestPhaseBindingSnapshot* binding)
{
    const unsigned char* start;
    const unsigned char* end;
    DlumaQuestPhaseDiagnostic diagnostic;
    unsigned int canonical_size = 0;
    if (!reader || !binding) return 0;
    dluma_json_space(reader);
    start = reader->cursor;
    if (!dluma_json_skip_value(reader)) return 0;
    end = reader->cursor;
    if (end <= start || (unsigned int)(end - start) >
            DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES)
        return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (!dluma_quest_phase_json_canonicalize_object(
            start, (unsigned int)(end - start),
            binding->arguments, DLUMA_ARRAY_COUNT(binding->arguments),
            &canonical_size, &diagnostic) ||
        canonical_size != (unsigned int)(end - start) ||
        memcmp(binding->arguments, start, canonical_size) != 0)
        return 0;
    binding->arguments_size = canonical_size;
    return 1;
}

static int dluma_quest_binding_hash_matches(
    const unsigned char* object_start,
    const unsigned char* hash_separator,
    const char* expected)
{
    BYTE* identity;
    SIZE_T prefix_size;
    char actual[DLUMA_SHA256_HEX_CAPACITY];
    int result;
    if (!object_start || !hash_separator ||
        hash_separator <= object_start || *hash_separator != ',' ||
        !dluma_quest_sha256_text(expected))
        return 0;
    prefix_size = (SIZE_T)(hash_separator - object_start);
    if (prefix_size >= DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES) return 0;
    identity = (BYTE*)dluma_alloc(prefix_size + 1U);
    if (!identity) return 0;
    memcpy(identity, object_start, prefix_size);
    identity[prefix_size] = '}';
    dluma_sha256_bytes_hex(identity, prefix_size + 1U, actual);
    result = dluma_stricmp(actual, expected) == 0;
    dluma_free(identity);
    return result;
}

static int dluma_quest_parse_binding(
    DlumaJsonReader* reader,
    DlumaQuestPhaseBindingSnapshot* binding)
{
    const unsigned char* object_start;
    const unsigned char* hash_separator = 0;
    unsigned int fields = 0;
    int first = 1;
    int hash_was_last = 0;
    uint64_t source_line = 0;
    if (!reader || !binding) return 0;
    memset(binding, 0, sizeof(*binding));
    dluma_json_space(reader);
    object_start = reader->cursor;
    if (!dluma_json_object_begin(reader)) return 0;
    for (;;) {
        const unsigned char* separator = reader->cursor;
        char key[64];
        unsigned int bit;
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (hash_was_last ||
            !dluma_json_string(reader, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_quest_text_equal(key, "quest")) bit = 1U;
        else if (dluma_quest_text_equal(key, "phase")) bit = 2U;
        else if (dluma_quest_text_equal(key, "source_line")) bit = 4U;
        else if (dluma_quest_text_equal(key, "runtime_kind")) bit = 8U;
        else if (dluma_quest_text_equal(key, "provider_mod")) bit = 16U;
        else if (dluma_quest_text_equal(key, "handler")) bit = 32U;
        else if (dluma_quest_text_equal(key, "contract_id")) bit = 64U;
        else if (dluma_quest_text_equal(key, "arguments")) bit = 128U;
        else if (dluma_quest_text_equal(key, "wait")) bit = 256U;
        else if (dluma_quest_text_equal(key, "completion")) bit = 512U;
        else if (dluma_quest_text_equal(key, "binding_hash")) bit = 1024U;
        else return 0;
        if (fields & bit) return 0;
        fields |= bit;
        if (bit == 1U) {
            if (!dluma_json_string(
                    reader, binding->quest_id,
                    DLUMA_ARRAY_COUNT(binding->quest_id)))
                return 0;
        } else if (bit == 2U) {
            if (!dluma_json_string(
                    reader, binding->phase_id,
                    DLUMA_ARRAY_COUNT(binding->phase_id)))
                return 0;
        } else if (bit == 4U) {
            if (!dluma_json_u64(reader, &source_line) ||
                !source_line || source_line > 0xFFFFFFFFULL)
                return 0;
            binding->source_line = (unsigned int)source_line;
        } else if (bit == 8U) {
            char kind[16];
            if (!dluma_json_string(reader, kind, DLUMA_ARRAY_COUNT(kind)))
                return 0;
            if (dluma_quest_text_equal(kind, "lua"))
                binding->runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_LUA;
            else if (dluma_quest_text_equal(kind, "native"))
                binding->runtime_kind = DLUMA_QUEST_PHASE_RUNTIME_NATIVE;
            else return 0;
        } else if (bit == 16U) {
            if (!dluma_json_string(
                    reader, binding->provider_mod,
                    DLUMA_ARRAY_COUNT(binding->provider_mod)))
                return 0;
        } else if (bit == 32U) {
            if (!dluma_json_string(
                    reader, binding->handler_id,
                    DLUMA_ARRAY_COUNT(binding->handler_id)))
                return 0;
        } else if (bit == 64U) {
            if (!dluma_json_string(
                    reader, binding->contract_id,
                    DLUMA_ARRAY_COUNT(binding->contract_id)))
                return 0;
        } else if (bit == 128U) {
            if (!dluma_quest_parse_arguments(reader, binding)) return 0;
        } else if (bit == 256U) {
            if (!dluma_quest_parse_wait(reader, &binding->wait_seconds))
                return 0;
        } else if (bit == 512U) {
            char completion[32];
            if (!dluma_json_string(
                    reader, completion, DLUMA_ARRAY_COUNT(completion)))
                return 0;
            if (dluma_quest_text_equal(completion, "fire_and_forget"))
                binding->completion =
                    DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET;
            else if (dluma_quest_text_equal(completion, "stock_timed_wait"))
                binding->completion =
                    DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT;
            else return 0;
        } else {
            hash_separator = separator;
            hash_was_last = 1;
            if (!dluma_json_string(
                    reader, binding->binding_hash,
                    DLUMA_ARRAY_COUNT(binding->binding_hash)))
                return 0;
        }
    }
    if (!dluma_json_object_end(reader) || fields != 2047U ||
        !binding->quest_id[0] || !binding->phase_id[0] ||
        !dluma_quest_lower_identifier(binding->provider_mod, 127U) ||
        !dluma_quest_lower_identifier(binding->handler_id, 127U) ||
        !dluma_quest_contract_matches(binding) ||
        ((binding->wait_seconds == 0.0) !=
         (binding->completion ==
          DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET)) ||
        (binding->wait_seconds > 0.0 &&
         binding->completion !=
             DLUMA_QUEST_PHASE_COMPLETION_STOCK_TIMED_WAIT) ||
        !hash_separator ||
        !dluma_quest_binding_hash_matches(
            object_start, hash_separator, binding->binding_hash))
        return 0;
    return 1;
}

static int dluma_quest_parse_bindings_array(
    DlumaJsonReader* reader,
    DlumaQuestPhaseBindingSnapshot* bindings,
    unsigned int capacity,
    unsigned int* count)
{
    int first = 1;
    if (!reader || !bindings || !count || !dluma_json_array_begin(reader))
        return 0;
    for (;;) {
        int next = dluma_json_array_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (*count >= capacity ||
            !dluma_quest_parse_binding(reader, &bindings[*count]))
            return 0;
        ++*count;
    }
    return dluma_json_array_end(reader);
}

static int dluma_quest_binding_duplicate(
    const DlumaQuestPhaseBindingSnapshot* bindings,
    unsigned int count)
{
    unsigned int index;
    for (index = 0; index < count; ++index) {
        unsigned int prior;
        for (prior = 0; prior < index; ++prior)
            if (dluma_quest_text_equal(
                    bindings[index].quest_id,
                    bindings[prior].quest_id) &&
                dluma_quest_text_equal(
                    bindings[index].phase_id,
                    bindings[prior].phase_id))
                return 1;
    }
    return 0;
}

static int dluma_quest_parse_sidecar_full(
    const BYTE* bytes,
    unsigned int size,
    DlumaQuestSidecarHeader* header,
    DlumaQuestPhaseBindingSnapshot* bindings,
    unsigned int capacity,
    unsigned int* count)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    unsigned int initial_count;
    int first = 1;
    if (!bytes || !size || !header || !bindings || !count) return 0;
    initial_count = *count;
    memset(header, 0, sizeof(*header));
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        char key[64];
        unsigned int bit;
        int next = dluma_json_object_next(&reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(&reader, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_json_take(&reader, ':'))
            return 0;
        if (dluma_quest_text_equal(key, "schema")) bit = 1U;
        else if (dluma_quest_text_equal(key, "generated_by")) bit = 2U;
        else if (dluma_quest_text_equal(key, "source")) bit = 4U;
        else if (dluma_quest_text_equal(key, "output")) bit = 8U;
        else if (dluma_quest_text_equal(key, "bindings")) bit = 16U;
        else if (dluma_quest_text_equal(key, "provider_hashes_diagnostic"))
            bit = 32U;
        else return 0;
        if (fields & bit) return 0;
        fields |= bit;
        if (bit == 1U) {
            if (!dluma_json_string(
                    &reader, header->schema,
                    DLUMA_ARRAY_COUNT(header->schema)))
                return 0;
        } else if (bit == 2U) {
            if (!dluma_json_string(
                    &reader, header->generated_by,
                    DLUMA_ARRAY_COUNT(header->generated_by)))
                return 0;
        } else if (bit == 4U) {
            if (!dluma_quest_parse_file_identity(
                    &reader, header->source_path,
                    DLUMA_ARRAY_COUNT(header->source_path),
                    header->source_sha256))
                return 0;
        } else if (bit == 8U) {
            if (!dluma_quest_parse_file_identity(
                    &reader, header->output_path,
                    DLUMA_ARRAY_COUNT(header->output_path),
                    header->output_sha256))
                return 0;
        } else if (bit == 16U) {
            if (!dluma_quest_parse_bindings_array(
                    &reader, bindings, capacity, count))
                return 0;
            header->bindings_present = 1U;
        } else if (!dluma_json_skip_value(&reader)) return 0;
    }
    return dluma_json_object_end(&reader) && dluma_json_finished(&reader) &&
        (fields & 31U) == 31U && header->bindings_present &&
        *count > initial_count &&
        dluma_quest_text_equal(
            header->schema, "dluma.quest_phase_bindings.v1") &&
        dluma_quest_text_equal(
            header->generated_by, "dluma");
}

static int dluma_quest_inventory_one(
    const WCHAR* path,
    const WCHAR* final_root,
    DlumaHandoffSidecarRecord* output)
{
    BYTE* sidecar_bytes = 0;
    BYTE* source_bytes = 0;
    BYTE* xml_bytes = 0;
    unsigned int sidecar_size = 0;
    unsigned int source_size = 0;
    unsigned int xml_size = 0;
    WCHAR sidecar_final[DLUMA_MAX_WPATH];
    WCHAR source_lexical[DLUMA_MAX_WPATH];
    WCHAR source_final[DLUMA_MAX_WPATH];
    WCHAR xml_lexical[DLUMA_MAX_WPATH];
    WCHAR xml_final[DLUMA_MAX_WPATH];
    WCHAR source_path[DLUMA_MAX_WPATH];
    WCHAR output_path[DLUMA_MAX_WPATH];
    DlumaQuestSidecarHeader header;
    char sidecar_sha[DLUMA_SHA256_HEX_CAPACITY];
    char source_sha[DLUMA_SHA256_HEX_CAPACITY];
    char xml_sha[DLUMA_SHA256_HEX_CAPACITY];
    SIZE_T suffix_size = wcslen(L".dlumaq.json");
    SIZE_T path_size;
    int result = 0;
    memset(output, 0, sizeof(*output));
    if (!dluma_quest_read_locked(
            path, final_root, DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES,
            &sidecar_bytes, &sidecar_size, sidecar_final,
            DLUMA_ARRAY_COUNT(sidecar_final), sidecar_sha) ||
        !dluma_quest_parse_sidecar_header(
            sidecar_bytes, sidecar_size, &header) ||
        !dluma_utf8_to_wide(
            header.source_path, source_path,
            DLUMA_ARRAY_COUNT(source_path)) ||
        !dluma_utf8_to_wide(
            header.output_path, output_path,
            DLUMA_ARRAY_COUNT(output_path)))
        goto done;
    dluma_wstr_copy(
        source_lexical, DLUMA_ARRAY_COUNT(source_lexical), source_path);
    if (!dluma_quest_read_locked(
            source_lexical, final_root, DLUMA_QUEST_XML_MAX_BYTES,
            &source_bytes, &source_size, source_final,
            DLUMA_ARRAY_COUNT(source_final), source_sha) ||
        dluma_wstricmp(
            dluma_quest_skip_extended(source_path),
            dluma_quest_skip_extended(source_final)) != 0 ||
        dluma_stricmp(source_sha, header.source_sha256) != 0)
        goto done;
    path_size = wcslen(sidecar_final);
    if (path_size <= suffix_size ||
        !dluma_quest_wends_icase(sidecar_final, L".dlumaq.json") ||
        path_size - suffix_size + wcslen(L".xml") + 1U >
            DLUMA_ARRAY_COUNT(xml_lexical))
        goto done;
    dluma_wstr_copy(xml_lexical, DLUMA_ARRAY_COUNT(xml_lexical), sidecar_final);
    xml_lexical[path_size - suffix_size] = 0;
    dluma_wstr_append(xml_lexical, DLUMA_ARRAY_COUNT(xml_lexical), L".xml");
    if (!dluma_quest_read_locked(
            xml_lexical, final_root, DLUMA_QUEST_XML_MAX_BYTES,
            &xml_bytes, &xml_size, xml_final,
            DLUMA_ARRAY_COUNT(xml_final), xml_sha) ||
        dluma_wstricmp(
            dluma_quest_skip_extended(output_path),
            dluma_quest_skip_extended(xml_final)) != 0 ||
        dluma_stricmp(xml_sha, header.output_sha256) != 0 ||
        !dluma_wide_to_utf8(
            sidecar_final, output->canonical_path_utf8,
            DLUMA_ARRAY_COUNT(output->canonical_path_utf8)))
        goto done;
    dluma_str_copy(output->sidecar_sha256,
                 DLUMA_ARRAY_COUNT(output->sidecar_sha256), sidecar_sha);
    dluma_str_copy(output->compiled_xml_sha256,
                 DLUMA_ARRAY_COUNT(output->compiled_xml_sha256), xml_sha);
    result = 1;
done:
    if (sidecar_bytes) dluma_free(sidecar_bytes);
    if (source_bytes) dluma_free(source_bytes);
    if (xml_bytes) dluma_free(xml_bytes);
    return result;
}

static void dluma_quest_inventory_sort(
    DlumaHandoffSidecarRecord* output,
    unsigned int count)
{
    unsigned int index;
    for (index = 1U; index < count; ++index) {
        DlumaHandoffSidecarRecord value = output[index];
        unsigned int cursor = index;
        while (cursor && dluma_stricmp(
                output[cursor - 1U].canonical_path_utf8,
                value.canonical_path_utf8) > 0) {
            output[cursor] = output[cursor - 1U];
            --cursor;
        }
        output[cursor] = value;
    }
}

static int dluma_quest_inventory_walk(
    const WCHAR* lexical_directory,
    const WCHAR* final_root,
    unsigned int depth,
    unsigned int* visited,
    DlumaHandoffSidecarRecord* output,
    unsigned int capacity,
    unsigned int* count)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE search;
    int result = 1;
    if (!lexical_directory || !final_root || !visited || !output || !count ||
        depth > DLUMA_QUEST_INVENTORY_MAX_DEPTH ||
        !dluma_checked_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), lexical_directory, L"*"))
        return 0;
    memset(&data, 0, sizeof(data));
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        WCHAR child[DLUMA_MAX_WPATH];
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (++*visited > DLUMA_QUEST_INVENTORY_MAX_ENTRIES ||
            !dluma_checked_path_join(
                child, DLUMA_ARRAY_COUNT(child),
                lexical_directory, data.cFileName)) {
            result = 0;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            result = 0;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DlumaDirectoryIdentity identity;
            if (!dluma_open_directory_identity(child, &identity) ||
                !dluma_final_path_equal_or_under(identity.final_path, final_root) ||
                !dluma_quest_inventory_walk(
                    child, final_root, depth + 1U, visited,
                    output, capacity, count)) {
                result = 0;
                break;
            }
        } else if (dluma_quest_wends_icase(data.cFileName, L".dlumaq.json")) {
            if (*count >= capacity ||
                !dluma_quest_inventory_one(
                    child, final_root, &output[*count])) {
                result = 0;
                break;
            }
            ++*count;
        }
    } while (FindNextFileW(search, &data));
    if (result && GetLastError() != ERROR_NO_MORE_FILES) result = 0;
    FindClose(search);
    return result;
}

int dluma_quest_phase_inventory_project(
    const WCHAR* project_root,
    DlumaHandoffSidecarRecord* output,
    unsigned int capacity,
    unsigned int* count_out)
{
    DlumaDirectoryIdentity root;
    unsigned int visited = 0;
    unsigned int count = 0;
    if (!project_root || !project_root[0] || !output ||
        !capacity || capacity > DLUMA_HANDOFF_MAX_SIDECARS || !count_out)
        return 0;
    *count_out = 0;
    memset(output, 0, capacity * sizeof(*output));
    if (!dluma_open_directory_identity(project_root, &root) ||
        !dluma_quest_inventory_walk(
            project_root, root.final_path, 0U, &visited,
            output, capacity, &count))
        return 0;
    dluma_quest_inventory_sort(output, count);
    *count_out = count;
    return 1;
}

int dluma_quest_phase_sidecar_is_loader_owned(
    const WCHAR* sidecar_path,
    const WCHAR* project_root)
{
    DlumaDirectoryIdentity root;
    BYTE* bytes = 0;
    unsigned int size = 0;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaQuestSidecarHeader header;
    int result = 0;
    if (!sidecar_path || !project_root ||
        !dluma_open_directory_identity(project_root, &root) ||
        !dluma_quest_read_locked(
            sidecar_path, root.final_path,
            DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES,
            &bytes, &size, final_path, DLUMA_ARRAY_COUNT(final_path), sha256))
        return 0;
    if (dluma_quest_parse_sidecar_header(bytes, size, &header)) result = 1;
    dluma_free(bytes);
    return result;
}

static int dluma_quest_load_bindings_one(
    const WCHAR* path,
    const WCHAR* final_root,
    DlumaQuestPhaseBindingSnapshot* bindings,
    unsigned int capacity,
    unsigned int* count)
{
    BYTE* bytes = 0;
    unsigned int size = 0;
    unsigned int initial_count;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char sidecar_sha[DLUMA_SHA256_HEX_CAPACITY];
    DlumaQuestSidecarHeader header;
    DlumaHandoffSidecarRecord verified;
    int result = 0;
    if (!path || !final_root || !bindings || !count) return 0;
    initial_count = *count;
    memset(&header, 0, sizeof(header));
    memset(&verified, 0, sizeof(verified));
    if (!dluma_quest_read_locked(
            path, final_root, DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES,
            &bytes, &size, final_path, DLUMA_ARRAY_COUNT(final_path),
            sidecar_sha) ||
        !dluma_quest_parse_sidecar_full(
            bytes, size, &header, bindings, capacity, count) ||
        !dluma_quest_inventory_one(path, final_root, &verified) ||
        dluma_stricmp(sidecar_sha, verified.sidecar_sha256) != 0 ||
        dluma_stricmp(header.output_sha256,
                    verified.compiled_xml_sha256) != 0)
        goto done;
    result = 1;
done:
    if (!result) {
        memset(bindings + initial_count, 0,
               (*count - initial_count) * sizeof(*bindings));
        *count = initial_count;
    }
    if (bytes) dluma_free(bytes);
    return result;
}

static int dluma_quest_load_bindings_walk(
    const WCHAR* lexical_directory,
    const WCHAR* final_root,
    unsigned int depth,
    unsigned int* visited,
    DlumaQuestPhaseBindingSnapshot* bindings,
    unsigned int capacity,
    unsigned int* binding_count,
    unsigned int* sidecar_count)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE search;
    int result = 1;
    if (!lexical_directory || !final_root || !visited || !bindings ||
        !binding_count || !sidecar_count ||
        depth > DLUMA_QUEST_INVENTORY_MAX_DEPTH ||
        !dluma_checked_path_join(
            pattern, DLUMA_ARRAY_COUNT(pattern), lexical_directory, L"*"))
        return 0;
    memset(&data, 0, sizeof(data));
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        WCHAR child[DLUMA_MAX_WPATH];
        if ((data.cFileName[0] == L'.' && !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' && data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (++*visited > DLUMA_QUEST_INVENTORY_MAX_ENTRIES ||
            !dluma_checked_path_join(
                child, DLUMA_ARRAY_COUNT(child),
                lexical_directory, data.cFileName) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            result = 0;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DlumaDirectoryIdentity identity;
            if (!dluma_open_directory_identity(child, &identity) ||
                !dluma_final_path_equal_or_under(identity.final_path, final_root) ||
                !dluma_quest_load_bindings_walk(
                    child, final_root, depth + 1U, visited,
                    bindings, capacity, binding_count, sidecar_count)) {
                result = 0;
                break;
            }
        } else if (dluma_quest_wends_icase(data.cFileName, L".dlumaq.json")) {
            if (*sidecar_count >= DLUMA_HANDOFF_MAX_SIDECARS ||
                !dluma_quest_load_bindings_one(
                    child, final_root, bindings, capacity, binding_count)) {
                result = 0;
                break;
            }
            ++*sidecar_count;
        }
    } while (FindNextFileW(search, &data));
    if (result && GetLastError() != ERROR_NO_MORE_FILES) result = 0;
    FindClose(search);
    return result;
}

int dluma_quest_phase_load_project_bindings(
    const WCHAR* project_root,
    unsigned int map_generation,
    DlumaQuestPhaseBindingSet* output)
{
    DlumaDirectoryIdentity root;
    DlumaQuestPhaseBindingSnapshot* bindings;
    unsigned int visited = 0;
    unsigned int count = 0;
    unsigned int sidecar_count = 0;
    if (!project_root || !project_root[0] || !map_generation || !output)
        return 0;
    memset(output, 0, sizeof(*output));
    bindings = (DlumaQuestPhaseBindingSnapshot*)dluma_alloc(
        DLUMA_QUEST_PHASE_MAX_BINDINGS * sizeof(*bindings));
    if (!bindings) return 0;
    memset(bindings, 0,
           DLUMA_QUEST_PHASE_MAX_BINDINGS * sizeof(*bindings));
    if (!dluma_open_directory_identity(project_root, &root) ||
        !dluma_quest_load_bindings_walk(
            project_root, root.final_path, 0U, &visited,
            bindings, DLUMA_QUEST_PHASE_MAX_BINDINGS,
            &count, &sidecar_count) ||
        dluma_quest_binding_duplicate(bindings, count)) {
        dluma_free(bindings);
        return 0;
    }
    if (!count) {
        dluma_free(bindings);
        bindings = 0;
    }
    output->map_generation = map_generation;
    output->binding_count = count;
    output->sidecar_count = sidecar_count;
    output->bindings = bindings;
    return 1;
}

void dluma_quest_phase_release_bindings(
    DlumaQuestPhaseBindingSet* bindings)
{
    if (!bindings) return;
    if (bindings->bindings) dluma_free(bindings->bindings);
    memset(bindings, 0, sizeof(*bindings));
}
