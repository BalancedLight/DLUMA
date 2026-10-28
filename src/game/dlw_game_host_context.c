#include "dlw_game_host_context.h"
#include "dlw_player_service.h"
#include "dlw_media_service.h"
#include "dlw_quest_phase_service.h"
#include "dlw_quest_hud_service.h"

#include "../dlw_native_gameplay.h"
#include "../dlw_quest_hud.h"
#include "../dlw_path_security.h"

#include <stdint.h>
#include <string.h>

#ifndef CREATE_NEW
#define CREATE_NEW 1UL
#endif
#ifndef FILE_FLAG_WRITE_THROUGH
#define FILE_FLAG_WRITE_THROUGH 0x80000000UL
#endif

#define DLUMA_GAME_HOST_CONTEXT_VERSION 1U
#define DLUMA_GAME_HOST_STATE_MAGIC 0x48474c44UL
#define DLUMA_GAME_HOST_STORAGE_KEY_LIMIT 128U
#define DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT (4U * 1024U)
#define DLUMA_GAME_HOST_STORAGE_TOTAL_LIMIT (64U * 1024U)
static const WCHAR dluma_game_host_storage_extension[] = L".dlumas";
#define DLUMA_GAME_HOST_COMPANION_LIMIT (512U * 1024U * 1024U)
#define DLUMA_GAME_HOST_COMPANION_ENTRY_LIMIT (1024U * 1024U)
#define DLUMA_GAME_HOST_SCRATCH_CAPACITY \
    (DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 1U)

#define DLUMA_GAME_EVENT_UPDATE 5U
#define DLUMA_GAME_EVENT_TIMER 6U
#define DLUMA_GAME_EVENT_SENSOR_ENTER 7U
#define DLUMA_GAME_EVENT_SENSOR_LEAVE 8U
#define DLUMA_GAME_EVENT_INTERACTION_BEGIN 9U
#define DLUMA_GAME_EVENT_CONFIGURATION_CHANGED 10U

#define DLUMA_GAME_TOKEN_TIMER 1U
#define DLUMA_GAME_TOKEN_SUBSCRIPTION 2U
#define DLUMA_GAME_TOKEN_COMPANION 3U

typedef struct DlumaGameHostTimer {
    uint64_t callback_id;
    uint64_t due_ms;
    uint64_t interval_ms;
    uint32_t generation;
    unsigned char active;
    unsigned char repeating;
    unsigned char reserved[2];
} DlumaGameHostTimer;

typedef struct DlumaGameHostSubscription {
    uint64_t callback_id;
    uint64_t native_registration_token;
    uint32_t generation;
    uint32_t event_kind;
    uint32_t native_kind;
    unsigned char active;
    unsigned char native_registered;
    unsigned char native_exit_suspended;
    unsigned char reserved[1];
    char name[96];
} DlumaGameHostSubscription;

typedef struct DlumaGameHostCompanion {
    DlumaCompanionArchive* archive;
    uint64_t size;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    uint32_t generation;
    unsigned char active;
    unsigned char retained;
    unsigned char reserved[2];
} DlumaGameHostCompanion;

typedef struct DlumaGameHostState {
    uint32_t magic;
    DlumaModOwner* owner;
    const DlumaModPackageSnapshot* package;
    DlumaModConfigIdentity config_identity;
    DlumaLuaConfigSchema* config_schema;
    DlumaGameHostTimer* timers;
    DlumaGameHostSubscription* subscriptions;
    DlumaGameHostCompanion* companions;
    DlumaGameHostQuestHudOverride quest_hud[DLUMA_GAME_HOST_QUEST_HUD_LIMIT];
    uint32_t timer_capacity;
    uint32_t subscription_capacity;
    uint32_t companion_capacity;
    uint32_t next_generation;
    uint32_t map_exit_suspended;
    void* expert_user;
    DlumaGameHostEventSinkFn event_sink;
    void* event_user;
    char storage_key[DLUMA_SHA256_HEX_CAPACITY];
    char scratch[DLUMA_GAME_HOST_SCRATCH_CAPACITY];
} DlumaGameHostState;

static volatile LONG g_dluma_game_host_token_generation;

static DlumaGameHostState* dluma_game_host_state(
    DlumaGameHostContext* context)
{
    DlumaGameHostState* state;
    if (!context ||
        context->struct_size < sizeof(*context) ||
        context->struct_version !=
            DLUMA_GAME_HOST_CONTEXT_VERSION ||
        !context->state)
        return 0;
    state = (DlumaGameHostState*)context->state;
    return state->magic == DLUMA_GAME_HOST_STATE_MAGIC
        ? state : 0;
}

static int dluma_game_host_copy_view(
    char* output,
    unsigned int capacity,
    DlumaStringView value)
{
    if (!output || !capacity ||
        value.size >= capacity ||
        (!value.data && value.size))
        return 0;
    if (value.size) {
        uint64_t index;
        for (index = 0; index < value.size; ++index)
            if (!value.data[index])
                return 0;
        memcpy(output, value.data, (SIZE_T)value.size);
    }
    output[value.size] = 0;
    return 1;
}

static int dluma_game_host_string_equal(
    const char* left,
    const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (left[index] != right[index])
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static uint32_t dluma_game_host_next_generation(
    DlumaGameHostState* state)
{
    uint32_t generation;
    if (!state) return 0;
    generation = (uint32_t)dluma_atomic_increment(
        &g_dluma_game_host_token_generation) & 0x00FFFFFFU;
    if (!generation)
        generation = (uint32_t)dluma_atomic_increment(
            &g_dluma_game_host_token_generation) & 0x00FFFFFFU;
    state->next_generation = generation;
    return generation;
}

static uint64_t dluma_game_host_token(
    uint32_t kind,
    uint32_t generation,
    uint32_t index)
{
    return ((uint64_t)(kind & 0xFFU) << 56U) |
        ((uint64_t)(generation & 0x00FFFFFFU) << 32U) |
        ((uint64_t)index + 1U);
}

static int dluma_game_host_decode_token(
    uint64_t token,
    uint32_t expected_kind,
    uint32_t capacity,
    uint32_t* index,
    uint32_t* generation)
{
    uint32_t low = (uint32_t)(token & 0xFFFFFFFFULL);
    uint32_t kind = (uint32_t)(token >> 56U);
    uint32_t token_generation =
        (uint32_t)((token >> 32U) & 0x00FFFFFFULL);
    if (kind != expected_kind || !low ||
        low > capacity || !token_generation ||
        !index || !generation)
        return 0;
    *index = low - 1U;
    *generation = token_generation;
    return 1;
}

static int dluma_game_host_storage_key_valid(
    const char* value)
{
    unsigned int index;
    if (!value || strlen(value) != 64U) return 0;
    for (index = 0; index < 64U; ++index)
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'A' && value[index] <= 'F') ||
              (value[index] >= 'a' && value[index] <= 'f')))
            return 0;
    return 1;
}

static int dluma_game_host_user_key_valid(
    const char* value)
{
    unsigned int index;
    unsigned int length;
    if (!value || !value[0]) return 0;
    length = (unsigned int)strlen(value);
    if (length > DLUMA_GAME_HOST_STORAGE_KEY_LIMIT ||
        value[length - 1U] == '.' ||
        value[length - 1U] == ' ')
        return 0;
    if ((length == 1U && value[0] == '.') ||
        (length == 2U && value[0] == '.' &&
         value[1] == '.'))
        return 0;
    for (index = 0; index < length; ++index) {
        char character = value[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '-' ||
              character == '.'))
            return 0;
    }
    return 1;
}

