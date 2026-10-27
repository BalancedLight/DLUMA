#include "fixes/custom_map_asset_isolation/CurveBorrowBoundary.h"
#include <cstdio>
#include <cstring>
namespace c=dluma::fixes::custom_map_asset_isolation::curves;
using c::CurveBorrowResult;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);return false;}}while(0)
namespace {
struct Fixture {
    c::CurveBorrowBoundary tracker;DlumaGameFrameAuthorityState a={};
    Fixture(){a.exact_route_identity_seeded=1;a.game_frame_authority_frozen=1;
        a.game_frame_authority_proof=DLUMA_GAME_FRAME_AUTHORITY_REQUIRED_PROOF;
        a.exact_route_serialization_token=55;a.exact_route_game_subobject=0x1000;a.exact_route_game_primary=0x2000;
        a.exact_route_caller_module_base=0x3000;a.exact_route_caller_rva=0x4000;frame(10,7,100);}
    void frame(std::uint64_t seq,unsigned tid,std::uint64_t qpc){
        auto& t=a.trace[a.trace_write_index];t={};t.sequence=seq;t.thread_id=static_cast<LONG>(tid);
        t.game_subobject=a.exact_route_game_subobject;t.game_primary=a.exact_route_game_primary;
        t.caller_module_base=a.exact_route_caller_module_base;t.caller_rva=a.exact_route_caller_rva;
        t.serialization_token=a.exact_route_serialization_token;t.callback_entry_qpc=qpc;
        t.concurrent_depth=1;t.recursive_depth=1;t.route_proof=DLUMA_GAME_FRAME_AUTHORITY_STATIC_PROOF;
        t.authority_result=DLUMA_GAME_FRAME_AUTHORITY_RESULT_ROUTE_DISPATCH;
        a.trace_write_index=(a.trace_write_index+1)%64;if(a.trace_count<64)++a.trace_count;
        a.current_dispatch_thread_id=static_cast<LONG>(tid);a.last_route_sequence=seq;a.game_frame_authority_sequence=seq;
    }
    DlumaGameFrameCallbackTrace& last(){return a.trace[(a.trace_write_index+63)%64];}
    void complete(){auto& t=last();t.original_entry_qpc=t.callback_entry_qpc+30;t.original_return_qpc=t.callback_entry_qpc+60;
        t.callback_return_qpc=t.callback_entry_qpc+80;t.original_callback_invoked=1;t.original_callback_returned=1;a.current_dispatch_thread_id=0;}
    CurveBorrowResult borrow(unsigned tid=7,std::uint64_t qpc=110){return c::NoteTransientCurveBorrow(&tracker,&a,tid,qpc);}
    CurveBorrowResult verify(){return c::VerifyCurveBorrowBoundary(&tracker,&a);}
};
bool PreOriginalIsNotCompletion(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);return true;}
bool OriginalReturnAloneIsNotEnough(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);f.complete();f.last().callback_return_qpc=0;CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);return true;}
bool MatchingCompletedFrameWorks(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);f.complete();CHECK(f.verify()==CurveBorrowResult::Succeeded);return true;}
bool MigrationRetainsSerializedIdentity(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);f.complete();f.frame(11,8,200);f.complete();CHECK(f.verify()==CurveBorrowResult::Succeeded);return true;}
bool PriorFrameCannotExpireCurrentDebt(){Fixture f;f.complete();f.frame(11,8,200);CHECK(f.borrow(8,210)==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);return true;}
bool OverlapAndWrongRouteRefuse(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);f.complete();f.last().concurrent_depth=2;CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);f.last().concurrent_depth=1;f.a.game_frame_authority_conflict=1;CHECK(f.verify()==CurveBorrowResult::AuthorityConflict);return true;}
bool ForeignReaderIsNotAssumedDrained(){Fixture f;CHECK(f.borrow(99)==CurveBorrowResult::ForeignBorrow);f.complete();CHECK(f.verify()==CurveBorrowResult::ForeignBorrow);return true;}
bool UnknownBorrowNeverExpiresAsTemporary(){Fixture f;c::NoteUnknownCurveBorrow(&f.tracker);f.complete();CHECK(f.verify()==CurveBorrowResult::UnknownBorrow);return true;}
bool TypedNativeCallMustActuallyReturn(){Fixture f;c::CurveNativeScope token={};CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,110,9,c::TypedCurveCaller::PlayerPreservation,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.borrow()==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::ScopeActive);
    CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&token)==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::Succeeded);return true;}
