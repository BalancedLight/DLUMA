#ifndef DLUMA_API_RUNTIME_H
#define DLUMA_API_RUNTIME_H

#include "../../sdk/c/dluma_api.h"
#include "dlw_capability_registry.h"
#include "dlw_mod_owner.h"

typedef uint32_t (*DlumaApiCurrentThreadIdFn)(void* user);
typedef DlumaStatus (*DlumaApiBackendInvokeFn)(
    void* user,
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size);
typedef DlumaStatus (*DlumaApiBackendGetServiceFn)(
    void* user,
    DlumaStringView service_id,
    uint32_t minimum_version,
    void* output_table,
    uint32_t output_size);

typedef struct DlumaApiBackendV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    void* user;
    DlumaApiCurrentThreadIdFn current_thread_id;
    DlumaApiBackendInvokeFn invoke;
    DlumaApiBackendGetServiceFn get_service;
} DlumaApiBackendV1;

typedef struct DlumaApiRuntime {
    uint32_t struct_size;
    uint32_t struct_version;
    DlumaModOwner* owner;
    DlumaCapabilityRegistry* capabilities;
    DlumaApiBackendV1 backend;
    DlumaApiV1 api;
} DlumaApiRuntime;

#ifdef __cplusplus
extern "C" {
#endif

DlumaStatus dluma_api_runtime_initialize(
    DlumaApiRuntime* runtime,
    DlumaModOwner* owner,
    DlumaCapabilityRegistry* capabilities,
    const DlumaApiBackendV1* backend);
const DlumaApiV1* dluma_api_runtime_api(DlumaApiRuntime* runtime);
DlumaStatus dluma_api_runtime_invoke(
    DlumaApiRuntime* runtime,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size);
int dluma_api_runtime_operation_context(
    uint32_t operation_id,
    const char** capability_name,
    const char** access_name);
int dluma_api_string_equals_literal(
    DlumaStringView value,
    const char* literal);
int dluma_api_copy_service_table(
    void* output,
    uint32_t output_size,
    const void* table,
    uint32_t table_size);

#ifdef __cplusplus
}
#endif

#endif
