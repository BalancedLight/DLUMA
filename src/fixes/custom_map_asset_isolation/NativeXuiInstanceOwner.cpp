#include "NativeXuiInstanceOwner.h"
extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockExclusive(PVOID);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PVOID);
__declspec(dllimport) BYTE WINAPI TryAcquireSRWLockExclusive(PVOID);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
__declspec(dllimport) unsigned short WINAPI RtlCaptureStackBackTrace(unsigned long,unsigned long,void**,unsigned long*);
#endif
}
namespace dluma::fixes::custom_map_asset_isolation::xui_instance {
namespace {
class Guard {State* state;public:explicit Guard(State* s):state(s){AcquireSRWLockExclusive(&state->lock);}
    ~Guard(){ReleaseSRWLockExclusive(&state->lock);}};
bool Same(xui_content::Key a,xui_content::Key b)noexcept{return a.manager==b.manager&&a.generation==b.generation;}
bool Live(const Instance& i)noexcept{return i.life==Life::Active||i.life==Life::Retired||i.life==Life::Provisional;}
bool WriterFenceLocked(const State* s,const Writer& writer)noexcept{
    if(!writer.active||writer.state!=reinterpret_cast<std::uintptr_t>(s)||writer.slot>=128||
       s->writer_addresses[writer.slot]!=reinterpret_cast<std::uintptr_t>(&writer)||writer.thread_id!=GetCurrentThreadId()||
       writer.concurrent_at_begin||writer.concurrency_revision!=s->concurrency_revision)return false;
    for(unsigned n=0;n<128;++n)if(s->writer_addresses[n]&&s->writer_threads[n]!=writer.thread_id)return false;
    return true;
}
bool Fail(State* s,Loss reason=Loss::Generic,xui_content::Key manager={},std::uintptr_t object=0,
          std::uint64_t expected=0,std::uint64_t actual=0)noexcept{
    if(s->first_loss.reason==Loss::None){
        s->first_loss={reason,manager,object,s->generation,s->revision,expected,actual,s->active_writers};
        s->first_loss.thread_id=GetCurrentThreadId();
        if(reason==Loss::Capacity)for(const auto& entry:s->records){
            const auto life=static_cast<unsigned>(entry.life);if(life<5){++s->first_loss.capacity_life_counts[life];
                if(Same(entry.manager,manager))++s->first_loss.capacity_manager_life_counts[life];}
            if(entry.life==Life::Destroyed&&entry.birth_pending)++s->first_loss.capacity_pending_destroyed;
        }
#if defined(DLUMA_PRIVATE_XUI_DIAGNOSTIC) && DLUMA_PRIVATE_XUI_DIAGNOSTIC
        // Retain only the first bounded stack in the existing failure record.
        // Symbol resolution and logging happen later, outside this owner lock.
        void* frames[16]={};unsigned short count=0;
        __try{count=RtlCaptureStackBackTrace(0,16,frames,nullptr);}__except(1){count=0;}
        if(count<=16){s->first_loss.stack_count=count;
            for(unsigned short n=0;n<count;++n)s->first_loss.stack[n]=reinterpret_cast<std::uintptr_t>(frames[n]);}
#endif
    }
    s->lost=true;return false;
}
std::size_t ReceiverBucket(std::uintptr_t value)noexcept {
    value^=value>>17;value*=static_cast<std::uintptr_t>(0xed5ad4bbU);value^=value>>11;
    return static_cast<std::size_t>(value)&(kReceiverBuckets-1);
}
void IndexInsert(State* s,std::size_t index)noexcept {
    const auto identity=s->records[index].interface_object;if(!identity||s->receiver_index_lost)return;
    const auto bucket=ReceiverBucket(identity);const auto node=static_cast<std::uint32_t>(index+1);
    const auto head=s->receiver_heads[bucket];
    if(head>kInstances||head==node||s->receiver_next[index]||s->receiver_prev[index]||
       (head&&s->receiver_prev[head-1])){s->receiver_index_lost=true;return;}
    s->receiver_next[index]=head;if(head)s->receiver_prev[head-1]=node;s->receiver_heads[bucket]=node;
}
void IndexRemove(State* s,std::size_t index)noexcept {
    const auto identity=s->records[index].interface_object;if(!identity||s->receiver_index_lost)return;
    const auto bucket=ReceiverBucket(identity);const auto node=static_cast<std::uint32_t>(index+1);
    const auto prev=s->receiver_prev[index],next=s->receiver_next[index];
    if(prev>kInstances||next>kInstances||
       (prev?s->receiver_next[prev-1]!=node:s->receiver_heads[bucket]!=node)||
       (next&&s->receiver_prev[next-1]!=node)){s->receiver_index_lost=true;return;}
    if(prev)s->receiver_next[prev-1]=next;else s->receiver_heads[bucket]=next;
    if(next)s->receiver_prev[next-1]=prev;
    s->receiver_next[index]=s->receiver_prev[index]=0;
}
}
bool SealStartup(State* s)noexcept {
    if(!s)return false;Guard guard(s);if(s->sealed||s->lost||s->generation||s->active_writers)return Fail(s,Loss::Startup);
    s->sealed=true;return true;
}
bool BeginWriter(State* s,Writer* out)noexcept {
    if(!s||!out||out->active)return false;Guard guard(s);
    if(!s->sealed||s->active_writers==UINT32_MAX||s->revision==UINT64_MAX)return Fail(s,Loss::WriterLimit);
    std::size_t slot=0;while(slot<128&&s->writer_addresses[slot])++slot;if(slot==128)return Fail(s,Loss::WriterLimit);
    const auto thread=GetCurrentThreadId();bool concurrent=false;
    for(std::size_t i=0;i<128;++i)if(s->writer_addresses[i]&&s->writer_threads[i]!=thread)concurrent=true;
    if(concurrent){if(s->concurrency_revision==UINT64_MAX)return Fail(s,Loss::WriterLimit);++s->concurrency_revision;}
    ++s->active_writers;*out={reinterpret_cast<std::uintptr_t>(s),++s->revision,static_cast<std::uint32_t>(slot),true,thread,s->concurrency_revision,concurrent};
    s->writer_threads[slot]=thread;
    s->writer_addresses[slot]=reinterpret_cast<std::uintptr_t>(out);return true;
}
bool EndWriter(State* s,Writer* token)noexcept {
    if(!s||!token)return false;Guard guard(s);
    if(!token->active||token->state!=reinterpret_cast<std::uintptr_t>(s)||!s->active_writers||token->slot>=128||
        s->writer_addresses[token->slot]!=reinterpret_cast<std::uintptr_t>(token)||token->thread_id!=GetCurrentThreadId()||s->writer_threads[token->slot]!=token->thread_id)return Fail(s,Loss::WriterIdentity);
    --s->active_writers;s->writer_addresses[token->slot]=0;s->writer_threads[token->slot]=0;token->active=false;++s->revision;return !s->lost;
}
namespace {
bool Insert(State* s,const Instance& observed,bool provisional,std::uint64_t* output)noexcept {
    if(!s->sealed||!s->active_writers||s->lost||!observed.manager.manager||!observed.manager.generation||
        !observed.object||!observed.vtable||s->generation==UINT64_MAX||
        (provisional?observed.life!=Life::Provisional:(observed.life!=Life::Active&&observed.life!=Life::Retired)))return Fail(s,Loss::InvalidBirth,observed.manager,observed.object);
    Instance* slot=nullptr;
    for(auto& entry:s->records){if((Live(entry)||entry.birth_pending)&&entry.object==observed.object)return Fail(s,Loss::DuplicateBirth,observed.manager,observed.object);
        if(!slot&&!Live(entry)&&!entry.birth_pending)slot=&entry;}
    if(!slot)return Fail(s,Loss::Capacity,observed.manager,observed.object);*slot=observed;slot->generation=++s->generation;slot->birth_pending=provisional;
    IndexInsert(s,static_cast<std::size_t>(slot-s->records));
    if(output)*output=slot->generation;++s->revision;return true;
}
}
bool Created(State* s,const Instance& observed)noexcept{if(!s)return false;Guard guard(s);return Insert(s,observed,false,nullptr);}
bool Born(State* s,const Instance& observed,std::uint64_t* output)noexcept{
    if(output)*output=0;if(!s||!output)return false;Guard guard(s);return Insert(s,observed,true,output);}
bool FinishBirth(State* s,xui_content::Key manager,std::uintptr_t object,std::uint64_t generation,bool returned,std::uintptr_t native_return)noexcept{
    if(!s)return false;Guard guard(s);
    for(auto& entry:s->records)if(entry.generation==generation&&entry.object==object&&Same(entry.manager,manager)&&entry.birth_pending){
        entry.birth_pending=false;
        if(returned&&native_return!=object)return Fail(s,Loss::FactoryReturn,manager,object,object,native_return);
        if(returned&&entry.life==Life::Provisional)entry.life=Life::Active;
        ++s->revision;return !s->lost;
    }
    return Fail(s,Loss::MissingBirth,manager,object,generation,native_return);
}
bool MarkLost(State* s,Loss reason,xui_content::Key manager,std::uintptr_t object,std::uint64_t expected,std::uint64_t actual)noexcept{
    if(!s)return false;Guard guard(s);return Fail(s,reason,manager,object,expected,actual);}
bool RetirementEntry(State* s,xui_content::Key manager,std::uintptr_t object,unsigned flags,
                     std::uint64_t nested,std::uint64_t* generation,const Writer* bulk_writer)noexcept{
    if(generation)*generation=0;if(!s||!generation)return false;Guard guard(s);
    if(!s->sealed||s->lost||!s->active_writers)return Fail(s,Loss::WriterIdentity,manager,object);
    for(const auto& entry:s->records)if(Live(entry)&&entry.object==object&&Same(entry.manager,manager)){
        const bool active=entry.life==Life::Active||entry.life==Life::Provisional;
        const bool bulk=flags==1&&active&&bulk_writer&&WriterFenceLocked(s,*bulk_writer);
        if((flags==0&&active)||(flags==1&&active&&nested&&nested==entry.generation)||bulk||(flags==2&&entry.life==Life::Retired)){
            *generation=entry.generation;return true;
        }
        return Fail(s,Loss::CallbackFlags,manager,object,flags==1?nested:2,flags);
    }
    return Fail(s,Loss::UnobservedRetirement,manager,object);
}
bool Retired(State* s,xui_content::Key manager,std::uintptr_t object,std::uint64_t generation)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||!s->active_writers)return Fail(s);
    for(auto& entry:s->records)if(Live(entry)&&entry.object==object&&Same(entry.manager,manager)){
        if(generation&&generation!=entry.generation)return Fail(s,Loss::WriterIdentity,manager,object,generation,entry.generation);
        entry.life=Life::Retired;++s->revision;return true;}
    return Fail(s,Loss::UnobservedRetirement,manager,object);
}
bool AllRetired(State* s,xui_content::Key manager,const Writer* bulk_writer)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||!s->active_writers)return Fail(s);
    if(bulk_writer&&!WriterFenceLocked(s,*bulk_writer))return Fail(s,Loss::CollectorFence,manager);
    for(auto& entry:s->records)if((entry.life==Life::Active||entry.life==Life::Provisional)&&Same(entry.manager,manager))entry.life=Life::Retired;
    ++s->revision;return true;
}
bool Collected(State* s,xui_content::Key manager,std::uint32_t expected,bool empty,std::uint64_t revision)noexcept {
    if(!s)return false;Guard guard(s);
    if(!s->sealed||s->lost||s->active_writers!=1||!empty||s->revision!=revision)return Fail(s,Loss::CollectorFence,manager,0,revision,s->revision);
    std::size_t count=0;for(const auto& entry:s->records)if(entry.life==Life::Retired&&Same(entry.manager,manager))++count;
    if(count!=expected)return Fail(s,Loss::CollectorCount,manager,0,expected,count);
    for(std::size_t n=0;n<kInstances;++n){auto& entry=s->records[n];
        if(entry.life==Life::Retired&&Same(entry.manager,manager)){IndexRemove(s,n);entry.life=Life::Destroyed;}}
    ++s->revision;return true;
}
bool CollectedNested(State* s,xui_content::Key manager,std::uint32_t expected,bool empty,const Writer& writer)noexcept{
    if(!s)return false;Guard guard(s);
    if(!s->sealed||s->lost||!empty||!WriterFenceLocked(s,writer))return Fail(s,Loss::CollectorFence,manager,0,writer.concurrency_revision,s->concurrency_revision);
    std::size_t count=0;for(const auto& entry:s->records)if(entry.life==Life::Retired&&Same(entry.manager,manager))++count;
    if(count!=expected)return Fail(s,Loss::CollectorCount,manager,0,expected,count);
    for(std::size_t n=0;n<kInstances;++n)if(s->records[n].life==Life::Retired&&Same(s->records[n].manager,manager)){
        IndexRemove(s,n);s->records[n].life=Life::Destroyed;}
    ++s->revision;return true;
}
bool ManagerDestroyed(State* s,xui_content::Key manager)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed)return Fail(s);
    for(std::size_t n=0;n<kInstances;++n){auto& entry=s->records[n];
        if(Live(entry)&&Same(entry.manager,manager)){IndexRemove(s,n);entry.life=Life::Destroyed;}}
    ++s->revision;return !s->lost;
}
bool FinishLoad(State* s,std::uint64_t serial,Origin origin)noexcept {
    if(!s||!serial)return false;Guard guard(s);
    if(!s->sealed||s->lost||(origin!=Origin::UiLoadGlobal&&origin!=Origin::UiLoadProject&&origin!=Origin::UiLoadUnknown))return Fail(s,Loss::LoadOrigin,{},0,serial,static_cast<std::uint64_t>(origin));
    for(auto& entry:s->records)if(entry.load_serial==serial&&entry.origin==Origin::UiLoadPending)entry.origin=origin;
    ++s->revision;return true;
}
bool CopyInventory(State* s,xui_content::Key manager,Instance* out,std::size_t capacity,std::size_t* count,std::uint64_t* revision)noexcept {
    if(count)*count=0;if(revision)*revision=0;
    if(!s||!out||!capacity||capacity>kInstances||!count||!revision)return false;Guard guard(s);
    if(!s->sealed||s->lost||s->active_writers)return false;
    for(const auto& entry:s->records)if(Live(entry)&&Same(entry.manager,manager)){
        if(*count==capacity){*count=0;return false;}out[(*count)++]=entry;}
    *revision=s->revision;return true;
}
bool TryCopyActiveReceiver(State* s,std::uintptr_t identity,std::uint64_t expected,Instance* output)noexcept {
    if(output)*output={};
    if(!s||!output||!identity)return false;
    Guard guard(s);
    if(!s->sealed||s->lost||s->receiver_index_lost||s->active_writers)return false;
    const Instance* match=nullptr;auto node=s->receiver_heads[ReceiverBucket(identity)];std::size_t visited=0;
    while(node){
        if(node>kInstances||++visited>kReceiverScanLimit)return false;
        const auto index=static_cast<std::size_t>(node-1);const auto& entry=s->records[index];
        if(!Live(entry)||!entry.interface_object||ReceiverBucket(entry.interface_object)!=ReceiverBucket(identity))return false;
        if(entry.interface_object==identity){
            if(match||entry.life!=Life::Active)return false;
            match=&entry;
        }
        node=s->receiver_next[index];
    }
    if(!match||!match->generation||!match->manager.manager||!match->manager.generation||
       (expected&&match->generation!=expected))return false;
    *output=*match;return true;
}
bool TryMatchActiveReceiver(State* s,xui_content::Key manager,std::uintptr_t identity,
    std::uint64_t expected,std::uint64_t* output)noexcept {
    if(output)*output=0;
    if(!output||!manager.manager||!manager.generation)return false;
    Instance match={};
    if(!TryCopyActiveReceiver(s,identity,expected,&match)||!Same(match.manager,manager))return false;
    *output=match.generation;return true;
}
bool CopyReceiverDiagnostics(State* s,std::uintptr_t identity,ReceiverDiagnostics* output)noexcept {
    if(output)*output={};if(!s||!output||!TryAcquireSRWLockExclusive(&s->lock))return false;
    output->generation=s->generation;output->revision=s->revision;output->writers=s->active_writers;
    output->sealed=s->sealed;output->lost=s->lost.load();output->index_lost=s->receiver_index_lost;
    output->first_loss=s->first_loss;
    if(identity)for(const auto& entry:s->records)if(entry.generation&&entry.interface_object==identity){
        if(!output->matches)output->first=entry;++output->matches;}
    ReleaseSRWLockExclusive(&s->lock);return true;
}
bool ProjectInputInstancesClosed(State* s)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||s->active_writers)return false;
    for(const auto& entry:s->records)if(Live(entry)&&entry.epoch&&
        (entry.origin==Origin::UiLoadPending||entry.origin==Origin::UiLoadProject||entry.origin==Origin::UiLoadUnknown))return false;
    return true;
}
} // namespace
