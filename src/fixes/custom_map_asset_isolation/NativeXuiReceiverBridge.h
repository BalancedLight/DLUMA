#ifndef DLUMA_NATIVE_XUI_RECEIVER_BRIDGE_H
#define DLUMA_NATIVE_XUI_RECEIVER_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Internal observation only. All identities must originate in a qualified,
 * serialized native callback; never pass values supplied by a mod. The caller
 * owns the receiver lifetime and must not retain its pointer. A successful
 * snapshot is not a pin or proof of type. expected_manager may be null.
 * expected_instance_generation is zero for a new unique-current lookup; the
 * returned manager_generation must be checked separately by manager owners.
 * level_interface is the session ILevel interface, not its native level.
 * The exact-profile bridge reads the native identity for manager matching. */
typedef struct DlumaXuiReceiverObservation {
    uint64_t manager_generation;
    uint64_t instance_generation;
    uint64_t load_serial;
    uint32_t epoch;
    uint32_t origin;
    uint8_t exact_input;
    uint8_t sha256[32];
} DlumaXuiReceiverObservation;
int dluma_xui_observe_borrowed_receiver(const void* receiver,const void* level_interface,
    const void* expected_manager,uint64_t expected_instance_generation,
    DlumaXuiReceiverObservation* output);
#ifdef __cplusplus
}
#endif
#endif
