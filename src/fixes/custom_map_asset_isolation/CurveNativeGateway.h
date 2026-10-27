#ifndef DLUMA_FIXES_CURVE_NATIVE_GATEWAY_H
#define DLUMA_FIXES_CURVE_NATIVE_GATEWAY_H
#include "CurveBaselineSnapshot.h"
#include "CurveBorrowBoundary.h"
#include "CurveLoadOrigin.h"
#include <atomic>

namespace dluma::fixes::custom_map_asset_isolation::curves {
enum class CurveHook : std::uint8_t {
    Load, Reload, GetId, FindId, GetIndex, GetName, CopyPointers, Count,
    Add, RebuildAll, RebuildSamples, DestroySpline, EvaluateSamples,
    EvaluateSpline, MaxX, MinX, LoadXmlPoints, LoadBinaryPoints,
    AddPoint, RemovePoint, ItemDefinitions, ItemParser, SaveManager,
    RebuildEveryCurve, NearestPoint, SetPoint, ConstructSpline,
    RandomDistribution, LevelDistribution, RepairValue, SalvageValue,
    RepairPartsMultiplier, FighterDamageXp,
    ItemFamilyLoad, ItemRowsXml, ItemCutTypes, ItemAffixes, ItemFinalize,
    ItemClear, ItemDestroy, ItemConstruct,
    ItemRegisterEnum, ItemRegisterSavedEnum, ItemRegisterCannonEnum,
    ItemGenerateEnum, ItemGenerateSavedEnum, ItemGenerateCannonEnum, CountHooks,
};
constexpr std::size_t kCurveHookCount=static_cast<std::size_t>(CurveHook::CountHooks);
struct CurveHookDescriptor {
    CurveHook kind;const char* name;std::uintptr_t function_rva,pointer_slot_rva;
    std::uint8_t prefix[16];std::uint8_t prefix_size,stolen_bytes;
    std::int8_t displacement_offset,instruction_end;
    std::uint8_t second_displacement_offset=0,second_instruction_end=0,trailing_branch_offset=0;
};
struct CurveGatewayContext {
    DlumaGameFrameAuthorityState authority;
    std::uint64_t transaction;
    std::uint32_t epoch;
    // Root derives these from its consumed post-C10 token, absent native
    // session/local actor, completed project callbacks and exact startup seam.
    bool post_c10_consumers_closed;
    bool startup_owner_absence;
};
using CurveContextFn=bool(*)(void*,CurveGatewayContext*) noexcept;
// Root reads the actual current project epoch and exact registry replay scope.
// Potential writes promote Curves even if their file origin is unknown. This
// callback confers no project ownership or retirement authority.
using CurvePotentialWriteFn=void(*)(void*,CurveHook) noexcept;
struct CurveCallToken;
enum class CurveItemEvent:std::uint8_t {Entering,Returned,Unwound};
using CurveItemEventFn=void(*)(void*,CurveHook,const CurveCallToken*,CurveItemEvent,
    std::uintptr_t argument,std::uintptr_t native_caller,std::uintptr_t native_result) noexcept;
struct CurveGatewayCallbacks {CurveReadFn read;CurveContextFn context;void* user;CurvePotentialWriteFn potential_write;
    CurveItemEventFn item_event=nullptr;};
enum class CurveGateResult : std::uint8_t {
    Succeeded, InvalidArgument, ProfileMismatch, IncompleteCoverage, Busy,
    WrongLease, LostObservation, BorrowersActive, WrongContext, TimedOut,
};
enum class CurveLeasePurpose:std::uint8_t {Owner,Snapshot};
struct CurveGateLease {std::uint64_t generation,transaction;std::uint32_t thread_id,epoch;
    CurveLeasePurpose purpose=CurveLeasePurpose::Owner;};
struct CurveCallToken {std::uint64_t serial;std::uint32_t depth;CurveHook kind;bool active,outer;};
struct CurveItemInspection {CurveCallToken call;std::uint32_t thread_id;bool active;};
struct CurveAppendRecord {
    std::uintptr_t object;std::uint32_t index;CurveSourceReceipt origin;
    bool detached,retired,unknown_borrow,world_borrow;
};
struct CurvePrivateAllocation {
    std::uintptr_t object;std::uint64_t load_serial;
    std::uint32_t thread_id;
    bool adopted,ignored,retired;
};
struct CurveNativeGateway {
    std::uintptr_t module_base=0;
    CurveGatewayCallbacks callbacks={};std::uintptr_t originals[kCurveHookCount]={};
    std::atomic<std::uint32_t> owner_thread{0},active_calls{0};
    std::atomic<std::uint32_t> pending_writers{0},pending_readers{0};
    // One word linearizes native arrival with owner publication. Its high bit
    // remains set from the first owner write through retirement and reopening.
    std::atomic<std::uint32_t> arrivals{0};
    std::atomic<bool> publication_active{false},publication_conflict{false},deletion_started{false},poisoned{false};
    std::atomic<std::uint64_t> revision{1},owner_generation{1},lease_generation{0};
    std::atomic<bool> held{false},lost{false},startup_sealed{false};
    std::atomic_flag records_lock=ATOMIC_FLAG_INIT;
    CurveAppendRecord appended[kMaximumCurves]={};std::size_t appended_count=0;
    CurvePrivateAllocation allocations[kMaximumCurves]={};std::size_t allocation_count=0;
    CurveBorrowBoundary borrowers;
    CurveGateLease lease={};CurveFrameStamp snapshot_frame={};std::uint32_t baseline_count=0;
    bool initialized=false,baseline_sealed=false;
};
const CurveHookDescriptor* CurveHookCatalog(std::size_t*) noexcept;
std::uintptr_t CurveHookWrapper(CurveHook) noexcept;
CurveGateResult InitializeCurveGateway(CurveNativeGateway*,std::uintptr_t,const char*,
    const CurveGatewayCallbacks*,const std::uintptr_t*,std::size_t) noexcept;
CurveGateResult BindCurveHookBridge(CurveNativeGateway*) noexcept;
CurveGateResult SealCurveHookStartup(CurveNativeGateway*) noexcept;
CurveGateResult VerifyCurveHookCoverage(CurveNativeGateway*) noexcept;
void BeginCurveCall(CurveNativeGateway*,CurveHook,CurveCallToken*) noexcept;
void EndCurveCall(CurveNativeGateway*,CurveCallToken*) noexcept;
CurveGateResult CloseCurveGateway(CurveNativeGateway*,std::uint32_t timeout_ms,CurveGateLease*) noexcept;
// Freezes admitted and pending calls for metadata only. This lease is valid on
// the exact current serialized frame and cannot publish or retire native data.
CurveGateResult CloseCurveGatewayForSnapshot(CurveNativeGateway*,std::uint32_t timeout_ms,CurveGateLease*) noexcept;
CurveGateResult OpenCurveGateway(CurveNativeGateway*,const CurveGateLease*) noexcept;
bool IsItemDefinitionHook(CurveHook) noexcept;
// A natural full Item load is already on the stack. Brief inspection can close
// other entrants before that original reads any manager field; no token can be
// fabricated by a metadata callback or a nested definition parser.
bool BeginCurveItemInspection(CurveNativeGateway*,const CurveCallToken*,CurveItemInspection*) noexcept;
bool CurveItemInspectionMatches(CurveNativeGateway*,const CurveItemInspection*) noexcept;
bool EndCurveItemInspection(CurveNativeGateway*,CurveItemInspection*) noexcept;
bool CaptureCurveGatewayFence(void*,CurveReadFence*) noexcept;
bool MakeCurveGatewayAccess(CurveNativeGateway*,NativeCurveAccess*) noexcept;
bool SealCurveBaseline(CurveNativeGateway*,const CurveGateLease*,const CurveSnapshot*) noexcept;
// Called only from Add's real post-original observation after count/slot checks.
void ObserveCurveAppend(CurveNativeGateway*,std::uintptr_t,std::uint32_t before_count,
    std::int32_t returned_index) noexcept;
void ObserveCurvePointer(CurveNativeGateway*,std::uintptr_t object,std::uintptr_t caller) noexcept;
// Integer IDs also retain a dependency on tail index identity. A transient
// Curve* consumer classification does not imply that its input ID is transient.
void ObserveCurveId(CurveNativeGateway*,std::int32_t id,std::uintptr_t caller) noexcept;
// Exact constructor-return and entire Load-return observers. Fresh ignored
// allocations never become manager tail records or generic borrowed objects.
void ObserveCurveConstruction(CurveNativeGateway*,std::uintptr_t object,std::uintptr_t caller) noexcept;
void ObserveCurveDestruction(CurveNativeGateway*,std::uintptr_t object) noexcept;
bool FinishCurvePrivateAllocations(CurveNativeGateway*,const CurveLoadToken*) noexcept;
bool CurveGatewayLeaseMatches(CurveNativeGateway*,const CurveGateLease*) noexcept;
bool BeginCurveOwnerPublication(CurveNativeGateway*,const CurveGateLease*) noexcept;
bool BeginCurveOwnerRetirement(CurveNativeGateway*,const CurveGateLease*) noexcept;
// Copies an observed actual native append receipt; tail position grants no ownership.
bool FindCurveAppend(CurveNativeGateway*,std::uintptr_t,CurveAppendRecord*) noexcept;
bool MarkCurveAppendDetached(CurveNativeGateway*,std::uintptr_t) noexcept;
bool MarkCurveAppendRetired(CurveNativeGateway*,std::uintptr_t) noexcept;
// Production calls the original deleting destructor under a closed gate and
// transaction. This cannot destroy manager-visible or baseline objects.
bool DestroyDetachedCurve(CurveNativeGateway*,const CurveGateLease*,std::uintptr_t) noexcept;
} // namespace dluma::fixes::custom_map_asset_isolation::curves
#endif
