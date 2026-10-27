#ifndef DLUMA_NATIVE_XUI_CONTENT_OWNER_H
#define DLUMA_NATIVE_XUI_CONTENT_OWNER_H
#include "NativeXuiRegistrySnapshot.h"
#include "NativeFileLifetimeLedger.h"

namespace dluma::fixes::custom_map_asset_isolation::xui_content {
constexpr std::size_t kInputs=4096,kOwners=256,kPathBytes=512,kActiveLoads=64;
enum class Route : std::uint8_t { Xml, BinaryPack, BinaryFile };
enum class Realm : std::uint8_t { Unknown, Global, Project };
struct Key {std::uintptr_t manager;std::uint64_t generation;};
struct Input {
    file_lifetime::FileRecord file;
    file_lifetime::SourceKey selected_source;
    char path[kPathBytes];unsigned char sha256[32];std::uint64_t bytes;
    Realm realm;std::uint32_t source_epoch;
    bool complete;
};
struct BaselineInput {
    file_lifetime::SourceKey source;
    char path[kPathBytes];unsigned char sha256[32];std::uint64_t bytes;
};
struct Owner {Key key;std::uint32_t touched_epoch;bool ended;};
struct Load {
    std::uintptr_t state;Key owner;std::uint64_t serial;
    std::uint32_t epoch,input_count,slot;Route route;
    bool active,clean_startup,dirty;
};
struct State {
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::atomic<bool> lost{false};
    BaselineInput inputs[kInputs]={};Owner owners[kOwners]={};
    std::uintptr_t active_addresses[kActiveLoads]={};std::uint64_t active_serials[kActiveLoads]={};
    std::size_t input_count=0,owner_count=0;
    std::uint64_t next_serial=0,revision=0;
    std::atomic<std::uint64_t> baseline_generation{0};
    std::uint32_t active_loads=0;
    bool startup_sealed=false,project_seen=false;std::atomic<bool> baseline_frozen{false};
};
bool SealStartup(State*)noexcept;
bool Begin(State*,Key,std::uint32_t epoch,Route,bool clean_startup,Load*)noexcept;
// A path is only an exact native request key. Actual source generation and
// bytes are mandatory; neither a filename nor a provider realm grants equality.
bool Observe(State*,Load*,const Input&)noexcept;
// Missing/failed input observation remains a possible native write, even when
// the native parser reports failure after partially constructing its objects.
bool End(State*,Load*,bool no_input_is_native_noop,bool* affected)noexcept;
bool OwnerDestroyed(State*,Key)noexcept;
bool CaptureBaseline(State*,std::uint64_t generation)noexcept;
// Only the matching completed native destructor establishes this boundary.
// Registry metadata and an empty factory snapshot cannot substitute for it.
bool VerifyEndedOwners(State*,std::uint64_t baseline,std::uint32_t epoch)noexcept;

constexpr std::size_t kInputLeases=64;
struct InputLeaseSlot {void* pair;std::uint64_t generation;bool reading;};
struct InputLeasePool {void* lock=nullptr;std::uint64_t next_generation=0;InputLeaseSlot slots[kInputLeases]={};};
struct InputLeaseToken {std::uintptr_t pool;std::uint64_t generation;std::uint32_t slot;bool active;};
using CloseInputFn=bool(*)(void*,void*)noexcept;
using FreeInputFn=void(*)(void*,void*)noexcept;
// Publish the heap pair before duplicating any native handle. Failed closes
// retain the same pair for a later drain; freeing a pair requires close success.
bool TrackInputLease(InputLeasePool*,void*,InputLeaseToken*)noexcept;
bool CompleteInputLease(InputLeasePool*,InputLeaseToken*,CloseInputFn,FreeInputFn,void*)noexcept;
bool DrainInputLeases(InputLeasePool*,CloseInputFn,FreeInputFn,void*)noexcept;
} // namespace
#endif
