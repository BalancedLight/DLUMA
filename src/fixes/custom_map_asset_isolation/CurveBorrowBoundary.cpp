#define _HAS_EXCEPTIONS 0
#include "CurveBorrowBoundary.h"

namespace dluma::fixes::custom_map_asset_isolation::curves {
namespace {
struct ScopeStack {
    CurveBorrowBoundary* tracker;
    std::uint64_t serial[16];
    std::uint32_t depth,thread_id;
};
__declspec(thread) ScopeStack scopes={};
void Lock(CurveBorrowBoundary* t) noexcept {while(t->lock.test_and_set(std::memory_order_acquire)) {}}
void Unlock(CurveBorrowBoundary* t) noexcept {t->lock.clear(std::memory_order_release);}
bool ValidAuthority(const DlumaGameFrameAuthorityState* a) noexcept {
    if(!a || !a->exact_route_identity_seeded || !a->game_frame_authority_frozen ||
        a->game_frame_authority_conflict || a->game_frame_authority_timed_out ||
        a->thread_diagnostic_overflow || !a->exact_route_serialization_token ||
        !a->exact_route_game_subobject || !a->exact_route_game_primary ||
        a->trace_count>DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY ||
        a->trace_write_index>=DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY)return false;
    const auto required=static_cast<unsigned>(a->game_frame_playtest_handoff_latched ?
        DLUMA_GAME_FRAME_AUTHORITY_PLAYTEST_REQUIRED_PROOF:DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF);
    return (static_cast<unsigned>(a->game_frame_authority_proof)&required)==required;
}
bool ExactTrace(const DlumaGameFrameAuthorityState& a,const DlumaGameFrameCallbackTrace& t) noexcept {
    return t.sequence && t.thread_id>0 && t.callback_entry_qpc &&
        t.concurrent_depth==1 && t.recursive_depth==1 &&
        t.serialization_token==a.exact_route_serialization_token &&
        t.game_subobject==a.exact_route_game_subobject && t.game_primary==a.exact_route_game_primary &&
        t.caller_module_base==a.exact_route_caller_module_base && t.caller_rva==a.exact_route_caller_rva &&
        (t.route_proof&DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF)==DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF &&
        (t.authority_result==DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH ||
         t.authority_result==DLUMA_GAME_FRAME_AUTHORITY_RESULT_FRONTEND_LATCHED ||
         t.authority_result==DLUMA_GAME_FRAME_AUTHORITY_RESULT_PLAYTEST_HANDOFF_LATCHED);
}
const DlumaGameFrameCallbackTrace* TraceAt(const DlumaGameFrameAuthorityState& a,unsigned i) noexcept {
    const auto pos=(a.trace_write_index+DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY-1U-i)%DLUMA_GAME_FRAME_AUTHORITY_TRACE_CAPACITY;
    return &a.trace[pos];
}
}  // namespace
CurveBorrowResult CaptureActiveCurveFrame(const DlumaGameFrameAuthorityState* a,std::uint32_t tid,
    std::uint64_t qpc,CurveFrameStamp* out) noexcept {
    if(!out || !tid || !qpc)return CurveBorrowResult::InvalidArgument;*out={};
    if(!ValidAuthority(a))return CurveBorrowResult::AuthorityConflict;
    if(a->current_dispatch_thread_id!=static_cast<LONG>(tid))return CurveBorrowResult::NoActiveFrame;
    for(unsigned i=0;i<a->trace_count;++i){const auto& t=*TraceAt(*a,i);
        if(t.sequence!=a->last_route_sequence || t.thread_id!=static_cast<LONG>(tid))continue;
        if(!ExactTrace(*a,t) || t.callback_return_qpc || qpc<t.callback_entry_qpc)return CurveBorrowResult::NoActiveFrame;
        *out={t.sequence,t.serialization_token,qpc,t.game_subobject,t.game_primary,tid};return CurveBorrowResult::Succeeded;}
    return CurveBorrowResult::NoActiveFrame;
}
CurveBorrowResult NoteTransientCurveBorrow(CurveBorrowBoundary* t,const DlumaGameFrameAuthorityState* a,
    std::uint32_t tid,std::uint64_t qpc) noexcept {
    if(!t)return CurveBorrowResult::InvalidArgument;
    Lock(t);
    if(scopes.tracker==t && scopes.depth && scopes.thread_id==tid){Unlock(t);return CurveBorrowResult::Succeeded;}
    Unlock(t);CurveFrameStamp stamp={};const auto result=CaptureActiveCurveFrame(a,tid,qpc,&stamp);
    Lock(t);
    if(result!=CurveBorrowResult::Succeeded){t->foreign_borrow=true;Unlock(t);return CurveBorrowResult::ForeignBorrow;}
    if(t->last_frame_borrow.sequence && (t->last_frame_borrow.serialization_token!=stamp.serialization_token ||
        t->last_frame_borrow.game_subobject!=stamp.game_subobject || t->last_frame_borrow.game_primary!=stamp.game_primary ||
        stamp.sequence<t->last_frame_borrow.sequence ||
        (stamp.sequence==t->last_frame_borrow.sequence && stamp.thread_id!=t->last_frame_borrow.thread_id))){
        t->scope_lost=true;Unlock(t);return CurveBorrowResult::AuthorityConflict;}
    if(stamp.sequence>=t->last_frame_borrow.sequence)t->last_frame_borrow=stamp;
    Unlock(t);return CurveBorrowResult::Succeeded;
}
void NoteUnknownCurveBorrow(CurveBorrowBoundary* t) noexcept {if(t){Lock(t);t->unknown_borrow=true;Unlock(t);}}
CurveBorrowResult BeginTypedCurveNativeScope(CurveBorrowBoundary* t,const DlumaGameFrameAuthorityState* a,
    std::uint32_t tid,std::uint64_t qpc,std::uint64_t transaction,TypedCurveCaller caller,CurveNativeScope* token) noexcept {
    if(!t || !token || token->active || !transaction ||
        (caller!=TypedCurveCaller::PlayerPreservation && caller!=TypedCurveCaller::PlayerVariablesReset &&
         caller!=TypedCurveCaller::NativeSessionTeardown))return CurveBorrowResult::InvalidArgument;
    CurveFrameStamp stamp={};auto result=CaptureActiveCurveFrame(a,tid,qpc,&stamp);if(result!=CurveBorrowResult::Succeeded)return result;
    Lock(t);
    if(scopes.depth>=16 || (scopes.depth && (scopes.tracker!=t || scopes.thread_id!=tid)) || t->scope_lost){
        t->scope_lost=true;Unlock(t);return CurveBorrowResult::WrongScope;}
    const auto serial=++t->next_scope_serial;if(!serial){t->scope_lost=true;Unlock(t);return CurveBorrowResult::WrongScope;}
    scopes.tracker=t;scopes.thread_id=tid;scopes.serial[scopes.depth]=serial;
    *token={reinterpret_cast<std::uintptr_t>(t),serial,transaction,tid,scopes.depth,caller,true};
    ++scopes.depth;++t->active_scopes;Unlock(t);return CurveBorrowResult::Succeeded;
}
CurveBorrowResult EndTypedCurveNativeScope(CurveBorrowBoundary* t,std::uint32_t tid,CurveNativeScope* token) noexcept {
    if(!t || !token)return CurveBorrowResult::InvalidArgument;Lock(t);
    if(!token->active || token->tracker_identity!=reinterpret_cast<std::uintptr_t>(t) || token->thread_id!=tid ||
        scopes.tracker!=t || !scopes.depth || token->depth!=scopes.depth-1 || scopes.serial[token->depth]!=token->serial || !t->active_scopes){
        t->scope_lost=true;Unlock(t);return CurveBorrowResult::WrongScope;}
    --scopes.depth;--t->active_scopes;token->active=false;
    if(!scopes.depth){scopes.tracker=nullptr;scopes.thread_id=0;}Unlock(t);return CurveBorrowResult::Succeeded;
}
CurveBorrowResult VerifyCurveBorrowBoundary(CurveBorrowBoundary* t,const DlumaGameFrameAuthorityState* a) noexcept {
    if(!t)return CurveBorrowResult::InvalidArgument;
    if(!ValidAuthority(a))return CurveBorrowResult::AuthorityConflict;
    Lock(t);
    if(t->scope_lost){Unlock(t);return CurveBorrowResult::WrongScope;}
    if(t->unknown_borrow){Unlock(t);return CurveBorrowResult::UnknownBorrow;}
    if(t->foreign_borrow){Unlock(t);return CurveBorrowResult::ForeignBorrow;}
    if(t->active_scopes){Unlock(t);return CurveBorrowResult::ScopeActive;}
    const auto debt=t->last_frame_borrow;
    if(!debt.sequence){Unlock(t);return CurveBorrowResult::Succeeded;}
    if(debt.serialization_token!=a->exact_route_serialization_token || debt.game_subobject!=a->exact_route_game_subobject ||
        debt.game_primary!=a->exact_route_game_primary){Unlock(t);return CurveBorrowResult::AuthorityConflict;}
    for(unsigned i=0;i<a->trace_count;++i){const auto& f=*TraceAt(*a,i);
        if(f.sequence<debt.sequence)continue;
        if(!ExactTrace(*a,f) || !f.original_callback_invoked || !f.original_callback_returned ||
            !f.original_entry_qpc || f.original_entry_qpc<f.callback_entry_qpc || f.original_return_qpc<f.original_entry_qpc ||
            f.callback_return_qpc<f.original_return_qpc || f.callback_return_qpc<debt.observed_qpc ||
            (f.sequence==debt.sequence && f.thread_id!=static_cast<LONG>(debt.thread_id)))continue;
        t->last_frame_borrow={};Unlock(t);return CurveBorrowResult::Succeeded;
    }
    Unlock(t);return CurveBorrowResult::IncompleteFrame;
}
bool IsTypedCurveNativeScopeActive(CurveBorrowBoundary* t,std::uint32_t tid,const CurveNativeScope* token) noexcept {
    if(!t || !token)return false;Lock(t);
    const bool active=token->active && token->tracker_identity==reinterpret_cast<std::uintptr_t>(t) && token->thread_id==tid &&
        scopes.tracker==t && scopes.thread_id==tid && token->depth<scopes.depth && scopes.serial[token->depth]==token->serial && t->active_scopes;
    Unlock(t);return active;
}
}  // namespace dluma::fixes::custom_map_asset_isolation::curves
