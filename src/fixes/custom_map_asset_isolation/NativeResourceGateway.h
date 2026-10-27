#ifndef DLUMA_NATIVE_RESOURCE_GATEWAY_H
#define DLUMA_NATIVE_RESOURCE_GATEWAY_H
#include "PackedAssetWindows.h"
#include "NativeFileHookBridge.h"
#include "ResourceProviderScope.h"
#include "../../dlw_game_frame_authority.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::resource_gateway {
enum class Route : std::uint8_t {
    PackConstruct, PackOpen, PackLoad, PackRegister, PackUnregister, PackDestroy,
    AssetLoad, AssetRelease, PackRelease, TexturePackRegister, TextureConstruct,
    TextureDestroy, TextureLoad, TextureDynamic, TextureRegister, TextureImage,
    TextureCallback, MaterialPackLoad, MaterialReload, MaterialPackDestroy,
    MaterialAcquire, MaterialRebind, DemandSchedule, DemandUnload, StreamResource, DemandEvict,
    ContainerOpen, ContainerFetch, ContainerLoadData, ContainerDataSize, FullPath, MaterialFileRead,
    MaterialRelease, MaterialAddRef, MaterialReleaseRef, TextureRelease,
    Count,
};
constexpr std::size_t kRoutes=static_cast<std::size_t>(Route::Count);
constexpr std::size_t kPendingInputs=8192,kTextureBindings=retained::kMaximumResources;
struct Descriptor {
    Route route;const char* name;
    std::uintptr_t engine_rva,filesystem_rva,pointer_slot;
    unsigned char prefix[16];std::uint8_t prefix_size,stolen_size,displacement_offset,instruction_end;
    bool shared_observer;
    bool pointer_in_filesystem;
};
struct Context {
    DlumaGameFrameAuthorityState authority;
    std::uint64_t transaction;std::uint32_t epoch;
    bool clean_startup,post_c10_consumers_closed;
};
using ReadFn=retained::windows::ReadFn;
using ContextFn=bool(*)(void*,Context*)noexcept;
using PotentialFn=void(*)(void*,Route)noexcept;
using ReplayFn=bool(*)(void*,Route,std::uintptr_t)noexcept;
struct Call {
    std::uint64_t serial;std::uint32_t depth;Route route;packed::Key pack;
    bool active,outer,returned;
};
struct InputKey {std::uint64_t generation;std::uint32_t slot;};
struct PendingInput {
    InputKey key;file_lifetime::FileRecord file;file_lifetime::SourceKey selected_source;
    packed::Key pack;std::uint64_t load_scope;
    retained::Realm realm;std::uint32_t epoch;
    unsigned char sha256[32];std::uint64_t bytes;
    bool active,bound,completed,scope_ended;
};
// Called only after an actual opened FileRecord has been copied. The root's
// content lease must pin this native file object, never reopen a path by name.
using PinInputFn=bool(*)(void*,InputKey,const file_lifetime::FileRecord&)noexcept;
enum class EventPhase : std::uint8_t { Enter, Returned, Unwound };
struct MaterialEvent {
    Route route;EventPhase phase;std::uintptr_t owner,arguments[5],result;
};
using MaterialFn=void(*)(void*,const MaterialEvent&)noexcept;
using MaterialFileFn=bool(*)(void*,std::uintptr_t material,const file_lifetime::FileRecord&,
    file_lifetime::SourceKey,retained::Realm,std::uint32_t epoch)noexcept;
using ClassifySourceFn=bool(*)(void*,file_lifetime::SourceKey,retained::Realm*,std::uint32_t*)noexcept;
using FullPathFn=void(*)(void*,EventPhase,bool resource_owned,std::uintptr_t returned_native_string,
    const resource_provider::Receipt*)noexcept;
using StreamRequestFn=bool(*)(void*,std::uintptr_t owner,std::uintptr_t operation,std::uintptr_t pack,
    std::uintptr_t entry,std::uintptr_t destination,std::uintptr_t callback,std::uintptr_t user)noexcept;
