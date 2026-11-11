#include "fixes/custom_map_asset_isolation/NativeXuiInstanceOwner.h"
#include "fixes/custom_map_asset_isolation/NativeXuiBirthEnvelope.h"
#include "fixes/custom_map_asset_isolation/NativeDetourMemory.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <new>
extern "C" {
__declspec(dllimport) void WINAPI RaiseException(DWORD,DWORD,DWORD,const ULONG_PTR*);
std::uintptr_t NativeBirthCaller(void*,void*,unsigned char);
extern unsigned char NativeBirthSite[16];
}
namespace root=dluma::fixes::custom_map_asset_isolation;
namespace xui=root::xui;namespace xui_content=root::xui_content;namespace xui_instance=root::xui_instance;
struct HookStatusRecord {volatile LONG active_calls=0;bool installed=true;};
static HookStatusRecord statuses[5];static HookStatusRecord* g_xui_factory_statuses[]={statuses,statuses+1,statuses+2,statuses+3};
static HookStatusRecord* g_xui_birth_status=statuses+4;
static bool g_xui_birth_requested=true,g_xui_manager_observation_ready=true,g_xui_instance_observation_ready=true;
static volatile LONG g_trace_stopping,g_startup_b0_custom_staging_seen;
static thread_local std::uint32_t g_current_thread_hook_call_depth;
static xui_instance::State g_xui_instances;static xui::ManagerLedger g_xui_manager_ledger;
static xui_content::State g_xui_content_owner;
struct Input {xui_content::Load load={};xui_content::Input instance_input={};};
static Input input;static Input* g_xui_input_frames[]={&input};static unsigned g_xui_input_frame_count=1;
namespace content {inline bool TryGetCurrentEpochId(int*,std::uint32_t* out){*out=3;return true;}}
static int g_content_ledger;
class ActiveHookCall {HookStatusRecord* status;public:explicit ActiveHookCall(HookStatusRecord* s):status(s){if(s)++s->active_calls;}~ActiveHookCall(){if(status)--status->active_calls;}};
struct Object {std::uintptr_t fixture_vtable,id,interface_object,factory,package,owner,next,prev;unsigned char flags,padding[7];};
struct Manager {unsigned char prefix[16];std::uintptr_t vt,live_head;std::uint32_t live_count,pad;std::uintptr_t dead_head;std::uint32_t dead_count,pad2;};
static Manager fixture_manager;static Object objects[2];static std::uintptr_t fixture_vtable[111];
static Object* next_object;static unsigned mode,callback_calls,reads_after_free;static bool freed[2],read_failure;
static bool editor;static void* expected_argument=reinterpret_cast<void*>(0x12345678);
static std::uintptr_t last_notification_return;
namespace dluma::fixes::custom_map_asset_isolation::xui {
bool LookupManager(ManagerLedger*,std::uintptr_t value,ManagerRecord* out)noexcept{
    if(value!=reinterpret_cast<std::uintptr_t>(&::fixture_manager))return false;
    *out={value,0x4000,9,3,ManagerLife::Live};return true;}
}
static bool ReadBytes(std::uintptr_t address,void* out,std::size_t bytes)noexcept{
    for(unsigned i=0;i<2;++i){auto begin=reinterpret_cast<std::uintptr_t>(&objects[i]);if(address>=begin&&address-begin<sizeof(Object)){
        if(freed[i]){++reads_after_free;return false;}if(read_failure&&address==begin+16)return false;
        if(bytes>sizeof(Object)-(address-begin))return false;std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}}
    auto begin=reinterpret_cast<std::uintptr_t>(&fixture_manager);if(address>=begin&&address-begin<=sizeof(fixture_manager)&&bytes<=sizeof(fixture_manager)-(address-begin)){
        std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}return false;}
static std::uintptr_t __fastcall FakeCreate(void*,void*,void*,void*);
void __fastcall HookXuiFactoryRetire(void*,void*);
static void __fastcall FakeRetire(void* factory,void* value){auto* object=static_cast<Object*>(value);
    if(object->flags&3)return;object->flags|=1;
    if(mode==8)HookXuiFactoryRetire(factory,value); // native OnDestroy reenters Destroy on the same object
    if(mode==9)return; // an incomplete outer transition must remain a real loss
    if(mode==11)RaiseException(0xE1000005,0,0,nullptr);
    object->flags=2;fixture_manager.dead_head=reinterpret_cast<std::uintptr_t>(object);++fixture_manager.dead_count;}
static void __fastcall FakeCollect(void*){for(unsigned i=0;i<2;++i)if(objects[i].flags==2)freed[i]=true;fixture_manager.dead_head=0;fixture_manager.dead_count=0;}
static volatile LONG other_writer_ok;
static DWORD WINAPI OtherWriter(void*){xui_instance::Writer writer={};
    if(xui_instance::BeginWriter(&g_xui_instances,&writer)&&xui_instance::EndWriter(&g_xui_instances,&writer))other_writer_ok=1;
    return 0;}
