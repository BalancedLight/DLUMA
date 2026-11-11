#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "../src/fixes/custom_map_asset_isolation/NativeDetourMemory.h"
namespace detour=dluma::fixes::custom_map_asset_isolation::detour;
constexpr std::size_t kAtomicDetourSize=16,kMaximumStolenBytes=32;
enum class HookRouteKind{Pointer,FunctionDetour,CallEnvelope};
struct HookRoute{HookRouteKind kind;std::uintptr_t address;unsigned char original_bytes[32],routing_bytes[32];
 unsigned char byte_count,stolen_size;bool enabled,installed,ever_installed;detour::ProtectionState protection;};
static unsigned calls,fail_at,flushes;static bool fail_flush;
static bool ProtectDetourRange(std::uintptr_t at,std::size_t bytes,std::uint32_t protection,std::uint32_t* previous)noexcept{
 ++calls;if(calls==fail_at)return false;DWORD old=0;const auto ok=VirtualProtect(reinterpret_cast<void*>(at),bytes,protection,&old);*previous=old;return ok!=0;}
static bool FlushDetourRange(std::uintptr_t at,std::size_t bytes)noexcept{++flushes;return !fail_flush&&FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),bytes);}
static bool BytesEqual(std::uintptr_t at,const void* bytes,std::size_t count)noexcept{return !std::memcmp(reinterpret_cast<void*>(at),bytes,count);}
#include "../src/fixes/custom_map_asset_isolation/NativeHookRouteMutation.inl"
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static DWORD Protection(void* page){MEMORY_BASIC_INFORMATION region={};return VirtualQuery(page,&region,sizeof(region))==sizeof(region)?region.Protect:0;}
static HookRoute Route(void* page,unsigned offset,HookRouteKind kind){HookRoute route={};route.kind=kind;route.address=reinterpret_cast<std::uintptr_t>(page)+offset;
 route.enabled=true;route.byte_count=kind==HookRouteKind::Pointer?8:16;route.stolen_size=route.byte_count;std::memset(route.routing_bytes,0x5a,route.byte_count);return route;}
int main(){
 auto* page=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(page);DWORD old=0;CHECK(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
 auto route=Route(page,0,HookRouteKind::CallEnvelope),sibling=Route(page,16,HookRouteKind::FunctionDetour);
 CHECK(CaptureRouteProtection(&route)&&CaptureRouteProtection(&sibling));CHECK(route.protection.original==PAGE_EXECUTE_READ);
 fail_at=2;CHECK(!ApplyRoute(&route)&&route.installed&&route.ever_installed&&route.protection.pending&&Protection(page)==PAGE_EXECUTE_READWRITE);
 CHECK(!CaptureRouteProtection(&route));
 fail_at=4;CHECK(!RestoreRoute(&route)&&!route.installed&&route.protection.pending&&Protection(page)==PAGE_EXECUTE_READWRITE);
 fail_at=5;CHECK(!RestoreRoute(&route)&&route.protection.pending);fail_at=0;
 CHECK(RestoreRoute(&route)&&!route.protection.pending&&Protection(page)==PAGE_EXECUTE_READ);
 // Initial failure on one route must not replace a pre-captured sibling baseline.
 fail_at=calls+2;CHECK(!ApplyRoute(&route)&&route.installed&&route.protection.pending);
 fail_at=0;CHECK(ApplyRoute(&sibling)&&Protection(page)==PAGE_EXECUTE_READ);
 CHECK(RestoreRoute(&sibling)&&RestoreRoute(&route)&&Protection(page)==PAGE_EXECUTE_READ);
 auto pointer=Route(page,32,HookRouteKind::Pointer);CHECK(CaptureRouteProtection(&pointer));
 fail_flush=true;CHECK(!ApplyRoute(&pointer)&&pointer.installed&&!pointer.protection.pending);
 fail_flush=false;CHECK(RestoreRoute(&pointer)&&!pointer.installed);
 CHECK(flushes>0&&VirtualFree(page,0,MEM_RELEASE));std::puts("PASS actual shared route mutation protection failure/rollback/retry");return 0;
}
