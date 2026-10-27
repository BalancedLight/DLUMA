#ifndef DLUMA_ISOLATION_STARTUP_H
#define DLUMA_ISOLATION_STARTUP_H
#include "dlw_win32.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DlumaIsolationScriptEntryObservation {
    const void* return_address;
    const void* callback_address;
    DWORD thread_id;
} DlumaIsolationScriptEntryObservation;
/* Capture real entry evidence before host discovery or original initialization.
 * Reentrant/repeated entry cannot manufacture a new clean-startup window. */
void dluma_isolation_note_script_entry(const void* return_address,
    const void* callback_address, DWORD thread_id);
int dluma_isolation_snapshot_script_entry(DlumaIsolationScriptEntryObservation* output);
#ifdef __cplusplus
}
#endif
#endif
