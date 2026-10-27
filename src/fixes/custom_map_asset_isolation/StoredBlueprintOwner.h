#ifndef DLUMA_FIXES_STORED_BLUEPRINT_OWNER_H
#define DLUMA_FIXES_STORED_BLUEPRINT_OWNER_H
#include "CurveBorrowBoundary.h"
#include <cstddef>
#include <cstdint>
namespace dluma::fixes::custom_map_asset_isolation::stored_blueprints {
constexpr std::size_t kBlueprintBytes=176,kMaximumOwners=2,kMaximumCopies=65536,kMaximumBindings=131072;
constexpr std::uintptr_t kBlueprintVtableRva=0x15618A8,kItemContextVtableRva=0x163D3B8;
enum class Result:std::uint8_t {Succeeded,InvalidArgument,ProfileMismatch,WrongScope,OwnerChanged,
    InvalidGraph,ReadFailed,UnownedBlueprint,CapacityExceeded,WriteFailed,OwnedPayloadChanged};
struct PlayerOwner {std::uintptr_t state;std::uint64_t generation;};
struct Fence {
    std::uintptr_t game_di,session,blueprint_store;
    PlayerOwner players[kMaximumOwners];std::uint32_t player_count,epoch,thread_id;
    std::uint64_t teardown_generation,frame_route_token,owner_revision;
};
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t) noexcept;
using ComparePointerFn=bool(*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t) noexcept;
// Root returns a fence only in its qualified pre-original SessionOnDestroy
// callback, while the original session and cached process-owner graph are live.
using FenceFn=bool(*)(void*,Fence*) noexcept;
struct Access {
    std::uintptr_t gamedll_base;char sha256[65];ReadFn read;ComparePointerFn compare_pointer;
    FenceFn fence;void* user;curves::CurveBorrowBoundary* borrowers;
    const curves::CurveNativeScope* teardown_scope;
};
struct BlueprintCopy {
    alignas(8) std::uint8_t native[kBlueprintBytes];
    std::uint8_t captured[kBlueprintBytes];
    std::uintptr_t original,session;std::uint64_t id,teardown_generation;
    std::uint32_t epoch;bool published;
};
struct Binding {std::uintptr_t address,expected,desired;};
struct Owner {
    // Native payload addresses must stay allocated for the process lifetime.
    // Root uses stable owned storage; no native object destructor owns copies.
    BlueprintCopy* copies;std::size_t copy_capacity,copy_count;
    Binding* work;std::size_t work_capacity,work_count;
    Fence last_fence;Result result;bool initialized;
};
bool Initialize(Owner*,BlueprintCopy*,std::size_t,Binding*,std::size_t) noexcept;
// Copies the entire exact scalar payload and redirects only stored params
// pointers. No extra native constructor, serializer or teardown is invoked.
Result PinBeforeSessionDestroy(Owner*,const Access&) noexcept;
// Safe after the native session store has gone: reads only our owned copy.
// The full Item owner must separately restore derived payload baseline values.
bool ValidateOwnedBlueprint(const Owner*,std::uintptr_t,std::uint64_t* id=nullptr) noexcept;
bool NativeComparePointer(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::stored_blueprints
#endif
