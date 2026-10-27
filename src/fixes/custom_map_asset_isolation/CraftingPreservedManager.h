#ifndef DLUMA_FIXES_CRAFTING_PRESERVED_MANAGER_H
#define DLUMA_FIXES_CRAFTING_PRESERVED_MANAGER_H
#include "CraftingManagerSnapshot.h"
namespace dluma::fixes::custom_map_asset_isolation::crafting_owner {
constexpr std::size_t kMaximumManagers=4096;
enum class Result:std::uint8_t {Succeeded,InvalidArgument,WrongScope,BaselineUnavailable,BaselineChanged,UnknownManager,
    CapacityExceeded,WriteFailed,NativeFailed,OwnerChanged,BorrowersActive};
struct Manager {std::uintptr_t address;std::uint64_t generation;std::uint32_t epoch;bool held,baseline,retired;};
struct Context {
    std::uintptr_t game_di,current_manager;
    std::uint64_t game_di_generation,revision,baseline_generation,transaction;
    std::uint32_t epoch,thread_id;
    bool writers_closed,initial_pristine,post_c10_closed;
};
using ContextFn=bool(*)(void*,Context*) noexcept;
using OriginalRebuildFn=bool(*)(void*,std::uintptr_t) noexcept;
using CompareFn=bool(*)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t) noexcept;
using BaselinePublishFn=bool(*)(void*,bool begin) noexcept;
struct Access {std::uintptr_t gamedll_base;char sha256[65];stored_blueprints::ReadFn read;ContextFn context;CompareFn compare;
    OriginalRebuildFn original_rebuild;BaselinePublishFn publish_baseline;void* user;};
struct Owner {
    Access access;Manager* managers;std::size_t capacity,count;
    crafting_snapshot::Snapshot baseline;
    std::uintptr_t game_di,baseline_manager,current_manager;
    std::uint64_t game_di_generation,baseline_generation,next_manager_generation,last_transaction;
    Result result;bool initialized,baseline_captured,lost,restored;
};
bool Initialize(Owner*,const Access&,Manager*,std::size_t,const crafting_snapshot::Snapshot&) noexcept;
// Requires an actual pristine/native-writer closure from the production gate.
Result CaptureBaseline(Owner*) noexcept;
// The native rebuild runs exactly once. Successful preparation removes only
// the old owning root before that original starts; its graph remains held.
// Failures record lost authority and do not pretend a freed baseline survived.
Result RunNativeRebuild(Owner*,std::uintptr_t actual_receiver) noexcept;
// Restore the already-held B0 graph after C10. All other graphs remain held;
// full Item consumer rebinding must separately qualify their retirement.
Result RestoreBaselineRoot(Owner*,std::uint64_t transaction) noexcept;
Result VerifyBaselineRoot(Owner*,std::uint64_t transaction) noexcept;
bool HeldManager(const Owner*,std::uintptr_t,Manager*) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_owner
#endif
