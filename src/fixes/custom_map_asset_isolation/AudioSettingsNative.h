#ifndef DLUMA_AUDIO_SETTINGS_NATIVE_H
#define DLUMA_AUDIO_SETTINGS_NATIVE_H
#include "AudioWorkerReset.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::audio_settings_native {
constexpr std::size_t kUserBuses = 3;
constexpr std::uintptr_t kSetBusMaximumRva = 0xFC8C0;
constexpr std::uintptr_t kDspMixImportRva = 0x8576E0;
constexpr std::uintptr_t kDspMixCallerReturnRva = 0xF9207;

enum class Result : std::uint8_t {
    Succeeded, Pending, InvalidArgument, WrongOwner, InvalidSettings,
    ReadFailed, Changed, Unauthorized, Busy, NativeFailure, StaleRequest,
};
using ReadFn = bool(*)(void*, std::uintptr_t, void*, std::size_t) noexcept;
using OwnerFn = bool(*)(void*, std::uintptr_t*, std::uint64_t*) noexcept;
using AuthorizeFn = bool(*)(void*, std::uint64_t transaction, std::uint32_t epoch, bool mutation) noexcept;
using SetMaximumFn = bool(*)(void*, std::uint64_t, std::uint32_t, std::uintptr_t, unsigned, float) noexcept;
struct Access {
    std::uintptr_t engine_base;
    ReadFn read;
    OwnerFn owner;
    AuthorizeFn authorize;
    SetMaximumFn set_maximum;
    void* user;
    bool exact_engine_profile;
};
struct UserGains {
    std::uintptr_t game, settings, audio;
    std::uint64_t owner_generation;
    std::int32_t levels[5]; // FX, music, speech, movie, movie-source selector.
    float grain, maximum[kUserBuses];
    bool complete;
};
struct Receipt {
    std::uintptr_t connection;
    std::uint64_t owner_generation, worker_before;
    float observed_mix;
    bool succeeded;
};
struct State {
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    UserGains desired = {};
    Receipt receipts[kUserBuses] = {};
    std::uint64_t request = 0, transaction = 0, worker_at_apply = 0;
    std::uint32_t epoch = 0;
    bool active = false, setters_finished = false, native_failed = false;
};
// Reads current user values. It does not substitute startup defaults or change
// the AudioSettings object, backend, output device or global script selector.
Result Capture(const Access&, UserGains*) noexcept;
// authorize must be the actual Audio owner callback/transaction fence. Native
// setter success is not completion: the matching mixer receipts are required.
Result Apply(State*, const Access&, const UserGains&, std::uint64_t transaction,
    std::uint32_t epoch, std::uint64_t completed_worker_count, std::uint64_t* request) noexcept;
// All inputs below are actual native observations. The leaf IAT shim supplies
// R12 (the loop's bus index), preserving the native float in XMM1 and caller.
void ObserveMix(State*, std::uintptr_t engine_base, std::uintptr_t caller,
    const audio_worker_reset::WorkerFrame&, std::uint64_t owner_generation,
    std::uint64_t observed_bus, std::uintptr_t connection, float observed_mix,
    int native_result) noexcept;
Result Verify(State*, const Access&, std::uint64_t request,
    std::uint64_t completed_worker_count) noexcept;
void Cancel(State*, std::uint64_t request) noexcept;
bool NativeSetMaximum(const Access&, std::uint64_t transaction, std::uint32_t epoch,
    std::uintptr_t, unsigned, float) noexcept;
} // namespace
#endif