struct Callbacks {
    void* user;ReadFn read;ContextFn context;PotentialFn potential;ReplayFn replay;
    PinInputFn pin_input;MaterialFn material;MaterialFileFn material_file;
    resource_provider::RevisionFn source_revision;ClassifySourceFn classify_source;
    FullPathFn full_path;
    StreamRequestFn stream_request;
};
struct TextureBinding {retained::Identity texture;packed::Key pack;bool active;};
struct Lease {std::uint64_t generation,transaction;std::uint32_t epoch,thread;};
struct State {
    std::uintptr_t engine_base=0,filesystem_base=0;
    Callbacks callbacks={};file_lifetime::bridge::State* files=nullptr;
    packed::Ledger* packs=nullptr;retained::Ledger* textures=nullptr;
    std::uintptr_t originals[kRoutes]={},installed_targets[kRoutes]={};
    std::atomic<std::uint32_t> owner_thread{0},active_calls{0},pending_calls{0};
    std::atomic<std::uint64_t> revision{1},generation{1},next_input{0};
    std::atomic<bool> held{false},lost{false},sealed{false};
    std::atomic_flag records_lock=ATOMIC_FLAG_INIT;
    PendingInput inputs[kPendingInputs]={};TextureBinding bindings[kTextureBindings]={};
    std::size_t binding_count=0;Lease lease={};
    resource_provider::Access provider_access={};std::uintptr_t provider_probe_target=0;
    bool initialized=false;
};
const Descriptor* Catalog(std::size_t*)noexcept;
std::uintptr_t Wrapper(Route)noexcept;
bool Initialize(State*,std::uintptr_t engine,std::uintptr_t filesystem,const char* engine_sha256,
    const char* filesystem_sha256,const Callbacks&,file_lifetime::bridge::State*,
    packed::Ledger*,retained::Ledger*,audio::AudioFullPathProbeState*,std::uintptr_t provider_probe_target)noexcept;
bool BindOriginal(State*,Route,std::uintptr_t original,std::uintptr_t installed_target=0)noexcept;
bool PublishBridge(State*)noexcept;
bool SealInstalled(State*)noexcept;
bool VerifyInstalled(State*)noexcept;
void Begin(State*,Route,std::uintptr_t pack,Call*)noexcept;
void End(State*,Call*)noexcept;
bool Close(State*,std::uint32_t timeout_ms,Lease*)noexcept;
bool Open(State*,const Lease&)noexcept;
bool CaptureProducerFence(void*,packed::Fence*)noexcept;
bool AuthorizeNativeOperation(void*,packed::windows::NativeOperation,packed::Key,bool entering)noexcept;
// Shared monolith hooks call Begin/End around their original and invoke these
// exact successful-return observers. The catalogue identifies shared routes.
void PackConstructed(State*,std::uintptr_t,Call*)noexcept;
void PackRegistered(State*,packed::Key)noexcept;
void PackLoaded(State*,packed::Key)noexcept;
void PackDestroyEntered(State*,packed::Key)noexcept;
void PackDestroyReturned(State*,packed::Key)noexcept;
void PackUnregistered(State*,packed::Key)noexcept;
void TextureConstructed(State*,std::uintptr_t)noexcept;
void TextureDestroyEntered(State*,std::uintptr_t,retained::Identity*)noexcept;
void TextureDestroyReturned(State*,retained::Identity)noexcept;
// The caller passes the source realm from the actual selected source ledger.
// FileIdentity is rechecked immediately against the native file bridge.
bool ObserveOpenedFile(State*,std::uintptr_t file,retained::Realm,std::uint32_t epoch,InputKey*)noexcept;
bool ObserveNativeOpenedFile(State*,std::uintptr_t file,const char* consumed_native_path,
    retained::Realm,std::uint32_t epoch,InputKey*)noexcept;
bool CompleteInput(State*,InputKey,const unsigned char sha256[32],std::uint64_t bytes)noexcept;
void FailInput(State*,InputKey)noexcept;
std::size_t PendingInputCount(State*)noexcept;
std::uintptr_t CurrentOwner(State*,Route)noexcept;
bool HasActiveInputScope(State*)noexcept;
bool SnapshotPackKey(State*,std::uintptr_t,packed::Key*)noexcept;
} // namespace
#endif
