#define _HAS_EXCEPTIONS 0
#include "NativeFileHookBridge.h"
#include <cstring>

namespace dluma::fixes::custom_map_asset_isolation::file_lifetime::bridge {
namespace {
State* active_bridge;
struct DestructionFrame {std::uintptr_t address;bool source;};
struct LocalState {State* bridge;ThreadScope factory;DestructionFrame destroying[kFactoryDepth];unsigned depth;
    const FileRecord* fresh_return;unsigned fresh_depth;};
__declspec(thread) LocalState local;
bool Read(void* user,std::uintptr_t address,void* output,std::size_t size)noexcept{
    auto* state=static_cast<State*>(user);return state&&state->read&&state->read(state->read_user,address,output,size);
}
bool Resolve(void* user,std::uintptr_t address,SourceKey* output)noexcept{
    auto* state=static_cast<State*>(user);
    return state&&(SnapshotSourceKey(&state->ledger,address,output)||ObserveSourceAddress(&state->ledger,address,false,output));
}
bool Observing(State* state)noexcept{return state&&dluma_atomic_compare_exchange(&state->observing,0,0)!=0;}
bool BindThread(State* state)noexcept{
    if(local.bridge==state)return true;
    if(local.factory.depth||local.depth){RecordCoverageLoss(&state->ledger,Failure::InvalidToken);return false;}
    local={};local.bridge=state;return true;
}
class Activity {
    State* state_;
public:
    explicit Activity(State* state)noexcept:state_(state){if(state_)dluma_atomic_increment(&state_->active_calls);}
    ~Activity(){if(state_)dluma_atomic_decrement(&state_->active_calls);}
};
class ConstructorCall {
    State* state_;Activity activity_;
public:
    explicit ConstructorCall(State* state)noexcept:state_(state),activity_(state){
        if(Observing(state_)&&(!BindThread(state_)||!local.factory.depth))RecordCoverageLoss(&state_->ledger,Failure::UnobservedFactory);
    }
    void returned(void* value)noexcept{
        if(Observing(state_)&&value)(void)windows::ObserveReturnedFile(&state_->binding,&state_->ledger,local.factory,reinterpret_cast<std::uintptr_t>(value));
    }
};
class DestructionCall {
    State* state_;Activity activity_;Token file_token_;SourceDestructionToken source_token_;bool pushed_;
public:
    DestructionCall(State* state,void* object,bool source)noexcept:
        state_(state),activity_(state),file_token_{},source_token_{},pushed_(false){
        if(!Observing(state_)||!object||!BindThread(state_))return;
        const auto address=reinterpret_cast<std::uintptr_t>(object);
        for(unsigned i=0;i<local.depth;++i)if(local.destroying[i].address==address&&local.destroying[i].source==source)return;
        if(local.depth>=kFactoryDepth){RecordCoverageLoss(&state_->ledger,Failure::Capacity);return;}
        local.destroying[local.depth++]={address,source};pushed_=true;
        if(source){
            SourceKey identity={};
            if(!Resolve(state_,address,&identity)){RecordCoverageLoss(&state_->ledger,Failure::SourceIdentityMismatch);return;}
            source_token_=BeginSourceDestruction(&state_->ledger,address);
        }else file_token_=windows::ObserveDestructorEntry(&state_->binding,&state_->ledger,address);
    }
    ~DestructionCall(){
        if(source_token_.active)(void)EndSourceDestruction(&state_->ledger,&source_token_);
        if(file_token_.active)(void)EndDestroy(&state_->ledger,&file_token_);
        if(pushed_&&local.depth)--local.depth;
    }
};
template<class Function> Function Original(windows::Route route)noexcept{
    return reinterpret_cast<Function>(active_bridge->originals[static_cast<unsigned>(route)]);
}
using WinFactoryFn=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
using AsyncFactoryFn=void*(__fastcall*)(void*,const char*,bool);
using SourceReadFn=void*(__fastcall*)(void*,void*,std::uint32_t);
using MemoryFactoryFn=void*(__fastcall*)(const char*);
using AsyncConstructorFn=void*(__fastcall*)(void*,void*,void*);
using MemoryConstructorFn=void*(__fastcall*)(void*,void*,void*,std::uint64_t);
using ScalarDestructorFn=void*(__fastcall*)(void*,std::uint32_t);
using DestructorFn=void(__fastcall*)(void*);
using ScheduleFn=void(__fastcall*)(void*,void*,std::uint64_t,std::uint64_t,void*,void*,std::int32_t,std::uint32_t);
void* __fastcall Win32Factory(void* source,const char* path,std::uint32_t access,std::uint32_t creation,std::uint32_t flags)noexcept{
    FactoryRequest request={};request.route=windows::Route::Win32Factory;
    request.explicit_source=reinterpret_cast<std::uintptr_t>(source);request.path=reinterpret_cast<std::uintptr_t>(path);
    request.path_representation=PathRepresentation::NarrowString;request.access=access;request.creation=creation;request.flags=flags;
    FactoryCall call(active_bridge,request);if(!call.admitted())return nullptr;
    auto* result=Original<WinFactoryFn>(windows::Route::Win32Factory)(source,path,access,creation,flags);call.returned(result);return result;
}
void* __fastcall AsyncFactory(void* source,const char* path,bool unbuffered)noexcept{
    FactoryRequest request={};request.route=windows::Route::AsyncFactory;
    request.explicit_source=reinterpret_cast<std::uintptr_t>(source);request.path=reinterpret_cast<std::uintptr_t>(path);
    request.path_representation=PathRepresentation::NarrowString;request.unbuffered=unbuffered;
    FactoryCall call(active_bridge,request);if(!call.admitted())return nullptr;
    auto* result=Original<AsyncFactoryFn>(windows::Route::AsyncFactory)(source,path,unbuffered);call.returned(result);return result;
}
#define SOURCE_FACTORY(name) \
void* __fastcall name(void* source,void* path,std::uint32_t flags)noexcept{ \
    FactoryRequest request={};request.route=windows::Route::name; \
    request.explicit_source=reinterpret_cast<std::uintptr_t>(source);request.path=reinterpret_cast<std::uintptr_t>(path); \
    request.path_representation=PathRepresentation::NativePath;request.flags=flags; \
    FactoryCall call(active_bridge,request);if(!call.admitted())return nullptr; \
    auto* result=Original<SourceReadFn>(windows::Route::name)(source,path,flags);call.returned(result);return result;}
SOURCE_FACTORY(DirectoryRead)
SOURCE_FACTORY(DirectoryWrite)
SOURCE_FACTORY(ZipFactory)
#undef SOURCE_FACTORY
void* __fastcall MemoryFactory(const char* path)noexcept{
    FactoryRequest request={};request.route=windows::Route::MemoryFactory;
    request.path=reinterpret_cast<std::uintptr_t>(path);request.path_representation=PathRepresentation::NarrowString;
    FactoryCall call(active_bridge,request);if(!call.admitted())return nullptr;
    auto* result=Original<MemoryFactoryFn>(windows::Route::MemoryFactory)(path);call.returned(result);return result;
}
void* __fastcall AsyncConstructor(void* object,void* source,void* queue)noexcept{
    ConstructorCall call(active_bridge);
    auto* result=Original<AsyncConstructorFn>(windows::Route::AsyncConstructor)(object,source,queue);call.returned(result);return result;
}
void* __fastcall MemoryConstructor(void* object,void* source,void* buffer,std::uint64_t size)noexcept{
    ConstructorCall call(active_bridge);
    auto* result=Original<MemoryConstructorFn>(windows::Route::MemoryConstructor)(object,source,buffer,size);call.returned(result);return result;
}
#define SCALAR_DESTRUCTOR(name,source) \
void* __fastcall name(void* object,std::uint32_t flags)noexcept{ \
    DestructionCall call(active_bridge,object,source);return Original<ScalarDestructorFn>(windows::Route::name)(object,flags);}
SCALAR_DESTRUCTOR(Win32Destructor,false)
SCALAR_DESTRUCTOR(GzipDestructor,false)
SCALAR_DESTRUCTOR(BaseDestructor,false)
SCALAR_DESTRUCTOR(DirectorySourceDestructor,true)
SCALAR_DESTRUCTOR(PackSourceDestructor,true)
SCALAR_DESTRUCTOR(FallbackSourceDestructor,true)
#undef SCALAR_DESTRUCTOR
#define COMPLETE_DESTRUCTOR(name) \
void __fastcall name(void* object)noexcept{DestructionCall call(active_bridge,object,false);Original<DestructorFn>(windows::Route::name)(object);}
COMPLETE_DESTRUCTOR(AsyncDestructor)
COMPLETE_DESTRUCTOR(ZipDestructor)
COMPLETE_DESTRUCTOR(MemoryDestructor)
#undef COMPLETE_DESTRUCTOR
void __fastcall QueueSchedule(void* queue,void* file,std::uint64_t offset,std::uint64_t length,
                             void* buffer,void* control,std::int32_t priority,std::uint32_t kind)noexcept{
    Activity call(active_bridge);
    if(Observing(active_bridge))(void)windows::ObserveQueueSchedule(&active_bridge->binding,&active_bridge->ledger,
        reinterpret_cast<std::uintptr_t>(file),reinterpret_cast<std::uintptr_t>(control));
    Original<ScheduleFn>(windows::Route::QueueSchedule)(queue,file,offset,length,buffer,control,priority,kind);
}
}
FactoryCall::FactoryCall(State* state)noexcept:FactoryCall(state,FactoryRequest{}){}
FactoryCall::FactoryCall(State* state,const FactoryRequest& request)noexcept:state_(state),token_{},rejected_(false),route_(request.route),registry_token_{}{
    if(state_)dluma_atomic_increment(&state_->active_calls);
    if(!Observing(state_))return;
    if(!BindThread(state_)){rejected_=true;return;}
    const auto last_error=GetLastError();
    FactoryReplayAuthorization authority={};bool authorized=false;
    if(state_->authorize_factory&&request.route!=windows::Route::Count){
        auto actual=request;actual.source_identity={};actual.source_identity_observed=false;
        if(actual.explicit_source)
            actual.source_identity_observed=SnapshotSourceKey(&state_->ledger,actual.explicit_source,&actual.source_identity);
        authorized=state_->authorize_factory(state_->authorize_factory_user,actual,&authority);
        if(authorized&&actual.explicit_source){
            SourceKey after={};
            authorized=actual.source_identity_observed&&SnapshotSourceKey(&state_->ledger,actual.explicit_source,&after)&&
                after.address==actual.source_identity.address&&after.generation==actual.source_identity.generation;
        }
    }
    if(!authorized && state_->registry && state_->registry->sealed && state_->registry_tracking_live &&
       dluma_atomic_compare_exchange(const_cast<volatile LONG*>(state_->registry_tracking_live),0,0)) {
        rejected_=!cold_reboot::BeginRetainingObservation(state_->registry,cold_reboot::ContentDomainId::VfsSources,
            cold_reboot::RetainingObservationOrigin::ExternalProducer,&registry_token_);
    }
    if(!rejected_)
        rejected_=authorized?!BeginReplayFactory(&state_->ledger,&local.factory,&token_,authority):
                             !BeginFactory(&state_->ledger,&local.factory,&token_);
    SetLastError(last_error);
}
FactoryCall::~FactoryCall(){
    if(token_.active)(void)EndFactory(&state_->ledger,&local.factory,&token_);
    if(registry_token_.active)(void)cold_reboot::EndRetainingObservation(state_->registry,&registry_token_);
    if(state_)dluma_atomic_decrement(&state_->active_calls);
}
void FactoryCall::returned(void* value)noexcept{
    if(!token_.active||!value)return;
    const auto last_error=GetLastError();
    const auto observed=windows::ObserveReturnedFile(&state_->binding,&state_->ledger,local.factory,reinterpret_cast<std::uintptr_t>(value));
    FileRecord actual={};
    if(observed.active && route_==windows::Route::ZipFactory && state_->fresh_zip_observer &&
       !local.fresh_return && SnapshotFileIdentity(&state_->ledger,reinterpret_cast<std::uintptr_t>(value),&actual) &&
       actual.generation==observed.generation && actual.kind==FileKind::Zip && actual.root==token_.root) {
        local.fresh_return=&actual;local.fresh_depth=local.factory.depth;
        state_->fresh_zip_observer(state_->fresh_zip_user,actual,observed.slot);
        local.fresh_return=nullptr;local.fresh_depth=0;
    }
    SetLastError(last_error);
}
bool ConfigureFreshZipObserver(State* state,FreshFileObserverFn observer,void* user)noexcept{
    if(!state||!observer||Observing(state)||state->ledger.hooks_complete||state->fresh_zip_observer)return false;
    state->fresh_zip_observer=observer;state->fresh_zip_user=user;return true;
}
bool ConfigureRegistryAdmission(State* state,cold_reboot::ContentDomainRestartRegistry* registry,
    const volatile LONG* tracking_live)noexcept {
    if(!state||!registry||!tracking_live||Observing(state)||state->ledger.hooks_complete||state->registry)return false;
    state->registry=registry;state->registry_tracking_live=tracking_live;return true;
}
bool FreshZipReturnMatches(State* state,const FileRecord& expected)noexcept{
    if(!Observing(state)||local.bridge!=state||!local.fresh_return||!local.factory.depth||
       local.fresh_depth!=local.factory.depth||expected.kind!=FileKind::Zip||expected.life!=Life::Live)return false;
    const auto& actual=*local.fresh_return;FileRecord current={};
    return actual.kind==FileKind::Zip && actual.root==local.factory.root && actual.file==expected.file &&
        actual.root==expected.root && actual.generation==expected.generation &&
        actual.source.address==expected.source.address && actual.source.generation==expected.source.generation &&
        SnapshotFileIdentity(&state->ledger,actual.file,&current) && current.life==Life::Live && current.kind==actual.kind &&
        current.generation==actual.generation && current.root==actual.root &&
        current.source.address==actual.source.address && current.source.generation==actual.source.generation;
}
bool Prepare(State* state,std::uintptr_t base,const char* sha,windows::ReadFn read,void* user)noexcept{
    if(!state||!read||(active_bridge&&active_bridge!=state))return false;
    std::memset(state,0,sizeof(*state));Initialize(&state->ledger);
    state->read=read;state->read_user=user;state->binding={base,&Read,&Resolve,state,false};
    if(!windows::Validate(&state->binding,sha))return false;
    std::size_t count=0;const auto* specs=windows::HookPlan(&count);
    for(std::size_t i=0;i<count;++i)state->originals[static_cast<unsigned>(specs[i].route)]=reinterpret_cast<void*>(base+specs[i].rva);
    active_bridge=state;return true;
}
bool Seal(State* state,std::uint64_t mask)noexcept{
    if(!state||!state->binding.validated||!CompleteHookInstallation(&state->ledger,mask,windows::kRequiredHookMask))return false;
    g_dluma_file_completion_ring=&state->ledger.completions;
    dluma_atomic_exchange(&state->observing,1);return true;
}
bool BindOriginal(State* state,windows::Route route,void* original)noexcept{
    const auto index=static_cast<unsigned>(route);
    if(!state||index>=static_cast<unsigned>(windows::Route::Count)||!original)return false;
    state->originals[index]=original;return true;
}
bool SourcePublication(State* state,std::uintptr_t address,bool constructed)noexcept{
    if(!Observing(state)||!address)return false;
    std::uintptr_t vtable=0;
    if(!Read(state,address,&vtable,sizeof(vtable))){RecordCoverageLoss(&state->ledger,Failure::SourceIdentityMismatch);return false;}
    const bool known=vtable==state->binding.base+0x3F4A8||vtable==state->binding.base+0x44A48||vtable==state->binding.base+0x3F748;
    SourceKey identity={};return ObserveSourceAddress(&state->ledger,address,constructed&&known,&identity);
}
bool ExistingSource(State* state,std::uintptr_t address)noexcept{
    SourceKey identity={};return state&&ObserveSourceAddress(&state->ledger,address,false,&identity);
}
bool SourceIdentity(State* state,std::uintptr_t address,SourceKey* output)noexcept{
    return state&&SnapshotSourceKey(&state->ledger,address,output);
}
bool FileIdentity(State* state,std::uintptr_t address,FileRecord* output)noexcept{
    return state&&SnapshotFileIdentity(&state->ledger,address,output);
}
bool Idle(const State* state)noexcept{
    return state&&dluma_atomic_compare_exchange(const_cast<volatile LONG*>(&state->active_calls),0,0)==0&&
        dluma_atomic_compare_exchange(&g_dluma_file_completion_probe_active_calls,0,0)==0;
}
bool SnapshotFactoryPath(State* state,const FactoryRequest& request,FactoryPathSnapshot* output)noexcept{
    if(!output)return false;*output={};
    if(!state||!state->binding.validated||!state->read||!request.path)return false;
    const auto text=[state](std::uintptr_t address,char* out,std::size_t bound)noexcept{
        if(!address||address>UINTPTR_MAX-bound)return false;
        for(std::size_t i=0;i<bound;++i){
            if(!state->read(state->read_user,address+i,out+i,1))return false;
            if(!out[i])return true;
        }return false;
    };
    output->representation=request.path_representation;
    if(request.path_representation==PathRepresentation::NarrowString)
        output->complete=text(request.path,output->narrow,kFactoryPathCapacity);
    else if(request.path_representation==PathRepresentation::NativePath){
        if(request.path>UINTPTR_MAX-68)return false;
        std::uint32_t flags=0,after=0;std::uintptr_t filename=request.path,directory=request.path+32;
        if(!state->read(state->read_user,request.path+64,&flags,4))return false;
        if((flags&1)&&!state->read(state->read_user,request.path,&filename,8))return false;
        if((flags&2)&&!state->read(state->read_user,request.path+32,&directory,8))return false;
        if(!text(filename,output->filename,(flags&1)?kFactoryPathCapacity:32)||
           !text(directory,output->directory,(flags&2)?kFactoryPathCapacity:32)||
           !state->read(state->read_user,request.path+64,&after,4)||after!=flags)return false;
        std::uintptr_t pointer=0;
        if((flags&1)&&(!state->read(state->read_user,request.path,&pointer,8)||pointer!=filename))return false;
        if((flags&2)&&(!state->read(state->read_user,request.path+32,&pointer,8)||pointer!=directory))return false;
        output->native_path_flags=flags;output->complete=true;
    }
    return output->complete;
}
void* HookTarget(windows::Route route)noexcept{
#define TARGET(name) case windows::Route::name:return reinterpret_cast<void*>(&name)
    switch(route){
    TARGET(Win32Factory);TARGET(AsyncFactory);TARGET(DirectoryRead);TARGET(DirectoryWrite);TARGET(ZipFactory);TARGET(MemoryFactory);
    TARGET(AsyncConstructor);TARGET(MemoryConstructor);TARGET(Win32Destructor);TARGET(AsyncDestructor);TARGET(GzipDestructor);
    TARGET(ZipDestructor);TARGET(MemoryDestructor);TARGET(BaseDestructor);TARGET(QueueSchedule);
    TARGET(DirectorySourceDestructor);TARGET(PackSourceDestructor);TARGET(FallbackSourceDestructor);
    case windows::Route::QueueCancelComplete:return reinterpret_cast<void*>(&dluma_file_completion_cancel_probe);
    case windows::Route::QueueSkippedComplete:return reinterpret_cast<void*>(&dluma_file_completion_skipped_probe);
    case windows::Route::QueueReadComplete:return reinterpret_cast<void*>(&dluma_file_completion_read_probe);
    default:return nullptr;
    }
#undef TARGET
}
}
