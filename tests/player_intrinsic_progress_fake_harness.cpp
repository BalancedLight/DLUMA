#include "fixes/custom_map_asset_isolation/PlayerIntrinsicProgressSnapshot.h"
#include <cstdio>
#include <cstring>
namespace intrinsic=dluma::fixes::custom_map_asset_isolation::player_intrinsic_progress;
namespace stored=dluma::fixes::custom_map_asset_isolation::player_stored_progress;
namespace {
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fake {
 unsigned char state[1200]={},rows[256]={},attributes[40]={},data[256]={};std::uint64_t revision=1;bool exclusive=true,active=false;
 template<class T>void Put(unsigned char* p,std::size_t off,T value){std::memcpy(p+off,&value,sizeof(value));}
 Fake(){Put(state,0,std::uintptr_t{0x117E2FF8});Put(state,8,std::uintptr_t{0x117E3788});Put(state,16,std::uintptr_t{0x117E37B0});Put(state,48,std::uintptr_t{0x117E37C0});Put(state,64,reinterpret_cast<std::uintptr_t>(rows));Put(state,72,std::uint32_t{8});Put(state,76,std::uint32_t{8});Put(state,1080,reinterpret_cast<std::uintptr_t>(attributes));Put(attributes,32,std::uintptr_t{0x20000000});}
};
bool Read(void*,std::uintptr_t p,void* out,std::size_t n)noexcept{if(!p&&n)return false;std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Fence(void* user,stored::Fence* out)noexcept{auto& f=*static_cast<Fake*>(user);*out={reinterpret_cast<std::uintptr_t>(f.state),f.active?1U:0U,0,7,f.revision,GetCurrentThreadId(),f.exclusive};return true;}
bool Capture(Fake& f,intrinsic::Snapshot& out){stored::Reader r={0x10000000,Read,Fence,nullptr,&f,1<<20,true};return intrinsic::Capture(r,reinterpret_cast<std::uintptr_t>(f.state),7,&out);}
bool NineActualFields(){Fake f;intrinsic::Snapshot a={},b={};CHECK(Capture(f,a)&&Capture(f,b));CHECK(a.intrinsic_fields_complete&&a.owned_user_graph_complete&&intrinsic::Equal(a,b));for(auto& d:a.fields)CHECK(d.complete&&d.bytes>0);return true;}
bool DerivedUpdatesExcludedUserValuesPreserved(){Fake f;intrinsic::Snapshot a={},b={};CHECK(Capture(f,a));f.state[290]=3;std::memset(f.state+1136,11,24);std::memset(f.state+1164,22,32);std::memset(f.rows,33,8);std::memset(f.rows+12,44,8);std::memset(f.rows+22,55,2);CHECK(Capture(f,b)&&intrinsic::Equal(a,b));f.rows[8]=99;CHECK(Capture(f,b)&&!intrinsic::Equal(a,b));return true;}
bool SelectedIdentityIsNotDerivedPerk(){Fake f;intrinsic::Snapshot a={},b={};CHECK(Capture(f,a));f.state[1060]=9;CHECK(Capture(f,b)&&!intrinsic::Equal(a,b));return true;}
bool AttributeContentsDeeplyCaptured(){Fake f;f.Put(f.attributes,0,reinterpret_cast<std::uintptr_t>(f.data));f.Put(f.attributes,8,std::uint16_t{1});f.Put(f.attributes,10,std::uint16_t{1});intrinsic::Snapshot a={},b={};CHECK(Capture(f,a));f.data[4]=78;CHECK(Capture(f,b)&&!intrinsic::Equal(a,b));return true;}
bool TimedDefinitionPointerIsRetained(){Fake f;f.Put(f.state,264,reinterpret_cast<std::uintptr_t>(f.data));f.Put(f.state,272,std::uint32_t{1});f.Put(f.state,276,std::uint32_t{1});f.Put(f.data,8,std::uintptr_t{0x40000000});intrinsic::Snapshot a={},b={};CHECK(Capture(f,a)&&a.timed_buff_definition_count==1);f.Put(f.data,8,std::uintptr_t{0x40000008});CHECK(Capture(f,b)&&!intrinsic::Equal(a,b));return true;}
bool UnknownOwnedDataBlocksWholeGraph(){Fake f;f.Put(f.state,1032,reinterpret_cast<std::uintptr_t>(f.data));f.Put(f.state,1040,std::uint32_t{1});intrinsic::Snapshot a={};CHECK(Capture(f,a));CHECK(a.intrinsic_fields_complete&&!a.owned_user_graph_complete&&(a.unqualified_graphs&intrinsic::AuxiliaryVectors));return true;}
bool TransientActorPointersAreNotPersistentRewards(){Fake f;f.Put(f.state,904,reinterpret_cast<std::uintptr_t>(f.data));f.Put(f.state,912,std::uint32_t{1});f.Put(f.state,916,std::uint32_t{1});f.Put(f.data,0,std::uintptr_t{0x40000000});intrinsic::Snapshot a={};CHECK(Capture(f,a)&&a.transient_actor_count==1);CHECK(!a.owned_user_graph_complete&&(a.unqualified_graphs&intrinsic::TransientActors));return true;}
bool FogAndPursuitAreDeepValues(){Fake f;const auto link=reinterpret_cast<std::uintptr_t>(f.data+40);f.Put(f.state,960,link);f.Put(f.state,968,std::uint32_t{1});f.Put(f.data,0,std::uint32_t{3});f.Put(f.data,8,std::uint32_t{8});f.Put(f.data,16,reinterpret_cast<std::uintptr_t>(f.data+200));f.data[200]=3;intrinsic::Snapshot a={},b={};CHECK(Capture(f,a)&&a.owned_user_graph_complete);f.data[200]=7;CHECK(Capture(f,b)&&!intrinsic::Equal(a,b));f.Put(f.data,40,link);CHECK(!Capture(f,b));return true;}
bool CyclicListAndMissingFenceReject(){Fake f;f.Put(f.state,144,reinterpret_cast<std::uintptr_t>(f.data+16));f.Put(f.state,152,reinterpret_cast<std::uintptr_t>(f.data+16));f.Put(f.state,160,std::uint32_t{1});f.Put(f.data,16,reinterpret_cast<std::uintptr_t>(f.data+16));intrinsic::Snapshot a={};CHECK(!Capture(f,a));Fake g;g.exclusive=false;CHECK(!Capture(g,a));return true;}
}
int main(){bool(*tests[])()={NineActualFields,DerivedUpdatesExcludedUserValuesPreserved,SelectedIdentityIsNotDerivedPerk,AttributeContentsDeeplyCaptured,TimedDefinitionPointerIsRetained,UnknownOwnedDataBlocksWholeGraph,CyclicListAndMissingFenceReject,FogAndPursuitAreDeepValues,TransientActorPointersAreNotPersistentRewards};for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);++i){if(!tests[i]())return 1;std::printf("PASS intrinsic_progress_%u\n",i);}return 0;}
