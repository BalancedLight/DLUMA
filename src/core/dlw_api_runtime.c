#include "dlw_api_runtime.h"

#include "../generated/dluma_api_catalog.generated.h"
#include "../generated/dluma_api_operations.generated.h"

#include <string.h>

typedef struct DlumaSizedRecordPrefix {
    uint32_t struct_size;
    uint32_t struct_version;
} DlumaSizedRecordPrefix;

typedef struct DlumaResultRecordPrefix {
    uint32_t struct_size;
    uint32_t struct_version;
    DlumaResult call;
} DlumaResultRecordPrefix;

static const DlumaOperationPolicy* dluma_api_operation_policy(
    uint32_t operation_id)
{
    uint32_t index;
    for (index = 0;
         index < DLUMA_GENERATED_OPERATION_POLICY_COUNT;
         ++index) {
        if (g_dluma_generated_operation_policies[index].operation_id ==
            operation_id)
            return &g_dluma_generated_operation_policies[index];
    }
    return 0;
}

int dluma_api_runtime_operation_context(
    uint32_t operation_id,
    const char** capability_name,
    const char** access_name)
{
    const DlumaOperationPolicy* policy =
        dluma_api_operation_policy(operation_id);
    if (capability_name) *capability_name = "";
    if (access_name) *access_name = "";
    if (!policy) return 0;
    if (capability_name)
        *capability_name = policy->capability_name;
    if (access_name)
        *access_name = policy->access_name;
    return 1;
}

static const char* dluma_api_status_code(DlumaStatus status)
{
    switch (status) {
    case DLUMA_STATUS_INVALID_ARGUMENT: return "invalid_argument";
    case DLUMA_STATUS_UNAVAILABLE: return "unsupported_capability";
    case DLUMA_STATUS_ACCESS_DENIED: return "access_denied";
    case DLUMA_STATUS_SCOPE_DENIED: return "scope_denied";
    case DLUMA_STATUS_WRONG_THREAD: return "wrong_thread";
    case DLUMA_STATUS_STALE_HANDLE: return "stale_handle";
    case DLUMA_STATUS_FOREIGN_HANDLE: return "foreign_handle";
    case DLUMA_STATUS_NOT_FOUND: return "not_found";
    case DLUMA_STATUS_LIMIT_EXCEEDED: return "limit_exceeded";
    case DLUMA_STATUS_INSUFFICIENT_BUFFER: return "insufficient_buffer";
    case DLUMA_STATUS_CHANGED_SINCE_APPROVAL: return "changed_since_approval";
    case DLUMA_STATUS_OWNER_REVOKED: return "owner_revoked";
    case DLUMA_STATUS_REMOVED: return "removed_operation";
    case DLUMA_STATUS_INTERNAL_ERROR: return "internal_error";
    default: return "";
    }
}

