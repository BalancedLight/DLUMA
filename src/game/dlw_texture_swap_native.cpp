#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include "dlw_texture_swap_native.h"
#include "../fixes/custom_map_asset_isolation/NativeXuiObservation.h"
#include "../fixes/custom_map_asset_isolation/NativeXuiReceiverBridge.h"
extern "C" unsigned int dluma_lua_runtime_current_map_generation(void);
extern "C" int dluma_hud_visibility_backend_document_reload_active(void);
extern "C" void dluma_log_line(const char*,const char*);

namespace {
constexpr unsigned kPairs=11,kAssets=kPairs*2,kWaitFrames=600;
constexpr uintptr_t kIat=0x13CE500,kCaller=0x4ACC35,kSetter=0x5E9F10;
struct String {const char* data;unsigned size,capacity;};
using Setter=unsigned char(__fastcall*)(void*,const String*);
using Resolve=uintptr_t(__fastcall*)(uintptr_t);
using ReleaseDefinition=void(__fastcall*)(uintptr_t);
struct Retained {
    uintptr_t definition,backing,manager;
    char alias[128],filename[512],key[512];
    unsigned char geometry[56];
    unsigned diagnostic_first_mask,diagnostic_last_mask,diagnostic_reports;
    bool active,ready,released,uncertain;
};
struct Service {
    DlumaTextureResources resources;
    Retained assets[kAssets];
    DlumaTextureServiceStatus status;
    uint64_t owner,manager_generation;
    uint32_t owner_generation,map,wait_frames;
    unsigned diagnostic_reports;
    uintptr_t manager;
    char last_original[128];
    bool stop,dead;
};
SRWLOCK g_lock=SRWLOCK_INIT;
Service g_service={};
uint64_t g_next_token;
uintptr_t g_engine,g_game;
Setter g_original;
bool g_installed,g_protection_lost,g_shutting_down;
bool g_document_reload_reserved;
volatile LONG g_lifetime_lost;
PVOID volatile g_manager_identity;
thread_local bool g_entered;

bool Read(uintptr_t address,void* output,size_t bytes)
{
    if(!address||!output||!bytes||address>UINTPTR_MAX-bytes)return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,bytes,&read)&&read==bytes;
}
template<class T> bool Field(uintptr_t object,uintptr_t offset,T* out)
{return object<=UINTPTR_MAX-offset&&Read(object+offset,out,sizeof(*out));}
bool CopyName(uintptr_t address,char* out,unsigned capacity)
{
    for(unsigned i=0;i<capacity;++i){if(!Read(address+i,out+i,1)){out[i]=0;return false;}if(!out[i])return true;}
    if(capacity)out[capacity-1]=0;
    return false;
}
bool CopyString(const String* value,char* output,unsigned capacity)
{
    String copy={};
    if(!Read(reinterpret_cast<uintptr_t>(value),&copy,sizeof(copy))||!copy.size||copy.size>=capacity||
        copy.capacity<copy.size||!Read(reinterpret_cast<uintptr_t>(copy.data),output,copy.size))return false;
    for(unsigned i=0;i<copy.size;++i)if(!output[i])return false;
    output[copy.size]=0;return true;
}
String View(const char* value)
{const auto n=static_cast<unsigned>(std::strlen(value));return {value,n,n};}
bool Enabled()
{
#if defined(DLUMA_PRIVATE_TEXTURE_SWAP_DIAGNOSTIC) && DLUMA_PRIVATE_TEXTURE_SWAP_DIAGNOSTIC
    WCHAR text[4]={};return GetEnvironmentVariableW(L"DLUMA_TEXTURE_SWAP_DIAGNOSTIC",text,4)==1&&text[0]==L'1';
#else
    return false;
#endif
}
bool Frame(void** level,uintptr_t* manager)
{return Enabled()&&dluma_xui_native_frame(level,manager)!=0;}
bool Bytes(uintptr_t address,const unsigned char* expected,size_t size)
{unsigned char bytes[32]={};return size<=sizeof(bytes)&&Read(address,bytes,size)&&!std::memcmp(bytes,expected,size);}
bool Routes()
{
    constexpr unsigned char setter[]={0x48,0x8b,0x49,0x18,0x48,0x85,0xc9,0x74,0x05,0xe9,0x62,0x2e,0x06,0x00,0x32,0xc0};
    constexpr unsigned char resolve[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x51,0x18,0x33,0xff};
    constexpr unsigned char release[]={0x40,0x53,0x48,0x83,0xec,0x20,0xf0,0xff,0x49,0x10,0x48,0x8b,0xd9,0x75,0x3e,0x48};
    constexpr unsigned char call[]={0xff,0x15,0xcb,0x18,0xf2,0x00};
    uintptr_t current=0;
    return g_engine&&g_game&&Bytes(g_engine+kSetter,setter,sizeof(setter))&&
        Bytes(g_engine+0x67C1E0,resolve,sizeof(resolve))&&Bytes(g_engine+0x67C180,release,sizeof(release))&&
        Bytes(g_game+kCaller-6,call,sizeof(call))&&Field(g_game,kIat,&current)&&
        current==reinterpret_cast<uintptr_t>(g_original)&&!g_protection_lost;
}
/* The exact native tree node has links at +0/+16, its alias at -24 and
 * definition at -8. Bound the native lookup as well as string reads. */
