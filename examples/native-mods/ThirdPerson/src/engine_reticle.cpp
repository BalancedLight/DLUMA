#include "engine_reticle.h"

#include <windows.h>
#include <MinHook.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace thirdperson::reticle {
namespace {
using SetType = void (__fastcall*)(void*, int);
using WeaponType = std::uint64_t (__fastcall*)(void*);

template<class T> T& field(void* object, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}

std::atomic<void*> eligiblePlayer{nullptr};
unsigned char* game = nullptr;
SetType original = nullptr;
bool created = false;
bool published = false;
bool enabled = false;

bool focused()
{
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

void __fastcall setType(void* hud, int type)
{
    const void* caller = _ReturnAddress();
    void* expected = eligiblePlayer.load(std::memory_order_acquire);
    // This call site is reached only after the native HUD and difficulty
    // visibility checks, in the branch that hides an aiming firearm.
    if (type == 0 && expected && hud && game && caller == game + 0x4CF6D7 &&
        !*(game + 0x1D0791C) && focused()) {
        void* gameInstance = *reinterpret_cast<void**>(game + 0x1D07F88);
        void* session = gameInstance ? field<void*>(gameInstance, 1288) : nullptr;
        void* manager = session ? field<void*>(session, 184) : nullptr;
        if (manager && field<void*>(manager, 80) && field<void*>(manager, 88) == expected) {
            void* weapon = field<void*>(expected, 3128);
            if (weapon && field<unsigned char>(weapon, 16)) {
                const auto weaponType = reinterpret_cast<WeaponType>(game + 0xD3C8D0)(weapon);
                // Type 4 belongs to the native bow path and remains untouched.
                if (weaponType > 0 && weaponType <= 5 && weaponType != 4)
                    type = static_cast<int>(weaponType);
            }
        }
    }
    original(hud, type);
}
} // namespace

void setEligiblePlayer(void* player)
{
    eligiblePlayer.store(player, std::memory_order_release);
}

bool install(unsigned char* module)
{
    constexpr unsigned char prologue[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xec, 0x20, 0x48, 0x63, 0x81, 0x58, 0x01, 0x00};
    if (!module || (game && game != module) || enabled ||
        std::memcmp(module + 0x4CF920, prologue, sizeof(prologue)) != 0) return false;
    setEligiblePlayer(nullptr);
    game = module;
    if (!created) {
        if (MH_CreateHook(game + 0x4CF920, reinterpret_cast<void*>(setType),
                reinterpret_cast<void**>(&original)) != MH_OK) return false;
        created = true;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&setType), &self)) {
        if (!published && MH_RemoveHook(game + 0x4CF920) == MH_OK) {
            created = false;
            original = nullptr;
        }
        return false;
    }
    // Keep the trampoline alive even if a callback was delayed before entry.
    published = true;
    if (MH_EnableHook(game + 0x4CF920) != MH_OK) return false;
    enabled = true;
    return true;
}

bool disable()
{
    setEligiblePlayer(nullptr);
    if (!enabled) return true;
    if (MH_DisableHook(game + 0x4CF920) != MH_OK) return false;
    enabled = false;
    return true;
}

} // namespace thirdperson::reticle
