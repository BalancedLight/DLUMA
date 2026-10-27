#include <cstdint>
#include <cstring>
#include <cstdio>
#include <intrin.h>
using HMODULE=void*;using LPCSTR=const char*;using FARPROC=void(*)();
using LONG=long;using DWORD=unsigned long;
#define WINAPI __stdcall
enum class FixState {UnsupportedBuild,RuntimeFailed,ApplyFailed,RuntimePartial,Applied};
struct FixStatus {FixState state;};
struct RuntimeContext {HMODULE gamedll_module,engine_module;std::uintptr_t engine_base;
    explicit RuntimeContext(HMODULE module):gamedll_module(module),engine_module(reinterpret_cast<void*>(2)),engine_base(0x10000000){} };
enum class HookRouteKind {Pointer};
struct HookRoute {HookRouteKind kind;std::uintptr_t address;void* original_target;void* hook_target;
    unsigned char byte_count,stolen_size;bool enabled,installed,ever_installed;unsigned char original_bytes[32],routing_bytes[32];};
struct {bool stop_requested;} g_dluma;
FixStatus g_status={FixState::UnsupportedBuild};
constexpr unsigned kQuiesceTimeoutMs=8;
constexpr char kSupportedEngineSha256[]="profile";
static bool profile=true,restore_ok=true,native_success=true;
static unsigned originals,initializations,restores;
static DWORD error_value;
static HMODULE loaded_module;
static std::uintptr_t slot_value,test_caller;
static void target(){}
static FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR) {++originals;error_value=99;return native_success?target:nullptr;}
static DWORD GetLastError(){return error_value;}
static void SetLastError(DWORD value){error_value=value;}
static HMODULE GetModuleHandleW(const wchar_t*){return loaded_module;}
static void* fake_return_address(){return reinterpret_cast<void*>(test_caller);}
#define _ReturnAddress fake_return_address
static LONG dluma_atomic_compare_exchange(volatile LONG* p,LONG value,LONG expected){return _InterlockedCompareExchange(p,value,expected);}
static LONG dluma_atomic_exchange(volatile LONG* p,LONG value){return _InterlockedExchange(p,value);}
static void dluma_atomic_increment(volatile LONG* p){_InterlockedIncrement(p);}
static void dluma_atomic_decrement(volatile LONG* p){_InterlockedDecrement(p);}
static void dluma_log_pair(const char*,const char*,const char*){}
static void Sleep(DWORD){}
static bool SupportedIdentity(const RuntimeContext&){return profile;}
static bool ValidateLoadedModuleSha256(HMODULE,const char*){return profile;}
static bool PinLoaderIfNeeded(bool){return true;}
static bool ReadBytes(std::uintptr_t,void* out,std::size_t n){if(n!=8)return false;std::memcpy(out,&slot_value,8);return true;}
static FixStatus MakeStatus(FixState state,std::uintptr_t=0,std::uintptr_t=0,bool=false){g_status.state=state;return g_status;}
static bool ApplyRoute(HookRoute* route){slot_value=reinterpret_cast<std::uintptr_t>(route->hook_target);route->installed=true;return true;}
static bool RestoreRoute(HookRoute* route){if(!route->installed)return true;if(!restore_ok)return false;
    ++restores;slot_value=reinterpret_cast<std::uintptr_t>(route->original_target);route->installed=false;return true;}
static bool FinishDeferredRendererObservation(){++initializations;
    if(slot_value!=reinterpret_cast<std::uintptr_t>(&GetProcAddress))return false;
    error_value=17;(void)MakeStatus(FixState::Applied);return true;}
#include "fixes/custom_map_asset_isolation/RendererStartupIntegration.inl"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static void reset(){g_renderer_startup_phase=0;g_renderer_startup_active=0;g_renderer_startup_route={};
    originals=initializations=restores=0;profile=restore_ok=native_success=true;loaded_module=nullptr;g_dluma.stop_requested=false;
    slot_value=reinterpret_cast<std::uintptr_t>(&GetProcAddress);test_caller=0;}
int main(){RuntimeContext context(reinterpret_cast<void*>(1));reset();
    CHECK(ArmRendererStartup(context).state==FixState::RuntimePartial && !initializations && g_renderer_startup_route.installed);
    CHECK(HookRendererStartupGetProcAddress(nullptr,"unrelated")==target && !initializations);
    loaded_module=reinterpret_cast<void*>(3);test_caller=context.engine_base+kRendererCreateLookupReturnRva;
    CHECK(HookRendererStartupGetProcAddress(reinterpret_cast<void*>(4),"CreateRenderer")==target && !initializations);
    CHECK(HookRendererStartupGetProcAddress(loaded_module,"CreateRenderer")==target);
    CHECK(initializations==1 && restores==1 && GetLastError()==99 && g_status.state==FixState::Applied && g_renderer_startup_active==0);
    CHECK(HookRendererStartupGetProcAddress(loaded_module,"CreateRenderer")==target && initializations==1);
    CHECK(StopRendererStartup());
    reset();CHECK(ArmRendererStartup(context).state==FixState::RuntimePartial);CHECK(StopRendererStartup());
    loaded_module=reinterpret_cast<void*>(3);test_caller=context.engine_base+kRendererCreateLookupReturnRva;
    CHECK(HookRendererStartupGetProcAddress(loaded_module,"CreateRenderer")==target && !initializations);
    reset();profile=false;CHECK(ArmRendererStartup(context).state==FixState::UnsupportedBuild && !g_renderer_startup_route.installed);
    reset();slot_value=7;CHECK(ArmRendererStartup(context).state==FixState::UnsupportedBuild && slot_value==7);
    reset();CHECK(ArmRendererStartup(context).state==FixState::RuntimePartial);restore_ok=false;
    loaded_module=reinterpret_cast<void*>(3);test_caller=context.engine_base+kRendererCreateLookupReturnRva;
    CHECK(HookRendererStartupGetProcAddress(loaded_module,"CreateRenderer")==target && !initializations && g_status.state==FixState::RuntimeFailed);
    CHECK(!StopRendererStartup());restore_ok=true;CHECK(StopRendererStartup());
    reset();CHECK(ArmRendererStartup(context).state==FixState::RuntimePartial);native_success=false;
    loaded_module=reinterpret_cast<void*>(3);test_caller=context.engine_base+kRendererCreateLookupReturnRva;
    CHECK(!HookRendererStartupGetProcAddress(loaded_module,"CreateRenderer") && !initializations && GetLastError()==99);
    CHECK(StopRendererStartup());std::puts("renderer startup boundary scenarios passed");return 0;
}
