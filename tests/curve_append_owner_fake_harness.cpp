#include "fixes/custom_map_asset_isolation/CurveAppendOnlyOwner.h"
#include "fixes/custom_map_asset_isolation/NativeDetourBranch.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
namespace f=dluma::fixes::custom_map_asset_isolation::file_lifetime;
using R=c::CurveOwnerResult;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Shape {std::uintptr_t vt,samples;std::uint32_t samples_count,samples_capacity,group,pad28,bounds[4],requested,pad52;
    std::uintptr_t name;std::uint32_t name_count,name_capacity,sum,pad76;std::uintptr_t points;
    std::uint32_t point_count,point_capacity,points_group,pad100,logical_count,pad108;};
struct Vec {std::uintptr_t data;std::uint32_t count,capacity,group,pad;};
struct Storage {c::CurveRecord records[4]={};std::uint8_t bytes[2048]={};c::CurveSnapshot value={records,4,bytes,sizeof(bytes)};};
struct Fixture {
    static constexpr std::uintptr_t base=0x10000000;
    c::CurveNativeGateway gate;c::CurveAppendOnlyOwner owner;
    c::CurveGatewayContext context={};Storage baseline,current;
    Shape shapes[4]={};c::CurvePoint points[4][2]={};std::uint32_t samples[4][2]={};char names[4][24]={};std::uintptr_t objects[4]={};
    std::uintptr_t manager_vt=base+c::kManagerVtableRva,manager=reinterpret_cast<std::uintptr_t>(&manager_vt);
    Vec vector={reinterpret_cast<std::uintptr_t>(objects),0,4,1,0},item_vector={};
    std::uintptr_t indexed[6]={},scalars[5]={};unsigned writes=0,fail_write=0,deletes=0,potential_writes=0,private_deletes=0;
    c::CurveProcessCacheSnapshot caches={};
    bool fail_delete=false,coverage=true,invalid_delete=false;
    std::atomic<bool> pause_delete{false},delete_entered{false},release_delete{false};
    std::atomic<bool> inject_context_reader{false},context_reader_ready{false};
    Fixture();
    void add(const char* name){auto i=vector.count++;auto& s=shapes[i];std::strcpy(names[i],name);
        s.vt=base+c::kSplineVtableRva;s.samples=reinterpret_cast<std::uintptr_t>(samples[i]);s.samples_count=2;s.samples_capacity=2;s.requested=2;
        s.bounds[1]=0x3f800000;s.bounds[3]=0x3f800000;s.name=reinterpret_cast<std::uintptr_t>(names[i]);s.name_count=static_cast<unsigned>(std::strlen(name));s.name_capacity=23;
        s.points=reinterpret_cast<std::uintptr_t>(points[i]);s.point_count=2;s.point_capacity=2;s.logical_count=2;
        points[i][0]={0,0,0};points[i][1]={0x3f800000,0x3f800000,1};samples[i][1]=0x3f800000;objects[i]=reinterpret_cast<std::uintptr_t>(&s);}
    void project(c::CurveSourceOwnership ownership=c::CurveSourceOwnership::ExclusiveProject,bool observe=true){
        context.transaction=31;context.epoch=4;context.post_c10_consumers_closed=true;context.startup_owner_absence=false;
        c::CurveLoadToken load={};c::BeginCurveLoadObservation("data/curves.xml",4,&load);
        c::CurveOpenedSource source={};source.file={0x6000,{0x7000,11},9,8,f::FileKind::Win32,f::Life::Live};
        source.source_revision=7;source.project_epoch=4;source.ownership=ownership;c::NoteOpenedCurveSource("data/curves.xml",&source);
        auto before=vector.count;add("project_curve");if(observe)c::ObserveCurveAppend(&gate,objects[before],before,static_cast<int>(before));c::EndCurveLoadObservation(&load);
        indexed[1]=objects[before];scalars[4]=objects[before];}
};
Fixture* private_delete_fixture=nullptr;
std::uintptr_t __fastcall PrivateDestructor(void* object,unsigned char flags){
    auto& x=*private_delete_fixture;const auto identity=reinterpret_cast<std::uintptr_t>(object);
    for(std::uint32_t i=0;i<x.vector.count;++i)if(x.objects[i]==identity)x.invalid_delete=true;
    if(flags!=1)x.invalid_delete=true;++x.private_deletes;return identity;
}
bool Copy(std::uintptr_t p,void* out,std::size_t n,const void* object,std::size_t size){
    auto begin=reinterpret_cast<std::uintptr_t>(object);if(p<begin || n>size || p-begin>size-n)return false;
    std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
bool Read(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {
    auto& x=*static_cast<Fixture*>(user);
    std::size_t cache_count=0;const auto* caches=c::CurveProcessCacheCatalog(&cache_count);
    for(std::size_t i=0;i<cache_count;++i){
        if(p==x.base+caches[i].id_rva && n==4){std::memcpy(out,&x.caches.entries[i].id,4);return true;}
        if(p==x.base+caches[i].flag_rva && n==4){std::memcpy(out,&x.caches.entries[i].flag,4);return true;}}
    if(p>=x.base+0x1E12890 && p+n<=x.base+0x1E128E0){std::memcpy(out,reinterpret_cast<const std::uint8_t*>(x.caches.weights)+(p-x.base-0x1E12890),n);return true;}
    if(p==x.base+0x1DF4FA4 && n==4){std::memcpy(out,&x.caches.weight_sum,4);return true;}
    if(p==x.base+0x1DF4FA0 && n==1){std::memcpy(out,&x.caches.weights_valid,1);return true;}
    if(p==x.base+c::kManagerRva && n==8){std::memcpy(out,&x.manager,8);return true;}
    if(p>=x.base+c::kVectorRva && p+n<=x.base+c::kVectorRva+sizeof(Vec)){std::memcpy(out,reinterpret_cast<const std::uint8_t*>(&x.vector)+(p-x.base-c::kVectorRva),n);return true;}
    if(p==x.base+c::kItemManagerRva && n==8){const auto vt=x.base+c::kItemManagerVtableRva;std::memcpy(out,&vt,8);return true;}
    if(p==x.base+c::kItemManagerRva+0x218 && n==sizeof(Vec)){std::memcpy(out,&x.item_vector,n);return true;}
    if(p>=x.base+c::kItemManagerRva+0x230 && p+n<=x.base+c::kItemManagerRva+0x258){std::memcpy(out,reinterpret_cast<const std::uint8_t*>(x.scalars)+(p-x.base-c::kItemManagerRva-0x230),n);return true;}
    std::size_t count=0;const auto* catalog=c::CurveHookCatalog(&count);
    for(std::size_t i=0;i<count;++i){const auto& d=catalog[i];if(d.pointer_slot_rva && p==x.base+d.pointer_slot_rva && n==8){const auto value=x.coverage?c::CurveHookWrapper(d.kind):0;std::memcpy(out,&value,8);return true;}
        if(!d.pointer_slot_rva && p==x.base+d.function_rva && n==14){std::uint8_t patch[14]={0xff,0x25};auto wrapper=x.coverage?c::CurveHookWrapper(d.kind):0;std::memcpy(patch+6,&wrapper,8);std::memcpy(out,patch,n);return true;}}
    return Copy(p,out,n,&x.manager_vt,8)||Copy(p,out,n,x.shapes,sizeof(x.shapes))||Copy(p,out,n,x.points,sizeof(x.points))||Copy(p,out,n,x.samples,sizeof(x.samples))||
        Copy(p,out,n,x.names,sizeof(x.names))||Copy(p,out,n,x.objects,sizeof(x.objects))||Copy(p,out,n,x.indexed,sizeof(x.indexed));
}
bool Context(void* user,c::CurveGatewayContext* out) noexcept {auto& x=*static_cast<Fixture*>(user);
    if(x.inject_context_reader.exchange(false)){x.context_reader_ready.store(true);while(!x.gate.pending_readers.load())Sleep(1);}
    *out=x.context;return true;}
void PotentialWrite(void* user,c::CurveHook) noexcept {auto& x=*static_cast<Fixture*>(user);if(x.context.epoch)++x.potential_writes;}
bool Write(void* user,std::uintptr_t p,const void* before,const void* desired,std::size_t n) noexcept {
    auto& x=*static_cast<Fixture*>(user);if(++x.writes==x.fail_write)return false;
    std::uint8_t bytes[8]={};if(n>8 || !Read(user,p,bytes,n) || std::memcmp(bytes,before,n))return false;
    std::size_t cache_count=0;const auto* caches=c::CurveProcessCacheCatalog(&cache_count);
    for(std::size_t i=0;i<cache_count;++i){
        if(p==x.base+caches[i].id_rva && n==4){std::memcpy(&x.caches.entries[i].id,desired,4);return true;}
        if(p==x.base+caches[i].flag_rva && n==4){std::memcpy(&x.caches.entries[i].flag,desired,4);return true;}}
    if(p>=x.base+0x1E12890 && p+n<=x.base+0x1E128E0){std::memcpy(reinterpret_cast<std::uint8_t*>(x.caches.weights)+(p-x.base-0x1E12890),desired,n);return true;}
    if(p==x.base+0x1DF4FA4 && n==4){std::memcpy(&x.caches.weight_sum,desired,4);return true;}
    if(p==x.base+0x1DF4FA0 && n==1){std::memcpy(&x.caches.weights_valid,desired,1);return true;}
    if(p==x.base+c::kVectorRva+8){std::memcpy(&x.vector.count,desired,n);return true;}
    if(p>=x.base+c::kItemManagerRva+0x230 && p+n<=x.base+c::kItemManagerRva+0x258){std::memcpy(reinterpret_cast<std::uint8_t*>(x.scalars)+(p-x.base-c::kItemManagerRva-0x230),desired,n);return true;}
    const auto start=reinterpret_cast<std::uintptr_t>(x.indexed);if(p>=start && p+n<=start+sizeof(x.indexed)){std::memcpy(reinterpret_cast<void*>(p),desired,n);return true;}return false;
}
bool Delete(void* user,const c::CurveGateLease* lease,std::uintptr_t object) noexcept {
    auto& x=*static_cast<Fixture*>(user);c::CurveAppendRecord r={};
    if(x.fail_delete || !c::CurveGatewayLeaseMatches(&x.gate,lease) || !c::FindCurveAppend(&x.gate,object,&r) || !r.detached || r.retired ||
        r.unknown_borrow || x.vector.count!=2)return false;
    if(x.pause_delete.load()){x.delete_entered.store(true);while(!x.release_delete.load())Sleep(1);}
    for(unsigned i=0;i<2;++i)if(x.objects[i]==object)return false;++x.deletes;return true;
}
Fixture::Fixture(){
    add("base_curve");add("global_extra");for(auto& p:indexed)p=objects[0];for(auto& p:scalars)p=objects[1];
    item_vector={reinterpret_cast<std::uintptr_t>(indexed),6,6,1,0};gate.module_base=base;gate.callbacks={Read,Context,this,PotentialWrite};gate.initialized=true;gate.startup_sealed.store(true);
    std::size_t count=0;const auto* catalog=c::CurveHookCatalog(&count);for(std::size_t i=0;i<count;++i)gate.originals[i]=base+catalog[i].function_rva;
    gate.originals[static_cast<std::size_t>(c::CurveHook::DestroySpline)]=reinterpret_cast<std::uintptr_t>(&PrivateDestructor);private_delete_fixture=this;
    context.startup_owner_absence=true;auto& a=context.authority;a.exact_route_identity_seeded=1;a.game_frame_authority_frozen=1;
    a.game_frame_authority_proof=DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF;a.exact_route_serialization_token=1;a.exact_route_game_subobject=2;a.exact_route_game_primary=3;
    c::CurveOwnerOperations operations={&gate,Write,Delete,this};c::InitializeCurveOwner(&owner,&operations,&baseline.value,&current.value);
}
bool CompleteAppendCleanup(){auto p=std::make_unique<Fixture>();auto& x=*p;CHECK(c::CaptureCurveOwnerBaseline(&x.owner)==R::Succeeded);auto original=x.objects[0];
    x.project();CHECK(c::PrepareCurveOwner(&x.owner,31)==R::Succeeded);CHECK(c::ResetCurveOwner(&x.owner)==R::Succeeded);
    CHECK(x.vector.count==2 && x.objects[0]==original && x.indexed[1]==original && x.scalars[4]==x.objects[1]);CHECK(x.deletes==0);
    CHECK(c::RetireCurveOwnerTail(&x.owner)==R::Succeeded);CHECK(x.deletes==1);CHECK(c::CompleteCurveOwner(&x.owner)==R::Succeeded);CHECK(!x.gate.held.load());return true;}
bool UnobservedTailRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project(c::CurveSourceOwnership::ExclusiveProject,false);
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::OwnershipUnknown);CHECK(x->writes==0 && x->deletes==0 && !x->gate.held.load());return true;}
bool SharedTailRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project(c::CurveSourceOwnership::SharedGlobal);
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::OwnershipUnknown);return true;}
bool UnknownBorrowRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();c::ObserveCurvePointer(&x->gate,x->objects[2],0x1234);
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::OwnershipUnknown);return true;}
bool BaselineMutationRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();x->points[0][0].type=1;
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::BaselineChanged);return true;}
bool EqualReplacementBuffersRefuse(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();x->shapes[0].samples=x->shapes[1].samples;
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::BaselineChanged);return true;}
bool FailedBindingStoreKeepsTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);x->fail_write=2;
    CHECK(c::ResetCurveOwner(&x->owner)==R::WriteFailed);CHECK(x->vector.count==3 && x->deletes==0);return true;}
bool FailedCountStoreKeepsTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);x->fail_write=3;
    CHECK(c::ResetCurveOwner(&x->owner)==R::WriteFailed);CHECK(x->vector.count==3 && x->deletes==0);return true;}
bool FailedDeleteRetainsDetachedAllocation(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);
    CHECK(c::ResetCurveOwner(&x->owner)==R::Succeeded);x->fail_delete=true;CHECK(c::RetireCurveOwnerTail(&x->owner)==R::DeleteFailed);CHECK(x->vector.count==2 && !x->deletes && x->owner.detached_count==1);return true;}
bool StaleContextCannotRetire(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);
    CHECK(c::ResetCurveOwner(&x->owner)==R::Succeeded);++x->context.transaction;CHECK(c::RetireCurveOwnerTail(&x->owner)==R::InvalidArgument);CHECK(!x->deletes);return true;}
bool CancelReopensWithoutMutation(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);
    CHECK(c::CancelCurveOwner(&x->owner)==R::Succeeded);CHECK(!x->gate.held.load() && x->vector.count==3 && !x->writes);return true;}
bool DuplicateNameAllocationNotRegistered(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);c::ObserveCurveAppend(&x->gate,0x5000,2,0);
    CHECK(!x->gate.appended_count && !x->gate.lost.load());return true;}
