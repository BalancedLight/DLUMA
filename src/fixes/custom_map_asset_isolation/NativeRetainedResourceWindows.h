#ifndef DLUMA_NATIVE_RETAINED_RESOURCE_WINDOWS_H
#define DLUMA_NATIVE_RETAINED_RESOURCE_WINDOWS_H
#include "NativeRetainedResourceOwner.h"

namespace dluma::fixes::custom_map_asset_isolation::retained::windows {
constexpr char kPlayerEngineSha256[] = "a18ca1103614947ae7ff956c6852f45123b440563a967d9e1e496c42fa574a3c";
constexpr std::uintptr_t kTextureManagerRva = 0xB4F0A0;
constexpr std::uintptr_t kTextureManagerVtableRva = 0x942828;
constexpr std::uintptr_t kTextureVtableRva = 0x9427F8;
constexpr std::uintptr_t kTextureDestroyRva = 0x7ACEB0;
constexpr std::uintptr_t kTextureLoadRva = 0x7AB1A0;
constexpr std::uintptr_t kTextureConstructorRva = 0x7AB740;
constexpr std::uintptr_t kTextureReleaseRva = 0x7AD080;
constexpr std::uintptr_t kRendererGlobalRva = 0xACCAA0;
constexpr std::uintptr_t kRendererVtableRva = 0x940F58;
constexpr std::uintptr_t kRendererAcquireRva = 0x7A1640;
constexpr std::uintptr_t kRendererReleaseRva = 0x7A16C0;
using ReadFn = bool (*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
using DestroyTextureFn = void (__fastcall*)(void*, void*);
struct Binding {
    std::uintptr_t engine_base;
    char engine_sha256[65];
    ReadFn read;
    void* read_user;
    FenceFn fence;
    void* fence_user;
    Ledger* ledger;
    // Captured original relay from the installed destroy route, or the exact
    // native entry before any detour. Never call a hook that records this same
    // explicit destruction twice.
    DestroyTextureFn original_destroy;
    std::uintptr_t gate_renderer;
    std::uint32_t gate_thread;
    bool renderer_section_owned, renderer_acquired, texture_section_owned;
    bool validated;
};
Result Validate(Binding*) noexcept;
// Fails immediately if native renderer/texture locks cannot be acquired. The
// integration fence supplies observed post-C10 authority and completed native
// producer/borrower/worker receipts; its native_lock_owned flag is ignored.
// This module obtains that part of the proof by acquiring the real native locks.
Result EnterTextureGate(Binding*) noexcept;
void LeaveTextureGate(Binding*) noexcept;
Result CaptureTextures(Binding*, Inventory*) noexcept;
Operations MakeTextureOperations(Binding*) noexcept;
} // namespace
#endif
