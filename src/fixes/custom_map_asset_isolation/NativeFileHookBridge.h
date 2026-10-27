#ifndef DLUMA_NATIVE_FILE_HOOK_BRIDGE_H
#define DLUMA_NATIVE_FILE_HOOK_BRIDGE_H
#include "NativeFileLifetimeWindows.h"
#include "ContentDomainRestartRegistry.h"

namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::bridge {
enum class PathRepresentation : std::uint8_t { None, NarrowString, NativePath };
struct FactoryRequest {
    windows::Route route=windows::Route::Count;
    std::uintptr_t explicit_source=0,path=0;
    SourceKey source_identity={};
    PathRepresentation path_representation=PathRepresentation::None;
    std::uint32_t mode=0,access=0,creation=0,flags=0;
    bool unbuffered=false,source_identity_observed=false;
};
constexpr std::size_t kFactoryPathCapacity=1024;
struct FactoryPathSnapshot {
    PathRepresentation representation;
    char narrow[kFactoryPathCapacity],filename[kFactoryPathCapacity],directory[kFactoryPathCapacity];
    std::uint32_t native_path_flags;
    bool complete;
};
// The callback offers a transaction-bound replay observation for this exact
// invocation. The ledger independently validates it; false offers no exception
// to ordinary admission. Configure before Seal, alongside the ledger validator.
using AuthorizeFactoryFn=bool(*)(void*,const FactoryRequest&,FactoryReplayAuthorization*)noexcept;
using FreshFileObserverFn=void(*)(void*,const FileRecord&,std::uint32_t file_slot)noexcept;
struct State {
    Ledger ledger;
    windows::Binding binding;
    windows::ReadFn read;
    void* read_user;
    AuthorizeFactoryFn authorize_factory;
    void* authorize_factory_user;
    FreshFileObserverFn fresh_zip_observer;
    void* fresh_zip_user;
    cold_reboot::ContentDomainRestartRegistry* registry;
    const volatile LONG* registry_tracking_live;
    void* originals[static_cast<unsigned>(windows::Route::Count)];
    volatile LONG observing;
    volatile LONG active_calls;
};
bool Prepare(State*,std::uintptr_t,const char*,windows::ReadFn,void*)noexcept;
bool Seal(State*,std::uint64_t installed_mask)noexcept;
void* HookTarget(windows::Route)noexcept;
bool BindOriginal(State*,windows::Route,void*)noexcept;
bool SourcePublication(State*,std::uintptr_t,bool constructed_before_publication)noexcept;
bool ExistingSource(State*,std::uintptr_t)noexcept;
bool SourceIdentity(State*,std::uintptr_t,SourceKey*)noexcept;
bool FileIdentity(State*,std::uintptr_t,FileRecord*)noexcept;
// Installed before observation starts. Runs only inside the actual ZIP factory
// return, before its new file is published to its caller. No constructor,
// outer factory or later file lookup can acquire this temporary authority.
bool ConfigureFreshZipObserver(State*,FreshFileObserverFn,void*)noexcept;
bool ConfigureRegistryAdmission(State*,cold_reboot::ContentDomainRestartRegistry*,const volatile LONG* tracking_live)noexcept;
bool FreshZipReturnMatches(State*,const FileRecord&)noexcept;
bool Idle(const State*)noexcept;
// Copies the actual argument only. NativePath keeps its two native components;
// this never normalizes, resolves a provider or inherits an ancestor's path.
bool SnapshotFactoryPath(State*,const FactoryRequest&,FactoryPathSnapshot*)noexcept;

/* Used by the two existing public/native open wrappers as well as the new
 * nullable factories. Rejection is valid only for a nullable factory. */
class FactoryCall {
    State* state_;FactoryToken token_;bool rejected_;windows::Route route_;
    cold_reboot::RetainingObservationToken registry_token_;
public:
    explicit FactoryCall(State*)noexcept;
    FactoryCall(State*,const FactoryRequest&)noexcept;
    ~FactoryCall();
    bool admitted()const noexcept{return !rejected_;}
    void returned(void*)noexcept;
    FactoryCall(const FactoryCall&)=delete;
    FactoryCall& operator=(const FactoryCall&)=delete;
};
}
#endif