bool CoverageLossRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();x->coverage=false;CHECK(c::PrepareCurveOwner(&x->owner,31)==R::GateRejected);return true;}
bool NativeWholeCallDrains(){auto x=std::make_unique<Fixture>();c::CurveCallToken token={};c::BeginCurveCall(&x->gate,c::CurveHook::ItemDefinitions,&token);CHECK(token.active && x->gate.active_calls.load()==1);
    c::CurveGateLease lease={};CHECK(c::CloseCurveGateway(&x->gate,0,&lease)==c::CurveGateResult::Busy);c::EndCurveCall(&x->gate,&token);CHECK(x->gate.active_calls.load()==0);return true;}
bool WritesPromoteWithoutSourceReceipt(){auto x=std::make_unique<Fixture>();x->context.epoch=4;c::CurveCallToken token={};
    c::BeginCurveCall(&x->gate,c::CurveHook::EvaluateSamples,&token);c::EndCurveCall(&x->gate,&token);CHECK(x->potential_writes==0);
    c::BeginCurveCall(&x->gate,c::CurveHook::Load,&token);CHECK(x->potential_writes==1);c::EndCurveCall(&x->gate,&token);
    CHECK(!x->gate.appended_count);return true;}
bool UnownedDestructorPreservesBaselineGeneration(){auto x=std::make_unique<Fixture>();const auto generation=x->gate.owner_generation.load();
    c::ObserveCurveDestruction(&x->gate,0x9000);CHECK(x->gate.owner_generation.load()==generation);
    c::ObserveCurveDestruction(&x->gate,x->objects[0]);CHECK(x->gate.owner_generation.load()==generation+1);return true;}
bool PrivateDuplicateWaitsUntilWholeLoad(){auto x=std::make_unique<Fixture>();c::CurveLoadToken load={};
    CHECK(c::BeginCurveLoadObservation("data/curves.xml",4,&load));x->add("base_curve");--x->vector.count;
    c::ObserveCurveConstruction(&x->gate,x->objects[2],x->base+0x10E1CFD);
    c::ObserveCurveAppend(&x->gate,x->objects[2],2,0);CHECK(!x->private_deletes && !x->gate.appended_count);
    CHECK(c::FinishCurvePrivateAllocations(&x->gate,&load));CHECK(x->private_deletes==1 && !x->invalid_delete && x->vector.count==2);
    CHECK(c::FinishCurvePrivateAllocations(&x->gate,&load));CHECK(x->private_deletes==1);CHECK(c::EndCurveLoadObservation(&load));return true;}
bool AdoptedAllocationNeverUsesPrivateDestructor(){auto x=std::make_unique<Fixture>();c::CurveLoadToken load={};
    CHECK(c::BeginCurveLoadObservation("data/curves.xml",4,&load));x->add("project_curve");--x->vector.count;
    c::ObserveCurveConstruction(&x->gate,x->objects[2],x->base+0x10E1E9A);++x->vector.count;
    c::ObserveCurveAppend(&x->gate,x->objects[2],2,2);CHECK(c::FinishCurvePrivateAllocations(&x->gate,&load));
    CHECK(!x->private_deletes && x->vector.count==3);CHECK(c::EndCurveLoadObservation(&load));return true;}
bool UnknownConstructorCannotGrantPrivateOwnership(){auto x=std::make_unique<Fixture>();c::CurveLoadToken load={};
    CHECK(c::BeginCurveLoadObservation("data/curves.xml",4,&load));x->add("base_curve");--x->vector.count;
    c::ObserveCurveConstruction(&x->gate,x->objects[2],x->base+0x1234);c::ObserveCurveAppend(&x->gate,x->objects[2],2,0);
    CHECK(c::FinishCurvePrivateAllocations(&x->gate,&load));CHECK(!x->private_deletes && !x->gate.allocation_count);CHECK(c::EndCurveLoadObservation(&load));return true;}
bool RepeatedDuplicateLoadsReuseTrackingSlot(){auto x=std::make_unique<Fixture>();
    for(unsigned i=0;i<3;++i){c::CurveLoadToken load={};CHECK(c::BeginCurveLoadObservation("data/curves.xml",4,&load));x->add("base_curve");--x->vector.count;
        c::ObserveCurveConstruction(&x->gate,x->objects[2],x->base+0x10E27CF);c::ObserveCurveAppend(&x->gate,x->objects[2],2,0);
        CHECK(c::FinishCurvePrivateAllocations(&x->gate,&load));CHECK(c::EndCurveLoadObservation(&load));}
    CHECK(x->private_deletes==3 && x->gate.allocation_count==1 && !x->invalid_delete);return true;}
