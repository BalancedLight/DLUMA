#include "fixes/custom_map_asset_isolation/MaterialBaselineOwner.h"
#include <cstdio>
#include <cstring>
extern "C" __declspec(dllimport) void WINAPI RaiseException(DWORD,DWORD,DWORD,const ULONG_PTR*);
extern "C" unsigned long __cdecl _exception_code(void);
namespace m=dluma::fixes::custom_map_asset_isolation::material_baseline;
namespace g=dluma::fixes::custom_map_asset_isolation::resource_gateway;
namespace p=dluma::fixes::custom_map_asset_isolation::packed;
namespace r=dluma::fixes::custom_map_asset_isolation::retained;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::bridge {
bool FileIdentity(State*,std::uintptr_t,FileRecord*)noexcept{return false;}
bool SourceIdentity(State*,std::uintptr_t,SourceKey*)noexcept{return false;}
}
#define CHECK(x)do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
struct Fixture;
static Fixture* active;
struct Fixture {
 g::State gateway;m::State state;
 r::Record resource_records[8]={};r::Ledger resources={};p::Origin origins[8]={};p::Ledger packs={};
 unsigned char container[128]={},manager[176]={},wrapper[16]={},record[80]={},definition[24]={},variant[24]={},node[48]={};
 std::uintptr_t registered[3]={0x6000,0x7000};char path[32]="baseline.mp",name[32]="menu_surface.mat";
 unsigned char payload[4]={1,2,3,4};m::Wrapper baseline_wrappers[8]={},scratch_wrappers[8]={};
 m::Snapshot snapshot={};p::Fence fence={};std::uint32_t epoch=0;std::uint64_t file_generation=1,source_generation=1;
 unsigned native_calls=0,replay_exits=0;bool permit=true,short_read=false,omit_provider=false,project=false,raise_reload=false,reorder_reload=false;
 Fixture(){
  active=this;r::Initialize(&resources,resource_records,8);p::Initialize(&packs,origins,8);
  gateway.initialized=true;gateway.packs=&packs;gateway.textures=&resources;gateway.callbacks.user=this;
  gateway.callbacks.read=Read;gateway.callbacks.context=Context;gateway.callbacks.potential=Potential;
  state.gateway=&gateway;state.engine_base=0x180000000;state.read=Read;state.fence=Fence;state.user=this;
  state.baseline.wrappers=baseline_wrappers;state.baseline.capacity=8;
  snapshot.wrappers=scratch_wrappers;snapshot.capacity=8;
  fence.owner_generation=1;fence.thread_id=GetCurrentThreadId();fence.mutation_revision=1;
  fence.producers_closed=fence.consumers_drained=fence.renderer_lock_owned=fence.worker_lock_owned=true;
  Put(manager,0,state.engine_base+0x94AC48);Put(manager,0x50,reinterpret_cast<std::uintptr_t>(registered));Put32(manager,0x58,2);
  Put(manager,0x68,0x6000);Put(manager,0x70,0x7000);Put(manager,0x78,reinterpret_cast<std::uintptr_t>(node+16));Put32(manager,0x80,1);
  Put32(node,0,77);Put(node,8,reinterpret_cast<std::uintptr_t>(wrapper));Put(wrapper,0,state.engine_base+0x94AB98);Put(wrapper,8,reinterpret_cast<std::uintptr_t>(record));
  Put(record,0,reinterpret_cast<std::uintptr_t>(definition));Put(definition,0,reinterpret_cast<std::uintptr_t>(name));
  Put(record,8,reinterpret_cast<std::uintptr_t>(variant));Put(record,16,reinterpret_cast<std::uintptr_t>(wrapper));Put(record,24,0x7000);
  Put32(record,32,5);Put32(record,36,77);Put32(record,40,1);Put(record,56,reinterpret_cast<std::uintptr_t>(name));
  Put32(container,8,1);Put32(container,24,0x12345678);Put32(container,28,1);
 }
 static void Put(unsigned char* p,std::size_t offset,std::uintptr_t value){std::memcpy(p+offset,&value,8);}
 static void Put32(unsigned char* p,std::size_t offset,std::uint32_t value){std::memcpy(p+offset,&value,4);}
 static bool Read(void*,std::uintptr_t address,void* out,std::size_t bytes)noexcept{
  if(!address||!out)return false;std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;
 }
 static bool Context(void* user,g::Context* out)noexcept{auto* f=static_cast<Fixture*>(user);std::memset(out,0,sizeof(*out));out->epoch=f->epoch;out->clean_startup=!f->epoch;return true;}
 static void Potential(void*,g::Route)noexcept{}
 static bool Fence(void* user,p::Fence* out)noexcept{*out=static_cast<Fixture*>(user)->fence;return true;}
 void Event(g::Route route,g::EventPhase phase,std::uintptr_t owner,std::uintptr_t a0=0,std::uintptr_t a1=0,std::uintptr_t a2=0,std::uintptr_t a3=0,std::uintptr_t result=0){
  m::Observe(&state,{route,phase,owner,{a0,a1,a2,a3,0},result});
 }
 m::Load* Load(std::uintptr_t pack){
  g::Call load={};g::Begin(&gateway,g::Route::MaterialPackLoad,pack,&load);
  Event(g::Route::MaterialPackLoad,g::EventPhase::Enter,pack,reinterpret_cast<std::uintptr_t>(path),0);
  g::Call open={};g::Begin(&gateway,g::Route::ContainerOpen,reinterpret_cast<std::uintptr_t>(container),&open);
  Event(g::Route::ContainerOpen,g::EventPhase::Enter,reinterpret_cast<std::uintptr_t>(container),reinterpret_cast<std::uintptr_t>(path),1,0x12345678);
  if(!omit_provider){f::FileRecord file={};file.file=0x9000;file.generation=file_generation++;file.life=f::Life::Live;file.kind=f::FileKind::Win32;
   (void)m::ObserveFile(&state,pack,file,{0xA000,source_generation},project?r::Realm::Project:r::Realm::Global,project?2:0);}
  Event(g::Route::MaterialFileRead,g::EventPhase::Returned,reinterpret_cast<std::uintptr_t>(container+40),0,16,0,0,short_read?8:16);
  Event(g::Route::ContainerOpen,g::EventPhase::Returned,reinterpret_cast<std::uintptr_t>(container),0,0,0,0,1);open.returned=true;g::End(&gateway,&open);
  void* data=payload;unsigned size=4;
  Event(g::Route::ContainerFetch,g::EventPhase::Returned,reinterpret_cast<std::uintptr_t>(container),0,45,reinterpret_cast<std::uintptr_t>(&data),reinterpret_cast<std::uintptr_t>(&size),1);
  Event(g::Route::MaterialPackLoad,g::EventPhase::Returned,pack,0,0,0,0,1);load.returned=true;g::End(&gateway,&load);
  for(auto& value:state.loads)if(value.pack==pack&&!value.retired)return &value;return nullptr;
 }
 bool Baseline(){
  if(!Load(0x7000)->complete)return false;
  if(m::Capture(&state,reinterpret_cast<std::uintptr_t>(manager),&snapshot)!=m::Result::Succeeded)return false;
  if(m::CaptureBaseline(&state,snapshot,1)!=m::Result::Succeeded)return false;
  epoch=2;fence.epoch=2;fence.transaction=4;fence.authoritative_post_destroy=true;
  registered[1]=0x7100;Put(manager,0x70,0x7100);Put(record,24,0x7100);return true;
 }
 static bool Replay(void* user,const m::Baseline&,bool entering)noexcept{
  auto* f=static_cast<Fixture*>(user);if(!entering)++f->replay_exits;return !entering||f->permit;}
 static unsigned __fastcall Reload(void*){auto& f=*active;++f.native_calls;
  if(f.raise_reload)RaiseException(0xE0424242,0,0,nullptr);
  f.registered[1]=0x7200;Put(f.manager,0x70,0x7200);Put(f.record,24,0x7200);f.Load(0x7200);
  if(f.reorder_reload){f.registered[0]=0x7200;f.registered[1]=0x6000;}return 1;}
 m::Result Restore(){return m::Restore(&state,&snapshot,{this,Replay,Reload});}
};
bool EquivalentInput(){Fixture f;auto* a=f.Load(0x5000);auto* b=f.Load(0x5100);CHECK(a&&b&&m::SameInput(*a,*b));CHECK(a->inputs[0].opened_file.generation!=b->inputs[0].opened_file.generation);return true;}
bool DifferentBytes(){Fixture f;auto* a=f.Load(0x5000);f.payload[0]=9;auto* b=f.Load(0x5100);CHECK(!m::SameInput(*a,*b));return true;}
bool DifferentProvider(){Fixture f;auto* a=f.Load(0x5000);++f.source_generation;auto* b=f.Load(0x5100);CHECK(!m::SameInput(*a,*b));return true;}
bool ShortRead(){Fixture f;f.short_read=true;CHECK(!f.Load(0x5000)->complete);return true;}
bool MissingProvider(){Fixture f;f.omit_provider=true;CHECK(!f.Load(0x5000)->complete);return true;}
bool ProjectNotBaseline(){Fixture f;f.project=true;auto* a=f.Load(0x5000);f.project=false;auto* b=f.Load(0x5100);CHECK(!m::SameInput(*a,*b));return true;}
bool NativeRestore(){Fixture f;CHECK(f.Baseline());CHECK(f.Restore()==m::Result::Succeeded);CHECK(f.native_calls==1);CHECK(f.snapshot.local_pack==0x7200);return true;}
bool NativeWrongInput(){Fixture f;CHECK(f.Baseline());f.payload[0]=9;CHECK(f.Restore()==m::Result::VerificationFailed);return true;}
bool ReplayRequired(){Fixture f;CHECK(f.Baseline());f.permit=false;CHECK(f.Restore()==m::Result::ReplayUnavailable);CHECK(!f.native_calls);return true;}
bool UnknownBorrower(){Fixture f;CHECK(f.Baseline());std::strcpy(f.name,"project_surface.mat");CHECK(f.Restore()==m::Result::UnclosedConsumer);CHECK(!f.native_calls);return true;}
bool ExtraPack(){Fixture f;CHECK(f.Baseline());Fixture::Put32(f.manager,0x58,3);f.registered[2]=0xDEAD;CHECK(f.Restore()==m::Result::BaselineChanged);return true;}
bool ActualReleasedBaselineWrapper(){Fixture f;CHECK(f.Baseline());void* slot=f.wrapper;const auto manager=reinterpret_cast<std::uintptr_t>(f.manager),object=reinterpret_cast<std::uintptr_t>(f.wrapper);
 f.Event(g::Route::MaterialRelease,g::EventPhase::Enter,manager,reinterpret_cast<std::uintptr_t>(&slot),object);
 f.Event(g::Route::MaterialReleaseRef,g::EventPhase::Returned,object,0,0,0,0,1);slot=nullptr;
 f.Event(g::Route::MaterialRelease,g::EventPhase::Returned,manager,reinterpret_cast<std::uintptr_t>(&slot),object);
 CHECK(f.state.baseline_wrapper_released[0]);Fixture::Put(f.manager,0x78,0);Fixture::Put32(f.manager,0x80,0);
 CHECK(f.Restore()==m::Result::Succeeded);return true;}