static int dluma_game_host_wpath_under(
    const WCHAR* child,
    const WCHAR* parent)
{
    SIZE_T index;
    SIZE_T parent_length;
    if (!child || !parent) return 0;
    parent_length = wcslen(parent);
    if (!parent_length) return 0;
    for (index = 0; index < parent_length; ++index)
        if (!child[index] ||
            dluma_wascii_lower(child[index]) !=
                dluma_wascii_lower(parent[index]))
            return 0;
    return child[parent_length] == 0 ||
        child[parent_length] == L'\\' ||
        child[parent_length] == L'/';
}

static int dluma_game_host_storage_paths(
    DlumaGameHostState* state,
    const char* key,
    WCHAR* directory,
    unsigned int directory_capacity,
    WCHAR* path,
    unsigned int path_capacity)
{
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    WCHAR data_root[DLUMA_MAX_WPATH];
    WCHAR storage_key[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR file_name[96];
    HANDLE directory_handle;
    HANDLE data_handle;
    HANDLE module_handle;
    BY_HANDLE_FILE_INFORMATION information;
    WCHAR final_directory[DLUMA_MAX_WPATH];
    WCHAR final_data[DLUMA_MAX_WPATH];
    WCHAR final_module[DLUMA_MAX_WPATH];
    if (!state ||
        !dluma_game_host_storage_key_valid(
            state->storage_key) ||
        !dluma_game_host_user_key_valid(key) ||
        !directory || !directory_capacity ||
        !path || !path_capacity ||
        !dluma_utf8_to_wide(
            state->storage_key,
            storage_key,
            DLUMA_ARRAY_COUNT(storage_key)) ||
        !dluma_utf8_to_wide(
            key, file_name,
            DLUMA_ARRAY_COUNT(file_name)))
        return 0;
    dluma_path_join(
        runtime_root, DLUMA_ARRAY_COUNT(runtime_root),
        g_dluma.module_dir, L"dluma_runtime");
    dluma_path_join(
        data_root, DLUMA_ARRAY_COUNT(data_root),
        runtime_root, L"mod_data");
    dluma_path_join(
        directory, directory_capacity,
        data_root, storage_key);
    if (!directory[0] ||
        !dluma_mkdirs(directory))
        return 0;
    module_handle = CreateFileW(
        g_dluma.module_dir,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    data_handle = CreateFileW(
        data_root,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    directory_handle = CreateFileW(
        directory,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (module_handle == INVALID_HANDLE_VALUE ||
        data_handle == INVALID_HANDLE_VALUE ||
        directory_handle == INVALID_HANDLE_VALUE) {
        if (module_handle != INVALID_HANDLE_VALUE)
            CloseHandle(module_handle);
        if (data_handle != INVALID_HANDLE_VALUE)
            CloseHandle(data_handle);
        if (directory_handle != INVALID_HANDLE_VALUE)
            CloseHandle(directory_handle);
        return 0;
    }
    memset(&information, 0, sizeof(information));
    if (!dluma_get_final_path_checked(
            module_handle,
            final_module,
            DLUMA_ARRAY_COUNT(final_module)) ||
        !GetFileInformationByHandle(
            data_handle, &information) ||
        !(information.dwFileAttributes &
            FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes &
            FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            data_handle,
            final_data,
            DLUMA_ARRAY_COUNT(final_data)) ||
        !dluma_game_host_wpath_under(
            final_data, final_module) ||
        !GetFileInformationByHandle(
            directory_handle, &information) ||
        !(information.dwFileAttributes &
            FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes &
            FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory_handle,
            final_directory,
            DLUMA_ARRAY_COUNT(final_directory)) ||
        !dluma_game_host_wpath_under(
            final_directory, final_data)) {
        CloseHandle(module_handle);
        CloseHandle(data_handle);
        CloseHandle(directory_handle);
        return 0;
    }
    CloseHandle(module_handle);
    CloseHandle(data_handle);
    CloseHandle(directory_handle);
    dluma_wstr_append(
        file_name, DLUMA_ARRAY_COUNT(file_name),
        dluma_game_host_storage_extension);
    dluma_path_join(
        path, path_capacity,
        directory, file_name);
    return path[0] &&
        final_directory[0];
}

static HANDLE dluma_game_host_open_storage_file(
    const WCHAR* path,
    const WCHAR* directory,
    int write,
    uint64_t maximum,
    uint64_t* size_out)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;
    LARGE_INTEGER size;
    WCHAR final_file[DLUMA_MAX_WPATH];
    WCHAR final_directory[DLUMA_MAX_WPATH];
    HANDLE directory_handle;
    if (size_out) *size_out = 0;
    directory_handle = CreateFileW(
        directory,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (directory_handle == INVALID_HANDLE_VALUE ||
        !dluma_get_final_path_checked(
            directory_handle,
            final_directory,
            DLUMA_ARRAY_COUNT(final_directory))) {
        if (directory_handle != INVALID_HANDLE_VALUE)
            CloseHandle(directory_handle);
        return INVALID_HANDLE_VALUE;
    }
    CloseHandle(directory_handle);
    file = CreateFileW(
        path,
        write ? GENERIC_WRITE : GENERIC_READ,
        write ? 0 : FILE_SHARE_READ,
        0,
        write ? CREATE_NEW : OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_OPEN_REPARSE_POINT |
            (write ? FILE_FLAG_WRITE_THROUGH : 0U),
        0);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(file, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY |
             FILE_ATTRIBUTE_REPARSE_POINT)) ||
        information.nNumberOfLinks != 1U ||
        !GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 ||
        (uint64_t)size.QuadPart > maximum ||
        !dluma_get_final_path_checked(
            file, final_file,
            DLUMA_ARRAY_COUNT(final_file)) ||
        !dluma_game_host_wpath_under(
            final_file, final_directory)) {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    if (size_out)
        *size_out = (uint64_t)size.QuadPart;
    return file;
}

static uint64_t dluma_game_host_storage_usage(
    const WCHAR* directory,
    int* valid)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    uint64_t total = 0;
    if (valid) *valid = 0;
    dluma_path_join(
        pattern, DLUMA_ARRAY_COUNT(pattern),
        directory, L"*");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            if (valid) *valid = 1;
            return 0;
        }
        return 0;
    }
    do {
        uint64_t size = 0;
        HANDLE file;
        SIZE_T length;
        if ((data.cFileName[0] == L'.' &&
             !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' &&
             data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (data.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY |
             FILE_ATTRIBUTE_REPARSE_POINT)) {
            FindClose(find);
            return 0;
        }
        length = wcslen(data.cFileName);
        if (length <= DLUMA_ARRAY_COUNT(dluma_game_host_storage_extension) - 1U ||
            dluma_wstricmp(
                data.cFileName + length - (DLUMA_ARRAY_COUNT(dluma_game_host_storage_extension) - 1U),
                dluma_game_host_storage_extension) != 0) {
            FindClose(find);
            return 0;
        }
        dluma_path_join(
            path, DLUMA_ARRAY_COUNT(path),
            directory, data.cFileName);
        file = dluma_game_host_open_storage_file(
            path, directory, 0,
            DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
            &size);
        if (file == INVALID_HANDLE_VALUE ||
            UINT64_MAX - total < size) {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            FindClose(find);
            return 0;
        }
        CloseHandle(file);
        total += size;
    } while (FindNextFileW(find, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        FindClose(find);
        return 0;
    }
    FindClose(find);
    if (valid) *valid = 1;
    return total;
}

static DlumaStatus dluma_game_host_scalar_copy(
    DlumaGameHostState* state,
    const DlumaScalar* source,
    DlumaScalar* output)
{
    if (!state || !source || !output)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (source->kind > DLUMA_SCALAR_STRING ||
        (source->kind == DLUMA_SCALAR_BOOLEAN &&
         source->integer_value != 0 &&
         source->integer_value != 1) ||
        (source->kind == DLUMA_SCALAR_NUMBER &&
         (!(source->number_value ==
                source->number_value) ||
          source->number_value >
              1.7976931348623157e+308 ||
          source->number_value <
              -1.7976931348623157e+308)))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    *output = *source;
    if (source->kind == DLUMA_SCALAR_STRING) {
        uint64_t index;
        if (source->string_value.size >=
                DLUMA_GAME_HOST_SCRATCH_CAPACITY ||
            (!source->string_value.data &&
             source->string_value.size))
            return DLUMA_STATUS_LIMIT_EXCEEDED;
        for (index = 0;
             index < source->string_value.size;
             ++index)
            if (!source->string_value.data[index])
                return DLUMA_STATUS_INVALID_ARGUMENT;
        if (source->string_value.size)
            memcpy(
                state->scratch,
                source->string_value.data,
                (SIZE_T)source->string_value.size);
        state->scratch[source->string_value.size] = 0;
        output->string_value.data = state->scratch;
        output->string_value.size =
            source->string_value.size;
    }
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_config_get(
    DlumaGameHostState* state,
    const DlumaConfigGetRequest* request,
    DlumaScalarResult* result)
{
    char key[DLUMA_LUA_CONFIG_IDENTIFIER_CAPACITY];
    DlumaLuaConfigValue value;
    unsigned int setting_index;
    int has_override = 0;
    char error[96];
    if (!state || !state->config_schema ||
        !state->config_identity.config_available ||
        !dluma_game_host_copy_view(
            key, DLUMA_ARRAY_COUNT(key),
            request->key) ||
        !dluma_lua_config_find_setting(
            state->config_schema,
            key,
            &setting_index))
        return DLUMA_STATUS_NOT_FOUND;
    memset(&value, 0, sizeof(value));
    error[0] = 0;
    if (!dluma_mod_config_read(
            &state->config_identity,
            state->config_schema,
            setting_index,
            &value,
            &has_override,
            error,
            DLUMA_ARRAY_COUNT(error)))
        return dluma_stricmp(
                   error, "config_schema_stale") == 0
            ? DLUMA_STATUS_CHANGED_SINCE_APPROVAL
            : DLUMA_STATUS_INTERNAL_ERROR;
    memset(&result->value, 0, sizeof(result->value));
    if (value.kind == DLUMA_LUA_CONFIG_BOOLEAN) {
        result->value.kind = DLUMA_SCALAR_BOOLEAN;
        result->value.integer_value =
            value.integer ? 1 : 0;
    } else if (value.kind == DLUMA_LUA_CONFIG_INTEGER) {
        result->value.kind = DLUMA_SCALAR_INTEGER;
        result->value.integer_value =
            (int64_t)value.integer;
    } else if (value.kind == DLUMA_LUA_CONFIG_ENUM ||
               value.kind == DLUMA_LUA_CONFIG_TEXT) {
        SIZE_T length = strlen(value.text);
        if (length >= sizeof(state->scratch))
            return DLUMA_STATUS_LIMIT_EXCEEDED;
        memcpy(state->scratch, value.text, length + 1U);
        result->value.kind = DLUMA_SCALAR_STRING;
        result->value.string_value.data =
            state->scratch;
        result->value.string_value.size =
            (uint64_t)length;
    } else {
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    (void)has_override;
    return DLUMA_STATUS_OK;
}

static int dluma_game_host_storage_encode(
    const DlumaScalar* value,
    unsigned char* output,
    unsigned int capacity,
    unsigned int* size_out)
{
    uint64_t payload_size;
    unsigned int total;
    if (!value || !output || capacity < 16U ||
        !size_out)
        return 0;
    if (value->kind == DLUMA_SCALAR_BOOLEAN) {
        if (value->integer_value != 0 &&
            value->integer_value != 1)
            return 0;
        payload_size = 1U;
    } else if (value->kind == DLUMA_SCALAR_INTEGER)
        payload_size = 8U;
    else if (value->kind == DLUMA_SCALAR_NUMBER) {
        if (!(value->number_value == value->number_value) ||
            value->number_value >
                1.7976931348623157e+308 ||
            value->number_value <
                -1.7976931348623157e+308)
            return 0;
        payload_size = 8U;
    } else if (value->kind == DLUMA_SCALAR_STRING) {
        uint64_t index;
        if (!value->string_value.data &&
            value->string_value.size)
            return 0;
        for (index = 0;
             index < value->string_value.size;
             ++index)
            if (!value->string_value.data[index])
                return 0;
        payload_size = value->string_value.size;
    } else
        return 0;
    if (payload_size >
            DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT ||
        payload_size > UINT32_MAX - 16U)
        return 0;
    total = (unsigned int)payload_size + 16U;
    if (total > capacity)
        return 0;
    memset(output, 0, total);
    output[0] = 'D';
    output[1] = 'L';
    output[2] = 'W';
    output[3] = 'S';
    output[4] = 1U;
    output[5] = (unsigned char)value->kind;
    memcpy(output + 8U, &payload_size, sizeof(payload_size));
    if (value->kind == DLUMA_SCALAR_BOOLEAN) {
        output[16] =
            value->integer_value ? 1U : 0U;
    } else if (value->kind == DLUMA_SCALAR_INTEGER) {
        memcpy(
            output + 16U,
            &value->integer_value,
            sizeof(value->integer_value));
    } else if (value->kind == DLUMA_SCALAR_NUMBER) {
        memcpy(
            output + 16U,
            &value->number_value,
            sizeof(value->number_value));
    } else if (payload_size) {
        memcpy(
            output + 16U,
            value->string_value.data,
            (SIZE_T)payload_size);
    }
    *size_out = total;
    return 1;
}

static DlumaStatus dluma_game_host_storage_decode(
    DlumaGameHostState* state,
    const unsigned char* bytes,
    unsigned int size,
    DlumaScalar* output)
{
    uint64_t payload_size = 0;
    uint32_t kind;
    if (!state || !bytes || size < 16U ||
        !output ||
        bytes[0] != 'D' || bytes[1] != 'L' ||
        bytes[2] != 'W' || bytes[3] != 'S' ||
        bytes[4] != 1U)
        return DLUMA_STATUS_INTERNAL_ERROR;
    kind = bytes[5];
    memcpy(
        &payload_size,
        bytes + 8U,
        sizeof(payload_size));
    if (payload_size != (uint64_t)size - 16U)
        return DLUMA_STATUS_INTERNAL_ERROR;
    memset(output, 0, sizeof(*output));
    output->kind = kind;
    if (kind == DLUMA_SCALAR_BOOLEAN &&
        payload_size == 1U) {
        if (bytes[16] > 1U)
            return DLUMA_STATUS_INTERNAL_ERROR;
        output->integer_value = bytes[16];
    } else if (kind == DLUMA_SCALAR_INTEGER &&
               payload_size == 8U) {
        memcpy(
            &output->integer_value,
            bytes + 16U,
            sizeof(output->integer_value));
    } else if (kind == DLUMA_SCALAR_NUMBER &&
               payload_size == 8U) {
        memcpy(
            &output->number_value,
            bytes + 16U,
            sizeof(output->number_value));
        if (output->number_value !=
                output->number_value ||
            output->number_value >
                1.7976931348623157e+308 ||
            output->number_value <
                -1.7976931348623157e+308)
            return DLUMA_STATUS_INTERNAL_ERROR;
    } else if (kind == DLUMA_SCALAR_STRING &&
               payload_size <
                   DLUMA_GAME_HOST_SCRATCH_CAPACITY) {
        if (payload_size)
            memcpy(
                state->scratch,
                bytes + 16U,
                (SIZE_T)payload_size);
        state->scratch[payload_size] = 0;
        output->string_value.data =
            state->scratch;
        output->string_value.size =
            payload_size;
    } else {
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_storage_get(
    DlumaGameHostState* state,
    const DlumaStorageGetRequest* request,
    DlumaScalarResult* result)
{
    char key[DLUMA_GAME_HOST_STORAGE_KEY_LIMIT + 1U];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    HANDLE file;
    uint64_t size = 0;
    unsigned char* bytes;
    DWORD read_size = 0;
    DlumaStatus status;
    if (!state ||
        !dluma_game_host_copy_view(
            key, DLUMA_ARRAY_COUNT(key),
            request->key) ||
        !dluma_game_host_storage_paths(
            state, key,
            directory, DLUMA_ARRAY_COUNT(directory),
            path, DLUMA_ARRAY_COUNT(path)))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    file = dluma_game_host_open_storage_file(
        path, directory, 0,
        DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
        &size);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_PATH_NOT_FOUND)
            return DLUMA_STATUS_INTERNAL_ERROR;
        if (!request->has_fallback) {
            memset(&result->value, 0, sizeof(result->value));
            result->value.kind = DLUMA_SCALAR_NONE;
            return DLUMA_STATUS_OK;
        }
        return dluma_game_host_scalar_copy(
            state, &request->fallback,
            &result->value);
    }
    if (!size || size >
            DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U) {
        CloseHandle(file);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    bytes = (unsigned char*)dluma_alloc((SIZE_T)size);
    if (!bytes ||
        !ReadFile(
            file, bytes, (DWORD)size,
            &read_size, 0) ||
        read_size != (DWORD)size) {
        CloseHandle(file);
        dluma_free(bytes);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    CloseHandle(file);
    status = dluma_game_host_storage_decode(
        state, bytes, (unsigned int)size,
        &result->value);
    dluma_free(bytes);
    return status;
}

static DlumaStatus dluma_game_host_storage_set(
    DlumaGameHostState* state,
    const DlumaStorageSetRequest* request,
    DlumaBoolResult* result)
{
    char key[DLUMA_GAME_HOST_STORAGE_KEY_LIMIT + 1U];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    char suffix[32];
    WCHAR suffix_wide[32];
    unsigned char* bytes;
    unsigned int encoded_size = 0;
    uint64_t old_size = 0;
    uint64_t usage;
    int usage_valid = 0;
    HANDLE old_file;
    HANDLE file;
    DWORD written = 0;
    DWORD attributes;
    if (!state ||
        !dluma_game_host_copy_view(
            key, DLUMA_ARRAY_COUNT(key),
            request->key) ||
        !dluma_game_host_storage_paths(
            state, key,
            directory, DLUMA_ARRAY_COUNT(directory),
            path, DLUMA_ARRAY_COUNT(path)))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    bytes = (unsigned char*)dluma_alloc(
        DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U);
    if (!bytes ||
        !dluma_game_host_storage_encode(
            &request->value,
            bytes,
            DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
            &encoded_size)) {
        dluma_free(bytes);
        return DLUMA_STATUS_INVALID_ARGUMENT;
    }
    old_file = dluma_game_host_open_storage_file(
        path, directory, 0,
        DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
        &old_size);
    if (old_file != INVALID_HANDLE_VALUE)
        CloseHandle(old_file);
    else {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND &&
            error != ERROR_PATH_NOT_FOUND) {
            dluma_free(bytes);
            return DLUMA_STATUS_INTERNAL_ERROR;
        }
        old_size = 0;
    }
    usage = dluma_game_host_storage_usage(
        directory, &usage_valid);
    if (!usage_valid || usage < old_size ||
        usage - old_size >
            DLUMA_GAME_HOST_STORAGE_TOTAL_LIMIT ||
        encoded_size >
            DLUMA_GAME_HOST_STORAGE_TOTAL_LIMIT -
                (usage - old_size)) {
        dluma_free(bytes);
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    dluma_wstr_copy(
        temporary, DLUMA_ARRAY_COUNT(temporary),
        path);
    suffix[0] = 0;
    dluma_str_append(
        suffix, DLUMA_ARRAY_COUNT(suffix), ".");
    dluma_u64_to_text(
        GetTickCount64() ^ (ULONGLONG)GetCurrentThreadId(),
        suffix + strlen(suffix),
        (unsigned int)(DLUMA_ARRAY_COUNT(suffix) -
            strlen(suffix)));
    dluma_str_append(
        suffix, DLUMA_ARRAY_COUNT(suffix), ".tmp");
    if (!dluma_utf8_to_wide(
            suffix, suffix_wide,
            DLUMA_ARRAY_COUNT(suffix_wide))) {
        dluma_free(bytes);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    dluma_wstr_append(
        temporary,
        DLUMA_ARRAY_COUNT(temporary),
        suffix_wide);
    file = dluma_game_host_open_storage_file(
        temporary, directory, 1,
        DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
        0);
    if (file == INVALID_HANDLE_VALUE ||
        !WriteFile(
            file, bytes, encoded_size,
            &written, 0) ||
        written != encoded_size ||
        !FlushFileBuffers(file)) {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        DeleteFileW(temporary);
        dluma_free(bytes);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    CloseHandle(file);
    dluma_free(bytes);
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes &
            (FILE_ATTRIBUTE_DIRECTORY |
             FILE_ATTRIBUTE_REPARSE_POINT))) {
        DeleteFileW(temporary);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    if (!MoveFileExW(
            temporary, path,
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    result->value = 1U;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_storage_delete(
    DlumaGameHostState* state,
    const DlumaConfigGetRequest* request,
    DlumaBoolResult* result)
{
    char key[DLUMA_GAME_HOST_STORAGE_KEY_LIMIT + 1U];
    WCHAR directory[DLUMA_MAX_WPATH];
    WCHAR path[DLUMA_MAX_WPATH];
    HANDLE file;
    uint64_t size = 0;
    if (!state ||
        !dluma_game_host_copy_view(
            key, DLUMA_ARRAY_COUNT(key),
            request->key) ||
        !dluma_game_host_storage_paths(
            state, key,
            directory, DLUMA_ARRAY_COUNT(directory),
            path, DLUMA_ARRAY_COUNT(path)))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    file = dluma_game_host_open_storage_file(
        path, directory, 0,
        DLUMA_GAME_HOST_STORAGE_VALUE_LIMIT + 16U,
        &size);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND) {
            result->value = 0U;
            return DLUMA_STATUS_OK;
        }
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
    CloseHandle(file);
    if (!DeleteFileW(path))
        return DLUMA_STATUS_INTERNAL_ERROR;
    result->value = 1U;
    (void)size;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_timer_add(
    DlumaGameHostState* state,
    const DlumaTimerRequest* request,
    DlumaTokenResult* result,
    int repeating)
{
    uint32_t index;
    uint32_t generation;
    uint64_t interval;
    if (!state || !state->owner ||
        !request || !result ||
        !(request->seconds > 0.0) ||
        request->seconds > 86400.0 ||
        !request->callback_id ||
        !state->timers ||
        state->owner->active_timers >=
            state->owner->max_timers)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    interval = (uint64_t)(
        request->seconds * 1000.0 + 0.999999);
    if (!interval) interval = 1U;
    for (index = 0;
         index < state->timer_capacity;
         ++index)
        if (!state->timers[index].active)
            break;
    if (index == state->timer_capacity)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    generation =
        dluma_game_host_next_generation(state);
    memset(
        &state->timers[index], 0,
        sizeof(state->timers[index]));
    state->timers[index].callback_id =
        request->callback_id;
    state->timers[index].interval_ms =
        interval;
    state->timers[index].due_ms =
        GetTickCount64() + interval;
    state->timers[index].generation =
        generation;
    state->timers[index].active = 1U;
    state->timers[index].repeating =
        repeating ? 1U : 0U;
    ++state->owner->active_timers;
    result->token = dluma_game_host_token(
        DLUMA_GAME_TOKEN_TIMER,
        generation,
        index);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_subscription_add(
    DlumaGameHostState* state,
    uint32_t operation_id,
    const DlumaEventSubscribeRequest* request,
    DlumaTokenResult* result)
{
    uint32_t index;
    uint32_t generation;
    uint32_t event_kind;
    uint32_t native_kind = 0;
    char name[96];
    DlumaLua54Error error;
    uint64_t token;
    if (!state || !state->owner ||
        !state->subscriptions ||
        state->owner->active_subscriptions >=
            state->owner->max_subscriptions)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    if (operation_id == 4001U)
        event_kind = DLUMA_GAME_EVENT_UPDATE;
    else if (operation_id == 4003U) {
        event_kind =
            DLUMA_GAME_EVENT_SENSOR_ENTER;
        native_kind =
            DLUMA_LUA54_EVENT_SENSOR_ENTER;
    } else if (operation_id == 4004U) {
        event_kind =
            DLUMA_GAME_EVENT_SENSOR_LEAVE;
        native_kind =
            DLUMA_LUA54_EVENT_SENSOR_LEAVE;
    } else if (operation_id == 4005U) {
        event_kind =
            DLUMA_GAME_EVENT_INTERACTION_BEGIN;
        native_kind =
            DLUMA_LUA54_EVENT_INTERACT_BEGIN;
    } else if (operation_id == 9002U) {
        event_kind =
            DLUMA_GAME_EVENT_CONFIGURATION_CHANGED;
    } else {
        return DLUMA_STATUS_UNAVAILABLE;
    }
    name[0] = 0;
    if (request->name.size &&
        !dluma_game_host_copy_view(
            name, DLUMA_ARRAY_COUNT(name),
            request->name))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    for (index = 0;
         index < state->subscription_capacity;
         ++index)
        if (!state->subscriptions[index].active)
            break;
    if (index == state->subscription_capacity)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    generation =
        dluma_game_host_next_generation(state);
    token = dluma_game_host_token(
        DLUMA_GAME_TOKEN_SUBSCRIPTION,
        generation,
        index);
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (native_kind &&
        !dluma_native_gameplay_subscription_add_ex(
            state->owner->owner_id,
            state->owner->map_generation,
            token,
            request->callback_id,
            native_kind,
            name,
            &error))
        return DLUMA_STATUS_UNAVAILABLE;
    memset(
        &state->subscriptions[index], 0,
        sizeof(state->subscriptions[index]));
    state->subscriptions[index].callback_id =
        request->callback_id;
    state->subscriptions[index]
        .native_registration_token = token;
    state->subscriptions[index].generation =
        generation;
    state->subscriptions[index].event_kind =
        event_kind;
    state->subscriptions[index].native_kind =
        native_kind;
    state->subscriptions[index].active = 1U;
    state->subscriptions[index].native_registered =
        native_kind ? 1U : 0U;
    dluma_str_copy(
        state->subscriptions[index].name,
        DLUMA_ARRAY_COUNT(
            state->subscriptions[index].name),
        name);
    ++state->owner->active_subscriptions;
    result->token = token;
    return DLUMA_STATUS_OK;
}

static void dluma_game_host_cancel_subscription(
    DlumaGameHostState* state,
    DlumaGameHostSubscription* subscription)
{
    DlumaLua54Error error;
    if (!state || !subscription ||
        !subscription->active)
        return;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (subscription->native_registered)
        (void)dluma_native_gameplay_subscription_remove_ex(
            state->owner->owner_id,
            subscription->native_registration_token,
            &error);
    memset(subscription, 0, sizeof(*subscription));
    if (state->owner->active_subscriptions)
        --state->owner->active_subscriptions;
}

#include "dlw_quest_hud_service.inc"

static DlumaStatus dluma_game_host_cancel(
    DlumaGameHostState* state,
    const DlumaTokenRequest* request,
    DlumaBoolResult* result)
{
    uint32_t kind =
        (uint32_t)(request->token >> 56U);
    uint32_t index;
    uint32_t generation;
    if (!state || !result)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    result->value = 0U;
    if (kind == DLUMA_GAME_TOKEN_QUEST_HUD)
        return dluma_game_host_quest_hud_cancel(state, request->token, result);
    if (kind == DLUMA_GAME_TOKEN_TIMER) {
        DlumaGameHostTimer* timer;
        if (!dluma_game_host_decode_token(
                request->token, kind,
                state->timer_capacity,
                &index, &generation))
            return DLUMA_STATUS_OK;
        timer = &state->timers[index];
        if (!timer->active ||
            timer->generation != generation)
            return DLUMA_STATUS_OK;
        memset(timer, 0, sizeof(*timer));
        if (state->owner->active_timers)
            --state->owner->active_timers;
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    if (kind == DLUMA_GAME_TOKEN_SUBSCRIPTION) {
        DlumaGameHostSubscription* subscription;
        if (!dluma_game_host_decode_token(
                request->token, kind,
                state->subscription_capacity,
                &index, &generation))
            return DLUMA_STATUS_OK;
        subscription =
            &state->subscriptions[index];
        if (!subscription->active ||
            subscription->generation != generation)
            return DLUMA_STATUS_OK;
        dluma_game_host_cancel_subscription(
            state, subscription);
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    if (kind == DLUMA_GAME_TOKEN_COMPANION) {
        DlumaGameHostCompanion* companion;
        if (!dluma_game_host_decode_token(
                request->token, kind,
                state->companion_capacity,
                &index, &generation))
            return DLUMA_STATUS_OK;
        companion = &state->companions[index];
        if (!companion->active ||
            companion->generation != generation)
            return DLUMA_STATUS_OK;
        if (dluma_media_service_archive_in_use(companion->archive)) {
            companion->retained = 1U;
        } else {
            dluma_companion_archive_close(companion->archive);
            dluma_free(companion->archive);
        }
        memset(companion, 0, sizeof(*companion));
        if (state->owner->active_companion_artifacts)
            --state->owner->active_companion_artifacts;
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_companion_open(
    DlumaGameHostState* state,
    const DlumaCompanionOpenRequest* request,
    DlumaCompanionOpenResult* result)
{
    DlumaModArtifactSnapshot artifact;
    DlumaCompanionArchiveSnapshot discovered;
    const DlumaModCompanionDeclaration* declaration = 0;
    DlumaGameHostCompanion* companion;
    char relative_path[
        DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char normalized[
        DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char source_owner_id[DLUMA_PROCESS_SOURCE_OWNER_ID_CAPACITY];
    uint32_t index;
    uint32_t declaration_index;
    uint32_t generation;
    DlumaStatus status;
    if (!state || !state->package ||
        !state->companions ||
        !dluma_game_host_copy_view(
            relative_path,
            DLUMA_ARRAY_COUNT(relative_path),
            request->relative_path) ||
        !dluma_mod_relative_path_validate_and_normalize(
            relative_path,
            normalized,
            DLUMA_ARRAY_COUNT(normalized)) ||
        !dluma_game_host_string_equal(
            relative_path, normalized))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    for (declaration_index = 0;
         declaration_index <
             state->package->manifest.companion_count;
         ++declaration_index) {
        const DlumaModCompanionDeclaration* candidate =
            &state->package->manifest.companion[declaration_index];
        if (dluma_stricmp(candidate->path, relative_path) == 0) {
            if (declaration) return DLUMA_STATUS_INVALID_ARGUMENT;
            declaration = candidate;
        }
    }
    if (!declaration) return DLUMA_STATUS_NOT_FOUND;
    result->sha256.required = DLUMA_SHA256_HEX_CAPACITY;
    if (!result->sha256.data ||
        result->sha256.capacity < result->sha256.required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    if (state->owner->active_companion_artifacts >=
        state->owner->max_companion_artifacts)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    for (index = 0;
         index < state->companion_capacity;
         ++index)
        if (!state->companions[index].active)
            break;
    if (index == state->companion_capacity)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    companion = &state->companions[index];
    memset(companion, 0, sizeof(*companion));
    memset(&artifact, 0, sizeof(artifact));
    artifact.struct_size = (uint32_t)sizeof(artifact);
    status = dluma_mod_package_read_artifact(
        state->package,
        relative_path,
        DLUMA_MOD_ARTIFACT_COMPANION,
        DLUMA_GAME_HOST_COMPANION_LIMIT,
        &artifact);
    if (status != DLUMA_STATUS_OK)
        return status;
    memset(&discovered, 0, sizeof(discovered));
    dluma_str_copy(
        source_owner_id, DLUMA_ARRAY_COUNT(source_owner_id),
        "dluma.companion:");
    dluma_str_append(
        source_owner_id, DLUMA_ARRAY_COUNT(source_owner_id),
        artifact.sha256);
    if (!dluma_companion_archive_discover_file(
            artifact.final_path,
            declaration->audio_manifest,
            &discovered) ||
        dluma_stricmp(discovered.sha256, artifact.sha256) != 0 ||
        discovered.volume_serial != artifact.volume_serial ||
        discovered.file_index_high != artifact.file_index_high ||
        discovered.file_index_low != artifact.file_index_low ||
        discovered.file_size_high != 0U ||
        discovered.file_size_low != artifact.size ||
        dluma_wstricmp(discovered.final_path, artifact.final_path) != 0 ||
        !(companion->archive =
              (DlumaCompanionArchive*)dluma_alloc(
                  sizeof(*companion->archive))) ||
        !dluma_companion_archive_lock(
            &discovered, companion->archive) ||
        !dluma_companion_archive_set_source_owner(
            companion->archive, source_owner_id,
            artifact.sha256, artifact.sha256)) {
        dluma_mod_artifact_release(&artifact);
        if (companion->archive) {
            dluma_companion_archive_close(companion->archive);
            dluma_free(companion->archive);
        }
        memset(companion, 0, sizeof(*companion));
        return DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
    }
    companion->size = artifact.size;
    dluma_str_copy(
        companion->sha256,
        DLUMA_ARRAY_COUNT(companion->sha256),
        artifact.sha256);
    dluma_mod_artifact_release(&artifact);
    generation =
        dluma_game_host_next_generation(state);
    companion->generation = generation;
    companion->active = 1U;
    ++state->owner->active_companion_artifacts;
    result->token = dluma_game_host_token(
        DLUMA_GAME_TOKEN_COMPANION,
        generation,
        index);
    result->size = companion->size;
    memcpy(
        result->sha256.data, companion->sha256,
        DLUMA_SHA256_HEX_CAPACITY);
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_companion_resolve(
    DlumaGameHostState* state,
    uint64_t token,
    DlumaGameHostCompanion** companion_out)
{
    uint32_t index;
    uint32_t generation;
    DlumaGameHostCompanion* companion;
    if (companion_out) *companion_out = 0;
    if (!state || !companion_out ||
        !dluma_game_host_decode_token(
            token, DLUMA_GAME_TOKEN_COMPANION,
            state->companion_capacity,
            &index, &generation))
        return DLUMA_STATUS_STALE_HANDLE;
    companion = &state->companions[index];
    if (!companion->active ||
        companion->generation != generation)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    if (!companion->archive || !companion->archive->validated ||
        companion->archive->file == INVALID_HANDLE_VALUE)
        return DLUMA_STATUS_STALE_HANDLE;
    *companion_out = companion;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_host_companion_entry_read(
    DlumaGameHostState* state,
    const DlumaCompanionEntryReadRequest* request,
    DlumaCompanionEntryReadResult* result)
{
    DlumaGameHostCompanion* companion = 0;
    char entry_path[DLUMA_COMPANION_ARCHIVE_MAX_NAME];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    unsigned int required = 0;
    DlumaStatus status;
    if (!state || !request || !result ||
        !dluma_game_host_copy_view(
            entry_path, DLUMA_ARRAY_COUNT(entry_path),
            request->entry_path))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_game_host_companion_resolve(
        state, request->token, &companion);
    if (status != DLUMA_STATUS_OK) return status;
    if (!dluma_companion_archive_read_entry(
            companion->archive, entry_path,
            0, 0, &required)) {
        if (!required) return DLUMA_STATUS_NOT_FOUND;
    }
    if (required > DLUMA_GAME_HOST_COMPANION_ENTRY_LIMIT)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    result->bytes.required = required;
    result->sha256.required = DLUMA_SHA256_HEX_CAPACITY;
    if ((required &&
         (!result->bytes.data ||
          result->bytes.capacity < required)) ||
        !result->sha256.data ||
        result->sha256.capacity < result->sha256.required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    if (!dluma_companion_archive_read_entry(
            companion->archive, entry_path,
            result->bytes.data, required, &required))
        return DLUMA_STATUS_CHANGED_SINCE_APPROVAL;
    dluma_sha256_bytes_hex(
        result->bytes.data, required, sha256);
    memcpy(
        result->sha256.data, sha256,
        DLUMA_SHA256_HEX_CAPACITY);
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_game_host_context_initialize(
    DlumaGameHostContext* context,
    const DlumaGameHostContextDefinition* definition)
{
    DlumaGameHostState* state;
    if (!context || !definition ||
        context->struct_size < sizeof(*context) ||
        definition->struct_size <
            sizeof(*definition) ||
        definition->struct_version != 1U ||
        !definition->owner)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(context, 0, sizeof(*context));
    context->struct_size =
        (uint32_t)sizeof(*context);
    context->struct_version =
        DLUMA_GAME_HOST_CONTEXT_VERSION;
    state = (DlumaGameHostState*)dluma_alloc(
        sizeof(*state));
    if (!state)
        return DLUMA_STATUS_INTERNAL_ERROR;
    memset(state, 0, sizeof(*state));
    state->magic = DLUMA_GAME_HOST_STATE_MAGIC;
    state->owner = definition->owner;
    state->package = definition->package;
    state->expert_user = definition->expert_user;
    state->event_sink = definition->event_sink;
    state->event_user = definition->event_user;
    if (definition->config_identity)
        state->config_identity =
            *definition->config_identity;
    if (definition->config_schema) {
        state->config_schema =
            (DlumaLuaConfigSchema*)dluma_alloc(
                sizeof(*state->config_schema));
        if (!state->config_schema)
            goto allocation_failed;
        *state->config_schema =
            *definition->config_schema;
    } else if (
        state->config_identity.config_available) {
        state->config_schema =
            (DlumaLuaConfigSchema*)dluma_alloc(
                sizeof(*state->config_schema));
        if (!state->config_schema ||
            !dluma_mod_config_snapshot(
                state->config_identity.owner_kind,
                state->config_identity.registry_key,
                state->config_identity.metadata_fingerprint,
                state->config_identity
                    .config_schema_fingerprint,
                state->config_schema)) {
            dluma_free(state->config_schema);
            state->config_schema = 0;
            state->config_identity.config_available = 0;
        }
    }
    if (definition->storage_key)
        dluma_str_copy(
            state->storage_key,
            DLUMA_ARRAY_COUNT(state->storage_key),
            definition->storage_key);
    state->timer_capacity =
        definition->owner->max_timers;
    state->subscription_capacity =
        definition->owner->max_subscriptions;
    state->companion_capacity =
        definition->owner->max_companion_artifacts;
    if (state->timer_capacity) {
        state->timers = (DlumaGameHostTimer*)dluma_alloc(
            (SIZE_T)state->timer_capacity *
            sizeof(*state->timers));
        if (!state->timers)
            goto allocation_failed;
        memset(
            state->timers, 0,
            (SIZE_T)state->timer_capacity *
            sizeof(*state->timers));
    }
    if (state->subscription_capacity) {
        state->subscriptions =
            (DlumaGameHostSubscription*)dluma_alloc(
                (SIZE_T)state->subscription_capacity *
                sizeof(*state->subscriptions));
        if (!state->subscriptions)
            goto allocation_failed;
        memset(
            state->subscriptions, 0,
            (SIZE_T)state->subscription_capacity *
            sizeof(*state->subscriptions));
    }
    if (state->companion_capacity) {
        state->companions =
            (DlumaGameHostCompanion*)dluma_alloc(
                (SIZE_T)state->companion_capacity *
                sizeof(*state->companions));
        if (!state->companions)
            goto allocation_failed;
        memset(
            state->companions, 0,
            (SIZE_T)state->companion_capacity *
            sizeof(*state->companions));
    }
    context->state = state;
    return DLUMA_STATUS_OK;

allocation_failed:
    context->state = state;
    dluma_game_host_context_revoke(context);
    return DLUMA_STATUS_INTERNAL_ERROR;
}

void dluma_game_host_context_revoke(
    DlumaGameHostContext* context)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    uint32_t index;
    int media_drained = 1;
    if (!state) {
        if (context) {
            memset(context, 0, sizeof(*context));
            context->struct_size =
                (uint32_t)sizeof(*context);
        }
        return;
    }
    if (state->owner)
        (void)dluma_quest_phase_service_detach(
            state->owner->owner_id);
    dluma_game_host_quest_hud_clear(state);
    if (state->owner)
        media_drained = dluma_media_service_revoke_owner(
            state->owner->owner_id,
            state->owner->generation);
    for (index = 0;
         index < state->subscription_capacity;
         ++index)
        dluma_game_host_cancel_subscription(
            state, &state->subscriptions[index]);
    for (index = 0;
         index < state->companion_capacity;
         ++index) {
        if (state->companions[index].archive &&
            media_drained &&
            !dluma_media_service_archive_in_use(
                state->companions[index].archive)) {
            dluma_companion_archive_close(
                state->companions[index].archive);
            dluma_free(state->companions[index].archive);
        } else if (state->companions[index].archive) {
            state->companions[index].retained = 1U;
        }
    }
    if (state->owner) {
        dluma_player_service_revoke_owner(
            state->owner->owner_id);
        state->owner->active_timers = 0U;
        state->owner->active_subscriptions = 0U;
        state->owner->active_companion_artifacts = 0U;
        state->owner->active_handles = 0U;
    }
    state->magic = 0U;
    dluma_free(state->config_schema);
    dluma_free(state->timers);
    dluma_free(state->subscriptions);
    dluma_free(state->companions);
    dluma_free(state);
    memset(context, 0, sizeof(*context));
    context->struct_size =
        (uint32_t)sizeof(*context);
}

void dluma_game_host_context_advance_scope(
    DlumaGameHostContext* context)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    uint32_t index;
    int media_drained;
    if (!state) return;
    dluma_game_host_quest_hud_clear(state);
    media_drained = state->owner
        ? dluma_media_service_revoke_owner(
              state->owner->owner_id,
              state->owner->generation)
        : 1;
    for (index = 0;
         index < state->timer_capacity;
         ++index)
        memset(
            &state->timers[index], 0,
            sizeof(state->timers[index]));
    for (index = 0;
         index < state->subscription_capacity;
         ++index)
        dluma_game_host_cancel_subscription(
            state, &state->subscriptions[index]);
    for (index = 0;
         index < state->companion_capacity;
         ++index) {
        if (state->companions[index].archive &&
            media_drained &&
            !dluma_media_service_archive_in_use(
                state->companions[index].archive)) {
            dluma_companion_archive_close(
                state->companions[index].archive);
            dluma_free(state->companions[index].archive);
        } else if (state->companions[index].archive) {
            state->companions[index].retained = 1U;
        }
        memset(
            &state->companions[index], 0,
            sizeof(state->companions[index]));
    }
    if (state->owner) {
        dluma_player_service_revoke_owner(
            state->owner->owner_id);
        state->owner->active_timers = 0U;
        state->owner->active_subscriptions = 0U;
        state->owner->active_companion_artifacts = 0U;
        state->owner->active_handles = 0U;
    }
}

int dluma_game_host_context_suspend_map_for_exit(DlumaGameHostContext* context)
{
    DlumaGameHostState* state=dluma_game_host_state(context);
    uint32_t index;
    int complete=1;
    if (!state) return context && !context->state;
    if (!state->owner) return 0;
    state->map_exit_suspended=1U;
    dluma_mod_owner_begin_revoke(state->owner);
    dluma_game_host_quest_hud_clear(state);
    if (!dluma_media_service_revoke_owner(state->owner->owner_id,0U)) complete=0;
    for (index=0;index<state->subscription_capacity;++index) {
        DlumaGameHostSubscription* sub=&state->subscriptions[index];
        DlumaLua54Error error;
        if (!sub->active || !sub->native_registered) continue;
        memset(&error,0,sizeof(error));error.struct_size=sizeof(error);
        if (dluma_native_gameplay_subscription_remove_ex(state->owner->owner_id,
                sub->native_registration_token,&error) ||
            strlen(error.code)==sizeof("unknown_subscription")-1 &&
            memcmp(error.code,"unknown_subscription",sizeof("unknown_subscription"))==0) {
            sub->native_registered=0U;
            sub->native_exit_suspended=1U;
        } else complete=0;
    }
    dluma_player_service_revoke_owner(state->owner->owner_id);
    state->owner->active_handles=0U;
    return complete;
}

void dluma_game_host_context_bind_clean_frontend(DlumaGameHostContext* context)
{
    DlumaGameHostState* state=dluma_game_host_state(context);
    uint32_t index;
    if (!state || !state->owner || !state->map_exit_suspended) return;
    state->owner->active_handles=0U;
    state->owner->active_timers=0U;
    state->owner->active_subscriptions=0U;
    state->owner->active_companion_artifacts=0U;
    for(index=0;index<state->timer_capacity;++index)
        if(state->timers[index].active)++state->owner->active_timers;
    for(index=0;index<state->subscription_capacity;++index)
        if(state->subscriptions[index].active && !state->subscriptions[index].native_exit_suspended)
            ++state->owner->active_subscriptions;
    for(index=0;index<state->companion_capacity;++index)
        if(state->companions[index].active)++state->owner->active_companion_artifacts;
}

int dluma_game_host_context_needs_map_rebind(DlumaGameHostContext* context)
{
    DlumaGameHostState* state=dluma_game_host_state(context);
    return state && state->map_exit_suspended;
}

void dluma_game_host_context_rebind_map(
    DlumaGameHostContext* context)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    uint32_t index;
    if (!state || !state->owner) return;
    dluma_game_host_quest_hud_clear(state);
    /* Rebind is called after the owner generation advances.  Revoke media
     * across that owner's generations so a stale cue or music token cannot
     * leave engine audio running without a callable owner handle. */
    (void)dluma_media_service_revoke_owner(
        state->owner->owner_id,
        0U);
    state->owner->active_timers = 0U;
    state->owner->active_subscriptions = 0U;
    state->owner->active_companion_artifacts = 0U;
    for (index = 0;
         index < state->timer_capacity;
         ++index)
        if (state->timers[index].active)
            ++state->owner->active_timers;
    for (index = 0;
         index < state->subscription_capacity;
         ++index) {
        DlumaGameHostSubscription* subscription =
            &state->subscriptions[index];
        DlumaLua54Error error;
        int registered = 1;
        if (!subscription->active) continue;
        if (subscription->native_registered || subscription->native_exit_suspended) {
            memset(&error, 0, sizeof(error));
            error.struct_size = sizeof(error);
            (void)dluma_native_gameplay_subscription_remove_ex(
                state->owner->owner_id,
                subscription->native_registration_token,
                &error);
            memset(&error, 0, sizeof(error));
            error.struct_size = sizeof(error);
            registered =
                dluma_native_gameplay_subscription_add_ex(
                    state->owner->owner_id,
                    state->owner->map_generation,
                    subscription->native_registration_token,
                    subscription->callback_id,
                    subscription->native_kind,
                    subscription->name,
                    &error);
            if (registered) {
                subscription->native_registered=1U;
                subscription->native_exit_suspended=0U;
            }
        }
        if (!registered) {
            memset(
                subscription, 0,
                sizeof(*subscription));
            continue;
        }
        ++state->owner->active_subscriptions;
    }
    for (index = 0;
         index < state->companion_capacity;
         ++index)
        if (state->companions[index].active)
            ++state->owner->active_companion_artifacts;
    state->map_exit_suspended=0U;
}

void dluma_game_host_context_pump(
    DlumaGameHostContext* context,
    double delta_seconds)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    uint64_t now;
    uint32_t index;
    if (!state || !state->owner ||
        state->owner->state !=
            DLUMA_MOD_OWNER_ACTIVE ||
        !state->event_sink)
        return;
    now = GetTickCount64();
    for (index = 0;
         index < state->subscription_capacity;
         ++index) {
        DlumaGameHostSubscription snapshot;
        DlumaGameHostSubscription* current =
            &state->subscriptions[index];
        if (!current->active ||
            current->event_kind !=
                DLUMA_GAME_EVENT_UPDATE)
            continue;
        snapshot = *current;
        state->event_sink(
            state->event_user,
            DLUMA_GAME_EVENT_UPDATE,
            snapshot.callback_id,
            dluma_game_host_token(
                DLUMA_GAME_TOKEN_SUBSCRIPTION,
                snapshot.generation,
                index),
            delta_seconds,
            "");
    }
    for (index = 0;
         index < state->timer_capacity;
         ++index) {
        DlumaGameHostTimer snapshot;
        DlumaGameHostTimer* current =
            &state->timers[index];
        uint64_t token;
        if (!current->active ||
            current->due_ms > now)
            continue;
        snapshot = *current;
        token = dluma_game_host_token(
            DLUMA_GAME_TOKEN_TIMER,
            snapshot.generation,
            index);
        if (snapshot.repeating) {
            uint64_t next_due = current->due_ms;
            do {
                if (UINT64_MAX - next_due <
                    current->interval_ms) {
                    current->active = 0U;
                    break;
                }
                next_due += current->interval_ms;
            } while (next_due <= now);
            current->due_ms = next_due;
        } else {
            memset(current, 0, sizeof(*current));
            if (state->owner->active_timers)
                --state->owner->active_timers;
        }
        state->event_sink(
            state->event_user,
            DLUMA_GAME_EVENT_TIMER,
            snapshot.callback_id,
            token,
            (double)now,
            "");
    }
}

void dluma_game_host_context_notify_config_changed(
    DlumaGameHostContext* context,
    const char* key)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    uint32_t index;
    if (!state || !state->owner ||
        state->owner->state != DLUMA_MOD_OWNER_ACTIVE ||
        !state->event_sink || !key || !key[0])
        return;
    for (index = 0;
         index < state->subscription_capacity;
         ++index) {
        DlumaGameHostSubscription snapshot;
        DlumaGameHostSubscription* current =
            &state->subscriptions[index];
        if (!current->active ||
            current->event_kind !=
                DLUMA_GAME_EVENT_CONFIGURATION_CHANGED ||
            (current->name[0] &&
             !dluma_game_host_string_equal(
                 current->name, key)))
            continue;
        snapshot = *current;
        state->event_sink(
            state->event_user,
            DLUMA_GAME_EVENT_CONFIGURATION_CHANGED,
            snapshot.callback_id,
            dluma_game_host_token(
                DLUMA_GAME_TOKEN_SUBSCRIPTION,
                snapshot.generation,
                index),
            0.0,
            key);
    }
}

void* dluma_game_host_context_expert_user(
    DlumaGameHostContext* context)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    return state ? state->expert_user : 0;
}

DlumaStatus dluma_game_host_context_resolve_companion(
    DlumaGameHostContext* context,
    uint64_t token,
    DlumaCompanionArchive** archive_out)
{
    DlumaGameHostState* state = dluma_game_host_state(context);
    DlumaGameHostCompanion* companion = 0;
    DlumaStatus status;
    if (archive_out) *archive_out = 0;
    if (!archive_out) return DLUMA_STATUS_INVALID_ARGUMENT;
    status = dluma_game_host_companion_resolve(
        state, token, &companion);
    if (status == DLUMA_STATUS_OK)
        *archive_out = companion->archive;
    return status;
}

void dluma_game_host_context_prepare_capabilities(
    DlumaGameHostContext* context,
    DlumaCapabilityRegistry* registry)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    DlumaStringView ready;
    if (!state || !registry) return;
    ready.data = "ready";
    ready.size = 5U;
    (void)dluma_capability_registry_set(
        registry,
        DLUMA_CAPABILITY_CORE_TIMERS,
        state->timer_capacity != 0U,
        state->timer_capacity != 0U,
        ready);
    (void)dluma_capability_registry_set(
        registry,
        DLUMA_CAPABILITY_CONFIGURATION_MOD,
        state->config_schema != 0,
        state->config_schema != 0,
        ready);
    (void)dluma_capability_registry_set(
        registry,
        DLUMA_CAPABILITY_STORAGE_MOD,
        dluma_game_host_storage_key_valid(
            state->storage_key),
        dluma_game_host_storage_key_valid(
            state->storage_key),
        ready);
    (void)dluma_capability_registry_set(
        registry,
        DLUMA_CAPABILITY_COMPANION_CONTENT,
        state->package &&
            state->package->manifest.companion_count != 0U,
        state->package &&
            state->package->manifest.companion_count != 0U,
        ready);
}

DlumaStatus dluma_game_host_context_invoke(
    DlumaGameHostContext* context,
    uint32_t operation_id,
    const void* request,
    void* result)
{
    DlumaGameHostState* state =
        dluma_game_host_state(context);
    if (!state || !request || !result)
        return DLUMA_STATUS_UNAVAILABLE;
    switch (operation_id) {
    case 5003U:
        return dluma_game_host_quest_hud_snapshot((DlumaQuestHudResult*)result);
    case 5004U:
        return dluma_game_host_quest_hud_override(state,
            (const DlumaQuestHudOverrideRequest*)request, (DlumaTokenResult*)result);
    case 1004U:
        return dluma_game_host_timer_add(
            state,
            (const DlumaTimerRequest*)request,
            (DlumaTokenResult*)result,
            0);
    case 1005U:
        return dluma_game_host_timer_add(
            state,
            (const DlumaTimerRequest*)request,
            (DlumaTokenResult*)result,
            1);
    case 1006U:
        return dluma_game_host_cancel(
            state,
            (const DlumaTokenRequest*)request,
            (DlumaBoolResult*)result);
    case 4001U:
    case 4003U:
    case 4004U:
    case 4005U:
    case 9002U:
        return dluma_game_host_subscription_add(
            state,
            operation_id,
            (const DlumaEventSubscribeRequest*)request,
            (DlumaTokenResult*)result);
    case 9001U:
        return dluma_game_host_config_get(
            state,
            (const DlumaConfigGetRequest*)request,
            (DlumaScalarResult*)result);
    case 10001U:
        return dluma_game_host_storage_get(
            state,
            (const DlumaStorageGetRequest*)request,
            (DlumaScalarResult*)result);
    case 10002U:
        return dluma_game_host_storage_set(
            state,
            (const DlumaStorageSetRequest*)request,
            (DlumaBoolResult*)result);
    case 10003U:
        return dluma_game_host_storage_delete(
            state,
            (const DlumaConfigGetRequest*)request,
            (DlumaBoolResult*)result);
    case 11001U:
        return dluma_game_host_companion_open(
            state,
            (const DlumaCompanionOpenRequest*)request,
            (DlumaCompanionOpenResult*)result);
    case 11002U:
        return dluma_game_host_companion_entry_read(
            state,
            (const DlumaCompanionEntryReadRequest*)request,
            (DlumaCompanionEntryReadResult*)result);
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}
