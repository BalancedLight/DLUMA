#define _HAS_EXCEPTIONS 0
#include "CurveNativeGateway.h"
#include "CurveProcessCaches.h"
#include <cstring>
#include <intrin.h>

extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64();
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned long);
extern "C" __declspec(dllimport) void* __stdcall RtlLookupFunctionEntry(unsigned long long,unsigned long long*,void*);

namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
constexpr CurveHookDescriptor catalog[]={
    {CurveHook::Load,"CurveLoad",0x10E1BF0,0,{0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41},16,17,-1,-1},
    {CurveHook::Reload,"CurveReload",0x10E1B70,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x15,0xa7,0xd0,0xbb},16,17,13,17},
    {CurveHook::GetId,"CurveGetId",0x10E1880,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33},16,17,-1,-1},
    {CurveHook::FindId,"CurveFindId",0x10E1910,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33},16,17,-1,-1},
    {CurveHook::GetIndex,"CurveGetIndex",0x10E19D0,kManagerVtableRva+40,{},0,0,-1,-1},
    {CurveHook::GetName,"CurveGetName",0x10E19A0,kManagerVtableRva+48,{},0,0,-1,-1},
    {CurveHook::CopyPointers,"CurveCopyPointers",0x10E1B50,kManagerVtableRva+56,{},0,0,-1,-1},
    {CurveHook::Count,"CurveCount",0x10E1A00,kManagerVtableRva+64,{},0,0,-1,-1},
    {CurveHook::Add,"CurveAdd",0x10E1A10,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xda,0x48,0x83,0xc2,0x38},16,16,-1,-1},
    {CurveHook::RebuildAll,"CurveRebuildIndex",0x10E1AA0,kManagerVtableRva+80,{},0,0,-1,-1},
    {CurveHook::RebuildSamples,"CurveRebuildSamples",0x111F430,0,{0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x33},16,17,-1,-1},
    {CurveHook::DestroySpline,"CurveDestroySpline",0x11958C0,kSplineVtableRva+8,{},0,0,-1,-1},
    {CurveHook::EvaluateSamples,"CurveEvaluateSamples",0x111F530,0,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0x01,0x0f,0x29,0x74,0x24,0x30,0x48,0x8b},16,17,-1,-1},
    {CurveHook::EvaluateSpline,"CurveEvaluateSpline",0x1195AA0,0,{0x48,0x83,0xec,0x38,0x44,0x8b,0x51,0x68,0x45,0x33,0xc0,0x0f,0x28,0xe9,0x4c,0x8b},16,17,-1,-1},
    {CurveHook::MaxX,"CurveMaxX",0x1195C60,kSplineVtableRva+40,{},0,0,-1,-1},
    {CurveHook::MinX,"CurveMinX",0x1195C50,kSplineVtableRva+48,{},0,0,-1,-1},
    {CurveHook::LoadXmlPoints,"CurveLoadXmlPoints",0x1195DA0,0,{0x44,0x88,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x53,0x56,0x57,0x41,0x54},16,16,-1,-1},
    {CurveHook::LoadBinaryPoints,"CurveLoadBinaryPoints",0x1196440,0,{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x48,0x8b,0xec,0x48},16,19,-1,-1},
    {CurveHook::AddPoint,"CurveAddPoint",0x1195940,0,{0x40,0x53,0x48,0x83,0xec,0x40,0x44,0x8b,0x51,0x68,0x45,0x33,0xc0,0x33,0xc0,0x48},16,18,-1,-1},
    {CurveHook::RemovePoint,"CurveRemovePoint",0x1195A40,kSplineVtableRva+112,{},0,0,-1,-1},
    {CurveHook::ItemDefinitions,"ItemLoadDefinitions",0x67DF00,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48},16,20,-1,-1},
    {CurveHook::ItemParser,"ItemDefinitionParser",0x6804A0,0,{0x44,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x41,0x54,0x41,0x55},16,16,-1,-1},
    {CurveHook::SaveManager,"CurveSaveManager",0x10E1710,kManagerVtableRva+16,{},0,0,-1,-1},
    {CurveHook::RebuildEveryCurve,"CurveRebuildEveryCurve",0x10E1B00,kManagerVtableRva+88,{},0,0,-1,-1},
    {CurveHook::NearestPoint,"CurveNearestPoint",0x1195C80,kSplineVtableRva+120,{},0,0,-1,-1},
    {CurveHook::SetPoint,"CurveSetPoint",0x1195D70,kSplineVtableRva+128,{},0,0,-1,-1},
    {CurveHook::ConstructSpline,"CurveConstructSpline",0x1195800,0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x33,0xff,0x48},16,19,-1,-1},
    {CurveHook::RandomDistribution,"CurveRandomDistribution",0x689530,0,{0x4c,0x8b,0xdc,0x49,0x89,0x73,0x20,0xf3,0x0f,0x11,0x54,0x24,0x18,0xf3,0x0f,0x11},16,19,-1,-1},
    {CurveHook::LevelDistribution,"CurveLevelDistribution",0x70D5B0,0,{0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55},16,16,-1,-1},
    {CurveHook::RepairValue,"CurveRepairValue",0xA89600,0,{0x40,0x55,0x53,0x57,0x41,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24,0x90,0x48,0x81,0xec},16,20,-1,-1},
    {CurveHook::SalvageValue,"CurveSalvageValue",0xA89C90,0,{0x40,0x55,0x53,0x56,0x57,0x48,0x8d,0x6c,0x24,0x88,0x48,0x81,0xec,0x78,0x01,0x00},16,17,-1,-1},
    {CurveHook::RepairPartsMultiplier,"CurveRepairPartsMultiplier",0xB42D70,0,{0x40,0x57,0x48,0x83,0xec,0x40,0x8b,0x05,0x24,0x51,0x2c,0x01,0x48,0x89,0x5c,0x24},16,17,8,12},
    {CurveHook::FighterDamageXp,"CurveFighterDamageXp",0xD60CD0,0,{0x40,0x55,0x56,0x41,0x55,0x48,0x81,0xec,0x20,0x01,0x00,0x00,0x48,0x8b,0x01,0x4d},16,18,-1,-1},
    {CurveHook::ItemFamilyLoad,"ItemFamilyLoad",0x45C160,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x4c},16,20,-1,-1},
    {CurveHook::ItemRowsXml,"ItemRowsXml",0x67FF50,0,{0x40,0x55,0x53,0x48,0x8d,0xac,0x24,0xc8,0xfe,0xfe,0xff,0xb8,0x38,0x02,0x01,0x00},16,16,-1,-1},
    {CurveHook::ItemCutTypes,"ItemCutTypes",0x68F2F0,0,{0x40,0x55,0x57,0x41,0x55,0x48,0x8d,0xac,0x24,0x60,0xf9,0xff,0xff,0x48,0x81,0xec},16,20,-1,-1},
    {CurveHook::ItemAffixes,"ItemAffixes",0x68F8D0,0,{0x40,0x55,0x57,0x41,0x55,0x48,0x8d,0xac,0x24,0xd0,0xfc,0xff,0xff,0x48,0x81,0xec},16,20,-1,-1},
    {CurveHook::ItemFinalize,"ItemFinalize",0x67FBD0,0,{0x40,0x53,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0xe8,0x32,0xeb,0xff,0xff,0x44,0x8b},16,21,10,14,17,21,0},
    {CurveHook::ItemClear,"ItemClear",0x67DE60,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x05,0x18,0x27,0x79,0x01,0xa8,0x01,0x75,0x1a},16,16,8,12,0,0,14},
    {CurveHook::ItemDestroy,"ItemDestroy",0x67DBA0,0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48},16,22,18,22},
    {CurveHook::ItemConstruct,"ItemConstruct",0x67D730,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x0f,0x57,0xdb,0xf3,0x0f,0x10,0x0d,0xf7,0x23,0xe0},16,17,13,17},
    {CurveHook::ItemRegisterEnum,"ItemRegisterEnum",0x682820,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x05,0x58,0xdd,0x78,0x01,0x48,0x8b,0xd9,0xa8},16,17,8,12},
    {CurveHook::ItemRegisterSavedEnum,"ItemRegisterSavedEnum",0x6828D0,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x05,0xa8,0xdc,0x78,0x01,0x48,0x8b,0xd9,0xa8},16,17,8,12},
    {CurveHook::ItemRegisterCannonEnum,"ItemRegisterCannonEnum",0x682980,0,{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x05,0xf8,0xdb,0x78,0x01,0x48,0x8b,0xd9,0xa8},16,17,8,12},
    {CurveHook::ItemGenerateEnum,"ItemGenerateEnum",0x682A30,0,{0x41,0x54,0x41,0x55,0x48,0x83,0xec,0x38,0x45,0x33,0xed,0x4c,0x8b,0xe1,0x44,0x39},16,21,-1,-1},
    {CurveHook::ItemGenerateSavedEnum,"ItemGenerateSavedEnum",0x682BB0,0,{0x48,0x89,0x6c,0x24,0x20,0x56,0x48,0x83,0xec,0x20,0x33,0xed,0x48,0x8b,0xf1,0x39},16,21,-1,-1},
    {CurveHook::ItemGenerateCannonEnum,"ItemGenerateCannonEnum",0x682C80,0,{0x40,0x55,0x41,0x54,0x48,0x83,0xec,0x38,0x45,0x33,0xe4,0x48,0x8b,0xe9,0x44,0x39},16,21,-1,-1},
};
static_assert(sizeof(catalog)/sizeof(catalog[0])==kCurveHookCount,"complete Curves routes");
constexpr bool ValidDetourSizes() noexcept {
    for(const auto& d:catalog)if(!d.pointer_slot_rva){
        if((d.function_rva&15U)||d.stolen_bytes<16||d.stolen_bytes>32)return false;
        if(d.trailing_branch_offset&&d.trailing_branch_offset+2!=d.stolen_bytes)return false;
        if(d.second_displacement_offset&&
            (d.second_displacement_offset+4>d.stolen_bytes||d.second_instruction_end>d.stolen_bytes))return false;
    }return true;
}
static_assert(ValidDetourSizes(),"every Curve/Item entry must fit the atomic 16-byte patch");
std::atomic<CurveNativeGateway*> bridge{nullptr};
constexpr std::uint32_t publication_bit=0x80000000U;
struct ThreadState {CurveNativeGateway* gate;std::uint64_t serial,stack[128];CurveHook kinds[128];unsigned depth;};
__declspec(thread) ThreadState tls={};
void Lock(CurveNativeGateway* g) noexcept {while(g->records_lock.test_and_set(std::memory_order_acquire)) {}}
void Unlock(CurveNativeGateway* g) noexcept {g->records_lock.clear(std::memory_order_release);}
std::size_t Index(CurveHook h) noexcept {return static_cast<std::size_t>(h);}
bool Read(CurveNativeGateway* g,std::uintptr_t p,void* out,std::size_t n) noexcept {
    return g && p && p<=UINTPTR_MAX-n && g->callbacks.read(g->callbacks.user,p,out,n);
}
bool Writer(CurveHook h) noexcept {
    return h==CurveHook::Load || h==CurveHook::Reload || h==CurveHook::Add || h==CurveHook::RebuildAll ||
        h==CurveHook::RebuildSamples || h==CurveHook::DestroySpline || h==CurveHook::LoadXmlPoints ||
        h==CurveHook::LoadBinaryPoints || h==CurveHook::AddPoint || h==CurveHook::RemovePoint ||
        h==CurveHook::ItemDefinitions || h==CurveHook::ItemParser || h==CurveHook::RebuildEveryCurve || h==CurveHook::SetPoint || h==CurveHook::ConstructSpline ||
        h==CurveHook::RandomDistribution || h==CurveHook::LevelDistribution || h==CurveHook::RepairValue || h==CurveHook::SalvageValue ||
        h==CurveHook::RepairPartsMultiplier || h==CurveHook::FighterDamageXp ||
        (h>=CurveHook::ItemFamilyLoad && h<=CurveHook::ItemGenerateCannonEnum);
}
bool PotentialWriter(CurveNativeGateway* g,CurveHook h) noexcept {
    if(h<CurveHook::RandomDistribution || h>CurveHook::FighterDamageXp)return Writer(h);
    std::size_t count=0;const auto* caches=CurveProcessCacheCatalog(&count);
    const auto index=Index(h)-Index(CurveHook::RandomDistribution);std::uint32_t flag=0;
    if(index>=count || !Read(g,g->module_base+caches[index].flag_rva,&flag,4) || flag!=1)return true;
    if(h!=CurveHook::LevelDistribution)return false;
    std::int32_t id=0;std::uint8_t valid=0;
    return !Read(g,g->module_base+caches[index].id_rva,&id,4) ||
        (id!=-1 && (!Read(g,g->module_base+0x1DF4FA0,&valid,1) || valid!=1));
}
bool Ancestor(CurveHook h) noexcept {for(unsigned i=0;i+1<tls.depth;++i)if(tls.kinds[i]==h)return true;return false;}
std::size_t Find(CurveNativeGateway* g,std::uintptr_t object) noexcept {
    for(std::size_t i=g->appended_count;i>0;--i)if(g->appended[i-1].object==object && !g->appended[i-1].retired)return i-1;
    return kMaximumCurves;
}
__declspec(noinline) bool Context(CurveNativeGateway* g,CurveGatewayContext* c) noexcept {
    // Keep the large authority trace initialization outside the SEH wrappers.
    // Inlining its aggregate assignment into them crashes the MSVC optimizer.
    std::memset(c,0,sizeof(*c));const auto capture=g->callbacks.context;
    return capture && capture(g->callbacks.user,c);
}
std::uintptr_t FunctionBegin(CurveNativeGateway* g,std::uintptr_t caller) noexcept {
    if(caller<g->module_base || caller-g->module_base>=0x2082000)return 0;
    unsigned long long base=0;auto* entry=RtlLookupFunctionEntry(caller-1,&base,nullptr);
    std::uint32_t begin=0;if(!entry || base!=g->module_base || !Read(g,reinterpret_cast<std::uintptr_t>(entry),&begin,4))return 0;
    return begin;
}
bool TransientCaller(std::uintptr_t rva) noexcept {
    // Exact local caller audit: these functions consume pointers in their own
    // frame/typed scope; the three field-retaining owners are handled separately.
    constexpr std::uintptr_t functions[]={0x7D5D0,0x95507,0x95F10,0x1AAE80,0x1AAED0,0x1AB710,
        0x25CA20,0x6318C0,0x689530,0x70D5B0,0xA0DD73,0xA0FA79,0xA89600,0xA89C90,
        0xB26A40,0xB35AF0,0xB42D70,0xB94020,0xC294D0,0xC6CA00,0xC70E95,0xC71310,0xCD0800,
        0xCD2C30,0xCD69D0,0xD60CD0,0xF0A2F0,0xF0A390,0xF814D0,0x1001530,0x1006850,0x1006940,
        0x10075E0,0x1009C10,0x100B4B0,0x1101D20,0x11099C0,0x1109AA0,0x1109BF0,0x11508BC,0x1150947};
    for(auto value:functions)if(value==rva)return true;return false;
}
void UnknownPointers(CurveNativeGateway* g) noexcept {
    Lock(g);for(std::size_t i=0;i<g->appended_count;++i)if(!g->appended[i].retired)g->appended[i].unknown_borrow=true;Unlock(g);
}
bool BridgeRead(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {return Read(static_cast<CurveNativeGateway*>(user),p,out,n);}
bool ManagerContains(CurveNativeGateway* g,std::uintptr_t object,bool* found) noexcept {
    *found=false;std::uintptr_t vector=0;std::uint32_t count=0;
    if(!Read(g,g->module_base+kVectorRva,&vector,8) || !Read(g,g->module_base+kVectorRva+8,&count,4) || count>kMaximumCurves)return false;
    for(std::uint32_t i=0;i<count;++i){std::uintptr_t value=0;if(!Read(g,vector+8ULL*i,&value,8))return false;if(value==object){*found=true;break;}}
    return true;
}
void NoteAllocationAdd(CurveNativeGateway* g,std::uintptr_t object,bool adopted,bool ignored) noexcept {
    Lock(g);for(std::size_t i=0;i<g->allocation_count;++i){auto& a=g->allocations[i];
        if(!a.retired && a.object==object){a.adopted=adopted;a.ignored=ignored;break;}}Unlock(g);
}
void FinishLoad(CurveNativeGateway* g,const CurveLoadToken& load) noexcept {
    CurveSourceReceipt final={};const bool valid=CurrentCurveLoadSource(&final);
    Lock(g);for(std::size_t i=0;i<g->appended_count;++i){auto& r=g->appended[i];
        if(r.retired || r.origin.load_serial!=load.serial || r.origin.load_thread_id!=GetCurrentThreadId())continue;
        if(!valid || r.origin.source.address!=final.source.address || r.origin.source.generation!=final.source.generation ||
            r.origin.source_revision!=final.source_revision || r.origin.open_root!=final.open_root ||
            r.origin.project_epoch!=final.project_epoch || r.origin.ownership!=final.ownership)r.origin.valid=false;}
    Unlock(g);
}
#define BEGIN(name) auto* g=bridge.load();CurveCallToken token={};BeginCurveCall(g,CurveHook::name,&token)
#define ORIGINAL(type,name) reinterpret_cast<type>(g->originals[Index(CurveHook::name)])
#define END() __finally {EndCurveCall(g,&token);}
void __fastcall HookLoad(void* self,const void* path) {
    BEGIN(Load);CurveLoadToken load={};CurveGatewayContext context;char name[512]={};std::uintptr_t pointer=0;
    bool observed=Context(g,&context) && Read(g,reinterpret_cast<std::uintptr_t>(path),&pointer,8);
    if(observed){std::size_t i=0;for(;i<sizeof(name);++i){if(!Read(g,pointer+i,name+i,1)){observed=false;break;}if(!name[i])break;}
        observed=observed && i<sizeof(name) && BeginCurveLoadObservation(name,context.epoch,&load);}
    if(!observed)g->lost.store(true);
    bool completed=false;
    __try {ORIGINAL(void(__fastcall*)(void*,const void*),Load)(self,path);completed=true;}
    __finally {if(load.active){FinishLoad(g,load);
        if(!completed || !FinishCurvePrivateAllocations(g,&load))g->lost.store(true);
        if(!EndCurveLoadObservation(&load))g->lost.store(true);}EndCurveCall(g,&token);}
}
std::uintptr_t __fastcall HookReload(void* self) {BEGIN(Reload);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*),Reload)(self);}END()}
int __fastcall HookGetId(void* self,const void* key) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(GetId);
    __try{const auto result=ORIGINAL(int(__fastcall*)(void*,const void*),GetId)(self,key);ObserveCurveId(g,result,caller);return result;}END()}
