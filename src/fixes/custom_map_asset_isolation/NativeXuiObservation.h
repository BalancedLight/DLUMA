#ifndef DLUMA_NATIVE_XUI_OBSERVATION_H
#define DLUMA_NATIVE_XUI_OBSERVATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int dluma_xui_native_frame(void** level,uintptr_t* manager);
/* Internal only: caller must already own an exact qualified native UI callback
 * borrow. This query is observation, never an independent lifetime grant. */
int dluma_xui_native_borrow_context(void** level,uintptr_t* manager);
uint64_t dluma_xui_native_manager_generation(uintptr_t manager);
#ifdef __cplusplus
}
#endif
#endif
