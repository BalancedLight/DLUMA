#include "NativeXuiContentOwner.h"
#include <cstring>
extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockExclusive(PVOID);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PVOID);
}
namespace dluma::fixes::custom_map_asset_isolation::xui_content {
namespace {
bool Lock(State* s)noexcept {
    if(!s)return false;
    if(s->lock.test_and_set(std::memory_order_acquire)){s->lost.store(true);return false;}
    return true;
}
void Unlock(State* s)noexcept{s->lock.clear(std::memory_order_release);}
bool Same(Key a,Key b)noexcept{return a.manager==b.manager&&a.generation==b.generation;}
bool Same(file_lifetime::SourceKey a,file_lifetime::SourceKey b)noexcept {
    return a.address==b.address&&a.generation==b.generation;
}
bool Valid(const Input& i)noexcept {
    if(!i.complete||!i.file.file||!i.file.generation||!i.file.root||
       i.file.life!=file_lifetime::Life::Live||!i.selected_source.address||!i.selected_source.generation||
       (i.file.source.address&&!Same(i.file.source,i.selected_source))||
       !i.path[0]||!std::memchr(i.path,0,sizeof(i.path)))return false;
    unsigned char nonzero=0;for(auto b:i.sha256)nonzero|=b;
    return nonzero&&(i.realm==Realm::Global?i.source_epoch==0:
        i.realm==Realm::Project&&i.source_epoch!=0);
}
bool Same(const BaselineInput& b,const Input& i)noexcept {
    bool same_path=true;
    for(std::size_t n=0;n<kPathBytes;++n){if(b.path[n]!=i.path[n]){same_path=false;break;}if(!b.path[n])break;}
    return Same(b.source,i.selected_source)&&b.bytes==i.bytes&&
        same_path&&!std::memcmp(b.sha256,i.sha256,sizeof(b.sha256));
}
Owner* Find(State* s,Key key)noexcept {
    for(std::size_t n=0;n<s->owner_count;++n)if(Same(s->owners[n].key,key))return s->owners+n;
    return nullptr;
}
bool Current(State* s,const Load* l)noexcept {
    return l&&l->active&&l->state==reinterpret_cast<std::uintptr_t>(s)&&l->serial&&
        l->slot<kActiveLoads&&s->active_addresses[l->slot]==reinterpret_cast<std::uintptr_t>(l)&&
        s->active_serials[l->slot]==l->serial&&s->active_loads&&Find(s,l->owner)&&!Find(s,l->owner)->ended;
}
}
bool SealStartup(State* s)noexcept {
    if(!Lock(s))return false;
    const bool ok=!s->startup_sealed&&!s->lost.load()&&!s->next_serial&&!s->baseline_frozen;
    if(ok)s->startup_sealed=true;else s->lost.store(true);
    Unlock(s);return ok;
}
bool Begin(State* s,Key key,std::uint32_t epoch,Route route,bool clean,Load* out)noexcept {
    if(!out||out->active||!Lock(s))return false;
    bool ok=s->startup_sealed&&!s->lost.load()&&key.manager&&key.generation&&
        s->next_serial!=UINT64_MAX&&s->active_loads!=UINT32_MAX&&(!clean||(!epoch&&!s->baseline_frozen));
    auto* owner=Find(s,key);
    std::size_t slot=0;while(slot<kActiveLoads&&s->active_addresses[slot])++slot;
    if(slot==kActiveLoads)ok=false;
    if(ok&&owner&&owner->ended)ok=false;
    if(ok&&!owner){
        // A completed native destruction receipt can be retired when a new
        // generation needs the slot. No surviving contaminated owner is lost.
        for(std::size_t n=0;n<s->owner_count;++n)if(s->owners[n].ended){owner=s->owners+n;break;}
        if(!owner){if(s->owner_count==kOwners)ok=false;else owner=s->owners+s->owner_count++;}
        if(owner)*owner={key,0,false};
    }
    if(ok){*out={reinterpret_cast<std::uintptr_t>(s),key,++s->next_serial,epoch,0,static_cast<std::uint32_t>(slot),route,true,clean,false};
        s->active_addresses[slot]=reinterpret_cast<std::uintptr_t>(out);s->active_serials[slot]=out->serial;
        if(epoch)s->project_seen=true;
        ++s->active_loads;++s->revision;}
    else s->lost.store(true);
    Unlock(s);return ok;
}
bool Observe(State* s,Load* load,const Input& input)noexcept {
    if(!Lock(s))return false;
    bool ok=Current(s,load)&&!s->lost.load();
    if(ok){++load->input_count;
        const bool valid=Valid(input);
        bool baseline_match=false;
        if(valid)for(std::size_t i=0;i<s->input_count;++i)if(Same(s->inputs[i],input)){baseline_match=true;break;}
        if(load->clean_startup){
            // A missing startup digest cannot bless later content. Keep the
            // startup corpus partial and classify that later load as dirty.
            if(valid&&input.realm==Realm::Global&&!s->baseline_frozen&&!baseline_match){
                if(s->input_count==kInputs)ok=false;
                else {auto& saved=s->inputs[s->input_count++];saved.source=input.selected_source;
                    std::memcpy(saved.path,input.path,sizeof(saved.path));
                    std::memcpy(saved.sha256,input.sha256,sizeof(saved.sha256));saved.bytes=input.bytes;}
            }
        } else if(load->epoch && (!valid||input.realm!=Realm::Global||!s->baseline_frozen||!baseline_match))
            load->dirty=true;
        ++s->revision;
    }
    if(!ok)s->lost.store(true);
    Unlock(s);return ok;
}
bool End(State* s,Load* load,bool noop,bool* affected)noexcept {
    if(affected)*affected=false;
    if(!affected||!Lock(s))return false;
    const bool current=Current(s,load);
    bool ok=current&&!s->lost.load();
    if(current){
        if(!load->input_count&&!noop&&load->epoch)load->dirty=true;
        if(load->epoch&&load->dirty){auto* owner=Find(s,load->owner);
            if(owner->touched_epoch&&owner->touched_epoch!=load->epoch)ok=false;
            else owner->touched_epoch=load->epoch;
            *affected=true;
        }
        --s->active_loads;s->active_addresses[load->slot]=0;s->active_serials[load->slot]=0;load->active=false;++s->revision;
    }
    if(!ok)s->lost.store(true);
    Unlock(s);return ok;
}
bool OwnerDestroyed(State* s,Key key)noexcept {
    if(!Lock(s))return false;
    auto* owner=Find(s,key);
    const bool ok=s->startup_sealed&&!s->lost.load()&&key.manager&&key.generation&&(!owner||!owner->ended);
    if(ok&&owner){owner->ended=true;++s->revision;}
    else if(!ok)s->lost.store(true);
    Unlock(s);return ok;
}
bool CaptureBaseline(State* s,std::uint64_t generation)noexcept {
    if(!Lock(s))return false;
    const bool ok=generation&&s->startup_sealed&&!s->lost.load()&&!s->active_loads&&
        (s->baseline_frozen||!s->project_seen)&&(!s->baseline_frozen||s->baseline_generation==generation);
    if(ok){s->baseline_frozen=true;s->baseline_generation=generation;}
    Unlock(s);return ok;
}
bool VerifyEndedOwners(State* s,std::uint64_t baseline,std::uint32_t epoch)noexcept {
    if(!Lock(s))return false;
    bool ok=baseline&&epoch&&s->baseline_frozen&&s->baseline_generation==baseline&&
        s->startup_sealed&&!s->lost.load()&&!s->active_loads;
    for(std::size_t i=0;ok&&i<s->owner_count;++i){const auto& owner=s->owners[i];
        if(owner.touched_epoch&&!owner.ended)ok=false;}
    Unlock(s);return ok;
}
bool TrackInputLease(InputLeasePool* p,void* pair,InputLeaseToken* token)noexcept {
    if(!p||!pair||!token||token->active)return false;
    AcquireSRWLockExclusive(&p->lock);
    std::size_t slot=kInputLeases;bool duplicate=false;
    for(std::size_t i=0;i<kInputLeases;++i){if(!p->slots[i].pair&&slot==kInputLeases)slot=i;
        if(p->slots[i].pair==pair)duplicate=true;}
    const bool ok=!duplicate&&slot<kInputLeases&&p->next_generation!=UINT64_MAX;
    if(ok){const auto generation=++p->next_generation;p->slots[slot]={pair,generation,true};
        *token={reinterpret_cast<std::uintptr_t>(p),generation,static_cast<std::uint32_t>(slot),true};}
    ReleaseSRWLockExclusive(&p->lock);return ok;
}
bool CompleteInputLease(InputLeasePool* p,InputLeaseToken* token,CloseInputFn close,FreeInputFn release,void* user)noexcept {
    if(!p||!token||!close||!release)return false;
    AcquireSRWLockExclusive(&p->lock);
    bool ok=token->active&&token->pool==reinterpret_cast<std::uintptr_t>(p)&&token->slot<kInputLeases;
    if(ok){auto& slot=p->slots[token->slot];ok=slot.pair&&slot.reading&&slot.generation==token->generation;
        if(ok){slot.reading=false;token->active=false;ok=close(user,slot.pair);
            if(ok){release(user,slot.pair);slot={};}}}
    ReleaseSRWLockExclusive(&p->lock);return ok;
}
bool DrainInputLeases(InputLeasePool* p,CloseInputFn close,FreeInputFn release,void* user)noexcept {
    if(!p||!close||!release)return false;
    AcquireSRWLockExclusive(&p->lock);
    bool ready=true;
    for(auto& slot:p->slots)if(slot.pair){
        if(slot.reading||!close(user,slot.pair)){ready=false;continue;}
        release(user,slot.pair);slot={};
    }
    ReleaseSRWLockExclusive(&p->lock);return ready;
}
} // namespace