bool CompleteFixture(Fixture& x){CHECK(c::CaptureCurveOwnerBaseline(&x.owner)==R::Succeeded);x.project();
    CHECK(c::PrepareCurveOwner(&x.owner,31)==R::Succeeded);CHECK(c::ResetCurveOwner(&x.owner)==R::Succeeded);
    CHECK(c::RetireCurveOwnerTail(&x.owner)==R::Succeeded);CHECK(c::CompleteCurveOwner(&x.owner)==R::Succeeded);return true;}
bool FinalReverifyDoesNotMutate(){auto x=std::make_unique<Fixture>();CHECK(CompleteFixture(*x));const auto writes=x->writes,deletes=x->deletes;
    ++x->gate.revision;c::CurveGateLease lease={};CHECK(c::ReverifyCompletedCurveOwner(&x->owner,31,&lease)==R::Succeeded);
    CHECK(lease.generation && x->gate.held.load() && !x->owner.mutation_started && x->writes==writes && x->deletes==deletes);
    CHECK(c::OpenCurveGateway(&x->gate,&lease)==c::CurveGateResult::Succeeded);return true;}
bool FinalReverifyRejectsLaterTail(){auto x=std::make_unique<Fixture>();CHECK(CompleteFixture(*x));x->add("late_curve");
    CHECK(c::ReverifyCompletedCurveOwner(&x->owner,31)==R::BaselineChanged);CHECK(!x->gate.held.load() && x->deletes==1);return true;}
bool FinalReverifyRejectsChangedBindings(){auto x=std::make_unique<Fixture>();CHECK(CompleteFixture(*x));x->scalars[0]=x->objects[0];
    CHECK(c::ReverifyCompletedCurveOwner(&x->owner,31)==R::BindingFailed);CHECK(!x->gate.held.load());return true;}
bool FinalReverifyRejectsStaleTransaction(){auto x=std::make_unique<Fixture>();CHECK(CompleteFixture(*x));
    CHECK(c::ReverifyCompletedCurveOwner(&x->owner,32)==R::WrongTransaction);CHECK(!x->gate.held.load());return true;}
bool WriterBetweenCaptureAndSealInvalidatesSnapshot(){auto x=std::make_unique<Fixture>();c::CurveGateLease first={},second={};c::NativeCurveAccess access={};
    CHECK(c::CloseCurveGateway(&x->gate,50,&first)==c::CurveGateResult::Succeeded);CHECK(c::MakeCurveGatewayAccess(&x->gate,&access));
    CHECK(c::CaptureCurveSnapshot(&access,&x->baseline.value)==c::CurveResult::Succeeded);CHECK(c::OpenCurveGateway(&x->gate,&first)==c::CurveGateResult::Succeeded);
    c::CurveCallToken call={};c::BeginCurveCall(&x->gate,c::CurveHook::Load,&call);c::EndCurveCall(&x->gate,&call);
    CHECK(c::CloseCurveGateway(&x->gate,50,&second)==c::CurveGateResult::Succeeded);CHECK(!c::SealCurveBaseline(&x->gate,&second,&x->baseline.value));
    CHECK(!x->gate.baseline_sealed);CHECK(c::OpenCurveGateway(&x->gate,&second)==c::CurveGateResult::Succeeded);return true;}
bool QueuedWriterPreventsBaselinePublication(){auto x=std::make_unique<Fixture>();c::CurveGateLease lease={};c::NativeCurveAccess access={};
    CHECK(c::CloseCurveGateway(&x->gate,50,&lease)==c::CurveGateResult::Succeeded);CHECK(c::MakeCurveGatewayAccess(&x->gate,&access));
    CHECK(c::CaptureCurveSnapshot(&access,&x->baseline.value)==c::CurveResult::Succeeded);std::atomic<bool> entered{false};
    std::thread writer([&](){c::CurveCallToken call={};c::BeginCurveCall(&x->gate,c::CurveHook::Load,&call);entered.store(true);c::EndCurveCall(&x->gate,&call);});
    const auto start=GetTickCount64();while(!x->gate.pending_writers.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool queued=x->gate.pending_writers.load()==1 && !x->gate.active_calls.load() && !entered.load();
    const bool rejected=!c::SealCurveBaseline(&x->gate,&lease,&x->baseline.value);c::CurveReadFence fence={};
    const bool closed_fence_refused=!c::CaptureCurveGatewayFence(&x->gate,&fence);
    const auto opened=c::OpenCurveGateway(&x->gate,&lease);writer.join();
    CHECK(queued && rejected && closed_fence_refused && opened==c::CurveGateResult::Succeeded);
    CHECK(entered.load() && !x->gate.pending_writers.load() && !x->gate.active_calls.load() && !x->gate.lost.load());return true;}
bool ProcessCacheAndWeightsRestore(){auto x=std::make_unique<Fixture>();x->caches.entries[0]={-1,1};
    CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    std::strcpy(x->names[2],"item_level_distribution");x->shapes[2].name_count=23;
    x->caches.entries[1]={2,1};x->caches.weights_valid=1;x->caches.weight_sum=0x41200000;
    for(auto& weight:x->caches.weights)weight=0x3f800000;
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);CHECK(c::ResetCurveOwner(&x->owner)==R::Succeeded);
    CHECK(x->caches.entries[0].id==-1 && x->caches.entries[0].flag==1);
    CHECK(!x->caches.entries[1].flag && !x->caches.entries[1].id && !x->caches.weights_valid && !x->caches.weight_sum);
    for(auto weight:x->caches.weights)CHECK(!weight);
    CHECK(c::RetireCurveOwnerTail(&x->owner)==R::Succeeded);CHECK(c::CompleteCurveOwner(&x->owner)==R::Succeeded);return true;}
bool InvalidCachedCurveIdentityRefuses(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    x->caches.entries[0]={2,1};CHECK(c::PrepareCurveOwner(&x->owner,31)==R::SnapshotFailed);CHECK(!x->writes && !x->deletes);return true;}
bool FailedCacheStoreKeepsTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    x->caches.entries[0]={-1,1};CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);x->fail_write=3;
    CHECK(c::ResetCurveOwner(&x->owner)==R::WriteFailed);CHECK(x->vector.count==3 && !x->deletes);return true;}
bool FinalReverifyRejectsChangedCaches(){auto x=std::make_unique<Fixture>();CHECK(CompleteFixture(*x));x->caches.entries[0]={-1,1};
    CHECK(c::ReverifyCompletedCurveOwner(&x->owner,31)==R::CacheFailed);CHECK(!x->gate.held.load());return true;}
