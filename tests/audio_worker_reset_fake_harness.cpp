#include "fixes/custom_map_asset_isolation/AudioWorkerReset.h"
#include "fixes/custom_map_asset_isolation/AudioWorkerResetWindows.h"
#include <cstdio>
#include <cstring>
namespace reset = dluma::fixes::custom_map_asset_isolation::audio_worker_reset;
namespace {
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); return false; } } while (0)
struct Fake {
    reset::Command command = {};
    reset::Snapshot state = {};
    bool lock_busy = false;
    bool locked = false;
    bool fence = true;
    bool evict_ok = true;
    bool damage_global = false;
    unsigned marks = 0, evictions = 0;
};
bool Try(void* user) noexcept { auto& f=*static_cast<Fake*>(user); if(f.lock_busy||f.locked)return false;f.locked=true;return true; }
void Leave(void* user) noexcept { static_cast<Fake*>(user)->locked=false; }
bool Fence(void* user,const reset::Plan*) noexcept { return static_cast<Fake*>(user)->fence; }
bool Snap(void* user,const reset::Plan*,reset::Snapshot* out) noexcept {auto& f=*static_cast<Fake*>(user);if(!f.locked)return false;*out=f.state;return true;}
bool Mark(void* user,std::uintptr_t) noexcept {auto& f=*static_cast<Fake*>(user);if(!f.locked)return false;++f.marks;return true;}
bool Evict(void* user,std::uintptr_t,const reset::KeptBank* keep,std::size_t count) noexcept {
 auto& f=*static_cast<Fake*>(user);if(!f.locked||count!=1||std::strcmp(keep[0].name,"global_bank")!=0)return false;
 ++f.evictions;if(f.damage_global)++f.state.preserved_reference_fingerprint;
 if(f.evict_ok)f.state.project_bank_count=0;return f.evict_ok;
}
reset::Plan Plan() {
 reset::Plan p={};p.audio_owner=0x1000;p.bank_manager=0x2000;p.epoch=7;p.transaction_generation=8;p.fence_identity=9;p.deadline_tick=100;
 p.preserved_reference_fingerprint=10;p.keep_count=1;p.complete_content_provenance=true;p.all_retaining_owners_classified=true;
 std::strcpy(p.keep[0].name,"global_bank");p.keep[0].record=0x3000;p.keep[0].native_file=0x4000;p.keep[0].instance_generation=1;p.keep[0].provider_identity=2;p.keep[0].content_sha256[0]=3;return p;
}
void Init(Fake& f) {
 reset::Operations ops={&f,&Try,&Leave,&Fence,&Snap,&Mark,&Evict,true};reset::Initialize(&f.command,&ops);
 f.state.audio_owner=0x1000;f.state.bank_manager=0x2000;f.state.fence_identity=9;f.state.preserved_reference_fingerprint=10;
 f.state.active_voices=1;f.state.project_bank_count=1;f.state.preserved_bank_identities_match=true;f.state.complete=true;
}
reset::WorkerFrame Frame(bool after=false,std::uint64_t generation=1) {return {0x1000,99,generation,1,20,true,after};}
bool FullDrainRequired() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));auto pre=Frame();reset::BeforeOriginalUpdate(&f.command,pre);CHECK(f.marks==1);
 auto post=Frame(true,2);f.state.active_voices=0;f.state.deferred_voices=1;
 reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==0);CHECK(!reset::ProvidersCanRetire(&f.command));
 f.state.deferred_voices=0;post.original_returned=false;reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==0);
 post.original_returned=true;post.original_completion_generation=1;reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==0);
 post.original_completion_generation=2;reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==1);CHECK(reset::ProvidersCanRetire(&f.command));
 reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==1);CHECK(reset::ResetCompletedCommand(&f.command));return true;
}
bool PendingNativeRequestRejected() {
 Fake f;Init(f);f.state.native_unload_request_pending=true;CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());
 CHECK(f.marks==0);CHECK(reset::Poll(&f.command,2)==reset::Phase::Failed);CHECK(!reset::ProvidersCanRetire(&f.command));return true;
}
bool TimeoutRetainsProviders() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());
 CHECK(reset::Poll(&f.command,100)==reset::Phase::Failed);CHECK(!reset::ProvidersCanRetire(&f.command));CHECK(!reset::ResetCompletedCommand(&f.command));
 f.state.active_voices=0;reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(f.evictions==0);return true;
}
bool CancelBeforeMutation() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::RequestCancellation(&f.command);reset::BeforeOriginalUpdate(&f.command,Frame());
 CHECK(f.marks==0);CHECK(reset::Poll(&f.command,2)==reset::Phase::Cancelled);CHECK(!reset::ProvidersCanRetire(&f.command));CHECK(reset::ResetCompletedCommand(&f.command));return true;
}
bool CancelAfterMutation() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());reset::RequestCancellation(&f.command);
 CHECK(reset::Poll(&f.command,2)==reset::Phase::Failed);CHECK(!reset::ProvidersCanRetire(&f.command));return true;
}
bool WorkerIdentityCannotMigrate() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());auto post=Frame(true,2);++post.current_thread_id;
 reset::AfterOriginalUpdate(&f.command,post);CHECK(f.evictions==0);CHECK(reset::Poll(&f.command,2)==reset::Phase::Failed);return true;
}
bool AsyncReadsMustDrain() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());f.state.active_voices=0;f.state.pending_bank_reads=1;
 reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(f.evictions==0);f.state.pending_bank_reads=0;reset::AfterOriginalUpdate(&f.command,Frame(true,3));CHECK(f.evictions==1);return true;
}
bool SourceInstanceBorrowersMustDrain() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());
 f.state.active_voices=0;f.state.unreleased_source_instances=1;
 reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(f.evictions==0);
 f.state.unreleased_source_instances=0;reset::AfterOriginalUpdate(&f.command,Frame(true,3));CHECK(f.evictions==1);return true;
}
bool ProtectedReferencesVerified() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());f.state.active_voices=0;f.damage_global=true;
 reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(f.evictions==1);CHECK(!reset::ProvidersCanRetire(&f.command));CHECK(reset::Poll(&f.command,2)==reset::Phase::Failed);return true;
}
bool EpochFenceChangesAbort() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());f.state.active_voices=0;f.fence=false;
 reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(f.evictions==0);CHECK(reset::Poll(&f.command,2)==reset::Phase::Failed);return true;
}
bool InvalidPlansDoNotPublish() {
 Fake f;Init(f);auto p=Plan();std::memset(p.keep[0].content_sha256,0,32);CHECK(!reset::Enqueue(&f.command,p));
 p=Plan();p.keep_count=2;p.keep[1]=p.keep[0];std::strcpy(p.keep[0].name,"aa");std::strcpy(p.keep[1].name,"b8");CHECK(!reset::Enqueue(&f.command,p));
 p=Plan();p.all_retaining_owners_classified=false;CHECK(!reset::Enqueue(&f.command,p));CHECK(f.marks==0);return true;
}
bool BusyLockRetriesWithoutMutation() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));f.lock_busy=true;reset::BeforeOriginalUpdate(&f.command,Frame());CHECK(f.marks==0);
 f.lock_busy=false;reset::BeforeOriginalUpdate(&f.command,Frame());CHECK(f.marks==1);return true;
}
bool NoFalseReleaseAcknowledgement() {
 Fake f;Init(f);CHECK(reset::Enqueue(&f.command,Plan()));CHECK(!reset::Enqueue(&f.command,Plan()));reset::BeforeOriginalUpdate(&f.command,Frame());
 f.state.active_voices=0;f.evict_ok=false;reset::AfterOriginalUpdate(&f.command,Frame(true,2));CHECK(!reset::ProvidersCanRetire(&f.command));return true;
}
bool ReadNativeFixture(void*,std::uintptr_t address,void* out,std::size_t size)noexcept {
 const std::uintptr_t base=0x10000000,owner=0x20000000;
 const unsigned char call[]={0x48,0x8b,0x0d,0x51,0x89,0x9b,0x00,0x48,0x8b,0x01,0xff,0x50,0x18};
 if(address==base+0x110420&&size==sizeof(call)){std::memcpy(out,call,size);return true;}
 std::uintptr_t value=address==base+0xAC8D78?owner:address==owner?base+0x8A9628:address==base+0x8A9640?base+0xF86A0:0;
 if(!value||size!=sizeof(value))return false;std::memcpy(out,&value,size);return true;
}
bool ExactWorkerRouteRequired() {
 reset::windows::Binding b={};b.engine_base=0x10000000;b.read_memory=ReadNativeFixture;b.validated=true;b.exact_player_engine_image=true;
 reset::WorkerFrame frame={};CHECK(reset::windows::MakeWorkerFrame(&b,0x20000000,0x1011042D,3,9,2,true,&frame));
 CHECK(frame.exact_native_route&&frame.original_returned&&frame.original_completion_generation==2);
 CHECK(!reset::windows::MakeWorkerFrame(&b,0x20000000,0x1011042E,3,9,2,true,&frame));
 CHECK(!frame.exact_native_route);CHECK(!reset::windows::MakeWorkerFrame(&b,0x20000001,0x1011042D,3,9,2,true,&frame));
 b.validated=false;CHECK(reset::windows::ObserveWorkerFrame(&b,0x20000000,0x1011042D,3,9,2,true,&frame));
 CHECK(!reset::windows::MakeWorkerFrame(&b,0x20000000,0x1011042D,3,9,2,true,&frame));
 CHECK(!reset::windows::MakeOperations(&b).native_targets_validated);
 b.exact_player_engine_image=false;CHECK(!reset::windows::ObserveWorkerFrame(&b,0x20000000,0x1011042D,3,9,2,true,&frame));return true;
}
bool NativeSourceGraphMeasuresBorrowers() {
 struct Fixture {
  unsigned char memory[4096]={};
  static bool Read(void* user,std::uintptr_t at,void* out,std::size_t size)noexcept{
   auto& f=*static_cast<Fixture*>(user);if(at<0x20000000||size>sizeof(f.memory)||at-0x20000000>sizeof(f.memory)-size)return false;
   std::memcpy(out,f.memory+at-0x20000000,size);return true;
  }
  void Q(std::size_t offset,std::uintptr_t value){std::memcpy(memory+offset,&value,8);}
  void D(std::size_t offset,std::uint32_t value){std::memcpy(memory+offset,&value,4);}
 } f;
 reset::Plan p=Plan();p.audio_owner=0x20000000;p.bank_manager=0x20000100;
 // The owner->manager member is outside the compact native graph buffer;
 // use a read callback that supplies that independently observed link.
 const auto read=[](void* user,std::uintptr_t at,void* out,std::size_t size)noexcept {
  if(at==0x20001E28&&size==8){const std::uintptr_t manager=0x20000100;std::memcpy(out,&manager,8);return true;}
  return Fixture::Read(user,at,out,size);
 };
 f.Q(0,0x108A9628);f.Q(0x100,0x20000248);f.D(0x108,1);
 f.Q(0x118,0x20000400);f.D(0x120,1);f.Q(0x148,0x20000900);f.D(0x150,1);f.D(0x154,1);f.D(0x1A8,3);
 std::uint32_t hash=0;for(const char* s="global_bank";*s;++s)hash=hash*41U+static_cast<unsigned char>(*s);
 f.D(0x200,hash);f.Q(0x218,0x30000000);f.D(0x240,1);f.D(0x260,1);
 f.D(0x3F0,17);f.Q(0x3F8,0x20000500);f.D(0x418,1);
 f.Q(0x518,0x40000000);f.D(0x520,17);f.D(0x524,hash);f.Q(0x528,0x30000000);
 f.Q(0x900,0x20000800);f.Q(0x800,0x20000500);f.D(0x820,2);f.memory[0x828]=1;
 p.keep[0].record=0x20000200;p.keep[0].native_file=0x30000000;
 reset::windows::Binding b={};b.engine_base=0x10000000;b.read_user=&f;b.read_memory=read;b.exact_player_engine_image=true;
 reset::windows::SourceReference sources[8]={};reset::windows::NativeInventory inventory={};inventory.sources=sources;inventory.source_capacity=8;
 CHECK(reset::windows::ReadNativeInventory(&b,p,&inventory));CHECK(inventory.complete&&inventory.source_count==1&&inventory.pending_bank_reads==4);
 CHECK(inventory.unreleased_source_instances==1&&inventory.kept_bank_addresses_match&&inventory.project_banks==0);
 CHECK(sources[0].source==0x20000500&&sources[0].property_holder==0x40000000);
 f.D(0x820,1);CHECK(reset::windows::ReadNativeInventory(&b,p,&inventory));CHECK(inventory.unreleased_source_instances==0);
 ++p.keep[0].record;CHECK(reset::windows::ReadNativeInventory(&b,p,&inventory));CHECK(!inventory.kept_bank_addresses_match);--p.keep[0].record;
 f.Q(0x528,0x30000001);CHECK(!reset::windows::ReadNativeInventory(&b,p,&inventory));CHECK(!inventory.complete);f.Q(0x528,0x30000000);
 f.Q(0x248,0x20000248);CHECK(!reset::windows::ReadNativeInventory(&b,p,&inventory));CHECK(!inventory.complete);return true;
}
} // namespace
int main() {
 struct Test {const char* name;bool(*run)();};
 const Test tests[]={{"full_drain_required",FullDrainRequired},{"pending_request_rejected",PendingNativeRequestRejected},{"timeout_retains_providers",TimeoutRetainsProviders},{"cancel_before_mutation",CancelBeforeMutation},{"cancel_after_mutation",CancelAfterMutation},{"worker_identity_fenced",WorkerIdentityCannotMigrate},{"async_reads_drain",AsyncReadsMustDrain},{"protected_references_verified",ProtectedReferencesVerified},{"epoch_fence_abort",EpochFenceChangesAbort},{"invalid_plan_rejected",InvalidPlansDoNotPublish},{"lock_busy_retries",BusyLockRetriesWithoutMutation},{"no_false_ack",NoFalseReleaseAcknowledgement},{"exact_worker_route",ExactWorkerRouteRequired}};
 for(const auto& test:tests){if(!test.run())return 1;std::printf("PASS %s\n",test.name);}
 if(!SourceInstanceBorrowersMustDrain()||!NativeSourceGraphMeasuresBorrowers())return 1;
 std::puts("PASS source_instance_borrowers");std::puts("PASS native_source_graph");return 0;
}
