#include "fixes/custom_map_asset_isolation/AudioBusNative.h"
#include <cstdio>
#include <cstring>
namespace a=dluma::fixes::custom_map_asset_isolation::audio_bus_native;
constexpr std::uintptr_t engine=0x100000,owner=0x2000000,env=0x3000000,groups=0x4000000,parameters=0x5000000;
unsigned char image[0xE8D000],object[30000],environments[1536],group_data[112],parameter_data[24];
struct Region{std::uintptr_t at;unsigned char* data;std::size_t size;};
Region regions[]={{engine,image,sizeof(image)},{owner,object,sizeof(object)},{env,environments,sizeof(environments)},{groups,group_data,sizeof(group_data)},{parameters,parameter_data,sizeof(parameter_data)}};
bool allow=true,mutate=true,parked=true;unsigned writes=0;
bool Read(void*,std::uintptr_t p,void* v,std::size_t n) noexcept{for(const auto& r:regions)if(p>=r.at&&n<=r.size&&p-r.at<=r.size-n){std::memcpy(v,r.data+(p-r.at),n);return true;}return false;}
bool Write(void*,std::uintptr_t p,const void* v,std::size_t n) noexcept{++writes;for(const auto& r:regions)if(p>=r.at&&n<=r.size&&p-r.at<=r.size-n){std::memcpy(r.data+(p-r.at),v,n);return true;}return false;}
template<class T>void Put(std::uintptr_t p,T v){Write(nullptr,p,&v,sizeof(v));}
template<class T>T Get(std::uintptr_t p){T v={};Read(nullptr,p,&v,sizeof(v));return v;}
bool Fence(void*,a::Fence* f,bool mutation) noexcept{if(!allow||(mutation&&!mutate))return false;*f={owner,3,7,parked,true,true,mutate};return true;}
a::Access Init(){std::memset(image,0,sizeof(image));std::memset(object,0,sizeof(object));std::memset(environments,0,sizeof(environments));
    std::memset(group_data,0,sizeof(group_data));std::memset(parameter_data,0,sizeof(parameter_data));allow=mutate=parked=true;
    Put(engine+0xAC8D78,owner);Put(owner,engine+0x8A9628);Put(owner+15252,4U);Put(owner+7536,std::uintptr_t(0x700));
    Put(owner+7912,groups);Put(owner+7920,4U);Put(owner+7924,14U);Put(owner+26272,static_cast<unsigned char>(1));Put(engine+0xDD1C54,3U);
    for(unsigned b=0;b<4;++b){Put(groups+8ULL*b,std::uintptr_t(0x100+b));for(unsigned d=0;d<2;++d)Put(owner+25632+112ULL*d+8ULL*b,std::uintptr_t(0x200+b*3+d));
        Put(owner+15272+740ULL*b+180,0.5f);Put(owner+15272+740ULL*b+184,0.75f);}
    for(unsigned e=0;e<2;++e){Put(owner+8+24ULL*e,env+768ULL*e);Put(owner+16+24ULL*e,1U);Put(owner+20+24ULL*e,16U);
        const char text[]="shared";Write(nullptr,env+768ULL*e+32,text,sizeof(text));
        const auto manager=owner+(e?7936:176);for(unsigned role=0;role<3;++role)Put(manager+7248+8ULL*role,manager+2416ULL*role);}
    Put(owner+7456,parameters);Put(owner+7464,1U);Put(owner+7468,1U);Put(parameters,engine+0x8F0000);Put(parameters+8,1U);Put(parameters+16,engine+0xAD5190);
    const char name[]="spatial_gain";Write(nullptr,engine+0x8F0000,name,sizeof(name));Put(engine+0xAD5190,0.75f);writes=0;
    return {engine,&Read,&Write,&Fence,nullptr,true};
}
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(){static a::Snapshot baseline,current,scratch;float maximum[3]={0.8f,0.5f,0.5f};auto access=Init();
    CHECK(a::Capture(access,&baseline)==a::Result::Succeeded);std::puts("PASS capture all native owners");
    Put(owner+15272+180,0.8f);Put(owner+15272+32,9.0f);Put(env+48+32,'x');Put(owner+16,2U);Put(engine+0xA933E0,99U);Put(engine+0xAD5190,0.2f);
    Put(owner+7936+4832,100.0f);Put(owner+7936+4832+60,99U);Put(owner+15272+189,static_cast<unsigned char>(0xA5));
    CHECK(a::Restore(access,baseline,&scratch)==a::Result::Succeeded);CHECK(Get<float>(owner+15272+180)==0.8f&&Get<unsigned char>(owner+15272+189)==0xA5&&Get<unsigned>(owner+7936+4832+60)==99U);
    CHECK(a::Capture(access,&current)==a::Result::Succeeded&&a::Same(baseline,current,maximum));std::puts("PASS mappings environments reverb listener and scalar restore");
    Put(owner+7936+4832,4.0f);CHECK(a::Capture(access,&current)==a::Result::Succeeded&&!a::Same(baseline,current,maximum));std::puts("PASS listener residue detected");
    access=Init();Put(owner+15272+588,8U);CHECK(a::Capture(access,&current)==a::Result::ReadFailed);std::puts("PASS effect count bound");
    access=Init();Put(owner+16,17U);CHECK(a::Capture(access,&current)==a::Result::ReadFailed);std::puts("PASS environment vector bound");
    access=Init();Put(owner+7936+7248,owner+7936+1);CHECK(a::Capture(access,&current)==a::Result::ReadFailed);std::puts("PASS listener pointer roles validated");
    access=Init();Put(owner+7936+2416+2405,static_cast<unsigned char>(1));CHECK(a::Capture(access,&current)==a::Result::ReadFailed);std::puts("PASS pending publication rejected");
    access=Init();parked=false;CHECK(a::Restore(access,baseline,&scratch)==a::Result::Unauthorized&&!writes);std::puts("PASS unparked mutation rejected");
    access=Init();mutate=false;CHECK(a::Restore(access,baseline,&scratch)==a::Result::Unauthorized&&!writes);std::puts("PASS open producer mutation rejected");
    access=Init();Put(parameters+16,engine+0xAD5194);writes=0;CHECK(a::Restore(access,baseline,&scratch)==a::Result::Changed&&!writes);std::puts("PASS scalar owner replacement rejected");
    access=Init();Put(groups,std::uintptr_t(0x999));writes=0;CHECK(a::Restore(access,baseline,&scratch)==a::Result::Changed&&!writes);std::puts("PASS backend replacement rejected");
    access=Init();Put(owner+15256,1U);CHECK(a::Capture(access,&current)==a::Result::ReadFailed);std::puts("PASS unapplied effects rejected");
    access=Init();CHECK(a::Capture(access,&baseline)==a::Result::Succeeded);
    baseline.on_start_count=1;baseline.on_start[0]={0x8ACB08,0x111D50,0x111DC0};CHECK(a::ReplayProgramSupported(baseline));
    Put(engine+0x8ACB08+40,engine+0x111D50);Put(engine+0x8ACB08+48,engine+0x111DC0);CHECK(a::ReplayProgramUnchanged(access,baseline));
    Put(engine+0x8ACB08+48,engine+0x111020);CHECK(!a::ReplayProgramUnchanged(access,baseline));std::puts("PASS immutable opcode binding rechecked before mutation");
    baseline.on_start[0]={0x8ACA88,0x110EE0,0x111020};CHECK(!a::ReplayProgramSupported(baseline));
    baseline.on_start[0]={0x8ACB88,0x110EE0,0x111F50};CHECK(!a::ReplayProgramSupported(baseline));std::puts("PASS recursive and unload OnStart owners rejected before replay");return 0;
}
