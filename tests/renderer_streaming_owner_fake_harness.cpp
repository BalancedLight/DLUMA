#include "fixes/custom_map_asset_isolation/RendererStreamingOwner.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <new>
extern "C" __declspec(dllimport) LPVOID WINAPI VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);
extern "C" __declspec(dllimport) BOOL WINAPI VirtualFree(LPVOID,SIZE_T,DWORD);
extern "C" __declspec(dllimport) void WINAPI InitializeCriticalSection(void*);
extern "C" __declspec(dllimport) void WINAPI DeleteCriticalSection(void*);
extern "C" __declspec(dllimport) void WINAPI Sleep(DWORD);
namespace s=dluma::fixes::custom_map_asset_isolation::renderer_streaming;
namespace p=dluma::fixes::custom_map_asset_isolation::packed;
namespace r=dluma::fixes::custom_map_asset_isolation::retained;
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
extern "C" bool CheckRendererRelayUnwind(const unsigned char*,std::size_t) noexcept;
struct Fixture;static Fixture* current;static s::State owner;
struct Fixture{
 unsigned char* image=nullptr;std::uintptr_t api=0,vector[16]={};unsigned char descriptors[16][64]={},textures[16][160]={};
 s::Record records[16]={};s::NativeDescriptor scratch[16]={};p::Origin origins[8]={};p::Ledger packs={};p::Fence fence={};
 std::uint32_t used=0,deleted=0;bool ready=false,skip_collect=false;
 std::atomic<bool> hold_tick{false},tick_entered{false},resume_tick{false};
 unsigned char native_pack[160]={},stream_job[80]={};std::uint32_t stream_operation[2]={0,1};
 std::uintptr_t stream_entry=0;bool classify_stream=false,stream_owned=false,wrong_operation=false;
 Fixture(){
  current=this;image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x48B000,0x3000,PAGE_READWRITE));if(!image)return;
  owner.~State();::new(&owner)s::State{};
  api=reinterpret_cast<std::uintptr_t>(image)+0x70E38;Put(0x481E00,reinterpret_cast<std::uintptr_t>(vector));Put32(0x481E0C,16);
  Put(0x70E38+56,reinterpret_cast<std::uintptr_t>(image)+0x1B120);Put(0x70E38+64,reinterpret_cast<std::uintptr_t>(image)+0x1B120);image[0x1B120]=0xC3;
  for(const auto off:{0x481D88,0x481DD0,0x481E10,0x481E28})Put32(off,63);
  for(const auto off:{0x481E48,0x481D90,0x481DD8})InitializeCriticalSection(image+off);
  std::size_t count=0;const auto* hooks=s::Catalog(&count);for(std::size_t i=0;i<count;++i)std::memcpy(image+hooks[i].rva,hooks[i].prefix,16);
  p::Initialize(&packs,origins,8);fence.owner_generation=1;fence.thread_id=GetCurrentThreadId();
  fence.producers_closed=fence.consumers_drained=fence.renderer_lock_owned=fence.worker_lock_owned=true;
  ready=s::Initialize(&owner,reinterpret_cast<std::uintptr_t>(image),0x180000000,s::kPlayerRendererSha256,Read,Fence,Pack,this,records,16,scratch);
  if(!ready)return;
  for(std::size_t i=0;i<count;++i){auto route=hooks[i].route;std::uintptr_t original=0;
   switch(route){case s::Route::Create:original=reinterpret_cast<std::uintptr_t>(&Create);break;
   case s::Route::Delete:original=reinterpret_cast<std::uintptr_t>(&Delete);break;
   case s::Route::Request:original=reinterpret_cast<std::uintptr_t>(&Request);break;
   case s::Route::FullRequest:original=reinterpret_cast<std::uintptr_t>(&FullRequest);break;
   case s::Route::Tick:original=reinterpret_cast<std::uintptr_t>(&Tick);break;
   case s::Route::Control:original=reinterpret_cast<std::uintptr_t>(&Control);break;
   default:original=reinterpret_cast<std::uintptr_t>(&Lifecycle);break;}
   if(!s::BindOriginal(&owner,route,original)){ready=false;return;}
   unsigned char jump[14]={0xff,0x25,0,0,0,0};const auto wrapper=s::Wrapper(route);std::memcpy(jump+6,&wrapper,8);std::memcpy(image+hooks[i].rva,jump,14);
  }
  ready=s::Publish(&owner)&&s::Seal(&owner);
 }
 ~Fixture(){s::Leave(&owner);if(image){for(const auto off:{0x481E48,0x481D90,0x481DD8})DeleteCriticalSection(image+off);VirtualFree(image,0,0x8000);}current=nullptr;}
 void Put(std::size_t off,std::uintptr_t v){std::memcpy(image+off,&v,8);}void Put32(std::size_t off,std::uint32_t v){std::memcpy(image+off,&v,4);}
 std::uint32_t Count(){std::uint32_t count=0;std::memcpy(&count,image+0x481E08,4);return count;}
 static bool Read(void* user,std::uintptr_t address,void* out,std::size_t bytes)noexcept{
  auto* f=static_cast<Fixture*>(user);if(address==0x180ACD0F8&&bytes==8){const auto ptr=reinterpret_cast<std::uintptr_t>(&f->api);std::memcpy(out,&ptr,8);return true;}
  if(address==0x180ACCDA8&&bytes==8){const std::uintptr_t ptr=0x7000;std::memcpy(out,&ptr,8);return true;}
  const struct{const void* data;std::size_t size;} ranges[]={{f->image,0x48B000},{&f->api,sizeof(f->api)},{f->vector,sizeof(f->vector)},
   {f->descriptors,sizeof(f->descriptors)},{f->textures,sizeof(f->textures)},{f->native_pack,sizeof(f->native_pack)},
   {f->stream_job,sizeof(f->stream_job)},{f->stream_operation,sizeof(f->stream_operation)}};
  for(const auto& range:ranges){const auto begin=reinterpret_cast<std::uintptr_t>(range.data);if(address>=begin&&address-begin<=range.size&&bytes<=range.size-(address-begin)){
   std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}}
  return false;
 }
 static bool Fence(void* user,p::Fence* out)noexcept{*out=static_cast<Fixture*>(user)->fence;return true;}
 static bool Pack(void* user,std::uintptr_t address,p::Key* out)noexcept{return p::FindActive(&static_cast<Fixture*>(user)->packs,address,out)==p::Result::Succeeded;}
 static std::uintptr_t __fastcall Create(void* texture,void* pack,void* entry,void*){
  auto& f=*current;auto* data=f.descriptors[f.used++];const auto p=reinterpret_cast<std::uintptr_t>(pack),e=reinterpret_cast<std::uintptr_t>(entry),t=reinterpret_cast<std::uintptr_t>(texture);
  std::memcpy(data+16,&p,8);std::memcpy(data+24,&e,8);std::memcpy(data+32,&t,8);
  f.vector[f.Count()]=reinterpret_cast<std::uintptr_t>(data);f.Put32(0x481E08,f.Count()+1);return reinterpret_cast<std::uintptr_t>(data);
 }
 static void __fastcall Delete(void* d){static_cast<unsigned char*>(d)[49]=1;}
 static std::uintptr_t __fastcall Request(void*,int,int){return 7;}
 static void __fastcall FullRequest(void*){}
 static void __fastcall Control(void*,std::uint32_t,void*){}
 static std::uintptr_t __fastcall Lifecycle(){return 0;}
 static void __fastcall Tick(std::uint32_t* out){auto& f=*current;*out=0;
  if(f.classify_stream){f.stream_owned=s::OwnsStreamRequest(&owner,0x7000,reinterpret_cast<std::uintptr_t>(f.stream_operation)+(f.wrong_operation?4:0),
    reinterpret_cast<std::uintptr_t>(f.native_pack),f.stream_entry,reinterpret_cast<std::uintptr_t>(f.textures[7]),
    reinterpret_cast<std::uintptr_t>(f.image)+0x18520,reinterpret_cast<std::uintptr_t>(f.stream_job));return;}
  if(f.hold_tick.load()){f.tick_entered.store(true);while(!f.resume_tick.load())Sleep(1);}
  if(f.skip_collect)return;
  std::uint32_t count=f.Count();for(std::uint32_t i=count;i;--i)if(reinterpret_cast<unsigned char*>(f.vector[i-1])[49]){
   for(std::uint32_t j=i;j<count;++j)f.vector[j-1]=f.vector[j];--count;++f.deleted;}f.Put32(0x481E08,count);
 }
 p::Key AddPack(bool project){p::Key key={};p::Constructed(&packs,0x5000+packs.count*0x100,&key);p::Registered(&packs,key);
  r::Input input={};input.file=0xA000;input.file_generation=1;input.source=project?0xB000:0xC000;input.source_generation=1;
  input.content_sha256[0]=17;input.content_bytes=1024;input.realm=project?r::Realm::Project:r::Realm::Global;input.epoch=project?2:0;
  p::RetainInput(&packs,key,input);p::Loaded(&packs,key);return key;}
 std::uintptr_t Add(p::Key key){const auto index=used;const auto texture=reinterpret_cast<std::uintptr_t>(textures[index]);
  const auto descriptor=reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,void*,void*)>(s::Wrapper(s::Route::Create))(
   reinterpret_cast<void*>(texture),reinterpret_cast<void*>(key.address),reinterpret_cast<void*>(key.address+0x80),nullptr);
  std::memcpy(textures[index]+88,&descriptor,8);return descriptor;}
 bool Baseline(){if(!ready||!s::Enter(&owner,20)||!s::CaptureBaseline(&owner,packs,1))return false;s::Leave(&owner);
  fence.transaction=9;fence.epoch=2;fence.authoritative_post_destroy=true;return true;}
 bool Qualify(p::Key key,std::uintptr_t descriptor){const auto* d=reinterpret_cast<unsigned char*>(descriptor);std::uintptr_t backend=0;std::memcpy(&backend,d+32,8);
  p::NativePack pack={};pack.object=key.address;r::NativeState texture={};texture.backend=backend;texture.deferred=key.address;
  return s::QualifyPack(&owner,packs,pack,&texture,1);}
};
bool NativeDeletionAndCollection(){Fixture f;CHECK(f.ready);auto stock=f.AddPack(false);f.Add(stock);CHECK(f.Baseline());auto project=f.AddPack(true);auto desc=f.Add(project);
 CHECK(s::Enter(&owner,20));CHECK(f.Qualify(project,desc));CHECK(s::PermitNativeTextureDeletion(&owner,true));
 reinterpret_cast<void(__fastcall*)(void*)>(s::Wrapper(s::Route::Delete))(reinterpret_cast<void*>(desc));CHECK(s::PermitNativeTextureDeletion(&owner,false));
 CHECK(!s::CanRetireSource(&owner,{0xB000,1},9));CHECK(f.Count()==2);CHECK(s::CollectDeleted(&owner));CHECK(f.Count()==1&&f.deleted==1);
 CHECK(s::CanRetireSource(&owner,{0xB000,1},9));CHECK(s::ReclaimSource(&owner,{0xB000,1},9));return true;}