bool QueuedReaderPreventsMutation(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);std::atomic<bool> entered{false};
    std::thread reader([&](){c::CurveCallToken call={};c::BeginCurveCall(&x->gate,c::CurveHook::EvaluateSamples,&call);entered.store(true);c::EndCurveCall(&x->gate,&call);});
    const auto start=GetTickCount64();while(!x->gate.pending_readers.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool queued=x->gate.pending_readers.load()==1 && !entered.load();
    const bool rejected=c::ResetCurveOwner(&x->owner)!=R::Succeeded;
    const auto opened=c::OpenCurveGateway(&x->gate,&x->owner.lease);reader.join();
    CHECK(queued && rejected && opened==c::CurveGateResult::Succeeded && entered.load());
    CHECK(!x->writes && !x->deletes && x->vector.count==3 && !x->gate.pending_readers.load());return true;}
bool InitializedCacheReadDoesNotPromote(){auto x=std::make_unique<Fixture>();x->context.epoch=4;c::CurveCallToken call={};
    c::BeginCurveCall(&x->gate,c::CurveHook::RandomDistribution,&call);c::EndCurveCall(&x->gate,&call);CHECK(x->potential_writes==1);
    x->caches.entries[0]={-1,1};c::BeginCurveCall(&x->gate,c::CurveHook::RandomDistribution,&call);c::EndCurveCall(&x->gate,&call);
    CHECK(x->potential_writes==1);x->caches.entries[1]={-1,1};
    c::BeginCurveCall(&x->gate,c::CurveHook::LevelDistribution,&call);c::EndCurveCall(&x->gate,&call);CHECK(x->potential_writes==1);return true;}
bool UnknownReturnedIdRetainsItsTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    c::ObserveCurveId(&x->gate,2,0x1234);CHECK(c::PrepareCurveOwner(&x->owner,31)==R::OwnershipUnknown);
    CHECK(x->vector.count==3 && !x->writes && !x->deletes);return true;}
bool BaselineReturnedIdDoesNotDebtTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    c::ObserveCurveId(&x->gate,0,0x1234);CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);CHECK(c::CancelCurveOwner(&x->owner)==R::Succeeded);return true;}
bool ReaderAfterPublicationRetainsTail(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);CHECK(c::ResetCurveOwner(&x->owner)==R::Succeeded);
    std::atomic<bool> entered{false};std::thread reader([&](){c::CurveCallToken call={};c::BeginCurveCall(&x->gate,c::CurveHook::EvaluateSamples,&call);
        entered.store(true);c::EndCurveCall(&x->gate,&call);});
    const auto start=GetTickCount64();while(!x->gate.pending_readers.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool queued=x->gate.publication_conflict.load() && !entered.load();
    const bool refused=c::RetireCurveOwnerTail(&x->owner)!=R::Succeeded && !x->deletes &&
        c::OpenCurveGateway(&x->gate,&x->owner.lease)==c::CurveGateResult::LostObservation;
    // The fake allocation was not freed. Production has no equivalent escape
    // from a failed publication; release this synthetic thread for test teardown.
    x->gate.arrivals.fetch_and(0x7fffffffU);x->gate.owner_thread.store(0);reader.join();
    CHECK(queued && refused);return true;}
bool ReaderBetweenFinalCheckAndPublicationCas(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);x->inject_context_reader.store(true);std::atomic<bool> entered{false};
    std::thread reader([&](){while(!x->context_reader_ready.load())Sleep(1);c::CurveCallToken call={};
        c::BeginCurveCall(&x->gate,c::CurveHook::EvaluateSpline,&call);entered.store(true);c::EndCurveCall(&x->gate,&call);});
    const bool refused=!c::BeginCurveOwnerPublication(&x->gate,&x->owner.lease) && !entered.load() && !x->gate.publication_active.load();
    const auto canceled=c::CancelCurveOwner(&x->owner);reader.join();
    CHECK(refused && canceled==R::Succeeded && entered.load() && x->vector.count==3 && !x->writes && !x->deletes);return true;}
bool ReaderDuringNativeDeletionCannotResume(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);x->project();
    CHECK(c::PrepareCurveOwner(&x->owner,31)==R::Succeeded);CHECK(c::ResetCurveOwner(&x->owner)==R::Succeeded);x->pause_delete.store(true);
    std::atomic<bool> entered{false};std::thread reader([&](){while(!x->delete_entered.load())Sleep(1);c::CurveCallToken call={};
        c::BeginCurveCall(&x->gate,c::CurveHook::EvaluateSamples,&call);entered.store(true);c::EndCurveCall(&x->gate,&call);});
    std::thread release([&](){while(!x->gate.pending_readers.load())Sleep(1);x->release_delete.store(true);});
    const auto retired=c::RetireCurveOwnerTail(&x->owner);release.join();
    const bool refused=retired==R::GateRejected && x->deletes==1 && x->gate.poisoned.load() && !entered.load() &&
        c::OpenCurveGateway(&x->gate,&x->owner.lease)==c::CurveGateResult::LostObservation;
    // This harness's delete callback records deletion without freeing memory.
    // Only its teardown may release the synthetic reader after the assertion.
    x->gate.arrivals.fetch_and(0x7fffffffU);x->gate.poisoned.store(false);x->gate.owner_thread.store(0);reader.join();
    CHECK(refused);return true;}
bool cache_abi_ok=true;
bool TrailingBranchPreservesDestination(){
    namespace d=dluma::fixes::custom_map_asset_isolation::detour;
    for(unsigned condition=0;condition<16;++condition)for(auto displacement:{-32,0,26,127}){
        std::uint8_t code[48];std::memset(code,0xcc,sizeof(code));code[14]=static_cast<std::uint8_t>(0x70+condition);
        code[15]=static_cast<std::uint8_t>(displacement);std::size_t size=0;
        CHECK(d::ExpandTrailingConditionalBranch(code,sizeof(code),0x10000000,0x20000000,16,14,&size));
        std::int32_t relative=0;std::memcpy(&relative,code+16,4);
        CHECK(size==20 && code[14]==0x0f && code[15]==0x80+condition &&
            static_cast<std::int64_t>(0x20000000)+20+relative==static_cast<std::int64_t>(0x10000000)+16+displacement);
        for(unsigned i=0;i<14;++i)CHECK(code[i]==0xcc);
        for(unsigned i=20;i<sizeof(code);++i)CHECK(code[i]==0xcc);
    }return true;}
