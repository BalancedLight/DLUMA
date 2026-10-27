#include "fixes/custom_map_asset_isolation/NativeResourceGateway.h"
#include <cstdio>
#include <cstring>
#include <thread>
extern "C" __declspec(dllimport) void WINAPI Sleep(DWORD);
namespace g=dluma::fixes::custom_map_asset_isolation::resource_gateway;
namespace p=dluma::fixes::custom_map_asset_isolation::packed;
namespace r=dluma::fixes::custom_map_asset_isolation::retained;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
namespace a=dluma::fixes::custom_map_asset_isolation::audio;
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::bridge {
bool FileIdentity(State*,std::uintptr_t,FileRecord*)noexcept{return false;}
bool SourceIdentity(State*,std::uintptr_t,SourceKey*)noexcept{return false;}
}
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fixture {
 g::State gateway;f::bridge::State files={};p::Ledger packs={};p::Origin origins[8]={};r::Ledger textures={};r::Record records[8]={};
 a::AudioFullPathProbeState probe={};std::atomic<unsigned> potential{0};bool installed=false,replay=false,context_valid=true;
 int corrupted=-1;std::uintptr_t target=0x12340000;bool ready=false,stream_qualified=false,stream_arguments=false;std::uint32_t epoch=0;
 Fixture(){
  p::Initialize(&packs,origins,8);r::Initialize(&textures,records,8);
  g::Callbacks callbacks={this,Read,Context,Potential,Replay,Pin,Material,MaterialFile,Revision,Classify};
  ready=g::Initialize(&gateway,0x180000000,0x190000000,r::windows::kPlayerEngineSha256,f::windows::kFilesystemSha256,
    callbacks,&files,&packs,&textures,&probe,target);
 }
 static bool Read(void* user,std::uintptr_t address,void* out,std::size_t bytes)noexcept{
  const auto& f=*static_cast<Fixture*>(user);std::size_t count=0;const auto* catalog=g::Catalog(&count);
  for(std::size_t i=0;i<count;++i){const auto& route=catalog[i];
   if(route.pointer_slot){const auto slot=(route.pointer_in_filesystem?0x190000000ULL:0x180000000ULL)+route.pointer_slot;
    if(address==slot&&bytes==8){const auto value=f.installed?g::Wrapper(route.route):
      (route.engine_rva?0x180000000ULL+route.engine_rva:0x190000000ULL+route.filesystem_rva);
     const auto result=value+(f.corrupted==static_cast<int>(i)?1:0);std::memcpy(out,&result,8);return true;}}
   else if(address==0x180000000ULL+route.engine_rva){
    if(!f.installed&&bytes==route.prefix_size){std::memcpy(out,route.prefix,bytes);return true;}
    if(f.installed&&bytes==14){unsigned char jump[14]={0xff,0x25,0,0,0,0};const auto value=g::Wrapper(route.route)+(f.corrupted==static_cast<int>(i)?1:0);
     std::memcpy(jump+6,&value,8);std::memcpy(out,jump,14);return true;}
   }
  }
  if(address==0x190000000ULL+a::kFilesystemFullPathProviderProbeRva&&bytes==14){
   unsigned char jump[14]={0xff,0x25,0,0,0,0};std::memcpy(jump+6,&f.target,8);std::memcpy(out,jump,14);return true;}
  return false;
 }
 static bool Context(void* user,g::Context* out)noexcept{const auto& f=*static_cast<Fixture*>(user);std::memset(out,0,sizeof(*out));out->clean_startup=f.context_valid&&!f.epoch;out->epoch=f.epoch;return true;}
 static void Potential(void* user,g::Route)noexcept{static_cast<Fixture*>(user)->potential.fetch_add(1);}
 static bool Replay(void* user,g::Route,std::uintptr_t)noexcept{return static_cast<Fixture*>(user)->replay;}
 static bool Pin(void*,g::InputKey,const f::FileRecord&)noexcept{return false;}
 static void Material(void*,const g::MaterialEvent&)noexcept{}
 static bool MaterialFile(void*,std::uintptr_t,const f::FileRecord&,f::SourceKey,r::Realm,std::uint32_t)noexcept{return false;}
 static bool Revision(void*,std::uint64_t* out)noexcept{*out=1;return true;}
 static bool Classify(void*,f::SourceKey,r::Realm*,std::uint32_t*)noexcept{return false;}
 bool Seal(){installed=true;return ready&&g::SealInstalled(&gateway);}
};
bool ExactInstalledRoutes(){Fixture f;CHECK(f.ready);CHECK(!g::SealInstalled(&f.gateway));CHECK(f.Seal());
 std::size_t count=0;const auto* routes=g::Catalog(&count);CHECK(count==g::kRoutes);
 for(std::size_t i=0;i<count;++i){CHECK(g::Wrapper(routes[i].route));f.corrupted=static_cast<int>(i);CHECK(!g::VerifyInstalled(&f.gateway));}
 f.corrupted=-1;CHECK(g::VerifyInstalled(&f.gateway));++f.target;CHECK(!g::VerifyInstalled(&f.gateway));return true;}
