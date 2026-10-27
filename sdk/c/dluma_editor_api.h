#ifndef DLUMA_EDITOR_API_H
#define DLUMA_EDITOR_API_H

#include <stdint.h>

#if defined(_WIN32)
#define DLUMA_EDITOR_API_CALL __cdecl
#define DLUMA_EDITOR_API_EXPORT __declspec(dllexport)
#else
#define DLUMA_EDITOR_API_CALL
#define DLUMA_EDITOR_API_EXPORT
#endif

#define DLUMA_EDITOR_MOD_ABI_V1 0x00010000u
#define DLUMA_EDITOR_MOD_GET_API_V1_EXPORT "DLUMAEditorMod_GetApiV1"
#define DLUMA_EDITOR_PATH_CAPACITY 1024u
#define DLUMA_EDITOR_ID_CAPACITY 128u
#define DLUMA_EDITOR_ARGUMENT_CAPACITY 8192u

typedef enum DlumaEditorStatusV1 {
    DLUMA_EDITOR_STATUS_OK = 0,
    DLUMA_EDITOR_STATUS_INVALID_ARGUMENT = 1,
    DLUMA_EDITOR_STATUS_NOT_DECLARED = 2,
    DLUMA_EDITOR_STATUS_ALREADY_REGISTERED = 3,
    DLUMA_EDITOR_STATUS_PERMISSION_DENIED = 4,
    DLUMA_EDITOR_STATUS_LIMIT_EXCEEDED = 5,
    DLUMA_EDITOR_STATUS_INTERNAL_ERROR = 6
} DlumaEditorStatusV1;

typedef struct DlumaEditorLoadEventV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t process_start_time;
    char host_release[64];
    char editor_sha256[65];
} DlumaEditorLoadEventV1;

typedef struct DlumaEditorContextEventV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t context_generation;
    char project_root_utf8[DLUMA_EDITOR_PATH_CAPACITY];
    char project_name[DLUMA_EDITOR_ID_CAPACITY];
    char map_name[DLUMA_EDITOR_ID_CAPACITY];
} DlumaEditorContextEventV1;

typedef struct DlumaEditorSessionEventV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t session_generation;
} DlumaEditorSessionEventV1;

typedef struct DlumaEditorQuestValidationRequestV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t source_line;
    uint32_t arguments_size;
    char handler_id[DLUMA_EDITOR_ID_CAPACITY];
    char phase_id[DLUMA_EDITOR_ID_CAPACITY];
    char arguments_json[DLUMA_EDITOR_ARGUMENT_CAPACITY + 1u];
} DlumaEditorQuestValidationRequestV1;

typedef struct DlumaEditorQuestValidationResultV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t accepted;
    uint32_t reserved;
    char diagnostic[256];
} DlumaEditorQuestValidationResultV1;

typedef DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *DlumaEditorQuestValidatorV1)(
    void* user,
    const DlumaEditorQuestValidationRequestV1* request,
    DlumaEditorQuestValidationResultV1* result);

typedef struct DlumaEditorQuestServiceV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    void* owner_context;
    DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *attach_validator)(
        void* owner_context,
        const char* handler_id,
        uint32_t handler_id_size,
        DlumaEditorQuestValidatorV1 validator,
        void* validator_user);
} DlumaEditorQuestServiceV1;

typedef struct DlumaEditorHostApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    DlumaEditorQuestServiceV1 quest;
} DlumaEditorHostApiV1;

typedef DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *DlumaEditorLoadCallbackV1)(
    const DlumaEditorLoadEventV1* event_value);
typedef DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *DlumaEditorContextCallbackV1)(
    const DlumaEditorContextEventV1* event_value);
typedef DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *DlumaEditorSessionCallbackV1)(
    const DlumaEditorSessionEventV1* event_value);

typedef struct DlumaEditorModApiV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    DlumaEditorLoadCallbackV1 editor_load;
    DlumaEditorContextCallbackV1 context_changed;
    DlumaEditorSessionCallbackV1 session_begin;
    DlumaEditorSessionCallbackV1 session_end;
} DlumaEditorModApiV1;

typedef DlumaEditorStatusV1 (DLUMA_EDITOR_API_CALL *DlumaEditorModGetApiV1Fn)(
    const DlumaEditorHostApiV1* host_api,
    DlumaEditorModApiV1* mod_api);

#endif
