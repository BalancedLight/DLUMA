#include "NativeXuiInstanceOwner.h"
extern "C" {
__declspec(dllimport) VOID WINAPI AcquireSRWLockExclusive(PVOID);
__declspec(dllimport) VOID WINAPI ReleaseSRWLockExclusive(PVOID);
}
namespace dluma::fixes::custom_map_asset_isolation::xui_instance {
namespace {
class Guard {State* state;public:explicit Guard(State* s):state(s){AcquireSRWLockExclusive(&state->lock);}
    ~Guard(){ReleaseSRWLockExclusive(&state->lock);}};
bool Same(xui_content::Key a,xui_content::Key b)noexcept{return a.manager==b.manager&&a.generation==b.generation;}
bool Live(const Instance& i)noexcept{return i.life==Life::Active||i.life==Life::Retired;}
bool Fail(State* s)noexcept{s->lost=true;return false;}
}
bool SealStartup(State* s)noexcept {
    if(!s)return false;Guard guard(s);if(s->sealed||s->lost||s->generation||s->active_writers)return Fail(s);
    s->sealed=true;return true;
}
bool BeginWriter(State* s,Writer* out)noexcept {
    if(!s||!out||out->active)return false;Guard guard(s);
    if(!s->sealed||s->active_writers==UINT32_MAX||s->revision==UINT64_MAX)return Fail(s);
    std::size_t slot=0;while(slot<128&&s->writer_addresses[slot])++slot;if(slot==128)return Fail(s);
    ++s->active_writers;*out={reinterpret_cast<std::uintptr_t>(s),++s->revision,static_cast<std::uint32_t>(slot),true};
    s->writer_addresses[slot]=reinterpret_cast<std::uintptr_t>(out);return true;
}
bool EndWriter(State* s,Writer* token)noexcept {
    if(!s||!token)return false;Guard guard(s);
    if(!token->active||token->state!=reinterpret_cast<std::uintptr_t>(s)||!s->active_writers||token->slot>=128||
        s->writer_addresses[token->slot]!=reinterpret_cast<std::uintptr_t>(token))return Fail(s);
    --s->active_writers;s->writer_addresses[token->slot]=0;token->active=false;++s->revision;return !s->lost;
}
bool Created(State* s,const Instance& observed)noexcept {
    if(!s)return false;Guard guard(s);
    if(!s->sealed||!s->active_writers||s->lost||!observed.manager.manager||!observed.manager.generation||
        !observed.object||!observed.vtable||s->generation==UINT64_MAX||
        (observed.life!=Life::Active&&observed.life!=Life::Retired))return Fail(s);
    Instance* slot=nullptr;
    for(auto& entry:s->records){if(Live(entry)&&entry.object==observed.object)return Fail(s);
        if(!slot&&!Live(entry))slot=&entry;}
    if(!slot)return Fail(s);*slot=observed;slot->generation=++s->generation;++s->revision;return true;
}
bool Retired(State* s,xui_content::Key manager,std::uintptr_t object)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||!s->active_writers)return Fail(s);
    for(auto& entry:s->records)if(Live(entry)&&entry.object==object&&Same(entry.manager,manager)){
        entry.life=Life::Retired;++s->revision;return true;}
    return Fail(s);
}
bool AllRetired(State* s,xui_content::Key manager)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||!s->active_writers)return Fail(s);
    for(auto& entry:s->records)if(entry.life==Life::Active&&Same(entry.manager,manager))entry.life=Life::Retired;
    ++s->revision;return true;
}
bool Collected(State* s,xui_content::Key manager,std::uint32_t expected,bool empty,std::uint64_t revision)noexcept {
    if(!s)return false;Guard guard(s);
    if(!s->sealed||s->lost||s->active_writers!=1||!empty||s->revision!=revision)return Fail(s);
    std::size_t count=0;for(const auto& entry:s->records)if(entry.life==Life::Retired&&Same(entry.manager,manager))++count;
    if(count!=expected)return Fail(s);
    for(auto& entry:s->records)if(entry.life==Life::Retired&&Same(entry.manager,manager))entry.life=Life::Destroyed;
    ++s->revision;return true;
}
bool ManagerDestroyed(State* s,xui_content::Key manager)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed)return Fail(s);
    for(auto& entry:s->records)if(Live(entry)&&Same(entry.manager,manager))entry.life=Life::Destroyed;
    ++s->revision;return !s->lost;
}
bool FinishLoad(State* s,std::uint64_t serial,Origin origin)noexcept {
    if(!s||!serial)return false;Guard guard(s);
    if(!s->sealed||s->lost||(origin!=Origin::UiLoadGlobal&&origin!=Origin::UiLoadProject&&origin!=Origin::UiLoadUnknown))return Fail(s);
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
bool ProjectInputInstancesClosed(State* s)noexcept {
    if(!s)return false;Guard guard(s);if(!s->sealed||s->lost||s->active_writers)return false;
    for(const auto& entry:s->records)if(Live(entry)&&entry.epoch&&
        (entry.origin==Origin::UiLoadPending||entry.origin==Origin::UiLoadProject||entry.origin==Origin::UiLoadUnknown))return false;
    return true;
}
} // namespace
