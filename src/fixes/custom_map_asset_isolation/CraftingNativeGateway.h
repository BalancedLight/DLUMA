#ifndef DLUMA_FIXES_CRAFTING_NATIVE_GATEWAY_H
#define DLUMA_FIXES_CRAFTING_NATIVE_GATEWAY_H
#include "CraftingPreservedManager.h"
#include <atomic>
namespace dluma::fixes::custom_map_asset_isolation::crafting_gateway {
enum class Hook:std::uint8_t {GameConstructor,GameDestructor,Rebuild,DestroyManager,Load,Effects,Sets,Elementals,Count};
constexpr std::size_t kHookCount=static_cast<std::size_t>(Hook::Count);
struct Descriptor {Hook kind;const char* name;std::uintptr_t rva;std::uint8_t prefix[16],stolen;};
using PotentialWriteFn=void(*)(void*,Hook) noexcept;
using FailureFn=void(*)(void*,Hook,crafting_owner::Result) noexcept;
struct Callbacks {stored_blueprints::ReadFn read;crafting_owner::ContextFn context;PotentialWriteFn potential_write;FailureFn failure;void* user;};
struct Lease {std::uint64_t generation;std::uint32_t thread_id;};
struct Gateway {
    std::uintptr_t base=0,originals[kHookCount]={};Callbacks callbacks={};crafting_owner::Owner* owner=nullptr;
    std::atomic<std::uint32_t> writer_thread{0},pending{0};
    std::atomic<std::uint64_t> revision{1},game_generation{0},lease_generation{0};
    std::atomic<std::uintptr_t> game_di{0};std::atomic<bool> lost{false};
    Lease lease={};bool initialized=false;std::atomic<bool> metadata_lease{false};
};
const Descriptor* Catalog(std::size_t*) noexcept;
std::uintptr_t Wrapper(Hook) noexcept;
bool Initialize(Gateway*,std::uintptr_t,const char*,const Callbacks&,const std::uintptr_t*,std::size_t) noexcept;
bool Bind(Gateway*,crafting_owner::Owner*) noexcept;
bool MakeOwnerAccess(Gateway*,crafting_owner::Access*) noexcept;
bool VerifyCoverage(Gateway*) noexcept;
bool Close(Gateway*,Lease*) noexcept;
bool Open(Gateway*,const Lease&) noexcept;
// ABI wrapper boundary: caller is the actual native return address. This
// separate dispatch also permits deterministic offline tests of the same body.
void DispatchRebuild(Gateway*,std::uintptr_t receiver,std::uintptr_t caller) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_gateway
#endif