int __fastcall HookFindId(void* self,const void* key) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(FindId);
    __try{const auto result=ORIGINAL(int(__fastcall*)(void*,const void*),FindId)(self,key);ObserveCurveId(g,result,caller);return result;}END()}
std::uintptr_t __fastcall HookGetIndex(void* self,int index) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(GetIndex);
    __try {const auto value=ORIGINAL(std::uintptr_t(__fastcall*)(void*,int),GetIndex)(self,index);
        if(!Ancestor(CurveHook::GetName))ObserveCurvePointer(g,value,caller);return value;}END()
}
std::uintptr_t __fastcall HookGetName(void* self,const void* key) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(GetName);
    __try {const auto value=ORIGINAL(std::uintptr_t(__fastcall*)(void*,const void*),GetName)(self,key);ObserveCurvePointer(g,value,caller);return value;}END()
}
std::uintptr_t __fastcall HookCopyPointers(void* self,void* out) {BEGIN(CopyPointers);__try{const auto result=ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*),CopyPointers)(self,out);UnknownPointers(g);return result;}END()}
unsigned __fastcall HookCount(void* self) {BEGIN(Count);__try{return ORIGINAL(unsigned(__fastcall*)(void*),Count)(self);}END()}
int __fastcall HookAdd(void* self,void* object) {
    BEGIN(Add);std::uint32_t before=UINT32_MAX;if(!Read(g,g->module_base+kVectorRva+8,&before,4))g->lost.store(true);
    __try {const auto result=ORIGINAL(int(__fastcall*)(void*,void*),Add)(self,object);ObserveCurveAppend(g,reinterpret_cast<std::uintptr_t>(object),before,result);return result;}END()
}
std::uintptr_t __fastcall HookRebuildAll(void* self,int index) {BEGIN(RebuildAll);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,int),RebuildAll)(self,index);}END()}
std::uintptr_t __fastcall HookRebuildSamples(void* self,int count) {BEGIN(RebuildSamples);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,int),RebuildSamples)(self,count);}END()}
std::uintptr_t __fastcall HookDestroySpline(void* self,unsigned char flags) {BEGIN(DestroySpline);ObserveCurveDestruction(g,reinterpret_cast<std::uintptr_t>(self));__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,unsigned char),DestroySpline)(self,flags);}END()}
float __fastcall HookEvaluateSamples(void* self,float x) {BEGIN(EvaluateSamples);__try{return ORIGINAL(float(__fastcall*)(void*,float),EvaluateSamples)(self,x);}END()}
float __fastcall HookEvaluateSpline(void* self,float x) {BEGIN(EvaluateSpline);__try{return ORIGINAL(float(__fastcall*)(void*,float),EvaluateSpline)(self,x);}END()}
float __fastcall HookMaxX(void* self) {BEGIN(MaxX);__try{return ORIGINAL(float(__fastcall*)(void*),MaxX)(self);}END()}
float __fastcall HookMinX(void* self) {BEGIN(MinX);__try{return ORIGINAL(float(__fastcall*)(void*),MinX)(self);}END()}
std::uintptr_t __fastcall HookLoadXmlPoints(void* self,void* parser,unsigned char reverse) {BEGIN(LoadXmlPoints);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*,unsigned char),LoadXmlPoints)(self,parser,reverse);}END()}
std::uintptr_t __fastcall HookLoadBinaryPoints(void* self,void* file,unsigned char reverse) {BEGIN(LoadBinaryPoints);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*,unsigned char),LoadBinaryPoints)(self,file,reverse);}END()}
std::uintptr_t __fastcall HookAddPoint(void* self,float x,float y) {BEGIN(AddPoint);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,float,float),AddPoint)(self,x,y);}END()}
std::uintptr_t __fastcall HookRemovePoint(void* self,int index) {BEGIN(RemovePoint);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,int),RemovePoint)(self,index);}END()}
void ItemEvent(CurveNativeGateway* g,CurveHook h,const CurveCallToken* token,CurveItemEvent event,
    std::uintptr_t argument,std::uintptr_t caller,std::uintptr_t result) noexcept {
    if(g->callbacks.item_event)g->callbacks.item_event(g->callbacks.user,h,token,event,argument,caller,result);
}
#define ITEM_BEGIN(kind,argument) const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(kind); \
    std::uintptr_t native_result=0;bool returned=false;ItemEvent(g,CurveHook::kind,&token,CurveItemEvent::Entering,argument,caller,0)
