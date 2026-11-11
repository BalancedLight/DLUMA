#ifndef DLUMA_NATIVE_FILE_OBSERVATION_H
#define DLUMA_NATIVE_FILE_OBSERVATION_H
#include "CustomMapAssetTrace.h"
#include "NativeFileHookBridge.h"
namespace dluma::fixes::custom_map_asset_isolation::file_observation {
namespace bridge=file_lifetime::bridge;
namespace windows=file_lifetime::windows;
using OpenFn=void*(*)(void*,void*,const char*,int,std::uint32_t)noexcept;
using OpenedFn=void(*)(void*,void*,const char*,void*)noexcept;
struct Callbacks {bridge::State* files;OpenFn original;OpenedFn opened;void* user;};
inline bool Prepare(bridge::State* files,std::uintptr_t artifact_base,const char* hash,
                    windows::ReadFn read,void* user)noexcept
{
    // This must precede bridge preparation and every route publication. The
    // fix-off startup path uses the same trace helpers as the full fix.
    if(!trace::Initialized())trace::Initialize();
    return bridge::Prepare(files,artifact_base,hash,read,user);
}
inline bool Ready(const Callbacks& callbacks)noexcept
{return callbacks.original&&trace::Initialized()&&bridge::PreparedForPublication(callbacks.files);}
/* Before any route publication, no hook can borrow this binding. Keep the
 * prepared originals intact; after publication they remain process backed. */
inline bool AbortUnpublished(bridge::State* prepared)noexcept
{
    if(!prepared||dluma_atomic_compare_exchange(&prepared->observing,0,0)||
       dluma_atomic_compare_exchange(&prepared->active_calls,0,0))return false;
    prepared->binding.validated=false;return true;
}
inline void* Open(const Callbacks& callbacks,bool native,void* native_source,
                  const char* path,int mode,std::uint32_t flags)noexcept
{
    if(!Ready(callbacks))return nullptr;
    bridge::FactoryRequest request={};request.route=native?windows::Route::NativeOpen:windows::Route::PublicOpen;
    request.explicit_source=reinterpret_cast<std::uintptr_t>(native_source);
    request.path=reinterpret_cast<std::uintptr_t>(path);request.path_representation=bridge::PathRepresentation::NarrowString;
    request.mode=static_cast<std::uint32_t>(mode);request.flags=flags;
    bridge::FactoryCall call(callbacks.files,request);if(!call.admitted())return nullptr;
    char copied[trace::kEventTextCapacity]={};
    (void)trace::CopyReadableCString(copied,sizeof(copied),path,4096);
    void* result=callbacks.original(callbacks.user,native_source,path,mode,flags);
    call.returned(result);
    // Public opens also supply the requested path. Observers still establish
    // the returned file identity and consumed-byte proof independently.
    if(result&&callbacks.opened)callbacks.opened(callbacks.user,result,path,native_source);
    return result;
}
}
#endif
