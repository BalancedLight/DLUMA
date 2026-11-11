#include "../src/fixes/custom_map_asset_isolation/NativeDetourMemory.h"
#include <cstdio>
#include <cstring>
namespace d=dluma::fixes::custom_map_asset_isolation::detour;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Fixture {
 alignas(16) unsigned char memory[16]={};
 unsigned calls=0,exchanges=0,flushes=0,fail_protect=0;bool fail_flush=false;
 std::uint32_t protection=0x20;
 bool Protect(std::uintptr_t,std::size_t,std::uint32_t requested,std::uint32_t* previous)noexcept{
  ++calls;*previous=protection;if(calls==fail_protect)return false;protection=requested;return true;}
 bool Flush(std::uintptr_t,std::size_t)noexcept{++flushes;return !fail_flush;}
 bool Exchange(std::uintptr_t,std::size_t count,const unsigned char* expected,const unsigned char* desired)noexcept{
  ++exchanges;if(std::memcmp(memory,expected,count))return false;std::memcpy(memory,desired,count);return true;}
 auto Patch(const unsigned char* expected,const unsigned char* desired,std::size_t bytes=16,d::ProtectionState* state=nullptr){
  return d::AtomicPatch(reinterpret_cast<std::uintptr_t>(memory),bytes,expected,desired,
   [this](auto address,auto size,auto protection,auto* old)noexcept{return Protect(address,size,protection,old);},
   [this](auto address,auto size)noexcept{return Flush(address,size);},
   [this](auto address,auto size,const auto* from,const auto* to)noexcept{return Exchange(address,size,from,to);},state);}
};
int main(){
 unsigned char expected[16]={},desired[16];std::memset(desired,0x5A,16);
 Fixture f;auto result=f.Patch(expected,desired);
 CHECK(result.Succeeded()&&result.exchanged&&result.previous_protection==0x20&&f.protection==0x20&&f.flushes==1);
 f=Fixture{};f.fail_protect=1;result=f.Patch(expected,desired);
 CHECK(!result.Succeeded()&&!result.exchanged&&!f.exchanges&&!f.flushes);
 f=Fixture{};f.memory[0]=1;result=f.Patch(expected,desired);
 CHECK(!result.Succeeded()&&!result.exchanged&&f.memory[0]==1&&f.protection==0x20&&!f.flushes);
 f=Fixture{};f.fail_flush=true;result=f.Patch(expected,desired);
 CHECK(!result.Succeeded()&&result.exchanged&&!result.flushed&&result.protection_restored&&f.memory[0]==0x5A);
 f=Fixture{};f.fail_protect=2;result=f.Patch(expected,desired);
 CHECK(!result.Succeeded()&&result.exchanged&&result.flushed&&!result.protection_restored&&result.previous_protection==0x20);
 CHECK(f.memory[0]==0x5A&&f.protection==0x40);
 // Both routes capture RX before either publishes on their shared page.
 d::ProtectionState first={0x20,true,false},sibling={0x20,true,false};
 f=Fixture{};f.fail_protect=2;result=f.Patch(expected,desired,16,&first);
 CHECK(result.exchanged&&!result.protection_restored&&first.pending&&f.protection==0x40);
 // Rollback must restore the original RX, rather than its observed RWX.
 f.fail_protect=4;result=f.Patch(desired,expected,16,&first);
 CHECK(result.exchanged&&!result.Succeeded()&&first.pending&&!f.memory[0]&&f.protection==0x40);
 const auto exchanges=f.exchanges;f.fail_protect=5;
 auto protect=[&](auto at,auto size,auto requested,auto* old)noexcept{return f.Protect(at,size,requested,old);};
 CHECK(!d::RestoreProtection(reinterpret_cast<std::uintptr_t>(f.memory),16,&first,protect)&&first.pending);
 f.fail_protect=0;CHECK(d::RestoreProtection(reinterpret_cast<std::uintptr_t>(f.memory),16,&first,protect));
 CHECK(!first.pending&&f.protection==0x20&&f.exchanges==exchanges);
 // A sibling already prepared before failure must not adopt the writable page.
 f.protection=0x40;result=f.Patch(expected,desired,16,&sibling);
 CHECK(result.Succeeded()&&!sibling.pending&&f.protection==0x20&&result.previous_protection==0x40);
 d::ProtectionState uncaptured={};f=Fixture{};result=f.Patch(expected,desired,16,&uncaptured);
 CHECK(!result.exchanged&&!f.calls&&!f.exchanges);
 f=Fixture{};result=f.Patch(expected,desired,8);CHECK(result.Succeeded()&&f.memory[7]==0x5A&&f.memory[8]==0);
 unsigned allocations=0;std::uintptr_t selected=0;
 auto allocate=[&](std::uintptr_t at,std::size_t bytes,std::uint32_t protection)noexcept->void*{
  ++allocations;if(bytes!=4096||protection!=4)return nullptr;if(allocations!=3)return nullptr;selected=at;return reinterpret_cast<void*>(at);};
 CHECK(d::AllocateRelayNear(0x180001234ULL,4,allocate)==reinterpret_cast<void*>(0x180020000ULL));
 CHECK(selected==0x180020000ULL&&allocations==3);
 std::puts("PASS: shared atomic detour memory primitives");return 0;
}
