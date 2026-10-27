#include "fixes/custom_map_asset_isolation/AudioSettingsNative.h"
#include <cstdio>
#include <cstring>
namespace a=dluma::fixes::custom_map_asset_isolation::audio_settings_native;
namespace w=dluma::fixes::custom_map_asset_isolation::audio_worker_reset;
using R=a::Result;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
template<class T> void Put(unsigned char* p,const T& x){std::memcpy(p,&x,sizeof(x));}
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    alignas(8) unsigned char game[0x400]={},settings[0x58]={},audio[0x6800]={};
    float grain=0.01f; std::uint64_t generation=7; bool authorized=true,fail_set=false;
    unsigned writes=0; a::Access access={}; a::State state; a::UserGains gains={};
    std::uint64_t request=0;
    Fixture();
    bool Apply(){return a::Capture(access,&gains)==R::Succeeded && a::Apply(&state,access,gains,12,2,9,&request)==R::Pending;}
    w::WorkerFrame frame() const {return {reinterpret_cast<std::uintptr_t>(audio),base+0x11042D,9,100,GetCurrentThreadId(),true,false};}
    void Mix(unsigned bus,float gain,int result=0){a::ObserveMix(&state,base,base+a::kDspMixCallerReturnRva,frame(),generation,bus,0x1000+bus,gain,result);}
    void All(){for(unsigned i=0;i<3;++i)Mix(i,gains.maximum[i]);}
};
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* obj,std::size_t size) {
    const auto begin=reinterpret_cast<std::uintptr_t>(obj);if(p<begin || n>size || p-begin>size-n)return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;
}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
    auto& f=*static_cast<Fixture*>(user);
    if(n==8 && (p==f.base+0xACA818 || p==f.base+0xAC8D78)){const auto value=reinterpret_cast<std::uintptr_t>(p==f.base+0xACA818?f.game:f.audio);std::memcpy(out,&value,8);return true;}
    if(n==4 && p==f.base+0xA91E60){std::memcpy(out,&f.grain,4);return true;}
    return Copy(p,out,n,f.game,sizeof(f.game)) || Copy(p,out,n,f.settings,sizeof(f.settings)) || Copy(p,out,n,f.audio,sizeof(f.audio));
}
bool Owner(void* user,std::uintptr_t* out,std::uint64_t* generation) noexcept {auto& f=*static_cast<Fixture*>(user);
    *out=reinterpret_cast<std::uintptr_t>(f.audio);*generation=f.generation;return true;}
bool Authorize(void* user,std::uint64_t transaction,std::uint32_t epoch,bool) noexcept {
    return static_cast<Fixture*>(user)->authorized && transaction==12 && epoch==2;
}
bool Set(void* user,std::uint64_t transaction,std::uint32_t epoch,std::uintptr_t owner,unsigned bus,float gain) noexcept {
    auto& f=*static_cast<Fixture*>(user);if(!Authorize(user,transaction,epoch,true)||owner!=reinterpret_cast<std::uintptr_t>(f.audio)||bus>=3)return false;
    ++f.writes;if(f.fail_set)return false;Put(f.audio+0x3C5C+740*bus,gain);return true;
}
Fixture::Fixture(){Put(game,base+0x8D4D78);Put(game+0x3D8,reinterpret_cast<std::uintptr_t>(settings));
    Put(audio,base+0x8A9628);Put(audio+0x3B94,3U);const std::int32_t values[]={50,80,25,70,2};
    std::memcpy(settings+0x34,values,sizeof(values));access={base,Read,Owner,Authorize,Set,this,true};}
bool AllBusesAndFullWorkerReturnRequired(){Fixture f;CHECK(f.Apply()&&f.writes==3);
    f.Mix(0,f.gains.maximum[0]);f.Mix(1,f.gains.maximum[1]);CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Pending);
    f.Mix(2,f.gains.maximum[2]);CHECK(a::Verify(&f.state,f.access,f.request,9)==R::Pending);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Succeeded);return true;}
bool FirstDirtyWorkerIsAccepted(){Fixture f;CHECK(f.Apply());f.All();CHECK(f.frame().original_completion_generation==9);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Succeeded);return true;}
bool OldOrForeignReceiptCannotComplete(){Fixture f;CHECK(f.Apply());auto frame=f.frame();frame.original_completion_generation=8;
    for(unsigned i=0;i<3;++i)a::ObserveMix(&f.state,f.base,f.base+a::kDspMixCallerReturnRva,frame,7,i,0x100+i,f.gains.maximum[i],0);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Pending);frame=f.frame();frame.route_identity++;
    a::ObserveMix(&f.state,f.base,f.base+a::kDspMixCallerReturnRva,frame,7,0,0x100,f.gains.maximum[0],0);
    frame=f.frame();frame.current_thread_id++;
    a::ObserveMix(&f.state,f.base,f.base+a::kDspMixCallerReturnRva,frame,7,0,0x100,f.gains.maximum[0],0);
    a::ObserveMix(&f.state,f.base,f.base+a::kDspMixCallerReturnRva+1,f.frame(),7,0,0x100,f.gains.maximum[0],0);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Pending);return true;}