uintptr_t Lookup(uintptr_t manager,const char* alias)
{
    uintptr_t node=0;if(!Field(manager,952,&node))return 0;
    for(unsigned i=0;node&&i<64;++i){uintptr_t name=0,definition=0;char copy[128]={};
        if(node<24||!Field(node-24,0,&name)||!name||!CopyName(name,copy,sizeof(copy)))return 0;
        int order=std::strcmp(alias,copy);
        if(!order)return Field(node-8,0,&definition)?definition:0;
        if(!Field(node,order<0?0:16,&node))return 0;
    }
    return 0;
}
unsigned char Normalize(unsigned char c)
{if(c=='\\')return '/';return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;}
bool SameKey(const char* a,const char* b)
{
    for(unsigned i=0;i<512;++i){if(Normalize(static_cast<unsigned char>(a[i]))!=Normalize(static_cast<unsigned char>(b[i])))return false;if(!a[i])return true;}
    return false;
}
Retained* Handle(uintptr_t value)
{
    for(auto& asset:g_service.assets)if(value==reinterpret_cast<uintptr_t>(&asset)&&asset.active)return &asset;
    return nullptr;
}
bool Definition(Retained* asset,const char* alias)
{
    uintptr_t filename=0;char copy[512]={};LONG references=0;
    return asset&&asset->manager==g_service.manager&&!std::strcmp(asset->alias,alias)&&
        Lookup(asset->manager,alias)==asset->definition&&Field(asset->definition,16,&references)&&references>0&&
        Field(asset->definition,32,&filename)&&filename&&CopyName(filename,copy,sizeof(copy))&&!std::strcmp(copy,asset->filename);
}
enum BackingFailure : unsigned {
    FailureAlias=0x1,FailureRead=0x2,FailureNull=0x4,FailureSentinel=0x8,
    FailureFallbackAbsent=0x10,FailureFallback=0x20,FailureClass=0x40,FailureGpu=0x80,
    FailureDimensionsEmpty=0x100,FailureResidency=0x200,FailureFormat=0x400,
    FailureKeyMissing=0x800,FailureKeyMismatch=0x1000,FailureGeometryRead=0x2000,
    FailureGeometry=0x4000,FailureRetainedIdentity=0x8000,FailureResolve=0x10000,
    FailureAcquire=0x20000
};
enum BackingRead : unsigned {
    ReadBacking=0x1,ReadFallback=0x2,ReadClass=0x4,ReadGpu=0x8,ReadResident=0x10,
    ReadFull=0x20,ReadFormat=0x40,ReadKeyPointer=0x80,ReadKey=0x100,ReadGeometry=0x200
};
struct BackingSnapshot {
    unsigned mask,reads,format;
    uintptr_t backing,fallback,gpu,key,vtable;
    unsigned short resident[2],full[2];
    unsigned char geometry[56];
    char name[512];
};
/* This is the same readiness predicate as before. Independent guarded reads
 * preserve the first actual refusal and the remaining observable conditions. */
