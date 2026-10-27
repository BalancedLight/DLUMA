#include "NativeResourceProduction.h"
#include <cstring>
extern "C" __declspec(dllimport) LPVOID WINAPI VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);
extern "C" __declspec(dllimport) BOOL WINAPI VirtualFree(LPVOID,SIZE_T,DWORD);

namespace dluma::fixes::custom_map_asset_isolation::resource_production {
namespace {
using g=resource_gateway::State;
constexpr std::size_t kTextureCount=retained::kMaximumResources,kPackCount=packed::kMaximumPacks;
constexpr std::size_t kStorageBytes=kTextureCount*(sizeof(retained::Record)+2*sizeof(retained::NativeState)+sizeof(retained::Identity))+
 kPackCount*(sizeof(packed::Origin)+sizeof(packed::NativePack)+sizeof(packed::PlanItem))+
 2*material_baseline::kMaximumWrappers*sizeof(material_baseline::Wrapper)+
 kTextureCount*(sizeof(renderer_streaming::Record)+sizeof(renderer_streaming::NativeDescriptor))+128;
template<class T> T* Take(unsigned char*& cursor,std::size_t count)noexcept{
 const auto address=(reinterpret_cast<std::uintptr_t>(cursor)+alignof(T)-1)&~std::uintptr_t(alignof(T)-1);
 auto* out=reinterpret_cast<T*>(address);cursor=reinterpret_cast<unsigned char*>(out+count);return out;
}
bool Read(void* user,std::uintptr_t address,void* output,std::size_t bytes)noexcept{
 auto* s=static_cast<State*>(user);return s->upstream.read&&s->upstream.read(s->upstream.user,address,output,bytes);
}
bool Context(void* user,resource_gateway::Context* out)noexcept{
 auto* s=static_cast<State*>(user);return s->upstream.context&&s->upstream.context(s->upstream.user,out);
}
void Potential(void* user,resource_gateway::Route route)noexcept{
 auto* s=static_cast<State*>(user);if(s->upstream.potential)s->upstream.potential(s->upstream.user,route);
}
bool Replay(void* user,resource_gateway::Route route,std::uintptr_t owner)noexcept{
 auto* s=static_cast<State*>(user);
 if(route==resource_gateway::Route::MaterialPackDestroy&&owner==s->material_retirement_pack&&
    s->material_retirement_generation&&s->material_retirement_thread==GetCurrentThreadId()&&
    s->gateway.held.load()&&s->gateway.owner_thread.load()==GetCurrentThreadId())return true;
 return s->upstream.replay&&s->upstream.replay(s->upstream.user,route,owner);
}
bool Pin(void* user,resource_gateway::InputKey input,const file_lifetime::FileRecord& file)noexcept{
 auto* s=static_cast<State*>(user);
 return s->upstream.pin_input?s->upstream.pin_input(s->upstream.user,input,file):resource_input_capture::Queue(&s->capture,input,file);
}
void Material(void* user,const resource_gateway::MaterialEvent& event)noexcept{
 material_baseline::Observe(&static_cast<State*>(user)->material,event);
}
bool MaterialFile(void* user,std::uintptr_t material,const file_lifetime::FileRecord& file,
 file_lifetime::SourceKey source,retained::Realm realm,std::uint32_t epoch)noexcept{
 return material_baseline::ObserveFile(&static_cast<State*>(user)->material,material,file,source,realm,epoch);
}
bool Revision(void* user,std::uint64_t* out)noexcept{
 auto* s=static_cast<State*>(user);return s->upstream.source_revision&&s->upstream.source_revision(s->upstream.user,out);
}
bool Classify(void* user,file_lifetime::SourceKey source,retained::Realm* realm,std::uint32_t* epoch)noexcept{
 auto* s=static_cast<State*>(user);return s->upstream.classify_source&&s->upstream.classify_source(s->upstream.user,source,realm,epoch);
}
void FullPath(void* user,resource_gateway::EventPhase phase,bool resource_owned,std::uintptr_t result,
 const resource_provider::Receipt* receipt)noexcept{
 auto* s=static_cast<State*>(user);if(s->upstream.full_path)s->upstream.full_path(s->upstream.user,phase,resource_owned,result,receipt);
}
bool RendererFence(void* user,packed::Fence* out)noexcept{
 return resource_gateway::CaptureProducerFence(&static_cast<State*>(user)->gateway,out);
}
bool RendererPackKey(void* user,std::uintptr_t object,packed::Key* out)noexcept{
 return resource_gateway::SnapshotPackKey(&static_cast<State*>(user)->gateway,object,out);
}
bool RendererStreamRequest(void* user,std::uintptr_t owner,std::uintptr_t operation,std::uintptr_t pack,
 std::uintptr_t entry,std::uintptr_t destination,std::uintptr_t callback,std::uintptr_t job)noexcept{
 return renderer_streaming::OwnsStreamRequest(&static_cast<State*>(user)->renderer,owner,operation,pack,entry,destination,callback,job);
}
bool CombinedFence(void* user,packed::Fence* out)noexcept{
 auto* s=static_cast<State*>(user);if(!RendererFence(user,out)||!renderer_streaming::EntriesClosedAndIdle(&s->renderer))return false;
 const auto revision=s->renderer.revision.load();if(out->mutation_revision>UINT64_MAX-revision)return false;
 out->mutation_revision+=revision;return true;
}
bool AuthorizePack(void* user,packed::windows::NativeOperation operation,packed::Key key,bool entering)noexcept{
 return resource_gateway::AuthorizeNativeOperation(&static_cast<State*>(user)->gateway,operation,key,entering);
}
bool StreamProof(void* user,const packed::NativePack& pack,const retained::NativeState* textures,std::size_t count)noexcept{
 auto* s=static_cast<State*>(user);return renderer_streaming::QualifyPack(&s->renderer,s->packs,pack,textures,count);
}
bool StreamMutation(void* user,bool entering,bool returned)noexcept{
 auto* s=static_cast<State*>(user);
 if(entering)return renderer_streaming::PermitNativeTextureDeletion(&s->renderer,true);
 const bool closed=renderer_streaming::PermitNativeTextureDeletion(&s->renderer,false);
 if(!returned){s->renderer.lost.store(true);return false;}
 return closed&&renderer_streaming::CollectDeleted(&s->renderer)&&renderer_streaming::SynchronizeOrigins(&s->renderer,s->packs);
}
bool TextureFence(void* user,retained::Fence* out)noexcept{
 auto* s=static_cast<State*>(user);packed::Fence fence={};
 if(!out||!packed::windows::ReadFence(&s->pack_native,&fence))return false;
 *out={fence.transaction,fence.owner_generation,fence.mutation_revision,fence.epoch,fence.thread_id,
  fence.authoritative_post_destroy,fence.producers_closed,fence.consumers_drained,false,false};
 if(!fence.transaction){out->worker_completions_drained=true;return true;}
 file_lifetime::OutstandingWork work={};
 if(!file_lifetime::SnapshotOutstandingWork(&s->gateway.files->ledger,&work)||!work.reads_drained)return false;
 out->worker_completions_drained=true;return true;
}
bool MaterialFence(void* user,packed::Fence* out)noexcept{
 return packed::windows::ReadFence(&static_cast<State*>(user)->pack_native,out);
}
void Leave(State* s)noexcept{
 renderer_streaming::LeaveNative(&s->renderer);
 retained::windows::LeaveTextureGate(&s->texture_native);packed::windows::LeaveGate(&s->pack_native);
 if(s->file_admission_owned){(void)file_lifetime::ReopenAdmission(&s->gateway.files->ledger,s->gate.transaction);s->file_admission_owned=false;}
 if(s->gate.generation){(void)resource_gateway::Open(&s->gateway,s->gate);s->gate={};}
 renderer_streaming::OpenEntries(&s->renderer);
}
bool Enter(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!s->available||s->gate.generation||resource_input_capture::Outstanding(&s->capture))return false;
 if(!renderer_streaming::CloseEntries(&s->renderer,50))return false;
 if(!resource_gateway::Close(&s->gateway,50,&s->gate)){Leave(s);return false;}
 if(s->gate.transaction!=transaction||s->gate.epoch!=epoch){Leave(s);return false;}
 if(packed::windows::EnterGate(&s->pack_native)!=packed::Result::Succeeded||
  retained::windows::EnterTextureGate(&s->texture_native)!=retained::Result::Succeeded||
  !renderer_streaming::EnterNative(&s->renderer)){Leave(s);return false;}
 // A queued renderer completion may still need its native file open. Prove
 // that work absent before closing file admission; never make it fail merely
 // to reach an apparently idle cleanup boundary.
 if(transaction&&!renderer_streaming::SynchronizeOrigins(&s->renderer,s->packs)){Leave(s);return false;}
 if(transaction){
  file_lifetime::OutstandingWork before={};
  if(!file_lifetime::SnapshotOutstandingWork(&s->gateway.files->ledger,&before)||
   !file_lifetime::CloseAdmission(&s->gateway.files->ledger,transaction)){Leave(s);return false;}
  s->file_admission_owned=!before.admission_closed;
  (void)file_lifetime::DrainCompletedControls(&s->gateway.files->ledger);
 }
 return true;
}
bool Capture(State* s)noexcept{
 return packed::windows::Capture(&s->pack_native,&s->pack_snapshot)==packed::Result::Succeeded&&
  retained::windows::CaptureTextures(&s->texture_native,&s->texture_snapshot)==retained::Result::Succeeded;
}
bool UnloadUnborrowedLocal(void* user,std::uintptr_t manager,std::uintptr_t pack,std::uint64_t generation)noexcept{
 auto* s=static_cast<State*>(user);
 std::uint64_t renderer_revision=0;
 if(!s||s->material_retirement_pack||!renderer_streaming::ReadGateRevision(&s->renderer,&renderer_revision)||
    material_baseline::Capture(&s->material,manager,&s->material_snapshot)!=material_baseline::Result::Succeeded||
    !material_baseline::MatchUnborrowedLocal(&s->material,s->material_snapshot,pack,generation))return false;
 std::uintptr_t actual_manager=0,vtable=0,unload=0,current=0;
 const auto base=s->gateway.engine_base;
 if(!Read(s,base+0xACD118,&actual_manager,8)||actual_manager!=manager||!Read(s,manager,&vtable,8)||vtable!=base+0x94AC48||
    !Read(s,vtable+8,&unload,8)||unload!=base+0x8053E0||!Read(s,manager+112,&current,8)||current!=pack)return false;
 s->material_retirement_pack=pack;s->material_retirement_generation=generation;s->material_retirement_thread=GetCurrentThreadId();
 bool returned=false;
 __try{reinterpret_cast<void(__fastcall*)(void*,void*,int)>(unload)(reinterpret_cast<void*>(manager),reinterpret_cast<void*>(pack),0);returned=true;}
 __finally{s->material_retirement_pack=0;s->material_retirement_generation=0;s->material_retirement_thread=0;}
 if(!returned||!Read(s,manager+112,&current,8)||current!=pack){s->material.lost.store(true);return false;}
 // The native combined reload clears this exact ownership slot after deleting
 // the old registered pack. There are no wrappers referencing it in this
 // branch, and all manager/renderer creators remain closed throughout.
 *reinterpret_cast<std::uintptr_t*>(manager+112)=0;
 return !s->gateway.lost.load()&&!s->material.lost.load();
}
}
bool Initialize(State* s,std::uintptr_t engine,std::uintptr_t filesystem,const char* engine_sha,const char* fs_sha,
 const resource_gateway::Callbacks& callbacks,file_lifetime::bridge::State* files,audio::AudioFullPathProbeState* probe,
 std::uintptr_t probe_target,std::uintptr_t renderer,const char* renderer_sha)noexcept{
 if(!s||s->initialized||s->storage||!files||!engine_sha||!callbacks.read||!callbacks.context||!callbacks.potential||
  !callbacks.source_revision||!callbacks.classify_source||!renderer_sha)return false;
 s->storage=VirtualAlloc(nullptr,kStorageBytes,0x3000,PAGE_READWRITE);if(!s->storage)return false;
 auto* cursor=static_cast<unsigned char*>(s->storage);
 retained::Initialize(&s->textures,Take<retained::Record>(cursor,kTextureCount),kTextureCount);
 packed::Initialize(&s->packs,Take<packed::Origin>(cursor,kPackCount),kPackCount);
 s->texture_snapshot.objects=Take<retained::NativeState>(cursor,kTextureCount);s->texture_snapshot.capacity=kTextureCount;
 s->texture_plan.resources=Take<retained::Identity>(cursor,kTextureCount);s->texture_plan.capacity=kTextureCount;
 s->pack_snapshot.packs=Take<packed::NativePack>(cursor,kPackCount);s->pack_snapshot.pack_capacity=kPackCount;
 s->pack_snapshot.textures=Take<retained::NativeState>(cursor,kTextureCount);s->pack_snapshot.texture_capacity=kTextureCount;
 s->pack_plan.items=Take<packed::PlanItem>(cursor,kPackCount);s->pack_plan.capacity=kPackCount;
 s->material.baseline.wrappers=Take<material_baseline::Wrapper>(cursor,material_baseline::kMaximumWrappers);
 s->material.baseline.capacity=material_baseline::kMaximumWrappers;
 s->material_snapshot.wrappers=Take<material_baseline::Wrapper>(cursor,material_baseline::kMaximumWrappers);
 s->material_snapshot.capacity=material_baseline::kMaximumWrappers;s->upstream=callbacks;
 s->renderer_records=Take<renderer_streaming::Record>(cursor,kTextureCount);
 s->renderer_scratch=Take<renderer_streaming::NativeDescriptor>(cursor,kTextureCount);
 const resource_gateway::Callbacks actual={s,Read,Context,Potential,Replay,Pin,Material,MaterialFile,Revision,Classify,FullPath,RendererStreamRequest};
 bool ok=resource_gateway::Initialize(&s->gateway,engine,filesystem,engine_sha,fs_sha,actual,files,&s->packs,&s->textures,probe,probe_target);
 s->pack_native.engine_base=s->texture_native.engine_base=engine;
 std::memcpy(s->pack_native.engine_sha256,engine_sha,65);std::memcpy(s->texture_native.engine_sha256,engine_sha,65);
 s->pack_native.read=Read;s->pack_native.read_user=s;s->pack_native.producer_fence=CombinedFence;
 s->pack_native.authorize=AuthorizePack;s->pack_native.gateway_user=s;
 s->pack_native.stream_proof=StreamProof;s->pack_native.stream_mutation=StreamMutation;s->pack_native.stream_user=s;
 s->pack_native.ledger=&s->packs;s->pack_native.texture_ledger=&s->textures;
 s->texture_native.read=Read;s->texture_native.read_user=s;s->texture_native.fence=TextureFence;
 s->texture_native.fence_user=s;s->texture_native.ledger=&s->textures;
 s->material.gateway=&s->gateway;s->material.engine_base=engine;s->material.read=Read;s->material.fence=MaterialFence;s->material.user=s;
 ok=ok&&packed::windows::Validate(&s->pack_native)==packed::Result::Succeeded&&
  retained::windows::Validate(&s->texture_native)==retained::Result::Succeeded;
 if(!ok)return false;
 s->initialized=true;return !renderer||InitializeRenderer(s,renderer,renderer_sha);
}
bool InitializeRenderer(State* s,std::uintptr_t renderer,const char* renderer_sha)noexcept{
 if(!s||!s->initialized||s->renderer.validated||!renderer||!renderer_sha)return false;
 return renderer_streaming::Initialize(&s->renderer,renderer,s->gateway.engine_base,renderer_sha,Read,RendererFence,RendererPackKey,s,
     s->renderer_records,kTextureCount,s->renderer_scratch);
}
bool StartInputCapture(State* s)noexcept{
 return s&&s->initialized&&resource_input_capture::Start(&s->capture,&s->gateway,s->gateway.files,512ULL*1024*1024);
}
bool SealEngine(State* s)noexcept{
 if(!s||!s->initialized||!s->capture.accepting.load()||!resource_gateway::SealInstalled(&s->gateway))return false;
 s->pack_native.original_evict=reinterpret_cast<packed::windows::EvictPackFn>(resource_gateway::Wrapper(resource_gateway::Route::DemandEvict));
 s->pack_native.original_release_reference=reinterpret_cast<packed::windows::ReleaseReferenceFn>(resource_gateway::Wrapper(resource_gateway::Route::AssetRelease));
 s->pack_native.original_release_pack=reinterpret_cast<packed::windows::ReleasePackFn>(resource_gateway::Wrapper(resource_gateway::Route::PackRelease));
 s->texture_native.original_destroy=reinterpret_cast<retained::windows::DestroyTextureFn>(s->gateway.originals[static_cast<unsigned>(resource_gateway::Route::TextureDestroy)]);
 return true;
}
bool Seal(State* s)noexcept{
 if(!SealEngine(s)||!renderer_streaming::Seal(&s->renderer))return false;
 s->available=true;return true;
}
bool CaptureBaseline(State* s,std::uint64_t generation)noexcept{
 if(!s||!generation)return false;if(s->baseline_generation)return s->baseline_generation==generation;
 if(!Enter(s,0,0))return false;bool ok=false;void* staged_storage=nullptr;
 __try{
  if(renderer_streaming::CollectDeleted(&s->renderer)&&Capture(s)){
   const auto texture_bytes=s->textures.capacity*sizeof(retained::Record),pack_bytes=s->packs.capacity*sizeof(packed::Origin);
   staged_storage=VirtualAlloc(nullptr,texture_bytes+pack_bytes+16,0x3000,PAGE_READWRITE);
   if(staged_storage){
    auto* cursor=static_cast<unsigned char*>(staged_storage);auto staged_textures=s->textures;auto staged_packs=s->packs;
    staged_textures.records=Take<retained::Record>(cursor,s->textures.capacity);staged_packs.records=Take<packed::Origin>(cursor,s->packs.capacity);
    std::memcpy(staged_textures.records,s->textures.records,texture_bytes);std::memcpy(staged_packs.records,s->packs.records,pack_bytes);
    ok=retained::CaptureBaseline(&staged_textures,s->texture_snapshot,generation)==retained::Result::Succeeded&&
       packed::CaptureBaseline(&staged_packs,s->pack_snapshot,generation)==packed::Result::Succeeded;
    packed::Fence current={};
    ok=ok&&packed::windows::ReadFence(&s->pack_native,&current)&&
        current.mutation_revision==s->pack_snapshot.fence.mutation_revision&&
        current.owner_generation==s->pack_snapshot.fence.owner_generation;
    ok=ok&&renderer_streaming::CaptureBaseline(&s->renderer,staged_packs,generation);
    if(ok){
     // Both native inventories belong to the same held fence. Publish B0 only
     // after both owner captures succeed; a retry cannot preserve half a B0.
     std::memcpy(s->textures.records,staged_textures.records,texture_bytes);std::memcpy(s->packs.records,staged_packs.records,pack_bytes);
     staged_textures.records=s->textures.records;staged_packs.records=s->packs.records;s->textures=staged_textures;s->packs=staged_packs;
     s->baseline_generation=generation;
    }
   }
  }
 }__finally{if(staged_storage)(void)VirtualFree(staged_storage,0,0x8000);Leave(s);}return ok;
}
bool PreflightResources(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!s->baseline_generation||!transaction||!epoch||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{ok=Capture(s)&&packed::Prepare(s->packs,s->textures,s->pack_snapshot,&s->pack_plan)==packed::Result::Succeeded&&
  retained::PreflightWithPackTextures(s->textures,s->texture_snapshot,s->pack_snapshot.textures,s->pack_snapshot.texture_count)==retained::Result::Succeeded;
 }__finally{Leave(s);}return ok;
}
bool CaptureMaterialBaseline(State* s,std::uintptr_t manager,std::uint64_t generation)noexcept{
 if(!s||!manager||!generation)return false;
 if(s->material.baseline.captured)return s->material.baseline.generation==generation;
 if(!Enter(s,0,0))return false;bool ok=false;
 __try{ok=material_baseline::Capture(&s->material,manager,&s->material_snapshot)==material_baseline::Result::Succeeded&&
  material_baseline::CaptureBaseline(&s->material,s->material_snapshot,generation)==material_baseline::Result::Succeeded;
 }__finally{Leave(s);}return ok;
}
bool ResetResources(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!s->baseline_generation||!transaction||!epoch||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{
  ok=renderer_streaming::CollectDeleted(&s->renderer)&&renderer_streaming::SynchronizeOrigins(&s->renderer,s->packs)&&
   Capture(s)&&packed::Prepare(s->packs,s->textures,s->pack_snapshot,&s->pack_plan)==packed::Result::Succeeded&&
   retained::PreflightWithPackTextures(s->textures,s->texture_snapshot,s->pack_snapshot.textures,s->pack_snapshot.texture_count)==retained::Result::Succeeded&&
   packed::Reset(&s->packs,&s->textures,packed::windows::MakeOperations(&s->pack_native),&s->pack_snapshot,&s->pack_plan)==packed::Result::Succeeded&&
   retained::windows::CaptureTextures(&s->texture_native,&s->texture_snapshot)==retained::Result::Succeeded&&
   retained::Prepare(s->textures,s->texture_snapshot,&s->texture_plan)==retained::Result::Succeeded;
  if(ok){
   ok=renderer_streaming::PermitNativeTextureDeletion(&s->renderer,true);
   if(ok){bool returned=false,complete=false,closed=false;
    __try{complete=retained::Execute(&s->textures,retained::windows::MakeTextureOperations(&s->texture_native),&s->texture_snapshot,&s->texture_plan)==retained::Result::Succeeded;returned=true;}
    __finally{closed=StreamMutation(s,false,returned);}
    ok=complete&&closed;
   }
  }
  if(ok){s->completed_transaction=transaction;s->completed_epoch=epoch;}
 }__finally{Leave(s);}return ok;
}
bool VerifyResources(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||s->completed_transaction!=transaction||s->completed_epoch!=epoch||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{ok=Capture(s)&&packed::Prepare(s->packs,s->textures,s->pack_snapshot,&s->pack_plan)==packed::Result::Succeeded&&!s->pack_plan.count&&
  retained::Prepare(s->textures,s->texture_snapshot,&s->texture_plan)==retained::Result::Succeeded&&!s->texture_plan.count;
 }__finally{Leave(s);}return ok;
}
bool PreflightMaterials(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!transaction||!epoch||!s->material.baseline.captured||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{ok=material_baseline::Capture(&s->material,s->material.baseline.manager,&s->material_snapshot)==material_baseline::Result::Succeeded&&
  material_baseline::PreflightAbsentLocal(&s->material,s->material_snapshot)==material_baseline::Result::Succeeded;
 }__finally{Leave(s);}return ok;
}
bool ResetMaterials(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!transaction||!epoch||!s->material.baseline.captured||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{ok=material_baseline::ResetAbsentLocal(&s->material,&s->material_snapshot,UnloadUnborrowedLocal,s)==material_baseline::Result::Succeeded;
 }__finally{Leave(s);}return ok;
}
bool VerifyMaterials(State* s,std::uint64_t transaction,std::uint32_t epoch)noexcept{
 if(!s||!transaction||!epoch||!Enter(s,transaction,epoch))return false;bool ok=false;
 __try{ok=material_baseline::Capture(&s->material,s->material.baseline.manager,&s->material_snapshot)==material_baseline::Result::Succeeded&&
  material_baseline::VerifyAbsentLocal(&s->material,s->material_snapshot)==material_baseline::Result::Succeeded;
 }__finally{Leave(s);}return ok;
}
bool CanRetireSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 file_lifetime::OutstandingWork work={};
 return s&&s->available&&!s->gateway.lost.load()&&!s->gateway.active_calls.load()&&!s->gateway.pending_calls.load()&&
  file_lifetime::SnapshotOutstandingWork(&s->gateway.files->ledger,&work)&&work.reads_drained&&work.transaction==transaction&&
  !resource_gateway::PendingInputCount(&s->gateway)&&
  !resource_input_capture::Outstanding(&s->capture)&&
  material_baseline::CanRetireSource(&s->material,source,transaction)&&
  renderer_streaming::CanRetireSource(&s->renderer,source,transaction)&&
  packed::CanRetireSource(s->packs,source.address,source.generation,transaction)==packed::Result::Succeeded&&
  retained::CanRetireSource(s->textures,source.address,source.generation,transaction)==retained::Result::Succeeded;
}
bool ReclaimRetiredSource(State* s,file_lifetime::SourceKey source,std::uint64_t transaction)noexcept{
 return s&&material_baseline::ReclaimRetiredSource(&s->material,source,transaction)&&renderer_streaming::ReclaimSource(&s->renderer,source,transaction)&&
  packed::ReclaimRetired(&s->packs,source.address,source.generation,transaction)==packed::Result::Succeeded&&
  retained::ReclaimEnded(&s->textures,source.address,source.generation,transaction)==retained::Result::Succeeded;
}
bool StopInputCapture(State* s,std::uint32_t timeout)noexcept{return s&&resource_input_capture::Stop(&s->capture,timeout);}
} // namespace
