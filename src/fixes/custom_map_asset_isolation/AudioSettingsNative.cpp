#define _HAS_EXCEPTIONS 0
#include "AudioSettingsNative.h"
#include <cstring>
#include <intrin.h>

namespace dluma::fixes::custom_map_asset_isolation::audio_settings_native {
namespace {
bool Finite(float v) noexcept {
    std::uint32_t bits=0; std::memcpy(&bits,&v,4); return (bits&0x7F800000U)!=0x7F800000U;
}
bool SameFloat(float a,float b) noexcept { return !std::memcmp(&a,&b,4); }
bool Read(const Access& a,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return p && p<=UINTPTR_MAX-n && a.read(a.user,p,out,n);
}
bool Same(const UserGains& a,const UserGains& b) noexcept {
    return a.complete && b.complete && a.game==b.game && a.settings==b.settings && a.audio==b.audio &&
        a.owner_generation==b.owner_generation && !std::memcmp(a.levels,b.levels,sizeof(a.levels)) &&
        SameFloat(a.grain,b.grain) && !std::memcmp(a.maximum,b.maximum,sizeof(a.maximum));
}
Result ReadCurrent(const Access& a,UserGains* out) noexcept {
    *out={}; std::uintptr_t game_table=0,audio_table=0; std::uint32_t buses=0;
    if (!a.owner(a.user,&out->audio,&out->owner_generation) || !out->audio || !out->owner_generation ||
        !Read(a,a.engine_base+0xAC8D78,&audio_table,8) || audio_table!=out->audio)
        return Result::WrongOwner;
    if (!Read(a,a.engine_base+0xACA818,&out->game,8) || !out->game || out->game>UINTPTR_MAX-0x3E0 ||
        !Read(a,out->game,&game_table,8) || game_table!=a.engine_base+0x8D4D78 ||
        !Read(a,out->game+0x3D8,&out->settings,8) || !out->settings || out->settings>UINTPTR_MAX-0x58 ||
        out->audio>UINTPTR_MAX-0x6800 || !Read(a,out->audio,&audio_table,8) || audio_table!=a.engine_base+0x8A9628)
        return Result::WrongOwner;
    if (!Read(a,out->settings+0x34,out->levels,sizeof(out->levels)) ||
        !Read(a,a.engine_base+0xA91E60,&out->grain,4) || !Read(a,out->audio+0x3B94,&buses,4)) return Result::ReadFailed;
    if (!Finite(out->grain) || buses<kUserBuses || buses>14) return Result::InvalidSettings;
    for (unsigned i=0;i<kUserBuses;++i) {
        float value=static_cast<float>(out->levels[i])*out->grain;
        if (!Finite(value)) return Result::InvalidSettings;
        if (value<0.0f) value=0.0f;
        else if (value>1.0f) value=1.0f;
        out->maximum[i]=value;
    }
    out->complete=true; return Result::Succeeded;
}
bool Lock(State* s) noexcept { return s && !s->lock.test_and_set(std::memory_order_acquire); }
void Unlock(State* s) noexcept { s->lock.clear(std::memory_order_release); }
void LockReceipt(State* s) noexcept {
    // Every holder copies only bounded POD; none calls native code, reads
    // foreign memory or invokes callbacks while holding this lock.
    while (s->lock.test_and_set(std::memory_order_acquire)) _mm_pause();
}
}
Result Capture(const Access& a,UserGains* out) noexcept {
    if (!out) return Result::InvalidArgument;
    *out={};
    if (!a.exact_engine_profile || !a.engine_base || a.engine_base>UINTPTR_MAX-0xE8D000 || !a.read || !a.owner)
        return Result::InvalidArgument;
    UserGains first={},second={}; auto result=ReadCurrent(a,&first);
    if (result==Result::Succeeded) result=ReadCurrent(a,&second);
    if (result!=Result::Succeeded) return result;
    if (!Same(first,second)) return Result::Changed;
    *out=first; return Result::Succeeded;
}
Result Apply(State* s,const Access& a,const UserGains& desired,std::uint64_t transaction,
    std::uint32_t epoch,std::uint64_t completed,std::uint64_t* request) noexcept {
    if (request) *request=0;
    if (!s || !request || !desired.complete || !transaction || !epoch || !a.authorize || !a.set_maximum)
        return Result::InvalidArgument;
    if (!a.authorize(a.user,transaction,epoch,true)) return Result::Unauthorized;
    UserGains current={}; const auto captured=Capture(a,&current);
    if (captured!=Result::Succeeded) return captured;
    if (!Same(desired,current)) return Result::Changed;
    if (!Lock(s)) return Result::Busy;
    if (s->active || s->request==UINT64_MAX) { Unlock(s); return Result::Busy; }
    s->desired=desired; std::memset(s->receipts,0,sizeof(s->receipts));
    s->transaction=transaction; s->epoch=epoch; s->worker_at_apply=completed;
    s->active=true; s->setters_finished=false; s->native_failed=false; *request=++s->request;
    Unlock(s);
    bool applied=true;
    for (unsigned i=0;i<kUserBuses && applied;++i)
        applied=a.authorize(a.user,transaction,epoch,true) &&
            a.set_maximum(a.user,transaction,epoch,desired.audio,i,desired.maximum[i]);
    // A worker receipt can arrive concurrently. It must never block native
    // mixer execution behind a lock held during the setter/native callback.
    LockReceipt(s);
    if (s->request!=*request || !s->active) { Unlock(s); return Result::StaleRequest; }
    s->setters_finished=applied; s->native_failed|=!applied;
    Unlock(s); return applied ? Result::Pending : Result::NativeFailure;
}
void ObserveMix(State* s,std::uintptr_t base,std::uintptr_t caller,const audio_worker_reset::WorkerFrame& frame,
    std::uint64_t generation,std::uint64_t bus,std::uintptr_t connection,float mix,int native_result) noexcept {
    if (!base || base>UINTPTR_MAX-0xE8D000 || caller!=base+kDspMixCallerReturnRva ||
        !frame.exact_native_route || frame.original_returned || !frame.audio_owner ||
        frame.route_identity!=base+0x11042D || frame.current_thread_id!=GetCurrentThreadId() ||
        !generation || bus>=kUserBuses || !connection || !Finite(mix) || !s) return;
    LockReceipt(s);
    if (s->active && generation==s->desired.owner_generation && frame.audio_owner==s->desired.audio &&
        frame.original_completion_generation>=s->worker_at_apply) {
        auto& receipt=s->receipts[bus];
        // Never leave an earlier matching receipt in place after a newer
        // observed write changes this DSP connection or reports failure.
        receipt={connection,generation,frame.original_completion_generation,mix,
            native_result==0 && SameFloat(mix,s->desired.maximum[bus])};
        if (native_result!=0) s->native_failed=true;
    }
    Unlock(s);
}
Result Verify(State* s,const Access& a,std::uint64_t request,std::uint64_t completed) noexcept {
    if (!s || !request || !a.authorize) return Result::InvalidArgument;
    if (!Lock(s)) return Result::Busy;
    const auto transaction=s->transaction; const auto epoch=s->epoch;
    const bool current_request=s->active && request==s->request;
    Unlock(s);
    if (!current_request) return Result::StaleRequest;
    if (!a.authorize(a.user,transaction,epoch,false)) return Result::Unauthorized;
    UserGains current={}; const auto captured=Capture(a,&current);
    if (captured!=Result::Succeeded) return captured;
    if (!Lock(s)) return Result::Busy;
    Result result=Result::Succeeded;
    if (request!=s->request || !s->active) result=Result::StaleRequest;
    else if (!Same(current,s->desired)) result=Result::Changed;
    else if (s->native_failed) result=Result::NativeFailure;
    else if (!s->setters_finished) result=Result::Pending;
    else for (unsigned i=0;i<kUserBuses;++i) {
        const auto& r=s->receipts[i];
        if (!r.succeeded || r.owner_generation!=current.owner_generation ||
            r.worker_before<s->worker_at_apply || completed<=r.worker_before ||
            !SameFloat(r.observed_mix,s->desired.maximum[i])) { result=Result::Pending; break; }
        for (unsigned j=0;j<i;++j)
            if (s->receipts[j].connection==r.connection) { result=Result::NativeFailure; break; }
        if (result!=Result::Succeeded) break;
    }
    Unlock(s); return result;
}
void Cancel(State* s,std::uint64_t request) noexcept {
    if (!s) return;
    LockReceipt(s);
    if (s->request==request) s->active=false;
    Unlock(s);
}
bool NativeSetMaximum(const Access& a,std::uint64_t transaction,std::uint32_t epoch,
    std::uintptr_t audio,unsigned bus,float gain) noexcept {
    constexpr std::uint8_t prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x0f,0x29,0x74};
    if (!a.exact_engine_profile || !a.engine_base || a.engine_base>UINTPTR_MAX-0xE8D000 || !a.read || !a.owner ||
        !audio || bus>=kUserBuses || !Finite(gain) || !transaction || !epoch || !a.authorize ||
        !a.authorize(a.user,transaction,epoch,true)) return false;
    std::uintptr_t current=0,table=0; std::uint64_t generation=0; std::uint8_t actual[sizeof(prefix)]={};
    if (!a.owner(a.user,&current,&generation) || current!=audio || !generation ||
        !Read(a,audio,&table,8) || table!=a.engine_base+0x8A9628 ||
        !Read(a,a.engine_base+kSetBusMaximumRva,actual,sizeof(actual)) || std::memcmp(actual,prefix,sizeof(prefix))) return false;
    using Function=void(__fastcall*)(std::uintptr_t,unsigned,float);
    __try { reinterpret_cast<Function>(a.engine_base+kSetBusMaximumRva)(audio,bus,gain); }
    __except(1) { return false; }
    float observed=0;
    return Read(a,audio+0x3C5C+740ULL*bus,&observed,4) && SameFloat(observed,gain);
}
} // namespace