bool PendingIoBlocksDeletion(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);auto desc=f.Add(key);f.Put32(0x481D80,1);f.Put(0x481D70,0xDEAD);
 CHECK(s::Enter(&owner,20));CHECK(!f.Qualify(key,desc));CHECK(!s::PermitNativeTextureDeletion(&owner,true));return true;}
bool DeletedFlagAloneIsNotProof(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);auto desc=f.Add(key);reinterpret_cast<unsigned char*>(desc)[49]=1;
 CHECK(s::Enter(&owner,20));CHECK(!f.Qualify(key,desc));CHECK(owner.lost.load());return true;}
bool WrongPackGeneration(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);auto desc=f.Add(key);++f.origins[key.slot].key.generation;
 CHECK(s::Enter(&owner,20));CHECK(!f.Qualify(f.origins[key.slot].key,desc));return true;}
bool NativeCollectionMustRemove(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);auto desc=f.Add(key);CHECK(s::Enter(&owner,20));CHECK(f.Qualify(key,desc));
 CHECK(s::PermitNativeTextureDeletion(&owner,true));reinterpret_cast<void(__fastcall*)(void*)>(s::Wrapper(s::Route::Delete))(reinterpret_cast<void*>(desc));
 CHECK(s::PermitNativeTextureDeletion(&owner,false));f.skip_collect=true;CHECK(!s::CollectDeleted(&owner));CHECK(!s::CanRetireSource(&owner,{0xB000,1},9));return true;}
