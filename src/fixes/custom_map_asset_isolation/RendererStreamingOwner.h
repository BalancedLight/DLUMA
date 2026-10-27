#ifndef DLUMA_RENDERER_STREAMING_OWNER_H
#define DLUMA_RENDERER_STREAMING_OWNER_H
#include "PackedAssetWindows.h"
#include "NativeFileLifetimeLedger.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::renderer_streaming {
constexpr char kPlayerRendererSha256[]="49485c575fc04a0f11c8d430cf482d562dec0b8a846cbf18877fed891c835c7e";
constexpr std::size_t kMaximumDescriptors=retained::kMaximumResources;
enum class Route:std::uint8_t{Create,Delete,Request,FullRequest,Tick,Control,Finalize,Initialize,Count};
constexpr std::size_t kRoutes=static_cast<std::size_t>(Route::Count);
struct HookSpec{
 Route route;const char* name;std::uintptr_t rva;unsigned char prefix[16];
 std::uint8_t stolen,relative_offset,relative_end,second_relative_offset,second_relative_end;
};
enum class Life:std::uint8_t{Empty,Live,Deleted,Ended};
struct NativeDescriptor{
 std::uintptr_t object,job,next_request,pack,entry,texture,upload;
 std::uint8_t streaming,completed,wanted,deleted;
};
struct Record{
 NativeDescriptor native;packed::Key pack;
 file_lifetime::SourceKey sources[retained::kMaximumInputs];std::uint32_t source_count;
 std::uint64_t generation,verified_transaction;Life life;bool baseline,project;
};
struct Snapshot{
 NativeDescriptor* descriptors;std::size_t capacity,count;
 std::uintptr_t vector,request_head,io_head,completion_head,full_vector;
 std::uint32_t request_count,io_count,completion_count,full_count;
 std::uint64_t revision;bool complete,idle;
};
using ReadFn=packed::windows::ReadFn;
using FenceFn=bool(*)(void*,packed::Fence*)noexcept;
using PackKeyFn=bool(*)(void*,std::uintptr_t,packed::Key*)noexcept;
struct State{
 std::uintptr_t base=0,engine=0;ReadFn read=nullptr;FenceFn fence=nullptr;PackKeyFn pack_key=nullptr;void* user=nullptr;
 std::uintptr_t originals[kRoutes]={},installed[kRoutes]={};
 Record* records=nullptr;std::size_t capacity=0,count=0;std::uint64_t next_generation=0,baseline_generation=0;
 Snapshot scratch={};
 std::atomic<std::uint32_t> owner_thread{0},active{0},pending{0};std::atomic<std::uint64_t> revision{1};
 std::atomic<bool> lost{false};std::atomic_flag record_lock=ATOMIC_FLAG_INIT;
 packed::Fence authority={};bool validated=false,sealed=false,main_owned=false,io_owned=false,completion_owned=false;
 bool deleting_permitted=false,collecting_permitted=false;
};
const HookSpec* Catalog(std::size_t*)noexcept;
std::uintptr_t Wrapper(Route)noexcept;
bool Initialize(State*,std::uintptr_t renderer,std::uintptr_t engine,const char* actual_sha,
 ReadFn,FenceFn,PackKeyFn,void*,Record*,std::size_t,NativeDescriptor*)noexcept;
bool BindOriginal(State*,Route,std::uintptr_t)noexcept;
bool Publish(State*)noexcept;
bool Seal(State*)noexcept;
bool VerifyInstalled(State*)noexcept;
bool CopyEntryUnwind(State*,Route,unsigned char*,std::size_t,std::size_t*)noexcept;
// Closes actual creator/request/tick entry points, then tries the native main,
// IO and completion critical sections. Completion producers are not parked.
bool Enter(State*,std::uint32_t timeout_ms)noexcept;
bool CloseEntries(State*,std::uint32_t timeout_ms)noexcept;
bool EnterNative(State*)noexcept;
bool EntriesClosedAndIdle(State*)noexcept;
bool ReadGateRevision(State*,std::uint64_t*)noexcept;
void Leave(State*)noexcept;
void LeaveNative(State*)noexcept;
void OpenEntries(State*)noexcept;
bool Capture(State*,Snapshot*)noexcept;
bool CaptureBaseline(State*,const packed::Ledger&,std::uint64_t)noexcept;
bool SynchronizeOrigins(State*,const packed::Ledger&)noexcept;
// Called only with a fresh, qualified native pack/texture snapshot. No filename
// or provider is resolved here: source identities come from that exact pack key.
bool QualifyPack(State*,const packed::Ledger&,const packed::NativePack&,const retained::NativeState*,std::size_t)noexcept;
bool PermitNativeTextureDeletion(State*,bool entering)noexcept;
// Native tick runs only when request, full-load, IO and completion queues are
// empty. It then retires already deleted descriptors; no global flush is used.
bool CollectDeleted(State*)noexcept;
bool CanRetireSource(State*,file_lifetime::SourceKey,std::uint64_t)noexcept;
bool ReclaimSource(State*,file_lifetime::SourceKey,std::uint64_t)noexcept;
bool OwnsStreamRequest(State*,std::uintptr_t owner,std::uintptr_t operation,std::uintptr_t pack,
 std::uintptr_t entry,std::uintptr_t destination,std::uintptr_t callback,std::uintptr_t user)noexcept;
} // namespace
#endif
