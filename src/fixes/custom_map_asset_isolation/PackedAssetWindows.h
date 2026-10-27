#ifndef DLUMA_PACKED_ASSET_WINDOWS_H
#define DLUMA_PACKED_ASSET_WINDOWS_H
#include "PackedAssetOwner.h"
#include "NativeRetainedResourceWindows.h"

namespace dluma::fixes::custom_map_asset_isolation::packed::windows {
constexpr std::uintptr_t kRuntimeGlobalRva = 0xACCDA8;
constexpr std::uintptr_t kRuntimeVtableRva = 0x8F6CF8;
constexpr std::uintptr_t kAssetManagerRva = 0xB32A30;
constexpr std::uintptr_t kAssetManagerVtableRva = 0x8F4C58;
constexpr std::uintptr_t kAssetReleaseRva = 0x4507D0;
constexpr std::uintptr_t kPackReleaseRva = 0x46CF60;
constexpr std::uintptr_t kPackEvictRva = 0x471FA0;
enum class NativeOperation : std::uint8_t { Evict, ReleaseReference, ReleaseEmptyPack };
using ReadFn = retained::windows::ReadFn;
using FenceFn = bool (*)(void*, Fence*) noexcept;
using AuthorizeFn = bool (*)(void*, NativeOperation, Key, bool entering) noexcept;
using EvictPackFn = void (__fastcall*)(void*, void*);
using ReleaseReferenceFn = void (__fastcall*)(void*, void*);
using ReleasePackFn = void (__fastcall*)(void*, void*, bool);
using StreamProofFn = bool(*)(void*,const NativePack&,const retained::NativeState*,std::size_t) noexcept;
using StreamMutationFn = bool(*)(void*,bool entering,bool native_returned) noexcept;
struct Binding {
    std::uintptr_t engine_base;
    char engine_sha256[65];
    ReadFn read; void* read_user;
    FenceFn producer_fence; AuthorizeFn authorize; void* gateway_user;
    Ledger* ledger; retained::Ledger* texture_ledger;
    EvictPackFn original_evict;
    ReleaseReferenceFn original_release_reference;
    ReleasePackFn original_release_pack;
    std::uintptr_t runtime, renderer;
    std::uint32_t owner_thread;
    bool worker_section_owned, renderer_section_owned, renderer_acquired, texture_section_owned;
    bool validated;
    StreamProofFn stream_proof;
    StreamMutationFn stream_mutation;
    void* stream_user;
};
Result Validate(Binding*) noexcept;
Result EnterGate(Binding*) noexcept;
void LeaveGate(Binding*) noexcept;
bool ReadFence(Binding*, Fence*) noexcept;
Result Capture(Binding*, Snapshot*) noexcept;
Operations MakeOperations(Binding*) noexcept;
} // namespace
#endif
