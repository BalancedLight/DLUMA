#define _HAS_EXCEPTIONS 0
#include "CurveAppendOnlyOwner.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
bool Snapshot(CurveAppendOnlyOwner* o,ItemCurveBindingSnapshot* bindings) noexcept {
    NativeCurveAccess access={};return MakeCurveGatewayAccess(o->operations.gateway,&access) &&
        CaptureCurveSnapshot(&access,&o->current)==CurveResult::Succeeded &&
        CaptureItemCurveBindings(&access,&o->current,bindings)==CurveBindingResult::Succeeded &&
        CaptureCurveProcessCaches(&access,&o->current,&o->current_caches);
}
CurveOwnerResult Fail(CurveAppendOnlyOwner* o,CurveOwnerResult r) noexcept {return o->result=r;}
bool Held(CurveAppendOnlyOwner* o) noexcept {
    return o && CurveGatewayLeaseMatches(o->operations.gateway,&o->lease) &&
        o->transaction==o->lease.transaction;
}
bool Read(CurveAppendOnlyOwner* o,std::uintptr_t address,void* out,std::size_t n) noexcept {
    auto* g=o->operations.gateway;return g->callbacks.read(g->callbacks.user,address,out,n);
}
bool VerifyTailOwnership(CurveAppendOnlyOwner* o) noexcept {
    auto* g=o->operations.gateway;
    for(std::size_t i=o->baseline.count;i<o->current.count;++i){CurveAppendRecord r={};
        if(!FindCurveAppend(g,o->current.records[i].native_identity,&r) || r.index!=i || r.detached || r.retired ||
            !r.origin.valid || r.origin.ownership!=CurveSourceOwnership::ExclusiveProject ||
            r.origin.project_epoch!=o->lease.epoch || !r.origin.source.address || !r.origin.source.generation ||
            !r.origin.source_revision || !r.origin.file_generation || !r.origin.open_root || r.unknown_borrow)return false;
    }
    return true;
}
} // namespace
bool InitializeCurveOwner(CurveAppendOnlyOwner* o,const CurveOwnerOperations* ops,
    const CurveSnapshot* baseline,const CurveSnapshot* current) noexcept {
    if(!o || !ops || !ops->gateway || !ops->compare_write || !ops->destroy || !baseline || !current ||
        !baseline->records || !baseline->storage || !current->records || !current->storage ||
        baseline->records==current->records || baseline->storage==current->storage)return false;
    *o={};o->operations=*ops;o->baseline=*baseline;o->current=*current;
    o->baseline.complete=false;o->current.complete=false;return true;
}
CurveOwnerResult CaptureCurveOwnerBaseline(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !o->operations.gateway || o->baseline_captured)return CurveOwnerResult::InvalidArgument;
    auto* g=o->operations.gateway;CurveGateLease lease={};
    if(CloseCurveGateway(g,50,&lease)!=CurveGateResult::Succeeded)return Fail(o,CurveOwnerResult::GateRejected);
    NativeCurveAccess access={};
    const bool ok=MakeCurveGatewayAccess(g,&access) &&
        CaptureCurveSnapshot(&access,&o->baseline)==CurveResult::Succeeded &&
        CaptureItemCurveBindings(&access,&o->baseline,&o->baseline_bindings)==CurveBindingResult::Succeeded &&
        CaptureCurveProcessCaches(&access,&o->baseline,&o->baseline_caches) &&
        SealCurveBaseline(g,&lease,&o->baseline);
    const bool opened=OpenCurveGateway(g,&lease)==CurveGateResult::Succeeded;
    o->baseline_captured=ok && opened;return Fail(o,o->baseline_captured?CurveOwnerResult::Succeeded:CurveOwnerResult::SnapshotFailed);
}
CurveOwnerResult PrepareCurveOwner(CurveAppendOnlyOwner* o,std::uint64_t transaction) noexcept {
    if(!o || !o->baseline_captured || !transaction || o->prepared || o->mutation_started)return CurveOwnerResult::InvalidArgument;
    auto* g=o->operations.gateway;
    if(CloseCurveGateway(g,250,&o->lease)!=CurveGateResult::Succeeded)return Fail(o,CurveOwnerResult::GateRejected);
    o->transaction=transaction;
    if(o->lease.transaction!=transaction){OpenCurveGateway(g,&o->lease);o->lease={};return Fail(o,CurveOwnerResult::WrongTransaction);}
    ItemCurveBindingSnapshot bindings={};CurveOwnerResult failure=CurveOwnerResult::Succeeded;
    if(!Snapshot(o,&bindings))failure=CurveOwnerResult::SnapshotFailed;
    else {const auto prefix=ClassifyCurvePrefix(&o->baseline,&o->current);
        if(prefix.kind!=CurvePrefixKind::Identical && prefix.kind!=CurvePrefixKind::AppendedTail)failure=CurveOwnerResult::BaselineChanged;
        else if(!VerifyTailOwnership(o))failure=CurveOwnerResult::OwnershipUnknown;
        else if(PlanItemCurveBindingRestore(&o->baseline,&o->baseline_bindings,&o->current,&bindings,&o->bindings)!=CurveBindingResult::Succeeded)
            failure=CurveOwnerResult::BindingFailed;
        else if(!PlanCurveProcessCacheRestore(g->module_base,&o->baseline_caches,&o->current_caches,&o->cache_writes))failure=CurveOwnerResult::CacheFailed;
        else if(!Read(o,g->module_base+kVectorRva,&o->vector_identity,sizeof(o->vector_identity)))failure=CurveOwnerResult::SnapshotFailed;
    }
    if(failure!=CurveOwnerResult::Succeeded){OpenCurveGateway(g,&o->lease);o->lease={};return Fail(o,failure);}
    o->current_count=static_cast<std::uint32_t>(o->current.count);o->prepared=true;
    return Fail(o,CurveOwnerResult::Succeeded);
}
CurveOwnerResult CancelCurveOwner(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !o->prepared)return CurveOwnerResult::InvalidArgument;
    if(o->mutation_started)return Fail(o,CurveOwnerResult::AlreadyMutated);
    if(OpenCurveGateway(o->operations.gateway,&o->lease)!=CurveGateResult::Succeeded)return Fail(o,CurveOwnerResult::GateRejected);
    o->prepared=false;o->lease={};o->transaction=0;return Fail(o,CurveOwnerResult::Succeeded);
}
CurveOwnerResult ResetCurveOwner(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !o->prepared || !Held(o))return CurveOwnerResult::InvalidArgument;
    if(o->mutation_started)return Fail(o,CurveOwnerResult::AlreadyMutated);
    ItemCurveBindingSnapshot current_bindings={};
    if(!Snapshot(o,&current_bindings))return Fail(o,CurveOwnerResult::SnapshotFailed);
    const auto prefix=ClassifyCurvePrefix(&o->baseline,&o->current);
    if((prefix.kind!=CurvePrefixKind::Identical && prefix.kind!=CurvePrefixKind::AppendedTail) ||
        o->current.count!=o->current_count)return Fail(o,CurveOwnerResult::BaselineChanged);
    if(!VerifyTailOwnership(o))return Fail(o,CurveOwnerResult::OwnershipUnknown);
    if(PlanItemCurveBindingRestore(&o->baseline,&o->baseline_bindings,&o->current,&current_bindings,&o->bindings)!=CurveBindingResult::Succeeded)
        return Fail(o,CurveOwnerResult::BindingFailed);
    if(!PlanCurveProcessCacheRestore(o->operations.gateway->module_base,&o->baseline_caches,&o->current_caches,&o->cache_writes))
        return Fail(o,CurveOwnerResult::CacheFailed);
    std::uintptr_t vector=0;
    if(!Read(o,o->operations.gateway->module_base+kVectorRva,&vector,sizeof(vector)) || vector!=o->vector_identity)
        return Fail(o,CurveOwnerResult::BaselineChanged);
    // Prevalidate every address before the first write. A failed store never
    // advances to detachment or destruction; already restored B0 references are safe.
    for(std::size_t i=0;i<o->bindings.count;++i){std::uintptr_t value=0;const auto& w=o->bindings.writes[i];
        if(!Read(o,w.address,&value,sizeof(value)) || value!=w.expected)return Fail(o,CurveOwnerResult::BindingFailed);}
    if(!BeginCurveOwnerPublication(o->operations.gateway,&o->lease)){
        (void)CancelCurveOwner(o);return Fail(o,CurveOwnerResult::GateRejected);}
    o->mutation_started=true;
    for(std::size_t i=0;i<o->bindings.count;++i){const auto& w=o->bindings.writes[i];
        if(!o->operations.compare_write(o->operations.user,w.address,&w.expected,&w.desired,sizeof(w.expected)))
            return Fail(o,CurveOwnerResult::WriteFailed);}
    for(std::size_t i=0;i<o->cache_writes.count;++i){const auto& w=o->cache_writes.writes[i];
        if(!o->operations.compare_write(o->operations.user,w.address,&w.expected,&w.desired,w.size))return Fail(o,CurveOwnerResult::WriteFailed);}
    o->operations.gateway->revision.fetch_add(1);
    if(!Snapshot(o,&current_bindings) || VerifyItemCurveBindings(&o->baseline_bindings,&current_bindings)!=CurveBindingResult::Succeeded)
        return Fail(o,CurveOwnerResult::BindingFailed);
    if(!SameCurveProcessCaches(&o->baseline_caches,&o->current_caches))return Fail(o,CurveOwnerResult::CacheFailed);
    const auto desired=static_cast<std::uint32_t>(o->baseline.count);
    if(desired!=o->current_count){
        if(!o->operations.compare_write(o->operations.user,o->operations.gateway->module_base+kVectorRva+8,
            &o->current_count,&desired,sizeof(desired)))return Fail(o,CurveOwnerResult::WriteFailed);
        o->operations.gateway->revision.fetch_add(1);
        for(std::size_t i=o->baseline.count;i<o->current.count;++i){const auto object=o->current.records[i].native_identity;
            // Count already detached the entire tail. Record every object even
            // if a bookkeeping failure occurs, retaining its allocation.
            o->detached[o->detached_count++]=object;
            if(!MarkCurveAppendDetached(o->operations.gateway,object))return Fail(o,CurveOwnerResult::OwnershipUnknown);}
    }
    return VerifyCurveOwner(o);
}
CurveOwnerResult VerifyCurveOwner(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !o->mutation_started || !Held(o))return CurveOwnerResult::InvalidArgument;
    ItemCurveBindingSnapshot bindings={};
    o->verified=Snapshot(o,&bindings) && ClassifyCurvePrefix(&o->baseline,&o->current).kind==CurvePrefixKind::Identical &&
        VerifyItemCurveBindings(&o->baseline_bindings,&bindings)==CurveBindingResult::Succeeded &&
        SameCurveProcessCaches(&o->baseline_caches,&o->current_caches);
    return Fail(o,o->verified?CurveOwnerResult::Succeeded:CurveOwnerResult::BaselineChanged);
}
CurveOwnerResult RetireCurveOwnerTail(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !o->verified || !Held(o))return CurveOwnerResult::InvalidArgument;
    if(VerifyCurveOwner(o)!=CurveOwnerResult::Succeeded)return o->result;
    if(o->detached_count && !BeginCurveOwnerRetirement(o->operations.gateway,&o->lease))return Fail(o,CurveOwnerResult::GateRejected);
    for(;o->retired_count<o->detached_count;++o->retired_count){const auto object=o->detached[o->retired_count];
        if(!o->operations.destroy(o->operations.user,&o->lease,object))return Fail(o,CurveOwnerResult::DeleteFailed);
        if(!MarkCurveAppendRetired(o->operations.gateway,object))return Fail(o,CurveOwnerResult::OwnershipUnknown);
        if(!Held(o)){++o->retired_count;return Fail(o,CurveOwnerResult::GateRejected);}}
    return Fail(o,CurveOwnerResult::Succeeded);
}
CurveOwnerResult CompleteCurveOwner(CurveAppendOnlyOwner* o) noexcept {
    if(!o || !Held(o) || !o->verified || o->retired_count!=o->detached_count)return CurveOwnerResult::InvalidArgument;
    if(VerifyCurveOwner(o)!=CurveOwnerResult::Succeeded)return o->result;
    if(OpenCurveGateway(o->operations.gateway,&o->lease)!=CurveGateResult::Succeeded)return Fail(o,CurveOwnerResult::GateRejected);
    o->completed_transaction=o->transaction;
    o->lease={};o->prepared=false;o->mutation_started=false;o->verified=false;o->transaction=0;
    o->detached_count=0;o->retired_count=0;return Fail(o,CurveOwnerResult::Succeeded);
}
CurveOwnerResult ReverifyCompletedCurveOwner(CurveAppendOnlyOwner* o,std::uint64_t transaction,CurveGateLease* held) noexcept {
    if(!o || !o->baseline_captured || o->prepared || o->mutation_started || !transaction ||
        (held && held->generation))return CurveOwnerResult::InvalidArgument;
    if(transaction!=o->completed_transaction)return Fail(o,CurveOwnerResult::WrongTransaction);
    auto* g=o->operations.gateway;CurveGateLease lease={};
    if(CloseCurveGateway(g,250,&lease)!=CurveGateResult::Succeeded)return Fail(o,CurveOwnerResult::GateRejected);
    ItemCurveBindingSnapshot bindings={};CurveOwnerResult result=CurveOwnerResult::Succeeded;
    if(lease.transaction!=transaction)result=CurveOwnerResult::WrongTransaction;
    else if(!Snapshot(o,&bindings))result=CurveOwnerResult::SnapshotFailed;
    else if(ClassifyCurvePrefix(&o->baseline,&o->current).kind!=CurvePrefixKind::Identical)result=CurveOwnerResult::BaselineChanged;
    else if(VerifyItemCurveBindings(&o->baseline_bindings,&bindings)!=CurveBindingResult::Succeeded)result=CurveOwnerResult::BindingFailed;
    else if(!SameCurveProcessCaches(&o->baseline_caches,&o->current_caches))result=CurveOwnerResult::CacheFailed;
    if(result==CurveOwnerResult::Succeeded && held)*held=lease;
    else if(OpenCurveGateway(g,&lease)!=CurveGateResult::Succeeded)result=CurveOwnerResult::GateRejected;
    return Fail(o,result);
}
bool NativeCurveCompareWrite(void* user,std::uintptr_t address,const void* expected,const void* desired,std::size_t n) noexcept {
    auto* g=static_cast<CurveNativeGateway*>(user);CurveReadFence fence={};
    if(!g || !address || !expected || !desired || (n!=1 && n!=4 && n!=8) || !CaptureCurveGatewayFence(g,&fence) ||
        !fence.access_closed || !fence.readers_drained || !fence.authoritative_thread)return false;
    __try {if(std::memcmp(reinterpret_cast<const void*>(address),expected,n))return false;
        std::memcpy(reinterpret_cast<void*>(address),desired,n);return !std::memcmp(reinterpret_cast<const void*>(address),desired,n);}
    __except(1){g->lost.store(true);return false;}
}
bool NativeCurveDelete(void* user,const CurveGateLease* lease,std::uintptr_t object) noexcept {
    return DestroyDetachedCurve(static_cast<CurveNativeGateway*>(user),lease,object);
}
} // namespace dluma::fixes::custom_map_asset_isolation::curves
