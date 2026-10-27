#define _HAS_EXCEPTIONS 0
#include "NativeResourceGateway.h"
#include "CurveBorrowBoundary.h"
#include <cstring>
#include <intrin.h>
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();

namespace dluma::fixes::custom_map_asset_isolation::resource_gateway {
namespace {
constexpr Descriptor descriptors[]={
 {Route::PackConstruct,"PackConstruct",0x45f150,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0xc7},16,22,0,0,false},
 {Route::PackOpen,"PackOpen",0x460050,0,0,{0x44,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55},16,16,0,0,false},
 {Route::PackLoad,"PackLoad",0x46c860,0,0,{0x44,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55},16,16,0,0,false},
 {Route::PackRegister,"PackRegister",0x46c510,0,0,{0x48,0x8b,0xc4,0x55,0x56,0x41,0x54,0x41,0x56,0x48,0x8d,0x68,0xd8,0x48,0x81,0xec},16,20,0,0,false},
 {Route::PackUnregister,"PackUnregister",0x46c770,0,0,{0x48,0x85,0xd2,0x0f,0x84,0xe2,0x00,0x00,0x00,0x4c,0x89,0x44,0x24,0x18,0x48,0x89},16,19,5,9,false},
 {Route::PackDestroy,"PackDestroy",0x45f260,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48},16,22,0,0,false},
 {Route::AssetLoad,"AssetLoad",0x452860,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x83},16,19,0,0,false},
 {Route::AssetRelease,"AssetRelease",0x4507d0,0,0,{0x48,0x83,0xec,0x28,0x48,0x8b,0x0d,0xcd,0xc5,0x67,0x00,0xc7,0x44,0x24,0x38,0x00},16,19,7,11,false},
 {Route::PackRelease,"PackRelease",0x46cf60,0,0,{0x48,0x85,0xd2,0x0f,0x84,0xc9,0x00,0x00,0x00,0x48,0x89,0x74,0x24,0x10,0x57,0x48},16,19,5,9,true},
 {Route::TexturePackRegister,"TexturePackRegister",0x472c40,0,0,{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x53},16,16,0,0,false},
 {Route::TextureConstruct,"TextureConstruct",0x7ab740,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33},16,17,0,0,false},
 {Route::TextureDestroy,"TextureDestroy",0x7aceb0,0,0,{0x48,0x85,0xd2,0x0f,0x84,0xb5,0x00,0x00,0x00,0x48,0x89,0x5c,0x24,0x10,0x56,0x48},16,19,5,9,false},
 {Route::TextureLoad,"TextureLoad",0x7ab1a0,0,0,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x54,0x41,0x55},16,16,0,0,false},
 {Route::TextureDynamic,"TextureDynamic",0x7ac900,0,0,{0x44,0x89,0x4c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x54},16,16,0,0,false},
 {Route::TextureRegister,"TextureRegister",0x7b0030,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x89},16,19,0,0,false},
 {Route::TextureImage,"TextureImage",0x7abc00,0,0,{0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57},16,16,0,0,false},
 {Route::TextureCallback,"TextureCallback",0x7ab900,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},16,16,0,0,false},
 {Route::MaterialPackLoad,"MaterialPackLoad",0x82be30,0,0,{0x40,0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24},16,20,0,0,true},
 {Route::MaterialReload,"MaterialReload",0x806cc0,0,0,{0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41},16,17,0,0,true},
 {Route::MaterialPackDestroy,"MaterialPackDestroy",0x82bc10,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},16,20,0,0,true},
 {Route::MaterialAcquire,"MaterialAcquire",0x805560,0,0,{0x48,0x89,0x5c,0x24,0x08,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41},16,17,0,0,false},
 {Route::MaterialRebind,"MaterialRebind",0x8077c0,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57},16,16,0,0,true},
 {Route::DemandSchedule,"DemandSchedule",0x4712e0,0,0,{0x40,0x55,0x56,0x57,0x41,0x55,0x48,0x83,0xec,0x38,0x41,0x8b,0xf1,0x4d,0x8b,0xe8},16,16,0,0,false},
 {Route::DemandUnload,"DemandUnload",0x4715f0,0,0,{0x48,0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x20,0x8b,0x42,0x4c,0x48,0x8b,0xd9},16,16,0,0,false},
 {Route::StreamResource,"StreamResource",0x474080,0,0,{0x40,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x78,0x4d,0x8b,0xe9,0x4d},16,18,0,0,false},
 {Route::DemandEvict,"DemandEvict",0x471fa0,0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},16,20,0,0,true},
 {Route::ContainerOpen,"MaterialContainerOpen",0,0xe630,0x8574d8,{},0,0,0,0,false},
 {Route::ContainerFetch,"MaterialContainerFetch",0,0x10440,0x8574e0,{},0,0,0,0,false},
 {Route::ContainerLoadData,"MaterialContainerLoadData",0,0x10aa0,0x857510,{},0,0,0,0,false},
 {Route::ContainerDataSize,"MaterialContainerDataSize",0,0x109f0,0x857508,{},0,0,0,0,false},
 {Route::FullPath,"ResourceFullPath",0,0x14d80,0x857288,{},0,0,0,0,false},
 {Route::MaterialFileRead,"MaterialFileRead",0,0xe0b0,0x3e828,{},0,0,0,0,false,true},
 {Route::MaterialRelease,"MaterialRelease",0x8058e0,0,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xc7,0x44,0x24},16,21,0,0,false},
 {Route::MaterialAddRef,"MaterialAddRef",0x8169d0,0,0x94ac10,{},0,0,0,0,false},
 {Route::MaterialReleaseRef,"MaterialReleaseRef",0x8169e0,0,0x94ac18,{},0,0,0,0,false},
 {Route::TextureRelease,"TextureRelease",0x7ad080,0,0,{0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xf1},16,16,0,0,false},
};
static_assert(sizeof(descriptors)/sizeof(descriptors[0])==kRoutes,"complete resource gateway routes");
std::atomic<State*> bridge{nullptr};
struct Frame {Call* call;std::uintptr_t owner;resource_provider::Receipt selection;};
struct Operation {State* state;packed::windows::NativeOperation kind;packed::Key key;bool active;};
struct ThreadState {State* state;Frame frames[64];std::uint32_t depth;Operation operation;};
__declspec(thread) ThreadState tls={};
std::size_t Index(Route route)noexcept{return static_cast<std::size_t>(route);}
void Lock(State* s)noexcept{while(s->records_lock.test_and_set(std::memory_order_acquire)){}}
void Unlock(State* s)noexcept{s->records_lock.clear(std::memory_order_release);}
void Lost(State* s)noexcept{if(s){s->lost.store(true);s->revision.fetch_add(1);}}
bool Read(State* s,std::uintptr_t p,void* out,std::size_t bytes)noexcept{
 return s&&p&&bytes&&bytes-1<=UINTPTR_MAX-p&&s->callbacks.read(s->callbacks.user,p,out,bytes);
}
bool Sha(const char* a,const char* b)noexcept{
 if(!a||!b)return false;for(unsigned i=0;i<64;++i){char x=a[i],y=b[i];if(!x||!y)return false;
  if(x>='A'&&x<='F')x=static_cast<char>(x+32);if(y>='A'&&y<='F')y=static_cast<char>(y+32);if(x!=y)return false;}return !a[64]&&!b[64];
}
bool Retaining(Route route)noexcept{
 switch(route){case Route::PackDestroy:case Route::PackUnregister:case Route::AssetRelease:
 case Route::PackRelease:case Route::TextureDestroy:case Route::MaterialPackDestroy:
 case Route::DemandUnload:case Route::DemandEvict:case Route::MaterialRelease:case Route::MaterialReleaseRef:
 case Route::TextureRelease:return false;default:return true;}
}
bool PackReceiver(Route route)noexcept{
 return route==Route::PackOpen||route==Route::PackRegister||route==Route::PackUnregister||route==Route::PackDestroy||
 route==Route::AssetRelease||route==Route::PackRelease||route==Route::TexturePackRegister||route==Route::DemandSchedule||
 route==Route::DemandUnload||route==Route::StreamResource||route==Route::DemandEvict;
}
__declspec(noinline) bool ContextNow(State* s,Context* out)noexcept{
 std::memset(out,0,sizeof(*out));return s->callbacks.context&&s->callbacks.context(s->callbacks.user,out);
}
bool ContextValid(const Context& context,std::uint32_t tid)noexcept{
 if(context.clean_startup)return !context.transaction&&!context.epoch;
 LARGE_INTEGER qpc={};QueryPerformanceCounter(&qpc);curves::CurveFrameStamp frame={};
 return context.post_c10_consumers_closed&&context.transaction&&context.epoch&&
  curves::CaptureActiveCurveFrame(&context.authority,tid,static_cast<std::uint64_t>(qpc.QuadPart),&frame)==curves::CurveBorrowResult::Succeeded;
}
bool SameKey(packed::Key a,packed::Key b)noexcept{return a.address==b.address&&a.generation==b.generation&&a.slot==b.slot;}
packed::Key FindKey(State* s,std::uintptr_t object)noexcept{
 if(tls.state==s)for(std::size_t i=tls.depth;i;--i){const auto key=tls.frames[i-1].call->pack;
  if(key.address==object&&key.generation)return key;}
 if(tls.operation.active&&tls.operation.state==s&&tls.operation.key.address==object)return tls.operation.key;
 packed::Key key={};Lock(s);(void)packed::FindActive(s->packs,object,&key);Unlock(s);return key;
}
packed::Key CurrentPack(State* s)noexcept{
 if(tls.state!=s)return {};for(std::size_t i=tls.depth;i;--i)if(tls.frames[i-1].call->pack.generation)return tls.frames[i-1].call->pack;return {};
}
retained::Input Input(const PendingInput& p)noexcept{
 retained::Input out={};out.file=p.file.file;out.file_generation=p.file.generation;out.source=p.selected_source.address;
 out.source_generation=p.selected_source.generation;out.content_bytes=p.bytes;out.realm=p.realm;out.epoch=p.epoch;
 std::memcpy(out.content_sha256,p.sha256,32);return out;
}
void Propagate(State* s,packed::Key pack,const retained::Input& input)noexcept{
 if(input.realm!=retained::Realm::Project){
  if(input.realm!=retained::Realm::Global||pack.slot>=s->packs->count)return;
  const auto& origin=s->packs->records[pack.slot];
  // A global input in an unfinished project compilation does not make its
  // newly created textures global. Final all-global native loads do retain
  // their global identity, including resources loaded lazily after B0.
  if(!SameKey(origin.key,pack)||!origin.load_completed||origin.pending_inputs||!origin.global||origin.project)return;
 }
 for(std::size_t i=0;i<s->binding_count;++i){const auto& b=s->bindings[i];if(!b.active||!SameKey(b.pack,pack))continue;
  if(retained::RetainInput(s->textures,b.texture,input)!=retained::Result::Succeeded)Lost(s);}
}
void BindPending(State* s,std::uint64_t scope,packed::Key pack)noexcept{
 for(auto& input:s->inputs){if(!input.active||input.bound||input.load_scope!=scope)continue;
  input.pack=pack;input.bound=true;
  if(packed::InputPending(s->packs,pack)!=packed::Result::Succeeded){Lost(s);continue;}
  if(input.completed){const auto copied=Input(input);
   if(packed::InputCompleted(s->packs,pack,copied)!=packed::Result::Succeeded)Lost(s);else Propagate(s,pack,copied);
   input.active=false;}
 }
}
void Material(State* s,Route route,EventPhase phase,std::uintptr_t owner,
 std::uintptr_t a0=0,std::uintptr_t a1=0,std::uintptr_t a2=0,std::uintptr_t a3=0,std::uintptr_t a4=0,std::uintptr_t result=0)noexcept{
 if(s->callbacks.material){MaterialEvent event={route,phase,owner,{a0,a1,a2,a3,a4},result};s->callbacks.material(s->callbacks.user,event);}
 else if(CurrentOwner(s,Route::MaterialPackLoad)||route==Route::MaterialPackLoad||route==Route::MaterialReload)Lost(s);
}
#define ENTER(route,object) auto* s=bridge.load();Call call={};Begin(s,Route::route,reinterpret_cast<std::uintptr_t>(static_cast<const void*>(object)),&call)
#define ORIGINAL(type,route) reinterpret_cast<type>(s->originals[Index(Route::route)])
#define FINISH() __finally { End(s,&call); }
using Ptr=std::uintptr_t;
Ptr __fastcall HookPackConstruct(void* object)noexcept{
 ENTER(PackConstruct,object);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*),PackConstruct)(object);
  PackConstructed(s,result,&call);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookPackOpen(void* pack,const char* name,unsigned flags)noexcept{
 ENTER(PackOpen,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,const char*,unsigned),PackOpen)(pack,name,flags);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookPackLoad(void* runtime,const char* name,void** output,unsigned flags,const char* alias)noexcept{
 ENTER(PackLoad,nullptr);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,const char*,void**,unsigned,const char*),PackLoad)(runtime,name,output,flags,alias);
  if(static_cast<std::int32_t>(result)==0&&call.pack.generation)PackLoaded(s,call.pack);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookPackRegister(void* runtime,void** node,const char* name,void* pack)noexcept{
 ENTER(PackRegister,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void**,const char*,void*),PackRegister)(runtime,node,name,pack);
  if(call.pack.generation)PackRegistered(s,call.pack);else Lost(s);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookPackUnregister(void* runtime,void* node,void* pack)noexcept{
 ENTER(PackUnregister,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void*,void*),PackUnregister)(runtime,node,pack);
  if(node&&call.pack.generation)PackUnregistered(s,call.pack);call.returned=true;}FINISH();return result;
}
void __fastcall HookPackDestroy(void* pack)noexcept{
 ENTER(PackDestroy,pack);if(call.pack.generation)PackDestroyEntered(s,call.pack);
 __try{ORIGINAL(void(__fastcall*)(void*),PackDestroy)(pack);if(call.pack.generation)PackDestroyReturned(s,call.pack);call.returned=true;}FINISH();
}
void __fastcall HookAssetLoad(void* manager,const char* path,unsigned flags,const char* alias)noexcept{
 ENTER(AssetLoad,nullptr);__try{ORIGINAL(void(__fastcall*)(void*,const char*,unsigned,const char*),AssetLoad)(manager,path,flags,alias);call.returned=true;}FINISH();
}
Ptr __fastcall HookAssetRelease(void* manager,void* pack)noexcept{
 ENTER(AssetRelease,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void*),AssetRelease)(manager,pack);call.returned=true;}FINISH();return result;
}
void __fastcall HookPackRelease(void* runtime,void* pack,bool synchronize)noexcept{
 ENTER(PackRelease,pack);__try{ORIGINAL(void(__fastcall*)(void*,void*,bool),PackRelease)(runtime,pack,synchronize);call.returned=true;}FINISH();
}
Ptr __fastcall HookTexturePackRegister(void* runtime,void* pack,void* entry,void* vector,unsigned quality,void* name)noexcept{
 ENTER(TexturePackRegister,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void*,void*,void*,unsigned,void*),TexturePackRegister)(runtime,pack,entry,vector,quality,name);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookTextureConstruct(void* texture,unsigned flags,const char* name)noexcept{
 ENTER(TextureConstruct,texture);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,unsigned,const char*),TextureConstruct)(texture,flags,name);TextureConstructed(s,result);call.returned=true;}FINISH();return result;
}
void __fastcall HookTextureDestroy(void* manager,void* texture)noexcept{
 ENTER(TextureDestroy,texture);retained::Identity identity={};TextureDestroyEntered(s,reinterpret_cast<Ptr>(texture),&identity);
 __try{ORIGINAL(void(__fastcall*)(void*,void*),TextureDestroy)(manager,texture);if(identity.generation)TextureDestroyReturned(s,identity);call.returned=true;}FINISH();
}
Ptr __fastcall HookTextureLoad(void* manager,const char* path,int quality,int flags,const char* user)noexcept{
 ENTER(TextureLoad,manager);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,const char*,int,int,const char*),TextureLoad)(manager,path,quality,flags,user);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookTextureDynamic(void* manager,const char* name,int width,int height,unsigned depth,int cube,unsigned format,
 int flags,int viewport_width,int viewport_height,void* data,int data_size)noexcept{
 ENTER(TextureDynamic,manager);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,const char*,int,int,unsigned,int,unsigned,int,int,int,void*,int),TextureDynamic)
  (manager,name,width,height,depth,cube,format,flags,viewport_width,viewport_height,data,data_size);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookTextureRegister(const char* name,int width,int height,int depth,int mips,unsigned format,char type,void* data)noexcept{
 ENTER(TextureRegister,nullptr);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(const char*,int,int,int,int,unsigned,char,void*),TextureRegister)
  (name,width,height,depth,mips,format,type,data);call.returned=true;}FINISH();return result;
}
void __fastcall HookTextureImage(void* manager,void* texture,void* image)noexcept{
 ENTER(TextureImage,texture);__try{ORIGINAL(void(__fastcall*)(void*,void*,void*),TextureImage)(manager,texture,image);call.returned=true;}FINISH();
}
Ptr __fastcall HookTextureCallback(void* manager,const char* name,void* callback,void* context,int quality,unsigned flags)noexcept{
 ENTER(TextureCallback,manager);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,const char*,void*,void*,int,unsigned),TextureCallback)
  (manager,name,callback,context,quality,flags);call.returned=true;}FINISH();return result;
}
bool __fastcall HookMaterialPackLoad(void* pack,const char* path,int flags)noexcept{
 ENTER(MaterialPackLoad,pack);bool result=false;Material(s,Route::MaterialPackLoad,EventPhase::Enter,reinterpret_cast<Ptr>(pack),reinterpret_cast<Ptr>(path),flags);
 __try{result=ORIGINAL(bool(__fastcall*)(void*,const char*,int),MaterialPackLoad)(pack,path,flags);
  Material(s,Route::MaterialPackLoad,EventPhase::Returned,reinterpret_cast<Ptr>(pack),0,0,0,0,0,result);call.returned=true;}FINISH();return result;
}
unsigned __fastcall HookMaterialReload(void* manager)noexcept{
 ENTER(MaterialReload,manager);unsigned result=0;Material(s,Route::MaterialReload,EventPhase::Enter,reinterpret_cast<Ptr>(manager));
 __try{result=ORIGINAL(unsigned(__fastcall*)(void*),MaterialReload)(manager);
  Material(s,Route::MaterialReload,EventPhase::Returned,reinterpret_cast<Ptr>(manager),0,0,0,0,0,result);call.returned=true;}FINISH();return result;
}
void __fastcall HookMaterialPackDestroy(void* pack)noexcept{
 ENTER(MaterialPackDestroy,pack);Material(s,Route::MaterialPackDestroy,EventPhase::Enter,reinterpret_cast<Ptr>(pack));
 __try{ORIGINAL(void(__fastcall*)(void*),MaterialPackDestroy)(pack);
  Material(s,Route::MaterialPackDestroy,EventPhase::Returned,reinterpret_cast<Ptr>(pack));call.returned=true;}FINISH();
}
Ptr __fastcall HookMaterialAcquire(void* manager,void* context,const char* path,unsigned flags,const char* user)noexcept{
 ENTER(MaterialAcquire,manager);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void*,const char*,unsigned,const char*),MaterialAcquire)(manager,context,path,flags,user);call.returned=true;}FINISH();return result;
}
bool __fastcall HookMaterialRebind(void* pack,void* material,const char* name,unsigned flags)noexcept{
 ENTER(MaterialRebind,pack);bool result=false;__try{result=ORIGINAL(bool(__fastcall*)(void*,void*,const char*,unsigned),MaterialRebind)(pack,material,name,flags);
  Material(s,Route::MaterialRebind,EventPhase::Returned,reinterpret_cast<Ptr>(pack),reinterpret_cast<Ptr>(material),reinterpret_cast<Ptr>(name),flags,0,0,result);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookDemandSchedule(void* runtime,void* pack,const char* name,int type,int priority,int flags)noexcept{
 ENTER(DemandSchedule,pack);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*,void*,const char*,int,int,int),DemandSchedule)(runtime,pack,name,type,priority,flags);call.returned=true;}FINISH();return result;
}
void __fastcall HookDemandUnload(void* runtime,void* entry)noexcept{
 Ptr pack=0;auto* state=bridge.load();if(entry)(void)Read(state,reinterpret_cast<Ptr>(entry)+24,&pack,8);
 ENTER(DemandUnload,reinterpret_cast<void*>(pack));__try{ORIGINAL(void(__fastcall*)(void*,void*),DemandUnload)(runtime,entry);call.returned=true;}FINISH();
}
bool __fastcall HookStreamResource(void* runtime,void* operation,void* pack,void* entry,void* destination,void* callback,void* user,int priority,int first,int last)noexcept{
 ENTER(StreamResource,pack);bool result=false;
 const bool classified=s->callbacks.stream_request&&s->callbacks.stream_request(s->callbacks.user,reinterpret_cast<Ptr>(runtime),
  reinterpret_cast<Ptr>(operation),reinterpret_cast<Ptr>(pack),reinterpret_cast<Ptr>(entry),reinterpret_cast<Ptr>(destination),
  reinterpret_cast<Ptr>(callback),reinterpret_cast<Ptr>(user));
 __try{result=ORIGINAL(bool(__fastcall*)(void*,void*,void*,void*,void*,void*,void*,int,int,int),StreamResource)
  (runtime,operation,pack,entry,destination,callback,user,priority,first,last);
  if(result&&!classified){Context context={};
   if(!ContextNow(s,&context))Lost(s);
   else if(context.epoch){Lock(s);if(packed::ObserveUnclassifiedStream(s->packs,call.pack,context.epoch)!=packed::Result::Succeeded)Lost(s);Unlock(s);}}
  call.returned=true;}FINISH();return result;
}
void __fastcall HookDemandEvict(void* runtime,void* pack)noexcept{
 ENTER(DemandEvict,pack);__try{ORIGINAL(void(__fastcall*)(void*,void*),DemandEvict)(runtime,pack);call.returned=true;}FINISH();
}
bool __fastcall HookContainerOpen(void* container,const char* path,unsigned flags,unsigned magic)noexcept{
 ENTER(ContainerOpen,container);bool result=false;Material(s,Route::ContainerOpen,EventPhase::Enter,reinterpret_cast<Ptr>(container),reinterpret_cast<Ptr>(path),flags,magic);
 __try{result=ORIGINAL(bool(__fastcall*)(void*,const char*,unsigned,unsigned),ContainerOpen)(container,path,flags,magic);
  Material(s,Route::ContainerOpen,EventPhase::Returned,reinterpret_cast<Ptr>(container),0,0,0,0,0,result);call.returned=true;}FINISH();return result;
}
bool __fastcall HookContainerFetch(void* container,unsigned index,unsigned key,void** data,unsigned* size)noexcept{
 ENTER(ContainerFetch,container);bool result=false;
 __try{result=ORIGINAL(bool(__fastcall*)(void*,unsigned,unsigned,void**,unsigned*),ContainerFetch)(container,index,key,data,size);
  Material(s,Route::ContainerFetch,EventPhase::Returned,reinterpret_cast<Ptr>(container),index,key,reinterpret_cast<Ptr>(data),reinterpret_cast<Ptr>(size),0,result);call.returned=true;}FINISH();return result;
}
bool __fastcall HookContainerLoadData(void* container,unsigned index,void* data,void* entries)noexcept{
 ENTER(ContainerLoadData,container);bool result=false;
 Material(s,Route::ContainerLoadData,EventPhase::Enter,reinterpret_cast<Ptr>(container),index,reinterpret_cast<Ptr>(data),reinterpret_cast<Ptr>(entries));
 __try{result=ORIGINAL(bool(__fastcall*)(void*,unsigned,void*,void*),ContainerLoadData)(container,index,data,entries);
  Material(s,Route::ContainerLoadData,EventPhase::Returned,reinterpret_cast<Ptr>(container),index,reinterpret_cast<Ptr>(data),reinterpret_cast<Ptr>(entries),0,0,result);call.returned=true;}FINISH();return result;
}
unsigned __fastcall HookContainerDataSize(void* container,unsigned index)noexcept{
 ENTER(ContainerDataSize,container);unsigned result=0;__try{result=ORIGINAL(unsigned(__fastcall*)(void*,unsigned),ContainerDataSize)(container,index);
  Material(s,Route::ContainerDataSize,EventPhase::Returned,reinterpret_cast<Ptr>(container),index,0,0,0,0,result);call.returned=true;}FINISH();return result;
}
Ptr __fastcall HookFullPath(void* output,const char* path)noexcept{
 auto* s=bridge.load();Frame* target=nullptr;
 if(tls.state==s)for(std::size_t i=tls.depth;i;--i){const auto route=tls.frames[i-1].call->route;
  if(route==Route::AssetLoad||route==Route::PackLoad||route==Route::PackOpen||route==Route::MaterialPackLoad){target=&tls.frames[i-1];break;}}
 using Fn=Ptr(__fastcall*)(void*,const char*);
 resource_provider::Scope scope={};resource_provider::Receipt receipt={};
 if(s->callbacks.full_path)s->callbacks.full_path(s->callbacks.user,EventPhase::Enter,target!=nullptr,0,nullptr);
 const bool armed=target&&resource_provider::Begin(s->provider_access,target->call->serial,&scope);
 Ptr result=0;bool returned=false;
 __try{result=reinterpret_cast<Fn>(s->originals[Index(Route::FullPath)])(output,path);returned=true;
  if(target){if(!armed||!resource_provider::Finish(s->provider_access,&scope,result,&receipt))Lost(s);else target->selection=receipt;}
  if(s->callbacks.full_path)s->callbacks.full_path(s->callbacks.user,EventPhase::Returned,target!=nullptr,result,target?&receipt:nullptr);
 }__finally{resource_provider::Cancel(s->provider_access,&scope);if(!returned){if(target)Lost(s);
  if(s->callbacks.full_path)s->callbacks.full_path(s->callbacks.user,EventPhase::Unwound,target!=nullptr,0,nullptr);}}
 return result;
}
std::uint64_t __fastcall HookMaterialFileRead(void* file,void* data,std::uint64_t bytes)noexcept{
 auto* s=bridge.load();using Fn=std::uint64_t(__fastcall*)(void*,void*,std::uint64_t);
 const auto result=reinterpret_cast<Fn>(s->originals[Index(Route::MaterialFileRead)])(file,data,bytes);
 // Completion/read paths never wait on the resource producer gate. This
 // callback only observes a currently active material-container read.
 if(CurrentOwner(s,Route::MaterialPackLoad))Material(s,Route::MaterialFileRead,EventPhase::Returned,
  reinterpret_cast<Ptr>(file),reinterpret_cast<Ptr>(data),bytes,0,0,0,result);
 return result;
}
void __fastcall HookMaterialRelease(void* manager,void** slot)noexcept{
 ENTER(MaterialRelease,manager);Ptr object=0;if(slot)(void)Read(s,reinterpret_cast<Ptr>(slot),&object,8);
 Material(s,Route::MaterialRelease,EventPhase::Enter,reinterpret_cast<Ptr>(manager),reinterpret_cast<Ptr>(slot),object);
 __try{ORIGINAL(void(__fastcall*)(void*,void**),MaterialRelease)(manager,slot);
  Material(s,Route::MaterialRelease,EventPhase::Returned,reinterpret_cast<Ptr>(manager),reinterpret_cast<Ptr>(slot),object);call.returned=true;}FINISH();
}
Ptr __fastcall HookMaterialAddRef(void* material)noexcept{
 ENTER(MaterialAddRef,material);Ptr result=0;__try{result=ORIGINAL(Ptr(__fastcall*)(void*),MaterialAddRef)(material);call.returned=true;}FINISH();return result;
}
bool __fastcall HookMaterialReleaseRef(void* material)noexcept{
 ENTER(MaterialReleaseRef,material);bool result=false;__try{result=ORIGINAL(bool(__fastcall*)(void*),MaterialReleaseRef)(material);
  Material(s,Route::MaterialReleaseRef,EventPhase::Returned,reinterpret_cast<Ptr>(material),0,0,0,0,0,result);call.returned=true;}FINISH();return result;
}
bool __fastcall HookTextureRelease(void* manager,void* texture)noexcept{
 ENTER(TextureRelease,texture);bool result=false;__try{result=ORIGINAL(bool(__fastcall*)(void*,void*),TextureRelease)(manager,texture);call.returned=true;}FINISH();return result;
}
#undef ENTER
#undef ORIGINAL
#undef FINISH
}
const Descriptor* Catalog(std::size_t* count)noexcept{if(count)*count=kRoutes;return descriptors;}
std::uintptr_t Wrapper(Route route)noexcept{
 switch(route){
#define CASE(n) case Route::n:return reinterpret_cast<std::uintptr_t>(&Hook##n)
 CASE(PackConstruct);CASE(PackOpen);CASE(PackLoad);CASE(PackRegister);CASE(PackUnregister);CASE(PackDestroy);
 CASE(AssetLoad);CASE(AssetRelease);CASE(PackRelease);CASE(TexturePackRegister);CASE(TextureConstruct);CASE(TextureDestroy);
 CASE(TextureLoad);CASE(TextureDynamic);CASE(TextureRegister);CASE(TextureImage);CASE(TextureCallback);CASE(MaterialPackLoad);
 CASE(MaterialReload);CASE(MaterialPackDestroy);CASE(MaterialAcquire);CASE(MaterialRebind);CASE(DemandSchedule);
 CASE(DemandUnload);CASE(StreamResource);CASE(DemandEvict);CASE(ContainerOpen);CASE(ContainerFetch);CASE(ContainerLoadData);CASE(ContainerDataSize);CASE(FullPath);CASE(MaterialFileRead);
 CASE(MaterialRelease);CASE(MaterialAddRef);CASE(MaterialReleaseRef);CASE(TextureRelease);
#undef CASE
 default:return 0;}
}
bool Initialize(State* s,std::uintptr_t engine,std::uintptr_t filesystem,const char* engine_sha,const char* fs_sha,
 const Callbacks& callbacks,file_lifetime::bridge::State* files,packed::Ledger* packs,retained::Ledger* textures,
 audio::AudioFullPathProbeState* probe,std::uintptr_t probe_target)noexcept{
 if(!s||s->initialized||!engine||!filesystem||!files||!packs||!textures||!callbacks.read||!callbacks.context||
 !callbacks.potential||!callbacks.pin_input||!callbacks.material||!callbacks.material_file||
 !callbacks.source_revision||!callbacks.classify_source||!probe||!probe_target||
 !Sha(engine_sha,retained::windows::kPlayerEngineSha256)||!Sha(fs_sha,file_lifetime::windows::kFilesystemSha256))return false;
 s->engine_base=engine;s->filesystem_base=filesystem;s->callbacks=callbacks;s->files=files;s->packs=packs;s->textures=textures;
 s->provider_access={probe,files,callbacks.read,callbacks.source_revision,callbacks.user};s->provider_probe_target=probe_target;
 for(const auto& d:descriptors){const auto index=Index(d.route);
  if(d.pointer_slot){std::uintptr_t current=0;const auto slot_base=d.pointer_in_filesystem?filesystem:engine;
   const auto expected=d.engine_rva?engine+d.engine_rva:filesystem+d.filesystem_rva;
   if(!Read(s,slot_base+d.pointer_slot,&current,8)||current!=expected)return false;s->originals[index]=current;}
  else{unsigned char bytes[16]={};if(!Read(s,engine+d.engine_rva,bytes,d.prefix_size)||std::memcmp(bytes,d.prefix,d.prefix_size))return false;s->originals[index]=engine+d.engine_rva;}
  s->installed_targets[index]=Wrapper(d.route);
 }
 s->initialized=true;return true;
}
bool BindOriginal(State* s,Route route,std::uintptr_t original,std::uintptr_t installed)noexcept{
 if(!s||!s->initialized||s->sealed.load()||Index(route)>=kRoutes||!original)return false;
 const auto& d=descriptors[Index(route)];if(installed&&installed!=Wrapper(route)&&!d.shared_observer)return false;
 s->originals[Index(route)]=original;s->installed_targets[Index(route)]=installed?installed:Wrapper(route);return true;
}
bool PublishBridge(State* s)noexcept{
 if(!s||!s->initialized)return false;State* expected=nullptr;return bridge.compare_exchange_strong(expected,s)||expected==s;
}
bool VerifyInstalled(State* s)noexcept{
 if(!s||!s->initialized)return false;
 for(const auto& d:descriptors){const auto i=Index(d.route);if(!s->originals[i]||!s->installed_targets[i])return false;
  if(d.pointer_slot){std::uintptr_t target=0;const auto slot_base=d.pointer_in_filesystem?s->filesystem_base:s->engine_base;
   if(!Read(s,slot_base+d.pointer_slot,&target,8)||target!=s->installed_targets[i])return false;}
  else{unsigned char bytes[14]={};std::uintptr_t target=0;if(!Read(s,s->engine_base+d.engine_rva,bytes,sizeof(bytes)))return false;
   if(bytes[0]==0xff&&bytes[1]==0x25&&!bytes[2]&&!bytes[3]&&!bytes[4]&&!bytes[5])std::memcpy(&target,bytes+6,8);
   if(target!=s->installed_targets[i])return false;}
 }
 unsigned char probe_bytes[14]={};std::uintptr_t probe_target=0;
 if(!Read(s,s->filesystem_base+audio::kFilesystemFullPathProviderProbeRva,probe_bytes,sizeof(probe_bytes)))return false;
 if(probe_bytes[0]==0xff&&probe_bytes[1]==0x25&&!probe_bytes[2]&&!probe_bytes[3]&&!probe_bytes[4]&&!probe_bytes[5])
  std::memcpy(&probe_target,probe_bytes+6,8);
 return probe_target==s->provider_probe_target;
}
bool SealInstalled(State* s)noexcept{
 if(!s||!s->initialized||s->active_calls.load()||s->pending_calls.load()||!VerifyInstalled(s))return false;
 s->sealed.store(true);return true;
}
void Begin(State* s,Route route,std::uintptr_t owner,Call* out)noexcept{
 if(!s||!out||!s->initialized||Index(route)>=kRoutes)return;*out={};const auto tid=GetCurrentThreadId();
 s->pending_calls.fetch_add(1);s->revision.fetch_add(1);
 if(Retaining(route))s->callbacks.potential(s->callbacks.user,route);
 if(tls.depth&&tls.state!=s){Lost(s);s->pending_calls.fetch_sub(1);return;}
 const bool outer=!tls.depth;
 if(outer){for(;;){while(s->owner_thread.load()&&s->owner_thread.load()!=tid)Sleep(1);
   s->active_calls.fetch_add(1);if(!s->owner_thread.load()||s->owner_thread.load()==tid)break;s->active_calls.fetch_sub(1);}tls.state=s;}
 s->pending_calls.fetch_sub(1);
 if(tls.depth>=64){Lost(s);if(outer)s->active_calls.fetch_sub(1);return;}
 packed::Key pack={};if(owner&&PackReceiver(route))pack=FindKey(s,owner);
 if(s->held.load()){
  const bool owns_operation=tls.operation.active&&tls.operation.state==s&&
   (!pack.generation||SameKey(pack,tls.operation.key));
  if(Retaining(route)){if(!s->callbacks.replay||!s->callbacks.replay(s->callbacks.user,route,owner))Lost(s);}
  else if(!owns_operation&&(!s->callbacks.replay||!s->callbacks.replay(s->callbacks.user,route,owner)))Lost(s);
 }
 *out={s->revision.fetch_add(1)+1,tls.depth,route,pack,true,outer,false};
 tls.frames[tls.depth++]={out,owner,{}};
}
void End(State* s,Call* call)noexcept{
 if(!s||!call)return;
 if(!call->active||tls.state!=s||!tls.depth||call->depth!=tls.depth-1||tls.frames[call->depth].call!=call){Lost(s);return;}
 if(!call->returned){Lost(s);Material(s,call->route,EventPhase::Unwound,tls.frames[call->depth].owner);}
 Lock(s);
 for(auto& input:s->inputs)if(input.active&&!input.bound&&input.load_scope==call->serial){
  input.scope_ended=true;if(input.realm==retained::Realm::Project)Lost(s);if(input.completed)input.active=false;}
 Unlock(s);--tls.depth;tls.frames[tls.depth]={};call->active=false;s->revision.fetch_add(1);
 if(call->outer){tls.state=nullptr;s->active_calls.fetch_sub(1);}
}
std::uintptr_t CurrentOwner(State* s,Route route)noexcept{
 if(tls.state!=s)return 0;for(std::size_t i=tls.depth;i;--i)if(tls.frames[i-1].call->route==route)return tls.frames[i-1].owner;return 0;
}
bool HasActiveInputScope(State* s)noexcept{
 if(!s||tls.state!=s)return false;
 for(std::size_t i=tls.depth;i;--i){const auto route=tls.frames[i-1].call->route;
  if(route==Route::AssetLoad||route==Route::PackLoad||route==Route::PackOpen||route==Route::MaterialPackLoad)return true;}
 return false;
}
bool SnapshotPackKey(State* s,std::uintptr_t object,packed::Key* out)noexcept{
 if(!s||!out)return false;Lock(s);const bool found=packed::FindActive(s->packs,object,out)==packed::Result::Succeeded;Unlock(s);return found;
}
bool Close(State* s,std::uint32_t timeout,Lease* out)noexcept{
 if(!s||!out||!s->initialized||!s->sealed.load()||s->lost.load()||tls.depth||!VerifyInstalled(s))return false;*out={};
 const auto tid=GetCurrentThreadId();std::uint32_t no_owner=0;if(!s->owner_thread.compare_exchange_strong(no_owner,tid))return false;
 const auto start=GetTickCount64();while(s->active_calls.load()){
  if(GetTickCount64()-start>=timeout){s->owner_thread.store(0);return false;}Sleep(1);}
 Context context={};const bool valid=ContextNow(s,&context)&&ContextValid(context,tid);
 if(!valid||s->pending_calls.load()||s->lost.load()||PendingInputCount(s)){s->owner_thread.store(0);return false;}
 s->lease={s->generation.fetch_add(1)+1,context.transaction,context.epoch,tid};s->held.store(true);*out=s->lease;return true;
}
bool Open(State* s,const Lease& lease)noexcept{
 if(!s||!s->held.load()||s->owner_thread.load()!=GetCurrentThreadId()||lease.thread!=GetCurrentThreadId()||
 lease.generation!=s->lease.generation)return false;
 s->held.store(false);s->lease={};s->owner_thread.store(0);return true;
}
bool CaptureProducerFence(void* user,packed::Fence* out)noexcept{
 auto* s=static_cast<State*>(user);if(!s||!out)return false;*out={};
 if(!s->held.load()||s->lost.load()||s->pending_calls.load()||s->active_calls.load()||
 s->owner_thread.load()!=GetCurrentThreadId()||PendingInputCount(s))return false;
 Context context={};if(!ContextNow(s,&context)||!ContextValid(context,GetCurrentThreadId())||
 context.transaction!=s->lease.transaction||context.epoch!=s->lease.epoch||!VerifyInstalled(s))return false;
 const auto revision=s->revision.load();if(s->packs->revision>UINT64_MAX-revision||
 s->textures->revision>UINT64_MAX-revision-s->packs->revision)return false;
 out->transaction=context.transaction;out->epoch=context.epoch;out->thread_id=GetCurrentThreadId();out->owner_generation=s->lease.generation;
 out->mutation_revision=revision+s->packs->revision+s->textures->revision;
 out->authoritative_post_destroy=context.post_c10_consumers_closed;out->producers_closed=true;out->consumers_drained=context.clean_startup||context.post_c10_consumers_closed;return true;
}
bool AuthorizeNativeOperation(void* user,packed::windows::NativeOperation kind,packed::Key key,bool entering)noexcept{
 auto* s=static_cast<State*>(user);if(!s)return false;
 if(!entering){const bool same=tls.operation.active&&tls.operation.state==s&&tls.operation.kind==kind&&SameKey(tls.operation.key,key);
  if(same)tls.operation={};return same&&!s->lost.load()&&!s->active_calls.load()&&!s->pending_calls.load();}
 packed::Fence fence={};if(tls.operation.active||!CaptureProducerFence(s,&fence)||!fence.authoritative_post_destroy||key.slot>=s->packs->count)return false;
 Lock(s);const auto& origin=s->packs->records[key.slot];const bool valid=!origin.empty&&SameKey(origin.key,key)&&origin.project&&!origin.global&&!origin.baseline&&
 origin.registered&&origin.load_completed&&!origin.pending_inputs&&!origin.destructor_entered;Unlock(s);
 if(!valid)return false;tls.operation={s,kind,key,true};return true;
}
void PackConstructed(State* s,std::uintptr_t object,Call* call)noexcept{
 if(!s||!object||!call||tls.state!=s){Lost(s);return;}packed::Key key={};Lock(s);
 if(packed::Constructed(s->packs,object,&key)!=packed::Result::Succeeded){Unlock(s);Lost(s);return;}
 call->pack=key;
 for(std::size_t i=tls.depth;i;--i){auto& frame=tls.frames[i-1];const auto route=frame.call->route;
  if(route==Route::PackOpen)break;
  if((route==Route::PackLoad||route==Route::AssetLoad)&&!frame.call->pack.generation){frame.call->pack=key;BindPending(s,frame.call->serial,key);}}
 Unlock(s);
}
void PackRegistered(State* s,packed::Key key)noexcept{if(!s)return;Lock(s);if(packed::Registered(s->packs,key)!=packed::Result::Succeeded)Lost(s);Unlock(s);}
void PackLoaded(State* s,packed::Key key)noexcept{if(!s)return;Lock(s);
 if(packed::Loaded(s->packs,key)!=packed::Result::Succeeded)Lost(s);
 else{const auto& origin=s->packs->records[key.slot];for(std::size_t i=0;i<origin.input_count;++i)Propagate(s,key,origin.inputs[i]);}
 Unlock(s);}
