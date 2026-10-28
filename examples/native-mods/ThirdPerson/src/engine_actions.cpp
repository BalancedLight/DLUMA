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
std::atomic<ActionVisualClassBridge*> current{nullptr};
std::atomic<unsigned> callbacks{0};
std::atomic<VisualClass> originalClass{nullptr};
std::atomic_flag registrationLock = ATOMIC_FLAG_INIT;

struct RegistrationGuard {
    RegistrationGuard() { while (registrationLock.test_and_set(std::memory_order_acquire)) SwitchToThread(); }
    ~RegistrationGuard() { registrationLock.clear(std::memory_order_release); }
};

// The callback code is pinned for the lifetime of Player before publication:
// a thread already entering the vtable method may resume after unregistration.
void* __fastcall visualClassHook(void* host);
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
}

bool ActionVisualClassBridge::initialize(unsigned char* game, void* context,
    LocalPlayer localPlayer, WantsFirstPersonClass wantsFirstPersonClass)
{
    RegistrationGuard guard;
    if (!game || !localPlayer || !wantsFirstPersonClass || slot_ ||
        current.load(std::memory_order_acquire)) return false;
    void** slot = reinterpret_cast<void**>(game + 0x17DEDB8) + 2;
    void* expected = game + 0xB01AA0;
    if (*slot != expected) return false;
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
    return true;
}

bool ActionVisualClassBridge::shutdown()
{
    RegistrationGuard guard;
    if (!slot_) return true;
    DWORD protection = 0;
    bool restored = false;
    if (VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &protection)) {
        void* observed = InterlockedCompareExchangePointer(slot_,
            reinterpret_cast<void*>(originalClass.load(std::memory_order_acquire)), reinterpret_cast<void*>(&visualClassHook));
        DWORD ignored = 0;
        const bool protectedAgain = VirtualProtect(slot_, sizeof(void*), protection, &ignored) != 0;
        restored = (observed == reinterpret_cast<void*>(&visualClassHook) ||
            observed == reinterpret_cast<void*>(originalClass.load(std::memory_order_acquire))) && protectedAgain;
    }
    current.store(nullptr, std::memory_order_release);
    while (callbacks.load(std::memory_order_acquire)) SwitchToThread();
    if (restored) slot_ = nullptr;
    return restored;
}

} // namespace thirdperson