bool TrailingBranchRejectsUnqualifiedLayout(){
    namespace d=dluma::fixes::custom_map_asset_isolation::detour;
    std::uint8_t code[48]={};code[14]=0x75;code[15]=0x1a;std::size_t size=99;
    CHECK(!d::ExpandTrailingConditionalBranch(code,19,0x10000000,0x20000000,16,14,&size));
    CHECK(!d::ExpandTrailingConditionalBranch(code,sizeof(code),0x10000000,0x20000000,16,13,&size));
    CHECK(!d::ExpandTrailingConditionalBranch(code,sizeof(code),0x10000000,0x9000000000ULL,16,14,&size));
    CHECK(!d::ExpandTrailingConditionalBranch(code,sizeof(code),UINTPTR_MAX-8,0x20000000,16,14,&size));
    code[15]=0xfe;CHECK(!d::ExpandTrailingConditionalBranch(code,sizeof(code),0x10000000,0x20000000,16,14,&size));
    code[15]=0x1a;code[14]=0xeb;CHECK(!d::ExpandTrailingConditionalBranch(code,sizeof(code),0x10000000,0x20000000,16,14,&size));
    CHECK(size==99 && code[14]==0xeb && code[15]==0x1a);return true;}
void ActiveFrame(Fixture& x,std::uint64_t sequence){auto& a=x.context.authority;auto& t=a.trace[0];t={};
    a.current_dispatch_thread_id=static_cast<LONG>(GetCurrentThreadId());a.last_route_sequence=sequence;a.trace_count=1;a.trace_write_index=1;
    t.sequence=sequence;t.thread_id=a.current_dispatch_thread_id;t.game_subobject=a.exact_route_game_subobject;t.game_primary=a.exact_route_game_primary;
    t.caller_module_base=a.exact_route_caller_module_base;t.caller_rva=a.exact_route_caller_rva;t.serialization_token=a.exact_route_serialization_token;
    t.callback_entry_qpc=1;t.concurrent_depth=t.recursive_depth=1;t.route_proof=DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF;t.authority_result=DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH;
    x.context.startup_owner_absence=false;}
bool SnapshotLeaseCannotRetire(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);
    c::NoteUnknownCurveBorrow(&x->gate.borrowers);c::CurveGateLease lease={};
    CHECK(c::CloseCurveGatewayForSnapshot(&x->gate,0,&lease)==c::CurveGateResult::Succeeded && lease.purpose==c::CurveLeasePurpose::Snapshot);
    CHECK(c::CurveGatewayLeaseMatches(&x->gate,&lease) && !c::BeginCurveOwnerPublication(&x->gate,&lease) && !c::BeginCurveOwnerRetirement(&x->gate,&lease));
    auto forged=lease;forged.purpose=c::CurveLeasePurpose::Owner;CHECK(!c::CurveGatewayLeaseMatches(&x->gate,&forged));
    CHECK(c::OpenCurveGateway(&x->gate,&lease)==c::CurveGateResult::Succeeded && x->gate.borrowers.unknown_borrow && !x->writes && !x->deletes);return true;}
bool SnapshotLeasePinsFrame(){auto x=std::make_unique<Fixture>();CHECK(c::CaptureCurveOwnerBaseline(&x->owner)==R::Succeeded);ActiveFrame(*x,44);
    c::CurveGateLease lease={};CHECK(c::CloseCurveGatewayForSnapshot(&x->gate,0,&lease)==c::CurveGateResult::Succeeded);
    CHECK(c::CurveGatewayLeaseMatches(&x->gate,&lease));ActiveFrame(*x,45);CHECK(!c::CurveGatewayLeaseMatches(&x->gate,&lease));
    CHECK(c::OpenCurveGateway(&x->gate,&lease)==c::CurveGateResult::Succeeded);return true;}
