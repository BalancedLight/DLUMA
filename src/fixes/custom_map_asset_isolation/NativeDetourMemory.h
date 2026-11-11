#ifndef DLUMA_NATIVE_DETOUR_MEMORY_H
#define DLUMA_NATIVE_DETOUR_MEMORY_H
#include <cstddef>
#include <cstdint>

namespace dluma::fixes::custom_map_asset_isolation::detour {
struct AtomicPatchResult {
    bool exchanged=false,flushed=false,protection_restored=false;
    std::uint32_t previous_protection=0;
    bool Succeeded()const noexcept{return exchanged&&flushed&&protection_restored;}
};
struct ProtectionState {std::uint32_t original=0;bool captured=false,pending=false;};
// Every route sharing a page captures its original protection before the first
// publication. An outstanding repair must never adopt a later writable value.
template<class Protect>
inline bool RestoreProtection(std::uintptr_t address,std::size_t bytes,ProtectionState* state,Protect protect)noexcept
{
    if(!state||!state->captured)return false;if(!state->pending)return true;
    std::uint32_t ignored=0;if(!protect(address,bytes,state->original,&ignored))return false;
    state->pending=false;return true;
}
// One bounded algorithm for the existing 8/16-byte expected-value exchanges.
// Native adapters supply protect/flush/intrinsic operations. Ownership is
// reported independently of flush/protection failure: a caller must retain any
// reachable forwarding state and preserve the first page protection for retry.
template<class Protect,class Flush,class Exchange>
inline AtomicPatchResult AtomicPatch(std::uintptr_t address,std::size_t bytes,
    const std::uint8_t* expected,const std::uint8_t* desired,
    Protect protect,Flush flush,Exchange exchange,ProtectionState* state=nullptr)noexcept
{
    AtomicPatchResult result={};
    if(!address||!expected||!desired||(bytes!=8&&bytes!=16)||(address&(bytes-1))||
        address>UINTPTR_MAX-(bytes-1)||(state&&!state->captured))return result;
    if(!protect(address,bytes,0x40,&result.previous_protection))return result;
    if(state)state->pending=true;
    result.exchanged=exchange(address,bytes,expected,desired);
    result.flushed=!result.exchanged||flush(address,bytes);
    std::uint32_t ignored=0;
    result.protection_restored=protect(address,bytes,state?state->original:result.previous_protection,&ignored);
    if(state&&result.protection_restored)state->pending=false;
    return result;
}
// Preserve the project's symmetric 64-KiB search and finite near range. The
// new protection argument lets a retained route prepare RW then publish RX;
// legacy callers keep their established RWX allocation behavior explicitly.
template<class Allocate>
inline void* AllocateRelayNear(std::uintptr_t target,std::uint32_t protection,Allocate allocate)noexcept
{
    constexpr std::uintptr_t granularity=0x10000,maximum=0x70000000;
    if(!target)return nullptr;
    const auto anchor=target&~(granularity-1);
    for(std::uintptr_t distance=granularity;distance<maximum;distance+=granularity){
        const std::uintptr_t candidates[]={anchor<=UINTPTR_MAX-distance?anchor+distance:0,
            anchor>distance?anchor-distance:0};
        for(const auto at:candidates)if(at){
            void* result=allocate(at,4096,protection);if(result)return result;
        }
    }
    return nullptr;
}
}
#endif