bool RefZeroAloneNotDestruction(){Fixture f;CHECK(f.Baseline());f.Event(g::Route::MaterialReleaseRef,g::EventPhase::Returned,reinterpret_cast<std::uintptr_t>(f.wrapper),0,0,0,0,1);
 CHECK(!f.state.baseline_wrapper_released[0]);return true;}
bool DuplicatePack(){Fixture f;CHECK(f.Baseline());f.registered[0]=f.registered[1];CHECK(f.Restore()==m::Result::Incomplete);CHECK(!f.native_calls);return true;}
bool NativeReorderedPacks(){Fixture f;CHECK(f.Baseline());f.reorder_reload=true;CHECK(f.Restore()==m::Result::VerificationFailed);return true;}
bool PreflightReorderedGlobalPacks(){Fixture f;CHECK(f.Baseline());
 f.state.baseline.pack_count=3;f.state.baseline.packs[2]=0x8000;
 f.snapshot.pack_count=3;f.snapshot.packs[0]=0x8000;f.snapshot.packs[2]=0x6000;
 CHECK(m::Preflight(&f.state,f.snapshot)==m::Result::BaselineChanged);return true;}
bool CallReloadException(Fixture* fixture){
 __try{(void)fixture->Restore();}
 __except(_exception_code()==0xE0424242?1:0){return true;}
 return false;
}
bool ReplayPermitUnwind(){Fixture f;CHECK(f.Baseline());f.raise_reload=true;CHECK(CallReloadException(&f));CHECK(f.replay_exits==1);return true;}
bool AbsentBaseline(Fixture& f){
 Fixture::Put32(f.manager,0x58,1);Fixture::Put(f.manager,0x70,0);Fixture::Put(f.record,24,0x6000);
 if(m::Capture(&f.state,reinterpret_cast<std::uintptr_t>(f.manager),&f.snapshot)!=m::Result::Succeeded||
    m::CaptureBaseline(&f.state,f.snapshot,1)!=m::Result::Succeeded)return false;
 f.epoch=2;f.fence.epoch=2;f.fence.transaction=4;f.fence.authoritative_post_destroy=true;f.project=true;
 Fixture::Put32(f.manager,0x58,2);Fixture::Put(f.manager,0x70,0x7100);f.registered[1]=0x7100;
 return f.Load(0x7100)->complete;
}
bool UnloadLocal(void* user,std::uintptr_t manager,std::uintptr_t pack,std::uint64_t generation)noexcept{
 auto& f=*static_cast<Fixture*>(user);
 if(m::Capture(&f.state,manager,&f.snapshot)!=m::Result::Succeeded||!m::MatchUnborrowedLocal(&f.state,f.snapshot,pack,generation))return false;
 ++f.native_calls;Fixture::Put32(f.manager,0x58,1);Fixture::Put(f.manager,0x70,0);
 if(!f.short_read)f.Event(g::Route::MaterialPackDestroy,g::EventPhase::Returned,pack);
 return true;
}
bool AbsentLocalSplit(){Fixture f;CHECK(AbsentBaseline(f));CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::Succeeded);
 CHECK(f.native_calls==1&&!f.replay_exits);CHECK(m::VerifyAbsentLocal(&f.state,f.snapshot)==m::Result::Succeeded);
 CHECK(m::CanRetireSource(&f.state,{0xA000,1},4));CHECK(!m::CanRetireSource(&f.state,{0xA000,1},5));return true;}
