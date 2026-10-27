#include "fixes/custom_map_asset_isolation/NativeFileHookBridge.h"
#include <cstdio>
#include <cstring>
#include <thread>
namespace life=dluma::fixes::custom_map_asset_isolation::file_lifetime;
namespace win=life::windows;
namespace bridge=life::bridge;
namespace cold=dluma::fixes::custom_map_asset_isolation::cold_reboot;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return false;}}while(0)
namespace {
constexpr std::uintptr_t base=0x180000000ULL;
struct Source {std::uintptr_t vtable;};
struct File {std::uintptr_t vtable;unsigned char pad[16];std::uintptr_t source;};
static_assert(offsetof(File,source)==0x18);
bridge::State state;
Source source;
File file;
unsigned factory_calls,constructor_calls,base_destructor_calls;
bridge::FactoryRequest last_request={};bridge::FactoryPathSnapshot last_path={};
unsigned request_calls=0;bool offer_replay=false,validator_accepts=true;
char factory_path[64]="assets/baseline.bin",other_path[64]="assets/other.bin";
unsigned char native_path[68]={};char heap_name[64]="long_baseline_resource_filename.bin",heap_directory[64]="assets/shared";
bool Read(void*,std::uintptr_t address,void* output,std::size_t size)noexcept{
    std::size_t count=0;const auto* specs=win::HookPlan(&count);
    for(std::size_t i=0;i<count;++i)if(address==base+specs[i].rva&&size==specs[i].prefix_size){std::memcpy(output,specs[i].prefix,size);return true;}
    const std::uintptr_t vtables[][2]={{0x3f908,0xa690},{0x3f978,0xabb0},{0x3ea88,0x1d00},{0x449d8,0x24ba0},{0x3ee88,0x2b20}};
    for(const auto& pair:vtables)if(address==base+pair[0]&&size==sizeof(std::uintptr_t)){const auto value=base+pair[1];std::memcpy(output,&value,size);return true;}
    const auto source_address=reinterpret_cast<std::uintptr_t>(&source);
    const auto file_address=reinterpret_cast<std::uintptr_t>(&file);
    const struct{const void* data;std::size_t bytes;} ranges[]={{factory_path,sizeof(factory_path)},{other_path,sizeof(other_path)},
        {native_path,sizeof(native_path)},{heap_name,sizeof(heap_name)},{heap_directory,sizeof(heap_directory)}};
    for(const auto& range:ranges){const auto begin=reinterpret_cast<std::uintptr_t>(range.data);
        if(address>=begin&&address-begin<=range.bytes&&size<=range.bytes-(address-begin)){
            std::memcpy(output,reinterpret_cast<const void*>(address),size);return true;}}
    if((address>=source_address&&address-source_address+size<=sizeof(source))||
       (address>=file_address&&address-file_address+size<=sizeof(file))){std::memcpy(output,reinterpret_cast<void*>(address),size);return true;}
    return false;
}
void* __fastcall FakeFactory(void* owner,const char*,std::uint32_t,std::uint32_t,std::uint32_t)noexcept{
    ++factory_calls;file.vtable=base+0x3f908;file.source=reinterpret_cast<std::uintptr_t>(owner);return &file;
}
void* __fastcall FakeBaseDestructor(void* object,std::uint32_t)noexcept{++base_destructor_calls;return object;}
void* __fastcall FakeFileDestructor(void* object,std::uint32_t flags)noexcept{
    file.vtable=base+0x3e938; // During the nested base call this is no longer a concrete reader.
    using Scalar=void*(__fastcall*)(void*,std::uint32_t);
    return reinterpret_cast<Scalar>(bridge::HookTarget(win::Route::BaseDestructor))(object,flags);
}
void* __fastcall FakeSourceDestructor(void* object,std::uint32_t)noexcept{return object;}
void* __fastcall FakeConstructor(void* object,void* owner,void*,std::uint64_t)noexcept{
    ++constructor_calls;file.vtable=base+0x3ee88;file.source=reinterpret_cast<std::uintptr_t>(owner);return object;
}
bool Authorize(void*,const bridge::FactoryRequest& request,life::FactoryReplayAuthorization* out)noexcept{
    ++request_calls;last_request=request;(void)bridge::SnapshotFactoryPath(&state,request,&last_path);
    if(!offer_replay||!last_path.complete||std::strcmp(last_path.narrow,factory_path))return false;
    *out={1,2,3,9,2,GetCurrentThreadId()};return true;
}
bool ValidateReplay(void*,const life::FactoryReplayAuthorization& authority)noexcept{
    return validator_accepts&&authority.authority==1&&authority.authority_generation==2&&authority.observation==3&&
        authority.transaction==9&&authority.epoch==2&&authority.thread_id==GetCurrentThreadId();
}
volatile LONG tracking_live=1;
bool Reset(bool requests=false,bridge::FreshFileObserverFn observer=nullptr,
    cold::ContentDomainRestartRegistry* registry=nullptr){
    source={base+0x3f4a8};file={};factory_calls=constructor_calls=base_destructor_calls=0;
    request_calls=0;offer_replay=false;validator_accepts=true;last_request={};last_path={};
    CHECK(bridge::Prepare(&state,base,win::kFilesystemSha256,&Read,nullptr));
    CHECK(bridge::BindOriginal(&state,win::Route::Win32Factory,reinterpret_cast<void*>(&FakeFactory)));
    CHECK(bridge::BindOriginal(&state,win::Route::Win32Destructor,reinterpret_cast<void*>(&FakeFileDestructor)));
    CHECK(bridge::BindOriginal(&state,win::Route::BaseDestructor,reinterpret_cast<void*>(&FakeBaseDestructor)));
    CHECK(bridge::BindOriginal(&state,win::Route::DirectorySourceDestructor,reinterpret_cast<void*>(&FakeSourceDestructor)));
    CHECK(bridge::BindOriginal(&state,win::Route::MemoryConstructor,reinterpret_cast<void*>(&FakeConstructor)));
    if(requests){state.authorize_factory=Authorize;CHECK(life::ConfigureFactoryReplayValidator(&state.ledger,ValidateReplay,nullptr));}
    if(observer)CHECK(bridge::ConfigureFreshZipObserver(&state,observer,nullptr));
    if(registry)CHECK(bridge::ConfigureRegistryAdmission(&state,registry,&tracking_live));
    CHECK(bridge::Seal(&state,win::kRequiredHookMask));return true;
}
bool fresh_checks=true;
unsigned fresh_calls=0;
life::FileRecord fresh_file={};
void ObserveFresh(void*,const life::FileRecord& actual,std::uint32_t slot)noexcept {
    ++fresh_calls;fresh_file=actual;
    fresh_checks &= slot<life::kFiles && bridge::FreshZipReturnMatches(&state,actual);
    auto changed=actual;++changed.generation;
    fresh_checks &= !bridge::FreshZipReturnMatches(&state,changed);
    bool foreign=false;
    std::thread other([&]{foreign=!bridge::FreshZipReturnMatches(&state,actual);});other.join();
    fresh_checks &= foreign;
    {bridge::FactoryCall nested(&state);fresh_checks &= nested.admitted() && !bridge::FreshZipReturnMatches(&state,actual);}
    fresh_checks &= bridge::FreshZipReturnMatches(&state,actual);
    SetLastError(0xDEAD);
}
void* __fastcall FakeZipFactory(void* owner,void*,std::uint32_t)noexcept {
    ++factory_calls;file={base+0x449D8,{},reinterpret_cast<std::uintptr_t>(owner)};
    SetLastError(0x88);return &file;
}
bool FreshReturnAuthorityEndsBeforePublication(){
    fresh_calls=0;fresh_checks=true;fresh_file={};
    CHECK(Reset(false,&ObserveFresh));source.vtable=base+0x44A48;
    CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    CHECK(bridge::BindOriginal(&state,win::Route::ZipFactory,reinterpret_cast<void*>(&FakeZipFactory)));
    using Zip=void*(__fastcall*)(void*,void*,std::uint32_t);
    auto create=reinterpret_cast<Zip>(bridge::HookTarget(win::Route::ZipFactory));
    CHECK(create(&source,native_path,0)==&file && GetLastError()==0x88);
    CHECK(fresh_checks && fresh_calls==1 && factory_calls==1);
    CHECK(!bridge::FreshZipReturnMatches(&state,fresh_file));
    CHECK(!bridge::ConfigureFreshZipObserver(&state,&ObserveFresh,nullptr));return true;
}
cold::DomainResult PhaseOk(void*,const cold::RestartContext*)noexcept{return cold::DomainResult::Succeeded;}
cold::DomainResult StartMutation(void* user,const cold::RestartContext* context)noexcept{
    return cold::MarkRegistryMutationStarted(static_cast<cold::ContentDomainRestartRegistry*>(user),context)
        ?cold::DomainResult::Succeeded:cold::DomainResult::Failed;
}
bool late_factory_rejected=false;
bool TryFactoryAtCommit(void*,const cold::RestartContext*,const cold::RegistryFinalizationToken*)noexcept{
    using Factory=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
    auto create=reinterpret_cast<Factory>(bridge::HookTarget(win::Route::Win32Factory));
    late_factory_rejected=!create(&source,factory_path,0x80000000,3,0x80);
    return true;
}
bool OpenFilesystemLedgerCannotBypassRegistryCommit(){
    static cold::ContentDomainRestartRegistry registry;
    cold::InitializeRegistry(&registry);tracking_live=1;late_factory_rejected=false;
    CHECK(Reset(false,nullptr,&registry));
    CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    cold::ContentDomainAdapter owner={};owner.id=cold::ContentDomainId::VfsSources;
    std::memcpy(owner.name,"NativeFiles",sizeof("NativeFiles"));owner.enabled=owner.mandatory=owner.destructive=true;
    owner.user=&registry;owner.capture_baseline=owner.preflight=owner.quiesce=owner.cancel_quiesce=owner.reload=owner.verify=&PhaseOk;
    owner.reset=&StartMutation;
    CHECK(cold::RegisterAdapter(&registry,&owner));CHECK(cold::SetRegistryFinalizer(&registry,&TryFactoryAtCommit,nullptr));
    CHECK(cold::SealRegistry(&registry));cold::RestartContext context={};context.baseline_generation=1;
    context.owner_thread_id=GetCurrentThreadId();CHECK(cold::CaptureRegistryBaseline(&registry,&context)==cold::RestartResult::Verified);
    context.transaction_generation=1;context.content_epoch=1;
    CHECK(cold::RunRegistryTransaction(&registry,&context)==cold::RestartResult::RuntimeFailed);
    CHECK(late_factory_rejected && !factory_calls && !state.ledger.admission_closed && registry.admission_blocked);
    CHECK(!state.ledger.active_factories && !registry.retaining_observation_active_calls);return true;
}
bool CapturesActualFactoryArguments(){
    CHECK(Reset(true));CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    using Factory=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
    auto create=reinterpret_cast<Factory>(bridge::HookTarget(win::Route::Win32Factory));
    CHECK(create(&source,factory_path,0x80000000,3,0x80)==&file);
    CHECK(request_calls==1&&last_request.route==win::Route::Win32Factory&&last_request.path==reinterpret_cast<std::uintptr_t>(factory_path));
    CHECK(last_request.access==0x80000000&&last_request.creation==3&&last_request.flags==0x80);
    CHECK(last_request.explicit_source==reinterpret_cast<std::uintptr_t>(&source)&&last_request.source_identity_observed);
    CHECK(last_request.source_identity.address==last_request.explicit_source&&last_request.source_identity.generation);
    CHECK(last_path.complete&&!std::strcmp(last_path.narrow,factory_path));return true;
}
bool NativePathUsesActualComponents(){
    CHECK(Reset(true));bridge::FactoryRequest request={};request.path=reinterpret_cast<std::uintptr_t>(native_path);
    request.path_representation=bridge::PathRepresentation::NativePath;
    std::memset(native_path,0,sizeof(native_path));std::strcpy(reinterpret_cast<char*>(native_path),"file.bin");
    std::strcpy(reinterpret_cast<char*>(native_path+32),"assets");
    bridge::FactoryPathSnapshot snapshot={};CHECK(bridge::SnapshotFactoryPath(&state,request,&snapshot));
    CHECK(!std::strcmp(snapshot.filename,"file.bin")&&!std::strcmp(snapshot.directory,"assets")&&!snapshot.narrow[0]);
    const auto name=reinterpret_cast<std::uintptr_t>(heap_name),directory=reinterpret_cast<std::uintptr_t>(heap_directory);
    std::memcpy(native_path,&name,8);std::memcpy(native_path+32,&directory,8);native_path[64]=3;
    CHECK(bridge::SnapshotFactoryPath(&state,request,&snapshot));
    CHECK(!std::strcmp(snapshot.filename,heap_name)&&!std::strcmp(snapshot.directory,heap_directory));
    native_path[64]=0;std::memset(native_path,'x',32);CHECK(!bridge::SnapshotFactoryPath(&state,request,&snapshot));return true;
}
bool ClosedFactoriesRequireIndividualAuthority(){
    CHECK(Reset(true));CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    CHECK(life::CloseAdmission(&state.ledger,9));offer_replay=true;
    bridge::FactoryRequest request={};request.route=win::Route::PublicOpen;request.path_representation=bridge::PathRepresentation::NarrowString;
    request.path=reinterpret_cast<std::uintptr_t>(factory_path);
    {
        bridge::FactoryCall parent(&state,request);CHECK(parent.admitted());
        CHECK(!last_request.explicit_source&&!last_request.source_identity_observed&&!last_request.source_identity.generation);
        using Factory=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
        auto create=reinterpret_cast<Factory>(bridge::HookTarget(win::Route::Win32Factory));
        CHECK(create(&source,other_path,0x80000000,3,0x80)==nullptr&&!factory_calls);
        CHECK(last_request.path==reinterpret_cast<std::uintptr_t>(other_path));
        CHECK(create(&source,factory_path,0x80000000,3,0x80)==&file&&factory_calls==1);
    }
    CHECK(bridge::Idle(&state)&&!state.ledger.failure);return true;
}
bool CallbackCannotBypassLedgerValidator(){
    CHECK(Reset(true));CHECK(life::CloseAdmission(&state.ledger,9));offer_replay=true;validator_accepts=false;
    bridge::FactoryRequest request={};request.route=win::Route::PublicOpen;request.path_representation=bridge::PathRepresentation::NarrowString;
    request.path=reinterpret_cast<std::uintptr_t>(factory_path);bridge::FactoryCall call(&state,request);CHECK(!call.admitted());return true;
}
bool NullableGateAndNestedDestruction(){
    CHECK(Reset());CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    life::SourceKey identity={};CHECK(bridge::SourceIdentity(&state,reinterpret_cast<std::uintptr_t>(&source),&identity));
    using Factory=void*(__fastcall*)(void*,const char*,std::uint32_t,std::uint32_t,std::uint32_t);
    using Scalar=void*(__fastcall*)(void*,std::uint32_t);
    auto create=reinterpret_cast<Factory>(bridge::HookTarget(win::Route::Win32Factory));
    CHECK(create(&source,"fixture",0,0,0)==&file&&factory_calls==1);
    life::FileRecord observed={};CHECK(bridge::FileIdentity(&state,reinterpret_cast<std::uintptr_t>(&file),&observed));
    CHECK(observed.generation&&observed.source.generation==identity.generation);
    CHECK(life::CloseAdmission(&state.ledger,7));
    CHECK(create(&source,"fixture",0,0,0)==nullptr&&factory_calls==1);
    CHECK(reinterpret_cast<Scalar>(bridge::HookTarget(win::Route::Win32Destructor))(&file,1)==&file);
    CHECK(base_destructor_calls==1&&!state.ledger.failure&&!bridge::FileIdentity(&state,reinterpret_cast<std::uintptr_t>(&file),&observed));
    life::Proof proof={};CHECK(life::SnapshotSource(&state.ledger,identity,&proof)&&proof.ready&&!proof.source_destroyed);
    CHECK(!life::CompleteSourceRetirement(&state.ledger,proof));
    CHECK(reinterpret_cast<Scalar>(bridge::HookTarget(win::Route::DirectorySourceDestructor))(&source,1)==&source);
    CHECK(life::CompleteSourceRetirement(&state.ledger,proof)&&bridge::Idle(&state));
    CHECK(life::ReopenAdmission(&state.ledger,7));
    CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    life::SourceKey replacement={};CHECK(bridge::SourceIdentity(&state,reinterpret_cast<std::uintptr_t>(&source),&replacement));
    CHECK(replacement.generation!=identity.generation);return true;
}
bool InPlaceConstructorAlwaysForwardsAndBlocksUnknownCoverage(){
    CHECK(Reset());CHECK(bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    CHECK(life::CloseAdmission(&state.ledger,8));
    using Constructor=void*(__fastcall*)(void*,void*,void*,std::uint64_t);
    CHECK(reinterpret_cast<Constructor>(bridge::HookTarget(win::Route::MemoryConstructor))(&file,&source,nullptr,0)==&file);
    CHECK(constructor_calls==1&&state.ledger.failure==static_cast<LONG>(life::Failure::UnobservedFactory));
    return true;
}
bool ExistingSourceCannotBePromoted(){
    CHECK(Reset());CHECK(bridge::ExistingSource(&state,reinterpret_cast<std::uintptr_t>(&source)));
    life::SourceKey old={};CHECK(bridge::SourceIdentity(&state,reinterpret_cast<std::uintptr_t>(&source),&old));
    CHECK(!bridge::SourcePublication(&state,reinterpret_cast<std::uintptr_t>(&source),true));
    CHECK(state.ledger.failure==static_cast<LONG>(life::Failure::SourceReuse));return true;
}
struct EnvelopeFixture {std::uintptr_t address;unsigned char bytes[16];};
bool ReadEnvelope(void* user,std::uintptr_t address,void* output,std::size_t size)noexcept{
    const auto* fixture=static_cast<EnvelopeFixture*>(user);
    if(address!=fixture->address||size!=16)return false;std::memcpy(output,fixture->bytes,16);return true;
}
bool CompletionEnvelopesPreserveNativeBranches(){
    const struct {win::Route route;std::uintptr_t site,resume,target;unsigned char bytes[16];} fixtures[]={
        {win::Route::QueueCancelComplete,0x1cc01,0x1cc07,0x1cc22,{0x00,0xf0,0xff,0x4b,0x28,0x75,0x1b,0x48,0x8b,0x4b,0x10,0xff,0x15,0x5f,0x14,0x02}},
        {win::Route::QueueSkippedComplete,0x1ced8,0x1cee2,0x1cca1,{0x33,0xc0,0x88,0x85,0x80,0x01,0x00,0x00,0xf0,0xff,0x4f,0x28,0x0f,0x85,0xbf,0xfd}},
        {win::Route::QueueReadComplete,0x1d070,0x1d07a,0x1cca1,{0xf0,0xff,0x4f,0x28,0x0f,0x85,0x27,0xfc,0xff,0xff,0x48,0x8b,0x4f,0x10,0xff,0x15}},
    };
    for(const auto& fixture:fixtures){
        EnvelopeFixture memory={base+(fixture.site&~std::uintptr_t{15}),{}};
        std::memcpy(memory.bytes,fixture.bytes,16);
        win::Binding binding={base,&ReadEnvelope,nullptr,&memory,true};
        win::CompletionRelay result={};const auto allocation=base+0x80000;
        CHECK(win::PrepareCompletionRelay(&binding,fixture.route,allocation,0x700000000000ULL,&result));
        CHECK(result.site==base+fixture.site&&result.resume==base+fixture.resume&&result.conditional_target==base+fixture.target);
        const auto offset=result.site-result.patch_address;
        for(unsigned i=0;i<16;++i)if(i<offset||i>=offset+5)CHECK(result.original_envelope[i]==result.patched_envelope[i]);
        CHECK(result.patched_envelope[offset]==0xe9&&std::memcmp(result.code+16,fixture.bytes+offset,4)==0);
        CHECK(result.code[20]==0x0f&&result.code[21]==0x85); // JNE receives native LOCK DEC's ZF.
        std::int32_t displacement=0;std::memcpy(&displacement,result.code+22,4);
        CHECK(static_cast<std::int64_t>(allocation+26)+displacement==static_cast<std::int64_t>(base+fixture.target));
        std::uintptr_t resume=0;std::memcpy(&resume,result.code+32,8);CHECK(resume==base+fixture.resume);
        CHECK(result.unwind[0]==1&&result.unwind[1]==0&&result.unwind[2]==(fixture.route==win::Route::QueueCancelComplete?6:10));
        memory.bytes[0]^=1;CHECK(!win::PrepareCompletionRelay(&binding,fixture.route,allocation,0x700000000000ULL,&result));
    }return true;
}
}
int main(){
    if(!NullableGateAndNestedDestruction()||!InPlaceConstructorAlwaysForwardsAndBlocksUnknownCoverage()||
       !ExistingSourceCannotBePromoted()||!CompletionEnvelopesPreserveNativeBranches()||!CapturesActualFactoryArguments()||
       !NativePathUsesActualComponents()||!ClosedFactoriesRequireIndividualAuthority()||!CallbackCannotBypassLedgerValidator()||
       !FreshReturnAuthorityEndsBeforePublication()||!OpenFilesystemLedgerCannotBypassRegistryCommit())return 1;
    g_dluma_file_completion_ring=nullptr;
    std::puts("native file bridge: nullable admission, nested destruction, exact generations and three branch-aware envelopes passed");return 0;
}