bool PendingProducerInvalidatesFence(){Fixture f;CHECK(f.Seal());g::Lease lease={};CHECK(g::Close(&f.gateway,10,&lease));
 p::Fence proof={};CHECK(g::CaptureProducerFence(&f.gateway,&proof));std::atomic<bool> returned{false};
 std::thread producer([&]{g::Call call={};g::Begin(&f.gateway,g::Route::TextureLoad,0x4000,&call);call.returned=true;g::End(&f.gateway,&call);returned.store(true);});
 for(unsigned i=0;i<1000&&!f.gateway.pending_calls.load();++i)Sleep(1);
 const bool pending=f.gateway.pending_calls.load()!=0,blocked=!returned.load(),invalid=!g::CaptureProducerFence(&f.gateway,&proof);
 const bool opened=g::Open(&f.gateway,lease);producer.join();CHECK(pending&&blocked&&invalid&&opened&&returned.load());
 CHECK(!f.gateway.lost.load());CHECK(g::Close(&f.gateway,10,&lease));CHECK(g::Open(&f.gateway,lease));return true;}
bool FreshContextRequired(){Fixture f;CHECK(f.Seal());g::Lease lease={};CHECK(g::Close(&f.gateway,10,&lease));
 f.context_valid=false;p::Fence fence={};CHECK(!g::CaptureProducerFence(&f.gateway,&fence));CHECK(g::Open(&f.gateway,lease));return true;}
bool UnauthorizedOwnerRetainPoisonsProof(){Fixture f;CHECK(f.Seal());g::Lease lease={};CHECK(g::Close(&f.gateway,10,&lease));
 g::Call call={};g::Begin(&f.gateway,g::Route::MaterialAcquire,0x5000,&call);call.returned=true;g::End(&f.gateway,&call);
 p::Fence fence={};CHECK(f.gateway.lost.load()&&!g::CaptureProducerFence(&f.gateway,&fence));CHECK(g::Open(&f.gateway,lease));return true;}
bool NativeUnwindPoisonsCoverage(){Fixture f;CHECK(f.Seal());g::Call call={};g::Begin(&f.gateway,g::Route::AssetLoad,0,&call);
 CHECK(call.active);g::End(&f.gateway,&call);CHECK(!call.active&&!f.gateway.active_calls.load()&&f.gateway.lost.load());return true;}
bool ReplayedOwnerRetainRemainsObserved(){Fixture f;CHECK(f.Seal());g::Lease lease={};CHECK(g::Close(&f.gateway,10,&lease));f.replay=true;
 const auto prior=f.gateway.revision.load();g::Call call={};g::Begin(&f.gateway,g::Route::MaterialAcquire,0x5000,&call);
 call.returned=true;g::End(&f.gateway,&call);p::Fence fence={};CHECK(f.potential.load()==1&&f.gateway.revision.load()>prior);
 CHECK(g::CaptureProducerFence(&f.gateway,&fence));CHECK(g::Open(&f.gateway,lease));return true;}
bool StaleLeaseCannotOpen(){Fixture f;CHECK(f.Seal());g::Lease lease={};CHECK(g::Close(&f.gateway,10,&lease));auto stale=lease;++stale.generation;
 CHECK(!g::Open(&f.gateway,stale));CHECK(f.gateway.held.load());CHECK(g::Open(&f.gateway,lease));return true;}
bool FinalGlobalPackPropagatesGlobalOwnership(){Fixture f;CHECK(f.Seal());g::Call call={};g::Begin(&f.gateway,g::Route::PackLoad,0,&call);
 g::PackConstructed(&f.gateway,0x6000,&call);g::PackRegistered(&f.gateway,call.pack);g::TextureConstructed(&f.gateway,0x7000);
 r::Input input={};input.file=0x8000;input.file_generation=1;input.source=0x9000;input.source_generation=2;
 input.content_sha256[0]=42;input.content_bytes=16;input.realm=r::Realm::Global;
 CHECK(p::RetainInput(&f.packs,call.pack,input)==p::Result::Succeeded);g::PackLoaded(&f.gateway,call.pack);
 call.returned=true;g::End(&f.gateway,&call);CHECK(!f.gateway.lost.load());CHECK(f.records[0].global&&!f.records[0].project&&f.records[0].input_count==1);return true;}