static void __fastcall FakeRetireAll(void* factory){
    if(mode==12||mode==13||mode==14){objects[0].flags|=1;
        HookXuiFactoryRetire(factory,objects); // exact native bulk OnDestroy reentry
        if(mode==13)RaiseException(0xE1000006,0,0,nullptr);
        objects[0].flags=2;fixture_manager.dead_head=reinterpret_cast<std::uintptr_t>(objects);++fixture_manager.dead_count;
        if(mode==14){other_writer_ok=0;HANDLE thread=CreateThread(nullptr,0,&OtherWriter,nullptr,0,nullptr);
            if(!thread)RaiseException(0xE1000007,0,0,nullptr);
            const DWORD wait=WaitForSingleObject(thread,5000);CloseHandle(thread);
            if(wait!=0||!other_writer_ok)RaiseException(0xE1000008,0,0,nullptr);}}
    fixture_manager.live_head=0;fixture_manager.live_count=0;}
static auto g_original_xui_factory_create=&FakeCreate;
static auto g_original_xui_factory_retire=&FakeRetire;
static auto g_original_xui_factory_collect=&FakeCollect;
static auto g_original_xui_factory_retire_all=&FakeRetireAll;
#include "fixes/custom_map_asset_isolation/NativeXuiInstanceIntegration.inl"
static std::uintptr_t __fastcall Callback(void* value,void* argument){
    ++callback_calls;if(argument!=expected_argument)RaiseException(0xE1000001,0,0,nullptr);
    auto* object=static_cast<Object*>(value);
    if(mode!=4&&mode!=5){xui_instance::ReceiverDiagnostics diag={};if(!xui_instance::CopyReceiverDiagnostics(&g_xui_instances,object->interface_object,&diag)||
        diag.matches!=1||diag.first.life!=xui_instance::Life::Provisional)RaiseException(0xE1000002,0,0,nullptr);}
    if(mode==1||mode==8||mode==9||mode==10||mode==11){if(mode==10)object->flags=1;
        HookXuiFactoryRetire(reinterpret_cast<unsigned char*>(&fixture_manager)+16,object);
        if(mode==1||mode==8)HookXuiFactoryCollect(reinterpret_cast<unsigned char*>(&fixture_manager)+16);}
    if(mode==2&&object==objects){next_object=objects+1;mode=0;(void)HookXuiFactoryCreate(reinterpret_cast<unsigned char*>(&fixture_manager)+16,nullptr,argument,&fixture_manager);}
    if(mode==12||mode==13||mode==14){HookXuiFactoryRetireAll(reinterpret_cast<unsigned char*>(&fixture_manager)+16);
        HookXuiFactoryCollect(reinterpret_cast<unsigned char*>(&fixture_manager)+16);}
    if(mode==3)RaiseException(0xE1000003,0,0,nullptr);
    return 0x123456789abcdef0ULL;
}
static std::uintptr_t __fastcall FakeCreate(void* factory,void*,void* argument,void* owner){
    if(mode==6)return 0;
    if(mode==7)RaiseException(0xE1000004,0,0,nullptr);
    auto* object=next_object;object->fixture_vtable=reinterpret_cast<std::uintptr_t>(fixture_vtable);object->interface_object=reinterpret_cast<std::uintptr_t>(object)+0x5000;
    object->factory=reinterpret_cast<std::uintptr_t>(factory);object->owner=reinterpret_cast<std::uintptr_t>(owner);object->flags=0;
    ++fixture_manager.live_count;last_notification_return=NativeBirthCaller(object,argument,editor?1:0);return reinterpret_cast<std::uintptr_t>(object);
}
static void Reset(unsigned scenario){
    g_xui_instances.~State();new(&g_xui_instances)xui_instance::State; (void)xui_instance::SealStartup(&g_xui_instances);
    std::memset(objects,0,sizeof(objects));std::memset(&fixture_manager,0,sizeof(fixture_manager));std::memset(freed,0,sizeof(freed));
    mode=scenario;callback_calls=reads_after_free=0;read_failure=scenario==5;g_trace_stopping=scenario==4?1:0;next_object=objects;
    input.load.serial=17;input.load.epoch=3;input.instance_input.complete=true;input.instance_input.sha256[0]=0x42;
    for(auto& status:statuses){status.active_calls=0;status.installed=true;}
}
static bool Check(bool value,const char* message){if(!value)std::printf("FAIL %s\n",message);return value;}
static bool Cases(){
    for(unsigned scenario=0;scenario<15;++scenario){Reset(scenario);bool caught=false;
        __try{(void)HookXuiFactoryCreate(reinterpret_cast<unsigned char*>(&fixture_manager)+16,nullptr,expected_argument,&fixture_manager);}
        __except(1){caught=true;}
        if(!Check(caught==(scenario==3||scenario==7||scenario==11||scenario==13),"exception propagation")||!Check(!g_xui_instances.active_writers&&!g_current_thread_hook_call_depth&&!g_xui_creation_frame&&!g_xui_retirement_frame&&!g_xui_bulk_retirement_frame,"balanced creator/retirement scope"))return false;
        for(const auto& status:statuses)if(!Check(!status.active_calls,"balanced hook counters"))return false;
        if(!Check(callback_calls==((scenario==6||scenario==7)?0U:scenario==2?2U:1U),"native callback count")||!Check(!reads_after_free,"no freed object read"))return false;
        if(scenario<6&&scenario!=3&&!Check(last_notification_return==0x123456789abcdef0ULL,"raw native return"))return false;
        if(scenario==1&&!Check(g_xui_instances.records[0].life==xui_instance::Life::Destroyed&&!g_xui_instances.lost,"retirement/collection during OnCreate"))return false;
        if(scenario==2&&!Check(g_xui_instances.generation==2&&!g_xui_instances.lost,"nested creation"))return false;
        if(scenario==3&&!Check(g_xui_instances.records[0].life==xui_instance::Life::Provisional&&!g_xui_instances.records[0].birth_pending,"unwound birth not exposed"))return false;
        if(scenario==4){HookXuiFactoryRetire(reinterpret_cast<unsigned char*>(&fixture_manager)+16,objects);
            HookXuiFactoryCollect(reinterpret_cast<unsigned char*>(&fixture_manager)+16);HookXuiFactoryRetireAll(reinterpret_cast<unsigned char*>(&fixture_manager)+16);
            if(!Check(!g_xui_instances.generation&&!g_xui_instances.lost&&!g_xui_instances.active_writers,"stopped late pass-through"))return false;}
        if(scenario==5&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::CallbackRead,"first loss retained"))return false;
        if(scenario==6&&!Check(!g_xui_instances.generation&&!g_xui_instances.lost,"native creation failure"))return false;
        if(scenario==7&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::MissingBirth,"pre-birth native fault fails closed"))return false;
        if(scenario==8&&!Check(g_xui_instances.records[0].life==xui_instance::Life::Destroyed&&!g_xui_instances.lost&&
            g_xui_instances.records[0].generation==1&&g_xui_instances.records[0].manager.generation==9,"reentrant destroying no-op uses instance generation, not manager generation"))return false;
        if((scenario==9||scenario==10)&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::CallbackFlags,"unexplained/incomplete destroying remains a loss"))return false;
        if(scenario==11&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::CallbackRead,"native retirement fault retains loss"))return false;
        if(scenario==12&&!Check(!g_xui_instances.lost&&g_xui_instances.records[0].life==xui_instance::Life::Destroyed,"observed bulk OnDestroy permits matching nested no-op and collection"))return false;
        if(scenario==13&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::CallbackRead,"bulk native fault remains failure"))return false;
        if(scenario==14&&!Check(g_xui_instances.lost&&g_xui_instances.first_loss.reason==xui_instance::Loss::CollectorFence,"bulk refuses completed other-thread overlap before commit"))return false;
    }return true;
}
int main(){
    fixture_vtable[109]=fixture_vtable[110]=reinterpret_cast<std::uintptr_t>(&Callback);
    auto site=reinterpret_cast<std::uintptr_t>(NativeBirthSite);
    void* allocation=root::detour::AllocateRelayNear(site,PAGE_READWRITE,[](std::uintptr_t at,std::size_t size,std::uint32_t protection)noexcept{
        return VirtualAlloc(reinterpret_cast<void*>(at),size,MEM_COMMIT|MEM_RESERVE,protection);});
    if(!Check(allocation!=nullptr,"near allocation"))return 1;
    root::xui_birth::Envelope patch={};if(!Check(root::xui_birth::Build(NativeBirthSite,site,reinterpret_cast<std::uintptr_t>(allocation),
        reinterpret_cast<std::uintptr_t>(&HookXuiCreateRuntime),reinterpret_cast<std::uintptr_t>(&HookXuiCreateEditor),&patch),"qualified call bytes"))return 1;
    std::memcpy(allocation,patch.thunks,32);DWORD previous=0;if(!VirtualProtect(allocation,4096,PAGE_EXECUTE_READ,&previous)||!FlushInstructionCache(GetCurrentProcess(),allocation,32))return 1;
    if(!VirtualProtect(NativeBirthSite,16,PAGE_EXECUTE_READWRITE,&previous))return 1;std::memcpy(NativeBirthSite,patch.patched,16);
    DWORD ignored=0;if(!FlushInstructionCache(GetCurrentProcess(),NativeBirthSite,16)||!VirtualProtect(NativeBirthSite,16,previous,&ignored))return 1;
    editor=false;if(!Cases())return 1;editor=true;if(!Cases())return 1;
    std::puts("shared XUI birth dispatch and CALL unwind passed");return 0;
}
