#ifndef DLUMA_NATIVE_XUI_INSTANCE_OWNER_H
#define DLUMA_NATIVE_XUI_INSTANCE_OWNER_H
#include "NativeXuiContentOwner.h"
namespace dluma::fixes::custom_map_asset_isolation::xui_instance {
// Whole-HUD reload may retain native retired objects until later collection.
// Budget: 24k baseline + 20 document loads at 4.5k objects = 114k records.
// Never reclaim an active/retired record merely to remain within this bound.
constexpr std::size_t kInstances=131072;
// Fixed index: a callback fails closed on any bucket requiring a longer scan.
constexpr std::size_t kReceiverBuckets=32768,kReceiverScanLimit=64;
static_assert(kInstances<UINT32_MAX&&(kReceiverBuckets&(kReceiverBuckets-1))==0,"bounded 32-bit receiver indices");
enum class Life : std::uint8_t { Empty, Active, Retired, Destroyed, Provisional };
enum class Loss : std::uint8_t { None, Generic, Startup, WriterLimit, WriterIdentity,
    InvalidBirth, DuplicateBirth, Capacity, UnobservedRetirement, CollectorFence,
    CollectorCount, LoadOrigin, FactoryReturn, CallbackRead, CallbackFlags, MissingBirth };
struct FirstLoss {Loss reason=Loss::None;xui_content::Key manager={};std::uintptr_t object=0;
    std::uint64_t generation=0,revision=0,expected=0,actual=0;std::uint32_t writers=0;
    std::uint32_t thread_id=0,stack_count=0;std::uintptr_t stack[16]={};
    std::uint32_t capacity_life_counts[5]={},capacity_manager_life_counts[5]={},capacity_pending_destroyed=0;};
enum class Origin : std::uint8_t { ExternalUnknown, CleanStartupExternal, UiLoadPending, UiLoadGlobal, UiLoadProject, UiLoadUnknown };
struct Instance {
    xui_content::Key manager;
    std::uintptr_t object,interface_object,owner_context,vtable;
    std::uint64_t generation,load_serial;
    file_lifetime::SourceKey source;
    unsigned char sha256[32];std::uint32_t epoch;
    Life life;Origin origin;bool exact_input;bool birth_pending;
};
struct State {
    void* lock=nullptr;Instance records[kInstances]={};
    std::uintptr_t writer_addresses[128]={};
    std::uint32_t writer_threads[128]={};
    std::uint64_t concurrency_revision=0;
    std::uint64_t generation=0,revision=0;std::uint32_t active_writers=0;
    std::uint32_t receiver_heads[kReceiverBuckets]={};
    std::uint32_t receiver_next[kInstances]={},receiver_prev[kInstances]={};
    bool receiver_index_lost=false;
    bool sealed=false;std::atomic<bool> lost{false};
    FirstLoss first_loss={};
};
struct Writer {std::uintptr_t state;std::uint64_t revision;std::uint32_t slot;bool active;
    std::uint32_t thread_id;std::uint64_t concurrency_revision;bool concurrent_at_begin;};
bool SealStartup(State*)noexcept;
bool BeginWriter(State*,Writer*)noexcept;
bool EndWriter(State*,Writer*)noexcept;
// Native return observation supplies the actual factory manager generation,
// object/interface identities and the active XUI input scope. The latter is
// dependency provenance; it does not prove absence of external native clients.
bool Created(State*,const Instance&)noexcept;
bool Born(State*,const Instance&,std::uint64_t*)noexcept;
bool FinishBirth(State*,xui_content::Key,std::uintptr_t,std::uint64_t,bool,std::uintptr_t)noexcept;
bool CollectedNested(State*,xui_content::Key,std::uint32_t,bool,const Writer&)noexcept;
bool MarkLost(State*,Loss,xui_content::Key={},std::uintptr_t=0,std::uint64_t=0,std::uint64_t=0)noexcept;
// Entry flags1 are native no-ops only inside a matching observed outer
// transition. The nested generation is borrowed from that transient frame;
// all identities are checked against this same authoritative ledger.
bool RetirementEntry(State*,xui_content::Key,std::uintptr_t object,unsigned flags,
                     std::uint64_t nested_generation,std::uint64_t* generation,const Writer* bulk_writer=nullptr)noexcept;
bool Retired(State*,xui_content::Key,std::uintptr_t object,std::uint64_t expected_generation=0)noexcept;
bool AllRetired(State*,xui_content::Key,const Writer* bulk_writer=nullptr)noexcept;
// The collector receipt requires the original native collector to return with
// its dead list empty and no other concurrent observed writer.
bool Collected(State*,xui_content::Key,std::uint32_t expected_dead,bool native_dead_empty,std::uint64_t writer_revision)noexcept;
bool ManagerDestroyed(State*,xui_content::Key)noexcept;
bool FinishLoad(State*,std::uint64_t serial,Origin)noexcept;
bool CopyInventory(State*,xui_content::Key,Instance*,std::size_t capacity,std::size_t* count,std::uint64_t* revision)noexcept;
// Observation only: identity is compared, never dereferenced or retained by
// this call. Caller must already be inside a native callback whose receiver
// is valid. This result is not a pin and does not grant later dereference.
// A match establishes neither type nor source/ownership provenance.
// expected_generation==0 asks for the current generation; otherwise it must
// match exactly. Failure clears the output, including lost or over-budget index state.
bool TryMatchActiveReceiver(State*,xui_content::Key,std::uintptr_t interface_identity,
                            std::uint64_t expected_generation,std::uint64_t* generation)noexcept;
// Bounded snapshot of the same unique active receiver; valid only as an
// observation inside the caller-owned native receiver lifetime.
bool TryCopyActiveReceiver(State*,std::uintptr_t,std::uint64_t,Instance*)noexcept;
struct ReceiverDiagnostics {
    std::uint64_t generation,revision;
    std::uint32_t writers,matches;
    bool sealed,lost,index_lost;
    Instance first;
    FirstLoss first_loss;
};
// Diagnostic only; one bounded scan of the authoritative records. It neither
// dereferences the supplied identity nor grants a borrow or lifetime proof.
bool CopyReceiverDiagnostics(State*,std::uintptr_t,ReceiverDiagnostics*)noexcept;
// A load can instantiate an object in another native factory. Its consumed
// project/unknown input association must close there as well as at the loader.
bool ProjectInputInstancesClosed(State*)noexcept;
} // namespace
#endif
