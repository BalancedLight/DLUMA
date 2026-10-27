#define _HAS_EXCEPTIONS 0
#include "AudioDspNative.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::audio_dsp_native {
namespace {
struct Export { std::uintptr_t rva; const char* name; };
constexpr Export exports[]={
 {0x8B110,"?getParameter@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@HPEAMPEADH@Z"},
 {0x8B170,"?getNumParameters@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEAH@Z"},
 {0x8AFC0,"?getBypass@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEA_N@Z"},
 {0x8AF40,"?getActive@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEA_N@Z"},
 {0x8B2D0,"?getType@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEAW4FMOD_DSP_TYPE@@@Z"},
 {0x8B0D0,"?setParameter@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@HM@Z"},
 {0x8AF80,"?setBypass@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@_N@Z"},
 {0x8AF00,"?setActive@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@_N@Z"},
 {0x8E530,"?getReverbProperties@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEAUFMOD_REVERB_PROPERTIES@@@Z"},
 {0x8E500,"?setReverbProperties@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBUFMOD_REVERB_PROPERTIES@@@Z"},
 {0x8A560,"?getVolume@ChannelGroup@FMOD@@QEAA?AW4FMOD_RESULT@@PEAM@Z"},
 {0x8A520,"?setVolume@ChannelGroup@FMOD@@QEAA?AW4FMOD_RESULT@@M@Z"},
 {0x8E630,"?lockDSP@System@FMOD@@QEAA?AW4FMOD_RESULT@@XZ"},
 {0x8E660,"?unlockDSP@System@FMOD@@QEAA?AW4FMOD_RESULT@@XZ"},
 {0x8E4A0,"?getMasterChannelGroup@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEAPEAVChannelGroup@2@@Z"},
 {0x8AA30,"?getDSPHead@ChannelGroup@FMOD@@QEAA?AW4FMOD_RESULT@@PEAPEAVDSP@2@@Z"},
 {0x8AE00,"?getNumInputs@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@PEAH@Z"},
 {0x8AE60,"?getInput@DSP@FMOD@@QEAA?AW4FMOD_RESULT@@HPEAPEAV12@PEAPEAVDSPConnection@2@@Z"},
 {0x5B4B0,"?getMix@DSPConnection@FMOD@@QEAA?AW4FMOD_RESULT@@PEAM@Z"},
 {0x5B470,"?setMix@DSPConnection@FMOD@@QEAA?AW4FMOD_RESULT@@M@Z"},
};
using GetParameter=int(__fastcall*)(std::uintptr_t,int,float*,char*,int);
using GetInt=int(__fastcall*)(std::uintptr_t,int*);
using GetBool=int(__fastcall*)(std::uintptr_t,bool*);
using SetParameter=int(__fastcall*)(std::uintptr_t,int,float);
using SetBool=int(__fastcall*)(std::uintptr_t,bool);
using Reverb=int(__fastcall*)(std::uintptr_t,void*);
using GetVolume=int(__fastcall*)(std::uintptr_t,float*);
using SetVolume=int(__fastcall*)(std::uintptr_t,float);
using Lock=int(__fastcall*)(std::uintptr_t);
using GetObject=int(__fastcall*)(std::uintptr_t,std::uintptr_t*);
using GetInput=int(__fastcall*)(std::uintptr_t,int,std::uintptr_t*,std::uintptr_t*);
bool ValidIdentity(const Identity& i) noexcept {
    if(!i.owner||!i.system||!i.generation||i.buses<3||i.buses>kMaximumBuses)return false;
    for(unsigned b=0;b<i.buses;++b){
        if(!i.groups[b]||!i.dsp[b][0]||!i.dsp[b][1])return false;
        for(unsigned p=0;p<b;++p)if(i.groups[p]==i.groups[b])return false;
        for(unsigned d=0;d<kDspSlots;++d)if(i.dsp[b][d])
            for(unsigned p=0;p<=b;++p)for(unsigned q=0;q<kDspSlots;++q)
                if((p<b||q<d)&&i.dsp[p][q]==i.dsp[b][d])return false;
    }
    return true;
}
bool IdentityEqual(const Identity& a,const Identity& b) noexcept {
    return a.owner==b.owner&&a.system==b.system&&a.generation==b.generation&&a.buses==b.buses&&
        !std::memcmp(a.groups,b.groups,sizeof(a.groups))&&!std::memcmp(a.dsp,b.dsp,sizeof(a.dsp))&&
        (!a.master_effect||a.master_effect==b.master_effect);
}
bool Authorized(const Binding& b,const Identity& i,bool mutation) noexcept {
    return b.validated&&b.exact_image&&b.fence&&ValidIdentity(i)&&b.fence(b.user,i,mutation);
}
bool FloatEqual(float a,float b) noexcept {
    std::uint32_t x=0,y=0;std::memcpy(&x,&a,4);std::memcpy(&y,&b,4);
    if((x&0x7F800000U)==0x7F800000U||(y&0x7F800000U)==0x7F800000U)return false;
    float delta=a-b,magnitude=0;std::uint32_t bits=0;std::memcpy(&bits,&delta,4);bits&=0x7FFFFFFFU;
    std::memcpy(&delta,&bits,4);x&=0x7FFFFFFFU;std::memcpy(&magnitude,&x,4);
    return delta<=0.00001f*(1.0f+magnitude);
}
bool Finite(float v) noexcept {std::uint32_t bits=0;std::memcpy(&bits,&v,4);return (bits&0x7F800000U)!=0x7F800000U;}
int LockDsp(const Binding& b,std::uintptr_t system,bool acquire) noexcept {
    __try{return reinterpret_cast<Lock>(b.functions[acquire?12:13])(system);}
    __except(1){return -1;}
}
Result ReadLocked(const Binding& b,Snapshot* out) noexcept {
    __try {
        int inputs=0;
        if(reinterpret_cast<GetObject>(b.functions[14])(out->identity.system,&out->master)||!out->master||
            reinterpret_cast<GetObject>(b.functions[15])(out->master,&out->head)||!out->head||
            reinterpret_cast<GetInt>(b.functions[16])(out->head,&inputs)||inputs!=static_cast<int>(out->identity.buses)||
            reinterpret_cast<GetVolume>(b.functions[10])(out->master,&out->master_volume)||!Finite(out->master_volume))return Result::NativeFailure;
        for(unsigned bus=0;bus<out->identity.buses;++bus){
            if(reinterpret_cast<GetInput>(b.functions[17])(out->head,static_cast<int>(bus),nullptr,&out->connections[bus])||!out->connections[bus]||
                reinterpret_cast<GetVolume>(b.functions[18])(out->connections[bus],&out->mix[bus])||!Finite(out->mix[bus]))return Result::NativeFailure;
            for(unsigned prior=0;prior<bus;++prior)if(out->connections[prior]==out->connections[bus])return Result::Changed;
            if(reinterpret_cast<GetVolume>(b.functions[10])(out->identity.groups[bus],&out->volume[bus])||
                !Finite(out->volume[bus]))return Result::NativeFailure;
            for(unsigned slot=0;slot<kDspSlots;++slot){
                const auto object=out->identity.dsp[bus][slot];if(!object)continue;
                auto& d=out->dsp[bus][slot];
                if(reinterpret_cast<GetInt>(b.functions[4])(object,&d.type)||
                    reinterpret_cast<GetInt>(b.functions[1])(object,&d.count)||d.count<0||d.count>kMaximumParameters||
                    (slot<2&&d.type!=(slot?6:3))||
                    reinterpret_cast<GetBool>(b.functions[2])(object,&d.bypass)||
                    reinterpret_cast<GetBool>(b.functions[3])(object,&d.active))return Result::NativeFailure;
                for(int p=0;p<d.count;++p)
                    if(reinterpret_cast<GetParameter>(b.functions[0])(object,p,&d.parameters[p],nullptr,0)||
                        !Finite(d.parameters[p]))return Result::NativeFailure;
            }
        }
        if(out->identity.master_effect){const auto object=out->identity.master_effect;auto& d=out->master_effect;
            if(reinterpret_cast<GetInt>(b.functions[4])(object,&d.type)||d.type!=16||
                reinterpret_cast<GetInt>(b.functions[1])(object,&d.count)||d.count!=4||
                reinterpret_cast<GetBool>(b.functions[2])(object,&d.bypass)||reinterpret_cast<GetBool>(b.functions[3])(object,&d.active))return Result::NativeFailure;
            for(int p=0;p<d.count;++p)if(reinterpret_cast<GetParameter>(b.functions[0])(object,p,&d.parameters[p],nullptr,0)||!Finite(d.parameters[p]))return Result::NativeFailure;
        }
        for(unsigned i=0;i<4;++i){out->reverb[i][0]=i;
            if(reinterpret_cast<Reverb>(b.functions[8])(out->identity.system,out->reverb[i])||out->reverb[i][0]!=i)
                return Result::NativeFailure;
        }
    } __except(1){return Result::NativeFailure;}
    return Result::Succeeded;
}
Result RestoreLocked(const Binding& b,const Snapshot& s,const Snapshot& current) noexcept {
    __try {
        std::uintptr_t master=0,head=0;int inputs=0;
        if(reinterpret_cast<GetObject>(b.functions[14])(s.identity.system,&master)||master!=s.master||
            reinterpret_cast<GetObject>(b.functions[15])(master,&head)||head!=s.head||
            reinterpret_cast<GetInt>(b.functions[16])(head,&inputs)||inputs!=static_cast<int>(s.identity.buses))return Result::Changed;
        if(reinterpret_cast<SetVolume>(b.functions[11])(master,s.master_volume))return Result::NativeFailure;
        if(current.identity.master_effect){const auto object=current.identity.master_effect;
            if(!s.identity.master_effect){
                // Stock F79C0 retains this built-in cache and bypasses it when
                // disabled. Its four settings are overwritten on every enable.
                if(reinterpret_cast<SetBool>(b.functions[6])(object,true))return Result::NativeFailure;
            }else{const auto& d=s.master_effect;
                for(int p=0;p<d.count;++p)if(!FloatEqual(d.parameters[p],current.master_effect.parameters[p])&&
                    reinterpret_cast<SetParameter>(b.functions[5])(object,p,d.parameters[p]))return Result::NativeFailure;
                if(reinterpret_cast<SetBool>(b.functions[6])(object,d.bypass)||reinterpret_cast<SetBool>(b.functions[7])(object,d.active))return Result::NativeFailure;
            }
        }
        for(unsigned bus=0;bus<s.identity.buses;++bus){
            std::uintptr_t connection=0;
            if(reinterpret_cast<GetInput>(b.functions[17])(head,static_cast<int>(bus),nullptr,&connection)||connection!=s.connections[bus])return Result::Changed;
            if(bus>=3&&reinterpret_cast<SetVolume>(b.functions[19])(connection,s.mix[bus]))return Result::NativeFailure;
            if(bus>=3&&reinterpret_cast<SetVolume>(b.functions[11])(s.identity.groups[bus],s.volume[bus]))return Result::NativeFailure;
            for(unsigned slot=0;slot<kDspSlots;++slot){const auto object=s.identity.dsp[bus][slot];if(!object)continue;
                const auto& d=s.dsp[bus][slot];int count=0,type=0;
                if(reinterpret_cast<GetInt>(b.functions[1])(object,&count)||count!=d.count||
                    reinterpret_cast<GetInt>(b.functions[4])(object,&type)||type!=d.type)return Result::Changed;
                for(int p=0;p<count;++p){float actual=0;
                    if(reinterpret_cast<GetParameter>(b.functions[0])(object,p,&actual,nullptr,0))return Result::NativeFailure;
                    if(!FloatEqual(actual,d.parameters[p])&&reinterpret_cast<SetParameter>(b.functions[5])(object,p,d.parameters[p]))return Result::NativeFailure;
                }
                if(reinterpret_cast<SetBool>(b.functions[6])(object,d.bypass)||reinterpret_cast<SetBool>(b.functions[7])(object,d.active))return Result::NativeFailure;
            }
        }
        for(unsigned i=0;i<4;++i){std::uint32_t reverb[20];std::memcpy(reverb,s.reverb[i],sizeof(reverb));
            std::uint32_t actual[20]={};actual[0]=i;
            if(reinterpret_cast<Reverb>(b.functions[8])(s.identity.system,actual))return Result::NativeFailure;
            if(!std::memcmp(actual,reverb,sizeof(reverb)))continue;
            if(reinterpret_cast<Reverb>(b.functions[9])(s.identity.system,reverb))return Result::NativeFailure;
        }
    } __except(1){return Result::NativeFailure;}
    return Result::Succeeded;
}
}
bool Validate(Binding* b) noexcept {
    if(!b)return false;b->validated=false;
    if(!b->module||!b->exact_image||!b->resolve||!b->fence)return false;
    for(unsigned i=0;i<20;++i){b->functions[i]=b->resolve(b->user,exports[i].name);
        if(b->functions[i]!=b->module+exports[i].rva)return false;
    }
    b->validated=true;return true;
}
Result Capture(const Binding& b,const Identity& i,Snapshot* s) noexcept {
    if(!s)return Result::InvalidArgument;*s={};
    if(!Authorized(b,i,false))return Result::Unauthorized;s->identity=i;
    if(LockDsp(b,i.system,true))return Result::NativeFailure;
    auto result=ReadLocked(b,s);
    if(LockDsp(b,i.system,false))result=Result::NativeFailure;
    if(!Authorized(b,i,false))result=Result::Changed;
    s->complete=result==Result::Succeeded;return result;
}
Result Restore(const Binding& b,const Snapshot& s,const Identity* current) noexcept {
    if(!s.complete)return Result::InvalidArgument;
    const auto& identity=current?*current:s.identity;
    if(!Authorized(b,identity,true))return Result::Unauthorized;
    Snapshot actual={};const auto captured=Capture(b,identity,&actual);
    if(captured!=Result::Succeeded)return captured;
    if(!CanRestore(s,actual))return Result::Changed;
    if(LockDsp(b,s.identity.system,true))return Result::NativeFailure;
    auto result=RestoreLocked(b,s,actual);
    if(LockDsp(b,s.identity.system,false))result=Result::NativeFailure;
    if(!Authorized(b,identity,true))result=Result::Changed;return result;
}
bool CanRestore(const Snapshot& a,const Snapshot& b) noexcept {
    if(!a.complete||!b.complete||!ValidIdentity(a.identity)||!IdentityEqual(a.identity,b.identity)||a.master!=b.master||a.head!=b.head||
        std::memcmp(a.connections,b.connections,sizeof(a.connections)))return false;
    for(unsigned bus=0;bus<a.identity.buses;++bus)for(unsigned slot=0;slot<kDspSlots;++slot){
        const auto& x=a.dsp[bus][slot];const auto& y=b.dsp[bus][slot];
        if(x.type!=y.type||x.count!=y.count||x.count<0||x.count>kMaximumParameters)return false;
    }
    if(a.identity.master_effect&&(!b.identity.master_effect||a.master_effect.type!=b.master_effect.type||a.master_effect.count!=b.master_effect.count))return false;
    if(b.identity.master_effect&&(b.master_effect.type!=16||b.master_effect.count!=4))return false;
    return true;
}
bool Same(const Snapshot& a,const Snapshot& b,const float user_volume[3]) noexcept {
    if(!user_volume||!CanRestore(a,b)||!FloatEqual(a.master_volume,b.master_volume)||std::memcmp(a.reverb,b.reverb,sizeof(a.reverb)))return false;
    if(b.identity.master_effect){
        if(!a.identity.master_effect){if(!b.master_effect.bypass)return false;}
        else{if(a.master_effect.active!=b.master_effect.active||a.master_effect.bypass!=b.master_effect.bypass)return false;
            for(int p=0;p<a.master_effect.count;++p)if(!FloatEqual(a.master_effect.parameters[p],b.master_effect.parameters[p]))return false;}
    }
    for(unsigned bus=0;bus<a.identity.buses;++bus){
        if(!FloatEqual(a.volume[bus],b.volume[bus])||!FloatEqual(bus<3?user_volume[bus]:a.mix[bus],b.mix[bus]))return false;
        for(unsigned slot=0;slot<kDspSlots;++slot){const auto& x=a.dsp[bus][slot];const auto& y=b.dsp[bus][slot];
            if(x.type!=y.type||x.count!=y.count||x.active!=y.active||x.bypass!=y.bypass)return false;
            for(int p=0;p<x.count;++p)if(!FloatEqual(x.parameters[p],y.parameters[p]))return false;
        }
    }
    return true;
}
} // namespace
