#ifndef DLUMA_MATERIAL_BASELINE_OWNER_H
#define DLUMA_MATERIAL_BASELINE_OWNER_H
#include "NativeResourceGateway.h"
extern "C" {
#include "../../dlw_common.h"
}

namespace dluma::fixes::custom_map_asset_isolation::material_baseline {
constexpr std::size_t kMaximumLoads=64,kMaximumContainers=8,kMaximumWrappers=8192,kMaximumRegisteredPacks=64;
constexpr std::uint64_t kMaximumConsumedBytes=128U*1024U*1024U;
enum class Result : std::uint8_t {
    Succeeded, InvalidArgument, Incomplete, Capacity, NativeReadFailed, UnknownInput,
    ProviderChanged, BaselineUnavailable, BaselineChanged, UnclosedConsumer,
    ReplayUnavailable, NativeReloadFailed, VerificationFailed,
};
struct Container {
    std::uintptr_t object;
    std::uint32_t ordinal,flags,magic;
    file_lifetime::FileRecord opened_file;
    file_lifetime::SourceKey selected_source;
    retained::Realm realm;
    std::uint32_t epoch;
    bool opened,provider_known,read_failed;
};
struct Load {
    std::uintptr_t pack;
    std::uint64_t generation,bytes;
    std::uint32_t epoch,thread,containers,events;
    DlumaSha256Context digest;
    Container inputs[kMaximumContainers];
    unsigned char sha256[32];
    char path[retained::kNameCapacity];
    bool active,complete,loaded,retired,failed,project;
};
struct Wrapper {
    std::uintptr_t object,record,owner_pack;
    std::uint32_t key,references,flags;
    std::uint16_t variant;
    char name[retained::kNameCapacity];
};
struct Snapshot {
    Wrapper* wrappers;std::size_t capacity,count;
    std::uintptr_t manager,global_pack,local_pack;
    std::uintptr_t packs[kMaximumRegisteredPacks];std::size_t pack_count,local_index;
    packed::Fence fence;bool complete;
};
struct Baseline {
    Load input;
    Wrapper* wrappers;std::size_t capacity,count;
    std::uintptr_t manager,global_pack,packs[kMaximumRegisteredPacks];
    std::size_t pack_count,local_index;
    std::uint64_t generation;
    bool captured,local_absent;
};
using ReadFn=resource_gateway::ReadFn;
using FenceFn=bool(*)(void*,packed::Fence*)noexcept;
struct State {
    resource_gateway::State* gateway=nullptr;std::uintptr_t engine_base=0;
    ReadFn read=nullptr;FenceFn fence=nullptr;void* user=nullptr;
    Load loads[kMaximumLoads]={};std::uint64_t next_generation=0;
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::atomic<bool> lost{false},global_pack_mutated{false};
    bool baseline_wrapper_released[kMaximumWrappers]={};
    Baseline baseline={};
    std::uint64_t absent_reset_transaction=0;std::uint32_t absent_reset_epoch=0;
};
// Wire directly into the gateway's actual material/decoded-buffer callbacks.
void Observe(void*,const resource_gateway::MaterialEvent&)noexcept;
bool ObserveFile(void*,std::uintptr_t material,const file_lifetime::FileRecord&,
    file_lifetime::SourceKey,retained::Realm,std::uint32_t epoch)noexcept;
Result Capture(State*,std::uintptr_t manager,Snapshot*)noexcept;
Result CaptureBaseline(State*,const Snapshot&,std::uint64_t generation)noexcept;
Result Preflight(State*,const Snapshot&)noexcept;
// The root creates an exact registry replay permit for this baseline input,
// including nested native file factories. A plain recursion-depth bypass is
// not an implementation of this callback.
using ReplayFn=bool(*)(void*,const Baseline&,bool entering)noexcept;
using ReloadFn=unsigned(__fastcall*)(void*);
struct Operations {void* user;ReplayFn replay;ReloadFn original_reload;};
Result Restore(State*,Snapshot*,const Operations&)noexcept;
bool SameInput(const Load&,const Load&)noexcept;
// Strict split branch: B0 has no local pack and the outgoing local pack has no
// surviving material wrappers. It performs no load or fallback material bind.
using UnloadLocalFn=bool(*)(void*,std::uintptr_t manager,std::uintptr_t pack,std::uint64_t load_generation)noexcept;
Result PreflightAbsentLocal(State*,const Snapshot&)noexcept;
Result ResetAbsentLocal(State*,Snapshot*,UnloadLocalFn,void*)noexcept;
Result VerifyAbsentLocal(State*,const Snapshot&)noexcept;
bool MatchUnborrowedLocal(State*,const Snapshot&,std::uintptr_t,std::uint64_t)noexcept;
bool CanRetireSource(State*,file_lifetime::SourceKey,std::uint64_t transaction)noexcept;
bool ReclaimRetiredSource(State*,file_lifetime::SourceKey,std::uint64_t transaction)noexcept;
} // namespace
#endif