BackingSnapshot InspectBacking(const Retained* asset)
{
    BackingSnapshot s={};
    if(Field(asset->definition,24,&s.backing))s.reads|=ReadBacking;else s.mask|=FailureRead;
    if((s.reads&ReadBacking)&&!s.backing)s.mask|=FailureNull;
    if(s.backing==UINTPTR_MAX)s.mask|=FailureSentinel;
    if(Field(g_engine+0xB4F0A0,72,&s.fallback))s.reads|=ReadFallback;else s.mask|=FailureRead;
    if((s.reads&ReadFallback)&&!s.fallback)s.mask|=FailureFallbackAbsent;
    if(s.backing&&s.backing==s.fallback)s.mask|=FailureFallback;
    if(s.backing&&s.backing!=UINTPTR_MAX){
        if(Field(s.backing,0,&s.vtable)){s.reads|=ReadClass;if(s.vtable!=g_engine+0x9427F8)s.mask|=FailureClass;}else s.mask|=FailureRead;
        if(Field(s.backing,8,&s.gpu)){s.reads|=ReadGpu;if(!s.gpu)s.mask|=FailureGpu;}else s.mask|=FailureRead;
        if(Read(s.backing+34,s.resident,sizeof(s.resident)))s.reads|=ReadResident;else s.mask|=FailureRead;
        if(Read(s.backing+40,s.full,sizeof(s.full))){s.reads|=ReadFull;if(!s.full[0]||!s.full[1])s.mask|=FailureDimensionsEmpty;}else s.mask|=FailureRead;
        if((s.reads&(ReadResident|ReadFull))==(ReadResident|ReadFull)&&
           (s.resident[0]!=s.full[0]||s.resident[1]!=s.full[1]))s.mask|=FailureResidency;
        if(Field(s.backing,56,&s.format)){s.reads|=ReadFormat;if(((s.format>>24)&15)>2)s.mask|=FailureFormat;}else s.mask|=FailureRead;
        if(Field(s.backing,96,&s.key))s.reads|=ReadKeyPointer;else s.mask|=FailureRead;
        if(!s.key)s.mask|=FailureKeyMissing;
        else if(CopyName(s.key,s.name,sizeof(s.name))){s.reads|=ReadKey;if(!SameKey(s.name,asset->filename))s.mask|=FailureKeyMismatch;}
        else s.mask|=FailureRead|FailureKeyMissing;
    }
    if(Read(asset->definition+48,s.geometry,sizeof(s.geometry))){
        s.reads|=ReadGeometry;float dimensions[2]={};std::memcpy(dimensions,s.geometry,sizeof(dimensions));
        if((s.reads&ReadFull)&&(dimensions[0]!=static_cast<float>(s.full[0])||dimensions[1]!=static_cast<float>(s.full[1])))s.mask|=FailureGeometry;
    }else s.mask|=FailureRead|FailureGeometryRead;
    return s;
}
struct DiagnosticLine {
    char text[1024];unsigned used;
    void Add(const char* value){for(unsigned i=0;value&&value[i]&&used+1<sizeof(text);++i){unsigned char c=static_cast<unsigned char>(value[i]);text[used++]=c<32||c=='"'?'?':static_cast<char>(c);}text[used]=0;}
    void Number(uint64_t value,unsigned base=16){char digits[32]={};unsigned count=0;if(base==16)Add("0x");do{digits[count++]="0123456789ABCDEF"[value%base];value/=base;}while(value&&count<sizeof(digits));while(count&&used+1<sizeof(text))text[used++]=digits[--count];text[used]=0;}
    void Value(const char* label,uint64_t value,unsigned base=16){Add(label);Number(value,base);}
};
void ReportBacking(Retained* asset,const char* alias,const BackingSnapshot& s,const char* phase,bool final=false)
{
    if(!Enabled()||g_service.diagnostic_reports>=kAssets*4U)return;
    if(asset&&!final&&(asset->diagnostic_reports>=3U||
       (asset->diagnostic_reports&&asset->diagnostic_last_mask==s.mask)))return;
    if(asset){if(!asset->diagnostic_reports)asset->diagnostic_first_mask=s.mask;asset->diagnostic_last_mask=s.mask;++asset->diagnostic_reports;}
    ++g_service.diagnostic_reports;
    DiagnosticLine line={};line.Add("texture.preflight ");line.Add(phase);
    line.Value(" token=",g_service.status.token,10);line.Value(" asset=",g_service.resources.ready_count,10);
    line.Add(" alias=");line.Add(alias);line.Value(" mask=",s.mask);line.Value(" reads=",s.reads);
    line.Value(" alias_present=",asset&&asset->definition&&Lookup(g_service.manager,alias)==asset->definition?1:0,10);
    line.Value(" definition=",asset?asset->definition:0);line.Value(" backing=",s.backing);line.Value(" fallback=",s.fallback);
    line.Value(" gpu=",s.gpu);line.Value(" vtable=",s.vtable);line.Value(" format=",s.format);line.Value(" format_class=",(s.format>>24)&15,10);
    line.Value(" resident_w=",s.resident[0],10);line.Value(" resident_h=",s.resident[1],10);
    line.Value(" full_w=",s.full[0],10);line.Value(" full_h=",s.full[1],10);
    unsigned dimensions[2]={};std::memcpy(dimensions,s.geometry,sizeof(dimensions));
    line.Value(" geometry_w_bits=",dimensions[0]);line.Value(" geometry_h_bits=",dimensions[1]);dluma_log_line("DEBUG",line.text);
    line={};line.Add("texture.preflight filename token=");line.Number(g_service.status.token,10);line.Add(" alias=");line.Add(alias);line.Add(" value=");line.Add(asset?asset->filename:"");dluma_log_line("DEBUG",line.text);
    line={};line.Add("texture.preflight key token=");line.Number(g_service.status.token,10);line.Add(" alias=");line.Add(alias);line.Add(" value=");line.Add(s.name);dluma_log_line("DEBUG",line.text);
}
bool Backing(Retained* asset,bool capture)
{
    auto s=InspectBacking(asset);
    if(!s.mask&&!capture&&(!asset->ready||asset->backing!=s.backing||std::strcmp(asset->key,s.name)||
       std::memcmp(asset->geometry,s.geometry,sizeof(s.geometry))))s.mask|=FailureRetainedIdentity;
    if(s.mask){ReportBacking(asset,asset->alias,s,asset->diagnostic_reports?"change":"first");return false;}
    if(asset->diagnostic_reports)ReportBacking(asset,asset->alias,s,"ready");
    if(capture){asset->backing=s.backing;std::memcpy(asset->key,s.name,sizeof(s.name));std::memcpy(asset->geometry,s.geometry,sizeof(s.geometry));asset->ready=true;}
    return true;
}
int ReportAcquireFailure(const char* alias,uintptr_t definition,unsigned mask,const char* filename=nullptr)
{
    Retained observed={};observed.definition=definition;
    if(filename)for(unsigned i=0;i+1<sizeof(observed.filename)&&filename[i];++i)observed.filename[i]=filename[i];
    BackingSnapshot snapshot={};snapshot.mask=mask;
    ReportBacking(&observed,alias,snapshot,"acquire");return -1;
}
int Validate(void*,uintptr_t handle,const char* alias)
{
    auto* asset=Handle(handle);return !g_service.dead&&Definition(asset,alias)&&Backing(asset,false);
}
bool ResolveBacking(uintptr_t definition)
{__try {reinterpret_cast<Resolve>(g_engine+0x67C1E0)(definition);return true;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool DropDefinition(Retained* asset)
{
    if(!asset||asset->uncertain)return false;
    __try {reinterpret_cast<ReleaseDefinition>(g_engine+0x67C180)(asset->definition);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){asset->uncertain=true;return false;}
}
int Poll(void*,const char* alias,uintptr_t* handle)
{
    if(!handle||g_service.dead||g_lifetime_lost)return ReportAcquireFailure(alias,0,FailureAcquire);
    auto* asset=Handle(*handle);
    if(!*handle){
        for(auto& candidate:g_service.assets)if(!candidate.active){asset=&candidate;break;}
        uintptr_t definition=Lookup(g_service.manager,alias),filename=0;LONG refs=0;
        if(!asset||!definition)return ReportAcquireFailure(alias,definition,FailureAcquire|(!definition?FailureAlias:0));
        if(!Field(definition,16,&refs)||!Field(definition,32,&filename))return ReportAcquireFailure(alias,definition,FailureAcquire|FailureRead);
        if(refs<0||refs>=0x7ffffffe||!filename)return ReportAcquireFailure(alias,definition,FailureAcquire);
        if(!CopyName(filename,asset->filename,sizeof(asset->filename))||!asset->filename[0])return ReportAcquireFailure(alias,definition,FailureAcquire|FailureRead,asset->filename);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(definition+16));
        asset->definition=definition;asset->manager=g_service.manager;asset->active=true;
        std::memcpy(asset->alias,alias,std::strlen(alias)+1);*handle=reinterpret_cast<uintptr_t>(asset);
    }
    if(!Definition(asset,alias)){auto snapshot=InspectBacking(asset);snapshot.mask|=FailureAlias;ReportBacking(asset,alias,snapshot,"definition");return -1;}
    uintptr_t backing=0;if(!Field(asset->definition,24,&backing)||backing==UINTPTR_MAX){auto snapshot=InspectBacking(asset);ReportBacking(asset,alias,snapshot,"cached");return -1;}
    if(!backing&&!ResolveBacking(asset->definition)){auto snapshot=InspectBacking(asset);snapshot.mask|=FailureResolve;ReportBacking(asset,alias,snapshot,"resolve");return -1;}
    if(!Definition(asset,alias)){auto snapshot=InspectBacking(asset);snapshot.mask|=FailureAlias;ReportBacking(asset,alias,snapshot,"definition");return -1;}
    return Backing(asset,true)?1:0;
}
int Release(void*,uintptr_t handle)
{
    for(auto& asset:g_service.assets)if(handle==reinterpret_cast<uintptr_t>(&asset)&&asset.released){asset={};return 1;}
    auto* asset=Handle(handle);if(!asset)return 0;
    /* The manager callback marks native-freed records explicitly. Never call a
     * stale definition merely because a map generation advanced. */
    if(g_service.dead||asset->uncertain||g_lifetime_lost)return 0;
    void* level=nullptr;uintptr_t manager=0;LONG refs=0;
    if(!dluma_xui_native_borrow_context(&level,&manager)||manager!=asset->manager||
       !Field(asset->definition,16,&refs)||refs<=0)return 0;
    if(!DropDefinition(asset))return 0;
    *asset={};return 1;
}
struct Borrow {void* image;};
int Set(void* user,const char* alias)
{auto* borrow=static_cast<Borrow*>(user);String value=View(alias);return g_original(borrow->image,&value)!=0;}
bool Scoped(void* image,void** level,uintptr_t* manager,DlumaXuiReceiverObservation* observation)
{
    return dluma_xui_native_borrow_context(level,manager)&&*manager==g_service.manager&&
        dluma_lua_runtime_current_map_generation()==g_service.map&&
        dluma_xui_observe_borrowed_receiver(image,*level,reinterpret_cast<void*>(*manager),g_service.resources.swap.target_generation,observation)&&
        observation->manager_generation==g_service.manager_generation&&
        observation->exact_input&&observation->load_serial;
}
bool DispatchBorrowed(void* image,const String* requested,unsigned char* result)
{
    void* level=nullptr;uintptr_t manager=0;DlumaXuiReceiverObservation seen={};char alias[128]={};
    if(g_shutting_down||g_lifetime_lost||g_document_reload_reserved||g_service.dead||!g_service.resources.swap.state||
       dluma_hud_visibility_backend_document_reload_active()||
       !CopyString(requested,alias,sizeof(alias))||!Scoped(image,&level,&manager,&seen))return false;
    Borrow borrow={image};auto outcome=dluma_texture_resources_dispatch(&g_service.resources,1,g_service.map,seen.instance_generation,alias,Set,&borrow);
    if(g_service.resources.swap.target_generation==seen.instance_generation){
        for(unsigned i=0;i<g_service.resources.swap.count;++i)if(!std::strcmp(alias,g_service.resources.swap.entries[i].original)){
            std::memcpy(g_service.last_original,alias,std::strlen(alias)+1);break;}}
    *result=outcome!=DLUMA_TEXTURE_CALL_FAILED&&outcome!=DLUMA_TEXTURE_CALL_RESTORE_PENDING_FAILED;
    if(g_service.resources.swap.state==2){if(!g_service.stop)g_service.status.reason=DLUMA_TEXTURE_SWAP_NOT_READY;g_service.stop=true;g_service.status.state=DLUMA_TEXTURE_SERVICE_RESTORING;}
    else if(!g_service.resources.swap.state){g_service.stop=true;g_service.status.state=g_service.resources.count?DLUMA_TEXTURE_SERVICE_RESTORING:DLUMA_TEXTURE_SERVICE_STOPPED;}
    else if(outcome==DLUMA_TEXTURE_CALL_REPLACED)g_service.status.state=DLUMA_TEXTURE_SERVICE_ACTIVE;
    else if(!g_service.resources.swap.target_generation)g_service.status.state=DLUMA_TEXTURE_SERVICE_READY;
    return true;
}
/* Idle disables the component's Update. The supported private frame contract
 * can still borrow its current HUD-owned image; it never retains that pointer.
 * Both ledger records, class/back-links and the mapped instance must agree. */
bool RestoreCurrentFrame(void* level,uintptr_t manager)
{
    if(!g_service.stop||g_service.resources.swap.state!=2||!g_service.resources.swap.target_generation||
       manager!=g_service.manager||dluma_lua_runtime_current_map_generation()!=g_service.map)return false;
    uintptr_t hud=0,hud_class=0,selected=0;
    if(!Field(g_game,0x1D07FD0,&hud)||!hud||!Field(hud,0,&hud_class)||hud_class!=g_game+0x15BD918)return false;
    for(unsigned i=0;i<137;++i){uintptr_t component=0,component_class=0;
        if(!Field(hud,72+8ULL*i,&component))return false;
        if(!component)continue;
        if(!Field(component,0,&component_class))return false;
        if(component_class==g_game+0x15B1078){unsigned id=0;DlumaXuiReceiverObservation owner={};
            if(!dluma_xui_observe_borrowed_receiver(reinterpret_cast<void*>(component),level,reinterpret_cast<void*>(manager),0,&owner)||owner.manager_generation!=g_service.manager_generation)return false;
            if(selected||!owner.exact_input||!owner.load_serial||!Field(component,44,&id)||id!=49)return false;
            selected=component;}
    }
    uintptr_t image=0,object=0,vtable=0,backlink=0,texture_name=0;DlumaXuiReceiverObservation seen={};char current[128]={};
    if(!selected||!Field(selected,112,&image)||!image||
       !dluma_xui_observe_borrowed_receiver(reinterpret_cast<void*>(image),level,reinterpret_cast<void*>(manager),g_service.resources.swap.target_generation,&seen)||
       seen.manager_generation!=g_service.manager_generation||
       !seen.exact_input||!seen.load_serial||seen.instance_generation!=g_service.resources.swap.target_generation||
       !Field(image,24,&object)||!object||!Field(object,0,&vtable)||vtable!=g_engine+0x914E48||
       !Field(object,16,&backlink)||backlink!=image||!Field(object,1128,&texture_name)||!texture_name)return false;
    String current_view={};
    if(!Read(texture_name,&current_view,sizeof(current_view)))return false;
    const char* original=nullptr;
    if(!current_view.size){if(g_service.last_original[0])original=g_service.last_original;}
    else if(CopyString(reinterpret_cast<const String*>(texture_name),current,sizeof(current))){
        for(unsigned i=0;i<g_service.resources.swap.count;++i){const auto& entry=g_service.resources.swap.entries[i];
            if(!std::strcmp(current,entry.replacement)||!std::strcmp(current,entry.original)){original=entry.original;break;}}}
    if(!original)return false;
    /* Core retirement clears its entries, so copy before the native call. */
    char alias[128]={};std::memcpy(alias,original,std::strlen(original)+1);String requested=View(alias);unsigned char result=0;
    return DispatchBorrowed(reinterpret_cast<void*>(image),&requested,&result)&&result;
}
unsigned char __fastcall Hook(void* image,const String* requested)
{
    Setter original=g_original;
    if(!original)return 0;
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())!=g_game+kCaller||g_entered||
       !TryAcquireSRWLockExclusive(&g_lock))return original(image,requested);
    g_entered=true;unsigned char result=0;
    __try {if(!DispatchBorrowed(image,requested,&result))result=original(image,requested);}
    __finally {g_entered=false;ReleaseSRWLockExclusive(&g_lock);}
    return result;
}
bool Install()
{
    if(g_installed){uintptr_t value=0;return !g_protection_lost&&Field(g_game,kIat,&value)&&value==reinterpret_cast<uintptr_t>(&Hook);}
    g_engine=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"engine_x64_rwdi.dll"));
    g_game=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
    g_original=reinterpret_cast<Setter>(g_engine+kSetter);
    if(!Routes())return false;
    HMODULE own=nullptr,engine=nullptr,game=nullptr;
    constexpr DWORD pin=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN;
    if(!GetModuleHandleExW(pin,reinterpret_cast<LPCWSTR>(&Hook),&own)||
       !GetModuleHandleExW(pin,reinterpret_cast<LPCWSTR>(g_engine),&engine)||
       !GetModuleHandleExW(pin,reinterpret_cast<LPCWSTR>(g_game),&game))return false;
    DWORD protection=0,ignored=0;auto* slot=reinterpret_cast<PVOID volatile*>(g_game+kIat);
    if(!VirtualProtect(const_cast<PVOID*>(slot),sizeof(void*),PAGE_READWRITE,&protection))return false;
    void* previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&Hook),reinterpret_cast<void*>(g_original));
    bool restored=VirtualProtect(const_cast<PVOID*>(slot),sizeof(void*),protection,&ignored)!=0;
    g_installed=previous==reinterpret_cast<void*>(g_original);
    g_protection_lost=!restored;
    /* A published hook remains pinned and is permanent late passthrough. */
    return g_installed&&restored;
}
bool Match(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token)
{return owner&&owner==g_service.owner&&generation==g_service.owner_generation&&map==g_service.map&&token&&token==g_service.status.token;}
void Stop()
{
    g_service.stop=true;g_service.status.state=DLUMA_TEXTURE_SERVICE_RESTORING;
    /* Revoke and shutdown may run outside an engine frame. Only change copied
     * state here; the qualified pump owns all native releases. */
    if(g_service.resources.preparing.state){
        if(!g_service.resources.count)std::memset(&g_service.resources.preparing,0,sizeof(g_service.resources.preparing));
    }else (void)dluma_texture_swap_stop(&g_service.resources.swap,g_service.owner,g_service.owner_generation,g_service.map);
}
}