bool UnknownDescriptorBlocks(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);Fixture::Create(f.textures[0],reinterpret_cast<void*>(key.address),reinterpret_cast<void*>(key.address+0x80),nullptr);
 CHECK(s::Enter(&owner,20));s::Snapshot snapshot={};s::NativeDescriptor entries[16]={};snapshot.descriptors=entries;snapshot.capacity=16;
 CHECK(!s::Capture(&owner,&snapshot)&&owner.lost.load());return true;}
bool NewRequesterInvalidatesGate(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);auto desc=f.Add(key);CHECK(s::Enter(&owner,20));std::atomic<bool> done{false};
 std::thread producer([&]{reinterpret_cast<std::uintptr_t(__fastcall*)(void*,int,int)>(s::Wrapper(s::Route::Request))(reinterpret_cast<void*>(desc),1,2);done.store(true);});
 for(unsigned i=0;i<1000&&!owner.pending.load();++i)Sleep(1);const bool pending=owner.pending.load()!=0,blocked=!done.load();
 const bool denied=!f.Qualify(key,desc);s::Leave(&owner);producer.join();CHECK(pending&&blocked&&denied&&done.load());return true;}
bool ManagerReinitializationInvalidates(){Fixture f;CHECK(f.Baseline());reinterpret_cast<std::uintptr_t(__fastcall*)()>(s::Wrapper(s::Route::Initialize))();CHECK(owner.lost.load());return true;}
void MakeChainedUnwind(Fixture& f){
 f.Put32(0x3C,0x100);f.image[0x118]=0x0B;f.image[0x119]=2;f.Put32(0x100+24+112+24,0x900);f.Put32(0x100+24+112+28,36);
 const std::uint32_t entries[]={0x153E0,0x153EB,0x1000,0x153EB,0x153EF,0x1020,0x153EF,0x159A8,0x1040};
 std::memcpy(f.image+0x900,entries,sizeof(entries));
 const unsigned char primary[]={1,11,1,0,11,0xA2,0,0};std::memcpy(f.image+0x1000,primary,sizeof(primary));
 const unsigned char second[]={0x21,4,2,0,4,0x34,15,0};std::memcpy(f.image+0x1020,second,sizeof(second));
 std::memcpy(f.image+0x1028,entries,12);
 const unsigned char third[]={0x21,8,4,0,8,0x64,9,0,4,0x54,10,0};std::memcpy(f.image+0x1040,third,sizeof(third));
 std::memcpy(f.image+0x104C,entries+3,12);
}
bool RelayUnwindRecoversPartialPrologue(){Fixture f;CHECK(f.ready);MakeChainedUnwind(f);unsigned char codes[64]={};std::size_t bytes=0;
 CHECK(s::CopyEntryUnwind(&owner,s::Route::Tick,codes,sizeof(codes),&bytes));CHECK(CheckRendererRelayUnwind(codes,bytes));return true;}
