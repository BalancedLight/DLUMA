#define _HAS_EXCEPTIONS 0
#include "LocalizedTextObservationGate.h"
#include <cstring>

extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);

namespace dluma::fixes::custom_map_asset_isolation::localized_texts {
namespace {
constexpr TextHookDescriptor catalog[] = {
    {TextHook::Substitute,"Substitute",0x6B6550,0,{0x44,0x89,0x4c,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55}},
    {TextHook::Exists,"Exists",0x2872A0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
    {TextHook::CopyFiltered,"CopyFiltered",0x6B6190,0,{0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
    {TextHook::SaveBinary,"SaveBinary",0x6B6E90,0,{0x41,0x56,0x48,0x83,0xec,0x50,0x45,0x33,0xc0,0x41,0x8d,0x50,0x01,0xff,0x15,0x2d}},
    {TextHook::Insert,"Insert",0x6B5FE0,0,{0x48,0x85,0xd2,0x0f,0x84,0x98,0x01,0x00,0x00,0x4c,0x8b,0xdc,0x49,0x89,0x4b,0x08}},
    {TextHook::Clear,"Clear",0x287460,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x1d,0x2f,0x59,0x8c}},
    {TextHook::FromFile,"FromFile",0x6F97C0,0,{0x40,0x53,0x56,0x41,0x54,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0xb9,0x90,0x02,0x00}},
    {TextHook::LoadBinary,"LoadBinary",0x6B6B50,0,{0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x48,0x83,0xec,0x28,0x45,0x33,0xed}},
    {TextHook::RemoveBinary,"RemoveBinary",0x6B6D00,0,{0x40,0x53,0x57,0x48,0x83,0xec,0x58,0x45,0x33,0xc0,0x33,0xd2,0x40,0x32,0xff,0xff}},
    {TextHook::RemoveOne,"RemoveOne",0x6B7600,0,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x48}},
    {TextHook::ValueLookup,"ValueLookup",0x6B6AB0,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb6,0x02,0x48,0x8b,0xfa}},
    {TextHook::EntryLookup,"EntryLookup",0x6B7760,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x4c,0x8b,0x01,0x48,0x8b,0xf9}},
    {TextHook::FindOrInsert,"FindOrInsert",0x6B74E0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48}},
    {TextHook::ClearBucket,"ClearBucket",0x6B7440,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x83,0x79,0x10,0x00,0x48,0x8b,0xd9,0x0f,0x8e,0x81}},
    {TextHook::InitializeTable,"InitializeTable",0x6B7300,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0xbf}},
    {TextHook::CopyEntry,"CopyEntry",0x6B7250,0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x41}},
    {TextHook::TableAddress,"TableAddress",0x18AB50,0x8B8E48,{}},
};
static_assert(sizeof(catalog)/sizeof(catalog[0]) == kTextHookCount,"complete catalog");
std::atomic<LocalizedTextObservationGate*> bridge_gate{nullptr};
constexpr unsigned maximum_depth = 128;
struct ThreadState {
    LocalizedTextObservationGate* gate;
    std::uint64_t serial;
    std::uint64_t stack_serial[maximum_depth];
    TextHook stack_kind[maximum_depth];
    unsigned depth;
    unsigned replay_depth;
};
__declspec(thread) ThreadState thread_state = {};
std::size_t Index(TextHook h) noexcept { return static_cast<std::size_t>(h); }
bool IsWriter(TextHook h) noexcept {
    return h==TextHook::Insert || h==TextHook::Clear || h==TextHook::FromFile ||
        h==TextHook::LoadBinary || h==TextHook::RemoveBinary || h==TextHook::RemoveOne ||
        h==TextHook::FindOrInsert || h==TextHook::ClearBucket ||
        h==TextHook::InitializeTable || h==TextHook::CopyEntry;
}
bool HasAncestor(TextHook h) noexcept {
    for(unsigned i=0;i<thread_state.depth;++i) if(thread_state.stack_kind[i]==h) return true;
    return false;
}
bool Read(const LocalizedTextObservationGate* g,std::uintptr_t at,void* out,std::size_t n) noexcept {
    return at && at<=UINTPTR_MAX-n && g->read(g->read_user,at,out,n);
}
bool OriginalEntriesMatch(const LocalizedTextObservationGate* g) noexcept {
    for(const auto& d:catalog) {
        if(d.pointer_slot_rva) {
            std::uintptr_t pointer=0;
            if(!Read(g,g->engine_base+d.pointer_slot_rva,&pointer,sizeof(pointer)) ||
                pointer!=g->engine_base+d.function_rva) return false;
        } else {
            std::uint8_t bytes[16]={};
            if(!Read(g,g->engine_base+d.function_rva,bytes,sizeof(bytes)) ||
                std::memcmp(bytes,d.original_prefix,sizeof(bytes))) return false;
        }
    }
    return true;
}
bool StartupMatches(const LocalizedTextObservationGate* g,const ObservedTextStartup& s) noexcept {
    if(!s.callback_address || s.thread_id!=GetCurrentThreadId() ||
        s.callback_return_address!=g->engine_base+0x24B9AB)return false;
    const std::uintptr_t null_owner_rvas[]={0xACA818,0xACCC68,0xACCC70};
    for(auto rva:null_owner_rvas){std::uintptr_t owner=1;
        if(!Read(g,g->engine_base+rva,&owner,sizeof(owner)) || owner)return false;}
    std::uintptr_t callback=0;std::uint8_t call[2]={};
    return Read(g,g->engine_base+0xACCC58,&callback,sizeof(callback)) && callback==s.callback_address &&
        Read(g,g->engine_base+0x24B9A9,call,sizeof(call)) && call[0]==0xff && call[1]==0xd7;
}
bool ReplayMatches(const ObservedTextAccess* b,TextReplayOperation operation) noexcept {
    if(!b || !b->gate || !b->replay_authority || !b->replay_transaction ||
        !b->replay_lease.generation || b->replay_lease.generation!=b->gate->lease_generation.load() ||
        b->replay_lease.thread_id!=GetCurrentThreadId())return false;
    TextReplayAuthority authority={};
    return b->replay_authority(b->replay_user,&authority) &&
        authority.transaction_generation==b->replay_transaction && authority.operation==operation;
}
bool BeginReplay(LocalizedTextObservationGate* g) noexcept {
    const auto state=InspectTextGate(g);
    if(!state.held_by_this_thread || state.active_calls || state.unknown_borrow ||
        state.lost_observation || state.late_producer || thread_state.depth || thread_state.replay_depth) return false;
    thread_state.gate=g;thread_state.replay_depth=1;return true;
}
void EndReplay(LocalizedTextObservationGate* g) noexcept {
    if(thread_state.gate!=g || thread_state.depth || thread_state.replay_depth!=1) {
        g->lost_observation.store(true);return;
    }
    thread_state.replay_depth=0;thread_state.gate=nullptr;
}
bool BridgeRead(void* user,std::uintptr_t at,void* out,std::size_t n) noexcept {
    const auto* b=static_cast<ObservedTextAccess*>(user);
    return b && b->gate && Read(b->gate,at,out,n);
}

