#define _HAS_EXCEPTIONS 0
#include "NativeFileLifetimeLedger.h"
#include <cstring>
#include <intrin.h>
extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockExclusive(PVOID);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PVOID);
}
namespace dluma::fixes::custom_map_asset_isolation::file_lifetime {
namespace {
void Lose(Ledger* l,Failure f)noexcept{if(l)dluma_atomic_compare_exchange(&l->failure,static_cast<LONG>(f),0);}
/* The lock guards only bounded POD bookkeeping. No engine call, provider
 * lookup, allocator, wait or injected callback runs while it is held. */
class Guard {Ledger* l_;public:explicit Guard(Ledger* l)noexcept:l_(l){if(l_)AcquireSRWLockExclusive(&l_->lock);}~Guard(){if(l_)ReleaseSRWLockExclusive(&l_->lock);}bool ok()const noexcept{return l_!=nullptr;}};
bool Same(SourceKey a,SourceKey b)noexcept{return a.address==b.address&&a.generation==b.generation;}
bool Valid(SourceKey s)noexcept{return (!s.address&&!s.generation)||(s.address&&s.generation);}
std::uint64_t Next(Ledger* l)noexcept{if(l->next_generation==UINT64_MAX){Lose(l,Failure::Capacity);return 0;}return ++l->next_generation;}
void Changed(Ledger* l,SourceRecord* s=nullptr)noexcept{if(l->revision==UINT64_MAX){Lose(l,Failure::Capacity);return;}++l->revision;if(s)s->revision=l->revision;}
SourceRecord* Source(Ledger* l,SourceKey key,bool create,bool covered=false)noexcept{
 if(!key.address)return nullptr;
 SourceRecord* empty=nullptr;
 for(auto& s:l->sources){if(Same(s.key,key))return &s;if(s.key.address==key.address){Lose(l,Failure::SourceReuse);return nullptr;}if(!s.key.address&&!empty)empty=&s;}
 if(!create)return nullptr;if(!empty){Lose(l,Failure::Capacity);return nullptr;}
 *empty={};empty->key=key;empty->covered=covered;empty->life=Life::Live;Changed(l,empty);return empty;
}
FileRecord* LiveFile(Ledger* l,std::uintptr_t p)noexcept{
 for(auto& f:l->files)if(f.file==p&&f.life==Life::Live)return &f;return nullptr;
}
Token FileToken(Ledger* l,FileRecord* f)noexcept{return f?Token{f->generation,static_cast<std::uint32_t>(f-l->files),true}:Token{};}
bool CompleteControl(Ledger* l,std::uintptr_t control)noexcept{
 for(auto& d:l->streams)if(d.control==control&&d.life==Life::Live){
  auto* s=Source(l,d.source,false);if(!d.pending||(s&&!s->streams)){Lose(l,Failure::InvalidToken);return false;}
  --d.pending;if(s)--s->streams;if(!d.pending)d.life=Life::Ended;Changed(l,s);return true;
 }
 return false;
}
bool Drain(Ledger* l)noexcept{
 auto& ring=l->completions;
 const auto next=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&ring.next,0,0));
 auto consumed=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&ring.consumed,0,0));
 constexpr auto done=std::uint64_t{1}<<63;
 if(ring.overflow||next<consumed||next-consumed>kCompletionCapacity||next>=done){Lose(l,Failure::Capacity);return false;}
 /* Independent producers may publish out of ticket order. Consume any ready
  * observation, but recycle storage only past a contiguous completed prefix. */
 for(auto i=consumed;i<next;++i){auto& event=ring.events[i&(kCompletionCapacity-1)];
  if(static_cast<std::uint64_t>(_InterlockedCompareExchange64(&event.sequence,0,0))==i+1){
   (void)CompleteControl(l,event.control);
   _InterlockedExchange64(&event.sequence,static_cast<LONGLONG>((i+1)|done));
  }}
 while(consumed<next){auto& event=ring.events[consumed&(kCompletionCapacity-1)];
  if(static_cast<std::uint64_t>(_InterlockedCompareExchange64(&event.sequence,0,0))!=((consumed+1)|done))break;
  event.control=0;_InterlockedExchange64(&event.sequence,0);++consumed;
 }
 _InterlockedExchange64(&ring.consumed,static_cast<LONGLONG>(consumed));return true;
}
bool ProofUnderLock(Ledger* l,SourceKey key,Proof* p)noexcept{
 (void)Drain(l);
 *p={};p->source=key;p->ledger_revision=l->revision;p->transaction=l->transaction;
 p->failure=static_cast<Failure>(dluma_atomic_compare_exchange(&l->failure,0,0));
 p->admission_closed=l->admission_closed;p->active_factories=l->active_factories;p->active_destructors=l->active_destructors;
 p->pending_completion_events=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&l->completions.next,0,0)-_InterlockedCompareExchange64(&l->completions.consumed,0,0));
 auto* s=Source(l,key,false);if(!s)return false;
 p->source_revision=s->revision;p->source_covered=s->covered;p->source_destroyed=s->life==Life::Ended;p->live_files=s->live_files;p->deferred_controls=s->streams;
 p->ready=l->hooks_complete&&l->admission_closed&&l->transaction&&s->covered&&p->failure==Failure::None&&
   s->life!=Life::Destroying&&!s->live_files&&!s->streams&&!l->active_factories&&!l->active_destructors&&!p->pending_completion_events;return true;
}
} // namespace
void Initialize(Ledger* l)noexcept{if(l)std::memset(l,0,sizeof(*l));}
void RecordCoverageLoss(Ledger* l,Failure f)noexcept{Lose(l,f);}
bool CompleteHookInstallation(Ledger* l,std::uint64_t mask,std::uint64_t required)noexcept{
 Guard g(l);if(!g.ok())return false;if(!required||mask!=required||l->admission_closed||l->active_factories||l->active_destructors){Lose(l,Failure::HookCoverage);return false;}
 l->hooks_complete=true;Changed(l);return true;
}
bool RecordSourceConstruction(Ledger* l,SourceKey key)noexcept{
 Guard g(l);if(!g.ok()||!key.address||!Valid(key))return false;
 auto* s=Source(l,key,false);if(s)return s->covered;
 s=Source(l,key,true,l->hooks_complete&&!l->admission_closed);return s&&s->covered;
}
bool ObserveExistingSource(Ledger* l,SourceKey key)noexcept{Guard g(l);return g.ok()&&key.address&&Valid(key)&&Source(l,key,true)!=nullptr;}
bool SnapshotSourceKey(Ledger* l,std::uintptr_t address,SourceKey* output)noexcept{
 if(output)*output={};Guard g(l);if(!g.ok()||!address||!output)return false;
 for(const auto& s:l->sources)if(s.key.address==address&&s.life!=Life::Ended){*output=s.key;return true;}return false;
}
bool SnapshotFileIdentity(Ledger* l,std::uintptr_t address,FileRecord* output)noexcept{
 if(output)*output={};Guard g(l);if(!g.ok()||!address||!output)return false;
 const auto* file=LiveFile(l,address);if(!file)return false;*output=*file;return true;
}
bool SnapshotOutstandingWork(Ledger* l,OutstandingWork* output)noexcept{
 if(output)*output={};Guard guard(l);if(!guard.ok()||!output)return false;
 output->ledger_revision=l->revision;output->transaction=l->transaction;
 output->active_factories=l->active_factories;output->active_destructors=l->active_destructors;
 output->hooks_complete=l->hooks_complete;output->admission_closed=l->admission_closed;
 output->failure=static_cast<Failure>(dluma_atomic_compare_exchange(&l->failure,0,0));output->counts_complete=true;
 for(const auto& file:l->files)if(file.life==Life::Live||file.life==Life::Destroying){
  ++output->live_files;if(!file.source.address)++output->source_less_files;
 }
 for(const auto& control:l->streams){
  if(control.life==Life::Live){++output->live_controls;if(!control.source.address)++output->source_less_controls;}
  else if(control.life==Life::Destroying)++output->destroying_controls;
  if(control.life==Life::Live||control.life==Life::Destroying)output->pending_control_references+=control.pending;
  else if(control.pending){output->counts_complete=false;if(output->failure==Failure::None)output->failure=Failure::InvalidToken;}
 }
 const auto consumed=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&l->completions.consumed,0,0));
 const auto published=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&l->completions.next,0,0));
 output->completion_consumed=consumed;output->completion_published=published;
 if(l->completions.overflow||published<consumed||published-consumed>kCompletionCapacity||published>=(UINT64_C(1)<<63)){
  output->counts_complete=false;if(output->failure==Failure::None)output->failure=Failure::Capacity;
 }else output->pending_completion_events=published-consumed;
 output->reads_drained=output->counts_complete&&output->hooks_complete&&output->admission_closed&&output->transaction&&
  output->failure==Failure::None&&!output->active_factories&&!output->active_destructors&&!output->live_controls&&
  !output->destroying_controls&&!output->pending_control_references&&!output->pending_completion_events;
 return true;
}
bool ObserveSourceAddress(Ledger* l,std::uintptr_t address,bool constructed,SourceKey* output)noexcept{
 if(output)*output={};Guard g(l);if(!g.ok()||!address||!output)return false;
 SourceRecord* free=nullptr;
 for(auto& s:l->sources){
  if(s.key.address==address&&s.life!=Life::Ended){
   if(constructed){Lose(l,Failure::SourceReuse);return false;}
   *output=s.key;return true;
  }
  if((!s.key.address||s.life==Life::Ended)&&!free)free=&s;
 }
 if(!free){Lose(l,Failure::Capacity);return false;}
 const auto generation=Next(l);if(!generation)return false;
 *free={};free->key={address,generation};free->life=Life::Live;
 free->covered=constructed&&l->hooks_complete&&!l->admission_closed;
 Changed(l,free);*output=free->key;return true;
}
SourceDestructionToken BeginSourceDestruction(Ledger* l,std::uintptr_t address)noexcept{
 Guard g(l);if(!g.ok()||!address)return {};
 for(std::uint32_t i=0;i<kSources;++i){auto& s=l->sources[i];
  if(s.key.address!=address||s.life==Life::Ended)continue;
  if(s.life!=Life::Live){Lose(l,Failure::InvalidToken);return {};}
  s.life=Life::Destroying;++l->active_destructors;Changed(l,&s);return {s.key,i,true};
 }return {};
}
bool EndSourceDestruction(Ledger* l,SourceDestructionToken* token)noexcept{
 Guard g(l);if(!g.ok()||!token||!token->active||token->slot>=kSources)return false;
 auto& s=l->sources[token->slot];
 if(!Same(s.key,token->key)||s.life!=Life::Destroying||!l->active_destructors){Lose(l,Failure::InvalidToken);return false;}
 (void)Drain(l);
 if(s.live_files||s.streams){Lose(l,Failure::SourceIdentityMismatch);return false;}
 s.life=Life::Ended;--l->active_destructors;Changed(l,&s);token->active=false;return true;
}
bool BeginFactory(Ledger* l,ThreadScope* scope,FactoryToken* token)noexcept{
 if(token)*token={};Guard g(l);if(!g.ok()||!scope||!token)return false;
 if(scope->depth>=kFactoryDepth){Lose(l,Failure::Capacity);return false;}
 // An ordinary root admitted before closure may finish its own native call.
 // A replay root cannot confer blanket permission on an unvalidated child.
 if(l->admission_closed&&(!scope->depth||scope->replay.authority))return false;
 if(!scope->depth){scope->root=Next(l);scope->replay={};}
 else if(!scope->root){Lose(l,Failure::InvalidToken);return false;}
 if(!scope->root||l->active_factories==UINT32_MAX){Lose(l,Failure::Capacity);return false;}
 ++scope->depth;++l->active_factories;*token={scope->root,scope->depth,true};Changed(l);return true;
}
bool ConfigureFactoryReplayValidator(Ledger* l,ValidateFactoryReplayFn fn,void* user)noexcept{
 Guard g(l);if(!g.ok()||!fn||l->hooks_complete||l->admission_closed||l->active_factories||l->validate_factory_replay)return false;
 l->validate_factory_replay=fn;l->factory_replay_user=user;return true;
}
bool BeginReplayFactory(Ledger* l,ThreadScope* scope,FactoryToken* token,
                       const FactoryReplayAuthorization& authorization)noexcept{
 if(token)*token={};
 if(!l||!scope||!token||!authorization.authority||!authorization.authority_generation||!authorization.observation||
    !authorization.transaction||!authorization.epoch||authorization.thread_id!=GetCurrentThreadId())return false;
 ValidateFactoryReplayFn validate=nullptr;void* user=nullptr;
 {
  Guard g(l);
  if(!l->hooks_complete||(l->admission_closed&&l->transaction!=authorization.transaction)||l->failure)return false;
  validate=l->validate_factory_replay;user=l->factory_replay_user;
 }
 // No callback executes under the bookkeeping lock. The issuer must be a
 // same-thread active registry observation, so it cannot return/end while
 // this synchronous factory entry is waiting to acquire the ledger lock.
 if(!validate||!validate(user,authorization))return false;
 Guard g(l);
 if(!l->hooks_complete||(l->admission_closed&&l->transaction!=authorization.transaction)||l->failure||
    l->validate_factory_replay!=validate||l->factory_replay_user!=user)return false;
 if(scope->depth>=kFactoryDepth||l->active_factories==UINT32_MAX){Lose(l,Failure::Capacity);return false;}
 if(scope->depth){
  if(!scope->root||scope->replay.authority!=authorization.authority||
     scope->replay.authority_generation!=authorization.authority_generation||
     scope->replay.transaction!=authorization.transaction||scope->replay.epoch!=authorization.epoch)return false;
 }else{
  scope->root=Next(l);if(!scope->root)return false;scope->replay=authorization;
 }
 ++scope->depth;++l->active_factories;*token={scope->root,scope->depth,true};Changed(l);return true;
}
bool EndFactory(Ledger* l,ThreadScope* scope,FactoryToken* token)noexcept{
 Guard g(l);if(!g.ok()||!scope||!token)return false;
 if(!token->active||scope->root!=token->root||scope->depth!=token->depth||!l->active_factories){Lose(l,Failure::InvalidToken);return false;}
 --scope->depth;--l->active_factories;if(!scope->depth){scope->root=0;scope->replay={};}token->active=false;Changed(l);return true;
}
Token ObserveConstructed(Ledger* l,const ThreadScope& scope,std::uintptr_t file,SourceKey key,FileKind kind)noexcept{
 Guard g(l);if(!g.ok()||!file)return {};
 if(!scope.depth||!scope.root||!l->active_factories){Lose(l,Failure::UnobservedFactory);return {};}
 if(kind==FileKind::Unknown||!Valid(key)){Lose(l,Failure::UnknownClass);return {};}
 auto* prior=LiveFile(l,file);
 if(prior){if(prior->root==scope.root&&Same(prior->source,key)&&prior->kind==kind)return FileToken(l,prior);Lose(l,Failure::AddressReuse);return {};}
 SourceRecord* source=Source(l,key,true);if(key.address&&!source)return {};
 for(auto& f:l->files)if(f.life==Life::Empty||f.life==Life::Ended){
  f={file,key,Next(l),scope.root,kind,Life::Live};if(!f.generation)return {};
  if(source)++source->live_files;Changed(l,source);return FileToken(l,&f);
 }
 Lose(l,Failure::Capacity);return {};
}
Token BeginDestroy(Ledger* l,std::uintptr_t file,SourceKey key,FileKind kind)noexcept{
 Guard g(l);if(!g.ok()||!file)return {};
 auto* f=LiveFile(l,file);
 if(!f){auto* source=Source(l,key,false);if(source&&source->covered)Lose(l,Failure::UnknownFile);return {};}
 if(!Same(f->source,key)||f->kind!=kind){Lose(l,Failure::SourceIdentityMismatch);return {};}
 f->life=Life::Destroying;++l->active_destructors;Changed(l,Source(l,key,false));return FileToken(l,f);
}
bool EndDestroy(Ledger* l,Token* token)noexcept{
 Guard g(l);if(!g.ok()||!token||!token->active||token->slot>=kFiles)return false;
 auto& f=l->files[token->slot];
 if(f.generation!=token->generation||f.life!=Life::Destroying||!l->active_destructors){Lose(l,Failure::InvalidToken);return false;}
 auto* s=Source(l,f.source,false);if(s){if(!s->live_files){Lose(l,Failure::InvalidToken);return false;}--s->live_files;}
 f.life=Life::Ended;--l->active_destructors;token->active=false;Changed(l,s);return true;
}
Token ObserveDeferredControl(Ledger* l,std::uintptr_t file,std::uintptr_t control,SourceKey observed_source)noexcept{
 Guard g(l);if(!g.ok()||!file||!control)return {};
 (void)Drain(l);
 auto* f=LiveFile(l,file);SourceKey work_source={};
 if(f)work_source=f->source;
 else{
  auto* source=Source(l,observed_source,false);
  if(source&&source->covered){Lose(l,Failure::UnknownFile);return {};}
  if(observed_source.address&&!source){Lose(l,Failure::UnknownFile);return {};}
  /* A real queue schedule can use a native file that predates observation.
   * Count its control lifetime globally, including SourceKey0, without
   * manufacturing a FileRecord or retrospective source coverage. */
  work_source=observed_source;
 }
 for(std::uint32_t i=0;i<kStreams;++i){auto& d=l->streams[i];if(d.control==control&&d.life==Life::Live){
  if(!Same(d.source,work_source)){Lose(l,Failure::StreamReuse);return {};}
  if(d.pending==UINT32_MAX){Lose(l,Failure::Capacity);return {};}
  ++d.pending;auto* s=Source(l,d.source,false);if(s)++s->streams;Changed(l,s);return {d.generation,i,true};}}
 auto* s=Source(l,work_source,false);
 for(std::uint32_t i=0;i<kStreams;++i){auto& d=l->streams[i];if(d.life==Life::Empty||d.life==Life::Ended){
  d={control,work_source,Next(l),Life::Live,1};if(!d.generation)return {};if(s)++s->streams;Changed(l,s);return {d.generation,i,true};}}
 Lose(l,Failure::Capacity);return {};
}
bool CompleteDeferredControl(Ledger* l,std::uintptr_t control)noexcept{
 Guard g(l);if(!g.ok()||!control)return false;
 return CompleteControl(l,control);
}
bool PublishCompletedControl(CompletionRing* r,std::uintptr_t control)noexcept{
 if(!r||!control)return false;
 const auto ticket=static_cast<std::uint64_t>(_InterlockedIncrement64(&r->next)-1);
 const auto consumed=static_cast<std::uint64_t>(_InterlockedCompareExchange64(&r->consumed,0,0));
 if(ticket>=UINT64_MAX/2||ticket<consumed||ticket-consumed>=kCompletionCapacity){dluma_atomic_exchange(&r->overflow,1);return false;}
 auto& event=r->events[ticket&(kCompletionCapacity-1)];event.control=control;
 _InterlockedExchange64(&event.sequence,static_cast<LONGLONG>(ticket+1));return true;
}
bool DrainCompletedControls(Ledger* l)noexcept{Guard g(l);return g.ok()&&Drain(l);}
Token BeginControlFree(Ledger* l,std::uintptr_t control)noexcept{
 Guard g(l);if(!g.ok()||!control)return {};
 for(std::uint32_t i=0;i<kStreams;++i){auto& d=l->streams[i];if(d.control==control&&d.life==Life::Live){d.life=Life::Destroying;++l->active_destructors;Changed(l,Source(l,d.source,false));return {d.generation,i,true};}}
 return {};
}
bool EndControlFree(Ledger* l,Token* token)noexcept{
 Guard g(l);if(!g.ok()||!token||!token->active||token->slot>=kStreams)return false;
 auto& d=l->streams[token->slot];if(d.generation!=token->generation||d.life!=Life::Destroying||!l->active_destructors){Lose(l,Failure::InvalidToken);return false;}
 auto* s=Source(l,d.source,false);if(d.pending)Lose(l,Failure::InvalidToken);
 if(s){if(s->streams<d.pending){Lose(l,Failure::InvalidToken);return false;}s->streams-=d.pending;}
 d.pending=0;
 d.life=Life::Ended;--l->active_destructors;token->active=false;Changed(l,s);return true;
}
bool CloseAdmission(Ledger* l,std::uint64_t transaction)noexcept{
 Guard g(l);if(!g.ok()||!transaction)return false;
 if(l->admission_closed)return l->transaction==transaction;
 l->admission_closed=true;l->transaction=transaction;Changed(l);return true;
}
bool ReopenAdmission(Ledger* l,std::uint64_t transaction)noexcept{
 Guard g(l);if(!g.ok()||!l->admission_closed||l->transaction!=transaction||l->active_factories||l->active_destructors||l->failure)return false;
 l->admission_closed=false;l->transaction=0;Changed(l);return true;
}
bool SnapshotSource(Ledger* l,SourceKey key,Proof* p)noexcept{if(p)*p={};Guard g(l);return g.ok()&&p&&ProofUnderLock(l,key,p);}
bool ValidateProof(Ledger* l,const Proof& before)noexcept{
 Guard g(l);if(!g.ok()||!before.ready)return false;Proof now={};return ProofUnderLock(l,before.source,&now)&&now.ready&&
  now.ledger_revision==before.ledger_revision&&now.source_revision==before.source_revision&&now.transaction==before.transaction;
}
bool CompleteSourceRetirement(Ledger* l,const Proof& before)noexcept{
 Guard g(l);if(!g.ok()||!before.ready)return false;Proof now={};
 if(!ProofUnderLock(l,before.source,&now)||!now.ready||now.transaction!=before.transaction)return false;
 auto* source=Source(l,before.source,false);if(!source||source->life!=Life::Ended)return false;
 *source={};Changed(l);return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::file_lifetime
