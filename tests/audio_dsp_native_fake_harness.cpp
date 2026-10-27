#include "fixes/custom_map_asset_isolation/AudioDspNative.h"
#include <cstdio>
#include <cstring>
namespace a=dluma::fixes::custom_map_asset_isolation::audio_dsp_native;
struct Fake {
    a::Snapshot state={};bool authorized=true,mutation=true,locked=false,fail=false,drop=false;int writes=0,unlocks=0;
} g;
bool Fence(void*,const a::Identity& i,bool mutation) noexcept{return g.authorized&&(!mutation||g.mutation)&&i.owner==g.state.identity.owner&&i.generation==g.state.identity.generation;}
a::Dsp* D(std::uintptr_t p){if(p&&p==g.state.identity.master_effect)return &g.state.master_effect;for(unsigned b=0;b<g.state.identity.buses;++b)for(unsigned d=0;d<3;++d)if(g.state.identity.dsp[b][d]==p)return &g.state.dsp[b][d];return nullptr;}
int __fastcall Parameter(std::uintptr_t p,int n,float* v,char*,int){auto* d=D(p);if(!d||g.fail||!g.locked)return 1;*v=d->parameters[n];return 0;}
int __fastcall Count(std::uintptr_t p,int* v){auto* d=D(p);if(!d)return 1;*v=d->count;return 0;}
int __fastcall Type(std::uintptr_t p,int* v){auto* d=D(p);if(!d)return 1;*v=d->type;return 0;}
int __fastcall Bypass(std::uintptr_t p,bool* v){auto* d=D(p);if(!d)return 1;*v=d->bypass;return 0;}
int __fastcall Active(std::uintptr_t p,bool* v){auto* d=D(p);if(!d)return 1;*v=d->active;return 0;}
int __fastcall SetParameter(std::uintptr_t p,int n,float v){auto* d=D(p);if(!d||g.fail||!g.locked)return 1;++g.writes;if(!g.drop)d->parameters[n]=v;return 0;}
int __fastcall SetBypass(std::uintptr_t p,bool v){auto* d=D(p);if(!d)return 1;++g.writes;d->bypass=v;return 0;}
int __fastcall SetActive(std::uintptr_t p,bool v){auto* d=D(p);if(!d)return 1;++g.writes;d->active=v;return 0;}
int __fastcall Reverb(std::uintptr_t,void* p){auto* d=static_cast<std::uint32_t*>(p);if(*d>=4)return 1;std::memcpy(d,g.state.reverb[*d],80);return 0;}
int __fastcall SetReverb(std::uintptr_t,void* p){auto* d=static_cast<std::uint32_t*>(p);if(*d>=4)return 1;++g.writes;std::memcpy(g.state.reverb[*d],d,80);return 0;}
int __fastcall Volume(std::uintptr_t p,float* v){if(p==0x800){*v=g.state.master_volume;return 0;}for(unsigned i=0;i<g.state.identity.buses;++i)if(g.state.identity.groups[i]==p){*v=g.state.volume[i];return 0;}return 1;}
int __fastcall SetVolume(std::uintptr_t p,float v){if(p==0x800){++g.writes;g.state.master_volume=v;return 0;}for(unsigned i=0;i<g.state.identity.buses;++i)if(g.state.identity.groups[i]==p){++g.writes;g.state.volume[i]=v;return 0;}return 1;}
int __fastcall Lock(std::uintptr_t){if(g.locked)return 1;g.locked=true;return 0;}
int __fastcall Unlock(std::uintptr_t){g.locked=false;++g.unlocks;return 0;}
int __fastcall Master(std::uintptr_t,std::uintptr_t* p){*p=0x800;return 0;}
int __fastcall Head(std::uintptr_t,std::uintptr_t* p){*p=0x900;return 0;}
int __fastcall Inputs(std::uintptr_t,int* n){*n=static_cast<int>(g.state.identity.buses);return 0;}
int __fastcall Input(std::uintptr_t,int n,std::uintptr_t*,std::uintptr_t* p){*p=0xA00+n;return 0;}
int __fastcall Mix(std::uintptr_t p,float* v){*v=g.state.mix[p-0xA00];return 0;}
int __fastcall SetMix(std::uintptr_t p,float v){++g.writes;g.state.mix[p-0xA00]=v;return 0;}
a::Binding Init(){g={};g.state.identity.owner=0x100;g.state.identity.system=0x200;g.state.identity.generation=7;g.state.identity.buses=4;
    for(unsigned b=0;b<4;++b){g.state.identity.groups[b]=0x300+b;g.state.volume[b]=g.state.mix[b]=0.25f;
        for(unsigned s=0;s<2;++s){g.state.identity.dsp[b][s]=0x400+b*3+s;auto& d=g.state.dsp[b][s];d.type=s?6:3;d.count=2;d.active=true;d.parameters[0]=0.5f;d.parameters[1]=0.25f;}}
    for(unsigned i=0;i<4;++i)g.state.reverb[i][0]=i;
    a::Binding r={};r.exact_image=r.validated=true;r.fence=&Fence;
    r.functions[0]=reinterpret_cast<std::uintptr_t>(&Parameter);r.functions[1]=reinterpret_cast<std::uintptr_t>(&Count);r.functions[2]=reinterpret_cast<std::uintptr_t>(&Bypass);
    r.functions[3]=reinterpret_cast<std::uintptr_t>(&Active);r.functions[4]=reinterpret_cast<std::uintptr_t>(&Type);r.functions[5]=reinterpret_cast<std::uintptr_t>(&SetParameter);
    r.functions[6]=reinterpret_cast<std::uintptr_t>(&SetBypass);r.functions[7]=reinterpret_cast<std::uintptr_t>(&SetActive);r.functions[8]=reinterpret_cast<std::uintptr_t>(&Reverb);
    r.functions[9]=reinterpret_cast<std::uintptr_t>(&SetReverb);r.functions[10]=reinterpret_cast<std::uintptr_t>(&Volume);r.functions[11]=reinterpret_cast<std::uintptr_t>(&SetVolume);
    r.functions[12]=reinterpret_cast<std::uintptr_t>(&Lock);r.functions[13]=reinterpret_cast<std::uintptr_t>(&Unlock);
    r.functions[14]=reinterpret_cast<std::uintptr_t>(&Master);r.functions[15]=reinterpret_cast<std::uintptr_t>(&Head);r.functions[16]=reinterpret_cast<std::uintptr_t>(&Inputs);
    r.functions[17]=reinterpret_cast<std::uintptr_t>(&Input);r.functions[18]=reinterpret_cast<std::uintptr_t>(&Mix);r.functions[19]=reinterpret_cast<std::uintptr_t>(&SetMix);return r;
}
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(){
    a::Snapshot before={},after={};float user[3]={0.25f,0.25f,0.25f};auto b=Init();
    CHECK(a::Capture(b,g.state.identity,&before)==a::Result::Succeeded&&!g.locked);std::puts("PASS actual native parameter snapshot");
    g.state.dsp[3][1].parameters[0]=0.9f;g.state.reverb[0][3]=99;g.state.volume[3]=0.9f;g.state.mix[0]=0.75f;g.state.mix[3]=0.9f;
    CHECK(a::Restore(b,before)==a::Result::Succeeded&&g.state.mix[0]==0.75f&&g.state.volume[3]==0.25f&&g.state.mix[3]==0.25f);user[0]=0.75f;
    CHECK(a::Capture(b,g.state.identity,&after)==a::Result::Succeeded&&a::Same(before,after,user));std::puts("PASS restore while preserving current user connection gains");
    g.drop=true;g.state.dsp[2][0].parameters[0]=0.9f;CHECK(a::Restore(b,before)==a::Result::Succeeded);
    CHECK(a::Capture(b,g.state.identity,&after)==a::Result::Succeeded&&!a::Same(before,after,user));std::puts("PASS ignored setter detected by actual readback");
    b=Init();g.fail=true;CHECK(a::Capture(b,g.state.identity,&after)==a::Result::NativeFailure&&!g.locked&&g.unlocks==1);std::puts("PASS failed getter releases DSP lock");
    b=Init();g.mutation=false;CHECK(a::Restore(b,before)==a::Result::Unauthorized&&!g.writes);std::puts("PASS mutation requires owner fence");
    b=Init();g.state.identity.generation++;CHECK(a::Restore(b,before)==a::Result::Unauthorized&&!g.writes);std::puts("PASS owner lifetime reuse rejected");
    b=Init();g.state.dsp[1][0].type=9;CHECK(a::Capture(b,g.state.identity,&after)==a::Result::NativeFailure&&!g.locked);std::puts("PASS unexpected DSP topology rejected");
    b=Init();g.state.dsp[0][0].count=99;CHECK(a::Capture(b,g.state.identity,&after)==a::Result::NativeFailure);std::puts("PASS parameter capacity bounded");
    b=Init();g.state.identity.groups[1]=g.state.identity.groups[0];CHECK(a::Capture(b,g.state.identity,&after)==a::Result::Unauthorized);std::puts("PASS aliased group identity rejected");
    b=Init();CHECK(!a::Validate(&b)&&!b.validated);std::puts("PASS unqualified export binding rejected");
    b=Init();CHECK(a::Capture(b,g.state.identity,&before)==a::Result::Succeeded);
    g.state.identity.master_effect=0xB00;g.state.master_effect.type=16;g.state.master_effect.count=4;g.state.master_effect.active=true;g.state.master_effect.bypass=false;
    CHECK(a::Restore(b,before,&g.state.identity)==a::Result::Succeeded&&g.state.master_effect.bypass);
    user[0]=0.25f;CHECK(a::Capture(b,g.state.identity,&after)==a::Result::Succeeded&&a::Same(before,after,user));std::puts("PASS newly materialized built-in compressor is bypassed");
    g.state.master_effect.bypass=false;CHECK(a::Capture(b,g.state.identity,&after)==a::Result::Succeeded&&!a::Same(before,after,user));std::puts("PASS active extra master compressor rejected by readback");
    g.state.master_volume=0.7f;g.state.master_effect.bypass=true;CHECK(a::Restore(b,before,&g.state.identity)==a::Result::Succeeded&&g.state.master_volume==before.master_volume);std::puts("PASS native master gain restored");return 0;
}