bool LaterMixInvalidatesEarlierReceipt(){Fixture f;CHECK(f.Apply());f.All();CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Succeeded);
    f.Mix(1,0.1f);CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Pending);return true;}
bool NativeErrorIsNotAnAppliedMix(){Fixture f;CHECK(f.Apply());f.All();f.Mix(1,f.gains.maximum[1],4);f.All();
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::NativeFailure);return true;}
bool UserChangesAreNotOverwritten(){Fixture f;CHECK(a::Capture(f.access,&f.gains)==R::Succeeded);Put(f.settings+0x38,17);
    CHECK(a::Apply(&f.state,f.access,f.gains,12,2,9,&f.request)==R::Changed && !f.writes);
    CHECK(f.Apply());f.All();Put(f.settings+0x34,3);CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Changed);return true;}
bool OwnerRecreationInvalidatesReceipt(){Fixture f;CHECK(f.Apply());f.All();++f.generation;
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Changed);return true;}
bool AuthorityIsRequiredForApplyAndVerify(){Fixture f;CHECK(a::Capture(f.access,&f.gains)==R::Succeeded);f.authorized=false;
    CHECK(a::Apply(&f.state,f.access,f.gains,12,2,9,&f.request)==R::Unauthorized && !f.writes);
    f.authorized=true;CHECK(f.Apply());f.All();f.authorized=false;CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Unauthorized);return true;}
bool InvalidNativeStateDoesNotPublish(){Fixture f;Put(f.audio+0x3B94,2U);CHECK(a::Capture(f.access,&f.gains)==R::InvalidSettings && !f.gains.complete);
    Put(f.audio+0x3B94,3U);const unsigned nan=0x7FC00000U;std::memcpy(&f.grain,&nan,4);
    CHECK(a::Capture(f.access,&f.gains)==R::InvalidSettings && !f.gains.complete);return true;}
bool NativeClampingPreservesOtherUserValues(){Fixture f;Put(f.settings+0x34,-20);Put(f.settings+0x38,200);CHECK(f.Apply());
    CHECK(f.gains.maximum[0]==0.0f && f.gains.maximum[1]==1.0f && f.gains.levels[3]==70 && f.gains.levels[4]==2);
    std::int32_t fx=0;std::memcpy(&fx,f.settings+0x34,4);CHECK(fx==-20);return true;}
bool RepeatedRequestsCannotReuseOldCompletion(){Fixture f;CHECK(f.Apply());const auto old=f.request;f.All();a::Cancel(&f.state,old);
    CHECK(f.Apply() && f.request>old);CHECK(a::Verify(&f.state,f.access,old,10)==R::StaleRequest);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Pending);f.All();CHECK(a::Verify(&f.state,f.access,f.request,10)==R::Succeeded);return true;}
bool SetterFailureStaysFailed(){Fixture f;f.fail_set=true;CHECK(a::Capture(f.access,&f.gains)==R::Succeeded);
    CHECK(a::Apply(&f.state,f.access,f.gains,12,2,9,&f.request)==R::NativeFailure);f.All();
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::NativeFailure);return true;}
bool DistinctBusesRequireDistinctNativeConnections(){Fixture f;CHECK(f.Apply());
    for(unsigned i=0;i<3;++i)a::ObserveMix(&f.state,f.base,f.base+a::kDspMixCallerReturnRva,f.frame(),7,i,0x1000,f.gains.maximum[i],0);
    CHECK(a::Verify(&f.state,f.access,f.request,10)==R::NativeFailure);return true;}
}
extern "C" int dluma_test_audio_mix_call(void*,float,std::uint64_t);
extern "C" int dluma_audio_settings_dsp_mix_hook(void* connection,float value,std::uint64_t bus) {
    return connection==reinterpret_cast<void*>(0x1234) && value==0.375f && bus==0x1122334455667788ULL ? 73 : -1;
}
int main(){bool(*cases[])()={AllBusesAndFullWorkerReturnRequired,FirstDirtyWorkerIsAccepted,OldOrForeignReceiptCannotComplete,
    LaterMixInvalidatesEarlierReceipt,NativeErrorIsNotAnAppliedMix,UserChangesAreNotOverwritten,OwnerRecreationInvalidatesReceipt,
    AuthorityIsRequiredForApplyAndVerify,InvalidNativeStateDoesNotPublish,NativeClampingPreservesOtherUserValues,
    RepeatedRequestsCannotReuseOldCompletion,SetterFailureStaysFailed,DistinctBusesRequireDistinctNativeConnections};
    for(auto test:cases)if(!test())return 1;
    if(dluma_test_audio_mix_call(reinterpret_cast<void*>(0x1234),0.375f,0x1122334455667788ULL)!=73)return 1;
    std::puts("audio user gains: 14 scenarios passed, including the native mix shim ABI");return 0;}