static int dluma_api_view_valid(
    DlumaStringView value,
    uint64_t maximum,
    int allow_empty)
{
    uint64_t index = 0;
    if ((!value.data && value.size) ||
        value.size > maximum ||
        (!allow_empty && !value.size))
        return 0;
    while (index < value.size) {
        unsigned char lead = (unsigned char)value.data[index++];
        uint32_t codepoint;
        uint32_t remaining;
        uint32_t minimum;
        if (!lead) return 0;
        if (lead < 0x80U) continue;
        if ((lead & 0xE0U) == 0xC0U) {
            codepoint = lead & 0x1FU;
            remaining = 1U;
            minimum = 0x80U;
        } else if ((lead & 0xF0U) == 0xE0U) {
            codepoint = lead & 0x0FU;
            remaining = 2U;
            minimum = 0x800U;
        } else if ((lead & 0xF8U) == 0xF0U) {
            codepoint = lead & 0x07U;
            remaining = 3U;
            minimum = 0x10000U;
        } else {
            return 0;
        }
        if (value.size - index < remaining) return 0;
        while (remaining--) {
            unsigned char continuation =
                (unsigned char)value.data[index++];
            if ((continuation & 0xC0U) != 0x80U) return 0;
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
            return 0;
    }
    return 1;
}

static DlumaStatus dluma_api_handle_valid(
    const DlumaModOwner* owner,
    DlumaObjectHandle handle)
{
    if (!owner || !handle.handle_id) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (handle.owner_id != owner->owner_id)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    if (handle.generation != owner->map_generation)
        return DLUMA_STATUS_STALE_HANDLE;
    return DLUMA_STATUS_OK;
}

static int dluma_api_scalar_valid(const DlumaScalar* scalar)
{
    if (!scalar) return 0;
    switch (scalar->kind) {
    case DLUMA_SCALAR_NONE:
    case DLUMA_SCALAR_BOOLEAN:
    case DLUMA_SCALAR_INTEGER:
        return 1;
    case DLUMA_SCALAR_NUMBER:
        return scalar->number_value == scalar->number_value &&
               scalar->number_value <= 1.7976931348623157e+308 &&
               scalar->number_value >= -1.7976931348623157e+308;
    case DLUMA_SCALAR_STRING:
        return dluma_api_view_valid(scalar->string_value, 4096U, 1);
    default:
        return 0;
    }
}

static DlumaStatus dluma_api_validate_request(
    const DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request)
{
    switch (operation_id) {
    case 1001U: {
        const DlumaLogRequest* value = (const DlumaLogRequest*)request;
        return dluma_api_view_valid(value->level, 16U, 1) &&
               dluma_api_view_valid(value->message, 1024U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 1004U:
    case 1005U: {
        const DlumaTimerRequest* value = (const DlumaTimerRequest*)request;
        return value->callback_id &&
               value->seconds == value->seconds &&
               value->seconds > 0.0 &&
               value->seconds <= 86400.0
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 1006U:
    case 6006U:
    case 6011U:
    case 12003U: {
        const DlumaTokenRequest* value = (const DlumaTokenRequest*)request;
        return value->token ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 2001U: {
        const DlumaWorldFindRequest* value =
            (const DlumaWorldFindRequest*)request;
        return dluma_api_view_valid(value->name, 96U, 0) &&
               dluma_api_view_valid(value->expected_class, 96U, 1)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 2002U:
    case 2003U:
    case 2004U:
    case 3002U:
    case 3003U:
    case 3004U:
    case 3005U:
    case 3006U:
    case 3007U:
        return dluma_api_handle_valid(
            owner, ((const DlumaObjectRequest*)request)->handle);
    case 2005U: {
        const DlumaSetEnabledManyRequest* value =
            (const DlumaSetEnabledManyRequest*)request;
        uint32_t index;
        if (!value->names || !value->name_count ||
            value->name_count > 32U ||
            value->enabled > 1U || value->atomic > 1U)
            return DLUMA_STATUS_INVALID_ARGUMENT;
        for (index = 0; index < value->name_count; ++index) {
            if (!dluma_api_view_valid(value->names[index], 96U, 0))
                return DLUMA_STATUS_INVALID_ARGUMENT;
        }
        return DLUMA_STATUS_OK;
    }
    case 3009U:
    case 3011U: {
        const DlumaPlayerVariableRequest* value =
            (const DlumaPlayerVariableRequest*)request;
        DlumaStatus status =
            dluma_api_handle_valid(owner, value->player);
        if (status != DLUMA_STATUS_OK) return status;
        return dluma_api_view_valid(value->name, 64U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 3010U: {
        const DlumaPlayerVariableSetRequest* value =
            (const DlumaPlayerVariableSetRequest*)request;
        DlumaStatus status =
            dluma_api_handle_valid(owner, value->player);
        if (status != DLUMA_STATUS_OK) return status;
        return dluma_api_view_valid(value->name, 64U, 0) &&
               value->value == value->value &&
               value->value <= 1.7976931348623157e+308 &&
               value->value >= -1.7976931348623157e+308
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 4002U:
    case 8001U: {
        const DlumaNamedRequest* value = (const DlumaNamedRequest*)request;
        return dluma_api_view_valid(value->name, 96U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 4001U:
    case 4006U:
    case 9002U: {
        const DlumaEventSubscribeRequest* value =
            (const DlumaEventSubscribeRequest*)request;
        return value->callback_id &&
               dluma_api_view_valid(value->name, 96U, 1)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 12001U:
    case 12002U: {
        const DlumaQuestPhaseRegistrationRequest* value =
            (const DlumaQuestPhaseRegistrationRequest*)request;
        return value->callback_id &&
               dluma_api_view_valid(
                   value->key,
                   operation_id == 12001U ? 127U : 256U,
                   0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 4003U:
    case 4004U:
    case 4005U: {
        const DlumaEventSubscribeRequest* value =
            (const DlumaEventSubscribeRequest*)request;
        return value->callback_id &&
               dluma_api_view_valid(value->name, 96U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 5001U: {
        const DlumaSubtitleRequest* value =
            (const DlumaSubtitleRequest*)request;
        return dluma_api_view_valid(value->text, 1024U, 0) &&
               value->duration_seconds == value->duration_seconds &&
               value->duration_seconds > 0.0 &&
               value->duration_seconds <= 600.0 &&
               value->force <= 1U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 5002U:
    case 5005U: {
        const DlumaSubtitleUpdateRequest* value =
            (const DlumaSubtitleUpdateRequest*)request;
        return dluma_api_view_valid(
                   value->text,
                   operation_id == 5005U ? 512U : 1024U,
                   0) &&
               value->fade <= 1U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 5004U: {
        const DlumaQuestHudOverrideRequest* value =
            (const DlumaQuestHudOverrideRequest*)request;
        return dluma_api_view_valid(value->target_id, 96U, 0) &&
               dluma_api_view_valid(value->text, 512U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6001U: {
        const DlumaRadioDialogRequest* value =
            (const DlumaRadioDialogRequest*)request;
        return dluma_api_view_valid(value->speaker, 96U, 0) &&
               dluma_api_view_valid(value->group, 96U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6002U: {
        const DlumaAudioCuePlayRequest* value =
            (const DlumaAudioCuePlayRequest*)request;
        return value->bank_token &&
               dluma_api_view_valid(value->cue, 127U, 0) &&
               value->start_ms <= 86400000ULL &&
               value->initial_gain == value->initial_gain &&
               value->initial_gain >= 0.0f &&
               value->initial_gain <= 4.0f &&
               value->loop <= 1U &&
               value->fade_in_ms <= 60000U &&
               value->fade_out_ms <= 60000U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6003U: {
        const DlumaAudioCueStopRequest* value =
            (const DlumaAudioCueStopRequest*)request;
        return value->cue_token && value->fade_out_ms <= 60000U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6004U: {
        const DlumaAudioBankOpenRequest* value =
            (const DlumaAudioBankOpenRequest*)request;
        return value->companion_token &&
               dluma_api_view_valid(value->bank_id, 63U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6005U: {
        const DlumaAudioCueRequest* value =
            (const DlumaAudioCueRequest*)request;
        return value->bank_token &&
               dluma_api_view_valid(value->cue, 127U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6007U: {
        const DlumaAudioCueSeekRequest* value =
            (const DlumaAudioCueSeekRequest*)request;
        return value->cue_token && value->position_ms <= 86400000ULL
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6008U: {
        const DlumaAudioCueGainRequest* value =
            (const DlumaAudioCueGainRequest*)request;
        return value->cue_token && value->gain == value->gain &&
               value->gain >= 0.0f && value->gain <= 4.0f
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6009U:
    case 6012U: {
        const DlumaAudioFilterRequest* value =
            (const DlumaAudioFilterRequest*)request;
        return value->token &&
               value->wet_mix == value->wet_mix &&
               value->cutoff_hz == value->cutoff_hz &&
               value->wet_mix >= 0.0f && value->wet_mix <= 1.0f &&
               value->cutoff_hz >= 20.0f &&
               value->cutoff_hz <= 18000.0f
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 6010U:
        return ((const DlumaAudioMusicAcquireRequest*)request)
                       ->fade_out_ms <= 60000U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    case 6013U: {
        const DlumaAudioMusicReleaseRequest* value =
            (const DlumaAudioMusicReleaseRequest*)request;
        return value->token && value->resume_delay_ms <= 60000U
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 7001U:
    case 7002U: {
        const DlumaQuestTransitionRequest* value =
            (const DlumaQuestTransitionRequest*)request;
        return dluma_api_view_valid(value->quest_id, 128U, 0) &&
               dluma_api_view_valid(
                   value->phase_id, 128U, operation_id == 7002U)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 9001U:
    case 10003U: {
        const DlumaConfigGetRequest* value =
            (const DlumaConfigGetRequest*)request;
        return dluma_api_view_valid(
                   value->key, operation_id == 9001U ? 64U : 128U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 10001U: {
        const DlumaStorageGetRequest* value =
            (const DlumaStorageGetRequest*)request;
        return dluma_api_view_valid(value->key, 128U, 0) &&
               value->has_fallback <= 1U &&
               (!value->has_fallback ||
                dluma_api_scalar_valid(&value->fallback))
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 10002U: {
        const DlumaStorageSetRequest* value =
            (const DlumaStorageSetRequest*)request;
        return dluma_api_view_valid(value->key, 128U, 0) &&
               value->value.kind != DLUMA_SCALAR_NONE &&
               dluma_api_scalar_valid(&value->value)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    case 11001U: {
        const DlumaCompanionOpenRequest* value =
            (const DlumaCompanionOpenRequest*)request;
        uint64_t index;
        if (!dluma_api_view_valid(value->relative_path, 512U, 0))
            return DLUMA_STATUS_INVALID_ARGUMENT;
        for (index = 0; index + 1U < value->relative_path.size; ++index) {
            if (value->relative_path.data[index] == '.' &&
                value->relative_path.data[index + 1U] == '.')
                return DLUMA_STATUS_INVALID_ARGUMENT;
        }
        return DLUMA_STATUS_OK;
    }
    case 11002U: {
        const DlumaCompanionEntryReadRequest* value =
            (const DlumaCompanionEntryReadRequest*)request;
        return value->token &&
               dluma_api_view_valid(value->entry_path, 259U, 0)
            ? DLUMA_STATUS_OK : DLUMA_STATUS_INVALID_ARGUMENT;
    }
    default:
        return DLUMA_STATUS_OK;
    }
}

static int dluma_api_write_error_text(
    DlumaStringBuffer* output,
    const char* value)
{
    uint64_t length = 0;
    if (!output || !value) return 0;
    while (value[length]) ++length;
    output->required = length + 1U;
    if (!output->data || output->capacity < output->required)
        return 0;
    memcpy(output->data, value, (size_t)length);
    output->data[length] = 0;
    return 1;
}

static void dluma_api_publish_result(
    DlumaResultRecordPrefix* result,
    DlumaStatus status,
    const DlumaOperationPolicy* policy)
{
    DlumaError* error;
    if (!result) return;
    result->call.struct_size = (uint32_t)sizeof(result->call);
    result->call.struct_version = 1U;
    result->call.status = (uint32_t)status;
    result->call.reserved = 0;
    error = result->call.error;
    if (status == DLUMA_STATUS_OK || !error ||
        error->struct_size < sizeof(*error) ||
        error->struct_version != 1U)
        return;
    error->retryable =
        status == DLUMA_STATUS_UNAVAILABLE ||
        status == DLUMA_STATUS_WRONG_THREAD;
    error->operation_id = policy ? policy->operation_id : 0U;
    error->capability_id = policy ? policy->capability_id : 0U;
    error->access_id = policy ? policy->access_id : 0U;
    dluma_api_write_error_text(&error->code, dluma_api_status_code(status));
    dluma_api_write_error_text(
        &error->message,
        status == DLUMA_STATUS_UNAVAILABLE
            ? "The requested DLUMA capability is not ready."
            : "The DLUMA operation failed its shared runtime contract.");
}

static DlumaStatus DLUMA_API_CALL dluma_api_runtime_get_service(
    void* context,
    DlumaStringView service_id,
    uint32_t minimum_version,
    void* output_table,
    uint32_t output_size);

int dluma_api_string_equals_literal(
    DlumaStringView value,
    const char* literal)
{
    uint64_t index = 0;
    if (!literal || (!value.data && value.size)) return 0;
    while (literal[index]) {
        if (index >= value.size ||
            value.data[index] != literal[index])
            return 0;
        ++index;
    }
    return index == value.size;
}

int dluma_api_copy_service_table(
    void* output,
    uint32_t output_size,
    const void* table,
    uint32_t table_size)
{
    uint32_t copy_size;
    if (!output || !table || output_size < sizeof(uint32_t) * 3U)
        return 0;
    copy_size = output_size < table_size ? output_size : table_size;
    memset(output, 0, output_size);
    memcpy(output, table, copy_size);
    return 1;
}

#include "../generated/dluma_api_service_bindings.generated.inc"

static DlumaStatus DLUMA_API_CALL dluma_api_runtime_get_service(
    void* context,
    DlumaStringView service_id,
    uint32_t minimum_version,
    void* output_table,
    uint32_t output_size)
{
    DlumaApiRuntime* runtime = (DlumaApiRuntime*)context;
    DlumaStatus status = dluma_generated_fill_service(
        (DlumaApiRuntime*)context,
        service_id,
        minimum_version,
        output_table,
        output_size);
    if (status == DLUMA_STATUS_NOT_FOUND && runtime &&
        runtime->backend.get_service)
        return runtime->backend.get_service(
            runtime->backend.user,
            service_id,
            minimum_version,
            output_table,
            output_size);
    return status;
}

DlumaStatus dluma_api_runtime_initialize(
    DlumaApiRuntime* runtime,
    DlumaModOwner* owner,
    DlumaCapabilityRegistry* capabilities,
    const DlumaApiBackendV1* backend)
{
    if (!runtime || !owner || !capabilities || !backend ||
        owner->state != DLUMA_MOD_OWNER_ACTIVE ||
        backend->struct_size < sizeof(*backend) ||
        backend->struct_version != 1U ||
        !backend->current_thread_id ||
        !backend->invoke)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(runtime, 0, sizeof(*runtime));
    runtime->struct_size = (uint32_t)sizeof(*runtime);
    runtime->struct_version = 1U;
    runtime->owner = owner;
    runtime->capabilities = capabilities;
    runtime->backend = *backend;
    runtime->api.struct_size = (uint32_t)sizeof(runtime->api);
    runtime->api.abi_version = DLUMA_API_ABI_VERSION;
    runtime->api.api_major = 1U;
    runtime->api.api_minor = 0U;
    runtime->api.catalog_sha256 = DLUMA_API_CATALOG_SHA256;
    runtime->api.context = runtime;
    runtime->api.get_service = dluma_api_runtime_get_service;
    return DLUMA_STATUS_OK;
}

const DlumaApiV1* dluma_api_runtime_api(DlumaApiRuntime* runtime)
{
    if (!runtime || runtime->struct_version != 1U) return 0;
    return &runtime->api;
}

DlumaStatus dluma_api_runtime_invoke(
    DlumaApiRuntime* runtime,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result_value,
    uint32_t result_size)
{
    const DlumaOperationPolicy* policy =
        dluma_api_operation_policy(operation_id);
    const DlumaSizedRecordPrefix* request_prefix =
        (const DlumaSizedRecordPrefix*)request;
    DlumaResultRecordPrefix* result =
        (DlumaResultRecordPrefix*)result_value;
    DlumaStatus status;
    uint32_t thread_id;
    DlumaError* caller_error = 0;

    if (!runtime || !policy || !request || !result ||
        request_size < sizeof(*request_prefix) ||
        result_size < sizeof(*result) ||
        request_prefix->struct_size < request_size ||
        request_prefix->struct_version != 1U ||
        result->struct_size < result_size ||
        result->struct_version != 1U) {
        if (result && result_size >= sizeof(*result))
            dluma_api_publish_result(result, DLUMA_STATUS_INVALID_ARGUMENT, policy);
        return DLUMA_STATUS_INVALID_ARGUMENT;
    }

    caller_error = result->call.error;
    memset(&result->call, 0, sizeof(result->call));
    result->call.error = caller_error;
    if (policy->catalog_status == DLUMA_OPERATION_REMOVED) {
        dluma_api_publish_result(result, DLUMA_STATUS_REMOVED, policy);
        return DLUMA_STATUS_REMOVED;
    }
    if (policy->catalog_status == DLUMA_OPERATION_PLANNED) {
        dluma_api_publish_result(result, DLUMA_STATUS_UNAVAILABLE, policy);
        return DLUMA_STATUS_UNAVAILABLE;
    }

    thread_id = runtime->backend.current_thread_id(runtime->backend.user);
    status = dluma_mod_owner_require(
        runtime->owner,
        policy->scope_mask,
        policy->access_id,
        policy->thread_affinity == DLUMA_OPERATION_THREAD_GAME,
        thread_id);
    if (status != DLUMA_STATUS_OK) {
        dluma_api_publish_result(result, status, policy);
        return status;
    }
    if (!dluma_capability_registry_ready(
            runtime->capabilities,
            policy->capability_id)) {
        dluma_api_publish_result(result, DLUMA_STATUS_UNAVAILABLE, policy);
        return DLUMA_STATUS_UNAVAILABLE;
    }

    status = dluma_api_validate_request(
        runtime->owner, operation_id, request);
    if (status != DLUMA_STATUS_OK) {
        dluma_api_publish_result(result, status, policy);
        return status;
    }

    if (operation_id == 1002U) {
        const DlumaCapabilityQueryRequest* capability_request =
            (const DlumaCapabilityQueryRequest*)request;
        status = dluma_capability_registry_query(
            runtime->capabilities,
            capability_request->capability_id,
            (DlumaCapabilityQueryResult*)result);
    } else {
        status = runtime->backend.invoke(
            runtime->backend.user,
            runtime->owner,
            operation_id,
            request,
            request_size,
            result,
            result_size);
    }
    dluma_api_publish_result(result, status, policy);
    return status;
}