void PackDestroyEntered(State* s,packed::Key key)noexcept{if(!s)return;Lock(s);if(packed::DestructorEntered(s->packs,key)!=packed::Result::Succeeded)Lost(s);Unlock(s);}
void PackDestroyReturned(State* s,packed::Key key)noexcept{if(!s)return;Lock(s);if(packed::DestructorReturned(s->packs,key)!=packed::Result::Succeeded)Lost(s);Unlock(s);}
void PackUnregistered(State* s,packed::Key key)noexcept{if(!s)return;Lock(s);if(packed::Unregistered(s->packs,key)!=packed::Result::Succeeded)Lost(s);Unlock(s);}
void TextureConstructed(State* s,std::uintptr_t object)noexcept{
 if(!s||!object){Lost(s);return;}const auto pack=CurrentPack(s);retained::Identity identity={};Lock(s);
 if(retained::Constructed(s->textures,object,retained::Kind::Texture,&identity)!=retained::Result::Succeeded){Unlock(s);Lost(s);return;}
 if(pack.generation){std::size_t slot=0;while(slot<s->binding_count&&s->bindings[slot].active)++slot;
  if(slot==kTextureBindings)Lost(s);else{s->bindings[slot]={identity,pack,true};if(slot==s->binding_count)++s->binding_count;
   if(pack.slot<s->packs->count){const auto& origin=s->packs->records[pack.slot];
    if(!SameKey(origin.key,pack))Lost(s);else for(std::size_t i=0;i<origin.input_count;++i)Propagate(s,pack,origin.inputs[i]);}else Lost(s);}}
 Unlock(s);
}
void TextureDestroyEntered(State* s,std::uintptr_t object,retained::Identity* out)noexcept{
 if(out)*out={};if(!s||!out||!object)return;Lock(s);
 const auto found=retained::FindIdentity(s->textures,object,retained::Kind::Texture,out);
 if(found==retained::Result::Succeeded){if(retained::BeginDestruction(s->textures,*out)!=retained::Result::Succeeded)Lost(s);}
 else if(s->textures->baseline_captured)Lost(s);Unlock(s);
}
void TextureDestroyReturned(State* s,retained::Identity identity)noexcept{
 if(!s)return;Lock(s);if(retained::EndDestruction(s->textures,identity)!=retained::Result::Succeeded)Lost(s);
 for(std::size_t i=0;i<s->binding_count;++i)if(s->bindings[i].texture.object==identity.object&&s->bindings[i].texture.generation==identity.generation)s->bindings[i].active=false;Unlock(s);
}
bool ObserveOpenedFile(State* s,std::uintptr_t file,retained::Realm realm,std::uint32_t epoch,InputKey* out)noexcept{
 return ObserveNativeOpenedFile(s,file,nullptr,realm,epoch,out);
}
bool ObserveNativeOpenedFile(State* s,std::uintptr_t file,const char* native_path,retained::Realm realm,std::uint32_t epoch,InputKey* out)noexcept{
 if(out)*out={};if(!s||!out||!file||tls.state!=s||!tls.depth)return false;
 Frame* target_frame=nullptr;bool material_scope=false;
 for(std::size_t i=tls.depth;i;--i){const auto route=tls.frames[i-1].call->route;
  if(route==Route::PackOpen||route==Route::PackLoad||route==Route::AssetLoad||route==Route::MaterialPackLoad){
   target_frame=&tls.frames[i-1];material_scope=route==Route::MaterialPackLoad;break;}}
 if(!target_frame)return false;
 file_lifetime::FileRecord actual={};if(!file_lifetime::bridge::FileIdentity(s->files,file,&actual)||
 actual.life!=file_lifetime::Life::Live||!actual.generation){Lost(s);return false;}
 file_lifetime::SourceKey selected=actual.source;
 if(!selected.address){
  for(std::size_t i=tls.depth;i;--i){const auto& candidate=tls.frames[i-1];
   if(candidate.selection.exact&&resource_provider::Matches(s->provider_access,candidate.selection,candidate.call->serial,native_path)){
    selected=candidate.selection.source;break;}}
 }
 retained::Realm observed=retained::Realm::Unknown;std::uint32_t observed_epoch=0;
 if(!selected.address||!selected.generation||!s->callbacks.classify_source(s->callbacks.user,selected,&observed,&observed_epoch)||
 observed==retained::Realm::Unknown||(realm!=retained::Realm::Unknown&&(observed!=realm||observed_epoch!=epoch))){Lost(s);return false;}
 realm=observed;epoch=observed_epoch;
 if(material_scope){if(!s->callbacks.material_file(s->callbacks.user,target_frame->owner,actual,selected,realm,epoch))Lost(s);return !s->lost.load();}
 Call* target=target_frame->call;
 Lock(s);std::size_t slot=0;while(slot<kPendingInputs&&s->inputs[slot].active)++slot;
 if(slot==kPendingInputs){Unlock(s);Lost(s);return false;}
 auto& input=s->inputs[slot];input={};input.key={s->next_input.fetch_add(1)+1,static_cast<std::uint32_t>(slot)};
 input.file=actual;input.selected_source=selected;input.pack=target->pack;input.load_scope=target->serial;input.realm=realm;input.epoch=epoch;input.active=true;
 if(target->pack.generation){input.bound=true;if(packed::InputPending(s->packs,target->pack)!=packed::Result::Succeeded)Lost(s);}
 *out=input.key;Unlock(s);
 if(!s->callbacks.pin_input(s->callbacks.user,*out,actual)){FailInput(s,*out);return false;}return true;
}
bool CompleteInput(State* s,InputKey key,const unsigned char digest[32],std::uint64_t bytes)noexcept{
 if(!s||!digest||key.slot>=kPendingInputs)return false;Lock(s);auto& input=s->inputs[key.slot];
 if(!input.active||input.completed||input.key.generation!=key.generation){Unlock(s);Lost(s);return false;}
 std::memcpy(input.sha256,digest,32);input.bytes=bytes;input.completed=true;bool valid=true;
 if(input.bound){const auto copied=Input(input);valid=packed::InputCompleted(s->packs,input.pack,copied)==packed::Result::Succeeded;
  if(valid)Propagate(s,input.pack,copied);input.active=false;}
 else if(input.scope_ended){valid=input.realm==retained::Realm::Global;input.active=false;}
 Unlock(s);s->revision.fetch_add(1);if(!valid)Lost(s);return valid&&!s->lost.load();
}
void FailInput(State* s,InputKey key)noexcept{
 if(!s)return;Lock(s);if(key.slot<kPendingInputs&&s->inputs[key.slot].key.generation==key.generation)s->inputs[key.slot].completed=false;Unlock(s);Lost(s);
}
std::size_t PendingInputCount(State* s)noexcept{
 if(!s)return kPendingInputs;Lock(s);std::size_t count=0;for(const auto& input:s->inputs)if(input.active)++count;Unlock(s);return count;
}
} // namespace
