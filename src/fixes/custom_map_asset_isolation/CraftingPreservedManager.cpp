#define _HAS_EXCEPTIONS 0
#include "CraftingPreservedManager.h"
#include "CurveBaselineSnapshot.h"
#include <cstring>
namespace dluma::fixes::custom_map_asset_isolation::crafting_owner {
namespace {
bool Read(Owner* o,std::uintptr_t p,void* out,std::size_t n) noexcept {return p && p<=UINTPTR_MAX-n && o->access.read(o->access.user,p,out,n);}
bool ContextValid(Owner* o,Context* c) noexcept {
    std::uintptr_t game=0,manager=0;
    return o->access.context(o->access.user,c) && c->game_di && c->game_di_generation && c->baseline_generation && c->thread_id &&
        c->writers_closed && Read(o,o->access.gamedll_base+0x1D07F88,&game,8) && game==c->game_di &&
        Read(o,game+0xAA0,&manager,8) && manager==c->current_manager &&
        (!o->baseline_captured || (o->game_di==game && o->game_di_generation==c->game_di_generation && o->baseline_generation==c->baseline_generation));
}
std::size_t Find(const Owner* o,std::uintptr_t p) noexcept {
    for(std::size_t i=0;i<o->count;++i)if(o->managers[i].address==p && !o->managers[i].retired)return i;return o->count;
}
bool Add(Owner* o,std::uintptr_t p,std::uint32_t epoch,bool baseline) noexcept {
    if(!p || o->count>=o->capacity || Find(o,p)!=o->count)return false;
    o->managers[o->count++]={p,++o->next_manager_generation,epoch,false,baseline,false};return true;
}
bool SnapshotRead(void* user,std::uintptr_t p,void* out,std::size_t n) noexcept {return Read(static_cast<Owner*>(user),p,out,n);}
bool SnapshotFence(void* user,blueprint_rebuild::Fence* out) noexcept {
    auto* o=static_cast<Owner*>(user);Context c={};if(!ContextValid(o,&c))return false;
    const auto target=o->baseline_captured?o->baseline_manager:c.current_manager;
    const auto index=Find(o,target);if(!target || index==o->count || o->managers[index].retired)return false;
    if(target!=c.current_manager && !o->managers[index].held)return false;
    *out={c.game_di,target,o->managers[index].generation,c.revision,c.baseline_generation,c.thread_id,true,c.initial_pristine};return true;
}
blueprint_rebuild::Access SnapshotAccess(Owner* o) noexcept {
    blueprint_rebuild::Access a={};a.gamedll_base=o->access.gamedll_base;std::memcpy(a.sha256,o->access.sha256,65);
    a.read=SnapshotRead;a.fence=SnapshotFence;a.user=o;return a;
}
Result Fail(Owner* o,Result result,bool lost=false) noexcept {o->result=result;o->lost|=lost;return result;}
bool SameOwner(const Context& a,const Context& b) noexcept {
    return a.game_di==b.game_di && a.game_di_generation==b.game_di_generation && a.baseline_generation==b.baseline_generation &&
        a.epoch==b.epoch && a.transaction==b.transaction && a.thread_id==b.thread_id;
}
}
bool Initialize(Owner* o,const Access& a,Manager* managers,std::size_t capacity,const crafting_snapshot::Snapshot& storage) noexcept {
    if(!o || o->initialized || !a.gamedll_base || a.gamedll_base>UINTPTR_MAX-0x2082000 ||
        std::memcmp(a.sha256,curves::kPlayerGameDllSha256,65) || !a.read || !a.context || !a.compare || !a.original_rebuild || !a.publish_baseline ||
        !managers || !capacity || capacity>kMaximumManagers || !storage.spans || !storage.span_capacity || !storage.bytes || !storage.capacity)return false;
    *o={};o->access=a;o->managers=managers;o->capacity=capacity;o->baseline=storage;o->baseline.complete=false;o->initialized=true;return true;
}
Result CaptureBaseline(Owner* o) noexcept {
    if(!o || !o->initialized || o->baseline_captured || o->lost)return Result::InvalidArgument;Context c={};
    if(!ContextValid(o,&c) || !c.initial_pristine || c.epoch || c.transaction || !c.current_manager)return Fail(o,Result::WrongScope);
    const auto record=Find(o,c.current_manager);
    if(record==o->count || o->managers[record].epoch || o->managers[record].held)return Fail(o,Result::UnknownManager);
    const auto a=SnapshotAccess(o);if(crafting_snapshot::Capture(a,&o->baseline)!=crafting_snapshot::Result::Succeeded)return Fail(o,Result::BaselineUnavailable);
    Context after={};if(!ContextValid(o,&after) || !SameOwner(c,after) || c.revision!=after.revision || after.current_manager!=c.current_manager){
        o->baseline.complete=false;return Fail(o,Result::OwnerChanged);}
    if(!o->access.publish_baseline(o->access.user,true)){o->baseline.complete=false;return Fail(o,Result::OwnerChanged);}
    o->game_di=c.game_di;o->game_di_generation=c.game_di_generation;o->baseline_generation=c.baseline_generation;
    o->baseline_manager=o->current_manager=c.current_manager;o->managers[record].baseline=true;o->baseline_captured=true;
    if(!o->access.publish_baseline(o->access.user,false))return Fail(o,Result::OwnerChanged,true);return Fail(o,Result::Succeeded);
}
Result RunNativeRebuild(Owner* o,std::uintptr_t actual_receiver) noexcept {
    if(!o || !o->initialized || !actual_receiver)return Result::InvalidArgument;Context before={};
    const bool qualified=ContextValid(o,&before) && before.game_di==actual_receiver;
    Result result=Result::Succeeded;std::size_t old=Find(o,before.current_manager);bool transferred=false;
    if(!qualified)result=Result::WrongScope;
    else if(o->lost)result=Result::BaselineUnavailable;
    else if(o->baseline_captured && !o->lost){
        old=Find(o,before.current_manager);
        if(before.current_manager && old==o->count)result=Result::UnknownManager;
        else if(o->count>=o->capacity)result=Result::CapacityExceeded;
        else if(crafting_snapshot::Verify(SnapshotAccess(o),o->baseline)!=crafting_snapshot::Result::Succeeded)result=Result::BaselineChanged;
        else if(before.current_manager){
            transferred=o->access.compare(o->access.user,before.game_di+0xAA0,before.current_manager,0);
            if(!transferred)result=Result::WriteFailed;else {o->managers[old].held=true;o->current_manager=0;}
        }
    }
    // A failed observer does not duplicate or skip the engine's natural call.
    // Root supplies the receiver from the actual hook, even if its joined
    // context could not qualify preservation for that invocation.
    if(!o->access.original_rebuild(o->access.user,actual_receiver))return Fail(o,Result::NativeFailed,true);
    Context after={};if(!ContextValid(o,&after) || (qualified && !SameOwner(before,after)))return Fail(o,Result::OwnerChanged,true);
    o->restored=false;
    if(result!=Result::Succeeded){
        // The original may have destroyed an old root that could not transfer.
        // Do not leave its record available to future baseline restoration.
        if(!transferred && old<o->count)o->managers[old].retired=true;
        return Fail(o,result,true);
    }
    if(!o->baseline_captured && old<o->count)o->managers[old].retired=true;
    if(!after.current_manager || !Add(o,after.current_manager,after.epoch,false))return Fail(o,Result::UnknownManager,true);
    o->current_manager=after.current_manager;
    if(o->baseline_captured){
        if(crafting_snapshot::Verify(SnapshotAccess(o),o->baseline)!=crafting_snapshot::Result::Succeeded)return Fail(o,Result::BaselineChanged,true);
    }
    return Fail(o,Result::Succeeded);
}
Result RestoreBaselineRoot(Owner* o,std::uint64_t transaction) noexcept {
    if(!o || !o->initialized || !transaction || !o->baseline_captured || o->lost)return Result::InvalidArgument;Context c={};
    if(!ContextValid(o,&c) || !c.post_c10_closed || c.transaction!=transaction || !c.epoch)return Fail(o,Result::WrongScope);
    const auto current=Find(o,c.current_manager),baseline=Find(o,o->baseline_manager);
    if(current==o->count || baseline==o->count)return Fail(o,Result::UnknownManager);
    if(crafting_snapshot::Verify(SnapshotAccess(o),o->baseline)!=crafting_snapshot::Result::Succeeded)return Fail(o,Result::BaselineChanged);
    if(c.current_manager!=o->baseline_manager){
        if(!o->managers[baseline].held || !o->access.compare(o->access.user,c.game_di+0xAA0,c.current_manager,o->baseline_manager))return Fail(o,Result::WriteFailed,true);
        o->managers[current].held=true;o->managers[baseline].held=false;
    }
    o->current_manager=o->baseline_manager;o->last_transaction=transaction;o->restored=true;return VerifyBaselineRoot(o,transaction);
}
Result VerifyBaselineRoot(Owner* o,std::uint64_t transaction) noexcept {
    if(!o || !o->initialized || !o->baseline_captured || o->lost || !o->restored || transaction!=o->last_transaction)return Result::InvalidArgument;
    Context c={};if(!ContextValid(o,&c) || !c.post_c10_closed || c.transaction!=transaction || c.current_manager!=o->baseline_manager)return Fail(o,Result::OwnerChanged);
    return Fail(o,crafting_snapshot::Verify(SnapshotAccess(o),o->baseline)==crafting_snapshot::Result::Succeeded?Result::Succeeded:Result::BaselineChanged);
}
bool HeldManager(const Owner* o,std::uintptr_t p,Manager* out) noexcept {
    if(!o || !o->initialized || !p || !out)return false;const auto index=Find(o,p);
    if(index==o->count || !o->managers[index].held)return false;*out=o->managers[index];return true;
}
} // namespace dluma::fixes::custom_map_asset_isolation::crafting_owner