// Each POD token spans the whole native original, including recursion, file
// parsing before the first insert, and copying a borrowed value to owned data.
// SEH unwinding must release the observation even if the original faults.
#define HOOK_BEGIN(name) auto* g=bridge_gate.load(); TextCallToken token={}; \
    BeginTextCall(g,TextHook::name,&token)
#define HOOK_ORIGINAL(type,name) reinterpret_cast<type>(g->originals[Index(TextHook::name)])
#define HOOK_END() __finally { EndTextCall(g,&token); }
std::uintptr_t __fastcall HookSubstitute(void* table,void* out,const std::uint16_t* input,int depth) {
    HOOK_BEGIN(Substitute); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*,const std::uint16_t*,int),Substitute)(table,out,input,depth);} HOOK_END()
}
bool __fastcall HookExists(void* tools,const void* key) {
    HOOK_BEGIN(Exists); __try {return HOOK_ORIGINAL(bool(__fastcall*)(void*,const void*),Exists)(tools,key);} HOOK_END()
}
void __fastcall HookCopyFiltered(void* out,const char* filter) {
    HOOK_BEGIN(CopyFiltered); __try {HOOK_ORIGINAL(void(__fastcall*)(void*,const char*),CopyFiltered)(out,filter);} HOOK_END()
}
void __fastcall HookSaveBinary(const char* path) {
    HOOK_BEGIN(SaveBinary); __try {HOOK_ORIGINAL(void(__fastcall*)(const char*),SaveBinary)(path);} HOOK_END()
}
void __fastcall HookInsert(const char* key,const std::uint16_t* value,unsigned char replace) {
    HOOK_BEGIN(Insert); __try {HOOK_ORIGINAL(void(__fastcall*)(const char*,const std::uint16_t*,unsigned char),Insert)(key,value,replace);} HOOK_END()
}
std::uintptr_t __fastcall HookClear() {
    HOOK_BEGIN(Clear); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(),Clear)();} HOOK_END()
}
unsigned char __fastcall HookFromFile(const char* path,unsigned char replace) {
    HOOK_BEGIN(FromFile); __try {return HOOK_ORIGINAL(unsigned char(__fastcall*)(const char*,unsigned char),FromFile)(path,replace);} HOOK_END()
}
std::uintptr_t __fastcall HookLoadBinary(const char* path,unsigned char clear) {
    HOOK_BEGIN(LoadBinary); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(const char*,unsigned char),LoadBinary)(path,clear);} HOOK_END()
}
std::uintptr_t __fastcall HookRemoveBinary(const char* path) {
    HOOK_BEGIN(RemoveBinary); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(const char*),RemoveBinary)(path);} HOOK_END()
}
std::uintptr_t __fastcall HookRemoveOne(const char** key) {
    HOOK_BEGIN(RemoveOne); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(const char**),RemoveOne)(key);} HOOK_END()
}
std::uintptr_t __fastcall HookValueLookup(void* table,const char* key) {
    HOOK_BEGIN(ValueLookup); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(void*,const char*),ValueLookup)(table,key);} HOOK_END()
}
std::uintptr_t __fastcall HookEntryLookup(const char** key) {
    HOOK_BEGIN(EntryLookup); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(const char**),EntryLookup)(key);} HOOK_END()
}
std::uintptr_t __fastcall HookFindOrInsert(const char** key,const void* value) {
    HOOK_BEGIN(FindOrInsert); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(const char**,const void*),FindOrInsert)(key,value);} HOOK_END()
}
std::uintptr_t __fastcall HookClearBucket(void* bucket) {
    HOOK_BEGIN(ClearBucket); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(void*),ClearBucket)(bucket);} HOOK_END()
}
std::uintptr_t __fastcall HookInitializeTable() {
    HOOK_BEGIN(InitializeTable); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(),InitializeTable)();} HOOK_END()
}
std::uintptr_t __fastcall HookCopyEntry(void* entry,const char* key,const std::uint16_t* value) {
    HOOK_BEGIN(CopyEntry); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(void*,const char*,const std::uint16_t*),CopyEntry)(entry,key,value);} HOOK_END()
}
std::uintptr_t __fastcall HookTableAddress() {
    HOOK_BEGIN(TableAddress); __try {return HOOK_ORIGINAL(std::uintptr_t(__fastcall*)(),TableAddress)();} HOOK_END()
}
#undef HOOK_BEGIN
#undef HOOK_ORIGINAL
#undef HOOK_END
}  // namespace

