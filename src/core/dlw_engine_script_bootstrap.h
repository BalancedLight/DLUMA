#ifndef DLW_ENGINE_SCRIPT_BOOTSTRAP_H
#define DLW_ENGINE_SCRIPT_BOOTSTRAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*DlumaEngineScriptBootstrapReadFn)(void*,uintptr_t,void*,size_t);
typedef enum DlumaEngineScriptBootstrapFailure {
    DLUMA_EBS_NONE,DLUMA_EBS_INVALID_ARGUMENT,DLUMA_EBS_UNSUPPORTED_IMAGE,
    DLUMA_EBS_NOT_BEFORE_INITIALIZER,DLUMA_EBS_ADDRESS_OVERFLOW,DLUMA_EBS_ORDER_MISMATCH,
    DLUMA_EBS_CALLBACK_UNREADABLE,DLUMA_EBS_CALLBACK_MISMATCH,DLUMA_EBS_MANAGER_UNREADABLE,
    DLUMA_EBS_MANAGER_PUBLISHED,DLUMA_EBS_FACTORY_UNREADABLE,DLUMA_EBS_FACTORY_PUBLISHED,
    DLUMA_EBS_CALLER_MISMATCH,DLUMA_EBS_THREAD_MISMATCH,DLUMA_EBS_FACTORY_ORDER_MISMATCH
} DlumaEngineScriptBootstrapFailure;
typedef struct DlumaEngineScriptBootstrapInput {
    uintptr_t engine_base,proxy_initializer,actual_return_address;
    uint32_t entry_thread,current_thread;
    int exact_engine_profile,before_original_initializer;
    DlumaEngineScriptBootstrapReadFn read_memory;
    void* user;
} DlumaEngineScriptBootstrapInput;
typedef struct DlumaEngineScriptBootstrapResult {
    DlumaEngineScriptBootstrapFailure failure;
    uint32_t callback_owned,manager_unpublished,factory_unavailable,entry_validated;
} DlumaEngineScriptBootstrapResult;
/* Read-only A18 engine publication-stage checks. exact_engine_profile comes
 * from the authoritative full-image profile. before_original_initializer must
 * describe the current lexical callback phase, not cached startup history.
 * Reader checks module bounds; no engine/game call, allocation or patch occurs.
 * Publication-only preserves the established inventory proof's behavior and
 * NEVER establishes actual entry; new hook installers must use entered().
 * entered() also requires the real outer export's recorded _ReturnAddress and
 * entry thread (dluma_isolation_snapshot_script_entry), current thread, and
 * factory-publication code. Never substitute a helper frame's return address.
 * Failure clears evidence; successful evidence is valid only in that current
 * callback. Neither result pins actors/UI or proves domain-specific absence,
 * creator coverage, routing ownership or permission to mutate/install.
 */
int dluma_engine_script_bootstrap_unpublished(const DlumaEngineScriptBootstrapInput*,DlumaEngineScriptBootstrapResult*);
int dluma_engine_script_bootstrap_entered(const DlumaEngineScriptBootstrapInput*,DlumaEngineScriptBootstrapResult*);
#ifdef __cplusplus
}
#endif
#endif
