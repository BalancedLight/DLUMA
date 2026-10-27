#ifndef DLUMA_RESOURCE_PROVIDER_SCOPE_H
#define DLUMA_RESOURCE_PROVIDER_SCOPE_H
#include "AudioBankProviderCorrelation.h"
#include "NativeFileHookBridge.h"

namespace dluma::fixes::custom_map_asset_isolation::resource_provider {
constexpr std::size_t kPathBytes=1024;
using ReadFn=bool(*)(void*,std::uintptr_t,void*,std::size_t)noexcept;
using RevisionFn=bool(*)(void*,std::uint64_t*)noexcept;
struct Access {
    audio::AudioFullPathProbeState* probe;
    file_lifetime::bridge::State* files;
    ReadFn read;RevisionFn source_revision;void* user;
};
struct Scope {
    audio::AudioFullPathProbeToken probe;
    std::uint64_t before_revision,load_serial;
    std::uint32_t thread_id;
    bool active;
};
struct Receipt {
    file_lifetime::SourceKey source;
    std::uint64_t source_revision,load_serial;
    std::uint32_t thread_id;
    char resolved_path[kPathBytes];
    bool exact;
};
// One source-selection invocation per scope; scopes never span several path
// resolutions. The qualified interior probe runs under the native source lock.
bool Begin(const Access&,std::uint64_t load_serial,Scope*)noexcept;
bool Finish(const Access&,Scope*,std::uintptr_t returned_native_string,Receipt*)noexcept;
void Cancel(const Access&,Scope*)noexcept;
// Match only the actual path argument consumed by the native open in the same
// observed load scope. No filesystem lookup or path normalization occurs here.
bool Matches(const Access&,const Receipt&,std::uint64_t load_serial,
             const char* consumed_native_path)noexcept;
} // namespace
#endif