bool RelayUnwindRejectsDifferentParent(){Fixture f;CHECK(f.ready);MakeChainedUnwind(f);f.Put32(0x104C,0x153E0);unsigned char codes[64]={};std::size_t bytes=0;
 CHECK(!s::CopyEntryUnwind(&owner,s::Route::Tick,codes,sizeof(codes),&bytes)&&!bytes);return true;}
bool NativeApiLockMustBeNoOp(){Fixture f;CHECK(f.ready);f.image[0x1B120]=0x90;CHECK(!s::Enter(&owner,20));CHECK(!owner.owner_thread.load());return true;}
bool ObserverNeverWaitsAcrossNativeCalls(){Fixture f;CHECK(f.Baseline());auto key=f.AddPack(true);f.hold_tick.store(true);
 std::thread tick([]{std::uint32_t bytes=0;reinterpret_cast<void(__fastcall*)(std::uint32_t*)>(s::Wrapper(s::Route::Tick))(&bytes);});
 for(unsigned i=0;i<1000&&!f.tick_entered.load();++i)Sleep(1);
 std::atomic<bool> returned{false};std::thread creator([&]{f.Add(key);returned.store(true);});
 for(unsigned i=0;i<200&&!returned.load();++i)Sleep(1);const bool before_release=returned.load();f.resume_tick.store(true);creator.join();tick.join();
 CHECK(before_release&&owner.lost.load()&&f.used==1);return true;}
