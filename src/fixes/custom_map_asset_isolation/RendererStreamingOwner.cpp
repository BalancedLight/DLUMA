#include "RendererStreamingOwner.h"
#include <cstring>
extern "C" __declspec(dllimport) void WINAPI Sleep(DWORD);
extern "C" __declspec(dllimport) unsigned long long WINAPI GetTickCount64();
extern "C" __declspec(dllimport) LPVOID WINAPI VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);
extern "C" __declspec(dllimport) BOOL WINAPI VirtualFree(LPVOID,SIZE_T,DWORD);
namespace dluma::fixes::custom_map_asset_isolation::renderer_streaming {
namespace {
constexpr HookSpec hooks[]={
 {Route::Create,"Streaming descriptor create",0x15E10,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},20,0,0,0,0},
 {Route::Delete,"Streaming descriptor release",0x15F30,{0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0x79,0x20,0x00,0x48,0x8b,0xf9,0xc6,0x41},18,0,0,0,0},
 {Route::Request,"Streaming mip request",0x16CA0,{0x48,0x89,0x5c,0x24,0x10,0x4c,0x8b,0x59,0x20,0x33,0xdb,0x4c,0x8b,0xc9,0x4d,0x8b},18,0,0,0,0},
 {Route::FullRequest,"Streaming full request",0x16A70,{0x48,0x85,0xc9,0x0f,0x84,0xb7,0x00,0x00,0x00,0x53,0x48,0x83,0xec,0x20,0x48,0x83},19,5,9,0,0},
 {Route::Tick,"Streaming tick and descriptor collection",0x153E0,{0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x48,0x83,0xec,0x58,0x48,0x89,0x58,0x20,0x48},19,0,0,0,0},
 {Route::Control,"Streaming control",0x150A0,{0x83,0xfa,0x09,0x0f,0x87,0x85,0x02,0x00,0x00,0x48,0x8b,0xc4,0x53,0x48,0x81,0xec},20,5,9,0,0},
 {Route::Finalize,"Streaming manager finalize",0x14A50,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8d,0x0d,0xeb,0xd3,0x46,0x00,0xff,0x15,0x9d},19,9,13,15,19},
 {Route::Initialize,"Streaming manager initialize",0x149A0,{0x40,0x53,0x48,0x83,0xec,0x20,0x33,0xdb,0x48,0x8d,0x0d,0xe1,0xd3,0x46,0x00,0xc7},25,11,15,17,25},
};
std::atomic<State*> bridge{nullptr};
struct ThreadScope{State* state;unsigned depth;bool observing;Route outer_route;};__declspec(thread) ThreadScope thread={};
struct Call{State* state;Route route;bool outer,active,returned,observed,observation_acquired;};
std::size_t Index(Route route)noexcept{return static_cast<std::size_t>(route);}
void Lose(State* s)noexcept{if(s){s->lost.store(true);s->revision.fetch_add(1);}}
bool TryObservation(State* s)noexcept{return !s->record_lock.test_and_set(std::memory_order_acquire);}
void Unlock(State* s)noexcept{s->record_lock.clear(std::memory_order_release);}
bool Read(State* s,std::uintptr_t address,void* output,std::size_t bytes)noexcept{
 return s&&s->read&&address&&bytes&&bytes-1<=UINTPTR_MAX-address&&s->read(s->user,address,output,bytes);
}
template<class T>bool Field(State* s,std::uintptr_t object,std::size_t offset,T* out)noexcept{
 return object&&offset<=UINTPTR_MAX-object&&Read(s,object+offset,out,sizeof(*out));
}
bool Same(packed::Key a,packed::Key b)noexcept{return a.address==b.address&&a.generation==b.generation&&a.slot==b.slot;}
bool Native(State* s,std::uintptr_t object,NativeDescriptor* out)noexcept{
 if(!out)return false;*out={};out->object=object;
 return Field(s,object,0,&out->job)&&Field(s,object,8,&out->next_request)&&Field(s,object,16,&out->pack)&&
  Field(s,object,24,&out->entry)&&Field(s,object,32,&out->texture)&&Field(s,object,46,&out->streaming)&&
  Field(s,object,47,&out->completed)&&Field(s,object,48,&out->wanted)&&Field(s,object,49,&out->deleted)&&
  Field(s,object,56,&out->upload)&&out->pack&&out->entry&&out->texture&&out->streaming<=1&&out->deleted<=1;
}
Record* Find(State* s,std::uintptr_t address)noexcept{
 for(std::size_t i=s->count;i;--i){auto& r=s->records[i-1];if(r.life!=Life::Empty&&r.life!=Life::Ended&&r.native.object==address)return &r;}return nullptr;
}
bool Listed(State* s,std::uintptr_t object,bool* out)noexcept{
 *out=false;std::uintptr_t data=0;std::uint32_t count=0,capacity=0;
 if(!Field(s,s->base,0x481E00,&data)||!Field(s,s->base,0x481E08,&count)||!Field(s,s->base,0x481E0C,&capacity)||
  count>capacity||count>kMaximumDescriptors||(count&&!data))return false;
 for(std::uint32_t i=0;i<count;++i){std::uintptr_t value=0;if(!Field(s,data,8ULL*i,&value))return false;if(value==object)*out=true;}return true;
}
void Created(State* s,std::uintptr_t object,std::uintptr_t pack,std::uintptr_t texture,std::uintptr_t entry)noexcept{
 if(!object)return;NativeDescriptor native={};packed::Key key={};
 if(!Native(s,object,&native)||native.pack!=pack||native.texture!=texture||native.entry!=entry||native.deleted||Find(s,object)){
  Lose(s);return;}
 const bool known=s->pack_key&&s->pack_key(s->user,pack,&key);
 if(!known&&s->baseline_generation){Lose(s);return;}
 std::size_t slot=0;while(slot<s->count&&s->records[slot].life!=Life::Empty)++slot;
 if(slot==s->capacity||s->next_generation==UINT64_MAX){Lose(s);return;}
 auto& r=s->records[slot];r={};r.native=native;r.pack=known?key:packed::Key{pack,0,0};r.generation=++s->next_generation;r.life=Life::Live;
 if(slot==s->count)++s->count;s->revision.fetch_add(1);
}
void Begin(State* s,Route route,Call* call)noexcept{
 *call={};if(!s||!s->validated)return;const auto tid=GetCurrentThreadId();s->pending.fetch_add(1);s->revision.fetch_add(1);
 if(thread.depth&&thread.state!=s){s->pending.fetch_sub(1);Lose(s);return;}
 const bool outer=!thread.depth;
 if(outer){for(;;){while(s->owner_thread.load()&&s->owner_thread.load()!=tid)Sleep(1);
   s->active.fetch_add(1);if(!s->owner_thread.load()||s->owner_thread.load()==tid)break;s->active.fetch_sub(1);}
  thread.state=s;thread.outer_route=route;}
 s->pending.fetch_sub(1);
 // An observer must not add a waiting lock across native engine owners. Only
 // descriptor/vector writers need the short generation publication scope;
 // unrelated concurrent requests continue with their original concurrency.
 const bool writer=route==Route::Create||route==Route::Delete||route==Route::Tick||route==Route::Finalize||route==Route::Initialize;
 bool acquired=false;
 if(writer&&!thread.observing){acquired=TryObservation(s);thread.observing=acquired;if(!acquired)Lose(s);}
 if(s->owner_thread.load()==tid&&route!=Route::Delete&&route!=Route::Tick)Lose(s);
 if(s->owner_thread.load()==tid&&((route==Route::Delete&&!s->deleting_permitted)||(route==Route::Tick&&!s->collecting_permitted)))Lose(s);
 if(thread.observing&&(route==Route::Finalize||route==Route::Initialize)&&(s->baseline_generation||s->count))Lose(s);
 ++thread.depth;*call={s,route,outer,true,false,thread.observing,acquired};
}
void End(Call* call)noexcept{
 if(!call->active)return;auto* s=call->state;if(!call->returned)Lose(s);
 if(!thread.depth||thread.state!=s){Lose(s);return;}--thread.depth;call->active=false;s->revision.fetch_add(1);
 if(call->observation_acquired){thread.observing=false;Unlock(s);}
 if(call->outer){thread.state=nullptr;s->active.fetch_sub(1);}
}
template<class T>T Original(State* s,Route route)noexcept{return reinterpret_cast<T>(s->originals[Index(route)]);}
#define START(kind) auto* s=bridge.load();Call call={};Begin(s,Route::kind,&call)
#define END() __finally{End(&call);}
std::uintptr_t __fastcall Create(void* texture,void* pack,void* entry,void* header)noexcept{
 START(Create);std::uintptr_t result=0;__try{result=Original<std::uintptr_t(__fastcall*)(void*,void*,void*,void*)>(s,Route::Create)(texture,pack,entry,header);
  if(call.observed)Created(s,result,reinterpret_cast<std::uintptr_t>(pack),reinterpret_cast<std::uintptr_t>(texture),reinterpret_cast<std::uintptr_t>(entry));call.returned=true;}END();return result;
}
void __fastcall Delete(void* descriptor)noexcept{
 START(Delete);auto* record=call.observed?Find(s,reinterpret_cast<std::uintptr_t>(descriptor)):nullptr;
 if(call.observed&&!record&&s->baseline_generation)Lose(s);
 __try{Original<void(__fastcall*)(void*)>(s,Route::Delete)(descriptor);
  if(record){NativeDescriptor after={};if(!Native(s,record->native.object,&after)||!after.deleted||after.pack!=record->native.pack||after.texture!=record->native.texture)Lose(s);
   else{record->native=after;record->life=Life::Deleted;}}
  call.returned=true;}END();
}
std::uintptr_t __fastcall Request(void* descriptor,int level,int priority)noexcept{
 START(Request);std::uintptr_t result=0;__try{result=Original<std::uintptr_t(__fastcall*)(void*,int,int)>(s,Route::Request)(descriptor,level,priority);call.returned=true;}END();return result;
}
void __fastcall FullRequest(void* texture)noexcept{START(FullRequest);__try{Original<void(__fastcall*)(void*)>(s,Route::FullRequest)(texture);call.returned=true;}END();}
void __fastcall Tick(std::uint32_t* result)noexcept{
 START(Tick);__try{Original<void(__fastcall*)(std::uint32_t*)>(s,Route::Tick)(result);
  // Conflicting native vector writers invalidate coverage without waiting.
  // A successful exclusive scope makes this exact return join unambiguous.
  if(call.observed&&!s->lost.load())for(std::size_t i=0;i<s->count;++i){auto& r=s->records[i];if(r.life!=Life::Deleted)continue;
   bool listed=false;if(!Listed(s,r.native.object,&listed)){Lose(s);break;}if(!listed){r.life=Life::Ended;r.verified_transaction=s->authority.transaction;}}
  call.returned=true;}END();
}
void __fastcall Control(void* api,std::uint32_t command,void* input)noexcept{START(Control);__try{Original<void(__fastcall*)(void*,std::uint32_t,void*)>(s,Route::Control)(api,command,input);call.returned=true;}END();}
std::uintptr_t __fastcall Finalize()noexcept{START(Finalize);std::uintptr_t result=0;__try{result=Original<std::uintptr_t(__fastcall*)()>(s,Route::Finalize)();call.returned=true;}END();return result;}
std::uintptr_t __fastcall InitializeNative()noexcept{START(Initialize);std::uintptr_t result=0;__try{result=Original<std::uintptr_t(__fastcall*)()>(s,Route::Initialize)();call.returned=true;}END();return result;}
#undef START
#undef END
using TryLock=int(WINAPI*)(void*);using UnlockNative=void(WINAPI*)(void*);
template<class T>T Kernel(const char* name)noexcept{return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(reinterpret_cast<LPCWSTR>(L"kernel32.dll")),name));}
bool NativeOwner(State* s)noexcept{
 std::uintptr_t api=0,vtable=0,acquire=0,release=0;unsigned char empty_return=0;
 std::uint32_t io_marker=0,completion_marker=0,vector_marker=0,full_marker=0;
 return Field(s,s->engine,0xACD0F8,&api)&&Field(s,api,0,&vtable)&&vtable==s->base+0x70E38&&
  Field(s,vtable,56,&acquire)&&acquire==s->base+0x1B120&&Field(s,vtable,64,&release)&&release==acquire&&
  Field(s,s->base,0x1B120,&empty_return)&&empty_return==0xC3&&
  Field(s,s->base,0x481D88,&io_marker)&&io_marker==63&&Field(s,s->base,0x481DD0,&completion_marker)&&completion_marker==63&&
  Field(s,s->base,0x481E10,&vector_marker)&&vector_marker==63&&Field(s,s->base,0x481E28,&full_marker)&&full_marker==63;
}
bool Gate(State* s)noexcept{
 if(!s||!s->sealed||s->lost.load()||s->owner_thread.load()!=GetCurrentThreadId()||!s->main_owned||!s->io_owned||!s->completion_owned||
  s->active.load()||s->pending.load()||!NativeOwner(s)||!VerifyInstalled(s))return false;
 packed::Fence current={};return s->fence&&s->fence(s->user,&current)&&current.producers_closed&&current.consumers_drained&&
  current.owner_generation&&current.owner_generation==s->authority.owner_generation&&
  current.transaction==s->authority.transaction&&current.epoch==s->authority.epoch&&current.thread_id==GetCurrentThreadId();
}
bool CopySources(Record* record,const packed::Origin& pack)noexcept{
 if(pack.pending_inputs||(!pack.load_completed&&record->life==Life::Live))return false;
 if(!pack.project)return true;record->project=true;
 for(std::size_t i=0;i<pack.input_count;++i){const auto& in=pack.inputs[i];if(in.realm!=retained::Realm::Project)continue;
  bool found=false;for(std::size_t j=0;j<record->source_count;++j)if(record->sources[j].address==in.source&&record->sources[j].generation==in.source_generation)found=true;
  if(!found){if(record->source_count==retained::kMaximumInputs)return false;record->sources[record->source_count++]={in.source,in.source_generation};}}
 return record->source_count!=0;
}
}
const HookSpec* Catalog(std::size_t* count)noexcept{if(count)*count=kRoutes;return hooks;}
std::uintptr_t Wrapper(Route route)noexcept{
 switch(route){
#define ITEM(n) case Route::n:return reinterpret_cast<std::uintptr_t>(&n)
 ITEM(Create);ITEM(Delete);ITEM(Request);ITEM(FullRequest);ITEM(Tick);ITEM(Control);ITEM(Finalize);
 case Route::Initialize:return reinterpret_cast<std::uintptr_t>(&InitializeNative);default:return 0;
#undef ITEM
 }
}
bool Initialize(State* s,std::uintptr_t base,std::uintptr_t engine,const char* sha,ReadFn read,FenceFn fence,PackKeyFn key,
 void* user,Record* records,std::size_t capacity,NativeDescriptor* scratch)noexcept{
 if(!s||s->validated||!base||!engine||!sha||!read||!fence||!key||!records||!scratch||
  !capacity||capacity>kMaximumDescriptors||base>UINTPTR_MAX-0x48B000)return false;
 for(std::size_t i=0;i<sizeof(kPlayerRendererSha256);++i)if(sha[i]!=kPlayerRendererSha256[i])return false;
 s->base=base;s->engine=engine;s->read=read;s->fence=fence;s->pack_key=key;s->user=user;s->records=records;s->capacity=capacity;
 std::memset(records,0,capacity*sizeof(*records));s->scratch.descriptors=scratch;s->scratch.capacity=capacity;
 // Natural renderer initialization may follow the loader's script-entry
 // installation seam. No native lock is touched until Enter validates both
 // the actual receiver and all completed native initialization markers.
 for(const auto& hook:hooks){unsigned char actual[16]={};if(!Read(s,base+hook.rva,actual,16)||std::memcmp(actual,hook.prefix,16))return false;
  s->originals[Index(hook.route)]=base+hook.rva;s->installed[Index(hook.route)]=Wrapper(hook.route);}
 s->validated=true;return true;
}
bool BindOriginal(State* s,Route route,std::uintptr_t original)noexcept{if(!s||!s->validated||s->sealed||Index(route)>=kRoutes||!original)return false;s->originals[Index(route)]=original;return true;}
bool Publish(State* s)noexcept{if(!s||!s->validated)return false;State* expected=nullptr;return bridge.compare_exchange_strong(expected,s)||expected==s;}
bool VerifyInstalled(State* s)noexcept{
 if(!s||!s->validated)return false;for(const auto& hook:hooks){unsigned char bytes[14]={};std::uintptr_t target=0;
  if(!Read(s,s->base+hook.rva,bytes,14)||bytes[0]!=0xff||bytes[1]!=0x25||bytes[2]||bytes[3]||bytes[4]||bytes[5])return false;
  std::memcpy(&target,bytes+6,8);if(target!=s->installed[Index(hook.route)])return false;}return true;
}
bool Seal(State* s)noexcept{if(!VerifyInstalled(s)||s->active.load()||s->pending.load())return false;s->sealed=true;return true;}
bool CopyEntryUnwind(State* s,Route route,unsigned char* output,std::size_t capacity,std::size_t* bytes)noexcept{
 if(bytes)*bytes=0;if(!s||!s->validated||Index(route)>=kRoutes||!output||!bytes)return false;
 std::uint32_t nt=0,directory=0,size=0;std::uint16_t magic=0;
 if(!Field(s,s->base,0x3C,&nt)||nt>0x100000||!Field(s,s->base,nt+24,&magic)||magic!=0x20B||
  !Field(s,s->base,nt+24+112+24,&directory)||!Field(s,s->base,nt+24+112+28,&size)||
  !directory||!size||size%12||size/12>65536||directory>0x48B000||size>0x48B000-directory)return false;
 struct Runtime{std::uint32_t begin,end,unwind;};Runtime entry={};const auto target=hooks[Index(route)].rva;
 std::uint32_t first=0,last=size/12;
 while(first<last){const auto middle=first+(last-first)/2;Runtime candidate={};
  if(!Read(s,s->base+directory+12ULL*middle,&candidate,sizeof(candidate)))return false;
  if(candidate.begin<target)first=middle+1;else last=middle;}
 // Tick's copied prologue crosses three contiguous native pdata ranges. Each
 // secondary range describes additional saves and chains to its predecessor.
 // Normalize those native actions to offsets from the original entry; do not
 // copy a chain containing RVAs relative to the renderer into the relay image.
 struct Action{unsigned char offset,slots,data[6];};Action actions[28]={};std::size_t action_count=0,code_count=0;
 Runtime previous={};const auto stolen=hooks[Index(route)].stolen;bool complete=false;
 for(unsigned segment=0;segment<4;++segment,++first){
  if(first==size/12||!Read(s,s->base+directory+12ULL*first,&entry,sizeof(entry))||
     entry.begin!=(segment?previous.end:target)||entry.end<=entry.begin||entry.end>0x48B000||entry.unwind>0x48B000-4)return false;
  unsigned char header[4]={},codes[56]={};
  if(!Read(s,s->base+entry.unwind,header,4)||header[0]!=(segment?0x21:1)||header[3]||header[2]>28)return false;
  const std::size_t native_bytes=((static_cast<std::size_t>(header[2])+1)&~std::size_t{1})*2;
  if(entry.unwind>0x48B000-4-native_bytes||
     (native_bytes&&!Read(s,s->base+entry.unwind+4,codes,native_bytes)))return false;
  if(segment){Runtime parent={};if(!Field(s,s->base,entry.unwind+4+native_bytes,&parent)||
     parent.begin!=previous.begin||parent.end!=previous.end||parent.unwind!=previous.unwind)return false;}
  unsigned previous_offset=256;
  for(std::size_t code=0;code<header[2];){
   const unsigned offset=codes[2*code],op=codes[2*code+1]&15,info=codes[2*code+1]>>4;
   if(offset>header[1]||offset>previous_offset)return false;previous_offset=offset;
   const unsigned slots=op==0||op==2?1:op==4?2:op==1&&info<=1?2+info:0;
   if(!slots||code+slots>header[2])return false;
   const auto absolute=entry.begin-target+offset;
   if(absolute<=stolen){
    if(action_count==28||code_count+slots>28)return false;
    Action action={};action.offset=static_cast<unsigned char>(absolute);action.slots=static_cast<unsigned char>(slots);
    std::memcpy(action.data,codes+2*code,2*slots);action.data[0]=action.offset;
    std::size_t position=action_count;while(position&&actions[position-1].offset<action.offset){actions[position]=actions[position-1];--position;}
    actions[position]=action;++action_count;code_count+=slots;
   }
   code+=slots;
  }
  if(entry.end>=target+stolen){complete=true;break;}previous=entry;
 }
 const std::size_t length=4+((code_count+1)&~std::size_t{1})*2;
 if(!complete||length>capacity)return false;
 std::memset(output,0,length);output[0]=1;output[1]=stolen;output[2]=static_cast<unsigned char>(code_count);
 std::size_t cursor=4;for(std::size_t i=0;i<action_count;++i){std::memcpy(output+cursor,actions[i].data,2*actions[i].slots);cursor+=2*actions[i].slots;}
 *bytes=length;return true;
}
bool Enter(State* s,std::uint32_t timeout)noexcept{
 return CloseEntries(s,timeout)&&EnterNative(s);
}
bool EntriesClosedAndIdle(State* s)noexcept{
 return s&&s->sealed&&!s->lost.load()&&s->owner_thread.load()==GetCurrentThreadId()&&!s->active.load()&&!s->pending.load()&&VerifyInstalled(s);
}
bool ReadGateRevision(State* s,std::uint64_t* out)noexcept{
 if(!out||!Gate(s))return false;*out=s->revision.load();return true;
}
bool CloseEntries(State* s,std::uint32_t timeout)noexcept{
 if(!s||!s->sealed||s->lost.load()||thread.depth||!VerifyInstalled(s))return false;
 const auto tid=GetCurrentThreadId();std::uint32_t zero=0;if(!s->owner_thread.compare_exchange_strong(zero,tid))return false;
 const auto started=GetTickCount64();while(s->active.load()){if(GetTickCount64()-started>=timeout){s->owner_thread.store(0);return false;}Sleep(1);}
 if(!EntriesClosedAndIdle(s)){Leave(s);return false;}return true;
}
bool EnterNative(State* s)noexcept{
 if(!EntriesClosedAndIdle(s)||!s->fence(s->user,&s->authority)||s->authority.thread_id!=GetCurrentThreadId()||!NativeOwner(s)){Leave(s);return false;}
 const auto lock=Kernel<TryLock>("TryEnterCriticalSection");if(!lock){Leave(s);return false;}
 if(!lock(reinterpret_cast<void*>(s->base+0x481E48))){Leave(s);return false;}s->main_owned=true;
 if(!lock(reinterpret_cast<void*>(s->base+0x481D90))){Leave(s);return false;}s->io_owned=true;
 if(!lock(reinterpret_cast<void*>(s->base+0x481DD8))){Leave(s);return false;}s->completion_owned=true;
 if(!Gate(s)){Leave(s);return false;}return true;
}
void Leave(State* s)noexcept{
 LeaveNative(s);OpenEntries(s);
}
void LeaveNative(State* s)noexcept{
 if(!s||s->owner_thread.load()!=GetCurrentThreadId())return;const auto unlock=Kernel<UnlockNative>("LeaveCriticalSection");
 if(unlock){if(s->completion_owned)unlock(reinterpret_cast<void*>(s->base+0x481DD8));if(s->io_owned)unlock(reinterpret_cast<void*>(s->base+0x481D90));if(s->main_owned)unlock(reinterpret_cast<void*>(s->base+0x481E48));}
 s->main_owned=s->io_owned=s->completion_owned=s->deleting_permitted=s->collecting_permitted=false;
}
void OpenEntries(State* s)noexcept{
 if(!s||s->owner_thread.load()!=GetCurrentThreadId()||s->main_owned||s->io_owned||s->completion_owned)return;
 s->authority={};s->owner_thread.store(0);
}
bool Capture(State* s,Snapshot* out)noexcept{
 if(!out)return false;out->complete=out->idle=false;out->count=0;
 if(!Gate(s)||!out->descriptors||!out->capacity||out->capacity>kMaximumDescriptors)return false;
 std::uint32_t count=0,capacity=0,full_capacity=0;unsigned char dump=0;
 if(!Field(s,s->base,0x481E00,&out->vector)||!Field(s,s->base,0x481E08,&count)||!Field(s,s->base,0x481E0C,&capacity)||
  count>capacity||count>out->capacity||(count&&!out->vector)||!Field(s,s->base,0x481E38,&out->request_head)||
  !Field(s,s->base,0x481E40,&out->request_count)||!Field(s,s->base,0x481D70,&out->io_head)||!Field(s,s->base,0x481D80,&out->io_count)||
  !Field(s,s->base,0x481DB8,&out->completion_head)||!Field(s,s->base,0x481DC8,&out->completion_count)||
  !Field(s,s->base,0x481E18,&out->full_vector)||!Field(s,s->base,0x481E20,&out->full_count)||!Field(s,s->base,0x481E24,&full_capacity)||
  out->full_count>full_capacity||out->full_count>kMaximumDescriptors||!Field(s,s->base,0x7CE28,&dump))return false;
 for(std::uint32_t i=0;i<count;++i){std::uintptr_t object=0;if(!Field(s,out->vector,8ULL*i,&object)||!Native(s,object,&out->descriptors[i]))return false;
  for(std::uint32_t j=0;j<i;++j)if(out->descriptors[j].object==object)return false;
  if(s->baseline_generation){const auto* record=Find(s,object);const auto& native=out->descriptors[i];
   if(!record||record->native.pack!=native.pack||record->native.entry!=native.entry||record->native.texture!=native.texture||
      ((record->life==Life::Deleted)!=(native.deleted!=0))){Lose(s);return false;}}
 }
 for(std::size_t i=0;i<s->count;++i){const auto& record=s->records[i];if(record.life!=Life::Live&&record.life!=Life::Deleted)continue;
  bool present=false;for(std::uint32_t j=0;j<count;++j)if(out->descriptors[j].object==record.native.object)present=true;
  if(!present){Lose(s);return false;}}
 out->count=count;out->revision=s->revision.load();out->complete=Gate(s);
 out->idle=out->complete&&!out->request_count&&!out->request_head&&!out->io_count&&!out->io_head&&
  !out->completion_count&&!out->completion_head&&!out->full_count&&!dump;
 return out->complete;
}
bool OwnsStreamRequest(State* s,std::uintptr_t owner,std::uintptr_t operation,std::uintptr_t pack,
 std::uintptr_t entry,std::uintptr_t destination,std::uintptr_t callback,std::uintptr_t user)noexcept{
 if(!s||!s->validated||s->lost.load()||thread.state!=s||!thread.depth||!thread.observing||thread.outer_route!=Route::Tick||
    callback!=s->base+0x18520||!operation||!pack||!entry||!destination||!user||!NativeOwner(s)||!VerifyInstalled(s))return false;
 std::uintptr_t actual_owner=0,queue=0,descriptor=0,job_operation=0,job_buffer=0;std::uint32_t flags=0,state=0,references=0;
 NativeDescriptor native={};packed::Key key={};
 if(!Field(s,s->engine,packed::windows::kRuntimeGlobalRva,&actual_owner)||actual_owner!=owner||
    !Field(s,pack,32,&flags)||(flags&0x80)||!Field(s,user,0,&queue)||queue!=s->base+0x481D70||
    !Field(s,user,8,&descriptor)||!Field(s,user,16,&job_operation)||job_operation!=operation||
    !Field(s,user,32,&job_buffer)||job_buffer!=destination||!Native(s,descriptor,&native)||
    native.job!=user||native.pack!=pack||native.entry!=entry||native.deleted||
    !Field(s,operation,0,&state)||state||!Field(s,operation,4,&references)||references!=1||
    !s->pack_key(s->user,pack,&key))return false;
 const auto* record=Find(s,descriptor);if(!record||record->life!=Life::Live||!Same(record->pack,key))return false;
 const auto enter=Kernel<TryLock>("TryEnterCriticalSection");const auto leave=Kernel<UnlockNative>("LeaveCriticalSection");
 if(!enter||!leave||!enter(reinterpret_cast<void*>(s->base+0x481D90)))return false;
 bool valid=true,found=false;std::uintptr_t link=0,tail=0,previous=0;std::uint32_t count=0;
 valid=Field(s,s->base,0x481D70,&link)&&Field(s,s->base,0x481D78,&tail)&&Field(s,s->base,0x481D80,&count)&&count&&count<=kMaximumDescriptors;
 for(std::uint32_t i=0;valid&&i<count;++i){std::uintptr_t next=0,back=0;
  valid=link&&Field(s,link,0,&next)&&Field(s,link,8,&back)&&back==previous;
  if(valid){found|=user<=UINTPTR_MAX-64&&link==user+64;previous=link;link=next;}}
 valid=valid&&found&&!link&&previous==tail&&!s->lost.load();leave(reinterpret_cast<void*>(s->base+0x481D90));return valid;
}
bool CaptureBaseline(State* s,const packed::Ledger& packs,std::uint64_t generation)noexcept{
 if(!s||!generation||s->baseline_generation||s->authority.epoch||s->authority.transaction||!Capture(s,&s->scratch))return false;
 auto* staged=static_cast<Record*>(VirtualAlloc(nullptr,s->capacity*sizeof(Record),0x3000,PAGE_READWRITE));if(!staged)return false;
 std::memcpy(staged,s->records,s->capacity*sizeof(Record));std::size_t count=s->count;auto next=s->next_generation;bool valid=true;
 for(std::size_t i=0;i<count;++i)if(staged[i].life==Life::Ended)staged[i]={};
 for(std::size_t i=0;i<s->scratch.count;++i){const auto& native=s->scratch.descriptors[i];packed::Key key={};
  if(native.deleted||packed::FindActive(&packs,native.pack,&key)!=packed::Result::Succeeded||packs.records[key.slot].project){valid=false;break;}
  Record* record=nullptr;
  for(std::size_t j=0;j<count;++j)if(staged[j].life!=Life::Empty&&staged[j].native.object==native.object)record=&staged[j];
  if(!record){std::size_t slot=0;while(slot<count&&staged[slot].life!=Life::Empty)++slot;
   if(slot==s->capacity||next==UINT64_MAX){valid=false;break;}record=&staged[slot];*record={};record->generation=++next;if(slot==count)++count;}
  if(record->pack.generation&&!Same(record->pack,key)){valid=false;break;}
  record->native=native;record->pack=key;record->life=Life::Live;record->baseline=true;
 }
 valid=valid&&Gate(s);if(valid){std::memcpy(s->records,staged,s->capacity*sizeof(Record));s->count=count;s->next_generation=next;s->baseline_generation=generation;}
 (void)VirtualFree(staged,0,0x8000);return valid;
}
bool QualifyPack(State* s,const packed::Ledger& ledger,const packed::NativePack& pack,const retained::NativeState* textures,std::size_t count)noexcept{
 if(!s||!s->baseline_generation||!Capture(s,&s->scratch)||!s->scratch.idle)return false;
 packed::Key key={};if(packed::FindActive(&ledger,pack.object,&key)!=packed::Result::Succeeded)return false;
 const auto& origin=ledger.records[key.slot];
 for(std::size_t i=0;i<s->count;++i){auto& record=s->records[i];if(record.life!=Life::Empty&&Same(record.pack,key)&&!CopySources(&record,origin))return false;}
 for(std::size_t i=0;i<s->scratch.count;++i){const auto& native=s->scratch.descriptors[i];if(native.pack!=pack.object)continue;
  auto* record=Find(s,native.object);if(!record||!Same(record->pack,key)||native.job||native.streaming||native.next_request||native.upload)return false;
  if(native.deleted){if(record->life!=Life::Deleted)return false;continue;}
  bool owned=false;for(std::size_t j=0;j<count;++j)if(textures[j].backend==native.texture&&textures[j].deferred==pack.object&&!textures[j].references)owned=true;
  if(!owned||record->baseline||!origin.project||origin.baseline||origin.global)return false;
 }
 return Gate(s);
}
bool SynchronizeOrigins(State* s,const packed::Ledger& ledger)noexcept{
 if(!Gate(s)||!Capture(s,&s->scratch)||!s->scratch.idle)return false;
 for(std::size_t i=0;i<s->count;++i){auto& record=s->records[i];if(record.life==Life::Empty)continue;
  if(!record.pack.generation||record.pack.slot>=ledger.count)return false;
  const auto& origin=ledger.records[record.pack.slot];
  if(origin.empty||!Same(origin.key,record.pack)||!CopySources(&record,origin))return false;
  if(record.life==Life::Live&&origin.destructor_returned)return false;
  if(record.life==Life::Ended)record.verified_transaction=s->authority.transaction;
 }
 return Gate(s);
}
bool PermitNativeTextureDeletion(State* s,bool entering)noexcept{
 if(!s)return false;if(!entering){const bool permitted=s->deleting_permitted;s->deleting_permitted=false;return permitted&&Gate(s);}
 if(!Gate(s)||s->deleting_permitted||!Capture(s,&s->scratch)||!s->scratch.idle)return false;
 s->deleting_permitted=true;return true;
}
bool CollectDeleted(State* s)noexcept{
 if(!Gate(s)||s->collecting_permitted||!Capture(s,&s->scratch)||!s->scratch.idle)return false;
 bool any=false;for(std::size_t i=0;i<s->scratch.count;++i){const auto& d=s->scratch.descriptors[i];
  if(d.job||d.streaming||d.next_request||d.upload)return false;any|=d.deleted!=0;}
 if(!any)return true;
 s->collecting_permitted=true;std::uint32_t bytes=0;
 __try{reinterpret_cast<void(__fastcall*)(std::uint32_t*)>(Wrapper(Route::Tick))(&bytes);}
 __finally{s->collecting_permitted=false;}
 if(bytes||!Capture(s,&s->scratch)||!s->scratch.idle)return false;
 for(std::size_t i=0;i<s->scratch.count;++i)if(s->scratch.descriptors[i].deleted)return false;
 return !s->lost.load();
}
namespace {
bool CanRetireSourceLocked(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s||!transaction||s->lost.load()||s->active.load()||s->pending.load()||!s->baseline_generation)return false;
 for(std::size_t i=0;i<s->count;++i){const auto& r=s->records[i];for(std::size_t j=0;j<r.source_count;++j)
  if(r.sources[j].address==source.address&&r.sources[j].generation==source.generation&&
   (r.life!=Life::Ended||r.verified_transaction!=transaction))return false;}return true;
}
}
bool CanRetireSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s||thread.depth||!TryObservation(s))return false;const bool valid=CanRetireSourceLocked(s,source,transaction);Unlock(s);return valid;
}
bool ReclaimSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s||thread.depth||!TryObservation(s))return false;if(!CanRetireSourceLocked(s,source,transaction)){Unlock(s);return false;}
 for(std::size_t i=0;i<s->count;++i){auto& r=s->records[i];if(r.life!=Life::Ended||r.baseline)continue;
  for(std::size_t j=r.source_count;j;--j)if(r.sources[j-1].address==source.address&&r.sources[j-1].generation==source.generation){
   for(std::size_t k=j;k<r.source_count;++k)r.sources[k-1]=r.sources[k];--r.source_count;}
  if(!r.source_count)r={};}
 s->revision.fetch_add(1);Unlock(s);return true;
}
} // namespace
