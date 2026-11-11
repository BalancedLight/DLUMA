/* Reuse the existing exact-profile file fixture and its real bridge setup. */
#define main file_bridge_existing_regressions
#include "native_file_bridge_fake_harness.cpp"
#undef main
#include "../src/fixes/custom_map_asset_isolation/NativeFileObservation.h"
namespace observation=dluma::fixes::custom_map_asset_isolation::file_observation;
namespace trace=dluma::fixes::custom_map_asset_isolation::trace;
static unsigned readable_calls,open_calls,opened_calls;
static const char* actual_path;
static void* actual_source;
static int actual_mode;
static std::uint32_t actual_flags;
static const char* observed_path;
static void* observed_source;
namespace filewin=win;namespace filebridge=bridge;namespace files=life;
namespace vfs {struct InventorySource {std::uintptr_t source;};struct FilesystemSourceInventory {std::uint32_t source_count;InventorySource sources[1];};}
static bool g_native_factory_resolvers_sealed=false,g_fix_enabled=true,g_xui_observation_only=false,g_native_file_bridge_prepared=true;
static bridge::State& g_native_file_bridge=state;
struct StartupHookStatus {bool installed=true;};
static StartupHookStatus startup_status[static_cast<unsigned>(win::Route::Count)];
static StartupHookStatus* g_native_file_statuses[static_cast<unsigned>(win::Route::Count)]={};
static unsigned inventory_calls;
static bool CaptureCurrentFilesystemInventory(vfs::FilesystemSourceInventory*)noexcept {++inventory_calls;return false;}
#include "native_file_startup_seal_under_test.inl"
extern "C" int dluma_is_readable(const void* address,SIZE_T size)
{++readable_calls;return address&&size&&reinterpret_cast<std::uintptr_t>(address)>=0x10000;}
static void* Forward(void*,void* owner,const char* path,int mode,std::uint32_t flags)noexcept
{
    ++open_calls;actual_path=path;actual_source=owner;actual_mode=mode;actual_flags=flags;
    using Factory=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
    return reinterpret_cast<Factory>(bridge::HookTarget(win::Route::Win32Factory))(&source,path,0x80000000,3,flags);
}
static void Opened(void*,void* result,const char* path,void* owner)noexcept
{if(result==&file){++opened_calls;observed_path=path;observed_source=owner;}}
static bool Boot()
{
    CHECK(!trace::Initialized());
    source={base+0x3f4a8};file={};
    CHECK(observation::Prepare(&state,base,win::kFilesystemSha256,&Read,nullptr));
    // This is the missing pre-publication binding in the actual Player crash.
    CHECK(trace::Initialized());
    CHECK(bridge::BindOriginal(&state,win::Route::Win32Factory,reinterpret_cast<void*>(&FakeFactory)));
    observation::Callbacks callbacks={&state,&Forward,&Opened,nullptr};
    CHECK(observation::Ready(callbacks));
    // A fetched file hook must forward safely before observation is sealed.
    CHECK(observation::Open(callbacks,false,nullptr,factory_path,0,0x40)==&file);
    CHECK(open_calls==1&&opened_calls==1&&readable_calls&&actual_path==factory_path&&actual_mode==0&&actual_flags==0x40);
    CHECK(observed_path==factory_path&&!observed_source);
    CHECK(bridge::Idle(&state)&&!state.observing);
    for(unsigned n=0;n<static_cast<unsigned>(win::Route::Count);++n)g_native_file_statuses[n]=&startup_status[n];
    // Isolation still requires its authenticated source-registry inventory.
    CHECK(!SealNativeFileBridge()&&inventory_calls==1&&!state.observing);
    CHECK(observation::Prepare(&state,base,win::kFilesystemSha256,&Read,nullptr));
    CHECK(bridge::BindOriginal(&state,win::Route::Win32Factory,reinterpret_cast<void*>(&FakeFactory)));
    // Independent observation must work while isolation tracking is disabled.
    g_fix_enabled=false;g_xui_observation_only=true;
    CHECK(SealNativeFileBridge()&&inventory_calls==1&&state.observing);
    CHECK(observation::Open(callbacks,true,&source,factory_path,0,0x80)==&file);
    life::FileRecord record={};CHECK(bridge::FileIdentity(&state,reinterpret_cast<std::uintptr_t>(&file),&record));
    CHECK(record.life==life::Life::Live&&record.source.address==reinterpret_cast<std::uintptr_t>(&source)&&actual_source==&source);
    CHECK(record.generation&&record.root);
    CHECK(open_calls==2&&opened_calls==2&&bridge::Idle(&state));
    CHECK(observed_path==factory_path&&observed_source==&source);
    auto missing=callbacks;missing.original=nullptr;CHECK(!observation::Ready(missing));
    CHECK(!observation::Open(missing,false,nullptr,factory_path,0,0)&&open_calls==2);
    auto saved=state.originals[static_cast<unsigned>(win::Route::PublicOpen)];
    state.originals[static_cast<unsigned>(win::Route::PublicOpen)]=nullptr;
    CHECK(!observation::Ready(callbacks));CHECK(!observation::Open(callbacks,false,nullptr,factory_path,0,0)&&open_calls==2);
    state.originals[static_cast<unsigned>(win::Route::PublicOpen)]=saved;
    CHECK(!observation::AbortUnpublished(&state)&&observation::Ready(callbacks));
    // All trace defaults, including clock and thread callbacks, are bound.
    CHECK(trace::Publish(trace::AssetTraceEventType::FsOpenWatchedEnter,0,0,0,0,0,"path",nullptr));
    trace::AssetTraceEvent events[2]={};CHECK(trace::Drain(events,2)==1&&events[0].qpc&&events[0].thread_id);
    // Failed preparation never grants publication and preserves safe retry.
    CHECK(!observation::Prepare(&state,base,"invalid-profile",&Read,nullptr));
    CHECK(!observation::Ready(callbacks)&&observation::AbortUnpublished(&state));
    CHECK(!observation::Open(callbacks,false,nullptr,factory_path,0,0)&&open_calls==2&&bridge::Idle(&state));
    CHECK(observation::Prepare(&state,base,win::kFilesystemSha256,&Read,nullptr));
    CHECK(observation::Ready(callbacks));CHECK(observation::AbortUnpublished(&state));
    CHECK(!observation::Ready(callbacks)&&!state.observing&&bridge::Idle(&state));
    return true;
}
int main(){if(!Boot())return 1;std::puts("shared file observation boot passed");return 0;}