const TextHookDescriptor* TextHookCatalog(std::size_t* count) noexcept {
    if(count)*count=kTextHookCount;return catalog;
}
std::uintptr_t TextHookWrapper(TextHook kind) noexcept {
    switch(kind) {
#define CASE(name) case TextHook::name:return reinterpret_cast<std::uintptr_t>(&Hook##name)
    CASE(Substitute);CASE(Exists);CASE(CopyFiltered);CASE(SaveBinary);CASE(Insert);
    CASE(Clear);CASE(FromFile);CASE(LoadBinary);CASE(RemoveBinary);CASE(RemoveOne);
    CASE(ValueLookup);CASE(EntryLookup);CASE(FindOrInsert);CASE(ClearBucket);
    CASE(InitializeTable);CASE(CopyEntry);CASE(TableAddress);
#undef CASE
    default:return 0;
    }
}
TextGateResult InitializeTextGate(LocalizedTextObservationGate* g,
    std::uintptr_t base,const char* sha,ReadMemoryFn read,void* user,
    const std::uintptr_t* originals,std::size_t count,const ObservedTextStartup* startup) noexcept {
    if(!g || !base || base>UINTPTR_MAX-0xE8D000U || !sha || !read || !originals ||
        count!=kTextHookCount || !startup) return TextGateResult::InvalidArgument;
    if(g->initialized) return TextGateResult::AlreadyInitialized;
    if(std::strlen(sha)!=sizeof(kPlayerEngineSha256)-1 ||
        std::memcmp(sha,kPlayerEngineSha256,sizeof(kPlayerEngineSha256))) return TextGateResult::ProfileMismatch;
    for(std::size_t i=0;i<count;++i)
        if(!originals[i] || originals[i]==TextHookWrapper(catalog[i].kind) ||
            (!catalog[i].pointer_slot_rva && originals[i]==base+catalog[i].function_rva))
            return TextGateResult::InvalidArgument;
    g->engine_base=base;g->read=read;g->read_user=user;
    if(!OriginalEntriesMatch(g)) return TextGateResult::ProfileMismatch;
    if(!StartupMatches(g,*startup))return TextGateResult::StartupUnproven;
    g->startup=*startup;
    std::memcpy(g->originals,originals,sizeof(g->originals));g->initialized=true;
    return TextGateResult::Succeeded;
}
TextGateResult BindTextHookBridge(LocalizedTextObservationGate* g) noexcept {
    if(!g || !g->initialized)return TextGateResult::InvalidArgument;
    if(!StartupMatches(g,g->startup))return TextGateResult::StartupUnproven;
    LocalizedTextObservationGate* expected=nullptr;
    return bridge_gate.compare_exchange_strong(expected,g) || expected==g ?
        TextGateResult::Succeeded : TextGateResult::Busy;
}
TextGateResult SealTextHookStartup(LocalizedTextObservationGate* g) noexcept {
    if(!g || !g->initialized)return TextGateResult::InvalidArgument;
    if(g->startup_sealed || !StartupMatches(g,g->startup))return TextGateResult::StartupUnproven;
    const auto result=VerifyTextHookCoverage(g);if(result!=TextGateResult::Succeeded)return result;
    if(g->active_calls.load() || g->owner_thread.load())return TextGateResult::Busy;
    if(g->unknown_borrow.load())return TextGateResult::UnsafeBorrow;
    if(g->lost_observation.load())return TextGateResult::LostObservation;
    if(g->late_producer.load())return TextGateResult::LateProducer;
    g->startup_sealed=true;return TextGateResult::Succeeded;
}
TextGateResult VerifyTextHookCoverage(const LocalizedTextObservationGate* g) noexcept {
    if(!g || !g->initialized || bridge_gate.load()!=g)return TextGateResult::IncompleteCoverage;
    for(const auto& d:catalog) {
        const auto hook=TextHookWrapper(d.kind);
        if(!g->originals[Index(d.kind)])return TextGateResult::IncompleteCoverage;
        if(d.pointer_slot_rva) {
            std::uintptr_t pointer=0;
            if(!Read(g,g->engine_base+d.pointer_slot_rva,&pointer,sizeof(pointer)) || pointer!=hook)
                return TextGateResult::IncompleteCoverage;
        } else {
            // Root's atomic detours are FF25 + zero displacement + owned
            // 64-bit wrapper address. No generic 'some jump exists' approval.
            std::uint8_t bytes[14]={};std::uintptr_t target=0;
            if(!Read(g,g->engine_base+d.function_rva,bytes,sizeof(bytes)))return TextGateResult::IncompleteCoverage;
            std::memcpy(&target,bytes+6,sizeof(target));
            if(bytes[0]!=0xff || bytes[1]!=0x25 || bytes[2] || bytes[3] || bytes[4] || bytes[5] || target!=hook)
                return TextGateResult::IncompleteCoverage;
        }
    }
    return TextGateResult::Succeeded;
}
void BeginTextCall(LocalizedTextObservationGate* g,TextHook kind,TextCallToken* token) noexcept {
    if(!g || !g->initialized || !token || Index(kind)>=kTextHookCount)return;
    if(token->active || (thread_state.gate && thread_state.gate!=g) || thread_state.depth>=maximum_depth) {
        g->lost_observation.store(true);return;
    }
    const bool outer=thread_state.depth==0;
    const auto tid=GetCurrentThreadId();
    if(outer) {
        for(;;) {
            const auto owner=g->owner_thread.load();
            if(owner && owner!=tid) {
                // This call began after the reset's close boundary. Let the
                // native function resume only after reopening, but never
                // report the replay as clean if an old producer was queued.
                if(IsWriter(kind))g->late_producer.store(true);
                Sleep(1);continue;
            }
            g->active_calls.fetch_add(1);
            const auto after=g->owner_thread.load();
            if(!after || after==tid) break;
            if(IsWriter(kind))g->late_producer.store(true);
            g->active_calls.fetch_sub(1);
        }
        // An unexpected native call on the closed gate's owner cannot wait
        // for itself. Preserve native semantics but poison this transaction.
        if(g->owner_thread.load()==tid && !thread_state.replay_depth)
            g->lost_observation.store(true);
        thread_state.gate=g;
    }
    if(kind==TextHook::TableAddress ||
        (kind==TextHook::ValueLookup && !HasAncestor(TextHook::Substitute)) ||
        ((kind==TextHook::EntryLookup || kind==TextHook::FindOrInsert || kind==TextHook::CopyEntry) &&
            !HasAncestor(TextHook::Insert))) g->unknown_borrow.store(true);
    const auto serial=++thread_state.serial;
    if(!serial)g->lost_observation.store(true);
    const auto depth=thread_state.depth++;
    thread_state.stack_serial[depth]=serial;thread_state.stack_kind[depth]=kind;
    const bool observe=IsWriter(kind) && !thread_state.replay_depth;
    if(observe && g->commit_watch_transaction.load())g->late_producer.store(true);
    if(observe)g->revision.fetch_add(1);
    *token={reinterpret_cast<std::uintptr_t>(g),serial,depth,kind,true,outer,observe};
}
void EndTextCall(LocalizedTextObservationGate* g,TextCallToken* token) noexcept {
    if(!g || !token)return;
    if(!token->active || token->gate_identity!=reinterpret_cast<std::uintptr_t>(g) ||
        thread_state.gate!=g || !thread_state.depth || token->depth!=thread_state.depth-1 ||
        thread_state.stack_serial[token->depth]!=token->serial) {
        g->lost_observation.store(true);return;
    }
    if(token->revision_observed)g->revision.fetch_add(1);
    --thread_state.depth;token->active=false;
    if(token->outer) {
        g->active_calls.fetch_sub(1);
        if(!thread_state.replay_depth)thread_state.gate=nullptr;
    }
}
TextGateState InspectTextGate(const LocalizedTextObservationGate* g) noexcept {
    if(!g)return {};
    const auto owner=g->owner_thread.load();
    return {g->revision.load(),g->active_calls.load(),owner!=0,
        owner==GetCurrentThreadId() && g->held.load(),
        g->unknown_borrow.load(),g->lost_observation.load(),g->late_producer.load()};
}
TextGateResult CloseTextGate(LocalizedTextObservationGate* g,std::uint32_t timeout,TextGateLease* lease) noexcept {
    if(!g || !g->initialized || !lease || lease->generation || timeout>5000)return TextGateResult::InvalidArgument;
    if(!g->startup_sealed)return TextGateResult::StartupUnproven;
    if(thread_state.depth || thread_state.replay_depth)return TextGateResult::Busy;
    auto result=VerifyTextHookCoverage(g);if(result!=TextGateResult::Succeeded)return result;
    if(g->unknown_borrow.load())return TextGateResult::UnsafeBorrow;
    if(g->lost_observation.load())return TextGateResult::LostObservation;
    if(g->late_producer.load())return TextGateResult::LateProducer;
    const auto tid=GetCurrentThreadId();std::uint32_t expected=0;
    if(!g->owner_thread.compare_exchange_strong(expected,tid))return TextGateResult::Busy;
    const auto started=GetTickCount64();
    while(g->active_calls.load()) {
        if(GetTickCount64()-started>=timeout) {g->owner_thread.store(0);return TextGateResult::TimedOut;}
        Sleep(1);
    }
    result=VerifyTextHookCoverage(g);
    if(g->unknown_borrow.load())result=TextGateResult::UnsafeBorrow;
    if(g->lost_observation.load())result=TextGateResult::LostObservation;
    if(g->late_producer.load())result=TextGateResult::LateProducer;
    if(result!=TextGateResult::Succeeded){g->owner_thread.store(0);return result;}
    const auto generation=g->lease_generation.fetch_add(1)+1;
    if(!generation){g->lost_observation.store(true);g->owner_thread.store(0);return TextGateResult::LostObservation;}
    g->held.store(true);*lease={generation,tid};return TextGateResult::Succeeded;
}
TextGateResult OpenTextGate(LocalizedTextObservationGate* g,const TextGateLease* lease) noexcept {
    if(!g || !lease || !lease->generation || g->lease_generation.load()!=lease->generation)return TextGateResult::WrongLease;
    if(lease->thread_id!=GetCurrentThreadId() || g->owner_thread.load()!=lease->thread_id)return TextGateResult::WrongThread;
    if(thread_state.depth || thread_state.replay_depth)return TextGateResult::Busy;
    g->held.store(false);g->owner_thread.store(0);
    if(g->late_producer.load())return TextGateResult::LateProducer;
    if(g->unknown_borrow.load())return TextGateResult::UnsafeBorrow;
    if(g->lost_observation.load())return TextGateResult::LostObservation;
    return TextGateResult::Succeeded;
}
TextGateResult BeginTextCommitWatch(LocalizedTextObservationGate* g,const TextGateLease* lease,std::uint64_t transaction) noexcept {
    if(!g || !lease || !transaction || !lease->generation ||
        lease->generation!=g->lease_generation.load())return TextGateResult::WrongLease;
    const auto state=InspectTextGate(g);
    if(lease->thread_id!=GetCurrentThreadId() || !state.held_by_this_thread)return TextGateResult::WrongThread;
    if(state.active_calls || state.late_producer || state.unknown_borrow || state.lost_observation)return TextGateResult::Busy;
    std::uint64_t expected=0;
    return g->commit_watch_transaction.compare_exchange_strong(expected,transaction) ? TextGateResult::Succeeded : TextGateResult::Busy;
}
TextGateResult CompleteTextCommitWatch(LocalizedTextObservationGate* g,const TextGateLease* lease,std::uint64_t transaction) noexcept {
    if(!g || !lease || !transaction || !lease->generation ||
        lease->generation!=g->lease_generation.load() ||
        transaction!=g->commit_watch_transaction.load())return TextGateResult::WrongLease;
    const auto state=InspectTextGate(g);
    if(lease->thread_id!=GetCurrentThreadId() || !state.held_by_this_thread)return TextGateResult::WrongThread;
    if(state.late_producer)return TextGateResult::LateProducer;
    if(state.unknown_borrow)return TextGateResult::UnsafeBorrow;
    if(state.lost_observation)return TextGateResult::LostObservation;
    if(state.active_calls)return TextGateResult::Busy;
    g->commit_watch_transaction.store(0);return TextGateResult::Succeeded;
}
bool CaptureObservedTextFence(void* user,TextFence* out) noexcept {
    auto* b=static_cast<ObservedTextAccess*>(user);
    if(!b || !b->gate || !b->context || !out)return false;
    *out={};if(!b->context(b->context_user,out))return false;
    const auto s=InspectTextGate(b->gate);
    out->mutation_revision=s.revision;
    out->producer_admission_closed=s.admission_closed && s.held_by_this_thread;
    out->readers_drained=s.active_calls==0 && !s.unknown_borrow && !s.lost_observation && !s.late_producer;
    return true;
}
bool VerifyObservedTextProfile(void* user) noexcept {
    const auto* b=static_cast<ObservedTextAccess*>(user);
    return b && b->gate && VerifyTextHookCoverage(b->gate)==TextGateResult::Succeeded;
}
bool MakeObservedTextAccess(ObservedTextAccess* b,NativeTextAccess* access) noexcept {
    if(!b || !b->gate || !b->context || !b->replay_authority || !access ||
        !b->gate->startup_sealed || !VerifyObservedTextProfile(b))return false;
    *access={};access->engine_base=b->gate->engine_base;
    std::memcpy(access->engine_sha256,kPlayerEngineSha256,sizeof(kPlayerEngineSha256));
    access->read=BridgeRead;access->fence=CaptureObservedTextFence;access->user=b;
    access->clear=InvokeObservedTextClear;access->insert=InvokeObservedTextInsert;
    access->owned_profile=VerifyObservedTextProfile;return true;
}
bool BindObservedTextReplay(ObservedTextAccess* b,const TextGateLease* lease,std::uint64_t transaction) noexcept {
    if(!b || !b->gate || !lease || !transaction || transaction<=b->replay_transaction ||
        !lease->generation || lease->generation!=b->gate->lease_generation.load() ||
        lease->thread_id!=GetCurrentThreadId() || !InspectTextGate(b->gate).held_by_this_thread)return false;
    b->replay_lease=*lease;b->replay_transaction=transaction;return true;
}
bool InvokeObservedTextClear(void* user,std::uintptr_t function) noexcept {
    auto* b=static_cast<ObservedTextAccess*>(user);
    if(!b || !b->gate || function!=b->gate->engine_base+kClearTextsRva ||
        !ReplayMatches(b,TextReplayOperation::Clear) || !VerifyObservedTextProfile(b) || !BeginReplay(b->gate))return false;
    bool succeeded=false;
    __try {__try {HookClear();succeeded=true;} __except(1) {b->gate->lost_observation.store(true);}}
    __finally {EndReplay(b->gate);}
    return succeeded && !b->gate->lost_observation.load();
}
bool InvokeObservedTextInsert(void* user,std::uintptr_t function,const char* key,const std::uint16_t* value) noexcept {
    auto* b=static_cast<ObservedTextAccess*>(user);
    if(!b || !b->gate || !key || !value || function!=b->gate->engine_base+kInsertTextRva ||
        !ReplayMatches(b,TextReplayOperation::Insert) || !VerifyObservedTextProfile(b) || !BeginReplay(b->gate))return false;
    bool succeeded=false;
    __try {__try {HookInsert(key,value,0);succeeded=true;} __except(1) {b->gate->lost_observation.store(true);}}
    __finally {EndReplay(b->gate);}
    return succeeded && !b->gate->lost_observation.load();
}
}  // namespace dluma::fixes::custom_map_asset_isolation::localized_texts