#define ITEM_END(kind,argument) __finally {ItemEvent(g,CurveHook::kind,&token,returned?CurveItemEvent::Returned:CurveItemEvent::Unwound,argument,caller,native_result);EndCurveCall(g,&token);}
std::uintptr_t __fastcall HookItemDefinitions(const char** path,unsigned char mode) {ITEM_BEGIN(ItemDefinitions,reinterpret_cast<std::uintptr_t>(path));
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(const char**,unsigned char),ItemDefinitions)(path,mode);returned=true;return native_result;}ITEM_END(ItemDefinitions,reinterpret_cast<std::uintptr_t>(path))}
unsigned char __fastcall HookItemParser(void* self,void* file,int mode) {ITEM_BEGIN(ItemParser,reinterpret_cast<std::uintptr_t>(self));
    __try{native_result=ORIGINAL(unsigned char(__fastcall*)(void*,void*,int),ItemParser)(self,file,mode);returned=true;return static_cast<unsigned char>(native_result);}ITEM_END(ItemParser,reinterpret_cast<std::uintptr_t>(self))}
std::uintptr_t __fastcall HookItemFamilyLoad(void* output) {ITEM_BEGIN(ItemFamilyLoad,reinterpret_cast<std::uintptr_t>(output));
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(void*),ItemFamilyLoad)(output);returned=true;return native_result;}ITEM_END(ItemFamilyLoad,reinterpret_cast<std::uintptr_t>(output))}
unsigned char __fastcall HookItemRowsXml(void* source) {ITEM_BEGIN(ItemRowsXml,reinterpret_cast<std::uintptr_t>(source));
    __try{native_result=ORIGINAL(unsigned char(__fastcall*)(void*),ItemRowsXml)(source);returned=true;return static_cast<unsigned char>(native_result);}ITEM_END(ItemRowsXml,reinterpret_cast<std::uintptr_t>(source))}