extern "C" int dluma_texture_native_ready(void)
{void* level=nullptr;uintptr_t manager=0;return !g_lifetime_lost&&!g_shutting_down&&Frame(&level,&manager);}
extern "C" int dluma_texture_service_available(void)
{AcquireSRWLockShared(&g_lock);const bool retained=g_service.status.token!=0;ReleaseSRWLockShared(&g_lock);return retained||dluma_texture_native_ready();}
extern "C" int dluma_texture_native_document_reload_ready(void)
{
    if(!TryAcquireSRWLockShared(&g_lock))return 0;
    const bool ready=!g_lifetime_lost&&!g_entered&&!g_service.resources.swap.target_generation;
    ReleaseSRWLockShared(&g_lock);return ready?1:0;
}
extern "C" int dluma_texture_native_begin_document_reload(void)
{
    if(!TryAcquireSRWLockExclusive(&g_lock))return 0;
    const bool ready=!g_document_reload_reserved&&!g_shutting_down&&!g_lifetime_lost&&!g_entered&&
        !g_service.resources.swap.target_generation;
    if(ready)g_document_reload_reserved=true;
    ReleaseSRWLockExclusive(&g_lock);return ready?1:0;
}
extern "C" void dluma_texture_native_end_document_reload(void)
{AcquireSRWLockExclusive(&g_lock);g_document_reload_reserved=false;ReleaseSRWLockExclusive(&g_lock);}
extern "C" DlumaTextureSwapResult dluma_texture_service_begin(uint64_t owner,uint32_t generation,uint32_t map,const DlumaTextureSwapPair* pairs,unsigned count,uint64_t* token)
{
    if(token)*token=0;
    if(!owner||!generation||!map||!pairs||count!=kPairs||!token)return DLUMA_TEXTURE_SWAP_INVALID;
    for(unsigned i=0;i<count;++i){char expected[32]="hud_bossfight_flash_";unsigned n=20;
        if(i>=10)expected[n++]='1';expected[n++]=static_cast<char>('0'+i%10);expected[n]=0;
        if(!pairs[i].original||std::strcmp(expected,pairs[i].original))return DLUMA_TEXTURE_SWAP_INVALID;}
    if(!TryAcquireSRWLockExclusive(&g_lock))return DLUMA_TEXTURE_SWAP_BUSY;
    DlumaTextureSwapResult result=DLUMA_TEXTURE_SWAP_NOT_READY;
    void* level=nullptr;uintptr_t manager=0;
    if(g_document_reload_reserved||g_service.resources.count||g_service.resources.swap.state||g_service.resources.preparing.state)result=DLUMA_TEXTURE_SWAP_BUSY;
    else if(!g_lifetime_lost&&!g_shutting_down&&map==dluma_lua_runtime_current_map_generation()&&Frame(&level,&manager)&&Install()&&g_next_token<INT64_MAX){
        g_service={};g_service.owner=owner;g_service.owner_generation=generation;g_service.map=map;g_service.manager=manager;
        g_service.manager_generation=dluma_xui_native_manager_generation(manager);
        if(!g_service.manager_generation){ReleaseSRWLockExclusive(&g_lock);return DLUMA_TEXTURE_SWAP_NOT_READY;}
        result=dluma_texture_resources_stage(&g_service.resources,owner,generation,map,pairs,count,Poll,Release,Validate,nullptr);
        if(result==DLUMA_TEXTURE_SWAP_OK){g_service.status.token=++g_next_token;g_service.status.state=DLUMA_TEXTURE_SERVICE_PREPARING;g_service.status.asset_count=count*2;*token=g_service.status.token;InterlockedExchangePointer(&g_manager_identity,reinterpret_cast<void*>(manager));}
    }
    ReleaseSRWLockExclusive(&g_lock);return result;
}
extern "C" DlumaTextureSwapResult dluma_texture_service_status(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token,DlumaTextureServiceStatus* status)
{
    if(!status)return DLUMA_TEXTURE_SWAP_INVALID;
    AcquireSRWLockShared(&g_lock);bool match=Match(owner,generation,map,token);if(match){*status=g_service.status;if(g_lifetime_lost){status->state=DLUMA_TEXTURE_SERVICE_FAILED;status->reason=DLUMA_TEXTURE_SWAP_NOT_READY;}}ReleaseSRWLockShared(&g_lock);
    return match?DLUMA_TEXTURE_SWAP_OK:DLUMA_TEXTURE_SWAP_STALE;
}
extern "C" DlumaTextureSwapResult dluma_texture_service_stop(uint64_t owner,uint32_t generation,uint32_t map,uint64_t token)
{
    if(!TryAcquireSRWLockExclusive(&g_lock))return DLUMA_TEXTURE_SWAP_BUSY;
    DlumaTextureSwapResult result=DLUMA_TEXTURE_SWAP_STALE;
    if(Match(owner,generation,map,token)){Stop();result=g_service.resources.swap.state||g_service.resources.count?DLUMA_TEXTURE_SWAP_RETRY:DLUMA_TEXTURE_SWAP_OK;if(result==DLUMA_TEXTURE_SWAP_OK)g_service.status.state=DLUMA_TEXTURE_SERVICE_STOPPED;}
    ReleaseSRWLockExclusive(&g_lock);return result;
}
extern "C" void dluma_texture_service_revoke(uint64_t owner,uint32_t generation)
{AcquireSRWLockExclusive(&g_lock);if(owner&&owner==g_service.owner&&(!generation||generation==g_service.owner_generation))Stop();ReleaseSRWLockExclusive(&g_lock);}
extern "C" void dluma_texture_service_pump(void)
{
    if(!TryAcquireSRWLockExclusive(&g_lock))return;
    void* level=nullptr;uintptr_t manager=0;
    if(!g_document_reload_reserved&&!g_lifetime_lost&&Frame(&level,&manager)&&manager==g_service.manager&&!g_service.dead){
        uintptr_t route=0;
        if(dluma_lua_runtime_current_map_generation()!=g_service.map||!Field(g_game,kIat,&route)||route!=reinterpret_cast<uintptr_t>(&Hook))Stop();
        if(g_service.stop&&g_service.resources.preparing.state)(void)dluma_texture_resources_cancel_stage(&g_service.resources);
        else if(g_service.resources.preparing.state){
            auto result=dluma_texture_resources_pump(&g_service.resources,2);g_service.status.ready_count=g_service.resources.ready_count;
            if(result==DLUMA_TEXTURE_SWAP_OK)g_service.status.state=DLUMA_TEXTURE_SERVICE_READY;
            else if(result!=DLUMA_TEXTURE_SWAP_RETRY||++g_service.wait_frames>kWaitFrames){
                if(g_service.resources.preparing.state&&g_service.resources.ready_count<g_service.resources.count){
                    auto* pending=Handle(g_service.resources.handles[g_service.resources.ready_count]);
                    if(pending){auto snapshot=InspectBacking(pending);ReportBacking(pending,pending->alias,snapshot,"timeout",true);}}
                g_service.status.reason=DLUMA_TEXTURE_SWAP_NOT_READY;Stop();g_service.status.state=DLUMA_TEXTURE_SERVICE_FAILED;}
        }
        if(!g_service.resources.swap.state&&!g_service.resources.preparing.state&&g_service.resources.count)(void)dluma_texture_resources_cleanup(&g_service.resources);
        if(g_service.resources.swap.state==2&&g_service.stop){
            (void)RestoreCurrentFrame(level,manager);
            if(!g_lifetime_lost&&!g_service.resources.swap.state)(void)dluma_texture_resources_cleanup(&g_service.resources);
        }
        if(g_service.stop&&!g_service.resources.swap.state&&!g_service.resources.count&&g_service.status.state!=DLUMA_TEXTURE_SERVICE_FAILED)g_service.status.state=DLUMA_TEXTURE_SERVICE_STOPPED;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
extern "C" void dluma_texture_service_map_destroyed(uint32_t map)
{
    AcquireSRWLockExclusive(&g_lock);
    if(map&&map==g_service.map){g_service.dead=true;g_service.stop=true;dluma_texture_resources_map_destroyed(&g_service.resources,map);
        std::memset(&g_service.resources.preparing,0,sizeof(g_service.resources.preparing));
        auto result=dluma_texture_resources_cleanup(&g_service.resources);
        g_service.status.state=result==DLUMA_TEXTURE_SWAP_OK?DLUMA_TEXTURE_SERVICE_STOPPED:DLUMA_TEXTURE_SERVICE_RESTORING;}
    ReleaseSRWLockExclusive(&g_lock);
}
extern "C" void dluma_texture_native_manager_destroying(uintptr_t manager,uint64_t generation)
{
    if(reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&g_manager_identity,nullptr,nullptr))!=manager)return;
    if(!TryAcquireSRWLockExclusive(&g_lock)){InterlockedExchange(&g_lifetime_lost,1);return;}
    if(manager&&manager==g_service.manager&&generation&&generation==g_service.manager_generation){
        g_service.stop=true;
        /* The observer supplies this exact still-live owner before entering its
         * native destructor. Drop only our refs; native images retain theirs.
         * No image borrow or SetTexture is attempted during destruction. */
        for(auto& asset:g_service.assets)if(asset.active){LONG refs=0;
            if(Field(asset.definition,16,&refs)&&refs>0&&DropDefinition(&asset)){asset.active=false;asset.released=true;}}
        g_service.dead=true;g_service.status.state=DLUMA_TEXTURE_SERVICE_RESTORING;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
extern "C" void dluma_texture_native_manager_destroyed(uintptr_t manager,uint64_t generation)
{
    if(reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(&g_manager_identity,nullptr,nullptr))!=manager)return;
    if(!TryAcquireSRWLockExclusive(&g_lock)){InterlockedExchange(&g_lifetime_lost,1);return;}
    const bool matching=manager&&manager==g_service.manager&&generation&&generation==g_service.manager_generation;
    const uint32_t map=matching?g_service.map:0;
    ReleaseSRWLockExclusive(&g_lock);
    if(map)dluma_texture_service_map_destroyed(map);
}
extern "C" int dluma_texture_service_shutdown(void)
{
    AcquireSRWLockExclusive(&g_lock);Stop();
    const bool complete=!g_document_reload_reserved&&!g_service.resources.swap.state&&!g_service.resources.preparing.state&&!g_service.resources.count;
    if(complete)g_shutting_down=true;
    ReleaseSRWLockExclusive(&g_lock);return complete?1:0;
}