bool ScopeDoesNotEraseOlderFrameDebt(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);c::CurveNativeScope t={};CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,120,9,c::TypedCurveCaller::PlayerVariablesReset,&t)==CurveBorrowResult::Succeeded);
    CHECK(f.borrow(7,121)==CurveBorrowResult::Succeeded);CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&t)==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);return true;}
bool ReusedScopeTokenCannotReleaseNewCall(){Fixture f;c::CurveNativeScope first={},second={};CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,110,9,c::TypedCurveCaller::PlayerPreservation,&first)==CurveBorrowResult::Succeeded);auto stale=first;
    CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&first)==CurveBorrowResult::Succeeded);CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,120,10,c::TypedCurveCaller::PlayerPreservation,&second)==CurveBorrowResult::Succeeded);
    CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&stale)==CurveBorrowResult::WrongScope);CHECK(f.tracker.active_scopes==1);
    CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&second)==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::WrongScope);return true;}
bool UnorderedTimestampsRefuse(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);f.complete();f.last().original_return_qpc=f.last().original_entry_qpc-1;CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);return true;}
bool TeardownClosesOnlyItsOwnTransientCalls(){Fixture f;c::CurveNativeScope token={};
    CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,110,9,c::TypedCurveCaller::NativeSessionTeardown,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.borrow()==CurveBorrowResult::Succeeded);CHECK(f.verify()==CurveBorrowResult::ScopeActive);
    CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.verify()==CurveBorrowResult::Succeeded && !f.last().callback_return_qpc);return true;}
bool TeardownDoesNotEraseOuterFrameDebt(){Fixture f;CHECK(f.borrow()==CurveBorrowResult::Succeeded);c::CurveNativeScope token={};
    CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,120,9,c::TypedCurveCaller::NativeSessionTeardown,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.borrow(7,130)==CurveBorrowResult::Succeeded);CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.verify()==CurveBorrowResult::IncompleteFrame);f.complete();CHECK(f.verify()==CurveBorrowResult::Succeeded);return true;}
bool TeardownDoesNotCloseForeignBorrow(){Fixture f;c::CurveNativeScope token={};
    CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,110,9,c::TypedCurveCaller::NativeSessionTeardown,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.borrow(99,120)==CurveBorrowResult::ForeignBorrow);CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.verify()==CurveBorrowResult::ForeignBorrow);return true;}
bool TeardownDoesNotCloseUnknownEscape(){Fixture f;c::CurveNativeScope token={};
    CHECK(c::BeginTypedCurveNativeScope(&f.tracker,&f.a,7,110,9,c::TypedCurveCaller::NativeSessionTeardown,&token)==CurveBorrowResult::Succeeded);
    c::NoteUnknownCurveBorrow(&f.tracker);CHECK(c::EndTypedCurveNativeScope(&f.tracker,7,&token)==CurveBorrowResult::Succeeded);
    CHECK(f.verify()==CurveBorrowResult::UnknownBorrow);return true;}
}
int main(){bool(*tests[])()={PreOriginalIsNotCompletion,OriginalReturnAloneIsNotEnough,MatchingCompletedFrameWorks,MigrationRetainsSerializedIdentity,PriorFrameCannotExpireCurrentDebt,OverlapAndWrongRouteRefuse,ForeignReaderIsNotAssumedDrained,UnknownBorrowNeverExpiresAsTemporary,TypedNativeCallMustActuallyReturn,ScopeDoesNotEraseOlderFrameDebt,ReusedScopeTokenCannotReleaseNewCall,UnorderedTimestampsRefuse,
    TeardownClosesOnlyItsOwnTransientCalls,TeardownDoesNotEraseOuterFrameDebt,TeardownDoesNotCloseForeignBorrow,TeardownDoesNotCloseUnknownEscape};
    for(auto test:tests)if(!test())return 1;std::printf("Curve borrow boundary: %zu scenarios passed\n",sizeof(tests)/sizeof(tests[0]));return 0;}
