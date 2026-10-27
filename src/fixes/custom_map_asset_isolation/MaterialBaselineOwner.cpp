#include "MaterialBaselineOwner.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::material_baseline {
namespace g=resource_gateway;
namespace {
constexpr std::uintptr_t kManagerVtable=0x94AC48;
constexpr std::uintptr_t kMaterialVtable=0x94AB98;
struct ReleaseFrame {State* state;std::uintptr_t manager,slot,object;bool zero;};
struct Thread {State* state;Load* stack[16];std::uint32_t depth;ReleaseFrame releases[16];std::uint32_t release_depth;};
__declspec(thread) Thread tls={};
void Lock(State* s)noexcept{while(s->lock.test_and_set(std::memory_order_acquire)){} }
void Unlock(State* s)noexcept{s->lock.clear(std::memory_order_release);}
bool Read(State* s,std::uintptr_t p,void* out,std::size_t bytes)noexcept{
 return s&&s->read&&p&&bytes&&bytes-1<=UINTPTR_MAX-p&&s->read(s->user,p,out,bytes);
}
template<class T>bool Field(State* s,std::uintptr_t p,std::size_t offset,T* out)noexcept{
 return offset<=UINTPTR_MAX-p&&Read(s,p+offset,out,sizeof(*out));
}
bool Text(State* s,std::uintptr_t p,char* out)noexcept{
 std::memset(out,0,retained::kNameCapacity);if(!p)return true;
 for(std::size_t i=0;i<retained::kNameCapacity;++i){if(i>UINTPTR_MAX-p||!Read(s,p+i,out+i,1))return false;if(!out[i])return true;}return false;
}
void Number(Load& load,std::uint64_t value)noexcept{
 unsigned char bytes[8]={};for(unsigned i=0;i<8;++i)bytes[i]=static_cast<unsigned char>(value>>(8*i));dluma_sha256_update(&load.digest,bytes,8);
}
void String(Load& load,const char* text)noexcept{const auto length=std::strlen(text);Number(load,length);dluma_sha256_update(&load.digest,text,length);}
bool Bytes(State* s,Load& load,std::uintptr_t p,std::uint64_t count)noexcept{
 if(count>kMaximumConsumedBytes-load.bytes||(count&&!p))return false;
 unsigned char buffer[4096];std::uint64_t position=0;
 while(position<count){const auto size=static_cast<std::size_t>(count-position<sizeof(buffer)?count-position:sizeof(buffer));
  if(position>UINTPTR_MAX-p||!Read(s,p+static_cast<std::uintptr_t>(position),buffer,size))return false;
  dluma_sha256_update(&load.digest,buffer,size);position+=size;}
 load.bytes+=count;return true;
}
Load* Current(State* s)noexcept{return tls.state==s&&tls.depth?tls.stack[tls.depth-1]:nullptr;}
Container* Find(Load& load,std::uintptr_t container)noexcept{
 for(std::size_t i=load.containers;i;--i)if(load.inputs[i-1].object==container)return &load.inputs[i-1];return nullptr;
}
Load* FindLoad(State* s,std::uintptr_t pack)noexcept{
 Load* newest=nullptr;for(auto& load:s->loads)if(load.pack==pack&&!load.retired&&(!newest||load.generation>newest->generation))newest=&load;return newest;
}
bool Stable(const packed::Fence& a,const packed::Fence& b)noexcept{
 return a.transaction==b.transaction&&a.epoch==b.epoch&&a.owner_generation==b.owner_generation&&
 a.mutation_revision==b.mutation_revision&&a.thread_id==b.thread_id&&b.producers_closed&&b.consumers_drained&&
 b.renderer_lock_owned&&b.worker_lock_owned;
}
bool Fence(State* s,packed::Fence* out)noexcept{
 return s&&s->fence&&s->fence(s->user,out)&&out->thread_id==GetCurrentThreadId()&&out->owner_generation&&
 out->producers_closed&&out->consumers_drained&&out->renderer_lock_owned&&out->worker_lock_owned;
}
bool ExpectedBulk(State* s,std::uintptr_t container,unsigned index,std::uint64_t* bytes,std::uint32_t* entries)noexcept{
 *bytes=0;*entries=0;std::uintptr_t data=0,root=0;std::uint32_t count=0,expected=0;
 if(!Field(s,container,0,&data)||!Field(s,container,8,&count)||index>=count||count>256||
 !Field(s,data,88ULL*index+64,&root)||!Field(s,data,88ULL*index+72,&expected)||expected>65536)return false;
 std::uintptr_t stack[64]={};std::size_t depth=0;auto node=root;
 while(node||depth){while(node){if(depth==64)return false;stack[depth++]=node;if(!Read(s,node,&node,8))return false;}
  node=stack[--depth];if(node<48)return false;std::uint32_t length=0;
  if(!Read(s,node-36,&length,4)||length>kMaximumConsumedBytes-*bytes||*entries>=expected)return false;
  *bytes+=length;++*entries;if(!Field(s,node,16,&node))return false;}
 return *entries==expected;
}
bool Bulk(State* s,Load& load,const g::MaterialEvent& event)noexcept{
 std::uint64_t size=0;std::uint32_t count=0;
 if(!ExpectedBulk(s,event.owner,static_cast<unsigned>(event.arguments[0]),&size,&count))return false;
 std::uintptr_t entries=0;std::uint32_t actual=0,capacity=0;
 if(!Field(s,event.arguments[2],0,&entries)||!Field(s,event.arguments[2],8,&actual)||
 !Field(s,event.arguments[2],12,&capacity)||actual!=count||actual>capacity||(actual&&!entries))return false;
 Number(load,size);Number(load,count);
 // Native key/offset pairs are part of the consumed representation, separate
 // from the contiguous decoded payload. Capture before any caller can free it.
 return Bytes(s,load,entries,8ULL*count)&&Bytes(s,load,event.arguments[1],size);
}
void BeginLoad(State* s,const g::MaterialEvent& event)noexcept{
 if(tls.depth==16||(tls.depth&&tls.state!=s)){s->lost.store(true);return;}
 Load* record=nullptr;for(auto& load:s->loads)if(!load.pack||(load.retired&&!load.project)){record=&load;break;}
 if(!record||s->next_generation==UINT64_MAX){s->lost.store(true);return;}
 *record={};record->pack=event.owner;record->generation=++s->next_generation;record->thread=GetCurrentThreadId();record->active=true;
 g::Context context={};if(!s->gateway->callbacks.context(s->gateway->callbacks.user,&context)||
 !Text(s,event.arguments[0],record->path))record->failed=true;record->epoch=context.epoch;
 dluma_sha256_init(&record->digest);const char schema[]="DLW_MATERIAL_CONSUMED_INPUT_V1";
 dluma_sha256_update(&record->digest,schema,sizeof(schema));Number(*record,event.arguments[1]);String(*record,record->path);
 tls.state=s;tls.stack[tls.depth++]=record;
}
void FinishLoad(State* s,const g::MaterialEvent& event)noexcept{
 auto* load=Current(s);if(!load||load->pack!=event.owner){s->lost.store(true);return;}
 load->loaded=event.phase==g::EventPhase::Returned&&event.result!=0;
 Number(*load,load->loaded);Number(*load,load->containers);Number(*load,load->events);
 bool valid=!load->failed&&load->containers!=0;
 for(std::size_t i=0;i<load->containers;++i){auto& input=load->inputs[i];
  valid=valid&&!input.read_failed&&(!input.opened||input.provider_known);
  load->project|=input.realm==retained::Realm::Project;}
 dluma_sha256_final(&load->digest,load->sha256);load->complete=valid;load->active=false;
 --tls.depth;tls.stack[tls.depth]=nullptr;if(!tls.depth)tls.state=nullptr;
 if(s->baseline.captured)for(std::size_t i=0;i<s->baseline.pack_count;++i)
  if(i!=s->baseline.local_index&&event.owner==s->baseline.packs[i])s->global_pack_mutated.store(true);
}
}
void Observe(void* user,const g::MaterialEvent& event)noexcept{
 auto* s=static_cast<State*>(user);if(!s||!s->gateway||!s->read)return;Lock(s);
 if(event.route==g::Route::MaterialRelease&&s->baseline.captured){
  if(event.phase==g::EventPhase::Enter){
   if(tls.release_depth==16)s->lost.store(true);
   else tls.releases[tls.release_depth++]={s,event.owner,event.arguments[0],event.arguments[1],false};
  }else{
   if(!tls.release_depth)s->lost.store(true);
   else{const auto release=tls.releases[--tls.release_depth];tls.releases[tls.release_depth]={};std::uintptr_t current=1;
    if(release.state!=s||release.manager!=event.owner||release.slot!=event.arguments[0]||
     event.phase!=g::EventPhase::Returned||!Read(s,release.slot,&current,8)||current)s->lost.store(true);
    else if(release.zero)for(std::size_t i=0;i<s->baseline.count;++i)
     if(s->baseline.wrappers[i].object==release.object)s->baseline_wrapper_released[i]=true;
   }
  }Unlock(s);return;
 }
 if(event.route==g::Route::MaterialReleaseRef&&event.phase==g::EventPhase::Returned){
  if(tls.release_depth){auto& release=tls.releases[tls.release_depth-1];
   if(release.state==s&&release.object==event.owner)release.zero=event.result!=0;}
  Unlock(s);return;
 }
 if(event.route==g::Route::MaterialPackLoad){
  if(event.phase==g::EventPhase::Enter)BeginLoad(s,event);else FinishLoad(s,event);Unlock(s);return;}
 if(event.route==g::Route::MaterialPackDestroy&&event.phase==g::EventPhase::Returned){
  if(s->baseline.captured)for(std::size_t i=0;i<s->baseline.pack_count;++i)
   if(i!=s->baseline.local_index&&event.owner==s->baseline.packs[i])s->global_pack_mutated.store(true);
  for(auto& load:s->loads)if(load.pack==event.owner&&!load.active)load.retired=true;Unlock(s);return;}
 auto* load=Current(s);if(!load){Unlock(s);return;}
 if(event.phase==g::EventPhase::Unwound){load->failed=true;Unlock(s);return;}
 if(event.route==g::Route::MaterialFileRead){
  Container* input=event.owner>=40?Find(*load,event.owner-40):nullptr;
  if(!input||event.result!=event.arguments[1]){load->failed=true;if(input)input->read_failed=true;}
  Unlock(s);return;
 }
 if(event.route==g::Route::ContainerOpen&&event.phase==g::EventPhase::Enter){
  if(load->containers==kMaximumContainers){load->failed=true;Unlock(s);return;}
  auto& container=load->inputs[load->containers];container={};container.object=event.owner;container.ordinal=load->containers++;
  container.flags=static_cast<std::uint32_t>(event.arguments[1]);container.magic=static_cast<std::uint32_t>(event.arguments[2]);
  char name[retained::kNameCapacity]={};if(!Text(s,event.arguments[0],name))load->failed=true;
  Number(*load,static_cast<unsigned>(event.route));Number(*load,container.ordinal);Number(*load,container.flags);Number(*load,container.magic);String(*load,name);
  ++load->events;Unlock(s);return;
 }
 Container* container=Find(*load,event.owner);
 if(!container){Unlock(s);return;}
 if(event.phase!=g::EventPhase::Returned){Unlock(s);return;}
 Number(*load,static_cast<unsigned>(event.route));Number(*load,container->ordinal);Number(*load,event.result);++load->events;
 if(event.route==g::Route::ContainerOpen){
  container->opened=event.result!=0;
  if(container->opened&&!Bytes(s,*load,event.owner+24,16))load->failed=true;
 }else if(event.route==g::Route::ContainerFetch){
  Number(*load,event.arguments[0]);Number(*load,event.arguments[1]);
  if(event.result){std::uintptr_t data=0;std::uint32_t size=0;
   if(!Read(s,event.arguments[2],&data,8)||!Read(s,event.arguments[3],&size,4))load->failed=true;
   else{Number(*load,size);if(!Bytes(s,*load,data,size))load->failed=true;}}
 }else if(event.route==g::Route::ContainerLoadData){
  Number(*load,event.arguments[0]);if(event.result&&!Bulk(s,*load,event))load->failed=true;
 }else if(event.route==g::Route::ContainerDataSize)Number(*load,event.arguments[0]);
 Unlock(s);
}
bool ObserveFile(void* user,std::uintptr_t material,const file_lifetime::FileRecord& file,
 file_lifetime::SourceKey source,retained::Realm realm,std::uint32_t epoch)noexcept{
 auto* s=static_cast<State*>(user);if(!s)return false;Lock(s);auto* load=Current(s);
 if(!load||load->pack!=material||!file.file||!file.generation||file.life!=file_lifetime::Life::Live||
 !source.address||!source.generation||realm==retained::Realm::Unknown){if(load)load->failed=true;Unlock(s);return false;}
 auto container=g::CurrentOwner(s->gateway,g::Route::ContainerOpen);
 if(!container)container=g::CurrentOwner(s->gateway,g::Route::ContainerFetch);
 if(!container)container=g::CurrentOwner(s->gateway,g::Route::ContainerLoadData);
 auto* input=Find(*load,container);
 if(!input){load->failed=true;Unlock(s);return false;}
 if(input->provider_known&&(input->selected_source.address!=source.address||input->selected_source.generation!=source.generation)){
  input->read_failed=true;load->failed=true;Unlock(s);return false;}
 input->opened_file=file;input->selected_source=source;input->realm=realm;input->epoch=epoch;input->provider_known=true;
 load->project|=realm==retained::Realm::Project;Unlock(s);return true;
}
bool SameInput(const Load& a,const Load& b)noexcept{
 if(!a.complete||!b.complete||!a.loaded||!b.loaded||a.project||b.project||a.containers!=b.containers||a.bytes!=b.bytes||
 a.events!=b.events||std::memcmp(a.sha256,b.sha256,32)||std::memcmp(a.path,b.path,sizeof(a.path)))return false;
 for(std::size_t i=0;i<a.containers;++i){const auto& x=a.inputs[i];const auto& y=b.inputs[i];
  if(x.opened!=y.opened||x.flags!=y.flags||x.magic!=y.magic||x.provider_known!=y.provider_known||
   (x.opened&&(x.selected_source.address!=y.selected_source.address||x.selected_source.generation!=y.selected_source.generation)))return false;}
 return true;
}
Result Capture(State* s,std::uintptr_t manager,Snapshot* out)noexcept{
 if(!out)return Result::InvalidArgument;out->complete=false;out->count=0;
 if(!s||!s->engine_base||!out->wrappers||!out->capacity||out->capacity>kMaximumWrappers||!manager)return Result::InvalidArgument;
 packed::Fence before={},after={};if(!Fence(s,&before))return Result::Incomplete;
 std::uintptr_t vtable=0,packs=0,root=0;std::uint32_t count=0,pack_count=0;
 if(!Field(s,manager,0,&vtable)||vtable!=s->engine_base+kManagerVtable||!Field(s,manager,0x50,&packs)||
 !Field(s,manager,0x58,&pack_count)||pack_count>kMaximumRegisteredPacks||!Field(s,manager,0x68,&out->global_pack)||
 !Field(s,manager,0x70,&out->local_pack)||!Field(s,manager,0x78,&root)||!Field(s,manager,0x80,&count)||count>out->capacity)return Result::Incomplete;
 out->pack_count=pack_count;out->local_index=kMaximumRegisteredPacks;
 for(std::size_t i=0;i<pack_count;++i){if(!Field(s,packs,8*i,&out->packs[i])||!out->packs[i])return Result::Incomplete;
  for(std::size_t j=0;j<i;++j)if(out->packs[j]==out->packs[i])return Result::Incomplete;
  if(out->packs[i]==out->local_pack){if(out->local_index!=kMaximumRegisteredPacks)return Result::Incomplete;out->local_index=i;}}
 if(out->local_pack&&out->local_index==kMaximumRegisteredPacks)return Result::Incomplete;
 std::uintptr_t stack[64]={};std::size_t depth=0;auto node=root;
 while(node||depth){while(node){if(depth==64)return Result::Capacity;stack[depth++]=node;if(!Read(s,node,&node,8))return Result::Incomplete;}
  node=stack[--depth];if(node<16||out->count==count)return Result::Incomplete;
  auto& w=out->wrappers[out->count];w={};std::uintptr_t definition=0,name=0,variant=0,material_vtable=0,backlink=0;std::uint32_t native_key=0;
  if(!Read(s,node-16,&w.key,4)||!Read(s,node-8,&w.object,8)||!Field(s,w.object,8,&w.record)||
   !Field(s,w.object,0,&material_vtable)||material_vtable!=s->engine_base+kMaterialVtable||
   !Field(s,w.record,16,&backlink)||backlink!=w.object||!Field(s,w.record,36,&native_key)||native_key!=w.key||
   !Field(s,w.record,0,&definition)||!Field(s,w.record,8,&variant)||!Field(s,variant,16,&w.variant)||
   !Field(s,w.record,24,&w.owner_pack)||!Field(s,w.record,32,&w.flags)||!Field(s,w.record,40,&w.references)||
   !Field(s,w.record,56,&name))return Result::Incomplete;
  char first=0;if(name&&!Read(s,name,&first,1))return Result::Incomplete;
  if(!name||!first)if(!Read(s,definition,&name,8))return Result::Incomplete;
  if(!Text(s,name,w.name))return Result::Incomplete;++out->count;
  if(!Field(s,node,16,&node))return Result::Incomplete;
 }
 if(out->count!=count||!Fence(s,&after)||!Stable(before,after))return Result::Incomplete;
 out->manager=manager;out->fence=after;out->complete=true;return Result::Succeeded;
}
Result CaptureBaseline(State* s,const Snapshot& current,std::uint64_t generation)noexcept{
 if(!s||s->baseline.captured||s->lost.load()||!generation||!current.complete||current.fence.epoch||current.fence.transaction||
 !s->baseline.wrappers||current.count>s->baseline.capacity)return Result::InvalidArgument;
 packed::Fence now={};if(!Fence(s,&now)||!Stable(current.fence,now))return Result::Incomplete;
 Lock(s);Load* input=current.local_pack?FindLoad(s,current.local_pack):nullptr;
 if(current.local_pack&&(!input||!input->complete||!input->loaded||input->project||input->active)){Unlock(s);return Result::BaselineUnavailable;}
 if(input)s->baseline.input=*input;
 s->baseline.manager=current.manager;s->baseline.global_pack=current.global_pack;s->baseline.count=current.count;
 s->baseline.pack_count=current.pack_count;s->baseline.local_index=current.local_index;s->baseline.generation=generation;
 s->baseline.local_absent=!current.local_pack;
 std::memcpy(s->baseline.packs,current.packs,sizeof(current.packs));
 std::memcpy(s->baseline.wrappers,current.wrappers,current.count*sizeof(Wrapper));s->baseline.captured=true;Unlock(s);return Result::Succeeded;
}
Result Preflight(State* s,const Snapshot& current)noexcept{
 if(!s||!s->baseline.captured)return Result::BaselineUnavailable;
 if(!current.complete||s->lost.load()||s->global_pack_mutated.load()||current.manager!=s->baseline.manager||
 current.global_pack!=s->baseline.global_pack)return Result::BaselineChanged;
 const auto expected=s->baseline.pack_count+(s->baseline.local_absent&&current.local_pack?1:0);
 if(current.pack_count!=expected)return Result::BaselineChanged;
 std::size_t baseline_index=0;
 for(std::size_t i=0;i<current.pack_count;++i){if(i==current.local_index)continue;
  if(baseline_index==s->baseline.local_index)++baseline_index;
  if(baseline_index>=s->baseline.pack_count||current.packs[i]!=s->baseline.packs[baseline_index++])
   return Result::BaselineChanged;}
 for(std::size_t i=0;i<current.count;++i){const auto& w=current.wrappers[i];
  if(w.owner_pack!=current.local_pack)continue;
  bool known=false;for(std::size_t j=0;j<s->baseline.count;++j){const auto& old=s->baseline.wrappers[j];
   if(old.key==w.key&&old.variant==w.variant&&!std::memcmp(old.name,w.name,sizeof(w.name)))known=true;}
  if(!known)return Result::UnclosedConsumer;
 }
 return Result::Succeeded;
}
Result Restore(State* s,Snapshot* scratch,const Operations& operations)noexcept{
 if(!s||!scratch||!operations.replay||!operations.original_reload)return Result::InvalidArgument;
 auto result=Capture(s,s->baseline.manager,scratch);if(result!=Result::Succeeded)return result;
 result=Preflight(s,*scratch);if(result!=Result::Succeeded)return result;
 if(!operations.replay(operations.user,s->baseline,true))return Result::ReplayUnavailable;
 bool replay_closed=false;
 __try { operations.original_reload(reinterpret_cast<void*>(s->baseline.manager)); }
 __finally { replay_closed=operations.replay(operations.user,s->baseline,false); }
 if(!replay_closed)return Result::NativeReloadFailed;
 result=Capture(s,s->baseline.manager,scratch);if(result!=Result::Succeeded)return result;
 if(scratch->global_pack!=s->baseline.global_pack||scratch->pack_count!=s->baseline.pack_count||
 (s->baseline.local_absent?scratch->local_pack!=0:!scratch->local_pack)||
 scratch->local_index!=s->baseline.local_index)return Result::VerificationFailed;
 for(std::size_t i=0;i<scratch->pack_count;++i)
  if(i!=s->baseline.local_index&&scratch->packs[i]!=s->baseline.packs[i])return Result::VerificationFailed;
 Lock(s);Load* input=scratch->local_pack?FindLoad(s,scratch->local_pack):nullptr;
 const bool same=s->baseline.local_absent||(input&&SameInput(s->baseline.input,*input));Unlock(s);
 if(!same)return Result::VerificationFailed;
 for(std::size_t i=0;i<s->baseline.count;++i){const auto& old=s->baseline.wrappers[i];bool found=false;
  if(s->baseline_wrapper_released[i])continue;
  for(std::size_t j=0;j<scratch->count;++j){const auto& w=scratch->wrappers[j];
   if(old.object==w.object&&old.key==w.key&&old.variant==w.variant&&!std::memcmp(old.name,w.name,sizeof(w.name))&&
    w.owner_pack==(old.owner_pack==s->baseline.global_pack?s->baseline.global_pack:scratch->local_pack))found=true;}
  if(!found)return Result::VerificationFailed;
 }
 return Result::Succeeded;
}
Result PreflightAbsentLocal(State* s,const Snapshot& current)noexcept{
 if(!s||!s->baseline.captured||!s->baseline.local_absent)return Result::BaselineUnavailable;
 const auto result=Preflight(s,current);if(result!=Result::Succeeded)return result;
 packed::Fence now={};if(!Fence(s,&now)||!Stable(current.fence,now)||!now.transaction||!now.epoch||!now.authoritative_post_destroy)return Result::Incomplete;
 for(std::size_t i=0;i<current.count;++i){const auto& wrapper=current.wrappers[i];
  if(wrapper.owner_pack==current.local_pack)return Result::UnclosedConsumer;
  bool known=false;for(std::size_t j=0;j<s->baseline.pack_count;++j)known|=wrapper.owner_pack==s->baseline.packs[j];
  if(!known)return Result::UnclosedConsumer;
 }
 for(std::size_t i=0;i<s->baseline.count;++i){if(s->baseline_wrapper_released[i])continue;const auto& old=s->baseline.wrappers[i];bool present=false;
  for(std::size_t j=0;j<current.count;++j){const auto& w=current.wrappers[j];
   present|=old.object==w.object&&old.key==w.key&&old.variant==w.variant&&old.owner_pack==w.owner_pack&&!std::memcmp(old.name,w.name,sizeof(w.name));}
  if(!present)return Result::BaselineChanged;
 }
 Lock(s);bool valid=true;const auto* local=current.local_pack?FindLoad(s,current.local_pack):nullptr;
 if(current.local_pack)valid=local&&local->complete&&local->loaded&&local->project&&!local->active&&local->epoch==now.epoch;
 for(const auto& load:s->loads)if(load.pack&&!load.retired&&load.project&&load.pack!=current.local_pack)valid=false;
 Unlock(s);return valid?Result::Succeeded:Result::UnknownInput;
}
bool MatchUnborrowedLocal(State* s,const Snapshot& current,std::uintptr_t pack,std::uint64_t generation)noexcept{
 if(!pack||current.local_pack!=pack||PreflightAbsentLocal(s,current)!=Result::Succeeded)return false;
 Lock(s);const auto* load=FindLoad(s,pack);const bool matches=load&&load->generation==generation;Unlock(s);return matches;
}
Result ResetAbsentLocal(State* s,Snapshot* scratch,UnloadLocalFn unload,void* user)noexcept{
 if(!s||!scratch||!unload)return Result::InvalidArgument;
 auto result=Capture(s,s->baseline.manager,scratch);if(result!=Result::Succeeded)return result;
 result=PreflightAbsentLocal(s,*scratch);if(result!=Result::Succeeded)return result;
 const auto fence=scratch->fence;const auto pack=scratch->local_pack;const auto count=scratch->count;
 unsigned char before[32]={},after[32]={};DlumaSha256Context hash={};
 dluma_sha256_init(&hash);dluma_sha256_update(&hash,scratch->wrappers,count*sizeof(Wrapper));dluma_sha256_final(&hash,before);
 if(pack){Lock(s);const auto* load=FindLoad(s,pack);const auto generation=load?load->generation:0;Unlock(s);
  if(!generation||!unload(user,scratch->manager,pack,generation))return Result::NativeReloadFailed;
  Lock(s);bool destroyed=false;for(const auto& ended:s->loads)
   if(ended.pack==pack&&ended.generation==generation&&ended.retired&&!ended.active)destroyed=true;Unlock(s);
  if(!destroyed)return Result::VerificationFailed;
 }
 result=Capture(s,s->baseline.manager,scratch);if(result!=Result::Succeeded)return result;
 if(scratch->local_pack||scratch->count!=count||scratch->fence.transaction!=fence.transaction||scratch->fence.epoch!=fence.epoch||
    scratch->fence.owner_generation!=fence.owner_generation)return Result::VerificationFailed;
 dluma_sha256_init(&hash);dluma_sha256_update(&hash,scratch->wrappers,count*sizeof(Wrapper));dluma_sha256_final(&hash,after);
 if(std::memcmp(before,after,32)||PreflightAbsentLocal(s,*scratch)!=Result::Succeeded)return Result::VerificationFailed;
 s->absent_reset_transaction=fence.transaction;s->absent_reset_epoch=fence.epoch;return Result::Succeeded;
}
Result VerifyAbsentLocal(State* s,const Snapshot& current)noexcept{
 if(!s||current.local_pack||s->absent_reset_transaction!=current.fence.transaction||s->absent_reset_epoch!=current.fence.epoch)
  return Result::VerificationFailed;
 return PreflightAbsentLocal(s,current);
}
namespace {
bool CanRetireSourceLocked(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s||!source.address||!source.generation||!transaction||s->lost.load()||s->global_pack_mutated.load())return false;
 for(const auto& load:s->loads)for(std::size_t i=0;i<load.containers;++i){const auto& input=load.inputs[i];
  if(input.provider_known&&input.selected_source.address==source.address&&input.selected_source.generation==source.generation&&
     (!load.project||load.active||!load.retired||s->absent_reset_transaction!=transaction))return false;
 }return true;
}
}
bool CanRetireSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s)return false;Lock(s);const bool valid=CanRetireSourceLocked(s,source,transaction);Unlock(s);return valid;
}
bool ReclaimRetiredSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 if(!s)return false;Lock(s);if(!CanRetireSourceLocked(s,source,transaction)){Unlock(s);return false;}
 for(auto& load:s->loads){if(!load.retired||!load.project)continue;
  bool remaining=false;for(std::size_t i=0;i<load.containers;++i){auto& input=load.inputs[i];
   if(input.selected_source.address==source.address&&input.selected_source.generation==source.generation)input.provider_known=false;
   remaining|=input.realm==retained::Realm::Project&&input.provider_known;}
  if(!remaining)load={};
 }Unlock(s);return true;
}
} // namespace