bool AbsentLocalBorrowerBlocks(){Fixture f;CHECK(AbsentBaseline(f));Fixture::Put(f.record,24,0x7100);
 CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::UnclosedConsumer);CHECK(!f.native_calls);return true;}
bool AbsentLocalNeedsNativeDestruction(){Fixture f;CHECK(AbsentBaseline(f));f.short_read=true;
 CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::VerificationFailed);CHECK(!m::CanRetireSource(&f.state,{0xA000,1},4));return true;}
bool AbsentLocalRejectsOtherNativePack(){Fixture f;CHECK(AbsentBaseline(f));CHECK(f.Load(0x7200)->complete);
 CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::UnknownInput);CHECK(!f.native_calls);return true;}
bool LocalBaselineNeedsSeparateReplay(){Fixture f;CHECK(f.Baseline());CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::BaselineUnavailable);
 CHECK(!f.native_calls);return true;}
bool GlobalPackReloadInvalidatesBaseline(){Fixture f;CHECK(f.Baseline());f.Load(0x6000);CHECK(f.state.global_pack_mutated.load());CHECK(f.Restore()==m::Result::BaselineChanged);return true;}
bool AbsentLocalRepeatedSourceRetirement(){Fixture f;CHECK(AbsentBaseline(f));
 for(unsigned cycle=0;cycle<80;++cycle){
  if(cycle){++f.epoch;++f.source_generation;f.fence.epoch=f.epoch;++f.fence.transaction;
   Fixture::Put32(f.manager,0x58,2);Fixture::Put(f.manager,0x70,0x7100);f.registered[1]=0x7100;CHECK(f.Load(0x7100)->complete);}
  CHECK(m::ResetAbsentLocal(&f.state,&f.snapshot,UnloadLocal,&f)==m::Result::Succeeded);
  CHECK(m::ReclaimRetiredSource(&f.state,{0xA000,f.source_generation},f.fence.transaction));
 }CHECK(f.native_calls==80&&!f.state.lost.load());return true;}