bool ConcurrentRequestsKeepNativeConcurrency(){Fixture f;CHECK(f.Baseline());f.hold_tick.store(true);
 std::thread tick([]{std::uint32_t bytes=0;reinterpret_cast<void(__fastcall*)(std::uint32_t*)>(s::Wrapper(s::Route::Tick))(&bytes);});
 for(unsigned i=0;i<1000&&!f.tick_entered.load();++i)Sleep(1);
 std::atomic<bool> returned{false};std::thread request([&]{reinterpret_cast<std::uintptr_t(__fastcall*)(void*,int,int)>(s::Wrapper(s::Route::Request))(nullptr,1,1);returned.store(true);});
 for(unsigned i=0;i<200&&!returned.load();++i)Sleep(1);const bool before_release=returned.load();f.resume_tick.store(true);request.join();tick.join();
 CHECK(before_release&&!owner.lost.load());return true;}
bool StreamRequestCorrelatesActualQueue(){Fixture f;CHECK(f.Baseline());p::Key key={};
 CHECK(p::Constructed(&f.packs,reinterpret_cast<std::uintptr_t>(f.native_pack),&key)==p::Result::Succeeded);const auto descriptor=f.Add(key);
 const auto job=reinterpret_cast<std::uintptr_t>(f.stream_job),operation=reinterpret_cast<std::uintptr_t>(f.stream_operation),buffer=reinterpret_cast<std::uintptr_t>(f.textures[7]);
 const auto queue=reinterpret_cast<std::uintptr_t>(f.image)+0x481D70;f.stream_entry=key.address+0x80;
 std::memcpy(f.stream_job,&queue,8);std::memcpy(f.stream_job+8,&descriptor,8);std::memcpy(f.stream_job+16,&operation,8);std::memcpy(f.stream_job+32,&buffer,8);
 std::memcpy(reinterpret_cast<void*>(descriptor),&job,8);f.Put(0x481D70,job+64);f.Put(0x481D78,job+64);f.Put32(0x481D80,1);
 CHECK(!s::OwnsStreamRequest(&owner,0x7000,operation,key.address,f.stream_entry,buffer,reinterpret_cast<std::uintptr_t>(f.image)+0x18520,job));
 f.classify_stream=true;std::uint32_t bytes=0;auto tick=reinterpret_cast<void(__fastcall*)(std::uint32_t*)>(s::Wrapper(s::Route::Tick));
 tick(&bytes);CHECK(f.stream_owned);f.wrong_operation=true;tick(&bytes);CHECK(!f.stream_owned);
 f.wrong_operation=false;f.native_pack[32]=0x80;tick(&bytes);CHECK(!f.stream_owned);return true;}
int main(){const struct{const char* name;bool(*run)();} tests[]={{"native delete then GC",NativeDeletionAndCollection},{"pending IO",PendingIoBlocksDeletion},
 {"delete flag without completed release",DeletedFlagAloneIsNotProof},{"native pack generation",WrongPackGeneration},{"native collector removal",NativeCollectionMustRemove},
 {"unobserved descriptor",UnknownDescriptorBlocks},{"pending requester",NewRequesterInvalidatesGate},{"manager reinitialization",ManagerReinitializationInvalidates},
 {"OS relay unwind",RelayUnwindRecoversPartialPrologue},{"unwind chain identity",RelayUnwindRejectsDifferentParent},{"actual API lock ABI",NativeApiLockMustBeNoOp},
 {"observers never wait across native calls",ObserverNeverWaitsAcrossNativeCalls},{"native request concurrency preserved",ConcurrentRequestsKeepNativeConcurrency},
 {"actual native streaming queue correlation",StreamRequestCorrelatesActualQueue}};
 for(const auto& test:tests)if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}std::printf("PASS %zu renderer streaming scenarios\n",sizeof(tests)/sizeof(tests[0]));return 0;}