unsigned char __fastcall HookItemCutTypes(const char** path) {ITEM_BEGIN(ItemCutTypes,reinterpret_cast<std::uintptr_t>(path));
    __try{native_result=ORIGINAL(unsigned char(__fastcall*)(const char**),ItemCutTypes)(path);returned=true;return static_cast<unsigned char>(native_result);}ITEM_END(ItemCutTypes,reinterpret_cast<std::uintptr_t>(path))}
unsigned char __fastcall HookItemAffixes(const char** path) {ITEM_BEGIN(ItemAffixes,reinterpret_cast<std::uintptr_t>(path));
    __try{native_result=ORIGINAL(unsigned char(__fastcall*)(const char**),ItemAffixes)(path);returned=true;return static_cast<unsigned char>(native_result);}ITEM_END(ItemAffixes,reinterpret_cast<std::uintptr_t>(path))}
std::uintptr_t __fastcall HookItemFinalize() {ITEM_BEGIN(ItemFinalize,0);
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(),ItemFinalize)();returned=true;return native_result;}ITEM_END(ItemFinalize,0)}
void __fastcall HookItemClear() {ITEM_BEGIN(ItemClear,0);
    __try{ORIGINAL(void(__fastcall*)(),ItemClear)();returned=true;}ITEM_END(ItemClear,0)}
std::uintptr_t __fastcall HookItemDestroy(void* self) {ITEM_BEGIN(ItemDestroy,reinterpret_cast<std::uintptr_t>(self));
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(void*),ItemDestroy)(self);returned=true;return native_result;}ITEM_END(ItemDestroy,reinterpret_cast<std::uintptr_t>(self))}
std::uintptr_t __fastcall HookItemConstruct() {ITEM_BEGIN(ItemConstruct,0);
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(),ItemConstruct)();returned=true;return native_result;}ITEM_END(ItemConstruct,0)}
// Registration mutates the process manager lists; generation mutates the
// registered CEnumInfo objects. Both must participate in snapshot admission.
// The pointer passed to registration is a field, while generation takes the
// manager. Preserve those actual arguments for the owning observer.
#define REGISTER_ENUM(kind) std::uintptr_t __fastcall Hook##kind(void* field) {ITEM_BEGIN(kind,reinterpret_cast<std::uintptr_t>(field)); \
    __try{native_result=ORIGINAL(std::uintptr_t(__fastcall*)(void*),kind)(field);returned=true;return native_result;}ITEM_END(kind,reinterpret_cast<std::uintptr_t>(field))}
REGISTER_ENUM(ItemRegisterEnum)
REGISTER_ENUM(ItemRegisterSavedEnum)
REGISTER_ENUM(ItemRegisterCannonEnum)
#undef REGISTER_ENUM
#define GENERATE_ENUM(kind) void __fastcall Hook##kind(void* manager) {ITEM_BEGIN(kind,reinterpret_cast<std::uintptr_t>(manager)); \
    __try{ORIGINAL(void(__fastcall*)(void*),kind)(manager);returned=true;}ITEM_END(kind,reinterpret_cast<std::uintptr_t>(manager))}
GENERATE_ENUM(ItemGenerateEnum)
GENERATE_ENUM(ItemGenerateSavedEnum)
GENERATE_ENUM(ItemGenerateCannonEnum)
#undef GENERATE_ENUM
#undef ITEM_BEGIN
#undef ITEM_END
void __fastcall HookSaveManager() {BEGIN(SaveManager);__try{ORIGINAL(void(__fastcall*)(),SaveManager)();}END()}
std::uintptr_t __fastcall HookRebuildEveryCurve(void* self) {BEGIN(RebuildEveryCurve);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*),RebuildEveryCurve)(self);}END()}
unsigned __fastcall HookNearestPoint(void* self,double x,double y) {BEGIN(NearestPoint);__try{return ORIGINAL(unsigned(__fastcall*)(void*,double,double),NearestPoint)(self,x,y);}END()}
std::uintptr_t __fastcall HookSetPoint(void* self,int index,float x,float y) {BEGIN(SetPoint);__try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,int,float,float),SetPoint)(self,index,x,y);}END()}
std::uintptr_t __fastcall HookConstructSpline(void* self) {const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());BEGIN(ConstructSpline);
    __try {const auto result=ORIGINAL(std::uintptr_t(__fastcall*)(void*),ConstructSpline)(self);
        if(result!=reinterpret_cast<std::uintptr_t>(self))g->lost.store(true);else ObserveCurveConstruction(g,result,caller);return result;}END()}
// Whole native callers enter before their first cached ID/weight read. Nested
// getters/evaluations share this admission through the original's full return.
float __fastcall HookRandomDistribution(int seed,float low,float high,unsigned char reverse) {BEGIN(RandomDistribution);
    __try{return ORIGINAL(float(__fastcall*)(int,float,float,unsigned char),RandomDistribution)(seed,low,high,reverse);}END()}
unsigned char __fastcall HookLevelDistribution(void* a,void* b,void* c,unsigned d,void* e,void* f,unsigned char h,unsigned char i) {BEGIN(LevelDistribution);
    __try{return ORIGINAL(unsigned char(__fastcall*)(void*,void*,void*,unsigned,void*,void*,unsigned char,unsigned char),LevelDistribution)(a,b,c,d,e,f,h,i);}END()}
std::uintptr_t __fastcall HookRepairValue(void* a,void* b,void* c) {BEGIN(RepairValue);
    __try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*,void*),RepairValue)(a,b,c);}END()}
std::uintptr_t __fastcall HookSalvageValue(void* a,void* b) {BEGIN(SalvageValue);
    __try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*),SalvageValue)(a,b);}END()}
float __fastcall HookRepairPartsMultiplier(int amount) {BEGIN(RepairPartsMultiplier);
    __try{return ORIGINAL(float(__fastcall*)(int),RepairPartsMultiplier)(amount);}END()}
std::uintptr_t __fastcall HookFighterDamageXp(void* a,void* b,void* c) {BEGIN(FighterDamageXp);
    __try{return ORIGINAL(std::uintptr_t(__fastcall*)(void*,void*,void*),FighterDamageXp)(a,b,c);}END()}
