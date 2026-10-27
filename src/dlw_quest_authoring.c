#include "dlw_quest_authoring.h"
#include "core/dlw_quest_phase.h"
#include "dlw_native_mods.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_quest_editor.h"
#include "dlw_quest_phase_runtime.h"

#define DLUMA_QUEST_AUTHORING_MAX_SOURCE_BYTES (16UL * 1024UL * 1024UL)
#define DLUMA_QUEST_AUTHORING_MAX_OUTPUT_BYTES (64UL * 1024UL * 1024UL)
#define DLUMA_QUEST_AUTHORING_MAX_ALIASES \
    (DLUMA_MAX_NATIVE_MODS * DLUMA_MOD_MANIFEST_MAX_QUEST_PHASE_HANDLERS)

typedef struct DlumaQuestAuthoringBinding {
    DlumaQuestPhaseDirective directive;
    char command[64];
    char wait_json[64];
    char quest_id[128];
    unsigned int match_count;
} DlumaQuestAuthoringBinding;

typedef struct DlumaQuestAuthoringProviderDiagnostic {
    char provider_mod[128];
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char artifact_sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int runtime_kind;
} DlumaQuestAuthoringProviderDiagnostic;

struct DlumaQuestAuthoringTransaction {
    HANDLE source_file;
    HANDLE lowered_file;
    BY_HANDLE_FILE_INFORMATION source_identity;
    BYTE* source_bytes;
    unsigned int source_size;
    char source_sha256[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR source_final[DLUMA_MAX_WPATH];
    WCHAR lowered_path[DLUMA_MAX_WPATH];
    char lowered_path_utf8[DLUMA_MAX_WPATH * 2U];
    char lowered_sha256[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR output_lexical[DLUMA_MAX_WPATH];
    WCHAR project_root[DLUMA_MAX_WPATH];
    DlumaQuestAuthoringBinding* bindings;
    unsigned int binding_count;
    unsigned int custom;
    unsigned int had_old_output;
    char old_output_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char diagnostic[256];
};

static int dluma_quest_authoring_write_all(
    HANDLE file,
    const void* bytes,
    unsigned int size);

typedef struct DlumaQuestJsonWriter {
    char* bytes;
    unsigned int capacity;
    unsigned int size;
    int failed;
} DlumaQuestJsonWriter;

static const WCHAR* dluma_quest_authoring_skip_extended(const WCHAR* value)
{
    if (value && value[0] == L'\\' && value[1] == L'\\' &&
        value[2] == L'?' && value[3] == L'\\')
        return value + 4;
    return value;
}

static void dluma_quest_authoring_error(
    DlumaQuestAuthoringTransaction* transaction,
    const char* message)
{
    if (!transaction) return;
    dluma_str_copy(transaction->diagnostic,
                 DLUMA_ARRAY_COUNT(transaction->diagnostic),
                 message ? message : "quest authoring failed");
}

static int dluma_quest_authoring_read_handle(
    HANDLE file,
    unsigned int maximum,
    BYTE** bytes_out,
    unsigned int* size_out,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    LARGE_INTEGER size;
    LARGE_INTEGER zero;
    BYTE* bytes;
    DWORD read = 0;
    zero.QuadPart = 0;
    if (!file || file == INVALID_HANDLE_VALUE || !bytes_out || !size_out ||
        !sha256 || !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > maximum ||
        !SetFilePointerEx(file, zero, 0, FILE_BEGIN))
        return 0;
    bytes = (BYTE*)dluma_alloc((SIZE_T)size.QuadPart + 1U);
    if (!bytes) return 0;
    if (size.QuadPart &&
        (!ReadFile(file, bytes, (DWORD)size.QuadPart, &read, 0) ||
         read != (DWORD)size.QuadPart)) {
        dluma_free(bytes);
        return 0;
    }
    bytes[(DWORD)size.QuadPart] = 0;
    dluma_sha256_bytes_hex(bytes, (SIZE_T)size.QuadPart, sha256);
    *bytes_out = bytes;
    *size_out = (unsigned int)size.QuadPart;
    return 1;
}

static int dluma_quest_authoring_data_root(
    const WCHAR* candidate,
    const WCHAR* source_final,
    DlumaDirectoryIdentity* output)
{
    WCHAR data[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity candidate_identity;
    DlumaDirectoryIdentity data_identity;
    if (!candidate || !source_final || !output ||
        !dluma_open_directory_identity(candidate, &candidate_identity) ||
        !dluma_final_path_equal_or_under(
            source_final, candidate_identity.final_path) ||
        !dluma_checked_path_join(
            data, DLUMA_ARRAY_COUNT(data), candidate, L"data") ||
        !dluma_open_directory_identity(data, &data_identity) ||
        !dluma_final_path_equal_or_under(
            data_identity.final_path, candidate_identity.final_path) ||
        !dluma_final_path_equal_or_under(source_final, data_identity.final_path))
        return 0;
    *output = candidate_identity;
    return 1;
}

static int dluma_quest_authoring_resolve_project(
    const WCHAR* source_final,
    const WCHAR* authoritative_project,
    WCHAR* project_root,
    unsigned int capacity)
{
    DlumaDirectoryIdentity best;
    SIZE_T best_length = 0;
    unsigned int root_index;
    int have_best = 0;
    memset(&best, 0, sizeof(best));
    if (!source_final || !project_root || !capacity) return 0;
    if (authoritative_project && authoritative_project[0]) {
        if (!dluma_quest_authoring_data_root(
                authoritative_project, source_final, &best))
            return 0;
        dluma_wstr_copy(project_root, capacity, best.final_path);
        return project_root[0] != 0;
    }
    for (root_index = 0; root_index < g_dluma.workshop_root_count;
         ++root_index) {
        const WCHAR* lexical_root = g_dluma.workshop_roots[root_index];
        DlumaDirectoryIdentity root;
        DlumaDirectoryIdentity candidate;
        const WCHAR* relative;
        WCHAR child[DLUMA_MAX_WPATH];
        WCHAR component[260];
        unsigned int component_size = 0;
        if (!dluma_open_directory_identity(lexical_root, &root) ||
            !dluma_final_path_equal_or_under(source_final, root.final_path))
            continue;
        if (dluma_quest_authoring_data_root(
                lexical_root, source_final, &candidate)) {
            SIZE_T length = wcslen(candidate.final_path);
            if (!have_best || length > best_length) {
                best = candidate;
                best_length = length;
                have_best = 1;
            } else if (length == best_length &&
                       !dluma_directory_identity_equal(&best, &candidate))
                return 0;
        }
        relative = source_final + wcslen(root.final_path);
        while (*relative == L'\\' || *relative == L'/') ++relative;
        while (relative[component_size] &&
               relative[component_size] != L'\\' &&
               relative[component_size] != L'/') {
            if (component_size + 1U >= DLUMA_ARRAY_COUNT(component)) {
                component_size = 0;
                break;
            }
            component[component_size] = relative[component_size];
            ++component_size;
        }
        if (!component_size) continue;
        component[component_size] = 0;
        if (!dluma_checked_path_join(
                child, DLUMA_ARRAY_COUNT(child), lexical_root, component) ||
            !dluma_quest_authoring_data_root(child, source_final, &candidate))
            continue;
        {
            SIZE_T length = wcslen(candidate.final_path);
            if (!have_best || length > best_length) {
                best = candidate;
                best_length = length;
                have_best = 1;
            } else if (length == best_length &&
                       !dluma_directory_identity_equal(&best, &candidate))
                return 0;
        }
    }
    if (!have_best) return 0;
    dluma_wstr_copy(project_root, capacity, best.final_path);
    return 1;
}

static int dluma_quest_authoring_output_parent_is_contained(
    const WCHAR* output_path,
    const WCHAR* project_root)
{
    WCHAR parent[DLUMA_MAX_WPATH];
    DlumaDirectoryIdentity parent_identity;
    if (!output_path || !output_path[0] ||
        !project_root || !project_root[0])
        return 0;
    dluma_wstr_copy(parent, DLUMA_ARRAY_COUNT(parent), output_path);
    dluma_path_parent(parent);
    return parent[0] &&
        dluma_open_directory_identity(parent, &parent_identity) &&
        dluma_final_path_equal_or_under(
            parent_identity.final_path, project_root);
}

static int dluma_quest_authoring_command(
    const BYTE* line,
    unsigned int size,
    char* output,
    unsigned int capacity)
{
    unsigned int cursor = 0;
    unsigned int written = 0;
    if (!line || !output || !capacity) return 0;
    while (cursor < size && (line[cursor] == ' ' || line[cursor] == '\t'))
        ++cursor;
    if (cursor >= size || line[cursor++] != ',') return 0;
    while (cursor < size && line[cursor] != ' ' && line[cursor] != '\t' &&
           line[cursor] != '\r' && line[cursor] != '\n') {
        char value = (char)line[cursor++];
        if (written + 1U >= capacity ||
            !((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-'))
            return 0;
        output[written++] = value >= 'A' && value <= 'Z'
            ? (char)(value + ('a' - 'A')) : value;
    }
    output[written] = 0;
    return written != 0;
}

static int dluma_quest_authoring_known_command(
    const char* command,
    const DlumaQuestPhaseAlias* aliases,
    unsigned int alias_count)
{
    unsigned int index;
    if (!command) return 0;
    if (dluma_stricmp(command, "call-lua") == 0 ||
        dluma_stricmp(command, "call-native") == 0)
        return 1;
    for (index = 0; index < alias_count; ++index)
        if (dluma_stricmp(command, aliases[index].command) == 0)
            return 1;
    return 0;
}

static int dluma_quest_authoring_wait_json(
    const BYTE* line,
    unsigned int size,
    char output[64])
{
    const char marker[] = "-wait=";
    unsigned int cursor;
    unsigned int start = 0;
    unsigned int end;
    unsigned int token_end;
    unsigned int dot = 0xFFFFFFFFU;
    unsigned int integer_start;
    unsigned int written = 0;
    int found = 0;
    for (cursor = 0; cursor + sizeof(marker) - 1U <= size; ++cursor)
        if (memcmp(line + cursor, marker, sizeof(marker) - 1U) == 0) {
            start = cursor + sizeof(marker) - 1U;
            found = 1;
        }
    if (!found) return 0;
    end = start;
    while (end < size && line[end] != ' ' && line[end] != '\t' &&
           line[end] != '\r' && line[end] != '\n') {
        if (line[end] == '.') {
            if (dot != 0xFFFFFFFFU) return 0;
            dot = end;
        } else if (line[end] < '0' || line[end] > '9') return 0;
        ++end;
    }
    token_end = end;
    while (end < size) {
        if (line[end] != ' ' && line[end] != '\t' &&
            line[end] != '\r' && line[end] != '\n')
            return 0;
        ++end;
    }
    integer_start = start;
    while (integer_start + 1U <
               (dot == 0xFFFFFFFFU ? token_end : dot) &&
           line[integer_start] == '0')
        ++integer_start;
    while (integer_start <
           (dot == 0xFFFFFFFFU ? token_end : dot)) {
        if (written + 1U >= 64U) return 0;
        output[written++] = (char)line[integer_start++];
    }
    if (!written) output[written++] = '0';
    if (dot != 0xFFFFFFFFU) {
        unsigned int fraction_end = token_end;
        while (fraction_end > dot + 1U && line[fraction_end - 1U] == '0')
            --fraction_end;
        output[written++] = '.';
        if (fraction_end == dot + 1U) output[written++] = '0';
        else {
            unsigned int fraction = dot + 1U;
            while (fraction < fraction_end) {
                if (written + 1U >= 64U) return 0;
                output[written++] = (char)line[fraction++];
            }
        }
    } else {
        output[written++] = '.';
        output[written++] = '0';
    }
    output[written] = 0;
    return 1;
}

static int dluma_quest_authoring_parse(
    DlumaQuestAuthoringTransaction* transaction)
{
    DlumaQuestPhaseAlias* aliases;
    unsigned int alias_count;
    unsigned int cursor = 0;
    unsigned int line_number = 1;
    if (!transaction) return 0;
    aliases = (DlumaQuestPhaseAlias*)dluma_alloc(
        DLUMA_QUEST_AUTHORING_MAX_ALIASES * sizeof(*aliases));
    if (!aliases) return 0;
    memset(aliases, 0,
           DLUMA_QUEST_AUTHORING_MAX_ALIASES * sizeof(*aliases));
    alias_count = dluma_quest_editor_alias_snapshot(
        aliases, DLUMA_QUEST_AUTHORING_MAX_ALIASES);
    while (cursor < transaction->source_size) {
        unsigned int line_start = cursor;
        unsigned int line_end;
        char command[64];
        while (cursor < transaction->source_size &&
               transaction->source_bytes[cursor] != '\r' &&
               transaction->source_bytes[cursor] != '\n')
            ++cursor;
        line_end = cursor;
        if (cursor < transaction->source_size &&
            transaction->source_bytes[cursor] == '\r') {
            ++cursor;
            if (cursor < transaction->source_size &&
                transaction->source_bytes[cursor] == '\n')
                ++cursor;
        } else if (cursor < transaction->source_size &&
                   transaction->source_bytes[cursor] == '\n') {
            ++cursor;
        }
        if (dluma_quest_authoring_command(
                transaction->source_bytes + line_start,
                line_end - line_start, command,
                DLUMA_ARRAY_COUNT(command)) &&
            dluma_quest_authoring_known_command(
                command, aliases, alias_count)) {
            DlumaQuestAuthoringBinding* binding;
            DlumaQuestPhaseDiagnostic diagnostic;
            char validator_diagnostic[256];
            if (transaction->binding_count >= DLUMA_QUEST_PHASE_MAX_BINDINGS) {
                dluma_quest_authoring_error(
                    transaction, "quest source exceeds 256 custom bindings");
                dluma_free(aliases);
                return 0;
            }
            binding = &transaction->bindings[transaction->binding_count];
            memset(binding, 0, sizeof(*binding));
            dluma_str_copy(
                binding->command, DLUMA_ARRAY_COUNT(binding->command), command);
            memset(&diagnostic, 0, sizeof(diagnostic));
            binding->directive.struct_size = sizeof(binding->directive);
            if (!dluma_quest_phase_parse_directive(
                    transaction->source_bytes + line_start,
                    line_end - line_start, line_number,
                    aliases, alias_count, &binding->directive,
                    &diagnostic) ||
                !dluma_quest_authoring_wait_json(
                    transaction->source_bytes + line_start,
                    line_end - line_start, binding->wait_json)) {
                dluma_quest_authoring_error(
                    transaction,
                    diagnostic.message[0] ? diagnostic.message
                                          : "custom directive is malformed");
                dluma_free(aliases);
                return 0;
            }
            if (!dluma_quest_editor_handler_declared(
                    binding->directive.provider_mod,
                    binding->directive.handler_id,
                    binding->directive.runtime_kind)) {
                dluma_quest_authoring_error(
                    transaction,
                    "custom directive provider does not declare this handler");
                dluma_free(aliases);
                return 0;
            }
            validator_diagnostic[0] = 0;
            if (!dluma_native_mods_editor_validate_phase(
                    binding->directive.provider_mod,
                    binding->directive.handler_id,
                    binding->directive.phase_id,
                    binding->directive.source_line,
                    binding->directive.arguments,
                    binding->directive.arguments_size,
                    validator_diagnostic,
                    DLUMA_ARRAY_COUNT(validator_diagnostic))) {
                dluma_quest_authoring_error(
                    transaction,
                    validator_diagnostic[0]
                        ? validator_diagnostic
                        : "editor quest validator rejected the binding");
                dluma_free(aliases);
                return 0;
            }
            ++transaction->binding_count;
        }
        ++line_number;
    }
    dluma_free(aliases);
    transaction->custom = transaction->binding_count != 0;
    return 1;
}

static int dluma_quest_authoring_build_lowered_bytes(
    DlumaQuestAuthoringTransaction* transaction,
    char** bytes_out,
    unsigned int* size_out)
{
    char* bytes;
    unsigned int capacity;
    unsigned int cursor = 0;
    unsigned int written = 0;
    unsigned int line_number = 1;
    unsigned int binding_index = 0;
    if (!transaction || !bytes_out || !size_out ||
        !transaction->custom || !transaction->binding_count ||
        transaction->binding_count >
            (0xFFFFFFFFU - transaction->source_size - 1U) / 1024U)
        return 0;
    capacity = transaction->source_size +
        transaction->binding_count * 1024U + 1U;
    if (capacity > DLUMA_QUEST_AUTHORING_MAX_SOURCE_BYTES +
            DLUMA_QUEST_PHASE_MAX_BINDINGS * 1024U)
        return 0;
    bytes = (char*)dluma_alloc(capacity);
    if (!bytes) return 0;
    while (cursor < transaction->source_size) {
        unsigned int line_start = cursor;
        unsigned int line_end;
        unsigned int newline_end;
        unsigned int indent = 0;
        unsigned int copy_size;
        while (cursor < transaction->source_size &&
               transaction->source_bytes[cursor] != '\r' &&
               transaction->source_bytes[cursor] != '\n')
            ++cursor;
        line_end = cursor;
        if (cursor < transaction->source_size &&
            transaction->source_bytes[cursor] == '\r') {
            ++cursor;
            if (cursor < transaction->source_size &&
                transaction->source_bytes[cursor] == '\n')
                ++cursor;
        } else if (cursor < transaction->source_size &&
                   transaction->source_bytes[cursor] == '\n') {
            ++cursor;
        }
        newline_end = cursor;
        while (line_start + indent < line_end &&
               (transaction->source_bytes[line_start + indent] == ' ' ||
                transaction->source_bytes[line_start + indent] == '\t'))
            ++indent;
        if (binding_index < transaction->binding_count &&
            transaction->bindings[binding_index].directive.source_line ==
                line_number) {
            char compiler_line[1024];
            unsigned int compiler_line_size = 0;
            DlumaQuestAuthoringBinding* binding =
                &transaction->bindings[binding_index];
            if (written > capacity - indent - 1U ||
                !dluma_quest_phase_build_compiler_line(
                    binding->command, &binding->directive,
                    binding->wait_json, compiler_line,
                    DLUMA_ARRAY_COUNT(compiler_line), &compiler_line_size) ||
                compiler_line_size > capacity - written - indent - 1U) {
                dluma_free(bytes);
                return 0;
            }
            if (indent) {
                memcpy(bytes + written,
                       transaction->source_bytes + line_start, indent);
                written += indent;
            }
            memcpy(bytes + written, compiler_line, compiler_line_size);
            written += compiler_line_size;
            ++binding_index;
        } else {
            if (binding_index < transaction->binding_count &&
                transaction->bindings[binding_index].directive.source_line <
                    line_number) {
                dluma_free(bytes);
                return 0;
            }
            copy_size = line_end - line_start;
            if (copy_size > capacity - written - 1U) {
                dluma_free(bytes);
                return 0;
            }
            if (copy_size) {
                memcpy(bytes + written,
                       transaction->source_bytes + line_start, copy_size);
                written += copy_size;
            }
        }
        copy_size = newline_end - line_end;
        if (copy_size > capacity - written - 1U) {
            dluma_free(bytes);
            return 0;
        }
        if (copy_size) {
            memcpy(bytes + written,
                   transaction->source_bytes + line_end, copy_size);
            written += copy_size;
        }
        ++line_number;
    }
    if (binding_index != transaction->binding_count) {
        dluma_free(bytes);
        return 0;
    }
    bytes[written] = 0;
    *bytes_out = bytes;
    *size_out = written;
    return 1;
}

/* Keep the same file identity pinned while dropping write access. A compiler
 * reader that shares only reads cannot coexist with our original write handle. */
static int dluma_quest_authoring_lock_compiler_source(HANDLE* file)
{
    HANDLE pinned;
    HANDLE locked;
    if (!file || *file == INVALID_HANDLE_VALUE) return 0;
    pinned = ReOpenFile(*file, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, 0);
    if (pinned == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(*file);
    *file = pinned;
    locked = ReOpenFile(pinned, GENERIC_READ, FILE_SHARE_READ, 0);
    if (locked == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(pinned);
    *file = locked;
    return 1;
}

static int dluma_quest_authoring_create_lowered_source(
    DlumaQuestAuthoringTransaction* transaction)
{
    char* bytes = 0;
    unsigned int size = 0;
    unsigned int attempt;
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION information;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char number[32];
    WCHAR number_wide[32];
    BYTE* locked_bytes = 0;
    unsigned int locked_size = 0;
    char locked_sha256[DLUMA_SHA256_HEX_CAPACITY];
    int result = 0;
    memset(&information, 0, sizeof(information));
    if (!transaction ||
        !dluma_quest_authoring_build_lowered_bytes(
            transaction, &bytes, &size))
        return 0;
    for (attempt = 0; attempt < 32U; ++attempt) {
        ULONGLONG token =
            (GetTickCount64() << 16U) ^
            ((ULONGLONG)GetCurrentProcessId() << 32U) ^ attempt;
        dluma_u64_to_text(token, number, DLUMA_ARRAY_COUNT(number));
        if (!number[0] || !dluma_utf8_to_wide(
                number, number_wide, DLUMA_ARRAY_COUNT(number_wide)) ||
            wcslen(transaction->source_final) +
                wcslen(L".dlumaq..tmp.pxsl") +
                wcslen(number_wide) + 1U >
                    DLUMA_ARRAY_COUNT(transaction->lowered_path))
            break;
        dluma_wstr_copy(
            transaction->lowered_path,
            DLUMA_ARRAY_COUNT(transaction->lowered_path),
            transaction->source_final);
        dluma_wstr_append(
            transaction->lowered_path,
            DLUMA_ARRAY_COUNT(transaction->lowered_path), L".dlumaq.");
        dluma_wstr_append(
            transaction->lowered_path,
            DLUMA_ARRAY_COUNT(transaction->lowered_path), number_wide);
        dluma_wstr_append(
            transaction->lowered_path,
            DLUMA_ARRAY_COUNT(transaction->lowered_path), L".tmp.pxsl");
        file = CreateFileW(
            transaction->lowered_path,
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        if (file != INVALID_HANDLE_VALUE) break;
        transaction->lowered_path[0] = 0;
    }
    if (file == INVALID_HANDLE_VALUE || !transaction->lowered_path[0] ||
        !dluma_quest_authoring_write_all(file, bytes, size) ||
        !FlushFileBuffers(file) ||
        !dluma_quest_authoring_lock_compiler_source(&file) ||
        !dluma_quest_authoring_read_handle(
            file, DLUMA_QUEST_AUTHORING_MAX_SOURCE_BYTES +
                DLUMA_QUEST_PHASE_MAX_BINDINGS * 1024U,
            &locked_bytes, &locked_size, locked_sha256) ||
        locked_size != size || memcmp(locked_bytes, bytes, size) != 0 ||
        !GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_final_path_equal_or_under(
            final_path, transaction->project_root) ||
        !dluma_wide_to_utf8(
            dluma_quest_authoring_skip_extended(transaction->lowered_path),
            transaction->lowered_path_utf8,
            DLUMA_ARRAY_COUNT(transaction->lowered_path_utf8)))
        goto done;
    dluma_sha256_bytes_hex(bytes, size, transaction->lowered_sha256);
    transaction->lowered_file = file;
    file = INVALID_HANDLE_VALUE;
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!result && transaction->lowered_path[0]) {
        DeleteFileW(transaction->lowered_path);
        transaction->lowered_path[0] = 0;
        transaction->lowered_path_utf8[0] = 0;
    }
    if (bytes) dluma_free(bytes);
    if (locked_bytes) dluma_free(locked_bytes);
    return result;
}

static int dluma_quest_authoring_snapshot_old_output(
    DlumaQuestAuthoringTransaction* transaction)
{
    HANDLE file;
    BYTE* bytes = 0;
    unsigned int size = 0;
    if (!transaction) return 0;
    file = CreateFileW(
        transaction->output_lexical, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 1;
    transaction->had_old_output = 1U;
    if (!dluma_quest_authoring_read_handle(
            file, DLUMA_QUEST_AUTHORING_MAX_OUTPUT_BYTES,
            &bytes, &size, transaction->old_output_sha256)) {
        CloseHandle(file);
        return 0;
    }
    dluma_free(bytes);
    CloseHandle(file);
    return 1;
}

static DlumaQuestAuthoringPrepareResult dluma_quest_authoring_prepare_internal(
    const char* source_path,
    const char* output_path,
    const WCHAR* authoritative_project,
    DlumaQuestAuthoringTransaction** transaction_out)
{
    DlumaQuestAuthoringTransaction* transaction;
    WCHAR source_lexical[DLUMA_MAX_WPATH];
    DWORD attributes;
    if (!transaction_out) return DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    *transaction_out = 0;
    if (!source_path || !output_path ||
        !dluma_utf8_to_wide(
            source_path, source_lexical, DLUMA_ARRAY_COUNT(source_lexical)))
        return DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    transaction = (DlumaQuestAuthoringTransaction*)dluma_alloc(
        sizeof(*transaction));
    if (!transaction) return DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    memset(transaction, 0, sizeof(*transaction));
    transaction->source_file = INVALID_HANDLE_VALUE;
    transaction->lowered_file = INVALID_HANDLE_VALUE;
    transaction->bindings = (DlumaQuestAuthoringBinding*)dluma_alloc(
        DLUMA_QUEST_PHASE_MAX_BINDINGS * sizeof(*transaction->bindings));
    if (!transaction->bindings) {
        dluma_quest_authoring_release(transaction);
        return DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    }
    memset(transaction->bindings, 0,
           DLUMA_QUEST_PHASE_MAX_BINDINGS * sizeof(*transaction->bindings));
    if (!dluma_utf8_to_wide(
            output_path, transaction->output_lexical,
            DLUMA_ARRAY_COUNT(transaction->output_lexical))) {
        dluma_quest_authoring_error(transaction, "compiled output path is invalid");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    attributes = GetFileAttributesW(source_lexical);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        dluma_quest_authoring_error(
            transaction,
            "quest source path is missing, a directory, or a reparse point");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    transaction->source_file = CreateFileW(
        source_lexical, GENERIC_READ, FILE_SHARE_READ, 0,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (transaction->source_file == INVALID_HANDLE_VALUE) {
        dluma_quest_authoring_error(
            transaction,
            "quest source could not be locked without write/delete sharing");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!GetFileInformationByHandle(
            transaction->source_file, &transaction->source_identity) ||
        (transaction->source_identity.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        transaction->source_identity.nNumberOfLinks != 1U) {
        dluma_quest_authoring_error(
            transaction,
            "quest source file identity is invalid or ambiguous");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!dluma_get_final_path_checked(
            transaction->source_file, transaction->source_final,
            DLUMA_ARRAY_COUNT(transaction->source_final))) {
        dluma_quest_authoring_error(
            transaction,
            "quest source final path could not be resolved");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!dluma_quest_authoring_resolve_project(
            transaction->source_final, authoritative_project,
            transaction->project_root,
            DLUMA_ARRAY_COUNT(transaction->project_root))) {
        dluma_quest_authoring_error(
            transaction,
            "quest source is outside the authoritative project or the project identity is invalid");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!dluma_quest_authoring_output_parent_is_contained(
            transaction->output_lexical,
            transaction->project_root)) {
        dluma_quest_authoring_error(
            transaction,
            "compiled output parent escapes the authoritative project or is ambiguous");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!dluma_quest_authoring_read_handle(
            transaction->source_file, DLUMA_QUEST_AUTHORING_MAX_SOURCE_BYTES,
            &transaction->source_bytes, &transaction->source_size,
            transaction->source_sha256)) {
        dluma_quest_authoring_error(
            transaction, "quest source snapshot or SHA-256 failed");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (!dluma_quest_authoring_snapshot_old_output(transaction) ||
        !dluma_quest_authoring_parse(transaction)) {
        if (!transaction->diagnostic[0])
            dluma_quest_authoring_error(transaction, "quest source validation failed");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    if (transaction->custom &&
        !dluma_quest_authoring_create_lowered_source(transaction)) {
        dluma_quest_authoring_error(
            transaction,
            "loader-owned positional compiler snapshot could not be published");
        *transaction_out = transaction;
        return DLUMA_QUEST_AUTHORING_REJECT;
    }
    *transaction_out = transaction;
    return transaction->custom
        ? DLUMA_QUEST_AUTHORING_CUSTOM : DLUMA_QUEST_AUTHORING_STOCK;
}

DlumaQuestAuthoringPrepareResult dluma_quest_authoring_prepare(
    const char* source_path,
    const char* output_path,
    DlumaQuestAuthoringTransaction** transaction_out)
{
    return dluma_quest_authoring_prepare_internal(
        source_path, output_path, 0, transaction_out);
}

DlumaQuestAuthoringPrepareResult dluma_quest_authoring_prepare_for_project(
    const char* source_path,
    const char* output_path,
    const WCHAR* project_root,
    DlumaQuestAuthoringTransaction** transaction_out)
{
    if (!project_root || !project_root[0])
        return DLUMA_QUEST_AUTHORING_UNAVAILABLE;
    return dluma_quest_authoring_prepare_internal(
        source_path, output_path, project_root, transaction_out);
}

static int dluma_quest_xml_name_equal(
    const BYTE* value,
    unsigned int size,
    const char* expected)
{
    unsigned int expected_size = (unsigned int)strlen(expected);
    return size == expected_size && memcmp(value, expected, size) == 0;
}

static int dluma_quest_xml_append_codepoint(
    char* output,
    unsigned int capacity,
    unsigned int* size,
    unsigned int codepoint)
{
    if (!output || !size || !codepoint || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        return 0;
    if (codepoint <= 0x7FU) {
        if (*size + 1U >= capacity) return 0;
        output[(*size)++] = (char)codepoint;
    } else if (codepoint <= 0x7FFU) {
        if (*size + 2U >= capacity) return 0;
        output[(*size)++] = (char)(0xC0U | (codepoint >> 6U));
        output[(*size)++] = (char)(0x80U | (codepoint & 0x3FU));
    } else if (codepoint <= 0xFFFFU) {
        if (*size + 3U >= capacity) return 0;
        output[(*size)++] = (char)(0xE0U | (codepoint >> 12U));
        output[(*size)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[(*size)++] = (char)(0x80U | (codepoint & 0x3FU));
    } else {
        if (*size + 4U >= capacity) return 0;
        output[(*size)++] = (char)(0xF0U | (codepoint >> 18U));
        output[(*size)++] = (char)(0x80U | ((codepoint >> 12U) & 0x3FU));
        output[(*size)++] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[(*size)++] = (char)(0x80U | (codepoint & 0x3FU));
    }
    output[*size] = 0;
    return 1;
}

static int dluma_quest_xml_decode(
    const BYTE* raw,
    unsigned int raw_size,
    char* output,
    unsigned int capacity)
{
    unsigned int cursor = 0;
    unsigned int size = 0;
    if (!raw || !output || !capacity) return 0;
    while (cursor < raw_size) {
        unsigned int codepoint;
        if (raw[cursor] != '&') {
            if (!raw[cursor] || raw[cursor] < 0x20U ||
                size + 1U >= capacity)
                return 0;
            output[size++] = (char)raw[cursor++];
            continue;
        }
        if (cursor + 4U <= raw_size &&
            memcmp(raw + cursor, "&lt;", 4U) == 0) {
            codepoint = '<'; cursor += 4U;
        } else if (cursor + 4U <= raw_size &&
                   memcmp(raw + cursor, "&gt;", 4U) == 0) {
            codepoint = '>'; cursor += 4U;
        } else if (cursor + 5U <= raw_size &&
                   memcmp(raw + cursor, "&amp;", 5U) == 0) {
            codepoint = '&'; cursor += 5U;
        } else if (cursor + 6U <= raw_size &&
                   memcmp(raw + cursor, "&quot;", 6U) == 0) {
            codepoint = '"'; cursor += 6U;
        } else if (cursor + 6U <= raw_size &&
                   memcmp(raw + cursor, "&apos;", 6U) == 0) {
            codepoint = '\''; cursor += 6U;
        } else if (cursor + 3U < raw_size && raw[cursor + 1U] == '#') {
            unsigned int number = 0;
            unsigned int base = 10U;
            unsigned int digit_count = 0;
            cursor += 2U;
            if (cursor < raw_size &&
                (raw[cursor] == 'x' || raw[cursor] == 'X')) {
                base = 16U;
                ++cursor;
            }
            while (cursor < raw_size && raw[cursor] != ';') {
                unsigned int digit;
                BYTE value = raw[cursor++];
                if (value >= '0' && value <= '9') digit = value - '0';
                else if (base == 16U && value >= 'a' && value <= 'f')
                    digit = value - 'a' + 10U;
                else if (base == 16U && value >= 'A' && value <= 'F')
                    digit = value - 'A' + 10U;
                else return 0;
                if (digit >= base || number > (0x10FFFFU - digit) / base)
                    return 0;
                number = number * base + digit;
                ++digit_count;
            }
            if (!digit_count || cursor >= raw_size || raw[cursor++] != ';')
                return 0;
            codepoint = number;
        } else return 0;
        if (!dluma_quest_xml_append_codepoint(
                output, capacity, &size, codepoint))
            return 0;
    }
    output[size] = 0;
    return 1;
}

static int dluma_quest_xml_attribute(
    const BYTE* begin,
    const BYTE* end,
    const char* wanted,
    char* output,
    unsigned int capacity)
{
    const BYTE* cursor = begin;
    int found = 0;
    if (!begin || !end || begin > end || !wanted || !output) return 0;
    output[0] = 0;
    while (cursor < end) {
        const BYTE* name;
        const BYTE* value;
        unsigned int name_size;
        BYTE quote;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n' || *cursor == '/')) ++cursor;
        if (cursor >= end) break;
        name = cursor;
        while (cursor < end &&
               ((*cursor >= 'A' && *cursor <= 'Z') ||
                (*cursor >= 'a' && *cursor <= 'z') ||
                (*cursor >= '0' && *cursor <= '9') ||
                *cursor == '_' || *cursor == '-' || *cursor == ':')) ++cursor;
        name_size = (unsigned int)(cursor - name);
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
        if (!name_size || cursor >= end || *cursor++ != '=') return 0;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) ++cursor;
        if (cursor >= end || (*cursor != '"' && *cursor != '\'')) return 0;
        quote = *cursor++;
        value = cursor;
        while (cursor < end && *cursor != quote) ++cursor;
        if (cursor >= end) return 0;
        if (dluma_quest_xml_name_equal(name, name_size, wanted)) {
            if (found || !dluma_quest_xml_decode(
                    value, (unsigned int)(cursor - value),
                    output, capacity))
                return 0;
            found = 1;
        }
        ++cursor;
    }
    return found;
}

static int dluma_quest_parse_u32(const char* value, unsigned int* output)
{
    unsigned int result = 0;
    unsigned int cursor = 0;
    if (!value || !value[0] || !output) return 0;
    while (value[cursor]) {
        unsigned int digit;
        if (value[cursor] < '0' || value[cursor] > '9') return 0;
        digit = (unsigned int)(value[cursor++] - '0');
        if (result > (0xFFFFFFFFU - digit) / 10U) return 0;
        result = result * 10U + digit;
    }
    if (!result) return 0;
    *output = result;
    return 1;
}

static int dluma_quest_authoring_wait_attribute_matches(
    const BYTE* begin,
    const BYTE* end,
    const char* name,
    double expected)
{
    char value[64];
    double parsed = 0.0;
    if (!dluma_quest_xml_attribute(
            begin, end, name, value, DLUMA_ARRAY_COUNT(value)) ||
        !dluma_quest_phase_parse_wait_seconds(
            value, (unsigned int)strlen(value), &parsed))
        return 0;
    return parsed == expected;
}

static int dluma_quest_authoring_match_xml(
    DlumaQuestAuthoringTransaction* transaction,
    const BYTE* xml,
    unsigned int size)
{
    const BYTE* cursor = xml;
    const BYTE* end = xml + size;
    char quest_id[128];
    unsigned int index;
    int in_quest = 0;
    quest_id[0] = 0;
    if (!transaction || !xml || !size ||
        MultiByteToWideChar(CP_UTF8, 0x00000008UL,
                            (const char*)xml, (int)size, 0, 0) <= 0)
        return 0;
    while (cursor < end) {
        const BYTE* tag;
        const BYTE* tag_end;
        const BYTE* name;
        unsigned int name_size;
        BYTE quote = 0;
        int closing = 0;
        while (cursor < end && *cursor != '<') ++cursor;
        if (cursor >= end) break;
        tag = ++cursor;
        if (cursor < end && (*cursor == '!' || *cursor == '?')) {
            while (cursor < end && *cursor != '>') ++cursor;
            if (cursor < end) ++cursor;
            continue;
        }
        if (cursor < end && *cursor == '/') { closing = 1; ++cursor; }
        name = cursor;
        while (cursor < end &&
               ((*cursor >= 'A' && *cursor <= 'Z') ||
                (*cursor >= 'a' && *cursor <= 'z') ||
                (*cursor >= '0' && *cursor <= '9') ||
                *cursor == '_' || *cursor == '-' || *cursor == ':')) ++cursor;
        name_size = (unsigned int)(cursor - name);
        tag_end = cursor;
        while (tag_end < end) {
            if (quote) {
                if (*tag_end == quote) quote = 0;
            } else if (*tag_end == '"' || *tag_end == '\'') quote = *tag_end;
            else if (*tag_end == '>') break;
            ++tag_end;
        }
        if (tag_end >= end || quote || !name_size) return 0;
        if (closing && dluma_quest_xml_name_equal(name, name_size, "Quest")) {
            in_quest = 0;
            quest_id[0] = 0;
        } else if (!closing &&
                   dluma_quest_xml_name_equal(name, name_size, "Quest")) {
            if (in_quest || !dluma_quest_xml_attribute(
                    cursor, tag_end, "name", quest_id,
                    DLUMA_ARRAY_COUNT(quest_id)))
                return 0;
            in_quest = 1;
        } else if (!closing && in_quest &&
                   dluma_quest_xml_name_equal(name, name_size, "Phase")) {
            char type[64];
            char phase[128];
            char line[32];
            unsigned int source_line;
            if (dluma_quest_xml_attribute(
                    cursor, tag_end, "type", type, DLUMA_ARRAY_COUNT(type)) &&
                dluma_quest_xml_attribute(
                    cursor, tag_end, "name", phase, DLUMA_ARRAY_COUNT(phase)) &&
                dluma_quest_xml_attribute(
                    cursor, tag_end, "pxsl_line", line, DLUMA_ARRAY_COUNT(line)) &&
                dluma_stricmp(type, "wait guard") == 0 &&
                dluma_quest_parse_u32(line, &source_line)) {
                for (index = 0; index < transaction->binding_count; ++index) {
                    DlumaQuestAuthoringBinding* binding =
                        &transaction->bindings[index];
                    if (binding->directive.source_line == source_line &&
                        dluma_stricmp(binding->directive.phase_id, phase) == 0 &&
                        dluma_quest_authoring_wait_attribute_matches(
                            cursor, tag_end, "time",
                            binding->directive.wait_seconds) &&
                        dluma_quest_authoring_wait_attribute_matches(
                            cursor, tag_end, "time2p",
                            binding->directive.wait_seconds) &&
                        dluma_quest_authoring_wait_attribute_matches(
                            cursor, tag_end, "time3p",
                            binding->directive.wait_seconds) &&
                        dluma_quest_authoring_wait_attribute_matches(
                            cursor, tag_end, "time4p",
                            binding->directive.wait_seconds)) {
                        ++binding->match_count;
                        dluma_str_copy(binding->quest_id,
                                     DLUMA_ARRAY_COUNT(binding->quest_id),
                                     quest_id);
                    }
                }
            }
        }
        cursor = tag_end + 1U;
        (void)tag;
    }
    for (index = 0; index < transaction->binding_count; ++index) {
        unsigned int prior;
        DlumaQuestAuthoringBinding* binding = &transaction->bindings[index];
        if (binding->match_count != 1U || !binding->quest_id[0]) return 0;
        for (prior = 0; prior < index; ++prior)
            if (dluma_stricmp(
                    transaction->bindings[prior].quest_id,
                    binding->quest_id) == 0 &&
                dluma_stricmp(
                    transaction->bindings[prior].directive.phase_id,
                    binding->directive.phase_id) == 0)
                return 0;
    }
    return 1;
}

static void dluma_quest_json_bytes(
    DlumaQuestJsonWriter* writer,
    const void* bytes,
    unsigned int size)
{
    if (!writer || writer->failed || (size && !bytes) ||
        size > writer->capacity - writer->size) {
        if (writer) writer->failed = 1;
        return;
    }
    if (size) memcpy(writer->bytes + writer->size, bytes, size);
    writer->size += size;
}

static void dluma_quest_json_text(
    DlumaQuestJsonWriter* writer,
    const char* text)
{
    dluma_quest_json_bytes(writer, text, (unsigned int)strlen(text));
}

static void dluma_quest_json_string(
    DlumaQuestJsonWriter* writer,
    const char* value)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int cursor = 0;
    dluma_quest_json_text(writer, "\"");
    while (value && value[cursor] && !writer->failed) {
        unsigned char current = (unsigned char)value[cursor++];
        if (current == '"' || current == '\\') {
            char escaped[2] = {'\\', (char)current};
            dluma_quest_json_bytes(writer, escaped, 2U);
        } else if (current < 0x20U) {
            char escaped[6] = {'\\', 'u', '0', '0',
                               hex[current >> 4U], hex[current & 15U]};
            dluma_quest_json_bytes(writer, escaped, 6U);
        } else dluma_quest_json_bytes(writer, &current, 1U);
    }
    dluma_quest_json_text(writer, "\"");
}

static void dluma_quest_json_u32(
    DlumaQuestJsonWriter* writer,
    unsigned int value)
{
    char digits[16];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < DLUMA_ARRAY_COUNT(digits));
    while (count) dluma_quest_json_bytes(writer, &digits[--count], 1U);
}

static int dluma_quest_authoring_build_sidecar(
    DlumaQuestAuthoringTransaction* transaction,
    const WCHAR* output_final,
    const char* output_sha256,
    char** bytes_out,
    unsigned int* size_out)
{
    char source_utf8[DLUMA_MAX_WPATH * 2U];
    char output_utf8[DLUMA_MAX_WPATH * 2U];
    char* bytes;
    DlumaQuestJsonWriter writer;
    DlumaQuestAuthoringProviderDiagnostic diagnostics[
        DLUMA_QUEST_PHASE_MAX_BINDINGS];
    unsigned int diagnostic_count = 0;
    unsigned int index;
    if (!transaction || !output_final || !output_sha256 ||
        !bytes_out || !size_out ||
        !dluma_wide_to_utf8(
            transaction->source_final, source_utf8,
            DLUMA_ARRAY_COUNT(source_utf8)) ||
        !dluma_wide_to_utf8(
            output_final, output_utf8, DLUMA_ARRAY_COUNT(output_utf8)))
        return 0;
    memset(diagnostics, 0, sizeof(diagnostics));
    for (index = 0; index < transaction->binding_count; ++index) {
        const DlumaQuestAuthoringBinding* binding =
            &transaction->bindings[index];
        unsigned int existing;
        for (existing = 0; existing < diagnostic_count; ++existing)
            if (dluma_stricmp(
                    diagnostics[existing].provider_mod,
                    binding->directive.provider_mod) == 0)
                break;
        if (existing < diagnostic_count) continue;
        if (!dluma_quest_editor_provider_diagnostic(
                binding->directive.provider_mod,
                binding->directive.runtime_kind,
                diagnostics[diagnostic_count].manifest_sha256,
                diagnostics[diagnostic_count].artifact_sha256))
            continue;
        dluma_str_copy(
            diagnostics[diagnostic_count].provider_mod,
            DLUMA_ARRAY_COUNT(diagnostics[diagnostic_count].provider_mod),
            binding->directive.provider_mod);
        diagnostics[diagnostic_count].runtime_kind =
            binding->directive.runtime_kind;
        ++diagnostic_count;
    }
    for (index = 1; index < diagnostic_count; ++index) {
        DlumaQuestAuthoringProviderDiagnostic value = diagnostics[index];
        unsigned int cursor = index;
        while (cursor && dluma_stricmp(
                diagnostics[cursor - 1U].provider_mod,
                value.provider_mod) > 0) {
            diagnostics[cursor] = diagnostics[cursor - 1U];
            --cursor;
        }
        diagnostics[cursor] = value;
    }
    bytes = (char*)dluma_alloc(DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES);
    if (!bytes) return 0;
    memset(&writer, 0, sizeof(writer));
    writer.bytes = bytes;
    writer.capacity = DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES;
    dluma_quest_json_text(&writer, "{\"schema\":\"dluma.quest_phase_bindings.v1\",\"generated_by\":\"dluma\",\"source\":{\"path\":");
    dluma_quest_json_string(&writer, source_utf8);
    dluma_quest_json_text(&writer, ",\"sha256\":");
    dluma_quest_json_string(&writer, transaction->source_sha256);
    dluma_quest_json_text(&writer, "},\"output\":{\"path\":");
    dluma_quest_json_string(&writer, output_utf8);
    dluma_quest_json_text(&writer, ",\"sha256\":");
    dluma_quest_json_string(&writer, output_sha256);
    dluma_quest_json_text(&writer, "},\"bindings\":[");
    for (index = 0; index < transaction->binding_count; ++index) {
        DlumaQuestAuthoringBinding* binding = &transaction->bindings[index];
        unsigned int identity_start;
        char binding_hash[DLUMA_SHA256_HEX_CAPACITY];
        if (index) dluma_quest_json_text(&writer, ",");
        identity_start = writer.size;
        dluma_quest_json_text(&writer, "{\"quest\":");
        dluma_quest_json_string(&writer, binding->quest_id);
        dluma_quest_json_text(&writer, ",\"phase\":");
        dluma_quest_json_string(&writer, binding->directive.phase_id);
        dluma_quest_json_text(&writer, ",\"source_line\":");
        dluma_quest_json_u32(&writer, binding->directive.source_line);
        dluma_quest_json_text(&writer, ",\"runtime_kind\":");
        dluma_quest_json_string(
            &writer,
            binding->directive.runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA
                ? "lua" : "native");
        dluma_quest_json_text(&writer, ",\"provider_mod\":");
        dluma_quest_json_string(&writer, binding->directive.provider_mod);
        dluma_quest_json_text(&writer, ",\"handler\":");
        dluma_quest_json_string(&writer, binding->directive.handler_id);
        dluma_quest_json_text(&writer, ",\"contract_id\":");
        dluma_quest_json_string(&writer, binding->directive.contract_id);
        dluma_quest_json_text(&writer, ",\"arguments\":");
        dluma_quest_json_bytes(
            &writer, binding->directive.arguments,
            binding->directive.arguments_size);
        dluma_quest_json_text(&writer, ",\"wait\":");
        dluma_quest_json_text(&writer, binding->wait_json);
        dluma_quest_json_text(&writer, ",\"completion\":");
        dluma_quest_json_string(
            &writer,
            binding->directive.completion ==
                    DLUMA_QUEST_PHASE_COMPLETION_FIRE_AND_FORGET
                ? "fire_and_forget" : "stock_timed_wait");
        dluma_quest_json_text(&writer, "}");
        if (writer.failed) break;
        dluma_sha256_bytes_hex(
            writer.bytes + identity_start,
            writer.size - identity_start, binding_hash);
        --writer.size;
        dluma_quest_json_text(&writer, ",\"binding_hash\":");
        dluma_quest_json_string(&writer, binding_hash);
        dluma_quest_json_text(&writer, "}");
    }
    dluma_quest_json_text(&writer, "]");
    if (diagnostic_count) {
        dluma_quest_json_text(
            &writer, ",\"provider_hashes_diagnostic\":{");
        for (index = 0; index < diagnostic_count; ++index) {
            const DlumaQuestAuthoringProviderDiagnostic* diagnostic =
                &diagnostics[index];
            if (index) dluma_quest_json_text(&writer, ",");
            dluma_quest_json_string(&writer, diagnostic->provider_mod);
            dluma_quest_json_text(&writer, ":{\"manifest_sha256\":");
            dluma_quest_json_string(
                &writer, diagnostic->manifest_sha256);
            dluma_quest_json_text(
                &writer,
                diagnostic->runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA
                    ? ",\"content_sha256\":"
                    : ",\"module_sha256\":");
            dluma_quest_json_string(
                &writer, diagnostic->artifact_sha256);
            dluma_quest_json_text(&writer, "}");
        }
        dluma_quest_json_text(&writer, "}");
    }
    dluma_quest_json_text(&writer, "}");
    if (writer.failed || writer.size > DLUMA_QUEST_PHASE_MAX_SIDECAR_BYTES) {
        dluma_free(bytes);
        return 0;
    }
    *bytes_out = bytes;
    *size_out = writer.size;
    return 1;
}

static int dluma_quest_authoring_sidecar_path(
    const WCHAR* output_final,
    WCHAR* sidecar,
    unsigned int capacity)
{
    SIZE_T size;
    if (!output_final || !sidecar || !capacity) return 0;
    size = wcslen(output_final);
    if (size < 4U || dluma_wstricmp(output_final + size - 4U, L".xml") != 0 ||
        size - 4U + wcslen(L".dlumaq.json") + 1U > capacity)
        return 0;
    dluma_wstr_copy(sidecar, capacity, output_final);
    sidecar[size - 4U] = 0;
    dluma_wstr_append(sidecar, capacity, L".dlumaq.json");
    return 1;
}

static int dluma_quest_authoring_write_all(
    HANDLE file,
    const void* bytes,
    unsigned int size)
{
    const BYTE* cursor = (const BYTE*)bytes;
    while (size) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, size, &written, 0) || !written)
            return 0;
        cursor += written;
        size -= written;
    }
    return 1;
}

static int dluma_quest_authoring_publish(
    DlumaQuestAuthoringTransaction* transaction,
    const WCHAR* output_final,
    const char* output_sha256)
{
    WCHAR sidecar[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    char* bytes = 0;
    unsigned int size = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    char number[64];
    WCHAR wide_number[64];
    int result = 0;
    if (!dluma_quest_authoring_sidecar_path(
            output_final, sidecar, DLUMA_ARRAY_COUNT(sidecar)) ||
        !dluma_quest_authoring_build_sidecar(
            transaction, output_final, output_sha256,
            &bytes, &size))
        return 0;
    if (GetFileAttributesW(sidecar) != INVALID_FILE_ATTRIBUTES &&
        !dluma_quest_phase_sidecar_is_loader_owned(
            sidecar, transaction->project_root))
        goto done;
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), sidecar);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp-");
    dluma_u64_to_text(GetCurrentProcessId(), number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        goto done;
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), wide_number);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L"-");
    dluma_u64_to_text(GetTickCount64(), number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        goto done;
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), wide_number);
    file = CreateFileW(
        temporary, GENERIC_WRITE, 0, 0, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE ||
        !dluma_quest_authoring_write_all(file, bytes, size) ||
        !FlushFileBuffers(file))
        goto done;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(
            temporary, sidecar,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        goto done;
    result = 1;
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!result && temporary[0]) DeleteFileW(temporary);
    if (bytes) dluma_free(bytes);
    return result;
}

static int dluma_quest_authoring_remove_stale(
    DlumaQuestAuthoringTransaction* transaction,
    const WCHAR* output_final)
{
    WCHAR sidecar[DLUMA_MAX_WPATH];
    DWORD attributes;
    if (!dluma_quest_authoring_sidecar_path(
            output_final, sidecar, DLUMA_ARRAY_COUNT(sidecar)))
        return 0;
    attributes = GetFileAttributesW(sidecar);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 1;
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !dluma_quest_phase_sidecar_is_loader_owned(
            sidecar, transaction->project_root))
        return 0;
    return DeleteFileW(sidecar) != 0;
}

static void dluma_quest_authoring_remove_failed_output(
    DlumaQuestAuthoringTransaction* transaction,
    const WCHAR* output_path,
    const char* current_sha256)
{
    if (!transaction || !transaction->custom || !output_path ||
        !current_sha256)
        return;
    if (!transaction->had_old_output ||
        dluma_stricmp(
            transaction->old_output_sha256, current_sha256) != 0)
        (void)DeleteFileW(output_path);
}

int dluma_quest_authoring_finish(
    DlumaQuestAuthoringTransaction* transaction)
{
    HANDLE output = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION source_after;
    BY_HANDLE_FILE_INFORMATION output_info;
    BYTE* output_bytes = 0;
    BYTE* source_after_bytes = 0;
    unsigned int source_after_size = 0;
    unsigned int output_size = 0;
    WCHAR output_final[DLUMA_MAX_WPATH];
    char output_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char source_after_sha256[DLUMA_SHA256_HEX_CAPACITY];
    int result = 0;
    memset(&source_after, 0, sizeof(source_after));
    memset(&output_info, 0, sizeof(output_info));
    output_final[0] = 0;
    output_sha256[0] = 0;
    source_after_sha256[0] = 0;
    if (!transaction || transaction->source_file == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(
            transaction->source_file, &source_after) ||
        source_after.dwVolumeSerialNumber !=
            transaction->source_identity.dwVolumeSerialNumber ||
        source_after.nFileIndexHigh !=
            transaction->source_identity.nFileIndexHigh ||
        source_after.nFileIndexLow !=
            transaction->source_identity.nFileIndexLow ||
        source_after.nFileSizeHigh !=
            transaction->source_identity.nFileSizeHigh ||
        source_after.nFileSizeLow !=
            transaction->source_identity.nFileSizeLow ||
        !dluma_quest_authoring_read_handle(
            transaction->source_file,
            DLUMA_QUEST_AUTHORING_MAX_SOURCE_BYTES,
            &source_after_bytes, &source_after_size,
            source_after_sha256) ||
        source_after_size != transaction->source_size ||
        dluma_stricmp(
            source_after_sha256,
            transaction->source_sha256) != 0) {
        dluma_quest_authoring_error(transaction, "quest source identity changed");
        if (source_after_bytes) dluma_free(source_after_bytes);
        return 0;
    }
    dluma_free(source_after_bytes);
    source_after_bytes = 0;
    output = CreateFileW(
        transaction->output_lexical, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(output, &output_info) ||
        (output_info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        output_info.nNumberOfLinks != 1U ||
        !dluma_get_final_path_checked(
            output, output_final, DLUMA_ARRAY_COUNT(output_final)) ||
        !dluma_final_path_equal_or_under(
            output_final, transaction->project_root) ||
        !dluma_quest_authoring_read_handle(
            output, DLUMA_QUEST_AUTHORING_MAX_OUTPUT_BYTES,
            &output_bytes, &output_size, output_sha256)) {
        dluma_quest_authoring_error(
            transaction, "compiled quest XML could not be verified");
        goto done;
    }
    if (!transaction->custom) {
        result = dluma_quest_authoring_remove_stale(transaction, output_final);
        if (!result)
            dluma_quest_authoring_error(
                transaction, "loader-owned stale sidecar could not be removed");
        goto done;
    }
    if (!dluma_quest_authoring_match_xml(
            transaction, output_bytes, output_size)) {
        dluma_quest_authoring_error(
            transaction,
            "custom directives did not match exactly one stock wait-guard phase");
        goto done;
    }
    if (!dluma_quest_authoring_publish(
            transaction, output_final, output_sha256)) {
        dluma_quest_authoring_error(
            transaction, "quest phase sidecar publication failed");
        goto done;
    }
    result = 1;
done:
    if (source_after_bytes) dluma_free(source_after_bytes);
    if (output_bytes) dluma_free(output_bytes);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (!result && transaction->custom && output_final[0])
        dluma_quest_authoring_remove_failed_output(
            transaction, output_final, output_sha256);
    return result;
}

void dluma_quest_authoring_release(
    DlumaQuestAuthoringTransaction* transaction)
{
    if (!transaction) return;
    if (transaction->source_file != INVALID_HANDLE_VALUE)
        CloseHandle(transaction->source_file);
    if (transaction->lowered_file != INVALID_HANDLE_VALUE) {
        CloseHandle(transaction->lowered_file);
        transaction->lowered_file = INVALID_HANDLE_VALUE;
    }
    if (transaction->lowered_path[0])
        (void)DeleteFileW(transaction->lowered_path);
    if (transaction->source_bytes) dluma_free(transaction->source_bytes);
    if (transaction->bindings) dluma_free(transaction->bindings);
    memset(transaction, 0, sizeof(*transaction));
    dluma_free(transaction);
}

const char* dluma_quest_authoring_diagnostic(
    const DlumaQuestAuthoringTransaction* transaction)
{
    return transaction && transaction->diagnostic[0]
        ? transaction->diagnostic : "quest authoring unavailable";
}

const char* dluma_quest_authoring_compiler_source(
    const DlumaQuestAuthoringTransaction* transaction)
{
    return transaction && transaction->custom &&
        transaction->lowered_file != INVALID_HANDLE_VALUE &&
        transaction->lowered_path_utf8[0]
        ? transaction->lowered_path_utf8 : 0;
}

const char* dluma_quest_authoring_compiler_source_sha256(
    const DlumaQuestAuthoringTransaction* transaction)
{
    return transaction && transaction->custom &&
        transaction->lowered_sha256[0]
        ? transaction->lowered_sha256 : 0;
}
