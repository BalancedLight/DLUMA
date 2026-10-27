#ifndef DLUMA_AUDIO_SOUND_PROPERTY_NATIVE_H
#define DLUMA_AUDIO_SOUND_PROPERTY_NATIVE_H
#include "AudioSoundPropertySnapshot.h"

namespace dluma::fixes::custom_map_asset_isolation::audio_sound_properties {
enum class NativeOperation : std::uint8_t { ClearAliases, StoreValue, RemoveProperty, RegisterAlias };
using AuthorizeFn = bool (*)(void*, NativeOperation, std::uintptr_t,
                            const char*, const char*) noexcept;
struct NativeBinding {
    Access access;
    AuthorizeFn authorize;
    CanRemoveFn can_remove_extra;
    void* owner_user;
    bool validated;
};
// Exact entry-prefix and image qualification happens before this binding is
// exposed. Authorization still comes from the active Audio owner on each call.
bool ValidateNativeBinding(NativeBinding*, const char* exact_engine_sha256) noexcept;
RestoreOperations MakeNativeRestoreOperations(NativeBinding*) noexcept;
} // namespace
#endif