#undef BEGIN
#undef ORIGINAL
#undef END
} // namespace
const CurveHookDescriptor* CurveHookCatalog(std::size_t* count) noexcept {if(count)*count=kCurveHookCount;return catalog;}
std::uintptr_t CurveHookWrapper(CurveHook kind) noexcept {
    switch(kind){
#define CASE(name) case CurveHook::name:return reinterpret_cast<std::uintptr_t>(&Hook##name)
    CASE(Load);CASE(Reload);CASE(GetId);CASE(FindId);CASE(GetIndex);CASE(GetName);CASE(CopyPointers);CASE(Count);
    CASE(Add);CASE(RebuildAll);CASE(RebuildSamples);CASE(DestroySpline);CASE(EvaluateSamples);CASE(EvaluateSpline);
    CASE(MaxX);CASE(MinX);CASE(LoadXmlPoints);CASE(LoadBinaryPoints);CASE(AddPoint);CASE(RemovePoint);
    CASE(ItemDefinitions);CASE(ItemParser);CASE(SaveManager);CASE(RebuildEveryCurve);CASE(NearestPoint);CASE(SetPoint);CASE(ConstructSpline);
    CASE(RandomDistribution);CASE(LevelDistribution);CASE(RepairValue);CASE(SalvageValue);CASE(RepairPartsMultiplier);CASE(FighterDamageXp);
    CASE(ItemFamilyLoad);CASE(ItemRowsXml);CASE(ItemCutTypes);CASE(ItemAffixes);CASE(ItemFinalize);CASE(ItemClear);CASE(ItemDestroy);CASE(ItemConstruct);
    CASE(ItemRegisterEnum);CASE(ItemRegisterSavedEnum);CASE(ItemRegisterCannonEnum);
    CASE(ItemGenerateEnum);CASE(ItemGenerateSavedEnum);CASE(ItemGenerateCannonEnum);
#undef CASE
    default:return 0;}
}
CurveGateResult InitializeCurveGateway(CurveNativeGateway* g,std::uintptr_t base,const char* sha,
    const CurveGatewayCallbacks* callbacks,const std::uintptr_t* originals,std::size_t count) noexcept {
    if(!g || g->initialized || !base || !sha || !callbacks || !callbacks->read || !callbacks->context || !callbacks->potential_write ||
        !originals || count!=kCurveHookCount)return CurveGateResult::InvalidArgument;
    if(std::strlen(sha)!=64 || std::memcmp(sha,kPlayerGameDllSha256,65))return CurveGateResult::ProfileMismatch;
    g->module_base=base;g->callbacks=*callbacks;
    for(std::size_t i=0;i<count;++i){const auto& d=catalog[i];if(!originals[i])return CurveGateResult::IncompleteCoverage;
        if(d.pointer_slot_rva){std::uintptr_t value=0;if(!Read(g,base+d.pointer_slot_rva,&value,8) || value!=base+d.function_rva)return CurveGateResult::ProfileMismatch;}
        else {std::uint8_t bytes[16]={};if(!Read(g,base+d.function_rva,bytes,d.prefix_size) || std::memcmp(bytes,d.prefix,d.prefix_size))return CurveGateResult::ProfileMismatch;}
        g->originals[i]=originals[i];}
    g->initialized=true;return CurveGateResult::Succeeded;
}
CurveGateResult BindCurveHookBridge(CurveNativeGateway* g) noexcept {
    if(!g || !g->initialized)return CurveGateResult::InvalidArgument;CurveNativeGateway* expected=nullptr;
    return bridge.compare_exchange_strong(expected,g) || expected==g?CurveGateResult::Succeeded:CurveGateResult::Busy;
}
CurveGateResult VerifyCurveHookCoverage(CurveNativeGateway* g) noexcept {
    if(!g || !g->initialized)return CurveGateResult::InvalidArgument;
    for(const auto& d:catalog){const auto wrapper=CurveHookWrapper(d.kind);
        if(!g->originals[Index(d.kind)])return CurveGateResult::IncompleteCoverage;
        if(d.pointer_slot_rva){std::uintptr_t value=0;if(!Read(g,g->module_base+d.pointer_slot_rva,&value,8) || value!=wrapper)return CurveGateResult::IncompleteCoverage;}
        else {std::uint8_t bytes[14]={};std::uintptr_t target=0;
            if(!Read(g,g->module_base+d.function_rva,bytes,sizeof(bytes)))return CurveGateResult::IncompleteCoverage;
            if(bytes[0]==0xff && bytes[1]==0x25 && !bytes[2] && !bytes[3] && !bytes[4] && !bytes[5])std::memcpy(&target,bytes+6,8);
            // The root uses an absolute 14-byte indirect branch at the entry.
            if(target!=wrapper)return CurveGateResult::IncompleteCoverage;}}
    return CurveGateResult::Succeeded;
}
CurveGateResult SealCurveHookStartup(CurveNativeGateway* g) noexcept {
    CurveGatewayContext context;if(!g || bridge.load()!=g || !Context(g,&context) || !context.startup_owner_absence ||
        g->active_calls.load() || g->arrivals.load() || VerifyCurveHookCoverage(g)!=CurveGateResult::Succeeded)return CurveGateResult::WrongContext;
    g->startup_sealed.store(true);return CurveGateResult::Succeeded;
}
void BeginCurveCall(CurveNativeGateway* g,CurveHook h,CurveCallToken* out) noexcept {
    if(!g || !out || Index(h)>=kCurveHookCount)return;*out={};const auto tid=GetCurrentThreadId();
    // This is the first shared operation, including for pure readers. A caller
    // queued after the owner's final zero check therefore cannot be invisible.
    const auto arrival=g->arrivals.fetch_add(1);
    if(arrival&publication_bit){g->publication_conflict.store(true);
        if(g->deletion_started.load())g->poisoned.store(true);}
    const bool writer=Writer(h);
    // Keep separate counters for diagnostics. The shared arrivals word above
    // is the publication reservation, including the interval before these.
    if(writer){g->pending_writers.fetch_add(1);g->revision.fetch_add(1);}
    else g->pending_readers.fetch_add(1);
    // Even a saturated/lost call tracker must promote a potential writer; its
    // original still forwards and must not hide the domain from preflight.
    if(writer && PotentialWriter(g,h)){if(g->callbacks.potential_write)g->callbacks.potential_write(g->callbacks.user,h);else g->lost.store(true);}
    if(tls.depth && tls.gate!=g){if(writer)g->pending_writers.fetch_sub(1);else g->pending_readers.fetch_sub(1);g->arrivals.fetch_sub(1);g->lost.store(true);return;}
    const bool outer=!tls.depth;
    if(outer){for(;;){while(g->poisoned.load() || (g->owner_thread.load() && g->owner_thread.load()!=tid))Sleep(1);
        g->active_calls.fetch_add(1);if(!g->poisoned.load() && (!g->owner_thread.load() || g->owner_thread.load()==tid))break;g->active_calls.fetch_sub(1);}
        tls.gate=g;}
    if(writer)g->pending_writers.fetch_sub(1);else g->pending_readers.fetch_sub(1);
    g->arrivals.fetch_sub(1);
    if(tls.depth>=128){g->lost.store(true);if(outer)g->active_calls.fetch_sub(1);return;}
    if(g->held.load() && Writer(h))g->lost.store(true);
    const auto serial=++tls.serial;tls.stack[tls.depth]=serial;tls.kinds[tls.depth]=h;
    *out={serial,tls.depth,h,true,outer};++tls.depth;
    if(h==CurveHook::Reload)g->owner_generation.fetch_add(1);
}
void EndCurveCall(CurveNativeGateway* g,CurveCallToken* token) noexcept {
    if(!g || !token)return;
    if(!token->active || tls.gate!=g || !tls.depth || token->depth!=tls.depth-1 || tls.stack[token->depth]!=token->serial){g->lost.store(true);return;}
    if(Writer(token->kind))g->revision.fetch_add(1);--tls.depth;token->active=false;
    if(token->outer){tls.gate=nullptr;g->active_calls.fetch_sub(1);}
}
namespace {
CurveGateResult CloseGateway(CurveNativeGateway* g,std::uint32_t timeout_ms,CurveGateLease* out,CurveLeasePurpose purpose) noexcept {
    if(!g || !out || !g->initialized || !g->startup_sealed.load())return CurveGateResult::InvalidArgument;*out={};
    if(g->lost.load() || g->poisoned.load())return CurveGateResult::LostObservation;
    if(VerifyCurveHookCoverage(g)!=CurveGateResult::Succeeded)return CurveGateResult::IncompleteCoverage;
    if(tls.depth || g->held.load())return CurveGateResult::Busy;
    const auto tid=GetCurrentThreadId();std::uint32_t expected=0;
    if(!g->owner_thread.compare_exchange_strong(expected,tid))return CurveGateResult::Busy;
    const auto start=GetTickCount64();while(g->active_calls.load()){if(GetTickCount64()-start>=timeout_ms){g->owner_thread.store(0);return CurveGateResult::TimedOut;}Sleep(1);}
    // A canceled publication performed no writes. Its arrival observers have
    // finished when both active and reserved calls drain, so a new generation
    // may clear that diagnostic latch without reviving a poisoned transaction.
    if(!g->arrivals.load() && !g->publication_active.load() && !g->deletion_started.load())g->publication_conflict.store(false);
    CurveGatewayContext context;const bool captured=Context(g,&context);CurveGateResult result=CurveGateResult::Succeeded;
    g->snapshot_frame={};
    if(g->pending_writers.load() || g->pending_readers.load() || g->arrivals.load())result=CurveGateResult::Busy;
    else if(!captured)result=CurveGateResult::WrongContext;
    else if((purpose==CurveLeasePurpose::Snapshot || !g->baseline_sealed) && !context.startup_owner_absence){
        LARGE_INTEGER now={};CurveFrameStamp frame={};QueryPerformanceCounter(&now);
        if(CaptureActiveCurveFrame(&context.authority,tid,static_cast<std::uint64_t>(now.QuadPart),&frame)!=CurveBorrowResult::Succeeded)
            result=CurveGateResult::WrongContext;
        else if(purpose==CurveLeasePurpose::Snapshot)g->snapshot_frame=frame;}
    else if(purpose==CurveLeasePurpose::Owner && g->baseline_sealed && (!context.post_c10_consumers_closed || !context.transaction || !context.epoch))result=CurveGateResult::WrongContext;
    else if(purpose==CurveLeasePurpose::Owner && g->baseline_sealed && VerifyCurveBorrowBoundary(&g->borrowers,&context.authority)!=CurveBorrowResult::Succeeded)result=CurveGateResult::BorrowersActive;
    else if(g->lost.load())result=CurveGateResult::LostObservation;
    if(g->lost.load())result=CurveGateResult::LostObservation;
    if(result!=CurveGateResult::Succeeded){g->owner_thread.store(0);return result;}
    g->lease={g->lease_generation.fetch_add(1)+1,context.transaction,tid,context.epoch,purpose};g->held.store(true);*out=g->lease;
    return CurveGateResult::Succeeded;
}
}
CurveGateResult CloseCurveGateway(CurveNativeGateway* g,std::uint32_t timeout_ms,CurveGateLease* out) noexcept {
    return CloseGateway(g,timeout_ms,out,CurveLeasePurpose::Owner);
}
CurveGateResult CloseCurveGatewayForSnapshot(CurveNativeGateway* g,std::uint32_t timeout_ms,CurveGateLease* out) noexcept {
    return CloseGateway(g,timeout_ms,out,CurveLeasePurpose::Snapshot);
}
bool CurveGatewayLeaseMatches(CurveNativeGateway* g,const CurveGateLease* lease) noexcept {
    if(!g || !lease || !lease->generation || !g->held.load() || g->lost.load() || g->active_calls.load() || g->pending_writers.load() || g->pending_readers.load() ||
        (g->arrivals.load()&~publication_bit) || g->publication_conflict.load() || g->poisoned.load() ||
        g->owner_thread.load()!=GetCurrentThreadId() || lease->thread_id!=GetCurrentThreadId() ||
        lease->generation!=g->lease.generation || lease->transaction!=g->lease.transaction || lease->epoch!=g->lease.epoch || lease->purpose!=g->lease.purpose)return false;
    CurveGatewayContext c;if(!Context(g,&c) || c.transaction!=lease->transaction || c.epoch!=lease->epoch)return false;
    if(lease->purpose==CurveLeasePurpose::Snapshot){
        if(!g->snapshot_frame.sequence)return c.startup_owner_absence;
        LARGE_INTEGER now={};CurveFrameStamp frame={};QueryPerformanceCounter(&now);
        return CaptureActiveCurveFrame(&c.authority,GetCurrentThreadId(),static_cast<std::uint64_t>(now.QuadPart),&frame)==CurveBorrowResult::Succeeded &&
            frame.sequence==g->snapshot_frame.sequence && frame.serialization_token==g->snapshot_frame.serialization_token &&
            frame.game_primary==g->snapshot_frame.game_primary && frame.game_subobject==g->snapshot_frame.game_subobject && frame.thread_id==g->snapshot_frame.thread_id;
    }
    return !g->baseline_sealed || (c.post_c10_consumers_closed && VerifyCurveBorrowBoundary(&g->borrowers,&c.authority)==CurveBorrowResult::Succeeded);
}
bool IsItemDefinitionHook(CurveHook h) noexcept {
    return h==CurveHook::ItemDefinitions || h==CurveHook::ItemParser || (h>=CurveHook::ItemFamilyLoad && h<=CurveHook::ItemGenerateCannonEnum);
}
bool CurveItemInspectionMatches(CurveNativeGateway* g,const CurveItemInspection* scope) noexcept {
    if(!g || !scope || !scope->active || !scope->call.active || scope->call.kind!=CurveHook::ItemFamilyLoad || !scope->call.outer ||
        scope->thread_id!=GetCurrentThreadId() || g->owner_thread.load()!=scope->thread_id || g->held.load() || g->lost.load() ||
        g->poisoned.load() || g->active_calls.load()!=1 || g->arrivals.load() || g->pending_writers.load() || g->pending_readers.load() ||
        tls.gate!=g || tls.depth!=1 || tls.stack[0]!=scope->call.serial || tls.kinds[0]!=CurveHook::ItemFamilyLoad)return false;
    CurveGatewayContext context;CurveFrameStamp frame={};LARGE_INTEGER now={};QueryPerformanceCounter(&now);
    return Context(g,&context) && (context.startup_owner_absence ||
        CaptureActiveCurveFrame(&context.authority,scope->thread_id,static_cast<std::uint64_t>(now.QuadPart),&frame)==CurveBorrowResult::Succeeded);
}
bool BeginCurveItemInspection(CurveNativeGateway* g,const CurveCallToken* token,CurveItemInspection* scope) noexcept {
    if(!g || !token || !scope || !token->active || !token->outer || token->kind!=CurveHook::ItemFamilyLoad || token->depth ||
        tls.gate!=g || tls.depth!=1 || tls.stack[0]!=token->serial || tls.kinds[0]!=CurveHook::ItemFamilyLoad)return false;
    *scope={*token,GetCurrentThreadId(),true};std::uint32_t expected=0;
    if(!g->owner_thread.compare_exchange_strong(expected,scope->thread_id)){scope->active=false;return false;}
    if(CurveItemInspectionMatches(g,scope))return true;
    g->owner_thread.store(0);scope->active=false;return false;
}
bool EndCurveItemInspection(CurveNativeGateway* g,CurveItemInspection* scope) noexcept {
    if(!g || !scope || !scope->active || scope->thread_id!=GetCurrentThreadId() || g->owner_thread.load()!=scope->thread_id || g->held.load() ||
        tls.gate!=g || tls.depth!=1 || tls.stack[0]!=scope->call.serial || tls.kinds[0]!=CurveHook::ItemFamilyLoad)return false;
    // Inspection may fail because a producer arrived. No retired graph is
    // published by this lease, so its original is allowed to resume afterward.
    scope->active=false;g->owner_thread.store(0);return true;
}
CurveGateResult OpenCurveGateway(CurveNativeGateway* g,const CurveGateLease* lease) noexcept {
    // Opening is still permitted after a failure so blocked native calls can
    // finish; a failed transaction cannot turn that reopening into retirement.
    if(!g || !lease || !lease->generation || !g->held.load() || g->owner_thread.load()!=GetCurrentThreadId() ||
        lease->generation!=g->lease.generation || lease->thread_id!=GetCurrentThreadId())return CurveGateResult::WrongLease;
    if(g->publication_active.load()){
        // A failed destructive transaction never resumes a queued native call
        // with an argument from its old view. Only a clean CAS can reopen.
        if(g->publication_conflict.load() || g->poisoned.load())return CurveGateResult::LostObservation;
        std::uint32_t expected=publication_bit;
        if(!g->arrivals.compare_exchange_strong(expected,0)){
            g->publication_conflict.store(true);if(g->deletion_started.load())g->poisoned.store(true);
            return CurveGateResult::LostObservation;}
        g->publication_active.store(false);g->deletion_started.store(false);
    }
    g->held.store(false);g->lease={};g->owner_thread.store(0);return CurveGateResult::Succeeded;
}
bool BeginCurveOwnerPublication(CurveNativeGateway* g,const CurveGateLease* lease) noexcept {
    if(!lease || lease->purpose!=CurveLeasePurpose::Owner || !CurveGatewayLeaseMatches(g,lease) || g->publication_active.load() || g->deletion_started.load())return false;
    std::uint32_t expected=0;
    if(!g->arrivals.compare_exchange_strong(expected,publication_bit))return false;
    g->publication_active.store(true);
    if(CurveGatewayLeaseMatches(g,lease))return true;
    // No owner store has occurred. Preserve queued call reservations while
    // canceling the publication marker, so the caller may reopen normally.
    g->arrivals.fetch_and(~publication_bit);g->publication_active.store(false);return false;
}
bool BeginCurveOwnerRetirement(CurveNativeGateway* g,const CurveGateLease* lease) noexcept {
    if(!g || !lease || lease->purpose!=CurveLeasePurpose::Owner || !g->publication_active.load() || !CurveGatewayLeaseMatches(g,lease))return false;
    g->deletion_started.store(true);
    // A reservation racing the irreversible boundary either invalidates this
    // check or poisons later reopening. Neither path fabricates a native result.
    if(!CurveGatewayLeaseMatches(g,lease)){g->poisoned.store(true);return false;}return true;
}
bool CaptureCurveGatewayFence(void* user,CurveReadFence* out) noexcept {
    auto* g=static_cast<CurveNativeGateway*>(user);if(!g || !out)return false;*out={};
    const bool valid=CurveGatewayLeaseMatches(g,&g->lease);
    *out={g->owner_generation.load(),g->revision.load(),valid,valid,g->active_calls.load()==0 && valid};return valid;
}
bool MakeCurveGatewayAccess(CurveNativeGateway* g,NativeCurveAccess* access) noexcept {
    if(!g || !access || !g->initialized)return false;*access={};access->module_base=g->module_base;
    std::memcpy(access->module_sha256,kPlayerGameDllSha256,65);access->read=BridgeRead;access->fence=CaptureCurveGatewayFence;access->user=g;return true;
}
bool SealCurveBaseline(CurveNativeGateway* g,const CurveGateLease* lease,const CurveSnapshot* baseline) noexcept {
    if(!g || g->baseline_sealed || !CurveGatewayLeaseMatches(g,lease) || ValidateCurveSnapshot(baseline)!=CurveResult::Succeeded)return false;
    Lock(g);
    if(g->pending_writers.load() || g->pending_readers.load() || g->active_calls.load() || g->lost.load() ||
        baseline->fence.owner_generation!=g->owner_generation.load() ||
        !baseline->fence.access_closed || !baseline->fence.readers_drained || !baseline->fence.authoritative_thread){Unlock(g);return false;}
    if(baseline->fence.mutation_revision!=g->revision.load()){Unlock(g);return false;}
    std::uint32_t no_arrival=0;
    // Use the first entry reservation, not the later writer classification.
    // This also rejects a thread paused between those two observations.
    if(!g->arrivals.compare_exchange_strong(no_arrival,publication_bit)){Unlock(g);return false;}
    g->baseline_count=static_cast<std::uint32_t>(baseline->count);g->appended_count=0;g->baseline_sealed=true;
    g->arrivals.fetch_and(~publication_bit);Unlock(g);return true;
}
void ObserveCurveAppend(CurveNativeGateway* g,std::uintptr_t object,std::uint32_t before,std::int32_t index) noexcept {
    if(!g || !object || index<0)return;std::uint32_t count=0;std::uintptr_t vector=0,slot=0;
    if(!Read(g,g->module_base+kVectorRva,&vector,8) || !Read(g,g->module_base+kVectorRva+8,&count,4) ||
        !Read(g,vector+8ULL*static_cast<unsigned>(index),&slot,8)){g->lost.store(true);return;}
    if(count==before){if(static_cast<unsigned>(index)>=before)g->lost.store(true);
        else NoteAllocationAdd(g,object,slot==object,slot!=object);return;}
    if(count!=before+1 || static_cast<unsigned>(index)!=before || slot!=object){g->lost.store(true);return;}
    NoteAllocationAdd(g,object,true,false);
    if(!g->baseline_sealed)return;
    CurveSourceReceipt origin={};CurrentCurveLoadSource(&origin);Lock(g);
    std::size_t free_index=g->appended_count;
    for(std::size_t i=0;i<g->appended_count;++i)if(g->appended[i].retired){free_index=i;break;}
    if(free_index>=kMaximumCurves || Find(g,object)!=kMaximumCurves)g->lost.store(true);
    else {g->appended[free_index]={object,static_cast<unsigned>(index),origin,false,false,false,false};
        if(free_index==g->appended_count)++g->appended_count;}Unlock(g);
}
void ObserveCurvePointer(CurveNativeGateway* g,std::uintptr_t object,std::uintptr_t caller) noexcept {
    if(!g || !object || !g->baseline_sealed)return;Lock(g);const auto index=Find(g,object);
    if(index==kMaximumCurves){Unlock(g);return;}auto& record=g->appended[index];
    const auto function=FunctionBegin(g,caller);
    if(function==0x67DF00 || function==0x6804A0){Unlock(g);return;}
    if(function==0xE80200 || function==0xF77780){record.world_borrow=true;Unlock(g);return;}
    if(!TransientCaller(function)){record.unknown_borrow=true;Unlock(g);return;}Unlock(g);
    CurveGatewayContext context;LARGE_INTEGER qpc={};QueryPerformanceCounter(&qpc);
    if(!Context(g,&context) || NoteTransientCurveBorrow(&g->borrowers,&context.authority,GetCurrentThreadId(),
        static_cast<std::uint64_t>(qpc.QuadPart))!=CurveBorrowResult::Succeeded)g->lost.store(true);
}
void ObserveCurveId(CurveNativeGateway* g,std::int32_t id,std::uintptr_t caller) noexcept {
    if(!g || !g->baseline_sealed || id<0 || static_cast<std::uint32_t>(id)<g->baseline_count)return;
    // Add's duplicate lookup and GetName's nested ID lookup do not expose an
    // independent ID borrower. Their actual outer result is observed separately.
    if(Ancestor(CurveHook::Add) || Ancestor(CurveHook::GetName))return;
    std::uintptr_t vector=0,object=0;std::uint32_t count=0;
    if(!Read(g,g->module_base+kVectorRva,&vector,8) || !Read(g,g->module_base+kVectorRva+8,&count,4) ||
        count>kMaximumCurves || static_cast<std::uint32_t>(id)>=count || !Read(g,vector+8ULL*static_cast<std::uint32_t>(id),&object,8)){
        g->lost.store(true);return;}
    const auto function=FunctionBegin(g,caller);
    // These exact process owners are captured/restored before tail detachment.
    if(function==0x689530 || function==0x70D5B0 || function==0xA89600 || function==0xA89C90 ||
        function==0xB42D70 || function==0xD60CD0 || function==0x67DF00 || function==0x6804A0)return;
    if(function==0x95F10 || function==0xB26A40 || function==0xF0A2F0 || function==0xF0A390){
        // These callers keep the ID only in a register/stack local and return
        // a scalar result. The enclosing frame/typed scope also covers the
        // interval between ID acquisition and the later pointer lookup.
        CurveGatewayContext context;LARGE_INTEGER now={};QueryPerformanceCounter(&now);
        if(!Context(g,&context) || NoteTransientCurveBorrow(&g->borrowers,&context.authority,GetCurrentThreadId(),
            static_cast<std::uint64_t>(now.QuadPart))!=CurveBorrowResult::Succeeded)g->lost.store(true);
        return;
    }
    // AIDataSet (+16 map via 1AAE80), light/vehicle integer fields and other ID
    // retainers require their own owner closure. Never silently use the pointer
    // consumer allowlist to grant those retained integers a transient lifetime.
    Lock(g);const auto slot=Find(g,object);if(slot==kMaximumCurves)g->lost.store(true);
    else g->appended[slot].unknown_borrow=true;Unlock(g);
}
void ObserveCurveConstruction(CurveNativeGateway* g,std::uintptr_t object,std::uintptr_t caller) noexcept {
    if(!g || !object)return;const auto serial=CurrentCurveLoadSerial();if(!serial)return;
    if(caller!=g->module_base+0x10E1CFD && caller!=g->module_base+0x10E1E9A && caller!=g->module_base+0x10E27CF)return;
    bool found=false;std::uintptr_t vt=0;
    if(!ManagerContains(g,object,&found) || found || !Read(g,object,&vt,8) || vt!=g->module_base+kSplineVtableRva){g->lost.store(true);return;}
    Lock(g);std::size_t slot=g->allocation_count;
    for(std::size_t i=0;i<g->allocation_count;++i){const auto& a=g->allocations[i];
        if(!a.retired && a.object==object){g->lost.store(true);Unlock(g);return;}
        if(a.retired && slot==g->allocation_count)slot=i;}
    if(slot>=kMaximumCurves)g->lost.store(true);
    else {g->allocations[slot]={object,serial,GetCurrentThreadId(),false,false,false};if(slot==g->allocation_count)++g->allocation_count;}Unlock(g);
}
void ObserveCurveDestruction(CurveNativeGateway* g,std::uintptr_t object) noexcept {
    if(!g || !object)return;bool found=false;
    if(!ManagerContains(g,object,&found))g->lost.store(true);
    else if(found)g->owner_generation.fetch_add(1);
    // This ordinary destructor has taken ownership of its private allocation.
    // Never let end-of-load cleanup call that destructor a second time.
    Lock(g);for(std::size_t i=0;i<g->allocation_count;++i)if(g->allocations[i].object==object && !g->allocations[i].retired)
        g->allocations[i].retired=true;Unlock(g);
}
bool FinishCurvePrivateAllocations(CurveNativeGateway* g,const CurveLoadToken* load) noexcept {
    if(!g || !load || !load->active || !load->serial || load->serial!=CurrentCurveLoadSerial() || g->lost.load())return false;
    // Called only after original Load returned. Add has returned and its later
    // native sample rebuild has finished, so no local loader pointer remains.
    Lock(g);const auto allocation_count=g->allocation_count;Unlock(g);
    for(std::size_t i=0;i<allocation_count;++i){Lock(g);const auto allocation=g->allocations[i];Unlock(g);
        if(allocation.retired || allocation.load_serial!=load->serial || allocation.thread_id!=GetCurrentThreadId())continue;
        if(allocation.adopted){Lock(g);g->allocations[i].retired=true;Unlock(g);continue;}
        if(!allocation.ignored)continue;
        bool found=false;std::uintptr_t vt=0;
        if(!ManagerContains(g,allocation.object,&found) || found || !Read(g,allocation.object,&vt,8) || vt!=g->module_base+kSplineVtableRva)return false;
        const auto destructor=g->originals[Index(CurveHook::DestroySpline)];if(!destructor)return false;
        __try {reinterpret_cast<std::uintptr_t(__fastcall*)(void*,unsigned char)>(destructor)(reinterpret_cast<void*>(allocation.object),1);}
        __except(1){g->lost.store(true);return false;}
        Lock(g);g->allocations[i].retired=true;Unlock(g);
    }
    return true;
}
bool FindCurveAppend(CurveNativeGateway* g,std::uintptr_t object,CurveAppendRecord* out) noexcept {
    if(!g || !out)return false;Lock(g);const auto index=Find(g,object);if(index!=kMaximumCurves)*out=g->appended[index];Unlock(g);return index!=kMaximumCurves;
}
bool MarkCurveAppendDetached(CurveNativeGateway* g,std::uintptr_t object) noexcept {
    if(!CurveGatewayLeaseMatches(g,&g->lease))return false;Lock(g);const auto index=Find(g,object);
    if(index!=kMaximumCurves)g->appended[index].detached=true;Unlock(g);return index!=kMaximumCurves;
}
bool MarkCurveAppendRetired(CurveNativeGateway* g,std::uintptr_t object) noexcept {
    // Record a destructor that already returned even if a late arrival poisoned
    // its lease during that call. Requiring the old read fence here would lose
    // the physical deletion receipt and leave a freed pointer looking live.
    if(!g || !g->held.load() || !g->publication_active.load() || !g->deletion_started.load() ||
        g->owner_thread.load()!=GetCurrentThreadId() || g->lease.thread_id!=GetCurrentThreadId() || !g->lease.generation)return false;
    Lock(g);const auto index=Find(g,object);
    const bool valid=index!=kMaximumCurves && g->appended[index].detached;
    if(valid)g->appended[index].retired=true;Unlock(g);return valid;
}
bool DestroyDetachedCurve(CurveNativeGateway* g,const CurveGateLease* lease,std::uintptr_t object) noexcept {
    if(!g || !lease || lease->purpose!=CurveLeasePurpose::Owner || !g->publication_active.load() || !g->deletion_started.load() || !CurveGatewayLeaseMatches(g,lease))return false;CurveAppendRecord record={};
    if(!FindCurveAppend(g,object,&record) || !record.detached || record.retired || record.unknown_borrow ||
        !record.origin.valid || record.origin.project_epoch!=lease->epoch || record.origin.ownership!=CurveSourceOwnership::ExclusiveProject)return false;
    std::uint32_t count=0;std::uintptr_t vector=0,vtable=0;
    if(!Read(g,g->module_base+kVectorRva,&vector,8) || !Read(g,g->module_base+kVectorRva+8,&count,4) ||
        count!=g->baseline_count || !Read(g,object,&vtable,8) || vtable!=g->module_base+kSplineVtableRva)return false;
    for(std::uint32_t i=0;i<count;++i){std::uintptr_t current=0;if(!Read(g,vector+8ULL*i,&current,8) || current==object)return false;}
    __try {reinterpret_cast<std::uintptr_t(__fastcall*)(void*,unsigned char)>(g->originals[Index(CurveHook::DestroySpline)])(reinterpret_cast<void*>(object),1);return true;}
    __except(1){g->lost.store(true);return false;}
}
} // namespace dluma::fixes::custom_map_asset_isolation::curves