bool SnapshotRejectsQueuedItemProducer(){auto x=std::make_unique<Fixture>();c::CurveGateLease lease={};
    CHECK(c::CloseCurveGatewayForSnapshot(&x->gate,0,&lease)==c::CurveGateResult::Succeeded);std::atomic<bool> entered{false};
    std::thread writer([&](){c::CurveCallToken t={};c::BeginCurveCall(&x->gate,c::CurveHook::ItemFamilyLoad,&t);entered=true;c::EndCurveCall(&x->gate,&t);});
    const auto start=GetTickCount64();while(!x->gate.pending_writers.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool blocked=x->gate.pending_writers.load()==1 && !entered.load() && !c::CurveGatewayLeaseMatches(&x->gate,&lease);
    const auto opened=c::OpenCurveGateway(&x->gate,&lease);writer.join();CHECK(blocked && opened==c::CurveGateResult::Succeeded && entered.load());return true;}
bool SnapshotRejectsEveryEnumProducer(){
    for(auto hook:{c::CurveHook::ItemRegisterEnum,c::CurveHook::ItemRegisterSavedEnum,c::CurveHook::ItemRegisterCannonEnum,
            c::CurveHook::ItemGenerateEnum,c::CurveHook::ItemGenerateSavedEnum,c::CurveHook::ItemGenerateCannonEnum}){
        auto x=std::make_unique<Fixture>();x->context.epoch=4;c::CurveGateLease lease={};
        CHECK(c::CloseCurveGatewayForSnapshot(&x->gate,0,&lease)==c::CurveGateResult::Succeeded);
        const auto revision=x->gate.revision.load();std::atomic<bool> entered{false};
        std::thread writer([&](){c::CurveCallToken t={};c::BeginCurveCall(&x->gate,hook,&t);entered=true;c::EndCurveCall(&x->gate,&t);});
        const auto start=GetTickCount64();while(!x->gate.pending_writers.load() && GetTickCount64()-start<2000)Sleep(1);
        const bool invalid=x->gate.pending_writers.load()==1 && !entered.load() && !c::CurveGatewayLeaseMatches(&x->gate,&lease);
        const auto opened=c::OpenCurveGateway(&x->gate,&lease);writer.join();
        CHECK(invalid && opened==c::CurveGateResult::Succeeded && entered.load() && x->potential_writes==1 &&
            x->gate.revision.load()==revision+2 && !x->gate.lost.load() && !x->writes && !x->deletes);
    }return true;}
bool EnumGenerationInvalidatesOwnerInspection(){
    for(auto hook:{c::CurveHook::ItemRegisterEnum,c::CurveHook::ItemRegisterSavedEnum,c::CurveHook::ItemRegisterCannonEnum,
            c::CurveHook::ItemGenerateEnum,c::CurveHook::ItemGenerateSavedEnum,c::CurveHook::ItemGenerateCannonEnum}){
        auto x=std::make_unique<Fixture>();c::CurveCallToken actual={};c::CurveItemInspection inspection={};
        c::BeginCurveCall(&x->gate,c::CurveHook::ItemFamilyLoad,&actual);
        CHECK(c::BeginCurveItemInspection(&x->gate,&actual,&inspection));std::atomic<bool> entered{false};
        std::thread writer([&](){c::CurveCallToken t={};c::BeginCurveCall(&x->gate,hook,&t);entered=true;c::EndCurveCall(&x->gate,&t);});
        const auto start=GetTickCount64();while(!x->gate.pending_writers.load() && GetTickCount64()-start<2000)Sleep(1);
        const bool invalid=!entered.load() && !c::CurveItemInspectionMatches(&x->gate,&inspection);
        const bool ended=c::EndCurveItemInspection(&x->gate,&inspection);c::EndCurveCall(&x->gate,&actual);writer.join();
        CHECK(invalid && ended && entered.load() && !x->gate.lost.load() && !x->writes && !x->deletes);
    }return true;}
bool ItemInspectionRequiresActualOuterCall(){auto x=std::make_unique<Fixture>();c::CurveItemInspection inspection={};
    c::CurveCallToken fake={1,0,c::CurveHook::ItemFamilyLoad,true,true};CHECK(!c::BeginCurveItemInspection(&x->gate,&fake,&inspection));
    c::CurveCallToken actual={},nested={};c::BeginCurveCall(&x->gate,c::CurveHook::ItemFamilyLoad,&actual);
    CHECK(c::BeginCurveItemInspection(&x->gate,&actual,&inspection) && c::CurveItemInspectionMatches(&x->gate,&inspection));
    c::BeginCurveCall(&x->gate,c::CurveHook::ItemParser,&nested);CHECK(!c::CurveItemInspectionMatches(&x->gate,&inspection));c::EndCurveCall(&x->gate,&nested);
    CHECK(c::CurveItemInspectionMatches(&x->gate,&inspection) && c::EndCurveItemInspection(&x->gate,&inspection));c::EndCurveCall(&x->gate,&actual);
    CHECK(!c::BeginCurveItemInspection(&x->gate,&actual,&inspection));return true;}
bool ItemInspectionReopensAfterLateProducer(){auto x=std::make_unique<Fixture>();c::CurveCallToken actual={};c::CurveItemInspection inspection={};
    c::BeginCurveCall(&x->gate,c::CurveHook::ItemFamilyLoad,&actual);CHECK(c::BeginCurveItemInspection(&x->gate,&actual,&inspection));
    std::atomic<bool> entered{false};std::thread writer([&](){c::CurveCallToken t={};c::BeginCurveCall(&x->gate,c::CurveHook::ItemRowsXml,&t);entered=true;c::EndCurveCall(&x->gate,&t);});
    const auto start=GetTickCount64();while(!x->gate.pending_writers.load() && GetTickCount64()-start<2000)Sleep(1);
    const bool invalid=!c::CurveItemInspectionMatches(&x->gate,&inspection) && !entered.load();
    const bool ended=c::EndCurveItemInspection(&x->gate,&inspection);c::EndCurveCall(&x->gate,&actual);writer.join();
    CHECK(invalid && ended && entered.load() && !x->gate.lost.load());return true;}
unsigned item_entered=0,item_returned=0,item_native_calls=0;bool item_abi_ok=true;
void ItemEvent(void*,c::CurveHook h,const c::CurveCallToken* t,c::CurveItemEvent event,std::uintptr_t argument,std::uintptr_t caller,std::uintptr_t result) noexcept {
    item_abi_ok&=c::IsItemDefinitionHook(h) && t && t->active && caller;
    if(h>=c::CurveHook::ItemRegisterEnum && h<=c::CurveHook::ItemGenerateCannonEnum){
        item_abi_ok&=argument==0x1234 && t->kind==h;
        if(event==c::CurveItemEvent::Returned)item_abi_ok&=result==(h<=c::CurveHook::ItemRegisterCannonEnum?0x123456789abcdef0ULL:0);
    }
    if(event==c::CurveItemEvent::Entering)++item_entered;else if(event==c::CurveItemEvent::Returned)++item_returned;else item_abi_ok=false;
}
std::uintptr_t __fastcall FakeItemPointer(void* p){++item_native_calls;item_abi_ok&=p==reinterpret_cast<void*>(0x1234);return 0x123456789abcdef0ULL;}
unsigned char __fastcall FakeItemByte(void* p){++item_native_calls;item_abi_ok&=p==reinterpret_cast<void*>(0x1234);return 0xa5;}
std::uintptr_t __fastcall FakeItemNone(){++item_native_calls;return 0xfedcba9876543210ULL;}
void __fastcall FakeItemClear(){++item_native_calls;}
void __fastcall FakeItemGenerate(void* p){++item_native_calls;item_abi_ok&=p==reinterpret_cast<void*>(0x1234);}
float __fastcall FakeRandomDistribution(int a,float b,float d,unsigned char e){cache_abi_ok&=a==-7 && b==2.5f && d==9.0f && e==0xa5;return 6.25f;}
unsigned char __fastcall FakeLevelDistribution(void* a,void* b,void* d,unsigned e,void* f,void* h,unsigned char i,unsigned char j){
    cache_abi_ok&=a==reinterpret_cast<void*>(0x1000) && b==reinterpret_cast<void*>(0x2000) && d==reinterpret_cast<void*>(0x3000) &&
        e==0xfedcba98 && f==reinterpret_cast<void*>(0x5000) && h==reinterpret_cast<void*>(0x6000) && i==0xa5 && j==0x5a;return 0xc3;}
std::uintptr_t __fastcall FakeRepair(void* a,void* b,void* d){cache_abi_ok&=a==reinterpret_cast<void*>(0x1000) && b==reinterpret_cast<void*>(0x2000) && d==reinterpret_cast<void*>(0x3000);return 0x123456789abcdef0ULL;}
std::uintptr_t __fastcall FakeSalvage(void* a,void* b){cache_abi_ok&=a==reinterpret_cast<void*>(0x1000) && b==reinterpret_cast<void*>(0x2000);return 0xfedcba9876543210ULL;}
float __fastcall FakeRepairParts(int a){cache_abi_ok&=a==-23;return 1.5f;}
std::uintptr_t __fastcall FakeFighter(void* a,void* b,void* d){return FakeRepair(a,b,d);}
bool WholeCacheWrappersPreserveNativeAbi(){auto x=std::make_unique<Fixture>();CHECK(c::BindCurveHookBridge(&x->gate)==c::CurveGateResult::Succeeded);
    const std::uintptr_t originals[]={reinterpret_cast<std::uintptr_t>(&FakeRandomDistribution),reinterpret_cast<std::uintptr_t>(&FakeLevelDistribution),
        reinterpret_cast<std::uintptr_t>(&FakeRepair),reinterpret_cast<std::uintptr_t>(&FakeSalvage),reinterpret_cast<std::uintptr_t>(&FakeRepairParts),reinterpret_cast<std::uintptr_t>(&FakeFighter)};
    for(std::size_t i=0;i<6;++i)x->gate.originals[static_cast<std::size_t>(c::CurveHook::RandomDistribution)+i]=originals[i];
    CHECK(reinterpret_cast<float(__fastcall*)(int,float,float,unsigned char)>(c::CurveHookWrapper(c::CurveHook::RandomDistribution))(-7,2.5f,9.0f,0xa5)==6.25f);
    auto a=reinterpret_cast<void*>(0x1000),b=reinterpret_cast<void*>(0x2000),d=reinterpret_cast<void*>(0x3000);
    CHECK(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,void*,unsigned,void*,void*,unsigned char,unsigned char)>(c::CurveHookWrapper(c::CurveHook::LevelDistribution))
        (a,b,d,0xfedcba98,reinterpret_cast<void*>(0x5000),reinterpret_cast<void*>(0x6000),0xa5,0x5a)==0xc3);
    CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,void*)>(c::CurveHookWrapper(c::CurveHook::RepairValue))(a,b,d)==0x123456789abcdef0ULL);
    CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*)>(c::CurveHookWrapper(c::CurveHook::SalvageValue))(a,b)==0xfedcba9876543210ULL);
    CHECK(reinterpret_cast<float(__fastcall*)(int)>(c::CurveHookWrapper(c::CurveHook::RepairPartsMultiplier))(-23)==1.5f);
    CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,void*)>(c::CurveHookWrapper(c::CurveHook::FighterDamageXp))(a,b,d)==0x123456789abcdef0ULL);
    x->gate.callbacks.item_event=ItemEvent;auto input=reinterpret_cast<void*>(0x1234);
    for(auto h:{c::CurveHook::ItemFamilyLoad,c::CurveHook::ItemDestroy,
            c::CurveHook::ItemRegisterEnum,c::CurveHook::ItemRegisterSavedEnum,c::CurveHook::ItemRegisterCannonEnum}){x->gate.originals[static_cast<std::size_t>(h)]=reinterpret_cast<std::uintptr_t>(&FakeItemPointer);
        CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)(void*)>(c::CurveHookWrapper(h))(input)==0x123456789abcdef0ULL);}
    for(auto h:{c::CurveHook::ItemRowsXml,c::CurveHook::ItemCutTypes,c::CurveHook::ItemAffixes}){x->gate.originals[static_cast<std::size_t>(h)]=reinterpret_cast<std::uintptr_t>(&FakeItemByte);
        CHECK(reinterpret_cast<unsigned char(__fastcall*)(void*)>(c::CurveHookWrapper(h))(input)==0xa5);}
    for(auto h:{c::CurveHook::ItemFinalize,c::CurveHook::ItemConstruct}){x->gate.originals[static_cast<std::size_t>(h)]=reinterpret_cast<std::uintptr_t>(&FakeItemNone);
        CHECK(reinterpret_cast<std::uintptr_t(__fastcall*)()>(c::CurveHookWrapper(h))()==0xfedcba9876543210ULL);}
    x->gate.originals[static_cast<std::size_t>(c::CurveHook::ItemClear)]=reinterpret_cast<std::uintptr_t>(&FakeItemClear);
    reinterpret_cast<void(__fastcall*)()>(c::CurveHookWrapper(c::CurveHook::ItemClear))();
    for(auto h:{c::CurveHook::ItemGenerateEnum,c::CurveHook::ItemGenerateSavedEnum,c::CurveHook::ItemGenerateCannonEnum}){
        x->gate.originals[static_cast<std::size_t>(h)]=reinterpret_cast<std::uintptr_t>(&FakeItemGenerate);
        reinterpret_cast<void(__fastcall*)(void*)>(c::CurveHookWrapper(h))(input);}
    CHECK(item_abi_ok && item_native_calls==14 && item_entered==14 && item_returned==14);
    CHECK(cache_abi_ok && !x->gate.active_calls.load() && !x->gate.arrivals.load() && !x->gate.lost.load());return true;}
}
int main(){bool(*tests[])()={CompleteAppendCleanup,UnobservedTailRefuses,SharedTailRefuses,UnknownBorrowRefuses,BaselineMutationRefuses,
    EqualReplacementBuffersRefuse,FailedBindingStoreKeepsTail,FailedCountStoreKeepsTail,FailedDeleteRetainsDetachedAllocation,StaleContextCannotRetire,
    CancelReopensWithoutMutation,DuplicateNameAllocationNotRegistered,CoverageLossRefuses,NativeWholeCallDrains,WritesPromoteWithoutSourceReceipt,
    UnownedDestructorPreservesBaselineGeneration,PrivateDuplicateWaitsUntilWholeLoad,AdoptedAllocationNeverUsesPrivateDestructor,
    UnknownConstructorCannotGrantPrivateOwnership,RepeatedDuplicateLoadsReuseTrackingSlot,FinalReverifyDoesNotMutate,
    FinalReverifyRejectsLaterTail,FinalReverifyRejectsChangedBindings,FinalReverifyRejectsStaleTransaction,
    WriterBetweenCaptureAndSealInvalidatesSnapshot,QueuedWriterPreventsBaselinePublication,
    ProcessCacheAndWeightsRestore,InvalidCachedCurveIdentityRefuses,FailedCacheStoreKeepsTail,FinalReverifyRejectsChangedCaches,QueuedReaderPreventsMutation,
    InitializedCacheReadDoesNotPromote,UnknownReturnedIdRetainsItsTail,BaselineReturnedIdDoesNotDebtTail,
    ReaderAfterPublicationRetainsTail,ReaderDuringNativeDeletionCannotResume,ReaderBetweenFinalCheckAndPublicationCas,
    SnapshotLeaseCannotRetire,SnapshotLeasePinsFrame,SnapshotRejectsQueuedItemProducer,
    SnapshotRejectsEveryEnumProducer,EnumGenerationInvalidatesOwnerInspection,
    TrailingBranchPreservesDestination,TrailingBranchRejectsUnqualifiedLayout,
    ItemInspectionRequiresActualOuterCall,ItemInspectionReopensAfterLateProducer,WholeCacheWrappersPreserveNativeAbi};
    for(auto test:tests)if(!test())return 1;std::printf("Curve append owner: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