bool MixedPackRemainsProjectOwned(){Fixture f;CHECK(f.Seal());g::Call call={};g::Begin(&f.gateway,g::Route::PackLoad,0,&call);
 g::PackConstructed(&f.gateway,0x6000,&call);g::PackRegistered(&f.gateway,call.pack);g::TextureConstructed(&f.gateway,0x7000);
 r::Input input={};input.file=0x8000;input.file_generation=1;input.source=0x9000;input.source_generation=2;
 input.content_sha256[0]=42;input.content_bytes=16;input.realm=r::Realm::Global;
 CHECK(p::RetainInput(&f.packs,call.pack,input)==p::Result::Succeeded);++input.file_generation;input.realm=r::Realm::Project;input.epoch=2;
 CHECK(p::RetainInput(&f.packs,call.pack,input)==p::Result::Succeeded);g::PackLoaded(&f.gateway,call.pack);
 call.returned=true;g::End(&f.gateway,&call);CHECK(!f.gateway.lost.load());CHECK(!f.records[0].global&&f.records[0].project&&f.records[0].input_count==1);return true;}
bool __fastcall NativeStream(void*,void*,void*,void*,void*,void*,void*,int succeeds,int,int){return succeeds!=0;}
bool StreamProof(void* user,std::uintptr_t runtime,std::uintptr_t operation,std::uintptr_t pack,std::uintptr_t entry,
 std::uintptr_t buffer,std::uintptr_t callback,std::uintptr_t job)noexcept{auto& f=*static_cast<Fixture*>(user);
 f.stream_arguments=runtime==0x1000&&operation==0x2000&&pack==0x6000&&entry==0x3000&&buffer==0x4000&&callback==0x5000&&job==0x7000;
 return f.stream_arguments&&f.stream_qualified;}
bool SuccessfulStreamNeedsConcreteOwner(){Fixture f;p::Key key={};CHECK(p::Constructed(&f.packs,0x6000,&key)==p::Result::Succeeded);
 CHECK(g::BindOriginal(&f.gateway,g::Route::StreamResource,reinterpret_cast<std::uintptr_t>(&NativeStream)));f.gateway.callbacks.stream_request=StreamProof;
 CHECK(f.Seal());CHECK(g::PublishBridge(&f.gateway));f.epoch=2;
 auto stream=reinterpret_cast<bool(__fastcall*)(void*,void*,void*,void*,void*,void*,void*,int,int,int)>(g::Wrapper(g::Route::StreamResource));
 CHECK(!stream((void*)0x1000,(void*)0x2000,(void*)0x6000,(void*)0x3000,(void*)0x4000,(void*)0x5000,(void*)0x7000,0,0,0));CHECK(!f.origins[key.slot].unclassified_stream_epoch);
 f.stream_qualified=true;CHECK(stream((void*)0x1000,(void*)0x2000,(void*)0x6000,(void*)0x3000,(void*)0x4000,(void*)0x5000,(void*)0x7000,1,0,0));CHECK(f.stream_arguments&&!f.origins[key.slot].unclassified_stream_epoch);
 f.stream_qualified=false;CHECK(stream((void*)0x1000,(void*)0x2000,(void*)0x6000,(void*)0x3000,(void*)0x4000,(void*)0x5000,(void*)0x7000,1,0,0));CHECK(f.origins[key.slot].unclassified_stream_epoch==2);return true;}
int main(){const struct{const char* name;bool(*run)();} tests[]={
 {"all exact installed routes",ExactInstalledRoutes},{"pending producer invalidates cleanup fence",PendingProducerInvalidatesFence},
 {"fresh authoritative context",FreshContextRequired},{"unpermitted owner mutation",UnauthorizedOwnerRetainPoisonsProof},
 {"incomplete native original",NativeUnwindPoisonsCoverage},{"permitted replay remains observed",ReplayedOwnerRetainRemainsObserved},
 {"stale lease rejected",StaleLeaseCannotOpen},{"late global packs preserve global ownership",FinalGlobalPackPropagatesGlobalOwnership},
 {"mixed input belongs to project pack",MixedPackRemainsProjectOwned},{"successful stream requires concrete owner",SuccessfulStreamNeedsConcreteOwner}};
 for(const auto& test:tests)if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}
 std::printf("PASS %zu resource gateway scenarios\n",sizeof(tests)/sizeof(tests[0]));return 0;}
