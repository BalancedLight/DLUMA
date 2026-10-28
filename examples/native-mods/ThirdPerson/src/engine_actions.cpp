#include "engine_actions.h"

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace thirdperson {
bool specialActionActive(void* player)
{
    if (!player) return false;
    auto* p = static_cast<unsigned char*>(player);
    auto* grab = *reinterpret_cast<unsigned char**>(p + 3032);
    const auto grabType = *reinterpret_cast<const std::uint32_t*>(p + 1652);
    if (grab && grab[16] && grabType <= 16) return true;

    auto* stomp = *reinterpret_cast<unsigned char**>(p + 3112);
    if (stomp && stomp[16]) return true;

    auto* defensive = *reinterpret_cast<unsigned char**>(p + 3192);
    if (defensive && defensive[16]) {
        const auto state = *reinterpret_cast<const std::uint32_t*>(defensive + 56);
        if (state >= 5 && state <= 7) return true;
    }

    auto* pvp = *reinterpret_cast<unsigned char**>(p + 3240);
    return pvp && pvp[16] && *reinterpret_cast<void**>(pvp + 616) &&
        *reinterpret_cast<void**>(pvp + 624) == player;
}

namespace {
using VisualClass = void* (__fastcall*)(void*);
using CameraParameter = unsigned char (__fastcall*)(void*, unsigned int, void*);
std::atomic<ActionVisualClassBridge*> current{nullptr};
std::atomic<unsigned> callbacks{0};
std::atomic<VisualClass> originalClass{nullptr};
std::atomic<CameraParameter> originalCameraParameter{nullptr};
std::atomic_flag registrationLock = ATOMIC_FLAG_INIT;

struct RegistrationGuard {
    RegistrationGuard() { while (registrationLock.test_and_set(std::memory_order_acquire)) SwitchToThread(); }
    ~RegistrationGuard() { registrationLock.clear(std::memory_order_release); }
};

// The callback code is pinned for the lifetime of Player before publication:
// a thread already entering the vtable method may resume after unregistration.
void* __fastcall visualClassHook(void* host);
unsigned char __fastcall cameraParameterHook(void* target, unsigned int parameter, void* result);
}

// Access through a friend would expose the hook in the header; keep the
// state lookup in a small private implementation owned by this translation unit.
struct VisualClassState {
    unsigned char* game;
    void* context;
    ActionVisualClassBridge::LocalPlayer player;
    ActionVisualClassBridge::WantsFirstPersonClass enabled;
};
namespace {
VisualClassState state{};

void* __fastcall visualClassHook(void* host)
{
    callbacks.fetch_add(1, std::memory_order_acquire);
    void* result = originalClass.load(std::memory_order_acquire)(host);
    if (current.load(std::memory_order_acquire)) {
        void* player = state.player(state.context);
        if (player && host == static_cast<unsigned char*>(player) + 832 &&
            state.enabled(state.context))
            result = state.game + 0x1D88790;
    }
    callbacks.fetch_sub(1, std::memory_order_release);
    return result;
}

unsigned char __fastcall cameraParameterHook(void* target, unsigned int parameter, void* result)
{
    callbacks.fetch_add(1, std::memory_order_acquire);
    bool useDefault = false;
    if (target && (parameter == 5 || parameter == 8) &&
        current.load(std::memory_order_acquire)) {
        auto* cameraTarget = static_cast<unsigned char*>(target);
        void* player = state.player(state.context);
        // The camera can still refer to the prior visual during a refresh.
        // Identify its local owner without requiring it to be the new visual.
        if (player && *reinterpret_cast<void***>(target) ==
                reinterpret_cast<void**>(state.game + 0x17E2048) &&
            *reinterpret_cast<void**>(cameraTarget - 11472) == player &&
            !*reinterpret_cast<void**>(cameraTarget + 7560))
            useDefault = true;
    }
    const unsigned char value = useDefault ? 0 :
        originalCameraParameter.load(std::memory_order_acquire)(target, parameter, result);
    callbacks.fetch_sub(1, std::memory_order_release);
    return value;
}

bool restoreSlot(void** slot, void* original, void* replacement)
{
    if (!slot) return true;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    void* observed = InterlockedCompareExchangePointer(slot, original, replacement);
    DWORD ignored = 0;
    const bool protectedAgain = VirtualProtect(slot, sizeof(void*), protection, &ignored) != 0;
    return (observed == replacement || observed == original) && protectedAgain;
}
}

bool ActionVisualClassBridge::initialize(unsigned char* game, void* context,
    LocalPlayer localPlayer, WantsFirstPersonClass wantsFirstPersonClass)
{
    RegistrationGuard guard;
    if (!game || !localPlayer || !wantsFirstPersonClass || slot_ || cameraParameterSlot_ ||
        current.load(std::memory_order_acquire)) return false;
    void** slot = reinterpret_cast<void**>(game + 0x17DEDB8) + 2;
    void* expected = game + 0xB01AA0;
    void** cameraSlot = reinterpret_cast<void**>(game + 0x17E2048) + 11;
    void* expectedCamera = game + 0xB7E5F0;
    if (*slot != expected || *cameraSlot != expectedCamera) return false;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&visualClassHook), &module)) return false;

    game_ = game;
    context_ = context;
    localPlayer_ = localPlayer;
    wantsFirstPersonClass_ = wantsFirstPersonClass;
    state = {game, context, localPlayer, wantsFirstPersonClass};
    originalClass.store(reinterpret_cast<VisualClass>(expected), std::memory_order_release);
    originalCameraParameter.store(reinterpret_cast<CameraParameter>(expectedCamera), std::memory_order_release);
    current.store(this, std::memory_order_release);
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) {
        current.store(nullptr, std::memory_order_release);
        return false;
    }
    void* observed = InterlockedCompareExchangePointer(slot,
        reinterpret_cast<void*>(&visualClassHook), expected);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    if (observed != expected) {
        current.store(nullptr, std::memory_order_release);
        return false;
    }
    slot_ = slot;
    if (!VirtualProtect(cameraSlot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    observed = InterlockedCompareExchangePointer(cameraSlot,
        reinterpret_cast<void*>(&cameraParameterHook), expectedCamera);
    VirtualProtect(cameraSlot, sizeof(void*), protection, &ignored);
    if (observed != expectedCamera) return false;
    cameraParameterSlot_ = cameraSlot;
    return true;
}

bool ActionVisualClassBridge::shutdown()
{
    RegistrationGuard guard;
    if (!slot_ && !cameraParameterSlot_) return true;
    const bool cameraRestored = restoreSlot(cameraParameterSlot_,
        reinterpret_cast<void*>(originalCameraParameter.load(std::memory_order_acquire)),
        reinterpret_cast<void*>(&cameraParameterHook));
    const bool classRestored = restoreSlot(slot_,
        reinterpret_cast<void*>(originalClass.load(std::memory_order_acquire)),
        reinterpret_cast<void*>(&visualClassHook));
    current.store(nullptr, std::memory_order_release);
    while (callbacks.load(std::memory_order_acquire)) SwitchToThread();
    if (cameraRestored) cameraParameterSlot_ = nullptr;
    if (classRestored) slot_ = nullptr;
    return cameraRestored && classRestored;
}

} // namespace thirdperson