int main(){struct Test{const char* name;bool(*run)();};const Test tests[]={{"semantic input identity",EquivalentInput},{"changed decoded bytes",DifferentBytes},
 {"provider generation",DifferentProvider},{"short native read",ShortRead},{"missing opened provider",MissingProvider},{"project cannot seed B0",ProjectNotBaseline},
 {"native baseline replay",NativeRestore},{"native replay byte mismatch",NativeWrongInput},{"authenticated replay required",ReplayRequired},
 {"unclosed project borrower",UnknownBorrower},{"extra registered pack",ExtraPack},{"completed UI wrapper release",ActualReleasedBaselineWrapper},
 {"zero ref result is not completed destruction",RefZeroAloneNotDestruction},{"duplicate native pack",DuplicatePack},
 {"native replay preserves pack order",NativeReorderedPacks},{"preflight preserves global pack order",PreflightReorderedGlobalPacks},
 {"native exception releases replay permit",ReplayPermitUnwind},{"absent B0 native release split",AbsentLocalSplit},
 {"absent B0 live local borrower",AbsentLocalBorrowerBlocks},{"split requires completed native destructor",AbsentLocalNeedsNativeDestruction},
 {"unregistered retained material pack",AbsentLocalRejectsOtherNativePack},{"B0 local needs later replay",LocalBaselineNeedsSeparateReplay},
 {"all nonlocal baseline packs remain immutable",GlobalPackReloadInvalidatesBaseline},{"eighty material source retirements",AbsentLocalRepeatedSourceRetirement}};
 for(const auto& test:tests)if(!test.run()){std::printf("FAILED %s\n",test.name);return 1;}
 std::printf("PASS %zu material-owner scenarios\n",sizeof(tests)/sizeof(tests[0]));return 0;}
